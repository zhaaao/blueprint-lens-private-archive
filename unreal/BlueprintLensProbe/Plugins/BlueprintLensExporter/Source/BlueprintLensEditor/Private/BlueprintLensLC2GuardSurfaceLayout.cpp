#include "BlueprintLensLC2GuardSurfaceLayout.h"

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

bool PointInside(const FVector2D& Point, const FBox2D& Bounds)
{
	return Point.X > Bounds.Min.X && Point.X < Bounds.Max.X &&
		Point.Y > Bounds.Min.Y && Point.Y < Bounds.Max.Y;
}

bool SegmentIntersects(const FVector2D& A, const FVector2D& B, const FBox2D& Bounds)
{
	if (PointInside(A, Bounds) || PointInside(B, Bounds))
	{
		return true;
	}
	const FVector2D Delta = B - A;
	float Enter = 0.0f;
	float Exit = 1.0f;
	const auto Clip = [&Enter, &Exit](float Start, float D, float Min, float Max)
	{
		if (FMath::IsNearlyZero(D))
		{
			return Start > Min && Start < Max;
		}
		float Near = (Min - Start) / D;
		float Far = (Max - Start) / D;
		if (Near > Far)
		{
			Swap(Near, Far);
		}
		Enter = FMath::Max(Enter, Near);
		Exit = FMath::Min(Exit, Far);
		return Enter < Exit;
	};
	return Clip(A.X, Delta.X, Bounds.Min.X, Bounds.Max.X) &&
		Clip(A.Y, Delta.Y, Bounds.Min.Y, Bounds.Max.Y) && Exit > 0.0f && Enter < 1.0f;
}

FBox2D Bounds(const FVector2D& Position, const FVector2D& Size)
{
	return FBox2D(Position, Position + Size);
}

bool GateContainsRail(
	const FBlueprintLensLC2GuardSurfaceGate& Gate,
	const FBlueprintLensLC2GuardSurfaceRail& Rail,
	const TArray<FBlueprintLensLC2GuardSurfaceGate>& Gates)
{
	if (Gate.GroupId == Rail.OwnerGuardGroupId)
	{
		return true;
	}
	const FBlueprintLensLC2GuardSurfaceGate* Owner = Gates.FindByPredicate(
		[&Rail](const FBlueprintLensLC2GuardSurfaceGate& Candidate)
		{
			return Candidate.GroupId == Rail.OwnerGuardGroupId;
		});
	return Owner != nullptr && Owner->ParentGroupId == Gate.GroupId;
}

void AddLabel(
	FBlueprintLensLC2GuardSurfaceLayout& Result,
	const FString& Key,
	const FString& UnitId,
	const FString& Text,
	const FVector2D& Position,
	const FVector2D& Size)
{
	FBlueprintLensLC2GuardSurfaceLabel Label;
	Label.Key = Key;
	Label.UnitId = UnitId;
	Label.Text = Text;
	Label.ExclusionBounds = Bounds(Position, Size);
	Result.Labels.Add(MoveTemp(Label));
}

// Gate and predicate wording is read from the ledger, never authored here. The
// exclusion box keeps the frozen constant for the LC2 fixture and grows with a
// longer name so a renamed guard cannot silently under-reserve space.
FString GateLabelText(const FBlueprintLensLC2GuardCompound& Compound)
{
	return FString::Printf(TEXT("GUARD GATE · %s"), *Compound.GuardReaderText);
}

FVector2D GateLabelSize(const FString& Text)
{
	return FVector2D(FMath::Max(210.0f, Text.Len() * 8.4f), 22.0f);
}

FString PredicateLabelText(const FBlueprintLensLC2GuardCanonicalUnit& Predicate)
{
	return FString::Printf(
		TEXT("PREDICATE OWNERSHIP · %s"),
		*Predicate.ReaderLabel);
}

FVector2D PredicateLabelSize(const FString& Text)
{
	return FVector2D(FMath::Max(245.0f, Text.Len() * 6.5f), 22.0f);
}

float WidestUnbreakableLabelSegment(const FString& Text)
{
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	float Widest = 0.0f;
	TArray<FString> Segments;
	Text.ParseIntoArrayWS(Segments);
	for (const FString& Segment : Segments)
	{
		Widest = FMath::Max(
			Widest,
			FontMeasure->Measure(
				Segment,
				FAppStyle::Get().GetFontStyle("NormalFontBold")).X);
	}
	return Widest;
}

