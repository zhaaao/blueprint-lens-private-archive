#pragma once

#include "BlueprintLensLC4AsyncProfile.h"

enum class EBlueprintLensLC4AsyncProjectionStatus : uint8
{
	PartialOrderJoin,
	Frontier,
	Unavailable
};

struct FBlueprintLensLC4AsyncProjection
{
	FString ProjectorVersion;
	FString SourceProfileSha256;
	FString Variant;
	FBlueprintLensLC4AsyncSource Source;
	TArray<FBlueprintLensLC4AsyncContinuation> Continuations;
	TArray<FBlueprintLensLC4AsyncLaunch> Launches;
	TArray<FBlueprintLensLC4AsyncArrival> Arrivals;
	TArray<FBlueprintLensLC4AsyncInvocation> Invocations;
	TArray<FBlueprintLensLC4AsyncProof> Proofs;
	TArray<FBlueprintLensLC4AsyncBoundary> Boundaries;
	TArray<FString> AllRelationIds;
	TArray<FString> SourceEntityIds;
	FString StructuralSignature;
	FString EvidenceSignature;
	FString ProjectionIntegrityHash;
	FString DiagnosticCode;
	EBlueprintLensLC4AsyncProjectionStatus Status =
		EBlueprintLensLC4AsyncProjectionStatus::Unavailable;

	bool IsRenderable() const;
};

class FBlueprintLensLC4AsyncProjector
{
public:
	static FBlueprintLensLC4AsyncProjection Build(
		const FBlueprintLensLC4AsyncProfile& Profile,
		const FString& Variant);
};
