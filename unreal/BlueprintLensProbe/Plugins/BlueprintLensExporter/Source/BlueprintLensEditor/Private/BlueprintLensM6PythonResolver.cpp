// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6PythonResolver.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace BlueprintLensM6PythonResolver
{
namespace
{
const TCHAR* ConfigSection = TEXT("BlueprintLens.UI");
const TCHAR* ConfigKey = TEXT("PythonExecutable");

FM6PythonValidationResult Failure(
	const FM6PythonCandidate& Candidate,
	const TCHAR* Code,
	const TCHAR* Message)
{
	FM6PythonValidationResult Result;
	Result.ExecutablePath = Candidate.ExecutablePath;
	Result.Source = Candidate.Source;
	Result.FailureCode = Code;
	Result.FailureMessage = Message;
	return Result;
}

bool ParseVersion(
	const FString& Output,
	FString& OutImplementation,
	int32& OutMajor,
	int32& OutMinor)
{
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Parts;
		Line.TrimStartAndEnd().ParseIntoArray(Parts, TEXT("|"), true);
		if (Parts.Num() != 3)
		{
			continue;
		}
		if (!LexTryParseString(OutMajor, *Parts[1]) ||
			!LexTryParseString(OutMinor, *Parts[2]))
		{
			continue;
		}
		OutImplementation = Parts[0];
		return true;
	}
	return false;
}

bool IsSupportedVersion(
	const FString& Implementation,
	const int32 Major,
	const int32 Minor)
{
	return Implementation.Equals(TEXT("cpython"), ESearchCase::IgnoreCase) &&
		Major == 3 && Minor >= 11 && Minor <= 14;
}
} // namespace
} // namespace BlueprintLensM6PythonResolver

FM6PythonResolutionResult FM6PythonResolver::Resolve(
	const FString& WorkspaceRoot,
	const FString& PickerPath)
{
	const FString Root = FPaths::ConvertRelativePathToFull(WorkspaceRoot);
	TArray<FM6PythonCandidate> Candidates;
	const FString Saved = GetSavedPath();
	if (!Saved.IsEmpty())
	{
		Candidates.Add({Saved, EM6PythonResolutionSource::Saved});
	}
	Candidates.Add({
		FPaths::Combine(Root, TEXT(".venv/Scripts/python.exe")),
		EM6PythonResolutionSource::ProjectDotVenv});
	Candidates.Add({
		FPaths::Combine(Root, TEXT("venv/Scripts/python.exe")),
		EM6PythonResolutionSource::ProjectVenv});
	Candidates.Add({
		FindOnPath(TEXT("python")),
		EM6PythonResolutionSource::SystemPython});
	Candidates.Add({
		FindOnPath(TEXT("py")),
		EM6PythonResolutionSource::WindowsPy});
	if (!PickerPath.IsEmpty())
	{
		Candidates.Add({PickerPath, EM6PythonResolutionSource::Picker});
	}

	FM6PythonResolutionResult Result = ResolveCandidates(
		Candidates,
		[&Root](const FM6PythonCandidate& Candidate)
		{
			return ValidateCandidate(Candidate, Root);
		});
	if (Result.bValid)
	{
		SavePath(Result.ExecutablePath);
	}
	return Result;
}

FM6PythonResolutionResult FM6PythonResolver::ValidateAndPersist(
	const FString& ExecutablePath,
	const FString& WorkspaceRoot,
	const EM6PythonResolutionSource Source)
{
	const FM6PythonCandidate Candidate{ExecutablePath, Source};
	FM6PythonResolutionResult Result = ResolveCandidates(
		{Candidate},
		[&WorkspaceRoot](const FM6PythonCandidate& InCandidate)
		{
			return ValidateCandidate(InCandidate, WorkspaceRoot);
		});
	if (Result.bValid)
	{
		SavePath(Result.ExecutablePath);
	}
	return Result;
}

FM6PythonResolutionResult FM6PythonResolver::ResolveCandidates(
	const TArray<FM6PythonCandidate>& Candidates,
	const FM6PythonCandidateValidator& Validator)
{
	FM6PythonResolutionResult Result;
	Result.FailureCode = TEXT("M6_PYTHON_NOT_FOUND");
	Result.FailureMessage = TEXT("No supported Python runtime was found.");
	if (!Validator)
	{
		Result.FailureCode = TEXT("M6_PYTHON_VALIDATION_UNAVAILABLE");
		Result.FailureMessage = TEXT("Python candidate validation is unavailable.");
		return Result;
	}

	for (const FM6PythonCandidate& Candidate : Candidates)
	{
		const FM6PythonValidationResult CandidateResult = Validator(Candidate);
		FM6PythonAttempt Attempt;
		Attempt.ExecutablePath = Candidate.ExecutablePath;
		Attempt.Source = Candidate.Source;
		Attempt.bValid = CandidateResult.bValid;
		Attempt.Version = CandidateResult.Version;
		Attempt.FailureCode = CandidateResult.FailureCode;
		Attempt.FailureMessage = CandidateResult.FailureMessage;
		Result.Attempts.Add(MoveTemp(Attempt));
		if (CandidateResult.bValid)
		{
			TArray<FM6PythonAttempt> Attempts = MoveTemp(Result.Attempts);
			Result.bValid = true;
			Result.ExecutablePath = CandidateResult.ExecutablePath;
			Result.Source = CandidateResult.Source;
			Result.Version = CandidateResult.Version;
			Result.FailureCode = CandidateResult.FailureCode;
			Result.FailureMessage = CandidateResult.FailureMessage;
			Result.Attempts = MoveTemp(Attempts);
			return Result;
		}
		Result.FailureCode = CandidateResult.FailureCode;
		Result.FailureMessage = CandidateResult.FailureMessage;
	}
	return Result;
}

