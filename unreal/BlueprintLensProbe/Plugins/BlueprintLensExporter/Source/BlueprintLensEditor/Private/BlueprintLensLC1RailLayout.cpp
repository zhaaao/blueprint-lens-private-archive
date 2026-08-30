#include "BlueprintLensLC1RailLayout.h"

namespace
{
constexpr float NodeWidth = 250.0f;
constexpr float NodeHeight = 36.0f;
constexpr float Gap = 20.0f;
constexpr float Margin = 16.0f;

bool StrictlyIntersects(const FBlueprintLensLC1RailLayoutNode& A,
	const FBlueprintLensLC1RailLayoutNode& B)
{
	return A.Position.X < B.Position.X + B.Size.X &&
		A.Position.X + A.Size.X > B.Position.X &&
		A.Position.Y < B.Position.Y + B.Size.Y &&
		A.Position.Y + A.Size.Y > B.Position.Y;
}

void AddPort(FBlueprintLensLayoutNodeRequest& Node, const FString& Label,
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

void AddDeterministicPort(FBlueprintLensLayoutLedger& Ledger,
	const FBlueprintLensLayoutNodePlacement& Placement,
	const FBlueprintLensLayoutPortRequest& Port, const int32 Count)
{
	FBlueprintLensLayoutPortPlacement Result;
	Result.UnitId = Placement.UnitId;
	Result.Label = Port.Label;
	Result.bInput = Port.bInput;
	const float Fraction = static_cast<float>(Port.Order + 1) /
		static_cast<float>(Count + 1);
	Result.Position = FVector2D(
		Port.bInput ? Placement.Position.X : Placement.Position.X + Placement.Size.X,
		Placement.Position.Y + Placement.Size.Y * Fraction);
	Ledger.Ports.Add(MoveTemp(Result));
}
}

const FBlueprintLensLC1RailLayoutNode*
FBlueprintLensLC1RailLayout::FindNode(const FString& UnitId) const
{
	return Nodes.FindByPredicate([&UnitId](const auto& Node)
	{
		return Node.UnitId == UnitId;
	});
}

bool FBlueprintLensLC1RailLayout::CoversProjection(
	const FBlueprintLensLC1RailProjection& Projection) const
{
	if (DiagnosticCode != TEXT("LC1_RAIL_LAYOUT_COMPLETE") ||
		Nodes.Num() != Projection.OrderedCanonicalUnits.Num() ||
		LayoutRequest.Edges.Num() != Projection.OrderedExecutionRelations.Num())
	{
		return false;
	}
	TSet<FString> Seen;
	for (const auto& Node : Nodes)
	{
		if (Node.UnitId.IsEmpty() || Seen.Contains(Node.UnitId)) return false;
		Seen.Add(Node.UnitId);
	}
	for (const FString& Id : Projection.AllUnitIds)
	{
		if (!Seen.Contains(Id)) return false;
	}
	return true;
}

bool FBlueprintLensLC1RailLayout::HasValidSharedLedger() const
{
	return LayoutLedger.IsCompleteFor(LayoutRequest);
}

FBlueprintLensLC1RailLayout FBlueprintLensLC1RailLayoutBuilder::Build(
	const FBlueprintLensLC1RailProjection& Projection,
	const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth)
{
	FBlueprintLensLC1RailLayout Result;
	if (!Projection.IsRenderable() || TargetWidth < 320.0f)
	{
		Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (Explanation.FindUnit(UnitId) == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_REQUEST_UNAVAILABLE");
			return Result;
		}
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		Projection.OrderedExecutionRelations)
	{
		const FBlueprintLensRelation* SourceRelation =
			Explanation.FindRelation(Relation.RelationId);
		if (SourceRelation == nullptr ||
			SourceRelation->Kind !=
				EBlueprintLensRelationKind::ExecutionPredecessor ||
			SourceRelation->SourceUnitId != Relation.SourceUnitId ||
			SourceRelation->TargetUnitId != Relation.TargetUnitId)
		{
			Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_REQUEST_UNAVAILABLE");
			return Result;
		}
	}
	Result.CanvasSize = FVector2D(FMath::Max(TargetWidth, NodeWidth + Margin * 2.0f),
		Margin * 2.0f + Projection.OrderedCanonicalUnits.Num() * (NodeHeight + Gap));
	Result.LayoutRequest.GraphKey = FString::Printf(TEXT("LC1_SHARED_EXECUTION_RAIL:%s"), *Projection.SourceIrSha256);
	Result.LayoutRequest.Profile = EBlueprintLensLayoutProfile::Linear;
	Result.LayoutRequest.TargetWidth = TargetWidth;
	for (int32 Index = 0; Index < Projection.OrderedCanonicalUnits.Num(); ++Index)
	{
		const auto& Unit = Projection.OrderedCanonicalUnits[Index];
		FBlueprintLensLC1RailLayoutNode Node;
		Node.UnitId = Unit.UnitId;
		Node.ReaderLabel = Unit.ReaderLabel;
		Node.Size = FVector2D(FMath::Min(NodeWidth, TargetWidth - Margin * 2.0f), NodeHeight);
		Node.Position = FVector2D(Margin, Margin + Index * (NodeHeight + Gap));
		Result.Nodes.Add(MoveTemp(Node));
		FBlueprintLensLayoutNodeRequest RequestNode;
		RequestNode.UnitId = Unit.UnitId;
		RequestNode.DesiredSize = Result.Nodes.Last().Size;
		Result.LayoutRequest.Nodes.Add(MoveTemp(RequestNode));
	}
	for (const auto& Relation : Projection.OrderedExecutionRelations)
	{
		if (Result.FindNode(Relation.SourceUnitId) == nullptr || Result.FindNode(Relation.TargetUnitId) == nullptr)
		{
			Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_RELATION_BINDING_INVALID");
			return Result;
		}
		FBlueprintLensLayoutEdgeRequest Edge;
		Edge.RelationId = Relation.RelationId;
		Edge.SourceUnitId = Relation.SourceUnitId;
		Edge.TargetUnitId = Relation.TargetUnitId;
		Edge.SourcePortLabel = Relation.RelationId + TEXT(":out");
		Edge.TargetPortLabel = Relation.RelationId + TEXT(":in");
		Edge.Family = EBlueprintLensLayoutRelationFamily::Execution;
		Result.LayoutRequest.Edges.Add(MoveTemp(Edge));
		AddPort(*Result.LayoutRequest.Nodes.FindByPredicate([&Relation](const auto& N){ return N.UnitId == Relation.SourceUnitId; }), Result.LayoutRequest.Edges.Last().SourcePortLabel, false);
		AddPort(*Result.LayoutRequest.Nodes.FindByPredicate([&Relation](const auto& N){ return N.UnitId == Relation.TargetUnitId; }), Result.LayoutRequest.Edges.Last().TargetPortLabel, true);
	}
	Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_COMPLETE");
	if (!Result.LayoutRequest.IsValid() || !Result.CoversProjection(Projection))
	{
		Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_REQUEST_INVALID");
		return Result;
	}
	Result.LayoutLedger.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
	Result.LayoutLedger.BackendVersion = TEXT("BlueprintLens.DeterministicLC1.M1M2.v1");
	Result.LayoutLedger.ConfigurationFingerprint = FString::Printf(
		TEXT("profile=%s;target-width=%.2f"),
		BlueprintLensLayoutProfileName(Result.LayoutRequest.Profile),
		TargetWidth);
	Result.LayoutLedger.CanvasSize = Result.CanvasSize;
	for (const auto& Node : Result.Nodes)
	{
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = Node.UnitId;
		Placement.Position = Node.Position;
		Placement.Size = Node.Size;
		Result.LayoutLedger.Nodes.Add(MoveTemp(Placement));
	}
	for (const auto& RequestNode : Result.LayoutRequest.Nodes)
	{
		const auto* Placement = Result.LayoutLedger.Nodes.FindByPredicate([&RequestNode](const auto& P){ return P.UnitId == RequestNode.UnitId; });
		int32 Inputs = 0;
		int32 Outputs = 0;
		for (const auto& Port : RequestNode.Ports) Port.bInput ? ++Inputs : ++Outputs;
		for (const auto& Port : RequestNode.Ports) AddDeterministicPort(Result.LayoutLedger, *Placement, Port, Port.bInput ? Inputs : Outputs);
	}
	for (const auto& Edge : Result.LayoutRequest.Edges)
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
				Result.DiagnosticCode = TEXT("LC1_RAIL_LABEL_CLEARANCE_FAILED");
				return Result;
			}
		}
	}
	Result.DiagnosticCode = Result.LayoutLedger.IsCompleteFor(Result.LayoutRequest)
		? TEXT("LC1_RAIL_LAYOUT_COMPLETE") : TEXT("LC1_RAIL_LAYOUT_LEDGER_INVALID");
	return Result;
}
