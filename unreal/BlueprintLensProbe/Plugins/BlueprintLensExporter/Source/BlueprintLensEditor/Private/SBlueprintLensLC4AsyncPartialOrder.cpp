#include "SBlueprintLensLC4AsyncPartialOrder.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

const FBlueprintLensLC4AsyncVisualStyle& FBlueprintLensLC4AsyncVisualStyle::FrozenEffectTarget()
{
	static const FBlueprintLensLC4AsyncVisualStyle Style{
		TEXT("#11151b"), TEXT("#1a2028"), TEXT("#43505c"),
		TEXT("#54d6df"), TEXT("#e9a568"), TEXT("#bda7ff"),
		TEXT("#f3ca62"), TEXT("#76d49b"), TEXT("#f5f7fa"),
		TEXT("#9ba8b4"), 9.0f, 12.0f, 3.0f, 3.0f,
		2.0f, 8.0f, 6.0f, 8.0f,
		EBlueprintLensLC4AsyncArrowStyle::FilledTriangle};
	return Style;
}

FLinearColor BlueprintLensLC4AsyncRoundedBrushFill()
{
	return FLinearColor::White;
}

FLinearColor BlueprintLensLC4AsyncBoxElementTint(const FLinearColor& Fill)
{
	return Fill;
}

namespace
{
FLinearColor Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

void PaintLine(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Tint,
	const float Thickness)
{
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		Layer,
		Geometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Tint,
		true,
		Thickness);
}

void PaintBox(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FBox2D& Bounds,
	const FLinearColor& Fill,
	const FLinearColor& Outline = FLinearColor::Transparent,
	const float OutlineWidth = 0.0f,
	const float CornerRadius = 6.0f)
{
	struct FBrushKey
	{
		float Radius;
		FColor Outline;
		float Width;
		bool operator==(const FBrushKey& Other) const
		{
			return FMath::IsNearlyEqual(Radius, Other.Radius) &&
				Outline == Other.Outline &&
				FMath::IsNearlyEqual(Width, Other.Width);
		}
	};
	static TArray<TPair<FBrushKey, TSharedPtr<FSlateRoundedBoxBrush>>> Cache;
	const FBrushKey Key{
		CornerRadius,
		Outline.ToFColor(true),
		OutlineWidth};
	const FSlateRoundedBoxBrush* Brush = nullptr;
	for (const TPair<FBrushKey, TSharedPtr<FSlateRoundedBoxBrush>>& Entry : Cache)
	{
		if (Entry.Key == Key)
		{
			Brush = Entry.Value.Get();
			break;
		}
	}
	if (Brush == nullptr)
	{
		Cache.Emplace(
			Key,
			MakeShared<FSlateRoundedBoxBrush>(
				FSlateColor(BlueprintLensLC4AsyncRoundedBrushFill()),
				CornerRadius,
				FSlateColor(Outline),
				OutlineWidth,
				FVector2f(FMath::Max(1.0f, CornerRadius * 2.0f))));
		Brush = Cache.Last().Value.Get();
	}
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		Layer,
		Geometry.ToPaintGeometry(Bounds.Max - Bounds.Min, FSlateLayoutTransform(Bounds.Min)),
		Brush,
		ESlateDrawEffect::None,
		BlueprintLensLC4AsyncBoxElementTint(Fill));
}

void PaintCircle(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Center,
	const float Radius,
	const FLinearColor& Fill,
	const FLinearColor& Outline,
	const float OutlineWidth = 2.0f)
{
	constexpr int32 Segments = 24;
	TArray<FVector2D> Ring;
	Ring.Reserve(Segments + 1);
	for (int32 Segment = 0; Segment <= Segments; ++Segment)
	{
		const float Angle =
			2.0f * UE_PI * static_cast<float>(Segment) /
			static_cast<float>(Segments);
		Ring.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
	}
	PaintLine(
		OutDrawElements,
		Geometry,
		Layer,
		Ring,
		Outline,
		OutlineWidth);
}

