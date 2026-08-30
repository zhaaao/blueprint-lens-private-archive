#pragma once

#include "BlueprintLensLC1RailProjection.h"
#include "BlueprintLensLayoutContract.h"

struct FBlueprintLensLC1RailLayoutNode
{
	FString UnitId;
	FString ReaderLabel;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

struct FBlueprintLensLC1RailLayout
{
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC1RailLayoutNode> Nodes;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FString DiagnosticCode;

	const FBlueprintLensLC1RailLayoutNode* FindNode(const FString& UnitId) const;
	bool CoversProjection(const FBlueprintLensLC1RailProjection& Projection) const;
	bool HasValidSharedLedger() const;
};

class FBlueprintLensLC1RailLayoutBuilder
{
public:
	static FBlueprintLensLC1RailLayout Build(
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensExplanationModel& Explanation,
		float TargetWidth);
};
