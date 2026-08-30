// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6NativeGraphBridge.h"

#include "BlueprintEditor.h"
#include "BlueprintLensM6GraphResolver.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GraphEditor.h"
#include "Kismet2/KismetEditorUtilities.h"

FM6NativeGraphBridge::FM6NativeGraphBridge(
	TWeakPtr<FBlueprintEditor> InBlueprintEditor)
	: BlueprintEditor(MoveTemp(InBlueprintEditor))
{
}
FM6NativeGraphResult FM6NativeGraphBridge::Fail(
	const TCHAR* Code,
	const TCHAR* Message) const
{
	FM6Error Error;
	Error.Code = Code;
	Error.Phase = TEXT("view");
	Error.Message = Message;
	Error.bRetryable = false;
	return MakeError(MoveTemp(Error));
}

FM6NativeGraphResult FM6NativeGraphBridge::ApplyMembershipHighlight(
	const FM6BaselineFacts& Facts)
{
	const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (Blueprint == nullptr)
		return Fail(TEXT("M6_VIEW_SELECTION_SYNC_FAILED"), TEXT("the Blueprint editor is unavailable"));
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* ResolvedGraph = nullptr;
	for (UEdGraph* Candidate : Graphs)
	{
		if (Candidate != nullptr && Candidate->GetPathName() == Facts.GraphId)
		{
			if (ResolvedGraph != nullptr)
				return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the source graph identity is ambiguous"));
			ResolvedGraph = Candidate;
		}
	}
	if (ResolvedGraph == nullptr)
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the source graph cannot be resolved"));

	TMap<FString, TWeakObjectPtr<UEdGraphNode>> NewMemberNodes;
	TSet<FString> NewMemberEntityIds;
	for (const FM6BaselineEntity& Entity : Facts.Entities)
	{
		FGuid Guid;
		if (!FGuid::Parse(Entity.Source.NativeNodeGuid, Guid))
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("a native node identity is invalid"));
		UEdGraphNode* Node = nullptr;
		for (UEdGraphNode* Candidate : ResolvedGraph->Nodes)
		{
			if (Candidate != nullptr && Candidate->NodeGuid == Guid)
			{
				if (Node != nullptr)
					return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("a native node identity is ambiguous"));
				Node = Candidate;
			}
		}
		if (Node == nullptr || Entity.Source.NodeId != Entity.Id ||
			NewMemberEntityIds.Contains(Entity.Id))
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("a session entity has no native node"));
		NewMemberNodes.Add(Entity.Id, Node);
		NewMemberEntityIds.Add(Entity.Id);
	}
	const TSharedPtr<SGraphEditor> NewGraphEditor =
		Editor->OpenGraphAndBringToFront(ResolvedGraph, true);
	if (!NewGraphEditor.IsValid())
		return Fail(TEXT("M6_VIEW_SELECTION_SYNC_FAILED"), TEXT("the native graph surface cannot be opened"));

	// Commit only after the complete candidate has resolved.  A failed
	// replacement must not clear or partially rewrite the previous A highlight.
	if (const TSharedPtr<SGraphEditor> PreviousEditor = GraphEditor.Pin())
	{
		bApplyingSelection = true;
		PreviousEditor->ClearSelectionSet();
		bApplyingSelection = false;
	}
	Graph = ResolvedGraph;
	GraphEditor = NewGraphEditor;
	MemberNodes = MoveTemp(NewMemberNodes);
	MemberEntityIds = MoveTemp(NewMemberEntityIds);
	LastObservedEntityId.Reset();
	bRestoreMembershipNextTick = false;
#if WITH_DEV_AUTOMATION_TESTS
	AutomationGraphId.Reset();
	AutomationFullEntityIds.Reset();
	AutomationAppliedSelection.Reset();
	AutomationEntityLabels.Reset();
	AutomationExecutionEntityIds.Reset();
	PendingVisibleReviewNode.Reset();
	PendingVisibleReviewDelayTicks = 0;
#endif
	RestoreMembership();
	return MakeValue(true);
}

