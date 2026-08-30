#include "BlueprintLensLC3DerivationSpineLayout.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
constexpr float CompactThreshold = 620.0f;
constexpr float MinimumWidth = 430.0f;
constexpr float Margin = 24.0f;
constexpr float OperatorDiameter = 80.0f;
constexpr float ValueHitWidth = 128.0f;
constexpr float ValueHitHeight = 42.0f;
constexpr float CriterionWidth = 126.0f;
constexpr float CriterionHeight = 96.0f;
constexpr float ControlHitWidth = 132.0f;
constexpr float ControlHitHeight = 34.0f;
constexpr float ExecutionRailClearance = 12.0f;

FVector2D BoxCenter(const FBox2D& Bounds)
{
	return (Bounds.Min + Bounds.Max) * 0.5f;
}

FBox2D BoundsFromCenter(const FVector2D& Center, const FVector2D& Size)
{
	return FBox2D(Center - Size * 0.5f, Center + Size * 0.5f);
}

FString ValueDisplayLabel(const FString& ReaderLabel)
{
	return ReaderLabel.StartsWith(TEXT("Get "))
		? ReaderLabel.RightChop(4)
		: ReaderLabel;
}

FString OperatorDisplayLabel(const FString& ReaderLabel)
{
	int32 SeparatorIndex = INDEX_NONE;
	return ReaderLabel.FindChar(TEXT('_'), SeparatorIndex)
		? ReaderLabel.Left(SeparatorIndex)
		: ReaderLabel;
}

FString OperatorGlyph(const FString& ReaderLabel)
{
	if (ReaderLabel.StartsWith(TEXT("Add_")) || ReaderLabel == TEXT("Add"))
	{
		return TEXT("+");
	}
	if (ReaderLabel.StartsWith(TEXT("Subtract_")) ||
		ReaderLabel == TEXT("Subtract"))
	{
		return TEXT("\u2212");
	}
	return FString();
}

FSlateFontInfo FontForKind(
	const EBlueprintLensLC3DerivationSpineLabelKind Kind)
{
	switch (Kind)
	{
	case EBlueprintLensLC3DerivationSpineLabelKind::OperatorGlyph:
		// The glyph carries the operator's identity in the default reading, so
		// it has to fill its station the way the frozen design draws it.
		return FCoreStyle::GetDefaultFontStyle("Bold", 26);
	case EBlueprintLensLC3DerivationSpineLabelKind::Value:
	case EBlueprintLensLC3DerivationSpineLabelKind::CriterionValue:
		return FAppStyle::Get().GetFontStyle("NormalFontBold");
	default:
		return FAppStyle::Get().GetFontStyle("SmallFont");
	}
}

bool PointStrictlyInside(const FVector2D& Point, const FBox2D& Bounds)
{
	return Point.X > Bounds.Min.X && Point.X < Bounds.Max.X &&
		Point.Y > Bounds.Min.Y && Point.Y < Bounds.Max.Y;
}

bool SegmentIntersectsBounds(
	const FVector2D& Start,
	const FVector2D& End,
	const FBox2D& Bounds)
{
	const FBox2D InnerBounds(
		Bounds.Min + FVector2D(0.05f, 0.05f),
		Bounds.Max - FVector2D(0.05f, 0.05f));
	if (!InnerBounds.bIsValid)
	{
		return false;
	}
	if (PointStrictlyInside(Start, InnerBounds) ||
		PointStrictlyInside(End, InnerBounds))
	{
		return true;
	}

	const FVector2D Delta = End - Start;
	float Enter = 0.0f;
	float Exit = 1.0f;
	const auto ClipAxis = [&Enter, &Exit](
		const float StartValue,
		const float DeltaValue,
		const float Minimum,
		const float Maximum)
	{
		if (FMath::IsNearlyZero(DeltaValue))
		{
			return StartValue > Minimum && StartValue < Maximum;
		}
		float Near = (Minimum - StartValue) / DeltaValue;
		float Far = (Maximum - StartValue) / DeltaValue;
		if (Near > Far)
		{
			Swap(Near, Far);
		}
		Enter = FMath::Max(Enter, Near);
		Exit = FMath::Min(Exit, Far);
		return Enter < Exit;
	};

	return ClipAxis(Start.X, Delta.X, InnerBounds.Min.X, InnerBounds.Max.X) &&
		ClipAxis(Start.Y, Delta.Y, InnerBounds.Min.Y, InnerBounds.Max.Y) &&
		Exit > 0.0f && Enter < 1.0f;
}

bool RouteIntersectsLabels(
	const TArray<FVector2D>& Points,
	const TArray<FBlueprintLensLC3DerivationSpineLabel>& Labels)
{
	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		for (const FBlueprintLensLC3DerivationSpineLabel& Label : Labels)
		{
			if (Label.bHasBackingPlate)
			{
				continue;
			}
			if (SegmentIntersectsBounds(
				Points[PointIndex - 1],
				Points[PointIndex],
				Label.ExclusionBounds))
			{
				return true;
			}
		}
	}
	return false;
}

FVector2D MeasureText(const FString& Text, const FSlateFontInfo& Font)
{
	if (!FSlateApplication::IsInitialized())
	{
		return FVector2D(
			FMath::Max(8.0f, static_cast<float>(Text.Len()) * 7.0f),
			16.0f);
	}
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FontMeasure->Measure(Text, Font);
}

void AddLabel(
	FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FString& Key,
	const FString& UnitId,
	const FString& Text,
	const EBlueprintLensLC3DerivationSpineLabelKind Kind,
	const FVector2D& Position,
	const bool bSelectedEndpoint = false,
	const bool bHasBackingPlate = false)
{
	FBlueprintLensLC3DerivationSpineLabel Label;
	Label.Key = Key;
	Label.UnitId = UnitId;
	Label.Text = Text;
	Label.Kind = Kind;
	Label.Font = FontForKind(Kind);
	Label.Position = Position;
	Label.Size = MeasureText(Text, Label.Font);
	const float Clearance = FMath::Max(6.0f, Label.Size.Y * 0.25f);
	Label.ExclusionBounds = FBox2D(
		Position - FVector2D(Clearance, Clearance),
		Position + Label.Size + FVector2D(Clearance, Clearance));
	Label.bSelectedEndpoint = bSelectedEndpoint;
	Label.bHasBackingPlate = bHasBackingPlate;
	Layout.Labels.Add(MoveTemp(Label));
}

