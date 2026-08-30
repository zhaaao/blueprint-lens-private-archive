#pragma once

#include "BlueprintLensCompositeRailSlots.h"
#include "BlueprintLensLC1RailLayoutSession.h"

struct FBlueprintLensLC1RailSurfaceLabel
{
	FString Key;
	FString UnitId;
	FString Text;
	FBox2D MeasuredBounds;
};

struct FBlueprintLensLC1RailStation
{
	FString UnitId;
	FVector2D Position = FVector2D::ZeroVector;
	FBox2D HitRegion;
	bool bIsCriterion = false;
	bool bIsGuard = false;
	int32 GuardNestingDepth = 0;
	bool bGuardDetailExpanded = false;
	bool bBesideAttachmentExpanded = false;
	FBox2D GuardAppearanceBounds;
	TArray<FBox2D> BesideAttachmentBounds;
	FBox2D ExpandedAppearanceBounds;
};

struct FBlueprintLensLC1RailBetweenDecoration
{
	FString RelationId;
	FBox2D ActionBounds;
	FBox2D ExpandedContentBounds;
};

struct FBlueprintLensLC1RailSpanDecoration
{
	FString SpanId;
	FBox2D ActionBounds;
	FBox2D ExpandedContentBounds;
};

struct FBlueprintLensLC1RailRadiusState
{
	TArray<FString> DrawnUnitIds;
	TArray<FString> FoldedUnitIds;
	TArray<FString> RetainedBoundaryCapIds;
	int32 CurrentRadius = 0;
	int32 DefaultRadius = 0;
	int32 FoldedAttachmentStationCount = 0;
	int32 FoldedAttachmentCount = 0;
	FString ReaderText;
	FString FoldReaderText;
	FBox2D FoldBoundaryBounds;
};

struct FBlueprintLensLC1RailSurfaceLayout
{
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FString> CanonicalUnitIds;
	TArray<FBlueprintLensLC1RailSurfaceLabel> Labels;
	TArray<FBlueprintLensLC1RailStation> Stations;
	TArray<FBlueprintLensLC1RailBetweenDecoration> BetweenDecorations;
	TArray<FBlueprintLensLC1RailSpanDecoration> SpanDecorations;
	TArray<FVector2D> SpineRoute;
	TArray<FBox2D> BoundaryCapBounds;
	TMap<FString, FBox2D> ExpandedTerminalAttachmentBounds;
	FBox2D ScaleRuleBounds;
	FBox2D CriterionDockBounds;
	FBox2D SelectionExplanationBounds;
	FBlueprintLensLC1RailRadiusState Radius;
	FString DiagnosticCode;

	int32 DrawnUnitCount() const;
	FString InvariantDiagnostic(
		const FBlueprintLensLC1RailProjection& Projection) const;
	bool HasNoLabelIntersections() const;
	bool HasNoLabelRouteIntersections() const;
	bool IsRenderable(const FBlueprintLensLC1RailProjection& Projection) const;
};

class FBlueprintLensLC1RailSurfaceLayoutBuilder
{
public:
	static FBlueprintLensLC1RailSurfaceLayout Build(
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensLC1RailLayoutSessionResult& Session,
		float TargetWidth,
		int32 CurrentRadius = INDEX_NONE,
		int32 DefaultRadius = 13);

	static FBlueprintLensLC1RailSurfaceLayout Build(
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensLC1RailLayoutSessionResult& Session,
		const FBlueprintLensCompositeRailSlots& Slots,
		float TargetWidth,
		int32 CurrentRadius = INDEX_NONE,
		int32 DefaultRadius = 13,
		bool bDataAnswer = false);
};
