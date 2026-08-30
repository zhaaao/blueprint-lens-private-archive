// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensLC4AsyncFixture
{
	bool EnsureFixture(
		FString& OutAssetObjectPath,
		FString& OutSequenceNodeId,
		FString& OutCriterionNodeId,
		FString& OutError);

	bool ApplyScheduleVariant(
		UBlueprint& Blueprint,
		const FString& ScheduleVariant,
		FString& OutError);
}
