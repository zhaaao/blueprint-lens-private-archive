#pragma once

#include "BlueprintLensLC3DerivationSpineLayout.h"
#include "BlueprintLensLC3ValueConeLayoutSession.h"
#include "Widgets/SCompoundWidget.h"

class SCanvas;

enum class EBlueprintLensLC3ValueConeDensity : uint8
{
	Summary,
	Ports,
	Evidence
};

DECLARE_DELEGATE_OneParam(
	FOnBlueprintLensLC3ValueConeUnitSelected,
	const FString&);

class SBlueprintLensLC3ValueConeCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC3ValueConeCanvas)
		: _Density(EBlueprintLensLC3ValueConeDensity::Ports)
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC3ValueConeProjection, Projection)
		SLATE_ARGUMENT(
			FBlueprintLensLC3ValueConeLayoutSessionResult,
			InitialSession)
		SLATE_ATTRIBUTE(FString, SelectedUnitId)
		SLATE_ATTRIBUTE(EBlueprintLensLC3ValueConeDensity, Density)
		SLATE_EVENT(
			FOnBlueprintLensLC3ValueConeUnitSelected,
			OnUnitSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FVector2D GetSurfaceSize() const
	{
		return DerivationSpineLayout.IsRenderable(Projection)
			? DerivationSpineLayout.CanvasSize
			: Session.Layout.LayoutLedger.CanvasSize;
	}

	const FBlueprintLensLC3ValueConeLayout& GetLayoutForTesting() const
	{
		return Session.Layout;
	}

	const FBlueprintLensLC3ValueConeLayoutSessionResult&
	GetSessionForTesting() const
	{
		return Session;
	}

	const FBlueprintLensLC3DerivationSpineLayout&
	GetDerivationSpineLayoutForTesting() const
	{
		return DerivationSpineLayout;
	}

	bool IsUsingDerivationSpineForTesting() const
	{
		return DerivationSpineLayout.IsRenderable(Projection);
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
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TSharedRef<SWidget> BuildNodeCard(
		const FBlueprintLensLC3ValueConeLayoutNode& Node);
	TSharedRef<SWidget> BuildSurfaceLabel(
		const FBlueprintLensLC3DerivationSpineLabel& Label);
	FVector2D NodePosition(const FString& UnitId) const;
	FVector2D NodeSize(const FString& UnitId) const;
	void RebuildSession(float TargetWidth);
	void RebuildDerivationSpineLayout();
	void RebuildVisualChildren();
	int32 PaintRibbonFallback(
		const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;
	int32 PaintDerivationSpine(
		const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId) const;

	FBlueprintLensLC3ValueConeProjection Projection;
	FBlueprintLensLC3ValueConeLayoutSessionResult Session;
	FBlueprintLensLC3DerivationSpineLayout DerivationSpineLayout;
	TSharedPtr<SCanvas> VisualCanvas;
	TAttribute<FString> SelectedUnitId;
	TAttribute<EBlueprintLensLC3ValueConeDensity> Density;
	FOnBlueprintLensLC3ValueConeUnitSelected OnUnitSelected;
	float LastLayoutWidth = 0.0f;
	float PendingLayoutWidth = 0.0f;
	double PendingLayoutStartTime = 0.0;
};
