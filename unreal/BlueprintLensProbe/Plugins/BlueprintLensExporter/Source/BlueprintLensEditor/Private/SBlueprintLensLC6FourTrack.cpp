#include "SBlueprintLensLC6FourTrack.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

FLinearColor BlueprintLensLC6RoundedBrushFill()
{
	return FLinearColor::White;
}

FLinearColor BlueprintLensLC6BoxElementTint(const FLinearColor& Fill)
{
	return Fill;
}

namespace
{
FLinearColor Color(const TCHAR* Hex)
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

void PaintBox(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FBox2D& Bounds,
	const FLinearColor& Fill,
	const FLinearColor& Outline,
	const float OutlineWidth,
	const float Radius)
{
	struct FBrushKey
	{
		float Radius;
		FColor Outline;
		float Width;
		bool operator==(const FBrushKey& Other) const
		{
			return FMath::IsNearlyEqual(Radius, Other.Radius) &&
				Outline == Other.Outline && FMath::IsNearlyEqual(Width, Other.Width);
		}
	};
	static TArray<TPair<FBrushKey, TSharedPtr<FSlateRoundedBoxBrush>>> Cache;
	const FBrushKey Key{Radius, Outline.ToFColor(true), OutlineWidth};
	const FSlateRoundedBoxBrush* Brush = nullptr;
	for (const auto& Entry : Cache)
	{
		if (Entry.Key == Key)
		{
			Brush = Entry.Value.Get();
			break;
		}
	}
	if (Brush == nullptr)
	{
		Cache.Emplace(Key, MakeShared<FSlateRoundedBoxBrush>(
			FSlateColor(BlueprintLensLC6RoundedBrushFill()), Radius,
			FSlateColor(Outline), OutlineWidth,
			FVector2f(FMath::Max(1.0f, Radius * 2.0f))));
		Brush = Cache.Last().Value.Get();
	}
	FSlateDrawElement::MakeBox(
		Out, Layer,
		Geometry.ToPaintGeometry(
			Bounds.GetSize(), FSlateLayoutTransform(Bounds.Min)),
		Brush, ESlateDrawEffect::None,
		BlueprintLensLC6BoxElementTint(Fill));
}

void PaintLine(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Tint,
	const float Thickness)
{
	FSlateDrawElement::MakeLines(
		Out, Layer, Geometry.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, Tint, true, Thickness);
}

void PaintDashedLine(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Tint,
	const float Thickness)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	for (float Offset = 0.0f; Offset < Length; Offset += 11.0f)
	{
		const float SegmentEnd = FMath::Min(Offset + 7.0f, Length);
		PaintLine(Out, Geometry, Layer,
			{Start + Direction * Offset, Start + Direction * SegmentEnd},
			Tint, Thickness);
	}
}

FLinearColor StatusColor(const FBlueprintLensLC6Track* Track)
{
	return Track != nullptr &&
		Track->Status.Equals(TEXT("opaque"), ESearchCase::IgnoreCase)
		? Color(TEXT("#F0B35A")) :
		Track != nullptr &&
		Track->Status.Equals(TEXT("uncertain"), ESearchCase::IgnoreCase)
			? Color(TEXT("#E9D66B")) :
		Track != nullptr &&
		Track->Status.Equals(TEXT("unsupported"), ESearchCase::IgnoreCase)
				? Color(TEXT("#F07178")) :
		Color(TEXT("#D997FF"));
}
} // namespace

void SBlueprintLensLC6FourTrack::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	SelectedScenarioId = InArgs._SelectedScenarioId;
	OnScenarioSelected = InArgs._OnScenarioSelected;
	OnAction = InArgs._OnAction;
	SetClipping(EWidgetClipping::ClipToBoundsAlways);
	RebuildLabels();
}

FString SBlueprintLensLC6FourTrack::ResolveActionAtLocalPositionForTesting(
	const FVector2D& Local) const
{
	return Session.Layout.CompleteTextActionBounds.IsInside(Local)
		? TEXT("complete-text") : FString();
}

