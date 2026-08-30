// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensIntraBpPureFacts
{
	struct FIntraBpPureFactStats
	{
		int32 CandidateCount = 0;
		int32 BindingCount = 0;
	};

	bool ExportIntraBpPureCallFacts(
		const UBlueprint& Blueprint,
		const FString& CallNodeId,
		const FString& RawExportPath,
		FString& OutFilePath,
		FIntraBpPureFactStats& OutStats,
		FString& OutError);
}
