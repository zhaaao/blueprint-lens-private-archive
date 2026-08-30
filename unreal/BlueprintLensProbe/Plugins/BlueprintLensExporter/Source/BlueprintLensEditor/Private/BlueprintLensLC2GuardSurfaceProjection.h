#pragma once

#include "BlueprintLensLC2GuardOutlineProjection.h"

enum class EBlueprintLensLC2GuardSurfaceProjectionStatus : uint8
{
	GuardRails,
	Unavailable
};

struct FBlueprintLensLC2GuardCanonicalUnit
{
	FString UnitId;
	FString ReaderLabel;
	FString OwnerGuardGroupId;
	bool bIsPredicate = false;
	bool bIsBranch = false;
	bool bIsCriterion = false;
};

struct FBlueprintLensLC2GuardCompound
{
	FString GroupId;
	FString ParentGroupId;
	FString PredicateUnitId;
	FString BranchUnitId;
	FString GuardReaderText;
	TArray<FString> ExclusiveMemberUnitIds;
};

struct FBlueprintLensLC2GuardOutcomeRail
{
	FString GroupId;
	FString Title;
	FString EntryUnitId;
	FString OutcomeUnitId;
	FString ReconvergenceRelationId;
	TArray<FString> OrderedRelationIds;
	TArray<FString> CanonicalUnitIds;
};

struct FBlueprintLensLC2GuardForkMark
{
	FString BranchUnitId;
	TArray<FString> OutcomeGroupIds;
	FString ReaderText;
};

struct FBlueprintLensLC2GuardSurfaceProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	FString CriterionUnitId;
	TArray<FString> AllUnitIds;
	TArray<FString> AllRelationIds;
	TArray<FBlueprintLensLC2GuardCanonicalUnit> CanonicalUnits;
	TArray<FBlueprintLensLC2GuardCompound> Compounds;
	TArray<FBlueprintLensLC2GuardOutcomeRail> OutcomeRails;
	TArray<FBlueprintLensLC2GuardForkMark> ForkMarks;
	TArray<TPair<FString, FString>> IncomparableGroupIds;
	FString NoOrderReaderText;
	EBlueprintLensLC2GuardSurfaceProjectionStatus Status =
		EBlueprintLensLC2GuardSurfaceProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
	const FBlueprintLensLC2GuardCanonicalUnit* FindCanonicalUnit(
		const FString& UnitId) const;
	const FBlueprintLensLC2GuardCompound* FindCompound(
		const FString& GroupId) const;
};

class FBlueprintLensLC2GuardSurfaceProjector
{
public:
	static FBlueprintLensLC2GuardSurfaceProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC2GuardOutlineProjection& Outline);
};
