// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6Types.h"
#include "CoreMinimal.h"
#include "Templates/ValueOrError.h"

enum class EM6Baseline : uint8
{
	A,
	B,
	C
};

enum class EM6SelectionOrigin : uint8
{
	BaselineView,
	NativeGraph,
	Programmatic
};

struct FM6TelemetryCounts
{
	int32 Nodes = 0;
	int32 Relations = 0;
	int32 Edges = 0;
	int32 Sccs = 0;
	int32 Collapses = 0;
	int32 Fallbacks = 0;
	int32 Opaque = 0;
	int32 Uncertain = 0;
	int32 Unsupported = 0;
	int32 Truncated = 0;
	int32 ErrorReasons = 0;

	bool IsValid() const;
};

struct FM6TelemetryStageMeasurement
{
	FString Stage;
	bool bApplicable = true;
	FM6TelemetryCounts Counts;
	FString ErrorCode;
	FString StartTimestamp;
	FString ResultTimestamp;
	double DurationMs = 0.0;
};

struct FM6TelemetryReplayState
{
	EM6Baseline Baseline = EM6Baseline::A;
	FString SelectedEntityId;
	TSet<FString> ExpandedEntityIds;
	bool bReset = false;
};

using FM6TelemetryReplayResult =
	TValueOrError<FM6TelemetryReplayState, FM6Error>;

class FM6TelemetryRecorder
{
public:
	static const TArray<const TCHAR*>& RequiredStages();

	bool Begin(
		const FString& OutputPath,
		const FString& RunId,
		const FString& SemanticSha256,
		FM6Error& OutError);
	bool BeginStage(
		const FString& Stage,
		bool bApplicable,
		FM6Error& OutError);
	bool FinishStage(
		const FString& Stage,
		const FM6TelemetryCounts& Counts,
		const FString& ErrorCode,
		FM6Error& OutError);
	bool RecordMeasuredStage(
		const FM6TelemetryStageMeasurement& Measurement,
		FM6Error& OutError);
	bool RecordStage(
		const FString& Stage,
		bool bApplicable,
		const FM6TelemetryCounts& Counts,
		const FString& ErrorCode,
		FM6Error& OutError);
	bool RecordBaseline(EM6Baseline Baseline, FM6Error& OutError);
	bool RecordSelection(const FString& EntityId, FM6Error& OutError);
	bool RecordExpansion(
		const FString& EntityId,
		bool bExpanded,
		FM6Error& OutError);
	bool RecordSourceJump(
		const FString& EntityId,
		const FString& ErrorCode,
		FM6Error& OutError);
	bool RecordReset(FM6Error& OutError);
	bool RecordReplay(FM6Error& OutError);
	bool Seal(FString& OutPriorSha256, FM6Error& OutError);

	bool IsBegun() const { return bBegun; }
	bool IsSealed() const { return bSealed; }
	int32 EventCount() const { return Lines.Num(); }

	static FM6TelemetryReplayResult Replay(
		const FString& InputPath,
		const FString& ExpectedSemanticSha256);

private:
	bool AppendEvent(
		const FString& Phase,
		const FString& EventType,
		const TSharedRef<class FJsonObject>& Payload,
		const TSharedRef<class FJsonObject>& Result,
		FM6Error& OutError,
		const FString& Timestamp = FString(),
		double DurationMs = -1.0);
	bool CheckWritable(FM6Error& OutError) const;
	FM6TelemetryReplayState ReplayCurrent() const;

	FString Path;
	FString ActiveRunId;
	FString ActiveSemanticSha256;
	TArray<FString> Lines;
	TArray<FString> ResultStages;
	TArray<TPair<FString, TSharedPtr<class FJsonObject>>> InteractionEvents;
	FString ActiveStage;
	double ActiveStageStartedSeconds = 0.0;
	bool bActiveStageApplicable = false;
	bool bBegun = false;
	bool bSealed = false;
};
