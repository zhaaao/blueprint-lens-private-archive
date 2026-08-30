#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

struct FBlueprintLensLC5SourceIdentity
{
	FString CallGraphId;
	FString CallSiteNodeId;
};

struct FBlueprintLensLC5CallContext
{
	FString Id;
	FString ParentId;
	FString ClaimScope;
	TArray<FString> CallSiteStack;
};

struct FBlueprintLensLC5Occurrence
{
	FString OccurrenceId;
	FString SourceNodeId;
	FString CallContextId;
	FString Role;
};

struct FBlueprintLensLC5Binding
{
	int32 Ordinal = INDEX_NONE;
	FString Kind;
	FString RelationKind;
	FString SourceOccurrenceId;
	FString TargetOccurrenceId;
	FString CallPinId;
	FString FormalPinId;
	FString PropertyPath;
	FString PropertyName;
	FString CppType;
	FString Direction;
	FString Category;
	FString Container;
	FString RelationId;
};

struct FBlueprintLensLC5InternalRelation
{
	FString Kind;
	FString SourceEdgeId;
	FString SourceOccurrenceId;
	FString TargetOccurrenceId;
	FString RelationId;
};

struct FBlueprintLensLC5ContextBoundary
{
	FString Kind;
	FString ClaimScope;
	FString SourceOccurrenceId;
	FString TargetOccurrenceId;
	FString RelationId;
};

struct FBlueprintLensLC5Profile
{
	static inline const FString FrozenBlueprintAssetSha256 =
		TEXT("ffb14e0c9ab22e8fcd71472e063ee6f8f6c74b1fe3f5bfa8f0490e93c0c831b9");

	FString Format;
	FString FormatVersion;
	FString ProfileId;
	FString Status;
	FString Reason;
	int32 MaxCallDepth = INDEX_NONE;
	FString ProfilePath;
	FString ProfileSha256;
	FString BlueprintAssetSha256 = FrozenBlueprintAssetSha256;
	FBlueprintLensLC5SourceIdentity SourceIdentity;
	FBlueprintLensLC5CallContext CallContext;
	TArray<FBlueprintLensLC5Occurrence> Occurrences;
	TArray<FBlueprintLensLC5Binding> Bindings;
	TArray<FBlueprintLensLC5InternalRelation> InternalRelations;
	TArray<FBlueprintLensLC5ContextBoundary> ContextBoundaries;

	bool IsValid() const;
};

struct FBlueprintLensLC5LoadResult
{
	TSharedPtr<const FBlueprintLensLC5Profile> Profile;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FString Error;

	bool IsSuccess() const
	{
		return Profile.IsValid() && ExplanationModel.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensLC5ProfileLoader
{
public:
	static FBlueprintLensLC5LoadResult LoadFile(const FString& ProfilePath);
};
