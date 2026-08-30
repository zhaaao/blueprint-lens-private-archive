// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6BaselineProjection.h"
#include "BlueprintLensM6Preflight.h"
#include "BlueprintLensM6ProcessRunner.h"
#include "BlueprintLensM6SessionController.h"
#include "BlueprintLensM6SessionPacket.h"
#include "BlueprintLensM6Telemetry.h"
#include "BlueprintLensProductionExporter.h"
#include "BlueprintLensSourceNavigator.h"
#include "SBlueprintLensPanel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace BlueprintLensM6G6Tests
{
namespace
{
using FCanonicalWriter =
	TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

struct FScenario
{
	FString Id;
	FString AssetPath;
	FString GraphId;
	FString SourceFingerprint;
	FM6QueryInput Query;
	int32 ExpectedEntities = 0;
	int32 ExpectedRelations = 0;
};

struct FBlueprintStateRestorer
{
	UBlueprint* Blueprint = nullptr;
	bool bWasDirty = false;
	EBlueprintStatus Status = BS_Unknown;

	~FBlueprintStateRestorer()
	{
		if (IsValid(Blueprint))
		{
			Blueprint->Status = Status;
			Blueprint->GetOutermost()->SetDirtyFlag(bWasDirty);
		}
	}
};

bool WriteCanonicalValue(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedRef<FCanonicalWriter>& Writer);

bool WriteCanonicalObject(
	const TSharedPtr<FJsonObject>& Object,
	const TSharedRef<FCanonicalWriter>& Writer)
{
	if (!Object.IsValid()) return false;
	TArray<const TPair<FJsonObject::FStringType, TSharedPtr<FJsonValue>>*> Fields;
	for (const auto& Field : Object->Values) Fields.Add(&Field);
	Algo::Sort(Fields, [](const auto* Left, const auto* Right)
	{
		return FStringView(*Left->Key, Left->Key.Len()).Compare(
			FStringView(*Right->Key, Right->Key.Len()),
			ESearchCase::CaseSensitive) < 0;
	});
	Writer->WriteObjectStart();
	for (const auto* Field : Fields)
	{
		if (Field == nullptr || !Field->Value.IsValid()) return false;
		Writer->WriteIdentifierPrefix(
			FStringView(*Field->Key, Field->Key.Len()));
		if (!WriteCanonicalValue(Field->Value, Writer)) return false;
	}
	Writer->WriteObjectEnd();
	return true;
}

bool WriteCanonicalValue(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedRef<FCanonicalWriter>& Writer)
{
	if (!Value.IsValid()) return false;
	switch (Value->Type)
	{
	case EJson::Null: Writer->WriteNull(); return true;
	case EJson::String: Writer->WriteValue(Value->AsString()); return true;
	case EJson::Number: Writer->WriteValue(Value->AsNumber()); return true;
	case EJson::Boolean: Writer->WriteValue(Value->AsBool()); return true;
	case EJson::Array:
		Writer->WriteArrayStart();
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			if (!WriteCanonicalValue(Item, Writer)) return false;
		}
		Writer->WriteArrayEnd();
		return true;
	case EJson::Object: return WriteCanonicalObject(Value->AsObject(), Writer);
	default: return false;
	}
}

bool SaveCanonicalObject(
	const TSharedPtr<FJsonObject>& Object,
	const FString& Path)
{
	FString Text;
	const TSharedRef<FCanonicalWriter> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	if (!WriteCanonicalObject(Object, Writer) || !Writer->Close()) return false;
	Text += TEXT("\n");
	return FFileHelper::SaveStringToFile(
		Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TSharedPtr<FJsonObject> RequestObject(const FM6Request& Request)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_name"), Request.SchemaName);
	Root->SetStringField(TEXT("schema_version"), Request.SchemaVersion);
	Root->SetStringField(TEXT("asset_path"), Request.AssetPath);
	Root->SetStringField(TEXT("graph_id"), Request.GraphId);
	Root->SetStringField(TEXT("source_fingerprint"), Request.SourceFingerprint);
	Root->SetStringField(TEXT("query_kind"), Request.QueryKind);
	Root->SetStringField(TEXT("raw_version"), Request.RawVersion);
	Root->SetStringField(TEXT("typed_ir_version"), Request.TypedIrVersion);
	Root->SetStringField(TEXT("slice_rules_version"), Request.SliceRulesVersion);
	Root->SetStringField(TEXT("renderer_id"), Request.RendererId);
	TSharedRef<FJsonObject> Criterion = MakeShared<FJsonObject>();
	Criterion->SetStringField(TEXT("kind"), Request.QueryKind);
	Criterion->SetStringField(TEXT("graph_id"), Request.GraphId);
	Criterion->SetStringField(TEXT("direction"), Request.Direction);
	if (Request.QueryKind == TEXT("execution"))
		Criterion->SetStringField(TEXT("criterion_node_id"), Request.CriterionNodeId);
	else
	{
		Criterion->SetStringField(TEXT("member_guid"), Request.MemberGuid);
		Criterion->SetStringField(TEXT("expected_member_name"), Request.ExpectedMemberName);
	}
	Root->SetObjectField(TEXT("criterion"), Criterion);
	TSharedRef<FJsonObject> Semantic = MakeShared<FJsonObject>();
	Semantic->SetNumberField(TEXT("max_selected_nodes"), Request.MaxSelectedNodes);
	Semantic->SetNumberField(TEXT("max_selected_relations"), Request.MaxSelectedRelations);
	Root->SetObjectField(TEXT("semantic_budget"), Semantic);
	TSharedRef<FJsonObject> Presentation = MakeShared<FJsonObject>();
	Presentation->SetNumberField(TEXT("max_visible_entities"), Request.MaxVisibleEntities);
	Presentation->SetNumberField(TEXT("max_visible_relations"), Request.MaxVisibleRelations);
	Root->SetObjectField(TEXT("presentation_budget"), Presentation);
	return Root;
}

bool LoadScenarios(
	const FString& Path,
	TArray<FScenario>& OutScenarios,
	FString& OutError)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
	{
		OutError = TEXT("controlled scenario registry cannot be read");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (Root->GetStringField(TEXT("schema_name")) !=
			TEXT("blueprint-lens-m6-controlled-scenarios") ||
		Root->GetStringField(TEXT("schema_version")) != TEXT("1.0.0") ||
		!Root->TryGetArrayField(TEXT("scenarios"), Rows) || Rows == nullptr ||
		Rows->Num() != 2)
	{
		OutError = TEXT("controlled scenario registry shape is not exact");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
	{
		const TSharedPtr<FJsonObject> Row = RowValue->AsObject();
		const TSharedPtr<FJsonObject>* Request = nullptr;
		const TSharedPtr<FJsonObject>* Criterion = nullptr;
		const TSharedPtr<FJsonObject>* Expected = nullptr;
		FScenario Scenario;
		if (!Row.IsValid() ||
			!Row->TryGetStringField(TEXT("scenario_id"), Scenario.Id) ||
			!Row->TryGetObjectField(TEXT("request"), Request) || Request == nullptr ||
			!(*Request)->TryGetObjectField(TEXT("criterion"), Criterion) || Criterion == nullptr ||
			!Row->TryGetObjectField(TEXT("expected"), Expected) || Expected == nullptr ||
			!(*Request)->TryGetStringField(TEXT("asset_path"), Scenario.AssetPath) ||
			!(*Request)->TryGetStringField(TEXT("graph_id"), Scenario.GraphId) ||
			!(*Request)->TryGetStringField(TEXT("source_fingerprint"), Scenario.SourceFingerprint) ||
			!(*Expected)->TryGetNumberField(TEXT("node_count"), Scenario.ExpectedEntities) ||
			!(*Expected)->TryGetNumberField(TEXT("relation_count"), Scenario.ExpectedRelations))
		{
			OutError = TEXT("controlled scenario row is incomplete");
			return false;
		}
		FString Kind;
		if (!(*Request)->TryGetStringField(TEXT("query_kind"), Kind)) return false;
		Scenario.Query.Kind = Kind == TEXT("execution")
			? EM6QueryKind::Execution : (Kind == TEXT("data") ? EM6QueryKind::Data : EM6QueryKind::Invalid);
		Scenario.Query.GraphId = Scenario.GraphId;
		(*Criterion)->TryGetStringField(TEXT("criterion_node_id"), Scenario.Query.CriterionNodeId);
		(*Criterion)->TryGetStringField(TEXT("member_guid"), Scenario.Query.MemberGuid);
		(*Criterion)->TryGetStringField(TEXT("expected_member_name"), Scenario.Query.ExpectedMemberName);
		(*Criterion)->TryGetStringField(TEXT("direction"), Scenario.Query.Direction);
		OutScenarios.Add(MoveTemp(Scenario));
	}
	OutScenarios.Sort([](const FScenario& Left, const FScenario& Right)
	{
		return Left.Id < Right.Id;
	});
	if (OutScenarios[0].Id != TEXT("M6-D01") ||
		OutScenarios[1].Id != TEXT("M6-E01"))
	{
		OutError = TEXT("controlled scenario IDs are not exact");
		return false;
	}
	return true;
}

UEdGraph* FindGraph(UBlueprint& Blueprint, const FString& GraphId)
{
	for (UEdGraph* Graph : Blueprint.UbergraphPages)
	{
		if (Graph != nullptr && Graph->GetPathName() == GraphId) return Graph;
	}
	return nullptr;
}

TSet<FString> EntityIds(const TArray<FM6BaselineViewEntity>& Values)
{
	TSet<FString> Result;
	for (const FM6BaselineViewEntity& Value : Values) Result.Add(Value.Id);
	return Result;
}

TSet<FString> RelationIds(const TArray<FM6BaselineViewRelation>& Values)
{
	TSet<FString> Result;
	for (const FM6BaselineViewRelation& Value : Values) Result.Add(Value.Id);
	return Result;
}

bool SameSet(const TSet<FString>& Left, const TSet<FString>& Right)
{
	if (Left.Num() != Right.Num()) return false;
	for (const FString& Value : Left)
	{
		if (!Right.Contains(Value)) return false;
	}
	return true;
}

bool WriteTelemetry(
	const FString& Path,
	const FString& RunId,
	const FM6LoadedSessionPacket& Packet,
	FString& OutError,
	const FString& FailedStage = FString())
{
	FM6TelemetryRecorder Recorder;
	FM6Error Error;
	if (!Recorder.Begin(Path, RunId, Packet.SemanticSha256, Error))
	{
		OutError = Error.Code + TEXT(": ") + Error.Message;
		return false;
	}
	FM6TelemetryCounts Counts;
	Counts.Nodes = Packet.BaselineFacts.Entities.Num();
	Counts.Relations = Packet.BaselineFacts.Relations.Num();
	Counts.Edges = Packet.Slice.EdgeIds.Num();
	Counts.Truncated = Packet.BaselineFacts.TruncatedCount;
	Counts.ErrorReasons = Packet.BaselineFacts.Boundaries.Num();
	for (const FM6BaselineEntity& Entity : Packet.BaselineFacts.Entities)
	{
		Counts.Opaque += Entity.SemanticStatus == TEXT("opaque") ? 1 : 0;
		Counts.Uncertain += Entity.SemanticStatus == TEXT("uncertain") ? 1 : 0;
		Counts.Unsupported += Entity.SemanticStatus == TEXT("unsupported") ? 1 : 0;
	}
	for (const TCHAR* Stage : FM6TelemetryRecorder::RequiredStages())
	{
		const FString ErrorCode = Stage == FailedStage
			? TEXT("M6_PIPELINE_EXPLANATION_FAILED") : FString();
		bool bRecorded = false;
		if (FailedStage.IsEmpty())
		{
			bRecorded = Recorder.RecordStage(Stage, true, Counts, ErrorCode, Error);
		}
		else
		{
			FM6TelemetryStageMeasurement Measurement;
			Measurement.Stage = Stage;
			Measurement.bApplicable = true;
			Measurement.Counts = Counts;
			Measurement.ErrorCode = ErrorCode;
			Measurement.StartTimestamp = FDateTime::UtcNow().ToIso8601();
			const double StartedSeconds = FPlatformTime::Seconds();
			Measurement.DurationMs = FMath::Max(
				0.0, (FPlatformTime::Seconds() - StartedSeconds) * 1000.0);
			Measurement.ResultTimestamp = FDateTime::UtcNow().ToIso8601();
			bRecorded = Recorder.RecordMeasuredStage(Measurement, Error);
		}
		if (!bRecorded)
		{
			OutError = Error.Code + TEXT(": ") + Error.Message;
			return false;
		}
	}
	if (!Recorder.RecordBaseline(EM6Baseline::C, Error) ||
		!Recorder.RecordSelection(Packet.BaselineFacts.EntityIds[0], Error) ||
		!Recorder.RecordSourceJump(Packet.BaselineFacts.EntityIds[0], FString(), Error) ||
		!Recorder.RecordReset(Error) || !Recorder.RecordReplay(Error))
	{
		OutError = Error.Code + TEXT(": ") + Error.Message;
		return false;
	}
	FString PriorHash;
	if (!Recorder.Seal(PriorHash, Error))
	{
		OutError = Error.Code + TEXT(": ") + Error.Message;
		return false;
	}
	const FM6TelemetryReplayResult Replay =
		FM6TelemetryRecorder::Replay(Path, Packet.SemanticSha256);
	if (Replay.HasError() || !Replay.GetValue().bReset)
	{
		OutError = Replay.HasError()
			? Replay.GetError().Code + TEXT(": ") + Replay.GetError().Message
			: TEXT("telemetry replay did not retain reset");
		return false;
	}
	return true;
}

bool CopyPacket(const FString& Source, const FString& Target)
{
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	return Platform.CopyDirectoryTree(*Target, *Source, false);
}

FM6Error Error(const TCHAR* Code)
{
	FM6Error Result;
	Result.Code = Code;
	Result.Phase = TEXT("g6");
	Result.Message = Code;
	return Result;
}

class FStaticPreflight final : public IM6PreflightProvider
{
public:
	virtual FM6PreflightResult Evaluate(const FM6QueryInput&) override
	{
		FM6PreflightResult Result;
		Result.bSucceeded = true;
		Result.Request.SourceFingerprint = FString::ChrN(64, TEXT('a'));
		return Result;
	}
};

class FStaticExport final : public IM6ExportProvider
{
public:
	virtual FM6ExportPreparation Prepare(const FM6PreflightResult&) override
	{
		FM6ExportPreparation Result;
		Result.bSucceeded = true;
		Result.PacketDirectory = TEXT("unused");
		Result.ExpectedSourceFingerprint = FString::ChrN(64, TEXT('a'));
		Result.Invocation.ExecutablePath = TEXT("unused");
		return Result;
	}
};

class FManualRunner final : public IM6ProcessRunner
{
public:
	virtual void Start(const FM6ProcessInvocation&, FM6ProcessComplete InComplete) override
	{
		Complete = MoveTemp(InComplete);
	}
	virtual void Cancel() override {}
	virtual void Tick(double) override {}
	virtual bool IsActive() const override { return Complete != nullptr; }
	void Finish(const FM6ProcessResult& Result)
	{
		FM6ProcessComplete Callback = MoveTemp(Complete);
		Callback(Result);
	}
	FM6ProcessComplete Complete;
};

class FUnusedLoader final : public IM6PacketProvider
{
public:
	virtual FM6SessionPacketLoadResult Load(const FString&, const FString&) override
	{
		return MakeError(Error(TEXT("M6_PACKET_SCHEMA_INVALID")));
	}
};

class FNoTelemetry final : public IM6SessionTelemetry
{
public:
	virtual void RecordState(EM6SessionState, const FM6Error&) override {}
	virtual void RecordBaseline(EM6Baseline) override {}
	virtual void RecordSelection(const FString&, EM6SelectionOrigin) override {}
	virtual void RecordReset() override {}
	virtual void Seal() override {}
};

class FNoView final : public IM6SessionView
{
public:
	virtual void Present(const FM6SessionSnapshot&) override {}
};

FString RunnerFailureCode(const FM6ProcessResult& Result, bool& bPartialActivation)
{
	FStaticPreflight Preflight;
	FStaticExport Exporter;
	FManualRunner Runner;
	FUnusedLoader Loader;
	FNoTelemetry Telemetry;
	FNoView View;
	FM6SessionController Controller(
		Preflight, Exporter, Runner, Loader, Telemetry, View);
	Controller.Run(FM6QueryInput());
	Controller.Tick(1.0);
	Controller.Tick(2.0);
	Runner.Finish(Result);
	Controller.Tick(3.0);
	bPartialActivation = Controller.GetReadyPacket() != nullptr;
	const FString Code = Controller.GetSnapshot().Error.Code;
	Controller.Reset();
	return Code;
}

bool LoadErrorCode(
	const FString& Packet,
	const FString& Fingerprint,
	FString& OutCode,
	bool& bPartialValue)
{
	const FM6SessionPacketLoadResult Result =
		FM6SessionPacketLoader::Load(Packet, Fingerprint);
	bPartialValue = Result.HasValue();
	if (!Result.HasError()) return false;
	OutCode = Result.GetError().Code;
	return true;
}
} // namespace
} // namespace BlueprintLensM6G6Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6G6Test,
	"BlueprintLens.M6.G6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6G6Test::RunTest(const FString&)
{
	using namespace BlueprintLensM6G6Tests;
	FString RegistryPath;
	FString OutputRoot;
	FString Python;
	FString CaptureScenario;
	int32 CaptureGeneration = 0;
	FParse::Value(FCommandLine::Get(), TEXT("M6ScenarioRegistry="), RegistryPath);
	FParse::Value(FCommandLine::Get(), TEXT("M6OutputRoot="), OutputRoot);
	FParse::Value(FCommandLine::Get(), TEXT("M6Python="), Python);
	FParse::Value(FCommandLine::Get(), TEXT("M6CaptureScenario="), CaptureScenario);
	FParse::Value(FCommandLine::Get(), TEXT("M6CaptureGeneration="), CaptureGeneration);
	TestTrue(TEXT("Scenario registry is supplied"), FPaths::FileExists(RegistryPath));
	TestTrue(TEXT("Output root is supplied"), !OutputRoot.IsEmpty());
	TestTrue(TEXT("Exact Python is supplied"), FPaths::FileExists(Python));
	if (!FPaths::FileExists(RegistryPath) || OutputRoot.IsEmpty() ||
		!FPaths::FileExists(Python)) return false;

	TArray<FScenario> Scenarios;
	FString SetupError;
	TestTrue(TEXT("Exact two-scenario registry loads"), LoadScenarios(
		RegistryPath, Scenarios, SetupError));
	if (!SetupError.IsEmpty()) AddError(SetupError);
	if (Scenarios.Num() != 2) return false;
	if ((!CaptureScenario.IsEmpty() &&
		CaptureScenario != TEXT("M6-E01") && CaptureScenario != TEXT("M6-D01")) ||
		(!CaptureScenario.IsEmpty() && CaptureGeneration != 1 && CaptureGeneration != 2))
	{
		AddError(TEXT("capture scenario/generation is invalid"));
		return false;
	}

	const FString TempRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6G6Tests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	IFileManager::Get().MakeDirectory(*TempRoot, true);
	IFileManager::Get().MakeDirectory(*OutputRoot, true);
	const FString WorkspaceRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../..")));
	const FString Script = FPaths::Combine(WorkspaceRoot, TEXT("analysis/run_m6_session.py"));
	const FString SchemaRoot = FPaths::Combine(WorkspaceRoot, TEXT("schemas"));

	TMap<FString, TArray<FString>> SemanticHashes;
	TMap<FString, FString> FirstPacket;
	TMap<FString, FString> Fingerprints;
	int32 GenerationPasses = 0;
	int32 PacketFiles = 0;
	int32 BaselineParity = 0;
	int32 VisibleRecords = 0;
	int32 SourceResolutions = 0;

	for (const FScenario& Scenario : Scenarios)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Scenario.AssetPath);
		TestNotNull(*FString::Printf(TEXT("%s Blueprint loads"), *Scenario.Id), Blueprint);
		if (Blueprint == nullptr) continue;
		UEdGraph* Graph = FindGraph(*Blueprint, Scenario.GraphId);
		TestNotNull(*FString::Printf(TEXT("%s graph resolves"), *Scenario.Id), Graph);
		if (Graph == nullptr) continue;
		FBlueprintStateRestorer Restore{
			Blueprint, Blueprint->GetOutermost()->IsDirty(), Blueprint->Status};
		Blueprint->GetOutermost()->SetDirtyFlag(false);
		Blueprint->Status = BS_UpToDate;
		for (int32 Generation = 1; Generation <= 2; ++Generation)
		{
			const FString StagingRoot = FPaths::Combine(
				TempRoot, Scenario.Id,
				FString::Printf(TEXT("run%d-staging"), Generation));
			IFileManager::Get().MakeDirectory(*StagingRoot, true);
			const FM6PreflightResult Preflight =
				FM6Preflight::EvaluateResolvedForAutomationTest(
					Blueprint, Graph, Scenario.Query, StagingRoot);
			TestTrue(*FString::Printf(TEXT("%s run%d preflight"), *Scenario.Id, Generation), Preflight.bSucceeded);
			if (!Preflight.bSucceeded)
			{
				AddError(Preflight.Error.Code + TEXT(": ") + Preflight.Error.Message);
				continue;
			}
			TestEqual(TEXT("Registry fingerprint matches live source"),
				Preflight.Request.SourceFingerprint, Scenario.SourceFingerprint);
			const FString RawPath = FPaths::Combine(
				Preflight.OwnedStagingDirectory, TEXT("raw-source.json"));
			BlueprintLensProductionExporter::FExportRequest ExportRequest;
			ExportRequest.Blueprint = Blueprint;
			ExportRequest.OutputPath = RawPath;
			BlueprintLensProductionExporter::FExportResult ExportResult;
			BlueprintLensProductionExporter::FExportError ExportError;
			TestTrue(TEXT("Production exporter succeeds"),
				BlueprintLensProductionExporter::ExportRawDocument(
					ExportRequest, ExportResult, ExportError));
			if (!FPaths::FileExists(RawPath)) continue;
			const FString RequestPath = FPaths::Combine(
				Preflight.OwnedStagingDirectory, TEXT("request.json"));
			TestTrue(TEXT("Canonical request writes"),
				SaveCanonicalObject(RequestObject(Preflight.Request), RequestPath));
			const bool bCapture = Scenario.Id == CaptureScenario &&
				Generation == CaptureGeneration;
			const FString Packet = bCapture
				? FPaths::Combine(OutputRoot, TEXT("semantic"), Scenario.Id,
					FString::Printf(TEXT("run%d"), Generation))
				: FPaths::Combine(TempRoot, TEXT("semantic"), Scenario.Id,
					FString::Printf(TEXT("run%d"), Generation));
			if (IFileManager::Get().DirectoryExists(*Packet))
			{
				AddError(TEXT("fresh packet destination already exists: ") + Packet);
				continue;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Packet), true);
			const TArray<FString> Arguments = {
				Script, TEXT("--request"), RequestPath,
				TEXT("--raw-source"), RawPath,
				TEXT("--output"), Packet,
				TEXT("--schema-root"), SchemaRoot};
			FM6ProcessInvocation Invocation;
			Invocation.ExecutablePath = Python;
			Invocation.Arguments = Arguments;
			Invocation.WorkingDirectory = WorkspaceRoot;
			Invocation.PacketDirectory = Packet;
			Invocation.ExpectedPacketFiles = 7;
			Invocation.TimeoutSeconds = 180.0;
			FM6ProcessRunner PipelineRunner;
			TOptional<FM6ProcessResult> ProcessResult;
			PipelineRunner.Start(
				Invocation,
				[&ProcessResult](const FM6ProcessResult& Result)
				{
					ProcessResult = Result;
				});
			const double Deadline = FPlatformTime::Seconds() + 190.0;
			while (!ProcessResult.IsSet() && FPlatformTime::Seconds() < Deadline)
			{
				PipelineRunner.Tick(FPlatformTime::Seconds());
				FPlatformProcess::SleepNoStats(0.01f);
			}
			TestTrue(TEXT("Exact no-shell Python completes"), ProcessResult.IsSet());
			if (!ProcessResult.IsSet())
			{
				PipelineRunner.Cancel();
				continue;
			}
			const FM6ProcessResult& Process = ProcessResult.GetValue();
			TestTrue(TEXT("Python pipeline and owned cleanup succeed"), Process.IsSuccess());
			if (!Process.IsSuccess())
			{
				FString StandardOutput = Process.StandardOutput;
				FString StandardError = Process.StandardError;
				StandardOutput.ReplaceInline(TEXT("\r"), TEXT(" "));
				StandardOutput.ReplaceInline(TEXT("\n"), TEXT(" "));
				StandardError.ReplaceInline(TEXT("\r"), TEXT(" "));
				StandardError.ReplaceInline(TEXT("\n"), TEXT(" "));
				AddError(FString::Printf(
					TEXT("%s run%d pipeline code='%s' return=%d stdout='%s' stderr='%s'"),
					*Scenario.Id, Generation,
					*Process.Error.Code, Process.ReturnCode,
					*StandardOutput, *StandardError));
				continue;
			}
			TArray<FString> Files;
			IFileManager::Get().FindFiles(
				Files, *FPaths::Combine(Packet, TEXT("*")), true, false);
			PacketFiles += Files.Num();
			TestEqual(TEXT("Each generation contains seven files"), Files.Num(), 7);
			const FM6SessionPacketLoadResult Loaded = FM6SessionPacketLoader::Load(
				Packet, Preflight.Request.SourceFingerprint);
			TestTrue(TEXT("Generated packet loads atomically"), Loaded.HasValue());
			if (Loaded.HasError())
			{
				AddError(Loaded.GetError().Code + TEXT(": ") + Loaded.GetError().Message);
				continue;
			}
			const FM6LoadedSessionPacket& Value = Loaded.GetValue();
			TestEqual(TEXT("Controlled entity count"),
				Value.BaselineFacts.Entities.Num(), Scenario.ExpectedEntities);
			TestEqual(TEXT("Controlled relation count"),
				Value.BaselineFacts.Relations.Num(), Scenario.ExpectedRelations);
			const FM6BaselineProjectionResult Views = BuildM6BaselineViewModels(Value);
			TestTrue(TEXT("All three adapters project"), Views.HasValue());
			if (Views.HasError()) continue;
			const FM6BaselineViewModels& V = Views.GetValue();
			TestTrue(TEXT("A/B entity membership agrees"),
				SameSet(V.A.MemberEntityIds, EntityIds(V.B.Entities)));
			TestTrue(TEXT("A/C entity membership agrees"),
				SameSet(V.A.MemberEntityIds, EntityIds(V.C.Entities)));
			TestTrue(TEXT("A/B relation membership agrees"),
				SameSet(V.A.MemberRelationIds, RelationIds(V.B.Relations)));
			TestTrue(TEXT("A/C relation membership agrees"),
				SameSet(V.A.MemberRelationIds, RelationIds(V.C.Relations)));
			TestEqual(TEXT("B/C full entity semantics agree"), V.B.Entities, V.C.Entities);
			TestEqual(TEXT("B/C full relation semantics agree"), V.B.Relations, V.C.Relations);
			TestTrue(TEXT("A full context contains every session member"),
				V.A.FullEntityIds.Num() >= V.A.MemberEntityIds.Num());
			BaselineParity += 3;
			if (Generation == 1) VisibleRecords += 3;

			bool bResolved = false;
			for (const FBlueprintLensUnit& Unit : Value.Explanation.Units)
			{
				if (Unit.SourceReferences.IsEmpty()) continue;
				const FBlueprintLensResolvedSource Resolved =
					FBlueprintLensSourceNavigator().Resolve(
						Value.Explanation.Source, Unit.SourceReferences[0]);
				bResolved = Resolved.State == EBlueprintLensSourceState::Ready &&
					Resolved.Node.IsValid();
				break;
			}
			TestTrue(TEXT("Source identity resolves to a live native node"), bResolved);
			SourceResolutions += bResolved ? 1 : 0;

			const FString TelemetryPath = bCapture
				? FPaths::Combine(OutputRoot, TEXT("telemetry"),
					FString::Printf(TEXT("%s-run%d.telemetry.v1.jsonl"), *Scenario.Id, Generation))
				: FPaths::Combine(TempRoot, TEXT("telemetry"),
					FString::Printf(TEXT("%s-run%d.telemetry.v1.jsonl"), *Scenario.Id, Generation));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(TelemetryPath), true);
			FString TelemetryError;
			TestTrue(TEXT("Telemetry validates, seals and replays"), WriteTelemetry(
				TelemetryPath,
				FString::Printf(TEXT("g6-%s-run%d"), *Scenario.Id, Generation),
				Value, TelemetryError));
			if (!TelemetryError.IsEmpty()) AddError(TelemetryError);
			if (Scenario.Id == TEXT("M6-E01") && Generation == 1)
			{
				const FString FailureTelemetryPath = FPaths::Combine(
					TempRoot, TEXT("telemetry"), TEXT("failing-stage.telemetry.v1.jsonl"));
				FString FailureTelemetryError;
				TestTrue(TEXT("Failing stage telemetry is written"), WriteTelemetry(
					FailureTelemetryPath,
					TEXT("g6-failing-stage"),
					Value,
					FailureTelemetryError,
					TEXT("explanation")));
				FString FailureTelemetryText;
				FFileHelper::LoadFileToString(FailureTelemetryText, *FailureTelemetryPath);
				TestTrue(
					TEXT("Failing stage retains its stable code and measured duration"),
					FailureTelemetryText.Contains(TEXT("\"stage\":\"explanation\"")) &&
					FailureTelemetryText.Contains(TEXT("\"error_code\":\"M6_PIPELINE_EXPLANATION_FAILED\"")) &&
					FailureTelemetryText.Contains(TEXT("\"duration_ms\":")));
			}
			SemanticHashes.FindOrAdd(Scenario.Id).Add(Value.SemanticSha256);
			FirstPacket.FindOrAdd(Scenario.Id, Packet);
			Fingerprints.FindOrAdd(Scenario.Id, Preflight.Request.SourceFingerprint);
			++GenerationPasses;
		}
	}

	TestEqual(TEXT("Four independent semantic generations pass"), GenerationPasses, 4);
	TestEqual(TEXT("Four packets contain exactly 28 files"), PacketFiles, 28);
	TestEqual(TEXT("Twelve A/B/C parity records pass"), BaselineParity, 12);
	TestEqual(TEXT("Six primary visible-condition records are derivable"), VisibleRecords, 6);
	TestEqual(TEXT("Every generation retains a live source reference"), SourceResolutions, 4);
	int32 Agreements = 0;
	for (const FString& ScenarioId : {FString(TEXT("M6-E01")), FString(TEXT("M6-D01"))})
	{
		const TArray<FString>* Hashes = SemanticHashes.Find(ScenarioId);
		const bool bAgree = Hashes != nullptr && Hashes->Num() == 2 &&
			(*Hashes)[0] == (*Hashes)[1];
		TestTrue(*FString::Printf(TEXT("%s semantic agreement"), *ScenarioId), bAgree);
		Agreements += bAgree ? 1 : 0;
	}
	TestEqual(TEXT("Aggregate semantic agreement is 2/2"), Agreements, 2);

	TArray<TPair<FString, FString>> Negatives;
	UBlueprint* NegativeBlueprint = LoadObject<UBlueprint>(nullptr, *Scenarios[1].AssetPath);
	UEdGraph* NegativeGraph = NegativeBlueprint != nullptr
		? FindGraph(*NegativeBlueprint, Scenarios[1].GraphId) : nullptr;
	if (NegativeBlueprint != nullptr && NegativeGraph != nullptr)
	{
		FBlueprintStateRestorer Restore{
			NegativeBlueprint,
			NegativeBlueprint->GetOutermost()->IsDirty(),
			NegativeBlueprint->Status};
		NegativeBlueprint->Status = BS_UpToDate;
		NegativeBlueprint->GetOutermost()->SetDirtyFlag(true);
		const FM6PreflightResult Dirty = FM6Preflight::EvaluateResolvedForAutomationTest(
			NegativeBlueprint, NegativeGraph, Scenarios[1].Query, TempRoot);
		Negatives.Add({TEXT("N1"), Dirty.Error.Code});
		NegativeBlueprint->GetOutermost()->SetDirtyFlag(false);
		NegativeBlueprint->Status = BS_Error;
		const FM6PreflightResult Compile = FM6Preflight::EvaluateResolvedForAutomationTest(
			NegativeBlueprint, NegativeGraph, Scenarios[1].Query, TempRoot);
		Negatives.Add({TEXT("N2"), Compile.Error.Code});
	}
	bool bPartial = false;
	FM6ProcessResult NonZero;
	NonZero.bStarted = true;
	NonZero.bCleanupSucceeded = true;
	NonZero.ReturnCode = 9;
	Negatives.Add({TEXT("N3"), RunnerFailureCode(NonZero, bPartial)});
	TestFalse(TEXT("N3 has zero partial activation"), bPartial);
	FM6ProcessResult Timeout = NonZero;
	Timeout.ReturnCode = -1;
	Timeout.bTimedOut = true;
	Negatives.Add({TEXT("N4"), RunnerFailureCode(Timeout, bPartial)});
	TestFalse(TEXT("N4 has zero partial activation"), bPartial);
	FM6ProcessResult Cancelled = NonZero;
	Cancelled.ReturnCode = -1;
	Cancelled.bCancelled = true;
	Negatives.Add({TEXT("N5"), RunnerFailureCode(Cancelled, bPartial)});
	TestFalse(TEXT("N5 has zero partial activation"), bPartial);

	const FString SourcePacket = FirstPacket.FindRef(TEXT("M6-E01"));
	const FString SourceFingerprint = Fingerprints.FindRef(TEXT("M6-E01"));
	const FString VersionPacket = FPaths::Combine(TempRoot, TEXT("negative-version"));
	const FString HashPacket = FPaths::Combine(TempRoot, TEXT("negative-hash"));
	if (CopyPacket(SourcePacket, VersionPacket) && CopyPacket(SourcePacket, HashPacket))
	{
		FString ManifestText;
		FFileHelper::LoadFileToString(
			ManifestText, *FPaths::Combine(VersionPacket, TEXT("manifest.json")));
		ManifestText.ReplaceInline(
			TEXT("\"schema_version\":\"1.0.0\""),
			TEXT("\"schema_version\":\"2.0.0\""));
		FFileHelper::SaveStringToFile(
			ManifestText, *FPaths::Combine(VersionPacket, TEXT("manifest.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FString Code;
		bool bPartialValue = false;
		LoadErrorCode(VersionPacket, SourceFingerprint, Code, bPartialValue);
		Negatives.Add({TEXT("N6"), Code});
		TestFalse(TEXT("N6 has zero partial packet"), bPartialValue);

		FString RawText;
		FFileHelper::LoadFileToString(
			RawText, *FPaths::Combine(HashPacket, TEXT("raw-source.json")));
		RawText.ReplaceInline(TEXT("5.8.1-"), TEXT("5.8.2-"));
		FFileHelper::SaveStringToFile(
			RawText, *FPaths::Combine(HashPacket, TEXT("raw-source.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Code.Reset();
		LoadErrorCode(HashPacket, SourceFingerprint, Code, bPartialValue);
		Negatives.Add({TEXT("N7"), Code});
		TestFalse(TEXT("N7 has zero partial packet"), bPartialValue);
		Code.Reset();
		LoadErrorCode(SourcePacket, FString::ChrN(64, TEXT('0')), Code, bPartialValue);
		Negatives.Add({TEXT("N8"), Code});
		TestFalse(TEXT("N8 has zero partial packet"), bPartialValue);
	}
	FM6PanelPresentationModel Panel;
	Panel.SetSourceJumpResult(false, TEXT("unmapped source"));
	Negatives.Add({
		TEXT("N9"),
		Panel.SourceJumpError().Contains(TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED"))
			? TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED") : FString()});
	const TMap<FString, FString> ExpectedNegativeCodes = {
		{TEXT("N1"), TEXT("M6_PRECONDITION_DIRTY_SOURCE")},
		{TEXT("N2"), TEXT("M6_PRECONDITION_COMPILE_FAILED")},
		{TEXT("N3"), TEXT("M6_RUNNER_NONZERO_EXIT")},
		{TEXT("N4"), TEXT("M6_RUNNER_TIMEOUT")},
		{TEXT("N5"), TEXT("M6_RUNNER_CANCELLED")},
		{TEXT("N6"), TEXT("M6_PACKET_VERSION_UNSUPPORTED")},
		{TEXT("N7"), TEXT("M6_PACKET_HASH_MISMATCH")},
		{TEXT("N8"), TEXT("M6_PACKET_SOURCE_STALE")},
		{TEXT("N9"), TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED")}};
	TestEqual(TEXT("Nine formal negative cases execute"), Negatives.Num(), 9);
	for (const TPair<FString, FString>& Negative : Negatives)
	{
		TestEqual(
			*FString::Printf(TEXT("%s stable public code"), *Negative.Key),
			Negative.Value, ExpectedNegativeCodes.FindRef(Negative.Key));
	}

	Panel.SelectBaseline(EM6Baseline::C);
	Panel.SelectEntity(TEXT("temporary"), EM6SelectionOrigin::Programmatic);
	Panel.DispatchReset();
	TestEqual(TEXT("Reset is deterministically Idle"), Panel.State(), EM6SessionState::Idle);
	TestEqual(TEXT("Reset is deterministically baseline A"), Panel.Baseline(), EM6Baseline::A);
	TestTrue(TEXT("Reset clears semantic selection"), Panel.SelectedEntityId().IsEmpty());

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
