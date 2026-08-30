#include "BlueprintLensLC4SequenceLayout.h"

namespace
{
struct FEdgeSpec
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourcePortLabel;
	FString TargetPortLabel;
	TArray<FVector2D> BendPoints;
};

FBox2D Bounds(const float X, const float Y, const float W, const float H)
{
	return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
}

FVector2D BoxSize(const FBox2D& Box)
{
	return Box.Max - Box.Min;
}

EBlueprintLensLC4SequenceLayoutMode ModeForWidth(const float TargetWidth)
{
	if (TargetWidth <= 455.0f)
	{
		return EBlueprintLensLC4SequenceLayoutMode::Narrow430;
	}
	if (TargetWidth <= 590.0f)
	{
		return EBlueprintLensLC4SequenceLayoutMode::Compact480;
	}
	return EBlueprintLensLC4SequenceLayoutMode::Wide700;
}

float CanonicalWidth(const EBlueprintLensLC4SequenceLayoutMode Mode)
{
	switch (Mode)
	{
	case EBlueprintLensLC4SequenceLayoutMode::Narrow430:
		return 430.0f;
	case EBlueprintLensLC4SequenceLayoutMode::Compact480:
		return 480.0f;
	default:
		return 700.0f;
	}
}

FString ShortPrimaryLabel(const FString& ReaderLabel)
{
	if (ReaderLabel.StartsWith(TEXT("Set LC4Branch")))
	{
		return FString::Printf(
			TEXT("Set %s"),
			*ReaderLabel.RightChop(FCString::Strlen(TEXT("Set LC4Branch"))));
	}
	if (ReaderLabel == TEXT("Set LC4SideEffect"))
	{
		return TEXT("Set SideEffect");
	}
	if (ReaderLabel == TEXT("Set LC4Reconverged"))
	{
		return TEXT("Set Reconverged");
	}
	return ReaderLabel;
}

FString SecondaryLabel(
	const FString& ReaderLabel,
	const EBlueprintLensLC4SequenceVisualNodeKind Kind)
{
	if (Kind == EBlueprintLensLC4SequenceVisualNodeKind::Outside)
	{
		return TEXT("terminal");
	}
	if (Kind == EBlueprintLensLC4SequenceVisualNodeKind::Reconverged)
	{
		return TEXT("one canonical suffix");
	}
	if (Kind == EBlueprintLensLC4SequenceVisualNodeKind::Criterion)
	{
		return TEXT("CRITERION");
	}
	return ReaderLabel.StartsWith(TEXT("Set "))
		? ReaderLabel.RightChop(4)
		: ReaderLabel;
}

bool NearlyEqual(const FVector2D& A, const FVector2D& B, const float Tolerance)
{
	return A.Equals(B, Tolerance);
}

