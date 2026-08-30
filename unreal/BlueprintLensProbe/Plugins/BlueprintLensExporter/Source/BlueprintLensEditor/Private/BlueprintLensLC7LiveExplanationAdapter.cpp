#include "BlueprintLensLC7LiveExplanationAdapter.h"

namespace
{
constexpr TCHAR ProfileId[] = TEXT("LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1");

bool IsSCCRelation(const EBlueprintLensRelationKind Kind)
{
	return Kind == EBlueprintLensRelationKind::ExecutionPredecessor ||
		Kind == EBlueprintLensRelationKind::ControlsExecution;
}

const FBlueprintLensSourceReference* PrimarySource(
	const FBlueprintLensUnit& Unit)
{
	return Unit.SourceReferences.FindByPredicate([](const auto& Source)
	{
		return Source.bPrimary && !Source.BlueprintAssetPath.IsEmpty() &&
			!Source.GraphId.IsEmpty() && !Source.SourceNodeId.IsEmpty();
	});
}

struct FComponent
{
	TArray<FString> UnitIds;
};

void StrongConnect(
	const FString& UnitId,
	const TMap<FString, TArray<FString>>& Adjacency,
	int32& NextIndex,
	TMap<FString, int32>& Indices,
	TMap<FString, int32>& LowLinks,
	TArray<FString>& Stack,
	TSet<FString>& OnStack,
	TArray<FComponent>& Components)
{
	Indices.Add(UnitId, NextIndex);
	LowLinks.Add(UnitId, NextIndex);
	++NextIndex;
	Stack.Add(UnitId);
	OnStack.Add(UnitId);

	TArray<FString> Targets = Adjacency.FindRef(UnitId);
	Targets.Sort();
	for (const FString& Target : Targets)
	{
		if (!Indices.Contains(Target))
		{
			StrongConnect(
				Target, Adjacency, NextIndex, Indices, LowLinks,
				Stack, OnStack, Components);
			LowLinks.FindChecked(UnitId) = FMath::Min(
				LowLinks.FindChecked(UnitId), LowLinks.FindChecked(Target));
		}
		else if (OnStack.Contains(Target))
		{
			LowLinks.FindChecked(UnitId) = FMath::Min(
				LowLinks.FindChecked(UnitId), Indices.FindChecked(Target));
		}
	}

	if (LowLinks.FindChecked(UnitId) != Indices.FindChecked(UnitId))
	{
		return;
	}
	FComponent Component;
	while (!Stack.IsEmpty())
	{
		const FString Popped = Stack.Pop();
		OnStack.Remove(Popped);
		Component.UnitIds.Add(Popped);
		if (Popped == UnitId)
		{
			break;
		}
	}
	Component.UnitIds.Sort();
	Components.Add(MoveTemp(Component));
}

void VisitMembers(
	const FString& UnitId,
	const TMap<FString, TArray<FString>>& InternalAdjacency,
	const TSet<FString>& Members,
	TSet<FString>& Visited,
	TArray<FString>& Ordered)
{
	if (Visited.Contains(UnitId))
	{
		return;
	}
	Visited.Add(UnitId);
	Ordered.Add(UnitId);
	TArray<FString> Targets = InternalAdjacency.FindRef(UnitId);
	Targets.Sort();
	for (const FString& Target : Targets)
	{
		if (Members.Contains(Target))
		{
			VisitMembers(Target, InternalAdjacency, Members, Visited, Ordered);
		}
	}
}

FBlueprintLensLC7LiveExplanationAdapterResult Failure(const TCHAR* Code)
{
	FBlueprintLensLC7LiveExplanationAdapterResult Result;
	Result.DiagnosticCode = Code;
	return Result;
}
} // namespace

