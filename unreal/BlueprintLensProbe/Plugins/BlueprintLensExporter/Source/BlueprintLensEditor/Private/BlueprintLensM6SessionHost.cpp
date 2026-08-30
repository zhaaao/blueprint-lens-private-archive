// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6SessionHost.h"

#include "BlueprintEditor.h"
#include "BlueprintLensM6Preflight.h"
#include "BlueprintLensM6ProcessRunner.h"
#include "BlueprintLensM6PythonResolver.h"
#include "BlueprintLensM6SessionController.h"
#include "BlueprintLensM6SessionPacket.h"
#include "BlueprintLensM6Telemetry.h"
#include "BlueprintLensProductionExporter.h"
#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensM6SessionHost
{
FString WorkspaceRoot()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../..")));
}

namespace
{
using FCanonicalWriter =
	TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

FM6Error Error(const TCHAR* Code, const TCHAR* Phase, const FString& Message)
{
	FM6Error Result;
	Result.Code = Code;
	Result.Phase = Phase;
	Result.Message = Message;
	Result.bRetryable = false;
	return Result;
}

bool WriteValue(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedRef<FCanonicalWriter>& Writer);

bool WriteObject(
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
		Writer->WriteIdentifierPrefix(FStringView(*Field->Key, Field->Key.Len()));
		if (!WriteValue(Field->Value, Writer)) return false;
	}
	Writer->WriteObjectEnd();
	return true;
}

bool WriteValue(
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
			if (!WriteValue(Item, Writer)) return false;
		}
		Writer->WriteArrayEnd();
		return true;
	case EJson::Object: return WriteObject(Value->AsObject(), Writer);
	default: return false;
	}
}

bool WriteRequest(
	const FM6Request& Request,
	const FString& Path)
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
	FString Text;
	const TSharedRef<FCanonicalWriter> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	if (!WriteObject(Root, Writer) || !Writer->Close()) return false;
	Text += TEXT("\n");
	return FFileHelper::SaveStringToFile(
		Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool LoadStageTrace(
	const FString& Path,
	TMap<FString, FM6TelemetryStageMeasurement>& OutMeasurements)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
		return false;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Root->TryGetArrayField(TEXT("stages"), Values) || Values == nullptr)
		return false;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) return false;
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (!Object.IsValid()) return false;
		FM6TelemetryStageMeasurement Measurement;
		if (!Object->TryGetStringField(TEXT("stage"), Measurement.Stage) ||
			!Object->TryGetStringField(
				TEXT("start_timestamp"), Measurement.StartTimestamp) ||
			!Object->TryGetStringField(
				TEXT("result_timestamp"), Measurement.ResultTimestamp) ||
			!Object->TryGetNumberField(TEXT("duration_ms"), Measurement.DurationMs) ||
			!Object->TryGetStringField(TEXT("error_code"), Measurement.ErrorCode) ||
			!FM6TelemetryRecorder::RequiredStages().ContainsByPredicate(
				[&Measurement](const TCHAR* Candidate)
				{
					return Measurement.Stage == Candidate;
				}))
			return false;
		OutMeasurements.Add(Measurement.Stage, MoveTemp(Measurement));
	}
	return true;
}

class FPreflightAdapter final : public IM6PreflightProvider
{
public:
	explicit FPreflightAdapter(TWeakPtr<FBlueprintEditor> InEditor)
		: Editor(MoveTemp(InEditor))
	{
		Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6Sessions")));
		IFileManager::Get().MakeDirectory(*Root, true);
	}

	virtual FM6PreflightResult Evaluate(const FM6QueryInput& Query) override
	{
		return FM6Preflight::Evaluate(Editor.Pin(), Query, Root);
	}

private:
	TWeakPtr<FBlueprintEditor> Editor;
	FString Root;
};

class FExportAdapter final : public IM6ExportProvider
{
public:
	explicit FExportAdapter(TWeakPtr<FBlueprintEditor> InEditor)
		: Editor(MoveTemp(InEditor))
	{
	}

