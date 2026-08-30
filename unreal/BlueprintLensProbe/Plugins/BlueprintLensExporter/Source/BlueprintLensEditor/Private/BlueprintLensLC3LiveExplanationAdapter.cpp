#include "BlueprintLensLC3LiveExplanationAdapter.h"

#include "BlueprintLensDisplayLabel.h"
#include "BlueprintLensLC3ValueConeProjection.h"

namespace
{
FBlueprintLensLC3LiveExplanationAdapterResult Fail(
	const FBlueprintLensExplanationModel& Explanation,
	const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC3LiveExplanationAdapterResult Result;
	Result.InputUnitCount = Explanation.Units.Num();
	Result.InputRelationCount = Explanation.Relations.Num();
	Result.Explanation = Explanation;
	Result.DiagnosticCode = DiagnosticCode;
	return Result;
}

bool RelationIdLess(
	const FBlueprintLensRelation& Left,
	const FBlueprintLensRelation& Right)
{
	return Left.Id < Right.Id;
}
} // namespace

FBlueprintLensLC3LiveExplanationAdapterResult
FBlueprintLensLC3LiveExplanationAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	if (!Explanation.Groups.IsEmpty() || Explanation.bHasGroups ||
		Explanation.bHasGroupPartialOrder ||
		Explanation.FindUnit(Explanation.CriterionUnitId) == nullptr)
	{
		return Fail(Explanation, TEXT("LC3_LIVE_ADAPTER_INPUT_UNAVAILABLE"));
	}

	TArray<FString> OrderedConeUnitIds = {Explanation.CriterionUnitId};
	TArray<const FBlueprintLensRelation*> OrderedValueRelations;
	TSet<FString> ConeUnitIds = {Explanation.CriterionUnitId};
	for (int32 ConsumerIndex = 0;
		 ConsumerIndex < OrderedConeUnitIds.Num();
		 ++ConsumerIndex)
	{
		const FString ConsumerUnitId = OrderedConeUnitIds[ConsumerIndex];
		TArray<const FBlueprintLensRelation*> Inputs;
		for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		{
			if (Relation.Kind == EBlueprintLensRelationKind::ProvidesValue &&
				Relation.TargetUnitId == ConsumerUnitId)
			{
				Inputs.Add(&Relation);
			}
		}
		Inputs.Sort(
			[](const FBlueprintLensRelation& Left,
				const FBlueprintLensRelation& Right)
			{
				return RelationIdLess(Left, Right);
			});
		for (const FBlueprintLensRelation* Relation : Inputs)
		{
			const FBlueprintLensUnit* Producer =
				Relation != nullptr
					? Explanation.FindUnit(Relation->SourceUnitId)
					: nullptr;
			if (Relation == nullptr || Relation->Id.IsEmpty() ||
				Producer == nullptr || Producer->Role == EBlueprintLensRole::Control ||
				ConeUnitIds.Contains(Relation->SourceUnitId))
			{
				return Fail(
					Explanation,
					TEXT("LC3_LIVE_ADAPTER_VALUE_TREE_INVALID"));
			}
			ConeUnitIds.Add(Relation->SourceUnitId);
			OrderedConeUnitIds.Add(Relation->SourceUnitId);
			OrderedValueRelations.Add(Relation);
		}
	}
	if (OrderedValueRelations.IsEmpty())
	{
		return Fail(
			Explanation,
			TEXT("LC3_LIVE_ADAPTER_VALUE_SOURCE_UNAVAILABLE"));
	}
	if (OrderedConeUnitIds.Num() >
			BlueprintLensLC3ValueConeBounds::MaxConeUnitCount ||
		OrderedValueRelations.Num() >
			BlueprintLensLC3ValueConeBounds::MaxValueRelationCount)
	{
		return Fail(Explanation, TEXT("LC3_LIVE_ADAPTER_BOUND_EXCEEDED"));
	}

	const FBlueprintLensRelation* ControlRelation = nullptr;
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (Relation.Kind != EBlueprintLensRelationKind::ExecutionPredecessor ||
			Relation.TargetUnitId != Explanation.CriterionUnitId)
		{
			continue;
		}
		const FBlueprintLensUnit* Controller =
			Explanation.FindUnit(Relation.SourceUnitId);
		if (Controller == nullptr ||
			Controller->Role != EBlueprintLensRole::Control ||
			ControlRelation != nullptr)
		{
			return Fail(
				Explanation,
				TEXT("LC3_LIVE_ADAPTER_CONTROL_INVALID"));
		}
		ControlRelation = &Relation;
	}
	if (ControlRelation == nullptr ||
		ConeUnitIds.Contains(ControlRelation->SourceUnitId))
	{
		return Fail(Explanation, TEXT("LC3_LIVE_ADAPTER_CONTROL_INVALID"));
	}

	TSet<FString> CoreUnitIds = ConeUnitIds;
	CoreUnitIds.Add(ControlRelation->SourceUnitId);
	TSet<FString> CoreRelationIds;
	for (const FBlueprintLensRelation* Relation : OrderedValueRelations)
	{
		CoreRelationIds.Add(Relation->Id);
	}
	CoreRelationIds.Add(ControlRelation->Id);

	FBlueprintLensLC3LiveExplanationAdapterResult Result;
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
			Lane.EmptyMessage = TEXT("No units from the adapted LC3 core.");
		}
	}
	Result.Explanation.Counts.Lanes = Result.Explanation.Lanes.Num();
	Result.Explanation.Counts.Units = Result.Explanation.Units.Num();
	Result.Explanation.Counts.Relations = Result.Explanation.Relations.Num();

	FBlueprintLensGroup ValueCone;
	ValueCone.Id = TEXT("group.value_cone.live-criterion");
	ValueCone.Kind = EBlueprintLensGroupKind::ValueCone;
	ValueCone.Title = FString::Printf(
		TEXT("%s value provenance"),
		*BlueprintLensDisplayLabel(
			*Explanation.FindUnit(Explanation.CriterionUnitId)));
	ValueCone.OrderedUnitIds = OrderedConeUnitIds;
	for (const FBlueprintLensRelation* Relation : OrderedValueRelations)
	{
		ValueCone.OrderedRelationIds.Add(Relation->Id);
	}
	ValueCone.EntryUnitId = Explanation.CriterionUnitId;
	ValueCone.MemberCount = OrderedConeUnitIds.Num();
	ValueCone.ProjectionStatus = EBlueprintLensProjectionStatus::Complete;
	Result.Explanation.Groups = {MoveTemp(ValueCone)};
	Result.Explanation.bHasGroups = true;
	Result.Explanation.bHasGroupPartialOrder = false;
	Result.Explanation.GroupPartialOrder = FBlueprintLensGroupPartialOrder();

	TSet<FString> CoveredNonControlUnitIds;
	for (const FString& UnitId :
		 Result.Explanation.Groups[0].OrderedUnitIds)
	{
		if (!ConeUnitIds.Contains(UnitId))
		{
			return Fail(
				Explanation,
				TEXT("LC3_LIVE_ADAPTER_GROUP_COVER_INCOMPLETE"));
		}
		CoveredNonControlUnitIds.Add(UnitId);
	}
	if (CoveredNonControlUnitIds.Num() != ConeUnitIds.Num())
	{
		return Fail(
			Explanation,
			TEXT("LC3_LIVE_ADAPTER_GROUP_COVER_INCOMPLETE"));
	}

	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(Result.Explanation);
	if (!Projection.IsRenderable() ||
		Projection.Status != EBlueprintLensLC3ValueConeProjectionStatus::ValueCone)
	{
		return Fail(
			Explanation,
			TEXT("LC3_LIVE_ADAPTER_PROJECTOR_REJECTED"));
	}

	Result.AdaptedUnitCount = Result.Explanation.Units.Num();
	Result.AdaptedRelationCount = Result.Explanation.Relations.Num();
	Result.DiagnosticCode = TEXT("LC3_LIVE_EXPLANATION_ADAPTED");
	Result.bAdapted = true;
	return Result;
}
