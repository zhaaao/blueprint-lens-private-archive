#pragma once

#include "BlueprintLensLC7LayoutSession.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC7UnitSelected, FString);
DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC7Action, FString);

enum class EBlueprintLensLC7RoutePattern : uint8
{
	Solid,
	Dashed,
	Dotted
};

enum class EBlueprintLensLC7RouteMarker : uint8
{
	Triangle,
	Diamond,
	Circle,
	ReturnArrow,
	Square
};

struct FBlueprintLensLC7RelationVisualEncoding
{
	EBlueprintLensLC7RoutePattern Pattern =
		EBlueprintLensLC7RoutePattern::Solid;
	EBlueprintLensLC7RouteMarker Marker =
		EBlueprintLensLC7RouteMarker::Triangle;
	FLinearColor Tint = FLinearColor::White;
	float Thickness = 2.0f;
};

FLinearColor BlueprintLensLC7RoundedBrushFill();
FLinearColor BlueprintLensLC7BoxElementTint(const FLinearColor& Fill);
FBlueprintLensLC7RelationVisualEncoding BlueprintLensLC7RelationEncoding(
	EBlueprintLensLC7RelationFamily Family);

class SBlueprintLensLC7AdaptiveBackbone final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC7AdaptiveBackbone)
		: _SelectedUnitId(FString())
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC7Projection, Projection)
		SLATE_ARGUMENT(FBlueprintLensLC7LayoutSessionResult, InitialSession)
		SLATE_ATTRIBUTE(FString, SelectedUnitId)
		SLATE_EVENT(FOnBlueprintLensLC7UnitSelected, OnUnitSelected)
		SLATE_EVENT(FOnBlueprintLensLC7Action, OnAction)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsUnitSelected(const FString& UnitId) const;
	FString ResolveUnitAtLocalPositionForTesting(const FVector2D& Local) const;
	FString ResolveSelectionAtLocalPositionForTesting(
		const FVector2D& Local) const;
	FString ResolveActionAtLocalPositionForTesting(const FVector2D& Local) const;
	TArray<FBox2D> GetMemberHitTargetsForTesting() const;
	TArray<FBox2D> GetActionHitTargetsForTesting() const;
	float GetUnitOutlineWidthForTesting(const FString& UnitId) const;
	int32 GetRoutePaintLayerForTesting() const { return 1; }
	int32 GetNodePaintLayerForTesting() const { return 2; }
	int32 GetTextPaintLayerForTesting() const { return 3; }
	int32 GetVisibleSCCCountForTesting() const;
	int32 GetVisibleCriterionCountForTesting() const;
	int32 GetVisibleSourceAnchorCountForTesting() const;
	int32 GetCountedFoldAffordanceCountForTesting() const;
	int32 GetCountedIndexAffordanceCountForTesting() const;
	int32 GetParagraphTextCountForTesting() const;

	const FBlueprintLensLC7Layout& GetLayoutForTesting() const
	{
		return Session.Layout;
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	void RebuildLabels();
	const FBlueprintLensLC7SCCRecord* FindFocusedSCC() const;

	FBlueprintLensLC7Projection Projection;
	FBlueprintLensLC7LayoutSessionResult Session;
	TAttribute<FString> SelectedUnitId;
	FOnBlueprintLensLC7UnitSelected OnUnitSelected;
	FOnBlueprintLensLC7Action OnAction;
	TArray<FString> PaintedLabelTexts;
};
