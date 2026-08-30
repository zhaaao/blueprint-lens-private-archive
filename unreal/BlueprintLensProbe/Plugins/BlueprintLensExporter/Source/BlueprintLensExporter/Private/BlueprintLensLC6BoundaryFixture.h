// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace BlueprintLensLC6BoundaryFixture
{
	struct FScenarioAnchors
	{
		FString ScenarioId;
		FString RootNodeId;
		FString CriterionNodeId;
	};

	struct FFixtureAnchors
	{
		FString AssetObjectPath;
		FString GraphId;
		TArray<FScenarioAnchors> Scenarios;
	};

	bool EnsureFixture(FFixtureAnchors& OutAnchors, FString& OutError);
}
