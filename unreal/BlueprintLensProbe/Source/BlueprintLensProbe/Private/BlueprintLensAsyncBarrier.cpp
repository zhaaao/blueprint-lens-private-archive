// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncBarrier.h"

#include "BlueprintLensAsyncTrace.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	struct FFixtureAsyncSession
	{
		TUniquePtr<FBlueprintLensAsyncBarrierState> Barrier;
		TUniquePtr<FBlueprintLensAsyncTraceRecorder> Trace;
		FString InstanceId;
		FString SourceInvocationId;
		uint64 StartFrame = 0;
	};

	TMap<const UObject*, TUniquePtr<FFixtureAsyncSession>> FixtureSessions;
	FBlueprintLensLC4AsyncTraceBindings FixtureTraceBindings;
	FString FixtureScheduleVariant;
	FString FixtureRunId;
	bool bFixtureTraceConfigured = false;

	FString CloseReasonString(const EBlueprintLensAsyncTraceCloseReason Reason)
	{
		switch (Reason)
		{
		case EBlueprintLensAsyncTraceCloseReason::Complete:
			return TEXT("complete");
		case EBlueprintLensAsyncTraceCloseReason::Cancelled:
			return TEXT("cancelled");
		case EBlueprintLensAsyncTraceCloseReason::Timeout:
			return TEXT("timeout");
		case EBlueprintLensAsyncTraceCloseReason::Overflow:
			return TEXT("overflow");
		default:
			return TEXT("none");
		}
	}

	FFixtureAsyncSession* FindSession(const UObject* WorldContextObject)
	{
		const TUniquePtr<FFixtureAsyncSession>* Session = FixtureSessions.Find(WorldContextObject);
		return Session == nullptr ? nullptr : Session->Get();
	}

	FString MakeInstanceId(const UObject* WorldContextObject)
	{
		return WorldContextObject == nullptr ? TEXT("test-instance") : WorldContextObject->GetPathName();
	}

	FString MakeObservedInvocationId(const FFixtureAsyncSession& Session)
	{
		return FString::Printf(
			TEXT("%s:instance:%s:invocation:%s"),
			*FixtureRunId,
			*Session.InstanceId,
			*Session.SourceInvocationId);
	}

	int32 RelativeWorldTick(const FFixtureAsyncSession& Session)
	{
		return static_cast<int32>(GFrameCounter - Session.StartFrame);
	}
}

void UBlueprintLensAsyncBarrierLibrary::BeginLC4AsyncInvocation(
	UObject* WorldContextObject,
	const FName InvocationId)
{
	if (!bFixtureTraceConfigured)
	{
		FixtureSessions.Reset();
		return;
	}
	if (FindSession(WorldContextObject) != nullptr)
	{
		return;
	}
	TUniquePtr<FFixtureAsyncSession> NewSession = MakeUnique<FFixtureAsyncSession>();
	NewSession->Barrier = MakeUnique<FBlueprintLensAsyncBarrierState>();
	NewSession->Trace = MakeUnique<FBlueprintLensAsyncTraceRecorder>(64);
	NewSession->InstanceId = MakeInstanceId(WorldContextObject);
	NewSession->SourceInvocationId = InvocationId.ToString();
	NewSession->StartFrame = GFrameCounter;
	const FString ObservedInvocationId = MakeObservedInvocationId(*NewSession);
	FString Error;
	if (!NewSession->Barrier->BeginInvocation(
		ObservedInvocationId,
		{TEXT("A"), TEXT("B")},
		Error)
		|| !NewSession->Trace->Open(
			FString::Printf(TEXT("%s:trace:%s"), *FixtureRunId, *NewSession->InstanceId),
			NewSession->InstanceId,
			ObservedInvocationId,
			FixtureScheduleVariant,
			Error)
		|| !NewSession->Trace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("trace_boundary"),
			RelativeWorldTick(*NewSession),
			FString(),
			FString(),
			Error,
			TEXT("open"))
		|| !NewSession->Trace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("invocation_started"),
			RelativeWorldTick(*NewSession),
			FString(),
			FString(),
			Error))
	{
		return;
	}
	FixtureSessions.Add(WorldContextObject, MoveTemp(NewSession));
}

void UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncLaunch(
	UObject* WorldContextObject,
	const FName InvocationId,
	const FName ParticipantId)
{
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Trace || !Session->Trace->IsOpen()
		|| !Session->Barrier || Session->SourceInvocationId != InvocationId.ToString())
	{
		return;
	}
	FString Error;
	Session->Trace->Record(
		FixtureTraceBindings.LaunchIds.FindRef(ParticipantId),
		TEXT("launch"),
		RelativeWorldTick(*Session),
		ParticipantId.ToString(),
		FString(),
		Error);
}

