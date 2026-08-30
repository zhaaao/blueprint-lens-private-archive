#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC3ValueConeLayout.h"

struct FBlueprintLensLC3ValueConeLayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC3ValueConeLayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC3ValueConeLayoutSessionResult
{
	FBlueprintLensLC3ValueConeLayout Layout;
	TArray<FBlueprintLensLC3ValueConeLayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(
		const FBlueprintLensLC3ValueConeProjection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC3ValueConeLayoutSession
{
public:
	static FBlueprintLensLC3ValueConeLayoutSessionResult Build(
		const FBlueprintLensLC3ValueConeProjection& Projection,
		float TargetWidth,
		const FBlueprintLensLC3ValueConeLayoutSessionOptions& Options = {});

	static FBlueprintLensLC3ValueConeLayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC3ValueConeProjection& Projection,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
