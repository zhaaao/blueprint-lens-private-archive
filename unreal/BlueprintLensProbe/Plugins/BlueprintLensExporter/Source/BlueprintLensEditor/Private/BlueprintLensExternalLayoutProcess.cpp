#include "BlueprintLensExternalLayoutProcess.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"

namespace
{
FString ToUtf8String(const TArray<uint8>& Bytes)
{
	if (Bytes.IsEmpty())
	{
		return FString();
	}

	TArray<uint8> NullTerminated = Bytes;
	NullTerminated.Add(0);
	return FUTF8ToTCHAR(
		reinterpret_cast<const ANSICHAR*>(NullTerminated.GetData())).Get();
}

void ClosePipeHandles(void*& ReadPipe, void*& WritePipe)
{
	if (ReadPipe != nullptr || WritePipe != nullptr)
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
	}
}

void CloseReadHandle(void*& ReadPipe)
{
	if (ReadPipe != nullptr)
	{
		FPlatformProcess::ClosePipe(ReadPipe, nullptr);
		ReadPipe = nullptr;
	}
}

void CloseWriteHandle(void*& WritePipe)
{
	if (WritePipe != nullptr)
	{
		FPlatformProcess::ClosePipe(nullptr, WritePipe);
		WritePipe = nullptr;
	}
}

bool IsExistingFile(const FString& Candidate, FString& OutFullPath)
{
	if (Candidate.IsEmpty() || !FPaths::FileExists(Candidate))
	{
		return false;
	}
	OutFullPath = FPaths::ConvertRelativePathToFull(Candidate);
	return true;
}

void DrainPipe(void* ReadPipe, TArray<uint8>& OutBytes)
{
	TArray<uint8> Chunk;
	if (ReadPipe != nullptr && FPlatformProcess::ReadPipeToArray(ReadPipe, Chunk))
	{
		OutBytes.Append(Chunk);
	}
}

TArray<FString> ExecutableNames(const FString& ExecutableName)
{
	TArray<FString> Names;
	Names.Add(ExecutableName);
	if (!ExecutableName.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase))
	{
		Names.Add(ExecutableName + TEXT(".exe"));
	}
	return Names;
}
}

FString FBlueprintLensExternalLayoutProcess::ResolveExecutable(
	const FString& EnvironmentVariable,
	const FString& ExecutableName,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (ExecutableName.IsEmpty())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_EXTERNAL_EXECUTABLE_NAME_EMPTY");
		return FString();
	}

	const TArray<FString> Names = ExecutableNames(ExecutableName);
	const FString ExplicitRoot = EnvironmentVariable.IsEmpty()
		? FString()
		: FPlatformMisc::GetEnvironmentVariable(*EnvironmentVariable);
	if (!ExplicitRoot.IsEmpty())
	{
		FString FullPath;
		if (IsExistingFile(ExplicitRoot, FullPath))
		{
			return FullPath;
		}
		for (const FString& Name : Names)
		{
			const FString Candidates[] = {
				FPaths::Combine(ExplicitRoot, Name),
				FPaths::Combine(ExplicitRoot, TEXT("bin"), Name)};
			for (const FString& Candidate : Candidates)
			{
				if (IsExistingFile(Candidate, FullPath))
				{
					return FullPath;
				}
			}
		}
	}

	const FString PathVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> PathEntries;
	PathVariable.ParseIntoArray(
		PathEntries,
		FPlatformMisc::GetPathVarDelimiter(),
		true);
	for (const FString& Entry : PathEntries)
	{
		for (const FString& Name : Names)
		{
			FString FullPath;
			if (IsExistingFile(FPaths::Combine(Entry, Name), FullPath))
			{
				return FullPath;
			}
		}
	}

	OutDiagnostic = FString::Printf(
		TEXT("BLUEPRINT_LENS_EXTERNAL_EXECUTABLE_MISSING:%s"),
		*ExecutableName);
	return FString();
}