const FBlueprintLensLC3ValueConeLayoutNode* FindLayoutNode(
	const FBlueprintLensLC3ValueConeLayout& Layout,
	const FString& UnitId)
{
	return Layout.Nodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLC3ValueConeLayoutNode& Node)
		{
			return Node.UnitId == UnitId;
		});
}

void AddElementLabels(
	FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FBlueprintLensLC3DerivationSpineElement& Element)
{
	if (Element.Kind == EBlueprintLensLC3DerivationSpineElementKind::Value)
	{
		const FVector2D Position(
			Element.Bounds.Min.X + 12.0f,
			Element.Bounds.Min.Y + 2.0f);
		AddLabel(
			Layout,
			Element.UnitId + TEXT(":value"),
			Element.UnitId,
			Element.DisplayLabel,
			EBlueprintLensLC3DerivationSpineLabelKind::Value,
			Position);
		return;
	}
	if (Element.Kind == EBlueprintLensLC3DerivationSpineElementKind::Operator)
	{
		const FVector2D Center = BoxCenter(Element.Bounds);
		if (!Element.Glyph.IsEmpty())
		{
			const FSlateFontInfo GlyphFont = FontForKind(
				EBlueprintLensLC3DerivationSpineLabelKind::OperatorGlyph);
			const FVector2D GlyphSize = MeasureText(Element.Glyph, GlyphFont);
			AddLabel(
				Layout,
				Element.UnitId + TEXT(":glyph"),
				Element.UnitId,
				Element.Glyph,
				EBlueprintLensLC3DerivationSpineLabelKind::OperatorGlyph,
				FVector2D(
					Center.X - GlyphSize.X * 0.5f,
					Center.Y - GlyphSize.Y * 0.72f));
		}
		// The frozen design draws a glyph-only default Spine, but the accepted
		// reader requirement is that both operators stay nameable without
		// selection, so the station keeps its small-caps name in every state.
		const FSlateFontInfo NameFont = FontForKind(
			EBlueprintLensLC3DerivationSpineLabelKind::OperatorName);
		const FString NameText = Element.DisplayLabel.ToUpper();
		const FVector2D NameSize = MeasureText(NameText, NameFont);
		AddLabel(
			Layout,
			Element.UnitId + TEXT(":name"),
			Element.UnitId,
			NameText,
			EBlueprintLensLC3DerivationSpineLabelKind::OperatorName,
			FVector2D(
				Center.X - NameSize.X * 0.5f,
				Center.Y + 14.0f));
		return;
	}
	if (Element.Kind == EBlueprintLensLC3DerivationSpineElementKind::Criterion)
	{
		AddLabel(
			Layout,
			Element.UnitId + TEXT(":role"),
			Element.UnitId,
			TEXT("CRITERION"),
			EBlueprintLensLC3DerivationSpineLabelKind::CriterionRole,
			Element.Bounds.Min + FVector2D(14.0f, 13.0f));
		AddLabel(
			Layout,
			Element.UnitId + TEXT(":criterion"),
			Element.UnitId,
			Element.DisplayLabel,
			EBlueprintLensLC3DerivationSpineLabelKind::CriterionValue,
			Element.Bounds.Min + FVector2D(14.0f, 44.0f));
		return;
	}
	AddLabel(
		Layout,
		Element.UnitId + TEXT(":control"),
		Element.UnitId,
		Element.DisplayLabel,
		EBlueprintLensLC3DerivationSpineLabelKind::Control,
		Element.Bounds.Min + FVector2D(20.0f, 7.0f));
}

void SimplifyRoute(TArray<FVector2D>& Points)
{
	for (int32 Index = Points.Num() - 2; Index > 0; --Index)
	{
		const FVector2D Before = Points[Index] - Points[Index - 1];
		const FVector2D After = Points[Index + 1] - Points[Index];
		if (Points[Index].Equals(Points[Index - 1], 0.05f) ||
			Points[Index].Equals(Points[Index + 1], 0.05f) ||
			(FMath::IsNearlyZero(Before.X) && FMath::IsNearlyZero(After.X)) ||
			(FMath::IsNearlyZero(Before.Y) && FMath::IsNearlyZero(After.Y)))
		{
			Points.RemoveAt(Index);
		}
	}
}

float RouteScore(const TArray<FVector2D>& Points)
{
	float Score = static_cast<float>(Points.Num()) * 10.0f;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Score += FVector2D::Distance(Points[Index - 1], Points[Index]);
	}
	return Score;
}

// The frozen design draws every value provision as one broad, continuous,
// rounded ribbon: a cubic curve that leaves its source along the dominant axis
// and enters its target along the same axis. The orthogonal router below stays
// as the fail-closed fallback when a curve cannot clear the measured labels.
enum class ERibbonAxis : uint8
{
	// Pick the axis from the endpoint delta. The wide grammar anchors on facing
	// edges, so the delta and the anchor orientation always agree.
	Dominant,
	Horizontal,
	Vertical
};

TArray<FVector2D> BuildValueRibbon(
	const FVector2D& Origin,
	const FVector2D& End,
	const float LeadIn,
	const ERibbonAxis Axis)
{
	// A ribbon that leaves a named origin runs flat past its own name first, so
	// the curve never has to cross the label it belongs to.
	const FVector2D Start = LeadIn > 0.0f
		? FVector2D(Origin.X + LeadIn, Origin.Y)
		: Origin;
	const FVector2D Delta = End - Start;
	const bool bHorizontal = Axis == ERibbonAxis::Dominant
		? FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y)
		: Axis == ERibbonAxis::Horizontal;
	const float Reach = bHorizontal
		? FMath::Abs(Delta.X) * 0.55f
		: FMath::Abs(Delta.Y) * 0.55f;
	const FVector2D Reach2D = bHorizontal
		? FVector2D(FMath::Sign(Delta.X) * Reach, 0.0f)
		: FVector2D(0.0f, FMath::Sign(Delta.Y) * Reach);
	const FVector2D FirstControl = Start + Reach2D;
	const FVector2D SecondControl = End - Reach2D;

	constexpr int32 SegmentCount = 18;
	TArray<FVector2D> Points;
	Points.Reserve(SegmentCount + 2);
	if (LeadIn > 0.0f)
	{
		Points.Add(Origin);
	}
	for (int32 Index = 0; Index <= SegmentCount; ++Index)
	{
		const float T = static_cast<float>(Index) / SegmentCount;
		const float U = 1.0f - T;
		Points.Add(
			Start * (U * U * U) +
			FirstControl * (3.0f * U * U * T) +
			SecondControl * (3.0f * U * T * T) +
			End * (T * T * T));
	}
	return Points;
}

