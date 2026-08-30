#pragma once

#include "BlueprintLensExplanationModel.h"

struct FBlueprintLensLC3LiveExplanationAdapterResult
{
	FBlueprintLensExplanationModel Explanation;
	FString DiagnosticCode;
	int32 InputUnitCount = 0;
	int32 InputRelationCount = 0;
	int32 AdaptedUnitCount = 0;
	int32 AdaptedRelationCount = 0;
	bool bAdapted = false;

	bool IsSuccess() const
	{
		return bAdapted &&
			DiagnosticCode == TEXT("LC3_LIVE_EXPLANATION_ADAPTED");
	}
};

// The accepted LC3 D3 projector requires one optional value-cone group. Live
// packets may carry the same port-bound value relations without that group.
// This adapter admits a non-empty value tree up to the shared six-unit,
// five-relation upper bound, rooted at the criterion, plus one unique execution
// controller. It may extract that core from a larger Explanation, but the
// rebuilt group must cover every adapted non-control unit. The existing projector
// is the postcondition; this class does not render or change traversal.
class FBlueprintLensLC3LiveExplanationAdapter
{
public:
	static FBlueprintLensLC3LiveExplanationAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation);
};
