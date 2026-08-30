#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC3ValueConeProjectionStatus : uint8
{
	ValueCone,
	UngroupedFallback,
	Unavailable
};

namespace BlueprintLensLC3ValueConeBounds
{
constexpr int32 MinConeUnitCount = 2;
constexpr int32 MaxConeUnitCount = 6;
constexpr int32 MaxValueRelationCount = MaxConeUnitCount - 1;

FORCEINLINE bool IsSupportedTreeCardinality(
	const int32 ConeUnitCount,
	const int32 ValueRelationCount)
{
	return ConeUnitCount >= MinConeUnitCount &&
		ConeUnitCount <= MaxConeUnitCount &&
		ValueRelationCount == ConeUnitCount - 1;
}
} // namespace BlueprintLensLC3ValueConeBounds

struct FBlueprintLensLC3ValueConeStep
{
	FString RelationId;
	FString ProducerUnitId;
	FString ProducerReaderLabel;
	FString ProducerPortLabel;
	FString ConsumerUnitId;
	FString ConsumerReaderLabel;
	FString ConsumerPortLabel;
	int32 DerivationDepth = 0;
	int32 ConsumerInputCount = 0;
	TArray<FString> ConsumerInputPortLabels;
	FString ReaderRowText;
	int32 ProducerInputCount = 0;
	TArray<FString> ProducerInputPortLabels;
	FString ProducerInputSummaryText;
};

struct FBlueprintLensLC3ValueConeControl
{
	FString RelationId;
	FString ControllerUnitId;
	FString ControllerReaderLabel;
	FString ControllerPortLabel;
	FString TargetUnitId;
	FString TargetReaderLabel;
	FString TargetPortLabel;
	EBlueprintLensSemanticLabel SemanticLabel =
		EBlueprintLensSemanticLabel::NextExecution;
	FString ReaderRowText;
};

struct FBlueprintLensLC3ValueConeProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	FString CriterionUnitId;
	FString CriterionReaderLabel;
	int32 CriterionInputCount = 0;
	TArray<FString> CriterionInputPortLabels;
	FString GroupId;
	FString GroupTitle;
	TArray<FString> AllUnitIds;
	TArray<FString> AllRelationIds;
	TArray<FString> FallbackUnitIds;
	TArray<FString> FallbackRelationIds;
	TArray<FString> ConeUnitIds;
	TArray<FBlueprintLensLC3ValueConeStep> Steps;
	bool bHasControl = false;
	FBlueprintLensLC3ValueConeControl Control;
	TArray<FString> BoundaryNotices;
	TArray<FBlueprintLensClaimEvidence> GroupClaimEvidence;
	EBlueprintLensLC3ValueConeProjectionStatus Status =
		EBlueprintLensLC3ValueConeProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
};

class FBlueprintLensLC3ValueConeProjector
{
public:
	static FBlueprintLensLC3ValueConeProjection Build(
		const FBlueprintLensExplanationModel& Explanation);
};
