#include "BlueprintLensLC3ValueConeProjection.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR LC3ProjectorVersion[] =
	TEXT("BlueprintLens.LC3ValueConeProjector.v1");

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
	const FBlueprintLensLC3ValueConeProjection& Projection)
{
	FString Canonical;
	AppendValue(Canonical, TEXT("version"), Projection.ProjectorVersion);
	AppendValue(Canonical, TEXT("source-ir"), Projection.SourceIrSha256);
	AppendValue(Canonical, TEXT("criterion"), Projection.CriterionUnitId);
	AppendValue(
		Canonical,
		TEXT("criterion-label"),
		Projection.CriterionReaderLabel);
	AppendValue(
		Canonical,
		TEXT("criterion-input-count"),
		FString::FromInt(Projection.CriterionInputCount));
	AppendIds(
		Canonical,
		TEXT("criterion-input-ports"),
		Projection.CriterionInputPortLabels);
	AppendValue(Canonical, TEXT("group"), Projection.GroupId);
	AppendValue(Canonical, TEXT("group-title"), Projection.GroupTitle);
	AppendIds(Canonical, TEXT("all-units"), Projection.AllUnitIds);
	AppendIds(Canonical, TEXT("all-relations"), Projection.AllRelationIds);
	AppendIds(Canonical, TEXT("fallback-units"), Projection.FallbackUnitIds);
	AppendIds(
		Canonical,
		TEXT("fallback-relations"),
		Projection.FallbackRelationIds);
	AppendIds(Canonical, TEXT("cone-units"), Projection.ConeUnitIds);
	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		AppendValue(Canonical, TEXT("step-relation"), Step.RelationId);
		AppendValue(Canonical, TEXT("step-producer"), Step.ProducerUnitId);
		AppendValue(
			Canonical,
			TEXT("step-producer-label"),
			Step.ProducerReaderLabel);
		AppendValue(
			Canonical,
			TEXT("step-producer-port"),
			Step.ProducerPortLabel);
		AppendValue(Canonical, TEXT("step-consumer"), Step.ConsumerUnitId);
		AppendValue(
			Canonical,
			TEXT("step-consumer-label"),
			Step.ConsumerReaderLabel);
		AppendValue(
			Canonical,
			TEXT("step-consumer-port"),
			Step.ConsumerPortLabel);
		AppendValue(
			Canonical,
			TEXT("step-depth"),
			FString::FromInt(Step.DerivationDepth));
		AppendValue(
			Canonical,
			TEXT("step-input-count"),
			FString::FromInt(Step.ConsumerInputCount));
		AppendIds(
			Canonical,
			TEXT("step-input-ports"),
			Step.ConsumerInputPortLabels);
		AppendValue(Canonical, TEXT("step-row"), Step.ReaderRowText);
		AppendValue(
			Canonical,
			TEXT("step-producer-input-count"),
			FString::FromInt(Step.ProducerInputCount));
		AppendIds(
			Canonical,
			TEXT("step-producer-input-ports"),
			Step.ProducerInputPortLabels);
		AppendValue(
			Canonical,
			TEXT("step-producer-input-summary"),
			Step.ProducerInputSummaryText);
	}
	AppendBool(Canonical, TEXT("has-control"), Projection.bHasControl);
	AppendValue(
		Canonical,
		TEXT("control-relation"),
		Projection.Control.RelationId);
	AppendValue(
		Canonical,
		TEXT("control-unit"),
		Projection.Control.ControllerUnitId);
	AppendValue(
		Canonical,
		TEXT("control-label"),
		Projection.Control.ControllerReaderLabel);
	AppendValue(
		Canonical,
		TEXT("control-port"),
		Projection.Control.ControllerPortLabel);
	AppendValue(
		Canonical,
		TEXT("control-target"),
		Projection.Control.TargetUnitId);
	AppendValue(
		Canonical,
		TEXT("control-target-label"),
		Projection.Control.TargetReaderLabel);
	AppendValue(
		Canonical,
		TEXT("control-target-port"),
		Projection.Control.TargetPortLabel);
	AppendValue(
		Canonical,
		TEXT("control-semantic"),
		FString(LexToString(Projection.Control.SemanticLabel)));
	AppendValue(Canonical, TEXT("control-row"), Projection.Control.ReaderRowText);
	AppendIds(Canonical, TEXT("boundaries"), Projection.BoundaryNotices);
	for (const FBlueprintLensClaimEvidence& Evidence :
		 Projection.GroupClaimEvidence)
	{
		AppendValue(
			Canonical,
			TEXT("claim-component"),
			Evidence.Component);
		AppendValue(
			Canonical,
			TEXT("claim-owner"),
			Evidence.FactOwner);
		AppendValue(Canonical, TEXT("claim-source"), Evidence.Source);
	}
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	AppendValue(Canonical, TEXT("diagnostic"), Projection.DiagnosticCode);
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

