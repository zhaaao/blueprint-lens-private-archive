// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6SessionController.h"

namespace BlueprintLensM6SessionController
{
namespace
{
FM6Error MakeError(const TCHAR* Code, const TCHAR* Phase, const TCHAR* Message)
{
	FM6Error Error;
	Error.Code = Code;
	Error.Phase = Phase;
	Error.Message = Message;
	Error.bRetryable = false;
	return Error;
}
} // namespace
} // namespace BlueprintLensM6SessionController

FM6SessionController::FM6SessionController(
	IM6PreflightProvider& InPreflight,
	IM6ExportProvider& InExporter,
	IM6ProcessRunner& InRunner,
	IM6PacketProvider& InLoader,
	IM6SessionTelemetry& InTelemetry,
	IM6SessionView& InView)
	: Preflight(InPreflight)
	, Exporter(InExporter)
	, Runner(InRunner)
	, Loader(InLoader)
	, Telemetry(InTelemetry)
	, View(InView)
{
	Publish();
}

void FM6SessionController::Run(const FM6QueryInput& Query)
{
	if (IsActiveWork())
	{
		Snapshot.Error = BlueprintLensM6SessionController::MakeError(
			TEXT("M6_PRECONDITION_QUERY_INVALID"), TEXT("preflight"),
			TEXT("one M6 request is already active"));
		Publish();
		return;
	}
	PendingQuery = Query;
	PendingPreflight = FM6PreflightResult();
	PendingExport = FM6ExportPreparation();
	PendingProcessResult.Reset();
	bResetPending = false;
	bCancelRequiresRunner = false;
	++Snapshot.Generation;
	Snapshot.bHasPendingRequest = true;
	Snapshot.bReadySessionStale = ReadyPacket.IsSet();
	Telemetry.BeginStage(TEXT("request"), true);
	Telemetry.FinishStage(TEXT("request"), FM6Error());
	Transition(EM6SessionState::Preflight);
}

void FM6SessionController::Cancel()
{
	if (!IsActiveWork() || Snapshot.State == EM6SessionState::Cancelling) return;
	bResetPending = false;
	bCancelRequiresRunner = Snapshot.State == EM6SessionState::Running && Runner.IsActive();
	Transition(EM6SessionState::Cancelling);
	if (bCancelRequiresRunner) Runner.Cancel();
}

void FM6SessionController::Reset()
{
	if (IsActiveWork())
	{
		bResetPending = true;
		bCancelRequiresRunner = Snapshot.State == EM6SessionState::Running && Runner.IsActive();
		Transition(EM6SessionState::Cancelling);
		if (bCancelRequiresRunner) Runner.Cancel();
		return;
	}
	ClearToIdle(true);
}

void FM6SessionController::SelectBaseline(const EM6Baseline Baseline)
{
	if (Snapshot.State != EM6SessionState::Ready || !ReadyPacket.IsSet() ||
		Snapshot.Baseline == Baseline) return;
	Snapshot.Baseline = Baseline;
	Telemetry.RecordBaseline(Baseline);
	Publish();
}

void FM6SessionController::SelectEntity(
	const FString& EntityId,
	const EM6SelectionOrigin Origin)
{
	if (bPublishing || Snapshot.State != EM6SessionState::Ready ||
		!ReadyPacket.IsSet() || EntityId.IsEmpty() ||
		Snapshot.SelectedEntityId == EntityId) return;
	Snapshot.SelectedEntityId = EntityId;
	Telemetry.RecordSelection(EntityId, Origin);
	Publish();
}

void FM6SessionController::Tick(const double NowSeconds)
{
	switch (Snapshot.State)
	{
	case EM6SessionState::Preflight:
		Telemetry.BeginStage(TEXT("preflight"), true);
		PendingPreflight = Preflight.Evaluate(PendingQuery);
		Telemetry.FinishStage(
			TEXT("preflight"),
			PendingPreflight.bSucceeded ? FM6Error() : PendingPreflight.Error);
		if (!PendingPreflight.bSucceeded)
			Transition(EM6SessionState::Failed, PendingPreflight.Error);
		else
			Transition(EM6SessionState::Exporting);
		break;
	case EM6SessionState::Exporting:
		Telemetry.BeginStage(TEXT("export"), true);
		PendingExport = Exporter.Prepare(PendingPreflight);
		Telemetry.FinishStage(
			TEXT("export"),
			PendingExport.bSucceeded ? FM6Error() : PendingExport.Error);
		if (!PendingExport.bSucceeded)
			Transition(EM6SessionState::Failed, PendingExport.Error);
		else
		{
			const uint64 Generation = Snapshot.Generation;
			Transition(EM6SessionState::Running);
			Runner.Start(
				PendingExport.Invocation,
				[this, Generation](const FM6ProcessResult& Result)
				{
					OnProcessComplete(Generation, Result);
				});
		}
		break;
	case EM6SessionState::Running:
		Runner.Tick(NowSeconds);
		if (PendingProcessResult.IsSet()) HandleProcessResult();
		break;
	case EM6SessionState::Validating:
	{
		Telemetry.BeginStage(TEXT("packet"), true);
		FM6SessionPacketLoadResult LoadResult = Loader.Load(
			PendingExport.PacketDirectory,
			PendingExport.ExpectedSourceFingerprint);
		if (LoadResult.HasError())
		{
			Transition(EM6SessionState::Failed, LoadResult.GetError());
		}
		else
		{
			ReadyPacket = LoadResult.StealValue();
			Telemetry.ActivatePacket(*ReadyPacket);
			Snapshot.Baseline = EM6Baseline::A;
			Snapshot.SelectedEntityId.Reset();
			Snapshot.bHasPendingRequest = false;
			Snapshot.bReadySessionStale = false;
			Transition(EM6SessionState::Ready);
			Telemetry.FinishStage(TEXT("packet"), FM6Error());
		}
		break;
	}
	case EM6SessionState::Cancelling:
		if (bCancelRequiresRunner)
		{
			Runner.Tick(NowSeconds);
			if (PendingProcessResult.IsSet()) FinishCancellation();
		}
		else
		{
			FinishCancellation();
		}
		break;
	default:
		break;
	}
}

