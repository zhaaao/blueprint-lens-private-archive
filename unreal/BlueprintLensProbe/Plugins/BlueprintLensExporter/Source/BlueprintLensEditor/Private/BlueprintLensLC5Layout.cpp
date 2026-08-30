#include "BlueprintLensLC5Layout.h"

#include "Fonts/CompositeFont.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace
{
struct FRelationSpec
{
	FString RelationId;
	FString SourceOccurrenceId;
	FString TargetOccurrenceId;
	EBlueprintLensLayoutRelationFamily Family = EBlueprintLensLayoutRelationFamily::Execution;
	bool bParticipatesInRank = true;
};

struct FRouteGeometry
{
	FVector2D Source = FVector2D::ZeroVector;
	FVector2D Target = FVector2D::ZeroVector;
	TArray<FVector2D> Bends;
	bool bValid = false;
};

FRouteGeometry RouteGeometryFor(
	const EBlueprintLensLC5LayoutMode Mode,
	const FString& RelationId)
{
	FRouteGeometry Route;
	const bool bWide = Mode == EBlueprintLensLC5LayoutMode::SideBySide700;
	const bool bIs430 = Mode == EBlueprintLensLC5LayoutMode::SingleColumn430;
	if (RelationId.StartsWith(TEXT("context:call_enter:")))
	{
		Route.Source = bWide ? FVector2D(208.0f, 286.0f) :
			FVector2D(bIs430 ? 90.0f : 94.0f, 352.0f);
		Route.Target = bWide ? FVector2D(316.0f, 286.0f) :
			FVector2D(Route.Source.X, 460.0f);
		if (bWide) Route.Bends = {FVector2D(254.0f, 286.0f)};
	}
	else if (RelationId.StartsWith(TEXT("binding:argument:0:")))
	{
		Route.Source = bWide ? FVector2D(208.0f, 338.0f) :
			FVector2D(bIs430 ? 215.0f : 240.0f, 352.0f);
		Route.Target = bWide ? FVector2D(316.0f, 338.0f) :
			FVector2D(Route.Source.X, 460.0f);
		if (bWide) Route.Bends = {FVector2D(254.0f, 338.0f)};
	}
	else if (RelationId.StartsWith(TEXT("binding:argument:1:")))
	{
		Route.Source = bWide ? FVector2D(208.0f, 378.0f) :
			FVector2D(bIs430 ? 340.0f : 386.0f, 352.0f);
		Route.Target = bWide ? FVector2D(316.0f, 366.0f) :
			FVector2D(Route.Source.X, 460.0f);
		if (bWide) Route.Bends = {FVector2D(254.0f, 378.0f)};
	}
	else if (RelationId.StartsWith(TEXT("binding:result:2:")))
	{
		Route.Source = bWide ? FVector2D(370.0f, 674.0f) :
			FVector2D(bIs430 ? 94.0f : 98.0f, 844.0f);
		Route.Target = bWide ? FVector2D(208.0f, 390.0f) :
			FVector2D(bIs430 ? 60.0f : 64.0f, 312.0f);
		Route.Bends = bWide
			? TArray<FVector2D>{FVector2D(300.0f, 674.0f), FVector2D(300.0f, 390.0f)}
			: TArray<FVector2D>{FVector2D(bIs430 ? 50.0f : 54.0f, 844.0f),
				FVector2D(bIs430 ? 50.0f : 54.0f, 312.0f)};
	}
	else if (RelationId.StartsWith(TEXT("context:call_return:")))
	{
		Route.Source = bWide ? FVector2D(370.0f, 662.0f) :
			FVector2D(bIs430 ? 336.0f : 382.0f, 870.0f);
		Route.Target = bWide ? FVector2D(208.0f, 398.0f) :
			FVector2D(bIs430 ? 370.0f : 416.0f, 334.0f);
		Route.Bends = bWide
			? TArray<FVector2D>{FVector2D(254.0f, 662.0f), FVector2D(254.0f, 398.0f)}
			: TArray<FVector2D>{FVector2D(bIs430 ? 388.0f : 434.0f, 870.0f),
				FVector2D(bIs430 ? 388.0f : 434.0f, 334.0f)};
	}
	else if (RelationId.StartsWith(TEXT("internal:")) && RelationId.Contains(TEXT("output-Bonus")))
	{
		Route.Source = bWide ? FVector2D(340.0f, 370.0f) :
			FVector2D(bIs430 ? 114.0f : 118.0f, 576.0f);
		Route.Target = bWide ? FVector2D(406.0f, 444.0f) :
			FVector2D(bIs430 ? 148.0f : 152.0f, 666.0f);
		Route.Bends = {FVector2D(Route.Source.X, Route.Target.Y)};
	}
	else if (RelationId.StartsWith(TEXT("internal:")) && RelationId.Contains(TEXT("output-CurrentHealth")))
	{
		Route.Source = bWide ? FVector2D(382.0f, 370.0f) :
			FVector2D(bIs430 ? 215.0f : 240.0f, 576.0f);
		Route.Target = bWide ? FVector2D(450.0f, 444.0f) :
			FVector2D(Route.Source.X, 666.0f);
		if (bWide) Route.Bends = {FVector2D(382.0f, 424.0f)};
	}
	else if (RelationId.StartsWith(TEXT("internal:")) && RelationId.Contains(TEXT("output-then")))
	{
		Route.Source = bWide ? FVector2D(608.0f, 350.0f) :
			FVector2D(bIs430 ? 370.0f : 416.0f, 534.0f);
		Route.Target = bWide ? FVector2D(600.0f, 630.0f) :
			FVector2D(bIs430 ? 336.0f : 382.0f, 850.0f);
		Route.Bends = bWide
			? TArray<FVector2D>{FVector2D(630.0f, 350.0f), FVector2D(630.0f, 630.0f)}
			: TArray<FVector2D>{FVector2D(bIs430 ? 380.0f : 426.0f, 534.0f),
				FVector2D(bIs430 ? 380.0f : 426.0f, 850.0f)};
	}
	else if (RelationId.StartsWith(TEXT("internal:")) && RelationId.Contains(TEXT("output-ReturnValue")))
	{
		Route.Source = bWide ? FVector2D(500.0f, 514.0f) :
			FVector2D(bIs430 ? 215.0f : 240.0f, 740.0f);
		Route.Target = bWide ? FVector2D(500.0f, 606.0f) :
			FVector2D(Route.Source.X, 812.0f);
	}
	else
	{
		return Route;
	}
	Route.bValid = true;
	return Route;
}

bool StrictlyIntersects(const FBox2D& A, const FBox2D& B)
{
	return A.bIsValid && B.bIsValid && A.Min.X < B.Max.X && A.Max.X > B.Min.X &&
		A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
}

bool SegmentIntersectsBox(
	const FVector2D& Start,
	const FVector2D& End,
	const FBox2D& Bounds)
{
	if (!Bounds.bIsValid)
	{
		return false;
	}
	const FVector2D Delta = End - Start;
	float Enter = 0.0f;
	float Exit = 1.0f;
	const auto Clip = [&Enter, &Exit](const float P, const float Q)
	{
		if (FMath::IsNearlyZero(P))
		{
			return Q >= 0.0f;
		}
		const float Ratio = Q / P;
		if (P < 0.0f)
		{
			if (Ratio > Exit)
			{
				return false;
			}
			Enter = FMath::Max(Enter, Ratio);
		}
		else
		{
			if (Ratio < Enter)
			{
				return false;
			}
			Exit = FMath::Min(Exit, Ratio);
		}
		return true;
	};
	return Clip(-Delta.X, Start.X - Bounds.Min.X) &&
		Clip(Delta.X, Bounds.Max.X - Start.X) &&
		Clip(-Delta.Y, Start.Y - Bounds.Min.Y) &&
		Clip(Delta.Y, Bounds.Max.Y - Start.Y) &&
		Enter <= Exit;
}

FBox2D Box(const float X, const float Y, const float Width, const float Height)
{
	return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
}

FVector2D NodeSizeFor(
	const EBlueprintLensLC5LayoutMode Mode,
	const FString& OccurrenceId,
	const FString& CallId,
	const FString& EntryId,
	const FString& OperatorId,
	const FString& ReturnId)
{
	if (Mode == EBlueprintLensLC5LayoutMode::SideBySide700)
	{
		if (OccurrenceId == CallId)
		{
			return FVector2D(156.0f, 124.0f);
		}
		if (OccurrenceId == EntryId)
		{
			return FVector2D(320.0f, 92.0f);
		}
		return OccurrenceId == OperatorId ? FVector2D(230.0f, 70.0f) :
			OccurrenceId == ReturnId ? FVector2D(230.0f, 76.0f) : FVector2D::ZeroVector;
	}
	const bool bIs430 = Mode == EBlueprintLensLC5LayoutMode::SingleColumn430;
	return OccurrenceId == EntryId ? FVector2D(bIs430 ? 310.0f : 352.0f, 116.0f) :
		OccurrenceId == CallId ? FVector2D(bIs430 ? 310.0f : 352.0f, 116.0f) :
		OccurrenceId == OperatorId ? FVector2D(bIs430 ? 242.0f : 284.0f, 74.0f) :
		OccurrenceId == ReturnId ? FVector2D(bIs430 ? 242.0f : 284.0f, 80.0f) :
		FVector2D::ZeroVector;
}

FVector2D NodePositionFor(
	const EBlueprintLensLC5LayoutMode Mode,
	const FString& OccurrenceId,
	const FString& CallId,
	const FString& EntryId,
	const FString& OperatorId,
	const FString& ReturnId)
{
	if (Mode == EBlueprintLensLC5LayoutMode::SingleColumn430)
	{
		return OccurrenceId == CallId ? FVector2D(60.0f, 236.0f) :
			OccurrenceId == EntryId ? FVector2D(60.0f, 460.0f) :
			OccurrenceId == OperatorId ? FVector2D(94.0f, 666.0f) :
			OccurrenceId == ReturnId ? FVector2D(94.0f, 812.0f) : FVector2D::ZeroVector;
	}
	if (Mode == EBlueprintLensLC5LayoutMode::StackedDetail480)
	{
		return OccurrenceId == CallId ? FVector2D(64.0f, 236.0f) :
			OccurrenceId == EntryId ? FVector2D(64.0f, 460.0f) :
			OccurrenceId == OperatorId ? FVector2D(98.0f, 666.0f) :
			OccurrenceId == ReturnId ? FVector2D(98.0f, 812.0f) : FVector2D::ZeroVector;
	}
	return OccurrenceId == CallId ? FVector2D(52.0f, 278.0f) :
		OccurrenceId == EntryId ? FVector2D(316.0f, 278.0f) :
		OccurrenceId == OperatorId ? FVector2D(370.0f, 444.0f) :
		OccurrenceId == ReturnId ? FVector2D(370.0f, 606.0f) : FVector2D::ZeroVector;
}

const FBlueprintLensLayoutNodePlacement* FindNode(
	const FBlueprintLensLayoutLedger& Ledger, const FString& UnitId)
{
	return Ledger.Nodes.FindByPredicate([&UnitId](const auto& Node)
	{
		return Node.UnitId == UnitId;
	});
}

void AddLabel(
	FBlueprintLensLC5Layout& Layout,
	const FString& Id,
	const FString& Text,
	const FBox2D& Bounds,
	const int32 FontSize,
	const bool bBold = false,
	const FString& ColorHex = TEXT("#F2F5F8"))
{
	FBlueprintLensLC5Label Label;
	Label.Id = Id;
	Label.Text = Text;
	Label.Bounds = Bounds;
	Label.FontSize = FontSize;
	Label.bBold = bBold;
	Label.ColorHex = ColorHex;
	Layout.Labels.Add(MoveTemp(Label));
}

void BuildDisplayGeometry(FBlueprintLensLC5Layout& Layout)
{
	const float Width = Layout.CanvasSize.X;
	if (Layout.Mode == EBlueprintLensLC5LayoutMode::SideBySide700)
	{
		Layout.HeaderBounds = Box(24.0f, 24.0f, 652.0f, 124.0f);
		Layout.CriterionBounds = Box(36.0f, 76.0f, 170.0f, 34.0f);
		Layout.PlotBounds = Box(24.0f, 164.0f, 652.0f, 610.0f);
		Layout.CallerBounds = Box(36.0f, 184.0f, 188.0f, 570.0f);
		Layout.CalleeBounds = Box(292.0f, 184.0f, 372.0f, 570.0f);
		Layout.PortalBounds = Box(250.0f, 208.0f, 8.0f, 522.0f);
		Layout.PortalStart = FVector2D(254.0f, 208.0f);
		Layout.PortalEnd = FVector2D(254.0f, 730.0f);
		Layout.FrontierBounds = Box(24.0f, 798.0f, 652.0f, 92.0f);
		Layout.ActionsBounds = Box(24.0f, 914.0f, 652.0f, 62.0f);
	}
	else
	{
		const bool bIs430 = Layout.Mode == EBlueprintLensLC5LayoutMode::SingleColumn430;
		Layout.HeaderBounds = Box(24.0f, 24.0f, Width - 48.0f, 132.0f);
		Layout.CriterionBounds = Box(36.0f, 86.0f, 170.0f, 34.0f);
		Layout.PlotBounds = Box(24.0f, 176.0f, Width - 48.0f, bIs430 ? 810.0f : 730.0f);
		Layout.CallerBounds = Box(bIs430 ? 46.0f : 50.0f, 196.0f,
			bIs430 ? 338.0f : 380.0f, 178.0f);
		Layout.PortalBounds = Box(bIs430 ? 54.0f : 58.0f, 396.0f,
			bIs430 ? 322.0f : 364.0f, 8.0f);
		Layout.PortalStart = FVector2D(bIs430 ? 54.0f : 58.0f, 400.0f);
		Layout.PortalEnd = FVector2D(bIs430 ? 376.0f : 422.0f, 400.0f);
		Layout.CalleeBounds = Box(bIs430 ? 46.0f : 50.0f, 424.0f,
			bIs430 ? 338.0f : 380.0f, 482.0f);
		Layout.FrontierBounds = Box(24.0f, bIs430 ? 1010.0f : 930.0f,
			Width - 48.0f, 118.0f);
		Layout.ActionsBounds = Box(24.0f, bIs430 ? 1148.0f : 1068.0f,
			Width - 48.0f, 108.0f);
	}

	const bool bWide = Layout.Mode == EBlueprintLensLC5LayoutMode::SideBySide700;
	const bool bIs430 = Layout.Mode == EBlueprintLensLC5LayoutMode::SingleColumn430;
	if (bWide)
	{
		AddLabel(Layout, TEXT("question"),
			TEXT("How does CalculateRecovery use CurrentHealth and Bonus to produce NewHealth,"),
			Box(36.0f, 38.0f, Width - 72.0f, 14.0f), 9, true);
		AddLabel(Layout, TEXT("question.1"),
			TEXT("and how do those values correspond across the call boundary?"),
			Box(36.0f, 56.0f, 390.0f, 14.0f), 9, true);
	}
	else
	{
		AddLabel(Layout, TEXT("question"),
			TEXT("How does CalculateRecovery use CurrentHealth and Bonus\nto produce NewHealth across the call boundary?"),
			Box(36.0f, 38.0f, Width - 72.0f, 34.0f), 9, true);
	}
	AddLabel(Layout, TEXT("criterion"), TEXT("NewHealth · criterion"),
		Box(48.0f, bWide ? 85.0f : 95.0f, 150.0f, 16.0f), 12, true);
	AddLabel(Layout, TEXT("condition"), TEXT("Selected · Typed Portal Bridge"),
		Box(bWide ? 436.0f : 226.0f, bWide ? 58.0f : 96.0f,
			bWide ? 210.0f : Width - 250.0f, 14.0f), 9, true, TEXT("#D997FF"));
	AddLabel(Layout, TEXT("scope"), TEXT("Static contextual slice · depth 1"),
		Box(bWide ? 436.0f : 36.0f, bWide ? 88.0f : 132.0f,
			bWide ? 210.0f : 180.0f, 14.0f), bWide ? 10 : 9, false, TEXT("#A9B3C1"));
	AddLabel(Layout, TEXT("width"), FString::Printf(TEXT("%.0fpx · %s"), Width,
		bWide ? TEXT("side by side") : bIs430 ? TEXT("single column") : TEXT("stacked detail")),
		Box(226.0f + (bWide ? 210.0f : 0.0f), bWide ? 108.0f : 120.0f,
			180.0f, 13.0f), 8, false, TEXT("#A9B3C1"));
	AddLabel(Layout, TEXT("caller.region"), TEXT("CALLER · EventGraph"),
		Box(Layout.CallerBounds.Min.X + 12.0f, 200.0f, 160.0f, 14.0f), bWide ? 10 : 9, true, TEXT("#67B7FF"));
	AddLabel(Layout, TEXT("callee.region"), TEXT("CALLEE · CalculateRecovery"),
		Box(bWide ? 306.0f : 98.0f, bWide ? 200.0f : 436.0f, 190.0f, 14.0f), bWide ? 10 : 9, true, TEXT("#D997FF"));
	AddLabel(Layout, TEXT("portal"), bWide ? TEXT("STATIC PORTAL") : TEXT("STATIC TYPED PORTAL"),
		Box(bWide ? 262.0f : 100.0f, bWide ? 214.0f : 382.0f,
			bWide ? 96.0f : 150.0f, 13.0f), 8, true, TEXT("#A9B3C1"));

	for (const FBlueprintLensLayoutNodePlacement& Node : Layout.VisualOracleLedger.Nodes)
	{
		const FString Text = Node.UnitId == Layout.CallOccurrenceId ? TEXT("CalculateRecovery") :
			Node.UnitId == Layout.EntryOccurrenceId ? TEXT("Entry · CurrentHealth, Bonus") :
			Node.UnitId == Layout.OperatorOccurrenceId ? TEXT("CurrentHealth + Bonus") :
			TEXT("Return · NewHealth");
		AddLabel(Layout, TEXT("node.") + Node.UnitId, Text,
			Box(Node.Position.X + (bWide ? 12.0f : 14.0f),
				Node.Position.Y + (bWide ? 16.0f : 16.0f), Node.Size.X - 24.0f, 15.0f),
			bWide ? 11 : 10, true);
	}
	const auto NodeAt = [&Layout](const FString& Id)
	{
		return FindNode(Layout.VisualOracleLedger, Id);
	};
	const FBlueprintLensLayoutNodePlacement* Call = NodeAt(Layout.CallOccurrenceId);
	const FBlueprintLensLayoutNodePlacement* Entry = NodeAt(Layout.EntryOccurrenceId);
	const FBlueprintLensLayoutNodePlacement* Return = NodeAt(Layout.ReturnOccurrenceId);
	AddLabel(Layout, TEXT("call.current"), TEXT("CurrentHealth: int32"),
		Box(Call->Position.X + 12.0f, bWide ? 326.0f : 286.0f, 135.0f, 13.0f), 9, false, TEXT("#F0B35A"));
	AddLabel(Layout, TEXT("call.bonus"), TEXT("Bonus: int32"),
		Box(Call->Position.X + 12.0f, bWide ? 350.0f : 308.0f, 110.0f, 13.0f), 9, false, TEXT("#F0B35A"));
	AddLabel(Layout, TEXT("call.result"), TEXT("NewHealth: int32"),
		Box(Call->Position.X + 12.0f, bWide ? 374.0f : 330.0f, 125.0f, 13.0f), 9, false, TEXT("#D997FF"));
	AddLabel(Layout, TEXT("entry.current"), TEXT("CurrentHealth: int32"),
		Box(Entry->Position.X + 14.0f, bWide ? 326.0f : 510.0f, 140.0f, 13.0f), 9, false, TEXT("#F0B35A"));
	AddLabel(Layout, TEXT("entry.bonus"), TEXT("Bonus: int32"),
		Box(bWide ? 488.0f : Entry->Position.X + 14.0f, bWide ? 326.0f : 532.0f, 110.0f, 13.0f), 9, false, TEXT("#F0B35A"));
	AddLabel(Layout, TEXT("return.type"), TEXT("NewHealth: int32"),
		Box(Return->Position.X + 14.0f, bWide ? 650.0f : 858.0f, 125.0f, 13.0f), 9, false, TEXT("#D997FF"));

	if (bWide)
	{
		AddLabel(Layout, TEXT("frontier"), TEXT("Frontier · depth 1 · macro, impure, latent, cross-Blueprint and dynamic dispatch excluded"),
			Box(40.0f, 818.0f, 620.0f, 15.0f), 10, true, TEXT("#F07178"));
		AddLabel(Layout, TEXT("static.boundary"), TEXT("Static boundary · occurrences are not runtime invocations; no runtime order is claimed"),
			Box(40.0f, 850.0f, 620.0f, 15.0f), 10, false, TEXT("#A9B3C1"));
	}
	else
	{
		const float FrontierY = bIs430 ? 1030.0f : 950.0f;
		AddLabel(Layout, TEXT("frontier"), TEXT("Frontier · depth 1"), Box(40.0f, FrontierY, 180.0f, 14.0f), 10, true, TEXT("#F07178"));
		AddLabel(Layout, TEXT("frontier.reason"), TEXT("macro, impure, latent, cross-Blueprint\nand dynamic dispatch excluded"),
			Box(40.0f, FrontierY + 26.0f, 300.0f, 32.0f), 9, false, TEXT("#F07178"));
		AddLabel(Layout, TEXT("static.boundary"), TEXT("Static occurrences; not runtime invocations.\nNo runtime order is claimed."),
			Box(40.0f, FrontierY + 60.0f, 300.0f, 34.0f), 9, false, TEXT("#A9B3C1"));
	}

	const float NarrowShift = bIs430 ? 0.0f : 4.0f;
	const auto RelationLabel = [&Layout](const FString& Id, const FString& Text,
		const float X, const float Y, const float W, const FString& ColorHex, const int32 FontSize)
	{
		AddLabel(Layout, Id, Text, Box(X, Y, W, FontSize >= 8 ? 11.0f : 9.0f),
			FontSize, false, ColorHex);
	};
	RelationLabel(TEXT("relation.call_enter"), TEXT("static call_enter"), bWide ? 270.0f : 98.0f + NarrowShift,
		bWide ? 236.0f : 408.0f, 120.0f, TEXT("#67B7FF"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.argument.current_health"), TEXT("argument · CurrentHealth: int32"),
		bWide ? 330.0f : 228.0f + NarrowShift * 12.5f, bWide ? 312.0f : 586.0f, 190.0f, TEXT("#F0B35A"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.argument.bonus"), TEXT("argument · Bonus: int32"),
		bWide ? 330.0f : 256.0f + NarrowShift * 11.5f, bWide ? 354.0f : 574.0f, 160.0f, TEXT("#F0B35A"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.internal.bonus"), TEXT("internal data · Bonus"),
		bWide ? 430.0f : 124.0f + NarrowShift, bWide ? 382.0f : 600.0f, 145.0f, TEXT("#A7D46F"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.internal.current_health"), TEXT("internal data · CurrentHealth"),
		bWide ? 430.0f : 223.0f + NarrowShift * 6.25f, bWide ? 400.0f : 622.0f, 180.0f, TEXT("#A7D46F"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.internal.execution"), TEXT("internal execution · execution"),
		bWide ? 514.0f : 242.0f + NarrowShift * 11.5f, bWide ? 538.0f : 632.0f, 185.0f, TEXT("#A7D46F"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.internal.return_value"), TEXT("internal data · ReturnValue"),
		bWide ? 514.0f : 225.0f + NarrowShift * 6.25f, bWide ? 558.0f : 762.0f, 180.0f, TEXT("#A7D46F"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.result.new_health"), TEXT("result · NewHealth: int32"),
		bWide ? 392.0f : 62.0f + NarrowShift, bWide ? 690.0f : 930.0f, 170.0f, TEXT("#D997FF"), bWide ? 8 : 7);
	RelationLabel(TEXT("relation.call_return"), TEXT("static call_return"),
		bWide ? 420.0f : 244.0f + NarrowShift * 11.5f, bWide ? 730.0f : 930.0f, 130.0f, TEXT("#67B7FF"), bWide ? 8 : 7);

	const TArray<TPair<FString, FString>> Actions = {
		{TEXT("open_source"), TEXT("Open source")}, {TEXT("select"), TEXT("Select")},
		{TEXT("show_complete_text"), TEXT("Show complete text")},
		{TEXT("show_evidence"), TEXT("Show evidence")}};
	const float Gap = 12.0f;
	const float ActionWidth = bWide ? 140.0f :
		(Layout.ActionsBounds.GetSize().X - 24.0f - Gap) / 2.0f;
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		FBlueprintLensLC5ActionLayout Action;
		Action.ActionId = Actions[Index].Key;
		Action.Label = Actions[Index].Value;
		const int32 Column = bWide ? Index : Index % 2;
		const int32 Row = bWide ? 0 : Index / 2;
		const float WideX[] = {36.0f, 188.0f, 340.0f, 512.0f};
		const float WideWidth[] = {140.0f, 140.0f, 160.0f, 152.0f};
		Action.Bounds = Box(bWide ? WideX[Index] :
			Layout.ActionsBounds.Min.X + 12.0f + Column * (ActionWidth + Gap),
			Layout.ActionsBounds.Min.Y + 14.0f + Row * 46.0f,
			bWide ? WideWidth[Index] : ActionWidth, 34.0f);
		Layout.Actions.Add(MoveTemp(Action));
	}
}

FBlueprintLensLC5Layout BuildLiveLayout(
	const FBlueprintLensLC5Projection& Projection,
	const float RequestedWidth)
{
	FBlueprintLensLC5Layout Result;
	const float Width = FMath::Clamp(RequestedWidth, 430.0f, 700.0f);
	const bool bWide = Width >= 700.0f;
	Result.Mode = bWide
		? EBlueprintLensLC5LayoutMode::SideBySide700
		: Width <= 430.0f
			? EBlueprintLensLC5LayoutMode::SingleColumn430
			: EBlueprintLensLC5LayoutMode::StackedDetail480;
	Result.CallOccurrenceId = Projection.Occurrences[0].OccurrenceId;
	const FBlueprintLensLC5Occurrence* Entry =
		Projection.Occurrences.FindByPredicate(
			[](const FBlueprintLensLC5Occurrence& Occurrence)
			{
				return Occurrence.Role == TEXT("function_entry");
			});
	Result.EntryOccurrenceId = Entry != nullptr
		? Entry->OccurrenceId
		: Projection.Occurrences[1].OccurrenceId;
	Result.ReturnOccurrenceId = Projection.Occurrences.Last().OccurrenceId;
	Result.StaticRanks = Projection.LiveStaticRanks;

	const int32 BodyCount = Projection.Occurrences.Num() - 1;
	int32 MaxStaticRank = 0;
	TMap<int32, int32> UnitsPerRank;
	for (const TPair<FString, int32>& Rank : Projection.LiveStaticRanks)
	{
		MaxStaticRank = FMath::Max(MaxStaticRank, Rank.Value);
		++UnitsPerRank.FindOrAdd(Rank.Value);
	}
	const int32 Rows = MaxStaticRank + 1;
	const float HeaderBottom = bWide ? 212.0f : 276.0f;
	const float PlotTop = HeaderBottom + 12.0f;
	const float BodyTop = bWide ? 270.0f : 462.0f;
	const float BodyRowCadence = 74.0f;
	const float CalleeBottom = BodyTop +
		FMath::Max(0, Rows - 1) * BodyRowCadence + 80.0f;
	const float PlotBottom = CalleeBottom + 20.0f;
	const float FrontierTop = PlotBottom + 18.0f;
	const float ActionsTop = FrontierTop + 108.0f;
	Result.CanvasSize = FVector2D(Width, ActionsTop + 82.0f);
	Result.HeaderBounds = Box(24.0f, 24.0f, Width - 48.0f, HeaderBottom - 36.0f);
	Result.CriterionBounds = Box(36.0f, 84.0f, 220.0f, 34.0f);
	Result.PlotBounds = Box(24.0f, PlotTop, Width - 48.0f, PlotBottom - PlotTop);
	if (bWide)
	{
		Result.CallerBounds = Box(36.0f, 230.0f, 196.0f, PlotBottom - 250.0f);
		Result.CalleeBounds = Box(284.0f, 230.0f, Width - 308.0f, PlotBottom - 250.0f);
		Result.PortalBounds = Box(250.0f, 248.0f, 8.0f, PlotBottom - 282.0f);
		Result.PortalStart = FVector2D(254.0f, 248.0f);
		Result.PortalEnd = FVector2D(254.0f, PlotBottom - 34.0f);
	}
	else
	{
		Result.CallerBounds = Box(36.0f, 294.0f, Width - 72.0f, 104.0f);
		Result.CalleeBounds = Box(36.0f, 426.0f, Width - 72.0f, PlotBottom - 446.0f);
		Result.PortalBounds = Box(54.0f, 410.0f, Width - 108.0f, 8.0f);
		Result.PortalStart = FVector2D(54.0f, 414.0f);
		Result.PortalEnd = FVector2D(Width - 54.0f, 414.0f);
	}
	Result.FrontierBounds = Box(24.0f, FrontierTop, Width - 48.0f, 90.0f);
	Result.ActionsBounds = Box(24.0f, ActionsTop, Width - 48.0f, 62.0f);

	AddLabel(
		Result,
		TEXT("live.question"),
		FString::Printf(
			TEXT("What exported body lies behind the %s call boundary?"),
			*Projection.CalleeGraphName),
		Box(38.0f, 38.0f, Width - 76.0f, 20.0f),
		12,
		true);
	AddLabel(
		Result,
		TEXT("live.source"),
		FString::Printf(
			TEXT("Source · %s"),
			*Projection.SourceBlueprintAssetPath),
		Box(38.0f, 62.0f, Width - 76.0f, 16.0f),
		9,
		false,
		TEXT("#A9B3C1"));
	const float StateY = bWide ? 94.0f : 122.0f;
	const float ScopeY = bWide ? 128.0f : 150.0f;
	const float OrderY = bWide ? 146.0f : 172.0f;
	const float KeyY = bWide ? 178.0f : 220.0f;
	AddLabel(
		Result,
		TEXT("live.criterion"),
		Projection.CalleeGraphName + TEXT(" · selected call boundary"),
		bWide
			? Box(48.0f, 94.0f, 196.0f, 16.0f)
			: Box(48.0f, 94.0f, Width - 96.0f, 22.0f),
		10,
		true);
	AddLabel(
		Result,
		TEXT("live.state"),
		Projection.ClaimState,
		bWide
			? Box(276.0f, StateY, Width - 314.0f, 16.0f)
			: Box(48.0f, StateY, Width - 96.0f, 20.0f),
		9,
		true,
		TEXT("#D997FF"));
	AddLabel(
		Result,
		TEXT("live.scope"),
		FString::Printf(
			TEXT("Static typed-IR body · %d nodes · %d relations"),
			BodyCount,
			Projection.InternalRelations.Num()),
		Box(38.0f, ScopeY, Width - 76.0f, 16.0f),
		9,
		false,
		TEXT("#A9B3C1"));
	AddLabel(
		Result,
		TEXT("live.order"),
		Projection.StaticOrderStatement,
		Box(38.0f, OrderY, Width - 76.0f, bWide ? 28.0f : 44.0f),
		9,
		true,
		TEXT("#DCE6EF"));
	TArray<FString> LegendLabels;
	for (const FBlueprintLensLC5LegendEntry& LegendEntry : Projection.LegendEntries)
	{
		LegendLabels.Add(LegendEntry.ReaderLabel);
	}
	AddLabel(
		Result,
		TEXT("live.reading_key"),
		TEXT("KEY · ") + FString::Join(LegendLabels, TEXT(" · ")),
		Box(38.0f, KeyY, Width - 76.0f, bWide ? 18.0f : 38.0f),
		8,
		false,
		TEXT("#A9B3C1"));
	FString CallerGraphName = Projection.CallerGraphId;
	const int32 CallerDelimiter = CallerGraphName.Find(
		TEXT(":"),
		ESearchCase::CaseSensitive,
		ESearchDir::FromEnd);
	if (CallerDelimiter != INDEX_NONE)
	{
		CallerGraphName = CallerGraphName.Mid(CallerDelimiter + 1);
	}
	AddLabel(
		Result,
		TEXT("live.caller.region"),
		TEXT("CALL SITE · ") +
			Projection.LiveOccurrenceLabels.FindRef(Result.CallOccurrenceId) +
			TEXT(" · graph ") + CallerGraphName,
		Box(48.0f, bWide ? 240.0f : 304.0f,
			bWide ? 174.0f : Width - 96.0f, bWide ? 18.0f : 34.0f),
		9,
		true,
		TEXT("#67B7FF"));
	AddLabel(
		Result,
		TEXT("live.callee.region"),
		TEXT("EXPORTED BODY · ") + Projection.CalleeGraphName + TEXT("()"),
		Box(bWide ? 296.0f : 48.0f, bWide ? 240.0f : 434.0f,
			bWide ? Width - 332.0f : Width - 96.0f, 18.0f),
		9,
		true,
		TEXT("#D997FF"));
	AddLabel(
		Result,
		TEXT("live.portal"),
		TEXT("CALL PORTAL"),
		Box(bWide ? 260.0f : 112.0f, bWide ? 212.0f : 400.0f,
			bWide ? 120.0f : 180.0f, 13.0f),
		8,
		true,
		TEXT("#A9B3C1"));

	TArray<FRelationSpec> Relations;
	for (const FBlueprintLensLC5ContextBoundary& Boundary :
		Projection.ContextBoundaries)
	{
		Relations.Add({
			Boundary.RelationId,
			Boundary.SourceOccurrenceId,
			Boundary.TargetOccurrenceId,
			EBlueprintLensLayoutRelationFamily::Portal,
			false});
	}
	for (const FBlueprintLensLC5InternalRelation& Relation :
		Projection.InternalRelations)
	{
		Relations.Add({
			Relation.RelationId,
			Relation.SourceOccurrenceId,
			Relation.TargetOccurrenceId,
			Relation.Kind == TEXT("data")
				? EBlueprintLensLayoutRelationFamily::Value
				: EBlueprintLensLayoutRelationFamily::Execution,
			true});
	}
	Relations.Sort(
		[](const FRelationSpec& Left, const FRelationSpec& Right)
		{
			return Left.RelationId < Right.RelationId;
		});

	Result.LayoutRequest.GraphKey = TEXT("LC5_LIVE_TYPED_PORTAL_BODY");
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = Width;
	for (int32 Index = 0; Index < Projection.Occurrences.Num(); ++Index)
	{
		const FBlueprintLensLC5Occurrence& Occurrence =
			Projection.Occurrences[Index];
		FBlueprintLensLayoutNodeRequest Node;
		Node.UnitId = Occurrence.OccurrenceId;
		if (Index == 0)
		{
			Node.DesiredSize = FVector2D(
				bWide ? 164.0f : Width - 104.0f,
				54.0f);
		}
		else
		{
			const int32 Rank = Projection.LiveStaticRanks.FindRef(
				Occurrence.OccurrenceId);
			const int32 RankCount = FMath::Max(1, UnitsPerRank.FindRef(Rank));
			const float BodyAreaWidth = bWide
				? Width - 356.0f
				: Width - 128.0f;
			const float RankGap = 12.0f;
			Node.DesiredSize = FVector2D(
				(BodyAreaWidth - RankGap * (RankCount - 1)) / RankCount,
				46.0f);
		}
		int32 InputOrder = 0;
		int32 OutputOrder = 0;
		for (const FRelationSpec& Relation : Relations)
		{
			if (Relation.TargetOccurrenceId == Node.UnitId)
			{
				Node.Ports.Add({
					Relation.RelationId + TEXT(":in"),
					true,
					InputOrder++});
			}
			if (Relation.SourceOccurrenceId == Node.UnitId)
			{
				Node.Ports.Add({
					Relation.RelationId + TEXT(":out"),
					false,
					OutputOrder++});
			}
		}
		Result.LayoutRequest.Nodes.Add(MoveTemp(Node));
	}
	for (const FRelationSpec& Relation : Relations)
	{
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.RelationId;
		Edge.SourceUnitId = Relation.SourceOccurrenceId;
		Edge.TargetUnitId = Relation.TargetOccurrenceId;
		Edge.SourcePortLabel = Relation.RelationId + TEXT(":out");
		Edge.TargetPortLabel = Relation.RelationId + TEXT(":in");
		Edge.Family = Relation.Family;
		Edge.bParticipatesInRank = Relation.bParticipatesInRank;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	FBlueprintLensLayoutGroupRequest CallerGroup;
	CallerGroup.GroupId = TEXT("caller");
	CallerGroup.MemberUnitIds = {Result.CallOccurrenceId};
	Result.LayoutRequest.Groups.Add(MoveTemp(CallerGroup));
	FBlueprintLensLayoutGroupRequest CalleeGroup;
	CalleeGroup.GroupId = TEXT("callee");
	for (int32 Index = 1; Index < Projection.Occurrences.Num(); ++Index)
	{
		CalleeGroup.MemberUnitIds.Add(
			Projection.Occurrences[Index].OccurrenceId);
	}
	Result.LayoutRequest.Groups.Add(MoveTemp(CalleeGroup));
	if (!Result.LayoutRequest.IsValid())
	{
		Result.DiagnosticCode = TEXT("LC5_LIVE_LAYOUT_REQUEST_INVALID");
		return Result;
	}

	FBlueprintLensLayoutLedger Oracle;
	Oracle.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Oracle.BackendVersion = TEXT("BlueprintLens.LC5LiveTypedPortalOracle.v1");
	Oracle.ConfigurationFingerprint = FString::Printf(
		TEXT("lc5-live-static-rank-v2;body=%d;ranks=%d;width=%.0f"),
		BodyCount,
		Rows,
		Width);
	Oracle.CanvasSize = Result.CanvasSize;
	Oracle.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (int32 Index = 0; Index < Result.LayoutRequest.Nodes.Num(); ++Index)
	{
		const FBlueprintLensLayoutNodeRequest& Requested =
			Result.LayoutRequest.Nodes[Index];
		FBlueprintLensLayoutNodePlacement Node;
		Node.UnitId = Requested.UnitId;
		Node.Size = Requested.DesiredSize;
		if (Index == 0)
		{
			Node.Position = bWide
				? FVector2D(52.0f, 276.0f)
				: FVector2D(52.0f, 340.0f);
		}
		else
		{
			const int32 Rank = Projection.LiveStaticRanks.FindRef(
				Requested.UnitId);
			int32 RankOrdinal = 0;
			for (int32 Previous = 1; Previous < Index; ++Previous)
			{
				if (Projection.LiveStaticRanks.FindRef(
					Projection.Occurrences[Previous].OccurrenceId) == Rank)
				{
					++RankOrdinal;
				}
			}
			Node.Position = FVector2D(
				(bWide ? 300.0f : 52.0f) +
					RankOrdinal * (Node.Size.X + 12.0f),
				BodyTop + Rank * BodyRowCadence);
		}
		Oracle.Nodes.Add(MoveTemp(Node));
	}
	for (const FBlueprintLensLayoutNodeRequest& Requested :
		Result.LayoutRequest.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Node =
			FindNode(Oracle, Requested.UnitId);
		const int32 InputCount = Requested.Ports.FilterByPredicate(
			[](const FBlueprintLensLayoutPortRequest& Port)
			{
				return Port.bInput;
			}).Num();
		const int32 OutputCount = Requested.Ports.Num() - InputCount;
		int32 InputIndex = 0;
		int32 OutputIndex = 0;
		for (const FBlueprintLensLayoutPortRequest& Port : Requested.Ports)
		{
			FBlueprintLensLayoutPortPlacement Placement;
			Placement.UnitId = Requested.UnitId;
			Placement.Label = Port.Label;
			Placement.bInput = Port.bInput;
			const FBlueprintLensLayoutEdgeRequest* PortEdge =
				Result.LayoutRequest.Edges.FindByPredicate(
					[&Requested, &Port](
						const FBlueprintLensLayoutEdgeRequest& Edge)
					{
						return Port.bInput
							? Edge.TargetUnitId == Requested.UnitId &&
								Edge.TargetPortLabel == Port.Label
							: Edge.SourceUnitId == Requested.UnitId &&
								Edge.SourcePortLabel == Port.Label;
					});
			if (PortEdge != nullptr &&
				PortEdge->Family == EBlueprintLensLayoutRelationFamily::Portal)
			{
				Placement.Position = FVector2D(
					Port.bInput
						? Node->Position.X
						: Node->Position.X + Node->Size.X,
					Node->Position.Y + Node->Size.Y * 0.5f);
			}
			else
			{
				const int32 Count = Port.bInput ? InputCount : OutputCount;
				const int32 PortIndex = Port.bInput
					? InputIndex++
					: OutputIndex++;
				Placement.Position = FVector2D(
					Node->Position.X + Node->Size.X *
						static_cast<float>(PortIndex + 1) /
						static_cast<float>(Count + 1),
					Port.bInput ? Node->Position.Y : Node->Position.Y + Node->Size.Y);
			}
			Oracle.Ports.Add(MoveTemp(Placement));
		}
	}
	for (const FBlueprintLensLayoutEdgeRequest& Requested :
		Result.LayoutRequest.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source = Oracle.FindPort(
			Requested.SourceUnitId,
			Requested.SourcePortLabel,
			false);
		const FBlueprintLensLayoutPortPlacement* Target = Oracle.FindPort(
			Requested.TargetUnitId,
			Requested.TargetPortLabel,
			true);
		if (Source == nullptr || Target == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC5_LIVE_LAYOUT_PORT_UNACCOUNTED");
			return Result;
		}
		FBlueprintLensLayoutEdgePlacement Edge;
		Edge.RelationId = Requested.RelationId;
		Edge.SourceUnitId = Requested.SourceUnitId;
		Edge.TargetUnitId = Requested.TargetUnitId;
		Edge.SourcePortLabel = Requested.SourcePortLabel;
		Edge.TargetPortLabel = Requested.TargetPortLabel;
		Edge.Family = Requested.Family;
		if (Requested.Family == EBlueprintLensLayoutRelationFamily::Portal)
		{
			const float MiddleX = (Source->Position.X + Target->Position.X) * 0.5f;
			Edge.BendPoints = {
				FVector2D(MiddleX, Source->Position.Y),
				FVector2D(MiddleX, Target->Position.Y)};
		}
		else
		{
			const int32 SourceRank = Result.StaticRanks.FindRef(
				Requested.SourceUnitId);
			const int32 TargetRank = Result.StaticRanks.FindRef(
				Requested.TargetUnitId);
			if (TargetRank == SourceRank + 1)
			{
				const float MiddleY =
					(Source->Position.Y + Target->Position.Y) * 0.5f;
				Edge.BendPoints = {
					FVector2D(Source->Position.X, MiddleY),
					FVector2D(Target->Position.X, MiddleY)};
			}
			else
			{
				const float RouteX = bWide ? Width - 42.0f : Width - 50.0f;
				const float SourceGapY = Source->Position.Y + 10.0f;
				Edge.BendPoints = {
					FVector2D(Source->Position.X, SourceGapY),
					FVector2D(RouteX, SourceGapY),
					FVector2D(RouteX, Target->Position.Y - 10.0f),
					FVector2D(Target->Position.X, Target->Position.Y - 10.0f)};
			}
		}
		Result.EndpointGlyphIds.Add(Requested.RelationId + TEXT(":source"));
		Result.EndpointGlyphIds.Add(Requested.RelationId + TEXT(":target"));
		Oracle.Edges.Add(MoveTemp(Edge));
	}
	if (!Oracle.IsCompleteFor(Result.LayoutRequest))
	{
		Result.DiagnosticCode = TEXT("LC5_LIVE_LAYOUT_ORACLE_INCOMPLETE");
		return Result;
	}
	Result.VisualOracleLedger = Oracle;
	Result.LayoutLedger = Oracle;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Projection.Occurrences)
	{
		const FBlueprintLensLayoutNodePlacement* Node =
			FindNode(Oracle, Occurrence.OccurrenceId);
		FBlueprintLensLC5SourceAnchor Anchor;
		Anchor.OccurrenceId = Occurrence.OccurrenceId;
		Anchor.SourceNodeId = Occurrence.SourceNodeId;
		Anchor.Position = Node->Position + Node->Size * 0.5f;
		Result.SourceAnchors.Add(MoveTemp(Anchor));
		AddLabel(
			Result,
			TEXT("live.node.") + Occurrence.OccurrenceId,
			Projection.LiveOccurrenceLabels.FindRef(Occurrence.OccurrenceId),
			Box(
				Node->Position.X + 8.0f,
				Node->Position.Y + 8.0f,
				Node->Size.X - 16.0f,
				30.0f),
			9,
			Occurrence.Role == TEXT("call_site"));
	}
	AddLabel(
		Result,
		TEXT("live.frontier.claim"),
		Projection.ClaimBoundaryStatement,
		Box(38.0f, FrontierTop + 10.0f, Width - 76.0f, 42.0f),
		9,
		true,
		TEXT("#F07178"));
	AddLabel(
		Result,
		TEXT("live.frontier.static"),
		TEXT("Static typed-IR structure only · no runtime invocation or runtime order is claimed."),
		Box(38.0f, FrontierTop + 58.0f, Width - 76.0f, 18.0f),
		9,
		false,
		TEXT("#A9B3C1"));

	const TArray<TPair<FString, FString>> Actions = {
		{TEXT("open_source"), TEXT("Open source")},
		{TEXT("select"), TEXT("Select")},
		{TEXT("show_complete_text"), TEXT("Show complete text")},
		{TEXT("show_evidence"), TEXT("Show evidence")}};
	const float Gap = 8.0f;
	const float ActionWidth =
		(Result.ActionsBounds.GetSize().X - 30.0f - Gap * 3.0f) / 4.0f;
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		FBlueprintLensLC5ActionLayout Action;
		Action.ActionId = Actions[Index].Key;
		Action.Label = Actions[Index].Value;
		Action.Bounds = Box(
			Result.ActionsBounds.Min.X + 15.0f + Index * (ActionWidth + Gap),
			ActionsTop + 14.0f,
			ActionWidth,
			34.0f);
		Result.Actions.Add(MoveTemp(Action));
	}
	Result.DiagnosticCode = Result.HasNoTextOrRouteCollisions() &&
		Result.HasCompleteEndpointGlyphs() &&
		Result.HasStrictStaticRankOrder()
		? TEXT("LC5_LAYOUT_COMPLETE")
		: TEXT("LC5_LIVE_LAYOUT_INVARIANT_FAILED");
	return Result;
}

bool SameLedger(const FBlueprintLensLayoutLedger& A, const FBlueprintLensLayoutLedger& B, const float Tolerance)
{
	if (!A.CanvasSize.Equals(B.CanvasSize, Tolerance) || A.Nodes.Num() != B.Nodes.Num() ||
		A.Ports.Num() != B.Ports.Num() || A.Edges.Num() != B.Edges.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : A.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Other = FindNode(B, Node.UnitId);
		if (Other == nullptr || !Node.Position.Equals(Other->Position, Tolerance) ||
			!Node.Size.Equals(Other->Size, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : A.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Other = B.FindPort(
			Port.UnitId, Port.Label, Port.bInput);
		if (Other == nullptr || !Port.Position.Equals(Other->Position, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : A.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Other = B.Edges.FindByPredicate(
			[&Edge](const FBlueprintLensLayoutEdgePlacement& Candidate)
			{
				return Candidate.RelationId == Edge.RelationId;
			});
		if (Other == nullptr || Other->BendPoints.Num() != Edge.BendPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Edge.BendPoints.Num(); ++Index)
		{
			if (!Edge.BendPoints[Index].Equals(Other->BendPoints[Index], Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}
} // namespace

FSlateFontInfo BlueprintLensLC5Font(const int32 FontSize, const bool bBold)
{
	const float SlatePointSize = static_cast<float>(FontSize) * 0.75f;
	const TCHAR* Typeface = bBold ? TEXT("Bold") : TEXT("Regular");
	const FString FontPath = FPaths::Combine(
		FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")),
		TEXT("Fonts"),
		bBold ? TEXT("segoeuib.ttf") : TEXT("segoeui.ttf"));
	if (!FPaths::FileExists(FontPath))
	{
		return FCoreStyle::GetDefaultFontStyle(Typeface, SlatePointSize);
	}
	const int32 WeightIndex = bBold ? 1 : 0;
	static TSharedPtr<FCompositeFont> CompositeFonts[2];
	if (!CompositeFonts[WeightIndex].IsValid())
	{
		CompositeFonts[WeightIndex] = MakeShared<FCompositeFont>(
			FName(Typeface), FontPath, EFontHinting::Default,
			EFontLoadingPolicy::LazyLoad);
	}
	return FSlateFontInfo(
		CompositeFonts[WeightIndex], SlatePointSize, FName(Typeface));
}

bool FBlueprintLensLC5Layout::CoversProjection(const FBlueprintLensLC5Projection& Projection) const
{
	if (!Projection.IsRenderable() || DiagnosticCode != TEXT("LC5_LAYOUT_COMPLETE") ||
		LayoutRequest.Nodes.Num() != Projection.Occurrences.Num() ||
		LayoutRequest.Edges.Num() != Projection.AllRelationIds.Num() ||
		Actions.Num() != Projection.ActionIds.Num() || SourceAnchors.Num() != Projection.Occurrences.Num())
	{
		return false;
	}
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		if (!LayoutRequest.Edges.ContainsByPredicate([&RelationId](const auto& Edge)
		{
			return Edge.RelationId == RelationId;
		}))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC5Layout::HasValidSharedLedger() const
{
	return LayoutRequest.IsValid() && LayoutLedger.IsCompleteFor(LayoutRequest) &&
		VisualOracleLedger.IsCompleteFor(LayoutRequest) && LayoutLedger.HasNoNodeOverlaps();
}

bool FBlueprintLensLC5Layout::MatchesVisualOracle(const float Tolerance) const
{
	return SameLedger(LayoutLedger, VisualOracleLedger, Tolerance);
}

bool FBlueprintLensLC5Layout::HasNoLabelCollisions() const
{
	for (int32 A = 0; A < Labels.Num(); ++A)
	{
		if (!Labels[A].Bounds.bIsValid || Labels[A].Bounds.Min.X < 0.0f ||
			Labels[A].Bounds.Min.Y < 0.0f || Labels[A].Bounds.Max.X > CanvasSize.X + 0.5f ||
			Labels[A].Bounds.Max.Y > CanvasSize.Y + 0.5f)
		{
			return false;
		}
		for (int32 B = A + 1; B < Labels.Num(); ++B)
		{
			if (StrictlyIntersects(Labels[A].Bounds, Labels[B].Bounds))
			{
				return false;
			}
		}
	}
	return true;
}

bool FBlueprintLensLC5Layout::HasNoRouteNodeCollisions() const
{
	for (const FBlueprintLensLayoutEdgePlacement& Edge : LayoutLedger.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source = LayoutLedger.FindPort(
			Edge.SourceUnitId,
			Edge.SourcePortLabel,
			false);
		const FBlueprintLensLayoutPortPlacement* Target = LayoutLedger.FindPort(
			Edge.TargetUnitId,
			Edge.TargetPortLabel,
			true);
		if (Source == nullptr || Target == nullptr)
		{
			return false;
		}
		TArray<FVector2D> Points{Source->Position};
		Points.Append(Edge.BendPoints);
		Points.Add(Target->Position);
		for (int32 SegmentIndex = 1; SegmentIndex < Points.Num(); ++SegmentIndex)
		{
			for (const FBlueprintLensLayoutNodePlacement& Node : LayoutLedger.Nodes)
			{
				if (Node.UnitId == Edge.SourceUnitId ||
					Node.UnitId == Edge.TargetUnitId)
				{
					continue;
				}
				const FBox2D NodeBounds(
					Node.Position,
					Node.Position + Node.Size);
				if (SegmentIntersectsBox(
					Points[SegmentIndex - 1],
					Points[SegmentIndex],
					NodeBounds))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FBlueprintLensLC5Layout::HasCompleteEndpointGlyphs() const
{
	if (StaticRanks.IsEmpty())
	{
		return true;
	}
	if (EndpointGlyphIds.Num() != LayoutRequest.Edges.Num() * 2)
	{
		return false;
	}
	TSet<FString> UniqueGlyphIds;
	for (const FString& GlyphId : EndpointGlyphIds)
	{
		UniqueGlyphIds.Add(GlyphId);
	}
	if (UniqueGlyphIds.Num() != EndpointGlyphIds.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutEdgeRequest& Edge : LayoutRequest.Edges)
	{
		if (!UniqueGlyphIds.Contains(Edge.RelationId + TEXT(":source")) ||
			!UniqueGlyphIds.Contains(Edge.RelationId + TEXT(":target")) ||
			LayoutLedger.FindPort(
				Edge.SourceUnitId,
				Edge.SourcePortLabel,
				false) == nullptr ||
			LayoutLedger.FindPort(
				Edge.TargetUnitId,
				Edge.TargetPortLabel,
				true) == nullptr)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC5Layout::HasStrictStaticRankOrder() const
{
	if (StaticRanks.IsEmpty())
	{
		return true;
	}
	for (const FBlueprintLensLayoutEdgeRequest& Edge : LayoutRequest.Edges)
	{
		if (Edge.Family == EBlueprintLensLayoutRelationFamily::Portal)
		{
			if (Edge.bParticipatesInRank)
			{
				return false;
			}
			continue;
		}
		const int32* SourceRank = StaticRanks.Find(Edge.SourceUnitId);
		const int32* TargetRank = StaticRanks.Find(Edge.TargetUnitId);
		const FBlueprintLensLayoutNodePlacement* SourceNode =
			LayoutLedger.Nodes.FindByPredicate(
				[&Edge](const FBlueprintLensLayoutNodePlacement& Node)
				{
					return Node.UnitId == Edge.SourceUnitId;
				});
		const FBlueprintLensLayoutNodePlacement* TargetNode =
			LayoutLedger.Nodes.FindByPredicate(
				[&Edge](const FBlueprintLensLayoutNodePlacement& Node)
				{
					return Node.UnitId == Edge.TargetUnitId;
				});
		if (SourceRank == nullptr || TargetRank == nullptr ||
			*SourceRank >= *TargetRank || SourceNode == nullptr ||
			TargetNode == nullptr ||
			SourceNode->Position.Y >= TargetNode->Position.Y)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC5Layout::HasNoTextOrRouteCollisions() const
{
	return HasNoLabelCollisions() && HasNoRouteNodeCollisions();
}

FBlueprintLensLC5Layout FBlueprintLensLC5LayoutBuilder::Build(
	const FBlueprintLensLC5Projection& Projection,
	const float TargetWidth)
{
	FBlueprintLensLC5Layout Result;
	if (!Projection.IsRenderable())
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_PROJECTION_UNAVAILABLE");
		return Result;
	}
	if (Projection.bLiveCallBody)
	{
		return BuildLiveLayout(Projection, TargetWidth);
	}
	if (FMath::IsNearlyEqual(TargetWidth, 430.0f))
	{
		Result.Mode = EBlueprintLensLC5LayoutMode::SingleColumn430;
		Result.CanvasSize = FVector2D(430.0f, 1280.0f);
	}
	else if (FMath::IsNearlyEqual(TargetWidth, 480.0f))
	{
		Result.Mode = EBlueprintLensLC5LayoutMode::StackedDetail480;
		Result.CanvasSize = FVector2D(480.0f, 1200.0f);
	}
	else if (FMath::IsNearlyEqual(TargetWidth, 700.0f))
	{
		Result.Mode = EBlueprintLensLC5LayoutMode::SideBySide700;
		Result.CanvasSize = FVector2D(700.0f, 1000.0f);
	}
	else
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_WIDTH_UNSUPPORTED");
		return Result;
	}

	const FBlueprintLensLC5ContextBoundary* Enter = Projection.ContextBoundaries.FindByPredicate([](const auto& Item)
	{
		return Item.Kind == TEXT("call_enter");
	});
	const FBlueprintLensLC5ContextBoundary* Return = Projection.ContextBoundaries.FindByPredicate([](const auto& Item)
	{
		return Item.Kind == TEXT("call_return");
	});
	if (Enter == nullptr || Return == nullptr)
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_CONTEXT_BOUNDARY_MISSING");
		return Result;
	}
	Result.CallOccurrenceId = Enter->SourceOccurrenceId;
	Result.EntryOccurrenceId = Enter->TargetOccurrenceId;
	Result.ReturnOccurrenceId = Return->SourceOccurrenceId;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Projection.Occurrences)
	{
		if (Occurrence.OccurrenceId != Result.CallOccurrenceId &&
			Occurrence.OccurrenceId != Result.EntryOccurrenceId &&
			Occurrence.OccurrenceId != Result.ReturnOccurrenceId)
		{
			if (!Result.OperatorOccurrenceId.IsEmpty())
			{
				Result.DiagnosticCode = TEXT("LC5_LAYOUT_OPERATOR_AMBIGUOUS");
				return Result;
			}
			Result.OperatorOccurrenceId = Occurrence.OccurrenceId;
		}
	}
	if (Result.OperatorOccurrenceId.IsEmpty())
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_OPERATOR_MISSING");
		return Result;
	}

	TArray<FRelationSpec> Relations;
	for (const FBlueprintLensLC5Binding& Binding : Projection.Bindings)
	{
		Relations.Add({Binding.RelationId, Binding.SourceOccurrenceId,
			Binding.TargetOccurrenceId, EBlueprintLensLayoutRelationFamily::Value,
			Binding.Kind == TEXT("argument")});
	}
	for (const FBlueprintLensLC5ContextBoundary& Boundary : Projection.ContextBoundaries)
	{
		Relations.Add({Boundary.RelationId, Boundary.SourceOccurrenceId,
			Boundary.TargetOccurrenceId, EBlueprintLensLayoutRelationFamily::Portal,
			Boundary.Kind == TEXT("call_enter")});
	}
	for (const FBlueprintLensLC5InternalRelation& Relation : Projection.InternalRelations)
	{
		Relations.Add({Relation.RelationId, Relation.SourceOccurrenceId,
			Relation.TargetOccurrenceId,
			Relation.Kind == TEXT("data") ? EBlueprintLensLayoutRelationFamily::Value :
				EBlueprintLensLayoutRelationFamily::Execution, true});
	}
	Relations.Sort([](const auto& A, const auto& B)
	{
		return A.RelationId < B.RelationId;
	});

	Result.LayoutRequest.GraphKey = TEXT("LC5_TYPED_PORTAL_BRIDGE");
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = TargetWidth;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Projection.Occurrences)
	{
		FBlueprintLensLayoutNodeRequest Node;
		Node.UnitId = Occurrence.OccurrenceId;
		Node.DesiredSize = NodeSizeFor(Result.Mode, Node.UnitId,
			Result.CallOccurrenceId, Result.EntryOccurrenceId,
			Result.OperatorOccurrenceId, Result.ReturnOccurrenceId);
		if (Node.DesiredSize.IsNearlyZero())
		{
			Result.DiagnosticCode = TEXT("LC5_LAYOUT_NODE_UNACCOUNTED");
			return Result;
		}
		int32 InputOrder = 0;
		int32 OutputOrder = 0;
		for (const FRelationSpec& Relation : Relations)
		{
			if (Relation.TargetOccurrenceId == Node.UnitId)
			{
				Node.Ports.Add({Relation.RelationId + TEXT(":in"), true, InputOrder++});
			}
			if (Relation.SourceOccurrenceId == Node.UnitId)
			{
				Node.Ports.Add({Relation.RelationId + TEXT(":out"), false, OutputOrder++});
			}
		}
		Result.LayoutRequest.Nodes.Add(MoveTemp(Node));
	}
	for (const FRelationSpec& Relation : Relations)
	{
		if (!Projection.Occurrences.ContainsByPredicate([&Relation](const auto& Item)
			{ return Item.OccurrenceId == Relation.SourceOccurrenceId; }) ||
			!Projection.Occurrences.ContainsByPredicate([&Relation](const auto& Item)
			{ return Item.OccurrenceId == Relation.TargetOccurrenceId; }))
		{
			Result.DiagnosticCode = TEXT("LC5_LAYOUT_RELATION_ENDPOINT_UNACCOUNTED");
			return Result;
		}
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.RelationId;
		Edge.SourceUnitId = Relation.SourceOccurrenceId;
		Edge.TargetUnitId = Relation.TargetOccurrenceId;
		Edge.SourcePortLabel = Relation.RelationId + TEXT(":out");
		Edge.TargetPortLabel = Relation.RelationId + TEXT(":in");
		Edge.Family = Relation.Family;
		Edge.bParticipatesInRank = Relation.bParticipatesInRank;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	FBlueprintLensLayoutGroupRequest CallerGroup;
	CallerGroup.GroupId = TEXT("caller");
	CallerGroup.MemberUnitIds = {Result.CallOccurrenceId};
	Result.LayoutRequest.Groups.Add(MoveTemp(CallerGroup));
	FBlueprintLensLayoutGroupRequest CalleeGroup;
	CalleeGroup.GroupId = TEXT("callee");
	CalleeGroup.MemberUnitIds = {
		Result.EntryOccurrenceId, Result.OperatorOccurrenceId, Result.ReturnOccurrenceId};
	Result.LayoutRequest.Groups.Add(MoveTemp(CalleeGroup));
	if (!Result.LayoutRequest.IsValid())
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_REQUEST_INVALID");
		return Result;
	}

	FBlueprintLensLayoutLedger Oracle;
	Oracle.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Oracle.BackendVersion = TEXT("BlueprintLens.LC5ResponsiveOracle.v1");
	Oracle.ConfigurationFingerprint = FString::Printf(TEXT("lc5-responsive;mode=%d;width=%.0f"),
		static_cast<int32>(Result.Mode), TargetWidth);
	Oracle.CanvasSize = Result.CanvasSize;
	Oracle.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (const FBlueprintLensLayoutNodeRequest& Requested : Result.LayoutRequest.Nodes)
	{
		FBlueprintLensLayoutNodePlacement Node;
		Node.UnitId = Requested.UnitId;
		Node.Position = NodePositionFor(Result.Mode, Node.UnitId,
			Result.CallOccurrenceId, Result.EntryOccurrenceId,
			Result.OperatorOccurrenceId, Result.ReturnOccurrenceId);
		Node.Size = Requested.DesiredSize;
		Oracle.Nodes.Add(MoveTemp(Node));
	}
	for (const FBlueprintLensLayoutNodeRequest& Requested : Result.LayoutRequest.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Node = FindNode(Oracle, Requested.UnitId);
		for (const FBlueprintLensLayoutPortRequest& Port : Requested.Ports)
		{
			FBlueprintLensLayoutPortPlacement Placement;
			Placement.UnitId = Requested.UnitId;
			Placement.Label = Port.Label;
			Placement.bInput = Port.bInput;
			const FBlueprintLensLayoutEdgeRequest* Edge =
				Result.LayoutRequest.Edges.FindByPredicate(
					[&Requested, &Port](const FBlueprintLensLayoutEdgeRequest& Candidate)
					{
						return Port.bInput
							? Candidate.TargetUnitId == Requested.UnitId &&
								Candidate.TargetPortLabel == Port.Label
							: Candidate.SourceUnitId == Requested.UnitId &&
								Candidate.SourcePortLabel == Port.Label;
					});
			const FRouteGeometry Route = Edge != nullptr
				? RouteGeometryFor(Result.Mode, Edge->RelationId) : FRouteGeometry();
			if (!Route.bValid)
			{
				Result.DiagnosticCode = TEXT("LC5_LAYOUT_TARGET_ROUTE_UNACCOUNTED");
				return Result;
			}
			Placement.Position = Port.bInput ? Route.Target : Route.Source;
			Oracle.Ports.Add(MoveTemp(Placement));
		}
	}
	for (const FBlueprintLensLayoutEdgeRequest& Requested : Result.LayoutRequest.Edges)
	{
		FBlueprintLensLayoutEdgePlacement Edge;
		Edge.RelationId = Requested.RelationId;
		Edge.SourceUnitId = Requested.SourceUnitId;
		Edge.TargetUnitId = Requested.TargetUnitId;
		Edge.SourcePortLabel = Requested.SourcePortLabel;
		Edge.TargetPortLabel = Requested.TargetPortLabel;
		Edge.Family = Requested.Family;
		const FBlueprintLensLayoutPortPlacement* Source = Oracle.FindPort(
			Edge.SourceUnitId, Edge.SourcePortLabel, false);
		const FBlueprintLensLayoutPortPlacement* Target = Oracle.FindPort(
			Edge.TargetUnitId, Edge.TargetPortLabel, true);
		if (Source == nullptr || Target == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC5_LAYOUT_PORT_UNACCOUNTED");
			return Result;
		}
		const FRouteGeometry Route = RouteGeometryFor(Result.Mode, Requested.RelationId);
		if (!Route.bValid || !Source->Position.Equals(Route.Source, 0.1f) ||
			!Target->Position.Equals(Route.Target, 0.1f))
		{
			Result.DiagnosticCode = TEXT("LC5_LAYOUT_TARGET_ROUTE_ENDPOINT_MISMATCH");
			return Result;
		}
		Edge.BendPoints = Route.Bends;
		Oracle.Edges.Add(MoveTemp(Edge));
	}
	if (!Oracle.IsCompleteFor(Result.LayoutRequest))
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_ORACLE_INCOMPLETE");
		return Result;
	}
	Result.VisualOracleLedger = Oracle;
	Result.LayoutLedger = Oracle;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Projection.Occurrences)
	{
		const FBlueprintLensLayoutNodePlacement* Node = FindNode(Oracle, Occurrence.OccurrenceId);
		FBlueprintLensLC5SourceAnchor Anchor;
		Anchor.OccurrenceId = Occurrence.OccurrenceId;
		Anchor.SourceNodeId = Occurrence.SourceNodeId;
		Anchor.Position = Node->Position + Node->Size * 0.5f;
		Result.SourceAnchors.Add(MoveTemp(Anchor));
	}
	BuildDisplayGeometry(Result);
	Result.DiagnosticCode = Result.HasNoTextOrRouteCollisions()
		? TEXT("LC5_LAYOUT_COMPLETE") : TEXT("LC5_LAYOUT_TEXT_COLLISION");
	return Result;
}
