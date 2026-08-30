#include "BlueprintLensLC2GuardOutlineProjection.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR LC2ProjectorVersion[] =
	TEXT("BlueprintLens.LC2GuardOutlineProjector.v1");

void AppendValue(
	FString& Canonical,
	const TCHAR* Label,
	const FString& Value)
{
	Canonical += Label;
	Canonical += FString::Printf(
		TEXT("[%d:%s]"),
		Value.Len(),
		*Value);
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

void AppendBool(
	FString& Canonical,
	const TCHAR* Label,
	const bool bValue)
{
	AppendValue(Canonical, Label, bValue ? TEXT("1") : TEXT("0"));
}

FString CalculateProjectionIntegrityHash(
	const FBlueprintLensLC2GuardOutlineProjection& Projection)
{
	FString Canonical;
	AppendValue(
		Canonical,
		TEXT("version"),
		Projection.ProjectorVersion);
	AppendValue(
		Canonical,
		TEXT("source-ir"),
		Projection.SourceIrSha256);
	AppendValue(
		Canonical,
		TEXT("criterion"),
		Projection.CriterionUnitId);
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	AppendIds(
		Canonical,
		TEXT("all-units"),
		Projection.AllUnitIds);
	AppendIds(
		Canonical,
		TEXT("all-relations"),
		Projection.AllRelationIds);
	AppendIds(
		Canonical,
		TEXT("fallback-units"),
		Projection.FallbackUnitIds);
	AppendIds(
		Canonical,
		TEXT("fallback-relations"),
		Projection.FallbackRelationIds);
	for (const FBlueprintLensLC2GuardOutlineNest& Nest :
		 Projection.GuardNests)
	{
		AppendValue(Canonical, TEXT("nest-group"), Nest.GroupId);
		AppendValue(Canonical, TEXT("nest-label"), Nest.ReaderLabel);
		AppendIds(Canonical, TEXT("nest-units"), Nest.OrderedUnitIds);
		AppendValue(Canonical, TEXT("nest-parent"), Nest.ParentGroupId);
		AppendBool(Canonical, TEXT("nest-has-parent"), Nest.bHasParent);
		AppendValue(
			Canonical,
			TEXT("nest-parent-label"),
			Nest.ParentReaderLabel);
		AppendBool(
			Canonical,
			TEXT("nest-has-entered-by"),
			Nest.bHasEnteredBy);
		AppendValue(
			Canonical,
			TEXT("nest-entered-by"),
			FString(LexToString(Nest.EnteredBy)));
	}
	for (const FBlueprintLensLC2GuardOutlinePath& Path :
		 Projection.OutcomePaths)
	{
		AppendValue(Canonical, TEXT("path-group"), Path.GroupId);
		AppendValue(Canonical, TEXT("path-title"), Path.Title);
		AppendValue(Canonical, TEXT("path-entry"), Path.EntryUnitId);
		AppendValue(Canonical, TEXT("path-exit"), Path.ExitUnitId);
		AppendIds(
			Canonical,
			TEXT("path-relations"),
			Path.OrderedRelationIds);
		for (const FBlueprintLensLC2GuardOutlineRow& Row : Path.Rows)
		{
			AppendValue(Canonical, TEXT("row-unit"), Row.UnitId);
			AppendValue(Canonical, TEXT("row-label"), Row.ReaderLabel);
			AppendValue(Canonical, TEXT("row-next"), Row.NextRelationId);
			AppendBool(
				Canonical,
				TEXT("row-has-next"),
				Row.bHasNextRelation);
			AppendValue(
				Canonical,
				TEXT("row-next-semantic"),
				FString(LexToString(Row.NextRelationSemanticLabel)));
			AppendValue(
				Canonical,
				TEXT("row-depth"),
				FString::FromInt(Row.NestingDepth));
			AppendValue(Canonical, TEXT("row-guard"), Row.GuardGroupId);
			AppendValue(
				Canonical,
				TEXT("row-parent-guard"),
				Row.ParentGuardGroupId);
			AppendBool(
				Canonical,
				TEXT("row-has-parent-guard"),
				Row.bHasParentGuard);
			AppendValue(
				Canonical,
				TEXT("row-guard-label"),
				Row.GuardReaderLabel);
			AppendValue(
				Canonical,
				TEXT("row-parent-label"),
				Row.ParentGuardReaderLabel);
			AppendBool(
				Canonical,
				TEXT("row-has-entered-by"),
				Row.bHasEnteredBy);
			AppendValue(
				Canonical,
				TEXT("row-entered-by"),
				FString(LexToString(Row.EnteredBy)));
		}
	}
	for (const TPair<FString, FString>& Pair :
		 Projection.IncomparableGroupIds)
	{
		AppendValue(Canonical, TEXT("incomparable-first"), Pair.Key);
		AppendValue(Canonical, TEXT("incomparable-second"), Pair.Value);
	}
	AppendValue(
		Canonical,
		TEXT("partial-order-semantics"),
		Projection.GroupPartialOrderSemantics);
	AppendValue(
		Canonical,
		TEXT("diagnostic"),
		Projection.DiagnosticCode);
	return FMD5::HashAnsiString(*Canonical);
}

FString ReaderLabel(const FBlueprintLensUnit& Unit)
{
	if (Unit.bHasDisambiguator && !Unit.Disambiguator.Text.IsEmpty())
	{
		return FString::Printf(
			TEXT("%s (%s)"),
			*Unit.Title,
			*Unit.Disambiguator.Text);
	}
	return Unit.Title;
}

FBlueprintLensLC2GuardOutlineProjection SetFallback(
	FBlueprintLensLC2GuardOutlineProjection Projection,
	const TCHAR* DiagnosticCode)
{
	Projection.Status =
		EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback;
	Projection.DiagnosticCode = DiagnosticCode;
	Projection.OutcomePaths.Reset();
	Projection.GuardNests.Reset();
	Projection.IncomparableGroupIds.Reset();
	Projection.GroupPartialOrderSemantics.Reset();
	Projection.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Projection);
	return Projection;
}

