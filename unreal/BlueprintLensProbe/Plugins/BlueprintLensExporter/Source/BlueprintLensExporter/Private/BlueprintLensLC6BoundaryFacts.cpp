// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC6BoundaryFacts.h"

#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
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
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace BlueprintLensLC6BoundaryFacts
{
	namespace
	{
		struct FScenarioSource
		{
			FString ScenarioId;
			FString RootNodeId;
			FString CriterionNodeId;
			TArray<UEdGraphNode*> Nodes;
		};

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

		TArray<TSet<UEdGraphNode*>> ConnectedComponents(UEdGraph& Graph)
		{
			TMap<UEdGraphNode*, TSet<UEdGraphNode*>> Adjacency;
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				Adjacency.FindOrAdd(Node);
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr)
					{
						continue;
					}
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* Other = LinkedPin == nullptr ? nullptr : LinkedPin->GetOwningNode();
						if (Other != nullptr && Other != Node)
						{
							Adjacency.FindOrAdd(Node).Add(Other);
							Adjacency.FindOrAdd(Other).Add(Node);
						}
					}
				}
			}
			TArray<TSet<UEdGraphNode*>> Result;
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
				Result.Add(MoveTemp(Component));
			}
			return Result;
		}

		bool ResolveScenarios(
			UEdGraph& Graph,
			const BlueprintLensLC6BoundaryFixture::FFixtureAnchors& Anchors,
			TArray<FScenarioSource>& OutScenarios,
			FString& OutError)
		{
			if (Anchors.AssetObjectPath.IsEmpty() || Anchors.GraphId != Graph.GetPathName()
				|| Anchors.Scenarios.Num() != 4 || Graph.Nodes.Num() != 16)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source anchors or graph shape are invalid");
				return false;
			}
			const TMap<FString, int32> ExpectedNodes = {
				{TEXT("LC6_OPAQUE"), 3}, {TEXT("LC6_UNCERTAIN"), 3},
				{TEXT("LC6_UNSUPPORTED"), 3}, {TEXT("LC6_TRUNCATED"), 7}};
			const TMap<FString, int32> ExpectedEdges = {
				{TEXT("LC6_OPAQUE"), 2}, {TEXT("LC6_UNCERTAIN"), 2},
				{TEXT("LC6_UNSUPPORTED"), 2}, {TEXT("LC6_TRUNCATED"), 6}};
			const TArray<TSet<UEdGraphNode*>> Components = ConnectedComponents(Graph);
			if (Components.Num() != 4)
			{
				OutError = TEXT("LC6_COMPONENT_ISOLATION_INVALID: source graph is not four isolated components");
				return false;
			}

			TMap<FString, UEdGraphNode*> NodesById;
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr || !Node->NodeGuid.IsValid())
				{
					OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source node identity is invalid");
					return false;
				}
				const FString Id = NodeId(Graph.GetPathName(), *Node);
				if (NodesById.Contains(Id))
				{
					OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: duplicate source node identity");
					return false;
				}
				NodesById.Add(Id, Node);
			}
			TSet<FString> ScenarioIds;
			TSet<FString> RootIds;
			TSet<FString> CriterionIds;
			TSet<int32> ComponentIndices;
			for (const BlueprintLensLC6BoundaryFixture::FScenarioAnchors& Anchor : Anchors.Scenarios)
			{
				UEdGraphNode* Root = NodesById.FindRef(Anchor.RootNodeId);
				UEdGraphNode* Criterion = NodesById.FindRef(Anchor.CriterionNodeId);
				const int32* NodeExpectation = ExpectedNodes.Find(Anchor.ScenarioId);
				const int32* EdgeExpectation = ExpectedEdges.Find(Anchor.ScenarioId);
				if (Root == nullptr || Criterion == nullptr || NodeExpectation == nullptr || EdgeExpectation == nullptr
					|| !Root->IsA<UK2Node_CustomEvent>() || !Criterion->IsA<UK2Node_VariableSet>())
				{
					OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: scenario anchors do not resolve to expected node families");
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
				int32 EdgeCount = 0;
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
									EdgeCount += LinkedPin != nullptr
										&& Components[ComponentIndex].Contains(LinkedPin->GetOwningNode()) ? 1 : 0;
								}
							}
						}
					}
				}
				if (ComponentIndex == INDEX_NONE || Components[ComponentIndex].Num() != *NodeExpectation
					|| EdgeCount != *EdgeExpectation)
				{
					OutError = TEXT("LC6_COMPONENT_ISOLATION_INVALID: scenario component shape differs from contract");
					return false;
				}
				ScenarioIds.Add(Anchor.ScenarioId);
				RootIds.Add(Anchor.RootNodeId);
				CriterionIds.Add(Anchor.CriterionNodeId);
				ComponentIndices.Add(ComponentIndex);
				FScenarioSource Scenario;
				Scenario.ScenarioId = Anchor.ScenarioId;
				Scenario.RootNodeId = Anchor.RootNodeId;
				Scenario.CriterionNodeId = Anchor.CriterionNodeId;
				Scenario.Nodes = Components[ComponentIndex].Array();
				Algo::Sort(Scenario.Nodes, [&Graph](const UEdGraphNode* Left, const UEdGraphNode* Right)
				{
					return NodeId(Graph.GetPathName(), *Left) < NodeId(Graph.GetPathName(), *Right);
				});
				OutScenarios.Add(MoveTemp(Scenario));
			}
			if (ScenarioIds.Num() != 4 || RootIds.Num() != 4 || CriterionIds.Num() != 4
				|| ComponentIndices.Num() != 4 || OutScenarios.Num() != 4)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: scenario/root/criterion identities are not unique");
				return false;
			}
			Algo::Sort(OutScenarios, [](const FScenarioSource& Left, const FScenarioSource& Right)
			{
				return Left.ScenarioId < Right.ScenarioId;
			});
			return true;
		}

		TSharedPtr<FJsonObject> SerializeNode(const FString& GraphId, const UEdGraphNode& Node)
		{
			const FString Id = NodeId(GraphId, Node);
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Id);
			Json->SetStringField(TEXT("native_guid"), GuidText(Node.NodeGuid));
			Json->SetStringField(TEXT("class"), Node.GetClass()->GetPathName());
			if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(&Node))
			{
				Json->SetStringField(TEXT("event_name"), Event->CustomFunctionName.ToString());
			}
			if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(&Node))
			{
				TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
				Member->SetStringField(TEXT("name"), Variable->GetVarName().ToString());
				Member->SetStringField(TEXT("guid"), GuidText(Variable->VariableReference.GetMemberGuid()));
				Json->SetObjectField(TEXT("member"), Member);
			}
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node))
			{
				const UFunction* Function = Call->GetTargetFunction();
				TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
				FunctionJson->SetStringField(TEXT("name"), Function == nullptr ? FString() : Function->GetName());
				FunctionJson->SetStringField(
					TEXT("owner_class"),
					Function == nullptr || Function->GetOwnerClass() == nullptr
						? FString() : Function->GetOwnerClass()->GetPathName());
				FunctionJson->SetBoolField(TEXT("is_latent"), Function != nullptr && Function->HasMetaData(TEXT("Latent")));
				Json->SetObjectField(TEXT("function"), FunctionJson);
			}
			TArray<TSharedPtr<FJsonValue>> Pins;
			TArray<const UEdGraphPin*> SortedPins;
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin != nullptr)
				{
					SortedPins.Add(Pin);
				}
			}
			Algo::Sort(SortedPins, [&Id](const UEdGraphPin* Left, const UEdGraphPin* Right)
			{
				return BlueprintLensSequenceFacts::MakePinId(Id, *Left)
					< BlueprintLensSequenceFacts::MakePinId(Id, *Right);
			});
			for (const UEdGraphPin* Pin : SortedPins)
			{
				TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
				PinJson->SetStringField(TEXT("id"), BlueprintLensSequenceFacts::MakePinId(Id, *Pin));
				PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
				PinJson->SetStringField(TEXT("kind"), Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
				PinJson->SetStringField(TEXT("type_category"), Pin->PinType.PinCategory.ToString());
				Pins.Add(MakeShared<FJsonValueObject>(PinJson));
			}
			Json->SetArrayField(TEXT("pins"), Pins);
			return Json;
		}

		bool Publish(const FString& Text, const FString& FinalPath, FString& OutError)
		{
			IFileManager::Get().Delete(*FinalPath, false, true, true);
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true))
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source output directory could not be created");
				return false;
			}
			const FString TemporaryPath = FinalPath + TEXT(".tmp");
			IFileManager::Get().Delete(*TemporaryPath, false, true, true);
			if (!FFileHelper::SaveStringToFile(Text, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				|| !IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, true, false, true))
			{
				IFileManager::Get().Delete(*TemporaryPath, false, true, true);
				IFileManager::Get().Delete(*FinalPath, false, true, true);
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source output could not be published");
				return false;
			}
			return true;
		}
	}

	bool ExportBoundaryFacts(
		const UBlueprint& Blueprint,
		const BlueprintLensLC6BoundaryFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FBoundaryFactStats& OutStats,
		FString& OutError)
	{
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.boundary-source.json")));
		OutStats = FBoundaryFactStats();
		OutError.Reset();
		IFileManager::Get().Delete(*OutFilePath, false, true, true);
		if ((Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
			|| Blueprint.GeneratedClass == nullptr || Anchors.AssetObjectPath != Blueprint.GetPathName()
			|| Blueprint.UbergraphPages.Num() != 1)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source Blueprint compile/asset identity is invalid");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(const_cast<UBlueprint*>(&Blueprint));
		TArray<FScenarioSource> Scenarios;
		if (Graph == nullptr || !ResolveScenarios(*Graph, Anchors, Scenarios, OutError))
		{
			return false;
		}
		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		const FString AssetHash = Sha256File(AssetPath);
		const FString RawHash = Sha256File(RawExportPath);
		if (AssetHash.IsEmpty() || RawHash.IsEmpty())
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source provenance could not be hashed");
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> ScenarioValues;
		for (const FScenarioSource& Scenario : Scenarios)
		{
			TSharedPtr<FJsonObject> ScenarioJson = MakeShared<FJsonObject>();
			ScenarioJson->SetStringField(TEXT("scenario_id"), Scenario.ScenarioId);
			ScenarioJson->SetStringField(TEXT("root_node_id"), Scenario.RootNodeId);
			ScenarioJson->SetStringField(TEXT("criterion_node_id"), Scenario.CriterionNodeId);
			TArray<TSharedPtr<FJsonValue>> NodeValues;
			TSet<UEdGraphNode*> ScenarioNodeSet;
			for (UEdGraphNode* Node : Scenario.Nodes)
			{
				ScenarioNodeSet.Add(Node);
				NodeValues.Add(MakeShared<FJsonValueObject>(SerializeNode(Graph->GetPathName(), *Node)));
				++OutStats.NodeCount;
			}
			ScenarioJson->SetArrayField(TEXT("nodes"), NodeValues);
			TArray<TSharedPtr<FJsonObject>> Edges;
			for (UEdGraphNode* Node : Scenario.Nodes)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output)
					{
						continue;
					}
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* Target = LinkedPin == nullptr ? nullptr : LinkedPin->GetOwningNode();
						if (LinkedPin == nullptr || Target == nullptr || !ScenarioNodeSet.Contains(Target))
						{
							OutError = TEXT("LC6_COMPONENT_ISOLATION_INVALID: source edge crosses scenario ownership");
							return false;
						}
						const FString SourceNodeId = NodeId(Graph->GetPathName(), *Node);
						const FString TargetNodeId = NodeId(Graph->GetPathName(), *Target);
						TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
						Edge->SetStringField(TEXT("id"), EdgeId(Graph->GetPathName(), *Pin, *LinkedPin));
						Edge->SetStringField(TEXT("source_node_id"), SourceNodeId);
						Edge->SetStringField(TEXT("source_pin_id"), BlueprintLensSequenceFacts::MakePinId(SourceNodeId, *Pin));
						Edge->SetStringField(TEXT("target_node_id"), TargetNodeId);
						Edge->SetStringField(TEXT("target_pin_id"), BlueprintLensSequenceFacts::MakePinId(TargetNodeId, *LinkedPin));
						Edge->SetStringField(TEXT("kind"), Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
						Edges.Add(Edge);
					}
				}
			}
			Algo::Sort(Edges, [](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
			{
				return Left->GetStringField(TEXT("id")) < Right->GetStringField(TEXT("id"));
			});
			TArray<TSharedPtr<FJsonValue>> EdgeValues;
			for (const TSharedPtr<FJsonObject>& Edge : Edges)
			{
				EdgeValues.Add(MakeShared<FJsonValueObject>(Edge));
				++OutStats.EdgeCount;
			}
			ScenarioJson->SetArrayField(TEXT("edges"), EdgeValues);
			ScenarioValues.Add(MakeShared<FJsonValueObject>(ScenarioJson));
			++OutStats.ScenarioCount;
		}
		if (OutStats.ScenarioCount != 4 || OutStats.NodeCount != 16 || OutStats.EdgeCount != 12)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: source totals differ from contract");
			return false;
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-lc6-boundary-source"));
		Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("blueprint_asset_path"), Blueprint.GetPathName());
		Root->SetStringField(TEXT("graph_id"), Graph->GetPathName());
		Root->SetStringField(TEXT("asset_sha256"), AssetHash);
		Root->SetStringField(TEXT("raw_sha256"), RawHash);
		TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
		Compile->SetStringField(TEXT("status"), TEXT("up_to_date"));
		Compile->SetStringField(TEXT("package_guid"), GuidText(Blueprint.GetOutermost()->GetPersistentGuid()));
		Compile->SetStringField(TEXT("generated_class_path"), Blueprint.GeneratedClass->GetPathName());
		Root->SetObjectField(TEXT("compile_provenance"), Compile);
		Root->SetArrayField(TEXT("scenarios"), ScenarioValues);
		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("scenarios"), OutStats.ScenarioCount);
		Counts->SetNumberField(TEXT("nodes"), OutStats.NodeCount);
		Counts->SetNumberField(TEXT("edges"), OutStats.EdgeCount);
		Root->SetObjectField(TEXT("counts"), Counts);
		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) || !Publish(JsonText, OutFilePath, OutError))
		{
			return false;
		}
		return true;
	}
}
