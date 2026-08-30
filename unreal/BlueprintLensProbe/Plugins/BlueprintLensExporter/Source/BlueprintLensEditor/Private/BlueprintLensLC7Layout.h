#pragma once

#include "BlueprintLensLC7Projection.h"
#include "BlueprintLensLayoutContract.h"

enum class EBlueprintLensLC7ScaleMode : uint8
{
	Full,
	Focus,
	Index,
	CompleteText
};

enum class EBlueprintLensLC7ResponsiveMode : uint8
{
	SingleColumn430,
	StackedDetail480,
	SideBySide700
};

struct FBlueprintLensLC7TextMetrics
{
	TMap<FString, FVector2D> UnitLabelSizes;
	float AvailableOverviewHeight = 594.0f;
	float AvailableRouteClearance = 16.0f;
	float RequiredRouteClearance = 8.0f;
	float MaxNodeLabelWidth = 100.0f;
	float MaxNodeLabelHeight = 18.0f;
	int32 MaxFullUnitCount = 8;
	int32 MaxFullRelationCount = 8;
	int32 MaxFullSCCCount = 1;

	static FBlueprintLensLC7TextMetrics MeasuredForProjection(
		const FBlueprintLensLC7Projection& Projection);
	bool HasMeasurementsFor(const TSet<FString>& UnitIds) const;
	bool Fits(const TSet<FString>& UnitIds) const;
};

struct FBlueprintLensLC7Fold
{
	FString FoldId;
	FString OwnerSCCId;
	TSet<FString> UnitIds;
	TSet<FString> RelationIds;
	int32 UnitCount = 0;
	int32 RelationCount = 0;
	FString ExpansionActionId;
};

struct FBlueprintLensLC7IndexRow
{
	FString SCCId;
	TSet<FString> UnitIds;
	TSet<FString> RelationIds;
	int32 UnitCount = 0;
	int32 RelationCount = 0;
	FString SourceAnchorUnitId;
	FString ExpansionActionId;
};

struct FBlueprintLensLC7NodeLayout
{
	FString UnitId;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	FBox2D HitBounds = FBox2D(EForceInit::ForceInit);
	FBox2D LabelBounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC7RouteLayout
{
	FString RelationId;
	EBlueprintLensLC7RelationFamily Family =
		EBlueprintLensLC7RelationFamily::Entry;
	FString SourceUnitId;
	FString TargetUnitId;
	TArray<FVector2D> Points;
};

struct FBlueprintLensLC7ActionLayout
{
	FString ActionId;
	FBox2D HitBounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC7Layout
{
	EBlueprintLensLC7ScaleMode ScaleMode =
		EBlueprintLensLC7ScaleMode::CompleteText;
	EBlueprintLensLC7ResponsiveMode ResponsiveMode =
		EBlueprintLensLC7ResponsiveMode::SideBySide700;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FBox2D ContentBounds = FBox2D(EForceInit::ForceInit);
	FBox2D OverviewBounds = FBox2D(EForceInit::ForceInit);
	FBox2D DetailBounds = FBox2D(EForceInit::ForceInit);
	FString FocusedSCCId;
	FString CriterionUnitId;
	FString SelectedUnitId;
	int32 VisibleSCCCount = 0;
	TSet<FString> VisibleUnitIds;
	TSet<FString> VisibleRelationIds;
	TArray<FBlueprintLensLC7Fold> Folds;
	TArray<FBlueprintLensLC7IndexRow> IndexRows;
	TArray<FBlueprintLensLC7NodeLayout> Nodes;
	TArray<FBlueprintLensLC7RouteLayout> Routes;
	TArray<FBlueprintLensLC7ActionLayout> Actions;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FBlueprintLensLayoutLedger VisualOracleLedger;
	FString RecoverabilityHash;
	FString OverviewGeometryHash;
	FString DiagnosticCode;

	bool CoversProjection(const FBlueprintLensLC7Projection& Projection) const;
	bool HasValidSharedLedger() const;
	bool HasValidRecoverability(
		const FBlueprintLensLC7Projection& Projection) const;
	bool HasNonOverlappingHitTargets() const;
	bool HasInBoundsMeasuredLabels() const;
	bool HasDistinctRelationAttachments() const;
	bool HasZeroCollinearRouteOverlap() const;
	bool HasValidBendBudget() const;
	bool HasNoTextOrRouteCollisions() const;
	bool MatchesVisualOracle(float Tolerance = 1.0f) const;
};

class FBlueprintLensLC7LayoutBuilder
{
public:
	static FBlueprintLensLC7Layout Build(
		const FBlueprintLensLC7Projection& Projection,
		float TargetWidth,
		const FString& FocusedSCCId,
		const FBlueprintLensLC7TextMetrics& Metrics);
};