bool SameLedgerGeometry(
	const FBlueprintLensLayoutLedger& Actual,
	const FBlueprintLensLayoutLedger& Oracle,
	const float Tolerance)
{
	if (!NearlyEqual(Actual.CanvasSize, Oracle.CanvasSize, Tolerance) ||
		Actual.Nodes.Num() != Oracle.Nodes.Num() ||
		Actual.Ports.Num() != Oracle.Ports.Num() ||
		Actual.Edges.Num() != Oracle.Edges.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Expected : Oracle.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Found =
			Actual.Nodes.FindByPredicate(
				[&Expected](const FBlueprintLensLayoutNodePlacement& Candidate)
				{
					return Candidate.UnitId == Expected.UnitId;
				});
		if (Found == nullptr ||
			!NearlyEqual(Found->Position, Expected.Position, Tolerance) ||
			!NearlyEqual(Found->Size, Expected.Size, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Expected : Oracle.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Found = Actual.FindPort(
			Expected.UnitId,
			Expected.Label,
			Expected.bInput);
		if (Found == nullptr ||
			!NearlyEqual(Found->Position, Expected.Position, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Expected : Oracle.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Found =
			Actual.Edges.FindByPredicate(
				[&Expected](const FBlueprintLensLayoutEdgePlacement& Candidate)
				{
					return Candidate.RelationId == Expected.RelationId;
				});
		if (Found == nullptr ||
			Found->SourceUnitId != Expected.SourceUnitId ||
			Found->TargetUnitId != Expected.TargetUnitId ||
			Found->SourcePortLabel != Expected.SourcePortLabel ||
			Found->TargetPortLabel != Expected.TargetPortLabel ||
			Found->BendPoints.Num() != Expected.BendPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.BendPoints.Num(); ++Index)
		{
			if (!NearlyEqual(
					Found->BendPoints[Index],
					Expected.BendPoints[Index],
					Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}

const FBlueprintLensLayoutNodePlacement* FindNode(
	const FBlueprintLensLayoutLedger& Ledger,
	const FString& UnitId)
{
	return Ledger.Nodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLayoutNodePlacement& Candidate)
		{
			return Candidate.UnitId == UnitId;
		});
}

FBlueprintLensLC4SequenceLayout BuildLiveLayout(
	const FBlueprintLensLC4SequenceProjection& Projection,
	const float TargetWidth)
{
	FBlueprintLensLC4SequenceLayout Result;
	Result.bLiveExplanation = true;
	Result.Mode = ModeForWidth(TargetWidth);
	const float Width = CanonicalWidth(Result.Mode);
	const float RowCadence = 132.0f;
	const float HeaderHeight = 204.0f;
	const float FooterHeight = 84.0f;
	Result.CanvasSize = FVector2D(
		Width,
		HeaderHeight + RowCadence * Projection.Routes.Num() + FooterHeight);
	Result.TitlePosition = FVector2D(24.0f, 38.0f);
	Result.SubtitlePositions = {FVector2D(24.0f, 64.0f)};
	Result.OrderBandBounds = Bounds(24.0f, 86.0f, Width - 48.0f, 86.0f);
	Result.OrderStagePosition = FVector2D(42.0f, 111.0f);
	Result.OrderTextPosition = FVector2D(42.0f, 139.0f);
	Result.CountTextPositions = {FVector2D(42.0f, 161.0f)};
	Result.PinOrderPosition = FVector2D(24.0f, 194.0f);
	Result.SpineStart = FVector2D(64.0f, HeaderHeight + 20.0f);
	Result.SpineEnd = FVector2D(
		64.0f,
		HeaderHeight + RowCadence *
			FMath::Max(Projection.Routes.Num() - 1, 0) + 20.0f);

	TMap<FString, FBox2D> NodeBounds;
	NodeBounds.Add(
		Projection.SequenceUnitId,
		Bounds(
			46.0f,
			Result.SpineStart.Y,
			36.0f,
			FMath::Max(Result.SpineEnd.Y - Result.SpineStart.Y, 36.0f)));
	Result.VisualNodes.Add({
		Projection.SequenceUnitId,
		Projection.SequenceReaderLabel,
		TEXT("LC4-SEQ source"),
		EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine,
		INDEX_NONE});

	TSet<FString> VisualUnitIds = {Projection.SequenceUnitId};
	for (int32 RouteIndex = 0; RouteIndex < Projection.Routes.Num(); ++RouteIndex)
	{
		const FBlueprintLensLC4SequenceRoute& Route = Projection.Routes[RouteIndex];
		const float RowY = HeaderHeight + RowCadence * RouteIndex;
		FBlueprintLensLC4SequenceStationLayout Station;
		Station.Ordinal = Route.Ordinal;
		Station.Center = FVector2D(64.0f, RowY + 20.0f);
		Station.Radius = 18.0f;
		Station.LabelBounds = Bounds(92.0f, RowY - 2.0f, Width - 116.0f, 18.0f);
		Station.HitBounds = Bounds(42.0f, RowY - 2.0f, Width - 66.0f, 42.0f);
		Result.Stations.Add(Station);

		for (int32 UnitIndex = 0;
			 UnitIndex < Route.RouteUnitIds.Num();
			 ++UnitIndex)
		{
			const FString& UnitId = Route.RouteUnitIds[UnitIndex];
			if (!NodeBounds.Contains(UnitId))
			{
				const float NodeX = FMath::Min(
					142.0f + 150.0f * UnitIndex,
					Width - 150.0f);
				NodeBounds.Add(UnitId, Bounds(NodeX, RowY + 42.0f, 132.0f, 56.0f));
			}
			if (!VisualUnitIds.Contains(UnitId))
			{
				const bool bCriterion = UnitId == Projection.CriterionUnitId;
				const EBlueprintLensLC4SequenceVisualNodeKind Kind = bCriterion
					? EBlueprintLensLC4SequenceVisualNodeKind::Criterion
					: Route.CriterionRelation ==
						EBlueprintLensLC4CriterionRelation::Outside
					? EBlueprintLensLC4SequenceVisualNodeKind::Outside
					: EBlueprintLensLC4SequenceVisualNodeKind::Included;
				Result.VisualNodes.Add({
					UnitId,
					Route.RouteReaderLabels.IsValidIndex(UnitIndex)
						? Route.RouteReaderLabels[UnitIndex]
						: UnitId,
					bCriterion
						? TEXT("CRITERION")
						: Kind == EBlueprintLensLC4SequenceVisualNodeKind::Outside
						? TEXT("EXCLUDED SIBLING")
						: TEXT("INCLUDED"),
					Kind,
					Route.Ordinal});
				VisualUnitIds.Add(UnitId);
			}
		}
	}

	Result.LayoutRequest.GraphKey = FString::Printf(
		TEXT("LC4-SEQ-live:%s"), *Projection.ProjectionIntegrityHash);
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = Width;
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		const FBox2D* Box = NodeBounds.Find(UnitId);
		if (Box == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LIVE_LAYOUT_NODE_UNACCOUNTED");
			return Result;
		}
		Result.LayoutRequest.Nodes.Add({UnitId, BoxSize(*Box), {}});
	}

	auto AddPort = [&Result](
		const FString& UnitId,
		const FString& Label,
		const bool bInput,
		const int32 Order)
	{
		FBlueprintLensLayoutNodeRequest* Node =
			Result.LayoutRequest.Nodes.FindByPredicate(
				[&UnitId](const FBlueprintLensLayoutNodeRequest& Candidate)
				{
					return Candidate.UnitId == UnitId;
				});
		if (Node != nullptr && !Node->Ports.ContainsByPredicate(
			[&](const FBlueprintLensLayoutPortRequest& Port)
			{
				return Port.Label == Label && Port.bInput == bInput;
			}))
		{
			Node->Ports.Add({Label, bInput, Order});
		}
	};

	TArray<FEdgeSpec> Edges;
	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		AddPort(Projection.SequenceUnitId, Route.SourcePinName, false, Route.Ordinal);
		for (int32 Index = 0; Index < Route.RouteRelationIds.Num(); ++Index)
		{
			if (!Route.RouteUnitIds.IsValidIndex(Index))
			{
				Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LIVE_LAYOUT_ROUTE_INVALID");
				return Result;
			}
			FEdgeSpec Edge;
			Edge.RelationId = Route.RouteRelationIds[Index];
			Edge.SourceUnitId = Index == 0
				? Projection.SequenceUnitId
				: Route.RouteUnitIds[Index - 1];
			Edge.TargetUnitId = Route.RouteUnitIds[Index];
			Edge.SourcePortLabel = Index == 0
				? Route.SourcePinName
				: FString::Printf(TEXT("out:%s"), *Edge.RelationId);
			Edge.TargetPortLabel = FString::Printf(TEXT("in:%s"), *Edge.RelationId);
			AddPort(Edge.SourceUnitId, Edge.SourcePortLabel, false, Index);
			AddPort(Edge.TargetUnitId, Edge.TargetPortLabel, true, Index);
			Edges.Add(MoveTemp(Edge));
		}
	}
	for (const FEdgeSpec& Spec : Edges)
	{
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Spec.RelationId;
		Edge.SourceUnitId = Spec.SourceUnitId;
		Edge.TargetUnitId = Spec.TargetUnitId;
		Edge.SourcePortLabel = Spec.SourcePortLabel;
		Edge.TargetPortLabel = Spec.TargetPortLabel;
		Edge.Family = EBlueprintLensLayoutRelationFamily::Execution;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	FBlueprintLensLayoutGroupRequest Group;
	Group.GroupId = TEXT("lc4-seq.live-disclosure-rail");
	Group.MemberUnitIds = Projection.AllUnitIds;
	Result.LayoutRequest.Groups.Add(MoveTemp(Group));

	Result.LayoutLedger.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Result.LayoutLedger.BackendVersion =
		TEXT("BlueprintLens.DeterministicLC4SequenceLiveVisualOracle.v1");
	Result.LayoutLedger.ConfigurationFingerprint = FString::Printf(
		TEXT("profile=LayeredPorts;mode=%d;width=%.0f;oracle=live-v1"),
		static_cast<int32>(Result.Mode), Width);
	Result.LayoutLedger.CanvasSize = Result.CanvasSize;
	for (const FBlueprintLensLayoutNodeRequest& Requested :
		 Result.LayoutRequest.Nodes)
	{
		const FBox2D& Box = NodeBounds.FindChecked(Requested.UnitId);
		Result.LayoutLedger.Nodes.Add({
			Requested.UnitId, Box.Min, BoxSize(Box)});
	}
	for (const FBlueprintLensLayoutNodeRequest& Node : Result.LayoutRequest.Nodes)
	{
		const FBox2D& Box = NodeBounds.FindChecked(Node.UnitId);
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			FVector2D Position;
			if (Node.UnitId == Projection.SequenceUnitId && !Port.bInput)
			{
				const FBlueprintLensLC4SequenceRoute* Route =
					Projection.Routes.FindByPredicate(
						[&Port](const FBlueprintLensLC4SequenceRoute& Candidate)
						{
							return Candidate.SourcePinName == Port.Label;
						});
				const FBlueprintLensLC4SequenceStationLayout* Station =
					Route != nullptr ? Result.FindStation(Route->Ordinal) : nullptr;
				Position = Station != nullptr
					? Station->Center
					: FVector2D(Box.Max.X, Box.Min.Y);
			}
			else
			{
				Position = FVector2D(
					Port.bInput ? Box.Min.X : Box.Max.X,
					(Box.Min.Y + Box.Max.Y) * 0.5f);
			}
			Result.LayoutLedger.Ports.Add({
				Node.UnitId, Port.Label, Port.bInput, Position});
		}
	}
	for (const FEdgeSpec& Spec : Edges)
	{
		FBlueprintLensLayoutEdgePlacement Edge;
		Edge.RelationId = Spec.RelationId;
		Edge.SourceUnitId = Spec.SourceUnitId;
		Edge.TargetUnitId = Spec.TargetUnitId;
		Edge.SourcePortLabel = Spec.SourcePortLabel;
		Edge.TargetPortLabel = Spec.TargetPortLabel;
		Edge.Family = EBlueprintLensLayoutRelationFamily::Execution;
		Result.LayoutLedger.Edges.Add(MoveTemp(Edge));
	}
	Result.LayoutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	Result.VisualOracleLedger = Result.LayoutLedger;
	Result.OutsideDetailLines = {TEXT("excluded sibling disclosure")};
	Result.OutsideDetailLinePositions = {FVector2D(24.0f, Result.CanvasSize.Y - 72.0f)};
	Result.OutsideDetailLineSizes = {FVector2D(Width - 48.0f, 14.0f)};
	Result.OutsideDetailBounds = Bounds(24.0f, Result.CanvasSize.Y - 84.0f, Width - 48.0f, 18.0f);
	Result.Actions = {
		{TEXT("select"), TEXT("Select output"), Bounds(24.0f, Result.CanvasSize.Y - 50.0f, 118.0f, 34.0f)},
		{TEXT("all-text"), TEXT("Show all text"), Bounds(152.0f, Result.CanvasSize.Y - 50.0f, 112.0f, 34.0f)},
		{TEXT("evidence"), TEXT("Evidence"), Bounds(274.0f, Result.CanvasSize.Y - 50.0f, 92.0f, 34.0f)}};
	Result.DiagnosticCode = Result.LayoutRequest.IsValid() &&
		Result.LayoutLedger.IsCompleteFor(Result.LayoutRequest)
		? TEXT("LC4_SEQUENCE_LAYOUT_COMPLETE")
		: TEXT("LC4_SEQUENCE_LAYOUT_LEDGER_INVALID");
	return Result;
}
} // namespace

const FBlueprintLensLC4SequenceStationLayout*
FBlueprintLensLC4SequenceLayout::FindStation(const int32 Ordinal) const
{
	return Stations.FindByPredicate(
		[Ordinal](const FBlueprintLensLC4SequenceStationLayout& Station)
		{
			return Station.Ordinal == Ordinal;
		});
}

const FBlueprintLensLC4SequenceVisualNode*
FBlueprintLensLC4SequenceLayout::FindVisualNode(const FString& UnitId) const
{
	return VisualNodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLC4SequenceVisualNode& Node)
		{
			return Node.UnitId == UnitId;
		});
}

bool FBlueprintLensLC4SequenceLayout::CoversProjection(
	const FBlueprintLensLC4SequenceProjection& Projection) const
{
	if (!Projection.IsRenderable() || Stations.Num() != Projection.Routes.Num() ||
		LayoutRequest.Nodes.Num() != Projection.AllUnitIds.Num() ||
		LayoutRequest.Edges.Num() != Projection.AllRelationIds.Num())
	{
		return false;
	}
	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		if (FindStation(Route.Ordinal) == nullptr)
		{
			return false;
		}
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (!LayoutRequest.Nodes.ContainsByPredicate(
			[&UnitId](const FBlueprintLensLayoutNodeRequest& Node)
			{
				return Node.UnitId == UnitId;
			}))
		{
			return false;
		}
	}
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		if (!LayoutRequest.Edges.ContainsByPredicate(
			[&RelationId](const FBlueprintLensLayoutEdgeRequest& Edge)
			{
				return Edge.RelationId == RelationId;
			}))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC4SequenceLayout::HasValidSharedLedger() const
{
	return DiagnosticCode == TEXT("LC4_SEQUENCE_LAYOUT_COMPLETE") &&
		LayoutLedger.IsCompleteFor(LayoutRequest) &&
		NearlyEqual(LayoutLedger.CanvasSize, CanvasSize, 0.5f);
}

bool FBlueprintLensLC4SequenceLayout::MatchesVisualOracle(
	const float Tolerance) const
{
	return SameLedgerGeometry(LayoutLedger, VisualOracleLedger, Tolerance);
}

bool FBlueprintLensLC4SequenceLayout::HasNoLabelRouteCollisions() const
{
	if (bLiveExplanation)
	{
		return !Stations.IsEmpty() && OutsideDetailBounds.bIsValid;
	}
	const FBox2D Canvas(FVector2D::ZeroVector, CanvasSize);
	if (OutsideDetailLines.IsEmpty() ||
		OutsideDetailLines.Num() != OutsideDetailLinePositions.Num() ||
		OutsideDetailLines.Num() != OutsideDetailLineSizes.Num() ||
		!OutsideDetailBounds.bIsValid ||
		!Canvas.IsInsideOrOn(OutsideDetailBounds.Min) ||
		!Canvas.IsInsideOrOn(OutsideDetailBounds.Max) ||
		OutsideDetailBounds.Intersect(WarningBounds))
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : LayoutLedger.Nodes)
	{
		if (Node.UnitId == LayoutRequest.Nodes[0].UnitId)
		{
			continue;
		}
		const FBox2D NodeBounds(Node.Position, Node.Position + Node.Size);
		if (OutsideDetailBounds.Intersect(NodeBounds))
		{
			return false;
		}
	}
	for (const FBlueprintLensLC4SequenceStationLayout& Station : Stations)
	{
		if (!Station.LabelBounds.bIsValid ||
			!Canvas.IsInsideOrOn(Station.LabelBounds.Min) ||
			!Canvas.IsInsideOrOn(Station.LabelBounds.Max))
		{
			return false;
		}
		for (const FBlueprintLensLayoutNodePlacement& Node : LayoutLedger.Nodes)
		{
			if (Node.UnitId == LayoutRequest.Nodes[0].UnitId)
			{
				continue;
			}
			const FBox2D NodeBounds(Node.Position, Node.Position + Node.Size);
			if (Station.LabelBounds.Intersect(NodeBounds))
			{
				return false;
			}
		}
	}
	return true;
}

