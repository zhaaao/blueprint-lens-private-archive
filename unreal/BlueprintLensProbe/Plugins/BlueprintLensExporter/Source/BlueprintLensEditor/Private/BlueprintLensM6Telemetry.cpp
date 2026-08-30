// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6Telemetry.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensM6Telemetry
{
namespace
{
using FCanonicalWriter =
	TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

void SetError(FM6Error& OutError, const TCHAR* Code, const FString& Message)
{
	OutError.Code = Code;
	OutError.Phase = TEXT("telemetry");
	OutError.Message = Message;
	OutError.bRetryable = false;
}

FM6TelemetryReplayResult Fail(const TCHAR* Code, const FString& Message)
{
	FM6Error Error;
	SetError(Error, Code, Message);
	return MakeError(MoveTemp(Error));
}

bool IsLowerSha256(const FString& Value)
{
	if (Value.Len() != 64) return false;
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsDigit(Character) &&
			!(Character >= TEXT('a') && Character <= TEXT('f'))) return false;
	}
	return true;
}

bool IsRunId(const FString& Value)
{
	if (Value.IsEmpty()) return false;
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('.') &&
			Character != TEXT('_') && Character != TEXT('-')) return false;
	}
	return true;
}

bool IsPrivateValue(const FString& Value)
{
	return (Value.Len() >= 3 && FChar::IsAlpha(Value[0]) &&
		Value[1] == TEXT(':') && (Value[2] == TEXT('\\') || Value[2] == TEXT('/'))) ||
		Value.StartsWith(TEXT("\\\\")) ||
		Value.StartsWith(TEXT("file://"), ESearchCase::IgnoreCase) ||
		(Value.StartsWith(TEXT("/")) && !Value.StartsWith(TEXT("/Game/")));
}

bool IsKnownError(const FString& Code)
{
	if (Code.IsEmpty()) return true;
	static const TSet<FString> Codes = {
		TEXT("M6_PRECONDITION_NO_BLUEPRINT"), TEXT("M6_PRECONDITION_GRAPH_INVALID"),
		TEXT("M6_PRECONDITION_DIRTY_SOURCE"), TEXT("M6_PRECONDITION_COMPILE_FAILED"),
		TEXT("M6_PRECONDITION_QUERY_INVALID"), TEXT("M6_PRECONDITION_STAGING_UNAVAILABLE"),
		TEXT("M6_EXPORT_FAILED"), TEXT("M6_EXPORT_SOURCE_MISMATCH"),
		TEXT("M6_PIPELINE_TYPED_DOCUMENT_INVALID"), TEXT("M6_PIPELINE_SLICE_FAILED"),
		TEXT("M6_PIPELINE_EXPLANATION_FAILED"), TEXT("M6_PIPELINE_SHARED_FACTS_INVALID"),
		TEXT("M6_PIPELINE_BUDGET_EXCEEDED"),
		TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("M6_PACKET_VERSION_UNSUPPORTED"),
		TEXT("M6_PACKET_HASH_MISMATCH"), TEXT("M6_PACKET_CANONICAL_INVALID"),
		TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("M6_PACKET_SOURCE_STALE"),
		TEXT("M6_PACKET_PUBLISH_FAILED"), TEXT("M6_PACKET_OUTPUT_EXISTS"),
		TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("M6_RUNNER_NONZERO_EXIT"),
		TEXT("M6_RUNNER_TIMEOUT"), TEXT("M6_RUNNER_CANCELLED"),
		TEXT("M6_RUNNER_CLEANUP_FAILED"), TEXT("M6_VIEW_PROFILE_UNSUPPORTED"),
		TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("M6_VIEW_SELECTION_SYNC_FAILED"),
		TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED"), TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
		TEXT("M6_TELEMETRY_SEQUENCE_INVALID"), TEXT("M6_TELEMETRY_REPLAY_MISMATCH")};
	return Codes.Contains(Code);
}

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

