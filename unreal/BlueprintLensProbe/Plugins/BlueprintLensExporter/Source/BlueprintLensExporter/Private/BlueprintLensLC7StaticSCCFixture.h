// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace BlueprintLensLC7StaticSCCFixture
{
	struct FFixtureAnchors
	{
		FString AssetObjectPath;
		FString GraphId;
		FString EventNodeId;
		FString InitialiseNodeId;
		FString BranchNodeId;
		FString BodyNodeId;
		FString AdvanceNodeId;
		FString CriterionNodeId;
	};

	bool EnsureFixture(FFixtureAnchors& OutAnchors, FString& OutError);
}
