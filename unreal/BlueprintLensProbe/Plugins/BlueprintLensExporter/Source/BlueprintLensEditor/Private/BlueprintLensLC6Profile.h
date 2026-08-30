#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

struct FBlueprintLensLC6Frontier
{
	FString EdgeId;
	FString SourceNodeId;
	FString TargetNodeId;
};

struct FBlueprintLensLC6HopDistance
{
	FString NodeId;
	int32 Distance = INDEX_NONE;
};

struct FBlueprintLensLC6SourceEdge
{
	FString EdgeId;
	FString Kind;
	FString SourceNodeId;
	FString TargetNodeId;
};

struct FBlueprintLensLC6Scenario
{
	FString ScenarioId;
	FString TruthOwner;
	FString RootNodeId;
	FString CriterionNodeId;
	FString BoundaryNodeId;
	FString Status;
	FString Reason;
	FString StopKind;
	FString StopNodeId;
	FString RootTitle;
	FString CriterionTitle;
	FString BoundaryTitle;
	TArray<FString> SliceNodeIds;
	TArray<FString> SliceEdgeIds;
	TArray<FString> IncidentEdgeIds;
	TArray<FString> SourcePinIds;
	TArray<FString> CompleteNodeIds;
	TArray<FString> CompleteEdgeIds;
	TArray<FBlueprintLensLC6HopDistance> HopDistances;
	TArray<FBlueprintLensLC6Frontier> Frontiers;
	int32 MaxUpstreamHops = INDEX_NONE;
	int32 OmittedNodeCount = 0;
	int32 OmittedEdgeCount = 0;
};

struct FBlueprintLensLC6Profile
{
	FString CoreProfileId;
	FString QueryProfileId;
	FString ReadinessStatus;
	FString BlueprintAssetPath;
	FString AssetSha256;
	FString RawSha256;
	FString GraphId;
	FString CorePath;
	FString QueryPath;
	FString ReadinessPath;
	FString RawPath;
	FString CoreSha256;
	FString QuerySha256;
	FString ReadinessSha256;
	TArray<FBlueprintLensLC6Scenario> Scenarios;
	TMap<FString, FString> SourceTitles;
	TArray<FBlueprintLensLC6SourceEdge> SourceEdges;

	const FBlueprintLensLC6Scenario* FindScenario(const FString& ScenarioId) const;
	bool IsValid() const;
};

struct FBlueprintLensLC6LoadResult
{
	TSharedPtr<const FBlueprintLensLC6Profile> Profile;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FString Error;

	bool IsSuccess() const
	{
		return Profile.IsValid() && ExplanationModel.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensLC6ProfileLoader
{
public:
	static FBlueprintLensLC6LoadResult LoadFiles(
		const FString& CorePath,
		const FString& QueryPath,
		const FString& ReadinessPath,
		const FString& RawPath);
};