bool CanonicalLine(const TSharedPtr<FJsonObject>& Object, FString& OutLine)
{
	OutLine.Reset();
	const TSharedRef<FCanonicalWriter> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutLine);
	if (!WriteCanonicalObject(Object, Writer) || !Writer->Close()) return false;
	OutLine += TEXT("\n");
	return true;
}

bool Sha256Text(const FString& Text, FString& OutHash)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
		return false;
	OutHash = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

const TCHAR* BaselineString(const EM6Baseline Baseline)
{
	switch (Baseline)
	{
	case EM6Baseline::A: return TEXT("A");
	case EM6Baseline::B: return TEXT("B");
	case EM6Baseline::C: return TEXT("C");
	default: return TEXT("");
	}
}

bool ParseBaseline(const FString& Value, EM6Baseline& Out)
{
	if (Value == TEXT("A")) Out = EM6Baseline::A;
	else if (Value == TEXT("B")) Out = EM6Baseline::B;
	else if (Value == TEXT("C")) Out = EM6Baseline::C;
	else return false;
	return true;
}

TSharedRef<FJsonObject> ResultObject(const FString& Status, const FString& ErrorCode)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), Status);
	Result->SetStringField(TEXT("error_code"), ErrorCode);
	return Result;
}

void ApplyInteraction(
	FM6TelemetryReplayState& State,
	const FString& EventType,
	const TSharedPtr<FJsonObject>& Payload)
{
	if (EventType == TEXT("baseline_changed"))
	{
		FString Value;
		if (Payload->TryGetStringField(TEXT("baseline_id"), Value))
			ParseBaseline(Value, State.Baseline);
		State.bReset = false;
	}
	else if (EventType == TEXT("entity_selected"))
	{
		Payload->TryGetStringField(TEXT("entity_id"), State.SelectedEntityId);
		State.bReset = false;
	}
	else if (EventType == TEXT("entity_expanded"))
	{
		FString Id;
		if (Payload->TryGetStringField(TEXT("entity_id"), Id)) State.ExpandedEntityIds.Add(Id);
		State.bReset = false;
	}
	else if (EventType == TEXT("entity_collapsed"))
	{
		FString Id;
		if (Payload->TryGetStringField(TEXT("entity_id"), Id)) State.ExpandedEntityIds.Remove(Id);
		State.bReset = false;
	}
	else if (EventType == TEXT("reset"))
	{
		State.Baseline = EM6Baseline::A;
		State.SelectedEntityId.Reset();
		State.ExpandedEntityIds.Reset();
		State.bReset = true;
	}
}
} // namespace
} // namespace BlueprintLensM6Telemetry

bool FM6TelemetryCounts::IsValid() const
{
	return Nodes >= 0 && Relations >= 0 && Edges >= 0 && Sccs >= 0 &&
		Collapses >= 0 && Fallbacks >= 0 && Opaque >= 0 && Uncertain >= 0 &&
		Unsupported >= 0 && Truncated >= 0 && ErrorReasons >= 0;
}

const TArray<const TCHAR*>& FM6TelemetryRecorder::RequiredStages()
{
	static const TArray<const TCHAR*> Stages = {
		TEXT("request"), TEXT("preflight"), TEXT("export"),
		TEXT("typed_document"), TEXT("slice"), TEXT("explanation"),
		TEXT("baseline_projection"), TEXT("layout"), TEXT("render"),
		TEXT("packet"), TEXT("reset")};
	return Stages;
}

bool FM6TelemetryRecorder::Begin(
	const FString& OutputPath,
	const FString& RunId,
	const FString& SemanticSha256,
	FM6Error& OutError)
{
	OutError = FM6Error();
	if (bBegun || FPaths::FileExists(OutputPath) ||
		!BlueprintLensM6Telemetry::IsRunId(RunId) ||
		!BlueprintLensM6Telemetry::IsLowerSha256(SemanticSha256) ||
		OutputPath.IsEmpty())
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("telemetry begin arguments are invalid or destination exists"));
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("telemetry destination directory is unavailable"));
		return false;
	}
	Path = OutputPath;
	ActiveRunId = RunId;
	ActiveSemanticSha256 = SemanticSha256;
	Lines.Reset();
	ResultStages.Reset();
	InteractionEvents.Reset();
	ActiveStage.Reset();
	ActiveStageStartedSeconds = 0.0;
	bActiveStageApplicable = false;
	bBegun = true;
	bSealed = false;
	return true;
}