bool SBlueprintLensLC6FourTrack::IsScenarioSelected(
	const FString& ScenarioId) const
{
	return !ScenarioId.IsEmpty() &&
		ScenarioId == SelectedScenarioId.Get(FString());
}

FString SBlueprintLensLC6FourTrack::ResolveScenarioAtLocalPositionForTesting(
	const FVector2D& Local) const
{
	for (const FBlueprintLensLC6TrackLayout& Track : Session.Layout.Tracks)
	{
		if (Track.HitBounds.IsInside(Local))
		{
			return Track.ScenarioId;
		}
	}
	return FString();
}

TArray<FBox2D> SBlueprintLensLC6FourTrack::GetScenarioHitTargetsForTesting() const
{
	TArray<FBox2D> Result;
	for (const FBlueprintLensLC6TrackLayout& Track : Session.Layout.Tracks)
	{
		Result.Add(Track.HitBounds);
	}
	return Result;
}

int32 SBlueprintLensLC6FourTrack::GetCriterionMarkerCountForTesting() const
{
	return Session.Layout.Tracks.FilterByPredicate([](const auto& Track)
	{
		return !Track.CriterionMarker.IsNearlyZero();
	}).Num();
}

int32 SBlueprintLensLC6FourTrack::GetSemanticFenceCountForTesting() const
{
	return Session.Layout.Tracks.FilterByPredicate([](const auto& Track)
	{
		return Track.bHasSemanticFence;
	}).Num();
}

int32 SBlueprintLensLC6FourTrack::GetFrontierCountForTesting() const
{
	return Session.Layout.Tracks.FilterByPredicate([](const auto& Track)
	{
		return Track.FrontierBounds.bIsValid;
	}).Num();
}

void SBlueprintLensLC6FourTrack::RebuildLabels()
{
	TSharedRef<SCanvas> Canvas =
		SNew(SCanvas).Clipping(EWidgetClipping::ClipToBoundsAlways);
	const bool bHasSelectedScenario =
		!SelectedScenarioId.Get(FString()).IsEmpty();
	for (const FBlueprintLensLC6Label& Label : Session.Layout.Labels)
	{
		if (bHasSelectedScenario && Label.Id.StartsWith(TEXT("detail.empty.")))
		{
			continue;
		}
		Canvas->AddSlot().Position(Label.Bounds.Min).Size(Label.Bounds.GetSize())
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label.Text))
			.Font(BlueprintLensLC6Font(Label.FontSize, Label.bBold))
			.ColorAndOpacity(Color(*Label.ColorHex))
			.AutoWrapText(true)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
	}
	ChildSlot[Canvas];
}

