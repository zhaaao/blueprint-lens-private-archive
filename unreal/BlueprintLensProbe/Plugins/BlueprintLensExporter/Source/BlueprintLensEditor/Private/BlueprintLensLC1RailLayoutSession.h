#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC1RailLayout.h"

struct FBlueprintLensLC1RailLayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC1RailLayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC1RailLayoutSessionResult
{
	FBlueprintLensLC1RailLayout Layout;
	TArray<FBlueprintLensLC1RailLayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC1RailProjection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC1RailLayoutSession
{
public:
	static FBlueprintLensLC1RailLayoutSessionResult Build(
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth,
		const FBlueprintLensLC1RailLayoutSessionOptions& Options = {});
	static FBlueprintLensLC1RailLayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
