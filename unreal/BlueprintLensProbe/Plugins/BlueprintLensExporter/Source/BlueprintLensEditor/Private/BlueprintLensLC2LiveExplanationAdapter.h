#pragma once

#include "BlueprintLensExplanationModel.h"

struct FBlueprintLensLC2LiveExplanationAdapterResult
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
			DiagnosticCode == TEXT("LC2_LIVE_EXPLANATION_ADAPTED");
	}
};

// The accepted LC2 projector consumes generic Explanation Models, but its D2
// surface additionally requires the optional outcome-path and guard-nest cover.
// Live M6 packets carry the same predicate/control/reconvergence facts without
// that optional cover. This adapter is deliberately bounded to exactly two
// guards at depth two with three reconverging outcomes. It extracts only that
// structurally proven core from a larger live Explanation, requires the rebuilt
// group cover to account for every extracted unit, then hands the core back to
// the existing LC2 projector/layout/canvas chain. It never reads a fixture or
// changes traversal.
class FBlueprintLensLC2LiveExplanationAdapter
{
public:
	static FBlueprintLensLC2LiveExplanationAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation);
};
