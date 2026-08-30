#include "SBlueprintLensLC2GuardCanvas.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FLinearColor D2Color(const TCHAR* Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

namespace D2Palette
{
const TCHAR* PanelFill = TEXT("#181b1e");
const TCHAR* PanelStroke = TEXT("#31363b");
const TCHAR* Outer = TEXT("#efc33f");
const TCHAR* Inner = TEXT("#62d9df");
const TCHAR* OuterRail = TEXT("#d99a70");
const TCHAR* InnerRail = TEXT("#b78be5");
const TCHAR* AcceptedRail = TEXT("#72d9ac");
const TCHAR* CriterionFill = TEXT("#493d1d");
const TCHAR* Text = TEXT("#eef5f7");
const TCHAR* Muted = TEXT("#aab6bd");
const TCHAR* Focus = TEXT("#f1c85d");
} // namespace D2Palette

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
			FSlateColor(D2Color(OutlineHex)),
			OutlineWidth,
			FVector2f(Radius * 2.0f, Radius * 2.0f)));
	return *Cache.Last().Value;
}

FLinearColor RailColor(const FString& GroupId)
{
	if (GroupId.Contains(TEXT("outer_rejected")))
	{
		return D2Color(D2Palette::OuterRail);
	}
	if (GroupId.Contains(TEXT("inner_rejected")))
	{
		return D2Color(D2Palette::InnerRail);
	}
	return D2Color(D2Palette::AcceptedRail);
}

FLinearColor GateColor(const FString& GroupId)
{
	return GroupId.Contains(TEXT("inner_guard"))
		? D2Color(D2Palette::Inner)
		: D2Color(D2Palette::Outer);
}

FVector2D BoxSize(const FBox2D& Box)
{
	return Box.Max - Box.Min;
}
} // namespace

void SBlueprintLensLC2GuardCanvas::Construct(const FArguments& InArgs)
{
	Projection = InArgs._Projection;
	Session = InArgs._InitialSession;
	Explanation = InArgs._Explanation;
	SelectedUnitId = InArgs._SelectedUnitId;
	OnUnitSelected = InArgs._OnUnitSelected;
	if (!Session.IsRenderable(Projection) && Explanation.IsValid())
	{
		Session = FBlueprintLensLC2GuardLayoutSession::Build(
			Projection,
			*Explanation,
			700.0f);
	}
	LastLayoutWidth = Session.Layout.LayoutRequest.TargetWidth;
	RebuildSurface();
	RebuildVisualChildren();
}

void SBlueprintLensLC2GuardCanvas::Tick(
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

FVector2D SBlueprintLensLC2GuardCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(
		FMath::Min(Surface.CanvasSize.X, 430.0f),
		Surface.CanvasSize.Y);
}

FString SBlueprintLensLC2GuardCanvas::SelectedGuardGroupId() const
{
	const FString Selection = SelectedUnitId.Get();
	for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
	{
		if (Compound.BranchUnitId == Selection ||
			Compound.PredicateUnitId == Selection)
		{
			return Compound.GroupId;
		}
	}
	return FString();
}

