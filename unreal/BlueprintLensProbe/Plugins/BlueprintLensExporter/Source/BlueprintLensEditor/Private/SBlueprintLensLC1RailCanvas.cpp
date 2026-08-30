#include "SBlueprintLensLC1RailCanvas.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FLinearColor LC1Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

namespace LC1Palette
{
const TCHAR* PanelFill = TEXT("#181b1e");
const TCHAR* PanelStroke = TEXT("#31363b");
const TCHAR* Spine = TEXT("#8daebd");
const TCHAR* Station = TEXT("#9eb5c0");
const TCHAR* Criterion = TEXT("#efc33f");
const TCHAR* CriterionFill = TEXT("#493d1d");
const TCHAR* Selected = TEXT("#7fd4ff");
const TCHAR* GuardOuter = TEXT("#efc33f");
const TCHAR* GuardInner = TEXT("#62d9df");
const TCHAR* GuardFill = TEXT("#20282b");
const TCHAR* Fold = TEXT("#66747d");
const TCHAR* FoldFill = TEXT("#20272b");
const TCHAR* BoundaryCap = TEXT("#d17b75");
const TCHAR* BoundaryCapFill = TEXT("#2d2023");
const TCHAR* ScaleRule = TEXT("#d8b44e");
const TCHAR* ScaleRuleFill = TEXT("#1b2126");
const TCHAR* Text = TEXT("#eef5f7");
const TCHAR* Muted = TEXT("#aab6bd");
} // namespace LC1Palette

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
			FSlateColor(LC1Color(OutlineHex)),
			OutlineWidth,
			FVector2f(Radius * 2.0f, Radius * 2.0f)));
	return *Cache.Last().Value;
}

FVector2D BoxSize(const FBox2D& Box)
{
	return Box.Max - Box.Min;
}

void PaintDashedSegment(
	FSlateWindowElementList& OutDrawElements,
	const FGeometry& AllottedGeometry,
	const int32 LayerId,
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
	constexpr float DashLength = 6.0f;
	constexpr float GapLength = 5.0f;
	for (float Cursor = 0.0f; Cursor < Length; Cursor += DashLength + GapLength)
	{
		const float DashEnd = FMath::Min(Cursor + DashLength, Length);
		const TArray<FVector2D> Dash = {
			Start + Direction * Cursor,
			Start + Direction * DashEnd};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Dash,
			ESlateDrawEffect::None,
			Color,
			true,
			Thickness);
	}
}
} // namespace

void SBlueprintLensLC1RailCanvas::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	Explanation = InArgs._Explanation;
	CompositeSlots = InArgs._CompositeSlots;
	CurrentRadius = InArgs._CurrentRadius;
	bDataAnswer = InArgs._DataAnswer;
	ExpandedStationAppearance = InArgs._ExpandedStationAppearance;
	ExpandedStationAppearanceUnitId =
		InArgs._ExpandedStationAppearanceUnitId;
	ExpandedBetweenDecoration = InArgs._ExpandedBetweenDecoration;
	ExpandedBetweenDecorationRelationId =
		InArgs._ExpandedBetweenDecorationRelationId;
	ExpandedSpanAttachment = InArgs._ExpandedSpanAttachment;
	ExpandedSpanAttachmentId = InArgs._ExpandedSpanAttachmentId;
	ExpandedTerminalAttachment = InArgs._ExpandedTerminalAttachment;
	ExpandedTerminalAttachmentUnitId =
		InArgs._ExpandedTerminalAttachmentUnitId;
	SelectedUnitId = InArgs._SelectedUnitId;
	OnUnitSelected = InArgs._OnUnitSelected;
	OnDisclosureToggled = InArgs._OnDisclosureToggled;
	OnAttachmentDisclosureToggled =
		InArgs._OnAttachmentDisclosureToggled;
	OnBetweenDisclosureToggled = InArgs._OnBetweenDisclosureToggled;
	OnSpanDisclosureToggled = InArgs._OnSpanDisclosureToggled;
	OnFoldToggled = InArgs._OnFoldToggled;

	if (!Session.IsRenderable(Projection) && Explanation.IsValid())
	{
		Session = FBlueprintLensLC1RailLayoutSession::Build(
			Projection,
			*Explanation,
			700.0f);
	}

	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth;
	if (LastLayoutWidth <= 1.0f)
	{
		LastLayoutWidth = 700.0f;
	}
	RebuildSurface();
	RebuildVisualChildren();
}