void UBlueprintLensAsyncBarrierLibrary::ArriveAtLC4Barrier(
	UObject* WorldContextObject,
	const FName InvocationId,
	const FName ParticipantId,
	EBlueprintLensAsyncBarrierArrival& Result)
{
	Result = EBlueprintLensAsyncBarrierArrival::Closed;
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Barrier
		|| Session->SourceInvocationId != InvocationId.ToString())
	{
		return;
	}
	FString Error;
	const EBlueprintLensAsyncArrivalResult BarrierResult = Session->Barrier->Arrive(
		Session->Barrier->GetInvocationId(),
		ParticipantId);
	Result = static_cast<EBlueprintLensAsyncBarrierArrival>(BarrierResult);
	if (Session->Trace && Session->Trace->IsOpen())
	{
		if (BarrierResult == EBlueprintLensAsyncArrivalResult::Pending
			|| BarrierResult == EBlueprintLensAsyncArrivalResult::Released)
		{
			if (!Session->Trace->Record(
					FixtureTraceBindings.CompletionIds.FindRef(ParticipantId),
					TEXT("completion"),
					RelativeWorldTick(*Session),
					ParticipantId.ToString(),
					FString(),
					Error)
				|| !Session->Trace->Record(
					FixtureTraceBindings.ArrivalIds.FindRef(ParticipantId),
					TEXT("barrier_arrival"),
					RelativeWorldTick(*Session),
					ParticipantId.ToString(),
					ParticipantId.ToString(),
					Error))
			{
				return;
			}
		}
		else if (BarrierResult == EBlueprintLensAsyncArrivalResult::Duplicate)
		{
			Session->Trace->Record(
				FixtureTraceBindings.ArrivalIds.FindRef(ParticipantId),
				TEXT("duplicate_arrival"),
				RelativeWorldTick(*Session),
				ParticipantId.ToString(),
				ParticipantId.ToString(),
				Error);
		}
		else if (BarrierResult == EBlueprintLensAsyncArrivalResult::Unknown)
		{
			Session->Trace->Record(
				FixtureTraceBindings.BarrierSiteId,
				TEXT("unknown_arrival"),
				RelativeWorldTick(*Session),
				FString(),
				ParticipantId.ToString(),
				Error);
		}
	}
	if (BarrierResult == EBlueprintLensAsyncArrivalResult::Released
		&& Session->Trace && Session->Trace->IsOpen())
	{
		Session->Trace->Record(
			FixtureTraceBindings.BarrierReleaseId,
			TEXT("barrier_release"),
			RelativeWorldTick(*Session),
			FString(),
			FString(),
			Error);
	}
}

void UBlueprintLensAsyncBarrierLibrary::RecordLC4AsyncCriterion(
	UObject* WorldContextObject,
	const FName InvocationId)
{
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Trace || !Session->Trace->IsOpen()
		|| !Session->Barrier || Session->SourceInvocationId != InvocationId.ToString()
		|| !Session->Barrier->IsReleased())
	{
		return;
	}
	FString Error;
	Session->Trace->Record(
		FixtureTraceBindings.CriterionId,
		TEXT("criterion"),
		RelativeWorldTick(*Session),
		FString(),
		FString(),
		Error);
	if (!Session->Trace->Record(
			FixtureTraceBindings.CriterionId,
			TEXT("trace_boundary"),
			RelativeWorldTick(*Session),
			FString(),
			FString(),
			Error,
			TEXT("close")))
	{
		return;
	}
	Session->Trace->Close(EBlueprintLensAsyncTraceCloseReason::Complete, Error);
}

bool FBlueprintLensLC4AsyncTraceBindings::IsComplete(FString& OutError) const
{
	OutError.Reset();
	if (InvocationControllerId.IsEmpty() || BarrierSiteId.IsEmpty()
		|| BarrierReleaseId.IsEmpty() || CriterionId.IsEmpty())
	{
		OutError = TEXT("LC4-ASYNC controller, barrier release and criterion source identities are required.");
		return false;
	}
	for (const FName Participant : {FName(TEXT("A")), FName(TEXT("B"))})
	{
		if (LaunchIds.FindRef(Participant).IsEmpty()
			|| CompletionIds.FindRef(Participant).IsEmpty()
			|| ArrivalIds.FindRef(Participant).IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("LC4-ASYNC source identities are incomplete for participant %s."),
				*Participant.ToString());
			return false;
		}
	}
	return true;
}

