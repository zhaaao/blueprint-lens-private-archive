#include "SBlueprintLensLC7AdaptiveBackbone.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

FLinearColor BlueprintLensLC7RoundedBrushFill()
{
	return FLinearColor::White;
}

FLinearColor BlueprintLensLC7BoxElementTint(const FLinearColor& Fill)
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
	if (!Bounds.bIsValid)
	{
		return;
	}
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
			FSlateColor(BlueprintLensLC7RoundedBrushFill()), Radius,
			FSlateColor(Outline), OutlineWidth,
			FVector2f(FMath::Max(1.0f, Radius * 2.0f))));
		Brush = Cache.Last().Value.Get();
	}
	FSlateDrawElement::MakeBox(
		Out, Layer,
		Geometry.ToPaintGeometry(
			Bounds.GetSize(), FSlateLayoutTransform(Bounds.Min)),
		Brush, ESlateDrawEffect::None,
		BlueprintLensLC7BoxElementTint(Fill));
}

void PaintLine(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Tint,
	const float Thickness)
{
	if (Points.Num() < 2)
	{
		return;
	}
	FSlateDrawElement::MakeLines(
		Out, Layer, Geometry.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, Tint, true, Thickness);
}

void PaintSegmentPattern(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Tint,
	const float Thickness,
	const float MarkLength,
	const float GapLength)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	for (float Offset = 0.0f; Offset < Length;
		 Offset += MarkLength + GapLength)
	{
		const float MarkEnd = FMath::Min(Offset + MarkLength, Length);
		PaintLine(Out, Geometry, Layer,
			{Start + Direction * Offset, Start + Direction * MarkEnd},
			Tint, Thickness);
	}
}

void PaintSingleStrokeSegments(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Tint,
	const float Thickness)
{
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		PaintLine(Out, Geometry, Layer,
			{Points[Index - 1], Points[Index]}, Tint, Thickness);
	}
}

void PaintRouteMarker(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Previous,
	const FVector2D& Tip,
	const FBlueprintLensLC7RelationVisualEncoding& Encoding)
{
	const FVector2D Direction = (Tip - Previous).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const FVector2D Normal(-Direction.Y, Direction.X);
	const FVector2D Back = Tip - Direction * 8.0f;
	switch (Encoding.Marker)
	{
	case EBlueprintLensLC7RouteMarker::Triangle:
		PaintLine(Out, Geometry, Layer,
			{Tip, Back + Normal * 4.0f, Back - Normal * 4.0f, Tip},
			Encoding.Tint, Encoding.Thickness);
		break;
	case EBlueprintLensLC7RouteMarker::Diamond:
		PaintLine(Out, Geometry, Layer,
			{Tip, Back + Normal * 4.0f, Tip - Direction * 16.0f,
			 Back - Normal * 4.0f, Tip},
			Encoding.Tint, Encoding.Thickness);
		break;
	case EBlueprintLensLC7RouteMarker::Circle:
		PaintBox(Out, Geometry, Layer,
			FBox2D(Back - FVector2D(3.0f, 3.0f),
				Back + FVector2D(3.0f, 3.0f)),
			Color(TEXT("#10151C")), Encoding.Tint, 1.5f, 3.0f);
		break;
	case EBlueprintLensLC7RouteMarker::ReturnArrow:
		PaintLine(Out, Geometry, Layer,
			{Back + Normal * 5.0f, Tip, Back - Normal * 5.0f},
			Encoding.Tint, Encoding.Thickness);
		break;
	case EBlueprintLensLC7RouteMarker::Square:
		PaintBox(Out, Geometry, Layer,
			FBox2D(Back - FVector2D(3.0f, 3.0f),
				Back + FVector2D(3.0f, 3.0f)),
			Encoding.Tint, Encoding.Tint, 1.0f, 1.0f);
		break;
	}
}

