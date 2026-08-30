#include "BlueprintLensLC3ValueConeLayout.h"

namespace
{
constexpr float WideThreshold = 680.0f;
constexpr float Margin = 12.0f;

FVector2D NodeSize(
	const EBlueprintLensLC3ValueConeNodeKind Kind,
	const EBlueprintLensLC3ValueConeLayoutMode Mode)
{
	const float CompactWidthAdjustment =
		Mode == EBlueprintLensLC3ValueConeLayoutMode::Compact ? -4.0f : 0.0f;
	switch (Kind)
	{
	case EBlueprintLensLC3ValueConeNodeKind::Criterion:
		return FVector2D(160.0f + CompactWidthAdjustment, 92.0f);
	case EBlueprintLensLC3ValueConeNodeKind::Operator:
		return FVector2D(142.0f + CompactWidthAdjustment, 114.0f);
	case EBlueprintLensLC3ValueConeNodeKind::Control:
		return FVector2D(154.0f + CompactWidthAdjustment, 106.0f);
	default:
		return FVector2D(126.0f + CompactWidthAdjustment, 94.0f);
	}
}

bool StrictlyIntersects(
	const FBlueprintLensLC3ValueConeLayoutNode& A,
	const FBlueprintLensLC3ValueConeLayoutNode& B)
{
	return A.Position.X < B.Position.X + B.Size.X &&
		A.Position.X + A.Size.X > B.Position.X &&
		A.Position.Y < B.Position.Y + B.Size.Y &&
		A.Position.Y + A.Size.Y > B.Position.Y;
}

EBlueprintLensLayoutRelationFamily RelationFamily(
	const FBlueprintLensLC3ValueConeLayoutEdge& Edge)
{
	return Edge.bControl
		? EBlueprintLensLayoutRelationFamily::Execution
		: EBlueprintLensLayoutRelationFamily::Value;
}

FVector2D SourceAnchor(
	const FBlueprintLensLC3ValueConeLayout& Layout,
	const FBlueprintLensLC3ValueConeLayoutEdge& Edge)
{
	const FBlueprintLensLC3ValueConeLayoutNode* Node =
		Layout.FindNode(Edge.SourceUnitId);
	if (Node == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	if (Edge.bControl || Layout.Mode == EBlueprintLensLC3ValueConeLayoutMode::Wide)
	{
		return FVector2D(
			Node->Position.X + Node->Size.X,
			Node->Position.Y + Node->Size.Y * 0.5f);
	}
	return FVector2D(
		Node->Position.X + Node->Size.X * 0.5f,
		Node->Position.Y + Node->Size.Y);
}

FVector2D TargetAnchor(
	const FBlueprintLensLC3ValueConeLayout& Layout,
	const FBlueprintLensLC3ValueConeLayoutEdge& Edge)
{
	const FBlueprintLensLC3ValueConeLayoutNode* Node =
		Layout.FindNode(Edge.TargetUnitId);
	if (Node == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	if (Edge.bControl)
	{
		return FVector2D(
			Node->Position.X + Node->Size.X * 0.5f,
			Node->Position.Y + Node->Size.Y);
	}
	const int32 PortIndex = FMath::Max(
		0,
		Node->InputPortLabels.IndexOfByKey(Edge.TargetPortLabel));
	const int32 PortCount = FMath::Max(1, Node->InputPortLabels.Num());
	if (Layout.Mode == EBlueprintLensLC3ValueConeLayoutMode::Wide)
	{
		return FVector2D(
			Node->Position.X,
			Node->Position.Y +
				Node->Size.Y * static_cast<float>(PortIndex + 1) /
					static_cast<float>(PortCount + 1));
	}
	return FVector2D(
		Node->Position.X +
			Node->Size.X * static_cast<float>(PortIndex + 1) /
				static_cast<float>(PortCount + 1),
		Node->Position.Y);
}

void BuildSharedLayoutContract(
	FBlueprintLensLC3ValueConeLayout& Layout,
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const float TargetWidth)
{
	Layout.LayoutRequest.GraphKey = FString::Printf(
		TEXT("LC3:%s"),
		*Projection.GroupId);
	Layout.LayoutRequest.Profile =
		EBlueprintLensLayoutProfile::LayeredPorts;
	Layout.LayoutRequest.TargetWidth = TargetWidth;

	for (const FBlueprintLensLC3ValueConeLayoutNode& Node : Layout.Nodes)
	{
		FBlueprintLensLayoutNodeRequest RequestNode;
		RequestNode.UnitId = Node.UnitId;
		RequestNode.DesiredSize = Node.Size;
		for (int32 Index = 0; Index < Node.InputPortLabels.Num(); ++Index)
		{
			FBlueprintLensLayoutPortRequest Port;
			Port.Label = Node.InputPortLabels[Index];
			Port.bInput = true;
			Port.Order = Index;
			RequestNode.Ports.Add(MoveTemp(Port));
		}
		for (int32 Index = 0; Index < Node.OutputPortLabels.Num(); ++Index)
		{
			FBlueprintLensLayoutPortRequest Port;
			Port.Label = Node.OutputPortLabels[Index];
			Port.bInput = false;
			Port.Order = Index;
			RequestNode.Ports.Add(MoveTemp(Port));
		}
		Layout.LayoutRequest.Nodes.Add(MoveTemp(RequestNode));
	}

	const auto EnsureRequestedPort =
		[&Layout](
			const FString& UnitId,
			const FString& Label,
			const bool bInput)
		{
			FBlueprintLensLayoutNodeRequest* Node =
				Layout.LayoutRequest.Nodes.FindByPredicate(
					[&UnitId](const FBlueprintLensLayoutNodeRequest& Candidate)
					{
						return Candidate.UnitId == UnitId;
					});
			if (Node == nullptr || Node->Ports.ContainsByPredicate(
				[&Label, bInput](const FBlueprintLensLayoutPortRequest& Port)
				{
					return Port.Label == Label && Port.bInput == bInput;
				}))
			{
				return;
			}
			FBlueprintLensLayoutPortRequest Port;
			Port.Label = Label;
			Port.bInput = bInput;
			for (const FBlueprintLensLayoutPortRequest& Candidate : Node->Ports)
			{
				Port.Order += Candidate.bInput == bInput ? 1 : 0;
			}
			Node->Ports.Add(MoveTemp(Port));
		};

	for (const FBlueprintLensLC3ValueConeLayoutEdge& Edge : Layout.Edges)
	{
		EnsureRequestedPort(
			Edge.SourceUnitId,
			Edge.SourcePortLabel,
			false);
		EnsureRequestedPort(
			Edge.TargetUnitId,
			Edge.TargetPortLabel,
			true);
		FBlueprintLensLayoutEdgeRequest RequestEdge;
		RequestEdge.RelationId = Edge.RelationId;
		RequestEdge.SourceUnitId = Edge.SourceUnitId;
		RequestEdge.TargetUnitId = Edge.TargetUnitId;
		RequestEdge.SourcePortLabel = Edge.SourcePortLabel;
		RequestEdge.TargetPortLabel = Edge.TargetPortLabel;
		RequestEdge.Family = RelationFamily(Edge);
		RequestEdge.bParticipatesInRank = !Edge.bControl;
		Layout.LayoutRequest.Edges.Add(MoveTemp(RequestEdge));
	}

	FBlueprintLensLayoutGroupRequest Group;
	Group.GroupId = Projection.GroupId;
	Group.MemberUnitIds = Projection.ConeUnitIds;
	Layout.LayoutRequest.Groups.Add(MoveTemp(Group));

	Layout.LayoutLedger.Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	Layout.LayoutLedger.BackendVersion =
		TEXT("BlueprintLens.DeterministicLC3.v1");
	Layout.LayoutLedger.ConfigurationFingerprint = FString::Printf(
		TEXT("profile=LayeredPorts;mode=%d;width=%.2f"),
		static_cast<int32>(Layout.Mode),
		TargetWidth);
	Layout.LayoutLedger.CanvasSize = Layout.CanvasSize;
	for (const FBlueprintLensLC3ValueConeLayoutNode& Node : Layout.Nodes)
	{
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = Node.UnitId;
		Placement.Position = Node.Position;
		Placement.Size = Node.Size;
		Layout.LayoutLedger.Nodes.Add(MoveTemp(Placement));
	}
	for (const FBlueprintLensLayoutNodeRequest& Node : Layout.LayoutRequest.Nodes)
	{
		for (const FBlueprintLensLayoutPortRequest& Port : Node.Ports)
		{
			const FBlueprintLensLC3ValueConeLayoutEdge* Edge =
				Layout.Edges.FindByPredicate(
					[&Node, &Port](const FBlueprintLensLC3ValueConeLayoutEdge& Candidate)
					{
						return Port.bInput
							? Candidate.TargetUnitId == Node.UnitId &&
								Candidate.TargetPortLabel == Port.Label
							: Candidate.SourceUnitId == Node.UnitId &&
								Candidate.SourcePortLabel == Port.Label;
					});
			if (Edge == nullptr)
			{
				continue;
			}
			FBlueprintLensLayoutPortPlacement Placement;
			Placement.UnitId = Node.UnitId;
			Placement.Label = Port.Label;
			Placement.bInput = Port.bInput;
			Placement.Position = Port.bInput
				? TargetAnchor(Layout, *Edge)
				: SourceAnchor(Layout, *Edge);
			Layout.LayoutLedger.Ports.Add(MoveTemp(Placement));
		}
	}
	for (const FBlueprintLensLC3ValueConeLayoutEdge& Edge : Layout.Edges)
	{
		FBlueprintLensLayoutEdgePlacement Placement;
		Placement.RelationId = Edge.RelationId;
		Placement.SourceUnitId = Edge.SourceUnitId;
		Placement.TargetUnitId = Edge.TargetUnitId;
		Placement.SourcePortLabel = Edge.SourcePortLabel;
		Placement.TargetPortLabel = Edge.TargetPortLabel;
		Placement.Family = RelationFamily(Edge);
		if (Edge.bControl)
		{
			const FVector2D Start = SourceAnchor(Layout, Edge);
			const FVector2D End = TargetAnchor(Layout, Edge);
			if (Layout.Mode == EBlueprintLensLC3ValueConeLayoutMode::Wide)
			{
				Placement.BendPoints.Add(FVector2D(End.X, Start.Y));
			}
			else
			{
				const float RailX = Layout.CanvasSize.X - 18.0f;
				Placement.BendPoints = {
					FVector2D(RailX, Start.Y),
					FVector2D(RailX, End.Y + 12.0f),
					FVector2D(End.X, End.Y + 12.0f)};
			}
		}
		Layout.LayoutLedger.Edges.Add(MoveTemp(Placement));
	}
	Layout.LayoutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	if (!Layout.LayoutLedger.IsCompleteFor(Layout.LayoutRequest))
	{
		Layout.LayoutLedger.DiagnosticCode =
			TEXT("BLUEPRINT_LENS_LAYOUT_REQUEST_INVALID");
	}
}
} // namespace

const FBlueprintLensLC3ValueConeLayoutNode*
FBlueprintLensLC3ValueConeLayout::FindNode(const FString& UnitId) const
{
	return Nodes.FindByPredicate(
		[&UnitId](const FBlueprintLensLC3ValueConeLayoutNode& Node)
		{
			return Node.UnitId == UnitId;
		});
}

bool FBlueprintLensLC3ValueConeLayout::HasNoNodeOverlaps() const
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

bool FBlueprintLensLC3ValueConeLayout::CoversProjection(
	const FBlueprintLensLC3ValueConeProjection& Projection) const
{
	if (DiagnosticCode != TEXT("LC3_VALUE_CONE_LAYOUT_COMPLETE") ||
		Nodes.Num() != Projection.AllUnitIds.Num() ||
		Edges.Num() != Projection.AllRelationIds.Num())
	{
		return false;
	}

	TSet<FString> NodeIds;
	for (const FBlueprintLensLC3ValueConeLayoutNode& Node : Nodes)
	{
		if (Node.UnitId.IsEmpty() || NodeIds.Contains(Node.UnitId))
		{
			return false;
		}
		NodeIds.Add(Node.UnitId);
	}
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		if (!NodeIds.Contains(UnitId))
		{
			return false;
		}
	}

	TSet<FString> RelationIds;
	for (const FBlueprintLensLC3ValueConeLayoutEdge& Edge : Edges)
	{
		if (Edge.RelationId.IsEmpty() || RelationIds.Contains(Edge.RelationId) ||
			!NodeIds.Contains(Edge.SourceUnitId) ||
			!NodeIds.Contains(Edge.TargetUnitId) ||
			Edge.SourcePortLabel.IsEmpty() || Edge.TargetPortLabel.IsEmpty())
		{
			return false;
		}
		RelationIds.Add(Edge.RelationId);
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

bool FBlueprintLensLC3ValueConeLayout::HasValidSharedLedger() const
{
	return LayoutLedger.IsCompleteFor(LayoutRequest);
}

FBlueprintLensLC3ValueConeLayout
FBlueprintLensLC3ValueConeLayoutBuilder::Build(
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const float TargetWidth)
{
	FBlueprintLensLC3ValueConeLayout Result;
	if (Projection.Status !=
			EBlueprintLensLC3ValueConeProjectionStatus::ValueCone ||
		!Projection.IsRenderable())
	{
		Result.DiagnosticCode = TEXT("LC3_VALUE_CONE_LAYOUT_UNAVAILABLE");
		return Result;
	}

	const float EffectiveWidth = FMath::Max(TargetWidth, 360.0f);
	Result.Mode = EffectiveWidth >= WideThreshold
		? EBlueprintLensLC3ValueConeLayoutMode::Wide
		: EBlueprintLensLC3ValueConeLayoutMode::Compact;

	FBlueprintLensLC3ValueConeLayoutNode Criterion;
	Criterion.UnitId = Projection.CriterionUnitId;
	Criterion.ReaderLabel = Projection.CriterionReaderLabel;
	Criterion.RouteText = FString::Printf(
		TEXT("receives %s"),
		*FString::Join(Projection.CriterionInputPortLabels, TEXT(", ")));
	Criterion.InputPortLabels = Projection.CriterionInputPortLabels;
	Criterion.DerivationDepth = 0;
	Criterion.Kind = EBlueprintLensLC3ValueConeNodeKind::Criterion;
	Criterion.Size = NodeSize(Criterion.Kind, Result.Mode);
	Result.Nodes.Add(MoveTemp(Criterion));

	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		FBlueprintLensLC3ValueConeLayoutNode Node;
		Node.UnitId = Step.ProducerUnitId;
		Node.ReaderLabel = Step.ProducerReaderLabel;
		Node.RouteText = Step.ReaderRowText;
		Node.InputSummaryText = Step.ProducerInputSummaryText;
		Node.InputPortLabels = Step.ProducerInputPortLabels;
		Node.OutputPortLabels.Add(Step.ProducerPortLabel);
		Node.DerivationDepth = Step.DerivationDepth;
		Node.Kind = Step.ProducerInputCount > 0
			? EBlueprintLensLC3ValueConeNodeKind::Operator
			: EBlueprintLensLC3ValueConeNodeKind::Leaf;
		Node.Size = NodeSize(Node.Kind, Result.Mode);
		Result.Nodes.Add(MoveTemp(Node));

		FBlueprintLensLC3ValueConeLayoutEdge Edge;
		Edge.RelationId = Step.RelationId;
		Edge.SourceUnitId = Step.ProducerUnitId;
		Edge.TargetUnitId = Step.ConsumerUnitId;
		Edge.SourcePortLabel = Step.ProducerPortLabel;
		Edge.TargetPortLabel = Step.ConsumerPortLabel;
		Result.Edges.Add(MoveTemp(Edge));
	}

	FBlueprintLensLC3ValueConeLayoutNode Control;
	Control.UnitId = Projection.Control.ControllerUnitId;
	Control.ReaderLabel = Projection.Control.ControllerReaderLabel;
	Control.RouteText = Projection.Control.ReaderRowText;
	Control.OutputPortLabels.Add(Projection.Control.ControllerPortLabel);
	Control.Kind = EBlueprintLensLC3ValueConeNodeKind::Control;
	Control.Size = NodeSize(Control.Kind, Result.Mode);
	Result.Nodes.Add(MoveTemp(Control));

	FBlueprintLensLC3ValueConeLayoutEdge ControlEdge;
	ControlEdge.RelationId = Projection.Control.RelationId;
	ControlEdge.SourceUnitId = Projection.Control.ControllerUnitId;
	ControlEdge.TargetUnitId = Projection.Control.TargetUnitId;
	ControlEdge.SourcePortLabel = Projection.Control.ControllerPortLabel;
	ControlEdge.TargetPortLabel = Projection.Control.TargetPortLabel;
	ControlEdge.bControl = true;
	Result.Edges.Add(MoveTemp(ControlEdge));

	TMap<FString, float> Scalars;
	int32 LeafOrdinal = 0;
	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		if (Step.ProducerInputCount == 0)
		{
			Scalars.Add(Step.ProducerUnitId, static_cast<float>(LeafOrdinal++));
		}
	}
	for (int32 Depth = 2; Depth >= 0; --Depth)
	{
		for (FBlueprintLensLC3ValueConeLayoutNode& Node : Result.Nodes)
		{
			if (Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control ||
				Node.DerivationDepth != Depth || Scalars.Contains(Node.UnitId))
			{
				continue;
			}
			float Sum = 0.0f;
			int32 Count = 0;
			for (const FBlueprintLensLC3ValueConeLayoutEdge& Edge : Result.Edges)
			{
				if (!Edge.bControl && Edge.TargetUnitId == Node.UnitId)
				{
					const float* SourceScalar = Scalars.Find(Edge.SourceUnitId);
					if (SourceScalar != nullptr)
					{
						Sum += *SourceScalar;
						++Count;
					}
				}
			}
			if (Count > 0)
			{
				Scalars.Add(Node.UnitId, Sum / static_cast<float>(Count));
			}
		}
	}

	if (Scalars.Num() != Projection.ConeUnitIds.Num())
	{
		Result.Nodes.Reset();
		Result.Edges.Reset();
		Result.DiagnosticCode = TEXT("LC3_VALUE_CONE_LAYOUT_TOPOLOGY_INVALID");
		return Result;
	}

	if (Result.Mode == EBlueprintLensLC3ValueConeLayoutMode::Wide)
	{
		const float StageWidths[] = {160.0f, 142.0f, 142.0f, 126.0f};
		const float Gap = FMath::Max(
			18.0f,
			(EffectiveWidth - Margin * 2.0f - 570.0f) / 3.0f);
		float StageX[4];
		StageX[3] = Margin;
		StageX[2] = StageX[3] + StageWidths[3] + Gap;
		StageX[1] = StageX[2] + StageWidths[2] + Gap;
		StageX[0] = StageX[1] + StageWidths[1] + Gap;

		for (FBlueprintLensLC3ValueConeLayoutNode& Node : Result.Nodes)
		{
			if (Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control)
			{
				continue;
			}
			const float Scalar = Scalars.FindRef(Node.UnitId);
			Node.Position = FVector2D(
				StageX[FMath::Clamp(Node.DerivationDepth, 0, 3)],
				Margin + Scalar * 112.0f);
		}
		const float ControlY = Margin + 2.0f * 112.0f + 114.0f + 28.0f;
		FBlueprintLensLC3ValueConeLayoutNode* ControlNode =
			Result.Nodes.FindByPredicate(
				[](const FBlueprintLensLC3ValueConeLayoutNode& Node)
				{
					return Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control;
				});
		ControlNode->Position = FVector2D(Margin, ControlY);
		Result.CanvasSize = FVector2D(EffectiveWidth, ControlY + 118.0f);
	}
	else
	{
		const float LeftCenter = Margin + 61.0f;
		const float RightCenter = EffectiveWidth - Margin - 61.0f;
		const float ScalarSpan = RightCenter - LeftCenter;
		const float StageY[] = {396.0f, 264.0f, 132.0f, 20.0f};
		for (FBlueprintLensLC3ValueConeLayoutNode& Node : Result.Nodes)
		{
			if (Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control)
			{
				continue;
			}
			const float Scalar = Scalars.FindRef(Node.UnitId);
			const float CentreX = LeftCenter + ScalarSpan * (Scalar / 2.0f);
			Node.Position = FVector2D(
				FMath::Clamp(
					CentreX - Node.Size.X * 0.5f,
					Margin,
					EffectiveWidth - Margin - Node.Size.X),
				StageY[FMath::Clamp(Node.DerivationDepth, 0, 3)]);
		}
		FBlueprintLensLC3ValueConeLayoutNode* ControlNode =
			Result.Nodes.FindByPredicate(
				[](const FBlueprintLensLC3ValueConeLayoutNode& Node)
				{
					return Node.Kind == EBlueprintLensLC3ValueConeNodeKind::Control;
				});
		ControlNode->Position = FVector2D(Margin, 516.0f);
		Result.CanvasSize = FVector2D(EffectiveWidth, 634.0f);
	}

	Result.DiagnosticCode = Result.HasNoNodeOverlaps()
		? TEXT("LC3_VALUE_CONE_LAYOUT_COMPLETE")
		: TEXT("LC3_VALUE_CONE_LAYOUT_OVERLAP");
	BuildSharedLayoutContract(Result, Projection, EffectiveWidth);
	return Result;
}
