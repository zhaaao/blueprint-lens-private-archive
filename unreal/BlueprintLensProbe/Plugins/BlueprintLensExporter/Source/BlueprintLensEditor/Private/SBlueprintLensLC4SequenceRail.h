#pragma once

#include "BlueprintLensLC4SequenceLayoutSession.h"
#include "Widgets/SCompoundWidget.h"

class SCanvas;

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensLC4SequenceOutputSelected,
	int32);
DECLARE_DELEGATE(FOnBlueprintLensLC4SequenceAction);

class SBlueprintLensLC4SequenceRail final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC4SequenceRail)
		: _SelectedOrdinal(INDEX_NONE)
		, _Evidence(false)
		, _ActiveActionId(TEXT("select"))
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC4SequenceProjection, Projection)
		SLATE_ARGUMENT(
			FBlueprintLensLC4SequenceLayoutSessionResult,
			InitialSession)
		SLATE_ATTRIBUTE(int32, SelectedOrdinal)
		SLATE_ATTRIBUTE(bool, Evidence)
		SLATE_ATTRIBUTE(FString, ActiveActionId)
		SLATE_EVENT(
			FOnBlueprintLensLC4SequenceOutputSelected,
			OnOutputSelected)
		SLATE_EVENT(
			FOnBlueprintLensLC4SequenceAction,
			OnShowAllText)
		SLATE_EVENT(
			FOnBlueprintLensLC4SequenceAction,
			OnToggleEvidence)
		SLATE_EVENT(
			FOnBlueprintLensLC4SequenceAction,
			OnOpenSource)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	bool IsActionActive(const FString& ActionId) const;

	const FBlueprintLensLC4SequenceProjection& GetProjectionForTesting() const
	{
		return Projection;
	}

	const FBlueprintLensLC4SequenceLayout& GetLayoutForTesting() const
	{
		return Session.Layout;
	}

	const FBlueprintLensLC4SequenceLayoutSessionResult&
	GetSessionForTesting() const
	{
		return Session;
	}

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
	virtual FVector2D ComputeDesiredSize(
		float LayoutScaleMultiplier) const override;

private:
	void RebuildSession(float TargetWidth);
	void RebuildVisualChildren();

	FBlueprintLensLC4SequenceProjection Projection;
	FBlueprintLensLC4SequenceLayoutSessionResult Session;
	TSharedPtr<SCanvas> VisualCanvas;
	TAttribute<int32> SelectedOrdinal;
	TAttribute<bool> Evidence;
	TAttribute<FString> ActiveActionId;
	FOnBlueprintLensLC4SequenceOutputSelected OnOutputSelected;
	FOnBlueprintLensLC4SequenceAction OnShowAllText;
	FOnBlueprintLensLC4SequenceAction OnToggleEvidence;
	FOnBlueprintLensLC4SequenceAction OnOpenSource;
	float LastLayoutWidth = 0.0f;
	float PendingLayoutWidth = 0.0f;
	double PendingLayoutStartTime = 0.0;
};