FBlueprintLensLC4SequenceLayout FBlueprintLensLC4SequenceLayoutBuilder::Build(
	const FBlueprintLensLC4SequenceProjection& Projection,
	const float TargetWidth)
{
	if (Projection.bLiveExplanation)
	{
		return BuildLiveLayout(Projection, TargetWidth);
	}
	FBlueprintLensLC4SequenceLayout Result;
	Result.Mode = ModeForWidth(TargetWidth);
	const float Width = CanonicalWidth(Result.Mode);

	FBox2D SequenceBounds(EForceInit::ForceInit);
	TArray<FBox2D> BranchBounds;
	FBox2D SideEffectBounds(EForceInit::ForceInit);
	FBox2D ReconvergedBounds(EForceInit::ForceInit);
	FBox2D CriterionBounds(EForceInit::ForceInit);
	TArray<TArray<FVector2D>> RouteBends;

	if (Result.Mode == EBlueprintLensLC4SequenceLayoutMode::Wide700)
	{
		Result.CanvasSize = FVector2D(700.0f, 842.0f);
		Result.TitlePosition = FVector2D(24.0f, 38.0f);
		Result.SubtitlePositions = {FVector2D(24.0f, 64.0f)};
		Result.OrderBandBounds = Bounds(24.0f, 86.0f, 652.0f, 62.0f);
		Result.OrderStagePosition = FVector2D(42.0f, 109.0f);
		Result.OrderTextPosition = FVector2D(42.0f, 133.0f);
		Result.CountTextPositions = {FVector2D(430.0f, 111.0f), FVector2D(430.0f, 132.0f)};
		Result.PinOrderPosition = FVector2D(48.0f, 180.0f);
		Result.SpineStart = FVector2D(80.0f, 208.0f);
		Result.SpineEnd = FVector2D(80.0f, 710.0f);
		const float Ys[] = {230.0f, 342.0f, 590.0f, 700.0f};
		const float LabelYs[] = {182.0f, 296.0f, 542.0f, 680.0f};
		for (int32 Ordinal = 0; Ordinal < 4; ++Ordinal)
		{
			FBlueprintLensLC4SequenceStationLayout Station;
			Station.Ordinal = Ordinal;
			Station.Center = FVector2D(80.0f, Ys[Ordinal]);
			Station.Radius = 22.0f;
			Station.LabelBounds = Bounds(112.0f, LabelYs[Ordinal], 190.0f, 16.0f);
			Station.HitBounds = Bounds(56.0f, Ys[Ordinal] - 24.0f, 326.0f, 48.0f);
			Result.Stations.Add(Station);
		}
		SequenceBounds = Bounds(58.0f, 208.0f, 44.0f, 502.0f);
		BranchBounds = {Bounds(144,202,104,56), Bounds(276,202,104,56), Bounds(144,314,104,56), Bounds(276,314,104,56)};
		SideEffectBounds = Bounds(164,562,142,56);
		ReconvergedBounds = Bounds(473,402,134,62);
		CriterionBounds = Bounds(473,502,134,74);
		Result.MergeCenter = FVector2D(540.0f, 326.0f);
		Result.MergeRadius = 25.0f;
		Result.MergeToSuffixRoute = {FVector2D(540,352), FVector2D(540,397)};
		Result.WarningBounds = Bounds(424,586,232,58);
		Result.OutsideTerminalX = 374.0f;
		Result.OutsideTerminalStart = FVector2D(374,568);
		Result.OutsideTerminalEnd = FVector2D(374,612);
		Result.OutsideLabelPosition = FVector2D(390,548);
		Result.OutsideDetailLines = {TEXT("does not reach"), TEXT("criterion")};
		Result.OutsideDetailLinePositions = {FVector2D(390,556), FVector2D(390,568)};
		Result.OutsideDetailLineSizes = {FVector2D(80,12), FVector2D(80,12)};
		Result.OutsideDetailBounds = Bounds(390,556,80,28);
		Result.OutsideDetailFontSize = 8;
		Result.UnconnectedStubStartX = 103.0f;
		Result.UnconnectedStubEndX = 230.0f;
		Result.UnconnectedXTopLeft = FVector2D(216,687);
		Result.UnconnectedXBottomRight = FVector2D(230,713);
		Result.UnconnectedLabelPosition = FVector2D(246,705);
		Result.Actions = {
			{TEXT("select"),TEXT("Select output"),Bounds(24,746,118,38)},
			{TEXT("all-text"),TEXT("Show all text"),Bounds(152,746,112,38)},
			{TEXT("evidence"),TEXT("Evidence"),Bounds(274,746,92,38)},
			{TEXT("open-source"),TEXT("Open source"),Bounds(376,746,112,38)}};
		Result.FooterPosition = FVector2D(24,810);
		Result.FooterText = TEXT("OWNER SELECTED · FROZEN 700PX VISUAL ORACLE");
		RouteBends = {{}, {}, {FVector2D(468,230),FVector2D(472,326)}, {}, {}, {FVector2D(440,342),FVector2D(472,326)}, {}, {}};
	}
	else if (Result.Mode == EBlueprintLensLC4SequenceLayoutMode::Compact480)
	{
		Result.CanvasSize = FVector2D(480.0f, 1080.0f);
		Result.TitlePosition = FVector2D(24,38);
		Result.SubtitlePositions = {FVector2D(24,64), FVector2D(24,82)};
		Result.OrderBandBounds = Bounds(24,96,432,76);
		Result.OrderStagePosition = FVector2D(42,120);
		Result.OrderTextPosition = FVector2D(42,148);
		Result.CountTextPositions = {FVector2D(300,116),FVector2D(300,136),FVector2D(300,156)};
		Result.PinOrderPosition = FVector2D(24,198);
		Result.SpineStart = FVector2D(56,224);
		Result.SpineEnd = FVector2D(56,920);
		const float Ys[] = {250,410,800,920};
		const float LabelYs[] = {206,366,756,900};
		for (int32 Ordinal = 0; Ordinal < 4; ++Ordinal)
		{
			FBlueprintLensLC4SequenceStationLayout Station;
			Station.Ordinal = Ordinal;
			Station.Center = FVector2D(56,Ys[Ordinal]);
			Station.Radius = 18;
			Station.LabelBounds = Bounds(82,LabelYs[Ordinal],190,16);
			Station.HitBounds = Bounds(36,Ys[Ordinal]-22,260,44);
			Result.Stations.Add(Station);
		}
		SequenceBounds = Bounds(38,224,36,696);
		BranchBounds = {Bounds(92,224,92,52),Bounds(202,224,92,52),Bounds(92,384,92,52),Bounds(202,384,92,52)};
		SideEffectBounds = Bounds(92,774,136,52);
		ReconvergedBounds = Bounds(315,520,138,58);
		CriterionBounds = Bounds(315,602,138,68);
		Result.MergeCenter = FVector2D(384,468);
		Result.MergeRadius = 23;
		Result.MergeToSuffixRoute = {FVector2D(384,492),FVector2D(384,514)};
		Result.WarningBounds = Bounds(240,690,216,56);
		Result.OutsideTerminalX = 300;
		Result.OutsideTerminalStart = FVector2D(300,778);
		Result.OutsideTerminalEnd = FVector2D(300,822);
		Result.OutsideLabelPosition = FVector2D(316,794);
		Result.OutsideDetailLines = {TEXT("does not reach criterion")};
		Result.OutsideDetailLinePositions = {FVector2D(316,813)};
		Result.OutsideDetailLineSizes = {FVector2D(140,14)};
		Result.OutsideDetailBounds = Bounds(316,813,140,14);
		Result.OutsideDetailFontSize = 10;
		Result.UnconnectedStubStartX = 75;
		Result.UnconnectedStubEndX = 210;
		Result.UnconnectedXTopLeft = FVector2D(196,907);
		Result.UnconnectedXBottomRight = FVector2D(210,933);
		Result.UnconnectedLabelPosition = FVector2D(226,925);
		Result.Actions = {
			{TEXT("select"),TEXT("Select"),Bounds(24,1014,90,38)},
			{TEXT("all-text"),TEXT("All text"),Bounds(124,1014,92,38)},
			{TEXT("evidence"),TEXT("Evidence"),Bounds(226,1014,74,38)},
			{TEXT("open-source"),TEXT("Open source"),Bounds(310,1014,126,38)}};
		RouteBends = {{}, {}, {FVector2D(340,250),FVector2D(340,468)}, {}, {}, {FVector2D(326,410),FVector2D(340,468)}, {}, {}};
	}
	else
	{
		Result.CanvasSize = FVector2D(430.0f, 1360.0f);
		Result.TitlePosition = FVector2D(24,38);
		Result.SubtitlePositions = {FVector2D(24,64), FVector2D(24,82)};
		Result.OrderBandBounds = Bounds(24,98,382,88);
		Result.OrderStagePosition = FVector2D(42,123);
		Result.OrderTextPosition = FVector2D(42,153);
		Result.CountTextPositions = {FVector2D(42,170),FVector2D(42,181)};
		Result.PinOrderPosition = FVector2D(20,218);
		Result.SpineStart = FVector2D(50,236);
		Result.SpineEnd = FVector2D(50,1190);
		const float Ys[] = {260,520,1020,1190};
		const float LabelYs[] = {240,500,1000,1170};
		for (int32 Ordinal = 0; Ordinal < 4; ++Ordinal)
		{
			FBlueprintLensLC4SequenceStationLayout Station;
			Station.Ordinal = Ordinal;
			Station.Center = FVector2D(50,Ys[Ordinal]);
			Station.Radius = 18;
			Station.LabelBounds = Bounds(80,LabelYs[Ordinal],190,16);
			Station.HitBounds = Bounds(30,Ys[Ordinal]-22,302,80);
			Result.Stations.Add(Station);
		}
		SequenceBounds = Bounds(32,236,36,954);
		BranchBounds = {Bounds(82,284,112,52),Bounds(216,284,112,52),Bounds(82,544,112,52),Bounds(216,544,112,52)};
		SideEffectBounds = Bounds(82,1044,136,52);
		ReconvergedBounds = Bounds(278,732,128,58);
		CriterionBounds = Bounds(278,814,128,68);
		Result.MergeCenter = FVector2D(362,682);
		Result.MergeRadius = 22;
		Result.MergeToSuffixRoute = {FVector2D(362,705),FVector2D(362,726)};
		Result.WarningBounds = Bounds(190,902,216,56);
		Result.OutsideTerminalX = 292;
		Result.OutsideTerminalStart = FVector2D(292,1048);
		Result.OutsideTerminalEnd = FVector2D(292,1092);
		Result.OutsideLabelPosition = FVector2D(306,1064);
		Result.OutsideDetailLines = {TEXT("does not reach criterion")};
		Result.OutsideDetailLinePositions = {FVector2D(306,1083)};
		Result.OutsideDetailLineSizes = {FVector2D(114,14)};
		Result.OutsideDetailBounds = Bounds(306,1083,114,14);
		Result.OutsideDetailFontSize = 10;
		Result.UnconnectedStubStartX = 69;
		Result.UnconnectedStubEndX = 200;
		Result.UnconnectedXTopLeft = FVector2D(186,1177);
		Result.UnconnectedXBottomRight = FVector2D(200,1203);
		Result.UnconnectedLabelPosition = FVector2D(216,1195);
		Result.Actions = {
			{TEXT("select"),TEXT("Select"),Bounds(24,1280,84,38)},
			{TEXT("all-text"),TEXT("All text"),Bounds(118,1280,86,38)},
			{TEXT("evidence"),TEXT("Evidence"),Bounds(214,1280,72,38)},
			{TEXT("open-source"),TEXT("Open source"),Bounds(296,1280,112,38)}};
		RouteBends = {
			{FVector2D(76,260),FVector2D(76,310)}, {}, {FVector2D(360,310),FVector2D(360,682)},
			{FVector2D(76,520),FVector2D(76,570)}, {}, {FVector2D(350,570),FVector2D(350,682)},
			{FVector2D(76,1020),FVector2D(76,1070)}, {}};
	}

	if (!Projection.IsRenderable() || Projection.Routes.Num() != 4 ||
		Projection.Routes[0].RouteUnitIds.Num() != 2 ||
		Projection.Routes[1].RouteUnitIds.Num() != 2 ||
		Projection.Routes[2].RouteUnitIds.Num() != 1 ||
		!Projection.Routes[3].RouteUnitIds.IsEmpty())
	{
		Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LAYOUT_PROJECTION_UNAVAILABLE");
		return Result;
	}

	TMap<FString, FBox2D> NodeBounds;
	NodeBounds.Add(Projection.SequenceUnitId, SequenceBounds);
	NodeBounds.Add(Projection.Routes[0].RouteUnitIds[0], BranchBounds[0]);
	NodeBounds.Add(Projection.Routes[0].RouteUnitIds[1], BranchBounds[1]);
	NodeBounds.Add(Projection.Routes[1].RouteUnitIds[0], BranchBounds[2]);
	NodeBounds.Add(Projection.Routes[1].RouteUnitIds[1], BranchBounds[3]);
	NodeBounds.Add(Projection.Routes[2].RouteUnitIds[0], SideEffectBounds);
	NodeBounds.Add(Projection.Merge.NodeId, ReconvergedBounds);
	NodeBounds.Add(Projection.CriterionUnitId, CriterionBounds);

	Result.VisualNodes.Add({Projection.SequenceUnitId, Projection.SequenceReaderLabel, TEXT(""), EBlueprintLensLC4SequenceVisualNodeKind::SequenceSpine, INDEX_NONE});
	for (int32 RouteIndex = 0; RouteIndex < 2; ++RouteIndex)
	{
		for (int32 UnitIndex = 0; UnitIndex < 2; ++UnitIndex)
		{
			const FString& UnitId = Projection.Routes[RouteIndex].RouteUnitIds[UnitIndex];
			const FString& ReaderLabel = Projection.Routes[RouteIndex].RouteReaderLabels[UnitIndex];
			Result.VisualNodes.Add({UnitId, ShortPrimaryLabel(ReaderLabel), SecondaryLabel(ReaderLabel, EBlueprintLensLC4SequenceVisualNodeKind::Included), EBlueprintLensLC4SequenceVisualNodeKind::Included, RouteIndex});
		}
	}
	const FString& OutsideId = Projection.Routes[2].RouteUnitIds[0];
	const FString& OutsideReaderLabel = Projection.Routes[2].RouteReaderLabels[0];
	Result.VisualNodes.Add({OutsideId, ShortPrimaryLabel(OutsideReaderLabel), TEXT("terminal"), EBlueprintLensLC4SequenceVisualNodeKind::Outside, 2});
	Result.VisualNodes.Add({Projection.Merge.NodeId, ShortPrimaryLabel(Projection.Merge.ReaderLabel), TEXT("one canonical suffix"), EBlueprintLensLC4SequenceVisualNodeKind::Reconverged, INDEX_NONE});
	Result.VisualNodes.Add({Projection.CriterionUnitId, Projection.CriterionReaderLabel, TEXT("CRITERION"), EBlueprintLensLC4SequenceVisualNodeKind::Criterion, INDEX_NONE});

	Result.LayoutRequest.GraphKey = FString::Printf(TEXT("LC4:%s"), *Projection.ProjectionIntegrityHash);
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = Width;
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		const FBox2D* NodeBox = NodeBounds.Find(UnitId);
		if (NodeBox == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LAYOUT_NODE_UNACCOUNTED");
			return Result;
		}
		FBlueprintLensLayoutNodeRequest Node;
		Node.UnitId = UnitId;
		Node.DesiredSize = BoxSize(*NodeBox);
		Result.LayoutRequest.Nodes.Add(MoveTemp(Node));
	}

	auto AddPort = [&Result](const FString& UnitId, const FString& Label, const bool bInput, const int32 Order)
	{
		FBlueprintLensLayoutNodeRequest* Node = Result.LayoutRequest.Nodes.FindByPredicate(
			[&UnitId](const FBlueprintLensLayoutNodeRequest& Candidate){ return Candidate.UnitId == UnitId; });
		if (Node == nullptr || Node->Ports.ContainsByPredicate(
			[&Label,bInput](const FBlueprintLensLayoutPortRequest& Port){ return Port.Label == Label && Port.bInput == bInput; }))
		{
			return;
		}
		Node->Ports.Add({Label,bInput,Order});
	};

	TArray<FEdgeSpec> EdgeSpecs;
	int32 RouteBendIndex = 0;
	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		AddPort(Projection.SequenceUnitId, Route.SourcePinName, false, Route.Ordinal);
		for (int32 RelationIndex = 0; RelationIndex < Route.RouteRelationIds.Num(); ++RelationIndex)
		{
			FEdgeSpec Edge;
			Edge.RelationId = Route.RouteRelationIds[RelationIndex];
			Edge.SourceUnitId = RelationIndex == 0
				? Projection.SequenceUnitId
				: Route.RouteUnitIds[RelationIndex - 1];
			Edge.TargetUnitId = RelationIndex < Route.RouteUnitIds.Num()
				? Route.RouteUnitIds[RelationIndex]
				: Projection.Merge.NodeId;
			Edge.SourcePortLabel = RelationIndex == 0
				? Route.SourcePinName
				: FString::Printf(TEXT("out:%s"), *Edge.RelationId);
			Edge.TargetPortLabel = FString::Printf(TEXT("in:%s"), *Edge.RelationId);
			if (RouteBends.IsValidIndex(RouteBendIndex))
			{
				Edge.BendPoints = RouteBends[RouteBendIndex];
			}
			++RouteBendIndex;
			AddPort(Edge.SourceUnitId, Edge.SourcePortLabel, false, RelationIndex);
			AddPort(Edge.TargetUnitId, Edge.TargetPortLabel, true, RelationIndex);
			EdgeSpecs.Add(MoveTemp(Edge));
		}
	}
	for (int32 Index = 0; Index < Projection.Merge.SharedSuffixRelationIds.Num(); ++Index)
	{
		FEdgeSpec Edge;
		Edge.RelationId = Projection.Merge.SharedSuffixRelationIds[Index];
		Edge.SourceUnitId = Projection.Merge.SharedSuffixUnitIds[Index];
		Edge.TargetUnitId = Projection.Merge.SharedSuffixUnitIds[Index + 1];
		Edge.SourcePortLabel = FString::Printf(TEXT("out:%s"), *Edge.RelationId);
		Edge.TargetPortLabel = FString::Printf(TEXT("in:%s"), *Edge.RelationId);
		AddPort(Edge.SourceUnitId, Edge.SourcePortLabel, false, Index);
		AddPort(Edge.TargetUnitId, Edge.TargetPortLabel, true, Index);
		EdgeSpecs.Add(MoveTemp(Edge));
	}

	for (const FEdgeSpec& Spec : EdgeSpecs)
	{
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Spec.RelationId;
		Edge.SourceUnitId = Spec.SourceUnitId;
		Edge.TargetUnitId = Spec.TargetUnitId;
		Edge.SourcePortLabel = Spec.SourcePortLabel;
		Edge.TargetPortLabel = Spec.TargetPortLabel;
		Edge.Family = EBlueprintLensLayoutRelationFamily::Execution;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	FBlueprintLensLayoutGroupRequest Group;
	Group.GroupId = TEXT("lc4.sequence-disclosure-rail");
	Group.MemberUnitIds = Projection.AllUnitIds;
	Result.LayoutRequest.Groups.Add(MoveTemp(Group));

	Result.LayoutLedger.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Result.LayoutLedger.BackendVersion = TEXT("BlueprintLens.DeterministicLC4SequenceVisualOracle.v1");
	Result.LayoutLedger.ConfigurationFingerprint = FString::Printf(TEXT("profile=LayeredPorts;mode=%d;width=%.0f;oracle=v1"), static_cast<int32>(Result.Mode), Width);
	Result.LayoutLedger.CanvasSize = Result.CanvasSize;
	for (const FBlueprintLensLayoutNodeRequest& Requested : Result.LayoutRequest.Nodes)
	{
		const FBox2D& Box = NodeBounds.FindChecked(Requested.UnitId);
		Result.LayoutLedger.Nodes.Add({Requested.UnitId, Box.Min, BoxSize(Box)});
	}

	auto PortPosition = [&Result,&Projection,&NodeBounds](const FString& UnitId, const FString& Label, const bool bInput)
	{
		if (UnitId == Projection.SequenceUnitId)
		{
			const FBlueprintLensLC4SequenceRoute* Route = Projection.Routes.FindByPredicate(
				[&Label](const FBlueprintLensLC4SequenceRoute& Candidate){ return Candidate.SourcePinName == Label; });
			const FBlueprintLensLC4SequenceStationLayout* Station = Route != nullptr ? Result.FindStation(Route->Ordinal) : nullptr;
			return Station != nullptr ? Station->Center : FVector2D::ZeroVector;
		}
		const FBox2D& Box = NodeBounds.FindChecked(UnitId);
		if (UnitId == Projection.Merge.NodeId && bInput)
		{
			return Result.MergeCenter;
		}
		if (UnitId == Projection.Merge.NodeId && !bInput)
		{
			return FVector2D((Box.Min.X + Box.Max.X) * 0.5f, Box.Max.Y);
		}
		if (UnitId == Projection.CriterionUnitId && bInput)
		{
			return FVector2D((Box.Min.X + Box.Max.X) * 0.5f, Box.Min.Y);
		}
		return FVector2D(bInput ? Box.Min.X : Box.Max.X, (Box.Min.Y + Box.Max.Y) * 0.5f);
	};
	for (const FBlueprintLensLayoutNodeRequest& Node : Result.LayoutRequest.Nodes)
	{
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			Result.LayoutLedger.Ports.Add({Node.UnitId, Port.Label, Port.bInput, PortPosition(Node.UnitId, Port.Label, Port.bInput)});
		}
	}
	for (const FEdgeSpec& Spec : EdgeSpecs)
	{
		FBlueprintLensLayoutEdgePlacement Edge;
		Edge.RelationId = Spec.RelationId;
		Edge.SourceUnitId = Spec.SourceUnitId;
		Edge.TargetUnitId = Spec.TargetUnitId;
		Edge.SourcePortLabel = Spec.SourcePortLabel;
		Edge.TargetPortLabel = Spec.TargetPortLabel;
		Edge.Family = EBlueprintLensLayoutRelationFamily::Execution;
		Edge.BendPoints = Spec.BendPoints;
		Result.LayoutLedger.Edges.Add(MoveTemp(Edge));
	}
	Result.LayoutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	Result.VisualOracleLedger = Result.LayoutLedger;
	Result.DiagnosticCode = Result.LayoutRequest.IsValid() &&
		Result.LayoutLedger.IsCompleteFor(Result.LayoutRequest)
		? TEXT("LC4_SEQUENCE_LAYOUT_COMPLETE")
		: TEXT("LC4_SEQUENCE_LAYOUT_LEDGER_INVALID");
	return Result;
}
