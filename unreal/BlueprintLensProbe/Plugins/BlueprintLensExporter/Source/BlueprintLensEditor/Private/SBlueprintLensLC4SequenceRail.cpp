#include "SBlueprintLensLC4SequenceRail.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FLinearColor LC4Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

namespace LC4Palette
{
const TCHAR* PanelFill = TEXT("#202327");
const TCHAR* PanelStroke = TEXT("#3a3f45");
const TCHAR* OrderFill = TEXT("#171a1d");
const TCHAR* OrderStroke = TEXT("#34393e");
const TCHAR* Cyan = TEXT("#69d9df");
const TCHAR* IncludedFill = TEXT("#22383b");
const TCHAR* IncludedStroke = TEXT("#5bd4da");
const TCHAR* IncludedText = TEXT("#a7f3d0");
const TCHAR* OutsideFill = TEXT("#292317");
const TCHAR* OutsideStroke = TEXT("#d7a94e");
const TCHAR* OutsideText = TEXT("#f6cf73");
const TCHAR* EmptyFill = TEXT("#34393e");
const TCHAR* EmptyStroke = TEXT("#8a9299");
const TCHAR* MergeFill = TEXT("#202327");
const TCHAR* MergeStroke = TEXT("#e0b856");
const TCHAR* ReconvergedFill = TEXT("#2c291c");
const TCHAR* CriterionFill = TEXT("#493d1d");
const TCHAR* CriterionStroke = TEXT("#f1c85d");
const TCHAR* WarningFill = TEXT("#211f17");
const TCHAR* Text = TEXT("#e8edf0");
const TCHAR* Muted = TEXT("#aeb7be");
const TCHAR* Selected = TEXT("#ffffff");
} // namespace LC4Palette

const FSlateBrush& RoundedBrush(
	const float Radius,
	const TCHAR* OutlineHex,
	const float OutlineWidth)
{
	struct FKey
	{
		float Radius;
		FString Outline;
		float Width;
		bool operator==(const FKey& Other) const
		{
			return Outline == Other.Outline &&
				FMath::IsNearlyEqual(Radius, Other.Radius) &&
				FMath::IsNearlyEqual(Width, Other.Width);
		}
	};
	static TArray<TPair<FKey, TSharedPtr<FSlateRoundedBoxBrush>>> Cache;
	const FKey Key{Radius, OutlineHex, OutlineWidth};
	for (const auto& Entry : Cache)
	{
		if (Entry.Key == Key)
		{
			return *Entry.Value;
		}
	}
	Cache.Emplace(
		Key,
		MakeShared<FSlateRoundedBoxBrush>(
			FSlateColor(FLinearColor::White),
			Radius,
			FSlateColor(LC4Color(OutlineHex)),
			OutlineWidth,
			FVector2f(Radius * 2.0f, Radius * 2.0f)));
	return *Cache.Last().Value;
}

FVector2D BoxSize(const FBox2D& Box)
{
	return Box.Max - Box.Min;
}

void PaintLine(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const TArray<FVector2D>& Points,
	const FLinearColor& Color,
	const float Thickness)
{
	if (Points.Num() < 2)
	{
		return;
	}
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		Layer,
		Geometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Color,
		true,
		Thickness);
}

void PaintDashedLine(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Start,
	const FVector2D& End,
	const FLinearColor& Color,
	const float Thickness)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	for (float Cursor = 0.0f; Cursor < Length; Cursor += 14.0f)
	{
		PaintLine(
			OutDrawElements,
			Geometry,
			Layer,
			{Start + Direction * Cursor,
			 Start + Direction * FMath::Min(Cursor + 8.0f, Length)},
			Color,
			Thickness);
	}
}

FVector2D CubicPoint(
	const FVector2D& Start,
	const FVector2D& ControlA,
	const FVector2D& ControlB,
	const FVector2D& End,
	const float T)
{
	const float U = 1.0f - T;
	return U * U * U * Start +
		3.0f * U * U * T * ControlA +
		3.0f * U * T * T * ControlB +
		T * T * T * End;
}

void PaintArrowHead(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& Geometry,
	const int32 Layer,
	const FVector2D& Tip,
	const FVector2D& Direction,
	const FLinearColor& Color,
	const float)
{
	const FVector2D Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		return;
	}
	const FVector2D Normal(-Unit.Y, Unit.X);
	const FVector2D Base = Tip - Unit * 11.0f;
	const TArray<FSlateVertex> Vertices = {
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Tip),
			FVector2f(0.5f, 0.0f),
			Color.ToFColor(true)),
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Base + Normal * 6.0f),
			FVector2f(0.0f, 1.0f),
			Color.ToFColor(true)),
		FSlateVertex::Make(
			Geometry.GetAccumulatedRenderTransform(),
			FVector2f(Base - Normal * 6.0f),
			FVector2f(1.0f, 1.0f),
			Color.ToFColor(true))};
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

