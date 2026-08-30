// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6GraphResolver.h"

#include "BlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "GraphEditor.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
struct FOpenGraphCandidate
{
	UEdGraph* Graph = nullptr;
	TSharedPtr<SGraphEditor> GraphEditor;
	bool bActive = false;
	bool bForeground = false;
	double LastActivationTime = 0.0;
};

bool BelongsToBlueprint(
	const UBlueprint& Blueprint,
	const UEdGraph* Graph)
{
	if (Graph == nullptr) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(Graphs);
	return Graphs.Contains(Graph);
}

UEdGraph* FindGraphByPath(
	const UBlueprint& Blueprint,
	const FString& GraphPath)
{
	if (GraphPath.IsEmpty()) return nullptr;
	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(Graphs);
	UEdGraph* Match = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph == nullptr || Graph->GetPathName() != GraphPath) continue;
		if (Match != nullptr) return nullptr;
		Match = Graph;
	}
	return Match;
}

TSharedPtr<SGraphEditor> GraphEditorFromTab(
	const TSharedPtr<SDockTab>& Tab,
	UEdGraph* ExpectedGraph)
{
	if (!Tab.IsValid()) return nullptr;
	const TSharedRef<SWidget> Content = Tab->GetContent();
	if (Content->GetTypeAsString() != TEXT("SGraphEditor")) return nullptr;
	const TSharedRef<SGraphEditor> GraphEditor =
		StaticCastSharedRef<SGraphEditor>(Content);
	if (GraphEditor->GetCurrentGraph() != ExpectedGraph) return nullptr;
	return TSharedPtr<SGraphEditor>(GraphEditor);
}

bool IsBetterCandidate(
	const FOpenGraphCandidate& Candidate,
	const FOpenGraphCandidate& Current)
{
	if (Candidate.bActive != Current.bActive)
		return Candidate.bActive;
	if (Candidate.bForeground != Current.bForeground)
		return Candidate.bForeground;
	return Candidate.LastActivationTime > Current.LastActivationTime;
}

bool SameCandidateRank(
	const FOpenGraphCandidate& Left,
	const FOpenGraphCandidate& Right)
{
	return Left.bActive == Right.bActive &&
		Left.bForeground == Right.bForeground &&
		FMath::IsNearlyEqual(
			Left.LastActivationTime,
			Right.LastActivationTime,
			KINDA_SMALL_NUMBER);
}

void AddBestGraphTab(
	TArray<FOpenGraphCandidate>& Candidates,
	UEdGraph* Graph,
	const TSharedPtr<SGraphEditor>& GraphEditor,
	const TSharedPtr<SDockTab>& Tab)
{
	if (Graph == nullptr || !GraphEditor.IsValid() || !Tab.IsValid()) return;
	FOpenGraphCandidate Candidate;
	Candidate.Graph = Graph;
	Candidate.GraphEditor = GraphEditor;
	Candidate.bActive = Tab->IsActive();
	Candidate.bForeground = Tab->IsForeground();
	Candidate.LastActivationTime = Tab->GetLastActivationTime();

	for (FOpenGraphCandidate& Existing : Candidates)
	{
		if (Existing.Graph != Graph) continue;
		if (IsBetterCandidate(Candidate, Existing)) Existing = MoveTemp(Candidate);
		return;
	}
	Candidates.Add(MoveTemp(Candidate));
}

void CollectOpenGraphTabs(
	const TSharedPtr<FBlueprintEditor>& Editor,
	const UBlueprint& Blueprint,
	TArray<FOpenGraphCandidate>& OutCandidates)
{
	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph == nullptr) continue;
		TArray<TSharedPtr<SDockTab>> Tabs;
		if (!Editor->FindOpenTabsContainingDocument(Graph, Tabs)) continue;
		for (const TSharedPtr<SDockTab>& Tab : Tabs)
		{
			AddBestGraphTab(
				OutCandidates,
				Graph,
				GraphEditorFromTab(Tab, Graph),
				Tab);
		}
	}
}

TOptional<FOpenGraphCandidate> ChooseCurrentGraph(
	const TArray<FOpenGraphCandidate>& Candidates)
{
	if (Candidates.IsEmpty()) return {};
	const FOpenGraphCandidate* Best = &Candidates[0];
	for (int32 Index = 1; Index < Candidates.Num(); ++Index)
	{
		const FOpenGraphCandidate& Candidate = Candidates[Index];
		if (IsBetterCandidate(Candidate, *Best)) Best = &Candidate;
	}
	for (const FOpenGraphCandidate& Candidate : Candidates)
	{
		if (&Candidate != Best && SameCandidateRank(Candidate, *Best)) return {};
	}
	return *Best;
}
} // namespace

FM6GraphResolution FM6GraphResolver::Resolve(
	const TSharedPtr<FBlueprintEditor>& BlueprintEditor,
	const FString& GraphPath)
{
	FM6GraphResolution Result;
	if (!BlueprintEditor.IsValid()) return Result;
	UBlueprint* Blueprint = BlueprintEditor->GetBlueprintObj();
	if (Blueprint == nullptr) return Result;

	UEdGraph* FocusedGraph = BlueprintEditor->GetFocusedGraph();
	if (BelongsToBlueprint(*Blueprint, FocusedGraph) &&
		(GraphPath.IsEmpty() || FocusedGraph->GetPathName() == GraphPath))
	{
		Result.Graph = FocusedGraph;
		Result.GraphEditor = SGraphEditor::FindGraphEditorForGraph(FocusedGraph);
		return Result;
	}

	if (!GraphPath.IsEmpty())
	{
		Result.Graph = FindGraphByPath(*Blueprint, GraphPath);
		if (Result.Graph != nullptr)
		{
			Result.GraphEditor =
				SGraphEditor::FindGraphEditorForGraph(Result.Graph);
		}
		return Result;
	}

	TArray<FOpenGraphCandidate> OpenGraphCandidates;
	CollectOpenGraphTabs(BlueprintEditor, *Blueprint, OpenGraphCandidates);
	if (const TOptional<FOpenGraphCandidate> Current =
		ChooseCurrentGraph(OpenGraphCandidates))
	{
		Result.Graph = Current->Graph;
		Result.GraphEditor = Current->GraphEditor;
		return Result;
	}
	if (!OpenGraphCandidates.IsEmpty()) return Result;

	TSet<UEdGraph*> LastEditedGraphs;
	for (const FEditedDocumentInfo& Document : Blueprint->LastEditedDocuments)
	{
		if (UEdGraph* Graph = Cast<UEdGraph>(Document.EditedObjectPath.ResolveObject()))
		{
			if (BelongsToBlueprint(*Blueprint, Graph)) LastEditedGraphs.Add(Graph);
		}
	}
	if (LastEditedGraphs.Num() == 1)
	{
		for (UEdGraph* Graph : LastEditedGraphs)
		{
			Result.Graph = Graph;
			Result.GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph);
		}
	}
	return Result;
}
