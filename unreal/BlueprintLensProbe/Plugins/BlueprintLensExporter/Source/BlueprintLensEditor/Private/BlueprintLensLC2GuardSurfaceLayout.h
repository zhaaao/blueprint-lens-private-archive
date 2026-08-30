#pragma once

#include "BlueprintLensLC2GuardLayoutSession.h"

struct FBlueprintLensLC2GuardSurfaceLabel
{
	FString Key;
	FString UnitId;
	FString Text;
	FBox2D ExclusionBounds;
};

struct FBlueprintLensLC2GuardSurfaceGate
{
	FString GroupId;
	FString ParentGroupId;
	FString PredicateUnitId;
	FString BranchUnitId;
	FBox2D Bounds;
	FBox2D FocusBounds;
	bool bSelected = false;
};

struct FBlueprintLensLC2GuardSurfaceRail
{
	FString GroupId;
	FString OwnerGuardGroupId;
	FString OutcomeUnitId;
	bool bFolded = false;
	TArray<FVector2D> Points;
};

struct FBlueprintLensLC2GuardOutcomeFold
{
	TArray<FString> FoldedOutcomeGroupIds;
	FString ReaderText;
};

struct FBlueprintLensLC2GuardSurfaceLayout
{
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FString> CanonicalUnitIds;
	TArray<FBlueprintLensLC2GuardSurfaceLabel> Labels;
	TArray<FBlueprintLensLC2GuardSurfaceGate> Gates;
	TArray<FBlueprintLensLC2GuardSurfaceRail> Rails;
	TArray<FVector2D> EntryRoutePoints;
	FBox2D CriterionDockBounds;
	TArray<FBlueprintLensLC2GuardForkMark> ForkMarks;
	FBlueprintLensLC2GuardOutcomeFold OutcomeFold;
	FString SelectedGuardGroupId;
	FString BaseLedgerFingerprint;
	FString DiagnosticCode;

	int32 DrawnOutcomeCount() const;
	bool EveryDrawnRailReachesCriterion() const;
	bool HasNoLabelIntersections() const;
	bool HasNoRailObstacleIntersections() const;
	bool HasNoEntryRouteLabelIntersections() const;
	FString FirstIntersectionDiagnostic() const;
	bool IsRenderable(const FBlueprintLensLC2GuardSurfaceProjection& Projection) const;
};

class FBlueprintLensLC2GuardSurfaceLayoutBuilder
{
public:
	static FBlueprintLensLC2GuardSurfaceLayout Build(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection,
		const FBlueprintLensLC2GuardLayoutSessionResult& Session,
		float TargetWidth,
		const FString& SelectedGuardGroupId);
};