bool UBlueprintLensAsyncBarrierLibrary::ConfigureLC4AsyncTrace(
	const FString& ScheduleVariant,
	const FString& RunId,
	const FBlueprintLensLC4AsyncTraceBindings& Bindings,
	FString& OutError)
{
	OutError.Reset();
	if (!FixtureSessions.IsEmpty())
	{
		OutError = TEXT("Cannot reconfigure LC4-ASYNC while runtime sessions exist.");
		return false;
	}
	if (ScheduleVariant != TEXT("A_FIRST") && ScheduleVariant != TEXT("B_FIRST"))
	{
		OutError = FString::Printf(TEXT("Unsupported LC4-ASYNC schedule variant: %s"), *ScheduleVariant);
		return false;
	}
	if (RunId.IsEmpty())
	{
		OutError = TEXT("LC4-ASYNC run identity must be non-empty.");
		return false;
	}
	if (!Bindings.IsComplete(OutError))
	{
		return false;
	}
	FixtureTraceBindings = Bindings;
	FixtureScheduleVariant = ScheduleVariant;
	FixtureRunId = RunId;
	bFixtureTraceConfigured = true;
	return true;
}

void UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncTraceConfiguration()
{
	FixtureSessions.Reset();
	FixtureTraceBindings = FBlueprintLensLC4AsyncTraceBindings();
	FixtureScheduleVariant.Reset();
	FixtureRunId.Reset();
	bFixtureTraceConfigured = false;
}

bool UBlueprintLensAsyncBarrierLibrary::CancelLC4AsyncInvocation(
	UObject* WorldContextObject,
	const FString& InvocationId,
	const FString& Reason,
	FString& OutError)
{
	OutError.Reset();
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Barrier || !Session->Trace || !Session->Trace->IsOpen()
		|| Session->SourceInvocationId != InvocationId
		|| !Session->Barrier->Cancel(Session->Barrier->GetInvocationId(), Reason, OutError))
	{
		return false;
	}
	if (!Session->Trace->Record(
			FixtureTraceBindings.BarrierSiteId,
			TEXT("cancelled"),
			RelativeWorldTick(*Session),
			FString(),
			FString(),
			OutError)
		|| !Session->Trace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("trace_boundary"),
			RelativeWorldTick(*Session),
			FString(),
			FString(),
			OutError,
			TEXT("close")))
	{
		return false;
	}
	return Session->Trace->Close(EBlueprintLensAsyncTraceCloseReason::Cancelled, OutError);
}

bool UBlueprintLensAsyncBarrierLibrary::TimeoutLC4AsyncInvocation(
	UObject* WorldContextObject,
	const FString& InvocationId,
	FString& OutError)
{
	OutError.Reset();
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Barrier || !Session->Trace || !Session->Trace->IsOpen()
		|| Session->SourceInvocationId != InvocationId
		|| !Session->Barrier->IsOpen())
	{
		OutError = TEXT("Only the open LC4-ASYNC invocation can time out.");
		return false;
	}
	if (RelativeWorldTick(*Session) < 8)
	{
		OutError = TEXT("LC4-ASYNC invocation cannot time out before the eight-tick deadline.");
		return false;
	}
	if (!Session->Barrier->Cancel(Session->Barrier->GetInvocationId(), TEXT("timeout"), OutError))
	{
		return false;
	}
	if (!Session->Trace->Record(
			FixtureTraceBindings.BarrierSiteId,
			TEXT("trace_boundary"),
			RelativeWorldTick(*Session),
			FString(),
			FString(),
			OutError,
			TEXT("close")))
	{
		return false;
	}
	return Session->Trace->Close(EBlueprintLensAsyncTraceCloseReason::Timeout, OutError);
}

