#pragma once

#include "BlueprintLensLC2GuardSurfaceLayout.h"
#include "Widgets/SCompoundWidget.h"

class SCanvas;

enum class EBlueprintLensLC2GuardDensity : uint8
{
	Summary,
	Evidence
};

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensLC2GuardUnitSelected,
	const FString&);

class SBlueprintLensLC2GuardCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC2GuardCanvas)
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC2GuardSurfaceProjection, Projection)
		SLATE_ARGUMENT(
			FBlueprintLensLC2GuardLayoutSessionResult,
			InitialSession)
		SLATE_ARGUMENT(
			TSharedPtr<const FBlueprintLensExplanationModel>,
			Explanation)
		SLATE_ATTRIBUTE(FString, SelectedUnitId)
		SLATE_EVENT(
			FOnBlueprintLensLC2GuardUnitSelected,
			OnUnitSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const FBlueprintLensLC2GuardSurfaceLayout& GetSurface() const
	{
		return Surface;
	}

	// The panel rebuilds this widget on every selection, so it has to hand the
	// width back or the next canvas restarts at the session default and visibly
	// reflows from the wide grammar to the compact one.
	float GetLayoutWidth() const
	{
		return LastLayoutWidth;
	}

	const FBlueprintLensLC2GuardSurfaceLayout& GetSurfaceForTesting() const
	{
		return Surface;
	}

	const FBlueprintLensLC2GuardLayoutSessionResult&
	GetSessionForTesting() const
	{
		return Session;
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
	FString SelectedGuardGroupId() const;
	void RebuildSession(float TargetWidth);
	void RebuildSurface();
	void RebuildVisualChildren();
	TSharedRef<SWidget> BuildSurfaceLabel(
		const FBlueprintLensLC2GuardSurfaceLabel& Label);

	FBlueprintLensLC2GuardSurfaceProjection Projection;
	FBlueprintLensLC2GuardLayoutSessionResult Session;
	FBlueprintLensLC2GuardSurfaceLayout Surface;
	TSharedPtr<const FBlueprintLensExplanationModel> Explanation;
	TSharedPtr<SCanvas> VisualCanvas;
	TAttribute<FString> SelectedUnitId;
	FOnBlueprintLensLC2GuardUnitSelected OnUnitSelected;
	float LastLayoutWidth = 0.0f;
	float PendingLayoutWidth = 0.0f;
	double PendingLayoutStartTime = 0.0;
};