void FM6SessionController::Transition(
	const EM6SessionState State,
	const FM6Error& Error)
{
	Snapshot.State = State;
	Snapshot.Error = Error;
	Snapshot.bHasReadySession = ReadyPacket.IsSet();
	if (State == EM6SessionState::Failed)
	{
		Snapshot.bHasPendingRequest = false;
		Snapshot.bReadySessionStale = ReadyPacket.IsSet();
	}
	Telemetry.RecordState(State, Error);
	Publish();
}

void FM6SessionController::Publish()
{
	Snapshot.bHasReadySession = ReadyPacket.IsSet();
	bPublishing = true;
	View.Present(Snapshot);
	bPublishing = false;
}

void FM6SessionController::OnProcessComplete(
	const uint64 Generation,
	const FM6ProcessResult& Result)
{
	if (Generation != Snapshot.Generation) return;
	if (Snapshot.State != EM6SessionState::Running &&
		Snapshot.State != EM6SessionState::Cancelling) return;
	PendingProcessResult = Result;
}

void FM6SessionController::HandleProcessResult()
{
	const FM6ProcessResult Result = PendingProcessResult.GetValue();
	PendingProcessResult.Reset();
	if (!Result.bCleanupSucceeded)
	{
		Transition(
			EM6SessionState::Failed,
			Result.Error.IsSet()
				? Result.Error
				: BlueprintLensM6SessionController::MakeError(
					TEXT("M6_RUNNER_CLEANUP_FAILED"), TEXT("runner"),
					TEXT("owned child cleanup failed")));
		return;
	}
	if (Result.bCancelled || Result.bTimedOut || Result.ReturnCode != 0 ||
		Result.Error.IsSet())
	{
		FM6Error Error = Result.Error;
		if (!Error.IsSet())
		{
			Error = BlueprintLensM6SessionController::MakeError(
				Result.bTimedOut ? TEXT("M6_RUNNER_TIMEOUT")
					: (Result.bCancelled ? TEXT("M6_RUNNER_CANCELLED")
						: TEXT("M6_RUNNER_NONZERO_EXIT")),
				TEXT("runner"), TEXT("owned child did not complete successfully"));
		}
		Transition(EM6SessionState::Failed, Error);
		return;
	}
	Transition(EM6SessionState::Validating);
}

void FM6SessionController::FinishCancellation()
{
	FM6ProcessResult Result;
	if (PendingProcessResult.IsSet())
	{
		Result = PendingProcessResult.GetValue();
		PendingProcessResult.Reset();
		if (!Result.bCleanupSucceeded)
		{
			Transition(
				EM6SessionState::Failed,
				Result.Error.IsSet()
					? Result.Error
					: BlueprintLensM6SessionController::MakeError(
						TEXT("M6_RUNNER_CLEANUP_FAILED"), TEXT("runner"),
						TEXT("owned child cleanup failed")));
			return;
		}
	}
	if (bResetPending)
	{
		ClearToIdle(true);
		return;
	}
	Snapshot.bHasPendingRequest = false;
	Snapshot.bReadySessionStale = ReadyPacket.IsSet();
	Transition(ReadyPacket.IsSet() ? EM6SessionState::Ready : EM6SessionState::Idle);
}

void FM6SessionController::ClearToIdle(const bool bSeal)
{
	const uint64 NextGeneration = Snapshot.Generation + 1;
	if (bSeal)
	{
		Telemetry.BeginStage(TEXT("reset"), true);
		Telemetry.FinishStage(TEXT("reset"), FM6Error());
		Telemetry.RecordReset();
		Telemetry.Seal();
	}
	ReadyPacket.Reset();
	PendingProcessResult.Reset();
	PendingPreflight = FM6PreflightResult();
	PendingExport = FM6ExportPreparation();
	Snapshot = FM6SessionSnapshot();
	Snapshot.Generation = NextGeneration;
	bResetPending = false;
	bCancelRequiresRunner = false;
	Transition(EM6SessionState::Idle);
}

bool FM6SessionController::IsActiveWork() const
{
	return Snapshot.State == EM6SessionState::Preflight ||
		Snapshot.State == EM6SessionState::Exporting ||
		Snapshot.State == EM6SessionState::Running ||
		Snapshot.State == EM6SessionState::Validating ||
		Snapshot.State == EM6SessionState::Cancelling;
}
