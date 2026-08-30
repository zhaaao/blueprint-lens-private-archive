#include "SBlueprintLensLC3ValueConeCanvas.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FLinearColor NodeAccent(const EBlueprintLensLC3ValueConeNodeKind Kind)
{
	switch (Kind)
	{
	case EBlueprintLensLC3ValueConeNodeKind::Criterion:
		return FLinearColor(0.95f, 0.68f, 0.16f);
	case EBlueprintLensLC3ValueConeNodeKind::Operator:
		return FLinearColor(0.16f, 0.68f, 0.72f);
	case EBlueprintLensLC3ValueConeNodeKind::Control:
		return FLinearColor(0.29f, 0.48f, 0.66f);
	default:
		return FLinearColor(0.25f, 0.58f, 0.61f);
	}
}

FString NodeRoleLabel(const FBlueprintLensLC3ValueConeLayoutNode& Node)
{
	switch (Node.Kind)
	{
	case EBlueprintLensLC3ValueConeNodeKind::Criterion:
		return TEXT("CRITERION DOCK · ASSIGNED VALUE");
	case EBlueprintLensLC3ValueConeNodeKind::Operator:
		return FString::Printf(
			TEXT("MERGE OPERATOR · STAGE %d"),
			Node.DerivationDepth);
	case EBlueprintLensLC3ValueConeNodeKind::Control:
		return TEXT("EXECUTION CONTROL · SEPARATE RAIL");
	default:
		return FString::Printf(
			TEXT("VALUE TOKEN · STAGE %d"),
			Node.DerivationDepth);
	}
}

FLinearColor NodeSurface(const EBlueprintLensLC3ValueConeNodeKind Kind)
{
	switch (Kind)
	{
	case EBlueprintLensLC3ValueConeNodeKind::Criterion:
		return FLinearColor(0.16f, 0.11f, 0.035f, 0.98f);
	case EBlueprintLensLC3ValueConeNodeKind::Operator:
		return FLinearColor(0.035f, 0.13f, 0.15f, 0.98f);
	case EBlueprintLensLC3ValueConeNodeKind::Control:
		return FLinearColor(0.035f, 0.07f, 0.11f, 0.96f);
	default:
		return FLinearColor(0.035f, 0.095f, 0.105f, 0.96f);
	}
}

TArray<FVector2D> EdgeRoute(
	const FBlueprintLensLayoutLedger& Ledger,
	const FBlueprintLensLayoutEdgePlacement& Edge)
{
	TArray<FVector2D> Points;
	const FBlueprintLensLayoutPortPlacement* Source = Ledger.FindPort(
		Edge.SourceUnitId,
		Edge.SourcePortLabel,
		false);
	const FBlueprintLensLayoutPortPlacement* Target = Ledger.FindPort(
		Edge.TargetUnitId,
		Edge.TargetPortLabel,
		true);
	if (Source == nullptr || Target == nullptr)
	{
		return Points;
	}
	Points.Add(Source->Position);
	for (const FVector2D& BendPoint : Edge.BendPoints)
	{
		if (!Points.Last().Equals(BendPoint, 0.1f))
		{
			Points.Add(BendPoint);
		}
	}
	if (!Points.Last().Equals(Target->Position, 0.1f))
	{
		Points.Add(Target->Position);
	}
	return Points;
}

// The frozen design artifact states its palette as sRGB hex. Slate tints are
// linear, so the values must be converted rather than assigned directly, which
// is what made the first D3 render roughly twice as light as the design.
FLinearColor D3Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

namespace D3Palette
{
const TCHAR* PanelFill = TEXT("#181b1e");
const TCHAR* PanelStroke = TEXT("#31363b");
const TCHAR* Ribbon = TEXT("#66d7dc");
const TCHAR* ValueDot = TEXT("#70d9de");
const TCHAR* OperatorFill = TEXT("#123b3e");
const TCHAR* OperatorStroke = TEXT("#6edce1");
const TCHAR* SelectedStroke = TEXT("#f1c85d");
const TCHAR* CriterionFill = TEXT("#493d1d");
const TCHAR* RailStroke = TEXT("#8eb8da");
const TCHAR* RegionFill = TEXT("#162326");
const TCHAR* RegionStroke = TEXT("#62d9df");
const TCHAR* PortPill = TEXT("#2d363c");
} // namespace D3Palette

