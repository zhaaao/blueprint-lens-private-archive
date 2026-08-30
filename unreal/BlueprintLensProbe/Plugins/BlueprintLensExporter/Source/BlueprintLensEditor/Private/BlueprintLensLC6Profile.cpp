#include "BlueprintLensLC6Profile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR CoreFormat[] = TEXT("blueprint-lens-lc6-boundary-matrix");
constexpr TCHAR QueryFormat[] = TEXT("blueprint-lens-lc6-upstream-budget");
constexpr TCHAR ReadinessFormat[] = TEXT("blueprint-lens-lc6-readiness");
constexpr TCHAR RawFormat[] = TEXT("blueprint-lens-raw-probe");
constexpr TCHAR ContractVersion[] = TEXT("1.0.0");
constexpr TCHAR CoreProfileId[] = TEXT("LC6_CORE_BOUNDARY_MATRIX_V1");
constexpr TCHAR QueryProfileId[] = TEXT("LC6_MAX_UPSTREAM_HOPS_V1");
constexpr TCHAR BlueprintAssetPath[] =
	TEXT("/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix");
constexpr TCHAR GraphId[] =
	TEXT("/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix:EventGraph");
constexpr TCHAR AssetSha256[] =
	TEXT("cd98bc0c2337948158cb6c54fdd76059f6068a593ae86b4f4c9a2d5214a18ac7");
constexpr TCHAR RawSha256[] =
	TEXT("86614b0e5d51d84be55b09f5ca5a41c002a00ecbac1e1951960a93702428bd10");
constexpr TCHAR SchemaGateCommit[] = TEXT("7efeac56a7836e6e262b7b219b80c12a3a272a32");

FBlueprintLensLC6LoadResult Failure(const TCHAR* Code, const FString& Detail = FString())
{
	FBlueprintLensLC6LoadResult Result;
	Result.Error = Detail.IsEmpty() ? FString(Code) :
		FString::Printf(TEXT("%s: %s"), Code, *Detail);
	return Result;
}

bool ParseJson(const FString& Path, TSharedPtr<FJsonObject>& Root)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) &&
		Root.IsValid();
}

bool HashFile(const FString& Path, FString& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return false;
	}
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
	{
		return false;
	}
	Out = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

bool StringField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& Out,
	const bool bAllowEmpty = false)
{
	return Object.IsValid() && Object->TryGetStringField(Field, Out) &&
		(bAllowEmpty || !Out.IsEmpty());
}

bool IntField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32& Out)
{
	double Number = 0.0;
	if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number) ||
		!FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
	{
		return false;
	}
	Out = FMath::RoundToInt(Number);
	return Out >= 0;
}

bool ObjectField(
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

bool ArrayField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	const TArray<TSharedPtr<FJsonValue>>*& Out)
{
	return Object.IsValid() && Object->TryGetArrayField(Field, Out) && Out != nullptr;
}

bool JsonObjectAt(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	Out = Value->AsObject();
	return Out.IsValid();
}

bool StringArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TArray<FString>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!ArrayField(Object, Field, Values))
	{
		return false;
	}
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Item;
		if (!Value.IsValid() || !Value->TryGetString(Item) || Item.IsEmpty() ||
			Seen.Contains(Item))
		{
			return false;
		}
		Seen.Add(Item);
		Out.Add(MoveTemp(Item));
	}
	return true;
}

bool SameSet(const TArray<FString>& Left, const TArray<FString>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	TSet<FString> LeftSet;
	TSet<FString> RightSet;
	LeftSet.Append(Left);
	RightSet.Append(Right);
	if (LeftSet.Num() != Left.Num() || RightSet.Num() != Right.Num())
	{
		return false;
	}
	for (const FString& Item : LeftSet)
	{
		if (!RightSet.Contains(Item))
		{
			return false;
		}
	}
	return true;
}

const FBlueprintLensLC6SourceEdge* FindEdge(
	const TArray<FBlueprintLensLC6SourceEdge>& Edges,
	const FString& EdgeId)
{
	return Edges.FindByPredicate([&EdgeId](const FBlueprintLensLC6SourceEdge& Edge)
	{
		return Edge.EdgeId == EdgeId;
	});
}

