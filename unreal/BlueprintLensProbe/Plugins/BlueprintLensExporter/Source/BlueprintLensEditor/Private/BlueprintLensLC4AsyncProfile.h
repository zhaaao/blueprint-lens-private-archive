#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

struct FBlueprintLensLC4AsyncCounts
{
	int32 InvocationCount = 0;
	int32 RelationCount = 0;
	int32 IncomparabilityCheckCount = 0;
	int32 ContinuationCount = 0;
	int32 ParticipantCount = 0;
	int32 ScheduleVariantCount = 0;
};

struct FBlueprintLensLC4AsyncSource
{
	FString AssetPath;
	FString AssetSha256;
	FString GraphId;
	FString SequenceNodeId;
	FString CriterionNodeId;
	FString CriterionSourceAction;
};

struct FBlueprintLensLC4AsyncContinuation
{
	FString ParticipantId;
	FString NodeId;
	FString ResumePinId;
};

struct FBlueprintLensLC4AsyncLaunch
{
	int32 Ordinal = INDEX_NONE;
	FString ParticipantId;
	FString NodeId;
	FString SourcePinId;
};

struct FBlueprintLensLC4AsyncArrival
{
	FString ParticipantId;
	FString NodeId;
	FString ExecutePinId;
	FString ReleasePinId;
};

struct FBlueprintLensLC4AsyncRelation
{
	FString RelationId;
	FString RelationType;
	FString FromId;
	FString ToId;
	FString ClaimScope;
};

struct FBlueprintLensLC4AsyncProof
{
	FString LeftParticipantId;
	FString RightParticipantId;
	FString LeftCompletionEventId;
	FString RightCompletionEventId;
	bool bLeftReachesRight = true;
	bool bRightReachesLeft = true;
	bool bRelationSetComplete = false;
	FString Result;
	FString ProofBasis;
	TArray<FString> EvidenceRelationIds;
};

struct FBlueprintLensLC4AsyncInvocation
{
	FString ProductId;
	FString Variant;
	FString InvocationId;
	FString InstanceId;
	FString RunId;
	FString TraceId;
	TArray<FString> LaunchEventIds;
	TArray<FString> CompletionEventIds;
	TArray<FString> ArrivalEventIds;
	FString ReleaseEventId;
	FString CriterionEventId;
	TArray<FString> CompletionOrder;
	TArray<FBlueprintLensLC4AsyncRelation> Relations;
	TArray<FBlueprintLensLC4AsyncProof> Proofs;
	bool bComplete = false;
	FString CloseReason;
};

struct FBlueprintLensLC4AsyncBoundary
{
	FString Kind;
	FString Detail;
	FString Support;
};

struct FBlueprintLensLC4AsyncProfile
{
	FString Format;
	FString SchemaVersion;
	FString ProfileId;
	FString RulesVersion;
	FString ValidationState;
	FString ProfilePath;
	FString ProfileSha256;
	FBlueprintLensLC4AsyncCounts Counts;
	FBlueprintLensLC4AsyncSource Source;
	TArray<FBlueprintLensLC4AsyncContinuation> Continuations;
	TArray<FBlueprintLensLC4AsyncLaunch> Launches;
	TArray<FBlueprintLensLC4AsyncArrival> Arrivals;
	TArray<FBlueprintLensLC4AsyncInvocation> Invocations;
	TArray<FBlueprintLensLC4AsyncBoundary> Boundaries;
	TArray<FString> ParticipantIds;
	bool bBarrierSingleFire = false;
	FString BarrierSiteId;
	FString ReleaseSiteId;
	FString ResetPolicy;
	FString CancelPolicy;

	bool IsValid() const;
};

struct FBlueprintLensLC4AsyncLoadResult
{
	TSharedPtr<const FBlueprintLensLC4AsyncProfile> Profile;
	TSharedPtr<const FBlueprintLensExplanationModel> ExplanationModel;
	FString Error;

	bool IsSuccess() const
	{
		return Profile.IsValid() && ExplanationModel.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensLC4AsyncProfileLoader
{
public:
	static FBlueprintLensLC4AsyncLoadResult LoadFile(const FString& ProfilePath);
};
