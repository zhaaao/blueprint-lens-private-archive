#pragma once

#include "CoreMinimal.h"

struct FBlueprintLensExternalLayoutProcessOptions
{
	FString ExecutablePath;
	FString Arguments;
	FString StandardInput;
	FString WorkingDirectory;
	double TimeoutSeconds = 2.0;
};

struct FBlueprintLensExternalLayoutProcessResult
{
	bool bStarted = false;
	bool bTimedOut = false;
	int32 ReturnCode = -1;
	FString StandardOutput;
	FString StandardError;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return bStarted && !bTimedOut &&
			DiagnosticCode == TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_COMPLETE") &&
			ReturnCode == 0;
	}
};

class FBlueprintLensExternalLayoutProcess
{
public:
	static FBlueprintLensExternalLayoutProcessResult Run(
		const FBlueprintLensExternalLayoutProcessOptions& Options);

	static FString ResolveExecutable(
		const FString& EnvironmentVariable,
		const FString& ExecutableName,
		FString& OutDiagnostic);
};
