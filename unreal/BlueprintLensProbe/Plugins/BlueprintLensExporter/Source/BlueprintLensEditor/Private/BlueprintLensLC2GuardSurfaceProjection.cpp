#include "BlueprintLensLC2GuardSurfaceProjection.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR GuardSurfaceProjectorVersion[] =
	TEXT("BlueprintLens.LC2GuardSurfaceProjector.D2.v1");

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
	for (const FString& Id : Ids)
	{
		AppendValue(Canonical, TEXT("id"), Id);
	}
}

FString CalculateIntegrity(const FBlueprintLensLC2GuardSurfaceProjection& Value)
{
	FString Canonical;
	AppendValue(Canonical, TEXT("version"), Value.ProjectorVersion);
	AppendValue(Canonical, TEXT("ir"), Value.SourceIrSha256);
	AppendValue(Canonical, TEXT("criterion"), Value.CriterionUnitId);
	AppendIds(Canonical, TEXT("units"), Value.AllUnitIds);
	AppendIds(Canonical, TEXT("relations"), Value.AllRelationIds);
	for (const FBlueprintLensLC2GuardCanonicalUnit& Unit : Value.CanonicalUnits)
	{
		AppendValue(Canonical, TEXT("unit"), Unit.UnitId);
		AppendValue(Canonical, TEXT("owner"), Unit.OwnerGuardGroupId);
		AppendValue(Canonical, TEXT("role"), FString::Printf(
			TEXT("%d%d%d"), Unit.bIsPredicate, Unit.bIsBranch, Unit.bIsCriterion));
	}
	for (const FBlueprintLensLC2GuardCompound& Compound : Value.Compounds)
	{
		AppendValue(Canonical, TEXT("compound"), Compound.GroupId);
		AppendValue(Canonical, TEXT("parent"), Compound.ParentGroupId);
		AppendValue(Canonical, TEXT("predicate"), Compound.PredicateUnitId);
		AppendValue(Canonical, TEXT("branch"), Compound.BranchUnitId);
		AppendValue(Canonical, TEXT("guard-reader"), Compound.GuardReaderText);
		AppendIds(Canonical, TEXT("members"), Compound.ExclusiveMemberUnitIds);
	}
	for (const FBlueprintLensLC2GuardOutcomeRail& Rail : Value.OutcomeRails)
	{
		AppendValue(Canonical, TEXT("rail"), Rail.GroupId);
		AppendValue(Canonical, TEXT("outcome"), Rail.OutcomeUnitId);
		AppendValue(Canonical, TEXT("reconvergence"), Rail.ReconvergenceRelationId);
		AppendIds(Canonical, TEXT("rail-units"), Rail.CanonicalUnitIds);
	}
	for (const FBlueprintLensLC2GuardForkMark& Mark : Value.ForkMarks)
	{
		AppendValue(Canonical, TEXT("fork"), Mark.BranchUnitId);
		AppendIds(Canonical, TEXT("fork-outcomes"), Mark.OutcomeGroupIds);
	}
	AppendValue(Canonical, TEXT("diagnostic"), Value.DiagnosticCode);
	return FMD5::HashAnsiString(*Canonical);
}

FBlueprintLensLC2GuardSurfaceProjection Fail(
	FBlueprintLensLC2GuardSurfaceProjection Result,
	const TCHAR* Diagnostic)
{
	Result.Status = EBlueprintLensLC2GuardSurfaceProjectionStatus::Unavailable;
	Result.DiagnosticCode = Diagnostic;
	Result.ProjectionIntegrityHash = CalculateIntegrity(Result);
	return Result;
}

FString ReaderLabel(const FBlueprintLensUnit& Unit)
{
	return Unit.bHasDisambiguator && !Unit.Disambiguator.Text.IsEmpty()
		? FString::Printf(TEXT("%s (%s)"), *Unit.Title, *Unit.Disambiguator.Text)
		: Unit.Title;
}

bool HasIncomparablePair(
	const TArray<TPair<FString, FString>>& Pairs,
	const FString& A,
	const FString& B)
{
	return Pairs.ContainsByPredicate(
		[&A, &B](const TPair<FString, FString>& Pair)
		{
			return (Pair.Key == A && Pair.Value == B) ||
				(Pair.Key == B && Pair.Value == A);
		});
}

