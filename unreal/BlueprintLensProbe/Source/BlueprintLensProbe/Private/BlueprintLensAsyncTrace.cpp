// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncTrace.h"

FBlueprintLensAsyncTraceRecorder::FBlueprintLensAsyncTraceRecorder(const int32 InCapacity)
	: Capacity(FMath::Max(1, InCapacity))
{
}

bool FBlueprintLensAsyncTraceRecorder::Open(
	const FString& InTraceId,
	const FString& InInstanceId,
	const FString& InInvocationId,
	const FString& InScheduleVariant,
	FString& OutError)
{
	OutError.Reset();
	if (bOpen)
	{
		OutError = TEXT("Trace is already open.");
		return false;
	}
	if (InTraceId.IsEmpty() || InInstanceId.IsEmpty() || InInvocationId.IsEmpty())
	{
		OutError = TEXT("Trace, instance and invocation identities must be non-empty.");
		return false;
	}
	if (InScheduleVariant != TEXT("A_FIRST") && InScheduleVariant != TEXT("B_FIRST"))
	{
		OutError = FString::Printf(TEXT("Unsupported schedule variant: %s"), *InScheduleVariant);
		return false;
	}

	TraceId = InTraceId;
	InstanceId = InInstanceId;
	InvocationId = InInvocationId;
	ScheduleVariant = InScheduleVariant;
	Events.Reset();
	DroppedEventCount = 0;
	CloseReason = EBlueprintLensAsyncTraceCloseReason::None;
	bOpen = true;
	return true;
}

bool FBlueprintLensAsyncTraceRecorder::Record(
	const FString& SourceOccurrenceId,
	const FString& EventKind,
	const int32 WorldTick,
	const FString& ContinuationId,
	const FString& ParticipantId,
	FString& OutError,
	const FString& BoundaryPhase)
{
	OutError.Reset();
	if (!bOpen)
	{
		OutError = TEXT("Trace is closed.");
		return false;
	}
	if (SourceOccurrenceId.IsEmpty() || EventKind.IsEmpty() || WorldTick < 0)
	{
		OutError = TEXT("Trace event requires source occurrence, event kind and non-negative world tick.");
		return false;
	}
	if (Events.Num() >= Capacity)
	{
		++DroppedEventCount;
		bOpen = false;
		CloseReason = EBlueprintLensAsyncTraceCloseReason::Overflow;
		OutError = TEXT("Trace capacity exceeded.");
		return false;
	}

	FBlueprintLensAsyncTraceEvent& Event = Events.AddDefaulted_GetRef();
	Event.TraceId = TraceId;
	Event.InstanceId = InstanceId;
	Event.InvocationId = InvocationId;
	Event.ObservationIndex = Events.Num() - 1;
	Event.EventId = FString::Printf(TEXT("%s:event:%04d"), *TraceId, Event.ObservationIndex);
	Event.SourceOccurrenceId = SourceOccurrenceId;
	Event.EventKind = EventKind;
	Event.BoundaryPhase = BoundaryPhase;
	Event.WorldTick = WorldTick;
	Event.ContinuationId = ContinuationId;
	Event.ParticipantId = ParticipantId;
	return true;
}

bool FBlueprintLensAsyncTraceRecorder::Close(
	const EBlueprintLensAsyncTraceCloseReason Reason,
	FString& OutError)
{
	OutError.Reset();
	if (!bOpen)
	{
		OutError = TEXT("Trace is already closed.");
		return false;
	}
	if (Reason == EBlueprintLensAsyncTraceCloseReason::None
		|| Reason == EBlueprintLensAsyncTraceCloseReason::Overflow)
	{
		OutError = TEXT("Trace must close explicitly as complete, cancelled or timeout.");
		return false;
	}
	if (Reason == EBlueprintLensAsyncTraceCloseReason::Complete
		&& !ValidateCompleteEventSet(OutError))
	{
		return false;
	}
	bOpen = false;
	CloseReason = Reason;
	return true;
}

bool FBlueprintLensAsyncTraceRecorder::IsComplete() const
{
	FString Error;
	return !bOpen
		&& CloseReason == EBlueprintLensAsyncTraceCloseReason::Complete
		&& DroppedEventCount == 0
		&& ValidateCompleteEventSet(Error);
}