FBlueprintLensExternalLayoutProcessResult
FBlueprintLensExternalLayoutProcess::Run(
	const FBlueprintLensExternalLayoutProcessOptions& Options)
{
	FBlueprintLensExternalLayoutProcessResult Result;
	if (Options.ExecutablePath.IsEmpty() || Options.TimeoutSeconds <= 0.0)
	{
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_INVALID_REQUEST");
		return Result;
	}

	void* StdoutRead = nullptr;
	void* StdoutWrite = nullptr;
	void* StderrRead = nullptr;
	void* StderrWrite = nullptr;
	void* StdinRead = nullptr;
	void* StdinWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(StdoutRead, StdoutWrite) ||
		!FPlatformProcess::CreatePipe(StderrRead, StderrWrite) ||
		!FPlatformProcess::CreatePipe(StdinRead, StdinWrite, true))
	{
		ClosePipeHandles(StdoutRead, StdoutWrite);
		ClosePipeHandles(StderrRead, StderrWrite);
		ClosePipeHandles(StdinRead, StdinWrite);
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_PIPE_FAILED");
		return Result;
	}

	uint32 ProcessId = 0;
	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(
		*Options.ExecutablePath,
		*Options.Arguments,
		false,
		true,
		true,
		&ProcessId,
		0,
		Options.WorkingDirectory.IsEmpty()
			? nullptr
			: *Options.WorkingDirectory,
		StdoutWrite,
		StdinRead,
		StderrWrite);

	CloseWriteHandle(StdoutWrite);
	CloseReadHandle(StdinRead);
	CloseWriteHandle(StderrWrite);

	if (!ProcessHandle.IsValid())
	{
		ClosePipeHandles(StdoutRead, StdoutWrite);
		ClosePipeHandles(StderrRead, StderrWrite);
		ClosePipeHandles(StdinRead, StdinWrite);
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_START_FAILED");
		return Result;
	}
	Result.bStarted = true;

	if (!Options.StandardInput.IsEmpty())
	{
		FTCHARToUTF8 InputUtf8(*Options.StandardInput);
		int32 BytesWritten = 0;
		const bool bWroteInput = FPlatformProcess::WritePipe(
			StdinWrite,
			reinterpret_cast<const uint8*>(InputUtf8.Get()),
			InputUtf8.Length(),
			&BytesWritten);
		if (!bWroteInput || BytesWritten != InputUtf8.Length())
		{
			FPlatformProcess::TerminateProc(ProcessHandle, true);
			FPlatformProcess::WaitForProc(ProcessHandle);
			FPlatformProcess::CloseProc(ProcessHandle);
			ClosePipeHandles(StdoutRead, StdoutWrite);
			ClosePipeHandles(StderrRead, StderrWrite);
			ClosePipeHandles(StdinRead, StdinWrite);
			Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_STDIN_FAILED");
			return Result;
		}
	}
	CloseWriteHandle(StdinWrite);

	TArray<uint8> StdoutBytes;
	TArray<uint8> StderrBytes;
	const double StartSeconds = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		DrainPipe(StdoutRead, StdoutBytes);
		DrainPipe(StderrRead, StderrBytes);
		if (FPlatformTime::Seconds() - StartSeconds >= Options.TimeoutSeconds)
		{
			Result.bTimedOut = true;
			FPlatformProcess::TerminateProc(ProcessHandle, true);
			break;
		}
		FPlatformProcess::Sleep(0.005f);
	}
	FPlatformProcess::WaitForProc(ProcessHandle);
	DrainPipe(StdoutRead, StdoutBytes);
	DrainPipe(StderrRead, StderrBytes);
	FPlatformProcess::GetProcReturnCode(ProcessHandle, &Result.ReturnCode);
	FPlatformProcess::CloseProc(ProcessHandle);
	ClosePipeHandles(StdoutRead, StdoutWrite);
	ClosePipeHandles(StderrRead, StderrWrite);
	ClosePipeHandles(StdinRead, StdinWrite);

	Result.StandardOutput = ToUtf8String(StdoutBytes);
	Result.StandardError = ToUtf8String(StderrBytes);
	if (Result.bTimedOut)
	{
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_TIMEOUT");
	}
	else if (Result.ReturnCode != 0)
	{
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_EXIT_NONZERO");
	}
	else
	{
		Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_COMPLETE");
	}
	return Result;
}