bool FM6TelemetryRecorder::BeginStage(
	const FString& Stage,
	const bool bApplicable,
	FM6Error& OutError)
{
	if (!CheckWritable(OutError)) return false;
	const int32 StageIndex = RequiredStages().IndexOfByPredicate(
		[&Stage](const TCHAR* Candidate) { return Stage == Candidate; });
	if (StageIndex == INDEX_NONE || StageIndex != ResultStages.Num() ||
		!ActiveStage.IsEmpty())
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("stage start is out of order or a stage is already active"));
		return false;
	}
	ActiveStage = Stage;
	bActiveStageApplicable = bApplicable;
	ActiveStageStartedSeconds = FPlatformTime::Seconds();
	if (!bApplicable)
	{
		OutError = FM6Error();
		return true;
	}
	TSharedRef<FJsonObject> StartedPayload = MakeShared<FJsonObject>();
	StartedPayload->SetStringField(TEXT("stage"), Stage);
	return AppendEvent(
		TEXT("lifecycle"), TEXT("stage_started"), StartedPayload,
		BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError,
		FDateTime::UtcNow().ToIso8601());
}

bool FM6TelemetryRecorder::FinishStage(
	const FString& Stage,
	const FM6TelemetryCounts& Counts,
	const FString& ErrorCode,
	FM6Error& OutError)
{
	if (!CheckWritable(OutError)) return false;
	if (ActiveStage != Stage || !Counts.IsValid() ||
		!BlueprintLensM6Telemetry::IsKnownError(ErrorCode) ||
		(!bActiveStageApplicable && !ErrorCode.IsEmpty()))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("stage result is out of order or invalid"));
		return false;
	}
	TSharedRef<FJsonObject> CountsObject = MakeShared<FJsonObject>();
	CountsObject->SetNumberField(TEXT("nodes"), Counts.Nodes);
	CountsObject->SetNumberField(TEXT("relations"), Counts.Relations);
	CountsObject->SetNumberField(TEXT("edges"), Counts.Edges);
	CountsObject->SetNumberField(TEXT("sccs"), Counts.Sccs);
	CountsObject->SetNumberField(TEXT("collapses"), Counts.Collapses);
	CountsObject->SetNumberField(TEXT("fallbacks"), Counts.Fallbacks);
	CountsObject->SetNumberField(TEXT("opaque"), Counts.Opaque);
	CountsObject->SetNumberField(TEXT("uncertain"), Counts.Uncertain);
	CountsObject->SetNumberField(TEXT("unsupported"), Counts.Unsupported);
	CountsObject->SetNumberField(TEXT("truncated"), Counts.Truncated);
	CountsObject->SetNumberField(TEXT("error_reasons"), Counts.ErrorReasons);
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("stage"), Stage);
	Payload->SetStringField(
		TEXT("applicability"),
		bActiveStageApplicable ? TEXT("applicable") : TEXT("inapplicable"));
	Payload->SetObjectField(TEXT("counts"), CountsObject);
	const FString Status = !bActiveStageApplicable
		? TEXT("inapplicable") : (ErrorCode.IsEmpty() ? TEXT("ok") : TEXT("error"));
	const double DurationMs = bActiveStageApplicable
		? FMath::Max(0.0, (FPlatformTime::Seconds() - ActiveStageStartedSeconds) * 1000.0)
		: 0.0;
	if (!AppendEvent(
		TEXT("lifecycle"), TEXT("stage_result"), Payload,
		BlueprintLensM6Telemetry::ResultObject(Status, ErrorCode), OutError,
		FDateTime::UtcNow().ToIso8601(), DurationMs))
		return false;
	ResultStages.Add(Stage);
	ActiveStage.Reset();
	ActiveStageStartedSeconds = 0.0;
	bActiveStageApplicable = false;
	return true;
}

