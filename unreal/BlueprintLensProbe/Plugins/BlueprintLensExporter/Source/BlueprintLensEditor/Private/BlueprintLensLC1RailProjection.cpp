#include "BlueprintLensLC1RailProjection.h"

#include "BlueprintLensBoundaryFacts.h"
#include "BlueprintLensDisplayLabel.h"
#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR LC1RailProjectorVersion[] =
	TEXT("BlueprintLens.LC1RailProjector.v6");

bool IsRailRole(const EBlueprintLensRole Role)
{
	return Role == EBlueprintLensRole::Criterion ||
		Role == EBlueprintLensRole::Control ||
		Role == EBlueprintLensRole::Boundary;
}

bool IsDeferredRole(const EBlueprintLensRole Role)
{
	return Role == EBlueprintLensRole::Value ||
		Role == EBlueprintLensRole::Predicate;
}

void AppendValue(FString& Canonical, const TCHAR* Label, const FString& Value)
{
	Canonical += Label;
	Canonical += FString::Printf(TEXT("[%d:%s]"), Value.Len(), *Value);
}

void AppendIds(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<FString>& Ids)
{
	Canonical += Label;
	Canonical += TEXT("[");
	for (const FString& Id : Ids)
	{
		Canonical += FString::Printf(TEXT("%d:%s;"), Id.Len(), *Id);
	}
	Canonical += TEXT("]");
}

FString ReaderLabel(const FBlueprintLensUnit& Unit)
{
	if (Unit.bHasDisambiguator && !Unit.Disambiguator.Text.IsEmpty())
	{
		return FString::Printf(
			TEXT("%s (%s)"), *Unit.Title, *Unit.Disambiguator.Text);
	}
	return Unit.Title;
}

FString CalculateProjectionIntegrityHash(
	const FBlueprintLensLC1RailProjection& Projection)
{
	FString Canonical;
	AppendValue(Canonical, TEXT("version"), Projection.ProjectorVersion);
	AppendValue(Canonical, TEXT("source-ir"), Projection.SourceIrSha256);
	AppendValue(Canonical, TEXT("criterion"), Projection.CriterionUnitId);
	AppendValue(
		Canonical, TEXT("criterion-label"), Projection.CriterionReaderLabel);
	AppendValue(
		Canonical, TEXT("criterion-display"), Projection.CriterionDisplayLabel);
	AppendIds(Canonical, TEXT("all-units"), Projection.AllUnitIds);
	AppendIds(Canonical, TEXT("all-relations"), Projection.AllRelationIds);
	AppendIds(Canonical, TEXT("deferred-units"), Projection.DeferredUnitIds);
	AppendIds(
		Canonical, TEXT("deferred-relations"), Projection.DeferredRelationIds);
	AppendIds(Canonical, TEXT("fallback-units"), Projection.FallbackUnitIds);
	AppendIds(
		Canonical, TEXT("fallback-relations"), Projection.FallbackRelationIds);
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		 Projection.OrderedCanonicalUnits)
	{
		AppendValue(Canonical, TEXT("unit-id"), Unit.UnitId);
		AppendValue(Canonical, TEXT("unit-label"), Unit.ReaderLabel);
		AppendValue(Canonical, TEXT("unit-display"), Unit.DisplayLabel);
		AppendValue(
			Canonical, TEXT("unit-criterion"), Unit.bIsCriterion ? TEXT("1") : TEXT("0"));
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 Projection.OrderedExecutionRelations)
	{
		AppendValue(Canonical, TEXT("relation-id"), Relation.RelationId);
		AppendValue(Canonical, TEXT("relation-source"), Relation.SourceUnitId);
		AppendValue(Canonical, TEXT("relation-target"), Relation.TargetUnitId);
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 Projection.StationOrderRelations)
	{
		AppendValue(Canonical, TEXT("order-relation-id"), Relation.RelationId);
		AppendValue(
			Canonical, TEXT("order-relation-source"), Relation.SourceUnitId);
		AppendValue(
			Canonical, TEXT("order-relation-target"), Relation.TargetUnitId);
	}
	for (const FBlueprintLensLC1RailOrderRegion& Region :
		 Projection.OrderRegions)
	{
		AppendValue(Canonical, TEXT("order-region-id"), Region.RegionId);
		AppendValue(
			Canonical,
			TEXT("order-region-kind"),
			FString::FromInt(static_cast<int32>(Region.Kind)));
		AppendIds(
			Canonical, TEXT("order-region-members"), Region.MemberUnitIds);
		AppendValue(
			Canonical, TEXT("order-region-reader"), Region.ReaderText);
	}
	for (const FBlueprintLensLC1RailBoundaryCap& Cap : Projection.BoundaryCaps)
	{
		AppendValue(Canonical, TEXT("cap-unit"), Cap.UnitId);
		AppendValue(
			Canonical,
			TEXT("cap-status"),
			FString::FromInt(static_cast<int32>(Cap.SemanticStatus)));
		AppendValue(Canonical, TEXT("cap-title"), Cap.Title);
		AppendValue(Canonical, TEXT("cap-disclosure"), Cap.Disclosure);
	}
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	AppendValue(Canonical, TEXT("diagnostic"), Projection.DiagnosticCode);
	FTCHARToUTF8 Utf8(*Canonical);
	FMD5 Hash;
	Hash.Update(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	uint8 Digest[16];
	Hash.Final(Digest);
	return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
}