	virtual FM6ExportPreparation Prepare(
		const FM6PreflightResult& Preflight) override
	{
		FM6ExportPreparation Result;
		const TSharedPtr<FBlueprintEditor> Pinned = Editor.Pin();
		UBlueprint* Blueprint = Pinned.IsValid() ? Pinned->GetBlueprintObj() : nullptr;
		if (Blueprint == nullptr)
		{
			Result.Error = Error(TEXT("M6_EXPORT_FAILED"), TEXT("export"), TEXT("the current Blueprint is unavailable"));
			return Result;
		}
		const FString RawPath = FPaths::Combine(
			Preflight.OwnedStagingDirectory, TEXT("raw-source.json"));
		BlueprintLensProductionExporter::FExportRequest ExportRequest;
		ExportRequest.Blueprint = Blueprint;
		ExportRequest.OutputPath = RawPath;
		BlueprintLensProductionExporter::FExportResult ExportResult;
		BlueprintLensProductionExporter::FExportError ExportError;
		if (!BlueprintLensProductionExporter::ExportRawDocument(
			ExportRequest, ExportResult, ExportError))
		{
			Result.Error = Error(TEXT("M6_EXPORT_FAILED"), TEXT("export"), ExportError.Message);
			return Result;
		}
		if (ExportResult.BlueprintObjectPath != Preflight.Request.AssetPath)
		{
			Result.Error = Error(TEXT("M6_EXPORT_SOURCE_MISMATCH"), TEXT("export"), TEXT("the export source changed after preflight"));
			return Result;
		}
		const FString RequestPath = FPaths::Combine(
			Preflight.OwnedStagingDirectory, TEXT("request.json"));
		if (!WriteRequest(Preflight.Request, RequestPath))
		{
			Result.Error = Error(TEXT("M6_EXPORT_FAILED"), TEXT("export"), TEXT("the canonical M6 request could not be written"));
			return Result;
		}
		const FString Root = WorkspaceRoot();
		const FM6PythonResolutionResult PythonResult =
			FM6PythonResolver::Resolve(Root);
		if (!PythonResult.bValid)
		{
			Result.Error = Error(
				TEXT("M6_PYTHON_SETUP_REQUIRED"),
				TEXT("runner"),
				TEXT("A supported CPython 3.11 through 3.14 runtime with the Blueprint Lens analysis package is required."));
			return Result;
		}
		const FString Python = PythonResult.ExecutablePath;
		const FString Script = FPaths::Combine(Root, TEXT("analysis/run_m6_session.py"));
		const FString SchemaRoot = FPaths::Combine(Root, TEXT("schemas"));
		const FString PacketDirectory = FPaths::Combine(
			Preflight.OwnedStagingDirectory, TEXT("packet"));
		const FString StageTracePath = FPaths::Combine(
			Preflight.OwnedStagingDirectory, TEXT("stage-trace.json"));
		if (!FPaths::FileExists(Python) || !FPaths::FileExists(Script) ||
			!IFileManager::Get().DirectoryExists(*SchemaRoot) ||
			IFileManager::Get().DirectoryExists(*PacketDirectory))
		{
			Result.Error = Error(TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("runner"), TEXT("configured Python, M6 script, schemas or fresh packet destination are unavailable"));
			return Result;
		}
		Result.bSucceeded = true;
		Result.PacketDirectory = PacketDirectory;
		Result.ExpectedSourceFingerprint = Preflight.Request.SourceFingerprint;
		Result.Invocation.ExecutablePath = Python;
		Result.Invocation.WorkingDirectory = Root;
		Result.Invocation.PacketDirectory = PacketDirectory;
		Result.Invocation.ExpectedPacketFiles = 7;
		Result.Invocation.TimeoutSeconds = 180.0;
		Result.Invocation.Arguments = {
			Script, TEXT("--request"), RequestPath,
			TEXT("--raw-source"), RawPath,
			TEXT("--output"), PacketDirectory,
			TEXT("--schema-root"), SchemaRoot,
			TEXT("--stage-trace"), StageTracePath};
		LastStagingDirectory = Preflight.OwnedStagingDirectory;
		return Result;
	}

	const FString& StagingDirectory() const { return LastStagingDirectory; }

private:
	TWeakPtr<FBlueprintEditor> Editor;
	FString LastStagingDirectory;
};

class FPacketAdapter final : public IM6PacketProvider
{
public:
	virtual FM6SessionPacketLoadResult Load(
		const FString& PacketDirectory,
		const FString& ExpectedSourceFingerprint) override
	{
		return FM6SessionPacketLoader::Load(
			PacketDirectory, ExpectedSourceFingerprint);
	}
};

