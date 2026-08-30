#include "BlueprintLensLC2GuardLayout.h"

namespace
{
constexpr float Margin = 16.0f;

bool StrictlyIntersects(
	const FBlueprintLensLC2GuardLayoutNode& A,
	const FBlueprintLensLC2GuardLayoutNode& B)
{
	return A.Position.X < B.Position.X + B.Size.X &&
		A.Position.X + A.Size.X > B.Position.X &&
		A.Position.Y < B.Position.Y + B.Size.Y &&
		A.Position.Y + A.Size.Y > B.Position.Y;
}

EBlueprintLensLayoutRelationFamily RelationFamily(
	const FBlueprintLensRelation& Relation)
{
	return Relation.Kind == EBlueprintLensRelationKind::PredicateFor
		? EBlueprintLensLayoutRelationFamily::Predicate
		: EBlueprintLensLayoutRelationFamily::Execution;
}

FVector2D NodeSize(const FBlueprintLensLC2GuardCanonicalUnit& Unit)
{
	if (Unit.bIsCriterion)
	{
		return FVector2D(126.0f, 74.0f);
	}
	if (Unit.bIsBranch)
	{
		return FVector2D(104.0f, 58.0f);
	}
	if (Unit.bIsPredicate)
	{
		return FVector2D(104.0f, 42.0f);
	}
	return FVector2D(108.0f, 52.0f);
}

void AddPort(
	FBlueprintLensLayoutNodeRequest& Node,
	const FString& Label,
	const bool bInput)
{
	FBlueprintLensLayoutPortRequest Port;
	Port.Label = Label;
	Port.bInput = bInput;
	for (const FBlueprintLensLayoutPortRequest& Existing : Node.Ports)
	{
		Port.Order += Existing.bInput == bInput ? 1 : 0;
	}
	Node.Ports.Add(MoveTemp(Port));
}

void AddDeterministicPort(
	FBlueprintLensLayoutLedger& Ledger,
	const FBlueprintLensLayoutNodePlacement& Placement,
	const FBlueprintLensLayoutPortRequest& Port,
	const int32 SameDirectionCount)
{
	FBlueprintLensLayoutPortPlacement Result;
	Result.UnitId = Placement.UnitId;
	Result.Label = Port.Label;
	Result.bInput = Port.bInput;
	const float Fraction = static_cast<float>(Port.Order + 1) /
		static_cast<float>(SameDirectionCount + 1);
	Result.Position = FVector2D(
		Port.bInput ? Placement.Position.X : Placement.Position.X + Placement.Size.X,
		Placement.Position.Y + Placement.Size.Y * Fraction);
	Ledger.Ports.Add(MoveTemp(Result));
}
} // namespace

const FBlueprintLensLC2GuardLayoutNode*
FBlueprintLensLC2GuardLayout::FindNode(const FString& UnitId) const
{
	return Nodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLC2GuardLayoutNode& Node)
		{
			return Node.UnitId == UnitId;
		});
}

bool FBlueprintLensLC2GuardLayout::CoversProjection(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection) const
{
	if (DiagnosticCode != TEXT("LC2_GUARD_LAYOUT_COMPLETE") ||
		Nodes.Num() != Projection.AllUnitIds.Num() ||
		LayoutRequest.Edges.Num() != Projection.AllRelationIds.Num())
	{
		return false;
	}
	TSet<FString> UnitIds;
	for (const FBlueprintLensLC2GuardLayoutNode& Node : Nodes)
	{
		if (Node.UnitId.IsEmpty() || UnitIds.Contains(Node.UnitId))
		{
			return false;
		}
		UnitIds.Add(Node.UnitId);
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (!UnitIds.Contains(UnitId))
		{
			return false;
		}
	}
	return HasExclusiveCompoundOwnership(Projection);
}