bool UnitOwnsSourceNode(
	const FBlueprintLensUnit& Unit,
	const FString& SourceNodeId)
{
	return !SourceNodeId.IsEmpty() &&
		Unit.SourceReferences.ContainsByPredicate(
			[&SourceNodeId](const FBlueprintLensSourceReference& Reference)
			{
				return Reference.SourceNodeId == SourceNodeId;
			});
}

FBlueprintLensLC3ValueConeProjection SetUnavailable(
	FBlueprintLensLC3ValueConeProjection Projection,
	const TCHAR* DiagnosticCode)
{
	Projection.Status = EBlueprintLensLC3ValueConeProjectionStatus::Unavailable;
	Projection.DiagnosticCode = DiagnosticCode;
	Projection.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Projection);
	return Projection;
}

FBlueprintLensLC3ValueConeProjection SetFallback(
	FBlueprintLensLC3ValueConeProjection Projection,
	const TCHAR* DiagnosticCode)
{
	Projection.Status =
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback;
	Projection.DiagnosticCode = DiagnosticCode;
	Projection.CriterionInputCount = 0;
	Projection.CriterionInputPortLabels.Reset();
	Projection.GroupId.Reset();
	Projection.GroupTitle.Reset();
	Projection.ConeUnitIds.Reset();
	Projection.Steps.Reset();
	Projection.bHasControl = false;
	Projection.Control = FBlueprintLensLC3ValueConeControl();
	Projection.BoundaryNotices.Reset();
	Projection.GroupClaimEvidence.Reset();
	Projection.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Projection);
	return Projection;
}

bool HasSingleEndpoint(const FBlueprintLensRelation& Relation)
{
	return Relation.bHasSourceEdgeEndpoints &&
		Relation.SourceEdgeEndpoints.Num() == 1 &&
		!Relation.SourceEdgeEndpoints[0].SourcePortLabel.IsEmpty() &&
		!Relation.SourceEdgeEndpoints[0].TargetPortLabel.IsEmpty();
}

TArray<FString> InputPortLabels(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensGroup& Group,
	const FString& ConsumerUnitId)
{
	TArray<FString> Labels;
	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			Explanation.FindRelation(RelationId);
		if (Relation != nullptr &&
			Relation->TargetUnitId == ConsumerUnitId &&
			Relation->SourceEdgeEndpoints.Num() == 1)
		{
			Labels.Add(Relation->SourceEdgeEndpoints[0].TargetPortLabel);
		}
	}
	return Labels;
}
} // namespace