FLinearColor D3LabelColor(
	const EBlueprintLensLC3DerivationSpineLabelKind Kind)
{
	switch (Kind)
	{
	case EBlueprintLensLC3DerivationSpineLabelKind::CriterionRole:
		return D3Color(TEXT("#f0c95e"));
	case EBlueprintLensLC3DerivationSpineLabelKind::CriterionValue:
		return D3Color(TEXT("#ffe394"));
	case EBlueprintLensLC3DerivationSpineLabelKind::QualifiedPort:
		return D3Color(TEXT("#eef5f7"));
	case EBlueprintLensLC3DerivationSpineLabelKind::Region:
		return D3Color(TEXT("#68d9df"));
	case EBlueprintLensLC3DerivationSpineLabelKind::OperatorName:
		return D3Color(TEXT("#9fe7ea"));
	case EBlueprintLensLC3DerivationSpineLabelKind::OperatorGlyph:
		return D3Color(TEXT("#f4ffff"));
	case EBlueprintLensLC3DerivationSpineLabelKind::Control:
		return D3Color(TEXT("#d8e6f1"));
	default:
		return D3Color(TEXT("#e4e9ed"));
	}
}

// Slate has no circle primitive. A rounded box whose corner radius is half its
// drawn size is one, and it carries a fill and an outline in a single element,
// which is exactly the design's station.
//
// `MakeBox` does not apply the brush's own tint, so the brush keeps a white fill
// and every call site passes the real fill colour as the element tint. The
// outline colour is read from the brush, so it stays baked in here.
const FSlateBrush& D3RoundedBrush(
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
	for (const TPair<FKey, TSharedPtr<FSlateRoundedBoxBrush>>& Entry : Cache)
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
			FSlateColor(D3Color(OutlineHex)),
			OutlineWidth,
			FVector2f(Radius * 2.0f, Radius * 2.0f)));
	return *Cache.Last().Value;
}

// A dashed rail, emitted as explicit on/off runs because MakeLines draws solid.
TArray<TArray<FVector2D>> D3DashRuns(
	const TArray<FVector2D>& Points,
	const float DashLength,
	const float GapLength)
{
	TArray<TArray<FVector2D>> Runs;
	float Carried = 0.0f;
	bool bDrawing = true;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2D Start = Points[Index - 1];
		const FVector2D End = Points[Index];
		const float Length = static_cast<float>(FVector2D::Distance(Start, End));
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector2D Direction = (End - Start) / Length;
		float Cursor = 0.0f;
		while (Cursor < Length)
		{
			const float Span = bDrawing ? DashLength : GapLength;
			const float Remaining = Span - Carried;
			const float Step = FMath::Min(Remaining, Length - Cursor);
			if (bDrawing && Step > KINDA_SMALL_NUMBER)
			{
				Runs.Add({
					Start + Direction * Cursor,
					Start + Direction * (Cursor + Step)});
			}
			Cursor += Step;
			Carried += Step;
			if (Carried >= Span - KINDA_SMALL_NUMBER)
			{
				Carried = 0.0f;
				bDrawing = !bDrawing;
			}
		}
	}
	return Runs;
}
} // namespace

void SBlueprintLensLC3ValueConeCanvas::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	SelectedUnitId = InArgs._SelectedUnitId;
	Density = InArgs._Density;
	OnUnitSelected = InArgs._OnUnitSelected;
	Session = InArgs._InitialSession;
	if (!Session.IsRenderable(Projection))
	{
		Session = FBlueprintLensLC3ValueConeLayoutSession::Build(
			Projection,
			700.0f);
	}
	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth;
	RebuildDerivationSpineLayout();
	RebuildVisualChildren();
}

void SBlueprintLensLC3ValueConeCanvas::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Width = AllottedGeometry.GetLocalSize().X;
	if (Width <= 1.0f || FMath::IsNearlyEqual(Width, LastLayoutWidth, 0.5f))
	{
		PendingLayoutWidth = 0.0f;
		return;
	}
	if (!FMath::IsNearlyEqual(Width, PendingLayoutWidth, 0.5f))
	{
		PendingLayoutWidth = Width;
		PendingLayoutStartTime = InCurrentTime;
		return;
	}
	if (InCurrentTime - PendingLayoutStartTime >= 0.12)
	{
		RebuildSession(PendingLayoutWidth);
		PendingLayoutWidth = 0.0f;
	}
}

FVector2D SBlueprintLensLC3ValueConeCanvas::ComputeDesiredSize(float) const
{
	const FVector2D LayoutSize = GetSurfaceSize();
	return FVector2D(FMath::Min(LayoutSize.X, 430.0f), LayoutSize.Y);
}

