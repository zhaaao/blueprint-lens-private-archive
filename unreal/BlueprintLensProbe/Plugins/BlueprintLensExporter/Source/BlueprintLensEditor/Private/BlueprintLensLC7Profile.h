#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

struct FBlueprintLensLC7RelationBinding
{
	FString RelationId;
	FString SourceEdgeId;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourceNodeId;
	FString TargetNodeId;
	FString SourcePinId;
	FString TargetPinId;
	bool bReturning = false;
};

struct FBlueprintLensLC7SCC
{
	FString GroupId;
	FString EntryNodeId;
	FString ExitNodeId;
	FString EntryUnitId;
	FString ExitUnitId;
	TArray<FString> MemberNodeIds;
	TArray<FString> OrderedMemberUnitIds;
	TArray<FString> OrderedRelationIds;
	TArray<FString> InternalEdgeIds;
	TArray<FString> IncomingEdgeIds;
	TArray<FString> OutgoingEdgeIds;
	TArray<FString> ReturningEdgeIds;
};

struct FBlueprintLensLC7Profile
{
	bool bLiveExplanation = false;
	bool bExitOutsideSlice = false;
	FString ProfileId;
	FString ClaimScope;
	FString RuntimeIterations;
	FString ReadinessStatus;
	FString RelationFamilyStatement;
	FString ExitBoundaryStatement;
	FString BlueprintAssetPath;
	FString GraphId;
	FString AssetSha256;
	FString RawSha256;
	FString SourceSha256;
	FString AuditSha256;
	FString CriterionNodeId;
	FString CriterionUnitId;
	FString ExplanationPath;
	FString SCCProfilePath;
	FString ReviewedPath;
	FString ReadinessPath;
	FString ExplanationSha256;
	FString SCCProfileSha256;
	FString ReviewedSha256;
	FString ReadinessSha256;
	int32 SourceNodeCount = 0;
	int32 SourceEdgeCount = 0;
	int32 ExplanationUnitCount = 0;
	int32 ExplanationRelationCount = 0;
	int32 StructuralSCCCount = 0;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FBlueprintLensLC7SCC SCC;
	TArray<FBlueprintLensLC7RelationBinding> Relations;

	bool IsValid() const;
};

struct FBlueprintLensLC7LoadResult
{
	TSharedPtr<const FBlueprintLensLC7Profile> Profile;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FString Error;

	bool IsSuccess() const
	{
		return Profile.IsValid() && ExplanationModel.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensLC7ProfileLoader
{
public:
	static FBlueprintLensLC7LoadResult LoadFiles(
		const FString& ExplanationPath,
		const FString& SCCProfilePath,
		const FString& ReviewedPath,
		const FString& ReadinessPath);
};