bool FM6TelemetryRecorder::RecordMeasuredStage(
	const FM6TelemetryStageMeasurement& Measurement,
	FM6Error& OutError)
{
	if (!CheckWritable(OutError)) return false;
	const int32 StageIndex = RequiredStages().IndexOfByPredicate(
		[&Measurement](const TCHAR* Candidate)
		{
			return Measurement.Stage == Candidate;
		});
	if (StageIndex == INDEX_NONE || StageIndex != ResultStages.Num() ||
		!Measurement.Counts.IsValid() || !Measurement.ErrorCode.IsEmpty() &&
			!BlueprintLensM6Telemetry::IsKnownError(Measurement.ErrorCode) ||
		(!Measurement.bApplicable && !Measurement.ErrorCode.IsEmpty()) ||
		Measurement.DurationMs < 0.0 || !FMath::IsFinite(Measurement.DurationMs))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("measured stage is out of order or invalid"));
		return false;
	}

	if (Measurement.bApplicable)
	{
		if (Measurement.StartTimestamp.IsEmpty() ||
			Measurement.ResultTimestamp.IsEmpty())
		{
			BlueprintLensM6Telemetry::SetError(
				OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
				TEXT("applicable measured stage timestamps are required"));
			return false;
		}
		TSharedRef<FJsonObject> StartedPayload = MakeShared<FJsonObject>();
		StartedPayload->SetStringField(TEXT("stage"), Measurement.Stage);
		if (!AppendEvent(
			TEXT("lifecycle"), TEXT("stage_started"), StartedPayload,
			BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")),
			OutError, Measurement.StartTimestamp))
			return false;
	}

	TSharedRef<FJsonObject> CountsObject = MakeShared<FJsonObject>();
	CountsObject->SetNumberField(TEXT("nodes"), Measurement.Counts.Nodes);
	CountsObject->SetNumberField(TEXT("relations"), Measurement.Counts.Relations);
	CountsObject->SetNumberField(TEXT("edges"), Measurement.Counts.Edges);
	CountsObject->SetNumberField(TEXT("sccs"), Measurement.Counts.Sccs);
	CountsObject->SetNumberField(TEXT("collapses"), Measurement.Counts.Collapses);
	CountsObject->SetNumberField(TEXT("fallbacks"), Measurement.Counts.Fallbacks);
	CountsObject->SetNumberField(TEXT("opaque"), Measurement.Counts.Opaque);
	CountsObject->SetNumberField(TEXT("uncertain"), Measurement.Counts.Uncertain);
	CountsObject->SetNumberField(TEXT("unsupported"), Measurement.Counts.Unsupported);
	CountsObject->SetNumberField(TEXT("truncated"), Measurement.Counts.Truncated);
	CountsObject->SetNumberField(TEXT("error_reasons"), Measurement.Counts.ErrorReasons);
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("stage"), Measurement.Stage);
	Payload->SetStringField(
		TEXT("applicability"),
		Measurement.bApplicable ? TEXT("applicable") : TEXT("inapplicable"));
	Payload->SetObjectField(TEXT("counts"), CountsObject);
	const FString Status = !Measurement.bApplicable
		? TEXT("inapplicable")
		: (Measurement.ErrorCode.IsEmpty() ? TEXT("ok") : TEXT("error"));
	if (!AppendEvent(
		TEXT("lifecycle"), TEXT("stage_result"), Payload,
		BlueprintLensM6Telemetry::ResultObject(Status, Measurement.ErrorCode),
		OutError, Measurement.ResultTimestamp, Measurement.DurationMs))
		return false;
	ResultStages.Add(Measurement.Stage);
	OutError = FM6Error();
	return true;
}

