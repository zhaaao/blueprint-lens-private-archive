#include "BlueprintLensLC7Projection.h"

#include "IPlatformCrypto.h"

namespace
{
constexpr TCHAR ProjectionId[] = TEXT("LC7_CANONICAL_RECURRENCE_BACKBONE");

const TCHAR* LexToString(const EBlueprintLensLC7RelationFamily Family)
{
	switch (Family)
	{
	case EBlueprintLensLC7RelationFamily::Entry: return TEXT("entry");
	case EBlueprintLensLC7RelationFamily::Predicate: return TEXT("predicate");
	case EBlueprintLensLC7RelationFamily::Value: return TEXT("value");
	case EBlueprintLensLC7RelationFamily::Forward: return TEXT("forward");
	case EBlueprintLensLC7RelationFamily::Return: return TEXT("return");
	case EBlueprintLensLC7RelationFamily::Exit: return TEXT("exit");
	default: return TEXT("unknown");
	}
}

FString HashCanonical(const TArray<FString>& Lines)
{
	const FString Text = FString::Join(Lines, TEXT("\n"));
	FTCHARToUTF8 Converted(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
}

bool BuildCompleteText(
	const FBlueprintLensLC7Profile& Profile,
	FBlueprintLensLC7Projection& Projection)
{
	if (!Profile.ExplanationModel.IsValid())
	{
		return false;
	}
	const FBlueprintLensExplanationModel& Model = *Profile.ExplanationModel;
	if (Model.Query.Question.IsEmpty() || Model.CriterionUnitId.IsEmpty() ||
		Model.Units.IsEmpty() || Model.Relations.IsEmpty())
	{
		return false;
	}

	TSet<FString> UnitIds;
	TArray<const FBlueprintLensUnit*> Units;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		if (Unit.Id.IsEmpty() || Unit.Title.IsEmpty() || UnitIds.Contains(Unit.Id) ||
			Unit.SourceReferences.IsEmpty())
		{
			return false;
		}
		const FBlueprintLensSourceReference* Primary =
			Unit.SourceReferences.FindByPredicate([](const auto& Reference)
			{
				return Reference.bPrimary &&
					!Reference.BlueprintAssetPath.IsEmpty() &&
					!Reference.GraphId.IsEmpty() &&
					!Reference.SourceNodeId.IsEmpty();
			});
		if (Primary == nullptr)
		{
			return false;
		}
		UnitIds.Add(Unit.Id);
		Units.Add(&Unit);
	}
	if (!UnitIds.Contains(Model.CriterionUnitId))
	{
		return false;
	}

	TSet<FString> RelationIds;
	TArray<const FBlueprintLensRelation*> Relations;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Id.IsEmpty() || RelationIds.Contains(Relation.Id) ||
			!UnitIds.Contains(Relation.SourceUnitId) ||
			!UnitIds.Contains(Relation.TargetUnitId))
		{
			return false;
		}
		RelationIds.Add(Relation.Id);
		Relations.Add(&Relation);
	}
	Units.Sort([](const FBlueprintLensUnit& Left, const FBlueprintLensUnit& Right)
	{
		return Left.Id < Right.Id;
	});
	Relations.Sort([](
		const FBlueprintLensRelation& Left,
		const FBlueprintLensRelation& Right)
	{
		return Left.Id < Right.Id;
	});

	Projection.CompleteTextLines.Add(Model.Query.Question);
	for (const FBlueprintLensUnit* Unit : Units)
	{
		Projection.CompleteTextLines.Add(FString::Printf(
			TEXT("unit %s | %s | %s"), *Unit->Id,
			LexToString(Unit->Role), *Unit->Title));
	}
	for (const FBlueprintLensRelation* Relation : Relations)
	{
		Projection.CompleteTextLines.Add(FString::Printf(
			TEXT("relation %s | %s | %s -> %s | %s"),
			*Relation->Id, LexToString(Relation->Kind),
			*Relation->SourceUnitId, *Relation->TargetUnitId, *Relation->Label));
	}
	return true;
}

FBlueprintLensLC7Projection Fallback(
	const FBlueprintLensLC7Profile& Profile,
	const TCHAR* Code)
{
	FBlueprintLensLC7Projection Projection;
	Projection.DiagnosticCode = Code;
	Projection.Status = BuildCompleteText(Profile, Projection)
		? EBlueprintLensLC7ProjectionStatus::CompleteText
		: EBlueprintLensLC7ProjectionStatus::Unavailable;
	return Projection;
}

bool IsIn(const TArray<FString>& Values, const FString& Value)
{
	return Values.Contains(Value);
}

