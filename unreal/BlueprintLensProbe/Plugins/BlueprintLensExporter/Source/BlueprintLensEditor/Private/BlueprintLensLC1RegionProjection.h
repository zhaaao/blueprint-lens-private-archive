#pragma once

#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensLC1TypedIrFacts.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC1RegionProjectionStatus : uint8
{
	CompleteOperationRegion,
	OrderedVariableAssignments,
	StructuralRun,
	Unavailable
};

struct FBlueprintLensLC1ClaimEvidence
{
	FString ClaimPart;
	FString FactOwner;
	FString SourceId;
	FString Value;
};

struct FBlueprintLensLC1RegionProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	FString RegionId;
	FString RegionKind;
	TArray<FString> OrderedMemberUnitIds;
	TArray<FString> InternalRelationIds;
	TArray<FString> IncomingRelationIds;
	TArray<FString> OutgoingRelationIds;
	FString FirstMemberUnitId;
	FString LastMemberUnitId;
	FString SummaryTemplateId;
	TArray<FString> SummaryArguments;
	TArray<FBlueprintLensLC1ClaimEvidence> ClaimEvidence;
	EBlueprintLensLC1RegionProjectionStatus Status =
		EBlueprintLensLC1RegionProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
};

class FBlueprintLensLC1RegionProjector
{
public:
	static FBlueprintLensLC1RegionProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1TypedIrFacts& TypedIrFacts);
};
