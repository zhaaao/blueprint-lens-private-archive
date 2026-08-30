// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensLC7StaticSCCFixture.h"
#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensLC7StaticSCCAudit
{
	struct FSCCAuditStats
	{
		int32 NodeCount = 0;
		int32 EdgeCount = 0;
		int32 MemberCount = 0;
		int32 InternalEdgeCount = 0;
		int32 IncomingEdgeCount = 0;
		int32 OutgoingEdgeCount = 0;
	};

	bool AuditSCCSource(
		const UBlueprint& Blueprint,
		const BlueprintLensLC7StaticSCCFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FSCCAuditStats& OutStats,
		FString& OutError);
}
