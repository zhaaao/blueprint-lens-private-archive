#include "BlueprintLensLC4SequenceProfile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR LC4Format[] = TEXT("blueprint-lens-sequence-profile");
constexpr TCHAR LC4SchemaVersion[] = TEXT("1.0.0");
constexpr TCHAR LC4ProfileId[] = TEXT("LC4_SEQUENCE_FANOUT_TO_BOUNDARY_V1");
constexpr TCHAR LC4RulesVersion[] =
	TEXT("sequence_fanout_to_first_boundary_v1");
constexpr TCHAR LC4QueryMode[] = TEXT("sequence_fanout_overview");

struct FLC4IrNode
{
	FString Id;
	FString Title;
	FString NativeGuid;
	TMap<FString, FString> PinLabels;
};

struct FLC4IrEdge
{
	FString Id;
	FString SourceNodeId;
	FString SourcePinId;
	FString TargetNodeId;
	FString TargetPinId;
};

FBlueprintLensLC4SequenceLoadResult Failure(
	const TCHAR* Code,
	const FString& Detail = FString())
{
	FBlueprintLensLC4SequenceLoadResult Result;
	Result.Error = Detail.IsEmpty()
		? FString(Code)
		: FString::Printf(TEXT("%s: %s"), Code, *Detail);
	return Result;
}

bool RequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& Out)
{
	return Object.IsValid() && Object->TryGetStringField(Field, Out) &&
		!Out.IsEmpty();
}

bool RequiredInteger(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32& Out)
{
	double Number = 0.0;
	if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number) ||
		!FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)) ||
		Number < static_cast<double>(MIN_int32) ||
		Number > static_cast<double>(MAX_int32))
	{
		return false;
	}
	Out = FMath::RoundToInt(Number);
	return true;
}

bool RequiredObject(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TSharedPtr<FJsonObject>& Out)
{
	const TSharedPtr<FJsonObject>* Value = nullptr;
	if (!Object.IsValid() || !Object->TryGetObjectField(Field, Value) ||
		Value == nullptr || !Value->IsValid())
	{
		return false;
	}
	Out = *Value;
	return true;
}

bool RequiredArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	const TArray<TSharedPtr<FJsonValue>>*& Out)
{
	return Object.IsValid() && Object->TryGetArrayField(Field, Out) &&
		Out != nullptr;
}

bool StringArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TArray<FString>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!RequiredArray(Object, Field, Values))
	{
		return false;
	}
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Item;
		if (!Value.IsValid() || !Value->TryGetString(Item) || Item.IsEmpty() ||
			Out.Contains(Item))
		{
			return false;
		}
		Out.Add(MoveTemp(Item));
	}
	return true;
}

bool IntegerArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TArray<int32>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!RequiredArray(Object, Field, Values))
	{
		return false;
	}
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		double Number = 0.0;
		if (!Value.IsValid() || !Value->TryGetNumber(Number) ||
			!FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
		{
			return false;
		}
		const int32 Item = FMath::RoundToInt(Number);
		if (Out.Contains(Item))
		{
			return false;
		}
		Out.Add(Item);
	}
	return true;
}

bool HashFile(const FString& Path, FString& OutSha256)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return false;
	}
	TUniquePtr<FEncryptionContext> CryptoContext =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!CryptoContext.IsValid() ||
		!CryptoContext->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
	{
		return false;
	}
	OutSha256 = BytesToHex(Digest.GetData(), Digest.Num());
	return true;
}

bool ParseJsonFile(
	const FString& Path,
	TSharedPtr<FJsonObject>& OutRoot,
	FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("file is unreadable: %s"), *Path);
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
	{
		OutError = FString::Printf(TEXT("invalid JSON: %s"), *Path);
		return false;
	}
	return true;
}

bool ParseConnectionState(
	const FString& Value,
	EBlueprintLensLC4ConnectionState& Out)
{
	if (Value == TEXT("connected"))
	{
		Out = EBlueprintLensLC4ConnectionState::Connected;
		return true;
	}
	if (Value == TEXT("unconnected"))
	{
		Out = EBlueprintLensLC4ConnectionState::Unconnected;
		return true;
	}
	return false;
}

bool ParseCriterionRelation(
	const FString& Value,
	EBlueprintLensLC4CriterionRelation& Out)
{
	if (Value == TEXT("included"))
	{
		Out = EBlueprintLensLC4CriterionRelation::Included;
		return true;
	}
	if (Value == TEXT("outside"))
	{
		Out = EBlueprintLensLC4CriterionRelation::Outside;
		return true;
	}
	if (Value == TEXT("indeterminate"))
	{
		Out = EBlueprintLensLC4CriterionRelation::Indeterminate;
		return true;
	}
	return false;
}

