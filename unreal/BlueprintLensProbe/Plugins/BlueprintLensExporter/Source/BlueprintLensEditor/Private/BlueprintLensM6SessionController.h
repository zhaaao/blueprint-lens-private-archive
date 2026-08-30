// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6Preflight.h"
#include "BlueprintLensM6ProcessRunner.h"
#include "BlueprintLensM6SessionPacket.h"
#include "BlueprintLensM6Telemetry.h"
#include "CoreMinimal.h"

enum class EM6SessionState : uint8
{
	Idle,
	Preflight,
	Exporting,
	Running,
	Validating,
	Ready,
	Failed,
	Cancelling
};

struct FM6ExportPreparation
{
	bool bSucceeded = false;
	FM6Error Error;
	FM6ProcessInvocation Invocation;
	FString PacketDirectory;
	FString ExpectedSourceFingerprint;
};

struct FM6SessionSnapshot
{
	EM6SessionState State = EM6SessionState::Idle;
	EM6Baseline Baseline = EM6Baseline::A;
	FString SelectedEntityId;
	bool bHasReadySession = false;
	bool bReadySessionStale = false;
	bool bHasPendingRequest = false;
	uint64 Generation = 0;
	FM6Error Error;
};

class IM6PreflightProvider
{
public:
	virtual ~IM6PreflightProvider() = default;
	virtual FM6PreflightResult Evaluate(const FM6QueryInput& Query) = 0;
};

class IM6ExportProvider
{
public:
	virtual ~IM6ExportProvider() = default;
	virtual FM6ExportPreparation Prepare(
		const FM6PreflightResult& Preflight) = 0;
};

class IM6PacketProvider
{
public:
	virtual ~IM6PacketProvider() = default;
	virtual FM6SessionPacketLoadResult Load(
		const FString& PacketDirectory,
		const FString& ExpectedSourceFingerprint) = 0;
};

class IM6SessionTelemetry
{
public:
	virtual ~IM6SessionTelemetry() = default;
	virtual void RecordState(EM6SessionState State, const FM6Error& Error) = 0;
	virtual void BeginStage(const FString&, bool) {}
	virtual void FinishStage(const FString&, const FM6Error&) {}
	virtual void ActivatePacket(const FM6LoadedSessionPacket&) {}
	virtual void RecordBaseline(EM6Baseline Baseline) = 0;
	virtual void RecordSelection(
		const FString& EntityId,
		EM6SelectionOrigin Origin) = 0;
	virtual void RecordReset() = 0;
	virtual void Seal() = 0;
};

class IM6SessionView
{
public:
	virtual ~IM6SessionView() = default;
	virtual void Present(const FM6SessionSnapshot& Snapshot) = 0;
};

class FM6SessionController
{
public:
	FM6SessionController(
		IM6PreflightProvider& InPreflight,
		IM6ExportProvider& InExporter,
		IM6ProcessRunner& InRunner,
		IM6PacketProvider& InLoader,
		IM6SessionTelemetry& InTelemetry,
		IM6SessionView& InView);

	void Run(const FM6QueryInput& Query);
	void Cancel();
	void Reset();
	void SelectBaseline(EM6Baseline Baseline);
	void SelectEntity(const FString& EntityId, EM6SelectionOrigin Origin);
	void Tick(double NowSeconds);

	const FM6SessionSnapshot& GetSnapshot() const { return Snapshot; }
	const FM6LoadedSessionPacket* GetReadyPacket() const
	{
		return ReadyPacket.IsSet() ? &ReadyPacket.GetValue() : nullptr;
	}

private:
	void Transition(EM6SessionState State, const FM6Error& Error = FM6Error());
	void Publish();
	void OnProcessComplete(uint64 Generation, const FM6ProcessResult& Result);
	void HandleProcessResult();
	void FinishCancellation();
	void ClearToIdle(bool bSeal);
	bool IsActiveWork() const;

	IM6PreflightProvider& Preflight;
	IM6ExportProvider& Exporter;
	IM6ProcessRunner& Runner;
	IM6PacketProvider& Loader;
	IM6SessionTelemetry& Telemetry;
	IM6SessionView& View;

	FM6SessionSnapshot Snapshot;
	FM6QueryInput PendingQuery;
	FM6PreflightResult PendingPreflight;
	FM6ExportPreparation PendingExport;
	TOptional<FM6ProcessResult> PendingProcessResult;
	TOptional<FM6LoadedSessionPacket> ReadyPacket;
	bool bResetPending = false;
	bool bCancelRequiresRunner = false;
	bool bPublishing = false;
};