TSharedRef<SWidget> SBlueprintLensLC3ValueConeCanvas::BuildSurfaceLabel(
	const FBlueprintLensLC3DerivationSpineLabel& Label)
{
	return SNew(STextBlock)
		.Text(FText::FromString(Label.Text))
		.Font(Label.Font)
		.ColorAndOpacity(D3LabelColor(Label.Kind))
		.Visibility(EVisibility::HitTestInvisible);
}

TSharedRef<SWidget> SBlueprintLensLC3ValueConeCanvas::BuildNodeCard(
	const FBlueprintLensLC3ValueConeLayoutNode& Node)
{
	const FLinearColor Accent = NodeAccent(Node.Kind);
	const FLinearColor Surface = NodeSurface(Node.Kind);
	const FString UnitId = Node.UnitId;
	const bool bControl =
		Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control;
	const bool bOperator =
		Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Operator;
	const bool bCriterion =
		Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Criterion;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(NodeRoleLabel(Node)))
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		.ColorAndOpacity(Accent)
		.Justification(
			bOperator ? ETextJustify::Center : ETextJustify::Left)
		.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Node.ReaderLabel))
		.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
		.ColorAndOpacity(
			bCriterion ? FSlateColor(Accent) : FSlateColor::UseForeground())
		.Justification(
			bOperator ? ETextJustify::Center : ETextJustify::Left)
		.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Visibility_Lambda(
				[this]()
				{
					return Density.Get() ==
						EBlueprintLensLC3ValueConeDensity::Summary
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
			.Text(FText::FromString(Node.RouteText))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Justification(
				bOperator ? ETextJustify::Center : ETextJustify::Left)
			.AutoWrapText(true)
	];
	if (!Node.InputSummaryText.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Visibility_Lambda(
					[this]()
					{
						return Density.Get() ==
							EBlueprintLensLC3ValueConeDensity::Summary
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
				.Text(FText::FromString(Node.InputSummaryText))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(Accent)
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
		];
	}
	if (bControl)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(TEXT("It supplies no value.")))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(Accent)
				.AutoWrapText(true)
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda(
			[this, UnitId, Accent, Surface]()
			{
				const bool bSelected = SelectedUnitId.Get() == UnitId;
				return FSlateColor(bSelected
					? FLinearColor(
						FMath::Lerp(Surface.R, Accent.R, 0.34f),
						FMath::Lerp(Surface.G, Accent.G, 0.34f),
						FMath::Lerp(Surface.B, Accent.B, 0.34f),
						1.0f)
					: Surface);
			})
		.Padding(FMargin(
			bCriterion ? 9.0f : bOperator ? 7.0f : 6.0f))
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(FMargin(0.0f))
				.HAlign(HAlign_Fill)
				.ToolTipText(FText::FromString(Node.RouteText))
				.OnClicked_Lambda(
					[this, UnitId]()
					{
						OnUnitSelected.ExecuteIfBound(UnitId);
						return FReply::Handled();
					})
			[
				Content
			]
		];
}

FVector2D SBlueprintLensLC3ValueConeCanvas::NodePosition(
	const FString& UnitId) const
{
	const FBlueprintLensLayoutNodePlacement* Node =
		Session.Layout.LayoutLedger.Nodes.FindByPredicate(
			[&UnitId](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId == UnitId;
			});
	return Node != nullptr ? Node->Position : FVector2D::ZeroVector;
}

FVector2D SBlueprintLensLC3ValueConeCanvas::NodeSize(
	const FString& UnitId) const
{
	const FBlueprintLensLayoutNodePlacement* Node =
		Session.Layout.LayoutLedger.Nodes.FindByPredicate(
			[&UnitId](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId == UnitId;
			});
	return Node != nullptr ? Node->Size : FVector2D(1.0f, 1.0f);
}