void PaintRoute(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FBlueprintLensLC7RouteLayout& Route)
{
	if (Route.Points.Num() < 2)
	{
		return;
	}
	const FBlueprintLensLC7RelationVisualEncoding Encoding =
		BlueprintLensLC7RelationEncoding(Route.Family);
	if (Encoding.Pattern == EBlueprintLensLC7RoutePattern::Solid)
	{
		if (Route.Family == EBlueprintLensLC7RelationFamily::Return)
		{
			// Slate's thick-line tessellation outlines a U-shaped polyline as
			// two parallel bands. Paint its orthogonal segments independently;
			// shared endpoints keep the one semantic return route continuous.
			PaintSingleStrokeSegments(Out, Geometry, Layer, Route.Points,
				Encoding.Tint, Encoding.Thickness);
		}
		else
		{
			PaintLine(Out, Geometry, Layer, Route.Points,
				Encoding.Tint, Encoding.Thickness);
		}
	}
	else
	{
		for (int32 Index = 1; Index < Route.Points.Num(); ++Index)
		{
			const FVector2D& Start = Route.Points[Index - 1];
			const FVector2D& End = Route.Points[Index];
			if (Encoding.Pattern == EBlueprintLensLC7RoutePattern::Dashed)
			{
				PaintSegmentPattern(Out, Geometry, Layer, Start, End,
					Encoding.Tint, Encoding.Thickness, 8.0f, 5.0f);
			}
			else if (Encoding.Pattern == EBlueprintLensLC7RoutePattern::Dotted)
			{
				PaintSegmentPattern(Out, Geometry, Layer, Start, End,
					Encoding.Tint, Encoding.Thickness, 2.0f, 5.0f);
			}
		}
	}
	PaintRouteMarker(Out, Geometry, Layer,
		Route.Points[Route.Points.Num() - 2], Route.Points.Last(), Encoding);
}

FString ActionLabel(const FString& ActionId)
{
	if (ActionId == TEXT("inspect_cycle"))
	{
		return TEXT("Inspect cycle");
	}
	if (ActionId == TEXT("show_complete_text"))
	{
		return TEXT("Complete text");
	}
	if (ActionId == TEXT("open_source"))
	{
		return TEXT("Open source");
	}
	return ActionId;
}
} // namespace

FBlueprintLensLC7RelationVisualEncoding BlueprintLensLC7RelationEncoding(
	const EBlueprintLensLC7RelationFamily Family)
{
	FBlueprintLensLC7RelationVisualEncoding Result;
	switch (Family)
	{
	case EBlueprintLensLC7RelationFamily::Entry:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Solid;
		Result.Marker = EBlueprintLensLC7RouteMarker::Triangle;
		Result.Tint = Color(TEXT("#67B7FF"));
		break;
	case EBlueprintLensLC7RelationFamily::Predicate:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Dashed;
		Result.Marker = EBlueprintLensLC7RouteMarker::Diamond;
		Result.Tint = Color(TEXT("#F0B35A"));
		break;
	case EBlueprintLensLC7RelationFamily::Value:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Dotted;
		Result.Marker = EBlueprintLensLC7RouteMarker::Circle;
		Result.Tint = Color(TEXT("#D997FF"));
		break;
	case EBlueprintLensLC7RelationFamily::Forward:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Solid;
		Result.Marker = EBlueprintLensLC7RouteMarker::Triangle;
		Result.Tint = Color(TEXT("#A7D46F"));
		Result.Thickness = 2.5f;
		break;
	case EBlueprintLensLC7RelationFamily::Return:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Solid;
		Result.Marker = EBlueprintLensLC7RouteMarker::ReturnArrow;
		Result.Tint = Color(TEXT("#F07178"));
		Result.Thickness = 2.0f;
		break;
	case EBlueprintLensLC7RelationFamily::Exit:
		Result.Pattern = EBlueprintLensLC7RoutePattern::Solid;
		Result.Marker = EBlueprintLensLC7RouteMarker::Square;
		Result.Tint = Color(TEXT("#69D2C8"));
		break;
	}
	return Result;
}

void SBlueprintLensLC7AdaptiveBackbone::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	SelectedUnitId = InArgs._SelectedUnitId;
	OnUnitSelected = InArgs._OnUnitSelected;
	OnAction = InArgs._OnAction;
	SetClipping(EWidgetClipping::ClipToBoundsAlways);
	RebuildLabels();
}

