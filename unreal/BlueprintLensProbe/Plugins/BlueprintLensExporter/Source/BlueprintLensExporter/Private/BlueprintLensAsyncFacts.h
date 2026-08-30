// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensAsyncFacts
{
	struct FAsyncFactStats
	{
		int32 LaunchCount = 0;
		int32 ContinuationCount = 0;
		int32 ParticipantCount = 0;
	};

	bool ExportAsyncFacts(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		const FString& CriterionNodeId,
		FString& OutFilePath,
		FAsyncFactStats& OutStats,
		FString& OutError);
}
