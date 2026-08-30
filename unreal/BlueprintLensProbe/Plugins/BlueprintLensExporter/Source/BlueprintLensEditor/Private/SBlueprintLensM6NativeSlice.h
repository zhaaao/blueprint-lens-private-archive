// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6BaselineProjection.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FM6NativeSliceEntitySelected, FString);
DECLARE_DELEGATE_OneParam(FM6NativeSliceSourceRequested, FString);

class SBlueprintLensM6NativeSlice final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensM6NativeSlice)
		: _ViewModel(nullptr)
	{
	}
		SLATE_ARGUMENT(const FM6NativeSliceViewModel*, ViewModel)
		SLATE_EVENT(FM6NativeSliceEntitySelected, OnEntitySelected)
		SLATE_EVENT(FM6NativeSliceSourceRequested, OnSourceRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

#if WITH_DEV_AUTOMATION_TESTS
	bool IsReadOnlyForAutomationTest() const
	{
		return ViewModel != nullptr && ViewModel->bReadOnly;
	}
	int32 EntityCountForAutomationTest() const
	{
		return ViewModel == nullptr ? 0 : ViewModel->Entities.Num();
	}
	int32 RelationCountForAutomationTest() const
	{
		return ViewModel == nullptr ? 0 : ViewModel->Relations.Num();
	}
#endif

private:
	FReply SelectEntity(FString EntityId);
	FReply RequestSource(FString EntityId);

	const FM6NativeSliceViewModel* ViewModel = nullptr;
	FM6NativeSliceEntitySelected OnEntitySelected;
	FM6NativeSliceSourceRequested OnSourceRequested;
};
