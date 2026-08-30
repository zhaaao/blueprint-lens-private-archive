#include "BlueprintLensLC4AsyncLayout.h"

#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
FBox2D Box(const float X, const float Y, const float W, const float H)
{
	return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
}

FVector2D BoxSize(const FBox2D& Value)
{
	return Value.Max - Value.Min;
}

EBlueprintLensLC4AsyncLayoutMode ModeForWidth(const float Width)
{
	if (Width <= 455.0f)
	{
		return EBlueprintLensLC4AsyncLayoutMode::Narrow430;
	}
	return Width <= 590.0f
		? EBlueprintLensLC4AsyncLayoutMode::Compact480
		: EBlueprintLensLC4AsyncLayoutMode::Wide700;
}

float CanonicalWidth(const EBlueprintLensLC4AsyncLayoutMode Mode)
{
	switch (Mode)
	{
	case EBlueprintLensLC4AsyncLayoutMode::Narrow430: return 430.0f;
	case EBlueprintLensLC4AsyncLayoutMode::Compact480: return 480.0f;
	default: return 700.0f;
	}
}

float CanonicalHeight(const EBlueprintLensLC4AsyncLayoutMode Mode)
{
	switch (Mode)
	{
	case EBlueprintLensLC4AsyncLayoutMode::Narrow430: return 1148.0f;
	case EBlueprintLensLC4AsyncLayoutMode::Compact480: return 1134.0f;
	default: return 1050.0f;
	}
}

FVector2D MeasureText(
	const FString& Text,
	const int32 FontSize,
	const EBlueprintLensLC4AsyncFontWeight Weight)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC4 async layout requires Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Slate's glyph rasterization can extend a few pixels beyond the nominal
	// advance width (especially for bold capitals at non-integer DPI scales).
	// Keep an explicit breathing margin so ClipToBoundsAlways never trims the
	// last glyph during exact-width Editor review.
	return FontMeasure->Measure(Text, BlueprintLensLC4AsyncFont(FontSize, Weight)) +
		FVector2D(12.0f, 4.0f);
}