bool FBlueprintLensLC3ValueConeProjection::HasValidIntegrity() const
{
	return ProjectorVersion == LC3ProjectorVersion &&
		!SourceIrSha256.IsEmpty() &&
		!ProjectionIntegrityHash.IsEmpty() &&
		ProjectionIntegrityHash.Equals(
			CalculateProjectionIntegrityHash(*this),
			ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC3ValueConeProjection::IsRenderable() const
{
	const bool bSupportedLedgerCardinality =
		BlueprintLensLC3ValueConeBounds::IsSupportedTreeCardinality(
			AllUnitIds.Num() - 1,
			AllRelationIds.Num() - 1);
	const bool bSupportedConeCardinality =
		BlueprintLensLC3ValueConeBounds::IsSupportedTreeCardinality(
			ConeUnitIds.Num(),
			Steps.Num());
	return Status != EBlueprintLensLC3ValueConeProjectionStatus::Unavailable &&
		HasValidIntegrity() &&
		!CriterionUnitId.IsEmpty() &&
		bSupportedLedgerCardinality &&
		FallbackUnitIds == AllUnitIds &&
		FallbackRelationIds == AllRelationIds &&
		(Status ==
				EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback ||
			(bSupportedConeCardinality &&
				AllUnitIds.Num() == ConeUnitIds.Num() + 1 &&
				AllRelationIds.Num() == Steps.Num() + 1 && bHasControl));
}

FBlueprintLensLC3ValueConeProjection
FBlueprintLensLC3ValueConeProjector::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	FBlueprintLensLC3ValueConeProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.ProjectorVersion = LC3ProjectorVersion;
	Result.CriterionUnitId = Explanation.CriterionUnitId;

	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.Id.IsEmpty() || Result.AllUnitIds.Contains(Unit.Id))
		{
			return SetUnavailable(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_UNIT_LEDGER_INVALID"));
		}
		Result.AllUnitIds.Add(Unit.Id);
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Id.IsEmpty() || Result.AllRelationIds.Contains(Relation.Id))
		{
			return SetUnavailable(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_RELATION_LEDGER_INVALID"));
		}
		Result.AllRelationIds.Add(Relation.Id);
	}
	Result.FallbackUnitIds = Result.AllUnitIds;
	Result.FallbackRelationIds = Result.AllRelationIds;

	if (!BlueprintLensLC3ValueConeBounds::IsSupportedTreeCardinality(
		Result.AllUnitIds.Num() - 1,
		Result.AllRelationIds.Num() - 1))
	{
		return SetUnavailable(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_LEDGER_SIZE_INVALID"));
	}
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Result.CriterionUnitId);
	if (Criterion == nullptr)
	{
		return SetUnavailable(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_CRITERION_INVALID"));
	}
	Result.CriterionReaderLabel = ReaderLabel(*Criterion);

	if (!Explanation.bHasGroups || Explanation.Groups.Num() != 1 ||
		Explanation.Groups[0].Kind != EBlueprintLensGroupKind::ValueCone ||
		Explanation.Groups[0].ProjectionStatus !=
			EBlueprintLensProjectionStatus::Complete)
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_GROUP_SET_INVALID"));
	}

	const FBlueprintLensGroup& Group = Explanation.Groups[0];
	TSet<FString> ConeUnitSet;
	bool bGroupShapeValid =
		Group.EntryUnitId == Result.CriterionUnitId &&
		Group.MemberCount == Group.OrderedUnitIds.Num() &&
		BlueprintLensLC3ValueConeBounds::IsSupportedTreeCardinality(
			Group.OrderedUnitIds.Num(),
			Group.OrderedRelationIds.Num());
	for (const FString& UnitId : Group.OrderedUnitIds)
	{
		if (UnitId.IsEmpty() || ConeUnitSet.Contains(UnitId) ||
			Explanation.FindUnit(UnitId) == nullptr)
		{
			bGroupShapeValid = false;
			break;
		}
		ConeUnitSet.Add(UnitId);
	}
	if (!bGroupShapeValid)
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_GROUP_SHAPE_INVALID"));
	}

	for (const FString& UnitId : Group.OrderedUnitIds)
	{
		const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
		if (Unit != nullptr && Unit->Role == EBlueprintLensRole::Control)
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_CONTROLLER_IN_CONE"));
		}
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Kind ==
				EBlueprintLensRelationKind::ExecutionPredecessor &&
			ConeUnitSet.Contains(Relation.SourceUnitId))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_CONTROLLER_IN_CONE"));
		}
	}

	TMap<FString, int32> Depths;
	Depths.Add(Result.CriterionUnitId, 0);
	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			Explanation.FindRelation(RelationId);
		if (Relation == nullptr ||
			!ConeUnitSet.Contains(Relation->SourceUnitId) ||
			!ConeUnitSet.Contains(Relation->TargetUnitId))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_DERIVATION_ORDER_INVALID"));
		}
		const int32* ConsumerDepth = Depths.Find(Relation->TargetUnitId);
		if (ConsumerDepth == nullptr || Depths.Contains(Relation->SourceUnitId))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_DERIVATION_ORDER_INVALID"));
		}
		Depths.Add(Relation->SourceUnitId, *ConsumerDepth + 1);
	}
	if (Depths.Num() != Group.OrderedUnitIds.Num())
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_DERIVATION_ORDER_INVALID"));
	}
	for (const FString& UnitId : Group.OrderedUnitIds)
	{
		if (!Depths.Contains(UnitId))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_DERIVATION_ORDER_INVALID"));
		}
	}

	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			Explanation.FindRelation(RelationId);
		if (Relation == nullptr ||
			Relation->Kind != EBlueprintLensRelationKind::ProvidesValue ||
			!Relation->bHasSemanticLabel ||
			Relation->SemanticLabel != EBlueprintLensSemanticLabel::ValueInput ||
			!HasSingleEndpoint(*Relation))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_PORT_ENDPOINT_UNAVAILABLE"));
		}
	}

	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation& Relation =
			*Explanation.FindRelation(RelationId);
		const FBlueprintLensSourceEdgeEndpoint& Endpoint =
			Relation.SourceEdgeEndpoints[0];
		if (!Relation.bHasPortLabel ||
			Relation.PortLabel != Endpoint.SourcePortLabel ||
			Relation.Label != Endpoint.TargetPortLabel)
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_PORT_LABEL_UNBOUND"));
		}
	}

	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation& Relation =
			*Explanation.FindRelation(RelationId);
		const FBlueprintLensUnit* Source =
			Explanation.FindUnit(Relation.SourceUnitId);
		const FBlueprintLensUnit* Target =
			Explanation.FindUnit(Relation.TargetUnitId);
		const FBlueprintLensSourceEdgeEndpoint& Endpoint =
			Relation.SourceEdgeEndpoints[0];
		if (Source == nullptr || Target == nullptr ||
			!UnitOwnsSourceNode(*Source, Endpoint.SourceNodeId) ||
			!UnitOwnsSourceNode(*Target, Endpoint.TargetNodeId))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_ENDPOINT_UNIT_MISMATCH"));
		}
	}

	TArray<const FBlueprintLensRelation*> OutsideRelations;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!Group.OrderedRelationIds.Contains(Relation.Id))
		{
			OutsideRelations.Add(&Relation);
		}
	}
	if (OutsideRelations.Num() != 1)
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_CONTROL_INVALID"));
	}
	const FBlueprintLensRelation& ControlRelation = *OutsideRelations[0];
	const FBlueprintLensUnit* Controller =
		Explanation.FindUnit(ControlRelation.SourceUnitId);
	const FBlueprintLensUnit* ControlTarget =
		Explanation.FindUnit(ControlRelation.TargetUnitId);
	const bool bControlEndpointAvailable = HasSingleEndpoint(ControlRelation);
	const FBlueprintLensSourceEdgeEndpoint* ControlEndpoint =
		bControlEndpointAvailable
		? &ControlRelation.SourceEdgeEndpoints[0]
		: nullptr;
	if (ControlRelation.Kind !=
			EBlueprintLensRelationKind::ExecutionPredecessor ||
		!ControlRelation.bHasSemanticLabel ||
		ControlRelation.SemanticLabel !=
			EBlueprintLensSemanticLabel::NextExecution ||
		ControlRelation.TargetUnitId != Result.CriterionUnitId ||
		Controller == nullptr || Controller->Role != EBlueprintLensRole::Control ||
		ControlTarget == nullptr || ControlEndpoint == nullptr ||
		!ControlRelation.bHasPortLabel ||
		ControlRelation.PortLabel != ControlEndpoint->SourcePortLabel ||
		!UnitOwnsSourceNode(*Controller, ControlEndpoint->SourceNodeId) ||
		!UnitOwnsSourceNode(*ControlTarget, ControlEndpoint->TargetNodeId))
	{
		return SetFallback(
			MoveTemp(Result),
			TEXT("LC3_VALUE_CONE_CONTROL_INVALID"));
	}

	TSet<FString> ReaderLabels;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		const FString Label = ReaderLabel(Unit);
		if (Label.IsEmpty() || ReaderLabels.Contains(Label))
		{
			return SetFallback(
				MoveTemp(Result),
				TEXT("LC3_VALUE_CONE_LABEL_AMBIGUOUS"));
		}
		ReaderLabels.Add(Label);
	}

	for (const FBlueprintLensLane& Lane : Explanation.Lanes)
	{
		if (Lane.State != EBlueprintLensLaneState::Populated)
		{
			if (Lane.EmptyMessage.IsEmpty())
			{
				return SetFallback(
					MoveTemp(Result),
					TEXT("LC3_VALUE_CONE_BOUNDARY_MESSAGE_MISSING"));
			}
			Result.BoundaryNotices.Add(Lane.EmptyMessage);
		}
	}

	Result.GroupId = Group.Id;
	Result.GroupTitle = Group.Title;
	Result.ConeUnitIds = Group.OrderedUnitIds;
	Result.GroupClaimEvidence = Group.ClaimEvidence;
	Result.CriterionInputPortLabels =
		InputPortLabels(Explanation, Group, Result.CriterionUnitId);
	Result.CriterionInputCount = Result.CriterionInputPortLabels.Num();

	for (const FString& RelationId : Group.OrderedRelationIds)
	{
		const FBlueprintLensRelation& Relation =
			*Explanation.FindRelation(RelationId);
		const FBlueprintLensUnit& Producer =
			*Explanation.FindUnit(Relation.SourceUnitId);
		const FBlueprintLensUnit& Consumer =
			*Explanation.FindUnit(Relation.TargetUnitId);
		const FBlueprintLensSourceEdgeEndpoint& Endpoint =
			Relation.SourceEdgeEndpoints[0];
		FBlueprintLensLC3ValueConeStep Step;
		Step.RelationId = Relation.Id;
		Step.ProducerUnitId = Producer.Id;
		Step.ProducerReaderLabel = ReaderLabel(Producer);
		Step.ProducerPortLabel = Endpoint.SourcePortLabel;
		Step.ConsumerUnitId = Consumer.Id;
		Step.ConsumerReaderLabel = ReaderLabel(Consumer);
		Step.ConsumerPortLabel = Endpoint.TargetPortLabel;
		Step.DerivationDepth = Depths[Producer.Id];
		Step.ConsumerInputPortLabels =
			InputPortLabels(Explanation, Group, Consumer.Id);
		Step.ConsumerInputCount = Step.ConsumerInputPortLabels.Num();
		Step.ReaderRowText = FString::Printf(
			TEXT("supplies %s to %s \u00B7 %s"),
			*Step.ProducerPortLabel,
			*Step.ConsumerReaderLabel,
			*Step.ConsumerPortLabel);
		Step.ProducerInputPortLabels =
			InputPortLabels(Explanation, Group, Producer.Id);
		Step.ProducerInputCount = Step.ProducerInputPortLabels.Num();
		if (Step.ProducerInputCount > 1)
		{
			Step.ProducerInputSummaryText = FString::Printf(
				TEXT("combines %d inputs: %s"),
				Step.ProducerInputCount,
				*FString::Join(
					Step.ProducerInputPortLabels,
					TEXT(", ")));
		}
		Result.Steps.Add(MoveTemp(Step));
	}

	Result.bHasControl = true;
	Result.Control.RelationId = ControlRelation.Id;
	Result.Control.ControllerUnitId = Controller->Id;
	Result.Control.ControllerReaderLabel = ReaderLabel(*Controller);
	Result.Control.ControllerPortLabel = ControlEndpoint->SourcePortLabel;
	Result.Control.TargetUnitId = ControlTarget->Id;
	Result.Control.TargetReaderLabel = ReaderLabel(*ControlTarget);
	Result.Control.TargetPortLabel = ControlEndpoint->TargetPortLabel;
	Result.Control.SemanticLabel = ControlRelation.SemanticLabel;
	Result.Control.ReaderRowText = FString::Printf(
		TEXT("%s \u00B7 %s \u2192 %s \u00B7 %s"),
		*Result.Control.ControllerReaderLabel,
		*Result.Control.ControllerPortLabel,
		*Result.Control.TargetReaderLabel,
		*Result.Control.TargetPortLabel);

	Result.Status = EBlueprintLensLC3ValueConeProjectionStatus::ValueCone;
	Result.DiagnosticCode = TEXT("LC3_VALUE_CONE_COMPLETE");
	Result.ProjectionIntegrityHash =
		CalculateProjectionIntegrityHash(Result);
	return Result;
}
