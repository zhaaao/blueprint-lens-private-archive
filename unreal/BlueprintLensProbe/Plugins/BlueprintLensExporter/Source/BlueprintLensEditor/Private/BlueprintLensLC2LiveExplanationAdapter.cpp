#include "BlueprintLensLC2LiveExplanationAdapter.h"

namespace
{
struct FLiveGuardBranch
{
	const FBlueprintLensRelation* Predicate = nullptr;
	const FBlueprintLensRelation* TrueExit = nullptr;
	const FBlueprintLensRelation* FalseExit = nullptr;
	FString ReaderText;
};

FBlueprintLensLC2LiveExplanationAdapterResult Fail(
	const FBlueprintLensExplanationModel& Explanation,
	const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC2LiveExplanationAdapterResult Result;
	Result.InputUnitCount = Explanation.Units.Num();
	Result.InputRelationCount = Explanation.Relations.Num();
	Result.Explanation = Explanation;
	Result.DiagnosticCode = DiagnosticCode;
	return Result;
}

FString PredicateReaderText(const FBlueprintLensRelation& Relation)
{
	if (Relation.bHasPortLabel && !Relation.PortLabel.IsEmpty())
	{
		return Relation.PortLabel;
	}
	if (Relation.bHasSourceEdgeEndpoints &&
		Relation.SourceEdgeEndpoints.Num() == 1)
	{
		return Relation.SourceEdgeEndpoints[0].SourcePortLabel;
	}
	return FString();
}

const FBlueprintLensRelation* FindUniqueRelation(
	const FBlueprintLensExplanationModel& Explanation,
	TFunctionRef<bool(const FBlueprintLensRelation&)> Predicate)
{
	const FBlueprintLensRelation* Found = nullptr;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!Predicate(Relation))
		{
			continue;
		}
		if (Found != nullptr)
		{
			return nullptr;
		}
		Found = &Relation;
	}
	return Found;
}

FBlueprintLensGroup MakeOutcomePath(
	const FString& Id,
	const FString& Title,
	const TArray<FString>& UnitIds,
	const TArray<FString>& RelationIds)
{
	FBlueprintLensGroup Group;
	Group.Id = Id;
	Group.Kind = EBlueprintLensGroupKind::OutcomePath;
	Group.Title = Title;
	Group.OrderedUnitIds = UnitIds;
	Group.OrderedRelationIds = RelationIds;
	Group.EntryUnitId = UnitIds[0];
	Group.ExitUnitId = UnitIds.Last();
	Group.bHasExitUnitId = true;
	Group.MemberCount = UnitIds.Num();
	Group.ProjectionStatus = EBlueprintLensProjectionStatus::Complete;
	return Group;
}

FBlueprintLensGroup MakeGuardNest(
	const FString& Id,
	const FString& Title,
	const TArray<FString>& UnitIds,
	const TArray<FString>& RelationIds,
	const FString& ParentId,
	const bool bHasEnteredBy,
	const EBlueprintLensSemanticLabel EnteredBy)
{
	FBlueprintLensGroup Group;
	Group.Id = Id;
	Group.Kind = EBlueprintLensGroupKind::GuardNest;
	Group.Title = Title;
	Group.OrderedUnitIds = UnitIds;
	Group.OrderedRelationIds = RelationIds;
	Group.EntryUnitId = UnitIds[0];
	Group.bHasParent = !ParentId.IsEmpty();
	Group.ParentGroupId = ParentId;
	Group.bHasEnteredBy = bHasEnteredBy;
	Group.EnteredBy = EnteredBy;
	Group.MemberCount = UnitIds.Num();
	Group.ProjectionStatus = EBlueprintLensProjectionStatus::Complete;
	return Group;
}
} // namespace

