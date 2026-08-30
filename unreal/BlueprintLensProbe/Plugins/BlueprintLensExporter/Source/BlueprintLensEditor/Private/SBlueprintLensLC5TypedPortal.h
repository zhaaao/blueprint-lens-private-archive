#pragma once

#include "BlueprintLensLC5LayoutSession.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC5Action, FString);
DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC5OccurrenceSelected, FString);

FLinearColor BlueprintLensLC5RoundedBrushFill();
FLinearColor BlueprintLensLC5BoxElementTint(const FLinearColor& Fill);

class SBlueprintLensLC5TypedPortal final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC5TypedPortal)
		: _SelectedOccurrenceId(FString())
		, _ActiveActionId(TEXT("select"))
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC5Projection, Projection)
		SLATE_ARGUMENT(FBlueprintLensLC5LayoutSessionResult, InitialSession)
		SLATE_ATTRIBUTE(FString, SelectedOccurrenceId)
		SLATE_ATTRIBUTE(FString, ActiveActionId)
		SLATE_EVENT(FOnBlueprintLensLC5Action, OnAction)
		SLATE_EVENT(FOnBlueprintLensLC5OccurrenceSelected, OnOccurrenceSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	bool IsActionActive(const FString& ActionId) const;
	TArray<FString> HighlightedRelationIdsForTesting() const;

	const FBlueprintLensLC5Layout& GetLayoutForTesting() const
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
	bool IsRelationHighlighted(const FString& RelationId) const;

	FBlueprintLensLC5Projection Projection;
	FBlueprintLensLC5LayoutSessionResult Session;
	TAttribute<FString> SelectedOccurrenceId;
	TAttribute<FString> ActiveActionId;
	FOnBlueprintLensLC5Action OnAction;
	FOnBlueprintLensLC5OccurrenceSelected OnOccurrenceSelected;
};
