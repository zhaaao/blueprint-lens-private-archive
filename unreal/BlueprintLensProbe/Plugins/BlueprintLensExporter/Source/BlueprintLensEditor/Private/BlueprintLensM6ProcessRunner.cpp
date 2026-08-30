// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6ProcessRunner.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace BlueprintLensM6ProcessRunner
{
namespace
{
FM6Error MakeError(const TCHAR* Code, const TCHAR* Message)
{
	FM6Error Error;
	Error.Code = Code;
	Error.Phase = TEXT("runner");
	Error.Message = Message;
	Error.bRetryable = false;
	return Error;
}

FString Utf8Text(const TArray<uint8>& Bytes)
{
	if (Bytes.IsEmpty()) return FString();
	TArray<uint8> Terminated = Bytes;
	Terminated.Add(0);
	return FUTF8ToTCHAR(
		reinterpret_cast<const ANSICHAR*>(Terminated.GetData())).Get();
}

FString QuoteArgument(const FString& Argument)
{
	if (!Argument.IsEmpty() &&
		!Argument.Contains(TEXT(" ")) &&
		!Argument.Contains(TEXT("\t")) &&
		!Argument.Contains(TEXT("\"")))
	{
		return Argument;
	}
	FString Result(TEXT("\""));
	int32 Backslashes = 0;
	for (const TCHAR Character : Argument)
	{
		if (Character == TEXT('\\'))
		{
			++Backslashes;
			continue;
		}
		if (Character == TEXT('"'))
		{
			Result += FString::ChrN(Backslashes * 2 + 1, TEXT('\\'));
			Result.AppendChar(Character);
			Backslashes = 0;
			continue;
		}
		Result += FString::ChrN(Backslashes, TEXT('\\'));
		Backslashes = 0;
		Result.AppendChar(Character);
	}
	Result += FString::ChrN(Backslashes * 2, TEXT('\\'));
	Result.AppendChar(TEXT('"'));
	return Result;
}
} // namespace
} // namespace BlueprintLensM6ProcessRunner

FM6ProcessRunner::~FM6ProcessRunner()
{
	if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		FPlatformProcess::TerminateProc(ProcessHandle, true);
		FPlatformProcess::WaitForProc(ProcessHandle);
	}
	CloseOwnedHandles();
}

FString FM6ProcessRunner::BuildCommandLineForAutomationTest(
	const TArray<FString>& Arguments)
{
	TArray<FString> Quoted;
	for (const FString& Argument : Arguments)
	{
		Quoted.Add(BlueprintLensM6ProcessRunner::QuoteArgument(Argument));
	}
	return FString::Join(Quoted, TEXT(" "));
}

void FM6ProcessRunner::Start(
	const FM6ProcessInvocation& Invocation,
	FM6ProcessComplete OnComplete)
{
	if (IsActive())
	{
		FM6ProcessResult Result;
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("a child process is already active"));
		OnComplete(Result);
		return;
	}
	const FString ExecutablePath = FPaths::ConvertRelativePathToFull(
		Invocation.ExecutablePath);
	if (!FPaths::FileExists(ExecutablePath) ||
		Invocation.TimeoutSeconds <= 0.0 ||
		Invocation.ExpectedPacketFiles <= 0)
	{
		FM6ProcessResult Result;
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("process invocation is invalid"));
		OnComplete(Result);
		return;
	}

	if (!FPlatformProcess::CreatePipe(StdoutRead, StdoutWrite) ||
		!FPlatformProcess::CreatePipe(StderrRead, StderrWrite))
	{
		CloseOwnedHandles();
		FM6ProcessResult Result;
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("process pipes could not be created"));
		OnComplete(Result);
		return;
	}

	ActiveInvocation = Invocation;
	ActiveInvocation.ExecutablePath = ExecutablePath;
	Completion = MoveTemp(OnComplete);
	StdoutBytes.Reset();
	StderrBytes.Reset();
	StartSeconds = -1.0;
	CancelSeconds = -1.0;
	bCancelRequested = false;
	uint32 ProcessId = 0;
	const FString CommandLine = BuildCommandLineForAutomationTest(Invocation.Arguments);
	ProcessHandle = FPlatformProcess::CreateProc(
		*Invocation.ExecutablePath,
		*CommandLine,
		false,
		true,
		true,
		&ProcessId,
		0,
		Invocation.WorkingDirectory.IsEmpty() ? nullptr : *Invocation.WorkingDirectory,
		StdoutWrite,
		nullptr,
		StderrWrite);
	FPlatformProcess::ClosePipe(nullptr, StdoutWrite);
	StdoutWrite = nullptr;
	FPlatformProcess::ClosePipe(nullptr, StderrWrite);
	StderrWrite = nullptr;
	if (!ProcessHandle.IsValid())
	{
		FM6ProcessResult Result;
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_RUNNER_LAUNCH_FAILED"), TEXT("owned child process could not be launched"));
		Finish(MoveTemp(Result));
	}
}