FBlueprintLensLC2LiveExplanationAdapterResult
FBlueprintLensLC2LiveExplanationAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	if (!Explanation.Groups.IsEmpty() ||
		Explanation.bHasGroupPartialOrder ||
		Explanation.FindUnit(Explanation.CriterionUnitId) == nullptr)
	{
		return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_INPUT_UNAVAILABLE"));
	}

	TMap<FString, FLiveGuardBranch> Guards;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Kind != EBlueprintLensRelationKind::PredicateFor)
		{
			continue;
		}
		const FBlueprintLensUnit* Predicate =
			Explanation.FindUnit(Relation.SourceUnitId);
		const FBlueprintLensUnit* Branch =
			Explanation.FindUnit(Relation.TargetUnitId);
		const FString ReaderText = PredicateReaderText(Relation);
		if (Predicate == nullptr || Branch == nullptr ||
			Predicate->Role != EBlueprintLensRole::Predicate ||
			ReaderText.IsEmpty() || Guards.Contains(Relation.TargetUnitId))
		{
			return Fail(
				Explanation,
				TEXT("LC2_LIVE_ADAPTER_PREDICATE_BINDING_INVALID"));
		}
		FLiveGuardBranch Guard;
		Guard.Predicate = &Relation;
		Guard.ReaderText = ReaderText;
		Guards.Add(Relation.TargetUnitId, MoveTemp(Guard));
	}
	if (Guards.Num() != 2)
	{
		return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_GUARD_COUNT_INVALID"));
	}

	for (TPair<FString, FLiveGuardBranch>& Pair : Guards)
	{
		for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		{
			if (Relation.Kind != EBlueprintLensRelationKind::ControlsExecution ||
				Relation.SourceUnitId != Pair.Key || !Relation.bHasSemanticLabel)
			{
				continue;
			}
			const FBlueprintLensRelation** Target = nullptr;
			if (Relation.SemanticLabel == EBlueprintLensSemanticLabel::ConditionTrue)
			{
				Target = &Pair.Value.TrueExit;
			}
			else if (Relation.SemanticLabel ==
				EBlueprintLensSemanticLabel::ConditionFalse)
			{
				Target = &Pair.Value.FalseExit;
			}
			if (Target == nullptr || *Target != nullptr)
			{
				return Fail(
					Explanation,
					TEXT("LC2_LIVE_ADAPTER_BRANCH_EXIT_INVALID"));
			}
			*Target = &Relation;
		}
		if (Pair.Value.TrueExit == nullptr || Pair.Value.FalseExit == nullptr)
		{
			return Fail(
				Explanation,
				TEXT("LC2_LIVE_ADAPTER_BRANCH_EXIT_MISSING"));
		}
	}

	FString RootBranchId;
	FString NestedBranchId;
	const FBlueprintLensRelation* EnterNested = nullptr;
	for (const TPair<FString, FLiveGuardBranch>& Pair : Guards)
	{
		for (const FBlueprintLensRelation* Exit :
			{Pair.Value.TrueExit, Pair.Value.FalseExit})
		{
			if (Exit != nullptr && Guards.Contains(Exit->TargetUnitId))
			{
				if (EnterNested != nullptr)
				{
					return Fail(
						Explanation,
						TEXT("LC2_LIVE_ADAPTER_NESTING_AMBIGUOUS"));
				}
				RootBranchId = Pair.Key;
				NestedBranchId = Exit->TargetUnitId;
				EnterNested = Exit;
			}
		}
	}
	if (EnterNested == nullptr || RootBranchId.IsEmpty() ||
		NestedBranchId.IsEmpty() ||
		EnterNested->SemanticLabel != EBlueprintLensSemanticLabel::ConditionTrue)
	{
		return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_NESTING_INVALID"));
	}

	const FLiveGuardBranch& Root = Guards.FindChecked(RootBranchId);
	const FLiveGuardBranch& Nested = Guards.FindChecked(NestedBranchId);
	const FBlueprintLensRelation* RootOutcome = Root.FalseExit;
	if (RootOutcome == nullptr || RootOutcome->TargetUnitId == NestedBranchId)
	{
		return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_ROOT_OUTCOME_INVALID"));
	}
	const FBlueprintLensRelation* Entry = FindUniqueRelation(
		Explanation,
		[&RootBranchId](const FBlueprintLensRelation& Relation)
		{
			return Relation.Kind ==
					EBlueprintLensRelationKind::ExecutionPredecessor &&
				Relation.TargetUnitId == RootBranchId;
		});
	if (Entry == nullptr)
	{
		return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_ENTRY_INVALID"));
	}

	const TArray<const FBlueprintLensRelation*> OutcomeExits = {
		RootOutcome, Nested.FalseExit, Nested.TrueExit};
	TSet<FString> OutcomeUnitIds;
	TArray<const FBlueprintLensRelation*> Reconvergences;
	for (const FBlueprintLensRelation* Exit : OutcomeExits)
	{
		if (Exit == nullptr || Guards.Contains(Exit->TargetUnitId) ||
			Explanation.FindUnit(Exit->TargetUnitId) == nullptr ||
			OutcomeUnitIds.Contains(Exit->TargetUnitId))
		{
			return Fail(Explanation, TEXT("LC2_LIVE_ADAPTER_OUTCOME_INVALID"));
		}
		OutcomeUnitIds.Add(Exit->TargetUnitId);
		const FBlueprintLensRelation* Reconvergence = FindUniqueRelation(
			Explanation,
			[&Explanation, &Exit](const FBlueprintLensRelation& Relation)
			{
				return Relation.Kind ==
						EBlueprintLensRelationKind::ExecutionPredecessor &&
					Relation.SourceUnitId == Exit->TargetUnitId &&
					Relation.TargetUnitId == Explanation.CriterionUnitId;
			});
		if (Reconvergence == nullptr)
		{
			return Fail(
				Explanation,
				TEXT("LC2_LIVE_ADAPTER_RECONVERGENCE_MISSING"));
		}
		Reconvergences.Add(Reconvergence);
	}

	const FString EntryUnitId = Entry->SourceUnitId;
	const FString OuterOutcomeUnitId = RootOutcome->TargetUnitId;
	const FString InnerOutcomeUnitId = Nested.FalseExit->TargetUnitId;
	const FString PassedOutcomeUnitId = Nested.TrueExit->TargetUnitId;
	const TArray<FString> ExpectedCoreUnitIds = {
		EntryUnitId,
		RootBranchId,
		NestedBranchId,
		OuterOutcomeUnitId,
		InnerOutcomeUnitId,
		PassedOutcomeUnitId,
		Root.Predicate->SourceUnitId,
		Nested.Predicate->SourceUnitId,
		Explanation.CriterionUnitId};
	TSet<FString> CoreUnitIds;
	for (const FString& UnitId : ExpectedCoreUnitIds)
	{
		if (UnitId.IsEmpty() || Explanation.FindUnit(UnitId) == nullptr)
		{
			return Fail(
				Explanation,
				TEXT("LC2_LIVE_ADAPTER_CORE_COVER_INVALID"));
		}
		CoreUnitIds.Add(UnitId);
	}
	if (CoreUnitIds.Num() != ExpectedCoreUnitIds.Num())
	{
		return Fail(
			Explanation,
			TEXT("LC2_LIVE_ADAPTER_CORE_COVER_INVALID"));
	}

	const TArray<const FBlueprintLensRelation*> ExpectedCoreRelations = {
		Entry,
		Root.Predicate,
		Nested.Predicate,
		Root.TrueExit,
		Root.FalseExit,
		Nested.TrueExit,
		Nested.FalseExit,
		Reconvergences[0],
		Reconvergences[1],
		Reconvergences[2]};
	TSet<FString> CoreRelationIds;
	for (const FBlueprintLensRelation* Relation : ExpectedCoreRelations)
	{
		if (Relation == nullptr || Relation->Id.IsEmpty() ||
			!CoreUnitIds.Contains(Relation->SourceUnitId) ||
			!CoreUnitIds.Contains(Relation->TargetUnitId))
		{
			return Fail(
				Explanation,
				TEXT("LC2_LIVE_ADAPTER_CORE_COVER_INVALID"));
		}
		CoreRelationIds.Add(Relation->Id);
	}
	if (CoreRelationIds.Num() != ExpectedCoreRelations.Num())
	{
		return Fail(
			Explanation,
			TEXT("LC2_LIVE_ADAPTER_CORE_COVER_INVALID"));
	}

	FBlueprintLensLC2LiveExplanationAdapterResult Result;
	Result.InputUnitCount = Explanation.Units.Num();
	Result.InputRelationCount = Explanation.Relations.Num();
	Result.Explanation = Explanation;
	Result.Explanation.Units.RemoveAll(
		[&CoreUnitIds](const FBlueprintLensUnit& Unit)
		{
			return !CoreUnitIds.Contains(Unit.Id);
		});
	Result.Explanation.Relations.RemoveAll(
		[&CoreRelationIds](const FBlueprintLensRelation& Relation)
		{
			return !CoreRelationIds.Contains(Relation.Id);
		});
	for (FBlueprintLensLane& Lane : Result.Explanation.Lanes)
	{
		Lane.UnitIds.RemoveAll(
			[&CoreUnitIds](const FString& UnitId)
			{
				return !CoreUnitIds.Contains(UnitId);
			});
		if (Lane.UnitIds.IsEmpty())
		{
			Lane.State = EBlueprintLensLaneState::Empty;
			Lane.EmptyMessage = TEXT("No units from the adapted LC2 core.");
		}
	}
	Result.Explanation.Counts.Lanes = Result.Explanation.Lanes.Num();
	Result.Explanation.Counts.Units = Result.Explanation.Units.Num();
	Result.Explanation.Counts.Relations = Result.Explanation.Relations.Num();
	for (FBlueprintLensUnit& Unit : Result.Explanation.Units)
	{
		const FLiveGuardBranch* Guard = Guards.Find(Unit.Id);
		if (Guard == nullptr)
		{
			continue;
		}
		Unit.bHasDisambiguator = true;
		Unit.Disambiguator.Text = Guard->ReaderText;
		Unit.Disambiguator.RuleId = TEXT("lc2_live_predicate_for");
		Unit.Disambiguator.EvidenceRelationIds = {Guard->Predicate->Id};
	}

	const FString OuterGuardId = TEXT("group.guard_nest.outer_guard");
	const FString InnerGuardId = TEXT("group.guard_nest.inner_guard");
	const FString OuterOutcomeId =
		TEXT("group.outcome_path.outer_rejected");
	const FString InnerOutcomeId =
		TEXT("group.outcome_path.inner_rejected");
	const FString PassedOutcomeId = TEXT("group.outcome_path.accepted");
	Result.Explanation.Groups = {
		MakeOutcomePath(
			OuterOutcomeId,
			Root.ReaderText + TEXT(" was false"),
			{EntryUnitId, RootBranchId, OuterOutcomeUnitId},
			{Entry->Id, RootOutcome->Id}),
		MakeOutcomePath(
			InnerOutcomeId,
			Nested.ReaderText + TEXT(" was false"),
			{EntryUnitId, RootBranchId, NestedBranchId, InnerOutcomeUnitId},
			{Entry->Id, EnterNested->Id, Nested.FalseExit->Id}),
		MakeOutcomePath(
			PassedOutcomeId,
			TEXT("Both guard conditions passed"),
			{EntryUnitId, RootBranchId, NestedBranchId, PassedOutcomeUnitId},
			{Entry->Id, EnterNested->Id, Nested.TrueExit->Id}),
		MakeGuardNest(
			OuterGuardId,
			Root.ReaderText + TEXT(" guard"),
			{RootBranchId, Root.Predicate->SourceUnitId, NestedBranchId,
				PassedOutcomeUnitId, InnerOutcomeUnitId, OuterOutcomeUnitId,
				Explanation.CriterionUnitId},
			{EnterNested->Id, Nested.TrueExit->Id,
				Nested.FalseExit->Id, RootOutcome->Id},
			FString(),
			false,
			EBlueprintLensSemanticLabel::NextExecution),
		MakeGuardNest(
			InnerGuardId,
			Nested.ReaderText + TEXT(" guard"),
			{NestedBranchId, Nested.Predicate->SourceUnitId,
				PassedOutcomeUnitId, InnerOutcomeUnitId},
			{Nested.TrueExit->Id, Nested.FalseExit->Id},
			OuterGuardId,
			true,
			EnterNested->SemanticLabel)};
	TSet<FString> GroupCoveredUnitIds;
	for (const FBlueprintLensGroup& Group : Result.Explanation.Groups)
	{
		for (const FString& UnitId : Group.OrderedUnitIds)
		{
			if (!CoreUnitIds.Contains(UnitId))
			{
				return Fail(
					Explanation,
					TEXT("LC2_LIVE_ADAPTER_GROUP_COVER_INCOMPLETE"));
			}
			GroupCoveredUnitIds.Add(UnitId);
		}
	}
	if (GroupCoveredUnitIds.Num() != Result.Explanation.Units.Num())
	{
		return Fail(
			Explanation,
			TEXT("LC2_LIVE_ADAPTER_GROUP_COVER_INCOMPLETE"));
	}
	Result.Explanation.bHasGroups = true;
	Result.Explanation.GroupPartialOrder.IncomparableGroupIds = {
		TPair<FString, FString>(PassedOutcomeId, InnerOutcomeId),
		TPair<FString, FString>(PassedOutcomeId, OuterOutcomeId),
		TPair<FString, FString>(InnerOutcomeId, OuterOutcomeId)};
	Result.Explanation.GroupPartialOrder.Semantics =
		TEXT("no execution order is proven between these mutually exclusive branch outcomes");
	Result.Explanation.bHasGroupPartialOrder = true;
	Result.AdaptedUnitCount = Result.Explanation.Units.Num();
	Result.AdaptedRelationCount = Result.Explanation.Relations.Num();
	Result.DiagnosticCode = TEXT("LC2_LIVE_EXPLANATION_ADAPTED");
	Result.bAdapted = true;
	return Result;
}