bool SameLedger(const FBlueprintLensLayoutLedger& Actual, const FBlueprintLensLayoutLedger& Expected, const float Tolerance)
{
	if (!Actual.CanvasSize.Equals(Expected.CanvasSize, Tolerance) || Actual.Nodes.Num() != Expected.Nodes.Num() ||
		Actual.Ports.Num() != Expected.Ports.Num() || Actual.Edges.Num() != Expected.Edges.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : Expected.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Found = Actual.Nodes.FindByPredicate(
			[&Node](const FBlueprintLensLayoutNodePlacement& Candidate){ return Candidate.UnitId == Node.UnitId; });
		if (Found == nullptr || !Found->Position.Equals(Node.Position, Tolerance) || !Found->Size.Equals(Node.Size, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : Expected.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Found = Actual.FindPort(Port.UnitId, Port.Label, Port.bInput);
		if (Found == nullptr || !Found->Position.Equals(Port.Position, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Expected.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Found = Actual.Edges.FindByPredicate(
			[&Edge](const FBlueprintLensLayoutEdgePlacement& Candidate){ return Candidate.RelationId == Edge.RelationId; });
		if (Found == nullptr || Found->BendPoints.Num() != Edge.BendPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Edge.BendPoints.Num(); ++Index)
		{
			if (!Found->BendPoints[Index].Equals(Edge.BendPoints[Index], Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}

bool SegmentIntersects(const FVector2D& A, const FVector2D& B, const FBox2D& Bounds)
{
	if (!Bounds.bIsValid)
	{
		return false;
	}
	const FVector2D Delta = B - A;
	float Enter = 0.0f;
	float Leave = 1.0f;
	const float P[4] = {-Delta.X, Delta.X, -Delta.Y, Delta.Y};
	const float Q[4] = {A.X - Bounds.Min.X, Bounds.Max.X - A.X, A.Y - Bounds.Min.Y, Bounds.Max.Y - A.Y};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (FMath::IsNearlyZero(P[Index]))
		{
			if (Q[Index] < 0.0f) return false;
			continue;
		}
		const float Ratio = Q[Index] / P[Index];
		if (P[Index] < 0.0f) Enter = FMath::Max(Enter, Ratio);
		else Leave = FMath::Min(Leave, Ratio);
		if (Enter > Leave) return false;
	}
	return true;
}

bool BoxesOverlapWithPositiveArea(const FBox2D& Left, const FBox2D& Right)
{
	return Left.Min.X < Right.Max.X - KINDA_SMALL_NUMBER &&
		Left.Max.X > Right.Min.X + KINDA_SMALL_NUMBER &&
		Left.Min.Y < Right.Max.Y - KINDA_SMALL_NUMBER &&
		Left.Max.Y > Right.Min.Y + KINDA_SMALL_NUMBER;
}
} // namespace

FSlateFontInfo BlueprintLensLC4AsyncFont(
	const int32 FontSize,
	const EBlueprintLensLC4AsyncFontWeight Weight)
{
	// The frozen effect images use CSS pixel sizes. Slate font Size is in
	// points and converts at 96 DPI, so px * 72 / 96 preserves the target's
	// actual glyph scale while the layout contract retains its nominal pixels.
	const float SlatePointSize = static_cast<float>(FontSize) * 0.75f;
	const TCHAR* Typeface =
		Weight == EBlueprintLensLC4AsyncFontWeight::Bold ? TEXT("Bold") :
		Weight == EBlueprintLensLC4AsyncFontWeight::Semibold ? TEXT("SemiBold") :
		TEXT("Regular");
	const FString FontName =
		Weight == EBlueprintLensLC4AsyncFontWeight::Bold ? TEXT("segoeuib.ttf") :
		Weight == EBlueprintLensLC4AsyncFontWeight::Semibold ? TEXT("seguisb.ttf") :
		TEXT("segoeui.ttf");
	const FString FontPath = FPaths::Combine(
		FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")),
		TEXT("Fonts"),
		FontName);
	if (!FPaths::FileExists(FontPath))
	{
		return FCoreStyle::GetDefaultFontStyle(Typeface, SlatePointSize);
	}
	const FString SymbolFontPath = FPaths::Combine(
		FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")),
		TEXT("Fonts"),
		TEXT("seguisym.ttf"));
	const int32 WeightIndex =
		Weight == EBlueprintLensLC4AsyncFontWeight::Regular ? 0 :
		Weight == EBlueprintLensLC4AsyncFontWeight::Semibold ? 1 : 2;
	static TSharedPtr<FCompositeFont> CompositeFonts[3];
	if (!CompositeFonts[WeightIndex].IsValid())
	{
		TSharedRef<FCompositeFont> Composite = MakeShared<FCompositeFont>(
			FName(Typeface),
			FontPath,
			EFontHinting::Default,
			EFontLoadingPolicy::LazyLoad);
		if (FPaths::FileExists(SymbolFontPath))
		{
			Composite->FallbackTypeface.Typeface = FTypeface(
				TEXT("Symbol"),
				SymbolFontPath,
				EFontHinting::Default,
				EFontLoadingPolicy::LazyLoad);
		}
		CompositeFonts[WeightIndex] = Composite;
	}
	return FSlateFontInfo(
		CompositeFonts[WeightIndex],
		SlatePointSize,
		FName(Typeface));
}

bool FBlueprintLensLC4AsyncLayout::CoversProjection(const FBlueprintLensLC4AsyncProjection& Projection) const
{
	return Projection.IsRenderable() && LayoutRequest.Nodes.Num() == 9 && LayoutRequest.Edges.Num() == 8 &&
		Projection.AllRelationIds.Num() == 22 && Projection.Proofs.Num() == 2;
}

bool FBlueprintLensLC4AsyncLayout::HasValidSharedLedger() const
{
	return DiagnosticCode == TEXT("LC4_ASYNC_LAYOUT_COMPLETE") &&
		LayoutLedger.IsCompleteFor(LayoutRequest) && LayoutLedger.CanvasSize.Equals(CanvasSize, 0.5f);
}

bool FBlueprintLensLC4AsyncLayout::MatchesVisualOracle(const float Tolerance) const
{
	return SameLedger(LayoutLedger, VisualOracleLedger, Tolerance);
}

bool FBlueprintLensLC4AsyncLayout::HasNoTextOrRouteCollisions() const
{
	const FBox2D Canvas(FVector2D::ZeroVector, CanvasSize);
	for (int32 Left = 0; Left < ProtectedLabelBounds.Num(); ++Left)
	{
		if (!ProtectedLabelBounds[Left].bIsValid || !Canvas.IsInsideOrOn(ProtectedLabelBounds[Left].Min) ||
			!Canvas.IsInsideOrOn(ProtectedLabelBounds[Left].Max))
		{
			return false;
		}
		for (int32 Right = Left + 1; Right < ProtectedLabelBounds.Num(); ++Right)
		{
			if (BoxesOverlapWithPositiveArea(
					ProtectedLabelBounds[Left],
					ProtectedLabelBounds[Right]))
			{
				return false;
			}
		}
	}
	for (const TArray<FVector2D>& Route : PaintedRoutes)
	{
		for (int32 Segment = 0; Segment + 1 < Route.Num(); ++Segment)
		{
			for (const FBox2D& LabelBounds : ProtectedLabelBounds)
			{
				if (SegmentIntersects(Route[Segment], Route[Segment + 1], LabelBounds)) return false;
			}
		}
	}
	return true;
}

FBlueprintLensLC4AsyncLayout FBlueprintLensLC4AsyncLayoutBuilder::Build(
	const FBlueprintLensLC4AsyncProjection& Projection,
	const float TargetWidth)
{
	FBlueprintLensLC4AsyncLayout Result;
	Result.Mode = ModeForWidth(TargetWidth);
	const float Width = CanonicalWidth(Result.Mode);
	const float Height = CanonicalHeight(Result.Mode);
	const float Pad = Width == 430.0f ? 20.0f : Width == 480.0f ? 24.0f : 28.0f;
	const float XA = FMath::RoundToFloat(Width * (Width < 700.0f ? 0.31f : 232.0f / 700.0f));
	const float XB = FMath::RoundToFloat(Width * (Width < 700.0f ? 0.69f : 468.0f / 700.0f));
	const float Center = Width * 0.5f;
	const bool bNarrow = Width < 700.0f;
	const float RuleY = Width == 430.0f ? 166.0f : 152.0f;
	const float SourceY = RuleY + 68.0f;
	const float ContinuationY = SourceY + 152.0f;
	const float CompletionY = ContinuationY + 126.0f;
	const float ArrivalY = CompletionY + (bNarrow ? 190.0f : 106.0f);
	const float BarrierY = ArrivalY + 82.0f;
	const float CriterionY = BarrierY + 94.0f;
	const float FrontierY = CriterionY + 82.0f;
	const float ActionsY = FrontierY + 108.0f;
	Result.CanvasSize = FVector2D(Width, Height);
	Result.HeaderRuleY = RuleY;
	Result.LaunchA = FVector2D(XA, SourceY);
	Result.LaunchB = FVector2D(XB, SourceY);
	Result.ContinuationA = FVector2D(XA, ContinuationY);
	Result.ContinuationB = FVector2D(XB, ContinuationY);
	Result.CompletionA = FVector2D(XA, CompletionY);
	Result.CompletionB = FVector2D(XB, CompletionY);
	Result.ArrivalA = FVector2D(XA, ArrivalY);
	Result.ArrivalB = FVector2D(XB, ArrivalY);
	Result.Release = FVector2D(Center, BarrierY + 72.0f);
	Result.NarrowProofBracketY = CompletionY + 46.0f;
	Result.BarrierBounds = Box(Pad + 54.0f, BarrierY, Width - 2.0f * (Pad + 54.0f), 24.0f);
	const float CriterionWidth = FMath::Min(280.0f, Width - 2.0f * (Pad + 24.0f));
	Result.CriterionBounds = Box((Width - CriterionWidth) * 0.5f, CriterionY, CriterionWidth, 62.0f);
	Result.FrontierBounds = Box(Pad, FrontierY, Width - 2.0f * Pad, 94.0f);
	Result.ActionsBounds = Box(Pad, ActionsY, Width - 2.0f * Pad, 58.0f);
	if (!Projection.IsRenderable())
	{
		Result.DiagnosticCode = TEXT("LC4_ASYNC_LAYOUT_PROJECTION_UNAVAILABLE");
		return Result;
	}

	struct FNodeSpec { FString Id; FBox2D Bounds; };
	const TArray<FNodeSpec> Nodes = {
		{TEXT("source.sequence"), Box(Pad + 17.0f, SourceY - 7.0f, 14.0f, 14.0f)},
		{TEXT("source.launch.A"), Box(XA - 9.0f, SourceY - 9.0f, 18.0f, 18.0f)},
		{TEXT("source.launch.B"), Box(XB - 9.0f, SourceY - 9.0f, 18.0f, 18.0f)},
		{TEXT("dag.continuation.A"), Box(XA - 12.0f, ContinuationY - 12.0f, 24.0f, 24.0f)},
		{TEXT("dag.continuation.B"), Box(XB - 12.0f, ContinuationY - 12.0f, 24.0f, 24.0f)},
		{TEXT("dag.arrival.A"), Box(XA - 12.0f, ArrivalY - 12.0f, 24.0f, 24.0f)},
		{TEXT("dag.arrival.B"), Box(XB - 12.0f, ArrivalY - 12.0f, 24.0f, 24.0f)},
		{TEXT("barrier.and"), Result.BarrierBounds},
		{TEXT("criterion.dock"), Result.CriterionBounds}};
	Result.LayoutRequest.GraphKey = FString::Printf(TEXT("LC4ASYNC:%s"), *Projection.ProjectionIntegrityHash);
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = Width;
	for (const FNodeSpec& Spec : Nodes)
	{
		FBlueprintLensLayoutNodeRequest Node;
		Node.UnitId = Spec.Id;
		Node.DesiredSize = BoxSize(Spec.Bounds);
		Node.Ports = {{TEXT("in"), true, 0}, {TEXT("out"), false, 0}};
		Result.LayoutRequest.Nodes.Add(MoveTemp(Node));
		Result.LayoutLedger.Nodes.Add({Spec.Id, Spec.Bounds.Min, BoxSize(Spec.Bounds)});
		Result.LayoutLedger.Ports.Add({Spec.Id, TEXT("in"), true, FVector2D((Spec.Bounds.Min.X + Spec.Bounds.Max.X) * 0.5f, Spec.Bounds.Min.Y)});
		Result.LayoutLedger.Ports.Add({Spec.Id, TEXT("out"), false, FVector2D((Spec.Bounds.Min.X + Spec.Bounds.Max.X) * 0.5f, Spec.Bounds.Max.Y)});
	}
	auto AddEdge = [&Result](const FString& Id, const FString& Source, const FString& Target, const TArray<FVector2D>& Bends)
	{
		Result.LayoutRequest.Edges.Add({Id, Source, Target, TEXT("out"), TEXT("in"), EBlueprintLensLayoutRelationFamily::Execution, true});
		Result.LayoutLedger.Edges.Add({Id, Source, Target, TEXT("out"), TEXT("in"), EBlueprintLensLayoutRelationFamily::Execution, Bends});
	};
	AddEdge(TEXT("launch-order-A-B"), TEXT("source.launch.A"), TEXT("source.launch.B"), {FVector2D(XA, SourceY + 48.0f), FVector2D(XB, SourceY + 48.0f)});
	AddEdge(TEXT("launch-to-continuation-A"), TEXT("source.launch.A"), TEXT("dag.continuation.A"), {});
	AddEdge(TEXT("launch-to-continuation-B"), TEXT("source.launch.B"), TEXT("dag.continuation.B"), {});
	AddEdge(TEXT("continuation-to-arrival-A"), TEXT("dag.continuation.A"), TEXT("dag.arrival.A"), {Result.CompletionA});
	AddEdge(TEXT("continuation-to-arrival-B"), TEXT("dag.continuation.B"), TEXT("dag.arrival.B"), {Result.CompletionB});
	AddEdge(TEXT("arrival-to-barrier-A"), TEXT("dag.arrival.A"), TEXT("barrier.and"), {});
	AddEdge(TEXT("arrival-to-barrier-B"), TEXT("dag.arrival.B"), TEXT("barrier.and"), {});
	AddEdge(TEXT("release-to-criterion"), TEXT("barrier.and"), TEXT("criterion.dock"), {Result.Release});
	Result.LayoutLedger.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Result.LayoutLedger.BackendVersion = TEXT("BlueprintLens.DeterministicLC4AsyncPartialOrder.v1");
	Result.LayoutLedger.ConfigurationFingerprint = FString::Printf(TEXT("profile=LayeredPorts;width=%.0f;oracle=v1"), Width);
	Result.LayoutLedger.CanvasSize = Result.CanvasSize;
	Result.LayoutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	Result.VisualOracleLedger = Result.LayoutLedger;

	auto AddLabel = [&Result](
		const FString& Id,
		const FString& Text,
		const FVector2D& Anchor,
		const int32 FontSize,
		const bool bProtected,
		const float HorizontalAlignment = 0.0f,
		const EBlueprintLensLC4AsyncFontWeight Weight =
			EBlueprintLensLC4AsyncFontWeight::Regular)
	{
		const FVector2D MeasuredSize = MeasureText(Text, FontSize, Weight);
		const FVector2D GlyphSize = MeasuredSize - FVector2D(12.0f, 4.0f);
		const FVector2D ProtectedSize = GlyphSize + FVector2D(2.0f, 2.0f);
		const FVector2D Position(
			Anchor.X - GlyphSize.X * HorizontalAlignment,
			Anchor.Y);
		FBlueprintLensLC4AsyncLabel Label{
			Id,
			Text,
			Position,
			MeasuredSize,
			FontSize,
			Weight};
		Result.Labels.Add(Label);
		if (bProtected)
		{
			Result.ProtectedLabelBounds.Add(FBox2D(Position, Position + ProtectedSize));
		}
	};
	AddLabel(TEXT("header.eyebrow"), TEXT("LC4 · ASYNC · SELECTED"), FVector2D(Pad, 18.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("title"), TEXT("Partial-Order Join"), FVector2D(Pad, 45.0f), Width == 430.0f ? 22 : 24, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(
		TEXT("question.0"),
		Width == 430.0f ? TEXT("Why only after both complete —") : TEXT("Why only after both complete — and can either finish first?"),
		FVector2D(Pad, 86.0f),
		12,
		true);
	if (Width == 430.0f)
	{
		AddLabel(TEXT("question.1"), TEXT("and can either finish first?"), FVector2D(Pad, 104.0f), 12, true);
	}
	const FString VariantLabel = Projection.Variant == TEXT("A_FIRST")
		? TEXT("A FIRST")
		: TEXT("B FIRST");
	AddLabel(TEXT("variant"), FString::Printf(TEXT("%s · %.0f"), *VariantLabel, Width), FVector2D(Width - Pad, 18.0f), 10, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("counts"), TEXT("2 runs · 16 events · 22 relations · 2 proofs · 4 boundaries"), FVector2D(Pad, 126.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Semibold);
	AddLabel(TEXT("dag"), TEXT("TRANSITIVE-REDUCTION DAG · validated relations only"), FVector2D(Pad, SourceY - 62.0f), 9, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("source.caption"), TEXT("SOURCE-GUARANTEED LAUNCH ORDER"), FVector2D(Pad, SourceY - 43.0f), 9, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("sequence"), TEXT("Sequence"), FVector2D(Pad, SourceY + 20.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Semibold);
	AddLabel(TEXT("launch.A"), TEXT("launch A"), FVector2D(XA, SourceY - 23.0f), 10, true, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("launch.B"), TEXT("launch B"), FVector2D(XB, SourceY - 23.0f), 10, true, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("launch.order"), TEXT("launch A → B"), FVector2D(Center, SourceY + 30.0f), 11, true, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("observed"), Projection.Variant == TEXT("A_FIRST") ? TEXT("observed A → B · not causal") : TEXT("observed B → A · not causal"), FVector2D(Center, SourceY + 91.0f), 9, true, 0.5f, EBlueprintLensLC4AsyncFontWeight::Semibold);
	const FString ContinuationALabel = bNarrow ? TEXT("A · CONT. BOUNDARY") : TEXT("A · CONTINUATION BOUNDARY");
	const FString ContinuationBLabel = bNarrow ? TEXT("B · CONT. BOUNDARY") : TEXT("B · CONTINUATION BOUNDARY");
	AddLabel(TEXT("continuation.A"), ContinuationALabel, FVector2D(XA - 16.0f, ContinuationY - 24.0f), 10, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("continuation.B"), ContinuationBLabel, FVector2D(XB + 16.0f, ContinuationY - 24.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("rank"), TEXT("same rank · no precedence"), FVector2D(Center, CompletionY - 61.0f), 9, true, 0.5f, EBlueprintLensLC4AsyncFontWeight::Semibold);
	AddLabel(TEXT("complete.A"), TEXT("complete A"), FVector2D(XA - 16.0f, CompletionY - 20.0f), 10, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("complete.B"), TEXT("complete B"), FVector2D(XB + 16.0f, CompletionY - 20.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("arrive.A"), TEXT("arrive A"), FVector2D(XA - 16.0f, ArrivalY - 20.0f), 10, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("arrive.B"), TEXT("arrive B"), FVector2D(XB + 16.0f, ArrivalY - 20.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	if (bNarrow)
	{
		AddLabel(TEXT("proof.0"), TEXT("A ↛ B"), FVector2D(Pad + 12.0f, CompletionY + 76.0f), 10, true);
		AddLabel(TEXT("proof.1"), TEXT("B ↛ A"), FVector2D(Pad + 64.0f, CompletionY + 76.0f), 10, true);
		AddLabel(TEXT("proof.set"), TEXT("relation set complete"), FVector2D(Pad + 12.0f, CompletionY + 98.0f), 10, true);
		AddLabel(TEXT("proof.complete"), TEXT("therefore A ∥ B"), FVector2D(Width - Pad - 12.0f, CompletionY + 98.0f), 11, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	}
	else
	{
		const float ProofX = Width - 126.0f;
		const float ProofY = CompletionY - 68.0f;
		AddLabel(TEXT("proof.0"), TEXT("A does not reach B"), FVector2D(ProofX, ProofY), 12, true);
		AddLabel(TEXT("proof.1"), TEXT("B does not reach A"), FVector2D(ProofX, ProofY + 28.0f), 12, true);
		AddLabel(TEXT("proof.set"), TEXT("relation set complete"), FVector2D(ProofX, ProofY + 56.0f), 12, true);
		AddLabel(TEXT("proof.complete"), TEXT("therefore A ∥ B"), FVector2D(ProofX, ProofY + 84.0f), 14, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	}
	AddLabel(TEXT("socket.A"), TEXT("socket A"), FVector2D(XA - 10.0f, BarrierY - 19.0f), 9, true, 1.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("socket.B"), TEXT("socket B"), FVector2D(XB + 10.0f, BarrierY - 19.0f), 9, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("barrier.caption"), TEXT("EXPLICIT PROJECT-OWNED JOIN"), FVector2D(Pad, BarrierY + 42.0f), 9, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("release"), TEXT("RELEASE ONCE (1)"), FVector2D(Center + 14.0f, BarrierY + 49.0f), 10, true, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("barrier"), TEXT("AND · 2/2 ARRIVED"), FVector2D(Center, BarrierY + 4.0f), 10, false, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("criterion.eyebrow"), TEXT("CRITERION · AFTER RELEASE"), FVector2D(Center, CriterionY + 10.0f), 9, false, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("criterion"), TEXT("Set LC4AsyncComplete = true"), FVector2D(Center, CriterionY + 34.0f), 12, false, 0.5f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("frontier.title"), TEXT("FRONTIER · BOUNDED POSITIVE PROFILE"), FVector2D(Pad + 14.0f, FrontierY + 14.0f), 10, false, 0.0f, EBlueprintLensLC4AsyncFontWeight::Bold);
	AddLabel(TEXT("frontier.row.0"), TEXT("observed order only · 0.050 s ticks · 8-tick deadline"), FVector2D(Pad + 14.0f, FrontierY + 40.0f), 9, false);
	AddLabel(TEXT("frontier.row.1"), TEXT("no external service · cancel/incomplete → ABSTAINED"), FVector2D(Pad + 14.0f, FrontierY + 65.0f), 9, false);
	Result.PaintedRoutes = {
		{FVector2D(Pad + 32.0f, SourceY), Result.LaunchA, Result.LaunchB},
		{FVector2D(XA, SourceY + 10.0f), FVector2D(XA, ContinuationY - 12.0f)},
		{FVector2D(XB, SourceY + 10.0f), FVector2D(XB, ContinuationY - 12.0f)},
		{FVector2D(XA, ContinuationY + 12.0f), FVector2D(XA, CompletionY - 12.0f)},
		{FVector2D(XB, ContinuationY + 12.0f), FVector2D(XB, CompletionY - 12.0f)},
		{FVector2D(XA, CompletionY + 12.0f), FVector2D(XA, ArrivalY - 12.0f)},
		{FVector2D(XB, CompletionY + 12.0f), FVector2D(XB, ArrivalY - 12.0f)},
		{FVector2D(XA, ArrivalY + 12.0f), FVector2D(XA, BarrierY - 7.0f)},
		{FVector2D(XB, ArrivalY + 12.0f), FVector2D(XB, BarrierY - 7.0f)},
		{FVector2D(Center, BarrierY + 30.0f), Result.Release, FVector2D(Center, CriterionY)}};
	const float CellWidth = (Width - 2.0f * Pad) / 5.0f;
	const TCHAR* Ids[] = {TEXT("select"), TEXT("proof"), TEXT("all-text"), TEXT("evidence"), TEXT("open-source")};
	const TCHAR* Names[] = {TEXT("Select"), TEXT("Proof"), TEXT("All text"), TEXT("Evidence"), TEXT("Open source")};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Result.Actions.Add({Ids[Index], Names[Index], Box(Pad + CellWidth * Index, ActionsY, CellWidth, 58.0f)});
	}
	Result.DiagnosticCode = Result.LayoutRequest.IsValid() && Result.LayoutLedger.IsCompleteFor(Result.LayoutRequest) &&
		Result.HasNoTextOrRouteCollisions() ? TEXT("LC4_ASYNC_LAYOUT_COMPLETE") : TEXT("LC4_ASYNC_LAYOUT_INVARIANT_FAILED");
	return Result;
}