bool HasCompleteProjectionGroups(
	const FBlueprintLensExplanationModel& Explanation)
{
	for (const FBlueprintLensGroup& Group : Explanation.Groups)
	{
		if ((Group.Kind == EBlueprintLensGroupKind::OutcomePath ||
			 Group.Kind == EBlueprintLensGroupKind::GuardNest) &&
			Group.ProjectionStatus !=
				EBlueprintLensProjectionStatus::Complete)
		{
			return false;
		}
	}
	return true;
}

bool BuildGuardDepths(
	const FBlueprintLensExplanationModel& Explanation,
	const TArray<const FBlueprintLensGroup*>& GuardGroups,
	TMap<FString, int32>& OutDepths)
{
	for (const FBlueprintLensGroup* GuardGroup : GuardGroups)
	{
		if (GuardGroup == nullptr)
		{
			return false;
		}
		int32 Depth = 1;
		FString CurrentParentId = GuardGroup->bHasParent
			? GuardGroup->ParentGroupId
			: FString();
		TSet<FString> SeenGroupIds;
		SeenGroupIds.Add(GuardGroup->Id);
		while (!CurrentParentId.IsEmpty())
		{
			if (SeenGroupIds.Contains(CurrentParentId))
			{
				return false;
			}
			SeenGroupIds.Add(CurrentParentId);
			const FBlueprintLensGroup* Parent =
				Explanation.FindGroup(CurrentParentId);
			if (Parent == nullptr ||
				Parent->Kind != EBlueprintLensGroupKind::GuardNest)
			{
				return false;
			}
			++Depth;
			CurrentParentId = Parent->bHasParent
				? Parent->ParentGroupId
				: FString();
		}
		OutDepths.Add(GuardGroup->Id, Depth);
	}
	return true;
}