bool FM6TelemetryRecorder::RecordStage(
	const FString& Stage,
	const bool bApplicable,
	const FM6TelemetryCounts& Counts,
	const FString& ErrorCode,
	FM6Error& OutError)
{
	if (!CheckWritable(OutError)) return false;
	const int32 StageIndex = RequiredStages().IndexOfByPredicate(
		[&Stage](const TCHAR* Candidate) { return Stage == Candidate; });
	if (StageIndex == INDEX_NONE || StageIndex != ResultStages.Num() ||
		!Counts.IsValid() || !BlueprintLensM6Telemetry::IsKnownError(ErrorCode) ||
		(!bApplicable && !ErrorCode.IsEmpty()))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("stage result is out of order or invalid"));
		return false;
	}
	if (bApplicable)
	{
		TSharedRef<FJsonObject> StartedPayload = MakeShared<FJsonObject>();
		StartedPayload->SetStringField(TEXT("stage"), Stage);
		if (!AppendEvent(
			TEXT("lifecycle"), TEXT("stage_started"), StartedPayload,
			BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError))
			return false;
	}
	TSharedRef<FJsonObject> CountsObject = MakeShared<FJsonObject>();
	CountsObject->SetNumberField(TEXT("nodes"), Counts.Nodes);
	CountsObject->SetNumberField(TEXT("relations"), Counts.Relations);
	CountsObject->SetNumberField(TEXT("edges"), Counts.Edges);
	CountsObject->SetNumberField(TEXT("sccs"), Counts.Sccs);
	CountsObject->SetNumberField(TEXT("collapses"), Counts.Collapses);
	CountsObject->SetNumberField(TEXT("fallbacks"), Counts.Fallbacks);
	CountsObject->SetNumberField(TEXT("opaque"), Counts.Opaque);
	CountsObject->SetNumberField(TEXT("uncertain"), Counts.Uncertain);
	CountsObject->SetNumberField(TEXT("unsupported"), Counts.Unsupported);
	CountsObject->SetNumberField(TEXT("truncated"), Counts.Truncated);
	CountsObject->SetNumberField(TEXT("error_reasons"), Counts.ErrorReasons);
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("stage"), Stage);
	Payload->SetStringField(
		TEXT("applicability"), bApplicable ? TEXT("applicable") : TEXT("inapplicable"));
	Payload->SetObjectField(TEXT("counts"), CountsObject);
	const FString Status = !bApplicable
		? TEXT("inapplicable") : (ErrorCode.IsEmpty() ? TEXT("ok") : TEXT("error"));
	if (!AppendEvent(
		TEXT("lifecycle"), TEXT("stage_result"), Payload,
		BlueprintLensM6Telemetry::ResultObject(Status, ErrorCode), OutError))
		return false;
	ResultStages.Add(Stage);
	OutError = FM6Error();
	return true;
}

bool FM6TelemetryRecorder::RecordBaseline(
	const EM6Baseline Baseline,
	FM6Error& OutError)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(
		TEXT("baseline_id"), BlueprintLensM6Telemetry::BaselineString(Baseline));
	return AppendEvent(
		TEXT("interaction"), TEXT("baseline_changed"), Payload,
		BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError);
}

bool FM6TelemetryRecorder::RecordSelection(
	const FString& EntityId,
	FM6Error& OutError)
{
	if (EntityId.IsEmpty() || BlueprintLensM6Telemetry::IsPrivateValue(EntityId))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("entity selection contains a private or empty identity"));
		return false;
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("entity_id"), EntityId);
	return AppendEvent(
		TEXT("interaction"), TEXT("entity_selected"), Payload,
		BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError);
}

bool FM6TelemetryRecorder::RecordExpansion(
	const FString& EntityId,
	const bool bExpanded,
	FM6Error& OutError)
{
	if (EntityId.IsEmpty() || BlueprintLensM6Telemetry::IsPrivateValue(EntityId))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("entity expansion contains a private or empty identity"));
		return false;
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("entity_id"), EntityId);
	return AppendEvent(
		TEXT("interaction"),
		bExpanded ? TEXT("entity_expanded") : TEXT("entity_collapsed"),
		Payload, BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError);
}

