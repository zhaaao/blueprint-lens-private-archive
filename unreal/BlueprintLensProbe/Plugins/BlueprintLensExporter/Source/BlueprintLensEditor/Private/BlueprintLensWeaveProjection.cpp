#include "BlueprintLensWeaveProjection.h"

#include "Algo/Reverse.h"
#include "Algo/Sort.h"

namespace
{
template <typename ElementType>
void SortById(TArray<const ElementType*>& Values)
{
	Values.Sort(
		[](const ElementType& Left, const ElementType& Right)
		{
			return Left.Id < Right.Id;
		});
}

FString CoverageError(
	const TCHAR* Kind,
	const TSet<FString>& Accounted,
	const TArray<FString>& Expected)
{
	TArray<FString> Missing;
	for (const FString& Id : Expected)
	{
		if (!Accounted.Contains(Id))
		{
			Missing.Add(Id);
		}
	}
	Missing.Sort();
	return FString::Printf(
		TEXT("Causal Weave cannot account for every %s; missing: %s"),
		Kind,
		*FString::Join(Missing, TEXT(", ")));
}
} // namespace

FBlueprintLensWeaveProjection FBlueprintLensWeaveProjector::Build(
	const FBlueprintLensExplanationModel& Model)
{
	FBlueprintLensWeaveProjection Result;
	Result.Criterion = Model.FindUnit(Model.CriterionUnitId);
	if (Result.Criterion == nullptr)
	{
		Result.Error = TEXT("Causal Weave criterion is missing");
		return Result;
	}

	TArray<const FBlueprintLensRelation*> IncomingControlRelations;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::ControlsExecution
			&& Relation.TargetUnitId == Result.Criterion->Id)
		{
			IncomingControlRelations.Add(&Relation);
		}
	}
	SortById(IncomingControlRelations);
	if (IncomingControlRelations.Num() != 1)
	{
		Result.Error = FString::Printf(
			TEXT("Canonical Causal Weave requires exactly one control "
				 "relation into the criterion; found %d"),
			IncomingControlRelations.Num());
		return Result;
	}
	Result.CriterionControlRelation = IncomingControlRelations[0];

	const FBlueprintLensUnit* Current =
		Model.FindUnit(Result.CriterionControlRelation->SourceUnitId);
	if (Current == nullptr)
	{
		Result.Error =
			TEXT("Causal Weave control relation has a missing source unit");
		return Result;
	}

	TSet<FString> VisitedExecutionUnits;
	while (Current != nullptr)
	{
		if (VisitedExecutionUnits.Contains(Current->Id))
		{
			Result.Error =
				TEXT("Causal Weave canonical execution path contains a cycle");
			return Result;
		}
		VisitedExecutionUnits.Add(Current->Id);
		Result.ExecutionUnits.Add(Current);

		TArray<const FBlueprintLensRelation*> IncomingExecutionRelations;
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			if (Relation.Kind
					== EBlueprintLensRelationKind::ExecutionPredecessor
				&& Relation.TargetUnitId == Current->Id)
			{
				IncomingExecutionRelations.Add(&Relation);
			}
		}
		SortById(IncomingExecutionRelations);
		if (IncomingExecutionRelations.Num() > 1)
		{
			Result.Error = FString::Printf(
				TEXT("Canonical Causal Weave does not linearise %d incoming "
					 "execution relations at '%s'"),
				IncomingExecutionRelations.Num(),
				*Current->Id);
			return Result;
		}
		if (IncomingExecutionRelations.IsEmpty())
		{
			break;
		}

		Result.ExecutionRelations.Add(IncomingExecutionRelations[0]);
		Current =
			Model.FindUnit(IncomingExecutionRelations[0]->SourceUnitId);
		if (Current == nullptr)
		{
			Result.Error =
				TEXT("Causal Weave execution relation has a missing source");
			return Result;
		}
	}
	Algo::Reverse(Result.ExecutionUnits);
	Algo::Reverse(Result.ExecutionRelations);
	if (Result.ExecutionUnits.Num() != 3
		|| Result.ExecutionRelations.Num() != 2)
	{
		Result.Error = FString::Printf(
			TEXT("Canonical Causal Weave spike expects three execution stops "
				 "and two predecessor relations; found %d and %d"),
			Result.ExecutionUnits.Num(),
			Result.ExecutionRelations.Num());
		return Result;
	}

	TSet<FString> ExecutionUnitIds;
	for (const FBlueprintLensUnit* Unit : Result.ExecutionUnits)
	{
		ExecutionUnitIds.Add(Unit->Id);
		Result.AccountedUnitIds.Add(Unit->Id);
	}
	Result.AccountedUnitIds.Add(Result.Criterion->Id);
	Result.AccountedRelationIds.Add(Result.CriterionControlRelation->Id);
	for (const FBlueprintLensRelation* Relation : Result.ExecutionRelations)
	{
		Result.AccountedRelationIds.Add(Relation->Id);
	}

	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor
			&& ExecutionUnitIds.Contains(Relation.TargetUnitId))
		{
			const FBlueprintLensUnit* Unit =
				Model.FindUnit(Relation.SourceUnitId);
			if (Unit == nullptr)
			{
				Result.Error =
					TEXT("Causal Weave predicate relation has no source unit");
				return Result;
			}
			Result.PredicateRelations.Add(&Relation);
			Result.PredicateUnits.Add(Unit);
			Result.AccountedRelationIds.Add(Relation.Id);
			Result.AccountedUnitIds.Add(Unit->Id);
		}
		else if (Relation.Kind == EBlueprintLensRelationKind::ProvidesValue
			&& Relation.TargetUnitId == Result.Criterion->Id)
		{
			const FBlueprintLensUnit* Unit =
				Model.FindUnit(Relation.SourceUnitId);
			if (Unit == nullptr)
			{
				Result.Error =
					TEXT("Causal Weave value relation has no source unit");
				return Result;
			}
			Result.ValueRelations.Add(&Relation);
			Result.ValueUnits.Add(Unit);
			Result.AccountedRelationIds.Add(Relation.Id);
			Result.AccountedUnitIds.Add(Unit->Id);
		}
	}
	SortById(Result.PredicateRelations);
	SortById(Result.PredicateUnits);
	SortById(Result.ValueRelations);
	SortById(Result.ValueUnits);
	if (Result.PredicateUnits.Num() != 1 || Result.ValueUnits.Num() != 1)
	{
		Result.Error = FString::Printf(
			TEXT("Canonical Causal Weave spike expects one predicate and one "
				 "criterion value; found %d and %d"),
			Result.PredicateUnits.Num(),
			Result.ValueUnits.Num());
		return Result;
	}

	TArray<FString> ExpectedUnitIds;
	ExpectedUnitIds.Reserve(Model.Units.Num());
	Result.bAllSupported = true;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		ExpectedUnitIds.Add(Unit.Id);
		Result.bAllSupported &=
			Unit.SemanticStatus == EBlueprintLensSemanticStatus::Supported;
	}
	if (Result.AccountedUnitIds.Num() != ExpectedUnitIds.Num())
	{
		Result.Error = CoverageError(
			TEXT("unit"),
			Result.AccountedUnitIds,
			ExpectedUnitIds);
		return Result;
	}

	TArray<FString> ExpectedRelationIds;
	ExpectedRelationIds.Reserve(Model.Relations.Num());
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		ExpectedRelationIds.Add(Relation.Id);
	}
	if (Result.AccountedRelationIds.Num() != ExpectedRelationIds.Num())
	{
		Result.Error = CoverageError(
			TEXT("relation"),
			Result.AccountedRelationIds,
			ExpectedRelationIds);
		return Result;
	}

	return Result;
}
