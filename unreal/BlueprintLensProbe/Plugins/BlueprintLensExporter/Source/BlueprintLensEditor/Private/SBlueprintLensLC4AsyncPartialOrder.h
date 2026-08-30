#pragma once

#include "BlueprintLensLC4AsyncLayoutSession.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnBlueprintLensLC4AsyncAction, FString);

enum class EBlueprintLensLC4AsyncArrowStyle : uint8
{
	FilledTriangle
};

struct FBlueprintLensLC4AsyncVisualStyle
{
	FString CanvasFillHex;
	FString ActionsFillHex;
	FString ActionsStrokeHex;
	FString CyanHex;
	FString OrangeHex;
	FString PurpleHex;
	FString GoldHex;
	FString GreenHex;
	FString PrimaryTextHex;
	FString MutedTextHex;
	float LaunchRadius = 9.0f;
	float EventRadius = 12.0f;
	float NodeOutlineWidth = 3.0f;
	float SourceRouteWidth = 3.0f;
	float BarrierRadius = 2.0f;
	float CriterionRadius = 8.0f;
	float FrontierRadius = 6.0f;
	float ActionsRadius = 8.0f;
	EBlueprintLensLC4AsyncArrowStyle ArrowStyle =
		EBlueprintLensLC4AsyncArrowStyle::FilledTriangle;

	static const FBlueprintLensLC4AsyncVisualStyle& FrozenEffectTarget();
};

FLinearColor BlueprintLensLC4AsyncRoundedBrushFill();
FLinearColor BlueprintLensLC4AsyncBoxElementTint(const FLinearColor& Fill);

class SBlueprintLensLC4AsyncPartialOrder final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensLC4AsyncPartialOrder)
		: _ActiveActionId(TEXT("select"))
	{
	}
		SLATE_ARGUMENT(FBlueprintLensLC4AsyncProjection, Projection)
		SLATE_ARGUMENT(FBlueprintLensLC4AsyncLayoutSessionResult, InitialSession)
		SLATE_ATTRIBUTE(FString, ActiveActionId)
		SLATE_EVENT(FOnBlueprintLensLC4AsyncAction, OnAction)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	bool IsActionActive(const FString& ActionId) const;

	const FBlueprintLensLC4AsyncLayout& GetLayoutForTesting() const
	{
		return Session.Layout;
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
	void RebuildSession(float TargetWidth);
	void RebuildLabels();

	FBlueprintLensLC4AsyncProjection Projection;
	FBlueprintLensLC4AsyncLayoutSessionResult Session;
	TAttribute<FString> ActiveActionId;
	FOnBlueprintLensLC4AsyncAction OnAction;
	float LastLayoutWidth = 700.0f;
	float PendingLayoutWidth = 700.0f;
	double PendingLayoutStartTime = 0.0;
};
