#include "SBlueprintLensLC5TypedPortal.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

FLinearColor BlueprintLensLC5RoundedBrushFill()
{
	return FLinearColor::White;
}

FLinearColor BlueprintLensLC5BoxElementTint(const FLinearColor& Fill)
{
	return Fill;
}

namespace
{
FLinearColor Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
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
			FSlateColor(BlueprintLensLC5RoundedBrushFill()), Radius,
			FSlateColor(Outline), OutlineWidth,
			FVector2f(FMath::Max(1.0f, Radius * 2.0f))));
		Brush = Cache.Last().Value.Get();
	}
	FSlateDrawElement::MakeBox(
		Out, Layer,
		Geometry.ToPaintGeometry(Bounds.Max - Bounds.Min, FSlateLayoutTransform(Bounds.Min)),
		Brush, ESlateDrawEffect::None, BlueprintLensLC5BoxElementTint(Fill));
}

void PaintLine(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Tint,
	const float Thickness)
{
	FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, Tint, true, Thickness);
}

void PaintDashedLine(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Tint,
	const float Thickness,
	const float Dash = 7.0f,
	const float Gap = 6.0f)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	for (float Offset = 0.0f; Offset < Length; Offset += Dash + Gap)
	{
		const float SegmentEnd = FMath::Min(Offset + Dash, Length);
		PaintLine(Out, Geometry, Layer,
			{Start + Direction * Offset, Start + Direction * SegmentEnd}, Tint, Thickness);
	}
}

FLinearColor RelationColor(const FBlueprintLensLayoutEdgePlacement& Edge)
{
	switch (Edge.Family)
	{
	case EBlueprintLensLayoutRelationFamily::Value:
		return Color(TEXT("#F0B35A"));
	case EBlueprintLensLayoutRelationFamily::Predicate:
		return Color(TEXT("#D997FF"));
	case EBlueprintLensLayoutRelationFamily::Portal:
		return Color(TEXT("#67B7FF"));
	case EBlueprintLensLayoutRelationFamily::Frontier:
		return Color(TEXT("#F07178"));
	case EBlueprintLensLayoutRelationFamily::BackEdge:
		return Color(TEXT("#FF8A65"));
	case EBlueprintLensLayoutRelationFamily::Execution:
	default:
		return Color(TEXT("#A7D46F"));
	}
}

void PaintArrowhead(
	FSlateWindowElementList& Out,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Previous,
	const FVector2D& Target,
	const FLinearColor& Tint)
{
	const FVector2D Delta = Target - Previous;
	if (Delta.IsNearlyZero())
	{
		return;
	}
	const FVector2D Direction = Delta.GetSafeNormal();
	const FVector2D Perpendicular(-Direction.Y, Direction.X);
	const FVector2D Base = Target - Direction * 8.0f;
	PaintLine(
		Out,
		Geometry,
		Layer,
		{Base + Perpendicular * 4.0f, Target, Base - Perpendicular * 4.0f},
		Tint,
		2.0f);
}

FSlateFontInfo LabelFont(const FBlueprintLensLC5Label& Label)
{
	return BlueprintLensLC5Font(Label.FontSize, Label.bBold);
}

FLinearColor LabelColor(const FBlueprintLensLC5Label& Label)
{
	return Color(*Label.ColorHex);
}
} // namespace

void SBlueprintLensLC5TypedPortal::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	SelectedOccurrenceId = InArgs._SelectedOccurrenceId;
	ActiveActionId = InArgs._ActiveActionId;
	OnAction = InArgs._OnAction;
	OnOccurrenceSelected = InArgs._OnOccurrenceSelected;
	SetClipping(EWidgetClipping::ClipToBoundsAlways);
	RebuildLabels();
}

bool SBlueprintLensLC5TypedPortal::IsActionActive(const FString& ActionId) const
{
	return ActionId != TEXT("open_source") &&
		ActionId == ActiveActionId.Get(TEXT("select"));
}

TArray<FString> SBlueprintLensLC5TypedPortal::HighlightedRelationIdsForTesting() const
{
	TArray<FString> Result;
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		if (IsRelationHighlighted(RelationId))
		{
			Result.Add(RelationId);
		}
	}
	return Result;
}