FM6PythonValidationResult FM6PythonResolver::ValidateCandidate(
	const FM6PythonCandidate& Candidate,
	const FString& WorkspaceRoot)
{
	using namespace BlueprintLensM6PythonResolver;
	FM6PythonCandidate NormalizedCandidate = Candidate;
	NormalizedCandidate.ExecutablePath = Candidate.ExecutablePath.IsEmpty()
		? FString()
		: FPaths::ConvertRelativePathToFull(Candidate.ExecutablePath);
	if (NormalizedCandidate.ExecutablePath.IsEmpty() ||
		!FPaths::FileExists(NormalizedCandidate.ExecutablePath))
	{
		return Failure(
			NormalizedCandidate,
			TEXT("M6_PYTHON_NOT_FOUND"),
			TEXT("The candidate executable does not exist."));
	}

	const FString AnalysisDirectory = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(WorkspaceRoot), TEXT("analysis"));
	if (!IFileManager::Get().DirectoryExists(*AnalysisDirectory))
	{
		return Failure(
			NormalizedCandidate,
			TEXT("M6_PYTHON_IMPORT_FAILED"),
			TEXT("The Blueprint Lens analysis package is unavailable."));
	}

	const FString Probe = TEXT(
		"-c \"import sys; print('%s|%d|%d' % "
		"(sys.implementation.name, sys.version_info[0], "
		"sys.version_info[1])); import blueprint_lens\"");
	int32 ReturnCode = -1;
	FString StandardOutput;
	FString StandardError;
	const bool bStarted = FPlatformProcess::ExecProcess(
		*NormalizedCandidate.ExecutablePath,
		*Probe,
		&ReturnCode,
		&StandardOutput,
		&StandardError,
		*AnalysisDirectory);
	if (!bStarted)
	{
		return Failure(
			NormalizedCandidate,
			TEXT("M6_PYTHON_START_FAILED"),
			TEXT("The candidate executable could not be started."));
	}

	FString Implementation;
	int32 Major = 0;
	int32 Minor = 0;
	if (!ParseVersion(StandardOutput, Implementation, Major, Minor) ||
		!IsSupportedVersion(Implementation, Major, Minor))
	{
		return Failure(
			NormalizedCandidate,
			TEXT("M6_PYTHON_UNSUPPORTED_VERSION"),
			TEXT("The candidate is not CPython 3.11 through 3.14."));
	}

	FM6PythonValidationResult Result;
	Result.ExecutablePath = NormalizedCandidate.ExecutablePath;
	Result.Source = NormalizedCandidate.Source;
	Result.Version = FString::Printf(TEXT("%d.%d"), Major, Minor);
	if (ReturnCode != 0)
	{
		Result.FailureCode = TEXT("M6_PYTHON_IMPORT_FAILED");
		Result.FailureMessage = TEXT("The Blueprint Lens analysis package could not be imported.");
		return Result;
	}
	Result.bValid = true;
	return Result;
}

FString FM6PythonResolver::GetSavedPath()
{
	using namespace BlueprintLensM6PythonResolver;
	FString Result;
	if (GConfig != nullptr)
	{
		GConfig->GetString(ConfigSection, ConfigKey, Result, GEditorPerProjectIni);
	}
	return Result;
}

void FM6PythonResolver::SavePath(const FString& ExecutablePath)
{
	using namespace BlueprintLensM6PythonResolver;
	if (GConfig == nullptr) return;
	const FString NormalizedPath = FPaths::ConvertRelativePathToFull(ExecutablePath);
	GConfig->SetString(
		ConfigSection, ConfigKey, *NormalizedPath, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void FM6PythonResolver::ClearSavedPath()
{
	using namespace BlueprintLensM6PythonResolver;
	if (GConfig == nullptr) return;
	GConfig->RemoveKey(ConfigSection, ConfigKey, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

FString FM6PythonResolver::SourceLabel(const EM6PythonResolutionSource Source)
{
	switch (Source)
	{
	case EM6PythonResolutionSource::Saved: return TEXT("saved path");
	case EM6PythonResolutionSource::ProjectDotVenv: return TEXT("project .venv");
	case EM6PythonResolutionSource::ProjectVenv: return TEXT("project venv");
	case EM6PythonResolutionSource::SystemPython: return TEXT("system python");
	case EM6PythonResolutionSource::WindowsPy: return TEXT("Windows py launcher");
	case EM6PythonResolutionSource::Picker: return TEXT("file picker");
	default: return TEXT("none");
	}
}

FString FM6PythonResolver::FindOnPath(const FString& ExecutableName)
{
	const FString PathValue = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> Directories;
#if PLATFORM_WINDOWS
	PathValue.ParseIntoArray(Directories, TEXT(";"), true);
#else
	PathValue.ParseIntoArray(Directories, TEXT(":"), true);
#endif
	for (const FString& Directory : Directories)
	{
		FString Candidate = FPaths::Combine(Directory, ExecutableName);
#if PLATFORM_WINDOWS
		if (FPaths::GetExtension(Candidate).IsEmpty()) Candidate += TEXT(".exe");
#endif
		if (FPaths::FileExists(Candidate))
		{
			return FPaths::ConvertRelativePathToFull(Candidate);
		}
	}
	return FString();
}
