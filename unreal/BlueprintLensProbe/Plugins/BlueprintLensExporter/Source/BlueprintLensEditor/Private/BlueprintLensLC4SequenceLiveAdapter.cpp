#include "BlueprintLensLC4SequenceLiveAdapter.h"

#include "Algo/Reverse.h"
#include "IPlatformCrypto.h"

namespace
{
constexpr TCHAR SequenceClass[] =
	TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence");

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
		: Unit.SourceReferences.IsEmpty() ? nullptr : &Unit.SourceReferences[0];
}

bool ParseOrdinal(const FString& PinName, int32& OutOrdinal)
{
	if (!PinName.StartsWith(TEXT("then_"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	const FString Suffix = PinName.RightChop(5);
	if (Suffix.IsEmpty() || !Suffix.IsNumeric())
	{
		return false;
	}
	OutOrdinal = FCString::Atoi(*Suffix);
	return OutOrdinal >= 0;
}

bool IsExecutionRelation(const FBlueprintLensRelation& Relation)
{
	return Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor ||
		Relation.Kind == EBlueprintLensRelationKind::ControlsExecution;
}

FString Sha256Text(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num()).ToUpper();
}

const FBlueprintLensRelation* RelationForTypedEdge(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& SequenceUnitId,
	const FString& TargetUnitId,
	const FString& EdgeId,
	const FString& PinName)
{
	return Explanation.Relations.FindByPredicate(
		[&](const FBlueprintLensRelation& Relation)
		{
			return IsExecutionRelation(Relation) &&
				Relation.SourceUnitId == SequenceUnitId &&
				Relation.TargetUnitId == TargetUnitId &&
				(Relation.SourceEdgeIds.Contains(EdgeId) ||
					(Relation.bHasPortLabel &&
						Relation.PortLabel.Equals(
							PinName, ESearchCase::IgnoreCase)));
		});
}

bool PathToCriterion(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& StartUnitId,
	TArray<FString>& OutUnitIds,
	TArray<FString>& OutRelationIds)
{
	if (StartUnitId == Explanation.CriterionUnitId)
	{
		OutUnitIds = {StartUnitId};
		return true;
	}
	TArray<FString> Queue = {StartUnitId};
	TSet<FString> Visited = {StartUnitId};
	TMap<FString, FString> ParentUnit;
	TMap<FString, FString> ParentRelation;
	for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
	{
		const FString Current = Queue[Cursor];
		TArray<const FBlueprintLensRelation*> Outgoing;
		for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		{
			if (IsExecutionRelation(Relation) &&
				Relation.SourceUnitId == Current)
			{
				Outgoing.Add(&Relation);
			}
		}
		Outgoing.Sort(
			[](const FBlueprintLensRelation& A,
				const FBlueprintLensRelation& B)
			{
				return A.Id < B.Id;
			});
		for (const FBlueprintLensRelation* Relation : Outgoing)
		{
			if (Visited.Contains(Relation->TargetUnitId))
			{
				continue;
			}
			Visited.Add(Relation->TargetUnitId);
			ParentUnit.Add(Relation->TargetUnitId, Current);
			ParentRelation.Add(Relation->TargetUnitId, Relation->Id);
			Queue.Add(Relation->TargetUnitId);
			if (Relation->TargetUnitId == Explanation.CriterionUnitId)
			{
				TArray<FString> ReverseUnits;
				TArray<FString> ReverseRelations;
				FString Walker = Explanation.CriterionUnitId;
				while (Walker != StartUnitId)
				{
					ReverseUnits.Add(Walker);
					ReverseRelations.Add(ParentRelation.FindChecked(Walker));
					Walker = ParentUnit.FindChecked(Walker);
				}
				ReverseUnits.Add(StartUnitId);
				Algo::Reverse(ReverseUnits);
				Algo::Reverse(ReverseRelations);
				OutUnitIds = MoveTemp(ReverseUnits);
				OutRelationIds = MoveTemp(ReverseRelations);
				return true;
			}
		}
	}
	return false;
}

void AddUniqueUnit(FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensUnit& Unit)
{
	if (Explanation.FindUnit(Unit.Id) == nullptr)
	{
		Explanation.Units.Add(Unit);
	}
}

void AddUniqueRelation(FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensRelation& Relation)
{
	if (Explanation.FindRelation(Relation.Id) == nullptr)
	{
		Explanation.Relations.Add(Relation);
	}
}
} // namespace

FBlueprintLensExplanationModel
FBlueprintLensLC4SequenceLiveAdapter::ApplyReaderDisambiguators(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts)
{
	FBlueprintLensExplanationModel Result = Explanation;
	if (!TypedIrFacts.IsValid())
	{
		return Result;
	}
	const FBlueprintLensUnit* Criterion =
		Result.FindUnit(Result.CriterionUnitId);
	if (Criterion != nullptr && !Criterion->Title.IsEmpty())
	{
		struct FReaderRole
		{
			FString UnitId;
			FString Text;
			TArray<FString> EvidenceRelationIds;
		};
		TArray<FReaderRole> Roles;
		TArray<FString> CriterionEvidence;
		for (const FBlueprintLensUnit& Unit : Result.Units)
		{
			if (Unit.Id == Result.CriterionUnitId ||
				!Unit.Title.Equals(
					Criterion->Title,
					ESearchCase::CaseSensitive))
			{
				continue;
			}
			const FBlueprintLensSourceReference* Source = PrimarySource(Unit);
			const FBlueprintLensLC1NodeFact* Node = Source != nullptr
				? TypedIrFacts.NodesBySourceNodeId.Find(Source->SourceNodeId)
				: nullptr;
			if (Node == nullptr || Node->NodeClass != SequenceClass)
			{
				continue;
			}
			TArray<FString> PathUnitIds;
			TArray<FString> PathRelationIds;
			if (!PathToCriterion(
				Result,
				Unit.Id,
				PathUnitIds,
				PathRelationIds) ||
				PathRelationIds.IsEmpty())
			{
				continue;
			}
			FReaderRole Role;
			Role.UnitId = Unit.Id;
			Role.Text = PathRelationIds.Num() == 1
				? TEXT("direct feeder")
				: TEXT("upstream fan-out");
			Role.EvidenceRelationIds = PathRelationIds;
			Roles.Add(MoveTemp(Role));
			CriterionEvidence.AddUnique(PathRelationIds.Last());
		}
		if (!Roles.IsEmpty())
		{
			Roles.Add({
				Result.CriterionUnitId,
				TEXT("selected target"),
				MoveTemp(CriterionEvidence)});
			for (const FReaderRole& Role : Roles)
			{
				FBlueprintLensUnit* Unit = Result.Units.FindByPredicate(
					[&Role](const FBlueprintLensUnit& Candidate)
					{
						return Candidate.Id == Role.UnitId;
					});
				if (Unit == nullptr || Unit->bHasDisambiguator)
				{
					continue;
				}
				Unit->bHasDisambiguator = true;
				Unit->Disambiguator.Text = Role.Text;
				Unit->Disambiguator.RuleId =
					TEXT("lc4_sequence_duplicate_title_role_v1");
				Unit->Disambiguator.EvidenceRelationIds =
					Role.EvidenceRelationIds;
			}
		}
	}

	TMap<FString, int32> DuplicateTitleCounts;
	for (const FBlueprintLensUnit& Unit : Result.Units)
	{
		if (!Unit.Title.IsEmpty())
		{
			++DuplicateTitleCounts.FindOrAdd(Unit.Title);
		}
	}
	for (FBlueprintLensUnit& Unit : Result.Units)
	{
		const int32* Count = DuplicateTitleCounts.Find(Unit.Title);
		if (Unit.bHasDisambiguator || Count == nullptr || *Count < 2)
		{
			continue;
		}
		const FBlueprintLensSourceReference* Source = PrimarySource(Unit);
		const FBlueprintLensLC1NodeFact* Node = Source != nullptr
			? TypedIrFacts.NodesBySourceNodeId.Find(Source->SourceNodeId)
			: nullptr;
		if (Source == nullptr || Node == nullptr)
		{
			continue;
		}
		FString SourceIdentity = Source->NativeNodeGuid;
		if (SourceIdentity.IsEmpty())
		{
			const int32 NodeDelimiter = Source->SourceNodeId.Find(
				TEXT("::node::"),
				ESearchCase::CaseSensitive,
				ESearchDir::FromEnd);
			SourceIdentity = NodeDelimiter == INDEX_NONE
				? Source->SourceNodeId
				: Source->SourceNodeId.Mid(NodeDelimiter + 8);
		}
		if (SourceIdentity.IsEmpty())
		{
			continue;
		}
		Unit.bHasDisambiguator = true;
		Unit.Disambiguator.Text = SourceIdentity.Left(8).ToUpper();
		Unit.Disambiguator.RuleId =
			TEXT("source_identity_duplicate_title_v1");
		for (const FBlueprintLensRelation& Relation : Result.Relations)
		{
			if (Relation.SourceUnitId == Unit.Id ||
				Relation.TargetUnitId == Unit.Id)
			{
				Unit.Disambiguator.EvidenceRelationIds.AddUnique(Relation.Id);
			}
		}
		Unit.Disambiguator.EvidenceRelationIds.Sort();
	}
	return Result;
}

FBlueprintLensLC4SequenceLiveAdapterResult
FBlueprintLensLC4SequenceLiveAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1TypedIrFacts& TypedIrFacts)
{
	FBlueprintLensLC4SequenceLiveAdapterResult Result;
	if (!TypedIrFacts.IsValid() ||
		!TypedIrFacts.VerifiedIrSha256.Equals(
			Explanation.Source.IrSha256, ESearchCase::IgnoreCase))
	{
		Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LIVE_TYPED_IR_INVALID");
		return Result;
	}
	const FBlueprintLensExplanationModel ReaderExplanation =
		ApplyReaderDisambiguators(Explanation, TypedIrFacts);

	TMap<FString, const FBlueprintLensUnit*> UnitBySourceNodeId;
	for (const FBlueprintLensUnit& Unit : ReaderExplanation.Units)
	{
		const FBlueprintLensSourceReference* Source = PrimarySource(Unit);
		if (Source != nullptr && !Source->SourceNodeId.IsEmpty())
		{
			UnitBySourceNodeId.Add(Source->SourceNodeId, &Unit);
		}
	}

	for (const FBlueprintLensRelation& SelectedRelation : ReaderExplanation.Relations)
	{
		int32 SelectedOrdinal = INDEX_NONE;
		if (!SelectedRelation.bHasPortLabel ||
			!ParseOrdinal(SelectedRelation.PortLabel, SelectedOrdinal) ||
			!IsExecutionRelation(SelectedRelation))
		{
			continue;
		}
		++Result.CandidateRelationCount;
		const FBlueprintLensUnit* SequenceUnit =
			ReaderExplanation.FindUnit(SelectedRelation.SourceUnitId);
		const FBlueprintLensSourceReference* SequenceSource =
			SequenceUnit != nullptr ? PrimarySource(*SequenceUnit) : nullptr;
		const FBlueprintLensLC1NodeFact* SequenceNode =
			SequenceSource != nullptr
				? TypedIrFacts.NodesBySourceNodeId.Find(
					SequenceSource->SourceNodeId)
				: nullptr;
		if (SequenceUnit == nullptr || SequenceNode == nullptr ||
			SequenceNode->NodeClass != SequenceClass)
		{
			continue;
		}

		TArray<TPair<int32, const FBlueprintLensLC1NodeFact::FPin*>> Outputs;
		for (const FBlueprintLensLC1NodeFact::FPin& Pin : SequenceNode->Pins)
		{
			int32 Ordinal = INDEX_NONE;
			if (Pin.Direction == TEXT("output") && Pin.Kind == TEXT("execution") &&
				ParseOrdinal(Pin.Name, Ordinal))
			{
				Outputs.Add(TPair<int32,
					const FBlueprintLensLC1NodeFact::FPin*>(Ordinal, &Pin));
			}
		}
		Outputs.Sort(
			[](const auto& A, const auto& B)
			{
				return A.Key < B.Key;
			});
		if (Outputs.IsEmpty() || Outputs.Num() >
			BlueprintLensLC4SequenceLiveBounds::MaxDeclaredOutputsPerSequence)
		{
			++Result.RejectedSequenceRootCount;
			continue;
		}

		FBlueprintLensLC4SequenceLiveCase Case;
		Case.AnchorRelationId = SelectedRelation.Id;
		Case.SequenceUnitId = SequenceUnit->Id;
		Case.TargetUnitId = SelectedRelation.TargetUnitId;
		Case.Explanation = ReaderExplanation;
		FBlueprintLensLC4SequenceProfile& Profile = Case.Profile;
		Profile.bLiveExplanation = true;
		Profile.Format = TEXT("blueprint-lens-sequence-profile");
		Profile.SchemaVersion = TEXT("1.0.0");
		Profile.ProfileId = TEXT("LC4_SEQUENCE_FANOUT_TO_BOUNDARY_V1");
		Profile.RulesVersion = TEXT("sequence_fanout_to_first_boundary_v1");
		Profile.QueryMode = TEXT("sequence_fanout_overview");
		Profile.ProfilePath = FString::Printf(
			TEXT("live-explanation:%s"), *SelectedRelation.Id);
		Profile.ProfileSha256 = Sha256Text(FString::Printf(
			TEXT("%s:%s"),
			*Explanation.Source.IrSha256,
			*SequenceUnit->Id));
		Profile.Source.BlueprintAssetPath =
			ReaderExplanation.Source.BlueprintAssetPath;
		Profile.Source.BlueprintPackageSha256 =
			ReaderExplanation.Source.BlueprintPackageSha256;
		Profile.Source.GraphId = ReaderExplanation.Source.GraphId;
		Profile.Source.SequenceNodeId = SequenceUnit->Id;
		Profile.Source.CriterionNodeId = ReaderExplanation.CriterionUnitId;
		Profile.Source.IrPath = ReaderExplanation.Source.IrPath;
		Profile.Source.IrSha256 = ReaderExplanation.Source.IrSha256;

		for (const auto& OutputPair : Outputs)
		{
			const int32 Ordinal = OutputPair.Key;
			const FBlueprintLensLC1NodeFact::FPin& Pin = *OutputPair.Value;
			FBlueprintLensLC4SequenceOutput Output;
			Output.Ordinal = Ordinal;
			Output.SourcePinId = Pin.PinId;
			Output.SourcePinName = Pin.Name;
			TArray<const FBlueprintLensLC1EdgeFact*> ConnectedEdges;
			for (const FBlueprintLensLC1EdgeFact& Edge : TypedIrFacts.Edges)
			{
				if (Edge.bDirectionIsValid && Edge.Kind == TEXT("execution") &&
					Edge.SourceNodeId == SequenceSource->SourceNodeId &&
					Edge.SourcePinId == Pin.PinId)
				{
					ConnectedEdges.Add(&Edge);
				}
			}
			if (ConnectedEdges.IsEmpty())
			{
				Output.ConnectionState =
					EBlueprintLensLC4ConnectionState::Unconnected;
				Output.CriterionRelation =
					EBlueprintLensLC4CriterionRelation::Outside;
				Output.CriterionReason =
					TEXT("declared output is unconnected and excluded from this static answer");
				Output.TerminationKind =
					EBlueprintLensLC4TerminationKind::Unconnected;
				++Profile.Counts.UnconnectedOutputs;
				Profile.Outputs.Add(MoveTemp(Output));
				continue;
			}
			// Blueprint execution output pins are single-target in the retained
			// source.  Multiple typed edges are not collapsed into one route.
			if (ConnectedEdges.Num() != 1)
			{
				++Result.RejectedSequenceRootCount;
				Profile.Outputs.Reset();
				break;
			}
			const FBlueprintLensLC1EdgeFact& Edge = *ConnectedEdges[0];
			Output.ConnectionState = EBlueprintLensLC4ConnectionState::Connected;
			Output.ConnectedEdgeIds = {Edge.EdgeId};
			++Profile.Counts.ConnectedOutputs;
			const FBlueprintLensUnit* TargetUnit =
				UnitBySourceNodeId.FindRef(Edge.TargetNodeId);
			const FBlueprintLensRelation* SliceRelation = TargetUnit != nullptr
				? RelationForTypedEdge(
					ReaderExplanation, SequenceUnit->Id, TargetUnit->Id,
					Edge.EdgeId, Pin.Name)
				: nullptr;
			TArray<FString> PathUnitIds;
			TArray<FString> PathRelationIds;
			const bool bIncluded = SliceRelation != nullptr &&
				PathToCriterion(
					ReaderExplanation, TargetUnit->Id,
					PathUnitIds, PathRelationIds);
			if (bIncluded)
			{
				Output.CriterionRelation =
					EBlueprintLensLC4CriterionRelation::Included;
				Output.CriterionReason =
					TEXT("selected output is present in the current static answer");
				Output.ReachableNodeIds = MoveTemp(PathUnitIds);
				Output.ReachableEdgeIds = {SliceRelation->Id};
				for (const FString& RelationId : PathRelationIds)
				{
					Output.ReachableEdgeIds.AddUnique(RelationId);
				}
				Output.TerminationKind = EBlueprintLensLC4TerminationKind::Terminal;
				Output.TerminationNodeId = Explanation.CriterionUnitId;
				++Profile.Counts.CriterionIncludedOutputs;
			}
			else
			{
				Output.CriterionRelation =
					EBlueprintLensLC4CriterionRelation::Outside;
				Output.CriterionReason =
					TEXT("connected sibling is excluded from the current static answer");
				FBlueprintLensUnit OutsideUnit;
				if (TargetUnit != nullptr)
				{
					OutsideUnit = *TargetUnit;
				}
				else
				{
					const FBlueprintLensLC1NodeFact* TargetNode =
						TypedIrFacts.NodesBySourceNodeId.Find(Edge.TargetNodeId);
					OutsideUnit.Id = FString::Printf(
						TEXT("lc4-seq.outside-unit:%s"), *Edge.TargetNodeId);
					OutsideUnit.Role = EBlueprintLensRole::Boundary;
					OutsideUnit.Kind = EBlueprintLensUnitKind::Node;
					OutsideUnit.Title = TargetNode != nullptr
						? TargetNode->NativeTitle
						: Edge.TargetNodeId;
					OutsideUnit.SemanticStatus =
						EBlueprintLensSemanticStatus::Opaque;
					OutsideUnit.InclusionReasons = {TEXT("lc4_seq_excluded_sibling")};
					FBlueprintLensSourceReference Source;
					Source.BlueprintAssetPath = ReaderExplanation.Source.BlueprintAssetPath;
					Source.GraphId = ReaderExplanation.Source.GraphId;
					Source.SourceNodeId = Edge.TargetNodeId;
					Source.bPrimary = true;
					OutsideUnit.SourceReferences.Add(MoveTemp(Source));
				}
				AddUniqueUnit(Case.Explanation, OutsideUnit);
				FBlueprintLensRelation OutsideRelation;
				OutsideRelation.Id = FString::Printf(
					TEXT("lc4-seq.outside-relation:%s"), *Edge.EdgeId);
				OutsideRelation.SourceUnitId = SequenceUnit->Id;
				OutsideRelation.TargetUnitId = OutsideUnit.Id;
				OutsideRelation.Kind =
					EBlueprintLensRelationKind::ExecutionPredecessor;
				OutsideRelation.SourceEdgeIds = {Edge.EdgeId};
				OutsideRelation.bHasPortLabel = true;
				OutsideRelation.PortLabel = Pin.Name;
				AddUniqueRelation(Case.Explanation, OutsideRelation);
				Output.ReachableNodeIds = {OutsideUnit.Id};
				Output.ReachableEdgeIds = {OutsideRelation.Id};
				Output.TerminationKind = EBlueprintLensLC4TerminationKind::Terminal;
				Output.TerminationNodeId = OutsideUnit.Id;
				++Profile.Counts.OutsideCriterionConnectedOutputs;
			}
			Profile.Outputs.Add(MoveTemp(Output));
		}
		if (Profile.Outputs.Num() != Outputs.Num())
		{
			continue;
		}
		Profile.Counts.DeclaredOutputs = Profile.Outputs.Num();
		for (const FBlueprintLensLC4SequenceOutput& Output : Profile.Outputs)
		{
			Profile.AccountedUnitIds.AddUnique(SequenceUnit->Id);
			for (const FString& UnitId : Output.ReachableNodeIds)
			{
				Profile.AccountedUnitIds.AddUnique(UnitId);
			}
			for (const FString& RelationId : Output.ReachableEdgeIds)
			{
				Profile.AccountedRelationIds.AddUnique(RelationId);
			}
		}
		Case.Explanation.Counts.Units = Case.Explanation.Units.Num();
		Case.Explanation.Counts.Relations = Case.Explanation.Relations.Num();
		if (Profile.IsLiveBounded())
		{
			Result.Cases.Add(MoveTemp(Case));
		}
	}
	if (Result.Cases.IsEmpty())
	{
		Result.DiagnosticCode = Result.CandidateRelationCount == 0
			? TEXT("LC4_SEQUENCE_LIVE_NO_ORDINAL_RELATION")
			: TEXT("LC4_SEQUENCE_LIVE_NO_ADMITTED_ROOT");
	}
	return Result;
}
