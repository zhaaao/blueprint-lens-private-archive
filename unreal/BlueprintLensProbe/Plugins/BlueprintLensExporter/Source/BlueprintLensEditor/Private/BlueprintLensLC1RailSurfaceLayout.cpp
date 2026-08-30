#include "BlueprintLensLC1RailSurfaceLayout.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"

namespace
{
bool StrictlyIntersects(const FBox2D& A, const FBox2D& B)
{
	return A.Min.X < B.Max.X && A.Max.X > B.Min.X &&
		A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
}

bool SegmentIntersects(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
{
	if (!Box.bIsValid) return false;
	const float MinX = FMath::Min(A.X, B.X);
	const float MaxX = FMath::Max(A.X, B.X);
	const float MinY = FMath::Min(A.Y, B.Y);
	const float MaxY = FMath::Max(A.Y, B.Y);
	return MinX < Box.Max.X && MaxX > Box.Min.X &&
		MinY < Box.Max.Y && MaxY > Box.Min.Y;
}

FVector2D MeasureUnitLabelText(
	const FString& Text,
	const bool bCriterion)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC1 rail surface layout requires Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Font = FAppStyle::Get().GetFontStyle(
		bCriterion ? "NormalFontBold" : "SmallFont");
	return FontMeasure->Measure(Text, Font);
}

float MeasureWrappedSmallLabelHeight(
	const FString& Text,
	const float AvailableWidth)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC1 rail surface layout requires Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Font = FAppStyle::Get().GetFontStyle("SmallFont");
	const float SafeWidth = FMath::Max(AvailableWidth, 1.0f);
	const float SpaceWidth = FontMeasure->Measure(TEXT(" "), Font).X;
	const float LineHeight = FontMeasure->Measure(TEXT("Ag"), Font).Y;

	TArray<FString> ExplicitLines;
	Text.ParseIntoArray(ExplicitLines, TEXT("\n"), false);
	int32 WrappedLineCount = 0;
	for (const FString& ExplicitLine : ExplicitLines)
	{
		TArray<FString> Words;
		ExplicitLine.ParseIntoArray(Words, TEXT(" "), true);
		if (Words.IsEmpty())
		{
			++WrappedLineCount;
			continue;
		}

		++WrappedLineCount;
		float CurrentLineWidth = 0.0f;
		for (const FString& Word : Words)
		{
			const float WordWidth = FontMeasure->Measure(Word, Font).X;
			if (CurrentLineWidth > 0.0f &&
				CurrentLineWidth + SpaceWidth + WordWidth > SafeWidth)
			{
				++WrappedLineCount;
				CurrentLineWidth = 0.0f;
			}
			if (WordWidth > SafeWidth)
			{
				// Retain the final fragment width: the next word can only share that
				// line when it really fits, so this cannot undercount Slate wrapping.
				const int32 FragmentCount = FMath::CeilToInt(
					WordWidth / SafeWidth);
				WrappedLineCount += FragmentCount - 1;
				CurrentLineWidth = WordWidth -
					SafeWidth * static_cast<float>(FragmentCount - 1);
			}
			else
			{
				CurrentLineWidth +=
					(CurrentLineWidth > 0.0f ? SpaceWidth : 0.0f) + WordWidth;
			}
		}
	}

	// The extra point per line absorbs the text layout's line spacing without
	// forcing the cap-aware disclosure into a fixed-width, fixed-height box.
	return FMath::Max(
		LineHeight,
		static_cast<float>(FMath::Max(WrappedLineCount, 1)) *
			(LineHeight + 1.0f));
}

void AddLabel(FBlueprintLensLC1RailSurfaceLayout& Result, const FString& Key,
	const FString& UnitId, const FString& Text, const FVector2D& Position,
	const FVector2D& Size)
{
	FBlueprintLensLC1RailSurfaceLabel Label;
	Label.Key = Key;
	Label.UnitId = UnitId;
	Label.Text = Text;
	Label.MeasuredBounds = FBox2D(Position, Position + Size);
	Result.Labels.Add(MoveTemp(Label));
}

const TCHAR* SelectionExplanationText()
{
	return TEXT("Selecting any unit opens its Blueprint source at 1:1.\n"
		"Selection is focus, never a runtime claim.");
}

FString DataValueSourceMarkerKey(
	const FBlueprintLensCompositeAttachment& Attachment)
{
	if (Attachment.AttachmentId.StartsWith(TEXT("f12-value-empty:")))
	{
		return TEXT("empty:LC3:") + Attachment.AttachmentId;
	}
	if (Attachment.AttachmentId.StartsWith(TEXT("f12-value-bounded:")))
	{
		return TEXT("bound:LC3:") + Attachment.AttachmentId;
	}
	return TEXT("attachment:LC3:") + Attachment.AttachmentId;
}
}

int32 FBlueprintLensLC1RailSurfaceLayout::DrawnUnitCount() const
{
	return Stations.Num();
}

bool FBlueprintLensLC1RailSurfaceLayout::HasNoLabelIntersections() const
{
	for (int32 A = 0; A < Labels.Num(); ++A)
	{
		for (int32 B = A + 1; B < Labels.Num(); ++B)
		{
			if (StrictlyIntersects(Labels[A].MeasuredBounds, Labels[B].MeasuredBounds)) return false;
		}
	}
	return true;
}

bool FBlueprintLensLC1RailSurfaceLayout::HasNoLabelRouteIntersections() const
{
	for (int32 Index = 1; Index < SpineRoute.Num(); ++Index)
	{
		for (const auto& Label : Labels)
		{
			if (SegmentIntersects(SpineRoute[Index - 1], SpineRoute[Index], Label.MeasuredBounds)) return false;
		}
	}
	return true;
}

