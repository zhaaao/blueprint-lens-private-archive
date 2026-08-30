// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensM6SessionPacket.h"
#include "Templates/ValueOrError.h"

class FBlueprintEditor;
class SGraphEditor;
class UEdGraph;
class UEdGraphNode;

struct FM6NativeSelectionObservation
{
	FString EntityId;
	FString GraphId;
	FString Label;
	bool bHasExecutionPin = true;
	bool bOutsideCurrentSession = false;
};
using FM6NativeGraphResult = TValueOrError<bool, FM6Error>;

class FM6NativeGraphBridge
{
public:
	FM6NativeGraphBridge() = default;
	explicit FM6NativeGraphBridge(TWeakPtr<FBlueprintEditor> InBlueprintEditor);

	FM6NativeGraphResult ApplyMembershipHighlight(
		const FM6BaselineFacts& Facts);
	TOptional<FM6NativeSelectionObservation> ObserveNativeSelection();
	FM6NativeGraphResult FocusSemanticEntity(const FString& EntityId);
	void Tick();
	void Clear();
	void SetSelectionObserver(
		TFunction<void(const FM6NativeSelectionObservation&)> InObserver);

#if WITH_DEV_AUTOMATION_TESTS
	FM6NativeGraphResult SelectEntityForVisibleReview(const FString& EntityId);
	FM6NativeGraphResult SelectOutsideEntityForVisibleReview(
		const FString& EntityId);
	bool ApplyMembershipForAutomationTest(
		const FM6BaselineFacts& Facts,
		const TArray<FString>& FullEntityIds,
		FM6Error& OutError);
	FM6NativeSelectionObservation ObserveSelectionForAutomationTest(
		const TArray<FString>& SelectedEntityIds);
	void TickForAutomationTest();
	const TSet<FString>& GetAppliedSelectionForAutomationTest() const
	{
		return AutomationAppliedSelection;
	}
#endif

private:
	FM6NativeGraphResult Fail(const TCHAR* Code, const TCHAR* Message) const;
	void RestoreMembership();
	FString StableEntityId(const UEdGraphNode& Node) const;

	TWeakPtr<FBlueprintEditor> BlueprintEditor;
	TWeakPtr<SGraphEditor> GraphEditor;
	TWeakObjectPtr<UEdGraph> Graph;
	TMap<FString, TWeakObjectPtr<UEdGraphNode>> MemberNodes;
	TSet<FString> MemberEntityIds;
	TFunction<void(const FM6NativeSelectionObservation&)> SelectionObserver;
	FString LastObservedEntityId;
	bool bApplyingSelection = false;
	bool bRestoreMembershipNextTick = false;

#if WITH_DEV_AUTOMATION_TESTS
	FString AutomationGraphId;
	TSet<FString> AutomationFullEntityIds;
	TSet<FString> AutomationAppliedSelection;
	TMap<FString, FString> AutomationEntityLabels;
	TSet<FString> AutomationExecutionEntityIds;
	FM6NativeGraphResult QueueVisibleReviewSelection(const FString& EntityId);
	TWeakObjectPtr<UEdGraphNode> PendingVisibleReviewNode;
	int32 PendingVisibleReviewDelayTicks = 0;
#endif
};
