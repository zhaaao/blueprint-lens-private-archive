#pragma once

#include "BlueprintLensLC1TypedIrFacts.h"
#include "BlueprintLensLC5Projection.h"

namespace BlueprintLensLC5LiveBounds
{
	// The complete live population with an exported self-context callee body
	// measures 3, 5, 11 and 16 nodes. A strictly larger body is outside this
	// bounded adapter; no equality is an admission condition.
	constexpr int32 MaxCalleeBodyUnits = 16;
}

enum class EBlueprintLensLC5LiveClaimState : uint8
{
	FrozenConditionInstance,
	BeyondFrozenImpure,
	BodyUnavailable,
	Refused
};

struct FBlueprintLensLC5LiveCallCase
{
	FString CallUnitId;
	FString CallTitle;
	FString CalleeName;
	EBlueprintLensLC5LiveClaimState State =
		EBlueprintLensLC5LiveClaimState::Refused;
	FString ReaderStatement;
	FString DiagnosticCode;
	FBlueprintLensLC5Projection Projection;

	bool IsRenderable() const
	{
		return (State ==
				EBlueprintLensLC5LiveClaimState::FrozenConditionInstance ||
			State == EBlueprintLensLC5LiveClaimState::BeyondFrozenImpure) &&
			Projection.IsRenderable();
	}
};

struct FBlueprintLensLC5LiveTypedIrAdapterResult
{
	TArray<FBlueprintLensLC5LiveCallCase> Cases;
	int32 CandidateCallUnitCount = 0;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return !Cases.IsEmpty() && DiagnosticCode.IsEmpty();
	}
};

class FBlueprintLensLC5LiveTypedIrAdapter
{
public:
	static FBlueprintLensLC5LiveTypedIrAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1TypedIrFacts& TypedIrFacts);
};