const FBlueprintLensLC7SCCRecord*
SBlueprintLensLC7AdaptiveBackbone::FindFocusedSCC() const
{
	return Projection.SCCs.FindByPredicate([this](const auto& SCC)
	{
		return SCC.GroupId == Session.Layout.FocusedSCCId;
	});
}

bool SBlueprintLensLC7AdaptiveBackbone::IsUnitSelected(
	const FString& UnitId) const
{
	return !UnitId.IsEmpty() && UnitId == SelectedUnitId.Get(FString());
}

FString SBlueprintLensLC7AdaptiveBackbone::ResolveUnitAtLocalPositionForTesting(
	const FVector2D& Local) const
{
	const FBlueprintLensLC7SCCRecord* SCC = FindFocusedSCC();
	if (SCC == nullptr)
	{
		return FString();
	}
	for (const FString& MemberId : SCC->OrderedSpineUnitIds)
	{
		const FBlueprintLensLC7NodeLayout* Node =
			Session.Layout.Nodes.FindByPredicate(
				[&MemberId](const auto& Candidate)
				{
					return Candidate.UnitId == MemberId;
				});
		if (Node != nullptr && Node->HitBounds.IsInside(Local))
		{
			return MemberId;
		}
	}
	return FString();
}

FString SBlueprintLensLC7AdaptiveBackbone::ResolveSelectionAtLocalPositionForTesting(
	const FVector2D& Local) const
{
	const FString UnitId = ResolveUnitAtLocalPositionForTesting(Local);
	return IsUnitSelected(UnitId) ? FString() : UnitId;
}

FString SBlueprintLensLC7AdaptiveBackbone::ResolveActionAtLocalPositionForTesting(
	const FVector2D& Local) const
{
	for (const FBlueprintLensLC7ActionLayout& Action : Session.Layout.Actions)
	{
		if (Action.HitBounds.IsInside(Local))
		{
			return Action.ActionId;
		}
	}
	return FString();
}

TArray<FBox2D>
SBlueprintLensLC7AdaptiveBackbone::GetMemberHitTargetsForTesting() const
{
	TArray<FBox2D> Result;
	const FBlueprintLensLC7SCCRecord* SCC = FindFocusedSCC();
	if (SCC == nullptr)
	{
		return Result;
	}
	for (const FString& MemberId : SCC->OrderedSpineUnitIds)
	{
		const FBlueprintLensLC7NodeLayout* Node =
			Session.Layout.Nodes.FindByPredicate(
				[&MemberId](const auto& Candidate)
				{
					return Candidate.UnitId == MemberId;
				});
		if (Node != nullptr)
		{
			Result.Add(Node->HitBounds);
		}
	}
	return Result;
}

TArray<FBox2D>
SBlueprintLensLC7AdaptiveBackbone::GetActionHitTargetsForTesting() const
{
	TArray<FBox2D> Result;
	for (const FBlueprintLensLC7ActionLayout& Action : Session.Layout.Actions)
	{
		Result.Add(Action.HitBounds);
	}
	return Result;
}

