// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensProductionExporter.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensProductionExporter
{
	namespace
	{
		struct FExportStats
		{
			int32 GraphCount = 0;
			int32 NodeCount = 0;
			int32 PinCount = 0;
			int32 EdgeCount = 0;
			int32 UnsupportedNodeCount = 0;
		};

		void SetError(
			FExportError& OutError,
			const EExportErrorCode Code,
			FString Message)
		{
			OutError.Code = Code;
			OutError.Message = MoveTemp(Message);
		}

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

		FString PinContainerToString(const EPinContainerType ContainerType)
		{
			switch (ContainerType)
			{
			case EPinContainerType::Array:
				return TEXT("array");
			case EPinContainerType::Set:
				return TEXT("set");
			case EPinContainerType::Map:
				return TEXT("map");
			case EPinContainerType::None:
			default:
				return TEXT("none");
			}
		}

		bool ContainsGraph(const TArray<TObjectPtr<UEdGraph>>& Graphs, const UEdGraph* Graph)
		{
			for (const TObjectPtr<UEdGraph>& Candidate : Graphs)
			{
				if (Candidate.Get() == Graph)
				{
					return true;
				}
			}
			return false;
		}

		FString GraphKind(const UBlueprint& Blueprint, const UEdGraph& Graph)
		{
			if (Graph.GetFName() == TEXT("UserConstructionScript"))
			{
				return TEXT("construction_script");
			}
			if (ContainsGraph(Blueprint.UbergraphPages, &Graph))
			{
				return TEXT("event");
			}
			if (ContainsGraph(Blueprint.FunctionGraphs, &Graph))
			{
				return TEXT("function");
			}
			if (ContainsGraph(Blueprint.MacroGraphs, &Graph))
			{
				return TEXT("macro");
			}
			if (ContainsGraph(Blueprint.DelegateSignatureGraphs, &Graph))
			{
				return TEXT("delegate_signature");
			}
			return TEXT("unknown");
		}

		FString MakeNodeId(const FString& GraphId, const UEdGraphNode& Node)
		{
			const FString LocalId = Node.NodeGuid.IsValid()
				? GuidToString(Node.NodeGuid)
				: FString::Printf(TEXT("object-%s"), *Node.GetName());
			return FString::Printf(TEXT("%s::node::%s"), *GraphId, *LocalId);
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

		int32 PinSameNameOccurrence(const UEdGraphPin& Pin, const int32 PinIndex)
		{
			int32 SameNameOccurrence = 0;
			const UEdGraphNode* OwningNode = Pin.GetOwningNode();
			if (OwningNode != nullptr)
			{
				for (int32 EarlierIndex = 0; EarlierIndex < PinIndex; ++EarlierIndex)
				{
					const UEdGraphPin* EarlierPin = OwningNode->Pins[EarlierIndex];
					if (EarlierPin != nullptr
						&& EarlierPin->Direction == Pin.Direction
						&& EarlierPin->PinName == Pin.PinName)
					{
						++SameNameOccurrence;
					}
				}
			}
			return SameNameOccurrence;
		}

		FString MakePinId(const FString& NodeId, const UEdGraphPin& Pin, const int32 PinIndex)
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
				PinSameNameOccurrence(Pin, PinIndex));
			return FString::Printf(TEXT("%s::pin::%s"), *NodeId, *LocalId);
		}

		FString PinRole(const UEdGraphNode& Node, const UEdGraphPin& Pin)
		{
			if (const UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(&Node))
			{
				const UEdGraphPin* ValuePin = VariableSet->FindPin(
					VariableSet->GetVarName(),
					EGPD_Input);
				if (ValuePin == &Pin)
				{
					return TEXT("variable_set_value");
				}
			}

			if (const UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(&Node))
			{
				if (Branch->GetConditionPin() == &Pin)
				{
					return TEXT("branch_condition");
				}
			}

			return TEXT("none");
		}

		TSharedPtr<FJsonObject> SerializeTerminalType(const FEdGraphTerminalType& Type)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("category"), Type.TerminalCategory.ToString());
			Json->SetStringField(TEXT("subcategory"), Type.TerminalSubCategory.ToString());
			Json->SetStringField(
				TEXT("object_path"),
				Type.TerminalSubCategoryObject.IsValid()
					? Type.TerminalSubCategoryObject->GetPathName()
					: FString());
			Json->SetBoolField(TEXT("is_const"), Type.bTerminalIsConst);
			Json->SetBoolField(TEXT("is_weak_pointer"), Type.bTerminalIsWeakPointer);
			Json->SetBoolField(TEXT("is_uobject_wrapper"), Type.bTerminalIsUObjectWrapper);
			return Json;
		}

		TSharedPtr<FJsonObject> SerializePinType(const FEdGraphPinType& Type)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("category"), Type.PinCategory.ToString());
			Json->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
			Json->SetStringField(
				TEXT("object_path"),
				Type.PinSubCategoryObject.IsValid()
					? Type.PinSubCategoryObject->GetPathName()
					: FString());
			Json->SetStringField(TEXT("container"), PinContainerToString(Type.ContainerType));
			Json->SetBoolField(TEXT("is_reference"), Type.bIsReference);
			Json->SetBoolField(TEXT("is_const"), Type.bIsConst);
			Json->SetBoolField(TEXT("is_weak_pointer"), Type.bIsWeakPointer);
			Json->SetBoolField(TEXT("is_uobject_wrapper"), Type.bIsUObjectWrapper);
			Json->SetBoolField(
				TEXT("serialize_as_single_precision_float"),
				Type.bSerializeAsSinglePrecisionFloat);
			if (Type.ContainerType == EPinContainerType::Map)
			{
				Json->SetObjectField(TEXT("map_value_type"), SerializeTerminalType(Type.PinValueType));
			}
			return Json;
		}

		TSharedPtr<FJsonObject> SerializeMemberReference(const FMemberReference& Reference)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Reference.GetMemberName().ToString());
			Json->SetStringField(TEXT("guid"), GuidToString(Reference.GetMemberGuid()));
			Json->SetStringField(
				TEXT("parent_class"),
				Reference.GetMemberParentClass() != nullptr
					? Reference.GetMemberParentClass()->GetPathName()
					: FString());
			Json->SetBoolField(TEXT("is_self_context"), Reference.IsSelfContext());
			Json->SetBoolField(TEXT("is_local_scope"), Reference.IsLocalScope());
			return Json;
		}

		TSharedPtr<FJsonObject> SerializeNodeSymbol(const UEdGraphNode& Node)
		{
			if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(&Node))
			{
				TSharedPtr<FJsonObject> Symbol = SerializeMemberReference(VariableNode->VariableReference);
				Symbol->SetStringField(TEXT("kind"), TEXT("variable"));
				Symbol->SetStringField(
					TEXT("access"),
					Node.IsA<UK2Node_VariableGet>()
						? TEXT("get")
						: Node.IsA<UK2Node_VariableSet>() ? TEXT("set") : TEXT("unknown"));
				return Symbol;
			}

			if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(&Node))
			{
				TSharedPtr<FJsonObject> Symbol = SerializeMemberReference(CallNode->FunctionReference);
				const UFunction* Function = CallNode->GetTargetFunction();
				Symbol->SetStringField(TEXT("kind"), TEXT("function"));
				Symbol->SetBoolField(
					TEXT("is_pure"),
					Function != nullptr && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
				Symbol->SetBoolField(
					TEXT("is_latent"),
					Function != nullptr && Function->HasMetaData(TEXT("Latent")));
				return Symbol;
			}

			return nullptr;
		}

		TSharedPtr<FJsonObject> SerializePin(
			const UEdGraphNode& Node,
			const UEdGraphPin& Pin,
			const FString& PinId,
			const FString& NodeId)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), PinId);
			const FString PersistentGuid = PersistentPinGuid(Pin);
			Json->SetStringField(TEXT("persistent_guid"), PersistentGuid);
			Json->SetStringField(
				TEXT("identity_source"),
				PersistentGuid.IsEmpty() ? TEXT("pin_locator") : TEXT("persistent_guid"));
			Json->SetStringField(TEXT("node_id"), NodeId);
			Json->SetStringField(TEXT("name"), Pin.PinName.ToString());
			Json->SetStringField(TEXT("direction"), PinDirectionToString(Pin.Direction));
			Json->SetStringField(
				TEXT("kind"),
				Pin.PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
			Json->SetStringField(TEXT("pin_role"), PinRole(Node, Pin));
			Json->SetObjectField(TEXT("type"), SerializePinType(Pin.PinType));

			TSharedPtr<FJsonObject> Default = MakeShared<FJsonObject>();
			Default->SetStringField(TEXT("value"), Pin.DefaultValue);
			Default->SetStringField(
				TEXT("object_path"),
				Pin.DefaultObject != nullptr ? Pin.DefaultObject->GetPathName() : FString());
			Default->SetStringField(TEXT("text"), Pin.DefaultTextValue.ToString());
			Json->SetObjectField(TEXT("default"), Default);
			Json->SetBoolField(TEXT("hidden"), Pin.bHidden);
			Json->SetBoolField(TEXT("orphaned"), Pin.bOrphanedPin);
			Json->SetBoolField(TEXT("not_connectable"), Pin.bNotConnectable);
			return Json;
		}

		TSharedPtr<FJsonObject> SerializeNode(
			const UEdGraphNode& Node,
			const FString& NodeId,
			const TMap<const UEdGraphPin*, FString>& PinIds,
			FExportStats& Stats)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), NodeId);
			Json->SetStringField(TEXT("native_guid"), GuidToString(Node.NodeGuid));
			Json->SetStringField(
				TEXT("identity_source"),
				Node.NodeGuid.IsValid() ? TEXT("node_guid") : TEXT("fallback"));
			Json->SetStringField(TEXT("class"), Node.GetClass()->GetPathName());
			Json->SetStringField(
				TEXT("title"),
				Node.GetNodeTitle(ENodeTitleType::ListView).BuildSourceString());
			Json->SetNumberField(TEXT("position_x"), Node.NodePosX);
			Json->SetNumberField(TEXT("position_y"), Node.NodePosY);

			FString SemanticStatus = TEXT("unclassified");
			FString SemanticReason;
			if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(&Node))
			{
				const UFunction* Function = CallNode->GetTargetFunction();
				if (Function != nullptr && Function->HasMetaData(TEXT("Latent")))
				{
					SemanticStatus = TEXT("unsupported");
					SemanticReason = TEXT("latent_function");
					++Stats.UnsupportedNodeCount;
				}
			}
			Json->SetStringField(TEXT("semantic_status"), SemanticStatus);
			Json->SetStringField(TEXT("semantic_reason"), SemanticReason);
			if (TSharedPtr<FJsonObject> Symbol = SerializeNodeSymbol(Node))
			{
				Json->SetObjectField(TEXT("symbol"), Symbol);
			}

			TArray<const UEdGraphPin*> Pins;
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin != nullptr)
				{
					Pins.Add(Pin);
				}
			}
			Algo::Sort(Pins, [&PinIds](const UEdGraphPin* Left, const UEdGraphPin* Right)
			{
				return PinIds.FindChecked(Left) < PinIds.FindChecked(Right);
			});

			TArray<TSharedPtr<FJsonValue>> PinValues;
			for (const UEdGraphPin* Pin : Pins)
			{
				PinValues.Add(MakeShared<FJsonValueObject>(
					SerializePin(Node, *Pin, PinIds.FindChecked(Pin), NodeId)));
				++Stats.PinCount;
			}
			Json->SetArrayField(TEXT("pins"), PinValues);
			return Json;
		}

		bool SerializeGraph(
			const UBlueprint& Blueprint,
			const UEdGraph& Graph,
			TSharedPtr<FJsonObject>& OutGraph,
			FExportStats& Stats,
			FString& OutError)
		{
			const FString GraphId = Graph.GetPathName();
			TMap<const UEdGraphNode*, FString> NodeIds;
			TMap<const UEdGraphPin*, FString> PinIds;
			TSet<FString> UsedIds;
			TArray<const UEdGraphNode*> Nodes;
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				const FString NodeId = MakeNodeId(GraphId, *Node);
				if (UsedIds.Contains(NodeId))
				{
					OutError = FString::Printf(TEXT("Duplicate node identity: %s"), *NodeId);
					return false;
				}
				UsedIds.Add(NodeId);
				NodeIds.Add(Node, NodeId);
				Nodes.Add(Node);
				for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
				{
					const UEdGraphPin* Pin = Node->Pins[PinIndex];
					if (Pin == nullptr)
					{
						continue;
					}
					const FString PinId = MakePinId(NodeId, *Pin, PinIndex);
					if (UsedIds.Contains(PinId))
					{
						OutError = FString::Printf(TEXT("Duplicate pin identity: %s"), *PinId);
						return false;
					}
					UsedIds.Add(PinId);
					PinIds.Add(Pin, PinId);
				}
			}

			Algo::Sort(Nodes, [&NodeIds](const UEdGraphNode* Left, const UEdGraphNode* Right)
			{
				return NodeIds.FindChecked(Left) < NodeIds.FindChecked(Right);
			});

			OutGraph = MakeShared<FJsonObject>();
			OutGraph->SetStringField(TEXT("id"), GraphId);
			OutGraph->SetStringField(TEXT("name"), Graph.GetName());
			OutGraph->SetStringField(TEXT("kind"), GraphKind(Blueprint, Graph));
			OutGraph->SetStringField(TEXT("class"), Graph.GetClass()->GetPathName());
			TArray<TSharedPtr<FJsonValue>> NodeValues;
			for (const UEdGraphNode* Node : Nodes)
			{
				NodeValues.Add(MakeShared<FJsonValueObject>(
					SerializeNode(*Node, NodeIds.FindChecked(Node), PinIds, Stats)));
				++Stats.NodeCount;
			}
			OutGraph->SetArrayField(TEXT("nodes"), NodeValues);

			TArray<TSharedPtr<FJsonObject>> Edges;
			TSet<FString> UsedEdgeIds;
			for (const UEdGraphNode* Node : Nodes)
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output)
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (LinkedPin == nullptr || LinkedPin->GetOwningNode() == nullptr)
						{
							continue;
						}
						const FString* SourcePinId = PinIds.Find(Pin);
						const FString* TargetPinId = PinIds.Find(LinkedPin);
						const FString* SourceNodeId = NodeIds.Find(Node);
						const FString* TargetNodeId = NodeIds.Find(LinkedPin->GetOwningNode());
						if (SourcePinId == nullptr || TargetPinId == nullptr
							|| SourceNodeId == nullptr || TargetNodeId == nullptr)
						{
							OutError = FString::Printf(
								TEXT("Dangling link while exporting graph: %s"),
								*GraphId);
							return false;
						}
						const FString EdgeId = FString::Printf(
							TEXT("%s::edge::%s->%s"),
							*GraphId,
							**SourcePinId,
							**TargetPinId);
						if (UsedEdgeIds.Contains(EdgeId))
						{
							continue;
						}
						UsedEdgeIds.Add(EdgeId);
						TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
						Edge->SetStringField(TEXT("id"), EdgeId);
						Edge->SetStringField(
							TEXT("kind"),
							Pin->PinType.PinCategory == TEXT("exec") ? TEXT("execution") : TEXT("data"));
						Edge->SetStringField(TEXT("source_node_id"), *SourceNodeId);
						Edge->SetStringField(TEXT("source_pin_id"), *SourcePinId);
						Edge->SetStringField(TEXT("target_node_id"), *TargetNodeId);
						Edge->SetStringField(TEXT("target_pin_id"), *TargetPinId);
						Edge->SetBoolField(TEXT("direction_is_valid"), LinkedPin->Direction == EGPD_Input);
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
				++Stats.EdgeCount;
			}
			OutGraph->SetArrayField(TEXT("edges"), EdgeValues);
			return true;
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
	}

	bool ExportRawDocument(
		const FExportRequest& Request,
		FExportResult& OutResult,
		FExportError& OutError)
	{
		OutResult = FExportResult();
		OutError = FExportError();
		if (Request.Blueprint == nullptr
			|| Request.OutputPath.IsEmpty()
			|| FPaths::IsRelative(Request.OutputPath))
		{
			SetError(
				OutError,
				EExportErrorCode::InvalidRequest,
				TEXT("Blueprint and an absolute output path are required."));
			return false;
		}

		const UBlueprint& Blueprint = *Request.Blueprint;
		FExportStats Stats;
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		Algo::Sort(Graphs, [](const UEdGraph* Left, const UEdGraph* Right)
		{
			return Left->GetPathName() < Right->GetPathName();
		});

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-raw-probe"));
		Root->SetStringField(TEXT("format_version"), TEXT("0.2"));
		Root->SetStringField(TEXT("schema_status"), TEXT("unfrozen"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

		TSharedPtr<FJsonObject> BlueprintJson = MakeShared<FJsonObject>();
		BlueprintJson->SetStringField(TEXT("id"), Blueprint.GetPathName());
		BlueprintJson->SetStringField(TEXT("name"), Blueprint.GetName());
		BlueprintJson->SetStringField(TEXT("path"), Blueprint.GetPathName());
		BlueprintJson->SetStringField(
			TEXT("parent_class"),
			Blueprint.ParentClass != nullptr ? Blueprint.ParentClass->GetPathName() : FString());

		TArray<TSharedPtr<FJsonValue>> GraphValues;
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph == nullptr)
			{
				continue;
			}
			TSharedPtr<FJsonObject> GraphJson;
			FString SerializationError;
			if (!SerializeGraph(Blueprint, *Graph, GraphJson, Stats, SerializationError))
			{
				SetError(
					OutError,
					EExportErrorCode::SerializationFailed,
					MoveTemp(SerializationError));
				return false;
			}
			GraphValues.Add(MakeShared<FJsonValueObject>(GraphJson));
			++Stats.GraphCount;
		}
		BlueprintJson->SetArrayField(TEXT("graphs"), GraphValues);
		Root->SetObjectField(TEXT("blueprint"), BlueprintJson);

		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("graphs"), Stats.GraphCount);
		Counts->SetNumberField(TEXT("nodes"), Stats.NodeCount);
		Counts->SetNumberField(TEXT("pins"), Stats.PinCount);
		Counts->SetNumberField(TEXT("edges"), Stats.EdgeCount);
		Counts->SetNumberField(TEXT("unsupported_nodes"), Stats.UnsupportedNodeCount);
		Root->SetObjectField(TEXT("counts"), Counts);

		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			SetError(
				OutError,
				EExportErrorCode::SerializationFailed,
				TEXT("Failed to serialize the Blueprint JSON document."));
			return false;
		}

		const FString OutputPath = FPaths::ConvertRelativePathToFull(Request.OutputPath);
		const FString OutputDirectory = FPaths::GetPath(OutputPath);
		if (OutputDirectory.IsEmpty() || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			SetError(
				OutError,
				EExportErrorCode::WriteFailed,
				FString::Printf(TEXT("Could not create output directory: %s"), *OutputDirectory));
			return false;
		}
		if (!FFileHelper::SaveStringToFile(
				JsonText,
				*OutputPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			SetError(
				OutError,
				EExportErrorCode::WriteFailed,
				FString::Printf(TEXT("Could not write JSON file: %s"), *OutputPath));
			return false;
		}

		const FString Sha256 = Sha256File(OutputPath);
		if (Sha256.IsEmpty())
		{
			SetError(
				OutError,
				EExportErrorCode::WriteFailed,
				FString::Printf(TEXT("Could not hash JSON file: %s"), *OutputPath));
			return false;
		}

		OutResult.BlueprintObjectPath = Blueprint.GetPathName();
		OutResult.OutputPath = OutputPath;
		OutResult.Sha256 = Sha256;
		OutResult.GraphCount = Stats.GraphCount;
		OutResult.NodeCount = Stats.NodeCount;
		OutResult.PinCount = Stats.PinCount;
		OutResult.EdgeCount = Stats.EdgeCount;
		OutResult.UnsupportedNodeCount = Stats.UnsupportedNodeCount;
		return true;
	}
}

namespace BlueprintLens::Production
{
	bool ExportRawDocument(
		UBlueprint* Blueprint,
		const FString& OutputPath,
		FBlueprintLensExportResult& OutResult)
	{
		BlueprintLensProductionExporter::FExportRequest Request;
		Request.Blueprint = Blueprint;
		Request.OutputPath = OutputPath;
		BlueprintLensProductionExporter::FExportError Error;
		return BlueprintLensProductionExporter::ExportRawDocument(
			Request,
			OutResult,
			Error);
	}
}
