#pragma once

#include "BlueprintLensLC5Profile.h"

enum class EBlueprintLensLC5ProjectionStatus : uint8
{
	TypedPortalBridge,
	Frontier,
	Unavailable
};

struct FBlueprintLensLC5LegendEntry
{
	FString SemanticId;
	FString ReaderLabel;
	FString Family;
};

struct FBlueprintLensLC5Projection
{
	bool bLiveCallBody = false;
	FString SourceBlueprintAssetPath;
	FString CallUnitId;
	FString CallerGraphId;
	FString CalleeGraphName;
	FString ClaimState;
	FString ClaimBoundaryStatement;
	FString StaticOrderStatement;
	TArray<FBlueprintLensLC5LegendEntry> LegendEntries;
	TMap<FString, FString> LiveOccurrenceLabels;
	TMap<FString, int32> LiveStaticRanks;
	FString ProjectorVersion;
	FString SourceProfileSha256;
	FBlueprintLensLC5SourceIdentity SourceIdentity;
	FBlueprintLensLC5CallContext CallContext;
	TArray<FBlueprintLensLC5Occurrence> Occurrences;
	TArray<FBlueprintLensLC5Binding> Bindings;
	TArray<FBlueprintLensLC5InternalRelation> InternalRelations;
	TArray<FBlueprintLensLC5ContextBoundary> ContextBoundaries;
	TArray<FString> AllRelationIds;
	TArray<FString> SourceNodeIds;
	TArray<FString> ActionIds;
	TArray<FString> BoundaryText;
	FString ProjectionIntegrityHash;
	FString DiagnosticCode;
	EBlueprintLensLC5ProjectionStatus Status =
		EBlueprintLensLC5ProjectionStatus::Unavailable;

	bool IsRenderable() const;
};

class FBlueprintLensLC5Projector
{
public:
	static FBlueprintLensLC5Projection Build(
		const FBlueprintLensLC5Profile& Profile);
};
