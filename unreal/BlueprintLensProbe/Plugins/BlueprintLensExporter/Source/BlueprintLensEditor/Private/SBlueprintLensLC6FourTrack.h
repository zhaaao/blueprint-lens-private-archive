#pragma once

#include "BlueprintLensLC6LayoutSession.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC6ScenarioSelected, FString);
DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC6Action, FString);

FLinearColor BlueprintLensLC6RoundedBrushFill();
FLinearColor BlueprintLensLC6BoxElementTint(const FLinearColor& Fill);

class SBlueprintLensLC6FourTrack final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC6FourTrack)
		: _SelectedScenarioId(FString())
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC6Projection, Projection)
		SLATE_ARGUMENT(FBlueprintLensLC6LayoutSessionResult, InitialSession)
		SLATE_ATTRIBUTE(FString, SelectedScenarioId)
		SLATE_EVENT(FOnBlueprintLensLC6ScenarioSelected, OnScenarioSelected)
		SLATE_EVENT(FOnBlueprintLensLC6Action, OnAction)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsScenarioSelected(const FString& ScenarioId) const;
	FString ResolveScenarioAtLocalPositionForTesting(const FVector2D& Local) const;
	FString ResolveActionAtLocalPositionForTesting(const FVector2D& Local) const;
	TArray<FBox2D> GetScenarioHitTargetsForTesting() const;
	int32 GetTrackBackgroundPaintLayerForTesting() const { return 1; }
	int32 GetRoutePaintLayerForTesting() const { return 2; }
	int32 GetNodePaintLayerForTesting() const { return 3; }
	int32 GetCriterionMarkerCountForTesting() const;
	int32 GetSemanticFenceCountForTesting() const;
	int32 GetFrontierCountForTesting() const;

	const FBlueprintLensLC6Layout& GetLayoutForTesting() const
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

	FBlueprintLensLC6Projection Projection;
	FBlueprintLensLC6LayoutSessionResult Session;
	TAttribute<FString> SelectedScenarioId;
	FOnBlueprintLensLC6ScenarioSelected OnScenarioSelected;
	FOnBlueprintLensLC6Action OnAction;
};
