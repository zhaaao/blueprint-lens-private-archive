// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBlueprintEditor;
class SGraphEditor;
class UEdGraph;

struct FM6GraphResolution
{
	UEdGraph* Graph = nullptr;
	TSharedPtr<SGraphEditor> GraphEditor;

	bool IsValid() const
	{
		return Graph != nullptr;
	}
};

/** Resolves the current Blueprint graph without trusting BlueprintEditor focus alone. */
class FM6GraphResolver
{
public:
	/**
	 * Resolve an exact graph identity when GraphPath is supplied. With no path,
	 * recover the current graph from focus, open graph tabs, or an unambiguous
	 * single LastEditedDocuments entry.
	 */
	static FM6GraphResolution Resolve(
		const TSharedPtr<FBlueprintEditor>& BlueprintEditor,
		const FString& GraphPath = FString());
};