int32 SBlueprintLensLC6FourTrack::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& Geometry,
	const FSlateRect& Culling,
	FSlateWindowElementList& Out,
	const int32 Layer,
	const FWidgetStyle& Style,
	const bool bEnabled) const
{
	const FBlueprintLensLC6Layout& Layout = Session.Layout;
	FSlateDrawElement::MakeBox(
		Out, Layer,
		Geometry.ToPaintGeometry(Layout.CanvasSize, FSlateLayoutTransform()),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
		ESlateDrawEffect::None, Color(TEXT("#10151C")));
	PaintBox(Out, Geometry, Layer, Layout.OverviewBounds,
		Color(TEXT("#171D25")), Color(TEXT("#394654")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer, Layout.DetailBounds,
		Color(TEXT("#171D25")), Color(TEXT("#394654")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer, Layout.CompleteTextActionBounds,
		Color(TEXT("#1D2430")), Color(TEXT("#405066")), 1.0f, 5.0f);

	// Relations are always below nodes and labels.
	for (const FBlueprintLensLC6TrackLayout& Track : Layout.Tracks)
	{
		const FBlueprintLensLC6Track* Projected =
			Projection.FindTrack(Track.ScenarioId);
		const FLinearColor Accent = StatusColor(Projected);
		if (Projected != nullptr &&
			Projected->TruthOwner == TEXT("query_profile"))
		{
			PaintDashedLine(Out, Geometry, Layer + GetRoutePaintLayerForTesting(),
				Track.RouteStart, Track.RouteEnd, Accent, 3.0f);
		}
		else
		{
			PaintLine(Out, Geometry, Layer + GetRoutePaintLayerForTesting(),
				{Track.RouteStart, Track.RouteEnd}, Color(TEXT("#67B7FF")), 3.0f);
		}
	}

	for (const FBlueprintLensLC6TrackLayout& Track : Layout.Tracks)
	{
		const FBlueprintLensLC6Track* Projected =
			Projection.FindTrack(Track.ScenarioId);
		const FLinearColor Accent = StatusColor(Projected);
		const bool bSelected = IsScenarioSelected(Track.ScenarioId);
		const bool bQueryTrack = Projected != nullptr &&
			Projected->TruthOwner == TEXT("query_profile");
		PaintBox(Out, Geometry, Layer + GetTrackBackgroundPaintLayerForTesting(),
			Track.HitBounds,
			bQueryTrack
				? Color(TEXT("#241D31"))
				: bSelected ? Color(TEXT("#1B2530")) : Color(TEXT("#1D2430")),
			bSelected ? Accent : Color(TEXT("#405066")),
			bSelected ? 2.0f : 1.0f, 8.0f);
		if (Track.bHasSemanticFence)
		{
			PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
				Track.BoundaryBounds, Color(TEXT("#0E1117")), Accent, 1.0f, 5.0f);
			PaintLine(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
				{Track.SemanticFenceStart, Track.SemanticFenceEnd}, Accent, 3.0f);
		}
		else
		{
			PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
				Track.OmissionBounds, Color(TEXT("#0E1117")),
				Color(TEXT("#A9B3C1")), 1.0f, 5.0f);
			const FVector2D Center = Track.FrontierBounds.GetCenter();
			const FVector2D Half = Track.FrontierBounds.GetSize() * 0.5f;
			PaintLine(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
				{FVector2D(Center.X, Center.Y - Half.Y),
				 FVector2D(Center.X + Half.X, Center.Y),
				 FVector2D(Center.X, Center.Y + Half.Y),
				 FVector2D(Center.X - Half.X, Center.Y),
				 FVector2D(Center.X, Center.Y - Half.Y)}, Accent, 2.0f);
			for (const FBox2D& QueryNode : Track.QueryNodeBounds)
			{
				PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
					QueryNode, Color(TEXT("#171C24")), Accent, 1.0f, 5.0f);
			}
		}
		PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
			Track.CriterionBounds, Color(TEXT("#171C24")),
			Color(TEXT("#A7D46F")), 1.0f, 5.0f);
		const float DotRadius = 3.0f;
		PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
			FBox2D(Track.CriterionMarker - FVector2D(DotRadius, DotRadius),
				Track.CriterionMarker + FVector2D(DotRadius, DotRadius)),
			Color(TEXT("#A7D46F")), Color(TEXT("#A7D46F")), 0.0f, DotRadius);
	}
	return SCompoundWidget::OnPaint(
		Args, Geometry, Culling, Out,
		Layer + GetNodePaintLayerForTesting() + 1,
		Style, bEnabled);
}

FReply SBlueprintLensLC6FourTrack::OnMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local =
		Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FString ActionId = ResolveActionAtLocalPositionForTesting(Local);
	if (!ActionId.IsEmpty())
	{
		OnAction.ExecuteIfBound(ActionId);
		return FReply::Handled();
	}
	const FString ScenarioId = ResolveScenarioAtLocalPositionForTesting(Local);
	if (ScenarioId.IsEmpty())
	{
		return FReply::Unhandled();
	}
	OnScenarioSelected.ExecuteIfBound(
		IsScenarioSelected(ScenarioId) ? FString() : ScenarioId);
	return FReply::Handled();
}

FVector2D SBlueprintLensLC6FourTrack::ComputeDesiredSize(const float) const
{
	return Session.Layout.CanvasSize;
}