const FBlueprintLensLayoutNodePlacement* FindNode(
	const FBlueprintLensLayoutLedger& Ledger,
	const FString& UnitId)
{
	return Ledger.Nodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLayoutNodePlacement& Node)
		{
			return Node.UnitId == UnitId;
		});
}

FSlateFontInfo LC4Font(const int32 Size, const bool bBold)
{
	// SVG oracle sizes are CSS pixels; Slate font sizes are typographic points.
	// At 96 DPI, 1 CSS px = 0.75 pt.
	const int32 PointSize = FMath::Max(
		1,
		FMath::RoundToInt(static_cast<float>(Size) * 0.75f));
	return FCoreStyle::GetDefaultFontStyle(
		bBold ? TEXT("Bold") : TEXT("Regular"),
		PointSize);
}
} // namespace

void SBlueprintLensLC4SequenceRail::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	SelectedOrdinal = InArgs._SelectedOrdinal;
	Evidence = InArgs._Evidence;
	ActiveActionId = InArgs._ActiveActionId;
	OnOutputSelected = InArgs._OnOutputSelected;
	OnShowAllText = InArgs._OnShowAllText;
	OnToggleEvidence = InArgs._OnToggleEvidence;
	OnOpenSource = InArgs._OnOpenSource;

	if (!Session.IsRenderable(Projection))
	{
		Session = FBlueprintLensLC4SequenceLayoutSession::Build(
			Projection,
			700.0f);
	}
	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth;
	if (LastLayoutWidth <= 1.0f)
	{
		LastLayoutWidth = 700.0f;
	}
	RebuildVisualChildren();
}

bool SBlueprintLensLC4SequenceRail::IsActionActive(
	const FString& ActionId) const
{
	return ActionId != TEXT("open-source") &&
		ActiveActionId.Get(TEXT("select")) == ActionId;
}

void SBlueprintLensLC4SequenceRail::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Width = AllottedGeometry.GetLocalSize().X;
	const float TargetWidth = Width <= 455.0f
		? 430.0f
		: Width <= 590.0f ? 480.0f : 700.0f;
	if (Width <= 1.0f || FMath::IsNearlyEqual(TargetWidth, LastLayoutWidth, 0.5f))
	{
		PendingLayoutWidth = 0.0f;
		return;
	}
	if (!FMath::IsNearlyEqual(TargetWidth, PendingLayoutWidth, 0.5f))
	{
		PendingLayoutWidth = TargetWidth;
		PendingLayoutStartTime = InCurrentTime;
		return;
	}
	if (InCurrentTime - PendingLayoutStartTime >= 0.12)
	{
		RebuildSession(PendingLayoutWidth);
		PendingLayoutWidth = 0.0f;
	}
}

FVector2D SBlueprintLensLC4SequenceRail::ComputeDesiredSize(float) const
{
	return Session.Layout.CanvasSize;
}