void SBlueprintLensLC1RailCanvas::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Width = FMath::Max(AllottedGeometry.GetLocalSize().X, 430.0f);
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

FVector2D SBlueprintLensLC1RailCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(
		FMath::Min(Surface.CanvasSize.X, 430.0f),
		Surface.CanvasSize.Y);
}

void SBlueprintLensLC1RailCanvas::RebuildSession(const float TargetWidth)
{
	if (Explanation.IsValid())
	{
		const FBlueprintLensLC1RailLayoutSessionResult Candidate =
			FBlueprintLensLC1RailLayoutSession::Build(
				Projection,
				*Explanation,
				TargetWidth);
		if (Candidate.IsRenderable(Projection))
		{
			Session = Candidate;
		}
	}

	LastLayoutWidth = TargetWidth;
	RebuildSurface();
	RebuildVisualChildren();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SBlueprintLensLC1RailCanvas::RebuildSurface()
{
	Surface = FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
		Projection,
		Session,
		CompositeSlots,
		LastLayoutWidth,
		CurrentRadius,
		FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
		bDataAnswer);
}

TSharedRef<SWidget> SBlueprintLensLC1RailCanvas::BuildSurfaceLabel(
	const FBlueprintLensLC1RailSurfaceLabel& Label)
{
	const bool bCriterion = Label.Key == TEXT("criterion");
	const bool bFoldBoundary = Label.Key.StartsWith(TEXT("fold-boundary"));
	const bool bStage = Label.Key == TEXT("stage");
	const bool bRelationAnnotation = Label.Key == TEXT("relation-annotation");
	const bool bCriterionCaption = Label.Key == TEXT("criterion-caption");
	const bool bScaleRule = Label.Key == TEXT("scale-rule");
	const bool bSelectionExplanation = Label.Key == TEXT("selection-explanation");
	const bool bBoundaryCap = Label.Key.StartsWith(TEXT("boundary-cap-"));
	const bool bGuardMarker = Label.Key.StartsWith(TEXT("guard-marker:"));
	const bool bGuardDisclosure =
		Label.Key.StartsWith(TEXT("guard-disclosure:"));
	const bool bGuardDetail = Label.Key.StartsWith(TEXT("guard-detail:"));
	const bool bLC3Attachment =
		Label.Key.StartsWith(TEXT("attachment:LC3:")) ||
		Label.Key.StartsWith(TEXT("attachment-detail:LC3:")) ||
		Label.Key.StartsWith(TEXT("empty:LC3:")) ||
		Label.Key.StartsWith(TEXT("bound:LC3:"));
	const bool bF12Attachment =
		Label.Key.StartsWith(TEXT("attachment:F12:")) ||
		Label.Key.StartsWith(TEXT("attachment-detail:F12:"));
	const bool bLC6Attachment =
		Label.Key.StartsWith(TEXT("attachment:LC6:"));
	const bool bLC5Attachment =
		Label.Key.StartsWith(TEXT("attachment:LC5:"));
	const bool bLC5Refusal =
		Label.Key.StartsWith(TEXT("refusal:LC5:"));
	const bool bAttachmentAction =
		Label.Key.StartsWith(TEXT("attachment:LC3:")) ||
		(!bDataAnswer && Label.Key.StartsWith(TEXT("attachment:F12:"))) ||
		bLC5Attachment || bLC6Attachment;
	const bool bF12NoValueSource =
		bDataAnswer && Label.Key.StartsWith(TEXT("empty:LC3:"));
	const bool bF12ValueSourceBound =
		bDataAnswer && Label.Key.StartsWith(TEXT("bound:LC3:"));
	const bool bF12ValueSourceDetail = bDataAnswer &&
		Label.Key.StartsWith(TEXT("attachment-detail:LC3:f12-value"));
	const bool bIncomparableOrderBoundary =
		Label.Key.StartsWith(TEXT("order-boundary:incomparable:"));
	const bool bSccOrderBoundary =
		Label.Key.StartsWith(TEXT("order-boundary:scc:"));
	const bool bLC4SequenceBetweenAction =
		Label.Key.StartsWith(TEXT("between-decoration:LC4-SEQ:"));
	const bool bLC7SpanAction =
		Label.Key.StartsWith(TEXT("span-attachment:LC7:"));
	const FString LabelUnitId = Label.UnitId;
	TSharedRef<STextBlock> Text = SNew(STextBlock)
		.Text(FText::FromString(Label.Text))
		.Font(FAppStyle::Get().GetFontStyle(
			bCriterion || bCriterionCaption || bF12Attachment ||
				bLC5Attachment || bLC5Refusal
				? "NormalFontBold"
				: "SmallFont"))
		.ColorAndOpacity_Lambda(
			[
				this,
				LabelUnitId,
				bCriterion,
				bFoldBoundary,
				bStage,
				bRelationAnnotation,
				bCriterionCaption,
				bScaleRule,
				bSelectionExplanation,
				bBoundaryCap,
				bGuardMarker,
				bIncomparableOrderBoundary,
				bSccOrderBoundary,
				bGuardDetail]()
			{
				// Selection is explanation focus, so it must be visible on the
				// rail itself and not only in the action below it.
				if (!LabelUnitId.IsEmpty() &&
					LabelUnitId == SelectedUnitId.Get())
				{
					return FSlateColor(LC1Color(LC1Palette::Selected));
				}
				if (bStage)
				{
					return FSlateColor(LC1Color(TEXT("#92dce1")));
				}
				if (bRelationAnnotation)
				{
					return FSlateColor(LC1Color(LC1Palette::Muted));
				}
				if (bScaleRule)
				{
					return FSlateColor(LC1Color(LC1Palette::ScaleRule));
				}
				if (bSelectionExplanation)
				{
					return FSlateColor(LC1Color(LC1Palette::Muted));
				}
				if (bBoundaryCap)
				{
					return FSlateColor(LC1Color(LC1Palette::BoundaryCap));
				}
				if (bIncomparableOrderBoundary || bSccOrderBoundary)
				{
					return FSlateColor(LC1Color(
						bIncomparableOrderBoundary
							? TEXT("#92dce1")
							: LC1Palette::BoundaryCap));
				}
				if (bGuardMarker || bGuardDetail)
				{
					const FBlueprintLensLC1RailStation* GuardStation =
						Surface.Stations.FindByPredicate(
							[&LabelUnitId](const FBlueprintLensLC1RailStation& Station)
							{
								return Station.UnitId == LabelUnitId;
							});
					return FSlateColor(LC1Color(
						GuardStation != nullptr &&
							GuardStation->GuardNestingDepth > 0
							? LC1Palette::GuardInner
							: LC1Palette::GuardOuter));
				}
				return FSlateColor(
					bCriterion || bCriterionCaption
						? LC1Color(LC1Palette::Criterion)
						: bFoldBoundary
							? LC1Color(LC1Palette::Muted)
							: LC1Color(LC1Palette::Text));
			})
		.AutoWrapText(true)
		.Visibility(EVisibility::HitTestInvisible);
	if (bGuardDisclosure)
	{
		Text->SetTag(FName(
			TEXT("BlueprintLens.Automation.CompositeGuardDisclosure")));
	}
	if (bLC3Attachment)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC3AttachmentDisclosure")));
	}
	if (bF12Attachment)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12InclusionReason")));
	}
	if (bLC6Attachment)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC6TerminalDisclosure")));
	}
	if (bLC5Attachment)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC5TerminalDisclosure")));
	}
	if (bLC5Refusal)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC5RefusalDisclosure")));
	}
	if (bDataAnswer && bScaleRule)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12ScaleSummary")));
	}
	if (bDataAnswer && bStage)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12OrderSummary")));
	}
	if (bF12NoValueSource)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12NoValueSource")));
	}
	if (bF12ValueSourceBound)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12ValueSourceBound")));
	}
	if (bF12ValueSourceDetail)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.F12ValueSourceDetail")));
	}
	if (bFoldBoundary &&
		Surface.Radius.FoldedAttachmentStationCount > 0)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeAttachmentFoldDisclosure")));
	}
	if (bIncomparableOrderBoundary)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeOrderBoundary.Incomparable")));
	}
	else if (bSccOrderBoundary)
	{
		Text->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeOrderBoundary.SCC")));
	}
	if (bGuardMarker)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0.0f))
			.ToolTipText(FText::FromString(
				TEXT("Open or close this station's LC2 Guard Gate detail")))
			.OnClicked_Lambda(
				[this, LabelUnitId]()
				{
					OnDisclosureToggled.ExecuteIfBound(LabelUnitId);
					return FReply::Handled();
				})
			[
				Text
			];
	}
	if (bLC4SequenceBetweenAction)
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(5.0f, 2.0f))
			.ToolTipText(FText::FromString(
				TEXT("Open or close this relation's LC4-SEQ Sequence disclosure")))
			.OnClicked_Lambda(
				[this, LabelUnitId]()
				{
					OnBetweenDisclosureToggled.ExecuteIfBound(LabelUnitId);
					return FReply::Handled();
				})
			[
				Text
			];
		Button->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC4SequenceDisclosureAction")));
		return Button;
	}
	if (bLC7SpanAction)
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(5.0f, 2.0f))
			.ToolTipText(FText::FromString(
				TEXT("Open or close this SCC's LC7 static-slice backbone")))
			.OnPressed_Lambda(
				[this, LabelUnitId]()
				{
					OnSpanDisclosureToggled.ExecuteIfBound(LabelUnitId);
				})
		[
			Text
		];
		Button->SetTag(FName(TEXT(
			"BlueprintLens.Automation.CompositeLC7SpanDisclosureAction")));
		return Button;
	}
	if (bAttachmentAction)
	{
		const FString AttachmentGrammarId = bLC6Attachment
			? TEXT("LC6")
			: bLC5Attachment
				? TEXT("LC5")
				: bF12Attachment
					? TEXT("F12")
					: TEXT("LC3");
		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(5.0f, 1.0f))
			.ToolTipText(FText::FromString(
				bLC6Attachment
					? TEXT("Open or close this boundary ledger's LC6 truth-owner tracks")
					: bLC5Attachment
					? TEXT("Open or close this call boundary's exported LC5 callee body")
					: bF12Attachment
					? TEXT("Open or close this station's Data answer detail")
					: bDataAnswer
						? TEXT("Open or close this write's assigned Data value sources")
						: TEXT("Open or close this station's LC3 value provenance")))
			.OnClicked_Lambda(
				[this, LabelUnitId, AttachmentGrammarId]()
				{
					OnAttachmentDisclosureToggled.ExecuteIfBound(
						LabelUnitId,
						AttachmentGrammarId);
					return FReply::Handled();
				})
			[
				Text
			];
		Button->SetTag(FName(
			bLC6Attachment
				? TEXT("BlueprintLens.Automation.CompositeLC6TerminalDisclosureAction")
				: bLC5Attachment
					? TEXT("BlueprintLens.Automation.CompositeLC5TerminalDisclosureAction")
				: bF12Attachment
					? TEXT("BlueprintLens.Automation.F12DataAnswerDisclosureAction")
					: TEXT("BlueprintLens.Automation.CompositeLC3AttachmentAction")));
		return Button;
	}
	if (bFoldBoundary)
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0.0f))
			.ToolTipText(FText::FromString(
				TEXT("Expand the folded rail stations")))
			.OnClicked_Lambda(
				[this]()
				{
					OnFoldToggled.ExecuteIfBound();
					return FReply::Handled();
				})
			[
				Text
			];
		if (Surface.Radius.FoldedAttachmentStationCount > 0)
		{
			Button->SetTag(FName(TEXT(
				"BlueprintLens.Automation.CompositeAttachmentFoldAction")));
		}
		return Button;
	}
	return Text;
}

