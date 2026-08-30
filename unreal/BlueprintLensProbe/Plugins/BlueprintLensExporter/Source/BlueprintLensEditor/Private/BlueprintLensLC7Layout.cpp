#include "BlueprintLensLC7Layout.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"

namespace
{
FBox2D Box(
	const float X,
	const float Y,
	const float Width,
	const float Height)
{
	return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
}

bool StrictlyIntersects(const FBox2D& Left, const FBox2D& Right)
{
	return Left.bIsValid && Right.bIsValid &&
		Left.Min.X < Right.Max.X && Left.Max.X > Right.Min.X &&
		Left.Min.Y < Right.Max.Y && Left.Max.Y > Right.Min.Y;
}

bool Contains(const FBox2D& Outer, const FBox2D& Inner)
{
	return Outer.bIsValid && Inner.bIsValid &&
		Inner.Min.X >= Outer.Min.X && Inner.Max.X <= Outer.Max.X &&
		Inner.Min.Y >= Outer.Min.Y && Inner.Max.Y <= Outer.Max.Y;
}

bool SameSet(const TSet<FString>& Left, const TSet<FString>& Right)
{
	return Left.Num() == Right.Num() && Left.Difference(Right).IsEmpty();
}

FString StableSetText(const TSet<FString>& Values)
{
	TArray<FString> Sorted = Values.Array();
	Sorted.Sort();
	return FString::Join(Sorted, TEXT("|"));
}

const FBlueprintLensLC7SCCRecord* FindFocusedSCC(
	const FBlueprintLensLC7Projection& Projection,
	const FString& RequestedSCCId)
{
	const FBlueprintLensLC7SCCRecord* Focused = nullptr;
	for (const FBlueprintLensLC7SCCRecord& SCC : Projection.SCCs)
	{
		if (SCC.CriterionUnitId == Projection.CriterionUnitId)
		{
			if (Focused != nullptr)
			{
				return nullptr;
			}
			Focused = &SCC;
		}
	}
	if (Focused == nullptr ||
		(!RequestedSCCId.IsEmpty() && RequestedSCCId != Focused->GroupId))
	{
		return nullptr;
	}
	return Focused;
}

bool IsAccountableProjection(const FBlueprintLensLC7Projection& Projection)
{
	if (!Projection.IsRenderable() ||
		!Projection.AllUnitIds.Contains(Projection.CriterionUnitId) ||
		Projection.Relations.Num() != Projection.AllRelationIds.Num())
	{
		return false;
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (!Projection.UnitTitles.Contains(UnitId) ||
			Projection.UnitTitles.FindChecked(UnitId).IsEmpty() ||
			!Projection.SourceAnchors.Contains(UnitId))
		{
			return false;
		}
	}
	TSet<FString> RelationIds;
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		if (Relation.RelationId.IsEmpty() ||
			RelationIds.Contains(Relation.RelationId) ||
			!Projection.AllRelationIds.Contains(Relation.RelationId) ||
			!Projection.AllUnitIds.Contains(Relation.SourceUnitId) ||
			!Projection.AllUnitIds.Contains(Relation.TargetUnitId) ||
			!Projection.SCCs.ContainsByPredicate([&Relation](const auto& SCC)
			{
				return SCC.GroupId == Relation.OwningSCCId;
			}))
		{
			return false;
		}
		RelationIds.Add(Relation.RelationId);
	}
	for (const FBlueprintLensLC7SCCRecord& SCC : Projection.SCCs)
	{
		if (SCC.GroupId.IsEmpty() || SCC.OrderedSpineUnitIds.IsEmpty())
		{
			return false;
		}
		TSet<FString> Members;
		for (const FString& UnitId : SCC.OrderedSpineUnitIds)
		{
			if (!Projection.AllUnitIds.Contains(UnitId) || Members.Contains(UnitId))
			{
				return false;
			}
			Members.Add(UnitId);
		}
	}
	return SameSet(RelationIds, Projection.AllRelationIds);
}

void SetResponsiveGeometry(
	FBlueprintLensLC7Layout& Layout,
	const float TargetWidth)
{
	const float Width = TargetWidth >= 700.0f ? 700.0f :
		TargetWidth >= 480.0f ? 480.0f : 430.0f;
	if (Width >= 700.0f)
	{
		Layout.ResponsiveMode = EBlueprintLensLC7ResponsiveMode::SideBySide700;
		Layout.CanvasSize = FVector2D(700.0f, 760.0f);
		Layout.ContentBounds = Box(24.0f, 142.0f, 652.0f, 594.0f);
		Layout.OverviewBounds = Box(24.0f, 142.0f, 414.0f, 594.0f);
		Layout.DetailBounds = Box(454.0f, 142.0f, 222.0f, 594.0f);
	}
	else
	{
		Layout.ResponsiveMode = Width <= 430.0f
			? EBlueprintLensLC7ResponsiveMode::SingleColumn430
			: EBlueprintLensLC7ResponsiveMode::StackedDetail480;
		Layout.CanvasSize = FVector2D(Width, 1296.0f);
		Layout.ContentBounds = Box(24.0f, 142.0f, Width - 48.0f, 1130.0f);
		Layout.OverviewBounds = Box(24.0f, 142.0f, Width - 48.0f, 594.0f);
		Layout.DetailBounds = Box(24.0f, 752.0f, Width - 48.0f, 520.0f);
	}
}

void FinalizeRecoverabilityHash(FBlueprintLensLC7Layout& Layout)
{
	TArray<FString> Parts = {
		FString::FromInt(static_cast<int32>(Layout.ScaleMode)),
		Layout.FocusedSCCId,
		StableSetText(Layout.VisibleUnitIds),
		StableSetText(Layout.VisibleRelationIds)};
	for (const FBlueprintLensLC7Fold& Fold : Layout.Folds)
	{
		Parts.Add(Fold.FoldId + TEXT(":") + StableSetText(Fold.UnitIds) +
			TEXT(":") + StableSetText(Fold.RelationIds));
	}
	for (const FBlueprintLensLC7IndexRow& Row : Layout.IndexRows)
	{
		Parts.Add(Row.SCCId + TEXT(":") + StableSetText(Row.UnitIds) +
			TEXT(":") + StableSetText(Row.RelationIds));
	}
	Layout.RecoverabilityHash = FString::Join(Parts, TEXT(";"));
}

EBlueprintLensLayoutRelationFamily ToLayoutFamily(
	const EBlueprintLensLC7RelationFamily Family)
{
	switch (Family)
	{
	case EBlueprintLensLC7RelationFamily::Value:
		return EBlueprintLensLayoutRelationFamily::Value;
	case EBlueprintLensLC7RelationFamily::Predicate:
		return EBlueprintLensLayoutRelationFamily::Predicate;
	case EBlueprintLensLC7RelationFamily::Return:
		return EBlueprintLensLayoutRelationFamily::BackEdge;
	default:
		return EBlueprintLensLayoutRelationFamily::Execution;
	}
}

