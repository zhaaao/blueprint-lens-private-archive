#pragma once

#include "BlueprintLensExplanationModel.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

enum class EBlueprintLensSourceState : uint8
{
	Ready,
	Unsaved,
	Stale,
	Unresolved
};

struct FBlueprintLensResolvedSource
{
	EBlueprintLensSourceState State =
		EBlueprintLensSourceState::Unresolved;
	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakObjectPtr<UEdGraph> Graph;
	TWeakObjectPtr<UEdGraphNode> Node;
	FString Message;
};

class FBlueprintLensSourceNavigator
{
public:
	FBlueprintLensResolvedSource Resolve(
		const FBlueprintLensSource& Source,
		const FBlueprintLensSourceReference& Reference) const;

	bool Navigate(
		const FBlueprintLensResolvedSource& Source,
		FString& OutError) const;
};