class FTelemetryAdapter final : public IM6SessionTelemetry
{
public:
	struct FStageStart
	{
		bool bApplicable = true;
		double StartedSeconds = 0.0;
		FString StartedTimestamp;
	};

	virtual void RecordState(EM6SessionState State, const FM6Error& Error) override
	{
		LastState = State;
		LastStateError = Error;
	}

	virtual void BeginStage(const FString& Stage, const bool bApplicable) override
	{
		if (Stage.IsEmpty() || ActiveStages.Contains(Stage)) return;
		FStageStart Start;
		Start.bApplicable = bApplicable;
		Start.StartedSeconds = FPlatformTime::Seconds();
		Start.StartedTimestamp = FDateTime::UtcNow().ToIso8601();
		ActiveStages.Add(Stage, MoveTemp(Start));
	}

	virtual void FinishStage(
		const FString& Stage,
		const FM6Error& Error) override
	{
		FStageStart* Start = ActiveStages.Find(Stage);
		if (Start == nullptr) return;
		FM6TelemetryStageMeasurement Measurement;
		Measurement.Stage = Stage;
		Measurement.bApplicable = Start->bApplicable;
		Measurement.Counts = ActiveCounts;
		Measurement.ErrorCode = Error.Code;
		Measurement.StartTimestamp = Start->StartedTimestamp;
		Measurement.ResultTimestamp = FDateTime::UtcNow().ToIso8601();
		Measurement.DurationMs = Start->bApplicable
			? FMath::Max(0.0, (FPlatformTime::Seconds() - Start->StartedSeconds) * 1000.0)
			: 0.0;
		ActiveStages.Remove(Stage);
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed())
		{
			FM6Error Ignored;
			Recorder->RecordMeasuredStage(Measurement, Ignored);
		}
		else
		{
			PendingStages.Add(Stage, MoveTemp(Measurement));
		}
	}

	virtual void RecordBaseline(EM6Baseline Baseline) override
	{
		FM6Error Ignored;
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed())
			Recorder->RecordBaseline(Baseline, Ignored);
	}

	virtual void RecordSelection(
		const FString& EntityId,
		EM6SelectionOrigin) override
	{
		FM6Error Ignored;
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed())
			Recorder->RecordSelection(EntityId, Ignored);
	}

	virtual void RecordReset() override
	{
		FM6Error Ignored;
		if (!Recorder.IsValid() || !Recorder->IsBegun() || Recorder->IsSealed()) return;
		Recorder->RecordReset(Ignored);
	}

	virtual void Seal() override
	{
		FM6Error Ignored;
		FString Hash;
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed())
			Recorder->Seal(Hash, Ignored);
	}

	void SetStagingDirectory(const FString& InStagingDirectory)
	{
		StagingDirectory = InStagingDirectory;
	}

	virtual void ActivatePacket(const FM6LoadedSessionPacket& Packet) override
	{
		if (StagingDirectory.IsEmpty()) return;
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed()) return;
		Recorder = MakeUnique<FM6TelemetryRecorder>();
		ActiveCounts = FM6TelemetryCounts();
		FM6Error RecorderError;
		const FString Path = FPaths::Combine(
			StagingDirectory, TEXT("ue-interaction.telemetry.v1.jsonl"));
		const FString RunId = FString::Printf(
			TEXT("ue-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!Recorder->Begin(Path, RunId, Packet.SemanticSha256, RecorderError)) return;
		ActiveCounts.Nodes = Packet.BaselineFacts.Entities.Num();
		ActiveCounts.Relations = Packet.BaselineFacts.Relations.Num();
		ActiveCounts.Edges = Packet.Slice.EdgeIds.Num();
		ActiveCounts.Truncated = Packet.BaselineFacts.TruncatedCount;
		for (const FM6BaselineEntity& Entity : Packet.BaselineFacts.Entities)
		{
			ActiveCounts.Opaque += Entity.SemanticStatus == TEXT("opaque") ? 1 : 0;
			ActiveCounts.Uncertain += Entity.SemanticStatus == TEXT("uncertain") ? 1 : 0;
			ActiveCounts.Unsupported += Entity.SemanticStatus == TEXT("unsupported") ? 1 : 0;
		}
		ActiveCounts.ErrorReasons = Packet.BaselineFacts.Boundaries.Num();
		TMap<FString, FM6TelemetryStageMeasurement> TraceMeasurements;
		LoadStageTrace(
			FPaths::Combine(StagingDirectory, TEXT("stage-trace.json")),
			TraceMeasurements);
		for (auto& Pair : TraceMeasurements)
			if (!PendingStages.Contains(Pair.Key))
				PendingStages.Add(Pair.Key, MoveTemp(Pair.Value));
		for (const TCHAR* Stage : FM6TelemetryRecorder::RequiredStages())
		{
			if (FString(Stage) == TEXT("baseline_projection") ||
				FString(Stage) == TEXT("layout") ||
				FString(Stage) == TEXT("render") ||
				FString(Stage) == TEXT("packet") ||
				FString(Stage) == TEXT("reset"))
				break;
			FM6TelemetryStageMeasurement Measurement;
			if (FM6TelemetryStageMeasurement* Pending = PendingStages.Find(Stage))
			{
				Measurement = MoveTemp(*Pending);
				PendingStages.Remove(Stage);
			}
			else
			{
				Measurement.Stage = Stage;
				Measurement.bApplicable = false;
				Measurement.Counts = ActiveCounts;
				Measurement.DurationMs = 0.0;
			}
			Measurement.Counts = ActiveCounts;
			if (!Recorder->RecordMeasuredStage(Measurement, RecorderError)) return;
		}
	}

	void RecordSourceJump(const FString& EntityId, const FString& ErrorCode)
	{
		FM6Error Ignored;
		if (Recorder.IsValid() && Recorder->IsBegun() && !Recorder->IsSealed())
			Recorder->RecordSourceJump(EntityId, ErrorCode, Ignored);
	}

private:
	TUniquePtr<FM6TelemetryRecorder> Recorder;
	FM6TelemetryCounts ActiveCounts;
	FString StagingDirectory;
	TMap<FString, FStageStart> ActiveStages;
	TMap<FString, FM6TelemetryStageMeasurement> PendingStages;
	EM6SessionState LastState = EM6SessionState::Idle;
	FM6Error LastStateError;
};
} // namespace
} // namespace BlueprintLensM6SessionHost

