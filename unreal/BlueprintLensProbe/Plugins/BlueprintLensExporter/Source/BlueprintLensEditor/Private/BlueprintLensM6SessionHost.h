// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBlueprintEditor;
class FM6SessionController;
class IM6SessionView;
struct FM6Error;

namespace BlueprintLensM6SessionHost
{
FString WorkspaceRoot();
}

class FM6SessionHost
{
public:
	FM6SessionHost(
		TWeakPtr<FBlueprintEditor> BlueprintEditor,
		IM6SessionView& View);
	~FM6SessionHost();

	FM6SessionController& Controller();
	void Tick(double NowSeconds);
	void BeginStage(const FString& Stage, bool bApplicable);
	void FinishStage(const FString& Stage, const FM6Error& Error);
	void RecordSourceJump(const FString& EntityId, const FString& ErrorCode);

private:
	class FImpl;
	TUniquePtr<FImpl> Impl;
};