bool ParseTerminationKind(
	const FString& Value,
	EBlueprintLensLC4TerminationKind& Out)
{
	if (Value == TEXT("ordinary_reconvergence"))
	{
		Out = EBlueprintLensLC4TerminationKind::OrdinaryReconvergence;
		return true;
	}
	if (Value == TEXT("terminal"))
	{
		Out = EBlueprintLensLC4TerminationKind::Terminal;
		return true;
	}
	if (Value == TEXT("unconnected"))
	{
		Out = EBlueprintLensLC4TerminationKind::Unconnected;
		return true;
	}
	return false;
}

bool ParseOutput(
	const TSharedPtr<FJsonObject>& Object,
	FBlueprintLensLC4SequenceOutput& Out)
{
	FString Connection;
	FString Criterion;
	TSharedPtr<FJsonObject> Termination;
	FString TerminationKind;
	return RequiredInteger(Object, TEXT("ordinal"), Out.Ordinal) &&
		RequiredString(Object, TEXT("source_pin_id"), Out.SourcePinId) &&
		RequiredString(Object, TEXT("source_pin_name"), Out.SourcePinName) &&
		RequiredString(Object, TEXT("connection_state"), Connection) &&
		ParseConnectionState(Connection, Out.ConnectionState) &&
		RequiredString(Object, TEXT("criterion_relation"), Criterion) &&
		ParseCriterionRelation(Criterion, Out.CriterionRelation) &&
		RequiredString(Object, TEXT("criterion_reason"), Out.CriterionReason) &&
		StringArray(Object, TEXT("connected_edge_ids"), Out.ConnectedEdgeIds) &&
		StringArray(Object, TEXT("reachable_node_ids"), Out.ReachableNodeIds) &&
		StringArray(Object, TEXT("reachable_edge_ids"), Out.ReachableEdgeIds) &&
		RequiredObject(Object, TEXT("termination"), Termination) &&
		RequiredString(Termination, TEXT("kind"), TerminationKind) &&
		ParseTerminationKind(TerminationKind, Out.TerminationKind) &&
		RequiredString(Termination, TEXT("node_id"), Out.TerminationNodeId);
}