void SBlueprintLensLC4SequenceRail::RebuildSession(const float TargetWidth)
{
	const FBlueprintLensLC4SequenceLayoutSessionResult Candidate =
		FBlueprintLensLC4SequenceLayoutSession::Build(
			Projection,
			TargetWidth);
	if (Candidate.IsRenderable(Projection))
	{
		Session = Candidate;
		LastLayoutWidth = TargetWidth;
		RebuildVisualChildren();
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SBlueprintLensLC4SequenceRail::RebuildVisualChildren()
{
	VisualCanvas = SNew(SCanvas);
	const FBlueprintLensLC4SequenceLayout& Layout = Session.Layout;
	auto AddText = [this](
		const FString& Text,
		const FVector2D& Baseline,
		const FVector2D& Size,
		const int32 FontSize,
		const bool bBold,
		const FLinearColor& Color,
		const ETextJustify::Type Justification = ETextJustify::Left)
	{
		VisualCanvas->AddSlot()
		.Position(FVector2D(Baseline.X, Baseline.Y - FontSize - 3.0f))
		.Size(Size)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(LC4Font(FontSize, bBold))
				.ColorAndOpacity(Color)
				.Justification(Justification)
				.Clipping(EWidgetClipping::ClipToBounds)
				.Visibility(EVisibility::HitTestInvisible)
		];
	};
	if (Projection.bLiveExplanation)
	{
		AddText(
			TEXT("SEQUENCE DISCLOSURE RAIL"),
			Layout.TitlePosition,
			FVector2D(Layout.CanvasSize.X - 48.0f, 30.0f),
			21,
			true,
			LC4Color(LC4Palette::Cyan));
		AddText(
			TEXT("LC4-SEQ live static answer · complete declared sibling inventory"),
			Layout.SubtitlePositions[0],
			FVector2D(Layout.CanvasSize.X - 48.0f, 18.0f),
			13,
			false,
			LC4Color(LC4Palette::Muted));
		AddText(
			Projection.SequenceReaderLabel,
			FVector2D(42.0f, 105.0f),
			FVector2D(Layout.CanvasSize.X - 84.0f, 18.0f),
			11,
			true,
			LC4Color(LC4Palette::Cyan));
		TArray<FString> PinNames;
		for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
		{
			PinNames.Add(Route.SourcePinName);
		}
		AddText(
			FString::Join(PinNames, TEXT(" → ")),
			Layout.OrderTextPosition,
			FVector2D(Layout.CanvasSize.X - 84.0f, 22.0f),
			15,
			true,
			LC4Color(LC4Palette::Text));
		AddText(
			FString::Printf(
				TEXT("%d declared · %d connected · %d unconnected · %d included · %d excluded"),
				Projection.Counts.DeclaredOutputs,
				Projection.Counts.ConnectedOutputs,
				Projection.Counts.UnconnectedOutputs,
				Projection.Counts.CriterionIncludedOutputs,
				Projection.Counts.OutsideCriterionConnectedOutputs +
					Projection.Counts.UnconnectedOutputs),
			Layout.CountTextPositions[0],
			FVector2D(Layout.CanvasSize.X - 84.0f, 16.0f),
			10,
			false,
			LC4Color(LC4Palette::Muted));
		AddText(
			TEXT("PIN ORDER"),
			Layout.PinOrderPosition,
			FVector2D(100.0f, 18.0f),
			11,
			true,
			LC4Color(LC4Palette::Cyan));

		for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
		{
			const FBlueprintLensLC4SequenceStationLayout* Station =
				Layout.FindStation(Route.Ordinal);
			if (Station == nullptr)
			{
				continue;
			}
			const bool bExcluded = Route.ConnectionState ==
				EBlueprintLensLC4ConnectionState::Unconnected ||
				Route.CriterionRelation ==
					EBlueprintLensLC4CriterionRelation::Outside;
			const FString RouteText = bExcluded
				? FString::Printf(
					TEXT("%s · EXCLUDED · %s"),
					*Route.SourcePinName,
					*Route.SummaryText)
				: FString::Printf(
					TEXT("%s · INCLUDED · %s"),
					*Route.SourcePinName,
					*Route.SummaryText);
			TSharedRef<STextBlock> RouteLabel = SNew(STextBlock)
				.Text(FText::FromString(RouteText))
				.Font(LC4Font(10, true))
				.ColorAndOpacity(bExcluded
					? LC4Color(LC4Palette::OutsideText)
					: LC4Color(LC4Palette::IncludedText))
				.AutoWrapText(true)
				.Visibility(EVisibility::HitTestInvisible);
			if (bExcluded)
			{
				RouteLabel->SetTag(FName(TEXT(
					"BlueprintLens.Automation.LC4SequenceExcludedSibling")));
			}
			VisualCanvas->AddSlot()
			.Position(Station->LabelBounds.Min)
			.Size(FVector2D(
				Station->LabelBounds.GetSize().X,
				42.0f))
			[
				RouteLabel
			];
		}

		for (const FBlueprintLensLC4SequenceVisualNode& Visual :
			 Layout.VisualNodes)
		{
			if (Visual.Kind ==
				EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine)
			{
				continue;
			}
			const FBlueprintLensLayoutNodePlacement* Node =
				FindNode(Layout.LayoutLedger, Visual.UnitId);
			if (Node == nullptr)
			{
				continue;
			}
			AddText(
				Visual.PrimaryText,
				FVector2D(Node->Position.X + 6.0f, Node->Position.Y + 25.0f),
				FVector2D(Node->Size.X - 12.0f, 20.0f),
				12,
				true,
				LC4Color(LC4Palette::Text),
				ETextJustify::Center);
			AddText(
				Visual.SecondaryText,
				FVector2D(Node->Position.X + 6.0f, Node->Position.Y + 45.0f),
				FVector2D(Node->Size.X - 12.0f, 16.0f),
				9,
				false,
				LC4Color(LC4Palette::Muted),
				ETextJustify::Center);
		}
		for (const FBlueprintLensLC4SequenceActionLayout& Action : Layout.Actions)
		{
			AddText(
				Action.Label,
				FVector2D(Action.Bounds.Min.X, Action.Bounds.Min.Y + 23.0f),
				FVector2D(BoxSize(Action.Bounds).X, 18.0f),
				11,
				false,
				LC4Color(LC4Palette::Text),
				ETextJustify::Center);
		}
		ChildSlot[VisualCanvas.ToSharedRef()];
		return;
	}

	AddText(TEXT("SEQUENCE DISCLOSURE RAIL"), Layout.TitlePosition, FVector2D(Layout.CanvasSize.X - 48.0f, 30.0f), 21, true, LC4Color(LC4Palette::Cyan));
	const TArray<FString> Subtitles = Layout.Mode == EBlueprintLensLC4SequenceLayoutMode::Wide700
		? TArray<FString>({TEXT("One ordinal spine; every output, boundary and shared suffix is visible by default.")})
		: Layout.Mode == EBlueprintLensLC4SequenceLayoutMode::Compact480
			? TArray<FString>({TEXT("One ordinal spine; compact suffix below convergence."),TEXT("All outputs and boundaries remain visible.")})
			: TArray<FString>({TEXT("One ordinal spine; route detail stacks below each station."),TEXT("Labelled routes rejoin one canonical suffix.")});
	for (int32 Index = 0; Index < Layout.SubtitlePositions.Num(); ++Index)
	{
		AddText(Subtitles[Index], Layout.SubtitlePositions[Index], FVector2D(Layout.CanvasSize.X - 48.0f, 18.0f), 13, false, LC4Color(LC4Palette::Muted));
	}
	AddText(TEXT("SYNCHRONOUS SOURCE ORDER"), Layout.OrderStagePosition, FVector2D(260,18), 11, true, LC4Color(LC4Palette::Cyan));
	AddText(TEXT("then_0 → then_1 → then_2 → then_3"), Layout.OrderTextPosition, FVector2D(330,22), 15, true, LC4Color(LC4Palette::Text));
	TArray<FString> CountLines;
	if (Layout.Mode == EBlueprintLensLC4SequenceLayoutMode::Wide700)
	{
		CountLines = {TEXT("4 declared · 3 connected · 1 empty"),TEXT("2 included · 1 outside · 0 indeterminate")};
	}
	else if (Layout.Mode == EBlueprintLensLC4SequenceLayoutMode::Compact480)
	{
		CountLines = {TEXT("4 declared · 3 connected"),TEXT("1 empty · 2 included"),TEXT("1 outside · 0 indeterminate")};
	}
	else
	{
		CountLines = {TEXT("4 declared · 3 connected · 1 empty"),TEXT("2 included · 1 outside · 0 indeterminate")};
	}
	for (int32 Index = 0; Index < Layout.CountTextPositions.Num(); ++Index)
	{
		AddText(CountLines[Index], Layout.CountTextPositions[Index], FVector2D(250,14), 10, false, LC4Color(LC4Palette::Muted));
	}
	AddText(TEXT("PIN ORDER"), Layout.PinOrderPosition, FVector2D(100,18), 11, true, LC4Color(LC4Palette::Cyan));

	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		const FBlueprintLensLC4SequenceStationLayout* Station = Layout.FindStation(Route.Ordinal);
		if (Station == nullptr)
		{
			continue;
		}
		const FLinearColor StateColor = Route.ConnectionState == EBlueprintLensLC4ConnectionState::Unconnected
			? LC4Color(TEXT("#c1c7cc"))
			: Route.CriterionRelation == EBlueprintLensLC4CriterionRelation::Included
				? LC4Color(LC4Palette::IncludedText)
				: LC4Color(LC4Palette::OutsideText);
		const FString State = Route.ConnectionState == EBlueprintLensLC4ConnectionState::Unconnected
			? FString::Printf(TEXT("THEN_%d · UNCONNECTED"), Route.Ordinal)
			: Route.CriterionRelation == EBlueprintLensLC4CriterionRelation::Included
				? FString::Printf(TEXT("THEN_%d · INCLUDED"), Route.Ordinal)
				: FString::Printf(TEXT("THEN_%d · OUTSIDE CRITERION"), Route.Ordinal);
		AddText(FString::FromInt(Route.Ordinal), Station->Center + FVector2D(-8,5), FVector2D(16,18), 12, true, LC4Color(LC4Palette::Text), ETextJustify::Center);
		AddText(State, FVector2D(Station->LabelBounds.Min.X, Station->LabelBounds.Min.Y + 10.0f), BoxSize(Station->LabelBounds), 10, true, StateColor);
	}

	for (const FBlueprintLensLC4SequenceVisualNode& Visual : Layout.VisualNodes)
	{
		if (Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine)
		{
			continue;
		}
		const FBlueprintLensLayoutNodePlacement* Node = FindNode(Layout.LayoutLedger, Visual.UnitId);
		if (Node == nullptr)
		{
			continue;
		}
		const FVector2D Center(Node->Position.X + Node->Size.X * 0.5f, Node->Position.Y);
		if (Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Criterion)
		{
			AddText(Visual.SecondaryText, FVector2D(Center.X - Node->Size.X * 0.5f, Node->Position.Y + 24), FVector2D(Node->Size.X,16), 9, true, LC4Color(LC4Palette::CriterionStroke), ETextJustify::Center);
			AddText(Visual.PrimaryText, FVector2D(Center.X - Node->Size.X * 0.5f, Node->Position.Y + 50), FVector2D(Node->Size.X,20), 14, true, LC4Color(TEXT("#ffe394")), ETextJustify::Center);
		}
		else
		{
			const FLinearColor Primary = Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Reconverged
				? LC4Color(TEXT("#ffe08a"))
				: LC4Color(LC4Palette::Text);
			AddText(Visual.PrimaryText, FVector2D(Node->Position.X, Node->Position.Y + 24), FVector2D(Node->Size.X,20), Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Included ? 15 : 12, true, Primary, ETextJustify::Center);
			AddText(Visual.SecondaryText, FVector2D(Node->Position.X, Node->Position.Y + 43), FVector2D(Node->Size.X,16), 10, false, LC4Color(LC4Palette::Muted), ETextJustify::Center);
		}
	}

	AddText(TEXT("M1"), Layout.MergeCenter + FVector2D(-18,-4), FVector2D(36,14), 10, true, LC4Color(TEXT("#ffe08a")), ETextJustify::Center);
	AddText(TEXT("merge"), Layout.MergeCenter + FVector2D(-18,10), FVector2D(36,14), 10, false, LC4Color(LC4Palette::Muted), ETextJustify::Center);
	AddText(TEXT("ORDINARY MERGE · NO WAIT"), FVector2D(Layout.WarningBounds.Min.X, Layout.WarningBounds.Min.Y + 21), FVector2D(BoxSize(Layout.WarningBounds).X,16), 10, true, LC4Color(LC4Palette::CriterionStroke), ETextJustify::Center);
	AddText(TEXT("no join bar · no participant · no single-fire claim"), FVector2D(Layout.WarningBounds.Min.X, Layout.WarningBounds.Min.Y + 41), FVector2D(BoxSize(Layout.WarningBounds).X,14), 9, false, LC4Color(LC4Palette::Muted), ETextJustify::Center);
	AddText(TEXT("OUTSIDE"), Layout.OutsideLabelPosition, FVector2D(120,14), 10, true, LC4Color(LC4Palette::OutsideText));
	for (int32 LineIndex = 0; LineIndex < Layout.OutsideDetailLines.Num(); ++LineIndex)
	{
		AddText(
			Layout.OutsideDetailLines[LineIndex],
			Layout.OutsideDetailLinePositions[LineIndex],
			Layout.OutsideDetailLineSizes[LineIndex],
			Layout.OutsideDetailFontSize,
			false,
			LC4Color(LC4Palette::Muted));
	}
	AddText(TEXT("declared empty output · no edge"), Layout.UnconnectedLabelPosition, FVector2D(220,16), 12, false, LC4Color(LC4Palette::Muted));
	for (const FBlueprintLensLC4SequenceActionLayout& Action : Layout.Actions)
	{
		AddText(Action.Label, FVector2D(Action.Bounds.Min.X, Action.Bounds.Min.Y + 25), FVector2D(BoxSize(Action.Bounds).X,18), 12, false, LC4Color(LC4Palette::Text), ETextJustify::Center);
	}
	if (!Layout.FooterText.IsEmpty())
	{
		AddText(Layout.FooterText, Layout.FooterPosition, FVector2D(Layout.CanvasSize.X - 48,16), 11, true, LC4Color(LC4Palette::CriterionStroke));
	}
	ChildSlot[VisualCanvas.ToSharedRef()];
}