FBlueprintLensLC1RailProjection Failure(
	const FBlueprintLensExplanationModel& Explanation,
	const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC1RailProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = LC1RailProjectorVersion;
	Result.CriterionUnitId = Explanation.CriterionUnitId;
	Result.DiagnosticCode = DiagnosticCode;
	Result.ProjectionIntegrityHash = CalculateProjectionIntegrityHash(Result);
	return Result;
}
} // namespace

bool FBlueprintLensLC1RailProjection::HasValidIntegrity() const
{
	return ProjectorVersion == LC1RailProjectorVersion &&
		!SourceIrSha256.IsEmpty() && !ProjectionIntegrityHash.IsEmpty() &&
		ProjectionIntegrityHash.Equals(
			CalculateProjectionIntegrityHash(*this), ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC1RailProjection::IsRenderable() const
{
	const bool bMixedFeatureProjection = !DeferredRelationIds.IsEmpty();
	if (Status != EBlueprintLensLC1RailProjectionStatus::Complete ||
		!HasValidIntegrity() || CriterionUnitId.IsEmpty() ||
		CriterionReaderLabel.IsEmpty() || CriterionDisplayLabel.IsEmpty() ||
		AllUnitIds.Num() < 2 ||
		(bMixedFeatureProjection && AllRelationIds.IsEmpty()) ||
		(!bMixedFeatureProjection &&
			(AllRelationIds.Num() != AllUnitIds.Num() - 1 ||
				!DeferredUnitIds.IsEmpty())) ||
		FallbackUnitIds != AllUnitIds ||
		FallbackRelationIds != AllRelationIds ||
		OrderedCanonicalUnits.Num() != AllUnitIds.Num() ||
		OrderedExecutionRelations.Num() != AllRelationIds.Num())
	{
		return false;
	}

	TSet<FString> CarriedUnitIds;
	for (int32 Index = 0; Index < OrderedCanonicalUnits.Num(); ++Index)
	{
		const FBlueprintLensLC1RailCanonicalUnit& Unit =
			OrderedCanonicalUnits[Index];
		if (Unit.UnitId.IsEmpty() || Unit.UnitId != AllUnitIds[Index] ||
			CarriedUnitIds.Contains(Unit.UnitId) || Unit.ReaderLabel.IsEmpty() ||
			Unit.DisplayLabel.IsEmpty() ||
			(Unit.bIsCriterion != (Unit.UnitId == CriterionUnitId)))
		{
			return false;
		}
		CarriedUnitIds.Add(Unit.UnitId);
	}
	if (OrderedCanonicalUnits.Last().UnitId != CriterionUnitId ||
		!OrderedCanonicalUnits.Last().bIsCriterion ||
		CarriedUnitIds.Num() != AllUnitIds.Num())
	{
		return false;
	}

	TSet<FString> CarriedRelationIds;
	for (int32 Index = 0; Index < OrderedExecutionRelations.Num(); ++Index)
	{
		const FBlueprintLensLC1RailExecutionRelation& Relation =
			OrderedExecutionRelations[Index];
		if (Relation.RelationId.IsEmpty() ||
			Relation.RelationId != AllRelationIds[Index] ||
			CarriedRelationIds.Contains(Relation.RelationId) ||
			!CarriedUnitIds.Contains(Relation.SourceUnitId) ||
			!CarriedUnitIds.Contains(Relation.TargetUnitId))
		{
			return false;
		}
		if (!bMixedFeatureProjection &&
			(Relation.SourceUnitId != OrderedCanonicalUnits[Index].UnitId ||
				Relation.TargetUnitId != OrderedCanonicalUnits[Index + 1].UnitId))
		{
			return false;
		}
		CarriedRelationIds.Add(Relation.RelationId);
	}
	if (CarriedRelationIds.Num() != AllRelationIds.Num())
	{
		return false;
	}
	TSet<FString> StationOrderRelationIds;
	TMap<FString, TArray<FString>> Successors;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit : OrderedCanonicalUnits)
	{
		Successors.Add(Unit.UnitId, TArray<FString>());
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 StationOrderRelations)
	{
		if (Relation.RelationId.IsEmpty() ||
			StationOrderRelationIds.Contains(Relation.RelationId) ||
			!CarriedUnitIds.Contains(Relation.SourceUnitId) ||
			!CarriedUnitIds.Contains(Relation.TargetUnitId))
		{
			return false;
		}
		StationOrderRelationIds.Add(Relation.RelationId);
		Successors.FindChecked(Relation.SourceUnitId).Add(
			Relation.TargetUnitId);
	}
	if (StationOrderRelations.IsEmpty())
	{
		return false;
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 OrderedExecutionRelations)
	{
		if (!StationOrderRelationIds.Contains(Relation.RelationId))
		{
			return false;
		}
	}

	TMap<FString, int32> StationIndexByUnitId;
	for (int32 Index = 0; Index < OrderedCanonicalUnits.Num(); ++Index)
	{
		StationIndexByUnitId.Add(OrderedCanonicalUnits[Index].UnitId, Index);
	}
	const auto CanReach = [&Successors](
			const FString& StartUnitId,
			const FString& GoalUnitId)
	{
		TArray<FString> Pending = {StartUnitId};
		TSet<FString> Visited;
		while (!Pending.IsEmpty())
		{
			const FString Current = Pending.Pop(EAllowShrinking::No);
			if (Current == GoalUnitId)
			{
				return true;
			}
			if (Visited.Contains(Current))
			{
				continue;
			}
			Visited.Add(Current);
			const TArray<FString>* Next = Successors.Find(Current);
			if (Next != nullptr)
			{
				Pending.Append(*Next);
			}
		}
		return false;
	};

	TSet<FString> SeenRegionIds;
	for (const FBlueprintLensLC1RailOrderRegion& Region : OrderRegions)
	{
		const int32 MinimumMembers =
			Region.Kind ==
				EBlueprintLensLC1RailOrderRegionKind::StronglyConnected
			? 1
			: 2;
		if (Region.RegionId.IsEmpty() ||
			SeenRegionIds.Contains(Region.RegionId) ||
			Region.ReaderText.IsEmpty() ||
			Region.MemberUnitIds.Num() < MinimumMembers)
		{
			return false;
		}
		SeenRegionIds.Add(Region.RegionId);
		TSet<FString> RegionMembers;
		int32 MinimumIndex = MAX_int32;
		int32 MaximumIndex = INDEX_NONE;
		for (const FString& UnitId : Region.MemberUnitIds)
		{
			const int32* Index = StationIndexByUnitId.Find(UnitId);
			if (Index == nullptr || RegionMembers.Contains(UnitId))
			{
				return false;
			}
			RegionMembers.Add(UnitId);
			MinimumIndex = FMath::Min(MinimumIndex, *Index);
			MaximumIndex = FMath::Max(MaximumIndex, *Index);
		}
		if (MaximumIndex - MinimumIndex + 1 != Region.MemberUnitIds.Num())
		{
			return false;
		}
		for (int32 A = 0; A < Region.MemberUnitIds.Num(); ++A)
		{
			for (int32 B = A + 1; B < Region.MemberUnitIds.Num(); ++B)
			{
				const bool bForward = CanReach(
					Region.MemberUnitIds[A], Region.MemberUnitIds[B]);
				const bool bBackward = CanReach(
					Region.MemberUnitIds[B], Region.MemberUnitIds[A]);
				if (Region.Kind ==
						EBlueprintLensLC1RailOrderRegionKind::StronglyConnected
					? (!bForward || !bBackward)
					: (bForward || bBackward))
				{
					return false;
				}
			}
		}
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 StationOrderRelations)
	{
		const int32 SourceIndex =
			StationIndexByUnitId.FindChecked(Relation.SourceUnitId);
		const int32 TargetIndex =
			StationIndexByUnitId.FindChecked(Relation.TargetUnitId);
		const bool bSelfLoop = Relation.SourceUnitId == Relation.TargetUnitId;
		const bool bInsideOneScc = bSelfLoop ||
			CanReach(Relation.TargetUnitId, Relation.SourceUnitId);
		if (bInsideOneScc)
		{
			const bool bDeclared = OrderRegions.ContainsByPredicate(
				[&Relation](const FBlueprintLensLC1RailOrderRegion& Region)
				{
					return Region.Kind ==
							EBlueprintLensLC1RailOrderRegionKind::StronglyConnected &&
						Region.MemberUnitIds.Contains(Relation.SourceUnitId) &&
						Region.MemberUnitIds.Contains(Relation.TargetUnitId);
				});
			if (!bDeclared)
			{
				return false;
			}
		}
		else if (SourceIndex >= TargetIndex)
		{
			return false;
		}
	}

	TSet<FString> DeferredUnits;
	for (const FString& UnitId : DeferredUnitIds)
	{
		if (UnitId.IsEmpty() || CarriedUnitIds.Contains(UnitId) ||
			DeferredUnits.Contains(UnitId))
		{
			return false;
		}
		DeferredUnits.Add(UnitId);
	}
	TSet<FString> DeferredRelations;
	for (const FString& RelationId : DeferredRelationIds)
	{
		if (RelationId.IsEmpty() || CarriedRelationIds.Contains(RelationId) ||
			DeferredRelations.Contains(RelationId))
		{
			return false;
		}
		DeferredRelations.Add(RelationId);
	}

	TSet<FString> SeenCaps;
	for (const FBlueprintLensLC1RailBoundaryCap& Cap : BoundaryCaps)
	{
		if (Cap.UnitId.IsEmpty() ||
			!CarriedUnitIds.Contains(Cap.UnitId) || SeenCaps.Contains(Cap.UnitId) ||
			Cap.Title.IsEmpty() ||
			!BlueprintLensIsBoundarySemanticStatus(Cap.SemanticStatus) ||
			Cap.Disclosure != BlueprintLensBoundaryDisclosure(Cap.SemanticStatus))
		{
			return false;
		}
		SeenCaps.Add(Cap.UnitId);
	}
	return true;
}

FBlueprintLensLC1RailProjection FBlueprintLensLC1RailProjector::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	FBlueprintLensLC1RailProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = LC1RailProjectorVersion;
	Result.CriterionUnitId = Explanation.CriterionUnitId;

	TSet<FString> InputUnitIds;
	TSet<FString> RailUnitIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.Id.IsEmpty() || InputUnitIds.Contains(Unit.Id))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED"));
		}
		InputUnitIds.Add(Unit.Id);
		if (IsRailRole(Unit.Role))
		{
			RailUnitIds.Add(Unit.Id);
			if (BlueprintLensIsBoundarySemanticStatus(Unit.SemanticStatus))
			{
				FBlueprintLensLC1RailBoundaryCap Cap;
				Cap.UnitId = Unit.Id;
				Cap.SemanticStatus = Unit.SemanticStatus;
				Cap.Title = Unit.Title;
				Cap.Disclosure =
					BlueprintLensBoundaryDisclosure(Unit.SemanticStatus);
				Result.BoundaryCaps.Add(MoveTemp(Cap));
			}
		}
		else if (IsDeferredRole(Unit.Role))
		{
			// Stage 1 preserves value and predicate evidence outside the ordered
			// execution subset. No other role may be silently dropped from a rail.
			Result.DeferredUnitIds.Add(Unit.Id);
		}
		else
		{
			return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
		}
	}
	TSet<FString> InputRelationIds;
	TArray<const FBlueprintLensRelation*> ExecutionRelations;
	TArray<const FBlueprintLensRelation*> StationRelations;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Id.IsEmpty() || InputRelationIds.Contains(Relation.Id))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED"));
		}
		InputRelationIds.Add(Relation.Id);
		if (RailUnitIds.Contains(Relation.SourceUnitId) &&
			RailUnitIds.Contains(Relation.TargetUnitId))
		{
			StationRelations.Add(&Relation);
		}
		if (Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor)
		{
			if (!RailUnitIds.Contains(Relation.SourceUnitId) ||
				!RailUnitIds.Contains(Relation.TargetUnitId))
			{
				return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
			}
			ExecutionRelations.Add(&Relation);
		}
		else
		{
			// The rail is execution-only. Other proven relations stay explicitly
			// accounted as deferred rather than being painted as rail edges.
			Result.DeferredRelationIds.Add(Relation.Id);
		}
	}
	// An Explanation made solely of execution relations still has to prove one
	// complete chain. Only an explicit non-execution relation licenses the rail
	// to retain a smaller execution ledger without treating a broken chain as a
	// valid partial surface.
	const bool bHasNonExecutionRelations = !Result.DeferredRelationIds.IsEmpty();
	if ((!bHasNonExecutionRelations &&
		(Result.DeferredUnitIds.Num() != 0 ||
			ExecutionRelations.Num() != Explanation.Units.Num() - 1)) ||
		(bHasNonExecutionRelations && ExecutionRelations.IsEmpty()))
	{
		return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
	}
	if (StationRelations.IsEmpty())
	{
		return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
	}

	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Result.CriterionUnitId);
	if (Result.CriterionUnitId.IsEmpty() || Criterion == nullptr ||
		Criterion->Role != EBlueprintLensRole::Criterion ||
		ReaderLabel(*Criterion).IsEmpty())
	{
		return Failure(Explanation, TEXT("LC1_RAIL_CRITERION_NOT_DRAWN"));
	}
	if (bHasNonExecutionRelations)
	{
		// A mixed rail still orders every station by every proven relation whose
		// endpoints are stations. Relations outside the execution drawing ledger
		// therefore constrain placement without being repainted as rail edges.
		TArray<const FBlueprintLensUnit*> RailUnits;
		TMap<FString, int32> RailInputOrder;
		for (const FBlueprintLensUnit& Unit : Explanation.Units)
		{
			if (IsRailRole(Unit.Role))
			{
				if (ReaderLabel(Unit).IsEmpty())
				{
					return Failure(
						Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
				}
				RailInputOrder.Add(Unit.Id, RailUnits.Num());
				RailUnits.Add(&Unit);
			}
		}
		if (!RailUnitIds.Contains(Result.CriterionUnitId))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_CRITERION_NOT_DRAWN"));
		}

		TMap<FString, TArray<FString>> Successors;
		for (const FBlueprintLensUnit* Unit : RailUnits)
		{
			Successors.Add(Unit->Id, TArray<FString>());
		}
		for (const FBlueprintLensRelation* Relation : StationRelations)
		{
			if (Relation == nullptr ||
				!RailUnitIds.Contains(Relation->SourceUnitId) ||
				!RailUnitIds.Contains(Relation->TargetUnitId))
			{
				return Failure(
					Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
			}
			Successors.FindChecked(Relation->SourceUnitId).Add(
				Relation->TargetUnitId);
		}
		for (TPair<FString, TArray<FString>>& Pair : Successors)
		{
			Pair.Value.Sort([&RailInputOrder](const FString& A, const FString& B)
			{
				return RailInputOrder.FindChecked(A) < RailInputOrder.FindChecked(B);
			});
		}

		const auto CanReach = [&Successors](
			const FString& StartUnitId,
			const FString& GoalUnitId)
		{
			TArray<FString> Pending = {StartUnitId};
			TSet<FString> Visited;
			while (!Pending.IsEmpty())
			{
				const FString Current = Pending.Pop(EAllowShrinking::No);
				if (Current == GoalUnitId)
				{
					return true;
				}
				if (Visited.Contains(Current))
				{
					continue;
				}
				Visited.Add(Current);
				Pending.Append(Successors.FindChecked(Current));
			}
			return false;
		};

		// Collapse each SCC before sorting. Internal order is deliberately not
		// claimed; the public region ledger makes that exemption visible later.
		TArray<TArray<FString>> SccMembers;
		TMap<FString, int32> SccByUnitId;
		for (const FBlueprintLensUnit* Unit : RailUnits)
		{
			if (SccByUnitId.Contains(Unit->Id))
			{
				continue;
			}
			const int32 SccIndex = SccMembers.Num();
			TArray<FString> Members;
			for (const FBlueprintLensUnit* Candidate : RailUnits)
			{
				if (!SccByUnitId.Contains(Candidate->Id) &&
					CanReach(Unit->Id, Candidate->Id) &&
					CanReach(Candidate->Id, Unit->Id))
				{
					Members.Add(Candidate->Id);
				}
			}
			Members.Sort([&RailInputOrder](const FString& A, const FString& B)
			{
				return RailInputOrder.FindChecked(A) < RailInputOrder.FindChecked(B);
			});
			for (const FString& MemberUnitId : Members)
			{
				SccByUnitId.Add(MemberUnitId, SccIndex);
			}
			SccMembers.Add(MoveTemp(Members));
		}

		TArray<int32> Parent;
		Parent.SetNumUninitialized(SccMembers.Num());
		for (int32 Index = 0; Index < Parent.Num(); ++Index)
		{
			Parent[Index] = Index;
		}
		TFunction<int32(int32)> FindRoot;
		FindRoot = [&Parent, &FindRoot](const int32 Index)
		{
			if (Parent[Index] != Index)
			{
				Parent[Index] = FindRoot(Parent[Index]);
			}
			return Parent[Index];
		};
		const auto UnionRoots = [&Parent, &FindRoot](const int32 A, const int32 B)
		{
			const int32 RootA = FindRoot(A);
			const int32 RootB = FindRoot(B);
			if (RootA != RootB)
			{
				Parent[FMath::Max(RootA, RootB)] = FMath::Min(RootA, RootB);
			}
		};

		TArray<TPair<FString, FString>> IncomparableUnitPairs;
		if (Explanation.bHasGroupPartialOrder)
		{
			for (const TPair<FString, FString>& GroupPair :
				 Explanation.GroupPartialOrder.IncomparableGroupIds)
			{
				const FBlueprintLensGroup* FirstGroup =
					Explanation.FindGroup(GroupPair.Key);
				const FBlueprintLensGroup* SecondGroup =
					Explanation.FindGroup(GroupPair.Value);
				if (FirstGroup == nullptr || SecondGroup == nullptr ||
					!FirstGroup->bHasExitUnitId ||
					!SecondGroup->bHasExitUnitId)
				{
					return Failure(
						Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
				}
				if (!RailUnitIds.Contains(FirstGroup->ExitUnitId) ||
					!RailUnitIds.Contains(SecondGroup->ExitUnitId))
				{
					continue;
				}
				if (FirstGroup->ExitUnitId == SecondGroup->ExitUnitId ||
					CanReach(
						FirstGroup->ExitUnitId, SecondGroup->ExitUnitId) ||
					CanReach(
						SecondGroup->ExitUnitId, FirstGroup->ExitUnitId))
				{
					return Failure(
						Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
				}
				IncomparableUnitPairs.Emplace(
					FirstGroup->ExitUnitId, SecondGroup->ExitUnitId);
				UnionRoots(
					SccByUnitId.FindChecked(FirstGroup->ExitUnitId),
					SccByUnitId.FindChecked(SecondGroup->ExitUnitId));
			}
		}
		for (int32 Index = 0; Index < Parent.Num(); ++Index)
		{
			Parent[Index] = FindRoot(Index);
		}

		TMap<int32, TArray<int32>> ComponentsByBlock;
		for (int32 SccIndex = 0; SccIndex < SccMembers.Num(); ++SccIndex)
		{
			ComponentsByBlock.FindOrAdd(Parent[SccIndex]).Add(SccIndex);
		}
		TMap<int32, int32> MinimumInputByBlock;
		for (TPair<int32, TArray<int32>>& Pair : ComponentsByBlock)
		{
			Pair.Value.Sort([&SccMembers, &RailInputOrder](
				const int32 A, const int32 B)
			{
				return RailInputOrder.FindChecked(SccMembers[A][0]) <
					RailInputOrder.FindChecked(SccMembers[B][0]);
			});
			MinimumInputByBlock.Add(
				Pair.Key,
				RailInputOrder.FindChecked(SccMembers[Pair.Value[0]][0]));
		}

		TMap<int32, TArray<int32>> BlockSuccessors;
		TMap<int32, int32> BlockIndegree;
		for (const TPair<int32, TArray<int32>>& Pair : ComponentsByBlock)
		{
			BlockSuccessors.Add(Pair.Key, TArray<int32>());
			BlockIndegree.Add(Pair.Key, 0);
		}
		TSet<FString> SeenBlockEdges;
		for (const FBlueprintLensRelation* Relation : StationRelations)
		{
			const int32 SourceBlock =
				Parent[SccByUnitId.FindChecked(Relation->SourceUnitId)];
			const int32 TargetBlock =
				Parent[SccByUnitId.FindChecked(Relation->TargetUnitId)];
			if (SourceBlock == TargetBlock)
			{
				continue;
			}
			const FString EdgeKey = FString::Printf(
				TEXT("%d>%d"), SourceBlock, TargetBlock);
			if (!SeenBlockEdges.Contains(EdgeKey))
			{
				SeenBlockEdges.Add(EdgeKey);
				BlockSuccessors.FindChecked(SourceBlock).Add(TargetBlock);
				++BlockIndegree.FindChecked(TargetBlock);
			}
		}

		TArray<int32> AvailableBlocks;
		for (const TPair<int32, int32>& Pair : BlockIndegree)
		{
			if (Pair.Value == 0)
			{
				AvailableBlocks.Add(Pair.Key);
			}
		}
		TArray<int32> OrderedBlocks;
		while (!AvailableBlocks.IsEmpty())
		{
			AvailableBlocks.Sort([&MinimumInputByBlock](
				const int32 A, const int32 B)
			{
				return MinimumInputByBlock.FindChecked(A) <
					MinimumInputByBlock.FindChecked(B);
			});
			int32 SelectedAvailableIndex = 0;
			if (AvailableBlocks.Num() > 1)
			{
				for (int32 Index = 0; Index < AvailableBlocks.Num(); ++Index)
				{
					const int32 CandidateBlock = AvailableBlocks[Index];
					const bool bContainsCriterion =
						ComponentsByBlock.FindChecked(CandidateBlock)
						.ContainsByPredicate(
							[&SccMembers, &Result](const int32 SccIndex)
							{
								return SccMembers[SccIndex].Contains(
									Result.CriterionUnitId);
							});
					if (!bContainsCriterion)
					{
						SelectedAvailableIndex = Index;
						break;
					}
				}
			}
			const int32 SelectedBlock =
				AvailableBlocks[SelectedAvailableIndex];
			AvailableBlocks.RemoveAt(SelectedAvailableIndex);
			OrderedBlocks.Add(SelectedBlock);
			for (const int32 Successor :
				 BlockSuccessors.FindChecked(SelectedBlock))
			{
				int32& Indegree = BlockIndegree.FindChecked(Successor);
				--Indegree;
				if (Indegree == 0)
				{
					AvailableBlocks.Add(Successor);
				}
			}
		}
		if (OrderedBlocks.Num() != ComponentsByBlock.Num())
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}

		TArray<FString> OrderedUnitIds;
		for (const int32 Block : OrderedBlocks)
		{
			TArray<int32> Components = ComponentsByBlock.FindChecked(Block);
			Components.StableSort([&SccMembers, &RailInputOrder, &Result](
				const int32 A, const int32 B)
			{
				const bool bACriterion =
					SccMembers[A].Contains(Result.CriterionUnitId);
				const bool bBCriterion =
					SccMembers[B].Contains(Result.CriterionUnitId);
				if (bACriterion != bBCriterion)
				{
					return !bACriterion;
				}
				return RailInputOrder.FindChecked(SccMembers[A][0]) <
					RailInputOrder.FindChecked(SccMembers[B][0]);
			});
			for (const int32 SccIndex : Components)
			{
				TArray<FString> Members = SccMembers[SccIndex];
				Members.RemoveSingle(Result.CriterionUnitId);
				if (SccMembers[SccIndex].Contains(Result.CriterionUnitId))
				{
					Members.Add(Result.CriterionUnitId);
				}
				OrderedUnitIds.Append(Members);
			}
		}
		if (OrderedUnitIds.Num() != RailUnits.Num() ||
			OrderedUnitIds.Last() != Result.CriterionUnitId)
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}
		for (const FString& UnitId : OrderedUnitIds)
		{
			const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
			if (Unit == nullptr || !IsRailRole(Unit->Role) ||
				ReaderLabel(*Unit).IsEmpty())
			{
				return Failure(
					Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
			}
			FBlueprintLensLC1RailCanonicalUnit CanonicalUnit;
			CanonicalUnit.UnitId = Unit->Id;
			CanonicalUnit.ReaderLabel = ReaderLabel(*Unit);
			CanonicalUnit.DisplayLabel = BlueprintLensDisplayLabel(*Unit);
			CanonicalUnit.bIsCriterion = Unit->Id == Result.CriterionUnitId;
			Result.OrderedCanonicalUnits.Add(MoveTemp(CanonicalUnit));
			Result.AllUnitIds.Add(Unit->Id);
		}
		for (const FBlueprintLensRelation* Relation : StationRelations)
		{
			FBlueprintLensLC1RailExecutionRelation OrderRelation;
			OrderRelation.RelationId = Relation->Id;
			OrderRelation.SourceUnitId = Relation->SourceUnitId;
			OrderRelation.TargetUnitId = Relation->TargetUnitId;
			Result.StationOrderRelations.Add(MoveTemp(OrderRelation));
		}
		for (const FBlueprintLensRelation* Relation : ExecutionRelations)
		{
			FBlueprintLensLC1RailExecutionRelation OrderedRelation;
			OrderedRelation.RelationId = Relation->Id;
			OrderedRelation.SourceUnitId = Relation->SourceUnitId;
			OrderedRelation.TargetUnitId = Relation->TargetUnitId;
			Result.OrderedExecutionRelations.Add(MoveTemp(OrderedRelation));
			Result.AllRelationIds.Add(Relation->Id);
		}

		TMap<FString, int32> FinalIndexByUnitId;
		for (int32 Index = 0; Index < OrderedUnitIds.Num(); ++Index)
		{
			FinalIndexByUnitId.Add(OrderedUnitIds[Index], Index);
		}
		TSet<FString> SelfLoopUnitIds;
		for (const FBlueprintLensRelation* Relation : StationRelations)
		{
			if (Relation->SourceUnitId == Relation->TargetUnitId)
			{
				SelfLoopUnitIds.Add(Relation->SourceUnitId);
			}
		}
		for (int32 SccIndex = 0; SccIndex < SccMembers.Num(); ++SccIndex)
		{
			if (SccMembers[SccIndex].Num() < 2 &&
				!SelfLoopUnitIds.Contains(SccMembers[SccIndex][0]))
			{
				continue;
			}
			FBlueprintLensLC1RailOrderRegion Region;
			Region.Kind =
				EBlueprintLensLC1RailOrderRegionKind::StronglyConnected;
			Region.MemberUnitIds = SccMembers[SccIndex];
			Region.MemberUnitIds.Sort([&FinalIndexByUnitId](
				const FString& A, const FString& B)
			{
				return FinalIndexByUnitId.FindChecked(A) <
					FinalIndexByUnitId.FindChecked(B);
			});
			Region.RegionId = TEXT("scc:") + Region.MemberUnitIds[0];
			Region.ReaderText = TEXT(
				"UNORDERED SCC · No order is claimed inside this segment because "
				"its stations form one strongly connected component.");
			Result.OrderRegions.Add(MoveTemp(Region));
		}
		TMap<int32, TSet<FString>> IncomparableMembersByBlock;
		for (const TPair<FString, FString>& Pair : IncomparableUnitPairs)
		{
			const int32 Block = Parent[SccByUnitId.FindChecked(Pair.Key)];
			IncomparableMembersByBlock.FindOrAdd(Block).Add(Pair.Key);
			IncomparableMembersByBlock.FindOrAdd(Block).Add(Pair.Value);
		}
		for (const TPair<int32, TSet<FString>>& Pair :
			 IncomparableMembersByBlock)
		{
			FBlueprintLensLC1RailOrderRegion Region;
			Region.Kind = EBlueprintLensLC1RailOrderRegionKind::Incomparable;
			Region.MemberUnitIds = Pair.Value.Array();
			Region.MemberUnitIds.Sort([&FinalIndexByUnitId](
				const FString& A, const FString& B)
			{
				return FinalIndexByUnitId.FindChecked(A) <
					FinalIndexByUnitId.FindChecked(B);
			});
			Region.RegionId =
				TEXT("incomparable:") + Region.MemberUnitIds[0];
			Region.ReaderText = FString::Printf(
				TEXT("INCOMPARABLE · %s"),
				Explanation.GroupPartialOrder.Semantics.IsEmpty()
					? TEXT("no order is proven between these stations")
					: *Explanation.GroupPartialOrder.Semantics);
			Result.OrderRegions.Add(MoveTemp(Region));
		}
		Result.OrderRegions.Sort([&FinalIndexByUnitId](
			const FBlueprintLensLC1RailOrderRegion& A,
			const FBlueprintLensLC1RailOrderRegion& B)
		{
			const int32 AIndex =
				FinalIndexByUnitId.FindChecked(A.MemberUnitIds[0]);
			const int32 BIndex =
				FinalIndexByUnitId.FindChecked(B.MemberUnitIds[0]);
			return AIndex != BIndex
				? AIndex < BIndex
				: static_cast<int32>(A.Kind) < static_cast<int32>(B.Kind);
		});
		Result.FallbackUnitIds = Result.AllUnitIds;
		Result.FallbackRelationIds = Result.AllRelationIds;
		Result.CriterionReaderLabel = ReaderLabel(*Criterion);
		Result.CriterionDisplayLabel = BlueprintLensDisplayLabel(*Criterion);
		Result.Status = EBlueprintLensLC1RailProjectionStatus::Complete;
		Result.DiagnosticCode = TEXT("LC1_RAIL_COMPLETE");
		Result.ProjectionIntegrityHash = CalculateProjectionIntegrityHash(Result);
		return Result;
	}

	TMap<FString, const FBlueprintLensRelation*> Incoming;
	TMap<FString, const FBlueprintLensRelation*> Outgoing;
	for (const FBlueprintLensRelation* Relation : ExecutionRelations)
	{
		if (Incoming.Contains(Relation->TargetUnitId) ||
			Outgoing.Contains(Relation->SourceUnitId))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}
		Incoming.Add(Relation->TargetUnitId, Relation);
		Outgoing.Add(Relation->SourceUnitId, Relation);
	}

	if (!Incoming.Contains(Result.CriterionUnitId) ||
		Outgoing.Contains(Result.CriterionUnitId))
	{
		return Failure(
			Explanation,
			Incoming.Contains(Result.CriterionUnitId)
				? TEXT("LC1_RAIL_ORDER_NOT_PROVEN")
				: TEXT("LC1_RAIL_CRITERION_NOT_DRAWN"));
	}

	TArray<FString> ReverseUnitIds;
	TArray<const FBlueprintLensRelation*> ReverseRelations;
	FString Current = Result.CriterionUnitId;
	while (true)
	{
		ReverseUnitIds.Add(Current);
		const FBlueprintLensRelation* const* Previous = Incoming.Find(Current);
		if (Previous == nullptr)
		{
			break;
		}
		if (*Previous == nullptr || ReverseUnitIds.Contains((*Previous)->SourceUnitId))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}
		ReverseRelations.Add(*Previous);
		Current = (*Previous)->SourceUnitId;
	}

	TSet<FString> SeenUnits;
	for (int32 Index = ReverseUnitIds.Num() - 1; Index >= 0; --Index)
	{
		const FString& UnitId = ReverseUnitIds[Index];
		if (SeenUnits.Contains(UnitId))
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}
		SeenUnits.Add(UnitId);
		const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
		if (Unit == nullptr || ReaderLabel(*Unit).IsEmpty())
		{
			return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
		}
		FBlueprintLensLC1RailCanonicalUnit CanonicalUnit;
		CanonicalUnit.UnitId = Unit->Id;
		CanonicalUnit.ReaderLabel = ReaderLabel(*Unit);
		CanonicalUnit.DisplayLabel = BlueprintLensDisplayLabel(*Unit);
		CanonicalUnit.bIsCriterion = Unit->Id == Result.CriterionUnitId;
		Result.OrderedCanonicalUnits.Add(MoveTemp(CanonicalUnit));
		Result.AllUnitIds.Add(UnitId);
	}
	for (int32 Index = ReverseRelations.Num() - 1; Index >= 0; --Index)
	{
		const FBlueprintLensRelation* Relation = ReverseRelations[Index];
		if (Relation == nullptr)
		{
			return Failure(Explanation, TEXT("LC1_RAIL_ORDER_NOT_PROVEN"));
		}
		FBlueprintLensLC1RailExecutionRelation OrderedRelation;
		OrderedRelation.RelationId = Relation->Id;
		OrderedRelation.SourceUnitId = Relation->SourceUnitId;
		OrderedRelation.TargetUnitId = Relation->TargetUnitId;
		Result.OrderedExecutionRelations.Add(MoveTemp(OrderedRelation));
		Result.AllRelationIds.Add(Relation->Id);
	}
	for (const FBlueprintLensRelation* Relation : StationRelations)
	{
		FBlueprintLensLC1RailExecutionRelation OrderRelation;
		OrderRelation.RelationId = Relation->Id;
		OrderRelation.SourceUnitId = Relation->SourceUnitId;
		OrderRelation.TargetUnitId = Relation->TargetUnitId;
		Result.StationOrderRelations.Add(MoveTemp(OrderRelation));
	}

	if (Result.AllUnitIds.Num() < 2 ||
		Result.AllRelationIds.Num() != Result.AllUnitIds.Num() - 1 ||
		Result.OrderedExecutionRelations.Num() != Result.AllRelationIds.Num())
	{
		return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
	}
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (!SeenUnits.Contains(Unit.Id))
		{
			Result.DeferredUnitIds.Add(Unit.Id);
		}
	}
	TSet<FString> SeenRelations;
	for (const FString& RelationId : Result.AllRelationIds)
	{
		SeenRelations.Add(RelationId);
	}
	for (const FBlueprintLensRelation* Relation : ExecutionRelations)
	{
		if (Relation != nullptr && !SeenRelations.Contains(Relation->Id))
		{
			Result.DeferredRelationIds.Add(Relation->Id);
		}
	}
	if (!bHasNonExecutionRelations &&
		(SeenUnits.Num() != InputUnitIds.Num() ||
			Result.AllRelationIds.Num() != ExecutionRelations.Num()))
	{
		return Failure(Explanation, TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED"));
	}
	Result.FallbackUnitIds = Result.AllUnitIds;
	Result.FallbackRelationIds = Result.AllRelationIds;
	Result.CriterionReaderLabel = ReaderLabel(*Criterion);
	Result.CriterionDisplayLabel = BlueprintLensDisplayLabel(*Criterion);
	Result.Status = EBlueprintLensLC1RailProjectionStatus::Complete;
	Result.DiagnosticCode = TEXT("LC1_RAIL_COMPLETE");
	Result.ProjectionIntegrityHash = CalculateProjectionIntegrityHash(Result);
	return Result;
}