bool ParseProfile(
	const TSharedPtr<FJsonObject>& Root,
	const FString& ProfilePath,
	FBlueprintLensLC4SequenceProfile& Out,
	FString& Error)
{
	TSharedPtr<FJsonObject> Source;
	TSharedPtr<FJsonObject> Counts;
	const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Reconvergences = nullptr;
	FString IrFile;
	FString SliceFile;
	if (!RequiredString(Root, TEXT("format"), Out.Format) ||
		!RequiredString(Root, TEXT("schema_version"), Out.SchemaVersion) ||
		!RequiredString(Root, TEXT("profile_id"), Out.ProfileId) ||
		!RequiredString(Root, TEXT("rules_version"), Out.RulesVersion) ||
		!RequiredString(Root, TEXT("query_mode"), Out.QueryMode) ||
		!RequiredObject(Root, TEXT("source"), Source) ||
		!RequiredObject(Root, TEXT("counts"), Counts) ||
		!RequiredArray(Root, TEXT("outputs"), Outputs) ||
		!RequiredArray(Root, TEXT("reconvergences"), Reconvergences))
	{
		Error = TEXT("required root field is missing or malformed");
		return false;
	}
	if (Out.Format != LC4Format || Out.SchemaVersion != LC4SchemaVersion ||
		Out.ProfileId != LC4ProfileId || Out.RulesVersion != LC4RulesVersion ||
		Out.QueryMode != LC4QueryMode)
	{
		Error = TEXT("profile identity does not match the frozen LC4-SEQ contract");
		return false;
	}
	FString AssetFile;
	if (!RequiredString(
			Source,
			TEXT("blueprint_asset_path"),
			Out.Source.BlueprintAssetPath) ||
		!RequiredString(Source, TEXT("asset_file"), AssetFile) ||
		!RequiredString(
			Source,
			TEXT("asset_sha256"),
			Out.Source.BlueprintPackageSha256) ||
		!RequiredString(Source, TEXT("graph_id"), Out.Source.GraphId) ||
		!RequiredString(
			Source,
			TEXT("sequence_node_id"),
			Out.Source.SequenceNodeId) ||
		!RequiredString(
			Source,
			TEXT("criterion_node_id"),
			Out.Source.CriterionNodeId) ||
		!RequiredString(Source, TEXT("ir_file"), IrFile) ||
		!RequiredString(Source, TEXT("ir_sha256"), Out.Source.IrSha256) ||
		!RequiredString(Source, TEXT("slice_file"), SliceFile) ||
		!RequiredString(Source, TEXT("slice_sha256"), Out.Source.SliceSha256))
	{
		Error = TEXT("source binding is missing or malformed");
		return false;
	}
	const FString Directory = FPaths::GetPath(ProfilePath);
	Out.ProfilePath = FPaths::ConvertRelativePathToFull(ProfilePath);
	Out.Source.IrPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Directory, IrFile));
	Out.Source.SlicePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Directory, SliceFile));

	if (Outputs->Num() != 4)
	{
		Error = TEXT("exactly four output records are required");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Outputs)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		FBlueprintLensLC4SequenceOutput Output;
		if (!ParseOutput(Object, Output))
		{
			Error = TEXT("an output record is malformed");
			return false;
		}
		Out.Outputs.Add(MoveTemp(Output));
	}
	Out.Outputs.Sort(
		[](const FBlueprintLensLC4SequenceOutput& Left,
		   const FBlueprintLensLC4SequenceOutput& Right)
		{
			return Left.Ordinal < Right.Ordinal;
		});

	if (Reconvergences->Num() != 1)
	{
		Error = TEXT("exactly one ordinary reconvergence record is required");
		return false;
	}
	const TSharedPtr<FJsonObject> Reconvergence =
		(*Reconvergences)[0].IsValid()
		? (*Reconvergences)[0]->AsObject()
		: nullptr;
	TSharedPtr<FJsonObject> ReconvergenceTermination;
	FString ReconvergenceKind;
	FString ReconvergenceTerminationKind;
	if (!RequiredString(Reconvergence, TEXT("kind"), ReconvergenceKind) ||
		ReconvergenceKind != TEXT("ordinary_multi_predecessor") ||
		!RequiredString(
			Reconvergence,
			TEXT("node_id"),
			Out.Reconvergence.NodeId) ||
		!IntegerArray(
			Reconvergence,
			TEXT("incoming_output_ordinals"),
			Out.Reconvergence.IncomingOutputOrdinals) ||
		!StringArray(
			Reconvergence,
			TEXT("shared_reachable_node_ids"),
			Out.Reconvergence.SharedReachableNodeIds) ||
		!StringArray(
			Reconvergence,
			TEXT("shared_reachable_edge_ids"),
			Out.Reconvergence.SharedReachableEdgeIds) ||
		!RequiredObject(
			Reconvergence,
			TEXT("termination"),
			ReconvergenceTermination) ||
		!RequiredString(
			ReconvergenceTermination,
			TEXT("kind"),
			ReconvergenceTerminationKind) ||
		ReconvergenceTerminationKind != TEXT("criterion") ||
		!RequiredString(
			ReconvergenceTermination,
			TEXT("node_id"),
			Out.Reconvergence.CriterionNodeId))
	{
		Error = TEXT("ordinary reconvergence record is malformed");
		return false;
	}

	return RequiredInteger(
			Counts,
			TEXT("declared_output_count"),
			Out.Counts.DeclaredOutputs) &&
		RequiredInteger(
			Counts,
			TEXT("connected_output_count"),
			Out.Counts.ConnectedOutputs) &&
		RequiredInteger(
			Counts,
			TEXT("unconnected_output_count"),
			Out.Counts.UnconnectedOutputs) &&
		RequiredInteger(
			Counts,
			TEXT("criterion_included_output_count"),
			Out.Counts.CriterionIncludedOutputs) &&
		RequiredInteger(
			Counts,
			TEXT("outside_criterion_connected_output_count"),
			Out.Counts.OutsideCriterionConnectedOutputs) &&
		RequiredInteger(
			Counts,
			TEXT("indeterminate_output_count"),
			Out.Counts.IndeterminateOutputs);
}

