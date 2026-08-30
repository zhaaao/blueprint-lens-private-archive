#include "BlueprintLensLC1RegionProjection.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR VariableSetClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
constexpr TCHAR RegionProjectorVersion[] =
	TEXT("BlueprintLens.LC1RegionProjector.v1");

FBlueprintLensLC1RegionProjection Failure(const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC1RegionProjection Result;
	Result.DiagnosticCode = DiagnosticCode;
	return Result;
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

FString CalculateProjectionIntegrityHash(
	const FBlueprintLensLC1RegionProjection& Region)
{
	FString Canonical;
	AppendValue(
		Canonical,
		TEXT("version"),
		Region.ProjectorVersion);
	AppendValue(
		Canonical,
		TEXT("source-ir"),
		Region.SourceIrSha256);
	AppendValue(Canonical, TEXT("region-id"), Region.RegionId);
	AppendValue(Canonical, TEXT("region-kind"), Region.RegionKind);
	AppendIds(
		Canonical,
		TEXT("members"),
		Region.OrderedMemberUnitIds);
	AppendIds(
		Canonical,
		TEXT("internal"),
		Region.InternalRelationIds);
	AppendIds(
		Canonical,
		TEXT("incoming"),
		Region.IncomingRelationIds);
	AppendIds(
		Canonical,
		TEXT("outgoing"),
		Region.OutgoingRelationIds);
	AppendValue(
		Canonical,
		TEXT("first-member"),
		Region.FirstMemberUnitId);
	AppendValue(
		Canonical,
		TEXT("last-member"),
		Region.LastMemberUnitId);
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Region.Status)));
	AppendValue(
		Canonical,
		TEXT("template"),
		Region.SummaryTemplateId);
	AppendIds(
		Canonical,
		TEXT("arguments"),
		Region.SummaryArguments);
	for (const FBlueprintLensLC1ClaimEvidence& Evidence :
		 Region.ClaimEvidence)
	{
		AppendValue(
			Canonical,
			TEXT("claim-part"),
			Evidence.ClaimPart);
		AppendValue(
			Canonical,
			TEXT("fact-owner"),
			Evidence.FactOwner);
		AppendValue(
			Canonical,
			TEXT("source-id"),
			Evidence.SourceId);
		AppendValue(
			Canonical,
			TEXT("value"),
			Evidence.Value);
	}
	AppendValue(
		Canonical,
		TEXT("diagnostic"),
		Region.DiagnosticCode);
	return FMD5::HashAnsiString(*Canonical);
}

bool AddUniqueIds(
	const TArray<FString>& Ids,
	TSet<FString>& InOutIds)
{
	for (const FString& Id : Ids)
	{
		if (Id.IsEmpty() || InOutIds.Contains(Id))
		{
			return false;
		}
		InOutIds.Add(Id);
	}
	return true;
}

const FBlueprintLensRelation* FindRelation(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& RelationId)
{
	return Explanation.Relations.FindByPredicate(
		[&RelationId](const FBlueprintLensRelation& Relation)
		{
			return Relation.Id == RelationId;
		});
}

bool IsExactLC1Target(
	const FString& Target,
	const int32 OneBasedIndex)
{
	return Target == FString::Printf(
		TEXT("LC1Step%02dComplete"),
		OneBasedIndex);
}

FString JoinIds(const TArray<FString>& Ids)
{
	return FString::Join(Ids, TEXT(","));
}

FBlueprintLensLC1ClaimEvidence Evidence(
	const TCHAR* ClaimPart,
	const TCHAR* FactOwner,
	const FString& SourceId,
	const FString& Value)
{
	FBlueprintLensLC1ClaimEvidence Result;
	Result.ClaimPart = ClaimPart;
	Result.FactOwner = FactOwner;
	Result.SourceId = SourceId;
	Result.Value = Value;
	return Result;
}
} // namespace