// The execution relation owns a separate rail, never a diagonal across the
// value canvas: it runs horizontally under the whole reading path and turns up
// once, in the criterion column.
TArray<FVector2D> BuildExecutionRail(
	const FVector2D& RailStart,
	const float TurnX,
	const float RiserTopY)
{
	return {
		RailStart,
		FVector2D(TurnX, RailStart.Y),
		FVector2D(TurnX, RiserTopY)};
}

bool FindOrthogonalRoute(
	const FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FVector2D& Start,
	const FVector2D& End,
	TArray<FVector2D>& OutPoints)
{
	TArray<float> CandidateXs = {
		static_cast<float>(Start.X),
		static_cast<float>(End.X),
		Margin * 0.5f,
		static_cast<float>(Layout.CanvasSize.X) - Margin * 0.5f};
	TArray<float> CandidateYs = {
		static_cast<float>(Start.Y),
		static_cast<float>(End.Y),
		Margin * 0.5f,
		static_cast<float>(Layout.CanvasSize.Y) - Margin * 0.5f};
	for (const FBlueprintLensLC3DerivationSpineLabel& Label : Layout.Labels)
	{
		CandidateXs.Add(FMath::Max(2.0f, Label.ExclusionBounds.Min.X - 8.0f));
		CandidateXs.Add(FMath::Min(
			Layout.CanvasSize.X - 2.0f,
			Label.ExclusionBounds.Max.X + 8.0f));
		CandidateYs.Add(FMath::Max(2.0f, Label.ExclusionBounds.Min.Y - 8.0f));
		CandidateYs.Add(FMath::Min(
			Layout.CanvasSize.Y - 2.0f,
			Label.ExclusionBounds.Max.Y + 8.0f));
	}
	CandidateXs.Sort();
	CandidateYs.Sort();
	for (int32 Index = CandidateXs.Num() - 1; Index > 0; --Index)
	{
		if (FMath::IsNearlyEqual(CandidateXs[Index], CandidateXs[Index - 1], 0.1f))
		{
			CandidateXs.RemoveAt(Index);
		}
	}
	for (int32 Index = CandidateYs.Num() - 1; Index > 0; --Index)
	{
		if (FMath::IsNearlyEqual(CandidateYs[Index], CandidateYs[Index - 1], 0.1f))
		{
			CandidateYs.RemoveAt(Index);
		}
	}

	float BestScore = TNumericLimits<float>::Max();
	const auto Consider = [&Layout, &OutPoints, &BestScore](
		TArray<FVector2D> Candidate)
	{
		SimplifyRoute(Candidate);
		if (Candidate.Num() < 2 ||
			RouteIntersectsLabels(Candidate, Layout.Labels))
		{
			return;
		}
		const float Score = RouteScore(Candidate);
		if (Score < BestScore)
		{
			BestScore = Score;
			OutPoints = MoveTemp(Candidate);
		}
	};

	Consider({Start, End});
	Consider({Start, FVector2D(Start.X, End.Y), End});
	Consider({Start, FVector2D(End.X, Start.Y), End});
	for (const float X : CandidateXs)
	{
		Consider({
			Start,
			FVector2D(X, Start.Y),
			FVector2D(X, End.Y),
			End});
	}
	for (const float Y : CandidateYs)
	{
		Consider({
			Start,
			FVector2D(Start.X, Y),
			FVector2D(End.X, Y),
			End});
	}
	for (const float X : CandidateXs)
	{
		for (const float Y : CandidateYs)
		{
			Consider({
				Start,
				FVector2D(X, Start.Y),
				FVector2D(X, Y),
				FVector2D(End.X, Y),
				End});
			Consider({
				Start,
				FVector2D(Start.X, Y),
				FVector2D(X, Y),
				FVector2D(X, End.Y),
				End});
		}
	}
	return OutPoints.Num() >= 2;
}

bool TryAddRoute(
	FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FString& RelationId,
	const FString& SourceUnitId,
	const FString& TargetUnitId,
	const EBlueprintLensLayoutRelationFamily Family,
	const FVector2D& Start,
	const FVector2D& End,
	const TArray<FVector2D>& PreferredPoints = TArray<FVector2D>())
{
	TArray<FVector2D> Points;
	if (PreferredPoints.Num() >= 2 &&
		!RouteIntersectsLabels(PreferredPoints, Layout.Labels))
	{
		Points = PreferredPoints;
	}
	else if (!FindOrthogonalRoute(Layout, Start, End, Points))
	{
		return false;
	}
	FBlueprintLensLC3DerivationSpineRoute Route;
	Route.RelationId = RelationId;
	Route.SourceUnitId = SourceUnitId;
	Route.TargetUnitId = TargetUnitId;
	Route.Family = Family;
	Route.Points = MoveTemp(Points);
	Layout.Routes.Add(MoveTemp(Route));
	return true;
}