bool LoadIrFacts(
	const FBlueprintLensLC4SequenceProfile& Profile,
	TMap<FString, FLC4IrNode>& OutNodes,
	TMap<FString, FLC4IrEdge>& OutEdges,
	FString& Error)
{
	FString ActualIrHash;
	if (!HashFile(Profile.Source.IrPath, ActualIrHash) ||
		!ActualIrHash.Equals(
			Profile.Source.IrSha256,
			ESearchCase::IgnoreCase))
	{
		Error = TEXT("typed-IR companion is unreadable or hash-mismatched");
		return false;
	}
	FString ActualSliceHash;
	if (!HashFile(Profile.Source.SlicePath, ActualSliceHash) ||
		!ActualSliceHash.Equals(
			Profile.Source.SliceSha256,
			ESearchCase::IgnoreCase))
	{
		Error = TEXT("slice companion is unreadable or hash-mismatched");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!ParseJsonFile(Profile.Source.IrPath, Root, Error))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Blueprint;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	FString BlueprintPath;
	if (!RequiredObject(Root, TEXT("blueprint"), Blueprint) ||
		!RequiredString(Blueprint, TEXT("path"), BlueprintPath) ||
		BlueprintPath != Profile.Source.BlueprintAssetPath ||
		!RequiredArray(Blueprint, TEXT("graphs"), Graphs))
	{
		Error = TEXT("typed-IR Blueprint binding is malformed");
		return false;
	}

	TSharedPtr<FJsonObject> Graph;
	for (const TSharedPtr<FJsonValue>& Value : *Graphs)
	{
		const TSharedPtr<FJsonObject> Candidate = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		FString CandidateId;
		if (RequiredString(Candidate, TEXT("id"), CandidateId) &&
			CandidateId == Profile.Source.GraphId)
		{
			Graph = Candidate;
			break;
		}
	}
	if (!Graph.IsValid())
	{
		Error = TEXT("typed-IR graph binding is unavailable");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (!RequiredArray(Graph, TEXT("nodes"), Nodes) ||
		!RequiredArray(Graph, TEXT("edges"), Edges))
	{
		Error = TEXT("typed-IR graph ledger is malformed");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		FLC4IrNode Node;
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!RequiredString(Object, TEXT("id"), Node.Id) ||
			!RequiredString(Object, TEXT("title"), Node.Title) ||
			!RequiredString(Object, TEXT("native_guid"), Node.NativeGuid) ||
			!RequiredArray(Object, TEXT("pins"), Pins) ||
			OutNodes.Contains(Node.Id))
		{
			Error = TEXT("typed-IR node ledger is malformed");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
		{
			const TSharedPtr<FJsonObject> Pin = PinValue.IsValid()
				? PinValue->AsObject()
				: nullptr;
			FString PinId;
			FString PinName;
			if (!RequiredString(Pin, TEXT("id"), PinId) ||
				!RequiredString(Pin, TEXT("name"), PinName) ||
				Node.PinLabels.Contains(PinId))
			{
				Error = TEXT("typed-IR pin ledger is malformed");
				return false;
			}
			Node.PinLabels.Add(MoveTemp(PinId), MoveTemp(PinName));
		}
		OutNodes.Add(Node.Id, MoveTemp(Node));
	}

	for (const TSharedPtr<FJsonValue>& Value : *Edges)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		FLC4IrEdge Edge;
		FString Kind;
		if (!RequiredString(Object, TEXT("id"), Edge.Id) ||
			!RequiredString(Object, TEXT("kind"), Kind) ||
			Kind != TEXT("execution") ||
			!RequiredString(
				Object,
				TEXT("source_node_id"),
				Edge.SourceNodeId) ||
			!RequiredString(
				Object,
				TEXT("source_pin_id"),
				Edge.SourcePinId) ||
			!RequiredString(
				Object,
				TEXT("target_node_id"),
				Edge.TargetNodeId) ||
			!RequiredString(
				Object,
				TEXT("target_pin_id"),
				Edge.TargetPinId) ||
			OutEdges.Contains(Edge.Id))
		{
			Error = TEXT("typed-IR execution-edge ledger is malformed");
			return false;
		}
		OutEdges.Add(Edge.Id, MoveTemp(Edge));
	}
	return true;
}

void AddUnique(TArray<FString>& Values, const FString& Value)
{
	if (!Value.IsEmpty() && !Values.Contains(Value))
	{
		Values.Add(Value);
	}
}

