#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC2GuardOutlineProjectionStatus : uint8
{
	GroupedOutcomePaths,
	UngroupedFallback,
	Unavailable
};

struct FBlueprintLensLC2GuardOutlineRow
{
	FString UnitId;
	FString ReaderLabel;
	FString NextRelationId;
	bool bHasNextRelation = false;
	EBlueprintLensSemanticLabel NextRelationSemanticLabel =
		EBlueprintLensSemanticLabel::NextExecution;
	int32 NestingDepth = 0;
	FString GuardGroupId;
	FString ParentGuardGroupId;
	bool bHasParentGuard = false;
	FString GuardReaderLabel;
	FString ParentGuardReaderLabel;
	bool bHasEnteredBy = false;
	EBlueprintLensSemanticLabel EnteredBy =
		EBlueprintLensSemanticLabel::NextExecution;
};

struct FBlueprintLensLC2GuardOutlinePath
{
	FString GroupId;
	FString Title;
	FString EntryUnitId;
	FString ExitUnitId;
	TArray<FString> OrderedRelationIds;
	TArray<FBlueprintLensLC2GuardOutlineRow> Rows;
};

struct FBlueprintLensLC2GuardOutlineNest
{
	FString GroupId;
	FString ReaderLabel;
	TArray<FString> OrderedUnitIds;
	FString ParentGroupId;
	bool bHasParent = false;
	FString ParentReaderLabel;
	bool bHasEnteredBy = false;
	EBlueprintLensSemanticLabel EnteredBy =
		EBlueprintLensSemanticLabel::NextExecution;
};

struct FBlueprintLensLC2GuardOutlineProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	FString CriterionUnitId;
	TArray<FString> AllUnitIds;
	TArray<FString> AllRelationIds;
	TArray<FString> FallbackUnitIds;
	TArray<FString> FallbackRelationIds;
	TArray<FBlueprintLensLC2GuardOutlinePath> OutcomePaths;
	TArray<FBlueprintLensLC2GuardOutlineNest> GuardNests;
	TArray<TPair<FString, FString>> IncomparableGroupIds;
	FString GroupPartialOrderSemantics;
	EBlueprintLensLC2GuardOutlineProjectionStatus Status =
		EBlueprintLensLC2GuardOutlineProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
};

class FBlueprintLensLC2GuardOutlineProjector
{
public:
	static FBlueprintLensLC2GuardOutlineProjection Build(
		const FBlueprintLensExplanationModel& Explanation);
};
