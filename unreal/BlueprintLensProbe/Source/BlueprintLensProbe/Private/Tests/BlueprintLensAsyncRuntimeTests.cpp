// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncBarrier.h"
#include "BlueprintLensAsyncTrace.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensAsyncBarrierLifecycleTest,
	"BlueprintLens.Async.BarrierLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensAsyncBarrierLifecycleTest::RunTest(const FString&)
{
	FBlueprintLensAsyncBarrierState Barrier;
	FString Error;
	const TArray<FName> Participants = {TEXT("A"), TEXT("B")};
	TestTrue(TEXT("begin invocation"), Barrier.BeginInvocation(TEXT("invocation-1"), Participants, Error));
	TestEqual(TEXT("participant count"), Barrier.GetParticipantCount(), 2);
	TestEqual(
		TEXT("first participant waits"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("A")),
		EBlueprintLensAsyncArrivalResult::Pending);
	TestEqual(
		TEXT("duplicate arrival rejected"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("A")),
		EBlueprintLensAsyncArrivalResult::Duplicate);
	TestEqual(
		TEXT("unknown participant rejected"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("C")),
		EBlueprintLensAsyncArrivalResult::Unknown);
	TestEqual(
		TEXT("second participant releases exactly once"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("B")),
		EBlueprintLensAsyncArrivalResult::Released);
	TestTrue(TEXT("barrier released"), Barrier.IsReleased());
	TestEqual(
		TEXT("arrival after release is closed"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("B")),
		EBlueprintLensAsyncArrivalResult::Closed);
	TestFalse(
		TEXT("released barrier cannot begin again without explicit reset"),
		Barrier.BeginInvocation(TEXT("invocation-reused"), Participants, Error));

	TestTrue(
		TEXT("explicit reset opens a new invocation"),
		Barrier.Reset(TEXT("invocation-1"), TEXT("invocation-2"), Participants, Error));
	TestFalse(TEXT("reset clears release"), Barrier.IsReleased());
	TestEqual(TEXT("reset clears arrivals"), Barrier.GetArrivedCount(), 0);
	TestEqual(
		TEXT("old invocation cannot cross reset seam"),
		Barrier.Arrive(TEXT("invocation-1"), TEXT("A")),
		EBlueprintLensAsyncArrivalResult::Closed);
	TestTrue(TEXT("cancel current invocation"), Barrier.Cancel(TEXT("invocation-2"), TEXT("test"), Error));
	TestTrue(TEXT("barrier cancelled"), Barrier.IsCancelled());
	TestEqual(
		TEXT("arrival after cancellation is closed"),
		Barrier.Arrive(TEXT("invocation-2"), TEXT("A")),
		EBlueprintLensAsyncArrivalResult::Closed);
	TestFalse(
		TEXT("cancelled barrier cannot begin again without explicit reset"),
		Barrier.BeginInvocation(TEXT("invocation-3"), Participants, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensAsyncTraceBoundsTest,
	"BlueprintLens.Async.TraceBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensAsyncTraceBoundsTest::RunTest(const FString&)
{
	FBlueprintLensAsyncTraceRecorder Recorder(3);
	FString Error;
	TestTrue(
		TEXT("open trace"),
		Recorder.Open(TEXT("trace-1"), TEXT("instance-1"), TEXT("invocation-1"), TEXT("A_FIRST"), Error));
	TestTrue(
		TEXT("record opening boundary"),
		Recorder.Record(
			TEXT("trace-source"),
			TEXT("trace_boundary"),
			0,
			FString(),
			FString(),
			Error,
			TEXT("open")));
	TestTrue(
		TEXT("record launch"),
		Recorder.Record(TEXT("launch-a"), TEXT("launch"), 0, TEXT("A"), FString(), Error));
	TestTrue(
		TEXT("record completion"),
		Recorder.Record(TEXT("delay-a"), TEXT("completion"), 2, TEXT("A"), FString(), Error));
	TestEqual(TEXT("three events recorded"), Recorder.GetEvents().Num(), 3);
	TestEqual(TEXT("first observation index"), Recorder.GetEvents()[0].ObservationIndex, 0);
	TestEqual(TEXT("third observation index"), Recorder.GetEvents()[2].ObservationIndex, 2);
	TestFalse(
		TEXT("overflow event rejected"),
		Recorder.Record(TEXT("arrival-a"), TEXT("barrier_arrival"), 2, TEXT("A"), TEXT("A"), Error));
	TestEqual(TEXT("overflow increments dropped count"), Recorder.GetDroppedEventCount(), 1);
	TestEqual(
		TEXT("overflow closes trace"),
		Recorder.GetCloseReason(),
		EBlueprintLensAsyncTraceCloseReason::Overflow);
	TestFalse(TEXT("overflow trace is not complete"), Recorder.IsComplete());

	FBlueprintLensAsyncTraceRecorder IncompleteRecorder(64);
	TestTrue(
		TEXT("open incomplete trace"),
		IncompleteRecorder.Open(TEXT("trace-incomplete"), TEXT("instance-1"), TEXT("invocation-incomplete"), TEXT("B_FIRST"), Error));
	TestTrue(
		TEXT("record only one incomplete event"),
		IncompleteRecorder.Record(TEXT("controller"), TEXT("invocation_started"), 0, FString(), FString(), Error));
	TestFalse(
		TEXT("incomplete trace cannot close complete"),
		IncompleteRecorder.Close(EBlueprintLensAsyncTraceCloseReason::Complete, Error));
	TestFalse(TEXT("incomplete event set is not complete"), IncompleteRecorder.IsComplete());

	FBlueprintLensAsyncTraceRecorder CompleteRecorder(64);
	TestTrue(
		TEXT("open complete trace"),
		CompleteRecorder.Open(TEXT("trace-2"), TEXT("instance-1"), TEXT("invocation-2"), TEXT("B_FIRST"), Error));
	auto RecordCompleteEvent = [this, &CompleteRecorder, &Error](
		const TCHAR* Source,
		const TCHAR* Kind,
		const int32 Tick,
		const TCHAR* Continuation,
		const TCHAR* Participant,
		const TCHAR* Boundary = TEXT(""))
	{
		TestTrue(
			FString::Printf(TEXT("record complete event %s"), Kind),
			CompleteRecorder.Record(Source, Kind, Tick, Continuation, Participant, Error, Boundary));
	};
	RecordCompleteEvent(TEXT("trace-source"), TEXT("trace_boundary"), 0, TEXT(""), TEXT(""), TEXT("open"));
	RecordCompleteEvent(TEXT("controller"), TEXT("invocation_started"), 0, TEXT(""), TEXT(""));
	RecordCompleteEvent(TEXT("launch-a"), TEXT("launch"), 0, TEXT("A"), TEXT(""));
	RecordCompleteEvent(TEXT("launch-b"), TEXT("launch"), 0, TEXT("B"), TEXT(""));
	RecordCompleteEvent(TEXT("delay-b"), TEXT("completion"), 1, TEXT("B"), TEXT(""));
	RecordCompleteEvent(TEXT("arrival-b"), TEXT("barrier_arrival"), 1, TEXT("B"), TEXT("B"));
	RecordCompleteEvent(TEXT("delay-a"), TEXT("completion"), 4, TEXT("A"), TEXT(""));
	RecordCompleteEvent(TEXT("arrival-a"), TEXT("barrier_arrival"), 4, TEXT("A"), TEXT("A"));
	RecordCompleteEvent(TEXT("release"), TEXT("barrier_release"), 4, TEXT(""), TEXT(""));
	RecordCompleteEvent(TEXT("criterion"), TEXT("criterion"), 4, TEXT(""), TEXT(""));
	RecordCompleteEvent(TEXT("trace-source"), TEXT("trace_boundary"), 4, TEXT(""), TEXT(""), TEXT("close"));
	TestTrue(
		TEXT("close structurally complete trace"),
		CompleteRecorder.Close(EBlueprintLensAsyncTraceCloseReason::Complete, Error));
	TestTrue(TEXT("complete close is complete"), CompleteRecorder.IsComplete());
	TestFalse(
		TEXT("closed trace rejects later events"),
		CompleteRecorder.Record(TEXT("late"), TEXT("launch"), 1, TEXT("A"), FString(), Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensAsyncNegativeTraceLifecycleTest,
	"BlueprintLens.Async.NegativeTraceLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensAsyncNegativeTraceLifecycleTest::RunTest(const FString&)
{
	FBlueprintLensLC4AsyncTraceBindings Bindings;
	Bindings.InvocationControllerId = TEXT("source-controller");
	Bindings.BarrierSiteId = TEXT("source-barrier");
	Bindings.BarrierReleaseId = TEXT("source-release");
	Bindings.CriterionId = TEXT("source-criterion");
	for (const FName Participant : {FName(TEXT("A")), FName(TEXT("B"))})
	{
		Bindings.LaunchIds.Add(Participant, FString::Printf(TEXT("source-launch-%s"), *Participant.ToString()));
		Bindings.CompletionIds.Add(Participant, FString::Printf(TEXT("source-completion-%s"), *Participant.ToString()));
		Bindings.ArrivalIds.Add(Participant, FString::Printf(TEXT("source-arrival-%s"), *Participant.ToString()));
	}
	FString Error;
	const FString NegativeRunId = FString::Printf(
		TEXT("negative-%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	TestTrue(
		TEXT("configure negative trace"),
		UBlueprintLensAsyncBarrierLibrary::ConfigureLC4AsyncTrace(TEXT("A_FIRST"), NegativeRunId, Bindings, Error));
	UBlueprintLensAsyncBarrierLibrary::BeginLC4AsyncInvocation(nullptr, TEXT("negative-invocation"));
	UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncLaunch(nullptr, TEXT("negative-invocation"), TEXT("A"));
	UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncLaunch(nullptr, TEXT("negative-invocation"), TEXT("B"));
	EBlueprintLensAsyncBarrierArrival Result = EBlueprintLensAsyncBarrierArrival::Closed;
	UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(nullptr, TEXT("negative-invocation"), TEXT("A"), Result);
	TestEqual(TEXT("first negative arrival waits"), Result, EBlueprintLensAsyncBarrierArrival::Pending);
	UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(nullptr, TEXT("negative-invocation"), TEXT("A"), Result);
	TestEqual(TEXT("duplicate negative arrival classified"), Result, EBlueprintLensAsyncBarrierArrival::Duplicate);
	UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(nullptr, TEXT("negative-invocation"), TEXT("C"), Result);
	TestEqual(TEXT("unknown negative arrival classified"), Result, EBlueprintLensAsyncBarrierArrival::Unknown);
	TestTrue(
		TEXT("cancel negative invocation"),
		UBlueprintLensAsyncBarrierLibrary::CancelLC4AsyncInvocation(
			nullptr, TEXT("negative-invocation"), TEXT("negative-control"), Error));
	FString NegativePath;
	TestTrue(
		TEXT("save cancelled negative trace"),
		UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(nullptr, NegativePath, Error, false));

	FString JsonText;
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("cancelled trace readable"), FFileHelper::LoadFileToString(JsonText, *NegativePath))
		&& TestTrue(
			TEXT("cancelled trace parses"),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root))
		&& TestNotNull(TEXT("cancelled trace root"), Root.Get()))
	{
		TestEqual(TEXT("cancelled close reason"), Root->GetStringField(TEXT("close_reason")), TEXT("cancelled"));
		TestFalse(TEXT("cancelled trace never complete"), Root->GetBoolField(TEXT("complete")));
		TMap<FString, int32> Counts;
		for (const TSharedPtr<FJsonValue>& Event : Root->GetArrayField(TEXT("events")))
		{
			++Counts.FindOrAdd(Event->AsObject()->GetStringField(TEXT("event_kind")));
		}
		TestEqual(TEXT("one duplicate-arrival event"), Counts.FindRef(TEXT("duplicate_arrival")), 1);
		TestEqual(TEXT("one unknown-arrival event"), Counts.FindRef(TEXT("unknown_arrival")), 1);
		TestEqual(TEXT("one cancelled event"), Counts.FindRef(TEXT("cancelled")), 1);
	}
	if (TestTrue(
		TEXT("cancelled invocation explicitly resets to a new identity"),
		UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncInvocation(
			nullptr, TEXT("negative-invocation"), TEXT("negative-invocation-2"), Error)))
	{
		UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncLaunch(nullptr, TEXT("negative-invocation-2"), TEXT("A"));
		UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncLaunch(nullptr, TEXT("negative-invocation-2"), TEXT("B"));
		UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(nullptr, TEXT("negative-invocation-2"), TEXT("A"), Result);
		UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(nullptr, TEXT("negative-invocation-2"), TEXT("B"), Result);
		UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncCriterion(nullptr, TEXT("negative-invocation-2"));
		FString ResetTracePath;
		TestTrue(
			TEXT("save complete post-reset trace"),
			UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(nullptr, ResetTracePath, Error));
		JsonText.Reset();
		Root.Reset();
		if (TestTrue(TEXT("post-reset trace readable"), FFileHelper::LoadFileToString(JsonText, *ResetTracePath))
			&& TestTrue(
				TEXT("post-reset trace parses"),
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root))
			&& TestNotNull(TEXT("post-reset trace root"), Root.Get()))
		{
			TestEqual(
				TEXT("post-reset source invocation identity is new"),
				Root->GetStringField(TEXT("source_invocation_id")),
				TEXT("negative-invocation-2"));
			TestTrue(
				TEXT("post-reset observed invocation binds run, instance and source identity"),
				Root->GetStringField(TEXT("invocation_id")).Contains(NegativeRunId)
					&& Root->GetStringField(TEXT("invocation_id")).Contains(TEXT("test-instance"))
					&& Root->GetStringField(TEXT("invocation_id")).EndsWith(TEXT("negative-invocation-2")));
			int32 ResetCount = 0;
			for (const TSharedPtr<FJsonValue>& Event : Root->GetArrayField(TEXT("events")))
			{
				ResetCount += Event->AsObject()->GetStringField(TEXT("event_kind")) == TEXT("reset") ? 1 : 0;
			}
			TestEqual(TEXT("post-reset trace records one explicit reset"), ResetCount, 1);
		}
	}
	UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncTraceConfiguration();

	TestTrue(
		TEXT("configure timeout trace"),
		UBlueprintLensAsyncBarrierLibrary::ConfigureLC4AsyncTrace(
			TEXT("B_FIRST"),
			FString::Printf(TEXT("timeout-%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)),
			Bindings,
			Error));
	UBlueprintLensAsyncBarrierLibrary::BeginLC4AsyncInvocation(nullptr, TEXT("timeout-invocation"));
	TestFalse(
		TEXT("timeout before eight-tick deadline rejected"),
		UBlueprintLensAsyncBarrierLibrary::TimeoutLC4AsyncInvocation(nullptr, TEXT("timeout-invocation"), Error));
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		++GFrameCounter;
	}
	TestTrue(
		TEXT("timeout at eight-tick deadline closes invocation"),
		UBlueprintLensAsyncBarrierLibrary::TimeoutLC4AsyncInvocation(nullptr, TEXT("timeout-invocation"), Error));
	FString TimeoutPath;
	TestTrue(
		TEXT("save timeout negative trace"),
		UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(nullptr, TimeoutPath, Error, false));
	JsonText.Reset();
	Root.Reset();
	if (TestTrue(TEXT("timeout trace readable"), FFileHelper::LoadFileToString(JsonText, *TimeoutPath))
		&& TestTrue(
			TEXT("timeout trace parses"),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root))
		&& TestNotNull(TEXT("timeout trace root"), Root.Get()))
	{
		TestEqual(TEXT("timeout close reason"), Root->GetStringField(TEXT("close_reason")), TEXT("timeout"));
		TestFalse(TEXT("timeout trace never complete"), Root->GetBoolField(TEXT("complete")));
	}
	TestTrue(
		TEXT("timed-out invocation explicitly resets to new identity"),
		UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncInvocation(
			nullptr, TEXT("timeout-invocation"), TEXT("timeout-invocation-2"), Error));
	UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncTraceConfiguration();
	return !HasAnyErrors();
}

#endif
