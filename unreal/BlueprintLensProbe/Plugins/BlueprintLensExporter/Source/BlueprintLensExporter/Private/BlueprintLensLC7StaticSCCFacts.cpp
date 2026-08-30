// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC7StaticSCCFacts.h"

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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace BlueprintLensLC7StaticSCCFacts
{
	namespace
	{
		struct FSourceEdge
		{
			FString Id;
			FString SourceNodeId;
			FString SourcePinId;
			FString TargetNodeId;
			FString TargetPinId;
			FString Kind;
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

		bool Publish(const FString& Text, const FString& FinalPath, FString& OutError)
		{
			IFileManager::Get().Delete(*FinalPath, false, true, true);
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source output directory could not be created");
				return false;
			}
			const FString TemporaryPath = FinalPath + TEXT(".tmp");
			IFileManager::Get().Delete(*TemporaryPath, false, true, true);
			if (!FFileHelper::SaveStringToFile(
				Text,
				*TemporaryPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				|| !IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, true, false, true))
			{
				IFileManager::Get().Delete(*TemporaryPath, false, true, true);
				IFileManager::Get().Delete(*FinalPath, false, true, true);
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source output could not be published");
				return false;
			}
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> StringValues(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> Result;
			for (const FString& Value : Values)
			{
				Result.Add(MakeShared<FJsonValueString>(Value));
			}
			return Result;
		}

		TSharedPtr<FJsonObject> SerializeNode(
			const FString& GraphId,
			const UEdGraphNode& Node)
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
				Member->SetStringField(
					TEXT("guid"),
					GuidText(Variable->VariableReference.GetMemberGuid()));
				Json->SetObjectField(TEXT("member"), Member);
			}
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node))
			{
				const UFunction* Function = Call->GetTargetFunction();
				TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
				FunctionJson->SetStringField(
					TEXT("name"),
					Function == nullptr ? FString() : Function->GetName());
				FunctionJson->SetStringField(
					TEXT("owner_class"),
					Function == nullptr || Function->GetOwnerClass() == nullptr
						? FString()
						: Function->GetOwnerClass()->GetPathName());
				Json->SetObjectField(TEXT("function"), FunctionJson);
			}
			TArray<const UEdGraphPin*> Pins;
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin != nullptr)
				{
					Pins.Add(Pin);
				}
			}
			Algo::Sort(Pins, [&Id](const UEdGraphPin* Left, const UEdGraphPin* Right)
			{
				return BlueprintLensSequenceFacts::MakePinId(Id, *Left)
					< BlueprintLensSequenceFacts::MakePinId(Id, *Right);
			});
			TArray<TSharedPtr<FJsonValue>> PinValues;
			for (const UEdGraphPin* Pin : Pins)
			{
				TSharedPtr<FJsonObject> PinJson = MakeShared<FJsonObject>();
				PinJson->SetStringField(
					TEXT("id"),
					BlueprintLensSequenceFacts::MakePinId(Id, *Pin));
				PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinJson->SetStringField(
					TEXT("direction"),
					Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
				PinJson->SetStringField(
					TEXT("kind"),
					Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
				PinJson->SetStringField(TEXT("type_category"), Pin->PinType.PinCategory.ToString());
				PinValues.Add(MakeShared<FJsonValueObject>(PinJson));
			}
			Json->SetArrayField(TEXT("pins"), PinValues);
			return Json;
		}

		bool CollectGraph(
			UEdGraph& Graph,
			TMap<FString, UEdGraphNode*>& OutNodes,
			TArray<FSourceEdge>& OutEdges,
			FString& OutError)
		{
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr || !Node->NodeGuid.IsValid())
				{
					OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source node identity is invalid");
					return false;
				}
				const FString Id = NodeId(Graph.GetPathName(), *Node);
				if (OutNodes.Contains(Id))
				{
					OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: duplicate source node identity");
					return false;
				}
				OutNodes.Add(Id, Node);
			}
			for (const TPair<FString, UEdGraphNode*>& Pair : OutNodes)
			{
				for (UEdGraphPin* Pin : Pair.Value->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output)
					{
						continue;
					}
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* Target = LinkedPin == nullptr ? nullptr : LinkedPin->GetOwningNode();
						if (LinkedPin == nullptr || Target == nullptr)
						{
							OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source contains a dangling edge");
							return false;
						}
						const FString TargetId = NodeId(Graph.GetPathName(), *Target);
						if (!OutNodes.Contains(TargetId))
						{
							OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source edge leaves the fixture graph");
							return false;
						}
						FSourceEdge Edge;
						Edge.Id = EdgeId(Graph.GetPathName(), *Pin, *LinkedPin);
						Edge.SourceNodeId = Pair.Key;
						Edge.SourcePinId = BlueprintLensSequenceFacts::MakePinId(Pair.Key, *Pin);
						Edge.TargetNodeId = TargetId;
						Edge.TargetPinId = BlueprintLensSequenceFacts::MakePinId(TargetId, *LinkedPin);
						Edge.Kind = Pin->PinType.PinCategory == TEXT("exec")
							? TEXT("execution")
							: TEXT("data");
						OutEdges.Add(MoveTemp(Edge));
					}
				}
			}
			Algo::Sort(OutEdges, [](const FSourceEdge& Left, const FSourceEdge& Right)
			{
				return Left.Id < Right.Id;
			});
			TSet<FString> EdgeIds;
			for (const FSourceEdge& Edge : OutEdges)
			{
				EdgeIds.Add(Edge.Id);
			}
			if (OutNodes.Num() != 10 || OutEdges.Num() != 10 || EdgeIds.Num() != 10)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source totals differ from nodes=10 edges=10");
				return false;
			}
			return true;
		}

		bool FindSCC(
			const TMap<FString, UEdGraphNode*>& Nodes,
			const TArray<FSourceEdge>& Edges,
			TArray<FString>& OutMembers,
			FString& OutError)
		{
			TMap<FString, TArray<FString>> Adjacency;
			for (const TPair<FString, UEdGraphNode*>& Pair : Nodes)
			{
				Adjacency.Add(Pair.Key, {});
			}
			for (const FSourceEdge& Edge : Edges)
			{
				if (Edge.Kind == TEXT("execution"))
				{
					Adjacency.FindChecked(Edge.SourceNodeId).Add(Edge.TargetNodeId);
				}
			}
			for (TPair<FString, TArray<FString>>& Pair : Adjacency)
			{
				Pair.Value.Sort();
			}

			TMap<FString, int32> Indices;
			TMap<FString, int32> LowLinks;
			TArray<FString> Stack;
			TSet<FString> OnStack;
			TArray<TArray<FString>> Components;
			int32 NextIndex = 0;
			TFunction<void(const FString&)> StrongConnect;
			StrongConnect = [&](const FString& Current)
			{
				Indices.Add(Current, NextIndex);
				LowLinks.Add(Current, NextIndex);
				++NextIndex;
				Stack.Add(Current);
				OnStack.Add(Current);
				for (const FString& Target : Adjacency.FindChecked(Current))
				{
					if (!Indices.Contains(Target))
					{
						StrongConnect(Target);
						LowLinks.FindChecked(Current) = FMath::Min(
							LowLinks.FindChecked(Current),
							LowLinks.FindChecked(Target));
					}
					else if (OnStack.Contains(Target))
					{
						LowLinks.FindChecked(Current) = FMath::Min(
							LowLinks.FindChecked(Current),
							Indices.FindChecked(Target));
					}
				}
				if (LowLinks.FindChecked(Current) == Indices.FindChecked(Current))
				{
					TArray<FString> Component;
					FString Popped;
					do
					{
						Popped = Stack.Pop(EAllowShrinking::No);
						OnStack.Remove(Popped);
						Component.Add(Popped);
					} while (Popped != Current);
					Component.Sort();
					Components.Add(MoveTemp(Component));
				}
			};

			TArray<FString> NodeIds;
			Nodes.GenerateKeyArray(NodeIds);
			NodeIds.Sort();
			for (const FString& Id : NodeIds)
			{
				if (!Indices.Contains(Id))
				{
					StrongConnect(Id);
				}
			}
			TArray<TArray<FString>> NonTrivial;
			for (const TArray<FString>& Component : Components)
			{
				if (Component.Num() > 1)
				{
					NonTrivial.Add(Component);
				}
			}
			if (NonTrivial.Num() != 1 || NonTrivial[0].Num() != 3)
			{
				OutError = TEXT("LC7_SCC_MEMBERSHIP_INVALID: expected one nontrivial three-member SCC");
				return false;
			}
			OutMembers = NonTrivial[0];
			return true;
		}
	}

	bool ExportSCCFacts(
		const UBlueprint& Blueprint,
		const BlueprintLensLC7StaticSCCFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FSCCFactStats& OutStats,
		FString& OutError)
	{
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory, TEXT("BP_LC7_StaticSCC.scc-source.json")));
		OutStats = FSCCFactStats();
		OutError.Reset();
		IFileManager::Get().Delete(*OutFilePath, false, true, true);
		if ((Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
			|| Blueprint.GeneratedClass == nullptr || Blueprint.UbergraphPages.Num() != 1
			|| Anchors.AssetObjectPath != Blueprint.GetPathName())
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source Blueprint compile/asset identity is invalid");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(const_cast<UBlueprint*>(&Blueprint));
		if (Graph == nullptr || Graph->GetPathName() != Anchors.GraphId)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source graph identity is invalid");
			return false;
		}

		TMap<FString, UEdGraphNode*> Nodes;
		TArray<FSourceEdge> Edges;
		if (!CollectGraph(*Graph, Nodes, Edges, OutError))
		{
			return false;
		}
		for (const FString& RequiredId : {
			Anchors.EventNodeId,
			Anchors.InitialiseNodeId,
			Anchors.BranchNodeId,
			Anchors.BodyNodeId,
			Anchors.AdvanceNodeId,
			Anchors.CriterionNodeId})
		{
			if (!Nodes.Contains(RequiredId))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source anchor does not resolve");
				return false;
			}
		}

		TArray<FString> Members;
		if (!FindSCC(Nodes, Edges, Members, OutError))
		{
			return false;
		}
		const TSet<FString> MemberSet(Members);
		const TSet<FString> ExpectedMembers = {
			Anchors.BranchNodeId, Anchors.BodyNodeId, Anchors.AdvanceNodeId};
		if (MemberSet.Num() != ExpectedMembers.Num()
			|| !MemberSet.Includes(ExpectedMembers)
			|| MemberSet.Contains(Anchors.CriterionNodeId))
		{
			OutError = TEXT("LC7_SCC_MEMBERSHIP_INVALID: discovered SCC differs from anchored Branch/Body/Advance");
			return false;
		}

		TArray<FString> InternalEdges;
		TArray<FString> IncomingEdges;
		TArray<FString> OutgoingEdges;
		TArray<FString> ReturningEdges;
		FString EntryNodeId;
		FString ExitNodeId;
		for (const FSourceEdge& Edge : Edges)
		{
			if (Edge.Kind != TEXT("execution"))
			{
				continue;
			}
			const bool bSourceMember = MemberSet.Contains(Edge.SourceNodeId);
			const bool bTargetMember = MemberSet.Contains(Edge.TargetNodeId);
			if (bSourceMember && bTargetMember)
			{
				InternalEdges.Add(Edge.Id);
				if (Edge.SourceNodeId == Anchors.AdvanceNodeId
					&& Edge.TargetNodeId == Anchors.BranchNodeId)
				{
					ReturningEdges.Add(Edge.Id);
				}
			}
			else if (!bSourceMember && bTargetMember)
			{
				IncomingEdges.Add(Edge.Id);
				EntryNodeId = Edge.TargetNodeId;
			}
			else if (bSourceMember && !bTargetMember)
			{
				OutgoingEdges.Add(Edge.Id);
				ExitNodeId = Edge.SourceNodeId;
			}
		}
		InternalEdges.Sort();
		IncomingEdges.Sort();
		OutgoingEdges.Sort();
		ReturningEdges.Sort();
		if (InternalEdges.Num() != 3 || ReturningEdges.Num() != 1)
		{
			OutError = TEXT("LC7_SCC_EDGE_OWNERSHIP_INVALID: internal/returning edge inventory differs from 3/1");
			return false;
		}
		if (IncomingEdges.Num() != 1 || OutgoingEdges.Num() != 1
			|| EntryNodeId != Anchors.BranchNodeId || ExitNodeId != Anchors.BranchNodeId)
		{
			OutError = TEXT("LC7_SCC_BOUNDARY_INVALID: expected one Branch entry and one Branch exit");
			return false;
		}

		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		const FString AssetHash = Sha256File(AssetPath);
		const FString RawHash = Sha256File(RawExportPath);
		if (AssetHash.IsEmpty() || RawHash.IsEmpty())
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: source provenance could not be hashed");
			return false;
		}

		TArray<FString> NodeIds;
		Nodes.GenerateKeyArray(NodeIds);
		NodeIds.Sort();
		TArray<TSharedPtr<FJsonValue>> NodeValues;
		for (const FString& Id : NodeIds)
		{
			NodeValues.Add(MakeShared<FJsonValueObject>(SerializeNode(Graph->GetPathName(), *Nodes.FindChecked(Id))));
		}
		TArray<TSharedPtr<FJsonValue>> EdgeValues;
		for (const FSourceEdge& Edge : Edges)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Edge.Id);
			Json->SetStringField(TEXT("source_node_id"), Edge.SourceNodeId);
			Json->SetStringField(TEXT("source_pin_id"), Edge.SourcePinId);
			Json->SetStringField(TEXT("target_node_id"), Edge.TargetNodeId);
			Json->SetStringField(TEXT("target_pin_id"), Edge.TargetPinId);
			Json->SetStringField(TEXT("kind"), Edge.Kind);
			EdgeValues.Add(MakeShared<FJsonValueObject>(Json));
		}

		TSharedPtr<FJsonObject> SCC = MakeShared<FJsonObject>();
		SCC->SetArrayField(TEXT("member_node_ids"), StringValues(Members));
		SCC->SetArrayField(TEXT("internal_edge_ids"), StringValues(InternalEdges));
		SCC->SetArrayField(TEXT("incoming_edge_ids"), StringValues(IncomingEdges));
		SCC->SetArrayField(TEXT("outgoing_edge_ids"), StringValues(OutgoingEdges));
		SCC->SetArrayField(TEXT("returning_edge_ids"), StringValues(ReturningEdges));
		SCC->SetStringField(TEXT("entry_node_id"), EntryNodeId);
		SCC->SetStringField(TEXT("exit_node_id"), ExitNodeId);

		OutStats.NodeCount = Nodes.Num();
		OutStats.EdgeCount = Edges.Num();
		OutStats.MemberCount = Members.Num();
		OutStats.InternalEdgeCount = InternalEdges.Num();
		OutStats.IncomingEdgeCount = IncomingEdges.Num();
		OutStats.OutgoingEdgeCount = OutgoingEdges.Num();
		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("nodes"), OutStats.NodeCount);
		Counts->SetNumberField(TEXT("edges"), OutStats.EdgeCount);
		Counts->SetNumberField(TEXT("scc_members"), OutStats.MemberCount);
		Counts->SetNumberField(TEXT("internal_edges"), OutStats.InternalEdgeCount);
		Counts->SetNumberField(TEXT("incoming_edges"), OutStats.IncomingEdgeCount);
		Counts->SetNumberField(TEXT("outgoing_edges"), OutStats.OutgoingEdgeCount);

		TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
		Compile->SetStringField(TEXT("status"), TEXT("up_to_date"));
		Compile->SetStringField(
			TEXT("package_guid"),
			GuidText(Blueprint.GetOutermost()->GetPersistentGuid()));
		Compile->SetStringField(TEXT("generated_class_path"), Blueprint.GeneratedClass->GetPathName());

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-lc7-static-scc-source"));
		Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("blueprint_asset_path"), Blueprint.GetPathName());
		Root->SetStringField(TEXT("graph_id"), Graph->GetPathName());
		Root->SetStringField(TEXT("asset_sha256"), AssetHash);
		Root->SetStringField(TEXT("raw_sha256"), RawHash);
		Root->SetStringField(TEXT("criterion_node_id"), Anchors.CriterionNodeId);
		Root->SetObjectField(TEXT("compile_provenance"), Compile);
		Root->SetArrayField(TEXT("nodes"), NodeValues);
		Root->SetArrayField(TEXT("edges"), EdgeValues);
		Root->SetObjectField(TEXT("scc"), SCC);
		Root->SetObjectField(TEXT("counts"), Counts);

		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
			|| !Publish(JsonText, OutFilePath, OutError))
		{
			return false;
		}
		return true;
	}
}