bool SameSet(const TArray<FString>& Left, const TArray<FString>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	TSet<FString> LeftSet(Left);
	TSet<FString> RightSet(Right);
	return LeftSet.Num() == Left.Num() && RightSet.Num() == Right.Num() &&
		LeftSet.Difference(RightSet).IsEmpty() &&
		RightSet.Difference(LeftSet).IsEmpty();
}

TArray<FString>& FamilyIds(
	FBlueprintLensLC7SCCRecord& SCC,
	const EBlueprintLensLC7RelationFamily Family)
{
	switch (Family)
	{
	case EBlueprintLensLC7RelationFamily::Entry: return SCC.EntryRelationIds;
	case EBlueprintLensLC7RelationFamily::Predicate:
		return SCC.PredicateRelationIds;
	case EBlueprintLensLC7RelationFamily::Value: return SCC.ValueRelationIds;
	case EBlueprintLensLC7RelationFamily::Forward: return SCC.ForwardRelationIds;
	case EBlueprintLensLC7RelationFamily::Return: return SCC.ReturnRelationIds;
	default: return SCC.ExitRelationIds;
	}
}
} // namespace

int32 FBlueprintLensLC7Projection::CountRelations(
	const EBlueprintLensLC7RelationFamily Family) const
{
	return Relations.FilterByPredicate([Family](const auto& Relation)
	{
		return Relation.Family == Family;
	}).Num();
}

