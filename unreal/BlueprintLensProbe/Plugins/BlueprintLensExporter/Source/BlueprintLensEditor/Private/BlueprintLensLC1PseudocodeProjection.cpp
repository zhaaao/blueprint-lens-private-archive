#include "BlueprintLensLC1PseudocodeProjection.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR EventClass[] = TEXT("/Script/BlueprintGraph.K2Node_Event");
constexpr TCHAR VariableSetClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
constexpr TCHAR ProjectorVersion[] =
	TEXT("BlueprintLens.LC1PseudocodeProjector.v1");

FBlueprintLensLC1PseudocodeProjection Failure(const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC1PseudocodeProjection Result;
	Result.DiagnosticCode = DiagnosticCode;
	return Result;
}

void AppendValue(
	FString& Canonical,
	const TCHAR* Label,
	const FString& Value)
{
	Canonical += Label;
	Canonical += FString::Printf(TEXT("[%d:%s]"), Value.Len(), *Value);
}

void AppendIds(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<FString>& Values)
{
	Canonical += Label;
	Canonical += TEXT("[");
	for (const FString& Value : Values)
	{
		Canonical += FString::Printf(TEXT("%d:%s;"), Value.Len(), *Value);
	}
	Canonical += TEXT("]");
}

FString CalculateIntegrityHash(
	const FBlueprintLensLC1PseudocodeProjection& Projection)
{
	FString Canonical;
	AppendValue(Canonical, TEXT("version"), Projection.ProjectorVersion);
	AppendValue(Canonical, TEXT("source-ir"), Projection.SourceIrSha256);
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	for (const FBlueprintLensLC1PseudocodeLine& Line : Projection.Lines)
	{
		AppendValue(Canonical, TEXT("line-id"), Line.LineId);
		AppendValue(
			Canonical,
			TEXT("line-number"),
			FString::FromInt(Line.LineNumber));
		AppendValue(Canonical, TEXT("code"), Line.CodeText);
		AppendValue(
			Canonical,
			TEXT("role"),
			FString::FromInt(static_cast<int32>(Line.Role)));
		AppendValue(
			Canonical,
			TEXT("semantic-status"),
			FString::FromInt(static_cast<int32>(Line.SemanticStatus)));
		AppendValue(Canonical, TEXT("unit"), Line.UnitId);
		AppendValue(
			Canonical,
			TEXT("following-relation"),
			Line.FollowingRelationId);
		AppendValue(Canonical, TEXT("source-node"), Line.SourceNodeId);
		AppendIds(Canonical, TEXT("source-pins"), Line.SourcePinIds);
		AppendValue(Canonical, TEXT("fact-owner"), Line.FactOwner);
		AppendValue(
			Canonical,
			TEXT("diagnostic"),
			Line.ProjectionDiagnostic);
	}
	AppendValue(Canonical, TEXT("diagnostic"), Projection.DiagnosticCode);
	return FMD5::HashAnsiString(*Canonical);
}

const FBlueprintLensSourceReference* FindOnlyPrimarySource(
	const FBlueprintLensUnit& Unit)
{
	const FBlueprintLensSourceReference* Primary = nullptr;
	for (const FBlueprintLensSourceReference& Reference : Unit.SourceReferences)
	{
		if (!Reference.bPrimary)
		{
			continue;
		}
		if (Primary != nullptr || Reference.SourceNodeId.IsEmpty())
		{
			return nullptr;
		}
		Primary = &Reference;
	}
	return Primary;
}

const FBlueprintLensRelation* FindOnlyFollowingRelation(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& SourceUnitId,
	const FString& TargetUnitId)
{
	const FBlueprintLensRelation* Found = nullptr;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.SourceUnitId != SourceUnitId
			|| Relation.TargetUnitId != TargetUnitId
			|| Relation.Kind
				!= EBlueprintLensRelationKind::ExecutionPredecessor)
		{
			continue;
		}
		if (Found != nullptr || Relation.Id.IsEmpty())
		{
			return nullptr;
		}
		Found = &Relation;
	}
	return Found;
}

bool IsExpectedTarget(const FString& Target, const int32 OrderedIndex)
{
	if (OrderedIndex == 13)
	{
		return Target == TEXT("LC1Ready");
	}
	return OrderedIndex >= 1 && OrderedIndex <= 12
		&& Target == FString::Printf(
			TEXT("LC1Step%02dComplete"),
			OrderedIndex);
}
} // namespace

