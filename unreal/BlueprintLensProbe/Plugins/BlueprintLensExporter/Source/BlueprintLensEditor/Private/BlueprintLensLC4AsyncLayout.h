#pragma once

#include "BlueprintLensLC4AsyncProjection.h"
#include "BlueprintLensLayoutContract.h"
#include "Fonts/SlateFontInfo.h"

enum class EBlueprintLensLC4AsyncLayoutMode : uint8
{
	Narrow430,
	Compact480,
	Wide700
};

enum class EBlueprintLensLC4AsyncFontWeight : uint8
{
	Regular,
	Semibold,
	Bold
};

struct FBlueprintLensLC4AsyncLabel
{
	FString Id;
	FString Text;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D ApproximateSize = FVector2D::ZeroVector;
	int32 FontSize = 10;
	EBlueprintLensLC4AsyncFontWeight Weight = EBlueprintLensLC4AsyncFontWeight::Regular;
};

FSlateFontInfo BlueprintLensLC4AsyncFont(
	int32 FontSize,
	EBlueprintLensLC4AsyncFontWeight Weight);

struct FBlueprintLensLC4AsyncActionLayout
{
	FString ActionId;
	FString Label;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC4AsyncLayout
{
	EBlueprintLensLC4AsyncLayoutMode Mode = EBlueprintLensLC4AsyncLayoutMode::Wide700;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FBlueprintLensLayoutLedger VisualOracleLedger;
	TArray<FBlueprintLensLC4AsyncLabel> Labels;
	TArray<FBlueprintLensLC4AsyncActionLayout> Actions;
	TArray<FBox2D> ProtectedLabelBounds;
	TArray<TArray<FVector2D>> PaintedRoutes;
	FBox2D BarrierBounds = FBox2D(EForceInit::ForceInit);
	FBox2D CriterionBounds = FBox2D(EForceInit::ForceInit);
	FBox2D FrontierBounds = FBox2D(EForceInit::ForceInit);
	FBox2D ActionsBounds = FBox2D(EForceInit::ForceInit);
	FVector2D LaunchA = FVector2D::ZeroVector;
	FVector2D LaunchB = FVector2D::ZeroVector;
	FVector2D ContinuationA = FVector2D::ZeroVector;
	FVector2D ContinuationB = FVector2D::ZeroVector;
	FVector2D CompletionA = FVector2D::ZeroVector;
	FVector2D CompletionB = FVector2D::ZeroVector;
	FVector2D ArrivalA = FVector2D::ZeroVector;
	FVector2D ArrivalB = FVector2D::ZeroVector;
	FVector2D Release = FVector2D::ZeroVector;
	float HeaderRuleY = 0.0f;
	float NarrowProofBracketY = 0.0f;
	FString DiagnosticCode;

	bool CoversProjection(const FBlueprintLensLC4AsyncProjection& Projection) const;
	bool HasValidSharedLedger() const;
	bool MatchesVisualOracle(float Tolerance) const;
	bool HasNoTextOrRouteCollisions() const;
};

class FBlueprintLensLC4AsyncLayoutBuilder
{
public:
	static FBlueprintLensLC4AsyncLayout Build(
		const FBlueprintLensLC4AsyncProjection& Projection,
		float TargetWidth);
};