bool ValidateAndAccount(
	FBlueprintLensLC4SequenceProfile& Profile,
	const TMap<FString, FLC4IrNode>& Nodes,
	const TMap<FString, FLC4IrEdge>& Edges,
	FString& Error)
{
	int32 Connected = 0;
	int32 Unconnected = 0;
	int32 Included = 0;
	int32 OutsideConnected = 0;
	int32 Indeterminate = 0;
	AddUnique(Profile.AccountedUnitIds, Profile.Source.SequenceNodeId);

	for (int32 Index = 0; Index < Profile.Outputs.Num(); ++Index)
	{
		const FBlueprintLensLC4SequenceOutput& Output = Profile.Outputs[Index];
		if (Output.Ordinal != Index || !Nodes.Contains(Output.TerminationNodeId))
		{
			Error = TEXT("output ordinals or termination binding are invalid");
			return false;
		}
		const FLC4IrNode* SequenceNode =
			Nodes.Find(Profile.Source.SequenceNodeId);
		const FString* PinName = SequenceNode != nullptr
			? SequenceNode->PinLabels.Find(Output.SourcePinId)
			: nullptr;
		if (PinName == nullptr || *PinName != Output.SourcePinName)
		{
			Error = TEXT("output source pin does not match typed IR");
			return false;
		}

		if (Output.ConnectionState ==
			EBlueprintLensLC4ConnectionState::Connected)
		{
			++Connected;
			if (Output.ConnectedEdgeIds.Num() != 1 ||
				Output.ReachableNodeIds.IsEmpty() ||
				Output.ReachableEdgeIds.IsEmpty())
			{
				Error = TEXT("connected output has incomplete route evidence");
				return false;
			}
			const FLC4IrEdge* FirstEdge =
				Edges.Find(Output.ConnectedEdgeIds[0]);
			if (FirstEdge == nullptr ||
				FirstEdge->SourceNodeId != Profile.Source.SequenceNodeId ||
				FirstEdge->SourcePinId != Output.SourcePinId ||
				!Output.ReachableEdgeIds.Contains(FirstEdge->Id))
			{
				Error = TEXT("connected output edge is not owned by its source pin");
				return false;
			}
		}
		else
		{
			++Unconnected;
			if (!Output.ConnectedEdgeIds.IsEmpty() ||
				!Output.ReachableNodeIds.IsEmpty() ||
				!Output.ReachableEdgeIds.IsEmpty() ||
				Output.TerminationKind !=
					EBlueprintLensLC4TerminationKind::Unconnected)
			{
				Error = TEXT("unconnected output carries forged route evidence");
				return false;
			}
		}

		if (Output.CriterionRelation ==
			EBlueprintLensLC4CriterionRelation::Included)
		{
			++Included;
			if (Output.TerminationKind !=
					EBlueprintLensLC4TerminationKind::OrdinaryReconvergence ||
				Output.TerminationNodeId != Profile.Reconvergence.NodeId)
			{
				Error = TEXT("criterion-included output lacks ordinary reconvergence");
				return false;
			}
		}
		else if (Output.CriterionRelation ==
				 EBlueprintLensLC4CriterionRelation::Indeterminate)
		{
			++Indeterminate;
		}
		else if (Output.ConnectionState ==
				 EBlueprintLensLC4ConnectionState::Connected)
		{
			++OutsideConnected;
		}

		for (const FString& NodeId : Output.ReachableNodeIds)
		{
			if (!Nodes.Contains(NodeId))
			{
				Error = TEXT("output route references an unknown typed-IR node");
				return false;
			}
			AddUnique(Profile.AccountedUnitIds, NodeId);
		}
		for (const FString& EdgeId : Output.ReachableEdgeIds)
		{
			if (!Edges.Contains(EdgeId))
			{
				Error = TEXT("output route references an unknown typed-IR edge");
				return false;
			}
			AddUnique(Profile.AccountedRelationIds, EdgeId);
		}
	}

	if (Profile.Counts.DeclaredOutputs != Profile.Outputs.Num() ||
		Profile.Counts.ConnectedOutputs != Connected ||
		Profile.Counts.UnconnectedOutputs != Unconnected ||
		Profile.Counts.CriterionIncludedOutputs != Included ||
		Profile.Counts.OutsideCriterionConnectedOutputs != OutsideConnected ||
		Profile.Counts.IndeterminateOutputs != Indeterminate)
	{
		Error = TEXT("six output counts do not reconcile with output records");
		return false;
	}

	if (Profile.Reconvergence.IncomingOutputOrdinals != TArray<int32>({0, 1}) ||
		Profile.Reconvergence.NodeId.IsEmpty() ||
		Profile.Reconvergence.CriterionNodeId !=
			Profile.Source.CriterionNodeId ||
		Profile.Reconvergence.SharedReachableNodeIds.Num() != 2 ||
		Profile.Reconvergence.SharedReachableNodeIds[0] !=
			Profile.Reconvergence.NodeId ||
		Profile.Reconvergence.SharedReachableNodeIds[1] !=
			Profile.Source.CriterionNodeId ||
		Profile.Reconvergence.SharedReachableEdgeIds.Num() != 1)
	{
		Error = TEXT("ordinary reconvergence ownership is invalid");
		return false;
	}
	for (const FString& NodeId : Profile.Reconvergence.SharedReachableNodeIds)
	{
		if (!Nodes.Contains(NodeId))
		{
			Error = TEXT("shared suffix references an unknown typed-IR node");
			return false;
		}
		AddUnique(Profile.AccountedUnitIds, NodeId);
	}
	for (const FString& EdgeId : Profile.Reconvergence.SharedReachableEdgeIds)
	{
		if (!Edges.Contains(EdgeId))
		{
			Error = TEXT("shared suffix references an unknown typed-IR edge");
			return false;
		}
		AddUnique(Profile.AccountedRelationIds, EdgeId);
	}
	if (Profile.AccountedUnitIds.Num() != 8 ||
		Profile.AccountedRelationIds.Num() != 8)
	{
		Error = TEXT("LC4 reader projection must account for 8 units and 8 relations");
		return false;
	}
	return true;
}

