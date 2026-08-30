// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EBlueprintLensAsyncTraceCloseReason : uint8
{
	None,
	Complete,
	Cancelled,
	Timeout,
	Overflow
};

struct FBlueprintLensAsyncTraceEvent
{
	FString TraceId;
	FString InstanceId;
	FString InvocationId;
	FString EventId;
	FString SourceOccurrenceId;
	FString EventKind;
	FString BoundaryPhase;
	FString ContinuationId;
	FString ParticipantId;
	int32 ObservationIndex = INDEX_NONE;
	int32 WorldTick = INDEX_NONE;
};

class BLUEPRINTLENSPROBE_API FBlueprintLensAsyncTraceRecorder
{
public:
	explicit FBlueprintLensAsyncTraceRecorder(int32 InCapacity = 64);

	bool Open(
		const FString& InTraceId,
		const FString& InInstanceId,
		const FString& InInvocationId,
		const FString& InScheduleVariant,
		FString& OutError);

	bool Record(
		const FString& SourceOccurrenceId,
		const FString& EventKind,
		int32 WorldTick,
		const FString& ContinuationId,
		const FString& ParticipantId,
		FString& OutError,
		const FString& BoundaryPhase = FString());

	bool Close(EBlueprintLensAsyncTraceCloseReason Reason, FString& OutError);

	const TArray<FBlueprintLensAsyncTraceEvent>& GetEvents() const { return Events; }
	int32 GetCapacity() const { return Capacity; }
	int32 GetDroppedEventCount() const { return DroppedEventCount; }
	EBlueprintLensAsyncTraceCloseReason GetCloseReason() const { return CloseReason; }
	const FString& GetTraceId() const { return TraceId; }
	const FString& GetInstanceId() const { return InstanceId; }
	const FString& GetInvocationId() const { return InvocationId; }
	const FString& GetScheduleVariant() const { return ScheduleVariant; }
	bool IsOpen() const { return bOpen; }
	bool IsClosed() const { return !bOpen && CloseReason != EBlueprintLensAsyncTraceCloseReason::None; }
	bool IsComplete() const;

private:
	bool ValidateCompleteEventSet(FString& OutError) const;

	int32 Capacity = 64;
	int32 DroppedEventCount = 0;
	bool bOpen = false;
	FString TraceId;
	FString InstanceId;
	FString InvocationId;
	FString ScheduleVariant;
	EBlueprintLensAsyncTraceCloseReason CloseReason = EBlueprintLensAsyncTraceCloseReason::None;
	TArray<FBlueprintLensAsyncTraceEvent> Events;
};