bool FBlueprintLensLC1PseudocodeProjection::HasValidIntegrity() const
{
	return ProjectorVersion == ::ProjectorVersion
		&& !SourceIrSha256.IsEmpty()
		&& !ProjectionIntegrityHash.IsEmpty()
		&& ProjectionIntegrityHash.Equals(
			CalculateIntegrityHash(*this),
			ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC1PseudocodeProjection::IsRenderable() const
{
	return Status == EBlueprintLensLC1PseudocodeProjectionStatus::Complete
		&& DiagnosticCode == TEXT("LC1_PSEUDOCODE_COMPLETE")
		&& Lines.Num() == 14
		&& Lines[0].Role == EBlueprintLensRole::Control
		&& Lines.Last().Role == EBlueprintLensRole::Criterion
		&& HasValidIntegrity();
}

FBlueprintLensLC1PseudocodeProjection
FBlueprintLensLC1PseudocodeProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts)
{
	if (!Layout.IsReady() || Layout.Segments.Num() != 3
		|| Explanation.Units.Num() != 14
		|| Explanation.Relations.Num() != 13
		|| Layout.Segments[0].MemberUnitIds.Num() != 1
		|| Layout.Segments[1].MemberUnitIds.Num() != 12
		|| Layout.Segments[2].MemberUnitIds.Num() != 1)
	{
		return Failure(TEXT("LC1_PSEUDOCODE_PROFILE_INVALID"));
	}
	if (!TypedIrFacts.IsValid()
		|| !TypedIrFacts.VerifiedIrSha256.Equals(
			Explanation.Source.IrSha256,
			ESearchCase::IgnoreCase))
	{
		return Failure(TEXT("LC1_PSEUDOCODE_TYPED_IR_UNBOUND"));
	}

	TArray<FString> OrderedUnitIds = Layout.Segments[0].MemberUnitIds;
	OrderedUnitIds.Append(Layout.Segments[1].MemberUnitIds);
	OrderedUnitIds.Append(Layout.Segments[2].MemberUnitIds);

	FBlueprintLensLC1PseudocodeProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = ProjectorVersion;
	for (int32 Index = 0; Index < OrderedUnitIds.Num(); ++Index)
	{
		const FBlueprintLensUnit* Unit =
			Explanation.FindUnit(OrderedUnitIds[Index]);
		if (Unit == nullptr
			|| Unit->SemanticStatus != EBlueprintLensSemanticStatus::Supported)
		{
			return Failure(TEXT("LC1_PSEUDOCODE_UNIT_UNSUPPORTED"));
		}
		const FBlueprintLensSourceReference* Source =
			FindOnlyPrimarySource(*Unit);
		if (Source == nullptr)
		{
			return Failure(TEXT("LC1_PSEUDOCODE_PRIMARY_SOURCE_INVALID"));
		}

		FBlueprintLensLC1PseudocodeLine Line;
		Line.LineNumber = Index + 1;
		Line.LineId = FString::Printf(TEXT("lc1.code.%02d"), Index + 1);
		Line.Role = Unit->Role;
		Line.SemanticStatus = Unit->SemanticStatus;
		Line.UnitId = Unit->Id;
		Line.SourceNodeId = Source->SourceNodeId;
		Line.SourcePinIds = Source->SourcePinIds;

		if (Index + 1 < OrderedUnitIds.Num())
		{
			const FBlueprintLensRelation* Following =
				FindOnlyFollowingRelation(
					Explanation,
					Unit->Id,
					OrderedUnitIds[Index + 1]);
			if (Following == nullptr)
			{
				return Failure(
					TEXT("LC1_PSEUDOCODE_RELATION_OWNERSHIP_INVALID"));
			}
			Line.FollowingRelationId = Following->Id;
		}

		if (Index == 0)
		{
			const FBlueprintLensLC1NodeFact* NodeFact =
				TypedIrFacts.NodesBySourceNodeId.Find(Line.SourceNodeId);
			if (NodeFact == nullptr || NodeFact->NodeClass != EventClass
				|| !NodeFact->NativeTitle.Contains(
					TEXT("BeginPlay"),
					ESearchCase::IgnoreCase))
			{
				return Failure(TEXT("LC1_PSEUDOCODE_ENTRY_UNSUPPORTED"));
			}
			Line.CodeText = TEXT("event BeginPlay");
			Line.FactOwner = TEXT("typed_ir.node.class+title");
			Line.ProjectionDiagnostic = TEXT("LC1_CODE_EVENT");
		}
		else
		{
			const FBlueprintLensLC1OperationFact* Fact =
				TypedIrFacts.OperationsBySourceNodeId.Find(Line.SourceNodeId);
			if (Fact == nullptr || Fact->OperationClass != VariableSetClass)
			{
				return Failure(TEXT("LC1_PSEUDOCODE_OPERATION_UNSUPPORTED"));
			}
			if (!IsExpectedTarget(Fact->VariableTarget, Index))
			{
				return Failure(TEXT("LC1_PSEUDOCODE_TARGET_UNEXPECTED"));
			}
			if (Fact->ValueType != TEXT("bool")
				|| Fact->LiteralValue != TEXT("true"))
			{
				return Failure(TEXT("LC1_PSEUDOCODE_LITERAL_UNSUPPORTED"));
			}
			Line.CodeText = FString::Printf(
				TEXT("    %s = true;"),
				*Fact->VariableTarget);
			Line.FactOwner = TEXT("typed_ir.variable_set_value");
			Line.ProjectionDiagnostic = Index == 13
				? TEXT("LC1_CODE_CRITERION_ASSIGNMENT")
				: TEXT("LC1_CODE_REGION_ASSIGNMENT");
		}
		Result.Lines.Add(MoveTemp(Line));
	}

	TSet<FString> OwnedRelations;
	for (const FBlueprintLensLC1PseudocodeLine& Line : Result.Lines)
	{
		if (!Line.FollowingRelationId.IsEmpty())
		{
			OwnedRelations.Add(Line.FollowingRelationId);
		}
	}
	if (OwnedRelations.Num() != Explanation.Relations.Num())
	{
		return Failure(TEXT("LC1_PSEUDOCODE_RELATION_COVERAGE_INVALID"));
	}

	Result.Status = EBlueprintLensLC1PseudocodeProjectionStatus::Complete;
	Result.DiagnosticCode = TEXT("LC1_PSEUDOCODE_COMPLETE");
	Result.ProjectionIntegrityHash = CalculateIntegrityHash(Result);
	return Result;
}