void SBlueprintLensLC2GuardCanvas::RebuildSession(const float TargetWidth)
{
	if (Explanation.IsValid())
	{
		const FBlueprintLensLC2GuardLayoutSessionResult Candidate =
			FBlueprintLensLC2GuardLayoutSession::Build(
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

void SBlueprintLensLC2GuardCanvas::RebuildSurface()
{
	Surface = FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
		Projection,
		Session,
		LastLayoutWidth,
		SelectedGuardGroupId());
}

TSharedRef<SWidget> SBlueprintLensLC2GuardCanvas::BuildSurfaceLabel(
	const FBlueprintLensLC2GuardSurfaceLabel& Label)
{
	const bool bCriterion = Label.Key.Contains(TEXT("criterion"));
	const bool bGate = Label.Key.Contains(TEXT("gate"));
	return SNew(STextBlock)
		.Text(FText::FromString(Label.Text))
		.Font(FAppStyle::Get().GetFontStyle(
			bCriterion || bGate ? "NormalFontBold" : "SmallFont"))
		.ColorAndOpacity(bCriterion
			? D2Color(D2Palette::Outer)
			: bGate
				? D2Color(D2Palette::Inner)
				: D2Color(D2Palette::Text))
		.AutoWrapText(true)
		.Visibility(EVisibility::HitTestInvisible);
}

void SBlueprintLensLC2GuardCanvas::RebuildVisualChildren()
{
	VisualCanvas = SNew(SCanvas);
	for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Surface.Labels)
	{
		VisualCanvas->AddSlot()
			.Position(Label.ExclusionBounds.Min)
			.Size(BoxSize(Label.ExclusionBounds))
		[
			BuildSurfaceLabel(Label)
		];
	}
	ChildSlot[VisualCanvas.ToSharedRef()];
}

int32 SBlueprintLensLC2GuardCanvas::OnPaint(
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
		&RoundedBrush(6.0f, D2Palette::PanelStroke, 1.0f),
		ESlateDrawEffect::None,
		D2Color(D2Palette::PanelFill));

	for (const FBlueprintLensLC2GuardSurfaceGate& Gate : Surface.Gates)
	{
		const FLinearColor Accent = GateColor(Gate.GroupId);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				BoxSize(Gate.Bounds),
				FSlateLayoutTransform(Gate.Bounds.Min)),
			&RoundedBrush(
				7.0f,
				Gate.GroupId.Contains(TEXT("inner_guard"))
					? D2Palette::Inner
					: D2Palette::Outer,
				Gate.bSelected ? 2.5f : 1.5f),
			ESlateDrawEffect::None,
			FLinearColor(Accent.R * 0.09f, Accent.G * 0.09f, Accent.B * 0.09f, 0.94f));
		if (Gate.bSelected)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 2,
				AllottedGeometry.ToPaintGeometry(
					BoxSize(Gate.FocusBounds),
					FSlateLayoutTransform(Gate.FocusBounds.Min)),
				&RoundedBrush(9.0f, D2Palette::Focus, 1.0f),
				ESlateDrawEffect::None,
				FLinearColor::Transparent);
		}
	}

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(
			BoxSize(Surface.CriterionDockBounds),
			FSlateLayoutTransform(Surface.CriterionDockBounds.Min)),
		&RoundedBrush(7.0f, D2Palette::Outer, 1.5f),
		ESlateDrawEffect::None,
		D2Color(D2Palette::CriterionFill));
	if (Surface.EntryRoutePoints.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(),
			Surface.EntryRoutePoints,
			ESlateDrawEffect::None,
			D2Color(D2Palette::Inner),
			true,
			3.0f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(9.0f, 9.0f),
				FSlateLayoutTransform(
					Surface.EntryRoutePoints[0] - FVector2D(4.5f, 4.5f))),
			&RoundedBrush(4.5f, D2Palette::PanelFill, 1.0f),
			ESlateDrawEffect::None,
			D2Color(D2Palette::Inner));
	}

	for (const FBlueprintLensLC2GuardSurfaceRail& Rail : Surface.Rails)
	{
		if (Rail.bFolded || Rail.Points.Num() < 2)
		{
			continue;
		}
		const FLinearColor Color = RailColor(Rail.GroupId);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(),
			Rail.Points,
			ESlateDrawEffect::None,
			FLinearColor(0.02f, 0.025f, 0.025f, 0.95f),
			true,
			7.0f);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(),
			Rail.Points,
			ESlateDrawEffect::None,
			Color,
			true,
			4.0f);
		for (const FVector2D& Station : {Rail.Points[0], Rail.Points.Last()})
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 5,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(9.0f, 9.0f),
					FSlateLayoutTransform(Station - FVector2D(4.5f, 4.5f))),
				&RoundedBrush(4.5f, D2Palette::PanelFill, 1.0f),
				ESlateDrawEffect::None,
				Color);
		}
	}

	for (const FBlueprintLensLC2GuardForkMark& Mark : Surface.ForkMarks)
	{
		const FBlueprintLensLC2GuardSurfaceGate* Gate =
			Surface.Gates.FindByPredicate(
				[&Mark](const FBlueprintLensLC2GuardSurfaceGate& Candidate)
				{
					return Candidate.BranchUnitId == Mark.BranchUnitId;
				});
		if (Gate == nullptr)
		{
			continue;
		}
		const bool bWide = Surface.CanvasSize.Y <= 430.0f;
		const FVector2D Centre(
			Gate->Bounds.Min.X + 27.0f,
			Gate->Bounds.Min.Y + (bWide
				? (Gate->bSelected ? 77.0f : 53.0f)
				: (Gate->bSelected ? 75.0f : 73.0f)));
		const FLinearColor Accent = GateColor(Gate->GroupId);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 5,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(20.0f, 20.0f),
				FSlateLayoutTransform(Centre - FVector2D(10.0f, 10.0f))),
			&RoundedBrush(10.0f, D2Palette::PanelFill, 1.0f),
			ESlateDrawEffect::None,
			Accent);
		const TArray<FVector2D> ForkGlyph = {
			Centre + FVector2D(-5.0f, -1.0f),
			Centre + FVector2D(0.0f, -1.0f),
			Centre + FVector2D(0.0f, -6.0f),
			Centre + FVector2D(0.0f, 5.0f),
			Centre + FVector2D(5.0f, 5.0f)};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 6,
			AllottedGeometry.ToPaintGeometry(),
			ForkGlyph,
			ESlateDrawEffect::None,
			D2Color(D2Palette::PanelFill),
			true,
			1.5f);
	}

	return SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId + 7,
		InWidgetStyle,
		bParentEnabled);
}

FReply SBlueprintLensLC2GuardCanvas::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(
		MouseEvent.GetScreenSpacePosition());
	const FString UnitId = ResolveUnitAtLocalPositionForTesting(Local);
	if (!UnitId.IsEmpty())
	{
		OnUnitSelected.ExecuteIfBound(UnitId);
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FString SBlueprintLensLC2GuardCanvas::ResolveUnitAtLocalPositionForTesting(
	const FVector2D& LocalPosition) const
{
	for (const FBlueprintLensLC2GuardSurfaceLabel& Label : Surface.Labels)
	{
		if (!Label.UnitId.IsEmpty() &&
			Label.ExclusionBounds.IsInside(LocalPosition))
		{
			return Label.UnitId;
		}
	}
	for (int32 Index = Surface.Gates.Num() - 1; Index >= 0; --Index)
	{
		const FBlueprintLensLC2GuardSurfaceGate& Gate = Surface.Gates[Index];
		if (Gate.Bounds.IsInside(LocalPosition))
		{
			return Gate.BranchUnitId;
		}
	}
	return FString();
}
