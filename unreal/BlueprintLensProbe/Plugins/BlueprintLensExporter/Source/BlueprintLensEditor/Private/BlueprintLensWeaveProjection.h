#pragma once

#include "BlueprintLensExplanationModel.h"

struct FBlueprintLensWeaveProjection
{
	const FBlueprintLensUnit* Criterion = nullptr;
	const FBlueprintLensRelation* CriterionControlRelation = nullptr;
	TArray<const FBlueprintLensUnit*> ExecutionUnits;
	TArray<const FBlueprintLensRelation*> ExecutionRelations;
	TArray<const FBlueprintLensUnit*> PredicateUnits;
	TArray<const FBlueprintLensRelation*> PredicateRelations;
	TArray<const FBlueprintLensUnit*> ValueUnits;
	TArray<const FBlueprintLensRelation*> ValueRelations;
	TSet<FString> AccountedUnitIds;
	TSet<FString> AccountedRelationIds;
	bool bAllSupported = false;
	FString Error;

	bool IsValid() const
	{
		return Error.IsEmpty() && Criterion != nullptr;
	}
};

class FBlueprintLensWeaveProjector
{
public:
	static FBlueprintLensWeaveProjection Build(
		const FBlueprintLensExplanationModel& Model);
};