FString FBlueprintLensLC1RailSurfaceLayout::InvariantDiagnostic(
	const FBlueprintLensLC1RailProjection& Projection) const
{
	if (!Projection.IsRenderable()) return TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED");
	TSet<FString> Canonical;
	for (const FString& Id : CanonicalUnitIds)
	{
		if (Id.IsEmpty() || Canonical.Contains(Id)) return TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED");
		Canonical.Add(Id);
	}
	TArray<FString> ProvenOrder;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		 Projection.OrderedCanonicalUnits)
	{
		ProvenOrder.Add(Unit.UnitId);
	}
	if (CanonicalUnitIds != ProvenOrder)
	{
		return TEXT("LC1_RAIL_ORDER_NOT_PROVEN");
	}
	const FBlueprintLensLC1RailSurfaceLabel* ScaleRuleLabel =
		Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("scale-rule");
			});
	const FBlueprintLensLC1RailSurfaceLabel* SelectionExplanationLabel =
		Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("selection-explanation");
			});
	const FBlueprintLensLC1RailSurfaceLabel* StageLabel =
		Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("stage");
			});
	if (ScaleRuleLabel == nullptr || !ScaleRuleLabel->MeasuredBounds.bIsValid ||
		!ScaleRuleBounds.bIsValid ||
		StageLabel == nullptr ||
		ScaleRuleBounds.Max.Y > StageLabel->MeasuredBounds.Min.Y)
	{
		return TEXT("LC1_RAIL_SCALE_RULE_NOT_DRAWN");
	}
	for (const FBlueprintLensLC1RailOrderRegion& Region :
		 Projection.OrderRegions)
	{
		const bool bAnyMemberDrawn = Region.MemberUnitIds.ContainsByPredicate(
			[this](const FString& UnitId)
			{
				return Radius.DrawnUnitIds.Contains(UnitId);
			});
		if (!bAnyMemberDrawn)
		{
			continue;
		}
		const FString KeyPrefix = Region.Kind ==
				EBlueprintLensLC1RailOrderRegionKind::Incomparable
			? TEXT("order-boundary:incomparable:")
			: TEXT("order-boundary:scc:");
		const FString BoundaryKey = KeyPrefix + Region.RegionId;
		const FBlueprintLensLC1RailSurfaceLabel* BoundaryLabel =
			Labels.FindByPredicate(
				[&BoundaryKey](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == BoundaryKey;
				});
		if (BoundaryLabel == nullptr ||
			BoundaryLabel->Text != Region.ReaderText ||
			!BoundaryLabel->MeasuredBounds.bIsValid)
		{
			return TEXT("LC1_RAIL_ORDER_BOUNDARY_NOT_DRAWN");
		}
	}
	if (BoundaryCapBounds.Num() != Projection.BoundaryCaps.Num())
	{
		return TEXT("LC1_RAIL_BOUNDARY_CAP_NOT_DRAWN");
	}
	for (int32 Index = 0; Index < Projection.BoundaryCaps.Num(); ++Index)
	{
		const FBlueprintLensLC1RailBoundaryCap& Cap = Projection.BoundaryCaps[Index];
		const FString TitleKey = FString::Printf(
			TEXT("boundary-cap-title:%s"), *Cap.UnitId);
		const FString DisclosureKey = FString::Printf(
			TEXT("boundary-cap-disclosure:%s"), *Cap.UnitId);
		const FBlueprintLensLC1RailSurfaceLabel* TitleLabel =
			Labels.FindByPredicate([&TitleKey](const auto& Label)
			{
				return Label.Key == TitleKey;
			});
		const FBlueprintLensLC1RailSurfaceLabel* DisclosureLabel =
			Labels.FindByPredicate([&DisclosureKey](const auto& Label)
			{
				return Label.Key == DisclosureKey;
			});
		if (!BoundaryCapBounds[Index].bIsValid || TitleLabel == nullptr ||
			DisclosureLabel == nullptr || TitleLabel->Text != Cap.Title ||
			DisclosureLabel->Text != Cap.Disclosure)
		{
			return TEXT("LC1_RAIL_BOUNDARY_CAP_NOT_DRAWN");
		}
	}
	if (SelectionExplanationLabel == nullptr ||
		!SelectionExplanationLabel->MeasuredBounds.bIsValid ||
		!SelectionExplanationBounds.bIsValid ||
		!SelectionExplanationLabel->Text.Contains(
			TEXT("Selecting any unit opens its Blueprint source at 1:1.")) ||
		!SelectionExplanationLabel->Text.Contains(
			TEXT("Selection is focus, never a runtime claim.")))
	{
		return TEXT("LC1_RAIL_SELECTION_EXPLANATION_MISSING");
	}
	if (!Radius.DrawnUnitIds.Contains(Projection.CriterionUnitId) ||
		!Stations.ContainsByPredicate([&Projection](const auto& Station){ return Station.UnitId == Projection.CriterionUnitId && Station.bIsCriterion; }) ||
		!CriterionDockBounds.bIsValid)
	{
		return TEXT("LC1_RAIL_CRITERION_NOT_DRAWN");
	}
	const FBlueprintLensLC1RailSurfaceLabel* FoldBoundaryLabel =
		Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key.StartsWith(TEXT("fold-boundary"));
			});
	TSet<FString> Accounted;
	for (const FString& Id : Radius.DrawnUnitIds)
	{
		if (Accounted.Contains(Id)) return TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED");
		Accounted.Add(Id);
	}
	for (const FString& Id : Radius.FoldedUnitIds)
	{
		const bool bIsBoundaryCap = Projection.BoundaryCaps.ContainsByPredicate(
			[&Id](const FBlueprintLensLC1RailBoundaryCap& Cap)
			{
				return Cap.UnitId == Id;
			});
		if (Accounted.Contains(Id)) return TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED");
		if (bIsBoundaryCap) return TEXT("LC1_RAIL_BOUNDARY_CAP_NOT_DRAWN");
		Accounted.Add(Id);
	}
	for (const FString& Id : Radius.RetainedBoundaryCapIds)
	{
		const bool bIsBoundaryCap = Projection.BoundaryCaps.ContainsByPredicate(
			[&Id](const FBlueprintLensLC1RailBoundaryCap& Cap)
			{
				return Cap.UnitId == Id;
			});
		if (Accounted.Contains(Id) || !bIsBoundaryCap)
		{
			return TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED");
		}
		Accounted.Add(Id);
	}
	if (DrawnUnitCount() != Radius.DrawnUnitIds.Num() ||
		Radius.DrawnUnitIds.Num() + Radius.FoldedUnitIds.Num() +
			Radius.RetainedBoundaryCapIds.Num() != Projection.AllUnitIds.Num() ||
		Accounted.Num() != Projection.AllUnitIds.Num() ||
		(!Radius.FoldedUnitIds.IsEmpty() &&
			(!Radius.FoldBoundaryBounds.bIsValid || Radius.FoldReaderText.IsEmpty() ||
				FoldBoundaryLabel == nullptr ||
				FoldBoundaryLabel->Text != Radius.FoldReaderText ||
				!Radius.FoldBoundaryBounds.IsInsideOrOn(
					FoldBoundaryLabel->MeasuredBounds.Min) ||
				!Radius.FoldBoundaryBounds.IsInsideOrOn(
					FoldBoundaryLabel->MeasuredBounds.Max) ||
				(!Radius.RetainedBoundaryCapIds.IsEmpty() &&
					FoldBoundaryLabel->MeasuredBounds.GetSize().Y +
						KINDA_SMALL_NUMBER <
						MeasureWrappedSmallLabelHeight(
							Radius.FoldReaderText,
							FoldBoundaryLabel->MeasuredBounds.GetSize().X)))))
	{
		return TEXT("LC1_RAIL_RADIUS_FOLD_UNACCOUNTED");
	}
	const FBlueprintLensLC1RailStation* CriterionStation =
		Stations.FindByPredicate([&Projection](const auto& Station)
		{
			return Station.UnitId == Projection.CriterionUnitId &&
				Station.bIsCriterion;
		});
	if (CriterionStation == nullptr ||
		!CriterionDockBounds.IsInsideOrOn(CriterionStation->Position))
	{
		return TEXT("LC1_RAIL_CRITERION_STATION_OUTSIDE_DOCK");
	}
	if (Stations.IsEmpty() || SpineRoute.Num() != Stations.Num() + 2 ||
		SpineRoute[0].Y >= Stations[0].Position.Y)
	{
		return TEXT("LC1_RAIL_SPINE_ROUTE_INVALID");
	}
	for (int32 Index = 1; Index < SpineRoute.Num(); ++Index)
	{
		if (SpineRoute[Index].Y < SpineRoute[Index - 1].Y)
		{
			return TEXT("LC1_RAIL_SPINE_ROUTE_NOT_MONOTONIC");
		}
	}
	if (!CriterionDockBounds.IsInsideOrOn(SpineRoute.Last()))
	{
		return TEXT("LC1_RAIL_SPINE_ROUTE_INVALID");
	}
	if (!HasNoLabelIntersections() || !HasNoLabelRouteIntersections())
	{
		return TEXT("LC1_RAIL_LABEL_CLEARANCE_FAILED");
	}
	return FString();
}