void SBlueprintLensLC1RailCanvas::RebuildVisualChildren()
{
	VisualCanvas = SNew(SCanvas);
	for (const FBlueprintLensLC1RailSurfaceLabel& Label : Surface.Labels)
	{
		VisualCanvas->AddSlot()
			.Position(Label.MeasuredBounds.Min)
			.Size(BoxSize(Label.MeasuredBounds))
		[
			BuildSurfaceLabel(Label)
		];
	}
	if (ExpandedStationAppearance.IsValid() &&
		!ExpandedStationAppearanceUnitId.IsEmpty())
	{
		const FBlueprintLensLC1RailStation* Host =
			Surface.Stations.FindByPredicate(
				[this](const FBlueprintLensLC1RailStation& Station)
				{
					return Station.UnitId ==
						ExpandedStationAppearanceUnitId;
				});
		if (Host != nullptr && Host->ExpandedAppearanceBounds.bIsValid)
		{
			VisualCanvas->AddSlot()
				.Position(Host->ExpandedAppearanceBounds.Min)
				.Size(BoxSize(Host->ExpandedAppearanceBounds))
			[
				ExpandedStationAppearance.ToSharedRef()
			];
		}
	}
	if (ExpandedBetweenDecoration.IsValid() &&
		!ExpandedBetweenDecorationRelationId.IsEmpty())
	{
		const FBlueprintLensLC1RailBetweenDecoration* Host =
			Surface.BetweenDecorations.FindByPredicate(
				[this](const FBlueprintLensLC1RailBetweenDecoration& Decoration)
				{
					return Decoration.RelationId ==
						ExpandedBetweenDecorationRelationId;
				});
		if (Host != nullptr && Host->ExpandedContentBounds.bIsValid)
		{
			VisualCanvas->AddSlot()
				.Position(Host->ExpandedContentBounds.Min)
				.Size(BoxSize(Host->ExpandedContentBounds))
			[
				ExpandedBetweenDecoration.ToSharedRef()
			];
		}
	}
	if (ExpandedSpanAttachment.IsValid() &&
		!ExpandedSpanAttachmentId.IsEmpty())
	{
		const FBlueprintLensLC1RailSpanDecoration* Host =
			Surface.SpanDecorations.FindByPredicate(
				[this](const FBlueprintLensLC1RailSpanDecoration& Decoration)
				{
					return Decoration.SpanId == ExpandedSpanAttachmentId;
				});
		if (Host != nullptr && Host->ExpandedContentBounds.bIsValid)
		{
			VisualCanvas->AddSlot()
				.Position(Host->ExpandedContentBounds.Min)
				.Size(BoxSize(Host->ExpandedContentBounds))
			[
				ExpandedSpanAttachment.ToSharedRef()
			];
		}
	}
	if (ExpandedTerminalAttachment.IsValid() &&
		!ExpandedTerminalAttachmentUnitId.IsEmpty())
	{
		const FBox2D* Host =
			Surface.ExpandedTerminalAttachmentBounds.Find(
				ExpandedTerminalAttachmentUnitId);
		if (Host != nullptr && Host->bIsValid)
		{
			VisualCanvas->AddSlot()
				.Position(Host->Min)
				.Size(BoxSize(*Host))
			[
				ExpandedTerminalAttachment.ToSharedRef()
			];
		}
	}
	ChildSlot[VisualCanvas.ToSharedRef()];
}