bool ParseRaw(
	const TSharedPtr<FJsonObject>& Root,
	FBlueprintLensLC6Profile& Profile,
	TSet<FString>& PinIds,
	FString& Detail)
{
	FString Format;
	FString Version;
	TSharedPtr<FJsonObject> Blueprint;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!StringField(Root, TEXT("format"), Format) || Format != RawFormat ||
		!StringField(Root, TEXT("format_version"), Version) || Version != TEXT("0.2") ||
		!ObjectField(Root, TEXT("blueprint"), Blueprint) ||
		!StringField(Blueprint, TEXT("path"), Format) || Format != BlueprintAssetPath ||
		!ArrayField(Blueprint, TEXT("graphs"), Graphs))
	{
		Detail = TEXT("raw identity");
		return false;
	}

	int32 MatchingGraphs = 0;
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		TSharedPtr<FJsonObject> Graph;
		FString Id;
		if (!JsonObjectAt(GraphValue, Graph) || !StringField(Graph, TEXT("id"), Id))
		{
			Detail = TEXT("raw graph");
			return false;
		}
		if (Id != GraphId)
		{
			continue;
		}
		++MatchingGraphs;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
		if (!ArrayField(Graph, TEXT("nodes"), Nodes) ||
			!ArrayField(Graph, TEXT("edges"), Edges))
		{
			Detail = TEXT("raw graph arrays");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			TSharedPtr<FJsonObject> Node;
			FString NodeId;
			FString Title;
			FString Status;
			if (!JsonObjectAt(NodeValue, Node) ||
				!StringField(Node, TEXT("id"), NodeId) ||
				!StringField(Node, TEXT("title"), Title) ||
				!StringField(Node, TEXT("semantic_status"), Status) ||
				Profile.SourceTitles.Contains(NodeId) || Status == TEXT("truncated"))
			{
				Detail = TEXT("raw node identity/title/status");
				return false;
			}
			Profile.SourceTitles.Add(NodeId, Title);
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!ArrayField(Node, TEXT("pins"), Pins))
			{
				Detail = TEXT("raw node pins");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
			{
				TSharedPtr<FJsonObject> Pin;
				FString PinId;
				FString OwningNodeId;
				if (!JsonObjectAt(PinValue, Pin) ||
					!StringField(Pin, TEXT("id"), PinId) ||
					!StringField(Pin, TEXT("node_id"), OwningNodeId) ||
					OwningNodeId != NodeId || PinIds.Contains(PinId))
				{
					Detail = TEXT("raw pin identity");
					return false;
				}
				PinIds.Add(PinId);
			}
		}
		for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
		{
			TSharedPtr<FJsonObject> Edge;
			FBlueprintLensLC6SourceEdge Item;
			if (!JsonObjectAt(EdgeValue, Edge) ||
				!StringField(Edge, TEXT("id"), Item.EdgeId) ||
				!StringField(Edge, TEXT("kind"), Item.Kind) ||
				!StringField(Edge, TEXT("source_node_id"), Item.SourceNodeId) ||
				!StringField(Edge, TEXT("target_node_id"), Item.TargetNodeId) ||
				FindEdge(Profile.SourceEdges, Item.EdgeId) != nullptr ||
				!Profile.SourceTitles.Contains(Item.SourceNodeId) ||
				!Profile.SourceTitles.Contains(Item.TargetNodeId))
			{
				Detail = TEXT("raw edge identity/endpoints");
				return false;
			}
			Profile.SourceEdges.Add(MoveTemp(Item));
		}
	}
	if (MatchingGraphs != 1)
	{
		Detail = TEXT("raw EventGraph is not unique");
		return false;
	}
	return true;
}

bool ParseCoreScenario(
	const TSharedPtr<FJsonObject>& Object,
	FBlueprintLensLC6Scenario& Out)
{
	TSharedPtr<FJsonObject> Stop;
	return StringField(Object, TEXT("scenario_id"), Out.ScenarioId) &&
		StringField(Object, TEXT("root_node_id"), Out.RootNodeId) &&
		StringField(Object, TEXT("criterion_node_id"), Out.CriterionNodeId) &&
		StringField(Object, TEXT("boundary_node_id"), Out.BoundaryNodeId) &&
		StringField(Object, TEXT("status"), Out.Status) &&
		StringField(Object, TEXT("reason"), Out.Reason) &&
		StringArray(Object, TEXT("slice_node_ids"), Out.SliceNodeIds) &&
		StringArray(Object, TEXT("slice_edge_ids"), Out.SliceEdgeIds) &&
		StringArray(Object, TEXT("incident_edge_ids"), Out.IncidentEdgeIds) &&
		StringArray(Object, TEXT("source_pin_ids"), Out.SourcePinIds) &&
		ObjectField(Object, TEXT("stop_location"), Stop) &&
		StringField(Stop, TEXT("kind"), Out.StopKind) &&
		StringField(Stop, TEXT("node_id"), Out.StopNodeId);
}