TOptional<FM6NativeSelectionObservation>
FM6NativeGraphBridge::ObserveNativeSelection()
{
	const TSharedPtr<FBlueprintEditor> Blueprint = BlueprintEditor.Pin();
	if (bApplyingSelection || !Blueprint.IsValid()) return {};
	const FM6GraphResolution Resolution = FM6GraphResolver::Resolve(Blueprint);
	UEdGraph* ObservedGraph = Resolution.Graph;
	TSharedPtr<SGraphEditor> ObservedEditor = Resolution.GraphEditor;
	if (!ObservedEditor.IsValid() && ObservedGraph != nullptr &&
		ObservedGraph != Graph.Get())
		ObservedEditor = Blueprint->OpenGraphAndBringToFront(ObservedGraph, true);
	else if (ObservedGraph == nullptr || !ObservedEditor.IsValid())
	{
		ObservedGraph = Graph.Get();
		ObservedEditor = GraphEditor.Pin();
	}
	if (ObservedGraph == nullptr || !ObservedEditor.IsValid()) return {};
	FGraphPanelSelectionSet Selected = ObservedEditor->GetSelectedNodes();
	if (Selected.IsEmpty())
	{
		Selected = Blueprint->GetSelectedNodes();
	}
	if (Selected.IsEmpty())
	{
		LastObservedEntityId.Reset();
		return {};
	}
	TSet<UEdGraphNode*> MemberSelection;
	if (ObservedGraph == Graph.Get())
	{
		for (const auto& Pair : MemberNodes)
		{
			if (Pair.Value.IsValid()) MemberSelection.Add(Pair.Value.Get());
		}
	}
	bool bExactMembership = Selected.Num() == MemberSelection.Num();
	if (bExactMembership)
	{
		for (UEdGraphNode* Node : MemberSelection)
		{
			if (!Selected.Contains(Node)) { bExactMembership = false; break; }
		}
	}
	if (bExactMembership)
	{
		LastObservedEntityId.Reset();
		return {};
	}

	UEdGraphNode* Chosen = nullptr;
	for (UObject* Object : Selected)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
		{
			Chosen = Node;
			if (!MemberSelection.Contains(Node)) break;
		}
	}
	if (Chosen == nullptr)
	{
		LastObservedEntityId.Reset();
		return {};
	}
	FM6NativeSelectionObservation Observation;
	Observation.EntityId = StableEntityId(*Chosen);
	Observation.GraphId = Chosen->GetGraph() != nullptr
		? Chosen->GetGraph()->GetPathName()
		: FString();
	Observation.Label =
		Chosen->GetNodeTitle(ENodeTitleType::ListView).ToString();
	Observation.bHasExecutionPin = false;
	for (const UEdGraphPin* Pin : Chosen->Pins)
	{
		if (Pin != nullptr &&
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			Observation.bHasExecutionPin = true;
			break;
		}
	}
	Observation.bOutsideCurrentSession = !MemberEntityIds.Contains(Observation.EntityId);
	if (Observation.EntityId == LastObservedEntityId) return {};
	LastObservedEntityId = Observation.EntityId;
	if (SelectionObserver) SelectionObserver(Observation);
	return Observation;
}

FM6NativeGraphResult FM6NativeGraphBridge::FocusSemanticEntity(
	const FString& EntityId)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!AutomationFullEntityIds.IsEmpty())
	{
		if (!MemberEntityIds.Contains(EntityId))
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the entity is outside the current session"));
		AutomationAppliedSelection = {EntityId};
		LastObservedEntityId = EntityId;
		bRestoreMembershipNextTick = true;
		return MakeValue(true);
	}
#endif
	const TWeakObjectPtr<UEdGraphNode>* Node = MemberNodes.Find(EntityId);
	const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	if (Node == nullptr || !Node->IsValid() || !Editor.IsValid())
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the semantic entity has no live native node"));
	const TSharedPtr<SGraphEditor> Native =
		Editor->OpenGraphAndBringToFront(Node->Get()->GetGraph(), true);
	if (!Native.IsValid())
		return Fail(TEXT("M6_VIEW_SELECTION_SYNC_FAILED"), TEXT("the native graph surface cannot be focused"));
	bApplyingSelection = true;
	Native->ClearSelectionSet();
	Native->SetNodeSelection(Node->Get(), true);
	bApplyingSelection = false;
	GraphEditor = Native;
	LastObservedEntityId = EntityId;
	bRestoreMembershipNextTick = true;
	return MakeValue(true);
}

