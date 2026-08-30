#pragma once

#include "BlueprintLensLC1RailSurfaceLayout.h"
#include "Widgets/SCompoundWidget.h"

class SCanvas;

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensLC1RailUnitSelected,
	const FString&);

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensCompositeDisclosureToggled,
	const FString&);

DECLARE_DELEGATE_TwoParams(
	FOnBlueprintLensCompositeAttachmentDisclosureToggled,
	const FString&,
	const FString&);

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensCompositeBetweenDisclosureToggled,
	const FString&);

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensCompositeSpanDisclosureToggled,
	const FString&);

DECLARE_DELEGATE(FOnBlueprintLensCompositeFoldToggled);

class SBlueprintLensLC1RailCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC1RailCanvas)
		: _CurrentRadius(INDEX_NONE)
		, _DataAnswer(false)
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC1RailProjection, Projection)
		SLATE_ARGUMENT(
			FBlueprintLensLC1RailLayoutSessionResult,
			InitialSession)
		SLATE_ARGUMENT(
			TSharedPtr<const FBlueprintLensExplanationModel>,
			Explanation)
		SLATE_ARGUMENT(FBlueprintLensCompositeRailSlots, CompositeSlots)
		SLATE_ARGUMENT(int32, CurrentRadius)
		SLATE_ARGUMENT(bool, DataAnswer)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, ExpandedStationAppearance)
		SLATE_ARGUMENT(FString, ExpandedStationAppearanceUnitId)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, ExpandedBetweenDecoration)
		SLATE_ARGUMENT(FString, ExpandedBetweenDecorationRelationId)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, ExpandedSpanAttachment)
		SLATE_ARGUMENT(FString, ExpandedSpanAttachmentId)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, ExpandedTerminalAttachment)
		SLATE_ARGUMENT(FString, ExpandedTerminalAttachmentUnitId)
		SLATE_ATTRIBUTE(FString, SelectedUnitId)
		SLATE_EVENT(
			FOnBlueprintLensLC1RailUnitSelected,
			OnUnitSelected)
		SLATE_EVENT(
			FOnBlueprintLensCompositeDisclosureToggled,
			OnDisclosureToggled)
		SLATE_EVENT(
			FOnBlueprintLensCompositeAttachmentDisclosureToggled,
			OnAttachmentDisclosureToggled)
		SLATE_EVENT(
			FOnBlueprintLensCompositeBetweenDisclosureToggled,
			OnBetweenDisclosureToggled)
		SLATE_EVENT(
			FOnBlueprintLensCompositeSpanDisclosureToggled,
			OnSpanDisclosureToggled)
		SLATE_EVENT(
			FOnBlueprintLensCompositeFoldToggled,
			OnFoldToggled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const FBlueprintLensLC1RailSurfaceLayout& GetSurface() const
	{
		return Surface;
	}

	float GetLayoutWidth() const
	{
		return LastLayoutWidth;
	}

	const FBlueprintLensLC1RailSurfaceLayout& GetSurfaceForTesting() const
	{
		return Surface;
	}

	const FBlueprintLensLC1RailLayoutSessionResult&
	GetSessionForTesting() const
	{
		return Session;
	}

	const FBlueprintLensCompositeRailSlots& GetCompositeSlotsForTesting() const
	{
		return CompositeSlots;
	}

	FString ResolveUnitAtLocalPositionForTesting(
		const FVector2D& LocalPosition) const;

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

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
	void RebuildSession(float TargetWidth);
	void RebuildSurface();
	void RebuildVisualChildren();
	TSharedRef<SWidget> BuildSurfaceLabel(
		const FBlueprintLensLC1RailSurfaceLabel& Label);

	FBlueprintLensLC1RailProjection Projection;
	FBlueprintLensCompositeRailSlots CompositeSlots;
	FBlueprintLensLC1RailLayoutSessionResult Session;
	FBlueprintLensLC1RailSurfaceLayout Surface;
	TSharedPtr<const FBlueprintLensExplanationModel> Explanation;
	TSharedPtr<SWidget> ExpandedStationAppearance;
	FString ExpandedStationAppearanceUnitId;
	TSharedPtr<SWidget> ExpandedBetweenDecoration;
	FString ExpandedBetweenDecorationRelationId;
	TSharedPtr<SWidget> ExpandedSpanAttachment;
	FString ExpandedSpanAttachmentId;
	TSharedPtr<SWidget> ExpandedTerminalAttachment;
	FString ExpandedTerminalAttachmentUnitId;
	TSharedPtr<SCanvas> VisualCanvas;
	TAttribute<FString> SelectedUnitId;
	FOnBlueprintLensLC1RailUnitSelected OnUnitSelected;
	FOnBlueprintLensCompositeDisclosureToggled OnDisclosureToggled;
	FOnBlueprintLensCompositeAttachmentDisclosureToggled
		OnAttachmentDisclosureToggled;
	FOnBlueprintLensCompositeBetweenDisclosureToggled
		OnBetweenDisclosureToggled;
	FOnBlueprintLensCompositeSpanDisclosureToggled OnSpanDisclosureToggled;
	FOnBlueprintLensCompositeFoldToggled OnFoldToggled;
	int32 CurrentRadius = INDEX_NONE;
	bool bDataAnswer = false;
	float LastLayoutWidth = 0.0f;
	float PendingLayoutWidth = 0.0f;
	double PendingLayoutStartTime = 0.0;
};
