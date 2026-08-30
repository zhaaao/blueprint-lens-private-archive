#pragma once

#include "BlueprintLensLC1TypedIrFacts.h"
#include "BlueprintLensLC4SequenceProfile.h"
#include "CoreMinimal.h"

namespace BlueprintLensLC4SequenceLiveBounds
{
	// The accepted disclosure rail owns four ordinal outputs.  Every candidate
	// root in the retained 249-slice live population declares two; the fixture
	// proves the same surface can account for four.  Only a strictly larger
	// inventory is outside this bounded LC4-SEQ adapter.
	constexpr int32 MaxDeclaredOutputsPerSequence = 4;
}

struct FBlueprintLensLC4SequenceLiveCase
{
	FString AnchorRelationId;
	FString SequenceUnitId;
	FString TargetUnitId;
	FBlueprintLensLC4SequenceProfile Profile;
	FBlueprintLensExplanationModel Explanation;
};

struct FBlueprintLensLC4SequenceLiveAdapterResult
{
	TArray<FBlueprintLensLC4SequenceLiveCase> Cases;
	int32 CandidateRelationCount = 0;
	int32 RejectedSequenceRootCount = 0;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return !Cases.IsEmpty() && DiagnosticCode.IsEmpty();
	}
};

class FBlueprintLensLC4SequenceLiveAdapter
{
public:
	static FBlueprintLensExplanationModel ApplyReaderDisambiguators(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1TypedIrFacts& TypedIrFacts);

	static FBlueprintLensLC4SequenceLiveAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1TypedIrFacts& TypedIrFacts);
};