FBlueprintLensLC7LiveExplanationAdapterResult
FBlueprintLensLC7LiveExplanationAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	using namespace BlueprintLensLC7LiveBounds;
	if (Explanation.Units.IsEmpty() || Explanation.Relations.IsEmpty() ||
		Explanation.Units.Num() > MaxUnits ||
		Explanation.Relations.Num() > MaxRelations ||
		Explanation.Counts.Units != Explanation.Units.Num() ||
		Explanation.Counts.Relations != Explanation.Relations.Num() ||
		Explanation.Source.BlueprintAssetPath.IsEmpty() ||
		Explanation.Source.GraphId.IsEmpty() ||
		Explanation.CriterionUnitId.IsEmpty() ||
		Explanation.FindUnit(Explanation.CriterionUnitId) == nullptr)
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_LEDGER_OR_BOUND_EXCEEDED"));
	}

	TSet<FString> UnitIds;
	TMap<FString, const FBlueprintLensSourceReference*> Sources;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		const FBlueprintLensSourceReference* Source = PrimarySource(Unit);
		if (Unit.Id.IsEmpty() || UnitIds.Contains(Unit.Id) || Source == nullptr ||
			Source->BlueprintAssetPath != Explanation.Source.BlueprintAssetPath ||
			Source->GraphId != Explanation.Source.GraphId)
		{
			return Failure(TEXT("LC7_LIVE_ADAPTER_UNIT_SOURCE_INVALID"));
		}
		UnitIds.Add(Unit.Id);
		Sources.Add(Unit.Id, Source);
	}

	TMap<FString, TArray<FString>> Adjacency;
	TSet<FString> RelationIds;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Id.IsEmpty() || RelationIds.Contains(Relation.Id) ||
			!UnitIds.Contains(Relation.SourceUnitId) ||
			!UnitIds.Contains(Relation.TargetUnitId))
		{
			return Failure(TEXT("LC7_LIVE_ADAPTER_RELATION_INVALID"));
		}
		RelationIds.Add(Relation.Id);
		if (IsSCCRelation(Relation.Kind))
		{
			Adjacency.FindOrAdd(Relation.SourceUnitId).Add(Relation.TargetUnitId);
		}
	}

	TArray<FString> SortedUnitIds = UnitIds.Array();
	SortedUnitIds.Sort();
	int32 NextIndex = 0;
	TMap<FString, int32> Indices;
	TMap<FString, int32> LowLinks;
	TArray<FString> Stack;
	TSet<FString> OnStack;
	TArray<FComponent> Components;
	for (const FString& UnitId : SortedUnitIds)
	{
		if (!Indices.Contains(UnitId))
		{
			StrongConnect(
				UnitId, Adjacency, NextIndex, Indices, LowLinks,
				Stack, OnStack, Components);
		}
	}
	const TArray<FComponent> NonTrivial = Components.FilterByPredicate(
		[](const FComponent& Component)
		{
			return Component.UnitIds.Num() > 1;
		});
	if (NonTrivial.Num() != 1 ||
		NonTrivial[0].UnitIds.Num() > MaxSCCMembers)
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_SCC_COUNT_OR_BOUND_EXCEEDED"));
	}
	const TSet<FString> Members(NonTrivial[0].UnitIds);

	TArray<const FBlueprintLensRelation*> Internal;
	TArray<const FBlueprintLensRelation*> Incoming;
	TArray<const FBlueprintLensRelation*> Outgoing;
	int32 PredicateIncidentCount = 0;
	int32 ValueIncidentCount = 0;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		const bool bSourceMember = Members.Contains(Relation.SourceUnitId);
		const bool bTargetMember = Members.Contains(Relation.TargetUnitId);
		if (IsSCCRelation(Relation.Kind))
		{
			if (bSourceMember && bTargetMember)
			{
				Internal.Add(&Relation);
			}
			else if (!bSourceMember && bTargetMember)
			{
				Incoming.Add(&Relation);
			}
			else if (bSourceMember && !bTargetMember)
			{
				Outgoing.Add(&Relation);
			}
		}
		else if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
			(bSourceMember || bTargetMember))
		{
			++PredicateIncidentCount;
		}
		else if (Relation.Kind == EBlueprintLensRelationKind::ProvidesValue &&
			(bSourceMember || bTargetMember))
		{
			++ValueIncidentCount;
		}
	}
	if (Internal.Num() > MaxInternalExecutionRelations ||
		Internal.Num() < Members.Num() || Incoming.IsEmpty() ||
		Incoming.Num() > MaxIncomingExecutionRelations ||
		Outgoing.Num() > MaxOutgoingExecutionRelations ||
		PredicateIncidentCount > MaxPredicateRelations ||
		ValueIncidentCount > MaxValueRelations)
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_FAMILY_BOUND_EXCEEDED"));
	}
	TSet<FString> EntryTargets;
	for (const FBlueprintLensRelation* Relation : Incoming)
	{
		EntryTargets.Add(Relation->TargetUnitId);
	}
	if (EntryTargets.Num() != 1)
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_ENTRY_NOT_UNIQUE"));
	}

	TMap<FString, TArray<FString>> InternalAdjacency;
	for (const FBlueprintLensRelation* Relation : Internal)
	{
		InternalAdjacency.FindOrAdd(Relation->SourceUnitId).Add(
			Relation->TargetUnitId);
	}
	const FString EntryUnitId = EntryTargets.Array()[0];
	TSet<FString> Visited;
	TArray<FString> OrderedMemberUnitIds;
	VisitMembers(
		EntryUnitId, InternalAdjacency, Members, Visited, OrderedMemberUnitIds);
	if (Visited.Num() != Members.Num())
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_MEMBER_ORDER_INCOMPLETE"));
	}
	TMap<FString, int32> MemberOrder;
	for (int32 Index = 0; Index < OrderedMemberUnitIds.Num(); ++Index)
	{
		MemberOrder.Add(OrderedMemberUnitIds[Index], Index);
	}

	TSharedRef<FBlueprintLensExplanationModel> Adapted =
		MakeShared<FBlueprintLensExplanationModel>(Explanation);
	FBlueprintLensGroup Group;
	Group.Id = TEXT("live.lc7.scc.0");
	Group.Kind = EBlueprintLensGroupKind::Scc;
	Group.Title = TEXT("Static slice execution SCC");
	Group.OrderedUnitIds = OrderedMemberUnitIds;
	Group.EntryUnitId = EntryUnitId;
	Group.MemberCount = Members.Num();
	Group.ProjectionStatus = EBlueprintLensProjectionStatus::StructuralOnly;
	Group.DiagnosticCode = TEXT("LC7_LIVE_EXECUTION_CONTROL_SCC");

	TSharedRef<FBlueprintLensLC7Profile> Profile =
		MakeShared<FBlueprintLensLC7Profile>();
	Profile->bLiveExplanation = true;
	Profile->ProfileId = ProfileId;
	Profile->ClaimScope = TEXT("STATIC_SLICE_SCC_ADAPTATION");
	Profile->RuntimeIterations = TEXT("NOT_CLAIMED");
	Profile->ReadinessStatus = TEXT("LIVE_EXPLANATION_STRUCTURAL_ADAPTATION");
	Profile->RelationFamilyStatement =
		TEXT("SCC relation family · execution_predecessor + controls_execution.");
	Profile->BlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	Profile->GraphId = Explanation.Source.GraphId;
	Profile->AssetSha256 = Explanation.Source.BlueprintPackageSha256;
	Profile->CriterionUnitId = Explanation.CriterionUnitId;
	Profile->CriterionNodeId = Explanation.Query.CriterionSourceNodeId;
	Profile->SourceNodeCount = Explanation.Counts.SourceNodes > 0
		? Explanation.Counts.SourceNodes : Explanation.Units.Num();
	Profile->SourceEdgeCount = Explanation.Counts.SourceEdges > 0
		? Explanation.Counts.SourceEdges : Explanation.Relations.Num();
	Profile->ExplanationUnitCount = Explanation.Units.Num();
	Profile->ExplanationRelationCount = Explanation.Relations.Num();
	Profile->StructuralSCCCount = 1;
	Profile->SCC.GroupId = Group.Id;
	Profile->SCC.EntryUnitId = EntryUnitId;
	Profile->SCC.EntryNodeId = Sources.FindChecked(EntryUnitId)->SourceNodeId;
	Profile->SCC.OrderedMemberUnitIds = OrderedMemberUnitIds;
	for (const FString& MemberUnitId : OrderedMemberUnitIds)
	{
		Profile->SCC.MemberNodeIds.Add(
			Sources.FindChecked(MemberUnitId)->SourceNodeId);
	}

	auto AddEdgeId = [](TArray<FString>& Target, const FBlueprintLensRelation& Relation)
	{
		Target.Add(Relation.SourceEdgeIds[0]);
	};
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.SourceEdgeIds.Num() != 1 ||
			!Relation.bHasSourceEdgeEndpoints ||
			Relation.SourceEdgeEndpoints.Num() != 1 ||
			Relation.SourceEdgeEndpoints[0].SourceEdgeId != Relation.SourceEdgeIds[0])
		{
			return Failure(TEXT("LC7_LIVE_ADAPTER_SOURCE_EDGE_BINDING_INVALID"));
		}
		const FBlueprintLensSourceEdgeEndpoint& Endpoint =
			Relation.SourceEdgeEndpoints[0];
		if (Endpoint.SourceNodeId !=
				Sources.FindChecked(Relation.SourceUnitId)->SourceNodeId ||
			Endpoint.TargetNodeId !=
				Sources.FindChecked(Relation.TargetUnitId)->SourceNodeId ||
			Endpoint.SourcePinId.IsEmpty() || Endpoint.TargetPinId.IsEmpty())
		{
			return Failure(TEXT("LC7_LIVE_ADAPTER_SOURCE_ENDPOINT_INVALID"));
		}
		FBlueprintLensLC7RelationBinding Binding;
		Binding.RelationId = Relation.Id;
		Binding.SourceEdgeId = Relation.SourceEdgeIds[0];
		Binding.SourceUnitId = Relation.SourceUnitId;
		Binding.TargetUnitId = Relation.TargetUnitId;
		Binding.SourceNodeId = Endpoint.SourceNodeId;
		Binding.TargetNodeId = Endpoint.TargetNodeId;
		Binding.SourcePinId = Endpoint.SourcePinId;
		Binding.TargetPinId = Endpoint.TargetPinId;
		if (IsSCCRelation(Relation.Kind) &&
			Members.Contains(Relation.SourceUnitId) &&
			Members.Contains(Relation.TargetUnitId))
		{
			AddEdgeId(Profile->SCC.InternalEdgeIds, Relation);
			Group.OrderedRelationIds.Add(Relation.Id);
			Binding.bReturning =
				MemberOrder.FindChecked(Relation.TargetUnitId) <=
				MemberOrder.FindChecked(Relation.SourceUnitId);
			if (Binding.bReturning)
			{
				AddEdgeId(Profile->SCC.ReturningEdgeIds, Relation);
			}
		}
		else if (IsSCCRelation(Relation.Kind) &&
			!Members.Contains(Relation.SourceUnitId) &&
			Members.Contains(Relation.TargetUnitId))
		{
			AddEdgeId(Profile->SCC.IncomingEdgeIds, Relation);
		}
		else if (IsSCCRelation(Relation.Kind) &&
			Members.Contains(Relation.SourceUnitId) &&
			!Members.Contains(Relation.TargetUnitId))
		{
			AddEdgeId(Profile->SCC.OutgoingEdgeIds, Relation);
		}
		Profile->Relations.Add(MoveTemp(Binding));
	}
	Profile->SCC.OrderedRelationIds = Group.OrderedRelationIds;
	Profile->bExitOutsideSlice = Profile->SCC.OutgoingEdgeIds.IsEmpty();
	if (Profile->bExitOutsideSlice)
	{
		Profile->ExitBoundaryStatement =
			TEXT("Exit boundary · No outgoing relation in that family is present in "
				"the static slice; the SCC exit lies outside this static slice.");
	}
	else
	{
		const FBlueprintLensRelation* ExitRelation = Outgoing[0];
		Profile->SCC.ExitUnitId = ExitRelation->SourceUnitId;
		Profile->SCC.ExitNodeId =
			Sources.FindChecked(ExitRelation->SourceUnitId)->SourceNodeId;
		Group.bHasExitUnitId = true;
		Group.ExitUnitId = ExitRelation->SourceUnitId;
		Profile->ExitBoundaryStatement =
			TEXT("Exit boundary · One outgoing relation in that family is present "
				"in this static slice.");
	}
	Adapted->bHasGroups = true;
	Adapted->Groups.Add(MoveTemp(Group));
	Profile->ExplanationModel = Adapted;
	if (!Profile->IsValid())
	{
		return Failure(TEXT("LC7_LIVE_ADAPTER_PROFILE_INVALID"));
	}
	FBlueprintLensLC7LiveExplanationAdapterResult Result;
	Result.Profile = Profile;
	Result.DiagnosticCode = TEXT("LC7_LIVE_ADAPTER_COMPLETE");
	return Result;
}
