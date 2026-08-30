#include "BlueprintLensLC5LiveTypedIrAdapter.h"

#include "IPlatformCrypto.h"

namespace
{
constexpr TCHAR CallFunctionClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
constexpr TCHAR FunctionEntryClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_FunctionEntry");

const FBlueprintLensSourceReference* PrimarySource(
	const FBlueprintLensUnit& Unit)
{
	const FBlueprintLensSourceReference* Source =
		Unit.SourceReferences.FindByPredicate(
			[](const FBlueprintLensSourceReference& Candidate)
			{
				return Candidate.bPrimary;
			});
	return Source != nullptr
		? Source
		: Unit.SourceReferences.IsEmpty()
			? nullptr
			: &Unit.SourceReferences[0];
}

FString Sha256(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	return Context.IsValid() && Context->CalcSHA256(Bytes, Digest) &&
		Digest.Num() == 32
		? BytesToHex(Digest.GetData(), Digest.Num())
		: FString();
}

FString OccurrenceId(const FString& Prefix, const FString& SourceNodeId)
{
	const int32 Delimiter = SourceNodeId.Find(
		TEXT("::node::"),
		ESearchCase::CaseSensitive,
		ESearchDir::FromEnd);
	return Prefix + TEXT(":") +
		(Delimiter == INDEX_NONE
			? SourceNodeId
			: SourceNodeId.Mid(Delimiter + 8));
}

FBlueprintLensLC5Projection BuildProjection(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensUnit& CallUnit,
	const FBlueprintLensSourceReference& CallSource,
	const FBlueprintLensLC1NodeFact& CallNode,
	const FBlueprintLensLC1GraphFact& CalleeGraph,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts,
	const EBlueprintLensLC5LiveClaimState State,
	const FString& ReaderStatement)
{
	FBlueprintLensLC5Projection Result;
	Result.bLiveCallBody = true;
	Result.SourceBlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	Result.CallUnitId = CallUnit.Id;
	Result.CallerGraphId = Explanation.Source.GraphId;
	Result.CalleeGraphName = CalleeGraph.GraphName;
	Result.ClaimState = State ==
		EBlueprintLensLC5LiveClaimState::FrozenConditionInstance
		? TEXT("frozen-condition-instance")
		: TEXT("beyond-frozen-impure-adaptation");
	Result.ClaimBoundaryStatement = ReaderStatement;
	Result.StaticOrderStatement =
		TEXT("STATIC ORDER · top-to-bottom follows static reachability; same-row units are unordered; runtime order is not claimed.");
	Result.ProjectorVersion = TEXT("BlueprintLens.LC5LiveTypedIrAdapter.v1");
	Result.SourceProfileSha256 = TypedIrFacts.VerifiedIrSha256;
	Result.SourceIdentity.CallGraphId = CallNode.GraphId;
	Result.SourceIdentity.CallSiteNodeId = CallSource.SourceNodeId;
	Result.CallContext.Id = TEXT("live:") + CallUnit.Id;
	Result.CallContext.ParentId = TEXT("root");
	Result.CallContext.ClaimScope = ReaderStatement;
	Result.CallContext.CallSiteStack = {CallSource.SourceNodeId};

	FBlueprintLensLC5Occurrence CallOccurrence;
	CallOccurrence.OccurrenceId =
		OccurrenceId(TEXT("live-call"), CallSource.SourceNodeId);
	CallOccurrence.SourceNodeId = CallSource.SourceNodeId;
	CallOccurrence.CallContextId = Result.CallContext.Id;
	CallOccurrence.Role = TEXT("call_site");
	Result.LiveOccurrenceLabels.Add(
		CallOccurrence.OccurrenceId,
		CallUnit.Title);
	Result.Occurrences.Add(MoveTemp(CallOccurrence));

	TMap<FString, FString> OccurrenceByNodeId;
	for (const FString& NodeId : CalleeGraph.NodeIds)
	{
		const FBlueprintLensLC1NodeFact* Node =
			TypedIrFacts.NodesBySourceNodeId.Find(NodeId);
		if (Node == nullptr)
		{
			continue;
		}
		FBlueprintLensLC5Occurrence Occurrence;
		Occurrence.OccurrenceId = OccurrenceId(TEXT("live-body"), NodeId);
		Occurrence.SourceNodeId = NodeId;
		Occurrence.CallContextId = Result.CallContext.Id;
		Occurrence.Role = Node->NodeClass == FunctionEntryClass
			? TEXT("function_entry")
			: TEXT("callee_body");
		OccurrenceByNodeId.Add(NodeId, Occurrence.OccurrenceId);
		Result.LiveOccurrenceLabels.Add(
			Occurrence.OccurrenceId,
			Node->NativeTitle);
		Result.Occurrences.Add(MoveTemp(Occurrence));
	}

	for (const FString& EdgeId : CalleeGraph.EdgeIds)
	{
		const FBlueprintLensLC1EdgeFact* Edge =
			TypedIrFacts.Edges.FindByPredicate(
				[&EdgeId](const FBlueprintLensLC1EdgeFact& Candidate)
				{
					return Candidate.EdgeId == EdgeId;
				});
		if (Edge == nullptr ||
			!OccurrenceByNodeId.Contains(Edge->SourceNodeId) ||
			!OccurrenceByNodeId.Contains(Edge->TargetNodeId))
		{
			continue;
		}
		FBlueprintLensLC5InternalRelation Relation;
		Relation.Kind = Edge->Kind;
		Relation.SourceEdgeId = Edge->EdgeId;
		Relation.SourceOccurrenceId =
			OccurrenceByNodeId.FindChecked(Edge->SourceNodeId);
		Relation.TargetOccurrenceId =
			OccurrenceByNodeId.FindChecked(Edge->TargetNodeId);
		Relation.RelationId = TEXT("live-internal:") + Edge->EdgeId;
		Result.InternalRelations.Add(MoveTemp(Relation));
	}
	Result.LegendEntries = {
		{TEXT("caller_portal"), TEXT("blue call site/portal"),
			TEXT("portal relation and caller enclosure")},
		{TEXT("callee_body"), TEXT("purple exported body"),
			TEXT("callee enclosure")},
		{TEXT("context_divider"), TEXT("dashed call boundary"),
			TEXT("context boundary")}};
	if (Result.InternalRelations.ContainsByPredicate(
		[](const FBlueprintLensLC5InternalRelation& Relation)
		{
			return Relation.Kind != TEXT("data");
		}))
	{
		Result.LegendEntries.Add({
			TEXT("execution_relation"),
			TEXT("green execution"),
			TEXT("execution relation")});
	}
	if (Result.InternalRelations.ContainsByPredicate(
		[](const FBlueprintLensLC5InternalRelation& Relation)
		{
			return Relation.Kind == TEXT("data");
		}))
	{
		Result.LegendEntries.Add({
			TEXT("value_relation"),
			TEXT("amber value"),
			TEXT("value relation")});
	}

	TMap<FString, FString> SourceNodeByOccurrence;
	TMap<FString, int32> InDegree;
	TMap<FString, TArray<FString>> Successors;
	for (int32 Index = 1; Index < Result.Occurrences.Num(); ++Index)
	{
		const FBlueprintLensLC5Occurrence& Occurrence = Result.Occurrences[Index];
		SourceNodeByOccurrence.Add(
			Occurrence.OccurrenceId,
			Occurrence.SourceNodeId);
		InDegree.Add(Occurrence.OccurrenceId, 0);
	}
	for (const FBlueprintLensLC5InternalRelation& Relation :
		Result.InternalRelations)
	{
		if (!InDegree.Contains(Relation.SourceOccurrenceId) ||
			!InDegree.Contains(Relation.TargetOccurrenceId))
		{
			continue;
		}
		++InDegree.FindChecked(Relation.TargetOccurrenceId);
		Successors.FindOrAdd(Relation.SourceOccurrenceId).Add(
			Relation.TargetOccurrenceId);
	}
	const auto SortBySourceIdentity = [&SourceNodeByOccurrence](
		const FString& Left,
		const FString& Right)
	{
		return SourceNodeByOccurrence.FindRef(Left) <
			SourceNodeByOccurrence.FindRef(Right);
	};
	TArray<FString> Ready;
	for (const TPair<FString, int32>& EntryPair : InDegree)
	{
		if (EntryPair.Value == 0)
		{
			Ready.Add(EntryPair.Key);
			Result.LiveStaticRanks.Add(EntryPair.Key, 0);
		}
	}
	Ready.Sort(SortBySourceIdentity);
	TArray<FString> OrderedBodyOccurrenceIds;
	while (!Ready.IsEmpty())
	{
		const FString Current = Ready[0];
		Ready.RemoveAt(0);
		OrderedBodyOccurrenceIds.Add(Current);
		TArray<FString> CurrentSuccessors = Successors.FindRef(Current);
		CurrentSuccessors.Sort(SortBySourceIdentity);
		for (const FString& Successor : CurrentSuccessors)
		{
			const int32 CandidateRank =
				Result.LiveStaticRanks.FindRef(Current) + 1;
			int32& SuccessorRank =
				Result.LiveStaticRanks.FindOrAdd(Successor);
			SuccessorRank = FMath::Max(SuccessorRank, CandidateRank);
			int32& SuccessorInDegree = InDegree.FindChecked(Successor);
			--SuccessorInDegree;
			if (SuccessorInDegree == 0)
			{
				Ready.Add(Successor);
				Ready.Sort(SortBySourceIdentity);
			}
		}
	}
	if (OrderedBodyOccurrenceIds.Num() != InDegree.Num())
	{
		Result.LiveStaticRanks.Reset();
		Result.BoundaryText = {
			TEXT("The exported callee body contains a static relation cycle, so no top-to-bottom order is fabricated."),
			TEXT("Complete text remains available; traversal still stops at the original call boundary.")};
		Result.DiagnosticCode = TEXT("LC5_LIVE_CALLEE_BODY_CYCLIC");
		return Result;
	}
	TArray<FBlueprintLensLC5Occurrence> OrderedOccurrences;
	OrderedOccurrences.Add(Result.Occurrences[0]);
	for (const FString& OccurrenceId : OrderedBodyOccurrenceIds)
	{
		const FBlueprintLensLC5Occurrence* Occurrence =
			Result.Occurrences.FindByPredicate(
				[&OccurrenceId](const FBlueprintLensLC5Occurrence& Candidate)
				{
					return Candidate.OccurrenceId == OccurrenceId;
				});
		if (Occurrence != nullptr)
		{
			OrderedOccurrences.Add(*Occurrence);
		}
	}
	Result.Occurrences = MoveTemp(OrderedOccurrences);

	const FBlueprintLensLC5Occurrence* Entry =
		Result.Occurrences.FindByPredicate(
			[](const FBlueprintLensLC5Occurrence& Occurrence)
			{
				return Occurrence.Role == TEXT("function_entry");
			});
	if (Entry == nullptr && Result.Occurrences.Num() > 1)
	{
		Entry = &Result.Occurrences[1];
	}
	if (Entry != nullptr)
	{
		FBlueprintLensLC5ContextBoundary Boundary;
		Boundary.Kind = TEXT("call_enter");
		Boundary.ClaimScope = TEXT("static_export_membership");
		Boundary.SourceOccurrenceId = Result.Occurrences[0].OccurrenceId;
		Boundary.TargetOccurrenceId = Entry->OccurrenceId;
		Boundary.RelationId = TEXT("live-context:call_enter:") + CallUnit.Id;
		Result.ContextBoundaries.Add(MoveTemp(Boundary));
	}

	for (const FBlueprintLensLC5Occurrence& Occurrence : Result.Occurrences)
	{
		Result.SourceNodeIds.Add(Occurrence.SourceNodeId);
	}
	for (const FBlueprintLensLC5ContextBoundary& Boundary :
		Result.ContextBoundaries)
	{
		Result.AllRelationIds.Add(Boundary.RelationId);
	}
	for (const FBlueprintLensLC5InternalRelation& Relation :
		Result.InternalRelations)
	{
		Result.AllRelationIds.Add(Relation.RelationId);
	}
	Result.ActionIds = {
		TEXT("select"), TEXT("show_complete_text"),
		TEXT("show_evidence"), TEXT("open_source")};
	Result.BoundaryText = {
		ReaderStatement,
		TEXT("The body comes from the static typed-IR export; no runtime invocation or runtime order is claimed."),
		TEXT("Traversal still stops at the first impure call; this view looks behind that retained boundary without moving it.")};
	TArray<FString> Ledger = Result.SourceNodeIds;
	Ledger.Append(Result.AllRelationIds);
	Ledger.Append(Result.ActionIds);
	Ledger.Append(Result.BoundaryText);
	Ledger.Add(Result.StaticOrderStatement);
	Ledger.Add(TEXT("caller_graph:") + Result.CallerGraphId);
	for (const FBlueprintLensLC5LegendEntry& LegendEntry : Result.LegendEntries)
	{
		Ledger.Add(FString::Printf(
			TEXT("legend:%s=%s|%s"),
			*LegendEntry.SemanticId,
			*LegendEntry.ReaderLabel,
			*LegendEntry.Family));
	}
	for (int32 Index = 1; Index < Result.Occurrences.Num(); ++Index)
	{
		const FString& OccurrenceId = Result.Occurrences[Index].OccurrenceId;
		Ledger.Add(FString::Printf(
			TEXT("rank:%s=%d"),
			*OccurrenceId,
			Result.LiveStaticRanks.FindRef(OccurrenceId)));
	}
	Result.ProjectionIntegrityHash = Sha256(FString::Printf(
		TEXT("%s\n%s\n%s"),
		*Result.ProjectorVersion,
		*Result.SourceProfileSha256,
		*FString::Join(Ledger, TEXT("\n"))));
	Result.Status = EBlueprintLensLC5ProjectionStatus::TypedPortalBridge;
	Result.DiagnosticCode = TEXT("LC5_LIVE_TYPED_PORTAL_COMPLETE");
	return Result;
}
} // namespace