bool FM6TelemetryRecorder::RecordSourceJump(
	const FString& EntityId,
	const FString& ErrorCode,
	FM6Error& OutError)
{
	if (EntityId.IsEmpty() || BlueprintLensM6Telemetry::IsPrivateValue(EntityId) ||
		!BlueprintLensM6Telemetry::IsKnownError(ErrorCode))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("source jump event is invalid"));
		return false;
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("entity_id"), EntityId);
	return AppendEvent(
		TEXT("interaction"), TEXT("source_jump"), Payload,
		BlueprintLensM6Telemetry::ResultObject(
			ErrorCode.IsEmpty() ? TEXT("ok") : TEXT("error"), ErrorCode), OutError);
}

bool FM6TelemetryRecorder::RecordReset(FM6Error& OutError)
{
	return AppendEvent(
		TEXT("interaction"), TEXT("reset"), MakeShared<FJsonObject>(),
		BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError);
}

bool FM6TelemetryRecorder::RecordReplay(FM6Error& OutError)
{
	return AppendEvent(
		TEXT("interaction"), TEXT("replay"), MakeShared<FJsonObject>(),
		BlueprintLensM6Telemetry::ResultObject(TEXT("ok"), TEXT("")), OutError);
}

bool FM6TelemetryRecorder::Seal(
	FString& OutPriorSha256,
	FM6Error& OutError)
{
	if (!CheckWritable(OutError)) return false;
	if (ResultStages.Num() != RequiredStages().Num())
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("required stage results are incomplete"));
		return false;
	}
	const FString Prior = FString::Join(Lines, TEXT(""));
	if (!BlueprintLensM6Telemetry::Sha256Text(Prior, OutPriorSha256))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry hash failed"));
		return false;
	}
	const FM6TelemetryReplayState State = ReplayCurrent();
	TSharedRef<FJsonObject> FinalState = MakeShared<FJsonObject>();
	FinalState->SetStringField(
		TEXT("baseline_id"), BlueprintLensM6Telemetry::BaselineString(State.Baseline));
	if (State.SelectedEntityId.IsEmpty()) FinalState->SetField(TEXT("selected_entity_id"), MakeShared<FJsonValueNull>());
	else FinalState->SetStringField(TEXT("selected_entity_id"), State.SelectedEntityId);
	TArray<FString> Expanded = State.ExpandedEntityIds.Array();
	Expanded.Sort();
	TArray<TSharedPtr<FJsonValue>> ExpandedValues;
	for (const FString& Id : Expanded) ExpandedValues.Add(MakeShared<FJsonValueString>(Id));
	FinalState->SetArrayField(TEXT("expanded_entity_ids"), MoveTemp(ExpandedValues));
	FinalState->SetBoolField(TEXT("reset"), State.bReset);
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("event_count"), Lines.Num());
	Payload->SetStringField(TEXT("prior_sha256"), OutPriorSha256);
	TSharedRef<FJsonObject> Result =
		BlueprintLensM6Telemetry::ResultObject(TEXT("sealed"), TEXT(""));
	Result->SetObjectField(TEXT("final_state"), FinalState);
	if (!AppendEvent(TEXT("seal"), TEXT("record_sealed"), Payload, Result, OutError))
		return false;
	bSealed = true;
	return true;
}