const FBlueprintLensLC7Relation* FindRelation(
	const FBlueprintLensLC7Projection& Projection,
	const EBlueprintLensLC7RelationFamily Family,
	const FString& Source,
	const FString& Target)
{
	return Projection.Relations.FindByPredicate(
		[Family, &Source, &Target](const FBlueprintLensLC7Relation& Relation)
		{
			return Relation.Family == Family &&
				(Source.IsEmpty() || Relation.SourceUnitId == Source) &&
				(Target.IsEmpty() || Relation.TargetUnitId == Target);
		});
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
		if (Other == nullptr || Edge.SourceUnitId != Other->SourceUnitId ||
			Edge.TargetUnitId != Other->TargetUnitId ||
			Edge.Family != Other->Family ||
			Edge.BendPoints.Num() != Other->BendPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Edge.BendPoints.Num(); ++Index)
		{
			if (!Edge.BendPoints[Index].Equals(
				Other->BendPoints[Index], Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}

bool CollinearOverlap(
	const FVector2D& A,
	const FVector2D& B,
	const FVector2D& C,
	const FVector2D& D)
{
	if (FMath::IsNearlyEqual(A.X, B.X) && FMath::IsNearlyEqual(C.X, D.X) &&
		FMath::IsNearlyEqual(A.X, C.X))
	{
		return FMath::Min(FMath::Max(A.Y, B.Y), FMath::Max(C.Y, D.Y)) -
			FMath::Max(FMath::Min(A.Y, B.Y), FMath::Min(C.Y, D.Y)) > KINDA_SMALL_NUMBER;
	}
	if (FMath::IsNearlyEqual(A.Y, B.Y) && FMath::IsNearlyEqual(C.Y, D.Y) &&
		FMath::IsNearlyEqual(A.Y, C.Y))
	{
		return FMath::Min(FMath::Max(A.X, B.X), FMath::Max(C.X, D.X)) -
			FMath::Max(FMath::Min(A.X, B.X), FMath::Min(C.X, D.X)) > KINDA_SMALL_NUMBER;
	}
	return false;
}

bool SegmentIntersectsBoxInterior(
	const FVector2D& A,
	const FVector2D& B,
	const FBox2D& Bounds)
{
	if (FMath::IsNearlyEqual(A.X, B.X))
	{
		return A.X > Bounds.Min.X && A.X < Bounds.Max.X &&
			FMath::Min(A.Y, B.Y) < Bounds.Max.Y &&
			FMath::Max(A.Y, B.Y) > Bounds.Min.Y;
	}
	if (FMath::IsNearlyEqual(A.Y, B.Y))
	{
		return A.Y > Bounds.Min.Y && A.Y < Bounds.Max.Y &&
			FMath::Min(A.X, B.X) < Bounds.Max.X &&
			FMath::Max(A.X, B.X) > Bounds.Min.X;
	}
	return true;
}

void AddNode(
	FBlueprintLensLC7Layout& Layout,
	const FBlueprintLensLC7TextMetrics& Metrics,
	const FString& UnitId,
	const FBox2D& Bounds)
{
	FBlueprintLensLC7NodeLayout Node;
	Node.UnitId = UnitId;
	Node.Bounds = Bounds;
	Node.HitBounds = Bounds;
	const FVector2D Measured = Metrics.UnitLabelSizes.FindChecked(UnitId);
	Node.LabelBounds = Box(
		Bounds.Min.X + 9.0f, Bounds.Min.Y + 7.0f,
		Measured.X, Measured.Y);
	Layout.Nodes.Add(MoveTemp(Node));
}

void AddClippedNode(
	FBlueprintLensLC7Layout& Layout,
	const FBlueprintLensLC7TextMetrics& Metrics,
	const FString& UnitId,
	const FBox2D& Bounds)
{
	FBlueprintLensLC7NodeLayout Node;
	Node.UnitId = UnitId;
	Node.Bounds = Bounds;
	Node.HitBounds = Bounds;
	const FVector2D Measured = Metrics.UnitLabelSizes.FindChecked(UnitId);
	Node.LabelBounds = Box(
		Bounds.Min.X + 9.0f,
		Bounds.Min.Y + 7.0f,
		FMath::Min(Measured.X, Bounds.GetSize().X - 18.0f),
		FMath::Min(Measured.Y, Bounds.GetSize().Y - 14.0f));
	Layout.Nodes.Add(MoveTemp(Node));
}

void AddRoute(
	FBlueprintLensLC7Layout& Layout,
	const FBlueprintLensLC7Relation& Relation,
	std::initializer_list<FVector2D> Points)
{
	FBlueprintLensLC7RouteLayout Route;
	Route.RelationId = Relation.RelationId;
	Route.Family = Relation.Family;
	Route.SourceUnitId = Relation.SourceUnitId;
	Route.TargetUnitId = Relation.TargetUnitId;
	for (const FVector2D& Point : Points)
	{
		Route.Points.Add(Point);
	}
	Layout.Routes.Add(MoveTemp(Route));
}

bool BuildLiveA3NodesAndRoutes(
	FBlueprintLensLC7Layout& Layout,
	const FBlueprintLensLC7Projection& Projection,
	const FBlueprintLensLC7TextMetrics& Metrics,
	const FBlueprintLensLC7SCCRecord& SCC)
{
	constexpr float NodeHorizontalPadding = 18.0f;
	const float MaximumNodeWidth = Layout.OverviewBounds.GetSize().X - 16.0f;
	if (SCC.OrderedSpineUnitIds.Num() < 2 ||
		SCC.OrderedSpineUnitIds.Num() > 6)
	{
		return false;
	}
	const TSet<FString> Members(SCC.OrderedSpineUnitIds);
	const float OriginY = Layout.OverviewBounds.Min.Y;
	const float MemberX = Layout.OverviewBounds.Min.X + 152.0f;
	const float LeftX = Layout.OverviewBounds.Min.X + 8.0f;
	const float RightX = Layout.OverviewBounds.Max.X - 108.0f;
	const float MemberTop = OriginY + 58.0f;
	constexpr float MemberCadence = 70.0f;
	for (int32 Index = 0; Index < SCC.OrderedSpineUnitIds.Num(); ++Index)
	{
		const FString& UnitId = SCC.OrderedSpineUnitIds[Index];
		const float NodeWidth =
			Metrics.UnitLabelSizes.FindChecked(UnitId).X + NodeHorizontalPadding;
		if (NodeWidth > MaximumNodeWidth)
		{
			Layout.DiagnosticCode = TEXT("LC7_LAYOUT_LIVE_LABEL_WIDTH_UNAVAILABLE");
			return false;
		}
		const float NodeX = FMath::Clamp(
			MemberX,
			Layout.OverviewBounds.Min.X + 8.0f,
			Layout.OverviewBounds.Max.X - 8.0f - NodeWidth);
		AddClippedNode(
			Layout,
			Metrics,
			UnitId,
			Box(
				NodeX,
				MemberTop + MemberCadence * Index,
				NodeWidth,
				42.0f));
	}

	const auto BoundsFor = [&Layout](const FString& UnitId) -> const FBox2D*
	{
		const FBlueprintLensLC7NodeLayout* Node =
			Layout.Nodes.FindByPredicate(
				[&UnitId](const FBlueprintLensLC7NodeLayout& Candidate)
				{
					return Candidate.UnitId == UnitId;
				});
		return Node == nullptr ? nullptr : &Node->Bounds;
	};
	const auto IntersectsExisting = [&Layout](const FBox2D& Candidate)
	{
		return Layout.Nodes.ContainsByPredicate(
			[&Candidate](const FBlueprintLensLC7NodeLayout& Existing)
			{
				return StrictlyIntersects(Candidate, Existing.Bounds);
			});
	};
	const auto AddOutsideNode =
		[&Layout, &Metrics, &IntersectsExisting](
			const FString& UnitId,
			const float X,
			float PreferredY) -> bool
		{
			if (Layout.Nodes.ContainsByPredicate(
				[&UnitId](const FBlueprintLensLC7NodeLayout& Existing)
				{
					return Existing.UnitId == UnitId;
				}))
			{
				return true;
			}
			constexpr float HorizontalPadding = 18.0f;
			const float NodeWidth =
				Metrics.UnitLabelSizes.FindChecked(UnitId).X + HorizontalPadding;
			const float MinimumX = Layout.OverviewBounds.Min.X + 8.0f;
			const float MaximumX = Layout.OverviewBounds.Max.X - 8.0f - NodeWidth;
			if (MaximumX < MinimumX)
			{
				Layout.DiagnosticCode = TEXT("LC7_LAYOUT_LIVE_LABEL_WIDTH_UNAVAILABLE");
				return false;
			}
			const float CandidateX = FMath::Clamp(X, MinimumX, MaximumX);
			PreferredY = FMath::Clamp(
				PreferredY,
				Layout.OverviewBounds.Min.Y + 8.0f,
				Layout.OverviewBounds.Max.Y - 104.0f);
			FBox2D Candidate = Box(CandidateX, PreferredY, NodeWidth, 42.0f);
			for (int32 Attempt = 0; Attempt < 10 &&
				IntersectsExisting(Candidate); ++Attempt)
			{
				const float Direction = Attempt % 2 == 0 ? -1.0f : 1.0f;
				const float Distance = 52.0f * (Attempt / 2 + 1);
				Candidate = Box(
					CandidateX,
					FMath::Clamp(
						PreferredY + Direction * Distance,
						Layout.OverviewBounds.Min.Y + 8.0f,
						Layout.OverviewBounds.Max.Y - 104.0f),
					NodeWidth,
					42.0f);
			}
			AddClippedNode(Layout, Metrics, UnitId, Candidate);
			return true;
		};

	TMap<FString, int32> IncomingOrdinal;
	TMap<FString, int32> IncomingTotal;
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		if (Relation.Family == EBlueprintLensLC7RelationFamily::Entry &&
			!Members.Contains(Relation.SourceUnitId) &&
			Members.Contains(Relation.TargetUnitId))
		{
			++IncomingTotal.FindOrAdd(Relation.TargetUnitId);
		}
	}
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		if (Relation.Family == EBlueprintLensLC7RelationFamily::Entry &&
			!Members.Contains(Relation.SourceUnitId) &&
			Members.Contains(Relation.TargetUnitId))
		{
			const FBox2D* Target = BoundsFor(Relation.TargetUnitId);
			const int32 Ordinal = IncomingOrdinal.FindOrAdd(Relation.TargetUnitId)++;
			const int32 Total = IncomingTotal.FindChecked(Relation.TargetUnitId);
			if (!AddOutsideNode(
				Relation.SourceUnitId,
				LeftX,
				Target->GetCenter().Y - 21.0f +
					(Ordinal - (Total - 1) * 0.5f) * 52.0f))
			{
				return false;
			}
		}
		else if (Relation.Family ==
				EBlueprintLensLC7RelationFamily::Predicate &&
			!Members.Contains(Relation.SourceUnitId) &&
			Members.Contains(Relation.TargetUnitId))
		{
			const FBox2D* Target = BoundsFor(Relation.TargetUnitId);
			if (!AddOutsideNode(
				Relation.SourceUnitId, RightX, Target->GetCenter().Y - 21.0f))
			{
				return false;
			}
		}
		else if (Relation.Family == EBlueprintLensLC7RelationFamily::Exit &&
			Members.Contains(Relation.SourceUnitId) &&
			!Members.Contains(Relation.TargetUnitId))
		{
			const FBox2D* Source = BoundsFor(Relation.SourceUnitId);
			if (!AddOutsideNode(
				Relation.TargetUnitId, RightX, Source->GetCenter().Y - 21.0f))
			{
				return false;
			}
		}
	}
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		if (Relation.Family != EBlueprintLensLC7RelationFamily::Value)
		{
			continue;
		}
		const FBox2D* Target = BoundsFor(Relation.TargetUnitId);
		if (!AddOutsideNode(
			Relation.SourceUnitId,
			RightX,
			Target != nullptr
				? Target->Min.Y - 58.0f
				: OriginY + 8.0f))
		{
			return false;
		}
	}
	int32 RemainingIndex = 0;
	TArray<FString> SortedUnits = Projection.AllUnitIds.Array();
	SortedUnits.Sort();
	for (const FString& UnitId : SortedUnits)
	{
		if (BoundsFor(UnitId) == nullptr)
		{
			if (!AddOutsideNode(
				UnitId,
				RemainingIndex % 2 == 0 ? LeftX : RightX,
				OriginY + 8.0f + 52.0f * (RemainingIndex / 2)))
			{
				return false;
			}
			++RemainingIndex;
		}
	}
	if (Layout.Nodes.Num() != Projection.AllUnitIds.Num())
	{
		return false;
	}

	// A unit may sit between two relations, so its target attachment for the
	// incoming relation and source attachment for the outgoing relation share
	// one geometric namespace.  Separate source/target ordinals can both choose
	// offset zero and place two distinct relation endpoints at the same point.
	TMap<FString, int32> AttachmentPorts;
	TArray<const FBlueprintLensLC7Relation*> Relations;
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		Relations.Add(&Relation);
	}
	Relations.Sort([](
		const FBlueprintLensLC7Relation& Left,
		const FBlueprintLensLC7Relation& Right)
	{
		return Left.RelationId < Right.RelationId;
	});
	for (const FBlueprintLensLC7Relation* Relation : Relations)
	{
		const FBox2D* Source = BoundsFor(Relation->SourceUnitId);
		const FBox2D* Target = BoundsFor(Relation->TargetUnitId);
		if (Source == nullptr || Target == nullptr)
		{
			return false;
		}
		const float SourceOffset = 8.0f +
			5.0f * AttachmentPorts.FindOrAdd(Relation->SourceUnitId)++;
		const float TargetOffset = 8.0f +
			5.0f * AttachmentPorts.FindOrAdd(Relation->TargetUnitId)++;
		if (Relation->Family == EBlueprintLensLC7RelationFamily::Forward)
		{
			const FVector2D Start(
				Source->Min.X + SourceOffset, Source->Max.Y);
			const FVector2D End(
				Target->Min.X + TargetOffset, Target->Min.Y);
			const float CorridorY = (Start.Y + End.Y) * 0.5f;
			AddRoute(
				Layout,
				*Relation,
				{Start, FVector2D(Start.X, CorridorY),
				 FVector2D(End.X, CorridorY), End});
		}
		else if (Relation->Family == EBlueprintLensLC7RelationFamily::Return)
		{
			float CorridorX = MemberX - 24.0f - SourceOffset;
			const FVector2D CorridorStart(
				CorridorX, Source->Min.Y + SourceOffset);
			const FVector2D CorridorEnd(
				CorridorX, Target->Min.Y + TargetOffset);
			for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
			{
				if (Node.UnitId != Relation->SourceUnitId &&
					Node.UnitId != Relation->TargetUnitId &&
					SegmentIntersectsBoxInterior(
						CorridorStart, CorridorEnd, Node.Bounds))
				{
					CorridorX = FMath::Max(CorridorX, Node.Bounds.Max.X + 4.0f);
				}
			}
			AddRoute(
				Layout,
				*Relation,
				{FVector2D(Source->Min.X, Source->Min.Y + SourceOffset),
				 FVector2D(CorridorX, Source->Min.Y + SourceOffset),
				 FVector2D(CorridorX, Target->Min.Y + TargetOffset),
				 FVector2D(Target->Min.X, Target->Min.Y + TargetOffset)});
		}
		else if (Relation->Family == EBlueprintLensLC7RelationFamily::Value &&
			FMath::IsNearlyEqual(Source->GetCenter().X, Target->GetCenter().X))
		{
			const FVector2D Start(
				Source->Min.X + SourceOffset, Source->Max.Y);
			const FVector2D End(
				Target->Min.X + TargetOffset, Target->Min.Y);
			const float CorridorY = (Start.Y + End.Y) * 0.5f;
			AddRoute(
				Layout,
				*Relation,
				{Start, FVector2D(Start.X, CorridorY),
				 FVector2D(End.X, CorridorY), End});
		}
		else
		{
			const bool bLeftToRight = Source->GetCenter().X < Target->GetCenter().X;
			const FVector2D Start(
				bLeftToRight ? Source->Max.X : Source->Min.X,
				Source->Min.Y + SourceOffset);
			const FVector2D End(
				bLeftToRight ? Target->Min.X : Target->Max.X,
				Target->Min.Y + TargetOffset);
			float CorridorX = (Start.X + End.X) * 0.5f;
			const FVector2D CorridorStart(CorridorX, Start.Y);
			const FVector2D CorridorEnd(CorridorX, End.Y);
			if (Layout.Nodes.ContainsByPredicate(
				[&Relation, &CorridorStart, &CorridorEnd](
					const FBlueprintLensLC7NodeLayout& Node)
				{
					return Node.UnitId != Relation->SourceUnitId &&
						Node.UnitId != Relation->TargetUnitId &&
						SegmentIntersectsBoxInterior(
							CorridorStart, CorridorEnd, Node.Bounds);
				}))
			{
				CorridorX = bLeftToRight
					? Target->Min.X - 12.0f
					: Target->Max.X + 12.0f;
			}
			AddRoute(
				Layout,
				*Relation,
				{Start, FVector2D(CorridorX, Start.Y),
				 FVector2D(CorridorX, End.Y), End});
		}
	}
	return Layout.Routes.Num() == Projection.Relations.Num();
}

