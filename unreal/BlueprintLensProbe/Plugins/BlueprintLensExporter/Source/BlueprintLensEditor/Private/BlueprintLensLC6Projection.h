#pragma once

#include "BlueprintLensLC6Profile.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC6ProjectionStatus : uint8
{
	FourTrack,
	CompleteText,
	Unavailable
};

struct FBlueprintLensLC6OwnerBand
{
	FString TruthOwner;
	TArray<FString> ScenarioIds;
};

struct FBlueprintLensLC6Relation
{
	FString RelationId;
	FString ScenarioId;
	FString Kind;
	FString SourceNodeId;
	FString TargetNodeId;
};

struct FBlueprintLensLC6Track
{
	FString ScenarioId;
	FString TruthOwner;
	FString Status;
	FString Reason;
	FString RootNodeId;
	FString RootTitle;
	FString CriterionNodeId;
	FString CriterionTitle;
	FString BoundaryNodeId;
	FString BoundaryTitle;
	TArray<FString> MemberIds;
	TArray<FString> RelationIds;
	TArray<FString> EvidenceIds;
	TArray<FBlueprintLensLC6HopDistance> HopDistances;
	TArray<FBlueprintLensLC6Frontier> Frontiers;
	bool bHasSemanticFence = false;
	bool bHasOmissionAggregate = false;
	int32 MaxUpstreamHops = INDEX_NONE;
	int32 SelectedNodeCount = 0;
	int32 SelectedEdgeCount = 0;
	int32 CompleteNodeCount = 0;
	int32 CompleteEdgeCount = 0;
	int32 OmittedNodeCount = 0;
	int32 OmittedEdgeCount = 0;
};

struct FBlueprintLensLC6Projection
{
	static constexpr int32 MaxTrackCount = 4;

	EBlueprintLensLC6ProjectionStatus Status =
		EBlueprintLensLC6ProjectionStatus::Unavailable;
	bool bLiveBoundaryTracks = false;
	FString SourceBlueprintAssetPath;
	FString SourceIrSha256;
	FString DiagnosticCode;
	FString IntegrityHash;
	TArray<FBlueprintLensLC6OwnerBand> OwnerBands;
	TArray<FBlueprintLensLC6Track> Tracks;
	TArray<FBlueprintLensLC6Relation> Relations;
	TSet<FString> AllMemberIds;
	TSet<FString> AllRelationIds;
	TSet<FString> AllEvidenceIds;
	TArray<FString> BoundaryUnitIds;
	TArray<FString> AbsentScenarioIds;
	FString AbsenceStatement;
	FString ContributionStatement;
	TArray<FString> CompleteTextLines;

	bool IsRenderable() const
	{
		return Status == EBlueprintLensLC6ProjectionStatus::FourTrack &&
			Tracks.Num() > 0 && Tracks.Num() <= MaxTrackCount &&
			!IntegrityHash.IsEmpty() &&
			(!bLiveBoundaryTracks ||
				(!SourceBlueprintAssetPath.IsEmpty() &&
				 !SourceIrSha256.IsEmpty() &&
				 BoundaryUnitIds.Num() == Tracks.Num() &&
				 !AbsenceStatement.IsEmpty() &&
				 !ContributionStatement.IsEmpty()));
	}

	const FBlueprintLensLC6Track* FindTrack(const FString& ScenarioId) const;
};

class FBlueprintLensLC6Projector
{
public:
	static FBlueprintLensLC6Projection Build(
		const FBlueprintLensLC6Profile& Profile);
};