bool FBlueprintLensLC1RailSurfaceLayout::IsRenderable(
	const FBlueprintLensLC1RailProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC1_RAIL_SURFACE_LAYOUT_COMPLETE") &&
		InvariantDiagnostic(Projection).IsEmpty();
}

FBlueprintLensLC1RailSurfaceLayout FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
	const FBlueprintLensLC1RailProjection& Projection,
	const FBlueprintLensLC1RailLayoutSessionResult& Session,
	const float TargetWidth, const int32 RequestedRadius, const int32 DefaultRadius)

{
	return Build(
		Projection,
		Session,
		FBlueprintLensCompositeRailSlots(),
		TargetWidth,
		RequestedRadius,
		DefaultRadius);
}

FBlueprintLensLC1RailSurfaceLayout FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
	const FBlueprintLensLC1RailProjection& Projection,
	const FBlueprintLensLC1RailLayoutSessionResult& Session,
	const FBlueprintLensCompositeRailSlots& Slots,
	const float TargetWidth,
	const int32 RequestedRadius,
	const int32 DefaultRadius,
	const bool bDataAnswer)
{
	FBlueprintLensLC1RailSurfaceLayout Result;
	if (!Projection.IsRenderable() || !Session.IsRenderable(Projection) ||
		TargetWidth < 320.0f || DefaultRadius < 0 ||
		(!Slots.DiagnosticCode.IsEmpty() && !Slots.IsRenderable(Projection)))
	{
		Result.DiagnosticCode = TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED");
		return Result;
	}
	const bool bMixedFeatureProjection = !Projection.DeferredRelationIds.IsEmpty();
	const bool bStationOrderPresentation =
		bDataAnswer || bMixedFeatureProjection;
	for (const auto& Unit : Projection.OrderedCanonicalUnits) Result.CanonicalUnitIds.Add(Unit.UnitId);
	const int32 MaxRadius = Projection.OrderedCanonicalUnits.Num() - 1;
	Result.Radius.DefaultRadius = FMath::Min(DefaultRadius, MaxRadius);
	Result.Radius.CurrentRadius = FMath::Clamp(
		RequestedRadius == INDEX_NONE ? Result.Radius.DefaultRadius : RequestedRadius,
		0, MaxRadius);
	const int32 FirstDrawn = Projection.OrderedCanonicalUnits.Num() -
		(Result.Radius.CurrentRadius + 1);
	for (int32 Index = 0; Index < Projection.OrderedCanonicalUnits.Num(); ++Index)
	{
		const FString& UnitId = Projection.OrderedCanonicalUnits[Index].UnitId;
		const bool bBoundaryCap = Projection.BoundaryCaps.ContainsByPredicate(
			[&UnitId](const FBlueprintLensLC1RailBoundaryCap& Cap)
			{
				return Cap.UnitId == UnitId;
			});
		if (Index < FirstDrawn && bBoundaryCap)
		{
			// A cap is a displayed identity, not an omitted rail station.
			Result.Radius.RetainedBoundaryCapIds.Add(UnitId);
		}
		else if (Index < FirstDrawn)
		{
			Result.Radius.FoldedUnitIds.Add(UnitId);
		}
		else
		{
			Result.Radius.DrawnUnitIds.Add(UnitId);
		}
	}
	const float RailX = 30.0f;
	const float LabelX = 52.0f;
	const float AnnotationWidth = 60.0f;
	const float AnnotationX = TargetWidth - AnnotationWidth - 16.0f;
	const float UnitLabelWidth = FMath::Max(
		AnnotationX - LabelX - 12.0f,
		120.0f);
	const float FullLabelWidth = FMath::Max(TargetWidth - LabelX - 16.0f, 180.0f);
	constexpr float UnitLabelHeight = 36.0f;
	constexpr float CriterionNameOffset = 22.0f;
	float Y = 28.0f;
	const bool bBoundedRadius = Result.Radius.CurrentRadius < MaxRadius;
	const bool bDifferentFromNormal =
		Result.Radius.CurrentRadius != Result.Radius.DefaultRadius;
	Result.Radius.ReaderText = bDataAnswer
		? FString::Printf(
			TEXT("Scale: Data answer positions. Showing %d of %d writes, controllers, and boundaries toward %s"),
			Result.Radius.CurrentRadius + 1,
			MaxRadius + 1,
			*Projection.CriterionDisplayLabel)
		: bStationOrderPresentation
		? FString::Printf(
			TEXT("Scale: rail stations. Showing %d of %d stations toward %s"),
			Result.Radius.CurrentRadius + 1,
			MaxRadius + 1,
			*Projection.CriterionDisplayLabel)
		: FString::Printf(
			TEXT("%sShowing %d of %d hops back from %s"),
			bBoundedRadius ? TEXT("Scale: bounded radius. ")
				: TEXT("Scale: complete route. "),
			Result.Radius.CurrentRadius,
			MaxRadius,
			*Projection.CriterionDisplayLabel);
	if (bDifferentFromNormal)
	{
		Result.Radius.ReaderText += bDataAnswer
			? FString::Printf(
				TEXT("; normally shows %d answer positions"),
				Result.Radius.DefaultRadius + 1)
			: bStationOrderPresentation
			? FString::Printf(
				TEXT("; normally shows %d stations"),
				Result.Radius.DefaultRadius + 1)
			: FString::Printf(
				TEXT("; normally shows %d hops"),
				Result.Radius.DefaultRadius);
	}
	if (Result.Radius.FoldedUnitIds.IsEmpty() &&
		Result.Radius.RetainedBoundaryCapIds.IsEmpty())
	{
		Result.Radius.ReaderText += bDataAnswer
			? TEXT("; every write/controller position is visible and none is folded.")
			: bStationOrderPresentation
			? TEXT("; all rail stations are visible and no rail stations are folded.")
			: TEXT("; the complete route is visible with no units folded.");
	}
	else
	{
		Result.Radius.ReaderText += bDataAnswer
			? FString::Printf(
				TEXT(".\n%d earlier answer positions are folded into one counted boundary; %d boundary caps remain displayed."),
				Result.Radius.FoldedUnitIds.Num(),
				Result.Radius.RetainedBoundaryCapIds.Num())
			: FString::Printf(
				TEXT(".\n%d rail stations are folded into one counted boundary; %d boundary caps remain displayed."),
				Result.Radius.FoldedUnitIds.Num(),
				Result.Radius.RetainedBoundaryCapIds.Num());
	}
	const float ScaleRuleHeight =
		Result.Radius.FoldedUnitIds.IsEmpty() &&
			Result.Radius.RetainedBoundaryCapIds.IsEmpty() ? 42.0f : 66.0f;
	Result.ScaleRuleBounds = FBox2D(
		FVector2D(16.0f, Y),
		FVector2D(TargetWidth - 16.0f, Y + ScaleRuleHeight));
	AddLabel(
		Result,
		TEXT("scale-rule"),
		FString(),
		Result.Radius.ReaderText,
		FVector2D(32.0f, Y + 8.0f),
		FVector2D(FMath::Max(TargetWidth - 64.0f, 180.0f), ScaleRuleHeight - 16.0f));
	Y += ScaleRuleHeight + 10.0f;
	for (const FBlueprintLensLC1RailBoundaryCap& Cap : Projection.BoundaryCaps)
	{
		const FBlueprintLensCompositeTerminalCapSlot* TerminalSlot =
			Slots.TerminalCaps.FindByPredicate(
				[&Cap](const FBlueprintLensCompositeTerminalCapSlot& Slot)
				{
					return Slot.UnitId == Cap.UnitId;
				});
		const FBlueprintLensCompositeAttachment* F12Attachment =
			TerminalSlot != nullptr
				? TerminalSlot->Attachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("F12");
					})
				: nullptr;
		const FBlueprintLensCompositeAttachment* LC3Attachment =
			TerminalSlot != nullptr
				? TerminalSlot->Attachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC3");
					})
				: nullptr;
		const FBlueprintLensCompositeAttachment* LC6Attachment =
			TerminalSlot != nullptr
				? TerminalSlot->Attachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC6");
					})
				: nullptr;
		const FBlueprintLensCompositeAttachment* LC5Attachment =
			TerminalSlot != nullptr
				? TerminalSlot->Attachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC5");
					})
				: nullptr;
		const bool bLC3Expanded = LC3Attachment != nullptr &&
			LC3Attachment->Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded;
		const float DetailHeight = bLC3Expanded &&
			!LC3Attachment->DetailLines.IsEmpty()
			? MeasureWrappedSmallLabelHeight(
				FString::Join(LC3Attachment->DetailLines, TEXT("\n")),
				FullLabelWidth)
			: 0.0f;
		const bool bLC6Expanded = LC6Attachment != nullptr &&
			LC6Attachment->Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded &&
			LC6Attachment->ExpandedContentHeight > 0.0f;
		const bool bLC5Expanded = LC5Attachment != nullptr &&
			LC5Attachment->Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded &&
			LC5Attachment->ExpandedContentHeight > 0.0f;
		const float CapHeight = 82.0f +
			(F12Attachment != nullptr ? 40.0f : 0.0f) +
			(LC3Attachment != nullptr ? 40.0f : 0.0f) +
			(bLC3Expanded ? DetailHeight + 12.0f : 0.0f) +
			(LC5Attachment != nullptr ? 40.0f : 0.0f) +
			(bLC5Expanded
				? LC5Attachment->ExpandedContentHeight + 12.0f
				: 0.0f) +
			(LC6Attachment != nullptr ? 40.0f : 0.0f) +
			(bLC6Expanded
				? LC6Attachment->ExpandedContentHeight + 12.0f
				: 0.0f);
		const FBox2D CapBounds(
			FVector2D(16.0f, Y),
			FVector2D(TargetWidth - 16.0f, Y + CapHeight));
		Result.BoundaryCapBounds.Add(CapBounds);
		AddLabel(
			Result,
			FString::Printf(TEXT("boundary-cap-title:%s"), *Cap.UnitId),
			Cap.UnitId,
			Cap.Title,
			FVector2D(28.0f, Y + 8.0f),
			FVector2D(FullLabelWidth, 20.0f));
		AddLabel(
			Result,
			FString::Printf(TEXT("boundary-cap-disclosure:%s"), *Cap.UnitId),
			Cap.UnitId,
			Cap.Disclosure,
			FVector2D(28.0f, Y + 30.0f),
			FVector2D(FullLabelWidth, 44.0f));
		if (F12Attachment != nullptr)
		{
			AddLabel(
				Result,
				FString::Printf(
					TEXT("attachment:F12:%s"),
					*F12Attachment->AttachmentId),
				Cap.UnitId,
				F12Attachment->MarkerText,
				FVector2D(28.0f, Y + 78.0f),
				FVector2D(FullLabelWidth, 36.0f));
		}
		if (LC3Attachment != nullptr)
		{
			const float SourceTop =
				Y + 78.0f + (F12Attachment != nullptr ? 40.0f : 0.0f);
			AddLabel(
				Result,
				DataValueSourceMarkerKey(*LC3Attachment),
				Cap.UnitId,
				LC3Attachment->MarkerText,
				FVector2D(28.0f, SourceTop),
				FVector2D(FullLabelWidth, 36.0f));
			if (bLC3Expanded && !LC3Attachment->DetailLines.IsEmpty())
			{
				AddLabel(
					Result,
					FString::Printf(
						TEXT("attachment-detail:LC3:%s"),
						*LC3Attachment->AttachmentId),
					Cap.UnitId,
					FString::Join(LC3Attachment->DetailLines, TEXT("\n")),
					FVector2D(28.0f, SourceTop + 38.0f),
					FVector2D(FullLabelWidth, DetailHeight));
			}
		}
		if (LC5Attachment != nullptr)
		{
			const float PortalTop =
				Y + 78.0f +
				(F12Attachment != nullptr ? 40.0f : 0.0f) +
				(LC3Attachment != nullptr ? 40.0f : 0.0f) +
				(bLC3Expanded ? DetailHeight + 12.0f : 0.0f);
			const bool bRefusal =
				LC5Attachment->AttachmentId.StartsWith(TEXT("lc5-refusal:"));
			AddLabel(
				Result,
				FString(bRefusal
					? TEXT("refusal:LC5:")
					: TEXT("attachment:LC5:")) +
					LC5Attachment->AttachmentId,
				Cap.UnitId,
				LC5Attachment->MarkerText,
				FVector2D(28.0f, PortalTop),
				FVector2D(FullLabelWidth, 36.0f));
			if (bLC5Expanded)
			{
				Result.ExpandedTerminalAttachmentBounds.Add(
					Cap.UnitId,
					FBox2D(
						FVector2D(28.0f, PortalTop + 40.0f),
						FVector2D(
							TargetWidth - 28.0f,
							PortalTop + 40.0f +
								LC5Attachment->ExpandedContentHeight)));
			}
		}
		if (LC6Attachment != nullptr)
		{
			const float MatrixTop =
				Y + 78.0f +
				(F12Attachment != nullptr ? 40.0f : 0.0f) +
				(LC3Attachment != nullptr ? 40.0f : 0.0f) +
				(bLC3Expanded ? DetailHeight + 12.0f : 0.0f) +
				(LC5Attachment != nullptr ? 40.0f : 0.0f) +
				(bLC5Expanded
					? LC5Attachment->ExpandedContentHeight + 12.0f
					: 0.0f);
			AddLabel(
				Result,
				FString::Printf(
					TEXT("attachment:LC6:%s"),
					*LC6Attachment->AttachmentId),
				Cap.UnitId,
				LC6Attachment->MarkerText,
				FVector2D(28.0f, MatrixTop),
				FVector2D(FullLabelWidth, 36.0f));
			if (bLC6Expanded)
			{
				Result.ExpandedTerminalAttachmentBounds.Add(
					Cap.UnitId,
					FBox2D(
						FVector2D(28.0f, MatrixTop + 40.0f),
						FVector2D(
							TargetWidth - 28.0f,
							MatrixTop + 40.0f +
								LC6Attachment->ExpandedContentHeight)));
			}
		}
		Y += CapHeight + 8.0f;
	}
	if (!Result.Radius.FoldedUnitIds.IsEmpty())
	{
		for (const FString& FoldedUnitId : Result.Radius.FoldedUnitIds)
		{
			const FBlueprintLensCompositeStationSlot* FoldedStation =
				Slots.FindStation(FoldedUnitId);
			if (FoldedStation == nullptr ||
				FoldedStation->BesideAttachments.IsEmpty())
			{
				continue;
			}
			++Result.Radius.FoldedAttachmentStationCount;
			Result.Radius.FoldedAttachmentCount +=
				FoldedStation->BesideAttachments.Num();
		}
		const auto* First = Projection.OrderedCanonicalUnits.FindByPredicate([&Result](const auto& U){ return U.UnitId == Result.Radius.FoldedUnitIds[0]; });
		const auto* Last = Projection.OrderedCanonicalUnits.FindByPredicate([&Result](const auto& U){ return U.UnitId == Result.Radius.FoldedUnitIds.Last(); });
		Result.Radius.FoldReaderText = bDataAnswer
			? FString::Printf(
				TEXT("Folded Data boundary: %d earlier answer positions are not drawn.\n%s ... %s form one contiguous run; their proven-before placement is preserved.\n%d boundary caps remain displayed. Expand this boundary or raise the radius to return every write/controller position and its assigned source disclosure."),
				Result.Radius.FoldedUnitIds.Num(),
				*First->DisplayLabel,
				*Last->DisplayLabel,
				Result.Radius.RetainedBoundaryCapIds.Num())
			: Result.Radius.RetainedBoundaryCapIds.IsEmpty()
				? FString::Printf(
					TEXT("Folded boundary: %d earlier units are not drawn.\n%s ... %s "
						"form one contiguous run; their order is preserved.\nExpand this "
						"boundary or raise the radius to return every unit and its source identity."),
					Result.Radius.FoldedUnitIds.Num(), *First->DisplayLabel,
					*Last->DisplayLabel)
				: FString::Printf(
					TEXT("Folded boundary: %d earlier rail stations are not drawn.\n%s ... %s "
						"form one contiguous run; their order is preserved.\n%d boundary "
						"caps remain displayed; no direct rail segment crosses the omission.\n"
						"Expand this boundary or raise the radius to return every unit and "
						"its source identity."),
					Result.Radius.FoldedUnitIds.Num(), *First->DisplayLabel,
					*Last->DisplayLabel,
					Result.Radius.RetainedBoundaryCapIds.Num());
		if (Result.Radius.FoldedAttachmentStationCount > 0)
		{
			Result.Radius.FoldReaderText += FString::Printf(
				TEXT("\nThis fold also holds %d beside-station attachments "
					"beside %d of its stations; opening it restores those "
					"station attachments."),
				Result.Radius.FoldedAttachmentCount,
				Result.Radius.FoldedAttachmentStationCount);
		}
		const float FoldLabelHeight = MeasureWrappedSmallLabelHeight(
			Result.Radius.FoldReaderText, FullLabelWidth);
		const float FoldBlockHeight = FMath::Max(66.0f, FoldLabelHeight + 16.0f);
		Result.Radius.FoldBoundaryBounds = FBox2D(
			FVector2D(16.0f, Y),
			FVector2D(TargetWidth - 16.0f, Y + FoldBlockHeight));
		AddLabel(
			Result,
			Result.Radius.FoldedAttachmentStationCount > 0
				? TEXT("fold-boundary:attachments")
				: TEXT("fold-boundary"),
			FString(),
			Result.Radius.FoldReaderText,
			FVector2D(28.0f, Y + 8.0f),
			FVector2D(FullLabelWidth, FoldLabelHeight));
		Y += FoldBlockHeight + 20.0f;
	}
	const FString StageLabel = bDataAnswer
		? FString::Printf(
			TEXT("Proven-before answer-position order toward %s: %d writes; %d write-dependency %s %s placement; %d answer %s (writes, controllers, boundaries, and value-source units) %s represented across %d Data %s. Outside a declared SCC segment, a lower answer position cannot be a proven cause of an upper answer position; declared incomparable and SCC segments state their local order boundary."),
			*Projection.CriterionDisplayLabel,
			Slots.DataAnswerWriteCount,
			Projection.StationOrderRelations.Num(),
			Projection.StationOrderRelations.Num() == 1
				? TEXT("relation")
				: TEXT("relations"),
			Projection.StationOrderRelations.Num() == 1
				? TEXT("constrains")
				: TEXT("constrain"),
			Slots.DataAnswerUnitCount,
			Slots.DataAnswerUnitCount == 1
				? TEXT("unit")
				: TEXT("units"),
			Slots.DataAnswerUnitCount == 1
				? TEXT("is")
				: TEXT("are"),
			Slots.DataAnswerRelationCount,
			Slots.DataAnswerRelationCount == 1
				? TEXT("relation")
				: TEXT("relations"))
		: bStationOrderPresentation
		? FString::Printf(
			TEXT("Proven-before station order toward %s: %d station-to-station "
				"relations constrain placement; %d rail units and %d rail relations "
				"are drawn. Declared incomparable and SCC segments state their "
				"local order boundary."),
			*Projection.CriterionDisplayLabel,
			Projection.StationOrderRelations.Num(),
			Projection.AllUnitIds.Num(),
			Projection.AllRelationIds.Num())
		: FString::Printf(
			TEXT("This question's execution route follows proven predecessors backward "
				"toward %s: %d units and %d relations."),
			*Projection.CriterionDisplayLabel,
			Projection.AllUnitIds.Num(),
			Projection.AllRelationIds.Num());
	const float StageLabelHeight = bStationOrderPresentation
		? FMath::Max(
			36.0f,
			MeasureWrappedSmallLabelHeight(StageLabel, FullLabelWidth))
		: 36.0f;
	AddLabel(
		Result,
		TEXT("stage"),
		FString(),
		StageLabel,
		FVector2D(LabelX, Y),
		FVector2D(FullLabelWidth, StageLabelHeight));
	Y += StageLabelHeight + 12.0f;

	FString FirstDrawnUnitId;
	for (const FBlueprintLensLC1RailCanonicalUnit& CanonicalUnit :
		 Projection.OrderedCanonicalUnits)
	{
		if (!Result.Radius.DrawnUnitIds.Contains(CanonicalUnit.UnitId))
		{
			continue;
		}
		const bool bHasDrawnPredecessor =
			Projection.OrderedExecutionRelations.ContainsByPredicate(
				[&CanonicalUnit, &Result](
					const FBlueprintLensLC1RailExecutionRelation& Relation)
				{
					return Relation.TargetUnitId == CanonicalUnit.UnitId &&
						Result.Radius.DrawnUnitIds.Contains(Relation.SourceUnitId);
				});
		if (!bHasDrawnPredecessor)
		{
			FirstDrawnUnitId = CanonicalUnit.UnitId;
			break;
		}
	}
	for (const FString& UnitId : Result.Radius.DrawnUnitIds)
	{
		const auto* Unit = Projection.OrderedCanonicalUnits.FindByPredicate([&UnitId](const auto& U){ return U.UnitId == UnitId; });
		if (Unit == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED");
			return Result;
		}
		for (const FBlueprintLensCompositeSpanSlot& Span : Slots.Spans)
		{
			FString FirstDrawnMemberUnitId;
			for (const FBlueprintLensLC1RailCanonicalUnit& OrderedUnit :
				 Projection.OrderedCanonicalUnits)
			{
				if (Span.MemberUnitIds.Contains(OrderedUnit.UnitId) &&
					Result.Radius.DrawnUnitIds.Contains(OrderedUnit.UnitId))
				{
					FirstDrawnMemberUnitId = OrderedUnit.UnitId;
					break;
				}
			}
			if (FirstDrawnMemberUnitId != UnitId)
			{
				continue;
			}
			if (Span.bIsOrderBoundary && !Span.ReaderText.IsEmpty())
			{
				const float BoundaryHeight = FMath::Max(
					28.0f,
					MeasureWrappedSmallLabelHeight(
						Span.ReaderText, FullLabelWidth) + 8.0f);
				const FString BoundaryKey =
					(Span.OrderRegionKind ==
							EBlueprintLensLC1RailOrderRegionKind::Incomparable
						? FString(TEXT("order-boundary:incomparable:"))
						: FString(TEXT("order-boundary:scc:"))) +
					Span.SlotId.RightChop(6);
				AddLabel(
					Result,
					BoundaryKey,
					UnitId,
					Span.ReaderText,
					FVector2D(LabelX, Y),
					FVector2D(FullLabelWidth, BoundaryHeight));
				Y += BoundaryHeight + 8.0f;
			}
			const FBlueprintLensCompositeAttachment* LC7Attachment =
				Span.Attachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC7");
					});
			if (LC7Attachment != nullptr)
			{
				FBlueprintLensLC1RailSpanDecoration SpanLayout;
				SpanLayout.SpanId = Span.SlotId;
				SpanLayout.ActionBounds = FBox2D(
					FVector2D(LabelX, Y),
					FVector2D(TargetWidth - 16.0f, Y + 32.0f));
				AddLabel(
					Result,
					FString::Printf(
						TEXT("span-attachment:LC7:%s"), *Span.SlotId),
					Span.SlotId,
					LC7Attachment->MarkerText,
					SpanLayout.ActionBounds.Min,
					SpanLayout.ActionBounds.GetSize());
				Y = SpanLayout.ActionBounds.Max.Y + 6.0f;
				if (Span.Disclosure ==
						EBlueprintLensCompositeDisclosure::Expanded &&
					LC7Attachment->Disclosure ==
						EBlueprintLensCompositeDisclosure::Expanded &&
					LC7Attachment->ExpandedContentHeight > 0.0f)
				{
					const float ContentInset =
						TargetWidth >= 462.0f ? 16.0f : 0.0f;
					SpanLayout.ExpandedContentBounds = FBox2D(
						FVector2D(ContentInset, Y),
						FVector2D(
							TargetWidth - ContentInset,
							Y + LC7Attachment->ExpandedContentHeight));
					Y = SpanLayout.ExpandedContentBounds.Max.Y + 8.0f;
				}
				Result.SpanDecorations.Add(MoveTemp(SpanLayout));
			}
		}
		const FBlueprintLensCompositeStationSlot* CompositeStation =
			Slots.FindStation(UnitId);
		const FBlueprintLensCompositeAttachment* F12Attachment =
			CompositeStation != nullptr
				? CompositeStation->BesideAttachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("F12");
					})
				: nullptr;
		const FBlueprintLensCompositeAttachment* LC3Attachment =
			CompositeStation != nullptr
				? CompositeStation->BesideAttachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC3");
					})
				: nullptr;
		const FBlueprintLensCompositeAttachment* LC5StationAttachment =
			CompositeStation != nullptr
				? CompositeStation->BesideAttachments.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC5");
					})
				: nullptr;
		TArray<const FBlueprintLensCompositeAttachment*> BesideAttachments;
		if (LC3Attachment != nullptr)
		{
			BesideAttachments.Add(LC3Attachment);
		}
		if (F12Attachment != nullptr && !bDataAnswer)
		{
			BesideAttachments.Add(F12Attachment);
		}
		if (LC5StationAttachment != nullptr && !bDataAnswer)
		{
			BesideAttachments.Add(LC5StationAttachment);
		}
		const FBlueprintLensCompositeAttachment* BesideAttachment =
			!BesideAttachments.IsEmpty()
				? BesideAttachments[0]
				: bDataAnswer
					? (LC3Attachment != nullptr ? LC3Attachment : F12Attachment)
					: nullptr;
		for (const FBlueprintLensCompositeAttachment* Attachment :
			BesideAttachments)
		{
			if (Attachment->Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded)
			{
				BesideAttachment = Attachment;
				break;
			}
		}
		const bool bHasBesideAttachment = BesideAttachment != nullptr;
		const bool bBesideAttachmentExpanded = bHasBesideAttachment &&
			BesideAttachment->Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded;
		const float DataStationBaseHeight = bDataAnswer &&
			(F12Attachment != nullptr || LC3Attachment != nullptr)
			? 76.0f + (Unit->bIsCriterion ? CriterionNameOffset : 0.0f)
			: UnitLabelHeight;
		const float BesideAttachmentContentTop =
			Y + (bDataAnswer
				? DataStationBaseHeight
				: FMath::Max(
					UnitLabelHeight,
					static_cast<float>(BesideAttachments.Num()) * 40.0f) +
					(Unit->bIsCriterion ? CriterionNameOffset : 0.0f));
		const bool bIsGuard = CompositeStation != nullptr &&
			CompositeStation->Appearance.Kind ==
				EBlueprintLensCompositeStationAppearanceKind::Guard;
		const int32 GuardDepth = bIsGuard
			? CompositeStation->Appearance.NestingDepth
			: 0;
		const float StationLabelX = LabelX +
			static_cast<float>(GuardDepth) * 14.0f;
		const FVector2D LabelPosition(
			StationLabelX,
			Y + (Unit->bIsCriterion ? CriterionNameOffset : 0.0f));
		const float AttachmentWidth = bHasBesideAttachment && !bDataAnswer
			? FMath::Clamp(TargetWidth * 0.36f, 150.0f, 230.0f)
			: 0.0f;
		const float AttachmentX = !bDataAnswer
			? TargetWidth - AttachmentWidth - 16.0f
			: StationLabelX;
		const float DataStationTitleWidth = bDataAnswer &&
			F12Attachment != nullptr
			? FMath::Clamp(FullLabelWidth * 0.30f, 120.0f, 220.0f)
			: 0.0f;
		const float DataAnswerX = StationLabelX + DataStationTitleWidth + 10.0f;
		const float DataAnswerWidth = bDataAnswer && F12Attachment != nullptr
			? FMath::Max(TargetWidth - DataAnswerX - 16.0f, 180.0f)
			: 0.0f;
		const float AvailableUnitLabelWidth = bDataAnswer &&
			F12Attachment != nullptr
			? DataStationTitleWidth
			: bHasBesideAttachment
			? AttachmentX - StationLabelX - 8.0f
			: UnitLabelWidth - (StationLabelX - LabelX);
		const FVector2D LabelSize(
			FMath::Max(AvailableUnitLabelWidth, 100.0f),
			bIsGuard ? 22.0f : UnitLabelHeight);
		const FBox2D UnitLabelBounds(LabelPosition, LabelPosition + LabelSize);
		const FVector2D StationPosition(
			RailX,
			LabelPosition.Y +
				MeasureUnitLabelText(
					bIsGuard
						? CompositeStation->Appearance.MarkerText
						: Unit->DisplayLabel,
					Unit->bIsCriterion).Y * 0.5f);
		FBlueprintLensLC1RailStation Station;
		Station.UnitId = UnitId;
		Station.Position = StationPosition;
		Station.HitRegion = FBox2D(
			FVector2D(16.0f, UnitLabelBounds.Min.Y),
			FVector2D(
				TargetWidth - 16.0f,
				Y + (bDataAnswer
					? DataStationBaseHeight
					: FMath::Max(
						UnitLabelHeight,
						static_cast<float>(BesideAttachments.Num()) * 40.0f))));
		Station.bIsCriterion = Unit->bIsCriterion;
		Station.bIsGuard = bIsGuard;
		Station.GuardNestingDepth = GuardDepth;
		Station.bGuardDetailExpanded = bIsGuard &&
			CompositeStation->Appearance.Disclosure ==
				EBlueprintLensCompositeDisclosure::Expanded;
		Station.bBesideAttachmentExpanded = bBesideAttachmentExpanded;
		if (bIsGuard)
		{
			Station.GuardAppearanceBounds = FBox2D(
				FVector2D(StationLabelX - 8.0f, Y - 4.0f),
				FVector2D(
					TargetWidth - 16.0f,
					Y + UnitLabelHeight));
			if (Station.bGuardDetailExpanded &&
				CompositeStation->Appearance.ExpandedContentHeight > 0.0f)
			{
				const float ContentInset = TargetWidth >= 462.0f ? 16.0f : 0.0f;
				Station.ExpandedAppearanceBounds = FBox2D(
					FVector2D(ContentInset, Y + UnitLabelHeight + 8.0f),
					FVector2D(
						TargetWidth - ContentInset,
						Y + UnitLabelHeight + 8.0f +
							CompositeStation->Appearance.ExpandedContentHeight));
				Station.HitRegion.Max.Y =
					Station.ExpandedAppearanceBounds.Max.Y;
			}
			else if (Station.bGuardDetailExpanded)
			{
				const float DetailHeight = MeasureWrappedSmallLabelHeight(
					FString::Join(
						CompositeStation->Appearance.DetailLines,
						TEXT("\n")),
					LabelSize.X - 12.0f) + 12.0f;
				Station.GuardAppearanceBounds.Max.Y += DetailHeight;
				Station.HitRegion.Max.Y = Station.GuardAppearanceBounds.Max.Y;
			}
		}
		if (bHasBesideAttachment)
		{
			if (bDataAnswer)
			{
				if (F12Attachment != nullptr)
				{
					Station.BesideAttachmentBounds.Add(FBox2D(
						FVector2D(DataAnswerX, Y),
						FVector2D(TargetWidth - 16.0f, Y + 36.0f)));
				}
				if (LC3Attachment != nullptr)
				{
					Station.BesideAttachmentBounds.Add(FBox2D(
						FVector2D(
							StationLabelX,
							Y + 40.0f +
								(Unit->bIsCriterion
									? CriterionNameOffset
									: 0.0f)),
						FVector2D(
							TargetWidth - 16.0f,
							Y + 72.0f +
								(Unit->bIsCriterion
									? CriterionNameOffset
									: 0.0f))));
				}
			}
			else
			{
				for (int32 AttachmentIndex = 0;
					AttachmentIndex < BesideAttachments.Num();
					++AttachmentIndex)
				{
					const float AttachmentTop =
						Y + AttachmentIndex * 40.0f;
					Station.BesideAttachmentBounds.Add(FBox2D(
						FVector2D(AttachmentX, AttachmentTop),
						FVector2D(
							TargetWidth - 16.0f,
							AttachmentTop + UnitLabelHeight)));
				}
			}
			if (bBesideAttachmentExpanded &&
				BesideAttachment->ExpandedContentHeight > 0.0f)
			{
				const float ContentInset = TargetWidth >= 462.0f ? 16.0f : 0.0f;
				Station.ExpandedAppearanceBounds = FBox2D(
					FVector2D(ContentInset, BesideAttachmentContentTop + 8.0f),
					FVector2D(
						TargetWidth - ContentInset,
						BesideAttachmentContentTop + 8.0f +
							BesideAttachment->ExpandedContentHeight));
				Station.HitRegion.Max.Y =
					Station.ExpandedAppearanceBounds.Max.Y;
			}
			else if (bBesideAttachmentExpanded &&
				!BesideAttachment->DetailLines.IsEmpty())
			{
				const float DetailHeight = MeasureWrappedSmallLabelHeight(
					FString::Join(BesideAttachment->DetailLines, TEXT("\n")),
					FullLabelWidth - 12.0f) + 12.0f;
				Station.ExpandedAppearanceBounds = FBox2D(
					FVector2D(LabelX, BesideAttachmentContentTop + 4.0f),
					FVector2D(
						TargetWidth - 16.0f,
						BesideAttachmentContentTop + 4.0f + DetailHeight));
				Station.HitRegion.Max.Y =
					Station.ExpandedAppearanceBounds.Max.Y;
			}
		}
		Result.Stations.Add(MoveTemp(Station));
		const bool bHasBoundaryCap = Projection.BoundaryCaps.ContainsByPredicate(
			[&UnitId](const FBlueprintLensLC1RailBoundaryCap& Cap)
			{
				return Cap.UnitId == UnitId;
			});
		if (Unit->bIsCriterion)
		{
			const float CriterionDockBottom = FMath::Max(
				Y + 60.0f,
				Result.Stations.Last().HitRegion.Max.Y + 8.0f);
			Result.CriterionDockBounds = FBox2D(
				FVector2D(16.0f, Y - 8.0f),
				FVector2D(TargetWidth - 16.0f, CriterionDockBottom));
			AddLabel(
				Result,
				TEXT("criterion-caption"),
				FString(),
				TEXT("CRITERION"),
				FVector2D(LabelX, Y),
				FVector2D(LabelSize.X, 20.0f));
			AddLabel(
				Result,
				TEXT("criterion"),
				UnitId,
				Unit->DisplayLabel,
				UnitLabelBounds.Min,
				UnitLabelBounds.GetSize());
		}
		else if (!bHasBoundaryCap)
		{
			AddLabel(
				Result,
				bIsGuard
					? FString::Printf(TEXT("guard-marker:%s"), *UnitId)
					: UnitId,
				UnitId,
				bIsGuard
					? CompositeStation->Appearance.MarkerText
					: Unit->DisplayLabel,
				UnitLabelBounds.Min,
				UnitLabelBounds.GetSize());
			if (bIsGuard &&
				CompositeStation->Appearance.Disclosure ==
					EBlueprintLensCompositeDisclosure::Collapsed &&
				!CompositeStation->Appearance.GuardReaderText.IsEmpty())
			{
				AddLabel(
					Result,
					FString::Printf(
						TEXT("guard-disclosure:%s"), *UnitId),
					UnitId,
					FString::Printf(
						TEXT("Open guard structure · %s"),
						*CompositeStation->Appearance.GuardReaderText),
					FVector2D(StationLabelX, Y + 22.0f),
					FVector2D(LabelSize.X, 14.0f));
			}
			if (bIsGuard &&
				CompositeStation->Appearance.Disclosure ==
					EBlueprintLensCompositeDisclosure::Expanded &&
				CompositeStation->Appearance.ExpandedContentHeight <= 0.0f &&
				!CompositeStation->Appearance.DetailLines.IsEmpty())
			{
				const FString DetailText = FString::Join(
					CompositeStation->Appearance.DetailLines,
					TEXT("\n"));
				const float DetailHeight = MeasureWrappedSmallLabelHeight(
					DetailText,
					LabelSize.X - 12.0f);
				AddLabel(
					Result,
					FString::Printf(TEXT("guard-detail:%s"), *UnitId),
					UnitId,
					DetailText,
					FVector2D(StationLabelX + 6.0f, Y + 24.0f),
					FVector2D(LabelSize.X - 12.0f, DetailHeight));
			}

			const bool bIsFirstDrawnUnit = UnitId == FirstDrawnUnitId;
			const bool bHasOrderedIncomingRelation =
				Projection.OrderedExecutionRelations.ContainsByPredicate(
					[&UnitId](const FBlueprintLensLC1RailExecutionRelation& Relation)
					{
						return Relation.TargetUnitId == UnitId;
					});
			if (!bStationOrderPresentation &&
				!bIsFirstDrawnUnit && !bHasOrderedIncomingRelation)
			{
				Result.DiagnosticCode = TEXT("LC1_RAIL_ORDER_NOT_PROVEN");
				return Result;
			}
			if (!bStationOrderPresentation)
			{
				AddLabel(
					Result,
					TEXT("relation-annotation"),
					FString(),
					bIsFirstDrawnUnit ? TEXT("entry") : TEXT("then"),
					FVector2D(AnnotationX, Y),
					FVector2D(AnnotationWidth, 36.0f));
			}
		}
		if (bHasBesideAttachment && !bHasBoundaryCap)
		{
			if (bDataAnswer)
			{
				if (F12Attachment != nullptr)
				{
					AddLabel(
						Result,
						FString::Printf(
							TEXT("attachment:F12:%s"),
							*F12Attachment->AttachmentId),
						UnitId,
						F12Attachment->MarkerText,
						FVector2D(DataAnswerX, Y),
						FVector2D(DataAnswerWidth, 36.0f));
				}
				if (LC3Attachment != nullptr)
				{
					AddLabel(
						Result,
						DataValueSourceMarkerKey(*LC3Attachment),
						UnitId,
						LC3Attachment->MarkerText,
						FVector2D(
							StationLabelX,
							Y + 40.0f +
								(Unit->bIsCriterion
									? CriterionNameOffset
									: 0.0f)),
						FVector2D(
							TargetWidth - StationLabelX - 16.0f,
							32.0f));
				}
			}
			else
			{
				for (int32 AttachmentIndex = 0;
					AttachmentIndex < BesideAttachments.Num();
					++AttachmentIndex)
				{
					const FBlueprintLensCompositeAttachment* Attachment =
						BesideAttachments[AttachmentIndex];
					AddLabel(
						Result,
						Attachment->GrammarId == TEXT("LC5") &&
							Attachment->AttachmentId.StartsWith(
								TEXT("lc5-refusal:"))
							? FString(TEXT("refusal:LC5:")) +
								Attachment->AttachmentId
							: FString::Printf(
								TEXT("attachment:%s:%s"),
								*Attachment->GrammarId,
								*Attachment->AttachmentId),
						UnitId,
						Attachment->MarkerText,
						FVector2D(
							AttachmentX,
							Y + AttachmentIndex * 40.0f),
						FVector2D(AttachmentWidth, UnitLabelHeight));
				}
			}
			if (bBesideAttachmentExpanded &&
				BesideAttachment->ExpandedContentHeight <= 0.0f &&
				!BesideAttachment->DetailLines.IsEmpty())
			{
				const FString DetailText = FString::Join(
					BesideAttachment->DetailLines, TEXT("\n"));
				AddLabel(
					Result,
					FString::Printf(
						TEXT("attachment-detail:%s:%s"),
						*BesideAttachment->GrammarId,
						*BesideAttachment->AttachmentId),
					UnitId,
					DetailText,
					FVector2D(LabelX, BesideAttachmentContentTop + 10.0f),
					FVector2D(
						FullLabelWidth - 12.0f,
						Result.Stations.Last()
							.ExpandedAppearanceBounds.GetSize().Y - 12.0f));
			}
		}
		Result.SpineRoute.Add(StationPosition);
		const FBlueprintLensLC1RailStation& AddedStation =
			Result.Stations.Last();
		if (AddedStation.ExpandedAppearanceBounds.bIsValid)
		{
			Y = AddedStation.ExpandedAppearanceBounds.Max.Y + 8.0f;
		}
		else
		{
			Y += bDataAnswer
				? DataStationBaseHeight + 4.0f
				: AddedStation.bGuardDetailExpanded
				? FMath::Max(
					40.0f,
					AddedStation.GuardAppearanceBounds.Max.Y - Y + 8.0f)
				: FMath::Max(
					40.0f,
					static_cast<float>(BesideAttachments.Num()) * 40.0f);
		}
		for (const FBlueprintLensCompositeBetweenStationsSlot& Between :
			 Slots.BetweenStations)
		{
			if (Between.SourceUnitId != UnitId ||
				!Result.Radius.DrawnUnitIds.Contains(Between.TargetUnitId))
			{
				continue;
			}
			const FBlueprintLensCompositeAttachment* Decoration =
				Between.Decorations.FindByPredicate(
					[](const FBlueprintLensCompositeAttachment& Candidate)
					{
						return Candidate.GrammarId == TEXT("LC4-SEQ");
					});
			if (Decoration == nullptr)
			{
				continue;
			}
			FBlueprintLensLC1RailBetweenDecoration DecorationLayout;
			DecorationLayout.RelationId = Between.RelationId;
			DecorationLayout.ActionBounds = FBox2D(
				FVector2D(LabelX, Y),
				FVector2D(TargetWidth - 16.0f, Y + 28.0f));
			AddLabel(
				Result,
				FString::Printf(
					TEXT("between-decoration:LC4-SEQ:%s"),
					*Between.RelationId),
				Between.RelationId,
				Decoration->MarkerText,
				DecorationLayout.ActionBounds.Min,
				DecorationLayout.ActionBounds.GetSize());
			Y = DecorationLayout.ActionBounds.Max.Y + 6.0f;
			if (Decoration->Disclosure ==
					EBlueprintLensCompositeDisclosure::Expanded &&
				Decoration->ExpandedContentHeight > 0.0f)
			{
				const float ContentInset = TargetWidth >= 462.0f ? 16.0f : 0.0f;
				DecorationLayout.ExpandedContentBounds = FBox2D(
					FVector2D(ContentInset, Y),
					FVector2D(
						TargetWidth - ContentInset,
						Y + Decoration->ExpandedContentHeight));
				Y = DecorationLayout.ExpandedContentBounds.Max.Y + 8.0f;
			}
			Result.BetweenDecorations.Add(MoveTemp(DecorationLayout));
		}
	}
	if (!Result.Stations.IsEmpty())
	{
		Result.SpineRoute.Insert(
			FVector2D(RailX, Result.Stations[0].HitRegion.Min.Y),
			0);
	}
	if (Result.CriterionDockBounds.bIsValid)
	{
		const FBlueprintLensLC1RailStation* CriterionStation =
			Result.Stations.FindByPredicate(
				[](const FBlueprintLensLC1RailStation& Station)
				{
					return Station.bIsCriterion;
				});
		if (CriterionStation != nullptr)
		{
			Result.SpineRoute.Add(CriterionStation->Position);
		}
	}
	const float SelectionExplanationTop = Result.CriterionDockBounds.Max.Y + 10.0f;
	const float SelectionExplanationHeight = 66.0f;
	Result.SelectionExplanationBounds = FBox2D(
		FVector2D(16.0f, SelectionExplanationTop),
		FVector2D(TargetWidth - 16.0f,
			SelectionExplanationTop + SelectionExplanationHeight));
	AddLabel(
		Result,
		TEXT("selection-explanation"),
		FString(),
		SelectionExplanationText(),
		FVector2D(28.0f, SelectionExplanationTop + 8.0f),
		FVector2D(FMath::Max(TargetWidth - 56.0f, 180.0f), SelectionExplanationHeight - 16.0f));
	Y = FMath::Max(Y, SelectionExplanationTop + SelectionExplanationHeight);
	Result.CanvasSize = FVector2D(TargetWidth, Y + 12.0f);
	Result.DiagnosticCode = Result.InvariantDiagnostic(Projection);
	if (Result.DiagnosticCode.IsEmpty()) Result.DiagnosticCode = TEXT("LC1_RAIL_SURFACE_LAYOUT_COMPLETE");
	return Result;
}