void FM6NativeGraphBridge::Tick()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (PendingVisibleReviewNode.IsValid())
	{
		if (PendingVisibleReviewDelayTicks > 0)
		{
			--PendingVisibleReviewDelayTicks;
			return;
		}
		const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
		UEdGraphNode* Node = PendingVisibleReviewNode.Get();
		const TSharedPtr<SGraphEditor> Native = Editor.IsValid() && Node != nullptr
			? Editor->OpenGraphAndBringToFront(Node->GetGraph(), true)
			: nullptr;
		if (Native.IsValid())
		{
			bApplyingSelection = true;
			Native->ClearSelectionSet();
			Native->SetNodeSelection(Node, true);
			bApplyingSelection = false;
			const FGraphPanelSelectionSet ReviewSelection =
				Native->GetSelectedNodes();
			if (ReviewSelection.Contains(Node))
			{
				FM6NativeSelectionObservation Observation;
				Observation.EntityId = StableEntityId(*Node);
				Observation.GraphId = Node->GetGraph() != nullptr
					? Node->GetGraph()->GetPathName()
					: FString();
				Observation.Label =
					Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
				Observation.bHasExecutionPin = false;
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin != nullptr &&
						Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						Observation.bHasExecutionPin = true;
						break;
					}
				}
				Observation.bOutsideCurrentSession =
					!MemberEntityIds.Contains(Observation.EntityId);
				LastObservedEntityId = Observation.EntityId;
				if (SelectionObserver) SelectionObserver(Observation);
			}
		}
		PendingVisibleReviewNode.Reset();
		return;
	}
#endif
	if (bRestoreMembershipNextTick) RestoreMembership();
}

void FM6NativeGraphBridge::Clear()
{
	if (const TSharedPtr<SGraphEditor> Editor = GraphEditor.Pin())
	{
		bApplyingSelection = true;
		Editor->ClearSelectionSet();
		bApplyingSelection = false;
	}
	GraphEditor.Reset();
	Graph.Reset();
	MemberNodes.Reset();
	MemberEntityIds.Reset();
	LastObservedEntityId.Reset();
	bRestoreMembershipNextTick = false;
#if WITH_DEV_AUTOMATION_TESTS
	AutomationGraphId.Reset();
	AutomationFullEntityIds.Reset();
	AutomationAppliedSelection.Reset();
	AutomationEntityLabels.Reset();
	AutomationExecutionEntityIds.Reset();
	PendingVisibleReviewNode.Reset();
	PendingVisibleReviewDelayTicks = 0;
#endif
}

void FM6NativeGraphBridge::SetSelectionObserver(
	TFunction<void(const FM6NativeSelectionObservation&)> InObserver)
{
	SelectionObserver = MoveTemp(InObserver);
}

void FM6NativeGraphBridge::RestoreMembership()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!AutomationFullEntityIds.IsEmpty())
	{
		AutomationAppliedSelection = MemberEntityIds;
		bRestoreMembershipNextTick = false;
		return;
	}
#endif
	const TSharedPtr<SGraphEditor> Editor = GraphEditor.Pin();
	if (!Editor.IsValid()) return;
	bApplyingSelection = true;
	Editor->ClearSelectionSet();
	for (const auto& Pair : MemberNodes)
	{
		if (Pair.Value.IsValid()) Editor->SetNodeSelection(Pair.Value.Get(), true);
	}
	bApplyingSelection = false;
	bRestoreMembershipNextTick = false;
}

FString FM6NativeGraphBridge::StableEntityId(const UEdGraphNode& Node) const
{
	for (const auto& Pair : MemberNodes)
	{
		if (Pair.Value.Get() == &Node) return Pair.Key;
	}
	return FString::Printf(
		TEXT("%s::node::%s"),
		*Node.GetGraph()->GetPathName(),
		*Node.NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
}

#if WITH_DEV_AUTOMATION_TESTS
FM6NativeGraphResult FM6NativeGraphBridge::SelectOutsideEntityForVisibleReview(
	const FString& EntityId)
{
	if (EntityId.IsEmpty() || MemberEntityIds.Contains(EntityId))
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the review entity is not outside the current session"));
	return QueueVisibleReviewSelection(EntityId);
}

FM6NativeGraphResult FM6NativeGraphBridge::SelectEntityForVisibleReview(
	const FString& EntityId)
{
	if (EntityId.IsEmpty())
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the review entity is empty"));
	return QueueVisibleReviewSelection(EntityId);
}

FM6NativeGraphResult FM6NativeGraphBridge::QueueVisibleReviewSelection(
	const FString& EntityId)
{
	const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (Blueprint == nullptr)
		return Fail(TEXT("M6_VIEW_SELECTION_SYNC_FAILED"), TEXT("the Blueprint editor is unavailable"));
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraphNode* Resolved = nullptr;
	for (UEdGraph* CandidateGraph : Graphs)
	{
		if (CandidateGraph == nullptr) continue;
		for (UEdGraphNode* CandidateNode : CandidateGraph->Nodes)
		{
			if (CandidateNode == nullptr || StableEntityId(*CandidateNode) != EntityId) continue;
			if (Resolved != nullptr)
				return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the review entity identity is ambiguous"));
			Resolved = CandidateNode;
		}
	}
	if (Resolved == nullptr)
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("the review entity cannot be resolved"));
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved, false);
	const TSharedPtr<SGraphEditor> Native =
		Editor->OpenGraphAndBringToFront(Resolved->GetGraph(), true);
	if (!Native.IsValid())
		return Fail(TEXT("M6_VIEW_SELECTION_SYNC_FAILED"), TEXT("the review graph surface cannot be focused"));
	PendingVisibleReviewNode = Resolved;
	PendingVisibleReviewDelayTicks = 1;
	return MakeValue(true);
}