int32 SBlueprintLensLC4SequenceRail::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FBlueprintLensLC4SequenceLayout& Layout = Session.Layout;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(Layout.CanvasSize, FSlateLayoutTransform()),
		&RoundedBrush(16.0f, LC4Palette::PanelStroke, 1.0f),
		ESlateDrawEffect::None,
		LC4Color(LC4Palette::PanelFill));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(BoxSize(Layout.OrderBandBounds), FSlateLayoutTransform(Layout.OrderBandBounds.Min)),
		&RoundedBrush(9.0f, LC4Palette::OrderStroke, 1.0f),
		ESlateDrawEffect::None,
		LC4Color(LC4Palette::OrderFill));
	PaintLine(OutDrawElements, AllottedGeometry, LayerId + 2, {Layout.SpineStart,Layout.SpineEnd}, LC4Color(LC4Palette::Cyan), 3.0f);
	PaintArrowHead(OutDrawElements, AllottedGeometry, LayerId + 2, Layout.SpineEnd, Layout.SpineEnd-Layout.SpineStart, LC4Color(LC4Palette::Cyan), 3.0f);
	if (Projection.bLiveExplanation)
	{
		for (const FBlueprintLensLayoutEdgePlacement& Edge : Layout.LayoutLedger.Edges)
		{
			const FBlueprintLensLayoutPortPlacement* Source =
				Layout.LayoutLedger.FindPort(
					Edge.SourceUnitId, Edge.SourcePortLabel, false);
			const FBlueprintLensLayoutPortPlacement* Target =
				Layout.LayoutLedger.FindPort(
					Edge.TargetUnitId, Edge.TargetPortLabel, true);
			if (Source == nullptr || Target == nullptr)
			{
				continue;
			}
			const FBlueprintLensLC4SequenceRoute* Route =
				Projection.Routes.FindByPredicate(
					[&Edge](const FBlueprintLensLC4SequenceRoute& Candidate)
					{
						return Candidate.RouteRelationIds.Contains(Edge.RelationId);
					});
			const bool bOutside = Route != nullptr &&
				Route->CriterionRelation ==
					EBlueprintLensLC4CriterionRelation::Outside;
			const FLinearColor Color = bOutside
				? LC4Color(LC4Palette::OutsideStroke)
				: LC4Color(LC4Palette::Cyan);
			PaintLine(
				OutDrawElements,
				AllottedGeometry,
				LayerId + 3,
				{Source->Position, Target->Position},
				Color,
				3.0f);
			PaintArrowHead(
				OutDrawElements,
				AllottedGeometry,
				LayerId + 3,
				Target->Position,
				Target->Position - Source->Position,
				Color,
				3.0f);
		}
		for (const FBlueprintLensLC4SequenceVisualNode& Visual : Layout.VisualNodes)
		{
			if (Visual.Kind ==
				EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine)
			{
				continue;
			}
			const FBlueprintLensLayoutNodePlacement* Node =
				FindNode(Layout.LayoutLedger, Visual.UnitId);
			if (Node == nullptr)
			{
				continue;
			}
			const bool bOutside = Visual.Kind ==
				EBlueprintLensLC4SequenceVisualNodeKind::Outside;
			const bool bCriterion = Visual.Kind ==
				EBlueprintLensLC4SequenceVisualNodeKind::Criterion;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 4,
				AllottedGeometry.ToPaintGeometry(
					Node->Size, FSlateLayoutTransform(Node->Position)),
				&RoundedBrush(
					8.0f,
					bOutside
						? LC4Palette::OutsideStroke
						: bCriterion
						? LC4Palette::CriterionStroke
						: LC4Palette::IncludedStroke,
					1.0f),
				ESlateDrawEffect::None,
				LC4Color(
					bOutside
						? LC4Palette::OutsideFill
						: bCriterion
						? LC4Palette::CriterionFill
						: LC4Palette::IncludedFill));
		}
		for (const FBlueprintLensLC4SequenceStationLayout& Station :
			 Layout.Stations)
		{
			const FBlueprintLensLC4SequenceRoute* Route =
				Projection.FindRoute(Station.Ordinal);
			if (Route == nullptr)
			{
				continue;
			}
			const bool bSelected =
				SelectedOrdinal.Get(INDEX_NONE) == Station.Ordinal;
			const bool bExcluded = Route->ConnectionState ==
				EBlueprintLensLC4ConnectionState::Unconnected ||
				Route->CriterionRelation ==
					EBlueprintLensLC4CriterionRelation::Outside;
			const FVector2D Size(
				Station.Radius * 2.0f,
				Station.Radius * 2.0f);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 5,
				AllottedGeometry.ToPaintGeometry(
					Size,
					FSlateLayoutTransform(Station.Center - Size * 0.5f)),
				&RoundedBrush(
					Station.Radius,
					bSelected
						? LC4Palette::Selected
						: bExcluded
						? LC4Palette::OutsideStroke
						: LC4Palette::IncludedStroke,
					bSelected ? 3.0f : 2.0f),
				ESlateDrawEffect::None,
				LC4Color(
					bExcluded
						? LC4Palette::OutsideFill
						: LC4Palette::IncludedFill));
		}
		for (const FBlueprintLensLC4SequenceActionLayout& Action : Layout.Actions)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 4,
				AllottedGeometry.ToPaintGeometry(
					BoxSize(Action.Bounds),
					FSlateLayoutTransform(Action.Bounds.Min)),
				&RoundedBrush(7.0f, TEXT("#4a5056"), 1.0f),
				ESlateDrawEffect::None,
				LC4Color(LC4Palette::OrderFill));
		}
		return SCompoundWidget::OnPaint(
			Args,
			AllottedGeometry,
			MyCullingRect,
			OutDrawElements,
			LayerId + 6,
			InWidgetStyle,
			bParentEnabled);
	}

	for (const FBlueprintLensLayoutEdgePlacement& Edge : Layout.LayoutLedger.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source = Layout.LayoutLedger.FindPort(Edge.SourceUnitId,Edge.SourcePortLabel,false);
		const FBlueprintLensLayoutPortPlacement* Target = Layout.LayoutLedger.FindPort(Edge.TargetUnitId,Edge.TargetPortLabel,true);
		if (Source == nullptr || Target == nullptr)
		{
			continue;
		}
		const bool bOutside = Projection.Routes[2].RouteRelationIds.Contains(Edge.RelationId);
		const bool bMergeCurve =
			Projection.Routes[0].RouteRelationIds.Last() == Edge.RelationId ||
			Projection.Routes[1].RouteRelationIds.Last() == Edge.RelationId;
		TArray<FVector2D> Points;
		if (bMergeCurve && Edge.BendPoints.Num() == 2)
		{
			for (int32 Segment = 0; Segment <= 24; ++Segment)
			{
				Points.Add(CubicPoint(Source->Position,Edge.BendPoints[0],Edge.BendPoints[1],Target->Position,static_cast<float>(Segment)/24.0f));
			}
		}
		else
		{
			Points.Add(Source->Position);
			Points.Append(Edge.BendPoints);
			Points.Add(Target->Position);
		}
		const FLinearColor Color = bOutside ? LC4Color(LC4Palette::OutsideStroke) : LC4Color(LC4Palette::Cyan);
		PaintLine(OutDrawElements,AllottedGeometry,LayerId+3,Points,Color,bOutside?4.0f:3.0f);
		if (bMergeCurve || Projection.Merge.SharedSuffixRelationIds.Contains(Edge.RelationId))
		{
			PaintArrowHead(OutDrawElements,AllottedGeometry,LayerId+3,Points.Last(),Points.Last()-Points[Points.Num()-2],Color,3.0f);
		}
	}
	PaintLine(OutDrawElements,AllottedGeometry,LayerId+3,Layout.MergeToSuffixRoute,LC4Color(LC4Palette::Cyan),3.0f);
	PaintArrowHead(OutDrawElements,AllottedGeometry,LayerId+3,Layout.MergeToSuffixRoute.Last(),Layout.MergeToSuffixRoute.Last()-Layout.MergeToSuffixRoute[0],LC4Color(LC4Palette::Cyan),3.0f);

	for (const FBlueprintLensLC4SequenceVisualNode& Visual : Layout.VisualNodes)
	{
		if (Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine)
		{
			continue;
		}
		const FBlueprintLensLayoutNodePlacement* Node = FindNode(Layout.LayoutLedger, Visual.UnitId);
		if (Node == nullptr)
		{
			continue;
		}
		const TCHAR* Fill = Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Included
			? LC4Palette::IncludedFill
			: Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Outside
				? LC4Palette::OutsideFill
				: Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Reconverged
					? LC4Palette::ReconvergedFill
					: LC4Palette::CriterionFill;
		const TCHAR* Stroke = Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Included
			? LC4Palette::IncludedStroke
			: Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Outside
				? LC4Palette::OutsideStroke
				: Visual.Kind == EBlueprintLensLC4SequenceVisualNodeKind::Reconverged
					? LC4Palette::MergeStroke
					: LC4Palette::CriterionStroke;
		FSlateDrawElement::MakeBox(OutDrawElements,LayerId+4,AllottedGeometry.ToPaintGeometry(Node->Size,FSlateLayoutTransform(Node->Position)),&RoundedBrush(8.0f,Stroke,Visual.Kind==EBlueprintLensLC4SequenceVisualNodeKind::Criterion?2.0f:1.0f),ESlateDrawEffect::None,LC4Color(Fill));
	}

	for (const FBlueprintLensLC4SequenceStationLayout& Station : Layout.Stations)
	{
		const FBlueprintLensLC4SequenceRoute* Route = Projection.FindRoute(Station.Ordinal);
		if (Route == nullptr)
		{
			continue;
		}
		const bool bSelected = SelectedOrdinal.Get(INDEX_NONE) == Station.Ordinal;
		const TCHAR* Fill = Route->ConnectionState == EBlueprintLensLC4ConnectionState::Unconnected
			? LC4Palette::EmptyFill
			: Route->CriterionRelation == EBlueprintLensLC4CriterionRelation::Included
				? TEXT("#134044") : TEXT("#493b18");
		const TCHAR* Stroke = bSelected
			? LC4Palette::Selected
			: Route->ConnectionState == EBlueprintLensLC4ConnectionState::Unconnected
				? LC4Palette::EmptyStroke
				: Route->CriterionRelation == EBlueprintLensLC4CriterionRelation::Included
					? TEXT("#70dfe4") : LC4Palette::OutsideStroke;
		const FVector2D Size(Station.Radius*2.0f,Station.Radius*2.0f);
		FSlateDrawElement::MakeBox(OutDrawElements,LayerId+5,AllottedGeometry.ToPaintGeometry(Size,FSlateLayoutTransform(Station.Center-Size*0.5f)),&RoundedBrush(Station.Radius,Stroke,bSelected?3.0f:2.0f),ESlateDrawEffect::None,LC4Color(Fill));
	}
	const FVector2D MergeSize(Layout.MergeRadius*2.0f,Layout.MergeRadius*2.0f);
	FSlateDrawElement::MakeBox(OutDrawElements,LayerId+5,AllottedGeometry.ToPaintGeometry(MergeSize,FSlateLayoutTransform(Layout.MergeCenter-MergeSize*0.5f)),&RoundedBrush(Layout.MergeRadius,LC4Palette::MergeStroke,3.0f),ESlateDrawEffect::None,LC4Color(LC4Palette::MergeFill));
	FSlateDrawElement::MakeBox(OutDrawElements,LayerId+4,AllottedGeometry.ToPaintGeometry(BoxSize(Layout.WarningBounds),FSlateLayoutTransform(Layout.WarningBounds.Min)),&RoundedBrush(8.0f,LC4Palette::MergeStroke,1.0f),ESlateDrawEffect::None,LC4Color(LC4Palette::WarningFill));
	PaintLine(OutDrawElements,AllottedGeometry,LayerId+5,{Layout.OutsideTerminalStart,Layout.OutsideTerminalEnd},LC4Color(LC4Palette::OutsideStroke),4.0f);
	const FBlueprintLensLC4SequenceStationLayout* EmptyStation = Layout.FindStation(3);
	if (EmptyStation != nullptr)
	{
		PaintDashedLine(OutDrawElements,AllottedGeometry,LayerId+4,FVector2D(Layout.UnconnectedStubStartX,EmptyStation->Center.Y),FVector2D(Layout.UnconnectedXTopLeft.X,EmptyStation->Center.Y),LC4Color(LC4Palette::EmptyStroke),3.0f);
		PaintLine(OutDrawElements,AllottedGeometry,LayerId+5,{Layout.UnconnectedXTopLeft,Layout.UnconnectedXBottomRight},LC4Color(TEXT("#b4bac0")),3.0f);
		PaintLine(OutDrawElements,AllottedGeometry,LayerId+5,{FVector2D(Layout.UnconnectedXBottomRight.X,Layout.UnconnectedXTopLeft.Y),FVector2D(Layout.UnconnectedXTopLeft.X,Layout.UnconnectedXBottomRight.Y)},LC4Color(TEXT("#b4bac0")),3.0f);
	}
	for (int32 Index = 0; Index < Layout.Actions.Num(); ++Index)
	{
		const FBlueprintLensLC4SequenceActionLayout& Action = Layout.Actions[Index];
		const bool bActive = IsActionActive(Action.ActionId);
		FSlateDrawElement::MakeBox(OutDrawElements,LayerId+4,AllottedGeometry.ToPaintGeometry(BoxSize(Action.Bounds),FSlateLayoutTransform(Action.Bounds.Min)),&RoundedBrush(7.0f,bActive?TEXT("#3a4046"):TEXT("#4a5056"),1.0f),ESlateDrawEffect::None,LC4Color(bActive?TEXT("#3a4046"):LC4Palette::OrderFill));
	}
	return SCompoundWidget::OnPaint(Args,AllottedGeometry,MyCullingRect,OutDrawElements,LayerId+6,InWidgetStyle,bParentEnabled);
}

FReply SBlueprintLensLC4SequenceRail::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	for (const FBlueprintLensLC4SequenceActionLayout& Action : Session.Layout.Actions)
	{
		if (!Action.Bounds.IsInside(Local))
		{
			continue;
		}
		if (Action.ActionId == TEXT("select"))
		{
			OnOutputSelected.ExecuteIfBound(SelectedOrdinal.Get(INDEX_NONE) == INDEX_NONE ? 0 : SelectedOrdinal.Get());
		}
		else if (Action.ActionId == TEXT("all-text"))
		{
			OnShowAllText.ExecuteIfBound();
		}
		else if (Action.ActionId == TEXT("evidence"))
		{
			OnToggleEvidence.ExecuteIfBound();
		}
		else if (Action.ActionId == TEXT("open-source"))
		{
			OnOpenSource.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	for (const FBlueprintLensLC4SequenceStationLayout& Station : Session.Layout.Stations)
	{
		if (Station.HitBounds.IsInside(Local))
		{
			OnOutputSelected.ExecuteIfBound(Station.Ordinal);
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}