EBlueprintLensRole RoleForNode(
	const FBlueprintLensLC4SequenceProfile& Profile,
	const FString& NodeId)
{
	if (NodeId == Profile.Source.CriterionNodeId)
	{
		return EBlueprintLensRole::Criterion;
	}
	if (NodeId == Profile.Outputs[2].TerminationNodeId)
	{
		return EBlueprintLensRole::Boundary;
	}
	if (NodeId == Profile.Source.SequenceNodeId)
	{
		return EBlueprintLensRole::Control;
	}
	return EBlueprintLensRole::Consequence;
}

TSharedPtr<const FBlueprintLensExplanationModel> BuildExplanationAdapter(
	const FBlueprintLensLC4SequenceProfile& Profile,
	const TMap<FString, FLC4IrNode>& Nodes,
	const TMap<FString, FLC4IrEdge>& Edges)
{
	TSharedRef<FBlueprintLensExplanationModel> Model =
		MakeShared<FBlueprintLensExplanationModel>();
	Model->Format = TEXT("blueprint-lens-explanation");
	Model->SchemaVersion = TEXT("1.0.0");
	Model->RulesVersion = Profile.RulesVersion;
	Model->Source.IrPath = Profile.Source.IrPath;
	Model->Source.IrSha256 = Profile.Source.IrSha256;
	Model->Source.SlicePath = Profile.Source.SlicePath;
	Model->Source.SliceSha256 = Profile.Source.SliceSha256;
	Model->Source.BlueprintAssetPath = Profile.Source.BlueprintAssetPath;
	Model->Source.BlueprintPackageSha256 =
		Profile.Source.BlueprintPackageSha256;
	Model->Source.GraphId = Profile.Source.GraphId;
	Model->Query.Question = TEXT(
		"What does this Sequence trigger, in what pin order, and which outputs "
		"reach Set LC4Complete?");
	Model->Query.Direction = TEXT("sequence_fanout_overview");
	Model->Query.CriterionSourceNodeId = Profile.Source.CriterionNodeId;
	Model->CriterionUnitId = Profile.Source.CriterionNodeId;

	TMap<FString, TArray<FString>> RelevantPins;
	for (const FString& EdgeId : Profile.AccountedRelationIds)
	{
		const FLC4IrEdge& Edge = Edges[EdgeId];
		RelevantPins.FindOrAdd(Edge.SourceNodeId).AddUnique(Edge.SourcePinId);
		RelevantPins.FindOrAdd(Edge.TargetNodeId).AddUnique(Edge.TargetPinId);
	}
	for (const FBlueprintLensLC4SequenceOutput& Output : Profile.Outputs)
	{
		RelevantPins.FindOrAdd(Profile.Source.SequenceNodeId)
			.AddUnique(Output.SourcePinId);
	}

	for (const FString& NodeId : Profile.AccountedUnitIds)
	{
		const FLC4IrNode& Node = Nodes[NodeId];
		FBlueprintLensUnit Unit;
		Unit.Id = NodeId;
		Unit.Role = RoleForNode(Profile, NodeId);
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Title = Node.Title;
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons.Add(
			Unit.Role == EBlueprintLensRole::Boundary
				? TEXT("connected_output_outside_criterion")
				: Unit.Role == EBlueprintLensRole::Criterion
					? TEXT("criterion")
					: TEXT("sequence_profile_route"));
		FBlueprintLensSourceReference Reference;
		Reference.BlueprintAssetPath = Profile.Source.BlueprintAssetPath;
		Reference.GraphId = Profile.Source.GraphId;
		Reference.SourceNodeId = NodeId;
		Reference.NativeNodeGuid = Node.NativeGuid;
		Reference.SourcePinIds = RelevantPins.FindRef(NodeId);
		Reference.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Reference));
		Model->Units.Add(MoveTemp(Unit));
	}

	for (const FString& EdgeId : Profile.AccountedRelationIds)
	{
		const FLC4IrEdge& Edge = Edges[EdgeId];
		const FString SourcePort = Nodes[Edge.SourceNodeId].PinLabels[Edge.SourcePinId];
		const FString TargetPort = Nodes[Edge.TargetNodeId].PinLabels[Edge.TargetPinId];
		FBlueprintLensRelation Relation;
		Relation.Id = EdgeId;
		Relation.SourceUnitId = Edge.SourceNodeId;
		Relation.TargetUnitId = Edge.TargetNodeId;
		Relation.Kind = EBlueprintLensRelationKind::ExecutionPredecessor;
		Relation.Label = SourcePort;
		Relation.SourceEdgeIds.Add(EdgeId);
		Relation.bHasSourceEdgeEndpoints = true;
		FBlueprintLensSourceEdgeEndpoint Endpoint;
		Endpoint.SourceEdgeId = EdgeId;
		Endpoint.SourceNodeId = Edge.SourceNodeId;
		Endpoint.SourcePinId = Edge.SourcePinId;
		Endpoint.SourcePortLabel = SourcePort;
		Endpoint.TargetNodeId = Edge.TargetNodeId;
		Endpoint.TargetPinId = Edge.TargetPinId;
		Endpoint.TargetPortLabel = TargetPort;
		Relation.SourceEdgeEndpoints.Add(MoveTemp(Endpoint));
		Relation.bHasPortLabel = true;
		Relation.PortLabel = SourcePort;
		Relation.bHasSemanticLabel = true;
		Relation.SemanticLabel = EBlueprintLensSemanticLabel::NextExecution;
		Model->Relations.Add(MoveTemp(Relation));
	}

	const auto AddLane =
		[&Model](
			const EBlueprintLensRole Role,
			const FString& EmptyMessage)
		{
			FBlueprintLensLane Lane;
			Lane.Role = Role;
			for (const FBlueprintLensUnit& Unit : Model->Units)
			{
				if (Unit.Role == Role)
				{
					Lane.UnitIds.Add(Unit.Id);
				}
			}
			Lane.State = Lane.UnitIds.IsEmpty()
				? EBlueprintLensLaneState::Empty
				: EBlueprintLensLaneState::Populated;
			Lane.EmptyMessage = Lane.UnitIds.IsEmpty() ? EmptyMessage : FString();
			Model->Lanes.Add(MoveTemp(Lane));
		};
	AddLane(EBlueprintLensRole::Criterion, TEXT("Criterion is unavailable"));
	AddLane(EBlueprintLensRole::Control, TEXT("No Sequence control source"));
	AddLane(EBlueprintLensRole::Predicate, TEXT("No predicate facts in this overview"));
	AddLane(EBlueprintLensRole::Value, TEXT("No value facts in this overview"));
	AddLane(EBlueprintLensRole::Consequence, TEXT("No connected route facts"));
	AddLane(EBlueprintLensRole::Boundary, TEXT("No outside-criterion route"));
	Model->Counts.Lanes = Model->Lanes.Num();
	Model->Counts.Units = Model->Units.Num();
	Model->Counts.Relations = Model->Relations.Num();
	Model->Counts.SourceNodes = Model->Units.Num();
	Model->Counts.SourceEdges = Model->Relations.Num();
	return Model;
}
} // namespace

