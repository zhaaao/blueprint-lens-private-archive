// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EM6PythonResolutionSource : uint8
{
	None,
	Saved,
	ProjectDotVenv,
	ProjectVenv,
	SystemPython,
	WindowsPy,
	Picker
};

struct FM6PythonCandidate
{
	FString ExecutablePath;
	EM6PythonResolutionSource Source = EM6PythonResolutionSource::None;
};

struct FM6PythonAttempt
{
	FString ExecutablePath;
	EM6PythonResolutionSource Source = EM6PythonResolutionSource::None;
	bool bValid = false;
	FString Version;
	FString FailureCode;
	FString FailureMessage;
};

struct FM6PythonValidationResult
{
	bool bValid = false;
	FString ExecutablePath;
	EM6PythonResolutionSource Source = EM6PythonResolutionSource::None;
	FString Version;
	FString FailureCode;
	FString FailureMessage;
};

struct FM6PythonResolutionResult : public FM6PythonValidationResult
{
	TArray<FM6PythonAttempt> Attempts;
};

using FM6PythonCandidateValidator =
	TFunction<FM6PythonValidationResult(const FM6PythonCandidate&)>;

class FM6PythonResolver
{
public:
	static FM6PythonResolutionResult Resolve(
		const FString& WorkspaceRoot,
		const FString& PickerPath = FString());

	static FM6PythonResolutionResult ValidateAndPersist(
		const FString& ExecutablePath,
		const FString& WorkspaceRoot,
		EM6PythonResolutionSource Source = EM6PythonResolutionSource::Picker);

	static FM6PythonResolutionResult ResolveCandidates(
		const TArray<FM6PythonCandidate>& Candidates,
		const FM6PythonCandidateValidator& Validator);

	static FM6PythonValidationResult ValidateCandidate(
		const FM6PythonCandidate& Candidate,
		const FString& WorkspaceRoot);

	static FString GetSavedPath();
	static void SavePath(const FString& ExecutablePath);
	static void ClearSavedPath();
	static FString SourceLabel(EM6PythonResolutionSource Source);

private:
	static FString FindOnPath(const FString& ExecutableName);
};
