// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace BlueprintLensM7CorpusFixture
{
	enum class EEnsureResult : uint8
	{
		Created,
		Unchanged
	};

	struct FFixtureSummary
	{
		FString AssetObjectPath;
		FString GraphId;
		int32 NodeCount = 0;
		int32 ExecutionEdgeCount = 0;
		int32 DataEdgeCount = 0;
	};

	bool EnsureFixture(
		FFixtureSummary& OutSummary,
		EEnsureResult& OutResult,
		FString& OutError);

	bool EnsureEngineSampleFixture(
		FFixtureSummary& OutSummary,
		EEnsureResult& OutResult,
		FString& OutError);
}
