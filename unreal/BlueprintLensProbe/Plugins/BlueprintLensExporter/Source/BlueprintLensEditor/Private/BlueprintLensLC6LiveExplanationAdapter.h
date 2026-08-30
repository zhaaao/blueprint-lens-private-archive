#pragma once

#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensLC1RailProjection.h"
#include "BlueprintLensLC6Projection.h"

struct FBlueprintLensLC6LiveExplanationAdapterResult
{
	FBlueprintLensLC6Projection Projection;
	FString SourceBlueprintAssetPath;
	FString SourceIrSha256;
	TArray<FString> BoundaryUnitIds;
	TArray<FString> AbsentScenarioIds;
	FString AbsenceStatement;
	FString ContributionStatement;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return Projection.IsRenderable() && !SourceBlueprintAssetPath.IsEmpty() &&
			!SourceIrSha256.IsEmpty() && !BoundaryUnitIds.IsEmpty() &&
			!AbsenceStatement.IsEmpty() && !ContributionStatement.IsEmpty() &&
			DiagnosticCode == TEXT("LC6_LIVE_ADAPTER_COMPLETE");
	}
};

class FBlueprintLensLC6LiveExplanationAdapter
{
public:
	static constexpr int32 MaxLiveBoundaryTracks = 4;

	static FBlueprintLensLC6LiveExplanationAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1RailProjection& Rail);
};
