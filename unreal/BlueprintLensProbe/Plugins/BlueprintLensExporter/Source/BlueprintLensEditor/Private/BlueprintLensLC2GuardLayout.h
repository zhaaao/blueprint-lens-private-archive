#pragma once

#include "BlueprintLensLC2GuardSurfaceProjection.h"
#include "BlueprintLensLayoutContract.h"

struct FBlueprintLensLC2GuardLayoutNode
{
	FString UnitId;
	FString ReaderLabel;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

struct FBlueprintLensLC2GuardLayout
{
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC2GuardLayoutNode> Nodes;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FString DiagnosticCode;

	const FBlueprintLensLC2GuardLayoutNode* FindNode(const FString& UnitId) const;
	bool CoversProjection(const FBlueprintLensLC2GuardSurfaceProjection& Projection) const;
	bool HasExclusiveCompoundOwnership(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection) const;
	bool HasValidSharedLedger() const;
};

class FBlueprintLensLC2GuardLayoutBuilder
{
public:
	static FBlueprintLensLC2GuardLayout Build(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth);
};