void SetElementGeometry(
	FBlueprintLensLC3DerivationSpineElement& Element,
	const FVector2D& Center)
{
	switch (Element.Kind)
	{
	case EBlueprintLensLC3DerivationSpineElementKind::Operator:
		Element.Bounds = BoundsFromCenter(
			Center,
			FVector2D(OperatorDiameter, OperatorDiameter));
		Element.InputAnchor = FVector2D(Element.Bounds.Min.X, Center.Y);
		Element.OutputAnchor = FVector2D(Element.Bounds.Max.X, Center.Y);
		break;
	case EBlueprintLensLC3DerivationSpineElementKind::Criterion:
		Element.Bounds = BoundsFromCenter(
			Center,
			FVector2D(CriterionWidth, CriterionHeight));
		Element.InputAnchor = FVector2D(Element.Bounds.Min.X, Center.Y);
		Element.OutputAnchor = Element.InputAnchor;
		break;
	case EBlueprintLensLC3DerivationSpineElementKind::Control:
		Element.Bounds = BoundsFromCenter(
			Center,
			FVector2D(ControlHitWidth, ControlHitHeight));
		// The rail dot sits before its own label, as the frozen design draws it.
		Element.InputAnchor = FVector2D(Element.Bounds.Min.X + 7.0f, Center.Y);
		Element.OutputAnchor = FVector2D(Element.Bounds.Max.X, Center.Y);
		break;
	default:
		Element.Bounds = BoundsFromCenter(
			Center,
			FVector2D(ValueHitWidth, ValueHitHeight));
		Element.InputAnchor = Center;
		Element.OutputAnchor = FVector2D(Element.Bounds.Min.X + 2.0f, Center.Y + 13.0f);
		break;
	}
}

struct FRouteAnchors
{
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	ERibbonAxis Axis = ERibbonAxis::Dominant;
};

// Both ends of a route have to agree on which edge they use. The wide grammar
// always faces left-to-right, so its anchors are the element anchors. The
// compact grammar stacks the Spine, so a consumer below its producer is left
// from the bottom edge and entered at the top edge, while a consumer beside its
// producer keeps the horizontal treatment. Leaving downward but entering at the
// left edge is what forced every compact route into the orthogonal fallback.
FRouteAnchors RouteAnchorsFor(
	const FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FBlueprintLensLC3DerivationSpineElement& Source,
	const FBlueprintLensLC3DerivationSpineElement& Target,
	const FString& PortLabel)
{
	const FVector2D SourceCenter = BoxCenter(Source.Bounds);
	const FVector2D TargetCenter = BoxCenter(Target.Bounds);
	const bool bSelectedOperator =
		Target.Kind == EBlueprintLensLC3DerivationSpineElementKind::Operator &&
		Target.bSelected;
	const float PortOffset = bSelectedOperator
		? (PortLabel == TEXT("B") ? 17.0f : -17.0f)
		: 0.0f;

	FRouteAnchors Anchors;
	if (!Layout.bCompact)
	{
		Anchors.Start = Source.OutputAnchor;
		Anchors.End = bSelectedOperator
			? FVector2D(Target.Bounds.Min.X, TargetCenter.Y + PortOffset)
			: Target.InputAnchor;
		return Anchors;
	}

	if (Target.Bounds.Min.Y >= Source.Bounds.Max.Y - 4.0f)
	{
		Anchors.Start = FVector2D(SourceCenter.X, Source.Bounds.Max.Y);
		Anchors.End =
			FVector2D(TargetCenter.X + PortOffset, Target.Bounds.Min.Y);
		Anchors.Axis = ERibbonAxis::Vertical;
		return Anchors;
	}
	// A named origin still leaves from beside its own name, so the dot the
	// surface paints stays next to the name it belongs to.
	Anchors.Start =
		Source.Kind == EBlueprintLensLC3DerivationSpineElementKind::Value
			? Source.OutputAnchor
			: FVector2D(Source.Bounds.Max.X, SourceCenter.Y);
	Anchors.End = FVector2D(Target.Bounds.Min.X, TargetCenter.Y + PortOffset);
	Anchors.Axis = ERibbonAxis::Horizontal;
	return Anchors;
}

void AddQualifiedPortLabels(
	FBlueprintLensLC3DerivationSpineLayout& Layout,
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const FBlueprintLensLC3DerivationSpineElement& SelectedOperator)
{
	const FString OperatorName = SelectedOperator.DisplayLabel;
	int32 InputIndex = 0;
	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		if (Step.ConsumerUnitId != SelectedOperator.UnitId)
		{
			continue;
		}
		const FString Text = FString::Printf(
			TEXT("%s.%s"),
			*OperatorName,
			*Step.ConsumerPortLabel);
		FVector2D Position;
		if (Layout.bCompact)
		{
			const FSlateFontInfo Font = FontForKind(
				EBlueprintLensLC3DerivationSpineLabelKind::QualifiedPort);
			const FVector2D LabelSize = MeasureText(Text, Font);
			const float InputAnchorX =
				BoxCenter(SelectedOperator.Bounds).X +
				(InputIndex == 0 ? -17.0f : 17.0f);
			Position = FVector2D(
				InputIndex == 0
					? InputAnchorX - LabelSize.X - 8.0f
					: InputAnchorX + 8.0f,
				SelectedOperator.Bounds.Min.Y - LabelSize.Y - 7.0f);
		}
		else
		{
			Position = FVector2D(
				SelectedOperator.Bounds.Min.X - 74.0f,
				SelectedOperator.Bounds.Min.Y + 7.0f + InputIndex * 34.0f);
		}
		AddLabel(
			Layout,
			Step.RelationId + TEXT(":target-port"),
			SelectedOperator.UnitId,
			Text,
			EBlueprintLensLC3DerivationSpineLabelKind::QualifiedPort,
			Position,
			true,
			true);
		++InputIndex;
	}
	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		if (Step.ProducerUnitId != SelectedOperator.UnitId)
		{
			continue;
		}
		const FString Text = FString::Printf(
			TEXT("%s.%s"),
			*OperatorName,
			*Step.ProducerPortLabel);
		const FVector2D Position = Layout.bCompact
			? FVector2D(
				SelectedOperator.Bounds.Max.X + 8.0f,
				BoxCenter(SelectedOperator.Bounds).Y - 8.0f)
			: FVector2D(
				SelectedOperator.Bounds.Max.X + 10.0f,
				SelectedOperator.Bounds.Min.Y - 24.0f);
		AddLabel(
			Layout,
			Step.RelationId + TEXT(":source-port"),
			SelectedOperator.UnitId,
			Text,
			EBlueprintLensLC3DerivationSpineLabelKind::QualifiedPort,
			Position,
			true,
			true);
		break;
	}
}
} // namespace

