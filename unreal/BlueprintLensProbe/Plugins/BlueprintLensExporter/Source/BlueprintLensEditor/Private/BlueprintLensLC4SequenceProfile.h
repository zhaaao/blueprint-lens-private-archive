#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC4ConnectionState : uint8
{
	Connected,
	Unconnected
};

enum class EBlueprintLensLC4CriterionRelation : uint8
{
	Included,
	Outside,
	Indeterminate
};

enum class EBlueprintLensLC4TerminationKind : uint8
{
	OrdinaryReconvergence,
	Terminal,
	Unconnected
};

const TCHAR* LexToString(EBlueprintLensLC4ConnectionState Value);
const TCHAR* LexToString(EBlueprintLensLC4CriterionRelation Value);
const TCHAR* LexToString(EBlueprintLensLC4TerminationKind Value);

struct FBlueprintLensLC4SequenceSource
{
	FString BlueprintAssetPath;
	FString BlueprintPackageSha256;
	FString GraphId;
	FString SequenceNodeId;
	FString CriterionNodeId;
	FString IrPath;
	FString IrSha256;
	FString SlicePath;
	FString SliceSha256;
};

struct FBlueprintLensLC4SequenceOutput
{
	int32 Ordinal = INDEX_NONE;
	FString SourcePinId;
	FString SourcePinName;
	EBlueprintLensLC4ConnectionState ConnectionState =
		EBlueprintLensLC4ConnectionState::Unconnected;
	EBlueprintLensLC4CriterionRelation CriterionRelation =
		EBlueprintLensLC4CriterionRelation::Indeterminate;
	FString CriterionReason;
	TArray<FString> ConnectedEdgeIds;
	TArray<FString> ReachableNodeIds;
	TArray<FString> ReachableEdgeIds;
	EBlueprintLensLC4TerminationKind TerminationKind =
		EBlueprintLensLC4TerminationKind::Unconnected;
	FString TerminationNodeId;
};

struct FBlueprintLensLC4SequenceReconvergence
{
	FString NodeId;
	TArray<int32> IncomingOutputOrdinals;
	TArray<FString> SharedReachableNodeIds;
	TArray<FString> SharedReachableEdgeIds;
	FString CriterionNodeId;
};

struct FBlueprintLensLC4SequenceCounts
{
	int32 DeclaredOutputs = 0;
	int32 ConnectedOutputs = 0;
	int32 UnconnectedOutputs = 0;
	int32 CriterionIncludedOutputs = 0;
	int32 OutsideCriterionConnectedOutputs = 0;
	int32 IndeterminateOutputs = 0;
};

struct FBlueprintLensLC4SequenceProfile
{
	bool bLiveExplanation = false;
	FString Format;
	FString SchemaVersion;
	FString ProfileId;
	FString RulesVersion;
	FString QueryMode;
	FString ProfilePath;
	FString ProfileSha256;
	FBlueprintLensLC4SequenceSource Source;
	TArray<FBlueprintLensLC4SequenceOutput> Outputs;
	FBlueprintLensLC4SequenceReconvergence Reconvergence;
	FBlueprintLensLC4SequenceCounts Counts;
	TArray<FString> AccountedUnitIds;
	TArray<FString> AccountedRelationIds;

	bool IsValid() const;
	bool IsLiveBounded() const;
};

struct FBlueprintLensLC4SequenceLoadResult
{
	TSharedPtr<const FBlueprintLensLC4SequenceProfile> Profile;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FString Error;

	bool IsSuccess() const
	{
		return Profile.IsValid() && ExplanationModel.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensLC4SequenceProfileLoader
{
public:
	static FBlueprintLensLC4SequenceLoadResult LoadFile(
		const FString& ProfilePath);
};
