#pragma once

#include "BlueprintLensCompositeRailSlots.h"

namespace BlueprintLensF12DataAnswerBounds
{
// This is a local beside-station disclosure limit, not an adapter admission
// gate. Five is the maximum required_data_producer count measured across the
// eight retained M5 Data slices. A larger live answer remains renderable and
// visibly declares how many source identities were omitted from this view.
constexpr int32 MaxValueSourcesPerStation = 5;
} // namespace BlueprintLensF12DataAnswerBounds

struct FBlueprintLensF12DataRailAdapterResult
{
	FBlueprintLensExplanationModel Explanation;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return DiagnosticCode == TEXT("F12_DATA_RAIL_ADAPTED");
	}
};

class FBlueprintLensF12DataRailAdapter
{
public:
	static FBlueprintLensF12DataRailAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation);
};

struct FBlueprintLensF12DataStationDisclosure
{
	FString StationUnitId;
	TArray<FString> AttachedUnitIds;
	FString MarkerText;
	TArray<FString> DetailLines;
	TArray<FString> ValueSourceUnitIds;
	FString ValueSourceMarkerText;
	TArray<FString> ValueSourceDetailLines;
	bool bValueSourceDisclosureBounded = false;
};

struct FBlueprintLensF12DataAnswerProjection
{
	FString SourceIrSha256;
	FString SourceBlueprintAssetPath;
	int32 AnswerWriteCount = 0;
	int32 AnswerUnitCount = 0;
	int32 AnswerRelationCount = 0;
	TArray<FBlueprintLensF12DataStationDisclosure> Stations;
	FString DiagnosticCode;

	bool IsRenderable(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1RailProjection& Rail) const;
};

class FBlueprintLensF12DataAnswerProjector
{
public:
	static FBlueprintLensF12DataAnswerProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1RailProjection& Rail);

	static FBlueprintLensCompositeRailSlots Apply(
		const FBlueprintLensF12DataAnswerProjection& Projection,
		const FBlueprintLensCompositeRailSlots& BaseSlots);
};