void SBlueprintLensLC3ValueConeCanvas::RebuildSession(const float TargetWidth)
{
	const FBlueprintLensLC3ValueConeLayoutSessionResult Candidate =
		FBlueprintLensLC3ValueConeLayoutSession::Build(
			Projection,
			TargetWidth);
	if (!Candidate.IsRenderable(Projection))
	{
		return;
	}
	Session = Candidate;
	LastLayoutWidth = TargetWidth;
	RebuildDerivationSpineLayout();
	RebuildVisualChildren();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SBlueprintLensLC3ValueConeCanvas::RebuildDerivationSpineLayout()
{
	DerivationSpineLayout =
		FBlueprintLensLC3DerivationSpineLayoutBuilder::Build(
			Projection,
			Session.Layout,
			Session.Layout.LayoutRequest.TargetWidth,
			SelectedUnitId.Get());
}

void SBlueprintLensLC3ValueConeCanvas::RebuildVisualChildren()
{
	VisualCanvas = SNew(SCanvas);
	if (DerivationSpineLayout.IsRenderable(Projection))
	{
		for (const FBlueprintLensLC3DerivationSpineLabel& Label :
			 DerivationSpineLayout.Labels)
		{
			VisualCanvas->AddSlot()
				.Position(Label.Position)
				.Size(Label.Size)
			[
				BuildSurfaceLabel(Label)
			];
		}
	}
	else
	{
		for (const FBlueprintLensLC3ValueConeLayoutNode& Node :
			 Session.Layout.Nodes)
		{
			const FString UnitId = Node.UnitId;
			VisualCanvas->AddSlot()
				.Position(TAttribute<FVector2D>::CreateLambda(
					[this, UnitId]()
					{
						return NodePosition(UnitId);
					}))
				.Size(TAttribute<FVector2D>::CreateLambda(
					[this, UnitId]()
					{
						return NodeSize(UnitId);
					}))
			[
				BuildNodeCard(Node)
			];
		}
	}
	ChildSlot[VisualCanvas.ToSharedRef()];
}

int32 SBlueprintLensLC3ValueConeCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const int32 ContentLayer = DerivationSpineLayout.IsRenderable(Projection)
		? PaintDerivationSpine(AllottedGeometry, OutDrawElements, LayerId)
		: PaintRibbonFallback(AllottedGeometry, OutDrawElements, LayerId);
	return SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		ContentLayer,
		InWidgetStyle,
		bParentEnabled);
}

int32 SBlueprintLensLC3ValueConeCanvas::PaintRibbonFallback(
	const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId) const
{
	const FBlueprintLensLayoutLedger& Ledger = Session.Layout.LayoutLedger;
	const FLinearColor RibbonShadow(0.015f, 0.035f, 0.04f, 0.94f);
	const FLinearColor RibbonColor(0.12f, 0.72f, 0.72f, 0.96f);
	const FLinearColor ControlColor(0.32f, 0.52f, 0.72f, 0.88f);
	const FBlueprintLensLayoutNodePlacement* ControlNode =
		Ledger.Nodes.FindByPredicate(
			[this](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId ==
					Projection.Control.ControllerUnitId;
			});
	if (ControlNode != nullptr)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(Ledger.CanvasSize.X, ControlNode->Size.Y + 16.0f),
				FSlateLayoutTransform(FVector2D(
					0.0f,
					ControlNode->Position.Y - 8.0f))),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor(0.025f, 0.055f, 0.085f, 0.62f));
	}

	for (const FBlueprintLensLayoutEdgePlacement& Edge : Ledger.Edges)
	{
		const bool bControl =
			Edge.Family == EBlueprintLensLayoutRelationFamily::Execution;
		const TArray<FVector2D> Points = EdgeRoute(Ledger, Edge);
		if (Points.Num() < 2)
		{
			continue;
		}
		if (bControl)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				ControlColor,
				true,
				2.0f);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				RibbonShadow,
				true,
				12.0f);
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 2,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				RibbonColor,
				true,
				8.0f);
		}

		const FVector2D End = Points.Last();
		const FVector2D Direction =
			(End - Points[Points.Num() - 2]).GetSafeNormal();
		const FVector2D Perpendicular(-Direction.Y, Direction.X);
		const float ArrowLength = bControl ? 7.0f : 10.0f;
		const float ArrowHalfWidth = bControl ? 3.5f : 5.0f;
		const TArray<FVector2D> Arrow = {
			End - Direction * ArrowLength + Perpendicular * ArrowHalfWidth,
			End,
			End - Direction * ArrowLength - Perpendicular * ArrowHalfWidth};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(),
			Arrow,
			ESlateDrawEffect::None,
			bControl ? ControlColor : RibbonColor,
			true,
			bControl ? 2.0f : 3.0f);

		for (const FVector2D& Port : {Points[0], End})
		{
			const FVector2D MarkerSize = bControl
				? FVector2D(7.0f, 3.0f)
				: FVector2D(10.0f, 5.0f);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 3,
				AllottedGeometry.ToPaintGeometry(
					MarkerSize,
					FSlateLayoutTransform(Port - MarkerSize * 0.5f)),
				FAppStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				bControl ? ControlColor : RibbonColor);
		}
	}

	const FBlueprintLensLayoutNodePlacement* CriterionNode =
		Ledger.Nodes.FindByPredicate(
			[this](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId == Projection.CriterionUnitId;
			});
	if (CriterionNode != nullptr)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(5.0f, CriterionNode->Size.Y),
				FSlateLayoutTransform(CriterionNode->Position)),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			NodeAccent(EBlueprintLensLC3ValueConeNodeKind::Criterion));
	}

	return LayerId + 4;
}

