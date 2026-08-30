#pragma once

#include "BlueprintLensLC5Projection.h"
#include "BlueprintLensLayoutContract.h"
#include "Fonts/SlateFontInfo.h"

enum class EBlueprintLensLC5LayoutMode : uint8
{
	SingleColumn430,
	StackedDetail480,
	SideBySide700
};

struct FBlueprintLensLC5Label
{
	FString Id;
	FString Text;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	int32 FontSize = 10;
	bool bBold = false;
	FString ColorHex = TEXT("#F2F5F8");
};

FSlateFontInfo BlueprintLensLC5Font(int32 FontSize, bool bBold);

struct FBlueprintLensLC5ActionLayout
{
	FString ActionId;
	FString Label;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC5SourceAnchor
{
	FString OccurrenceId;
	FString SourceNodeId;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FBlueprintLensLC5Layout
{
	EBlueprintLensLC5LayoutMode Mode = EBlueprintLensLC5LayoutMode::SideBySide700;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FBlueprintLensLayoutLedger VisualOracleLedger;
	TArray<FBlueprintLensLC5Label> Labels;
	TArray<FBlueprintLensLC5ActionLayout> Actions;
	TArray<FBlueprintLensLC5SourceAnchor> SourceAnchors;
	TMap<FString, int32> StaticRanks;
	TArray<FString> EndpointGlyphIds;
	FBox2D HeaderBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CriterionBounds = FBox2D(EForceInit::ForceInit);
	FBox2D PlotBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CallerBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CalleeBounds = FBox2D(EForceInit::ForceInit);
	FBox2D PortalBounds = FBox2D(EForceInit::ForceInit);
	FVector2D PortalStart = FVector2D::ZeroVector;
	FVector2D PortalEnd = FVector2D::ZeroVector;
	FBox2D FrontierBounds = FBox2D(EForceInit::ForceInit);
	FBox2D ActionsBounds = FBox2D(EForceInit::ForceInit);
	FString CallOccurrenceId;
	FString EntryOccurrenceId;
	FString OperatorOccurrenceId;
	FString ReturnOccurrenceId;
	FString DiagnosticCode;

	bool CoversProjection(const FBlueprintLensLC5Projection& Projection) const;
	bool HasValidSharedLedger() const;
	bool MatchesVisualOracle(float Tolerance = 1.0f) const;
	bool HasNoLabelCollisions() const;
	bool HasNoRouteNodeCollisions() const;
	bool HasCompleteEndpointGlyphs() const;
	bool HasStrictStaticRankOrder() const;
	bool HasNoTextOrRouteCollisions() const;
};

class FBlueprintLensLC5LayoutBuilder
{
public:
	static FBlueprintLensLC5Layout Build(
		const FBlueprintLensLC5Projection& Projection,
		float TargetWidth);
};
