#include "BlueprintLensLC6Layout.h"

#include "Styling/CoreStyle.h"

namespace
{
FBox2D Box(const float X, const float Y, const float Width, const float Height)
{
	return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
}

bool StrictlyIntersects(const FBox2D& Left, const FBox2D& Right)
{
	return Left.bIsValid && Right.bIsValid &&
		Left.Min.X < Right.Max.X && Left.Max.X > Right.Min.X &&
		Left.Min.Y < Right.Max.Y && Left.Max.Y > Right.Min.Y;
}

void AddLabel(
	FBlueprintLensLC6Layout& Layout,
	const FString& Id,
	const FString& Text,
	const FBox2D& Bounds,
	const int32 FontSize,
	const bool bBold = false,
	const FString& Color = TEXT("#F2F5F8"))
{
	FBlueprintLensLC6Label Label;
	Label.Id = Id;
	Label.Text = Text;
	Label.Bounds = Bounds;
	Label.FontSize = FontSize;
	Label.bBold = bBold;
	Label.ColorHex = Color;
	Layout.Labels.Add(MoveTemp(Label));
}

const FBlueprintLensLayoutNodePlacement* FindNode(
	const FBlueprintLensLayoutLedger& Ledger,
	const FString& UnitId)
{
	return Ledger.Nodes.FindByPredicate([&UnitId](const auto& Node)
	{
		return Node.UnitId == UnitId;
	});
}

bool SameLedger(
	const FBlueprintLensLayoutLedger& Left,
	const FBlueprintLensLayoutLedger& Right,
	const float Tolerance)
{
	if (!Left.CanvasSize.Equals(Right.CanvasSize, Tolerance) ||
		Left.Nodes.Num() != Right.Nodes.Num() ||
		Left.Ports.Num() != Right.Ports.Num() ||
		Left.Edges.Num() != Right.Edges.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : Left.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Other = FindNode(Right, Node.UnitId);
		if (Other == nullptr || !Node.Position.Equals(Other->Position, Tolerance) ||
			!Node.Size.Equals(Other->Size, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : Left.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Other =
			Right.FindPort(Port.UnitId, Port.Label, Port.bInput);
		if (Other == nullptr || !Port.Position.Equals(Other->Position, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Left.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Other =
			Right.Edges.FindByPredicate([&Edge](const auto& Candidate)
			{
				return Candidate.RelationId == Edge.RelationId;
			});
		if (Other == nullptr || Edge.BendPoints.Num() != Other->BendPoints.Num())
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

FString ShortCriterionLabel(
	const FBlueprintLensLC6Track& Track,
	const bool bLiveBoundaryTracks)
{
	if (bLiveBoundaryTracks)
	{
		return Track.BoundaryNodeId == Track.CriterionNodeId
			? TEXT("Same unit")
			: TEXT("Criterion");
	}
	return Track.ScenarioId == TEXT("LC6_OPAQUE") ? TEXT("Set Opaque") :
		Track.ScenarioId == TEXT("LC6_UNCERTAIN") ? TEXT("Set Uncertain") :
		Track.ScenarioId == TEXT("LC6_UNSUPPORTED") ? TEXT("Set Unsupported") :
		TEXT("Set …06");
}

FString StatusColor(const FBlueprintLensLC6Track& Track)
{
	return Track.Status.Equals(TEXT("opaque"), ESearchCase::IgnoreCase)
		? TEXT("#F0B35A") :
		Track.Status.Equals(TEXT("uncertain"), ESearchCase::IgnoreCase)
			? TEXT("#E9D66B") :
		Track.Status.Equals(TEXT("unsupported"), ESearchCase::IgnoreCase)
				? TEXT("#F07178") :
		TEXT("#D997FF");
}

void BuildDisplayGeometry(
	FBlueprintLensLC6Layout& Layout,
	const FBlueprintLensLC6Projection& Projection)
{
	const bool bWide = Layout.Mode == EBlueprintLensLC6LayoutMode::SideBySide700;
	const float Width = Layout.CanvasSize.X;
	const float HitX = bWide ? 38.0f : Layout.Mode == EBlueprintLensLC6LayoutMode::SingleColumn430
		? 37.0f : 39.0f;
	const float HitWidth = bWide ? 374.0f : Width - 76.0f -
		(Layout.Mode == EBlueprintLensLC6LayoutMode::StackedDetail480 ? 4.0f : 0.0f);
	// Live duplicate titles carry an eight-character source identity through
	// BlueprintLensDisplayLabel. Reserve enough width for that identity to be
	// visible; retaining the fixture-sized node silently clips the distinction.
	const float BoundaryWidth = Projection.bLiveBoundaryTracks ? 150.0f :
		bWide ? 78.0f :
		Layout.Mode == EBlueprintLensLC6LayoutMode::SingleColumn430 ? 74.0f : 83.0f;
	const float CriterionWidth = 92.0f;
	const float CriterionX = HitX + HitWidth - CriterionWidth - 10.0f;
	Layout.OverviewBounds = bWide ? Box(24.0f, 120.0f, 404.0f, 570.0f) :
		Box(24.0f, 120.0f, Width - 48.0f, 570.0f);
	Layout.DetailBounds = bWide ? Box(442.0f, 120.0f, 234.0f, 570.0f) :
		Box(24.0f, 708.0f, Width - 48.0f, 570.0f);
	Layout.CompleteTextActionBounds = bWide ? Box(38.0f, 718.0f, 190.0f, 22.0f) :
		Box(32.0f, 1306.0f, 111.0f, 22.0f);

	AddLabel(Layout, TEXT("header.title"),
		Projection.bLiveBoundaryTracks
			? TEXT("Boundary Truth-Owner Tracks")
			: TEXT("Four-Track Boundary Overview"),
		Box(bWide ? 42.0f : 35.0f, 38.0f, Width - 77.0f, 28.0f), 17, true);
	AddLabel(Layout, TEXT("header.question"),
		TEXT("Why does analysis stop here — and who owns the stop?"),
		Box(bWide ? 42.0f : 35.0f, 73.0f, Width - 75.0f, 18.0f), 10, true);
	const int32 CoreTrackCount = Projection.Tracks.FilterByPredicate(
		[](const FBlueprintLensLC6Track& Track)
		{
			return Track.TruthOwner == TEXT("core_node_classification");
		}).Num();
	const int32 QueryTrackCount = Projection.Tracks.Num() - CoreTrackCount;
	AddLabel(
		Layout,
		TEXT("overview.core.owner"),
		Projection.bLiveBoundaryTracks
			? FString::Printf(
				TEXT("CORE CLASSIFICATION · %d LIVE INSTANCES"),
				CoreTrackCount)
			: TEXT("CORE CLASSIFICATION · 3"),
		Box(HitX + 2.0f, 142.0f, 270.0f, 14.0f),
		9,
		true,
		TEXT("#67B7FF"));
	const float QueryOwnerY = Projection.bLiveBoundaryTracks
		? 168.0f + Projection.Tracks.Num() * 92.0f + 6.0f
		: 458.0f;
	if (!Projection.bLiveBoundaryTracks || QueryTrackCount > 0)
	{
		AddLabel(
			Layout,
			TEXT("overview.query.owner"),
			Projection.bLiveBoundaryTracks
				? FString::Printf(
					TEXT("QUERY BUDGET · %d LIVE INSTANCES"),
					QueryTrackCount)
				: TEXT("QUERY BUDGET · 1"),
			Box(HitX + 2.0f, QueryOwnerY, 250.0f, 14.0f),
			9,
			true,
			TEXT("#D997FF"));
	}

	for (int32 Index = 0; Index < Projection.Tracks.Num(); ++Index)
	{
		const FBlueprintLensLC6Track& Track = Projection.Tracks[Index];
		FBlueprintLensLC6TrackLayout Geometry;
		Geometry.ScenarioId = Track.ScenarioId;
		const bool bQueryTrack =
			Track.TruthOwner == TEXT("query_profile");
		const float HitY = Projection.bLiveBoundaryTracks || !bQueryTrack
			? 168.0f + Index * 92.0f
			: 482.0f;
		Geometry.HitBounds = Box(
			HitX, HitY, HitWidth, bQueryTrack ? 192.0f : 78.0f);
		const FString Prefix = TEXT("overview.") + Track.ScenarioId;
		AddLabel(Layout, Prefix + TEXT(".status"), Track.Status.ToUpper(),
			Box(HitX + 10.0f, HitY + (bQueryTrack ? 12.0f : 8.0f), 110.0f, 14.0f),
			9, true, StatusColor(Track));
		if (!bQueryTrack)
		{
			Geometry.BoundaryBounds = Box(HitX + 10.0f, HitY + 33.0f,
				BoundaryWidth, 28.0f);
			Geometry.CriterionBounds = Box(CriterionX, HitY + 33.0f,
				CriterionWidth, 28.0f);
			Geometry.SemanticFenceStart = FVector2D(
				Geometry.BoundaryBounds.Max.X + 10.0f, HitY + 29.0f);
			Geometry.SemanticFenceEnd = FVector2D(
				Geometry.SemanticFenceStart.X, HitY + 65.0f);
			Geometry.RouteStart = FVector2D(
				Geometry.SemanticFenceStart.X + 3.0f, HitY + 47.0f);
			Geometry.RouteEnd = FVector2D(CriterionX, HitY + 47.0f);
			Geometry.bHasSemanticFence = true;
			AddLabel(Layout, Prefix + TEXT(".boundary"), Track.BoundaryTitle,
				Box(Geometry.BoundaryBounds.Min.X + 7.0f,
					Geometry.BoundaryBounds.Min.Y + 7.0f,
					BoundaryWidth - 12.0f, 14.0f), 8, true);
		}
		else
		{
			Geometry.OmissionBounds = Box(HitX + 10.0f, HitY + 49.0f,
				bWide ? 62.0f : HitWidth < 380.0f ? 59.0f : 66.0f, 42.0f);
			const float NodeWidth = bWide ? 30.0f : HitWidth < 380.0f ? 28.0f : 32.0f;
			const float Gap = bWide ? 10.0f : HitWidth < 380.0f ? 8.0f : 11.0f;
			const float FirstNodeX = Geometry.OmissionBounds.Max.X +
				(bWide ? 48.0f : HitWidth < 380.0f ? 36.0f : 49.0f);
			Geometry.RouteStart = FVector2D(Geometry.OmissionBounds.Max.X, HitY + 70.0f);
			Geometry.RouteEnd = FVector2D(CriterionX, HitY + 70.0f);
			Geometry.FrontierBounds = Box(
				Geometry.RouteStart.X + 3.0f, HitY + 56.0f, 26.0f, 28.0f);
			for (int32 NodeIndex = 0; NodeIndex < 4; ++NodeIndex)
			{
				Geometry.QueryNodeBounds.Add(Box(
					FirstNodeX + NodeIndex * (NodeWidth + Gap),
					HitY + 58.0f, NodeWidth, 24.0f));
				Geometry.QueryHopLabels.Add(FString::FromInt(NodeIndex + 3));
			}
			Geometry.CriterionBounds = Box(CriterionX, HitY + 56.0f,
				CriterionWidth, 28.0f);
			AddLabel(Layout, Prefix + TEXT(".omitted.nodes"), TEXT("3 nodes"),
				Box(Geometry.OmissionBounds.Min.X + 7.0f,
					Geometry.OmissionBounds.Min.Y + 8.0f, 48.0f, 14.0f), 8, true, TEXT("#A9B3C1"));
			AddLabel(Layout, Prefix + TEXT(".omitted.edges"), TEXT("3 edges"),
				Box(Geometry.OmissionBounds.Min.X + 7.0f,
					Geometry.OmissionBounds.Min.Y + 23.0f, 48.0f, 14.0f), 8, false, TEXT("#A9B3C1"));
			for (int32 NodeIndex = 0; NodeIndex < 4; ++NodeIndex)
			{
				AddLabel(Layout, Prefix + FString::Printf(TEXT(".hop.%d"), NodeIndex + 3),
					Geometry.QueryHopLabels[NodeIndex],
					Box(Geometry.QueryNodeBounds[NodeIndex].Min.X + NodeWidth * 0.34f,
						Geometry.QueryNodeBounds[NodeIndex].Min.Y + 6.0f, 10.0f, 12.0f), 7, true);
			}
			AddLabel(Layout, Prefix + TEXT(".caption"),
				TEXT("budget 3 · selected 4/3 · complete 7/6"),
				Box(HitX + 10.0f, HitY + 112.0f, 270.0f, 13.0f), 8);
			AddLabel(Layout, Prefix + TEXT(".hint"),
				TEXT("Select any track to inspect the stopping evidence →"),
				Box(HitX + 10.0f, HitY + 151.0f, 310.0f, 13.0f), 8, false, TEXT("#A9B3C1"));
		}
		Geometry.CriterionMarker = FVector2D(
			Geometry.CriterionBounds.Max.X - 8.0f,
			Geometry.CriterionBounds.GetCenter().Y);
		AddLabel(Layout, Prefix + TEXT(".criterion"),
			ShortCriterionLabel(Track, Projection.bLiveBoundaryTracks),
			Box(Geometry.CriterionBounds.Min.X + 8.0f,
				Geometry.CriterionBounds.Min.Y + 7.0f,
				CriterionWidth - 22.0f, 14.0f), bQueryTrack ? 8 : 7, true, TEXT("#A7D46F"));
		Layout.Tracks.Add(MoveTemp(Geometry));
	}

	const float DetailX = bWide ? 458.0f : 50.0f;
	const float DetailY = bWide ? 144.0f : 732.0f;
	AddLabel(Layout, TEXT("detail.heading"), TEXT("DETAIL"),
		Box(DetailX, DetailY, 80.0f, 16.0f), 10, true, TEXT("#A9B3C1"));
	AddLabel(Layout, TEXT("detail.empty.title"),
		Projection.bLiveBoundaryTracks
			? TEXT("Select an instance")
			: TEXT("Select a scenario"),
		Box(DetailX, DetailY + 46.0f, 180.0f, 24.0f), 14, true);
	AddLabel(Layout, TEXT("detail.empty.subtitle"), TEXT("to inspect why analysis stops"),
		Box(DetailX, DetailY + 70.0f, 190.0f, 14.0f), 9, false, TEXT("#A9B3C1"));
	AddLabel(Layout, TEXT("action.complete_text"), TEXT("Show complete text"),
		Box(Layout.CompleteTextActionBounds.Min.X + 10.0f,
			Layout.CompleteTextActionBounds.Min.Y + 4.0f,
			Layout.CompleteTextActionBounds.GetSize().X - 20.0f, 14.0f),
		bWide ? 8 : 7, true);
}

FVector2D NodePosition(
	const FBlueprintLensLC6Layout& Layout,
	const FString& ScenarioId,
	const int32 Index,
	const int32 Count)
{
	const FBlueprintLensLC6TrackLayout* Track =
		Layout.Tracks.FindByPredicate([&ScenarioId](const auto& Item)
		{
			return Item.ScenarioId == ScenarioId;
		});
	if (Track == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	const float Left = Track->HitBounds.Min.X + 8.0f;
	const float Right = Track->HitBounds.Max.X - 16.0f;
	const float X = Count > 1 ? FMath::Lerp(Left, Right, float(Index) / float(Count - 1)) : Left;
	const float Y = Track->HitBounds.Min.Y + (ScenarioId == TEXT("LC6_TRUNCATED") ? 98.0f : 62.0f);
	return FVector2D(X, Y);
}
} // namespace

FSlateFontInfo BlueprintLensLC6Font(const int32 FontSize, const bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(
		bBold ? TEXT("Bold") : TEXT("Regular"), FontSize);
}

bool FBlueprintLensLC6Layout::CoversProjection(
	const FBlueprintLensLC6Projection& Projection) const
{
	if (!Projection.IsRenderable() || DiagnosticCode != TEXT("LC6_LAYOUT_COMPLETE") ||
		Tracks.Num() != Projection.Tracks.Num() ||
		LayoutRequest.Nodes.Num() != Projection.AllMemberIds.Num() ||
		LayoutRequest.Edges.Num() != Projection.AllRelationIds.Num() ||
		SourceAnchors.Num() != Projection.AllMemberIds.Num())
	{
		return false;
	}
	for (const FBlueprintLensLC6Track& Projected : Projection.Tracks)
	{
		if (!Tracks.ContainsByPredicate([&Projected](const auto& Track)
			{ return Track.ScenarioId == Projected.ScenarioId; }))
		{
			return false;
		}
	}
	for (const FBlueprintLensLC6Relation& Relation : Projection.Relations)
	{
		const FBlueprintLensLayoutEdgeRequest* Edge =
			LayoutRequest.Edges.FindByPredicate([&Relation](const auto& Item)
			{
				return Item.RelationId == Relation.RelationId;
			});
		if (Edge == nullptr || Edge->SourceUnitId != Relation.SourceNodeId ||
			Edge->TargetUnitId != Relation.TargetNodeId)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC6Layout::HasValidSharedLedger() const
{
	return LayoutRequest.IsValid() &&
		VisualOracleLedger.IsCompleteFor(LayoutRequest) &&
		LayoutLedger.IsCompleteFor(LayoutRequest) &&
		LayoutLedger.HasNoNodeOverlaps();
}

bool FBlueprintLensLC6Layout::MatchesVisualOracle(const float Tolerance) const
{
	return SameLedger(LayoutLedger, VisualOracleLedger, Tolerance);
}

bool FBlueprintLensLC6Layout::HasNoTextOrRouteCollisions() const
{
	for (const FBlueprintLensLC6Label& Label : Labels)
	{
		if (!Label.Bounds.bIsValid || Label.Text.IsEmpty() ||
			Label.Bounds.Min.X < 0.0f || Label.Bounds.Min.Y < 0.0f ||
			Label.Bounds.Max.X > CanvasSize.X || Label.Bounds.Max.Y > CanvasSize.Y)
		{
			return false;
		}
	}
	for (int32 Left = 0; Left < Tracks.Num(); ++Left)
	{
		if (!Tracks[Left].HitBounds.bIsValid ||
			Tracks[Left].RouteStart.IsNearlyZero() || Tracks[Left].RouteEnd.IsNearlyZero() ||
			Tracks[Left].RouteStart.X >= Tracks[Left].RouteEnd.X)
		{
			return false;
		}
		for (int32 Right = Left + 1; Right < Tracks.Num(); ++Right)
		{
			if (StrictlyIntersects(Tracks[Left].HitBounds, Tracks[Right].HitBounds))
			{
				return false;
			}
		}
	}
	return true;
}

FBlueprintLensLC6Layout FBlueprintLensLC6LayoutBuilder::Build(
	const FBlueprintLensLC6Projection& Projection,
	const float TargetWidth)
{
	FBlueprintLensLC6Layout Result;
	Result.Mode = TargetWidth <= 430.0f
		? EBlueprintLensLC6LayoutMode::SingleColumn430
		: TargetWidth < 700.0f
			? EBlueprintLensLC6LayoutMode::StackedDetail480
			: EBlueprintLensLC6LayoutMode::SideBySide700;
	Result.CanvasSize = Result.Mode == EBlueprintLensLC6LayoutMode::SideBySide700
		? FVector2D(700.0f, 760.0f)
		: FVector2D(Result.Mode == EBlueprintLensLC6LayoutMode::SingleColumn430
			? 430.0f : 480.0f, 1350.0f);
	if (!Projection.IsRenderable())
	{
		Result.DiagnosticCode = TEXT("LC6_LAYOUT_PROJECTION_UNAVAILABLE");
		return Result;
	}

	BuildDisplayGeometry(Result, Projection);
	Result.LayoutRequest.GraphKey = TEXT("LC6_SPLIT_FRONTIER_ROUTES");
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	Result.LayoutRequest.TargetWidth = Result.CanvasSize.X;

	TArray<FBlueprintLensLC6Relation> Relations = Projection.Relations;
	Relations.Sort([](const auto& Left, const auto& Right)
	{
		return Left.RelationId < Right.RelationId;
	});
	for (const FBlueprintLensLC6Track& Track : Projection.Tracks)
	{
		TArray<FString> Members = Track.MemberIds;
		Members.Sort([&Track](const FString& Left, const FString& Right)
		{
			if (Track.ScenarioId == TEXT("LC6_TRUNCATED"))
			{
				const auto Distance = [&Track](const FString& Id)
				{
					const FBlueprintLensLC6HopDistance* Hop =
						Track.HopDistances.FindByPredicate([&Id](const auto& Item)
						{ return Item.NodeId == Id; });
					return Hop != nullptr ? Hop->Distance : MAX_int32;
				};
				return Distance(Left) > Distance(Right);
			}
			return Left < Right;
		});
		for (const FString& MemberId : Members)
		{
			FBlueprintLensLayoutNodeRequest Node;
			Node.UnitId = MemberId;
			Node.DesiredSize = FVector2D(8.0f, 8.0f);
			int32 InputOrder = 0;
			int32 OutputOrder = 0;
			for (const FBlueprintLensLC6Relation& Relation : Relations)
			{
				if (Relation.TargetNodeId == MemberId)
				{
					Node.Ports.Add({Relation.RelationId + TEXT(":in"), true, InputOrder++});
				}
				if (Relation.SourceNodeId == MemberId)
				{
					Node.Ports.Add({Relation.RelationId + TEXT(":out"), false, OutputOrder++});
				}
			}
			Result.LayoutRequest.Nodes.Add(MoveTemp(Node));
		}
	}
	for (const FBlueprintLensLC6Relation& Relation : Relations)
	{
		if (!Projection.AllMemberIds.Contains(Relation.SourceNodeId) ||
			!Projection.AllMemberIds.Contains(Relation.TargetNodeId))
		{
			Result.DiagnosticCode = TEXT("LC6_LAYOUT_RELATION_ENDPOINT_UNACCOUNTED");
			return Result;
		}
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.RelationId;
		Edge.SourceUnitId = Relation.SourceNodeId;
		Edge.TargetUnitId = Relation.TargetNodeId;
		Edge.SourcePortLabel = Relation.RelationId + TEXT(":out");
		Edge.TargetPortLabel = Relation.RelationId + TEXT(":in");
		Edge.Family = Relation.ScenarioId == TEXT("LC6_TRUNCATED")
			? EBlueprintLensLayoutRelationFamily::Frontier
			: Relation.Kind == TEXT("data")
				? EBlueprintLensLayoutRelationFamily::Value
				: EBlueprintLensLayoutRelationFamily::Execution;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	for (const FBlueprintLensLC6OwnerBand& Band : Projection.OwnerBands)
	{
		FBlueprintLensLayoutGroupRequest Group;
		Group.GroupId = Band.TruthOwner;
		for (const FString& ScenarioId : Band.ScenarioIds)
		{
			const FBlueprintLensLC6Track* Track = Projection.FindTrack(ScenarioId);
			if (Track != nullptr)
			{
				Group.MemberUnitIds.Append(Track->MemberIds);
			}
		}
		Result.LayoutRequest.Groups.Add(MoveTemp(Group));
	}
	if (!Result.LayoutRequest.IsValid())
	{
		Result.DiagnosticCode = TEXT("LC6_LAYOUT_REQUEST_INVALID");
		return Result;
	}

	FBlueprintLensLayoutLedger Oracle;
	Oracle.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Oracle.BackendVersion = TEXT("BlueprintLens.LC6FourTrackOracle.v1");
	Oracle.ConfigurationFingerprint = FString::Printf(
		TEXT("lc6-four-track;mode=%d;width=%.0f"),
		static_cast<int32>(Result.Mode), Result.CanvasSize.X);
	Oracle.CanvasSize = Result.CanvasSize;
	Oracle.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (const FBlueprintLensLC6Track& Track : Projection.Tracks)
	{
		TArray<FString> Members = Track.MemberIds;
		Members.Sort([&Track](const FString& Left, const FString& Right)
		{
			if (Track.ScenarioId == TEXT("LC6_TRUNCATED"))
			{
				const auto Distance = [&Track](const FString& Id)
				{
					const FBlueprintLensLC6HopDistance* Hop =
						Track.HopDistances.FindByPredicate([&Id](const auto& Item)
						{ return Item.NodeId == Id; });
					return Hop != nullptr ? Hop->Distance : MAX_int32;
				};
				return Distance(Left) > Distance(Right);
			}
			return Left < Right;
		});
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			FBlueprintLensLayoutNodePlacement Node;
			Node.UnitId = Members[Index];
			Node.Position = NodePosition(Result, Track.ScenarioId, Index, Members.Num());
			Node.Size = FVector2D(8.0f, 8.0f);
			Oracle.Nodes.Add(MoveTemp(Node));
		}
	}
	for (const FBlueprintLensLayoutNodeRequest& Node : Result.LayoutRequest.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Placement = FindNode(Oracle, Node.UnitId);
		if (Placement == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC6_LAYOUT_NODE_UNACCOUNTED");
			return Result;
		}
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			FBlueprintLensLayoutPortPlacement PortPlacement;
			PortPlacement.UnitId = Node.UnitId;
			PortPlacement.Label = Port.Label;
			PortPlacement.bInput = Port.bInput;
			PortPlacement.Position = Placement->Position +
				FVector2D(Port.bInput ? 0.0f : Placement->Size.X,
					Placement->Size.Y * 0.5f);
			Oracle.Ports.Add(MoveTemp(PortPlacement));
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
		Oracle.Edges.Add(MoveTemp(Edge));
	}
	if (!Oracle.IsCompleteFor(Result.LayoutRequest))
	{
		Result.DiagnosticCode = TEXT("LC6_LAYOUT_ORACLE_INCOMPLETE");
		return Result;
	}
	Result.VisualOracleLedger = Oracle;
	Result.LayoutLedger = Oracle;
	for (const FBlueprintLensLC6Track& Track : Projection.Tracks)
	{
		for (const FString& MemberId : Track.MemberIds)
		{
			const FBlueprintLensLayoutNodePlacement* Node = FindNode(Oracle, MemberId);
			FBlueprintLensLC6SourceAnchor Anchor;
			Anchor.ScenarioId = Track.ScenarioId;
			Anchor.SourceNodeId = MemberId;
			Anchor.Position = Node->Position + Node->Size * 0.5f;
			Result.SourceAnchors.Add(MoveTemp(Anchor));
		}
	}
	Result.DiagnosticCode = Result.HasNoTextOrRouteCollisions()
		? TEXT("LC6_LAYOUT_COMPLETE") : TEXT("LC6_LAYOUT_GEOMETRY_INVALID");
	return Result;
}