bool BuildA3Geometry(
	FBlueprintLensLC7Layout& Layout,
	const FBlueprintLensLC7Projection& Projection,
	const FBlueprintLensLC7TextMetrics& Metrics,
	const FBlueprintLensLC7SCCRecord& SCC)
{
	if (Projection.bLiveExplanation)
	{
		if (!BuildLiveA3NodesAndRoutes(
			Layout, Projection, Metrics, SCC))
		{
			return false;
		}
	}
	else
	{
	const FString Branch = SCC.EntryUnitId;
	const FBlueprintLensLC7Relation* InitialiseToBranch = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Entry, FString(), Branch);
	const FString Initialise = InitialiseToBranch == nullptr
		? FString() : InitialiseToBranch->SourceUnitId;
	const FBlueprintLensLC7Relation* EventToInitialise = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Entry, FString(), Initialise);
	const FBlueprintLensLC7Relation* CompareToBranch = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Predicate, FString(), Branch);
	const FString Compare = CompareToBranch == nullptr
		? FString() : CompareToBranch->SourceUnitId;
	const FBlueprintLensLC7Relation* GetToCompare = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Value, FString(), Compare);
	const FBlueprintLensLC7Relation* BranchToVisited = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Forward, Branch, FString());
	const FString Visited = BranchToVisited == nullptr
		? FString() : BranchToVisited->TargetUnitId;
	const FBlueprintLensLC7Relation* VisitedToAdvance = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Forward, Visited, FString());
	const FString Advance = VisitedToAdvance == nullptr
		? FString() : VisitedToAdvance->TargetUnitId;
	const FBlueprintLensLC7Relation* AdvanceToBranch = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Return, Advance, Branch);
	const FBlueprintLensLC7Relation* BranchToCriterion = FindRelation(
		Projection, EBlueprintLensLC7RelationFamily::Exit,
		Branch, Projection.CriterionUnitId);
	if (InitialiseToBranch == nullptr || EventToInitialise == nullptr ||
		CompareToBranch == nullptr || GetToCompare == nullptr ||
		BranchToVisited == nullptr || VisitedToAdvance == nullptr ||
		AdvanceToBranch == nullptr || BranchToCriterion == nullptr)
	{
		return false;
	}
	const FString Event = EventToInitialise->SourceUnitId;
	const FString Get = GetToCompare->SourceUnitId;
	TSet<FString> ExpectedUnits = {
		Event, Initialise, Get, Compare, Branch, Visited,
		Advance, Projection.CriterionUnitId};
	if (!SameSet(ExpectedUnits, Projection.AllUnitIds))
	{
		return false;
	}

	const float Left = Layout.OverviewBounds.Min.X + 16.0f;
	const float Centre = Layout.OverviewBounds.Min.X +
		FMath::FloorToFloat(Layout.OverviewBounds.GetSize().X * 0.5f) - 55.0f;
	const float Right = Layout.OverviewBounds.Max.X - 124.0f;
	const float OriginY = Layout.OverviewBounds.Min.Y;
	AddNode(Layout, Metrics, Event, Box(Left, OriginY + 110.0f, 100.0f, 42.0f));
	AddNode(Layout, Metrics, Initialise,
		Box(Left + 112.0f, OriginY + 110.0f, 104.0f, 42.0f));
	AddNode(Layout, Metrics, Get, Box(Right, OriginY + 78.0f, 108.0f, 42.0f));
	AddNode(Layout, Metrics, Compare, Box(Right, OriginY + 142.0f, 108.0f, 42.0f));
	AddNode(Layout, Metrics, Branch, Box(Centre, OriginY + 218.0f, 110.0f, 46.0f));
	AddNode(Layout, Metrics, Visited, Box(Centre, OriginY + 310.0f, 110.0f, 46.0f));
	AddNode(Layout, Metrics, Advance, Box(Centre, OriginY + 402.0f, 110.0f, 46.0f));
	AddNode(Layout, Metrics, Projection.CriterionUnitId,
		Box(Right, OriginY + 218.0f, 116.0f, 46.0f));

	const auto NodeBounds = [&Layout](const FString& UnitId) -> const FBox2D&
	{
		return Layout.Nodes.FindByPredicate([&UnitId](const auto& Node)
		{
			return Node.UnitId == UnitId;
		})->Bounds;
	};
	const FBox2D& EventBox = NodeBounds(Event);
	const FBox2D& InitialiseBox = NodeBounds(Initialise);
	const FBox2D& GetBox = NodeBounds(Get);
	const FBox2D& CompareBox = NodeBounds(Compare);
	const FBox2D& BranchBox = NodeBounds(Branch);
	const FBox2D& VisitedBox = NodeBounds(Visited);
	const FBox2D& AdvanceBox = NodeBounds(Advance);
	const FBox2D& CriterionBox = NodeBounds(Projection.CriterionUnitId);
	AddRoute(Layout, *EventToInitialise,
		{FVector2D(EventBox.Max.X, EventBox.GetCenter().Y),
		 FVector2D(InitialiseBox.Min.X, InitialiseBox.GetCenter().Y)});
	AddRoute(Layout, *InitialiseToBranch,
		{FVector2D(InitialiseBox.GetCenter().X, InitialiseBox.Max.Y),
		 FVector2D(InitialiseBox.GetCenter().X, OriginY + 188.0f),
		 FVector2D(BranchBox.Min.X + 30.0f, OriginY + 188.0f),
		 FVector2D(BranchBox.Min.X + 30.0f, BranchBox.Min.Y)});
	AddRoute(Layout, *GetToCompare,
		{FVector2D(GetBox.GetCenter().X, GetBox.Max.Y),
		 FVector2D(CompareBox.GetCenter().X, CompareBox.Min.Y)});
	AddRoute(Layout, *CompareToBranch,
		{FVector2D(CompareBox.GetCenter().X, CompareBox.Max.Y),
		 FVector2D(CompareBox.GetCenter().X, OriginY + 202.0f),
		 FVector2D(BranchBox.Max.X - 20.0f, OriginY + 202.0f),
		 FVector2D(BranchBox.Max.X - 20.0f, BranchBox.Min.Y)});
	AddRoute(Layout, *BranchToVisited,
		{FVector2D(BranchBox.GetCenter().X, BranchBox.Max.Y),
		 FVector2D(VisitedBox.GetCenter().X, VisitedBox.Min.Y)});
	AddRoute(Layout, *VisitedToAdvance,
		{FVector2D(VisitedBox.GetCenter().X, VisitedBox.Max.Y),
		 FVector2D(AdvanceBox.GetCenter().X, AdvanceBox.Min.Y)});
	AddRoute(Layout, *AdvanceToBranch,
		{FVector2D(AdvanceBox.Min.X, AdvanceBox.GetCenter().Y),
		 FVector2D(BranchBox.Min.X - 32.0f, AdvanceBox.GetCenter().Y),
		 FVector2D(BranchBox.Min.X - 32.0f, BranchBox.GetCenter().Y),
		 FVector2D(BranchBox.Min.X, BranchBox.GetCenter().Y)});
	AddRoute(Layout, *BranchToCriterion,
		{FVector2D(BranchBox.Max.X, BranchBox.GetCenter().Y),
		 FVector2D(CriterionBox.Min.X, CriterionBox.GetCenter().Y)});
	}

	Layout.LayoutRequest.GraphKey = Projection.IntegrityHash + TEXT(".A3");
	Layout.LayoutRequest.Profile = EBlueprintLensLayoutProfile::Cyclic;
	Layout.LayoutRequest.TargetWidth = Layout.CanvasSize.X;
	for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
	{
		FBlueprintLensLayoutNodeRequest Request;
		Request.UnitId = Node.UnitId;
		Request.DesiredSize = Node.Bounds.GetSize();
		Layout.LayoutRequest.Nodes.Add(MoveTemp(Request));
	}
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		const FBlueprintLensLC7RouteLayout* Route =
			Layout.Routes.FindByPredicate([&Relation](const auto& Candidate)
			{
				return Candidate.RelationId == Relation.RelationId;
			});
		if (Route == nullptr)
		{
			return false;
		}
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.RelationId;
		Edge.SourceUnitId = Relation.SourceUnitId;
		Edge.TargetUnitId = Relation.TargetUnitId;
		Edge.SourcePortLabel = Relation.RelationId + TEXT(":out");
		Edge.TargetPortLabel = Relation.RelationId + TEXT(":in");
		Edge.Family = ToLayoutFamily(Relation.Family);
		Edge.bParticipatesInRank = Relation.Family !=
			EBlueprintLensLC7RelationFamily::Return;
		Layout.LayoutRequest.Edges.Add(Edge);
		FBlueprintLensLayoutNodeRequest* Source =
			Layout.LayoutRequest.Nodes.FindByPredicate([&Relation](const auto& Node)
			{
				return Node.UnitId == Relation.SourceUnitId;
			});
		FBlueprintLensLayoutNodeRequest* Target =
			Layout.LayoutRequest.Nodes.FindByPredicate([&Relation](const auto& Node)
			{
				return Node.UnitId == Relation.TargetUnitId;
			});
		if (Source == nullptr || Target == nullptr)
		{
			return false;
		}
		FBlueprintLensLayoutPortRequest Output;
		Output.Label = Edge.SourcePortLabel;
		Output.Order = Source->Ports.Num();
		Source->Ports.Add(Output);
		FBlueprintLensLayoutPortRequest Input;
		Input.Label = Edge.TargetPortLabel;
		Input.bInput = true;
		Input.Order = Target->Ports.Num();
		Target->Ports.Add(Input);
	}
	FBlueprintLensLayoutGroupRequest Group;
	Group.GroupId = SCC.GroupId;
	Group.MemberUnitIds = SCC.OrderedSpineUnitIds;
	Layout.LayoutRequest.Groups.Add(MoveTemp(Group));
	if (!Layout.LayoutRequest.IsValid())
	{
		return false;
	}

	FBlueprintLensLayoutLedger Oracle;
	Oracle.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Oracle.BackendVersion = TEXT("BlueprintLens.LC7A3Oracle.v1");
	Oracle.ConfigurationFingerprint = FString::Printf(
		TEXT("lc7-a3;responsive=%d;width=%.0f"),
		static_cast<int32>(Layout.ResponsiveMode), Layout.CanvasSize.X);
	Oracle.CanvasSize = Layout.CanvasSize;
	Oracle.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
	{
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = Node.UnitId;
		Placement.Position = Node.Bounds.Min;
		Placement.Size = Node.Bounds.GetSize();
		Oracle.Nodes.Add(MoveTemp(Placement));
	}
	for (const FBlueprintLensLayoutEdgeRequest& Requested : Layout.LayoutRequest.Edges)
	{
		const FBlueprintLensLC7RouteLayout* Route =
			Layout.Routes.FindByPredicate([&Requested](const auto& Candidate)
			{
				return Candidate.RelationId == Requested.RelationId;
			});
		FBlueprintLensLayoutPortPlacement Output;
		Output.UnitId = Requested.SourceUnitId;
		Output.Label = Requested.SourcePortLabel;
		Output.Position = Route->Points[0];
		Oracle.Ports.Add(MoveTemp(Output));
		FBlueprintLensLayoutPortPlacement Input;
		Input.UnitId = Requested.TargetUnitId;
		Input.Label = Requested.TargetPortLabel;
		Input.bInput = true;
		Input.Position = Route->Points.Last();
		Oracle.Ports.Add(MoveTemp(Input));
		FBlueprintLensLayoutEdgePlacement Edge;
		Edge.RelationId = Requested.RelationId;
		Edge.SourceUnitId = Requested.SourceUnitId;
		Edge.TargetUnitId = Requested.TargetUnitId;
		Edge.SourcePortLabel = Requested.SourcePortLabel;
		Edge.TargetPortLabel = Requested.TargetPortLabel;
		Edge.Family = Requested.Family;
		for (int32 PointIndex = 1;
			PointIndex + 1 < Route->Points.Num(); ++PointIndex)
		{
			Edge.BendPoints.Add(Route->Points[PointIndex]);
		}
		Oracle.Edges.Add(MoveTemp(Edge));
	}
	if (!Oracle.IsCompleteFor(Layout.LayoutRequest))
	{
		return false;
	}
	Layout.VisualOracleLedger = Oracle;
	Layout.LayoutLedger = Oracle;

	const float ActionWidth = FMath::FloorToFloat(
		(Layout.OverviewBounds.GetSize().X - 46.0f) / 3.0f);
	const float ActionY = Layout.OverviewBounds.Max.Y - 42.0f;
	const float FirstActionX = Layout.OverviewBounds.Min.X + 16.0f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FBlueprintLensLC7ActionLayout Action;
		Action.ActionId = Index == 0 ? TEXT("inspect_cycle") :
			Index == 1 ? TEXT("show_complete_text") : TEXT("open_source");
		Action.HitBounds = Box(
			FirstActionX + Index * (ActionWidth + 7.0f),
			ActionY, ActionWidth, 30.0f);
		Layout.Actions.Add(MoveTemp(Action));
	}
	TArray<FString> GeometryParts;
	for (const FBlueprintLensLC7NodeLayout& Node : Layout.Nodes)
	{
		GeometryParts.Add(FString::Printf(TEXT("%s:%.0f,%.0f,%.0f,%.0f"),
			*Node.UnitId, Node.Bounds.Min.X, Node.Bounds.Min.Y,
			Node.Bounds.Max.X, Node.Bounds.Max.Y));
	}
	for (const FBlueprintLensLC7RouteLayout& Route : Layout.Routes)
	{
		GeometryParts.Add(Route.RelationId + TEXT(":") +
			FString::FromInt(Route.Points.Num()));
	}
	Layout.OverviewGeometryHash = FString::Join(GeometryParts, TEXT(";"));
	return true;
}
} // namespace

