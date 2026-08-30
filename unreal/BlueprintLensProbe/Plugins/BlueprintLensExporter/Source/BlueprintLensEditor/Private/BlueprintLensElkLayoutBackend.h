#pragma once

#include "BlueprintLensExternalLayoutProcess.h"
#include "BlueprintLensLayoutContract.h"

struct FBlueprintLensElkLayoutOptions
{
	FString NodeExecutablePath;
	FString ElkJsRoot;
	FString HelperPath;
	double TimeoutSeconds = 3.0;
	FString BackendVersionOverride;
};

class FBlueprintLensElkLayoutBackend final
	: public IBlueprintLensLayoutBackend
{
public:
	explicit FBlueprintLensElkLayoutBackend(
		const FBlueprintLensElkLayoutOptions& InOptions = {});

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override;
	virtual bool IsAvailable(FString& OutDiagnostic) const override;
	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest& Request) const override;

	static FString SerializeRequest(
		const FBlueprintLensLayoutRequest& Request,
		FString& OutDiagnostic);
	static bool NormalizeResponse(
		const FString& Json,
		const FBlueprintLensLayoutRequest& Request,
		const FString& BackendVersion,
		const FString& ConfigurationFingerprint,
		FBlueprintLensLayoutLedger& OutLedger,
		FString& OutDiagnostic);

private:
	FString ResolveNodeExecutable(FString& OutDiagnostic) const;
	FString ResolveElkRoot(FString& OutDiagnostic) const;
	FString ResolveHelper(FString& OutDiagnostic) const;

	FBlueprintLensElkLayoutOptions Options;
};