bool ContainsCriterion(
	const TArray<FBlueprintLensLC2GuardOutlinePath>& Paths,
	const FString& CriterionUnitId)
{
	for (const FBlueprintLensLC2GuardOutlinePath& Path : Paths)
	{
		if (Path.EntryUnitId == CriterionUnitId ||
			Path.ExitUnitId == CriterionUnitId)
		{
			return true;
		}
		for (const FBlueprintLensLC2GuardOutlineRow& Row : Path.Rows)
		{
			if (Row.UnitId == CriterionUnitId)
			{
				return true;
			}
		}
	}
	return false;
}
} // namespace

bool FBlueprintLensLC2GuardOutlineProjection::HasValidIntegrity() const
{
	return ProjectorVersion == LC2ProjectorVersion &&
		!SourceIrSha256.IsEmpty() &&
		!ProjectionIntegrityHash.IsEmpty() &&
		ProjectionIntegrityHash.Equals(
			CalculateProjectionIntegrityHash(*this),
			ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC2GuardOutlineProjection::IsRenderable() const
{
	return Status !=
			EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable &&
		HasValidIntegrity() &&
		!CriterionUnitId.IsEmpty() &&
		AllUnitIds.Num() == 9 &&
		AllRelationIds.Num() == 10 &&
		FallbackUnitIds == AllUnitIds &&
		FallbackRelationIds == AllRelationIds &&
		(Status ==
				EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback ||
			(OutcomePaths.Num() == 3 && GuardNests.Num() == 2 &&
				IncomparableGroupIds.Num() == 3));
}

FBlueprintLensLC2GuardOutlineProjection
FBlueprintLensLC2GuardOutlineProjector::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	FBlueprintLensLC2GuardOutlineProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = LC2ProjectorVersion;
	Result.CriterionUnitId = Explanation.CriterionUnitId;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.Id.IsEmpty() || Result.AllUnitIds.Contains(Unit.Id))
		{
			Result.Status =
				EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable;
			Result.DiagnosticCode = TEXT("LC2_GUARD_OUTLINE_UNIT_LEDGER_INVALID");
			Result.ProjectionIntegrityHash =
				CalculateProjectionIntegrityHash(Result);
			return Result;
		}
		Result.AllUnitIds.Add(Unit.Id);
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Id.IsEmpty() || Result.AllRelationIds.Contains(Relation.Id))
		{
			Result.Status =
				EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable;
			Result.DiagnosticCode =
				TEXT("LC2_GUARD_OUTLINE_RELATION_LEDGER_INVALID");
			Result.ProjectionIntegrityHash =
				CalculateProjectionIntegrityHash(Result);
			return Result;
		}
		Result.AllRelationIds.Add(Relation.Id);
	}
	Result.FallbackUnitIds = Result.AllUnitIds;
	Result.FallbackRelationIds = Result.AllRelationIds;

	if (Result.AllUnitIds.Num() != 9 || Result.AllRelationIds.Num() != 10)
	{
		Result.Status =
			EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable;
		Result.DiagnosticCode = TEXT("LC2_GUARD_OUTLINE_LEDGER_SIZE_INVALID");
		Result.ProjectionIntegrityHash =
			CalculateProjectionIntegrityHash(Result);
		return Result;
	}
	if (Explanation.FindUnit(Result.CriterionUnitId) == nullptr)
	{
		Result.Status =
			EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable;
		Result.DiagnosticCode = TEXT("LC2_GUARD_OUTLINE_CRITERION_INVALID");
		Result.ProjectionIntegrityHash =
			CalculateProjectionIntegrityHash(Result);
		return Result;
	}

	if (!Explanation.bHasGroups || Explanation.Groups.IsEmpty())
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_GROUPS_ABSENT"));
	}
	if (!Explanation.bHasGroupPartialOrder ||
		Explanation.GroupPartialOrder.Semantics.IsEmpty())
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_PARTIAL_ORDER_ABSENT"));
	}
	if (!HasCompleteProjectionGroups(Explanation))
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_GROUP_INCOMPLETE"));
	}

	TArray<const FBlueprintLensGroup*> OutcomeGroups;
	TArray<const FBlueprintLensGroup*> GuardGroups;
	for (const FBlueprintLensGroup& Group : Explanation.Groups)
	{
		if (Group.Kind == EBlueprintLensGroupKind::OutcomePath)
		{
			OutcomeGroups.Add(&Group);
		}
		else if (Group.Kind == EBlueprintLensGroupKind::GuardNest)
		{
			GuardGroups.Add(&Group);
		}
	}
	if (OutcomeGroups.Num() != 3 || GuardGroups.Num() != 2 ||
		Explanation.GroupPartialOrder.IncomparableGroupIds.Num() != 3)
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_GROUP_SET_INVALID"));
	}

	TSet<FString> OutcomeGroupIds;
	for (const FBlueprintLensGroup* Group : OutcomeGroups)
	{
		if (Group == nullptr || OutcomeGroupIds.Contains(Group->Id))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_GROUP_SET_INVALID"));
		}
		OutcomeGroupIds.Add(Group->Id);
		if (Group->ExitUnitId == Result.CriterionUnitId)
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_CRITERION_IN_PATH"));
		}
	}
	for (const TPair<FString, FString>& Pair :
		 Explanation.GroupPartialOrder.IncomparableGroupIds)
	{
		if (Pair.Key == Pair.Value ||
			!OutcomeGroupIds.Contains(Pair.Key) ||
			!OutcomeGroupIds.Contains(Pair.Value))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_PARTIAL_ORDER_INVALID"));
		}
		Result.IncomparableGroupIds.Add(Pair);
	}
	Result.GroupPartialOrderSemantics =
		Explanation.GroupPartialOrder.Semantics;

	TMap<FString, int32> GuardDepths;
	if (!BuildGuardDepths(Explanation, GuardGroups, GuardDepths))
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_NESTING_INVALID"));
	}
	for (const FBlueprintLensGroup* Group : GuardGroups)
	{
		if (Group == nullptr)
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_NESTING_INVALID"));
		}
		const FBlueprintLensUnit* EntryUnit =
			Explanation.FindUnit(Group->EntryUnitId);
		if (EntryUnit == nullptr || !EntryUnit->bHasDisambiguator ||
			EntryUnit->Disambiguator.Text.IsEmpty())
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_DISAMBIGUATOR_UNAVAILABLE"));
		}
		FBlueprintLensLC2GuardOutlineNest Nest;
		Nest.GroupId = Group->Id;
		Nest.ReaderLabel = ReaderLabel(*EntryUnit);
		Nest.OrderedUnitIds = Group->OrderedUnitIds;
		Nest.bHasParent = Group->bHasParent;
		Nest.ParentGroupId = Group->ParentGroupId;
		Nest.bHasEnteredBy = Group->bHasEnteredBy;
		Nest.EnteredBy = Group->EnteredBy;
		if (Nest.bHasParent)
		{
			const FBlueprintLensGroup* Parent =
				Explanation.FindGroup(Nest.ParentGroupId);
			if (Parent == nullptr ||
				Parent->Kind != EBlueprintLensGroupKind::GuardNest)
			{
				return SetFallback(
					MoveTemp(Result),
					TEXT("LC2_GUARD_OUTLINE_NESTING_INVALID"));
			}
			const FBlueprintLensUnit* ParentEntryUnit =
				Explanation.FindUnit(Parent->EntryUnitId);
			if (ParentEntryUnit == nullptr ||
				!ParentEntryUnit->bHasDisambiguator ||
				ParentEntryUnit->Disambiguator.Text.IsEmpty())
			{
				return SetFallback(
					MoveTemp(Result),
					TEXT("LC2_GUARD_OUTLINE_DISAMBIGUATOR_UNAVAILABLE"));
			}
			Nest.ParentReaderLabel = ReaderLabel(*ParentEntryUnit);
		}
		Result.GuardNests.Add(MoveTemp(Nest));
	}

	for (const FBlueprintLensGroup* Group : OutcomeGroups)
	{
		if (Group == nullptr ||
			Group->OrderedRelationIds.Num() !=
				Group->OrderedUnitIds.Num() - 1)
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC2_GUARD_OUTLINE_PATH_CHAIN_INVALID"));
		}
		FBlueprintLensLC2GuardOutlinePath Path;
		Path.GroupId = Group->Id;
		Path.Title = Group->Title;
		Path.EntryUnitId = Group->EntryUnitId;
		Path.ExitUnitId = Group->ExitUnitId;
		Path.OrderedRelationIds = Group->OrderedRelationIds;
		for (int32 UnitIndex = 0;
			 UnitIndex < Group->OrderedUnitIds.Num();
			 ++UnitIndex)
		{
			const FString& UnitId = Group->OrderedUnitIds[UnitIndex];
			const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
			if (Unit == nullptr)
			{
				return SetFallback(
					MoveTemp(Result),
					TEXT("LC2_GUARD_OUTLINE_PATH_UNIT_INVALID"));
			}
			FBlueprintLensLC2GuardOutlineRow Row;
			Row.UnitId = UnitId;
			Row.ReaderLabel = ReaderLabel(*Unit);
			int32 InnermostDepth = 0;
			const FBlueprintLensLC2GuardOutlineNest* InnermostNest = nullptr;
			const FBlueprintLensGroup* InnermostGroup = nullptr;
			for (const FBlueprintLensLC2GuardOutlineNest& Nest :
				 Result.GuardNests)
			{
				if (!Nest.OrderedUnitIds.Contains(UnitId))
				{
					continue;
				}
				const int32* Depth = GuardDepths.Find(Nest.GroupId);
				if (Depth != nullptr && *Depth > InnermostDepth)
				{
					InnermostDepth = *Depth;
					InnermostNest = &Nest;
					InnermostGroup = Explanation.FindGroup(Nest.GroupId);
				}
			}
			Row.NestingDepth = InnermostDepth;
			if (InnermostGroup != nullptr &&
				InnermostGroup->EntryUnitId != UnitId)
			{
				++Row.NestingDepth;
			}
			if (InnermostNest != nullptr)
			{
				Row.GuardGroupId = InnermostNest->GroupId;
				Row.bHasParentGuard = InnermostNest->bHasParent;
				Row.ParentGuardGroupId = InnermostNest->ParentGroupId;
				Row.GuardReaderLabel = InnermostNest->ReaderLabel;
				Row.ParentGuardReaderLabel = InnermostNest->ParentReaderLabel;
				Row.bHasEnteredBy = InnermostNest->bHasEnteredBy;
				Row.EnteredBy = InnermostNest->EnteredBy;
			}
			if (UnitIndex < Group->OrderedRelationIds.Num())
			{
				const FString& RelationId =
					Group->OrderedRelationIds[UnitIndex];
				const FBlueprintLensRelation* Relation =
					Explanation.FindRelation(RelationId);
				if (Relation == nullptr || !Relation->bHasSemanticLabel)
				{
					return SetFallback(
						MoveTemp(Result),
						TEXT("LC2_GUARD_OUTLINE_RELATION_SEMANTICS_UNAVAILABLE"));
				}
				Row.NextRelationId = RelationId;
				Row.bHasNextRelation = true;
				Row.NextRelationSemanticLabel = Relation->SemanticLabel;
			}
			Path.Rows.Add(MoveTemp(Row));
		}
		Result.OutcomePaths.Add(MoveTemp(Path));
	}

	if (ContainsCriterion(Result.OutcomePaths, Result.CriterionUnitId))
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC2_GUARD_OUTLINE_CRITERION_IN_PATH"));
	}
	Result.Status =
		EBlueprintLensLC2GuardOutlineProjectionStatus::GroupedOutcomePaths;
	Result.DiagnosticCode = TEXT("LC2_GUARD_OUTLINE_COMPLETE");
	Result.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Result);
	return Result;
}