FBlueprintLensLC7TextMetrics FBlueprintLensLC7TextMetrics::MeasuredForProjection(
	const FBlueprintLensLC7Projection& Projection)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC7 layout requires Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FBlueprintLensLC7TextMetrics Result;
	for (const TPair<FString, FString>& Pair : Projection.UnitTitles)
	{
		const FSlateFontInfo Font = FAppStyle::Get().GetFontStyle(
			Pair.Key == Projection.CriterionUnitId
				? "NormalFontBold" : "SmallFont");
		Result.UnitLabelSizes.Add(
			Pair.Key,
			FontMeasure->Measure(Pair.Value, Font));
	}
	return Result;
}

bool FBlueprintLensLC7TextMetrics::HasMeasurementsFor(
	const TSet<FString>& UnitIds) const
{
	for (const FString& UnitId : UnitIds)
	{
		const FVector2D* Size = UnitLabelSizes.Find(UnitId);
		if (Size == nullptr || Size->X <= 0.0f || Size->Y <= 0.0f)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC7TextMetrics::Fits(const TSet<FString>& UnitIds) const
{
	if (!HasMeasurementsFor(UnitIds) ||
		AvailableOverviewHeight < 594.0f ||
		AvailableRouteClearance < RequiredRouteClearance)
	{
		return false;
	}
	for (const FString& UnitId : UnitIds)
	{
		const FVector2D& Size = UnitLabelSizes.FindChecked(UnitId);
		if (Size.X > MaxNodeLabelWidth || Size.Y > MaxNodeLabelHeight)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC7Layout::HasValidRecoverability(
	const FBlueprintLensLC7Projection& Projection) const
{
	if (ScaleMode == EBlueprintLensLC7ScaleMode::CompleteText ||
		FocusedSCCId.IsEmpty() || SelectedUnitId.Len() > 0 ||
		!Projection.SCCs.ContainsByPredicate([this](const auto& SCC)
		{
			return SCC.GroupId == FocusedSCCId &&
				SCC.CriterionUnitId == CriterionUnitId;
		}) ||
		!Projection.AllUnitIds.Includes(VisibleUnitIds) ||
		!Projection.AllRelationIds.Includes(VisibleRelationIds))
	{
		return false;
	}
	TSet<FString> OwnedUnits = VisibleUnitIds;
	TSet<FString> OwnedRelations = VisibleRelationIds;
	const auto AddOwned = [](TSet<FString>& Owned, const TSet<FString>& Added)
	{
		if (!Owned.Intersect(Added).IsEmpty())
		{
			return false;
		}
		Owned.Append(Added);
		return true;
	};
	for (const FBlueprintLensLC7Fold& Fold : Folds)
	{
		if (Fold.FoldId.IsEmpty() || Fold.OwnerSCCId != FocusedSCCId ||
			Fold.UnitCount < 0 || Fold.RelationCount < 0 ||
			Fold.UnitCount != Fold.UnitIds.Num() ||
			Fold.RelationCount != Fold.RelationIds.Num() ||
			Fold.ExpansionActionId.IsEmpty() ||
			!Projection.AllUnitIds.Includes(Fold.UnitIds) ||
			!Projection.AllRelationIds.Includes(Fold.RelationIds) ||
			!AddOwned(OwnedUnits, Fold.UnitIds) ||
			!AddOwned(OwnedRelations, Fold.RelationIds))
		{
			return false;
		}
	}
	for (const FBlueprintLensLC7IndexRow& Row : IndexRows)
	{
		if (Row.SCCId.IsEmpty() || Row.SCCId == FocusedSCCId ||
			Row.UnitCount < 0 || Row.RelationCount < 0 ||
			Row.UnitCount != Row.UnitIds.Num() ||
			Row.RelationCount != Row.RelationIds.Num() ||
			Row.ExpansionActionId.IsEmpty() ||
			!Row.UnitIds.Contains(Row.SourceAnchorUnitId) ||
			!Projection.SourceAnchors.Contains(Row.SourceAnchorUnitId) ||
			!Projection.SCCs.ContainsByPredicate([&Row](const auto& SCC)
			{
				return SCC.GroupId == Row.SCCId;
			}) ||
			!Projection.AllUnitIds.Includes(Row.UnitIds) ||
			!Projection.AllRelationIds.Includes(Row.RelationIds) ||
			!AddOwned(OwnedUnits, Row.UnitIds) ||
			!AddOwned(OwnedRelations, Row.RelationIds))
		{
			return false;
		}
	}
	if (!SameSet(OwnedUnits, Projection.AllUnitIds) ||
		!SameSet(OwnedRelations, Projection.AllRelationIds))
	{
		return false;
	}
	if (ScaleMode == EBlueprintLensLC7ScaleMode::Full)
	{
		return Folds.IsEmpty() && IndexRows.IsEmpty() &&
			VisibleSCCCount == Projection.SCCs.Num();
	}
	const FBlueprintLensLC7SCCRecord* Focused = Projection.SCCs.FindByPredicate(
		[this](const auto& SCC) { return SCC.GroupId == FocusedSCCId; });
	TSet<FString> ExactFocus(Focused->OrderedSpineUnitIds);
	ExactFocus.Add(Projection.CriterionUnitId);
	if (!SameSet(VisibleUnitIds, ExactFocus) || VisibleSCCCount != 1)
	{
		return false;
	}
	return ScaleMode == EBlueprintLensLC7ScaleMode::Focus
		? !Folds.IsEmpty() && IndexRows.IsEmpty()
		: ScaleMode == EBlueprintLensLC7ScaleMode::Index &&
			Folds.IsEmpty() && IndexRows.Num() == Projection.SCCs.Num() - 1;
}

bool FBlueprintLensLC7Layout::CoversProjection(
	const FBlueprintLensLC7Projection& Projection) const
{
	return HasValidRecoverability(Projection) &&
		LayoutRequest.Nodes.Num() == VisibleUnitIds.Num() &&
		LayoutRequest.Edges.Num() == VisibleRelationIds.Num();
}

bool FBlueprintLensLC7Layout::HasValidSharedLedger() const
{
	return LayoutRequest.IsValid() &&
		LayoutLedger.IsCompleteFor(LayoutRequest) &&
		VisualOracleLedger.IsCompleteFor(LayoutRequest);
}

bool FBlueprintLensLC7Layout::HasNonOverlappingHitTargets() const
{
	TArray<FBox2D> Bounds;
	for (const FBlueprintLensLC7NodeLayout& Node : Nodes)
	{
		Bounds.Add(Node.HitBounds);
	}
	for (const FBlueprintLensLC7ActionLayout& Action : Actions)
	{
		Bounds.Add(Action.HitBounds);
	}
	for (int32 A = 0; A < Bounds.Num(); ++A)
	{
		for (int32 B = A + 1; B < Bounds.Num(); ++B)
		{
			if (StrictlyIntersects(Bounds[A], Bounds[B]))
			{
				return false;
			}
		}
	}
	return true;
}

bool FBlueprintLensLC7Layout::HasInBoundsMeasuredLabels() const
{
	for (const FBlueprintLensLC7NodeLayout& Node : Nodes)
	{
		if (!Contains(Node.Bounds, Node.LabelBounds) ||
			!Contains(OverviewBounds, Node.LabelBounds))
		{
			return false;
		}
	}
	return !Nodes.IsEmpty();
}

bool FBlueprintLensLC7Layout::HasDistinctRelationAttachments() const
{
	TSet<FString> Attachments;
	for (const FBlueprintLensLC7RouteLayout& Route : Routes)
	{
		if (Route.Points.Num() < 2)
		{
			return false;
		}
		for (const TPair<FString, FVector2D>& Attachment : {
			TPair<FString, FVector2D>(Route.SourceUnitId, Route.Points[0]),
			TPair<FString, FVector2D>(Route.TargetUnitId, Route.Points.Last())})
		{
			const FString Key = FString::Printf(TEXT("%s:%.2f:%.2f"),
				*Attachment.Key, Attachment.Value.X, Attachment.Value.Y);
			if (Attachments.Contains(Key))
			{
				return false;
			}
			Attachments.Add(Key);
		}
	}
	return Routes.Num() == VisibleRelationIds.Num();
}

bool FBlueprintLensLC7Layout::HasZeroCollinearRouteOverlap() const
{
	for (int32 A = 0; A < Routes.Num(); ++A)
	{
		for (int32 B = A + 1; B < Routes.Num(); ++B)
		{
			for (int32 ASegment = 0;
				ASegment + 1 < Routes[A].Points.Num(); ++ASegment)
			{
				for (int32 BSegment = 0;
					BSegment + 1 < Routes[B].Points.Num(); ++BSegment)
				{
					if (CollinearOverlap(
						Routes[A].Points[ASegment], Routes[A].Points[ASegment + 1],
						Routes[B].Points[BSegment], Routes[B].Points[BSegment + 1]))
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

bool FBlueprintLensLC7Layout::HasValidBendBudget() const
{
	for (const FBlueprintLensLC7RouteLayout& Route : Routes)
	{
		const int32 BendCount = FMath::Max(0, Route.Points.Num() - 2);
		if (BendCount > (Route.Family == EBlueprintLensLC7RelationFamily::Return
			? 2 : 3))
		{
			return false;
		}
	}
	return !Routes.IsEmpty();
}

bool FBlueprintLensLC7Layout::HasNoTextOrRouteCollisions() const
{
	if (!HasNonOverlappingHitTargets() || !HasInBoundsMeasuredLabels() ||
		!HasDistinctRelationAttachments() ||
		!HasZeroCollinearRouteOverlap() || !HasValidBendBudget())
	{
		return false;
	}
	for (const FBlueprintLensLC7RouteLayout& Route : Routes)
	{
		for (int32 Segment = 0; Segment + 1 < Route.Points.Num(); ++Segment)
		{
			for (const FBlueprintLensLC7NodeLayout& Node : Nodes)
			{
				if (Node.UnitId != Route.SourceUnitId &&
					Node.UnitId != Route.TargetUnitId &&
					SegmentIntersectsBoxInterior(
						Route.Points[Segment], Route.Points[Segment + 1],
						Node.Bounds))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FBlueprintLensLC7Layout::MatchesVisualOracle(const float Tolerance) const
{
	return SameLedger(LayoutLedger, VisualOracleLedger, Tolerance);
}

FBlueprintLensLC7Layout FBlueprintLensLC7LayoutBuilder::Build(
	const FBlueprintLensLC7Projection& Projection,
	const float TargetWidth,
	const FString& FocusedSCCId,
	const FBlueprintLensLC7TextMetrics& Metrics)
{
	FBlueprintLensLC7Layout Result;
	SetResponsiveGeometry(Result, TargetWidth);
	Result.CriterionUnitId = Projection.CriterionUnitId;
	if (!IsAccountableProjection(Projection))
	{
		Result.DiagnosticCode = TEXT("LC7_LAYOUT_PROJECTION_UNACCOUNTABLE");
		return Result;
	}
	const FBlueprintLensLC7SCCRecord* Focused =
		FindFocusedSCC(Projection, FocusedSCCId);
	if (Focused == nullptr)
	{
		Result.DiagnosticCode = TEXT("LC7_LAYOUT_FOCUS_UNAVAILABLE");
		return Result;
	}
	Result.FocusedSCCId = Focused->GroupId;

	const bool bFullFits =
		Projection.AllUnitIds.Num() <=
			(Projection.bLiveExplanation ? 10 : Metrics.MaxFullUnitCount) &&
		Projection.AllRelationIds.Num() <=
			(Projection.bLiveExplanation ? 10 : Metrics.MaxFullRelationCount) &&
		Projection.SCCs.Num() <= Metrics.MaxFullSCCCount &&
		(Projection.bLiveExplanation
			? Metrics.HasMeasurementsFor(Projection.AllUnitIds) &&
				Metrics.AvailableOverviewHeight >= 594.0f &&
				Metrics.AvailableRouteClearance >= Metrics.RequiredRouteClearance
			: Metrics.Fits(Projection.AllUnitIds));
	if (bFullFits)
	{
		Result.ScaleMode = EBlueprintLensLC7ScaleMode::Full;
		Result.VisibleUnitIds = Projection.AllUnitIds;
		Result.VisibleRelationIds = Projection.AllRelationIds;
		Result.VisibleSCCCount = Projection.SCCs.Num();
	}
	else
	{
		Result.VisibleUnitIds = TSet<FString>(Focused->OrderedSpineUnitIds);
		Result.VisibleUnitIds.Add(Projection.CriterionUnitId);
		if (!Metrics.Fits(Result.VisibleUnitIds))
		{
			Result.DiagnosticCode = TEXT("LC7_LAYOUT_MEASURED_FIT_FAILED");
			return Result;
		}
		Result.VisibleSCCCount = 1;
		for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
		{
			if (Relation.OwningSCCId == Focused->GroupId &&
				Result.VisibleUnitIds.Contains(Relation.SourceUnitId) &&
				Result.VisibleUnitIds.Contains(Relation.TargetUnitId))
			{
				Result.VisibleRelationIds.Add(Relation.RelationId);
			}
		}
		if (Projection.SCCs.Num() == 1)
		{
			Result.ScaleMode = EBlueprintLensLC7ScaleMode::Focus;
			FBlueprintLensLC7Fold Fold;
			Fold.FoldId = Focused->GroupId + TEXT(".off-focus");
			Fold.OwnerSCCId = Focused->GroupId;
			Fold.UnitIds = Projection.AllUnitIds.Difference(Result.VisibleUnitIds);
			Fold.RelationIds =
				Projection.AllRelationIds.Difference(Result.VisibleRelationIds);
			Fold.UnitCount = Fold.UnitIds.Num();
			Fold.RelationCount = Fold.RelationIds.Num();
			Fold.ExpansionActionId = TEXT("expand_fold:") + Fold.FoldId;
			if (Fold.UnitIds.IsEmpty() && Fold.RelationIds.IsEmpty())
			{
				Result.ScaleMode = EBlueprintLensLC7ScaleMode::CompleteText;
				Result.DiagnosticCode = TEXT("LC7_LAYOUT_FOCUS_EMPTY");
				return Result;
			}
			Result.Folds.Add(MoveTemp(Fold));
		}
		else
		{
			Result.ScaleMode = EBlueprintLensLC7ScaleMode::Index;
			for (const FBlueprintLensLC7SCCRecord& SCC : Projection.SCCs)
			{
				if (SCC.GroupId == Focused->GroupId)
				{
					continue;
				}
				FBlueprintLensLC7IndexRow Row;
				Row.SCCId = SCC.GroupId;
				Row.UnitIds = TSet<FString>(SCC.OrderedSpineUnitIds);
				for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
				{
					if (Relation.OwningSCCId == SCC.GroupId)
					{
						Row.RelationIds.Add(Relation.RelationId);
					}
				}
				Row.UnitCount = Row.UnitIds.Num();
				Row.RelationCount = Row.RelationIds.Num();
				Row.SourceAnchorUnitId = SCC.OrderedSpineUnitIds[0];
				Row.ExpansionActionId = TEXT("expand_scc:") + SCC.GroupId;
				Result.IndexRows.Add(MoveTemp(Row));
			}
		}
	}
	FinalizeRecoverabilityHash(Result);
	if (!Result.HasValidRecoverability(Projection))
	{
		Result.ScaleMode = EBlueprintLensLC7ScaleMode::CompleteText;
		Result.DiagnosticCode = TEXT("LC7_LAYOUT_RECOVERABILITY_FAILED");
		return Result;
	}
	if (Result.ScaleMode != EBlueprintLensLC7ScaleMode::Full)
	{
		Result.DiagnosticCode = TEXT("LC7_SCALE_MODE_SELECTED");
		return Result;
	}
	if (!BuildA3Geometry(Result, Projection, Metrics, *Focused) ||
		!Result.CoversProjection(Projection) ||
		!Result.HasValidSharedLedger() ||
		!Result.HasNoTextOrRouteCollisions() ||
		!Result.MatchesVisualOracle(1.0f))
	{
		Result.ScaleMode = EBlueprintLensLC7ScaleMode::CompleteText;
		if (Result.DiagnosticCode.IsEmpty())
		{
			Result.DiagnosticCode = TEXT("LC7_LAYOUT_A3_ORACLE_FAILED");
		}
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC7_A3_LAYOUT_COMPLETE");
	return Result;
}