bool FBlueprintLensLC1RegionProjection::HasValidIntegrity() const
{
	return ProjectorVersion == RegionProjectorVersion
		&& !SourceIrSha256.IsEmpty()
		&& !ProjectionIntegrityHash.IsEmpty()
		&& ProjectionIntegrityHash.Equals(
			CalculateProjectionIntegrityHash(*this),
			ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC1RegionProjection::IsRenderable() const
{
	return Status != EBlueprintLensLC1RegionProjectionStatus::Unavailable
		&& HasValidIntegrity()
		&& !RegionId.IsEmpty()
		&& !RegionKind.IsEmpty()
		&& OrderedMemberUnitIds.Num() == 12
		&& InternalRelationIds.Num() == 11
		&& IncomingRelationIds.Num() == 1
		&& OutgoingRelationIds.Num() == 1
		&& !SummaryTemplateId.IsEmpty();
}

FBlueprintLensLC1RegionProjection FBlueprintLensLC1RegionProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts)
{
	if (!Layout.IsReady() || Layout.Segments.Num() != 3
		|| Layout.Segments[0].Kind
			!= EBlueprintLensFrameFlowSegmentKind::Entry
		|| Layout.Segments[1].Kind
			!= EBlueprintLensFrameFlowSegmentKind::StraightRun
		|| Layout.Segments[2].Kind
			!= EBlueprintLensFrameFlowSegmentKind::CriterionFocus
		|| Layout.Segments[1].MemberUnitIds.Num() != 12
		|| Explanation.Units.Num() != 14
		|| Explanation.Relations.Num() != 13
		|| Layout.TruthCounts.UnitCount != 14
		|| Layout.TruthCounts.RelationCount != 13)
	{
		return Failure(TEXT("LC1_REGION_MEMBERSHIP_INVALID"));
	}

	const FBlueprintLensFrameFlowSegment& Run = Layout.Segments[1];
	TSet<FString> MemberIds;
	if (!AddUniqueIds(Run.MemberUnitIds, MemberIds))
	{
		return Failure(TEXT("LC1_REGION_MEMBERSHIP_INVALID"));
	}

	TArray<FString> SourceNodeIds;
	for (const FString& MemberUnitId : Run.MemberUnitIds)
	{
		const FBlueprintLensUnit* Unit =
			Explanation.FindUnit(MemberUnitId);
		if (Unit == nullptr
			|| Unit->SemanticStatus
				!= EBlueprintLensSemanticStatus::Supported)
		{
			return Failure(TEXT("LC1_REGION_MEMBER_UNSUPPORTED"));
		}

		const FBlueprintLensSourceReference* PrimarySource = nullptr;
		for (const FBlueprintLensSourceReference& Reference :
			 Unit->SourceReferences)
		{
			if (!Reference.bPrimary)
			{
				continue;
			}
			if (PrimarySource != nullptr || Reference.SourceNodeId.IsEmpty())
			{
				return Failure(TEXT("LC1_REGION_PRIMARY_SOURCE_INVALID"));
			}
			PrimarySource = &Reference;
		}
		if (PrimarySource == nullptr)
		{
			return Failure(TEXT("LC1_REGION_PRIMARY_SOURCE_INVALID"));
		}
		SourceNodeIds.Add(PrimarySource->SourceNodeId);
	}

	TSet<FString> RelationIds;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Id.IsEmpty() || RelationIds.Contains(Relation.Id))
		{
			return Failure(TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
		}
		RelationIds.Add(Relation.Id);
	}

	TArray<FString> ExpectedInternalRelations;
	for (int32 Index = 0; Index < Run.MemberUnitIds.Num() - 1; ++Index)
	{
		const FString& SourceUnitId = Run.MemberUnitIds[Index];
		const FString& TargetUnitId = Run.MemberUnitIds[Index + 1];
		const FBlueprintLensRelation* Relation =
			Explanation.Relations.FindByPredicate(
				[&SourceUnitId, &TargetUnitId](
					const FBlueprintLensRelation& Candidate)
				{
					return Candidate.SourceUnitId == SourceUnitId
						&& Candidate.TargetUnitId == TargetUnitId
						&& Candidate.Kind
							== EBlueprintLensRelationKind::
								ExecutionPredecessor;
				});
		if (Relation == nullptr)
		{
			return Failure(TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
		}
		ExpectedInternalRelations.Add(Relation->Id);
	}

	TArray<FString> ExpectedIncomingRelations;
	TArray<FString> ExpectedOutgoingRelations;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		const bool bSourceIsMember =
			MemberIds.Contains(Relation.SourceUnitId);
		const bool bTargetIsMember =
			MemberIds.Contains(Relation.TargetUnitId);
		if (!bSourceIsMember && bTargetIsMember)
		{
			if (Relation.TargetUnitId != Run.MemberUnitIds[0]
				|| Relation.Kind
					!= EBlueprintLensRelationKind::ExecutionPredecessor)
			{
				return Failure(
					TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
			}
			ExpectedIncomingRelations.Add(Relation.Id);
		}
		else if (bSourceIsMember && !bTargetIsMember)
		{
			if (Relation.SourceUnitId != Run.MemberUnitIds.Last()
				|| Relation.Kind
					!= EBlueprintLensRelationKind::ExecutionPredecessor)
			{
				return Failure(
					TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
			}
			ExpectedOutgoingRelations.Add(Relation.Id);
		}
	}

	if (ExpectedInternalRelations.Num() != 11
		|| ExpectedIncomingRelations.Num() != 1
		|| ExpectedOutgoingRelations.Num() != 1
		|| Run.MemberRelationIds != ExpectedInternalRelations
		|| Run.IncomingRelationIds != ExpectedIncomingRelations
		|| Run.OutgoingRelationIds != ExpectedOutgoingRelations)
	{
		return Failure(TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
	}

	TSet<FString> OwnedRelationIds;
	if (!AddUniqueIds(Run.MemberRelationIds, OwnedRelationIds)
		|| !AddUniqueIds(Run.IncomingRelationIds, OwnedRelationIds)
		|| !AddUniqueIds(Run.OutgoingRelationIds, OwnedRelationIds)
		|| OwnedRelationIds.Num() != Explanation.Relations.Num())
	{
		return Failure(TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
	}
	for (const FString& OwnedRelationId : OwnedRelationIds)
	{
		if (FindRelation(Explanation, OwnedRelationId) == nullptr)
		{
			return Failure(TEXT("LC1_REGION_RELATION_OWNERSHIP_INVALID"));
		}
	}

	FBlueprintLensLC1RegionProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = RegionProjectorVersion;
	Result.OrderedMemberUnitIds = Run.MemberUnitIds;
	Result.InternalRelationIds = Run.MemberRelationIds;
	Result.IncomingRelationIds = Run.IncomingRelationIds;
	Result.OutgoingRelationIds = Run.OutgoingRelationIds;
	Result.FirstMemberUnitId = Run.MemberUnitIds[0];
	Result.LastMemberUnitId = Run.MemberUnitIds.Last();

	FString CanonicalRegionIdentity;
	AppendIds(
		CanonicalRegionIdentity,
		TEXT("members"),
		Result.OrderedMemberUnitIds);
	AppendIds(
		CanonicalRegionIdentity,
		TEXT("internal"),
		Result.InternalRelationIds);
	AppendIds(
		CanonicalRegionIdentity,
		TEXT("incoming"),
		Result.IncomingRelationIds);
	AppendIds(
		CanonicalRegionIdentity,
		TEXT("outgoing"),
		Result.OutgoingRelationIds);
	Result.RegionId = TEXT("lc1.region.")
		+ FMD5::HashAnsiString(*CanonicalRegionIdentity);

	const bool bTypedIrBound = TypedIrFacts.IsValid()
		&& !Explanation.Source.IrSha256.IsEmpty()
		&& TypedIrFacts.VerifiedIrSha256.Equals(
			Explanation.Source.IrSha256,
			ESearchCase::IgnoreCase);
	bool bAllVariableSet = bTypedIrBound;
	bool bTargetsMatch = bTypedIrBound;
	bool bAllBoolean = bTypedIrBound;
	bool bAllTrue = bTypedIrBound;
	TArray<const FBlueprintLensLC1OperationFact*> OrderedFacts;
	for (int32 Index = 0; Index < SourceNodeIds.Num(); ++Index)
	{
		const FBlueprintLensLC1OperationFact* Fact =
			TypedIrFacts.OperationsBySourceNodeId.Find(
				SourceNodeIds[Index]);
		OrderedFacts.Add(Fact);
		if (Fact == nullptr)
		{
			bAllVariableSet = false;
			bTargetsMatch = false;
			bAllBoolean = false;
			bAllTrue = false;
			continue;
		}
		bAllVariableSet =
			bAllVariableSet && Fact->OperationClass == VariableSetClass;
		bTargetsMatch =
			bTargetsMatch
			&& IsExactLC1Target(Fact->VariableTarget, Index + 1);
		bAllBoolean = bAllBoolean && Fact->ValueType == TEXT("bool");
		bAllTrue = bAllTrue && Fact->LiteralValue == TEXT("true");
	}

	const FString SourceNodeLedger = JoinIds(SourceNodeIds);
	const FString RelationLedger = JoinIds(Result.InternalRelationIds);
	const FString Count = FString::FromInt(Run.MemberUnitIds.Num());
	const FString Sequence =
		Result.FirstMemberUnitId + TEXT("->") + Result.LastMemberUnitId;
	if (bAllVariableSet && bTargetsMatch && bAllBoolean && bAllTrue)
	{
		Result.Status =
			EBlueprintLensLC1RegionProjectionStatus::
				CompleteOperationRegion;
		Result.RegionKind = TEXT("operation_region");
		Result.SummaryTemplateId =
			TEXT("set_completion_flags_true_in_sequence");
		Result.SummaryArguments = {
			Count,
			OrderedFacts[0]->VariableTarget,
			OrderedFacts.Last()->VariableTarget,
			TEXT("true")};
		Result.ClaimEvidence = {
			Evidence(
				TEXT("operation"),
				TEXT("typed_ir.operation_class"),
				SourceNodeLedger,
				VariableSetClass),
			Evidence(
				TEXT("count"),
				TEXT("layout.straight_run_members"),
				Run.Id,
				Count),
			Evidence(
				TEXT("target_family"),
				TEXT("typed_ir.variable_set_value.name"),
				SourceNodeLedger,
				TEXT("LC1Step01Complete..LC1Step12Complete")),
			Evidence(
				TEXT("literal_value"),
				TEXT("typed_ir.variable_set_value.default"),
				SourceNodeLedger,
				TEXT("true")),
			Evidence(
				TEXT("sequence"),
				TEXT("layout.execution_relations"),
				RelationLedger,
				Sequence)};
		Result.DiagnosticCode = TEXT("LC1_REGION_COMPLETE");
	}
	else if (bAllVariableSet)
	{
		Result.Status =
			EBlueprintLensLC1RegionProjectionStatus::
				OrderedVariableAssignments;
		Result.RegionKind = TEXT("operation_region");
		Result.SummaryTemplateId = TEXT("ordered_variable_assignments");
		Result.SummaryArguments = {Count};
		Result.ClaimEvidence = {
			Evidence(
				TEXT("operation"),
				TEXT("typed_ir.operation_class"),
				SourceNodeLedger,
				VariableSetClass),
			Evidence(
				TEXT("count"),
				TEXT("layout.straight_run_members"),
				Run.Id,
				Count),
			Evidence(
				TEXT("sequence"),
				TEXT("layout.execution_relations"),
				RelationLedger,
				Sequence)};
		Result.DiagnosticCode = !bTargetsMatch
			? TEXT("LC1_REGION_TARGET_FAMILY_UNAVAILABLE")
			: !bAllBoolean
			? TEXT("LC1_REGION_BOOLEAN_TYPE_UNAVAILABLE")
			: TEXT("LC1_REGION_TRUE_LITERAL_UNAVAILABLE");
	}
	else
	{
		Result.Status =
			EBlueprintLensLC1RegionProjectionStatus::StructuralRun;
		Result.RegionKind = TEXT("structural_aggregate");
		Result.SummaryTemplateId = TEXT("structural_run");
		Result.SummaryArguments = {Count};
		Result.ClaimEvidence = {
			Evidence(
				TEXT("count"),
				TEXT("layout.straight_run_members"),
				Run.Id,
				Count),
			Evidence(
				TEXT("sequence"),
				TEXT("layout.execution_relations"),
				RelationLedger,
				Sequence)};
		Result.DiagnosticCode = !bTypedIrBound
			? TEXT("LC1_REGION_TYPED_IR_UNBOUND")
			: TEXT("LC1_REGION_OPERATION_CLASS_UNAVAILABLE");
	}
	Result.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Result);
	return Result;
}
