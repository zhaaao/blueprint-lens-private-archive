#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

enum class EBlueprintLensLC1RailProjectionStatus : uint8
{
	Complete,
	Unavailable
};

struct FBlueprintLensLC1RailCanonicalUnit
{
	FString UnitId;
	FString ReaderLabel;
	FString DisplayLabel;
	bool bIsCriterion = false;
};

struct FBlueprintLensLC1RailExecutionRelation
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
};

enum class EBlueprintLensLC1RailOrderRegionKind : uint8
{
	Incomparable,
	StronglyConnected
};

struct FBlueprintLensLC1RailOrderRegion
{
	FString RegionId;
	EBlueprintLensLC1RailOrderRegionKind Kind =
		EBlueprintLensLC1RailOrderRegionKind::Incomparable;
	TArray<FString> MemberUnitIds;
	FString ReaderText;
};

struct FBlueprintLensLC1RailBoundaryCap
{
	FString UnitId;
	EBlueprintLensSemanticStatus SemanticStatus =
		EBlueprintLensSemanticStatus::Supported;
	FString Title;
	FString Disclosure;
};

struct FBlueprintLensLC1RailProjection
{
	FString SourceIrSha256;
	FString ProjectorVersion;
	FString CriterionUnitId;
	FString CriterionReaderLabel;
	FString CriterionDisplayLabel;
	TArray<FString> AllUnitIds;
	TArray<FString> AllRelationIds;
	TArray<FBlueprintLensLC1RailCanonicalUnit> OrderedCanonicalUnits;
	TArray<FBlueprintLensLC1RailExecutionRelation> OrderedExecutionRelations;
	// Every proven relation whose endpoints are both rail stations constrains
	// vertical order, including relations deliberately retained outside the
	// execution-edge drawing ledger.
	TArray<FBlueprintLensLC1RailExecutionRelation> StationOrderRelations;
	TArray<FBlueprintLensLC1RailOrderRegion> OrderRegions;
	TArray<FBlueprintLensLC1RailBoundaryCap> BoundaryCaps;
	TArray<FString> DeferredUnitIds;
	TArray<FString> DeferredRelationIds;
	TArray<FString> FallbackUnitIds;
	TArray<FString> FallbackRelationIds;
	EBlueprintLensLC1RailProjectionStatus Status =
		EBlueprintLensLC1RailProjectionStatus::Unavailable;
	FString DiagnosticCode;
	FString ProjectionIntegrityHash;

	bool HasValidIntegrity() const;
	bool IsRenderable() const;
};

class FBlueprintLensLC1RailProjector
{
public:
	static FBlueprintLensLC1RailProjection Build(
		const FBlueprintLensExplanationModel& Explanation);
};