bool SBlueprintLensLC5TypedPortal::IsRelationHighlighted(const FString& RelationId) const
{
	const FString Selected = SelectedOccurrenceId.Get(FString());
	if (Selected.IsEmpty())
	{
		return false;
	}
	const bool bCallSelected = Selected == Session.Layout.CallOccurrenceId;
	if (bCallSelected && RelationId.StartsWith(TEXT("binding:")))
	{
		return true;
	}
	const FBlueprintLensLayoutEdgeRequest* Edge = Session.Layout.LayoutRequest.Edges.FindByPredicate(
		[&RelationId](const auto& Item) { return Item.RelationId == RelationId; });
	return Edge != nullptr && (Edge->SourceUnitId == Selected || Edge->TargetUnitId == Selected);
}

void SBlueprintLensLC5TypedPortal::RebuildLabels()
{
	TSharedRef<SCanvas> Canvas = SNew(SCanvas).Clipping(EWidgetClipping::ClipToBoundsAlways);
	for (const FBlueprintLensLC5Label& Label : Session.Layout.Labels)
	{
		TSharedRef<STextBlock> LabelWidget =
			SNew(STextBlock)
			.Text(FText::FromString(Label.Text))
			.Font(LabelFont(Label))
			.ColorAndOpacity(LabelColor(Label))
			.AutoWrapText(true)
			.Clipping(EWidgetClipping::ClipToBoundsAlways);
		if (Label.Id == TEXT("live.order"))
		{
			LabelWidget->SetTag(FName(TEXT(
				"BlueprintLens.Automation.LC5StaticOrderDisclosure")));
		}
		else if (Label.Id == TEXT("live.reading_key"))
		{
			LabelWidget->SetTag(FName(TEXT(
				"BlueprintLens.Automation.LC5ReadingKey")));
		}
		else if (Label.Id == TEXT("live.caller.region"))
		{
			LabelWidget->SetTag(FName(TEXT(
				"BlueprintLens.Automation.LC5CallerRole")));
		}
		else if (Label.Id == TEXT("live.callee.region"))
		{
			LabelWidget->SetTag(FName(TEXT(
				"BlueprintLens.Automation.LC5CalleeRole")));
		}
		Canvas->AddSlot().Position(Label.Bounds.Min).Size(Label.Bounds.GetSize())
		[
			LabelWidget
		];
	}
	for (const FBlueprintLensLC5ActionLayout& Action : Session.Layout.Actions)
	{
		const FSlateFontInfo Font = BlueprintLensLC5Font(9, true);
		Canvas->AddSlot().Position(Action.Bounds.Min).Size(Action.Bounds.GetSize())
		[
			SNew(STextBlock)
			.Text(FText::FromString(Action.Label))
			.Font(Font)
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(IsActionActive(Action.ActionId)
				? Color(TEXT("#ffffff")) : Color(TEXT("#aab5c0")))
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
	}
	ChildSlot[Canvas];
}

int32 SBlueprintLensLC5TypedPortal::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& Geometry,
	const FSlateRect& Culling,
	FSlateWindowElementList& Out,
	const int32 Layer,
	const FWidgetStyle& Style,
	const bool bEnabled) const
{
	const FBlueprintLensLC5Layout& Layout = Session.Layout;
	FSlateDrawElement::MakeBox(Out, Layer,
		Geometry.ToPaintGeometry(Layout.CanvasSize, FSlateLayoutTransform()),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None,
		Color(TEXT("#10151c")));
	PaintBox(Out, Geometry, Layer, Layout.HeaderBounds, Color(TEXT("#171d25")), Color(TEXT("#394654")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer + 1, Layout.CriterionBounds, Color(TEXT("#20364e")), Color(TEXT("#67b7ff")), 1.0f, 8.0f);
	PaintBox(Out, Geometry, Layer, Layout.PlotBounds, Color(TEXT("#171d25")), Color(TEXT("#394654")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer, Layout.CallerBounds, Color(TEXT("#172535")), Color(TEXT("#4d91c7")), 1.0f, 9.0f);
	PaintBox(Out, Geometry, Layer, Layout.CalleeBounds, Color(TEXT("#241b31")), Color(TEXT("#8863a8")), 1.0f, 9.0f);
	PaintDashedLine(Out, Geometry, Layer + 1, Layout.PortalStart, Layout.PortalEnd,
		Color(TEXT("#7F8B99")), 2.0f);
	PaintBox(Out, Geometry, Layer, Layout.FrontierBounds, Color(TEXT("#251a20")), Color(TEXT("#ff6b7a")), 2.0f, 7.0f);
	PaintBox(Out, Geometry, Layer, Layout.ActionsBounds, Color(TEXT("#171d25")), Color(TEXT("#394654")), 1.0f, 7.0f);
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Layout.LayoutLedger.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source = Layout.LayoutLedger.FindPort(
			Edge.SourceUnitId, Edge.SourcePortLabel, false);
		const FBlueprintLensLayoutPortPlacement* Target = Layout.LayoutLedger.FindPort(
			Edge.TargetUnitId, Edge.TargetPortLabel, true);
		if (Source == nullptr || Target == nullptr)
		{
			continue;
		}
		TArray<FVector2D> Points{Source->Position};
		Points.Append(Edge.BendPoints);
		Points.Add(Target->Position);
		const FLinearColor EdgeColor = RelationColor(Edge);
		if (Edge.RelationId.StartsWith(TEXT("binding:argument:")))
		{
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				PaintDashedLine(Out, Geometry, Layer + 1, Points[Index - 1], Points[Index],
					EdgeColor, IsRelationHighlighted(Edge.RelationId) ? 4.0f : 3.0f);
			}
		}
		else
		{
			PaintLine(Out, Geometry, Layer + 1, Points, EdgeColor,
				IsRelationHighlighted(Edge.RelationId) ? 4.0f : 3.0f);
		}
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : Layout.LayoutLedger.Nodes)
	{
		const bool bSelected = Node.UnitId == SelectedOccurrenceId.Get(FString());
		const FLinearColor Outline = Node.UnitId == Layout.CallOccurrenceId
			? Color(TEXT("#54b8ff")) : Node.UnitId == Layout.OperatorOccurrenceId
				? Color(TEXT("#a8e063")) : Color(TEXT("#c78bf4"));
		PaintBox(Out, Geometry, Layer + 2,
			FBox2D(Node.Position, Node.Position + Node.Size),
			Node.UnitId == Layout.OperatorOccurrenceId ? Color(TEXT("#223021")) :
				Node.UnitId == Layout.CallOccurrenceId ? Color(TEXT("#1b3043")) : Color(TEXT("#30213b")),
			Outline, bSelected ? 3.0f : 1.5f, 8.0f);
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Layout.LayoutLedger.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source = Layout.LayoutLedger.FindPort(
			Edge.SourceUnitId, Edge.SourcePortLabel, false);
		const FBlueprintLensLayoutPortPlacement* Target = Layout.LayoutLedger.FindPort(
			Edge.TargetUnitId, Edge.TargetPortLabel, true);
		if (Source == nullptr || Target == nullptr)
		{
			continue;
		}
		const FLinearColor EdgeColor = RelationColor(Edge);
		PaintBox(
			Out,
			Geometry,
			Layer + 3,
			FBox2D(
				Source->Position - FVector2D(3.5f, 3.5f),
				Source->Position + FVector2D(3.5f, 3.5f)),
			EdgeColor,
			Color(TEXT("#10151c")),
			1.0f,
			3.5f);
		PaintBox(
			Out,
			Geometry,
			Layer + 3,
			FBox2D(
				Target->Position - FVector2D(3.5f, 3.5f),
				Target->Position + FVector2D(3.5f, 3.5f)),
			Color(TEXT("#10151c")),
			EdgeColor,
			1.5f,
			3.5f);
		const FVector2D Previous = Edge.BendPoints.IsEmpty()
			? Source->Position
			: Edge.BendPoints.Last();
		PaintArrowhead(
			Out,
			Geometry,
			Layer + 3,
			Previous,
			Target->Position,
			EdgeColor);
	}
	for (const FBlueprintLensLC5ActionLayout& Action : Layout.Actions)
	{
		PaintBox(Out, Geometry, Layer + 2, Action.Bounds,
			IsActionActive(Action.ActionId) ? Color(TEXT("#344555")) : Color(TEXT("#222d39")),
			Color(TEXT("#607284")), 1.0f, 5.0f);
	}
	return SCompoundWidget::OnPaint(
		Args, Geometry, Culling, Out, Layer + 4, Style, bEnabled);
}

FReply SBlueprintLensLC5TypedPortal::OnMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	for (const FBlueprintLensLC5ActionLayout& Action : Session.Layout.Actions)
	{
		if (Action.Bounds.IsInside(Local))
		{
			OnAction.ExecuteIfBound(Action.ActionId);
			return FReply::Handled();
		}
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : Session.Layout.LayoutLedger.Nodes)
	{
		if (FBox2D(Node.Position, Node.Position + Node.Size).IsInside(Local))
		{
			OnOccurrenceSelected.ExecuteIfBound(Node.UnitId);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FVector2D SBlueprintLensLC5TypedPortal::ComputeDesiredSize(const float) const
{
	return Session.Layout.CanvasSize;
}
