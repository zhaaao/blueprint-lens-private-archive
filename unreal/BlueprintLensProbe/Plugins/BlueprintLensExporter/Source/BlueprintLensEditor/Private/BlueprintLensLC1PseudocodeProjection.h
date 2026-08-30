#pragma once

#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensLC1TypedIrFacts.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC1PseudocodeProjectionStatus : uint8
{
	Complete,
	Unavailable
};

struct FBlueprintLensLC1PseudocodeLine
{
	FString LineId;
	int32 LineNumber = 0;
	FString CodeText;
	EBlueprintLensRole Role = EBlueprintLensRole::Control;
	EBlueprintLensSemanticStatus SemanticStatus =
		EBlueprintLensSemanticStatus::Supported;
	FString UnitId;
	FString FollowingRelationId;
	FString SourceNodeId;
	TArray<FString> SourcePinIds;
	FString FactOwner;
	FString ProjectionDiagnostic;
};

struct FBlueprintLensLC1PseudocodeProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	TArray<FBlueprintLensLC1PseudocodeLine> Lines;
	EBlueprintLensLC1PseudocodeProjectionStatus Status =
		EBlueprintLensLC1PseudocodeProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
};

class FBlueprintLensLC1PseudocodeProjector
{
public:
	static FBlueprintLensLC1PseudocodeProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1TypedIrFacts& TypedIrFacts);
};