void PaintArrowHead(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Tip,
	const FVector2D& Direction,
	const FLinearColor& Tint)
{
	const FVector2D Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		return;
	}
	const FVector2D Back = Tip - Unit * 8.0f;
	const FVector2D Perpendicular(-Unit.Y, Unit.X);
	const TArray<FSlateVertex> Vertices = {
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Tip),
			FVector2f(0.5f, 0.0f),
			Tint.ToFColor(true)),
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Back + Perpendicular * 4.0f),
			FVector2f(0.0f, 1.0f),
			Tint.ToFColor(true)),
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Back - Perpendicular * 4.0f),
			FVector2f(1.0f, 1.0f),
			Tint.ToFColor(true))};
	const TArray<SlateIndex> Indices = {0, 1, 2};
	const FSlateResourceHandle WhiteResource =
		FSlateApplication::Get().GetRenderer()->GetResourceHandle(
			*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
	FSlateDrawElement::MakeCustomVerts(
		OutDrawElements,
		Layer,
		WhiteResource,
		Vertices,
		Indices,
		nullptr,
		0,
		0,
		ESlateDrawEffect::None);
}

FLinearColor LabelColor(const FString& Id)
{
	const FBlueprintLensLC4AsyncVisualStyle& Style =
		FBlueprintLensLC4AsyncVisualStyle::FrozenEffectTarget();
	if (Id.StartsWith(TEXT("proof")) || Id == TEXT("variant") || Id == TEXT("observed")) return Color(*Style.PurpleHex);
	if (Id == TEXT("source.caption") || Id == TEXT("dag") || Id == TEXT("header.eyebrow") || Id == TEXT("launch.order")) return Color(*Style.CyanHex);
	if (Id.StartsWith(TEXT("socket")) || Id == TEXT("barrier.caption")) return Color(*Style.GoldHex);
	if (Id == TEXT("barrier")) return Color(*Style.CanvasFillHex);
	if (Id == TEXT("release") || Id == TEXT("criterion.eyebrow")) return Color(*Style.GreenHex);
	if (Id == TEXT("frontier.title")) return Color(*Style.OrangeHex);
	if (Id == TEXT("frontier.row.1") || Id == TEXT("question.0") || Id == TEXT("question.1") || Id == TEXT("counts") || Id == TEXT("sequence") || Id == TEXT("rank")) return Color(*Style.MutedTextHex);
	return Color(*Style.PrimaryTextHex);
}

void PaintDashedLine(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Tint)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	const FVector2D Direction = Length > 0.0f ? Delta / Length : FVector2D::ZeroVector;
	for (float Cursor = 0.0f; Cursor < Length; Cursor += 14.0f)
	{
		PaintLine(OutDrawElements, Geometry, Layer,
			{Start + Direction * Cursor, Start + Direction * FMath::Min(Cursor + 8.0f, Length)}, Tint, 1.0f);
	}
}
} // namespace

void SBlueprintLensLC4AsyncPartialOrder::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	ActiveActionId = InArgs._ActiveActionId;
	OnAction = InArgs._OnAction;
	if (!Session.IsRenderable(Projection))
	{
		Session = FBlueprintLensLC4AsyncLayoutSession::Build(Projection, 700.0f);
	}
	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth > 1.0f
		? Session.Layout.LayoutRequest.TargetWidth : 700.0f;
	RebuildLabels();
}

bool SBlueprintLensLC4AsyncPartialOrder::IsActionActive(const FString& ActionId) const
{
	return ActionId != TEXT("open-source") && ActiveActionId.Get(TEXT("select")) == ActionId;
}

