// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensLC7StaticSCCFixture.h"
#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensLC7StaticSCCFacts
{
	struct FSCCFactStats
	{
		int32 NodeCount = 0;
		int32 EdgeCount = 0;
		int32 MemberCount = 0;
		int32 InternalEdgeCount = 0;
		int32 IncomingEdgeCount = 0;
		int32 OutgoingEdgeCount = 0;
	};

	bool ExportSCCFacts(
		const UBlueprint& Blueprint,
		const BlueprintLensLC7StaticSCCFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FSCCFactStats& OutStats,
		FString& OutError);
}
