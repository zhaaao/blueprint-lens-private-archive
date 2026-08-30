#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC7Layout.h"

struct FBlueprintLensLC7LayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC7LayoutSessionOptions
{
	FBlueprintLensLC7TextMetrics TextMetrics;
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC7LayoutSessionResult
{
	FBlueprintLensLC7Layout Layout;
	TArray<FBlueprintLensLC7LayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC7Projection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC7LayoutSession
{
public:
	static FBlueprintLensLC7LayoutSessionResult Build(
		const FBlueprintLensLC7Projection& Projection,
		float TargetWidth,
		const FString& FocusedSCCId,
		const FBlueprintLensLC7LayoutSessionOptions& Options = {});

	static FBlueprintLensLC7LayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC7Projection& Projection,
		float TargetWidth,
		const FString& FocusedSCCId,
		const FBlueprintLensLC7TextMetrics& Metrics,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
