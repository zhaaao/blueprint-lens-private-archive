#include "BlueprintLensLC1TypedIrFacts.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR VariableSetClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_VariableSet");

FBlueprintLensLC1TypedIrFacts Failure(const TCHAR* DiagnosticCode)
{
	FBlueprintLensLC1TypedIrFacts Result;
	Result.Error = DiagnosticCode;
	return Result;
}

bool CalculateJsonSha256(
	const FString& JsonText,
	FString& OutSha256)
{
	FTCHARToUTF8 Utf8Json(*JsonText);
	TArray<uint8> JsonBytes;
	JsonBytes.Append(
		reinterpret_cast<const uint8*>(Utf8Json.Get()),
		Utf8Json.Length());

	TUniquePtr<FEncryptionContext> CryptoContext =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!CryptoContext.IsValid()
		|| !CryptoContext->CalcSHA256(JsonBytes, Digest)
		|| Digest.Num() != 32)
	{
		return false;
	}
	OutSha256 = BytesToHex(Digest.GetData(), Digest.Num());
	return true;
}

bool TryGetRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	FString& OutValue)
{
	return Object.IsValid()
		&& Object->TryGetStringField(FieldName, OutValue)
		&& !OutValue.IsEmpty();
}
} // namespace

FBlueprintLensLC1TypedIrFacts FBlueprintLensLC1TypedIrFactLoader::LoadFile(
	const FBlueprintLensSource& Source,
	const bool bRequireVariableSetFacts)
{
	FString JsonText;
	if (Source.IrPath.IsEmpty()
		|| !FFileHelper::LoadFileToString(JsonText, *Source.IrPath))
	{
		return Failure(TEXT("LC1_IR_FILE_UNREADABLE"));
	}
	return LoadJson(JsonText, Source.IrSha256, bRequireVariableSetFacts);
}