class FM6SessionHost::FImpl
{
public:
	FImpl(
		TWeakPtr<FBlueprintEditor> Editor,
		IM6SessionView& View)
		: Preflight(Editor)
		, Exporter(Editor)
		, Controller(Preflight, Exporter, Runner, Loader, Telemetry, View)
	{
	}

	BlueprintLensM6SessionHost::FPreflightAdapter Preflight;
	BlueprintLensM6SessionHost::FExportAdapter Exporter;
	FM6ProcessRunner Runner;
	BlueprintLensM6SessionHost::FPacketAdapter Loader;
	BlueprintLensM6SessionHost::FTelemetryAdapter Telemetry;
	FM6SessionController Controller;
	FString ActivatedSemanticSha;
};

FM6SessionHost::FM6SessionHost(
	TWeakPtr<FBlueprintEditor> BlueprintEditor,
	IM6SessionView& View)
	: Impl(MakeUnique<FImpl>(MoveTemp(BlueprintEditor), View))
{
}

FM6SessionHost::~FM6SessionHost() = default;

FM6SessionController& FM6SessionHost::Controller()
{
	return Impl->Controller;
}

void FM6SessionHost::Tick(const double NowSeconds)
{
	Impl->Telemetry.SetStagingDirectory(Impl->Exporter.StagingDirectory());
	Impl->Controller.Tick(NowSeconds);
	if (Impl->Controller.GetSnapshot().State == EM6SessionState::Idle)
		Impl->ActivatedSemanticSha.Reset();
}

void FM6SessionHost::BeginStage(const FString& Stage, const bool bApplicable)
{
	Impl->Telemetry.BeginStage(Stage, bApplicable);
}

void FM6SessionHost::FinishStage(
	const FString& Stage,
	const FM6Error& Error)
{
	Impl->Telemetry.FinishStage(Stage, Error);
}

void FM6SessionHost::RecordSourceJump(
	const FString& EntityId,
	const FString& ErrorCode)
{
	Impl->Telemetry.RecordSourceJump(EntityId, ErrorCode);
}