const FBlueprintLensLC2GuardOutlinePath* FindPath(
	const FBlueprintLensLC2GuardOutlineProjection& Outline,
	const FString& GroupId)
{
	return Outline.OutcomePaths.FindByPredicate(
		[&GroupId](const FBlueprintLensLC2GuardOutlinePath& Path)
		{
			return Path.GroupId == GroupId;
		});
}

bool RelationMatchesSourcePort(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensRelation& Relation)
{
	if (Relation.Kind != EBlueprintLensRelationKind::ControlsExecution ||
		!Relation.bHasSemanticLabel || Relation.SourceEdgeEndpoints.IsEmpty())
	{
		return true;
	}
	const bool bIsGuardBranch = Explanation.Relations.ContainsByPredicate(
		[&Relation](const FBlueprintLensRelation& Candidate)
		{
			return Candidate.Kind == EBlueprintLensRelationKind::PredicateFor &&
				Candidate.TargetUnitId == Relation.SourceUnitId;
		});
	if (!bIsGuardBranch)
	{
		return true;
	}
	const FString& Port = Relation.SourceEdgeEndpoints[0].SourcePortLabel;
	if (Port.Equals(TEXT("then"), ESearchCase::IgnoreCase))
	{
		return Relation.SemanticLabel == EBlueprintLensSemanticLabel::ConditionTrue;
	}
	if (Port.Equals(TEXT("else"), ESearchCase::IgnoreCase))
	{
		return Relation.SemanticLabel == EBlueprintLensSemanticLabel::ConditionFalse;
	}
	return false;
}

bool HasUniquePathRows(const FBlueprintLensLC2GuardOutlinePath& Path)
{
	TSet<FString> UnitIds;
	for (const FBlueprintLensLC2GuardOutlineRow& Row : Path.Rows)
	{
		if (Row.UnitId.IsEmpty() || UnitIds.Contains(Row.UnitId))
		{
			return false;
		}
		UnitIds.Add(Row.UnitId);
	}
	return true;
}

bool HasExactIncomparablePairs(
	const TArray<TPair<FString, FString>>& Pairs,
	const TArray<FBlueprintLensLC2GuardOutcomeRail>& Rails)
{
	if (Pairs.Num() != 3 || Rails.Num() != 3)
	{
		return false;
	}
	TSet<FString> OutcomeIds;
	for (const FBlueprintLensLC2GuardOutcomeRail& Rail : Rails)
	{
		if (Rail.GroupId.IsEmpty() || OutcomeIds.Contains(Rail.GroupId))
		{
			return false;
		}
		OutcomeIds.Add(Rail.GroupId);
	}
	TSet<FString> Seen;
	for (const TPair<FString, FString>& Pair : Pairs)
	{
		if (Pair.Key == Pair.Value || !OutcomeIds.Contains(Pair.Key) ||
			!OutcomeIds.Contains(Pair.Value))
		{
			return false;
		}
		const FString Key = Pair.Key < Pair.Value
			? Pair.Key + TEXT("|") + Pair.Value
			: Pair.Value + TEXT("|") + Pair.Key;
		if (Seen.Contains(Key))
		{
			return false;
		}
		Seen.Add(Key);
	}
	for (int32 A = 0; A < Rails.Num(); ++A)
	{
		for (int32 B = A + 1; B < Rails.Num(); ++B)
		{
			const FString Key = Rails[A].GroupId < Rails[B].GroupId
				? Rails[A].GroupId + TEXT("|") + Rails[B].GroupId
				: Rails[B].GroupId + TEXT("|") + Rails[A].GroupId;
			if (!Seen.Contains(Key))
			{
				return false;
			}
		}
	}
	return true;
}
} // namespace

const FBlueprintLensLC2GuardCanonicalUnit*
FBlueprintLensLC2GuardSurfaceProjection::FindCanonicalUnit(
	const FString& UnitId) const
{
	return CanonicalUnits.FindByPredicate(
		[&UnitId](const FBlueprintLensLC2GuardCanonicalUnit& Unit)
		{
			return Unit.UnitId == UnitId;
		});
}

const FBlueprintLensLC2GuardCompound*
FBlueprintLensLC2GuardSurfaceProjection::FindCompound(
	const FString& GroupId) const
{
	return Compounds.FindByPredicate(
		[&GroupId](const FBlueprintLensLC2GuardCompound& Compound)
		{
			return Compound.GroupId == GroupId;
		});
}

