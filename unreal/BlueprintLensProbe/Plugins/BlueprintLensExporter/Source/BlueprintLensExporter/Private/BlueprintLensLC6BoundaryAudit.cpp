// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC6BoundaryAudit.h"

#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace BlueprintLensLC6BoundaryAudit
{
	namespace
	{
		FString GuidText(const FGuid& Guid)
		{
			return Guid.IsValid()
				? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
				: FString();
		}

		FString Sha256File(const FString& Path)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				return FString();
			}
			TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		FString NodeId(const FString& GraphId, const UEdGraphNode& Node)
		{
			return BlueprintLensSequenceFacts::MakeNodeId(GraphId, Node);
		}

		FString EdgeId(
			const FString& GraphId,
			const UEdGraphPin& Source,
			const UEdGraphPin& Target)
		{
			const FString SourceNodeId = NodeId(GraphId, *Source.GetOwningNode());
			const FString TargetNodeId = NodeId(GraphId, *Target.GetOwningNode());
			return FString::Printf(
				TEXT("%s::edge::%s->%s"),
				*GraphId,
				*BlueprintLensSequenceFacts::MakePinId(SourceNodeId, Source),
				*BlueprintLensSequenceFacts::MakePinId(TargetNodeId, Target));
		}

		bool PublishLines(const TArray<FString>& Lines, const FString& FinalPath, FString& OutError)
		{
			IFileManager::Get().Delete(*FinalPath, false, true, true);
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true))
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit output directory could not be created");
				return false;
			}
			const FString TemporaryPath = FinalPath + TEXT(".tmp");
			IFileManager::Get().Delete(*TemporaryPath, false, true, true);
			if (!FFileHelper::SaveStringArrayToFile(
				Lines, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				|| !IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, true, false, true))
			{
				IFileManager::Get().Delete(*TemporaryPath, false, true, true);
				IFileManager::Get().Delete(*FinalPath, false, true, true);
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit output could not be published");
				return false;
			}
			return true;
		}
	}

	bool AuditBoundarySource(
		const UBlueprint& Blueprint,
		const BlueprintLensLC6BoundaryFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FBoundaryAuditStats& OutStats,
		FString& OutError)
	{
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.boundary-audit.tsv")));
		OutStats = FBoundaryAuditStats();
		OutError.Reset();
		IFileManager::Get().Delete(*OutFilePath, false, true, true);
		if ((Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
			|| Blueprint.GeneratedClass == nullptr || Blueprint.UbergraphPages.Num() != 1
			|| Anchors.AssetObjectPath != Blueprint.GetPathName() || Anchors.Scenarios.Num() != 4)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit Blueprint/anchor identity is invalid");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(const_cast<UBlueprint*>(&Blueprint));
		if (Graph == nullptr || Graph->GetPathName() != Anchors.GraphId || Graph->Nodes.Num() != 16)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit graph identity or node count is invalid");
			return false;
		}

		TMap<FString, UEdGraphNode*> NodesById;
		TMap<UEdGraphNode*, TSet<UEdGraphNode*>> Adjacency;
		TArray<FString> NodeLines;
		TArray<FString> PinLines;
		TArray<FString> EdgeLines;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node == nullptr || !Node->NodeGuid.IsValid())
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit found an invalid node identity");
				return false;
			}
			const FString Id = NodeId(Graph->GetPathName(), *Node);
			if (NodesById.Contains(Id))
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit found duplicate node identity");
				return false;
			}
			NodesById.Add(Id, Node);
			Adjacency.FindOrAdd(Node);
			FString Detail = TEXT("-");
			if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
			{
				Detail = FString::Printf(TEXT("event=%s"), *Event->CustomFunctionName.ToString());
			}
			else if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
			{
				Detail = FString::Printf(
					TEXT("member=%s;guid=%s"),
					*Variable->GetVarName().ToString(),
					*GuidText(Variable->VariableReference.GetMemberGuid()));
			}
			else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				const UFunction* Function = Call->GetTargetFunction();
				Detail = FString::Printf(
					TEXT("function=%s;owner=%s;latent=%d"),
					Function == nullptr ? TEXT("") : *Function->GetName(),
					Function == nullptr || Function->GetOwnerClass() == nullptr
						? TEXT("") : *Function->GetOwnerClass()->GetPathName(),
					Function != nullptr && Function->HasMetaData(TEXT("Latent")) ? 1 : 0);
			}
			NodeLines.Add(FString::Printf(
				TEXT("NODE\t%s\t%s\t%s\t%s"),
				*Id, *GuidText(Node->NodeGuid), *Node->GetClass()->GetPathName(), *Detail));
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin == nullptr)
				{
					continue;
				}
				PinLines.Add(FString::Printf(
					TEXT("PIN\t%s\t%s\t%s\t%s\t%s"),
					*BlueprintLensSequenceFacts::MakePinId(Id, *Pin),
					*Id,
					*Pin->PinName.ToString(),
					Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"),
					*Pin->PinType.PinCategory.ToString()));
				if (Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* Target = LinkedPin == nullptr ? nullptr : LinkedPin->GetOwningNode();
					if (LinkedPin == nullptr || Target == nullptr)
					{
						OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit found a dangling link");
						return false;
					}
					Adjacency.FindOrAdd(Node).Add(Target);
					Adjacency.FindOrAdd(Target).Add(Node);
					EdgeLines.Add(FString::Printf(
						TEXT("EDGE\t%s\t%s\t%s\t%s"),
						*EdgeId(Graph->GetPathName(), *Pin, *LinkedPin),
						*Id,
						*NodeId(Graph->GetPathName(), *Target),
						Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data")));
				}
			}
		}
		OutStats.NodeCount = NodesById.Num();
		OutStats.EdgeCount = EdgeLines.Num();
		if (OutStats.NodeCount != 16 || OutStats.EdgeCount != 12)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit totals differ from contract");
			return false;
		}

		TArray<TSet<UEdGraphNode*>> Components;
		TSet<UEdGraphNode*> Visited;
		for (const TPair<UEdGraphNode*, TSet<UEdGraphNode*>>& Entry : Adjacency)
		{
			if (Visited.Contains(Entry.Key))
			{
				continue;
			}
			TSet<UEdGraphNode*> Component;
			TArray<UEdGraphNode*> Pending = {Entry.Key};
			while (!Pending.IsEmpty())
			{
				UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);
				if (Current == nullptr || Visited.Contains(Current))
				{
					continue;
				}
				Visited.Add(Current);
				Component.Add(Current);
				for (UEdGraphNode* Other : Adjacency.FindChecked(Current))
				{
					Pending.Add(Other);
				}
			}
			Components.Add(MoveTemp(Component));
		}
		if (Components.Num() != 4)
		{
			OutError = TEXT("LC6_COMPONENT_ISOLATION_INVALID: audit found cross-component ownership");
			return false;
		}

		const TMap<FString, int32> ExpectedNodeCounts = {
			{TEXT("LC6_OPAQUE"), 3}, {TEXT("LC6_UNCERTAIN"), 3},
			{TEXT("LC6_UNSUPPORTED"), 3}, {TEXT("LC6_TRUNCATED"), 7}};
		const TMap<FString, int32> ExpectedEdgeCounts = {
			{TEXT("LC6_OPAQUE"), 2}, {TEXT("LC6_UNCERTAIN"), 2},
			{TEXT("LC6_UNSUPPORTED"), 2}, {TEXT("LC6_TRUNCATED"), 6}};
		TSet<FString> ScenarioIds;
		TSet<FString> RootIds;
		TSet<FString> CriterionIds;
		TSet<int32> ComponentIndices;
		TArray<FString> ScenarioLines;
		for (const BlueprintLensLC6BoundaryFixture::FScenarioAnchors& Anchor : Anchors.Scenarios)
		{
			UEdGraphNode* Root = NodesById.FindRef(Anchor.RootNodeId);
			UEdGraphNode* Criterion = NodesById.FindRef(Anchor.CriterionNodeId);
			const int32* NodeExpectation = ExpectedNodeCounts.Find(Anchor.ScenarioId);
			const int32* EdgeExpectation = ExpectedEdgeCounts.Find(Anchor.ScenarioId);
			if (Root == nullptr || Criterion == nullptr || NodeExpectation == nullptr || EdgeExpectation == nullptr
				|| !Root->IsA<UK2Node_CustomEvent>() || !Criterion->IsA<UK2Node_VariableSet>())
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit anchors do not resolve uniquely");
				return false;
			}
			int32 ComponentIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Components.Num(); ++Index)
			{
				if (Components[Index].Contains(Root) && Components[Index].Contains(Criterion))
				{
					ComponentIndex = Index;
					break;
				}
			}
			int32 ComponentEdges = 0;
			if (ComponentIndex != INDEX_NONE)
			{
				for (UEdGraphNode* Node : Components[ComponentIndex])
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin != nullptr && Pin->Direction == EGPD_Output)
						{
							for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
							{
								ComponentEdges += LinkedPin != nullptr
									&& Components[ComponentIndex].Contains(LinkedPin->GetOwningNode()) ? 1 : 0;
							}
						}
					}
				}
			}
			if (ComponentIndex == INDEX_NONE || Components[ComponentIndex].Num() != *NodeExpectation
				|| ComponentEdges != *EdgeExpectation)
			{
				OutError = TEXT("LC6_COMPONENT_ISOLATION_INVALID: audit component shape differs from contract");
				return false;
			}
			ScenarioIds.Add(Anchor.ScenarioId);
			RootIds.Add(Anchor.RootNodeId);
			CriterionIds.Add(Anchor.CriterionNodeId);
			ComponentIndices.Add(ComponentIndex);
			ScenarioLines.Add(FString::Printf(
				TEXT("SCENARIO\t%s\t%s\t%s\t%d\t%d"),
				*Anchor.ScenarioId, *Anchor.RootNodeId, *Anchor.CriterionNodeId,
				*NodeExpectation, *EdgeExpectation));
		}
		OutStats.ScenarioCount = ScenarioLines.Num();
		if (OutStats.ScenarioCount != 4 || ScenarioIds.Num() != 4 || RootIds.Num() != 4
			|| CriterionIds.Num() != 4 || ComponentIndices.Num() != 4)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit scenario/root/criterion identities are not unique");
			return false;
		}

		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		const FString AssetHash = Sha256File(AssetPath);
		const FString RawHash = Sha256File(RawExportPath);
		if (AssetHash.IsEmpty() || RawHash.IsEmpty())
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: audit provenance could not be hashed");
			return false;
		}
		Algo::Sort(ScenarioLines);
		Algo::Sort(NodeLines);
		Algo::Sort(PinLines);
		Algo::Sort(EdgeLines);
		TArray<FString> Lines;
		Lines.Add(TEXT("FORMAT\tblueprint-lens-lc6-boundary-audit\t1.0.0"));
		Lines.Add(FString::Printf(TEXT("BLUEPRINT\t%s\t%s"), *Blueprint.GetPathName(), *Graph->GetPathName()));
		Lines.Add(FString::Printf(
			TEXT("COMPILE\tup_to_date\t%s\t%s\t%s\t%s"),
			*GuidText(Blueprint.GetOutermost()->GetPersistentGuid()),
			*Blueprint.GeneratedClass->GetPathName(), *AssetHash, *RawHash));
		Lines.Append(ScenarioLines);
		Lines.Append(NodeLines);
		Lines.Append(PinLines);
		Lines.Append(EdgeLines);
		Lines.Add(FString::Printf(
			TEXT("COUNTS\t%d\t%d\t%d"),
			OutStats.ScenarioCount, OutStats.NodeCount, OutStats.EdgeCount));
		return PublishLines(Lines, OutFilePath, OutError);
	}
}