void FM6ProcessRunner::Cancel()
{
	if (!IsActive() || bCancelRequested) return;
	bCancelRequested = true;
	CancelSeconds = LastTickSeconds;
	FPlatformProcess::TerminateProc(ProcessHandle, true);
}

void FM6ProcessRunner::Tick(const double NowSeconds)
{
	LastTickSeconds = NowSeconds;
	if (!IsActive()) return;
	if (StartSeconds < 0.0) StartSeconds = NowSeconds;
	DrainOutput();
	if (!bCancelRequested &&
		NowSeconds - StartSeconds >= ActiveInvocation.TimeoutSeconds)
	{
		bCancelRequested = true;
		CancelSeconds = NowSeconds;
		FPlatformProcess::TerminateProc(ProcessHandle, true);
	}
	if (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		if (bCancelRequested && NowSeconds - CancelSeconds > 5.0)
		{
			FM6ProcessResult Result;
			Result.bStarted = true;
			Result.bCancelled = true;
			Result.bCleanupSucceeded = false;
			Result.Error = BlueprintLensM6ProcessRunner::MakeError(
				TEXT("M6_RUNNER_CLEANUP_FAILED"), TEXT("owned child did not terminate within the cleanup bound"));
			Finish(MoveTemp(Result));
		}
		return;
	}

	FPlatformProcess::WaitForProc(ProcessHandle);
	DrainOutput();
	FM6ProcessResult Result;
	Result.bStarted = true;
	Result.bCleanupSucceeded = true;
	FPlatformProcess::GetProcReturnCode(ProcessHandle, &Result.ReturnCode);
	Result.StandardOutput = BlueprintLensM6ProcessRunner::Utf8Text(StdoutBytes);
	Result.StandardError = BlueprintLensM6ProcessRunner::Utf8Text(StderrBytes);
	if (bCancelRequested)
	{
		Result.bTimedOut = NowSeconds - StartSeconds >= ActiveInvocation.TimeoutSeconds;
		Result.bCancelled = !Result.bTimedOut;
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			Result.bTimedOut ? TEXT("M6_RUNNER_TIMEOUT") : TEXT("M6_RUNNER_CANCELLED"),
			Result.bTimedOut ? TEXT("owned child exceeded its timeout") : TEXT("owned child was cancelled"));
	}
	else if (Result.ReturnCode != 0)
	{
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_RUNNER_NONZERO_EXIT"), TEXT("owned child returned a non-zero exit"));
	}
	else if (!HasExpectedPacket())
	{
		Result.Error = BlueprintLensM6ProcessRunner::MakeError(
			TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("owned child did not publish the expected packet"));
	}
	Finish(MoveTemp(Result));
}

bool FM6ProcessRunner::IsActive() const
{
	return ProcessHandle.IsValid() && Completion.operator bool();
}

void FM6ProcessRunner::Finish(FM6ProcessResult Result)
{
	FM6ProcessComplete LocalCompletion = MoveTemp(Completion);
	CloseOwnedHandles();
	ActiveInvocation = FM6ProcessInvocation();
	StartSeconds = -1.0;
	CancelSeconds = -1.0;
	bCancelRequested = false;
	if (LocalCompletion) LocalCompletion(Result);
}

void FM6ProcessRunner::CloseOwnedHandles()
{
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::CloseProc(ProcessHandle);
		ProcessHandle.Reset();
	}
	if (StdoutRead != nullptr)
	{
		FPlatformProcess::ClosePipe(StdoutRead, nullptr);
		StdoutRead = nullptr;
	}
	if (StdoutWrite != nullptr)
	{
		FPlatformProcess::ClosePipe(nullptr, StdoutWrite);
		StdoutWrite = nullptr;
	}
	if (StderrRead != nullptr)
	{
		FPlatformProcess::ClosePipe(StderrRead, nullptr);
		StderrRead = nullptr;
	}
	if (StderrWrite != nullptr)
	{
		FPlatformProcess::ClosePipe(nullptr, StderrWrite);
		StderrWrite = nullptr;
	}
}

void FM6ProcessRunner::DrainOutput()
{
	TArray<uint8> Chunk;
	if (StdoutRead != nullptr && FPlatformProcess::ReadPipeToArray(StdoutRead, Chunk))
		StdoutBytes.Append(Chunk);
	Chunk.Reset();
	if (StderrRead != nullptr && FPlatformProcess::ReadPipeToArray(StderrRead, Chunk))
		StderrBytes.Append(Chunk);
}

bool FM6ProcessRunner::HasExpectedPacket() const
{
	if (ActiveInvocation.PacketDirectory.IsEmpty()) return false;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(
		Files,
		*FPaths::Combine(ActiveInvocation.PacketDirectory, TEXT("*")),
		true,
		false);
	return Files.Num() == ActiveInvocation.ExpectedPacketFiles &&
		FPaths::FileExists(FPaths::Combine(
			ActiveInvocation.PacketDirectory, TEXT("manifest.json")));
}