bool FM6TelemetryRecorder::AppendEvent(
	const FString& Phase,
	const FString& EventType,
	const TSharedRef<FJsonObject>& Payload,
	const TSharedRef<FJsonObject>& Result,
	FM6Error& OutError,
	const FString& Timestamp,
	const double DurationMs)
{
	if (!CheckWritable(OutError)) return false;
	TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("schema_version"), TEXT("1.0.0"));
	Event->SetStringField(TEXT("run_id"), ActiveRunId);
	Event->SetStringField(TEXT("semantic_sha256"), ActiveSemanticSha256);
	Event->SetNumberField(TEXT("sequence"), Lines.Num() + 1);
	Event->SetStringField(TEXT("phase"), Phase);
	Event->SetStringField(TEXT("event_type"), EventType);
	Event->SetObjectField(TEXT("payload"), Payload);
	Event->SetObjectField(TEXT("result"), Result);
	if (!Timestamp.IsEmpty()) Event->SetStringField(TEXT("timestamp"), Timestamp);
	if (DurationMs >= 0.0) Event->SetNumberField(TEXT("duration_ms"), DurationMs);
	FString Line;
	if (!BlueprintLensM6Telemetry::CanonicalLine(Event, Line) ||
		!FFileHelper::SaveStringToFile(
			Line, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append))
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SCHEMA_INVALID"),
			TEXT("canonical telemetry event could not be appended"));
		return false;
	}
	Lines.Add(Line);
	if (Phase == TEXT("interaction"))
		InteractionEvents.Add(TPair<FString, TSharedPtr<FJsonObject>>(EventType, Payload));
	OutError = FM6Error();
	return true;
}

bool FM6TelemetryRecorder::CheckWritable(FM6Error& OutError) const
{
	if (!bBegun || bSealed)
	{
		BlueprintLensM6Telemetry::SetError(
			OutError, TEXT("M6_TELEMETRY_SEQUENCE_INVALID"),
			TEXT("telemetry record is not writable"));
		return false;
	}
	return true;
}

FM6TelemetryReplayState FM6TelemetryRecorder::ReplayCurrent() const
{
	FM6TelemetryReplayState State;
	for (const auto& Event : InteractionEvents)
		BlueprintLensM6Telemetry::ApplyInteraction(State, Event.Key, Event.Value);
	return State;
}

