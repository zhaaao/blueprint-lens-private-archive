// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6Types.h"
#include "Templates/SharedPointer.h"

class FBlueprintEditor;
class UBlueprint;
class UEdGraph;

class FM6Preflight
{
public:
	static bool ComputeSourceFingerprint(
		const UBlueprint& Blueprint,
		FString& OutSha256);

	static FM6PreflightResult Evaluate(
		const TSharedPtr<FBlueprintEditor>& BlueprintEditor,
		const FM6QueryInput& Query,
		const FString& OwnedStagingRoot);

#if WITH_DEV_AUTOMATION_TESTS
	static UEdGraph* ResolveQueryGraphForAutomationTest(
		UBlueprint* Blueprint,
		UEdGraph* FocusedGraph,
		const FString& GraphId);

	static FM6PreflightResult EvaluateResolvedForAutomationTest(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FM6QueryInput& Query,
		const FString& OwnedStagingRoot);
#endif
};
