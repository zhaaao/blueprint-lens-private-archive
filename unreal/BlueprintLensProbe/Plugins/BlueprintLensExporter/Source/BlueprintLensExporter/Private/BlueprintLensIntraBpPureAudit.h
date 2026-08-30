// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensIntraBpPureAudit
{
	struct FIntraBpPureAuditStats
	{
		int32 CandidateCount = 0;
		int32 BindingCount = 0;
	};

	bool AuditIntraBpPureCall(
		const UBlueprint& Blueprint,
		const FString& CallNodeId,
		const FString& RawExportPath,
		FString& OutFilePath,
		FIntraBpPureAuditStats& OutStats,
		FString& OutError);
}