int32 SBlueprintLensLC3ValueConeCanvas::PaintDerivationSpine(
	const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId) const
{
	const int32 PanelLayer = LayerId;
	const int32 RegionLayer = LayerId + 1;
	const int32 RouteLayer = LayerId + 2;
	const int32 ShadowLayer = LayerId + 3;
	const int32 StationLayer = LayerId + 4;
	const int32 PillLayer = LayerId + 5;
	const FLinearColor RibbonColor = D3Color(D3Palette::Ribbon);
	const FLinearColor RailColor = D3Color(D3Palette::RailStroke);
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.34f);

	const auto PaintShadow = [&](const FBox2D& Bounds, const float Radius)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			ShadowLayer,
			AllottedGeometry.ToPaintGeometry(
				Bounds.GetSize(),
				FSlateLayoutTransform(Bounds.Min + FVector2D(0.0f, 5.0f))),
			&D3RoundedBrush(Radius, TEXT("#05070a"), 0.0f),
			ESlateDrawEffect::None,
			ShadowTint);
	};

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		PanelLayer,
		AllottedGeometry.ToPaintGeometry(
			DerivationSpineLayout.CanvasSize,
			FSlateLayoutTransform()),
		&D3RoundedBrush(10.0f, D3Palette::PanelStroke, 1.0f),
		ESlateDrawEffect::None,
		D3Color(D3Palette::PanelFill));

	if (DerivationSpineLayout.bHasLocalSubtree)
	{
		const FBox2D& Bounds = DerivationSpineLayout.LocalSubtreeBounds;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			RegionLayer,
			AllottedGeometry.ToPaintGeometry(
				Bounds.GetSize(),
				FSlateLayoutTransform(Bounds.Min)),
			&D3RoundedBrush(18.0f, D3Palette::RegionFill, 0.0f),
			ESlateDrawEffect::None,
			D3Color(D3Palette::RegionFill));
		const TArray<FVector2D> Border = {
			Bounds.Min,
			FVector2D(Bounds.Max.X, Bounds.Min.Y),
			Bounds.Max,
			FVector2D(Bounds.Min.X, Bounds.Max.Y),
			Bounds.Min};
		for (const TArray<FVector2D>& Run : D3DashRuns(Border, 8.0f, 6.0f))
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				RegionLayer,
				AllottedGeometry.ToPaintGeometry(),
				Run,
				ESlateDrawEffect::None,
				D3Color(D3Palette::RegionStroke),
				true,
				2.0f);
		}
	}

	for (const FBlueprintLensLC3DerivationSpineRoute& Route :
		 DerivationSpineLayout.Routes)
	{
		if (Route.Points.Num() < 2)
		{
			continue;
		}
		if (Route.Family == EBlueprintLensLayoutRelationFamily::Execution)
		{
			for (const TArray<FVector2D>& Run :
				 D3DashRuns(Route.Points, 9.0f, 7.0f))
			{
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					RouteLayer,
					AllottedGeometry.ToPaintGeometry(),
					Run,
					ESlateDrawEffect::None,
					RailColor,
					true,
					3.0f);
			}
			// The rail keeps the only arrowhead on the surface: it is the one
			// relation that has a direction the reader must not mistake for a
			// value hand-off.
			const FVector2D End = Route.Points.Last();
			const FVector2D Direction =
				(End - Route.Points[Route.Points.Num() - 2]).GetSafeNormal();
			const FVector2D Perpendicular(-Direction.Y, Direction.X);
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				RouteLayer,
				AllottedGeometry.ToPaintGeometry(),
				{
					End - Direction * 12.0f + Perpendicular * 7.0f,
					End,
					End - Direction * 12.0f - Perpendicular * 7.0f},
				ESlateDrawEffect::None,
				RailColor,
				true,
				3.0f);
			continue;
		}

		const FBlueprintLensLC3DerivationSpineElement* Source =
			DerivationSpineLayout.FindElement(Route.SourceUnitId);
		const FBlueprintLensLC3DerivationSpineElement* Target =
			DerivationSpineLayout.FindElement(Route.TargetUnitId);
		const bool bFromOperator = Source != nullptr &&
			Source->Kind ==
				EBlueprintLensLC3DerivationSpineElementKind::Operator;
		const bool bToCriterion = Target != nullptr &&
			Target->Kind ==
				EBlueprintLensLC3DerivationSpineElementKind::Criterion;
		const float Thickness = bToCriterion ? 10.0f : bFromOperator ? 9.0f : 7.0f;
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			RouteLayer,
			AllottedGeometry.ToPaintGeometry(),
			Route.Points,
			ESlateDrawEffect::None,
			RibbonColor,
			true,
			Thickness);
	}

	for (const FBlueprintLensLC3DerivationSpineElement& Element :
		 DerivationSpineLayout.Elements)
	{
		switch (Element.Kind)
		{
		case EBlueprintLensLC3DerivationSpineElementKind::Operator:
		{
			const float Radius =
				static_cast<float>(Element.Bounds.GetSize().X) * 0.5f;
			PaintShadow(Element.Bounds, Radius);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				StationLayer,
				AllottedGeometry.ToPaintGeometry(
					Element.Bounds.GetSize(),
					FSlateLayoutTransform(Element.Bounds.Min)),
				&D3RoundedBrush(
					Radius,
					Element.bSelected
						? D3Palette::SelectedStroke
						: D3Palette::OperatorStroke,
					Element.bSelected ? 3.0f : 2.0f),
				ESlateDrawEffect::None,
				D3Color(D3Palette::OperatorFill));
			break;
		}
		case EBlueprintLensLC3DerivationSpineElementKind::Criterion:
		{
			PaintShadow(Element.Bounds, 10.0f);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				StationLayer,
				AllottedGeometry.ToPaintGeometry(
					Element.Bounds.GetSize(),
					FSlateLayoutTransform(Element.Bounds.Min)),
				&D3RoundedBrush(10.0f, D3Palette::SelectedStroke, 2.0f),
				ESlateDrawEffect::None,
				D3Color(D3Palette::CriterionFill));
			break;
		}
		default:
		{
			const bool bControl = Element.Kind ==
				EBlueprintLensLC3DerivationSpineElementKind::Control;
			const FVector2D Dot(12.0f, 12.0f);
			// The layout records each origin's real route start in its output
			// anchor, so the dot lands on its ribbon in either grammar.
			const FVector2D Anchor =
				bControl ? Element.InputAnchor : Element.OutputAnchor;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				StationLayer,
				AllottedGeometry.ToPaintGeometry(
					Dot,
					FSlateLayoutTransform(Anchor - Dot * 0.5f)),
				&D3RoundedBrush(
					6.0f,
					bControl ? D3Palette::RailStroke : D3Palette::ValueDot,
					0.0f),
				ESlateDrawEffect::None,
				D3Color(
					bControl ? D3Palette::RailStroke : D3Palette::ValueDot));
			break;
		}
		}
	}

	// Qualified endpoints ride on their own pill so they read as port identity
	// rather than as free text sitting on a ribbon.
	for (const FBlueprintLensLC3DerivationSpineLabel& Label :
		 DerivationSpineLayout.Labels)
	{
		if (Label.Kind !=
			EBlueprintLensLC3DerivationSpineLabelKind::QualifiedPort)
		{
			continue;
		}
		const FVector2D Padding(9.0f, 3.0f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			PillLayer,
			AllottedGeometry.ToPaintGeometry(
				Label.Size + Padding * 2.0f,
				FSlateLayoutTransform(Label.Position - Padding)),
			&D3RoundedBrush(10.0f, D3Palette::PortPill, 0.0f),
			ESlateDrawEffect::None,
			D3Color(D3Palette::PortPill));
	}
	return PillLayer + 1;
}

FReply SBlueprintLensLC3ValueConeCanvas::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!DerivationSpineLayout.IsRenderable(Projection) ||
		MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}
	const FVector2D Position =
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	for (const FBlueprintLensLC3DerivationSpineElement& Element :
		 DerivationSpineLayout.Elements)
	{
		if (Position.X >= Element.Bounds.Min.X &&
			Position.X <= Element.Bounds.Max.X &&
			Position.Y >= Element.Bounds.Min.Y &&
			Position.Y <= Element.Bounds.Max.Y)
		{
			OnUnitSelected.ExecuteIfBound(Element.UnitId);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}