bool ParseQueryScenario(
	const TSharedPtr<FJsonObject>& Root,
	FBlueprintLensLC6Scenario& Out)
{
	TSharedPtr<FJsonObject> Counts;
	if (!StringField(Root, TEXT("scenario_id"), Out.ScenarioId) ||
		!StringField(Root, TEXT("root_node_id"), Out.RootNodeId) ||
		!StringField(Root, TEXT("criterion_node_id"), Out.CriterionNodeId) ||
		!StringField(Root, TEXT("status"), Out.Status) ||
		!StringField(Root, TEXT("reason"), Out.Reason) ||
		!IntField(Root, TEXT("max_upstream_hops"), Out.MaxUpstreamHops) ||
		!StringArray(Root, TEXT("selected_node_ids"), Out.SliceNodeIds) ||
		!StringArray(Root, TEXT("selected_edge_ids"), Out.SliceEdgeIds) ||
		!StringArray(Root, TEXT("complete_node_ids"), Out.CompleteNodeIds) ||
		!StringArray(Root, TEXT("complete_edge_ids"), Out.CompleteEdgeIds) ||
		!ObjectField(Root, TEXT("counts"), Counts) ||
		!IntField(Counts, TEXT("omitted_nodes"), Out.OmittedNodeCount) ||
		!IntField(Counts, TEXT("omitted_edges"), Out.OmittedEdgeCount))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Hops = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Frontiers = nullptr;
	if (!ArrayField(Root, TEXT("hop_distances"), Hops) ||
		!ArrayField(Root, TEXT("frontiers"), Frontiers))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Hops)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC6HopDistance Item;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("node_id"), Item.NodeId) ||
			!IntField(Object, TEXT("distance"), Item.Distance))
		{
			return false;
		}
		Out.HopDistances.Add(MoveTemp(Item));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Frontiers)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC6Frontier Item;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("edge_id"), Item.EdgeId) ||
			!StringField(Object, TEXT("source_node_id"), Item.SourceNodeId) ||
			!StringField(Object, TEXT("target_node_id"), Item.TargetNodeId))
		{
			return false;
		}
		Out.Frontiers.Add(MoveTemp(Item));
	}

	int32 CompleteNodes = 0;
	int32 CompleteEdges = 0;
	int32 SelectedNodes = 0;
	int32 SelectedEdges = 0;
	int32 FrontierCount = 0;
	return IntField(Counts, TEXT("complete_nodes"), CompleteNodes) &&
		IntField(Counts, TEXT("complete_edges"), CompleteEdges) &&
		IntField(Counts, TEXT("selected_nodes"), SelectedNodes) &&
		IntField(Counts, TEXT("selected_edges"), SelectedEdges) &&
		IntField(Counts, TEXT("frontiers"), FrontierCount) &&
		CompleteNodes == Out.CompleteNodeIds.Num() &&
		CompleteEdges == Out.CompleteEdgeIds.Num() &&
		SelectedNodes == Out.SliceNodeIds.Num() &&
		SelectedEdges == Out.SliceEdgeIds.Num() &&
		FrontierCount == Out.Frontiers.Num();
}

