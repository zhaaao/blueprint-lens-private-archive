#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC2GuardLayout.h"

struct FBlueprintLensLC2GuardLayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC2GuardLayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC2GuardLayoutSessionResult
{
	FBlueprintLensLC2GuardLayout Layout;
	TArray<FBlueprintLensLC2GuardLayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC2GuardSurfaceProjection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC2GuardLayoutSession
{
public:
	static FBlueprintLensLC2GuardLayoutSessionResult Build(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth,
		const FBlueprintLensLC2GuardLayoutSessionOptions& Options = {});

	static FBlueprintLensLC2GuardLayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
