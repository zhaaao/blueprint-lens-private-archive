#include "BlueprintLensLayoutContract.h"

namespace
{
bool StrictlyIntersects(
	const FBlueprintLensLayoutNodePlacement& A,
	const FBlueprintLensLayoutNodePlacement& B)
{
	return A.Position.X < B.Position.X + B.Size.X &&
		A.Position.X + A.Size.X > B.Position.X &&
		A.Position.Y < B.Position.Y + B.Size.Y &&
		A.Position.Y + A.Size.Y > B.Position.Y;
}
}

bool FBlueprintLensLayoutRequest::IsValid() const
{
	if (GraphKey.IsEmpty() || TargetWidth <= 0.0f || Nodes.IsEmpty())
	{
		return false;
	}

	TSet<FString> UnitIds;
	TMap<FString, TSet<FString>> InputPortsByUnit;
	TMap<FString, TSet<FString>> OutputPortsByUnit;
	for (const FBlueprintLensLayoutNodeRequest& Node : Nodes)
	{
		if (Node.UnitId.IsEmpty() || UnitIds.Contains(Node.UnitId) ||
			Node.DesiredSize.X <= 0.0f || Node.DesiredSize.Y <= 0.0f)
		{
			return false;
		}
		UnitIds.Add(Node.UnitId);
		TSet<FString>& InputPorts = InputPortsByUnit.Add(Node.UnitId);
		TSet<FString>& OutputPorts = OutputPortsByUnit.Add(Node.UnitId);
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			TSet<FString>& PortSet = Port.bInput ? InputPorts : OutputPorts;
			if (Port.Label.IsEmpty() || Port.Order < 0 || PortSet.Contains(Port.Label))
			{
				return false;
			}
			PortSet.Add(Port.Label);
		}
	}

	TSet<FString> RelationIds;
	for (const FBlueprintLensLayoutEdgeRequest& Edge : Edges)
	{
		if (Edge.RelationId.IsEmpty() || RelationIds.Contains(Edge.RelationId) ||
			!UnitIds.Contains(Edge.SourceUnitId) ||
			!UnitIds.Contains(Edge.TargetUnitId) ||
			Edge.SourcePortLabel.IsEmpty() || Edge.TargetPortLabel.IsEmpty() ||
			!OutputPortsByUnit.FindChecked(Edge.SourceUnitId).Contains(
				Edge.SourcePortLabel) ||
			!InputPortsByUnit.FindChecked(Edge.TargetUnitId).Contains(
				Edge.TargetPortLabel))
		{
			return false;
		}
		RelationIds.Add(Edge.RelationId);
	}

	TSet<FString> GroupIds;
	for (const FBlueprintLensLayoutGroupRequest& Group : Groups)
	{
		if (Group.GroupId.IsEmpty() || GroupIds.Contains(Group.GroupId))
		{
			return false;
		}
		GroupIds.Add(Group.GroupId);
	}
	for (const FBlueprintLensLayoutGroupRequest& Group : Groups)
	{
		if (Group.MemberUnitIds.IsEmpty() ||
			(!Group.ParentGroupId.IsEmpty() &&
				(Group.ParentGroupId == Group.GroupId ||
				 !GroupIds.Contains(Group.ParentGroupId))))
		{
			return false;
		}
		TSet<FString> MemberUnitIds;
		for (const FString& UnitId : Group.MemberUnitIds)
		{
			if (!UnitIds.Contains(UnitId) || MemberUnitIds.Contains(UnitId))
			{
				return false;
			}
			MemberUnitIds.Add(UnitId);
		}
	}
	return true;
}

const FBlueprintLensLayoutPortPlacement* FBlueprintLensLayoutLedger::FindPort(
	const FString& UnitId,
	const FString& Label,
	const bool bInput) const
{
	return Ports.FindByPredicate(
		[&UnitId, &Label, bInput](const FBlueprintLensLayoutPortPlacement& Port)
		{
			return Port.UnitId == UnitId && Port.Label == Label &&
				Port.bInput == bInput;
		});
}

bool FBlueprintLensLayoutLedger::HasNoNodeOverlaps() const
{
	for (int32 AIndex = 0; AIndex < Nodes.Num(); ++AIndex)
	{
		for (int32 BIndex = AIndex + 1; BIndex < Nodes.Num(); ++BIndex)
		{
			if (StrictlyIntersects(Nodes[AIndex], Nodes[BIndex]))
			{
				return false;
			}
		}
	}
	return true;
}

