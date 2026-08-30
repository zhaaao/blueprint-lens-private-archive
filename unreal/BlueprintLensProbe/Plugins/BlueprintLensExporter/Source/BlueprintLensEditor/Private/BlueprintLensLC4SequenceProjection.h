#pragma once

#include "BlueprintLensLC4SequenceProfile.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC4SequenceProjectionStatus : uint8
{
	DisclosureRail,
	CompleteTextFallback,
	Unavailable
};

struct FBlueprintLensLC4SequenceRoute
{
	int32 Ordinal = INDEX_NONE;
	FString SourcePinId;
	FString SourcePinName;
	EBlueprintLensLC4ConnectionState ConnectionState =
		EBlueprintLensLC4ConnectionState::Unconnected;
	EBlueprintLensLC4CriterionRelation CriterionRelation =
		EBlueprintLensLC4CriterionRelation::Indeterminate;
	FString CriterionReason;
	EBlueprintLensLC4TerminationKind TerminationKind =
		EBlueprintLensLC4TerminationKind::Unconnected;
	TArray<FString> RouteUnitIds;
	TArray<FString> RouteReaderLabels;
	TArray<FString> RouteRelationIds;
	FString SummaryText;
};

struct FBlueprintLensLC4SequenceMerge
{
	FString NodeId;
	FString ReaderLabel;
	TArray<int32> IncomingOutputOrdinals;
	TArray<FString> SharedSuffixUnitIds;
	TArray<FString> SharedSuffixReaderLabels;
	TArray<FString> SharedSuffixRelationIds;
};

struct FBlueprintLensLC4SequenceProjection
{
	bool bLiveExplanation = false;
	FString ProjectorVersion;
	FString SourceProfileSha256;
	FString SourceIrSha256;
	FString SequenceUnitId;
	FString SequenceReaderLabel;
	FString CriterionUnitId;
	FString CriterionReaderLabel;
	TArray<FBlueprintLensLC4SequenceRoute> Routes;
	FBlueprintLensLC4SequenceMerge Merge;
	FBlueprintLensLC4SequenceCounts Counts;
	TArray<FString> AllUnitIds;
	TArray<FString> AllRelationIds;
	TArray<FString> BoundaryNotices;
	EBlueprintLensLC4SequenceProjectionStatus Status =
		EBlueprintLensLC4SequenceProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
	const FBlueprintLensLC4SequenceRoute* FindRoute(int32 Ordinal) const;
};

class FBlueprintLensLC4SequenceProjector
{
public:
	static FBlueprintLensLC4SequenceProjection Build(
		const FBlueprintLensLC4SequenceProfile& Profile,
		const FBlueprintLensExplanationModel& Explanation);
};
