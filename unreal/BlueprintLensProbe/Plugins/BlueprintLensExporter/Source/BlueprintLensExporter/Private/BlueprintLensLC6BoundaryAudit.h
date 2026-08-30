// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensLC6BoundaryFixture.h"
#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensLC6BoundaryAudit
{
	struct FBoundaryAuditStats
	{
		int32 ScenarioCount = 0;
		int32 NodeCount = 0;
		int32 EdgeCount = 0;
	};

	bool AuditBoundarySource(
		const UBlueprint& Blueprint,
		const BlueprintLensLC6BoundaryFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FBoundaryAuditStats& OutStats,
		FString& OutError);
}
