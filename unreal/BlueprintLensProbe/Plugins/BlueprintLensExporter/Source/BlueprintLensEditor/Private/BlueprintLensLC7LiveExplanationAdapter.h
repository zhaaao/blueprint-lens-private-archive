#pragma once

#include "BlueprintLensLC7Profile.h"

namespace BlueprintLensLC7LiveBounds
{
constexpr int32 MaxUnits = 10;
constexpr int32 MaxRelations = 10;
constexpr int32 MaxSCCMembers = 6;
constexpr int32 MaxInternalExecutionRelations = 6;
constexpr int32 MaxIncomingExecutionRelations = 2;
constexpr int32 MaxOutgoingExecutionRelations = 1;
constexpr int32 MaxPredicateRelations = 1;
constexpr int32 MaxValueRelations = 1;
}

struct FBlueprintLensLC7LiveExplanationAdapterResult
{
	TSharedPtr<const FBlueprintLensLC7Profile> Profile;
	FString DiagnosticCode;

	bool IsSuccess() const
	{
		return Profile.IsValid() && Profile->IsValid() &&
			DiagnosticCode == TEXT("LC7_LIVE_ADAPTER_COMPLETE");
	}
};

class FBlueprintLensLC7LiveExplanationAdapter
{
public:
	static FBlueprintLensLC7LiveExplanationAdapterResult Build(
		const FBlueprintLensExplanationModel& Explanation);
};