FBlueprintLensLC7Projection FBlueprintLensLC7Projector::Build(
	const FBlueprintLensLC7Profile& Profile)
{
	FBlueprintLensLC7Projection Projection;
	if (!BuildCompleteText(Profile, Projection))
	{
		Projection.DiagnosticCode = TEXT("LC7_PROJECTION_TEXT_INVALID");
		return Projection;
	}
	if (!Profile.IsValid())
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_PROFILE_INVALID"));
	}
	const FBlueprintLensExplanationModel& Model = *Profile.ExplanationModel;
	const FBlueprintLensGroup* Group = nullptr;
	for (const FBlueprintLensGroup& Candidate : Model.Groups)
	{
		if (Candidate.Kind == EBlueprintLensGroupKind::Scc)
		{
			if (Group != nullptr)
			{
				return Fallback(Profile, TEXT("LC7_PROJECTION_SCC_COUNT"));
			}
			Group = &Candidate;
		}
	}
	if (Group == nullptr || Group->Id != Profile.SCC.GroupId ||
		Group->EntryUnitId != Profile.SCC.EntryUnitId ||
		Group->OrderedUnitIds != Profile.SCC.OrderedMemberUnitIds ||
		Group->OrderedRelationIds != Profile.SCC.OrderedRelationIds ||
		Group->MemberCount != Group->OrderedUnitIds.Num() ||
		(Profile.bLiveExplanation
			? Group->MemberCount > 6 ||
				Group->bHasExitUnitId == Profile.bExitOutsideSlice ||
				(Group->bHasExitUnitId &&
					Group->ExitUnitId != Profile.SCC.ExitUnitId)
			: Group->MemberCount != 3 || !Group->bHasExitUnitId ||
				Group->ExitUnitId != Profile.SCC.ExitUnitId))
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_SCC_BINDING"));
	}

	Projection.ProfileId = Profile.ProfileId;
	Projection.bLiveExplanation = Profile.bLiveExplanation;
	Projection.bExitOutsideSlice = Profile.bExitOutsideSlice;
	Projection.ClaimScope = Profile.ClaimScope;
	Projection.RuntimeIterations = Profile.RuntimeIterations;
	Projection.RelationFamilyStatement = Profile.RelationFamilyStatement;
	Projection.ExitBoundaryStatement = Profile.ExitBoundaryStatement;
	Projection.CriterionUnitId = Model.CriterionUnitId;
	Projection.ActionIds = {
		TEXT("inspect_cycle"), TEXT("open_source"),
		TEXT("show_complete_text")};

	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		if (Projection.AllUnitIds.Contains(Unit.Id))
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_UNIT_DUPLICATE"));
		}
		const FBlueprintLensSourceReference* Primary =
			Unit.SourceReferences.FindByPredicate([](const auto& Reference)
			{
				return Reference.bPrimary;
			});
		if (Primary == nullptr ||
			Primary->BlueprintAssetPath != Profile.BlueprintAssetPath ||
			Primary->GraphId != Profile.GraphId)
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_SOURCE_ANCHOR"));
		}
		Projection.AllUnitIds.Add(Unit.Id);
		Projection.UnitTitles.Add(Unit.Id, Unit.Title);
		Projection.SourceAnchors.Add(Unit.Id, *Primary);
	}

	TMap<FString, const FBlueprintLensLC7RelationBinding*> Bindings;
	for (const FBlueprintLensLC7RelationBinding& Binding : Profile.Relations)
	{
		if (Binding.RelationId.IsEmpty() || Bindings.Contains(Binding.RelationId))
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_RELATION_DUPLICATE"));
		}
		Bindings.Add(Binding.RelationId, &Binding);
	}
	if (Bindings.Num() != Model.Relations.Num())
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_RELATION_COVERAGE"));
	}

	FBlueprintLensLC7SCCRecord SCC;
	SCC.GroupId = Group->Id;
	SCC.EntryUnitId = Group->EntryUnitId;
	SCC.ExitUnitId = Group->ExitUnitId;
	SCC.CriterionUnitId = Model.CriterionUnitId;
	SCC.OrderedSpineUnitIds = Group->OrderedUnitIds;
	TArray<FString> InternalRelationIds;
	for (const FBlueprintLensLC7RelationBinding& Binding : Profile.Relations)
	{
		if (IsIn(Profile.SCC.InternalEdgeIds, Binding.SourceEdgeId))
		{
			InternalRelationIds.Add(Binding.RelationId);
		}
	}
	if (!SameSet(InternalRelationIds, Profile.SCC.OrderedRelationIds))
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_INTERNAL_OMISSION"));
	}

	for (const FBlueprintLensRelation& SourceRelation : Model.Relations)
	{
		const FBlueprintLensLC7RelationBinding* const* BindingValue =
			Bindings.Find(SourceRelation.Id);
		if (BindingValue == nullptr || *BindingValue == nullptr)
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_RELATION_MISSING"));
		}
		const FBlueprintLensLC7RelationBinding& Binding = **BindingValue;
		if (Binding.SourceUnitId != SourceRelation.SourceUnitId ||
			Binding.TargetUnitId != SourceRelation.TargetUnitId ||
			Binding.SourceEdgeId.IsEmpty() || Binding.SourcePinId.IsEmpty() ||
			Binding.TargetPinId.IsEmpty() ||
			!Projection.SourceAnchors.Contains(Binding.SourceUnitId) ||
			!Projection.SourceAnchors.Contains(Binding.TargetUnitId))
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_RELATION_BINDING"));
		}

		const bool bInternal = IsIn(Profile.SCC.InternalEdgeIds, Binding.SourceEdgeId);
		const bool bIncoming = IsIn(Profile.SCC.IncomingEdgeIds, Binding.SourceEdgeId);
		const bool bOutgoing = IsIn(Profile.SCC.OutgoingEdgeIds, Binding.SourceEdgeId);
		const bool bReturning =
			IsIn(Profile.SCC.ReturningEdgeIds, Binding.SourceEdgeId);
		if ((bIncoming && (bInternal || bOutgoing || bReturning)) ||
			(bOutgoing && (bInternal || bReturning)) ||
			(bReturning && !bInternal) || Binding.bReturning != bReturning)
		{
			return Fallback(Profile, TEXT("LC7_PROJECTION_FAMILY_OVERLAP"));
		}

		EBlueprintLensLC7RelationFamily Family;
		if (SourceRelation.Kind == EBlueprintLensRelationKind::PredicateFor)
		{
			Family = EBlueprintLensLC7RelationFamily::Predicate;
		}
		else if (SourceRelation.Kind == EBlueprintLensRelationKind::ProvidesValue)
		{
			Family = EBlueprintLensLC7RelationFamily::Value;
		}
		else if (bReturning)
		{
			Family = EBlueprintLensLC7RelationFamily::Return;
		}
		else if (bInternal)
		{
			Family = EBlueprintLensLC7RelationFamily::Forward;
		}
		else if (bOutgoing)
		{
			Family = EBlueprintLensLC7RelationFamily::Exit;
		}
		else
		{
			Family = EBlueprintLensLC7RelationFamily::Entry;
		}

		FBlueprintLensLC7Relation Relation;
		Relation.RelationId = SourceRelation.Id;
		Relation.OwningSCCId = Group->Id;
		Relation.Family = Family;
		Relation.Kind = SourceRelation.Kind;
		Relation.Label = SourceRelation.Label;
		Relation.SourceUnitId = Binding.SourceUnitId;
		Relation.TargetUnitId = Binding.TargetUnitId;
		Relation.SourceEdgeId = Binding.SourceEdgeId;
		Relation.SourceNodeId = Binding.SourceNodeId;
		Relation.TargetNodeId = Binding.TargetNodeId;
		Relation.SourcePinId = Binding.SourcePinId;
		Relation.TargetPinId = Binding.TargetPinId;
		Projection.Relations.Add(MoveTemp(Relation));
		Projection.AllRelationIds.Add(SourceRelation.Id);
		FamilyIds(SCC, Family).Add(SourceRelation.Id);
	}
	const bool bFamilyPartitionBoundExceeded = Profile.bLiveExplanation
		? Projection.AllUnitIds.Num() > 10 ||
			Projection.AllRelationIds.Num() > 10 ||
			SCC.EntryRelationIds.Num() > 2 ||
			SCC.PredicateRelationIds.Num() > 1 ||
			SCC.ValueRelationIds.Num() > 1 ||
			SCC.ForwardRelationIds.Num() > 5 ||
			SCC.ReturnRelationIds.Num() > 1 ||
			SCC.ExitRelationIds.Num() > 1
		: Projection.AllUnitIds.Num() != 8 ||
			Projection.AllRelationIds.Num() != 8 ||
			SCC.EntryRelationIds.Num() != 2 ||
			SCC.PredicateRelationIds.Num() != 1 ||
			SCC.ValueRelationIds.Num() != 1 ||
			SCC.ForwardRelationIds.Num() != 2 ||
			SCC.ReturnRelationIds.Num() != 1 ||
			SCC.ExitRelationIds.Num() != 1;
	if (bFamilyPartitionBoundExceeded)
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_FAMILY_PARTITION"));
	}
	for (TArray<FString>* Family : {
		&SCC.EntryRelationIds, &SCC.PredicateRelationIds,
		&SCC.ValueRelationIds, &SCC.ForwardRelationIds,
		&SCC.ReturnRelationIds, &SCC.ExitRelationIds})
	{
		Family->Sort();
	}
	Projection.Relations.Sort([](const auto& Left, const auto& Right)
	{
		return Left.RelationId < Right.RelationId;
	});
	Projection.SCCs.Add(MoveTemp(SCC));

	TArray<FString> Canonical = {
		ProjectionId, Projection.ProfileId, Projection.ClaimScope,
		Projection.RuntimeIterations, Projection.CriterionUnitId,
		FString::Join(Projection.ActionIds, TEXT("|"))};
	const FBlueprintLensLC7SCCRecord& CanonicalSCC = Projection.SCCs[0];
	const TArray<FString> SCCIdentity = {
		CanonicalSCC.GroupId, CanonicalSCC.EntryUnitId,
		CanonicalSCC.ExitUnitId, CanonicalSCC.CriterionUnitId};
	Canonical.Add(FString::Join(SCCIdentity, TEXT("|")));
	Canonical.Add(FString::Join(CanonicalSCC.OrderedSpineUnitIds, TEXT("|")));
	TArray<FString> SortedUnitIds = Projection.AllUnitIds.Array();
	SortedUnitIds.Sort();
	for (const FString& UnitId : SortedUnitIds)
	{
		const FBlueprintLensUnit* Unit = Model.FindUnit(UnitId);
		const FBlueprintLensSourceReference& Anchor =
			Projection.SourceAnchors.FindChecked(UnitId);
		const TArray<FString> UnitIdentity = {
			UnitId, Unit == nullptr ? FString() : Unit->Title,
			Anchor.BlueprintAssetPath, Anchor.GraphId,
			Anchor.SourceNodeId, Anchor.NativeNodeGuid};
		Canonical.Add(FString::Join(UnitIdentity, TEXT("|")));
	}
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		const TArray<FString> RelationIdentity = {
			Relation.RelationId, LexToString(Relation.Family),
			LexToString(Relation.Kind), Relation.Label,
			Relation.SourceUnitId, Relation.TargetUnitId,
			Relation.SourceEdgeId, Relation.SourceNodeId,
			Relation.TargetNodeId, Relation.SourcePinId,
			Relation.TargetPinId, Relation.OwningSCCId};
		Canonical.Add(FString::Join(RelationIdentity, TEXT("|")));
	}
	Projection.IntegrityHash = HashCanonical(Canonical);
	if (Projection.IntegrityHash.IsEmpty())
	{
		return Fallback(Profile, TEXT("LC7_PROJECTION_HASH_FAILED"));
	}
	Projection.Status = EBlueprintLensLC7ProjectionStatus::AdaptiveBackbone;
	return Projection;
}