bool ValidateCoreScenario(
	const FBlueprintLensLC6Scenario& Scenario,
	const FBlueprintLensLC6Profile& Profile,
	const TSet<FString>& PinIds)
{
	FString ExpectedStatus;
	FString ExpectedReason;
	int32 ExpectedNodes = 0;
	int32 ExpectedEdges = 0;
	int32 ExpectedIncidentEdges = 0;
	bool bRootBelongsToSlice = false;
	if (Scenario.ScenarioId == TEXT("LC6_OPAQUE"))
	{
		ExpectedStatus = TEXT("opaque");
		ExpectedReason = TEXT("function_body_not_expanded");
		ExpectedNodes = 2;
		ExpectedEdges = 1;
		ExpectedIncidentEdges = 2;
	}
	else if (Scenario.ScenarioId == TEXT("LC6_UNCERTAIN"))
	{
		ExpectedStatus = TEXT("uncertain");
		ExpectedReason = TEXT("node_family_not_in_supported_matrix_v1");
		ExpectedNodes = 3;
		ExpectedEdges = 2;
		ExpectedIncidentEdges = 1;
		bRootBelongsToSlice = true;
	}
	else if (Scenario.ScenarioId == TEXT("LC6_UNSUPPORTED"))
	{
		ExpectedStatus = TEXT("unsupported");
		ExpectedReason = TEXT("latent_function");
		ExpectedNodes = 2;
		ExpectedEdges = 1;
		ExpectedIncidentEdges = 2;
	}
	else
	{
		return false;
	}
	if (Scenario.TruthOwner != TEXT("core_node_classification") ||
		Scenario.Status != ExpectedStatus || Scenario.Reason != ExpectedReason ||
		Scenario.StopKind != TEXT("semantic_boundary") ||
		Scenario.StopNodeId != Scenario.BoundaryNodeId ||
		Scenario.SliceNodeIds.Num() != ExpectedNodes ||
		Scenario.SliceEdgeIds.Num() != ExpectedEdges ||
		Scenario.IncidentEdgeIds.Num() != ExpectedIncidentEdges ||
		!Scenario.SliceNodeIds.Contains(Scenario.BoundaryNodeId) ||
		!Scenario.SliceNodeIds.Contains(Scenario.CriterionNodeId) ||
		Scenario.SliceNodeIds.Contains(Scenario.RootNodeId) != bRootBelongsToSlice ||
		!Profile.SourceTitles.Contains(Scenario.RootNodeId) ||
		!Profile.SourceTitles.Contains(Scenario.BoundaryNodeId) ||
		!Profile.SourceTitles.Contains(Scenario.CriterionNodeId))
	{
		return false;
	}
	for (const FString& EdgeId : Scenario.SliceEdgeIds)
	{
		const FBlueprintLensLC6SourceEdge* Edge = FindEdge(Profile.SourceEdges, EdgeId);
		if (Edge == nullptr ||
			!Scenario.SliceNodeIds.Contains(Edge->SourceNodeId) ||
			!Scenario.SliceNodeIds.Contains(Edge->TargetNodeId))
		{
			return false;
		}
	}
	for (const FString& EdgeId : Scenario.IncidentEdgeIds)
	{
		const FBlueprintLensLC6SourceEdge* Edge = FindEdge(Profile.SourceEdges, EdgeId);
		if (Edge == nullptr ||
			(Edge->SourceNodeId != Scenario.BoundaryNodeId &&
			 Edge->TargetNodeId != Scenario.BoundaryNodeId))
		{
			return false;
		}
	}
	for (const FString& PinId : Scenario.SourcePinIds)
	{
		if (!PinIds.Contains(PinId) || !PinId.StartsWith(Scenario.BoundaryNodeId + TEXT("::pin::")))
		{
			return false;
		}
	}
	return true;
}