const TCHAR* LexToString(const EBlueprintLensLC4ConnectionState Value)
{
	return Value == EBlueprintLensLC4ConnectionState::Connected
		? TEXT("connected")
		: TEXT("unconnected");
}

const TCHAR* LexToString(const EBlueprintLensLC4CriterionRelation Value)
{
	switch (Value)
	{
	case EBlueprintLensLC4CriterionRelation::Included:
		return TEXT("included");
	case EBlueprintLensLC4CriterionRelation::Outside:
		return TEXT("outside");
	default:
		return TEXT("indeterminate");
	}
}

const TCHAR* LexToString(const EBlueprintLensLC4TerminationKind Value)
{
	switch (Value)
	{
	case EBlueprintLensLC4TerminationKind::OrdinaryReconvergence:
		return TEXT("ordinary reconvergence");
	case EBlueprintLensLC4TerminationKind::Terminal:
		return TEXT("terminal");
	default:
		return TEXT("unconnected");
	}
}

bool FBlueprintLensLC4SequenceProfile::IsValid() const
{
	if (bLiveExplanation)
	{
		return IsLiveBounded();
	}
	return Format == LC4Format && SchemaVersion == LC4SchemaVersion &&
		ProfileId == LC4ProfileId && RulesVersion == LC4RulesVersion &&
		QueryMode == LC4QueryMode && !ProfilePath.IsEmpty() &&
		!ProfileSha256.IsEmpty() && Outputs.Num() == 4 &&
		Reconvergence.IncomingOutputOrdinals == TArray<int32>({0, 1}) &&
		Counts.DeclaredOutputs == 4 && Counts.ConnectedOutputs == 3 &&
		Counts.UnconnectedOutputs == 1 && Counts.CriterionIncludedOutputs == 2 &&
		Counts.OutsideCriterionConnectedOutputs == 1 &&
		Counts.IndeterminateOutputs == 0 && AccountedUnitIds.Num() == 8 &&
		AccountedRelationIds.Num() == 8;
}