bool UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncInvocation(
	UObject* WorldContextObject,
	const FString& PreviousInvocationId,
	const FString& NewInvocationId,
	FString& OutError)
{
	OutError.Reset();
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	if (Session == nullptr || !Session->Barrier || !Session->Trace || !Session->Trace->IsClosed())
	{
		OutError = TEXT("Only a closed LC4-ASYNC invocation can be reset.");
		return false;
	}
	const FString InstanceId = Session->Trace->GetInstanceId();
	if (Session->SourceInvocationId != PreviousInvocationId)
	{
		OutError = TEXT("LC4-ASYNC reset previous source invocation identity does not match.");
		return false;
	}
	const FString PreviousObservedInvocationId = Session->Barrier->GetInvocationId();
	Session->SourceInvocationId = NewInvocationId;
	const FString NewObservedInvocationId = MakeObservedInvocationId(*Session);
	if (!Session->Barrier->Reset(
		PreviousObservedInvocationId,
		NewObservedInvocationId,
		{TEXT("A"), TEXT("B")},
		OutError))
	{
		Session->SourceInvocationId = PreviousInvocationId;
		return false;
	}

	TUniquePtr<FBlueprintLensAsyncTraceRecorder> NewTrace =
		MakeUnique<FBlueprintLensAsyncTraceRecorder>(64);
	if (!NewTrace->Open(
		FString::Printf(TEXT("%s:trace:%s:reset:%s"), *FixtureRunId, *InstanceId, *NewInvocationId),
		InstanceId,
		NewObservedInvocationId,
		FixtureScheduleVariant,
		OutError)
		|| !NewTrace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("trace_boundary"),
			0,
			FString(),
			FString(),
			OutError,
			TEXT("open"))
		|| !NewTrace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("reset"),
			0,
			FString(),
			FString(),
			OutError)
		|| !NewTrace->Record(
			FixtureTraceBindings.InvocationControllerId,
			TEXT("invocation_started"),
			0,
			FString(),
			FString(),
			OutError))
	{
		return false;
	}
	Session->Trace = MoveTemp(NewTrace);
	Session->StartFrame = GFrameCounter;
	return true;
}

bool UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(
	UObject* WorldContextObject,
	FString& OutFilePath,
	FString& OutError,
	const bool bRequireComplete)
{
	OutFilePath.Reset();
	OutError.Reset();
	FFixtureAsyncSession* Session = FindSession(WorldContextObject);
	FBlueprintLensAsyncTraceRecorder* Trace = Session == nullptr ? nullptr : Session->Trace.Get();
	if (Trace == nullptr || (bRequireComplete ? !Trace->IsComplete() : !Trace->IsClosed()))
	{
		OutError = TEXT("LC4-ASYNC trace is absent or incomplete.");
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-async-trace"));
	Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("run_id"), FixtureRunId);
	Root->SetStringField(TEXT("source_invocation_id"), Session->SourceInvocationId);
	Root->SetStringField(TEXT("trace_id"), Trace->GetTraceId());
	Root->SetStringField(TEXT("instance_id"), Trace->GetInstanceId());
	Root->SetStringField(TEXT("invocation_id"), Trace->GetInvocationId());
	Root->SetStringField(TEXT("schedule_variant"), Trace->GetScheduleVariant());
	Root->SetNumberField(TEXT("recording_limit"), Trace->GetCapacity());
	Root->SetNumberField(TEXT("dropped_event_count"), Trace->GetDroppedEventCount());
	Root->SetStringField(TEXT("close_reason"), CloseReasonString(Trace->GetCloseReason()));
	Root->SetBoolField(TEXT("complete"), Trace->IsComplete());
	TArray<TSharedPtr<FJsonValue>> EventValues;
	for (const FBlueprintLensAsyncTraceEvent& Event : Trace->GetEvents())
	{
		TSharedPtr<FJsonObject> EventObject = MakeShared<FJsonObject>();
		EventObject->SetStringField(TEXT("trace_id"), Event.TraceId);
		EventObject->SetStringField(TEXT("instance_id"), Event.InstanceId);
		EventObject->SetStringField(TEXT("invocation_id"), Event.InvocationId);
		EventObject->SetStringField(TEXT("event_id"), Event.EventId);
		EventObject->SetStringField(TEXT("source_occurrence_id"), Event.SourceOccurrenceId);
		EventObject->SetStringField(TEXT("event_kind"), Event.EventKind);
		EventObject->SetStringField(TEXT("boundary_phase"), Event.BoundaryPhase);
		EventObject->SetStringField(TEXT("continuation_id"), Event.ContinuationId);
		EventObject->SetStringField(TEXT("participant_id"), Event.ParticipantId);
		EventObject->SetNumberField(TEXT("observation_index"), Event.ObservationIndex);
		EventObject->SetNumberField(TEXT("world_tick"), Event.WorldTick);
		EventValues.Add(MakeShared<FJsonValueObject>(EventObject));
	}
	Root->SetArrayField(TEXT("events"), EventValues);

	FString JsonText;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OutError = TEXT("Could not serialize LC4-ASYNC runtime trace.");
		return false;
	}
	const FString OutputDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("BlueprintLens"),
		TEXT("AsyncTraces"));
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		OutError = FString::Printf(TEXT("Could not create async trace directory: %s"), *OutputDirectory);
		return false;
	}
	const FString FileStem = Trace->IsComplete()
		? Trace->GetScheduleVariant()
		: FString::Printf(
			TEXT("%s.%s"),
			*Trace->GetScheduleVariant(),
			*CloseReasonString(Trace->GetCloseReason()));
	const FString InstanceStem = FMD5::HashAnsiString(*Trace->GetInstanceId());
	OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		OutputDirectory,
		FString::Printf(
			TEXT("%s.%s.%s.async-trace.json"),
			*FixtureRunId,
			*InstanceStem,
			*FileStem)));
	if (!FFileHelper::SaveStringToFile(
		JsonText,
		*OutFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Could not write async trace: %s"), *OutFilePath);
		return false;
	}
	return true;
}

