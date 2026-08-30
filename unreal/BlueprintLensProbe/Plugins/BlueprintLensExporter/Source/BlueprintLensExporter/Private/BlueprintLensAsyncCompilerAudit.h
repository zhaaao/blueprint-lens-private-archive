// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensAsyncCompilerAudit
{
	struct FAsyncCompilerAuditStats
	{
		int32 LinkageCount = 0;
		int32 MatchedSourceCount = 0;
	};

	bool AuditAsyncCompilerLinkage(
		const UBlueprint& Blueprint,
		const FString& SourceFactsPath,
		FString& OutFilePath,
		FAsyncCompilerAuditStats& OutStats,
		FString& OutError);
}