FVector2D CubicPoint(
	const FVector2D& Start,
	const FVector2D& ControlA,
	const FVector2D& ControlB,
	const FVector2D& End,
	const float T)
{
	const float OneMinusT = 1.0f - T;
	return OneMinusT * OneMinusT * OneMinusT * Start +
		3.0f * OneMinusT * OneMinusT * T * ControlA +
		3.0f * OneMinusT * T * T * ControlB + T * T * T * End;
}

TArray<FVector2D> SmoothRail(
	const FVector2D& Start,
	const FVector2D& End,
	const float TurnStartX,
	const float TurnEndX)
{
	TArray<FVector2D> Points;
	const FVector2D CurveStart(TurnStartX, Start.Y);
	const FVector2D ControlA(TurnEndX, Start.Y);
	const FVector2D ControlB(FMath::Max(TurnEndX, End.X - 12.0f), End.Y);
	constexpr int32 Segments = 18;
	Points.Reserve(Segments + 2);
	Points.Add(Start);
	Points.Add(CurveStart);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		Points.Add(CubicPoint(
			CurveStart,
			ControlA,
			ControlB,
			End,
			static_cast<float>(Index) / Segments));
	}
	return Points;
}
} // namespace

bool FBlueprintLensLC2GuardSurfaceLayout::HasNoLabelIntersections() const
{
	for (int32 A = 0; A < Labels.Num(); ++A)
	{
		for (int32 B = A + 1; B < Labels.Num(); ++B)
		{
			if (StrictlyIntersects(Labels[A].ExclusionBounds, Labels[B].ExclusionBounds))
			{
				return false;
			}
		}
	}
	return true;
}

int32 FBlueprintLensLC2GuardSurfaceLayout::DrawnOutcomeCount() const
{
	return Rails.FilterByPredicate(
		[](const FBlueprintLensLC2GuardSurfaceRail& Rail)
		{
			return !Rail.bFolded;
		}).Num();
}

