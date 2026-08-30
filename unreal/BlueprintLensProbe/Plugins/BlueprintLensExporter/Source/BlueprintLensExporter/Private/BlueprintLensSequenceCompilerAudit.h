// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensSequenceCompilerAudit
{
	struct FSequenceCompilerAuditStats
	{
		int32 ConnectedOutputCount = 0;
	};

	bool AuditSequenceCompilerOrder(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		FString& OutFilePath,
		FSequenceCompilerAuditStats& OutStats,
		FString& OutError);
}
