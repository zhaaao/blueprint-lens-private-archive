// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraphNode;
class UEdGraphPin;

namespace BlueprintLensSequenceFacts
{
	struct FSequenceFactStats
	{
		int32 DeclaredOutputCount = 0;
		int32 ConnectedOutputCount = 0;
		int32 UnconnectedOutputCount = 0;
	};

	FString MakeNodeId(const FString& GraphId, const UEdGraphNode& Node);
	FString MakePinId(const FString& NodeId, const UEdGraphPin& Pin);

	bool ExportSequenceFacts(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		FString& OutFilePath,
		FSequenceFactStats& OutStats,
		FString& OutError);
}