const FBlueprintLensLC3DerivationSpineElement*
FBlueprintLensLC3DerivationSpineLayout::FindElement(
	const FString& UnitId) const
{
	return Elements.FindByPredicate(
		[&UnitId](const FBlueprintLensLC3DerivationSpineElement& Element)
		{
			return Element.UnitId == UnitId;
		});
}

bool FBlueprintLensLC3DerivationSpineLayout::CoversProjection(
	const FBlueprintLensLC3ValueConeProjection& Projection) const
{
	if (Elements.Num() != Projection.AllUnitIds.Num() ||
		Routes.Num() != Projection.AllRelationIds.Num())
	{
		return false;
	}
	TSet<FString> UnitIds;
	for (const FBlueprintLensLC3DerivationSpineElement& Element : Elements)
	{
		if (Element.UnitId.IsEmpty() || UnitIds.Contains(Element.UnitId))
		{
			return false;
		}
		UnitIds.Add(Element.UnitId);
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (!UnitIds.Contains(UnitId))
		{
			return false;
		}
	}
	TSet<FString> RelationIds;
	for (const FBlueprintLensLC3DerivationSpineRoute& Route : Routes)
	{
		if (Route.RelationId.IsEmpty() ||
			RelationIds.Contains(Route.RelationId) || Route.Points.Num() < 2)
		{
			return false;
		}
		RelationIds.Add(Route.RelationId);
	}
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		if (!RelationIds.Contains(RelationId))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC3DerivationSpineLayout::HasNoRouteLabelIntersections() const
{
	for (const FBlueprintLensLC3DerivationSpineRoute& Route : Routes)
	{
		if (RouteIntersectsLabels(Route.Points, Labels))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC3DerivationSpineLayout::HasNoLabelIntersections() const
{
	for (int32 LeftIndex = 0; LeftIndex < Labels.Num(); ++LeftIndex)
	{
		const FBox2D LeftBounds(
			Labels[LeftIndex].Position,
			Labels[LeftIndex].Position + Labels[LeftIndex].Size);
		for (int32 RightIndex = LeftIndex + 1;
			 RightIndex < Labels.Num();
			 ++RightIndex)
		{
			const FBox2D RightBounds(
				Labels[RightIndex].Position,
				Labels[RightIndex].Position + Labels[RightIndex].Size);
			if (LeftBounds.Min.X < RightBounds.Max.X &&
				LeftBounds.Max.X > RightBounds.Min.X &&
				LeftBounds.Min.Y < RightBounds.Max.Y &&
				LeftBounds.Max.Y > RightBounds.Min.Y)
			{
				return false;
			}
		}
	}
	return true;
}

bool FBlueprintLensLC3DerivationSpineLayout::IsRenderable(
	const FBlueprintLensLC3ValueConeProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC3_D3_SURFACE_COMPLETE") &&
		CoversProjection(Projection) && HasNoRouteLabelIntersections() &&
		HasNoLabelIntersections();
}

FBlueprintLensLC3DerivationSpineLayout
FBlueprintLensLC3DerivationSpineLayoutBuilder::Build(
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const FBlueprintLensLC3ValueConeLayout& BaseLayout,
	const float TargetWidth,
	const FString& SelectedUnitId)
{
	FBlueprintLensLC3DerivationSpineLayout Result;
	if (!Projection.IsRenderable() || !BaseLayout.CoversProjection(Projection) ||
		!BaseLayout.HasValidSharedLedger())
	{
		Result.DiagnosticCode = TEXT("LC3_D3_SURFACE_BASE_LEDGER_INVALID");
		return Result;
	}

	const float EffectiveWidth = FMath::Max(TargetWidth, MinimumWidth);
	Result.bCompact = EffectiveWidth < CompactThreshold;
	Result.BaseLedgerFingerprint = FString::Printf(
		TEXT("%s|%s|%s|%d|%d|%d"),
		BlueprintLensLayoutBackendName(BaseLayout.LayoutLedger.Backend),
		*BaseLayout.LayoutLedger.BackendVersion,
		*BaseLayout.LayoutLedger.ConfigurationFingerprint,
		BaseLayout.LayoutLedger.Nodes.Num(),
		BaseLayout.LayoutLedger.Ports.Num(),
		BaseLayout.LayoutLedger.Edges.Num());

	for (const FBlueprintLensLC3ValueConeLayoutNode& Node : BaseLayout.Nodes)
	{
		FBlueprintLensLC3DerivationSpineElement Element;
		Element.UnitId = Node.UnitId;
		Element.ReaderLabel = Node.ReaderLabel;
		switch (Node.Kind)
		{
		case EBlueprintLensLC3ValueConeNodeKind::Operator:
			Element.Kind = EBlueprintLensLC3DerivationSpineElementKind::Operator;
			Element.DisplayLabel = OperatorDisplayLabel(Node.ReaderLabel);
			Element.Glyph = OperatorGlyph(Node.ReaderLabel);
			break;
		case EBlueprintLensLC3ValueConeNodeKind::Criterion:
			Element.Kind = EBlueprintLensLC3DerivationSpineElementKind::Criterion;
			Element.DisplayLabel = Node.ReaderLabel;
			break;
		case EBlueprintLensLC3ValueConeNodeKind::Control:
			Element.Kind = EBlueprintLensLC3DerivationSpineElementKind::Control;
			Element.DisplayLabel = Node.ReaderLabel;
			break;
		default:
			Element.Kind = EBlueprintLensLC3DerivationSpineElementKind::Value;
			Element.DisplayLabel = ValueDisplayLabel(Node.ReaderLabel);
			break;
		}
		Result.Elements.Add(MoveTemp(Element));
	}

	FBlueprintLensLC3DerivationSpineElement* SelectedOperator =
		Result.Elements.FindByPredicate(
			[&SelectedUnitId](
				const FBlueprintLensLC3DerivationSpineElement& Element)
			{
				return Element.UnitId == SelectedUnitId &&
					Element.Kind ==
						EBlueprintLensLC3DerivationSpineElementKind::Operator;
			});
	if (SelectedOperator != nullptr)
	{
		const int32 InputCount = Projection.Steps.FilterByPredicate(
			[SelectedOperator](const FBlueprintLensLC3ValueConeStep& Step)
			{
				return Step.ConsumerUnitId == SelectedOperator->UnitId;
			}).Num();
		const int32 OutputCount = Projection.Steps.FilterByPredicate(
			[SelectedOperator](const FBlueprintLensLC3ValueConeStep& Step)
			{
				return Step.ProducerUnitId == SelectedOperator->UnitId;
			}).Num();
		if (InputCount < 1 || OutputCount < 1)
		{
			Result.Elements.Reset();
			Result.DiagnosticCode =
				TEXT("LC3_D3_SURFACE_OWNERSHIP_UNPROVEN");
			return Result;
		}
		SelectedOperator->bSelected = true;
		Result.SelectedOperatorUnitId = SelectedOperator->UnitId;
		Result.bHasLocalSubtree = true;
	}

	TArray<FBlueprintLensLC3DerivationSpineElement*> Operators;
	TArray<FBlueprintLensLC3DerivationSpineElement*> Values;
	FBlueprintLensLC3DerivationSpineElement* Criterion = nullptr;
	FBlueprintLensLC3DerivationSpineElement* Control = nullptr;
	for (FBlueprintLensLC3DerivationSpineElement& Element : Result.Elements)
	{
		switch (Element.Kind)
		{
		case EBlueprintLensLC3DerivationSpineElementKind::Operator:
			Operators.Add(&Element);
			break;
		case EBlueprintLensLC3DerivationSpineElementKind::Value:
			Values.Add(&Element);
			break;
		case EBlueprintLensLC3DerivationSpineElementKind::Criterion:
			Criterion = &Element;
			break;
		case EBlueprintLensLC3DerivationSpineElementKind::Control:
			Control = &Element;
			break;
		}
	}
	if (Operators.Num() != 2 || Values.Num() != 3 || Criterion == nullptr ||
		Control == nullptr)
	{
		Result.Elements.Reset();
		Result.DiagnosticCode = TEXT("LC3_D3_SURFACE_TOPOLOGY_UNSUPPORTED");
		return Result;
	}

	Operators.Sort(
		[&BaseLayout](
			const FBlueprintLensLC3DerivationSpineElement& Left,
			const FBlueprintLensLC3DerivationSpineElement& Right)
		{
			const FBlueprintLensLC3ValueConeLayoutNode* LeftNode =
				FindLayoutNode(BaseLayout, Left.UnitId);
			const FBlueprintLensLC3ValueConeLayoutNode* RightNode =
				FindLayoutNode(BaseLayout, Right.UnitId);
			return LeftNode != nullptr && RightNode != nullptr
				? LeftNode->DerivationDepth > RightNode->DerivationDepth
				: Left.UnitId < Right.UnitId;
		});
	Values.Sort(
		[&BaseLayout](
			const FBlueprintLensLC3DerivationSpineElement& Left,
			const FBlueprintLensLC3DerivationSpineElement& Right)
		{
			const FBlueprintLensLayoutNodePlacement* LeftNode =
				BaseLayout.LayoutLedger.Nodes.FindByPredicate(
					[&Left](const FBlueprintLensLayoutNodePlacement& Node)
					{
						return Node.UnitId == Left.UnitId;
					});
			const FBlueprintLensLayoutNodePlacement* RightNode =
				BaseLayout.LayoutLedger.Nodes.FindByPredicate(
					[&Right](const FBlueprintLensLayoutNodePlacement& Node)
					{
						return Node.UnitId == Right.UnitId;
					});
			return LeftNode != nullptr && RightNode != nullptr
				? LeftNode->Position.Y < RightNode->Position.Y
				: Left.UnitId < Right.UnitId;
		});

	FBlueprintLensLC3DerivationSpineElement* UpstreamOperator = Operators[0];
	FBlueprintLensLC3DerivationSpineElement* DownstreamOperator = Operators[1];
	TArray<FBlueprintLensLC3DerivationSpineElement*> UpstreamInputs;
	FBlueprintLensLC3DerivationSpineElement* DownstreamLeaf = nullptr;
	for (FBlueprintLensLC3DerivationSpineElement* Value : Values)
	{
		const bool bFeedsUpstream = Projection.Steps.ContainsByPredicate(
			[Value, UpstreamOperator](const FBlueprintLensLC3ValueConeStep& Step)
			{
				return Step.ProducerUnitId == Value->UnitId &&
					Step.ConsumerUnitId == UpstreamOperator->UnitId;
			});
		if (bFeedsUpstream)
		{
			UpstreamInputs.Add(Value);
		}
		else
		{
			DownstreamLeaf = Value;
		}
	}
	if (UpstreamInputs.Num() != 2 || DownstreamLeaf == nullptr)
	{
		Result.Elements.Reset();
		Result.DiagnosticCode = TEXT("LC3_D3_SURFACE_OWNERSHIP_UNPROVEN");
		return Result;
	}

	// Operand placement is the reading order. `Subtract` is not commutative, so
	// a reader who takes the operands in layout order has to meet port A before
	// port B; the layout ledger's own ordering carries no operand meaning. With
	// the operands placed by port, the entry slots - which are assigned by port
	// name - are reached without the two ribbons crossing.
	const auto ConsumedPortLabel =
		[&Projection](
			const FBlueprintLensLC3DerivationSpineElement* Operand,
			const FBlueprintLensLC3DerivationSpineElement* Consumer)
		{
			const FBlueprintLensLC3ValueConeStep* Step =
				Projection.Steps.FindByPredicate(
					[Operand, Consumer](const FBlueprintLensLC3ValueConeStep& Step)
					{
						return Step.ProducerUnitId == Operand->UnitId &&
							Step.ConsumerUnitId == Consumer->UnitId;
					});
			return Step != nullptr ? Step->ConsumerPortLabel : FString();
		};
	UpstreamInputs.Sort(
		[&ConsumedPortLabel, UpstreamOperator](
			const FBlueprintLensLC3DerivationSpineElement& Left,
			const FBlueprintLensLC3DerivationSpineElement& Right)
		{
			return ConsumedPortLabel(&Left, UpstreamOperator) <
				ConsumedPortLabel(&Right, UpstreamOperator);
		});

	if (!Result.bCompact)
	{
		const float Scale = EffectiveWidth / 700.0f;
		SetElementGeometry(*UpstreamInputs[0], FVector2D(88.0f * Scale, 105.0f));
		SetElementGeometry(*UpstreamInputs[1], FVector2D(88.0f * Scale, 190.0f));
		SetElementGeometry(*DownstreamLeaf, FVector2D(88.0f * Scale, 320.0f));
		SetElementGeometry(*UpstreamOperator, FVector2D(285.0f * Scale, 148.0f));
		SetElementGeometry(*DownstreamOperator, FVector2D(480.0f * Scale, 230.0f));
		SetElementGeometry(*Criterion, FVector2D(628.0f * Scale, 230.0f));
		SetElementGeometry(*Control, FVector2D(88.0f * Scale, 412.0f));
		Result.CanvasSize = FVector2D(EffectiveWidth, 470.0f);

		if (SelectedOperator == UpstreamOperator)
		{
			SetElementGeometry(*UpstreamInputs[0], FVector2D(95.0f * Scale, 86.0f));
			SetElementGeometry(*UpstreamInputs[1], FVector2D(95.0f * Scale, 244.0f));
			SetElementGeometry(*UpstreamOperator, FVector2D(302.0f * Scale, 165.0f));
			Result.LocalSubtreeBounds = FBox2D(
				FVector2D(Margin, 36.0f),
				FVector2D(372.0f * Scale, 292.0f));
		}
		else if (SelectedOperator == DownstreamOperator)
		{
			SetElementGeometry(*UpstreamOperator, FVector2D(272.0f * Scale, 112.0f));
			SetElementGeometry(*DownstreamLeaf, FVector2D(272.0f * Scale, 310.0f));
			SetElementGeometry(*DownstreamOperator, FVector2D(492.0f * Scale, 211.0f));
			SetElementGeometry(*Criterion, FVector2D(635.0f * Scale, 211.0f));
			Result.LocalSubtreeBounds = FBox2D(
				FVector2D(210.0f * Scale, 54.0f),
				FVector2D(548.0f * Scale, 356.0f));
		}
	}
	else
	{
		const float CenterX = EffectiveWidth * 0.5f;
		SetElementGeometry(*UpstreamInputs[0], FVector2D(86.0f, 68.0f));
		SetElementGeometry(*UpstreamInputs[1], FVector2D(EffectiveWidth - 86.0f, 68.0f));
		SetElementGeometry(*UpstreamOperator, FVector2D(CenterX, 160.0f));
		SetElementGeometry(*DownstreamLeaf, FVector2D(86.0f, 264.0f));
		SetElementGeometry(*DownstreamOperator, FVector2D(CenterX, 286.0f));
		SetElementGeometry(*Criterion, FVector2D(CenterX, 410.0f));
		SetElementGeometry(*Control, FVector2D(90.0f, 530.0f));
		Result.CanvasSize = FVector2D(EffectiveWidth, 590.0f);

		if (SelectedOperator == UpstreamOperator)
		{
			SetElementGeometry(*UpstreamInputs[0], FVector2D(82.0f, 82.0f));
			SetElementGeometry(*UpstreamInputs[1], FVector2D(EffectiveWidth - 82.0f, 82.0f));
			SetElementGeometry(*UpstreamOperator, FVector2D(CenterX, 205.0f));
			SetElementGeometry(*DownstreamLeaf, FVector2D(76.0f, 326.0f));
			SetElementGeometry(*DownstreamOperator, FVector2D(CenterX, 350.0f));
			SetElementGeometry(*Criterion, FVector2D(CenterX, 470.0f));
			SetElementGeometry(*Control, FVector2D(90.0f, 590.0f));
			Result.LocalSubtreeBounds = FBox2D(
				FVector2D(Margin, 28.0f),
				FVector2D(EffectiveWidth - Margin, 253.0f));
			Result.CanvasSize.Y = 650.0f;
		}
		else if (SelectedOperator == DownstreamOperator)
		{
			// The operands are placed by port here too: the value feeding A sits
			// left and high, B sits right and low, and the station is below
			// both, so each ribbon falls into its own slot without crossing.
			// The taller canvas is what buys the vertical room to do that at a
			// compact width.
			const bool bLeafHoldsFirstPort =
				ConsumedPortLabel(DownstreamLeaf, DownstreamOperator) <
				ConsumedPortLabel(UpstreamOperator, DownstreamOperator);
			const FVector2D FirstOperandSlot(100.0f, 250.0f);
			const FVector2D SecondOperandSlot(EffectiveWidth - 100.0f, 380.0f);
			SetElementGeometry(
				*DownstreamLeaf,
				bLeafHoldsFirstPort ? FirstOperandSlot : SecondOperandSlot);
			SetElementGeometry(
				*UpstreamOperator,
				bLeafHoldsFirstPort ? SecondOperandSlot : FirstOperandSlot);
			SetElementGeometry(*DownstreamOperator, FVector2D(CenterX, 480.0f));
			SetElementGeometry(*Criterion, FVector2D(CenterX, 600.0f));
			SetElementGeometry(*Control, FVector2D(90.0f, 710.0f));
			Result.LocalSubtreeBounds = FBox2D(
				FVector2D(Margin, 200.0f),
				FVector2D(EffectiveWidth - Margin, 530.0f));
			Result.CanvasSize.Y = 770.0f;
		}
	}

	if (!Result.bCompact)
	{
		// Stage headers carry the frozen left-to-right reading order:
		// origins, spine, criterion.
		AddLabel(
			Result,
			TEXT("stage:value-origins"),
			FString(),
			TEXT("VALUE ORIGINS"),
			EBlueprintLensLC3DerivationSpineLabelKind::Region,
			FVector2D(UpstreamInputs[0]->Bounds.Min.X + 12.0f, 14.0f));
		AddLabel(
			Result,
			TEXT("stage:derivation-spine"),
			FString(),
			TEXT("DERIVATION SPINE"),
			EBlueprintLensLC3DerivationSpineLabelKind::Region,
			FVector2D(UpstreamOperator->Bounds.Min.X - 18.0f, 14.0f));
		AddLabel(
			Result,
			TEXT("stage:criterion"),
			FString(),
			TEXT("CRITERION"),
			EBlueprintLensLC3DerivationSpineLabelKind::Region,
			FVector2D(Criterion->Bounds.Min.X, 14.0f));
	}

	for (const FBlueprintLensLC3DerivationSpineElement& Element : Result.Elements)
	{
		AddElementLabels(Result, Element);
	}
	if (SelectedOperator != nullptr)
	{
		AddQualifiedPortLabels(Result, Projection, *SelectedOperator);
		// The full caption spans half of a 430 px canvas, where it blocks every
		// route that has to enter the region from above. Compact therefore uses
		// a short caption anchored to the region's right edge, clear of the
		// operand column the incoming ribbons land in.
		const FString RegionText = Result.bCompact
			? FString::Printf(
				TEXT("SUBTREE \u00B7 %s"),
				*SelectedOperator->DisplayLabel.ToUpper())
			: FString::Printf(
				TEXT("LOCAL OPERATOR SUBTREE \u00B7 %s"),
				*SelectedOperator->DisplayLabel.ToUpper());
		const FVector2D RegionTextSize = MeasureText(
			RegionText,
			FontForKind(EBlueprintLensLC3DerivationSpineLabelKind::Region));
		AddLabel(
			Result,
			TEXT("local-subtree-region"),
			SelectedOperator->UnitId,
			RegionText,
			EBlueprintLensLC3DerivationSpineLabelKind::Region,
			Result.bCompact
				? FVector2D(
					Result.LocalSubtreeBounds.Max.X - RegionTextSize.X - 12.0f,
					Result.LocalSubtreeBounds.Min.Y + 9.0f)
				: Result.LocalSubtreeBounds.Min + FVector2D(12.0f, 9.0f));
	}

	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		const FBlueprintLensLC3DerivationSpineElement* Source =
			Result.FindElement(Step.ProducerUnitId);
		const FBlueprintLensLC3DerivationSpineElement* Target =
			Result.FindElement(Step.ConsumerUnitId);
		if (Source == nullptr || Target == nullptr)
		{
			Result.Routes.Reset();
			Result.DiagnosticCode =
				TEXT("LC3_D3_SURFACE_LABEL_CLEARANCE_FAILED");
			return Result;
		}
		const FRouteAnchors Anchors =
			RouteAnchorsFor(Result, *Source, *Target, Step.ConsumerPortLabel);
		const FVector2D RouteStart = Anchors.Start;
		const FVector2D RouteEnd = Anchors.End;
		float LeadIn = 0.0f;
		if (Anchors.Axis != ERibbonAxis::Vertical &&
			Source->Kind == EBlueprintLensLC3DerivationSpineElementKind::Value)
		{
			const FBlueprintLensLC3DerivationSpineLabel* OriginLabel =
				Result.Labels.FindByPredicate(
					[Source](const FBlueprintLensLC3DerivationSpineLabel& Label)
					{
						return Label.UnitId == Source->UnitId &&
							Label.Kind ==
								EBlueprintLensLC3DerivationSpineLabelKind::Value;
					});
			if (OriginLabel != nullptr)
			{
				LeadIn = FMath::Max(
					0.0f,
					static_cast<float>(
						OriginLabel->ExclusionBounds.Max.X - RouteStart.X) +
						10.0f);
			}
		}
		if (Source->Kind == EBlueprintLensLC3DerivationSpineElementKind::Value)
		{
			// The painted origin dot marks where the value leaves, so it has to
			// be the route's own start in whichever grammar is active.
			const FString SourceUnitId = Source->UnitId;
			FBlueprintLensLC3DerivationSpineElement* OriginElement =
				Result.Elements.FindByPredicate(
					[&SourceUnitId](
						const FBlueprintLensLC3DerivationSpineElement& Element)
					{
						return Element.UnitId == SourceUnitId;
					});
			if (OriginElement != nullptr)
			{
				OriginElement->OutputAnchor = RouteStart;
			}
		}
		if (!TryAddRoute(
				Result,
				Step.RelationId,
				Step.ProducerUnitId,
				Step.ConsumerUnitId,
				EBlueprintLensLayoutRelationFamily::Value,
				RouteStart,
				RouteEnd,
				BuildValueRibbon(RouteStart, RouteEnd, LeadIn, Anchors.Axis)))
		{
			Result.Routes.Reset();
			Result.DiagnosticCode =
				TEXT("LC3_D3_SURFACE_LABEL_CLEARANCE_FAILED");
			return Result;
		}
	}

	// The rail leaves the BeginPlay dot horizontally, clears its own label, and
	// turns up once under the criterion column. It never enters the value band.
	const FBlueprintLensLC3DerivationSpineLabel* ControlLabel =
		Result.Labels.FindByPredicate(
			[Control](const FBlueprintLensLC3DerivationSpineLabel& Label)
			{
				return Label.UnitId == Control->UnitId &&
					Label.Kind ==
						EBlueprintLensLC3DerivationSpineLabelKind::Control;
			});
	const float RailStartX = ControlLabel != nullptr
		? ControlLabel->ExclusionBounds.Max.X + ExecutionRailClearance
		: Control->Bounds.Max.X;
	const FVector2D ControlStart(
		RailStartX,
		BoxCenter(Control->Bounds).Y);
	const float RailTurnX = BoxCenter(Criterion->Bounds).X;
	const FVector2D ControlEnd(
		RailTurnX,
		Criterion->Bounds.Max.Y + ExecutionRailClearance);
	if (!TryAddRoute(
			Result,
			Projection.Control.RelationId,
			Projection.Control.ControllerUnitId,
			Projection.Control.TargetUnitId,
			EBlueprintLensLayoutRelationFamily::Execution,
			ControlStart,
			ControlEnd,
			BuildExecutionRail(ControlStart, RailTurnX, ControlEnd.Y)))
	{
		Result.Routes.Reset();
		Result.DiagnosticCode = TEXT("LC3_D3_SURFACE_LABEL_CLEARANCE_FAILED");
		return Result;
	}

	Result.DiagnosticCode = Result.HasNoRouteLabelIntersections() &&
		Result.HasNoLabelIntersections()
		? TEXT("LC3_D3_SURFACE_COMPLETE")
		: TEXT("LC3_D3_SURFACE_LABEL_CLEARANCE_FAILED");
	return Result;
}