float SBlueprintLensLC7AdaptiveBackbone::GetUnitOutlineWidthForTesting(
	const FString& UnitId) const
{
	return IsUnitSelected(UnitId) ? 3.0f : 1.25f;
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetVisibleSCCCountForTesting() const
{
	return Session.Layout.VisibleSCCCount;
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetVisibleCriterionCountForTesting() const
{
	return Session.Layout.Nodes.ContainsByPredicate([this](const auto& Node)
	{
		return Node.UnitId == Session.Layout.CriterionUnitId &&
			Projection.UnitTitles.Contains(Node.UnitId);
	}) ? 1 : 0;
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetVisibleSourceAnchorCountForTesting() const
{
	return Session.Layout.Nodes.FilterByPredicate([this](const auto& Node)
	{
		return Projection.SourceAnchors.Contains(Node.UnitId);
	}).Num();
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetCountedFoldAffordanceCountForTesting() const
{
	return Session.Layout.Folds.FilterByPredicate([](const auto& Fold)
	{
		return Fold.UnitCount >= 0 && Fold.RelationCount >= 0 &&
			!Fold.ExpansionActionId.IsEmpty();
	}).Num();
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetCountedIndexAffordanceCountForTesting() const
{
	return Session.Layout.IndexRows.FilterByPredicate([](const auto& Row)
	{
		return Row.UnitCount >= 0 && Row.RelationCount >= 0 &&
			!Row.ExpansionActionId.IsEmpty();
	}).Num();
}

int32 SBlueprintLensLC7AdaptiveBackbone::GetParagraphTextCountForTesting() const
{
	return PaintedLabelTexts.FilterByPredicate([](const FString& Text)
	{
		return Text.Contains(TEXT("\n")) || Text.Len() > 80;
	}).Num();
}

void SBlueprintLensLC7AdaptiveBackbone::RebuildLabels()
{
	PaintedLabelTexts.Reset();
	TSharedRef<SCanvas> Canvas =
		SNew(SCanvas).Clipping(EWidgetClipping::ClipToBoundsAlways);
	const auto AddLabel = [this, &Canvas](
		const FString& Text,
		const FBox2D& Bounds,
		const bool bBold,
		const FLinearColor& Tint,
		const ETextJustify::Type Justification = ETextJustify::Left)
	{
		if (Text.IsEmpty() || !Bounds.bIsValid)
		{
			return;
		}
		PaintedLabelTexts.Add(Text);
		Canvas->AddSlot().Position(Bounds.Min).Size(Bounds.GetSize())
		[
			SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(FAppStyle::Get().GetFontStyle(
				bBold ? "NormalFontBold" : "SmallFont"))
			.ColorAndOpacity(Tint)
			.Justification(Justification)
			.AutoWrapText(false)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
	};

	const FBlueprintLensLC7Layout& Layout = Session.Layout;
	if (Layout.OverviewBounds.bIsValid)
	{
		AddLabel(
			FString::Printf(TEXT("SCC %d"), Layout.VisibleSCCCount),
			FBox2D(Layout.OverviewBounds.Min + FVector2D(16.0f, 14.0f),
				Layout.OverviewBounds.Min + FVector2D(92.0f, 36.0f)),
			true, Color(TEXT("#DDE7F0")));
	}
	for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
	{
		const FString* Title = Projection.UnitTitles.Find(Node.UnitId);
		if (Title != nullptr)
		{
			const FBox2D SafeTextBounds(
				FVector2D(Node.Bounds.Min.X, Node.LabelBounds.Min.Y),
				FVector2D(Node.Bounds.Max.X, Node.LabelBounds.Max.Y));
			AddLabel(*Title, SafeTextBounds,
				Node.UnitId == Layout.CriterionUnitId,
				Color(TEXT("#E5EDF5")), ETextJustify::Center);
		}
	}
	for (const FBlueprintLensLC7ActionLayout& Action : Layout.Actions)
	{
		AddLabel(ActionLabel(Action.ActionId), Action.HitBounds, false,
			Color(TEXT("#B9C6D2")), ETextJustify::Center);
	}
	float AffordanceY = Layout.OverviewBounds.Min.Y + 52.0f;
	for (const FBlueprintLensLC7Fold& Fold : Layout.Folds)
	{
		AddLabel(
			FString::Printf(TEXT("FOLD %dU / %dR"),
				Fold.UnitCount, Fold.RelationCount),
			FBox2D(
				FVector2D(Layout.OverviewBounds.Min.X + 16.0f, AffordanceY),
				FVector2D(Layout.OverviewBounds.Max.X - 16.0f,
					AffordanceY + 24.0f)),
			true, Color(TEXT("#F0B35A")));
		AffordanceY += 30.0f;
	}
	for (const FBlueprintLensLC7IndexRow& Row : Layout.IndexRows)
	{
		AddLabel(
			FString::Printf(TEXT("SCC %dU / %dR"),
				Row.UnitCount, Row.RelationCount),
			FBox2D(
				FVector2D(Layout.OverviewBounds.Min.X + 16.0f, AffordanceY),
				FVector2D(Layout.OverviewBounds.Max.X - 16.0f,
					AffordanceY + 24.0f)),
			true, Color(TEXT("#67B7FF")));
		AffordanceY += 30.0f;
	}
	ChildSlot[Canvas];
}

int32 SBlueprintLensLC7AdaptiveBackbone::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& Geometry,
	const FSlateRect& Culling,
	FSlateWindowElementList& Out,
	const int32 Layer,
	const FWidgetStyle& Style,
	const bool bEnabled) const
{
	const FBlueprintLensLC7Layout& Layout = Session.Layout;
	FSlateDrawElement::MakeBox(
		Out, Layer,
		Geometry.ToPaintGeometry(Layout.CanvasSize, FSlateLayoutTransform()),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
		ESlateDrawEffect::None, Color(TEXT("#10151C")));
	PaintBox(Out, Geometry, Layer, Layout.OverviewBounds,
		Color(TEXT("#171D25")), Color(TEXT("#394654")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer, Layout.DetailBounds,
		Color(TEXT("#171D25")), Color(TEXT("#394654")), 1.0f, 9.0f);

	for (const FBlueprintLensLC7RouteLayout& Route : Layout.Routes)
	{
		PaintRoute(Out, Geometry,
			Layer + GetRoutePaintLayerForTesting(), Route);
	}

	const FBlueprintLensLC7SCCRecord* SCC = FindFocusedSCC();
	for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
	{
		const bool bMember = SCC != nullptr &&
			SCC->OrderedSpineUnitIds.Contains(Node.UnitId);
		const bool bCriterion = Node.UnitId == Layout.CriterionUnitId;
		const bool bSelected = IsUnitSelected(Node.UnitId);
		const FLinearColor Fill = bSelected ? Color(TEXT("#22364A")) :
			bCriterion ? Color(TEXT("#223021")) :
			bMember ? Color(TEXT("#1B3043")) : Color(TEXT("#1D2430"));
		const FLinearColor Outline = bSelected ? Color(TEXT("#9DD8FF")) :
			bCriterion ? Color(TEXT("#A7D46F")) :
			bMember ? Color(TEXT("#67B7FF")) : Color(TEXT("#526174"));
		PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
			Node.Bounds, Fill, Outline,
			GetUnitOutlineWidthForTesting(Node.UnitId), 7.0f);
		if (Projection.SourceAnchors.Contains(Node.UnitId))
		{
			const FVector2D Anchor = Node.Bounds.Max - FVector2D(7.0f, 7.0f);
			PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
				FBox2D(Anchor - FVector2D(2.5f, 2.5f),
					Anchor + FVector2D(2.5f, 2.5f)),
				Color(TEXT("#DDE7F0")), Color(TEXT("#DDE7F0")),
				0.0f, 2.5f);
		}
	}
	for (const FBlueprintLensLC7ActionLayout& Action : Layout.Actions)
	{
		PaintBox(Out, Geometry, Layer + GetNodePaintLayerForTesting(),
			Action.HitBounds, Color(TEXT("#222D39")),
			Color(TEXT("#607284")), 1.0f, 5.0f);
	}
	return SCompoundWidget::OnPaint(
		Args, Geometry, Culling, Out,
		Layer + GetTextPaintLayerForTesting(), Style, bEnabled);
}

FReply SBlueprintLensLC7AdaptiveBackbone::OnMouseButtonDown(
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
	const FString UnitId = ResolveUnitAtLocalPositionForTesting(Local);
	if (UnitId.IsEmpty())
	{
		return FReply::Unhandled();
	}
	OnUnitSelected.ExecuteIfBound(
		IsUnitSelected(UnitId) ? FString() : UnitId);
	return FReply::Handled();
}

FVector2D SBlueprintLensLC7AdaptiveBackbone::ComputeDesiredSize(const float) const
{
	return Session.Layout.CanvasSize;
}