void SBlueprintLensLC4AsyncPartialOrder::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Width = AllottedGeometry.GetLocalSize().X;
	if (Width <= 1.0f || FMath::IsNearlyEqual(Width, LastLayoutWidth, 1.0f))
	{
		return;
	}
	if (!FMath::IsNearlyEqual(Width, PendingLayoutWidth, 1.0f))
	{
		PendingLayoutWidth = Width;
		PendingLayoutStartTime = InCurrentTime;
		return;
	}
	if (InCurrentTime - PendingLayoutStartTime >= 0.08)
	{
		RebuildSession(Width);
	}
}

void SBlueprintLensLC4AsyncPartialOrder::RebuildSession(const float TargetWidth)
{
	const FBlueprintLensLC4AsyncLayoutSessionResult Candidate =
		FBlueprintLensLC4AsyncLayoutSession::Build(Projection, TargetWidth);
	if (!Candidate.IsRenderable(Projection))
	{
		return;
	}
	Session = Candidate;
	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth;
	RebuildLabels();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SBlueprintLensLC4AsyncPartialOrder::RebuildLabels()
{
	const FBlueprintLensLC4AsyncVisualStyle& Style =
		FBlueprintLensLC4AsyncVisualStyle::FrozenEffectTarget();
	TSharedRef<SCanvas> Canvas = SNew(SCanvas).Clipping(EWidgetClipping::ClipToBoundsAlways);
	for (const FBlueprintLensLC4AsyncLabel& Label : Session.Layout.Labels)
	{
		Canvas->AddSlot()
		.Position(Label.Position)
		.Size(Label.ApproximateSize)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Label.Text))
				.Font(BlueprintLensLC4AsyncFont(Label.FontSize, Label.Weight))
				.ColorAndOpacity(LabelColor(Label.Id))
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
	}
	for (const FBlueprintLensLC4AsyncActionLayout& Action : Session.Layout.Actions)
	{
		Canvas->AddSlot().Position(Action.Bounds.Min).Size(Action.Bounds.Max - Action.Bounds.Min)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Action.Label))
				.Font(BlueprintLensLC4AsyncFont(
					Session.Layout.Mode == EBlueprintLensLC4AsyncLayoutMode::Wide700 ? 10 : 9,
					EBlueprintLensLC4AsyncFontWeight::Semibold))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(
					IsActionActive(Action.ActionId)
						? Color(*Style.PrimaryTextHex)
						: Color(*Style.MutedTextHex))
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
	}
	ChildSlot
	[
		Canvas
	];
}

