#pragma once

#include "BlueprintLensLC7Profile.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC7ProjectionStatus : uint8
{
	AdaptiveBackbone,
	CompleteText,
	Unavailable
};

enum class EBlueprintLensLC7RelationFamily : uint8
{
	Entry,
	Predicate,
	Value,
	Forward,
	Return,
	Exit
};

struct FBlueprintLensLC7Relation
{
	FString RelationId;
	FString OwningSCCId;
	EBlueprintLensLC7RelationFamily Family =
		EBlueprintLensLC7RelationFamily::Entry;
	EBlueprintLensRelationKind Kind =
		EBlueprintLensRelationKind::ExecutionPredecessor;
	FString Label;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourceEdgeId;
	FString SourceNodeId;
	FString TargetNodeId;
	FString SourcePinId;
	FString TargetPinId;
};

struct FBlueprintLensLC7SCCRecord
{
	FString GroupId;
	FString EntryUnitId;
	FString ExitUnitId;
	FString CriterionUnitId;
	TArray<FString> OrderedSpineUnitIds;
	TArray<FString> EntryRelationIds;
	TArray<FString> PredicateRelationIds;
	TArray<FString> ValueRelationIds;
	TArray<FString> ForwardRelationIds;
	TArray<FString> ReturnRelationIds;
	TArray<FString> ExitRelationIds;
};

struct FBlueprintLensLC7Projection
{
	bool bLiveExplanation = false;
	bool bExitOutsideSlice = false;
	EBlueprintLensLC7ProjectionStatus Status =
		EBlueprintLensLC7ProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString IntegrityHash;
	FString ProfileId;
	FString ClaimScope;
	FString RuntimeIterations;
	FString RelationFamilyStatement;
	FString ExitBoundaryStatement;
	FString CriterionUnitId;
	TArray<FBlueprintLensLC7SCCRecord> SCCs;
	TArray<FBlueprintLensLC7Relation> Relations;
	TSet<FString> AllUnitIds;
	TSet<FString> AllRelationIds;
	TMap<FString, FString> UnitTitles;
	TMap<FString, FBlueprintLensSourceReference> SourceAnchors;
	TArray<FString> ActionIds;
	TArray<FString> CompleteTextLines;

	bool IsRenderable() const
	{
		return Status == EBlueprintLensLC7ProjectionStatus::AdaptiveBackbone &&
			!SCCs.IsEmpty() && !AllUnitIds.IsEmpty() &&
			AllRelationIds.Num() == Relations.Num() &&
			UnitTitles.Num() == AllUnitIds.Num() &&
			SourceAnchors.Num() == AllUnitIds.Num() &&
			!IntegrityHash.IsEmpty();
	}

	int32 CountRelations(EBlueprintLensLC7RelationFamily Family) const;
};

class FBlueprintLensLC7Projector
{
public:
	static FBlueprintLensLC7Projection Build(
		const FBlueprintLensLC7Profile& Profile);
};
