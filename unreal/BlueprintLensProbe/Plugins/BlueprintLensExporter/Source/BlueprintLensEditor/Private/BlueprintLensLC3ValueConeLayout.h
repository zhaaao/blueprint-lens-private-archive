#pragma once

#include "BlueprintLensLayoutContract.h"
#include "BlueprintLensLC3ValueConeProjection.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC3ValueConeLayoutMode : uint8
{
	Wide,
	Compact
};

enum class EBlueprintLensLC3ValueConeNodeKind : uint8
{
	Criterion,
	Operator,
	Leaf,
	Control
};

struct FBlueprintLensLC3ValueConeLayoutNode
{
	FString UnitId;
	FString ReaderLabel;
	FString RouteText;
	FString InputSummaryText;
	TArray<FString> InputPortLabels;
	TArray<FString> OutputPortLabels;
	int32 DerivationDepth = 0;
	EBlueprintLensLC3ValueConeNodeKind Kind =
		EBlueprintLensLC3ValueConeNodeKind::Leaf;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

struct FBlueprintLensLC3ValueConeLayoutEdge
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourcePortLabel;
	FString TargetPortLabel;
	bool bControl = false;
};

struct FBlueprintLensLC3ValueConeLayout
{
	EBlueprintLensLC3ValueConeLayoutMode Mode =
		EBlueprintLensLC3ValueConeLayoutMode::Compact;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC3ValueConeLayoutNode> Nodes;
	TArray<FBlueprintLensLC3ValueConeLayoutEdge> Edges;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FString DiagnosticCode;

	const FBlueprintLensLC3ValueConeLayoutNode* FindNode(
		const FString& UnitId) const;
	bool HasNoNodeOverlaps() const;
	bool CoversProjection(
		const FBlueprintLensLC3ValueConeProjection& Projection) const;
	bool HasValidSharedLedger() const;
};

class FBlueprintLensLC3ValueConeLayoutBuilder
{
public:
	static FBlueprintLensLC3ValueConeLayout Build(
		const FBlueprintLensLC3ValueConeProjection& Projection,
		float TargetWidth);
};