FBlueprintLensLC1TypedIrFacts FBlueprintLensLC1TypedIrFactLoader::LoadJson(
	const FString& JsonText,
	const FString& ExpectedIrSha256,
	const bool bRequireVariableSetFacts)
{
	FString ActualSha256;
	if (!CalculateJsonSha256(JsonText, ActualSha256)
		|| !ActualSha256.Equals(
			ExpectedIrSha256,
			ESearchCase::IgnoreCase))
	{
		return Failure(TEXT("LC1_IR_HASH_MISMATCH"));
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Failure(TEXT("LC1_IR_ROOT_MALFORMED"));
	}

	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	if (!Root->TryGetObjectField(TEXT("blueprint"), Blueprint)
		|| Blueprint == nullptr || !Blueprint->IsValid())
	{
		return Failure(TEXT("LC1_IR_ROOT_MALFORMED"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs)
		|| Graphs == nullptr)
	{
		return Failure(TEXT("LC1_IR_GRAPH_MALFORMED"));
	}

	FBlueprintLensLC1TypedIrFacts Result;
	Result.VerifiedIrSha256 = ActualSha256;
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
		if (!Graph.IsValid())
		{
			return Failure(TEXT("LC1_IR_GRAPH_MALFORMED"));
		}
		FBlueprintLensLC1GraphFact GraphFact;
		if (!TryGetRequiredString(Graph, TEXT("id"), GraphFact.GraphId)
			|| Result.GraphsById.Contains(GraphFact.GraphId))
		{
			return Failure(TEXT("LC1_IR_GRAPH_IDENTITY_MALFORMED"));
		}
		// Older typed-IR test producers only guaranteed graph identity. Keep
		// that existing loader contract; LC5 treats a missing semantic name as
		// non-matching rather than invalidating facts used by other grammars.
		Graph->TryGetStringField(TEXT("name"), GraphFact.GraphName);
		Graph->TryGetStringField(TEXT("kind"), GraphFact.GraphKind);
		if (GraphFact.GraphName.IsEmpty())
		{
			GraphFact.GraphName = GraphFact.GraphId;
		}

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph->TryGetArrayField(TEXT("nodes"), Nodes)
			|| Nodes == nullptr)
		{
			return Failure(TEXT("LC1_IR_GRAPH_MALFORMED"));
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> NodeObject =
				NodeValue->AsObject();
			if (!NodeObject.IsValid())
			{
				return Failure(TEXT("LC1_IR_NODE_MALFORMED"));
			}

			FString SourceNodeId;
			if (!TryGetRequiredString(
				NodeObject,
				TEXT("id"),
				SourceNodeId))
			{
				return Failure(TEXT("LC1_IR_NODE_ID_MISSING"));
			}

			FString OperationClass;
			if (!TryGetRequiredString(
				NodeObject,
				TEXT("class"),
				OperationClass))
			{
				return Failure(TEXT("LC1_IR_NODE_CLASS_MISSING"));
			}

			FString NativeTitle;
			if (!NodeObject->TryGetStringField(TEXT("title"), NativeTitle)
				|| NativeTitle.IsEmpty())
			{
				return Failure(TEXT("LC1_IR_NODE_TITLE_MISSING"));
			}
			if (Result.NodesBySourceNodeId.Contains(SourceNodeId))
			{
				return Failure(TEXT("LC1_IR_NODE_ID_DUPLICATE"));
			}
			FBlueprintLensLC1NodeFact NodeFact;
			NodeFact.SourceNodeId = SourceNodeId;
			NodeFact.GraphId = GraphFact.GraphId;
			NodeFact.NodeClass = OperationClass;
			NodeFact.NativeTitle = NativeTitle;
			const TSharedPtr<FJsonObject>* Symbol = nullptr;
			if (NodeObject->TryGetObjectField(TEXT("symbol"), Symbol)
				&& Symbol != nullptr && Symbol->IsValid())
			{
				NodeFact.bHasSymbol =
					(*Symbol)->TryGetStringField(
						TEXT("name"), NodeFact.SymbolName)
					&& !NodeFact.SymbolName.IsEmpty()
					&& (*Symbol)->TryGetBoolField(
						TEXT("is_self_context"), NodeFact.bIsSelfContext)
					&& (*Symbol)->TryGetBoolField(
						TEXT("is_pure"), NodeFact.bIsPure)
					&& (*Symbol)->TryGetBoolField(
						TEXT("is_latent"), NodeFact.bIsLatent);
			}

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!NodeObject->TryGetArrayField(TEXT("pins"), Pins)
				|| Pins == nullptr)
			{
				return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
			}

			TArray<TSharedPtr<FJsonObject>> ValuePins;
			for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
			{
				const TSharedPtr<FJsonObject> PinObject =
					PinValue->AsObject();
				if (!PinObject.IsValid())
				{
					return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
				}

				FString PinRole;
				FString Direction;
				FString Kind;
				if (!PinObject->TryGetStringField(
						TEXT("pin_role"),
						PinRole)
					|| !PinObject->TryGetStringField(
						TEXT("direction"),
						Direction)
					|| !PinObject->TryGetStringField(
						TEXT("kind"),
						Kind))
				{
					return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
				}
				FBlueprintLensLC1NodeFact::FPin PinFact;
				if (!TryGetRequiredString(PinObject, TEXT("id"), PinFact.PinId)
					|| !PinObject->TryGetStringField(TEXT("name"), PinFact.Name))
				{
					return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
				}
				PinFact.Direction = Direction;
				PinFact.Kind = Kind;
				NodeFact.Pins.Add(MoveTemp(PinFact));
				if (PinRole == TEXT("variable_set_value")
					&& Direction == TEXT("input")
					&& Kind == TEXT("data"))
				{
					ValuePins.Add(PinObject);
				}
			}
			GraphFact.NodeIds.Add(SourceNodeId);
			Result.NodesBySourceNodeId.Add(SourceNodeId, MoveTemp(NodeFact));

			if (OperationClass != VariableSetClass)
			{
				continue;
			}
			if (ValuePins.Num() != 1)
			{
				if (bRequireVariableSetFacts)
				{
					return Failure(TEXT("LC1_IR_AMBIGUOUS_VALUE_PIN"));
				}
				continue;
			}

			const TSharedPtr<FJsonObject>& ValuePin = ValuePins[0];
			FBlueprintLensLC1OperationFact Fact;
			Fact.SourceNodeId = SourceNodeId;
			Fact.OperationClass = OperationClass;
			if (!TryGetRequiredString(
					ValuePin,
					TEXT("id"),
					Fact.ValuePinId)
				|| !ValuePin->TryGetStringField(
					TEXT("name"),
					Fact.VariableTarget))
			{
				return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
			}

			const TSharedPtr<FJsonObject>* Type = nullptr;
			const TSharedPtr<FJsonObject>* Default = nullptr;
			if (!ValuePin->TryGetObjectField(TEXT("type"), Type)
				|| Type == nullptr || !Type->IsValid()
				|| !ValuePin->TryGetObjectField(TEXT("default"), Default)
				|| Default == nullptr || !Default->IsValid()
				|| !(*Type)->TryGetStringField(
					TEXT("category"),
					Fact.ValueType)
				|| !(*Default)->TryGetStringField(
					TEXT("value"),
					Fact.LiteralValue))
			{
				return Failure(TEXT("LC1_IR_PIN_MALFORMED"));
			}
			if (Result.OperationsBySourceNodeId.Contains(Fact.SourceNodeId))
			{
				return Failure(TEXT("LC1_IR_NODE_ID_DUPLICATE"));
			}
			Result.OperationsBySourceNodeId.Add(
				Fact.SourceNodeId,
				MoveTemp(Fact));
		}

		const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
		if (!Graph->TryGetArrayField(TEXT("edges"), Edges) || Edges == nullptr)
		{
			return Failure(TEXT("LC1_IR_EDGE_MALFORMED"));
		}
		for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
		{
			const TSharedPtr<FJsonObject> EdgeObject = EdgeValue->AsObject();
			FBlueprintLensLC1EdgeFact Edge;
			if (!EdgeObject.IsValid()
				|| !TryGetRequiredString(EdgeObject, TEXT("id"), Edge.EdgeId)
				|| !TryGetRequiredString(EdgeObject, TEXT("kind"), Edge.Kind)
				|| !TryGetRequiredString(EdgeObject, TEXT("source_node_id"), Edge.SourceNodeId)
				|| !TryGetRequiredString(EdgeObject, TEXT("source_pin_id"), Edge.SourcePinId)
				|| !TryGetRequiredString(EdgeObject, TEXT("target_node_id"), Edge.TargetNodeId)
				|| !TryGetRequiredString(EdgeObject, TEXT("target_pin_id"), Edge.TargetPinId)
				|| !EdgeObject->TryGetBoolField(TEXT("direction_is_valid"), Edge.bDirectionIsValid))
			{
				return Failure(TEXT("LC1_IR_EDGE_MALFORMED"));
			}
			Edge.GraphId = GraphFact.GraphId;
			if (Result.Edges.ContainsByPredicate(
				[&Edge](const FBlueprintLensLC1EdgeFact& Existing)
				{
					return Existing.EdgeId == Edge.EdgeId;
				}))
			{
				return Failure(TEXT("LC1_IR_EDGE_ID_DUPLICATE"));
			}
			GraphFact.EdgeIds.Add(Edge.EdgeId);
			Result.Edges.Add(MoveTemp(Edge));
		}
		Result.Graphs.Add(GraphFact);
		Result.GraphsById.Add(GraphFact.GraphId, MoveTemp(GraphFact));
	}
	return Result;
}