bool ValidateQueryScenario(
	const FBlueprintLensLC6Scenario& Scenario,
	const FBlueprintLensLC6Profile& Profile)
{
	if (Scenario.ScenarioId != TEXT("LC6_TRUNCATED") ||
		Scenario.TruthOwner != TEXT("query_profile") ||
		Scenario.Status != TEXT("truncated") ||
		Scenario.Reason != TEXT("max_upstream_hops_exhausted") ||
		Scenario.MaxUpstreamHops != 3 ||
		Scenario.CompleteNodeIds.Num() != 7 || Scenario.CompleteEdgeIds.Num() != 6 ||
		Scenario.SliceNodeIds.Num() != 4 || Scenario.SliceEdgeIds.Num() != 3 ||
		Scenario.OmittedNodeCount != 3 || Scenario.OmittedEdgeCount != 3 ||
		Scenario.Frontiers.Num() != 1 || Scenario.HopDistances.Num() != 7 ||
		!Scenario.SliceNodeIds.Contains(Scenario.CriterionNodeId) ||
		!Scenario.CompleteNodeIds.Contains(Scenario.RootNodeId) ||
		Scenario.SliceNodeIds.Contains(Scenario.RootNodeId))
	{
		return false;
	}

	TSet<FString> HopNodes;
	TSet<int32> Distances;
	TArray<FString> ExpectedSelected;
	for (const FBlueprintLensLC6HopDistance& Hop : Scenario.HopDistances)
	{
		if (!Scenario.CompleteNodeIds.Contains(Hop.NodeId) ||
			HopNodes.Contains(Hop.NodeId) || Distances.Contains(Hop.Distance) ||
			!Profile.SourceTitles.Contains(Hop.NodeId))
		{
			return false;
		}
		HopNodes.Add(Hop.NodeId);
		Distances.Add(Hop.Distance);
		if (Hop.Distance <= Scenario.MaxUpstreamHops)
		{
			ExpectedSelected.Add(Hop.NodeId);
		}
	}
	bool bHasAllDistances = Distances.Num() == 7;
	for (int32 Distance = 0; Distance <= 6; ++Distance)
	{
		bHasAllDistances = bHasAllDistances && Distances.Contains(Distance);
	}
	if (!bHasAllDistances || !SameSet(ExpectedSelected, Scenario.SliceNodeIds))
	{
		return false;
	}
	for (const FString& EdgeId : Scenario.CompleteEdgeIds)
	{
		const FBlueprintLensLC6SourceEdge* Edge = FindEdge(Profile.SourceEdges, EdgeId);
		if (Edge == nullptr ||
			!Scenario.CompleteNodeIds.Contains(Edge->SourceNodeId) ||
			!Scenario.CompleteNodeIds.Contains(Edge->TargetNodeId))
		{
			return false;
		}
	}
	TArray<FString> ExpectedSelectedEdges;
	for (const FString& EdgeId : Scenario.CompleteEdgeIds)
	{
		const FBlueprintLensLC6SourceEdge* Edge = FindEdge(Profile.SourceEdges, EdgeId);
		if (Scenario.SliceNodeIds.Contains(Edge->SourceNodeId) &&
			Scenario.SliceNodeIds.Contains(Edge->TargetNodeId))
		{
			ExpectedSelectedEdges.Add(EdgeId);
		}
	}
	if (!SameSet(ExpectedSelectedEdges, Scenario.SliceEdgeIds))
	{
		return false;
	}
	const FBlueprintLensLC6Frontier& Frontier = Scenario.Frontiers[0];
	const FBlueprintLensLC6SourceEdge* Crossing = FindEdge(Profile.SourceEdges, Frontier.EdgeId);
	const auto DistanceOf = [&Scenario](const FString& NodeId)
	{
		const FBlueprintLensLC6HopDistance* Hop =
			Scenario.HopDistances.FindByPredicate([&NodeId](const auto& Item)
			{
				return Item.NodeId == NodeId;
			});
		return Hop != nullptr ? Hop->Distance : INDEX_NONE;
	};
	return Crossing != nullptr &&
		Crossing->SourceNodeId == Frontier.SourceNodeId &&
		Crossing->TargetNodeId == Frontier.TargetNodeId &&
		DistanceOf(Frontier.SourceNodeId) == 4 &&
		DistanceOf(Frontier.TargetNodeId) == 3 &&
		!Scenario.SliceNodeIds.Contains(Frontier.SourceNodeId) &&
		Scenario.SliceNodeIds.Contains(Frontier.TargetNodeId);
}

TSharedPtr<FBlueprintLensExplanationModel> BuildExplanation(
	const FBlueprintLensLC6Profile& Profile)
{
	TSharedRef<FBlueprintLensExplanationModel> Model = MakeShared<FBlueprintLensExplanationModel>();
	Model->Format = TEXT("blueprint-lens-explanation");
	Model->SchemaVersion = TEXT("0.2.0");
	Model->RulesVersion = TEXT("lc6_split_truth_profiles_v1");
	Model->Source.BlueprintAssetPath = Profile.BlueprintAssetPath;
	Model->Source.BlueprintPackageSha256 = Profile.AssetSha256.ToUpper();
	Model->Source.GraphId = Profile.GraphId;
	Model->Query.Question = TEXT("Where and why does analysis stop in each LC6 scenario?");
	Model->Query.Direction = TEXT("validated_split_truth_profiles");
	const FBlueprintLensLC6Scenario* Query = Profile.FindScenario(TEXT("LC6_TRUNCATED"));
	if (Query != nullptr)
	{
		Model->Query.CriterionSourceNodeId = Query->CriterionNodeId;
		Model->CriterionUnitId = Query->CriterionNodeId;
	}
	TSet<FString> RelevantNodeIds;
	for (const FBlueprintLensLC6Scenario& Scenario : Profile.Scenarios)
	{
		RelevantNodeIds.Add(Scenario.RootNodeId);
		RelevantNodeIds.Add(Scenario.CriterionNodeId);
		if (!Scenario.BoundaryNodeId.IsEmpty())
		{
			RelevantNodeIds.Add(Scenario.BoundaryNodeId);
		}
		for (const FString& NodeId : Scenario.CompleteNodeIds)
		{
			RelevantNodeIds.Add(NodeId);
		}
	}
	TArray<FString> OrderedIds = RelevantNodeIds.Array();
	OrderedIds.Sort();
	for (const FString& NodeId : OrderedIds)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = NodeId;
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Title = Profile.SourceTitles.FindRef(NodeId);
		Unit.Role = NodeId == Model->CriterionUnitId
			? EBlueprintLensRole::Criterion : EBlueprintLensRole::Boundary;
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons.Add(TEXT("validated_lc6_split_truth_profile"));
		FBlueprintLensSourceReference Reference;
		Reference.BlueprintAssetPath = Profile.BlueprintAssetPath;
		Reference.GraphId = Profile.GraphId;
		Reference.SourceNodeId = NodeId;
		const FString NodeMarker = TEXT("::node::");
		const int32 NodeMarkerIndex = NodeId.Find(
			NodeMarker,
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart);
		if (NodeMarkerIndex != INDEX_NONE)
		{
			Reference.NativeNodeGuid = NodeId.Mid(
				NodeMarkerIndex + NodeMarker.Len(), 36);
		}
		Reference.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Reference));
		Model->Units.Add(MoveTemp(Unit));
	}
	for (const EBlueprintLensRole Role : {
		EBlueprintLensRole::Criterion, EBlueprintLensRole::Control,
		EBlueprintLensRole::Predicate, EBlueprintLensRole::Value,
		EBlueprintLensRole::Consequence, EBlueprintLensRole::Boundary})
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
			? EBlueprintLensLaneState::Empty : EBlueprintLensLaneState::Populated;
		Lane.EmptyMessage = Lane.UnitIds.IsEmpty()
			? TEXT("No facts in this LC6 role") : FString();
		Model->Lanes.Add(MoveTemp(Lane));
	}
	Model->Counts.Lanes = 6;
	Model->Counts.Units = Model->Units.Num();
	Model->Counts.SourceNodes = Model->Units.Num();
	return Model;
}
} // namespace