FBlueprintLensLC5LiveTypedIrAdapterResult
FBlueprintLensLC5LiveTypedIrAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts)
{
	FBlueprintLensLC5LiveTypedIrAdapterResult Result;
	if (!TypedIrFacts.IsValid() ||
		!TypedIrFacts.VerifiedIrSha256.Equals(
			Explanation.Source.IrSha256,
			ESearchCase::IgnoreCase))
	{
		Result.DiagnosticCode = TEXT("LC5_LIVE_TYPED_IR_INVALID");
		return Result;
	}

	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		const FBlueprintLensSourceReference* Source = PrimarySource(Unit);
		const FBlueprintLensLC1NodeFact* Node = Source != nullptr
			? TypedIrFacts.NodesBySourceNodeId.Find(Source->SourceNodeId)
			: nullptr;
		if (Source == nullptr || Node == nullptr ||
			Node->NodeClass != CallFunctionClass || !Node->bHasSymbol)
		{
			continue;
		}
		++Result.CandidateCallUnitCount;
		FBlueprintLensLC5LiveCallCase Case;
		Case.CallUnitId = Unit.Id;
		Case.CallTitle = Unit.Title;
		Case.CalleeName = Node->SymbolName;

		if (!Node->bIsSelfContext)
		{
			Case.State = EBlueprintLensLC5LiveClaimState::Refused;
			Case.DiagnosticCode = TEXT("LC5_LIVE_NON_SELF_CONTEXT_REFUSED");
			Case.ReaderStatement = FString::Printf(
				TEXT("%s · no body is rendered because this call is not self-context; the loaded sidecar does not carry an external callee body."),
				*Unit.Title);
			Result.Cases.Add(MoveTemp(Case));
			continue;
		}
		if (Node->bIsLatent)
		{
			Case.State = EBlueprintLensLC5LiveClaimState::Refused;
			Case.DiagnosticCode = TEXT("LC5_LIVE_LATENT_CALL_REFUSED");
			Case.ReaderStatement = FString::Printf(
				TEXT("%s · no body is rendered because latent call behaviour is outside this static typed-body adapter."),
				*Unit.Title);
			Result.Cases.Add(MoveTemp(Case));
			continue;
		}

		const TArray<FBlueprintLensLC1GraphFact> MatchingGraphs =
			TypedIrFacts.Graphs.FilterByPredicate(
				[Node](const FBlueprintLensLC1GraphFact& Graph)
				{
					return Graph.GraphName.Equals(
						Node->SymbolName,
						ESearchCase::CaseSensitive);
				});
		if (MatchingGraphs.IsEmpty())
		{
			Case.State = EBlueprintLensLC5LiveClaimState::BodyUnavailable;
			Case.DiagnosticCode = TEXT("LC5_LIVE_CALLEE_GRAPH_ABSENT");
			Case.ReaderStatement = FString::Printf(
				TEXT("%s · the self-context callee graph is absent from the typed-IR export, so no body is rendered."),
				*Unit.Title);
			Result.Cases.Add(MoveTemp(Case));
			continue;
		}
		if (MatchingGraphs.Num() != 1)
		{
			Case.State = EBlueprintLensLC5LiveClaimState::Refused;
			Case.DiagnosticCode = TEXT("LC5_LIVE_CALLEE_GRAPH_AMBIGUOUS");
			Case.ReaderStatement = FString::Printf(
				TEXT("%s · multiple exported graphs match this self-context callee name, so no body is chosen."),
				*Unit.Title);
			Result.Cases.Add(MoveTemp(Case));
			continue;
		}
		const FBlueprintLensLC1GraphFact& CalleeGraph = MatchingGraphs[0];
		if (CalleeGraph.NodeIds.Num() >
			BlueprintLensLC5LiveBounds::MaxCalleeBodyUnits)
		{
			Case.State = EBlueprintLensLC5LiveClaimState::Refused;
			Case.DiagnosticCode = TEXT("LC5_LIVE_CALLEE_BODY_BOUND_EXCEEDED");
			Case.ReaderStatement = FString::Printf(
				TEXT("%s · the exported callee body has %d nodes, beyond the measured live bound of %d; no partial body is fabricated."),
				*Unit.Title,
				CalleeGraph.NodeIds.Num(),
				BlueprintLensLC5LiveBounds::MaxCalleeBodyUnits);
			Result.Cases.Add(MoveTemp(Case));
			continue;
		}

		Case.State = Node->bIsPure
			? EBlueprintLensLC5LiveClaimState::FrozenConditionInstance
			: EBlueprintLensLC5LiveClaimState::BeyondFrozenImpure;
		Case.DiagnosticCode = Node->bIsPure
			? TEXT("LC5_LIVE_FROZEN_CONDITION_INSTANCE")
			: TEXT("LC5_LIVE_BEYOND_FROZEN_IMPURE");
		Case.ReaderStatement = Node->bIsPure
			? FString::Printf(
				TEXT("%s / %s · the exported %d-node callee body is an instance of LC5_INTRA_BP_PURE_CALL_V1."),
				*Explanation.Source.BlueprintAssetPath,
				*Node->SymbolName,
				CalleeGraph.NodeIds.Num())
			: FString::Printf(
				TEXT("%s / %s · the exported %d-node callee body is shown as an impure-call adaptation beyond LC5_INTRA_BP_PURE_CALL_V1; that frozen condition claims nothing about impure call behaviour."),
				*Explanation.Source.BlueprintAssetPath,
				*Node->SymbolName,
				CalleeGraph.NodeIds.Num());
		Case.Projection = BuildProjection(
			Explanation,
			Unit,
			*Source,
			*Node,
			CalleeGraph,
			TypedIrFacts,
			Case.State,
			Case.ReaderStatement);
		if (!Case.Projection.IsRenderable())
		{
			Case.State = EBlueprintLensLC5LiveClaimState::Refused;
			if (Case.Projection.DiagnosticCode ==
				TEXT("LC5_LIVE_CALLEE_BODY_CYCLIC"))
			{
				Case.DiagnosticCode = TEXT("LC5_LIVE_CALLEE_BODY_CYCLIC");
				Case.ReaderStatement = FString::Printf(
					TEXT("%s · the exported callee body contains a static relation cycle, so no top-to-bottom order or partial portal is fabricated; complete text remains available."),
					*Unit.Title);
			}
			else
			{
				Case.DiagnosticCode = TEXT("LC5_LIVE_PROJECTION_INCOMPLETE");
				Case.ReaderStatement = FString::Printf(
					TEXT("%s · the exported body could not be accounted completely, so no partial portal is rendered."),
					*Unit.Title);
			}
		}
		Result.Cases.Add(MoveTemp(Case));
	}

	if (Result.Cases.IsEmpty())
	{
		Result.DiagnosticCode = TEXT("LC5_LIVE_NO_CALL_UNIT_IN_SLICE");
	}
	return Result;
}
