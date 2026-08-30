#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC5Layout.h"

struct FBlueprintLensLC5LayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC5LayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC5LayoutSessionResult
{
	FBlueprintLensLC5Layout Layout;
	TArray<FBlueprintLensLC5LayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC5Projection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC5LayoutSession
{
public:
	static FBlueprintLensLC5LayoutSessionResult Build(
		const FBlueprintLensLC5Projection& Projection,
		float TargetWidth,
		const FBlueprintLensLC5LayoutSessionOptions& Options = {});

	static FBlueprintLensLC5LayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC5Projection& Projection,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