const FBlueprintLensLC6Scenario* FBlueprintLensLC6Profile::FindScenario(
	const FString& ScenarioId) const
{
	return Scenarios.FindByPredicate([&ScenarioId](const FBlueprintLensLC6Scenario& Scenario)
	{
		return Scenario.ScenarioId == ScenarioId;
	});
}

bool FBlueprintLensLC6Profile::IsValid() const
{
	if (CoreProfileId != ::CoreProfileId || QueryProfileId != ::QueryProfileId ||
		ReadinessStatus != TEXT("TRUTH_FROZEN") ||
		BlueprintAssetPath != ::BlueprintAssetPath || AssetSha256 != ::AssetSha256 ||
		RawSha256 != ::RawSha256 || GraphId != ::GraphId ||
		CorePath.IsEmpty() || QueryPath.IsEmpty() || ReadinessPath.IsEmpty() || RawPath.IsEmpty() ||
		CoreSha256.IsEmpty() || QuerySha256.IsEmpty() || ReadinessSha256.IsEmpty() ||
		Scenarios.Num() != 4 || SourceTitles.IsEmpty() || SourceEdges.IsEmpty())
	{
		return false;
	}
	const TArray<FString> Expected = {
		TEXT("LC6_OPAQUE"), TEXT("LC6_UNCERTAIN"),
		TEXT("LC6_UNSUPPORTED"), TEXT("LC6_TRUNCATED")};
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		if (Scenarios[Index].ScenarioId != Expected[Index])
		{
			return false;
		}
	}
	return true;
}