bool FBlueprintLensLC4SequenceProfile::IsLiveBounded() const
{
	if (!bLiveExplanation || Format != LC4Format ||
		SchemaVersion != LC4SchemaVersion || ProfileId != LC4ProfileId ||
		RulesVersion != LC4RulesVersion || QueryMode != LC4QueryMode ||
		ProfilePath.IsEmpty() || ProfileSha256.IsEmpty() ||
		Source.BlueprintAssetPath.IsEmpty() || Source.GraphId.IsEmpty() ||
		Source.SequenceNodeId.IsEmpty() || Source.CriterionNodeId.IsEmpty() ||
		Source.IrSha256.IsEmpty() || Outputs.IsEmpty() || Outputs.Num() > 4 ||
		Counts.DeclaredOutputs != Outputs.Num() ||
		Counts.ConnectedOutputs + Counts.UnconnectedOutputs != Outputs.Num() ||
		Counts.CriterionIncludedOutputs +
			Counts.OutsideCriterionConnectedOutputs !=
			Counts.ConnectedOutputs ||
		Counts.IndeterminateOutputs != 0 || AccountedUnitIds.IsEmpty())
	{
		return false;
	}
	int32 PreviousOrdinal = INDEX_NONE;
	for (const FBlueprintLensLC4SequenceOutput& Output : Outputs)
	{
		if (Output.Ordinal < 0 || Output.Ordinal <= PreviousOrdinal ||
			Output.SourcePinId.IsEmpty() || Output.SourcePinName.IsEmpty())
		{
			return false;
		}
		PreviousOrdinal = Output.Ordinal;
	}
	return true;
}

FBlueprintLensLC4SequenceLoadResult
FBlueprintLensLC4SequenceProfileLoader::LoadFile(const FString& ProfilePath)
{
	TSharedPtr<FJsonObject> Root;
	FString Error;
	if (!ParseJsonFile(ProfilePath, Root, Error))
	{
		return Failure(TEXT("LC4_SEQUENCE_PROFILE_UNREADABLE"), Error);
	}
	TSharedRef<FBlueprintLensLC4SequenceProfile> Profile =
		MakeShared<FBlueprintLensLC4SequenceProfile>();
	if (!ParseProfile(Root, ProfilePath, *Profile, Error))
	{
		return Failure(TEXT("LC4_SEQUENCE_PROFILE_MALFORMED"), Error);
	}
	if (!HashFile(Profile->ProfilePath, Profile->ProfileSha256))
	{
		return Failure(TEXT("LC4_SEQUENCE_PROFILE_HASH_UNAVAILABLE"));
	}
	TMap<FString, FLC4IrNode> Nodes;
	TMap<FString, FLC4IrEdge> Edges;
	if (!LoadIrFacts(*Profile, Nodes, Edges, Error))
	{
		return Failure(TEXT("LC4_SEQUENCE_PROFILE_SOURCE_MISMATCH"), Error);
	}
	if (!ValidateAndAccount(*Profile, Nodes, Edges, Error) ||
		!Profile->IsValid())
	{
		return Failure(TEXT("LC4_SEQUENCE_PROFILE_INVARIANT_FAILED"), Error);
	}

	FBlueprintLensLC4SequenceLoadResult Result;
	Result.ExplanationModel = BuildExplanationAdapter(*Profile, Nodes, Edges);
	Result.Profile = Profile;
	return Result;
}
