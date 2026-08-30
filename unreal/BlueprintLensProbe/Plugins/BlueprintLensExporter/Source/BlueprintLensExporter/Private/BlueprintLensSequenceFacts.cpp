// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_ExecutionSequence.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensSequenceFacts
{
	namespace
	{
		FString GuidToString(const FGuid& Guid)
		{
			return Guid.IsValid()
				? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
				: FString();
		}

		FString PinDirectionToString(const EEdGraphPinDirection Direction)
		{
			return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
		}

		FString EscapeIdentitySegment(FString Segment)
		{
			Segment.ReplaceInline(TEXT("%"), TEXT("%25"));
			Segment.ReplaceInline(TEXT(":"), TEXT("%3A"));
			Segment.ReplaceInline(TEXT("/"), TEXT("%2F"));
			return Segment;
		}

		FString PersistentPinGuid(const UEdGraphPin& Pin)
		{
#if WITH_EDITORONLY_DATA
			return GuidToString(Pin.PersistentGuid);
#else
			return FString();
#endif
		}

		int32 PinIndex(const UEdGraphPin& Pin)
		{
			const UEdGraphNode* Node = Pin.GetOwningNode();
			if (Node == nullptr)
			{
				return INDEX_NONE;
			}
			for (int32 Index = 0; Index < Node->Pins.Num(); ++Index)
			{
				if (Node->Pins[Index] == &Pin)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		int32 PinSameNameOccurrence(const UEdGraphPin& Pin)
		{
			const UEdGraphNode* Node = Pin.GetOwningNode();
			const int32 CurrentIndex = PinIndex(Pin);
			if (Node == nullptr || CurrentIndex == INDEX_NONE)
			{
				return 0;
			}
			int32 Occurrence = 0;
			for (int32 Index = 0; Index < CurrentIndex; ++Index)
			{
				const UEdGraphPin* Earlier = Node->Pins[Index];
				if (Earlier != nullptr
					&& Earlier->Direction == Pin.Direction
					&& Earlier->PinName == Pin.PinName)
				{
					++Occurrence;
				}
			}
			return Occurrence;
		}

		bool FindSequenceNode(
			const UBlueprint& Blueprint,
			const FString& SequenceNodeId,
			UK2Node_ExecutionSequence*& OutSequence,
			UEdGraph*& OutGraph,
			FString& OutError)
		{
			TArray<UEdGraph*> Graphs;
			Blueprint.GetAllGraphs(Graphs);
			int32 Matches = 0;
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph == nullptr)
				{
					continue;
				}
				const FString GraphId = Graph->GetPathName();
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node);
					if (Sequence != nullptr && MakeNodeId(GraphId, *Sequence) == SequenceNodeId)
					{
						++Matches;
						OutSequence = Sequence;
						OutGraph = Graph;
					}
				}
			}
			if (Matches != 1 || OutSequence == nullptr || OutGraph == nullptr)
			{
				OutError = FString::Printf(
					TEXT("Sequence root must resolve exactly once: %s (matches=%d)"),
					*SequenceNodeId,
					Matches);
				return false;
			}
			return true;
		}
	}

	FString MakeNodeId(const FString& GraphId, const UEdGraphNode& Node)
	{
		const FString LocalId = Node.NodeGuid.IsValid()
			? GuidToString(Node.NodeGuid)
			: FString::Printf(TEXT("object-%s"), *Node.GetName());
		return FString::Printf(TEXT("%s::node::%s"), *GraphId, *LocalId);
	}

	FString MakePinId(const FString& NodeId, const UEdGraphPin& Pin)
	{
		const FString PersistentGuid = PersistentPinGuid(Pin);
		if (!PersistentGuid.IsEmpty())
		{
			return FString::Printf(TEXT("%s::pin::persistent-%s"), *NodeId, *PersistentGuid);
		}
		const FString LocalId = FString::Printf(
			TEXT("locator-%s-%s-%d"),
			*PinDirectionToString(Pin.Direction),
			*EscapeIdentitySegment(Pin.PinName.ToString()),
			PinSameNameOccurrence(Pin));
		return FString::Printf(TEXT("%s::pin::%s"), *NodeId, *LocalId);
	}

	bool ExportSequenceFacts(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		FString& OutFilePath,
		FSequenceFactStats& OutStats,
		FString& OutError)
	{
		OutStats = FSequenceFactStats();
		OutFilePath.Reset();
		OutError.Reset();
		UK2Node_ExecutionSequence* Sequence = nullptr;
		UEdGraph* Graph = nullptr;
		if (!FindSequenceNode(Blueprint, SequenceNodeId, Sequence, Graph, OutError))
		{
			return false;
		}

		for (const UEdGraphPin* Pin : Sequence->Pins)
		{
			if (Pin != nullptr
				&& UEdGraphSchema_K2::IsExecPin(*Pin)
				&& Pin->Direction == EGPD_Output)
			{
				++OutStats.DeclaredOutputCount;
			}
		}

		const FString GraphId = Graph->GetPathName();
		TArray<TSharedPtr<FJsonValue>> Outputs;
		for (int32 Ordinal = 0; Ordinal < OutStats.DeclaredOutputCount; ++Ordinal)
		{
			UEdGraphPin* Pin = Sequence->GetThenPinGivenIndex(Ordinal);
			if (Pin == nullptr
				|| !UEdGraphSchema_K2::IsExecPin(*Pin)
				|| Pin->Direction != EGPD_Output)
			{
				OutError = FString::Printf(
					TEXT("Sequence API output ordinal is missing or invalid: %s ordinal=%d"),
					*SequenceNodeId,
					Ordinal);
				return false;
			}

			const FString SourcePinId = MakePinId(SequenceNodeId, *Pin);
			TArray<FString> EdgeIds;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* TargetNode = LinkedPin != nullptr
					? LinkedPin->GetOwningNode()
					: nullptr;
				if (LinkedPin == nullptr || TargetNode == nullptr)
				{
					OutError = FString::Printf(
						TEXT("Sequence output contains a dangling link: %s"),
						*SourcePinId);
					return false;
				}
				const FString TargetNodeId = MakeNodeId(GraphId, *TargetNode);
				const FString TargetPinId = MakePinId(TargetNodeId, *LinkedPin);
				EdgeIds.Add(FString::Printf(
					TEXT("%s::edge::%s->%s"),
					*GraphId,
					*SourcePinId,
					*TargetPinId));
			}
			Algo::Sort(EdgeIds);

			const bool bConnected = EdgeIds.Num() > 0;
			OutStats.ConnectedOutputCount += bConnected ? 1 : 0;
			OutStats.UnconnectedOutputCount += bConnected ? 0 : 1;
			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetNumberField(TEXT("ordinal"), Ordinal);
			Output->SetStringField(TEXT("source_pin_id"), SourcePinId);
			Output->SetStringField(TEXT("source_pin_name"), Pin->PinName.ToString());
			Output->SetStringField(
				TEXT("connection_state"),
				bConnected ? TEXT("connected") : TEXT("unconnected"));
			TArray<TSharedPtr<FJsonValue>> EdgeValues;
			for (const FString& EdgeId : EdgeIds)
			{
				EdgeValues.Add(MakeShared<FJsonValueString>(EdgeId));
			}
			Output->SetArrayField(TEXT("connected_edge_ids"), EdgeValues);
			Outputs.Add(MakeShared<FJsonValueObject>(Output));
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-sequence-source"));
		Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("blueprint_asset_path"), Blueprint.GetPathName());
		Root->SetStringField(TEXT("graph_id"), GraphId);
		Root->SetStringField(TEXT("sequence_node_id"), SequenceNodeId);
		Root->SetArrayField(TEXT("outputs"), Outputs);
		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("declared_output_count"), OutStats.DeclaredOutputCount);
		Counts->SetNumberField(TEXT("connected_output_count"), OutStats.ConnectedOutputCount);
		Counts->SetNumberField(TEXT("unconnected_output_count"), OutStats.UnconnectedOutputCount);
		Root->SetObjectField(TEXT("counts"), Counts);

		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			OutError = TEXT("Failed to serialize Sequence source facts.");
			return false;
		}

		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("SequenceFacts"));
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(TEXT("Could not create Sequence facts directory: %s"), *OutputDirectory);
			return false;
		}
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory,
			FString::Printf(TEXT("%s.sequence-source.json"), *Blueprint.GetName())));
		if (!FFileHelper::SaveStringToFile(
			JsonText,
			*OutFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write Sequence facts JSON: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}
