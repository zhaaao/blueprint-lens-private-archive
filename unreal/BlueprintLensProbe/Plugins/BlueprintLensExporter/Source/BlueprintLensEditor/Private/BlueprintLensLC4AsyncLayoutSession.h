#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC4AsyncLayout.h"

struct FBlueprintLensLC4AsyncLayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC4AsyncLayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC4AsyncLayoutSessionResult
{
	FBlueprintLensLC4AsyncLayout Layout;
	TArray<FBlueprintLensLC4AsyncLayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC4AsyncProjection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC4AsyncLayoutSession
{
public:
	static FBlueprintLensLC4AsyncLayoutSessionResult Build(
		const FBlueprintLensLC4AsyncProjection& Projection,
		float TargetWidth,
		const FBlueprintLensLC4AsyncLayoutSessionOptions& Options = {});

	static FBlueprintLensLC4AsyncLayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC4AsyncProjection& Projection,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