int32 SBlueprintLensLC4AsyncPartialOrder::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FBlueprintLensLC4AsyncLayout& Layout = Session.Layout;
	const FBlueprintLensLC4AsyncVisualStyle& Style =
		FBlueprintLensLC4AsyncVisualStyle::FrozenEffectTarget();
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(
			Layout.CanvasSize,
			FSlateLayoutTransform(FVector2D::ZeroVector)),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
		ESlateDrawEffect::None,
		Color(*Style.CanvasFillHex));
	PaintBox(
		OutDrawElements,
		AllottedGeometry,
		LayerId,
		Layout.BarrierBounds,
		Color(*Style.GoldHex),
		FLinearColor::Transparent,
		0.0f,
		Style.BarrierRadius);
	PaintBox(
		OutDrawElements,
		AllottedGeometry,
		LayerId,
		Layout.CriterionBounds,
		Color(TEXT("#193126")),
		Color(*Style.GreenHex),
		2.0f,
		Style.CriterionRadius);
	PaintBox(
		OutDrawElements,
		AllottedGeometry,
		LayerId,
		Layout.FrontierBounds,
		Color(TEXT("#2c241d")),
		Color(*Style.OrangeHex),
		2.0f,
		Style.FrontierRadius);
	PaintBox(
		OutDrawElements,
		AllottedGeometry,
		LayerId,
		Layout.ActionsBounds,
		Color(*Style.ActionsFillHex),
		Color(*Style.ActionsStrokeHex),
		1.0f,
		Style.ActionsRadius);
	PaintLine(
		OutDrawElements,
		AllottedGeometry,
		LayerId + 1,
		{FVector2D(Layout.ActionsBounds.Min.X, Layout.HeaderRuleY),
		 FVector2D(Layout.ActionsBounds.Max.X, Layout.HeaderRuleY)},
		Color(*Style.ActionsStrokeHex),
		1.0f);
	const FLinearColor RouteColors[] = {
		Color(*Style.CyanHex),
		Color(*Style.OrangeHex), Color(*Style.OrangeHex),
		Color(*Style.PurpleHex), Color(*Style.PurpleHex),
		Color(*Style.GoldHex), Color(*Style.GoldHex),
		Color(*Style.GoldHex), Color(*Style.GoldHex),
		Color(*Style.GreenHex)};
	for (int32 RouteIndex = 0; RouteIndex < Layout.PaintedRoutes.Num(); ++RouteIndex)
	{
		const TArray<FVector2D>& Route = Layout.PaintedRoutes[RouteIndex];
		const FLinearColor RouteColor = RouteColors[
			FMath::Min(
				RouteIndex,
				static_cast<int32>(UE_ARRAY_COUNT(RouteColors)) - 1)];
		PaintLine(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 1,
			Route,
			RouteColor,
			RouteIndex == 0 ? Style.SourceRouteWidth : RouteIndex == 9 ? 4.0f : 2.0f);
	}
	PaintArrowHead(OutDrawElements, AllottedGeometry, LayerId + 1,
		FVector2D(Layout.LaunchB.X - Style.LaunchRadius, Layout.LaunchB.Y), FVector2D(1.0f, 0.0f), Color(*Style.CyanHex));
	for (const TPair<FVector2D, FLinearColor>& Arrow : {
		TPair<FVector2D, FLinearColor>(Layout.ContinuationA - FVector2D(0.0f, Style.EventRadius), Color(*Style.OrangeHex)),
		TPair<FVector2D, FLinearColor>(Layout.ContinuationB - FVector2D(0.0f, Style.EventRadius), Color(*Style.OrangeHex)),
		TPair<FVector2D, FLinearColor>(Layout.CompletionA - FVector2D(0.0f, Style.EventRadius), Color(*Style.PurpleHex)),
		TPair<FVector2D, FLinearColor>(Layout.CompletionB - FVector2D(0.0f, Style.EventRadius), Color(*Style.PurpleHex)),
		TPair<FVector2D, FLinearColor>(Layout.ArrivalA - FVector2D(0.0f, Style.EventRadius), Color(*Style.GoldHex)),
		TPair<FVector2D, FLinearColor>(Layout.ArrivalB - FVector2D(0.0f, Style.EventRadius), Color(*Style.GoldHex)),
		TPair<FVector2D, FLinearColor>(Layout.Release, Color(*Style.GreenHex))})
	{
		PaintArrowHead(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 1,
			Arrow.Key,
			FVector2D(0.0f, 1.0f),
			Arrow.Value);
	}
	PaintDashedLine(OutDrawElements, AllottedGeometry, LayerId + 1,
		Layout.CompletionA, Layout.CompletionB, Color(*Style.ActionsStrokeHex));
	PaintCircle(
		OutDrawElements,
		AllottedGeometry,
		LayerId + 1,
		FVector2D(32.0f + (Layout.Mode == EBlueprintLensLC4AsyncLayoutMode::Narrow430 ? 20.0f : Layout.Mode == EBlueprintLensLC4AsyncLayoutMode::Compact480 ? 24.0f : 28.0f), Layout.LaunchA.Y),
		7.0f,
		Color(*Style.CanvasFillHex),
		Color(*Style.CyanHex));
	for (const TPair<FVector2D, FLinearColor>& Node : {
		TPair<FVector2D, FLinearColor>(Layout.LaunchA, Color(*Style.CyanHex)),
		TPair<FVector2D, FLinearColor>(Layout.LaunchB, Color(*Style.CyanHex)),
		TPair<FVector2D, FLinearColor>(Layout.ContinuationA, Color(*Style.OrangeHex)),
		TPair<FVector2D, FLinearColor>(Layout.ContinuationB, Color(*Style.OrangeHex)),
		TPair<FVector2D, FLinearColor>(Layout.CompletionA, Color(*Style.PurpleHex)),
		TPair<FVector2D, FLinearColor>(Layout.CompletionB, Color(*Style.PurpleHex)),
		TPair<FVector2D, FLinearColor>(Layout.ArrivalA, Color(*Style.GoldHex)),
		TPair<FVector2D, FLinearColor>(Layout.ArrivalB, Color(*Style.GoldHex))})
	{
		PaintCircle(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 1,
			Node.Key,
			Node.Key.Equals(Layout.LaunchA, 0.1f) || Node.Key.Equals(Layout.LaunchB, 0.1f)
				? Style.LaunchRadius : Style.EventRadius,
			Color(*Style.CanvasFillHex),
			Node.Value,
			Style.NodeOutlineWidth);
	}
	for (const FVector2D& Completion : {Layout.CompletionA, Layout.CompletionB})
	{
		PaintLine(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 2,
			{Completion - FVector2D(4.0f, 0.0f), Completion + FVector2D(4.0f, 0.0f)},
			Color(*Style.PurpleHex),
			2.0f);
	}
	for (const float SocketX : {Layout.ArrivalA.X, Layout.ArrivalB.X})
	{
		PaintCircle(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 1,
			FVector2D(SocketX, Layout.BarrierBounds.Min.Y),
			5.0f,
			Color(*Style.CanvasFillHex),
			Color(*Style.GoldHex));
	}
	if (Layout.Mode == EBlueprintLensLC4AsyncLayoutMode::Wide700)
	{
		const float X = Layout.CanvasSize.X - 150.0f;
		const float Y = Layout.CompletionA.Y - 76.0f;
		PaintLine(OutDrawElements, AllottedGeometry, LayerId + 1,
			{FVector2D(X, Y), FVector2D(X + 10.0f, Y), FVector2D(X + 10.0f, Y + 116.0f), FVector2D(X, Y + 116.0f)},
			Color(*Style.PurpleHex), 2.0f);
	}
	else
	{
		const float Y = Layout.NarrowProofBracketY;
		PaintLine(OutDrawElements, AllottedGeometry, LayerId + 1,
			{FVector2D(32.0f, Y), FVector2D(32.0f, Y + 8.0f), FVector2D(Layout.CanvasSize.X - 32.0f, Y + 8.0f), FVector2D(Layout.CanvasSize.X - 32.0f, Y)},
			Color(*Style.PurpleHex), 2.0f);
	}
	for (const FBlueprintLensLC4AsyncActionLayout& Action : Layout.Actions)
	{
		const float CenterX = (Action.Bounds.Min.X + Action.Bounds.Max.X) * 0.5f;
		const float HalfWidth = (Action.Bounds.Max.X - Action.Bounds.Min.X) * 0.34f;
		PaintLine(
			OutDrawElements,
			AllottedGeometry,
			LayerId + 1,
			{FVector2D(CenterX - HalfWidth, Action.Bounds.Min.Y + 42.0f),
			 FVector2D(CenterX + HalfWidth, Action.Bounds.Min.Y + 42.0f)},
			IsActionActive(Action.ActionId)
				? Color(*Style.PrimaryTextHex)
				: Color(*Style.ActionsStrokeHex),
			1.0f);
	}
	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

FReply SBlueprintLensLC4AsyncPartialOrder::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	for (const FBlueprintLensLC4AsyncActionLayout& Action : Session.Layout.Actions)
	{
		if (Action.Bounds.IsInside(Local))
		{
			OnAction.ExecuteIfBound(Action.ActionId);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FVector2D SBlueprintLensLC4AsyncPartialOrder::ComputeDesiredSize(const float) const
{
	return Session.Layout.CanvasSize;
}