int32 SBlueprintLensLC1RailCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(
			Surface.CanvasSize,
			FSlateLayoutTransform()),
		&RoundedBrush(6.0f, LC1Palette::PanelStroke, 1.0f),
		ESlateDrawEffect::None,
		LC1Color(LC1Palette::PanelFill));

	if (Surface.ScaleRuleBounds.bIsValid)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(Surface.ScaleRuleBounds),
				FSlateLayoutTransform(Surface.ScaleRuleBounds.Min)),
			&RoundedBrush(8.0f, LC1Palette::ScaleRule, 1.0f),
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::ScaleRuleFill));
	}

	if (Surface.Radius.FoldBoundaryBounds.bIsValid)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(Surface.Radius.FoldBoundaryBounds),
				FSlateLayoutTransform(
					Surface.Radius.FoldBoundaryBounds.Min)),
			&RoundedBrush(8.0f, LC1Palette::Fold, 1.5f),
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::FoldFill));

		if (Surface.Stations.Num() > 0)
		{
			const FVector2D Start(
				Surface.Stations[0].Position.X,
				Surface.Radius.FoldBoundaryBounds.Max.Y);
			PaintDashedSegment(
				OutDrawElements,
				AllottedGeometry,
				LayerId + 2,
				Start,
				Surface.Stations[0].Position,
				LC1Color(LC1Palette::Fold),
				2.0f);
		}
	}

	for (const FBox2D& CapBounds : Surface.BoundaryCapBounds)
	{
		if (!CapBounds.bIsValid)
		{
			continue;
		}
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(CapBounds),
				FSlateLayoutTransform(CapBounds.Min)),
			&RoundedBrush(8.0f, LC1Palette::BoundaryCap, 1.5f),
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::BoundaryCapFill));
	}

	if (Surface.SpineRoute.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			Surface.SpineRoute,
			ESlateDrawEffect::None,
			FLinearColor(0.02f, 0.025f, 0.025f, 0.95f),
			true,
			7.0f);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(),
			Surface.SpineRoute,
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::Spine),
			true,
			3.5f);
	}

	if (Surface.CriterionDockBounds.bIsValid)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(Surface.CriterionDockBounds),
				FSlateLayoutTransform(Surface.CriterionDockBounds.Min)),
			&RoundedBrush(7.0f, LC1Palette::Criterion, 1.5f),
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::CriterionFill));
	}

	for (const FBlueprintLensLC1RailStation& Station : Surface.Stations)
	{
		if (!Station.bIsGuard || !Station.GuardAppearanceBounds.bIsValid)
		{
			continue;
		}
		const TCHAR* GuardColor = Station.GuardNestingDepth > 0
			? LC1Palette::GuardInner
			: LC1Palette::GuardOuter;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(Station.GuardAppearanceBounds),
				FSlateLayoutTransform(Station.GuardAppearanceBounds.Min)),
			&RoundedBrush(7.0f, GuardColor, 1.5f),
			ESlateDrawEffect::None,
			LC1Color(LC1Palette::GuardFill));
	}

	const FString Selection = SelectedUnitId.Get();
	for (const FBlueprintLensLC1RailStation& Station : Surface.Stations)
	{
		const bool bSelected =
			!Selection.IsEmpty() && Station.UnitId == Selection;
		const FLinearColor StationColor = bSelected
			? LC1Color(LC1Palette::Selected)
			: Station.bIsCriterion
				? LC1Color(LC1Palette::Criterion)
				: Station.bIsGuard
					? LC1Color(
						Station.GuardNestingDepth > 0
							? LC1Palette::GuardInner
							: LC1Palette::GuardOuter)
					: LC1Color(LC1Palette::Station);
		const FVector2D MarkSize = bSelected
			? FVector2D(18.0f, 18.0f)
			: FVector2D(12.0f, 12.0f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(
				MarkSize,
				FSlateLayoutTransform(
					Station.Position - MarkSize * 0.5f)),
			&RoundedBrush(
				MarkSize.X * 0.5f,
				bSelected ? LC1Palette::Selected : LC1Palette::PanelStroke,
				bSelected ? 2.5f : 1.0f),
			ESlateDrawEffect::None,
			StationColor);
	}

	return SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId + 5,
		InWidgetStyle,
		bParentEnabled);
}