// A drawn outcome that stops short of the dock states the opposite of the truth:
// the reader sees an outcome that never reconverges. Nothing checked this before,
// so the compact band shipped with three dangling rails.
bool FBlueprintLensLC2GuardSurfaceLayout::EveryDrawnRailReachesCriterion() const
{
	if (!CriterionDockBounds.bIsValid)
	{
		return false;
	}
	for (const FBlueprintLensLC2GuardSurfaceRail& Rail : Rails)
	{
		if (Rail.bFolded)
		{
			continue;
		}
		if (Rail.Points.Num() < 2 ||
			!CriterionDockBounds.IsInsideOrOn(Rail.Points.Last()))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC2GuardSurfaceLayout::HasNoRailObstacleIntersections() const
{
	for (const FBlueprintLensLC2GuardSurfaceRail& Rail : Rails)
	{
		for (int32 Index = 1; Index < Rail.Points.Num(); ++Index)
		{
			for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Labels)
			{
				if (Label.Key == Rail.GroupId)
				{
					continue;
				}
				if (SegmentIntersects(Rail.Points[Index - 1], Rail.Points[Index],
					Label.ExclusionBounds))
				{
					return false;
				}
			}
			for (const FBlueprintLensLC2GuardSurfaceGate& Gate : Gates)
			{
				if (GateContainsRail(Gate, Rail, Gates))
				{
					continue;
				}
				if (SegmentIntersects(Rail.Points[Index - 1], Rail.Points[Index],
					Gate.FocusBounds))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FBlueprintLensLC2GuardSurfaceLayout::HasNoEntryRouteLabelIntersections() const
{
	for (int32 Index = 1; Index < EntryRoutePoints.Num(); ++Index)
	{
		for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Labels)
		{
			if (Label.Key != TEXT("begin") &&
				SegmentIntersects(
					EntryRoutePoints[Index - 1],
					EntryRoutePoints[Index],
					Label.ExclusionBounds))
			{
				return false;
			}
		}
	}
	return true;
}

FString FBlueprintLensLC2GuardSurfaceLayout::FirstIntersectionDiagnostic() const
{
	for (int32 A = 0; A < Labels.Num(); ++A)
	{
		for (int32 B = A + 1; B < Labels.Num(); ++B)
		{
			if (StrictlyIntersects(Labels[A].ExclusionBounds, Labels[B].ExclusionBounds))
			{
				return FString::Printf(
					TEXT("LABEL:%s:%s"),
					*Labels[A].Key,
					*Labels[B].Key);
			}
		}
	}
	for (const FBlueprintLensLC2GuardSurfaceRail& Rail : Rails)
	{
		for (int32 Index = 1; Index < Rail.Points.Num(); ++Index)
		{
			for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Labels)
			{
				if (Label.Key != Rail.GroupId &&
					SegmentIntersects(Rail.Points[Index - 1], Rail.Points[Index],
						Label.ExclusionBounds))
				{
					return FString::Printf(
						TEXT("RAIL_LABEL:%s:%s"), *Rail.GroupId, *Label.Key);
				}
			}
			for (const FBlueprintLensLC2GuardSurfaceGate& Gate : Gates)
			{
				if (!GateContainsRail(Gate, Rail, Gates) &&
					SegmentIntersects(Rail.Points[Index - 1], Rail.Points[Index],
						Gate.FocusBounds))
				{
					return FString::Printf(
						TEXT("RAIL_GATE:%s:%s"), *Rail.GroupId, *Gate.GroupId);
				}
			}
		}
	}
	for (int32 Index = 1; Index < EntryRoutePoints.Num(); ++Index)
	{
		for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Labels)
		{
			if (Label.Key != TEXT("begin") &&
				SegmentIntersects(
					EntryRoutePoints[Index - 1],
					EntryRoutePoints[Index],
					Label.ExclusionBounds))
			{
				return FString::Printf(
					TEXT("ENTRY_LABEL:%s"), *Label.Key);
			}
		}
	}
	return FString();
}

bool FBlueprintLensLC2GuardSurfaceLayout::IsRenderable(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC2_GUARD_SURFACE_LAYOUT_COMPLETE") &&
		Projection.IsRenderable() && CanonicalUnitIds == Projection.AllUnitIds &&
		Gates.Num() == 2 && Rails.Num() == 3 && ForkMarks.Num() == 2 &&
		EntryRoutePoints.Num() >= 2 && CriterionDockBounds.bIsValid &&
		EveryDrawnRailReachesCriterion() &&
		HasNoLabelIntersections() && HasNoRailObstacleIntersections() &&
		HasNoEntryRouteLabelIntersections() &&
		(DrawnOutcomeCount() + OutcomeFold.FoldedOutcomeGroupIds.Num() ==
			Projection.OutcomeRails.Num()) &&
		(Rails.FilterByPredicate(
			[](const FBlueprintLensLC2GuardSurfaceRail& Rail)
			{
				return Rail.bFolded;
			}).Num() == OutcomeFold.FoldedOutcomeGroupIds.Num()) &&
		(OutcomeFold.FoldedOutcomeGroupIds.IsEmpty()
			? OutcomeFold.ReaderText.IsEmpty()
			: !OutcomeFold.ReaderText.IsEmpty());
}

FBlueprintLensLC2GuardSurfaceLayout FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection,
	const FBlueprintLensLC2GuardLayoutSessionResult& Session,
	const float TargetWidth,
	const FString& SelectedGuardGroupId)
{
	FBlueprintLensLC2GuardSurfaceLayout Result;
	if (!Projection.IsRenderable() || !Session.IsRenderable(Projection) ||
		TargetWidth < 430.0f ||
		(!SelectedGuardGroupId.IsEmpty() && Projection.FindCompound(SelectedGuardGroupId) == nullptr))
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_LAYOUT_UNAVAILABLE");
		return Result;
	}
	Result.CanvasSize = Session.Layout.CanvasSize;
	Result.CanonicalUnitIds = Projection.AllUnitIds;
	Result.SelectedGuardGroupId = SelectedGuardGroupId;
	Result.BaseLedgerFingerprint = Session.Layout.LayoutLedger.ConfigurationFingerprint;
	const FBlueprintLensLC2GuardCompound* Outer =
		Projection.Compounds.FindByPredicate(
			[](const FBlueprintLensLC2GuardCompound& Compound)
			{
				return Compound.ParentGroupId.IsEmpty();
			});
	const FBlueprintLensLC2GuardCompound* Inner =
		Projection.Compounds.FindByPredicate(
			[](const FBlueprintLensLC2GuardCompound& Compound)
			{
				return !Compound.ParentGroupId.IsEmpty();
			});
	const FBlueprintLensLC2GuardCanonicalUnit* Criterion =
		Projection.FindCanonicalUnit(Projection.CriterionUnitId);
	if (Outer == nullptr || Inner == nullptr || Criterion == nullptr)
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_GATE_BINDING_INVALID");
		return Result;
	}

	const bool bWide = TargetWidth >= 680.0f;
	Result.CanvasSize = FVector2D(TargetWidth, bWide ? 420.0f : 610.0f);
	const float Right = TargetWidth - 16.0f;
	FBlueprintLensLC2GuardSurfaceGate OuterGate;
	OuterGate.GroupId = Outer->GroupId;
	OuterGate.ParentGroupId = Outer->ParentGroupId;
	OuterGate.PredicateUnitId = Outer->PredicateUnitId;
	OuterGate.BranchUnitId = Outer->BranchUnitId;
	OuterGate.Bounds = bWide
		? FBox2D(FVector2D(176.0f, 48.0f), FVector2D(512.0f, 370.0f))
		: FBox2D(FVector2D(16.0f, 72.0f), FVector2D(Right, 444.0f));
	OuterGate.bSelected = SelectedGuardGroupId == OuterGate.GroupId;
	const float OuterFocus = OuterGate.bSelected ? 12.0f : 6.0f;
	OuterGate.FocusBounds = FBox2D(
		OuterGate.Bounds.Min - FVector2D(OuterFocus, OuterFocus),
		OuterGate.Bounds.Max + FVector2D(OuterFocus, OuterFocus));
	Result.Gates.Add(OuterGate);

	FBlueprintLensLC2GuardSurfaceGate InnerGate;
	InnerGate.GroupId = Inner->GroupId;
	InnerGate.ParentGroupId = Inner->ParentGroupId;
	InnerGate.PredicateUnitId = Inner->PredicateUnitId;
	InnerGate.BranchUnitId = Inner->BranchUnitId;
	InnerGate.Bounds = bWide
		? FBox2D(FVector2D(222.0f, 182.0f), FVector2D(500.0f, 360.0f))
		: FBox2D(FVector2D(32.0f, 222.0f), FVector2D(Right - 16.0f, 420.0f));
	InnerGate.bSelected = SelectedGuardGroupId == InnerGate.GroupId;
	const float InnerFocus = InnerGate.bSelected ? 12.0f : 6.0f;
	InnerGate.FocusBounds = FBox2D(
		InnerGate.Bounds.Min - FVector2D(InnerFocus, InnerFocus),
		InnerGate.Bounds.Max + FVector2D(InnerFocus, InnerFocus));
	Result.Gates.Add(InnerGate);

	const FBlueprintLensLC2GuardCanonicalUnit* Begin =
		Projection.CanonicalUnits.FindByPredicate(
			[](const FBlueprintLensLC2GuardCanonicalUnit& Unit)
			{
				return Unit.OwnerGuardGroupId.IsEmpty() && !Unit.bIsCriterion;
			});
	if (Begin != nullptr)
	{
		AddLabel(Result, TEXT("begin"), Begin->UnitId, Begin->ReaderLabel,
			bWide ? FVector2D(38.0f, 184.0f) : FVector2D(32.0f, 20.0f),
			FVector2D(100.0f, 22.0f));
	}
	const FString OuterGateText = GateLabelText(*Outer);
	const FString InnerGateText = GateLabelText(*Inner);
	AddLabel(Result, TEXT("outer-gate"), Outer->BranchUnitId,
		OuterGateText,
		bWide ? FVector2D(194.0f, 64.0f) : FVector2D(32.0f, 86.0f),
		GateLabelSize(OuterGateText));
	AddLabel(Result, TEXT("inner-gate"), Inner->BranchUnitId,
		InnerGateText,
		bWide ? FVector2D(240.0f, 198.0f) : FVector2D(48.0f, 236.0f),
		GateLabelSize(InnerGateText));
	if (OuterGate.bSelected)
	{
		const FBlueprintLensLC2GuardCanonicalUnit* Predicate =
			Projection.FindCanonicalUnit(Outer->PredicateUnitId);
		if (Predicate != nullptr)
		{
			const FString Text = PredicateLabelText(*Predicate);
			AddLabel(Result, TEXT("outer-predicate"), Predicate->UnitId,
				Text,
				bWide ? FVector2D(194.0f, 90.0f) : FVector2D(32.0f, 112.0f),
				PredicateLabelSize(Text));
		}
	}
	if (InnerGate.bSelected)
	{
		const FBlueprintLensLC2GuardCanonicalUnit* Predicate =
			Projection.FindCanonicalUnit(Inner->PredicateUnitId);
		if (Predicate != nullptr)
		{
			const FString Text = PredicateLabelText(*Predicate);
			AddLabel(Result, TEXT("inner-predicate"), Predicate->UnitId,
				Text,
				bWide ? FVector2D(240.0f, 224.0f) : FVector2D(48.0f, 262.0f),
				PredicateLabelSize(Text));
		}
	}
	const float OuterForkY = bWide
		? (OuterGate.bSelected ? 116.0f : 92.0f)
		: (OuterGate.bSelected ? 138.0f : 136.0f);
	const float InnerForkY = bWide
		? (InnerGate.bSelected ? 250.0f : 226.0f)
		: (InnerGate.bSelected ? 288.0f : 286.0f);
	AddLabel(Result, TEXT("outer-fork"), FString(), TEXT("fork · unordered"),
		FVector2D(bWide ? 228.0f : 66.0f, OuterForkY),
		FVector2D(112.0f, 18.0f));
	AddLabel(Result, TEXT("inner-fork"), FString(), TEXT("fork · unordered"),
		FVector2D(bWide ? 274.0f : 82.0f, InnerForkY),
		FVector2D(112.0f, 18.0f));
	const float WideCriterionWidth =
		FMath::Max(112.0f, WidestUnbreakableLabelSegment(Criterion->ReaderLabel));
	const float WideCriterionX = TargetWidth - 1.0f - WideCriterionWidth;
	AddLabel(Result, TEXT("criterion"), Criterion->UnitId,
		FString::Printf(TEXT("CRITERION\n%s"), *Criterion->ReaderLabel),
		bWide ? FVector2D(WideCriterionX, 185.0f) : FVector2D(36.0f, 512.0f),
		FVector2D(bWide ? WideCriterionWidth : 150.0f, 52.0f));
	Result.EntryRoutePoints = bWide
		? TArray<FVector2D>{FVector2D(142.0f, 195.0f), FVector2D(176.0f, 195.0f)}
		: TArray<FVector2D>{FVector2D(48.0f, 48.0f), FVector2D(48.0f, 72.0f)};
	Result.CriterionDockBounds = bWide
		? FBox2D(
			FVector2D(WideCriterionX - 4.0f, 172.0f),
			FVector2D(TargetWidth - 1.0f, 252.0f))
		: FBox2D(FVector2D(24.0f, 496.0f), FVector2D(202.0f, 580.0f));

	for (int32 Index = 0; Index < Projection.OutcomeRails.Num(); ++Index)
	{
		const FBlueprintLensLC2GuardOutcomeRail& Source = Projection.OutcomeRails[Index];
		FBlueprintLensLC2GuardSurfaceRail Rail;
		Rail.GroupId = Source.GroupId;
		Rail.OutcomeUnitId = Source.OutcomeUnitId;
		const FBlueprintLensLC2GuardCanonicalUnit* Outcome =
			Projection.FindCanonicalUnit(Source.OutcomeUnitId);
		if (Outcome == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_RAIL_BINDING_INVALID");
			return Result;
		}
		Rail.OwnerGuardGroupId = Outcome->OwnerGuardGroupId;
		Rail.bFolded = false;
		const bool bOuterOutcome = Source.GroupId.Contains(TEXT("outer_rejected"));
		const bool bInnerOutcome = Source.GroupId.Contains(TEXT("inner_rejected"));
		const FString Prefix = bOuterOutcome
			? TEXT("Outer false · ")
			: bInnerOutcome ? TEXT("Inner false · ") : TEXT("Both pass · ");
		if (bWide)
		{
			const float LabelY = bOuterOutcome ? 124.0f : bInnerOutcome ? 268.0f : 322.0f;
			const float RailY = LabelY + 26.0f;
			AddLabel(Result, Source.GroupId, Source.OutcomeUnitId,
				Prefix + Outcome->ReaderLabel,
				FVector2D(342.0f, LabelY),
				FVector2D(176.0f, 20.0f));
			const FVector2D RailStart(
				bOuterOutcome ? 210.0f : 252.0f,
				RailY);
			const FVector2D RailEnd(
				WideCriterionX - 4.0f,
				bOuterOutcome ? 174.0f : bInnerOutcome ? 210.0f : 230.0f);
			Rail.Points = SmoothRail(
				RailStart,
				RailEnd,
				WideCriterionX - 8.0f,
				WideCriterionX - 4.0f);
		}
		else
		{
			const float LabelY = bOuterOutcome ? 166.0f : bInnerOutcome ? 316.0f : 366.0f;
			const float RailY = LabelY + 26.0f;
			AddLabel(Result, Source.GroupId, Source.OutcomeUnitId,
				Prefix + Outcome->ReaderLabel,
				FVector2D(bOuterOutcome ? 130.0f : 146.0f, LabelY),
				FVector2D(TargetWidth - (bOuterOutcome ? 162.0f : 178.0f), 20.0f));
			// Descend in the strip right of the inner gate's widest focus fence
			// (TargetWidth - 20), which is the only lane a rail that gate does not
			// own may use, then run back under the criterion caption into the dock's
			// right edge. The three turns and lanes are staggered so the shared
			// reconvergence stays three distinct paths.
			const float TurnX = TargetWidth -
				(bOuterOutcome ? 14.0f : bInnerOutcome ? 9.0f : 4.0f);
			const float LaneY = bOuterOutcome ? 568.0f : bInnerOutcome ? 572.0f : 576.0f;
			Rail.Points = {
				FVector2D(bOuterOutcome ? 48.0f : 64.0f, RailY),
				FVector2D(TurnX, RailY),
				FVector2D(TurnX, LaneY),
				FVector2D(Result.CriterionDockBounds.Max.X, LaneY)};
		}
		Result.Rails.Add(MoveTemp(Rail));
	}
	// The fold is accounted from what this layout actually placed, so the reader
	// budget can never restate the drawn count as if it were the truth count.
	for (const FBlueprintLensLC2GuardOutcomeRail& Source : Projection.OutcomeRails)
	{
		const FBlueprintLensLC2GuardSurfaceRail* Placed =
			Result.Rails.FindByPredicate(
				[&Source](const FBlueprintLensLC2GuardSurfaceRail& Rail)
				{
					return Rail.GroupId == Source.GroupId;
				});
		if (Placed == nullptr || Placed->bFolded)
		{
			Result.OutcomeFold.FoldedOutcomeGroupIds.Add(Source.GroupId);
		}
	}
	if (Result.DrawnOutcomeCount() + Result.OutcomeFold.FoldedOutcomeGroupIds.Num() !=
		Projection.OutcomeRails.Num())
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_OUTCOME_FOLD_UNACCOUNTED");
		return Result;
	}
	if (!Result.OutcomeFold.FoldedOutcomeGroupIds.IsEmpty())
	{
		const FBlueprintLensLC2GuardCanonicalUnit* FoldCriterion =
			Projection.FindCanonicalUnit(Projection.CriterionUnitId);
		if (FoldCriterion == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_CRITERION_BINDING_INVALID");
			return Result;
		}
		Result.OutcomeFold.ReaderText = FString::Printf(
			TEXT("%d more outcomes · all exit into %s · none ordered against the drawn ones"),
			Result.OutcomeFold.FoldedOutcomeGroupIds.Num(),
			*FoldCriterion->ReaderLabel);
	}
	Result.ForkMarks = Projection.ForkMarks;
	if (Result.ForkMarks.Num() != 2)
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_FORK_MARK_MISSING");
		return Result;
	}
	if (!Result.EveryDrawnRailReachesCriterion())
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_RECONVERGENCE_NOT_DRAWN");
		return Result;
	}
	if (!Result.HasNoLabelIntersections() || !Result.HasNoRailObstacleIntersections() ||
		!Result.HasNoEntryRouteLabelIntersections())
	{
		Result.DiagnosticCode = FString::Printf(
			TEXT("LC2_GUARD_SURFACE_HARD_OBSTACLE_INTERSECTION:%s"),
			*Result.FirstIntersectionDiagnostic());
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_LAYOUT_COMPLETE");
	return Result;
}
