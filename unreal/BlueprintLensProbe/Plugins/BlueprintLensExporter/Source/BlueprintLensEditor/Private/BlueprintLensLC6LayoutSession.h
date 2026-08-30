#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC6Layout.h"

struct FBlueprintLensLC6LayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC6LayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC6LayoutSessionResult
{
	FBlueprintLensLC6Layout Layout;
	TArray<FBlueprintLensLC6LayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC6Projection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC6LayoutSession
{
public:
	static FBlueprintLensLC6LayoutSessionResult Build(
		const FBlueprintLensLC6Projection& Projection,
		float TargetWidth,
		const FBlueprintLensLC6LayoutSessionOptions& Options = {});

	static FBlueprintLensLC6LayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC6Projection& Projection,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