FReply SBlueprintLensLC1RailCanvas::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(
		MouseEvent.GetScreenSpacePosition());
	for (const FBlueprintLensLC1RailStation& Station : Surface.Stations)
	{
		if (Station.bIsGuard &&
			Station.GuardAppearanceBounds.IsInside(LocalPosition))
		{
			OnDisclosureToggled.ExecuteIfBound(Station.UnitId);
			return FReply::Handled();
		}
	}
	const FString UnitId = ResolveUnitAtLocalPositionForTesting(LocalPosition);
	if (!UnitId.IsEmpty())
	{
		OnUnitSelected.ExecuteIfBound(UnitId);
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FString SBlueprintLensLC1RailCanvas::ResolveUnitAtLocalPositionForTesting(
	const FVector2D& LocalPosition) const
{
	for (const FBlueprintLensLC1RailSurfaceLabel& Label : Surface.Labels)
	{
		if (!Label.UnitId.IsEmpty() &&
			Label.MeasuredBounds.IsInside(LocalPosition))
		{
			return Label.UnitId;
		}
	}

	for (int32 Index = Surface.Stations.Num() - 1; Index >= 0; --Index)
	{
		const FBlueprintLensLC1RailStation& Station = Surface.Stations[Index];
		if (Station.HitRegion.IsInside(LocalPosition))
		{
			return Station.UnitId;
		}
	}

	if (Surface.CriterionDockBounds.IsInside(LocalPosition))
	{
		return Projection.CriterionUnitId;
	}
	return FString();
}
