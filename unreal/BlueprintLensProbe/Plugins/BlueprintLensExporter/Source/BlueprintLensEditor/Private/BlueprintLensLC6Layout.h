#pragma once

#include "BlueprintLensLC6Projection.h"
#include "BlueprintLensLayoutContract.h"
#include "Fonts/SlateFontInfo.h"

enum class EBlueprintLensLC6LayoutMode : uint8
{
	SingleColumn430,
	StackedDetail480,
	SideBySide700
};

struct FBlueprintLensLC6Label
{
	FString Id;
	FString Text;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	int32 FontSize = 8;
	bool bBold = false;
	FString ColorHex = TEXT("#F2F5F8");
};

struct FBlueprintLensLC6TrackLayout
{
	FString ScenarioId;
	FBox2D HitBounds = FBox2D(EForceInit::ForceInit);
	FBox2D BoundaryBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CriterionBounds = FBox2D(EForceInit::ForceInit);
	FBox2D FrontierBounds = FBox2D(EForceInit::ForceInit);
	FBox2D OmissionBounds = FBox2D(EForceInit::ForceInit);
	FVector2D RouteStart = FVector2D::ZeroVector;
	FVector2D RouteEnd = FVector2D::ZeroVector;
	FVector2D SemanticFenceStart = FVector2D::ZeroVector;
	FVector2D SemanticFenceEnd = FVector2D::ZeroVector;
	FVector2D CriterionMarker = FVector2D::ZeroVector;
	TArray<FBox2D> QueryNodeBounds;
	TArray<FString> QueryHopLabels;
	bool bHasSemanticFence = false;
};

struct FBlueprintLensLC6SourceAnchor
{
	FString ScenarioId;
	FString SourceNodeId;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FBlueprintLensLC6Layout
{
	EBlueprintLensLC6LayoutMode Mode = EBlueprintLensLC6LayoutMode::SideBySide700;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FBlueprintLensLayoutLedger VisualOracleLedger;
	TArray<FBlueprintLensLC6Label> Labels;
	TArray<FBlueprintLensLC6TrackLayout> Tracks;
	TArray<FBlueprintLensLC6SourceAnchor> SourceAnchors;
	FBox2D OverviewBounds = FBox2D(EForceInit::ForceInit);
	FBox2D DetailBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CompleteTextActionBounds = FBox2D(EForceInit::ForceInit);
	FString DiagnosticCode;

	bool CoversProjection(const FBlueprintLensLC6Projection& Projection) const;
	bool HasValidSharedLedger() const;
	bool MatchesVisualOracle(float Tolerance = 1.0f) const;
	bool HasNoTextOrRouteCollisions() const;
};

FSlateFontInfo BlueprintLensLC6Font(int32 FontSize, bool bBold);

class FBlueprintLensLC6LayoutBuilder
{
public:
	static FBlueprintLensLC6Layout Build(
		const FBlueprintLensLC6Projection& Projection,
		float TargetWidth);
};
