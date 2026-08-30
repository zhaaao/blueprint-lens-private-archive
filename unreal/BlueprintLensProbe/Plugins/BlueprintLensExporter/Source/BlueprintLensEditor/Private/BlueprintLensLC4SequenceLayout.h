#pragma once

#include "BlueprintLensLayoutContract.h"
#include "BlueprintLensLC4SequenceProjection.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC4SequenceLayoutMode : uint8
{
	Narrow430,
	Compact480,
	Wide700
};

enum class EBlueprintLensLC4SequenceVisualNodeKind : uint8
{
	SequenceSpine,
	Included,
	Outside,
	Reconverged,
	Criterion
};

struct FBlueprintLensLC4SequenceStationLayout
{
	int32 Ordinal = INDEX_NONE;
	FVector2D Center = FVector2D::ZeroVector;
	float Radius = 0.0f;
	FBox2D LabelBounds = FBox2D(EForceInit::ForceInit);
	FBox2D HitBounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC4SequenceVisualNode
{
	FString UnitId;
	FString PrimaryText;
	FString SecondaryText;
	EBlueprintLensLC4SequenceVisualNodeKind Kind =
		EBlueprintLensLC4SequenceVisualNodeKind::Included;
	int32 Ordinal = INDEX_NONE;
};

struct FBlueprintLensLC4SequenceActionLayout
{
	FString ActionId;
	FString Label;
	FBox2D Bounds = FBox2D(EForceInit::ForceInit);
};

struct FBlueprintLensLC4SequenceLayout
{
	bool bLiveExplanation = false;
	EBlueprintLensLC4SequenceLayoutMode Mode =
		EBlueprintLensLC4SequenceLayoutMode::Wide700;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FVector2D TitlePosition = FVector2D::ZeroVector;
	TArray<FVector2D> SubtitlePositions;
	FBox2D OrderBandBounds = FBox2D(EForceInit::ForceInit);
	FVector2D OrderStagePosition = FVector2D::ZeroVector;
	FVector2D OrderTextPosition = FVector2D::ZeroVector;
	TArray<FVector2D> CountTextPositions;
	FVector2D PinOrderPosition = FVector2D::ZeroVector;
	FVector2D SpineStart = FVector2D::ZeroVector;
	FVector2D SpineEnd = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC4SequenceStationLayout> Stations;
	TArray<FBlueprintLensLC4SequenceVisualNode> VisualNodes;
	FVector2D MergeCenter = FVector2D::ZeroVector;
	float MergeRadius = 0.0f;
	TArray<FVector2D> MergeToSuffixRoute;
	FBox2D WarningBounds = FBox2D(EForceInit::ForceInit);
	float OutsideTerminalX = 0.0f;
	FVector2D OutsideTerminalStart = FVector2D::ZeroVector;
	FVector2D OutsideTerminalEnd = FVector2D::ZeroVector;
	FVector2D OutsideLabelPosition = FVector2D::ZeroVector;
	TArray<FString> OutsideDetailLines;
	TArray<FVector2D> OutsideDetailLinePositions;
	TArray<FVector2D> OutsideDetailLineSizes;
	FBox2D OutsideDetailBounds = FBox2D(EForceInit::ForceInit);
	int32 OutsideDetailFontSize = 10;
	float UnconnectedStubStartX = 0.0f;
	float UnconnectedStubEndX = 0.0f;
	FVector2D UnconnectedXTopLeft = FVector2D::ZeroVector;
	FVector2D UnconnectedXBottomRight = FVector2D::ZeroVector;
	FVector2D UnconnectedLabelPosition = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC4SequenceActionLayout> Actions;
	FVector2D FooterPosition = FVector2D::ZeroVector;
	FString FooterText;
	FBlueprintLensLayoutRequest LayoutRequest;
	FBlueprintLensLayoutLedger LayoutLedger;
	FBlueprintLensLayoutLedger VisualOracleLedger;
	FString DiagnosticCode;

	const FBlueprintLensLC4SequenceStationLayout* FindStation(int32 Ordinal) const;
	const FBlueprintLensLC4SequenceVisualNode* FindVisualNode(
		const FString& UnitId) const;
	bool CoversProjection(
		const FBlueprintLensLC4SequenceProjection& Projection) const;
	bool HasValidSharedLedger() const;
	bool MatchesVisualOracle(float Tolerance = 1.0f) const;
	bool HasNoLabelRouteCollisions() const;
};

class FBlueprintLensLC4SequenceLayoutBuilder
{
public:
	static FBlueprintLensLC4SequenceLayout Build(
		const FBlueprintLensLC4SequenceProjection& Projection,
		float TargetWidth);
};