bool FBlueprintLensLC2GuardSurfaceProjection::HasValidIntegrity() const
{
	return ProjectorVersion == GuardSurfaceProjectorVersion &&
		!SourceIrSha256.IsEmpty() &&
		!ProjectionIntegrityHash.IsEmpty() && ProjectionIntegrityHash.Equals(
			CalculateIntegrity(*this), ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC2GuardSurfaceProjection::IsRenderable() const
{
	if (Status != EBlueprintLensLC2GuardSurfaceProjectionStatus::GuardRails ||
		!HasValidIntegrity() || AllUnitIds.Num() != 9 ||
		AllRelationIds.Num() != 10 || CanonicalUnits.Num() != 9 ||
		Compounds.Num() != 2 || OutcomeRails.Num() != 3 || ForkMarks.Num() != 2 ||
		IncomparableGroupIds.Num() != 3)
	{
		return false;
	}
	TSet<FString> SeenUnits;
	for (const FBlueprintLensLC2GuardCanonicalUnit& Unit : CanonicalUnits)
	{
		if (Unit.UnitId.IsEmpty() || SeenUnits.Contains(Unit.UnitId))
		{
			return false;
		}
		SeenUnits.Add(Unit.UnitId);
	}
	for (const FString& UnitId : AllUnitIds)
	{
		if (!SeenUnits.Contains(UnitId))
		{
			return false;
		}
	}
	return !CriterionUnitId.IsEmpty() &&
		FindCanonicalUnit(CriterionUnitId) != nullptr;
}

FBlueprintLensLC2GuardSurfaceProjection
FBlueprintLensLC2GuardSurfaceProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC2GuardOutlineProjection& Outline)
{
	FBlueprintLensLC2GuardSurfaceProjection Result;
	Result.ProjectorVersion = GuardSurfaceProjectorVersion;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.CriterionUnitId = Explanation.CriterionUnitId;
	Result.NoOrderReaderText = TEXT("ALTERNATIVES - NO ORDER PROVEN");
	if (Explanation.bHasGroupPartialOrder &&
		Explanation.GroupPartialOrder.IncomparableGroupIds.Num() != 3)
	{
		return Fail(
			MoveTemp(Result),
			TEXT("LC2_GUARD_SURFACE_FORK_MARK_MISSING"));
	}
	if (!Outline.IsRenderable() ||
		Outline.Status != EBlueprintLensLC2GuardOutlineProjectionStatus::GroupedOutcomePaths ||
		Outline.SourceIrSha256 != Result.SourceIrSha256 ||
		Explanation.Units.Num() != 9 || Explanation.Relations.Num() != 10)
	{
		return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_OUTLINE_UNAVAILABLE"));
	}
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.Id.IsEmpty() || Result.AllUnitIds.Contains(Unit.Id))
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_UNIT_LEDGER_INVALID"));
		}
		Result.AllUnitIds.Add(Unit.Id);
		FBlueprintLensLC2GuardCanonicalUnit Canonical;
		Canonical.UnitId = Unit.Id;
		Canonical.ReaderLabel = ReaderLabel(Unit);
		Canonical.bIsPredicate = Unit.Role == EBlueprintLensRole::Predicate;
		Canonical.bIsCriterion = Unit.Id == Result.CriterionUnitId;
		Result.CanonicalUnits.Add(MoveTemp(Canonical));
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!RelationMatchesSourcePort(Explanation, Relation))
		{
			return Fail(
				MoveTemp(Result),
				TEXT("LC2_GUARD_SURFACE_OUTCOME_LABEL_INVALID"));
		}
		if (Relation.Id.IsEmpty() || Result.AllRelationIds.Contains(Relation.Id))
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_RELATION_LEDGER_INVALID"));
		}
		Result.AllRelationIds.Add(Relation.Id);
	}

	for (const FBlueprintLensLC2GuardOutlineNest& Nest : Outline.GuardNests)
	{
		const FBlueprintLensGroup* Group = Explanation.FindGroup(Nest.GroupId);
		if (Group == nullptr || Group->Kind != EBlueprintLensGroupKind::GuardNest ||
			Group->EntryUnitId.IsEmpty())
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_GUARD_BINDING_INVALID"));
		}
		const FBlueprintLensRelation* PredicateRelation =
			Explanation.Relations.FindByPredicate(
				[&Group](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
						Relation.TargetUnitId == Group->EntryUnitId;
				});
		if (PredicateRelation == nullptr ||
			Explanation.FindUnit(PredicateRelation->SourceUnitId) == nullptr)
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_PREDICATE_BINDING_MISSING"));
		}
		const FBlueprintLensUnit* BranchUnit = Explanation.FindUnit(Group->EntryUnitId);
		if (BranchUnit == nullptr || !BranchUnit->bHasDisambiguator ||
			BranchUnit->Disambiguator.Text.IsEmpty())
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_GUARD_LABEL_MISSING"));
		}
		FBlueprintLensLC2GuardCompound Compound;
		Compound.GroupId = Group->Id;
		Compound.ParentGroupId = Group->bHasParent ? Group->ParentGroupId : FString();
		Compound.PredicateUnitId = PredicateRelation->SourceUnitId;
		Compound.BranchUnitId = Group->EntryUnitId;
		Compound.GuardReaderText = BranchUnit->Disambiguator.Text;
		Compound.ExclusiveMemberUnitIds = {
			Compound.PredicateUnitId, Compound.BranchUnitId};
		Result.Compounds.Add(MoveTemp(Compound));
	}
	if (Result.Compounds.Num() != 2 ||
		Result.Compounds[0].ParentGroupId.IsEmpty() ==
			Result.Compounds[1].ParentGroupId.IsEmpty() ||
		(Result.Compounds[0].ParentGroupId.IsEmpty()
			? Result.Compounds[1].ParentGroupId != Result.Compounds[0].GroupId
			: Result.Compounds[0].ParentGroupId != Result.Compounds[1].GroupId))
	{
		return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_COMPOUND_PARENT_INVALID"));
	}
	const FBlueprintLensLC2GuardCompound* NestedCompound =
		Result.Compounds[0].ParentGroupId.IsEmpty()
			? &Result.Compounds[1]
			: &Result.Compounds[0];
	const FBlueprintLensGroup* NestedGroup = Explanation.FindGroup(
		NestedCompound->GroupId);
	if (NestedGroup == nullptr || !NestedGroup->bHasEnteredBy ||
		NestedGroup->EnteredBy != EBlueprintLensSemanticLabel::ConditionTrue)
	{
		return Fail(
			MoveTemp(Result),
			TEXT("LC2_GUARD_SURFACE_ENTERED_BY_INVALID"));
	}
	for (FBlueprintLensLC2GuardCompound& Compound : Result.Compounds)
	{
		const FBlueprintLensLC2GuardCompound* Nested = Result.Compounds.FindByPredicate(
			[&Compound](const FBlueprintLensLC2GuardCompound& Candidate)
			{
				return Candidate.ParentGroupId == Compound.GroupId;
			});
		for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		{
			if (Relation.SourceUnitId != Compound.BranchUnitId ||
				Relation.Kind != EBlueprintLensRelationKind::ControlsExecution ||
				Relation.TargetUnitId == (Nested == nullptr ? FString() : Nested->BranchUnitId))
			{
				continue;
			}
			if (Result.CriterionUnitId == Relation.TargetUnitId ||
				Compound.ExclusiveMemberUnitIds.Contains(Relation.TargetUnitId) ||
				Result.Compounds.ContainsByPredicate(
					[&Relation](const FBlueprintLensLC2GuardCompound& Candidate)
					{
						return Candidate.BranchUnitId == Relation.TargetUnitId;
					}))
			{
				continue;
			}
			Compound.ExclusiveMemberUnitIds.Add(Relation.TargetUnitId);
		}
	}

	TSet<FString> ExclusiveMembers;
	for (const FBlueprintLensLC2GuardCompound& Compound : Result.Compounds)
	{
		for (const FString& UnitId : Compound.ExclusiveMemberUnitIds)
		{
			if (ExclusiveMembers.Contains(UnitId))
			{
				return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_DUPLICATE_CANONICAL_UNIT"));
			}
			ExclusiveMembers.Add(UnitId);
			FBlueprintLensLC2GuardCanonicalUnit* Unit =
				Result.CanonicalUnits.FindByPredicate(
					[&UnitId](const FBlueprintLensLC2GuardCanonicalUnit& Candidate)
					{
						return Candidate.UnitId == UnitId;
					});
			if (Unit == nullptr)
			{
				return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_COMPOUND_MEMBER_INVALID"));
			}
			Unit->OwnerGuardGroupId = Compound.GroupId;
			Unit->bIsBranch = UnitId == Compound.BranchUnitId;
		}
	}

	for (const FBlueprintLensLC2GuardOutlinePath& Path : Outline.OutcomePaths)
	{
		if (Path.Rows.IsEmpty() || !HasUniquePathRows(Path) ||
			Path.ExitUnitId.IsEmpty() ||
			Result.OutcomeRails.ContainsByPredicate(
				[&Path](const FBlueprintLensLC2GuardOutcomeRail& Rail)
				{
					return Rail.GroupId == Path.GroupId;
				}))
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_OUTCOME_RAIL_INVALID"));
		}
		const FBlueprintLensRelation* Reconvergence = Explanation.Relations.FindByPredicate(
			[&Path, &Result](const FBlueprintLensRelation& Relation)
			{
				return Relation.SourceUnitId == Path.ExitUnitId &&
					Relation.TargetUnitId == Result.CriterionUnitId;
			});
		if (Reconvergence == nullptr)
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_RECONVERGENCE_MISSING"));
		}
		FBlueprintLensLC2GuardOutcomeRail Rail;
		Rail.GroupId = Path.GroupId;
		Rail.Title = Path.Title;
		Rail.EntryUnitId = Path.EntryUnitId;
		Rail.OutcomeUnitId = Path.ExitUnitId;
		Rail.ReconvergenceRelationId = Reconvergence->Id;
		Rail.OrderedRelationIds = Path.OrderedRelationIds;
		Rail.CanonicalUnitIds = {Path.ExitUnitId};
		Result.OutcomeRails.Add(MoveTemp(Rail));
	}
	if (Result.OutcomeRails.Num() != 3)
	{
		return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_OUTCOME_RAIL_COUNT_INVALID"));
	}

	Result.IncomparableGroupIds = Outline.IncomparableGroupIds;
	if (!HasExactIncomparablePairs(Result.IncomparableGroupIds, Result.OutcomeRails))
	{
		return Fail(
			MoveTemp(Result),
			TEXT("LC2_GUARD_SURFACE_OUTCOME_ORDER_FABRICATED"));
	}
	for (const FBlueprintLensLC2GuardCompound& Compound : Result.Compounds)
	{
		FBlueprintLensLC2GuardForkMark Mark;
		Mark.BranchUnitId = Compound.BranchUnitId;
		for (const FBlueprintLensLC2GuardOutcomeRail& Rail : Result.OutcomeRails)
		{
			const FBlueprintLensLC2GuardOutlinePath* Path = FindPath(Outline, Rail.GroupId);
			if (Path != nullptr && Path->Rows.ContainsByPredicate(
				[&Compound](const FBlueprintLensLC2GuardOutlineRow& Row)
				{
					return Row.UnitId == Compound.BranchUnitId;
				}))
			{
				Mark.OutcomeGroupIds.Add(Rail.GroupId);
			}
		}
		bool bAllPairsProven = Mark.OutcomeGroupIds.Num() >= 2;
		for (int32 A = 0; A < Mark.OutcomeGroupIds.Num(); ++A)
		{
			for (int32 B = A + 1; B < Mark.OutcomeGroupIds.Num(); ++B)
			{
				bAllPairsProven &= HasIncomparablePair(
					Result.IncomparableGroupIds,
					Mark.OutcomeGroupIds[A],
					Mark.OutcomeGroupIds[B]);
			}
		}
		if (!bAllPairsProven)
		{
			return Fail(MoveTemp(Result), TEXT("LC2_GUARD_SURFACE_FORK_MARK_MISSING"));
		}
		Mark.ReaderText = TEXT("NO ORDER PROVEN BETWEEN THESE EXITS");
		Result.ForkMarks.Add(MoveTemp(Mark));
	}

	Result.Status = EBlueprintLensLC2GuardSurfaceProjectionStatus::GuardRails;
	Result.DiagnosticCode = TEXT("LC2_GUARD_SURFACE_COMPLETE");
	Result.ProjectionIntegrityHash = CalculateIntegrity(Result);
	return Result;
}
