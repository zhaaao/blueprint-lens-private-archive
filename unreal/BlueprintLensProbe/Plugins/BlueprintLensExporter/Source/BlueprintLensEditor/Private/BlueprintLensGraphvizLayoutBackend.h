#pragma once

#include "BlueprintLensExternalLayoutProcess.h"
#include "BlueprintLensLayoutContract.h"

struct FBlueprintLensGraphvizLayoutOptions
{
	FString ExecutablePath;
	double TimeoutSeconds = 2.0;
	FString BackendVersionOverride;
};

class FBlueprintLensGraphvizLayoutBackend final
	: public IBlueprintLensLayoutBackend
{
public:
	explicit FBlueprintLensGraphvizLayoutBackend(
		const FBlueprintLensGraphvizLayoutOptions& InOptions = {});

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override;
	virtual bool IsAvailable(FString& OutDiagnostic) const override;
	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest& Request) const override;

	static FString SerializeDot(
		const FBlueprintLensLayoutRequest& Request,
		FString& OutDiagnostic);
	static bool NormalizeJson(
		const FString& Json,
		const FBlueprintLensLayoutRequest& Request,
		const FString& BackendVersion,
		const FString& ConfigurationFingerprint,
		FBlueprintLensLayoutLedger& OutLedger,
		FString& OutDiagnostic);

private:
	FString ResolveExecutable(FString& OutDiagnostic) const;

	FBlueprintLensGraphvizLayoutOptions Options;
};
