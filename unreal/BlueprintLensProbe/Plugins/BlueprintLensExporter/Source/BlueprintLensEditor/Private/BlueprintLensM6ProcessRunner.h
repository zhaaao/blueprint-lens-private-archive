// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6Types.h"
#include "CoreMinimal.h"

struct FM6ProcessInvocation
{
	FString ExecutablePath;
	TArray<FString> Arguments;
	FString WorkingDirectory;
	FString PacketDirectory;
	double TimeoutSeconds = 120.0;
	int32 ExpectedPacketFiles = 7;
};

struct FM6ProcessResult
{
	bool bStarted = false;
	bool bTimedOut = false;
	bool bCancelled = false;
	bool bCleanupSucceeded = false;
	int32 ReturnCode = -1;
	FString StandardOutput;
	FString StandardError;
	FM6Error Error;

	bool IsSuccess() const
	{
		return bStarted && !bTimedOut && !bCancelled && bCleanupSucceeded &&
			ReturnCode == 0 && !Error.IsSet();
	}
};

using FM6ProcessComplete = TFunction<void(const FM6ProcessResult&)>;

class IM6ProcessRunner
{
public:
	virtual ~IM6ProcessRunner() = default;
	virtual void Start(
		const FM6ProcessInvocation& Invocation,
		FM6ProcessComplete OnComplete) = 0;
	virtual void Cancel() = 0;
	virtual void Tick(double NowSeconds) = 0;
	virtual bool IsActive() const = 0;
};

class FM6ProcessRunner final : public IM6ProcessRunner
{
public:
	FM6ProcessRunner() = default;
	virtual ~FM6ProcessRunner() override;

	virtual void Start(
		const FM6ProcessInvocation& Invocation,
		FM6ProcessComplete OnComplete) override;
	virtual void Cancel() override;
	virtual void Tick(double NowSeconds) override;
	virtual bool IsActive() const override;

	static FString BuildCommandLineForAutomationTest(
		const TArray<FString>& Arguments);

private:
	void Finish(FM6ProcessResult Result);
	void CloseOwnedHandles();
	void DrainOutput();
	bool HasExpectedPacket() const;

	FM6ProcessInvocation ActiveInvocation;
	FM6ProcessComplete Completion;
	FProcHandle ProcessHandle;
	void* StdoutRead = nullptr;
	void* StdoutWrite = nullptr;
	void* StderrRead = nullptr;
	void* StderrWrite = nullptr;
	TArray<uint8> StdoutBytes;
	TArray<uint8> StderrBytes;
	double StartSeconds = -1.0;
	double LastTickSeconds = 0.0;
	double CancelSeconds = -1.0;
	bool bCancelRequested = false;
};