bool FBlueprintLensLC2GuardLayout::HasExclusiveCompoundOwnership(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection) const
{
	if (LayoutRequest.Profile != EBlueprintLensLayoutProfile::Compound ||
		LayoutRequest.Groups.Num() != 2)
	{
		return false;
	}
	for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
	{
		const FBlueprintLensLayoutGroupRequest* Group =
			LayoutRequest.Groups.FindByPredicate(
				[&Compound](const FBlueprintLensLayoutGroupRequest& Candidate)
				{
					return Candidate.GroupId == Compound.GroupId;
				});
		if (Group == nullptr || Group->ParentGroupId != Compound.ParentGroupId ||
			Group->MemberUnitIds != Compound.ExclusiveMemberUnitIds)
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensLC2GuardLayout::HasValidSharedLedger() const
{
	return LayoutLedger.IsCompleteFor(LayoutRequest);
}

FBlueprintLensLC2GuardLayout FBlueprintLensLC2GuardLayoutBuilder::Build(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection,
	const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth)
{
	FBlueprintLensLC2GuardLayout Result;
	if (!Projection.IsRenderable() || Explanation.Units.Num() != 9 ||
		Explanation.Relations.Num() != 10 || TargetWidth < 430.0f)
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}
	const float Width = TargetWidth;
	Result.CanvasSize = FVector2D(Width, TargetWidth >= 680.0f ? 420.0f : 600.0f);
	Result.LayoutRequest.GraphKey = FString::Printf(
		TEXT("LC2-D2:%s"), *Projection.SourceIrSha256);
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::Compound;
	Result.LayoutRequest.TargetWidth = Width;

	const bool bWide = TargetWidth >= 680.0f;
	for (int32 Index = 0; Index < Projection.CanonicalUnits.Num(); ++Index)
	{
		const FBlueprintLensLC2GuardCanonicalUnit& Unit = Projection.CanonicalUnits[Index];
		FBlueprintLensLC2GuardLayoutNode Node;
		Node.UnitId = Unit.UnitId;
		Node.ReaderLabel = Unit.ReaderLabel;
		Node.Size = NodeSize(Unit);
		const bool bOuter = Unit.OwnerGuardGroupId ==
			Projection.Compounds[0].GroupId;
		const bool bInner = Unit.OwnerGuardGroupId ==
			Projection.Compounds[1].GroupId;
		if (bWide)
		{
			if (Unit.bIsCriterion)
			{
				Node.Position = FVector2D(558.0f, 174.0f);
			}
			else if (Unit.OwnerGuardGroupId.IsEmpty())
			{
				Node.Position = FVector2D(16.0f, 184.0f);
			}
			else if (Unit.bIsPredicate)
			{
				Node.Position = bOuter
					? FVector2D(184.0f, 56.0f)
					: FVector2D(230.0f, 190.0f);
			}
			else if (Unit.bIsBranch)
			{
				Node.Position = bOuter
					? FVector2D(184.0f, 106.0f)
					: FVector2D(230.0f, 240.0f);
			}
			else if (bOuter)
			{
				Node.Position = FVector2D(350.0f, 106.0f);
			}
			else
			{
				const bool bAccepted = Unit.ReaderLabel.Contains(TEXT("Accepted"));
				Node.Position = FVector2D(374.0f, bAccepted ? 300.0f : 238.0f);
			}
		}
		else
		{
			if (Unit.bIsCriterion)
			{
				Node.Position = FVector2D(32.0f, 510.0f);
			}
			else if (Unit.OwnerGuardGroupId.IsEmpty())
			{
				Node.Position = FVector2D(16.0f, 16.0f);
			}
			else if (Unit.bIsPredicate)
			{
				Node.Position = bOuter
					? FVector2D(32.0f, 82.0f)
					: FVector2D(48.0f, 246.0f);
			}
			else if (Unit.bIsBranch)
			{
				Node.Position = bOuter
					? FVector2D(32.0f, 132.0f)
					: FVector2D(48.0f, 296.0f);
			}
			else if (bOuter)
			{
				Node.Position = FVector2D(Width - 140.0f, 132.0f);
			}
			else
			{
				const bool bAccepted = Unit.ReaderLabel.Contains(TEXT("Accepted"));
				Node.Position = FVector2D(
					Width - 140.0f,
					bAccepted ? 368.0f : 296.0f);
			}
		}
		Node.Position.X = FMath::Min(Node.Position.X, Width - Margin - Node.Size.X);
		Result.Nodes.Add(MoveTemp(Node));
	}

	for (const FBlueprintLensLC2GuardLayoutNode& Node : Result.Nodes)
	{
		FBlueprintLensLayoutNodeRequest RequestNode;
		RequestNode.UnitId = Node.UnitId;
		RequestNode.DesiredSize = Node.Size;
		Result.LayoutRequest.Nodes.Add(MoveTemp(RequestNode));
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!Projection.AllRelationIds.Contains(Relation.Id) ||
			Result.FindNode(Relation.SourceUnitId) == nullptr ||
			Result.FindNode(Relation.TargetUnitId) == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_RELATION_BINDING_INVALID");
			return Result;
		}
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.Id;
		Edge.SourceUnitId = Relation.SourceUnitId;
		Edge.TargetUnitId = Relation.TargetUnitId;
		Edge.SourcePortLabel = Relation.Id + TEXT(":out");
		Edge.TargetPortLabel = Relation.Id + TEXT(":in");
		Edge.Family = RelationFamily(Relation);
		Edge.bParticipatesInRank = Relation.Kind != EBlueprintLensRelationKind::PredicateFor;
		FBlueprintLensLayoutNodeRequest* Source = Result.LayoutRequest.Nodes.FindByPredicate(
			[&Relation](const FBlueprintLensLayoutNodeRequest& Candidate)
			{
				return Candidate.UnitId == Relation.SourceUnitId;
			});
		FBlueprintLensLayoutNodeRequest* Target = Result.LayoutRequest.Nodes.FindByPredicate(
			[&Relation](const FBlueprintLensLayoutNodeRequest& Candidate)
			{
				return Candidate.UnitId == Relation.TargetUnitId;
			});
		AddPort(*Source, Edge.SourcePortLabel, false);
		AddPort(*Target, Edge.TargetPortLabel, true);
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
	}
	for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
	{
		FBlueprintLensLayoutGroupRequest Group;
		Group.GroupId = Compound.GroupId;
		Group.ParentGroupId = Compound.ParentGroupId;
		Group.MemberUnitIds = Compound.ExclusiveMemberUnitIds;
		Result.LayoutRequest.Groups.Add(MoveTemp(Group));
	}
	if (!Result.LayoutRequest.IsValid() || !Result.HasExclusiveCompoundOwnership(Projection))
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_COMPOUND_REQUEST_INVALID");
		return Result;
	}

	Result.LayoutLedger.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Result.LayoutLedger.BackendVersion = TEXT("BlueprintLens.DeterministicLC2.D2.v1");
	Result.LayoutLedger.ConfigurationFingerprint = FString::Printf(
		TEXT("profile=Compound;target-width=%.2f"), TargetWidth);
	Result.LayoutLedger.CanvasSize = Result.CanvasSize;
	for (const FBlueprintLensLC2GuardLayoutNode& Node : Result.Nodes)
	{
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = Node.UnitId;
		Placement.Position = Node.Position;
		Placement.Size = Node.Size;
		Result.LayoutLedger.Nodes.Add(MoveTemp(Placement));
	}
	for (const FBlueprintLensLayoutNodeRequest& Node : Result.LayoutRequest.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Placement =
			Result.LayoutLedger.Nodes.FindByPredicate(
				[&Node](const FBlueprintLensLayoutNodePlacement& Candidate)
				{
					return Candidate.UnitId == Node.UnitId;
				});
		int32 Inputs = 0;
		int32 Outputs = 0;
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			Port.bInput ? ++Inputs : ++Outputs;
		}
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			AddDeterministicPort(
				Result.LayoutLedger, *Placement, Port,
				Port.bInput ? Inputs : Outputs);
		}
	}
	for (const FBlueprintLensLayoutEdgeRequest& Edge : Result.LayoutRequest.Edges)
	{
		FBlueprintLensLayoutEdgePlacement Placement;
		Placement.RelationId = Edge.RelationId;
		Placement.SourceUnitId = Edge.SourceUnitId;
		Placement.TargetUnitId = Edge.TargetUnitId;
		Placement.SourcePortLabel = Edge.SourcePortLabel;
		Placement.TargetPortLabel = Edge.TargetPortLabel;
		Placement.Family = Edge.Family;
		Result.LayoutLedger.Edges.Add(MoveTemp(Placement));
	}
	Result.LayoutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (int32 A = 0; A < Result.Nodes.Num(); ++A)
	{
		for (int32 B = A + 1; B < Result.Nodes.Num(); ++B)
		{
			if (StrictlyIntersects(Result.Nodes[A], Result.Nodes[B]))
			{
				Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_NODE_OVERLAP");
				return Result;
			}
		}
	}
	if (!Result.LayoutLedger.IsCompleteFor(Result.LayoutRequest))
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_LEDGER_INVALID");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_COMPLETE");
	return Result;
}