FBlueprintLensLC6LoadResult FBlueprintLensLC6ProfileLoader::LoadFiles(
	const FString& CorePath,
	const FString& QueryPath,
	const FString& ReadinessPath,
	const FString& RawPath)
{
	TSharedPtr<FJsonObject> Core;
	TSharedPtr<FJsonObject> Query;
	TSharedPtr<FJsonObject> Readiness;
	TSharedPtr<FJsonObject> Raw;
	if (!ParseJson(CorePath, Core) || !ParseJson(QueryPath, Query) ||
		!ParseJson(ReadinessPath, Readiness) || !ParseJson(RawPath, Raw))
	{
		return Failure(TEXT("LC6_PROFILE_UNREADABLE"));
	}

	TSharedRef<FBlueprintLensLC6Profile> Profile = MakeShared<FBlueprintLensLC6Profile>();
	Profile->CorePath = CorePath;
	Profile->QueryPath = QueryPath;
	Profile->ReadinessPath = ReadinessPath;
	Profile->RawPath = RawPath;
	if (!HashFile(CorePath, Profile->CoreSha256) ||
		!HashFile(QueryPath, Profile->QuerySha256) ||
		!HashFile(ReadinessPath, Profile->ReadinessSha256) ||
		!HashFile(RawPath, Profile->RawSha256))
	{
		return Failure(TEXT("LC6_PROFILE_HASH_FAILED"));
	}

	FString Format;
	FString Version;
	FString CoreOwner;
	FString CoreRawSha;
	FString QueryOwner;
	FString QueryRawSha;
	FString QueryAsset;
	FString QueryGraph;
	if (!StringField(Core, TEXT("format"), Format) || Format != CoreFormat ||
		!StringField(Core, TEXT("format_version"), Version) || Version != ContractVersion ||
		!StringField(Core, TEXT("profile_id"), Profile->CoreProfileId) ||
		!StringField(Core, TEXT("truth_owner"), CoreOwner) ||
		!StringField(Core, TEXT("blueprint_asset_path"), Profile->BlueprintAssetPath) ||
		!StringField(Core, TEXT("asset_sha256"), Profile->AssetSha256) ||
		!StringField(Core, TEXT("raw_sha256"), CoreRawSha) ||
		!StringField(Core, TEXT("graph_id"), Profile->GraphId) ||
		!StringField(Query, TEXT("format"), Format) || Format != QueryFormat ||
		!StringField(Query, TEXT("format_version"), Version) || Version != ContractVersion ||
		!StringField(Query, TEXT("profile_id"), Profile->QueryProfileId) ||
		!StringField(Query, TEXT("truth_owner"), QueryOwner) ||
		!StringField(Query, TEXT("blueprint_asset_path"), QueryAsset) ||
		!StringField(Query, TEXT("asset_sha256"), Format) || Format != Profile->AssetSha256 ||
		!StringField(Query, TEXT("raw_sha256"), QueryRawSha) ||
		!StringField(Query, TEXT("graph_id"), QueryGraph) ||
		Profile->CoreProfileId != CoreProfileId || Profile->QueryProfileId != QueryProfileId ||
		CoreOwner != TEXT("core_node_classification") || QueryOwner != TEXT("query_profile") ||
		Profile->BlueprintAssetPath != BlueprintAssetPath || QueryAsset != Profile->BlueprintAssetPath ||
		Profile->AssetSha256 != AssetSha256 || Profile->GraphId != GraphId || QueryGraph != Profile->GraphId ||
		CoreRawSha != RawSha256 || QueryRawSha != RawSha256 || Profile->RawSha256 != RawSha256)
	{
		return Failure(TEXT("LC6_PROFILE_IDENTITY_INVALID"));
	}

	TSharedPtr<FJsonObject> Hashes;
	TSharedPtr<FJsonObject> Checks;
	FString Scope;
	FString Commit;
	int32 ChecksPassed = 0;
	int32 ChecksTotal = 0;
	if (!StringField(Readiness, TEXT("format"), Format) || Format != ReadinessFormat ||
		!StringField(Readiness, TEXT("format_version"), Version) || Version != ContractVersion ||
		!StringField(Readiness, TEXT("scope"), Scope) || Scope != TEXT("LC6-F1") ||
		!StringField(Readiness, TEXT("status"), Profile->ReadinessStatus) ||
		Profile->ReadinessStatus != TEXT("TRUTH_FROZEN") ||
		!StringField(Readiness, TEXT("schema_gate_commit"), Commit) || Commit != SchemaGateCommit ||
		!IntField(Readiness, TEXT("checks_passed"), ChecksPassed) || ChecksPassed != 16 ||
		!IntField(Readiness, TEXT("checks_total"), ChecksTotal) || ChecksTotal != 16 ||
		!ObjectField(Readiness, TEXT("hashes"), Hashes) ||
		!ObjectField(Readiness, TEXT("checks"), Checks))
	{
		return Failure(TEXT("LC6_READINESS_IDENTITY_INVALID"));
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Checks->Values)
	{
		bool bPassed = false;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(bPassed) || !bPassed)
		{
			return Failure(TEXT("LC6_READINESS_CHECK_FAILED"), Pair.Key);
		}
	}
	FString BoundCore;
	FString BoundQuery;
	FString BoundRaw;
	FString BoundAsset;
	if (Checks->Values.Num() != 16 ||
		!StringField(Hashes, TEXT("BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json"), BoundCore) ||
		!StringField(Hashes, TEXT("BP_LC6_BoundaryMatrix.upstream-budget.v1.json"), BoundQuery) ||
		!StringField(Hashes, TEXT("BP_LC6_BoundaryMatrix.raw-0.2.json"), BoundRaw) ||
		!StringField(Hashes, TEXT("asset_sha256"), BoundAsset) ||
		BoundCore != Profile->CoreSha256 || BoundQuery != Profile->QuerySha256 ||
		BoundRaw != Profile->RawSha256 || BoundAsset != Profile->AssetSha256)
	{
		return Failure(TEXT("LC6_READINESS_HASH_MISMATCH"));
	}
	const TArray<TSharedPtr<FJsonValue>>* ProfileIds = nullptr;
	if (!ArrayField(Readiness, TEXT("profile_ids"), ProfileIds) || ProfileIds->Num() != 2 ||
		(*ProfileIds)[0]->AsString() != CoreProfileId || (*ProfileIds)[1]->AsString() != QueryProfileId)
	{
		return Failure(TEXT("LC6_READINESS_PROFILE_IDS_INVALID"));
	}

	TSet<FString> RawPinIds;
	FString RawDetail;
	if (!ParseRaw(Raw, *Profile, RawPinIds, RawDetail))
	{
		return Failure(TEXT("LC6_RAW_INVALID"), RawDetail);
	}

	TSharedPtr<FJsonObject> CoreCounts;
	int32 CoreScenarioCount = 0;
	const TArray<TSharedPtr<FJsonValue>>* CoreScenarios = nullptr;
	if (!ObjectField(Core, TEXT("counts"), CoreCounts) ||
		!IntField(CoreCounts, TEXT("scenarios"), CoreScenarioCount) ||
		CoreScenarioCount != 3 || !ArrayField(Core, TEXT("scenarios"), CoreScenarios) ||
		CoreScenarios->Num() != 3)
	{
		return Failure(TEXT("LC6_CORE_SCENARIO_COUNT_INVALID"));
	}
	TSet<FString> ScenarioIds;
	for (const TSharedPtr<FJsonValue>& Value : *CoreScenarios)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC6Scenario Scenario;
		Scenario.TruthOwner = CoreOwner;
		if (!JsonObjectAt(Value, Object) || !ParseCoreScenario(Object, Scenario) ||
			ScenarioIds.Contains(Scenario.ScenarioId))
		{
			return Failure(TEXT("LC6_CORE_SCENARIO_INVALID"));
		}
		ScenarioIds.Add(Scenario.ScenarioId);
		Profile->Scenarios.Add(MoveTemp(Scenario));
	}

	FBlueprintLensLC6Scenario QueryScenario;
	QueryScenario.TruthOwner = QueryOwner;
	if (!ParseQueryScenario(Query, QueryScenario) || ScenarioIds.Contains(QueryScenario.ScenarioId))
	{
		return Failure(TEXT("LC6_QUERY_SCENARIO_INVALID"));
	}
	Profile->Scenarios.Add(MoveTemp(QueryScenario));

	const TMap<FString, int32> Order = {
		{TEXT("LC6_OPAQUE"), 0}, {TEXT("LC6_UNCERTAIN"), 1},
		{TEXT("LC6_UNSUPPORTED"), 2}, {TEXT("LC6_TRUNCATED"), 3}};
	Profile->Scenarios.Sort([&Order](const auto& Left, const auto& Right)
	{
		return Order.FindRef(Left.ScenarioId) < Order.FindRef(Right.ScenarioId);
	});
	for (FBlueprintLensLC6Scenario& Scenario : Profile->Scenarios)
	{
		if (!Order.Contains(Scenario.ScenarioId))
		{
			return Failure(TEXT("LC6_UNKNOWN_SCENARIO"), Scenario.ScenarioId);
		}
		Scenario.RootTitle = Profile->SourceTitles.FindRef(Scenario.RootNodeId);
		Scenario.CriterionTitle = Profile->SourceTitles.FindRef(Scenario.CriterionNodeId);
		Scenario.BoundaryTitle = Profile->SourceTitles.FindRef(Scenario.BoundaryNodeId);
		if (Scenario.RootTitle.IsEmpty() || Scenario.CriterionTitle.IsEmpty() ||
			(!Scenario.BoundaryNodeId.IsEmpty() && Scenario.BoundaryTitle.IsEmpty()))
		{
			return Failure(TEXT("LC6_SOURCE_TITLE_MISSING"), Scenario.ScenarioId);
		}
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!ValidateCoreScenario(Profile->Scenarios[Index], *Profile, RawPinIds))
		{
			return Failure(TEXT("LC6_CORE_CONTRACT_INVALID"), Profile->Scenarios[Index].ScenarioId);
		}
	}
	if (!ValidateQueryScenario(Profile->Scenarios[3], *Profile) || !Profile->IsValid())
	{
		return Failure(TEXT("LC6_QUERY_CONTRACT_INVALID"));
	}

	FBlueprintLensLC6LoadResult Result;
	Result.ExplanationModel = BuildExplanation(*Profile);
	Result.Profile = Profile;
	if (!Result.IsSuccess())
	{
		return Failure(TEXT("LC6_PROFILE_FINAL_INVALID"));
	}
	return Result;
}