bool FBlueprintLensAsyncBarrierState::ValidateNewInvocation(
	const FString& InInvocationId,
	const TArray<FName>& InParticipants,
	FString& OutError) const
{
	if (InInvocationId.IsEmpty())
	{
		OutError = TEXT("Invocation id must not be empty.");
		return false;
	}
	if (InParticipants.IsEmpty())
	{
		OutError = TEXT("Participant set must not be empty.");
		return false;
	}
	TSet<FName> UniqueParticipants;
	for (const FName Participant : InParticipants)
	{
		if (Participant.IsNone() || UniqueParticipants.Contains(Participant))
		{
			OutError = FString::Printf(
				TEXT("Participants must be non-empty and unique: %s"),
				*Participant.ToString());
			return false;
		}
		UniqueParticipants.Add(Participant);
	}
	return true;
}

void FBlueprintLensAsyncBarrierState::OpenInvocation(
	const FString& InInvocationId,
	const TArray<FName>& InParticipants)
{
	InvocationId = InInvocationId;
	Participants.Reset();
	for (const FName Participant : InParticipants)
	{
		Participants.Add(Participant);
	}
	ArrivedParticipants.Reset();
	CancellationReason.Reset();
	bOpen = true;
	bReleased = false;
	bCancelled = false;
}

bool FBlueprintLensAsyncBarrierState::BeginInvocation(
	const FString& InInvocationId,
	const TArray<FName>& InParticipants,
	FString& OutError)
{
	OutError.Reset();
	if (!InvocationId.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("Invocation already exists and requires explicit reset: %s"),
			*InvocationId);
		return false;
	}
	if (!ValidateNewInvocation(InInvocationId, InParticipants, OutError))
	{
		return false;
	}
	OpenInvocation(InInvocationId, InParticipants);
	return true;
}

EBlueprintLensAsyncArrivalResult FBlueprintLensAsyncBarrierState::Arrive(
	const FString& InInvocationId,
	const FName ParticipantId)
{
	if (!bOpen || InInvocationId != InvocationId)
	{
		return EBlueprintLensAsyncArrivalResult::Closed;
	}
	if (!Participants.Contains(ParticipantId))
	{
		return EBlueprintLensAsyncArrivalResult::Unknown;
	}
	if (ArrivedParticipants.Contains(ParticipantId))
	{
		return EBlueprintLensAsyncArrivalResult::Duplicate;
	}

	ArrivedParticipants.Add(ParticipantId);
	if (ArrivedParticipants.Num() == Participants.Num())
	{
		bOpen = false;
		bReleased = true;
		return EBlueprintLensAsyncArrivalResult::Released;
	}
	return EBlueprintLensAsyncArrivalResult::Pending;
}

bool FBlueprintLensAsyncBarrierState::Cancel(
	const FString& InInvocationId,
	const FString& Reason,
	FString& OutError)
{
	OutError.Reset();
	if (!bOpen || InInvocationId != InvocationId)
	{
		OutError = FString::Printf(
			TEXT("Only the open invocation can be cancelled: %s"),
			*InInvocationId);
		return false;
	}
	bOpen = false;
	bCancelled = true;
	CancellationReason = Reason;
	return true;
}

bool FBlueprintLensAsyncBarrierState::Reset(
	const FString& PreviousInvocationId,
	const FString& NewInvocationId,
	const TArray<FName>& InParticipants,
	FString& OutError)
{
	OutError.Reset();
	if (PreviousInvocationId != InvocationId)
	{
		OutError = FString::Printf(
			TEXT("Reset invocation mismatch: expected %s, received %s"),
			*InvocationId,
			*PreviousInvocationId);
		return false;
	}
	if (bOpen)
	{
		OutError = TEXT("Open invocation must be cancelled or released before reset.");
		return false;
	}
	if (NewInvocationId == PreviousInvocationId)
	{
		OutError = TEXT("Reset must create a new invocation identity.");
		return false;
	}
	if (!ValidateNewInvocation(NewInvocationId, InParticipants, OutError))
	{
		return false;
	}
	OpenInvocation(NewInvocationId, InParticipants);
	return true;
}
