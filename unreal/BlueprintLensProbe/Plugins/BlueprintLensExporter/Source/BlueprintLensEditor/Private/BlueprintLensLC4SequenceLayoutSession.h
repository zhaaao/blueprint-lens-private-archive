#pragma once

#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLC4SequenceLayout.h"

struct FBlueprintLensLC4SequenceLayoutAttempt
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	bool bAvailable = false;
	bool bAccepted = false;
	FString DiagnosticCode;
};

struct FBlueprintLensLC4SequenceLayoutSessionOptions
{
	FBlueprintLensElkLayoutOptions Elk;
	FBlueprintLensGraphvizLayoutOptions Graphviz;
};

struct FBlueprintLensLC4SequenceLayoutSessionResult
{
	FBlueprintLensLC4SequenceLayout Layout;
	TArray<FBlueprintLensLC4SequenceLayoutAttempt> Attempts;
	FString DiagnosticCode;

	bool IsRenderable(
		const FBlueprintLensLC4SequenceProjection& Projection) const;
	FString AttemptSummary() const;
};

class FBlueprintLensLC4SequenceLayoutSession
{
public:
	static FBlueprintLensLC4SequenceLayoutSessionResult Build(
		const FBlueprintLensLC4SequenceProjection& Projection,
		float TargetWidth,
		const FBlueprintLensLC4SequenceLayoutSessionOptions& Options = {});

	static FBlueprintLensLC4SequenceLayoutSessionResult BuildWithBackends(
		const FBlueprintLensLC4SequenceProjection& Projection,
		float TargetWidth,
		const IBlueprintLensLayoutBackend& ElkBackend,
		const IBlueprintLensLayoutBackend& GraphvizBackend);
};