bool FBlueprintLensLayoutLedger::IsCompleteFor(
	const FBlueprintLensLayoutRequest& Request) const
{
	if (!Request.IsValid() || DiagnosticCode != TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE") ||
		BackendVersion.IsEmpty() || ConfigurationFingerprint.IsEmpty() ||
		CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f ||
		Nodes.Num() != Request.Nodes.Num() || Edges.Num() != Request.Edges.Num())
	{
		return false;
	}

	TSet<FString> UnitIds;
	for (const FBlueprintLensLayoutNodePlacement& Node : Nodes)
	{
		if (Node.UnitId.IsEmpty() || UnitIds.Contains(Node.UnitId) ||
			Node.Size.X <= 0.0f || Node.Size.Y <= 0.0f ||
			Node.Position.X < 0.0f || Node.Position.Y < 0.0f)
		{
			return false;
		}
		UnitIds.Add(Node.UnitId);
		const FBlueprintLensLayoutNodeRequest* RequestedNode =
			Request.Nodes.FindByPredicate(
				[&Node](const FBlueprintLensLayoutNodeRequest& Candidate)
				{
					return Candidate.UnitId == Node.UnitId;
				});
		if (RequestedNode == nullptr ||
			!Node.Size.Equals(RequestedNode->DesiredSize) ||
			Node.Position.X + Node.Size.X > CanvasSize.X ||
			Node.Position.Y + Node.Size.Y > CanvasSize.Y)
		{
			return false;
		}
	}

	int32 RequestedPortCount = 0;
	for (const FBlueprintLensLayoutNodeRequest& Node : Request.Nodes)
	{
		RequestedPortCount += Node.Ports.Num();
	}
	if (Ports.Num() != RequestedPortCount)
	{
		return false;
	}
	TSet<FString> PortKeys;
	for (const FBlueprintLensLayoutPortPlacement& Port : Ports)
	{
		const FString PortKey = FString::Printf(
			TEXT("%s\x1f%s\x1f%d"),
			*Port.UnitId,
			*Port.Label,
			Port.bInput ? 1 : 0);
		const FBlueprintLensLayoutNodeRequest* RequestedNode =
			Request.Nodes.FindByPredicate(
				[&Port](const FBlueprintLensLayoutNodeRequest& Candidate)
				{
					return Candidate.UnitId == Port.UnitId;
				});
		if (Port.UnitId.IsEmpty() || Port.Label.IsEmpty() ||
			!UnitIds.Contains(Port.UnitId) || PortKeys.Contains(PortKey) ||
			Port.Position.X < 0.0f || Port.Position.Y < 0.0f ||
			Port.Position.X > CanvasSize.X || Port.Position.Y > CanvasSize.Y ||
			RequestedNode == nullptr ||
			!RequestedNode->Ports.ContainsByPredicate(
				[&Port](const FBlueprintLensLayoutPortRequest& Candidate)
				{
					return Candidate.Label == Port.Label &&
						Candidate.bInput == Port.bInput;
				}))
		{
			return false;
		}
		PortKeys.Add(PortKey);
	}

	TSet<FString> RelationIds;
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Edges)
	{
		if (Edge.RelationId.IsEmpty() || RelationIds.Contains(Edge.RelationId) ||
			!UnitIds.Contains(Edge.SourceUnitId) ||
			!UnitIds.Contains(Edge.TargetUnitId) ||
			Edge.SourcePortLabel.IsEmpty() || Edge.TargetPortLabel.IsEmpty())
		{
			return false;
		}
		RelationIds.Add(Edge.RelationId);
		const FBlueprintLensLayoutEdgeRequest* RequestedEdge =
			Request.Edges.FindByPredicate(
				[&Edge](const FBlueprintLensLayoutEdgeRequest& Candidate)
				{
					return Candidate.RelationId == Edge.RelationId;
				});
		if (RequestedEdge == nullptr ||
			RequestedEdge->SourceUnitId != Edge.SourceUnitId ||
			RequestedEdge->TargetUnitId != Edge.TargetUnitId ||
			RequestedEdge->SourcePortLabel != Edge.SourcePortLabel ||
			RequestedEdge->TargetPortLabel != Edge.TargetPortLabel ||
			RequestedEdge->Family != Edge.Family ||
			FindPort(Edge.SourceUnitId, Edge.SourcePortLabel, false) == nullptr ||
			FindPort(Edge.TargetUnitId, Edge.TargetPortLabel, true) == nullptr)
		{
			return false;
		}
		for (const FVector2D& BendPoint : Edge.BendPoints)
		{
			if (BendPoint.X < 0.0f || BendPoint.Y < 0.0f ||
				BendPoint.X > CanvasSize.X || BendPoint.Y > CanvasSize.Y)
			{
				return false;
			}
		}
	}

	return HasNoNodeOverlaps();
}

EBlueprintLensLayoutBackendKind FBlueprintLensLayoutBackendPolicy::PreferredBackend(
	const EBlueprintLensLayoutProfile Profile)
{
	return Profile == EBlueprintLensLayoutProfile::Linear
		? EBlueprintLensLayoutBackendKind::GraphvizDot
		: EBlueprintLensLayoutBackendKind::ElkLayered;
}

TArray<EBlueprintLensLayoutBackendKind> FBlueprintLensLayoutBackendPolicy::CandidateOrder(
	const EBlueprintLensLayoutProfile Profile)
{
	TArray<EBlueprintLensLayoutBackendKind> Result;
	Result.Add(PreferredBackend(Profile));
	if (Profile != EBlueprintLensLayoutProfile::Linear)
	{
		Result.Add(EBlueprintLensLayoutBackendKind::GraphvizDot);
	}
	Result.Add(EBlueprintLensLayoutBackendKind::Deterministic);
	return Result;
}

const TCHAR* BlueprintLensLayoutBackendName(
	const EBlueprintLensLayoutBackendKind Backend)
{
	switch (Backend)
	{
	case EBlueprintLensLayoutBackendKind::GraphvizDot:
		return TEXT("Graphviz.dot");
	case EBlueprintLensLayoutBackendKind::ElkLayered:
		return TEXT("ELK.Layered");
	default:
		return TEXT("Deterministic");
	}
}

const TCHAR* BlueprintLensLayoutProfileName(
	const EBlueprintLensLayoutProfile Profile)
{
	switch (Profile)
	{
	case EBlueprintLensLayoutProfile::LayeredPorts:
		return TEXT("LayeredPorts");
	case EBlueprintLensLayoutProfile::Compound:
		return TEXT("Compound");
	case EBlueprintLensLayoutProfile::Cyclic:
		return TEXT("Cyclic");
	default:
		return TEXT("Linear");
	}
}