bool FBlueprintLensAsyncTraceRecorder::ValidateCompleteEventSet(FString& OutError) const
{
	OutError.Reset();
	if (DroppedEventCount != 0)
	{
		OutError = TEXT("A complete LC4-ASYNC trace cannot contain dropped events.");
		return false;
	}
	TMap<FString, int32> KindCounts;
	TMap<FString, int32> CompletionIndex;
	TMap<FString, int32> ArrivalIndex;
	TArray<FString> LaunchOrder;
	int32 ReleaseIndex = INDEX_NONE;
	int32 CriterionIndex = INDEX_NONE;
	int32 InvocationStartedIndex = INDEX_NONE;
	int32 ResetIndex = INDEX_NONE;
	int32 OpeningBoundaryIndex = INDEX_NONE;
	int32 ClosingBoundaryIndex = INDEX_NONE;
	int32 FirstLaunchIndex = INDEX_NONE;
	int32 LaunchTick = INDEX_NONE;
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		const FBlueprintLensAsyncTraceEvent& Event = Events[Index];
		if (Event.TraceId != TraceId || Event.InstanceId != InstanceId
			|| Event.InvocationId != InvocationId || Event.ObservationIndex != Index
			|| Event.EventId != FString::Printf(TEXT("%s:event:%04d"), *TraceId, Index)
			|| Event.SourceOccurrenceId.IsEmpty() || Event.WorldTick < 0)
		{
			OutError = FString::Printf(TEXT("LC4-ASYNC trace identity or event order is invalid at index %d."), Index);
			return false;
		}
		++KindCounts.FindOrAdd(Event.EventKind);
		if (Event.EventKind == TEXT("trace_boundary"))
		{
			if (Event.BoundaryPhase == TEXT("open"))
			{
				OpeningBoundaryIndex = Index;
			}
			else if (Event.BoundaryPhase == TEXT("close"))
			{
				ClosingBoundaryIndex = Index;
			}
			else
			{
				OutError = TEXT("LC4-ASYNC trace boundary phase must be open or close.");
				return false;
			}
		}
		else if (!Event.BoundaryPhase.IsEmpty())
		{
			OutError = TEXT("Only trace-boundary events may carry a boundary phase.");
			return false;
		}
		if (Event.EventKind == TEXT("invocation_started"))
		{
			InvocationStartedIndex = Index;
		}
		else if (Event.EventKind == TEXT("reset"))
		{
			ResetIndex = Index;
		}
		if (Event.EventKind == TEXT("launch"))
		{
			FirstLaunchIndex = FirstLaunchIndex == INDEX_NONE ? Index : FirstLaunchIndex;
			LaunchOrder.Add(Event.ContinuationId);
			LaunchTick = LaunchTick == INDEX_NONE ? Event.WorldTick : LaunchTick;
			if (Event.WorldTick != LaunchTick || !Event.ParticipantId.IsEmpty())
			{
				OutError = TEXT("LC4-ASYNC launches must share one tick and bind through continuation identity.");
				return false;
			}
		}
		else if (Event.EventKind == TEXT("completion"))
		{
			if (Event.ContinuationId.IsEmpty() || !Event.ParticipantId.IsEmpty()
				|| CompletionIndex.Contains(Event.ContinuationId))
			{
				OutError = TEXT("LC4-ASYNC completion correlation is missing or duplicated.");
				return false;
			}
			CompletionIndex.Add(Event.ContinuationId, Index);
		}
		else if (Event.EventKind == TEXT("barrier_arrival"))
		{
			if (Event.ContinuationId != Event.ParticipantId || Event.ParticipantId.IsEmpty()
				|| ArrivalIndex.Contains(Event.ParticipantId))
			{
				OutError = TEXT("LC4-ASYNC arrival correlation is missing or duplicated.");
				return false;
			}
			ArrivalIndex.Add(Event.ParticipantId, Index);
		}
		else if (Event.EventKind == TEXT("barrier_release"))
		{
			ReleaseIndex = Index;
		}
		else if (Event.EventKind == TEXT("criterion"))
		{
			CriterionIndex = Index;
		}
	}

	const bool bCountsValid = KindCounts.FindRef(TEXT("trace_boundary")) == 2
		&& KindCounts.FindRef(TEXT("invocation_started")) == 1
		&& KindCounts.FindRef(TEXT("reset")) <= 1
		&& KindCounts.FindRef(TEXT("launch")) == 2
		&& KindCounts.FindRef(TEXT("completion")) == 2
		&& KindCounts.FindRef(TEXT("barrier_arrival")) == 2
		&& KindCounts.FindRef(TEXT("barrier_release")) == 1
		&& KindCounts.FindRef(TEXT("criterion")) == 1
		&& KindCounts.Num() == (ResetIndex == INDEX_NONE ? 7 : 8);
	if (!bCountsValid || OpeningBoundaryIndex != 0 || ClosingBoundaryIndex != Events.Num() - 1
		|| InvocationStartedIndex <= OpeningBoundaryIndex
		|| FirstLaunchIndex <= InvocationStartedIndex
		|| (ResetIndex != INDEX_NONE && (ResetIndex != OpeningBoundaryIndex + 1
			|| InvocationStartedIndex != ResetIndex + 1))
		|| LaunchOrder != TArray<FString>({TEXT("A"), TEXT("B")})
		|| !CompletionIndex.Contains(TEXT("A")) || !CompletionIndex.Contains(TEXT("B"))
		|| !ArrivalIndex.Contains(TEXT("A")) || !ArrivalIndex.Contains(TEXT("B"))
		|| CompletionIndex[TEXT("A")] >= ArrivalIndex[TEXT("A")]
		|| CompletionIndex[TEXT("B")] >= ArrivalIndex[TEXT("B")]
		|| ReleaseIndex <= ArrivalIndex[TEXT("A")] || ReleaseIndex <= ArrivalIndex[TEXT("B")]
		|| CriterionIndex <= ReleaseIndex || ClosingBoundaryIndex <= CriterionIndex)
	{
		OutError = TEXT("LC4-ASYNC positive trace event contract is incomplete or out of order.");
		return false;
	}
	const int32 CompletionA = CompletionIndex[TEXT("A")];
	const int32 CompletionB = CompletionIndex[TEXT("B")];
	if ((ScheduleVariant == TEXT("A_FIRST") && CompletionA >= CompletionB)
		|| (ScheduleVariant == TEXT("B_FIRST") && CompletionB >= CompletionA))
	{
		OutError = TEXT("LC4-ASYNC completion order does not match the schedule variant.");
		return false;
	}
	if (LaunchTick == INDEX_NONE || Events[ClosingBoundaryIndex].WorldTick - LaunchTick > 8)
	{
		OutError = TEXT("LC4-ASYNC complete trace exceeded its eight-tick deadline.");
		return false;
	}
	return true;
}