FM6TelemetryReplayResult FM6TelemetryRecorder::Replay(
	const FString& InputPath,
	const FString& ExpectedSemanticSha256)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *InputPath) ||
		Text.IsEmpty() || Text.Contains(TEXT("\r")) || !Text.EndsWith(TEXT("\n")))
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry record bytes are invalid"));
	TArray<FString> RawLines;
	Text.ParseIntoArrayLines(RawLines, true);
	if (RawLines.IsEmpty())
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry record is empty"));
	TArray<TSharedPtr<FJsonObject>> Events;
	FString RunId;
	for (int32 Index = 0; Index < RawLines.Num(); ++Index)
	{
		TSharedPtr<FJsonObject> Event;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(RawLines[Index]);
		if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_SCHEMA_INVALID"), FString::Printf(
					TEXT("telemetry line %d cannot be parsed"), Index + 1));
		FString Canonical;
		if (!BlueprintLensM6Telemetry::CanonicalLine(Event, Canonical) ||
			Canonical != RawLines[Index] + TEXT("\n"))
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry line is not canonical"));
		double Sequence = 0.0;
		FString EventRunId;
		FString Semantic;
		if (!Event->TryGetNumberField(TEXT("sequence"), Sequence) ||
			static_cast<int32>(Sequence) != Index + 1 ||
			!Event->TryGetStringField(TEXT("run_id"), EventRunId) ||
			!Event->TryGetStringField(TEXT("semantic_sha256"), Semantic) ||
			Semantic != ExpectedSemanticSha256 ||
			(Index > 0 && EventRunId != RunId))
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_SEQUENCE_INVALID"), TEXT("telemetry identity or sequence changed"));
		if (Index == 0) RunId = EventRunId;
		Events.Add(Event);
	}
	FString LastType;
	if (!Events.Last()->TryGetStringField(TEXT("event_type"), LastType) ||
		LastType != TEXT("record_sealed"))
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SEQUENCE_INVALID"), TEXT("telemetry record is not sealed"));
	const TSharedPtr<FJsonObject>* SealPayload = nullptr;
	const TSharedPtr<FJsonObject>* SealResult = nullptr;
	if (!Events.Last()->TryGetObjectField(TEXT("payload"), SealPayload) || SealPayload == nullptr ||
		!Events.Last()->TryGetObjectField(TEXT("result"), SealResult) || SealResult == nullptr)
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry seal is invalid"));
	FString PriorText;
	for (int32 Index = 0; Index < RawLines.Num() - 1; ++Index)
		PriorText += RawLines[Index] + TEXT("\n");
	FString ExpectedPriorHash;
	FString DeclaredPriorHash;
	double EventCount = 0.0;
	if (!BlueprintLensM6Telemetry::Sha256Text(PriorText, ExpectedPriorHash) ||
		!(*SealPayload)->TryGetStringField(TEXT("prior_sha256"), DeclaredPriorHash) ||
		!(*SealPayload)->TryGetNumberField(TEXT("event_count"), EventCount) ||
		DeclaredPriorHash != ExpectedPriorHash ||
		static_cast<int32>(EventCount) != Events.Num() - 1)
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_REPLAY_MISMATCH"), TEXT("telemetry seal hash disagrees"));

	FM6TelemetryReplayState State;
	TArray<FString> Stages;
	for (int32 Index = 0; Index < Events.Num() - 1; ++Index)
	{
		FString EventType;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (!Events[Index]->TryGetStringField(TEXT("event_type"), EventType) ||
			!Events[Index]->TryGetObjectField(TEXT("payload"), Payload) || Payload == nullptr)
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("telemetry event is invalid"));
		if (EventType == TEXT("stage_result"))
		{
			FString Stage;
			if (!(*Payload)->TryGetStringField(TEXT("stage"), Stage))
				return BlueprintLensM6Telemetry::Fail(
					TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("stage result is invalid"));
			Stages.Add(Stage);
		}
		BlueprintLensM6Telemetry::ApplyInteraction(State, EventType, *Payload);
	}
	if (Stages.Num() != RequiredStages().Num())
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SEQUENCE_INVALID"), TEXT("required stages are incomplete"));
	for (int32 Index = 0; Index < Stages.Num(); ++Index)
	{
		if (Stages[Index] != RequiredStages()[Index])
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_SEQUENCE_INVALID"), TEXT("required stages are out of order"));
	}
	const TSharedPtr<FJsonObject>* FinalState = nullptr;
	FString Baseline;
	FString Selected;
	bool bSelectedNull = false;
	const TArray<TSharedPtr<FJsonValue>>* Expanded = nullptr;
	bool bReset = false;
	if (!(*SealResult)->TryGetObjectField(TEXT("final_state"), FinalState) || FinalState == nullptr ||
		!(*FinalState)->TryGetStringField(TEXT("baseline_id"), Baseline) ||
		!(*FinalState)->TryGetArrayField(TEXT("expanded_entity_ids"), Expanded) || Expanded == nullptr ||
		!(*FinalState)->TryGetBoolField(TEXT("reset"), bReset))
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("sealed final state is invalid"));
	const TSharedPtr<FJsonValue> SelectedValue = (*FinalState)->TryGetField(TEXT("selected_entity_id"));
	if (!SelectedValue.IsValid())
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("sealed selection is missing"));
	bSelectedNull = SelectedValue->IsNull();
	if (!bSelectedNull && !SelectedValue->TryGetString(Selected))
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_SCHEMA_INVALID"), TEXT("sealed selection is invalid"));
	EM6Baseline DeclaredBaseline;
	if (!BlueprintLensM6Telemetry::ParseBaseline(Baseline, DeclaredBaseline) ||
		DeclaredBaseline != State.Baseline || Selected != State.SelectedEntityId ||
		bReset != State.bReset || Expanded->Num() != State.ExpandedEntityIds.Num())
		return BlueprintLensM6Telemetry::Fail(
			TEXT("M6_TELEMETRY_REPLAY_MISMATCH"), TEXT("sealed final state disagrees with replay"));
	for (const TSharedPtr<FJsonValue>& Value : *Expanded)
	{
		FString Id;
		if (!Value->TryGetString(Id) || !State.ExpandedEntityIds.Contains(Id))
			return BlueprintLensM6Telemetry::Fail(
				TEXT("M6_TELEMETRY_REPLAY_MISMATCH"), TEXT("sealed expansion state disagrees"));
	}
	return MakeValue(MoveTemp(State));
}