bool FM6NativeGraphBridge::ApplyMembershipForAutomationTest(
	const FM6BaselineFacts& Facts,
	const TArray<FString>& FullEntityIds,
	FM6Error& OutError)
{
	OutError = FM6Error();
	TSet<FString> NewFullEntityIds;
	TSet<FString> NewMemberEntityIds;
	TMap<FString, FString> NewEntityLabels;
	TSet<FString> NewExecutionEntityIds;
	for (const FString& Id : FullEntityIds) NewFullEntityIds.Add(Id);
	for (const FM6BaselineEntity& Entity : Facts.Entities)
	{
		if (!NewFullEntityIds.Contains(Entity.Id) ||
			NewMemberEntityIds.Contains(Entity.Id))
		{
			OutError.Code = TEXT("M6_VIEW_ENTITY_UNMAPPED");
			OutError.Phase = TEXT("view");
			OutError.Message = TEXT("automation membership is invalid");
			return false;
		}
		NewMemberEntityIds.Add(Entity.Id);
		NewEntityLabels.Add(Entity.Id, Entity.Label);
	}
	for (const FM6BaselineRelation& Relation : Facts.Relations)
	{
		if (!Relation.Kind.StartsWith(TEXT("execution"))) continue;
		if (NewMemberEntityIds.Contains(Relation.SourceEntityId))
			NewExecutionEntityIds.Add(Relation.SourceEntityId);
		if (NewMemberEntityIds.Contains(Relation.TargetEntityId))
			NewExecutionEntityIds.Add(Relation.TargetEntityId);
	}
	AutomationGraphId = Facts.GraphId;
	AutomationFullEntityIds = MoveTemp(NewFullEntityIds);
	MemberEntityIds = MoveTemp(NewMemberEntityIds);
	AutomationAppliedSelection = MemberEntityIds;
	AutomationEntityLabels = MoveTemp(NewEntityLabels);
	AutomationExecutionEntityIds = MoveTemp(NewExecutionEntityIds);
	LastObservedEntityId.Reset();
	bRestoreMembershipNextTick = false;
	return true;
}

FM6NativeSelectionObservation
FM6NativeGraphBridge::ObserveSelectionForAutomationTest(
	const TArray<FString>& SelectedEntityIds)
{
	FM6NativeSelectionObservation Observation;
	if (SelectedEntityIds.IsEmpty())
	{
		LastObservedEntityId.Reset();
		return Observation;
	}
	Observation.EntityId = SelectedEntityIds[0];
	Observation.GraphId = AutomationGraphId;
	Observation.Label = AutomationEntityLabels.FindRef(Observation.EntityId);
	Observation.bHasExecutionPin =
		AutomationExecutionEntityIds.Contains(Observation.EntityId);
	Observation.bOutsideCurrentSession =
		!MemberEntityIds.Contains(Observation.EntityId);
	AutomationAppliedSelection.Reset();
	for (const FString& Id : SelectedEntityIds) AutomationAppliedSelection.Add(Id);
	LastObservedEntityId = Observation.EntityId;
	if (SelectionObserver) SelectionObserver(Observation);
	return Observation;
}

void FM6NativeGraphBridge::TickForAutomationTest()
{
	Tick();
}
#endif
