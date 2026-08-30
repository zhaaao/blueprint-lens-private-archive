#include "BlueprintLensF12DataAnswerProjection.h"

#include "BlueprintLensDisplayLabel.h"

namespace
{
constexpr TCHAR MemberSetReason[] = TEXT("member_set");
constexpr TCHAR DirectWriteControllerReason[] =
	TEXT("direct_write_controller");
constexpr TCHAR RequiredDataProducerReason[] =
	TEXT("required_data_producer");

bool HasReason(const FBlueprintLensUnit& Unit, const TCHAR* Reason)
{
	return Unit.InclusionReasons.Contains(Reason);
}

bool IsWriteSpineUnit(const FBlueprintLensUnit& Unit)
{
	return HasReason(Unit, MemberSetReason) ||
		HasReason(Unit, DirectWriteControllerReason) ||
		Unit.Role == EBlueprintLensRole::Boundary ||
		Unit.SemanticStatus != EBlueprintLensSemanticStatus::Supported;
}

int32 WriteCount(const FBlueprintLensExplanationModel& Explanation)
{
	int32 Result = 0;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		Result += HasReason(Unit, MemberSetReason) ? 1 : 0;
	}
	return Result;
}

FString ReasonList(const FBlueprintLensUnit& Unit)
{
	return FString::Join(Unit.InclusionReasons, TEXT(", "));
}

FString SourceDiscriminator(const FBlueprintLensUnit& Unit)
{
	const FBlueprintLensSourceReference* Source =
		Unit.SourceReferences.FindByPredicate(
			[](const FBlueprintLensSourceReference& Candidate)
			{
				return Candidate.bPrimary;
			});
	if (Source == nullptr && !Unit.SourceReferences.IsEmpty())
	{
		Source = &Unit.SourceReferences[0];
	}
	if (Source == nullptr)
	{
		return FString();
	}
	if (!Source->NativeNodeGuid.IsEmpty())
	{
		return Source->NativeNodeGuid.Left(8).ToUpper();
	}
	if (!Source->SourceNodeId.IsEmpty())
	{
		const FString NodeMarker = TEXT("::node::");
		const int32 MarkerIndex = Source->SourceNodeId.Find(
			NodeMarker,
			ESearchCase::IgnoreCase,
			ESearchDir::FromEnd);
		const FString Tail = MarkerIndex == INDEX_NONE
			? Source->SourceNodeId
			: Source->SourceNodeId.Mid(MarkerIndex + NodeMarker.Len());
		return Tail.Left(8).ToUpper();
	}
	return FString();
}

FString ValueSourceReaderLabel(
	const FBlueprintLensUnit& Unit,
	const TArray<FString>& ValueSourceUnitIds,
	const FBlueprintLensExplanationModel& Explanation,
	const int32 SourceIndex)
{
	const FString BaseLabel = BlueprintLensDisplayLabel(Unit);
	int32 MatchingLabels = 0;
	for (const FString& UnitId : ValueSourceUnitIds)
	{
		const FBlueprintLensUnit* Candidate = Explanation.FindUnit(UnitId);
		if (Candidate != nullptr &&
			BlueprintLensDisplayLabel(*Candidate) == BaseLabel)
		{
			++MatchingLabels;
		}
	}
	if (MatchingLabels <= 1)
	{
		return BaseLabel;
	}
	const FString Discriminator = SourceDiscriminator(Unit);
	return FString::Printf(
		TEXT("%s (source %s)"),
		*BaseLabel,
		*(!Discriminator.IsEmpty()
			? Discriminator
			: FString::FromInt(SourceIndex + 1)));
}

FBlueprintLensF12DataAnswerProjection Failure(
	const FBlueprintLensExplanationModel& Explanation,
	const TCHAR* DiagnosticCode)
{
	FBlueprintLensF12DataAnswerProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.SourceBlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	Result.DiagnosticCode = DiagnosticCode;
	return Result;
}
} // namespace

FBlueprintLensF12DataRailAdapterResult
FBlueprintLensF12DataRailAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation)
{
	FBlueprintLensF12DataRailAdapterResult Result;
	Result.Explanation = Explanation;
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Explanation.CriterionUnitId);
	if (Criterion == nullptr || !HasReason(*Criterion, MemberSetReason))
	{
		Result.DiagnosticCode = TEXT("F12_DATA_WRITE_CRITERION_MISSING");
		return Result;
	}
	TSet<FString> WriteSpineUnitIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.InclusionReasons.IsEmpty())
		{
			Result.DiagnosticCode =
				TEXT("F12_DATA_INCLUSION_REASON_MISSING");
			return Result;
		}
		if (IsWriteSpineUnit(Unit))
		{
			WriteSpineUnitIds.Add(Unit.Id);
		}
	}
	int32 MappedSpineRelationCount = 0;
	for (FBlueprintLensRelation& Relation : Result.Explanation.Relations)
	{
		if (WriteSpineUnitIds.Contains(Relation.SourceUnitId) &&
			WriteSpineUnitIds.Contains(Relation.TargetUnitId))
		{
			// The packet relation remains untouched. This projection-only view
			// maps every proven write-to-write relation into the generic rail's
			// drawn-edge ledger so the shared three-exit orderer consumes it.
			Relation.Kind =
				EBlueprintLensRelationKind::ExecutionPredecessor;
			++MappedSpineRelationCount;
		}
	}
	if (WriteSpineUnitIds.Num() < 2 || MappedSpineRelationCount == 0)
	{
		Result.DiagnosticCode = TEXT("F12_DATA_WRITE_SPINE_UNCONNECTED");
		return Result;
	}

	// The member-level packet keeps its deterministic criterion anchor intact,
	// but the generic LC1 rail requires its one docked criterion to be terminal
	// in the proven station order.  A multi-write answer can legitimately place
	// the packet anchor before a later write (controller -> Set1 -> Set2).  Select
	// a deterministic terminal Set only in this projection copy; the accepted
	// packet and all member_set roles remain unchanged and fully represented.
	TSet<FString> NonTerminalWriteIds;
	for (const FBlueprintLensRelation& Relation : Result.Explanation.Relations)
	{
		if (Relation.SourceUnitId != Relation.TargetUnitId &&
			WriteSpineUnitIds.Contains(Relation.SourceUnitId) &&
			WriteSpineUnitIds.Contains(Relation.TargetUnitId))
		{
			const FBlueprintLensUnit* Source =
				Result.Explanation.FindUnit(Relation.SourceUnitId);
			if (Source != nullptr && HasReason(*Source, MemberSetReason))
			{
				NonTerminalWriteIds.Add(Source->Id);
			}
		}
	}
	if (NonTerminalWriteIds.Contains(Result.Explanation.CriterionUnitId))
	{
		TArray<FString> TerminalWriteIds;
		for (const FBlueprintLensUnit& Unit : Result.Explanation.Units)
		{
			if (HasReason(Unit, MemberSetReason) &&
				!NonTerminalWriteIds.Contains(Unit.Id))
			{
				TerminalWriteIds.Add(Unit.Id);
			}
		}
		TerminalWriteIds.Sort();
		if (!TerminalWriteIds.IsEmpty())
		{
			Result.Explanation.CriterionUnitId = TerminalWriteIds[0];
		}
	}
	Result.DiagnosticCode = TEXT("F12_DATA_RAIL_ADAPTED");
	return Result;
}

bool FBlueprintLensF12DataAnswerProjection::IsRenderable(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1RailProjection& Rail) const
{
	const int32 ExpectedWriteCount = WriteCount(Explanation);
	if (DiagnosticCode != TEXT("F12_DATA_ANSWER_COMPLETE") ||
		SourceIrSha256 != Explanation.Source.IrSha256 ||
		SourceBlueprintAssetPath != Explanation.Source.BlueprintAssetPath ||
		!Rail.IsRenderable() || SourceIrSha256 != Rail.SourceIrSha256 ||
		AnswerWriteCount <= 0 || AnswerWriteCount != ExpectedWriteCount ||
		AnswerUnitCount != Explanation.Units.Num() ||
		AnswerRelationCount != Explanation.Relations.Num() ||
		Stations.Num() != Rail.OrderedCanonicalUnits.Num())
	{
		return false;
	}

	TSet<FString> SeenStationIds;
	TSet<FString> CoveredUnitIds;
	for (int32 Index = 0; Index < Stations.Num(); ++Index)
	{
		const FBlueprintLensF12DataStationDisclosure& Station =
			Stations[Index];
		if (Station.StationUnitId !=
				Rail.OrderedCanonicalUnits[Index].UnitId ||
			SeenStationIds.Contains(Station.StationUnitId) ||
			Station.MarkerText.IsEmpty() || Station.DetailLines.IsEmpty())
		{
			return false;
		}
		SeenStationIds.Add(Station.StationUnitId);
		CoveredUnitIds.Add(Station.StationUnitId);
		for (const FString& UnitId : Station.AttachedUnitIds)
		{
			if (UnitId.IsEmpty() || CoveredUnitIds.Contains(UnitId))
			{
				return false;
			}
			CoveredUnitIds.Add(UnitId);
		}
		TSet<FString> ExpectedValueSourceIds;
		for (const FString& UnitId : Station.AttachedUnitIds)
		{
			const FBlueprintLensUnit* AttachedUnit =
				Explanation.FindUnit(UnitId);
			if (AttachedUnit != nullptr &&
				HasReason(*AttachedUnit, RequiredDataProducerReason))
			{
				ExpectedValueSourceIds.Add(UnitId);
			}
		}
		const bool bValueSourceCoverMatches =
			ExpectedValueSourceIds.Num() ==
				Station.ValueSourceUnitIds.Num() &&
			Station.ValueSourceUnitIds.ContainsByPredicate(
				[&ExpectedValueSourceIds](const FString& UnitId)
				{
					return !ExpectedValueSourceIds.Contains(UnitId);
				}) == false;
		const bool bExpectedBound =
			ExpectedValueSourceIds.Num() >
				BlueprintLensF12DataAnswerBounds::
					MaxValueSourcesPerStation;
		const int32 ExpectedDetailLineCount =
			FMath::Min(
				ExpectedValueSourceIds.Num(),
				BlueprintLensF12DataAnswerBounds::
					MaxValueSourcesPerStation) +
			(bExpectedBound ? 1 : 0);
		if (!bValueSourceCoverMatches ||
			Station.ValueSourceMarkerText.IsEmpty() ||
			Station.bValueSourceDisclosureBounded != bExpectedBound ||
			Station.ValueSourceDetailLines.Num() !=
				ExpectedDetailLineCount)
		{
			return false;
		}
	}
	return CoveredUnitIds.Num() == Explanation.Units.Num();
}

FBlueprintLensF12DataAnswerProjection
FBlueprintLensF12DataAnswerProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1RailProjection& Rail)
{
	if (!Rail.IsRenderable() ||
		Rail.SourceIrSha256 != Explanation.Source.IrSha256)
	{
		return Failure(Explanation, TEXT("F12_DATA_RAIL_UNAVAILABLE"));
	}
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Explanation.CriterionUnitId);
	if (Criterion == nullptr || !HasReason(*Criterion, MemberSetReason))
	{
		return Failure(Explanation, TEXT("F12_DATA_WRITE_CRITERION_MISSING"));
	}

	TSet<FString> RailUnitIds;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		Rail.OrderedCanonicalUnits)
	{
		RailUnitIds.Add(Unit.UnitId);
	}
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.InclusionReasons.IsEmpty() ||
			(IsWriteSpineUnit(Unit) != RailUnitIds.Contains(Unit.Id)))
		{
			return Failure(
				Explanation, TEXT("F12_DATA_WRITE_SPINE_COVERAGE_FAILED"));
		}
	}

	TMap<FString, TArray<FString>> Successors;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		Successors.Add(Unit.Id, TArray<FString>());
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		TArray<FString>* Next = Successors.Find(Relation.SourceUnitId);
		if (Next == nullptr ||
			Explanation.FindUnit(Relation.TargetUnitId) == nullptr)
		{
			return Failure(
				Explanation, TEXT("F12_DATA_RELATION_MEMBERSHIP_FAILED"));
		}
		Next->AddUnique(Relation.TargetUnitId);
	}

	TMap<FString, int32> RailIndexByUnitId;
	FBlueprintLensF12DataAnswerProjection Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.SourceBlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	Result.AnswerWriteCount = WriteCount(Explanation);
	Result.AnswerUnitCount = Explanation.Units.Num();
	Result.AnswerRelationCount = Explanation.Relations.Num();
	for (int32 Index = 0; Index < Rail.OrderedCanonicalUnits.Num(); ++Index)
	{
		const FString& StationUnitId =
			Rail.OrderedCanonicalUnits[Index].UnitId;
		RailIndexByUnitId.Add(StationUnitId, Index);
		FBlueprintLensF12DataStationDisclosure Station;
		Station.StationUnitId = StationUnitId;
		Result.Stations.Add(MoveTemp(Station));
	}

	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (RailUnitIds.Contains(Unit.Id))
		{
			continue;
		}
		TArray<TPair<FString, int32>> Pending = {{Unit.Id, 0}};
		TMap<FString, int32> BestDistanceByUnitId;
		BestDistanceByUnitId.Add(Unit.Id, 0);
		int32 BestRailIndex = INDEX_NONE;
		int32 BestDistance = MAX_int32;
		while (!Pending.IsEmpty())
		{
			const TPair<FString, int32> Current =
				Pending.Pop(EAllowShrinking::No);
			if (Current.Value > BestDistance)
			{
				continue;
			}
			if (const int32* RailIndex =
					RailIndexByUnitId.Find(Current.Key))
			{
				if (Current.Value < BestDistance ||
					(Current.Value == BestDistance &&
						(BestRailIndex == INDEX_NONE ||
							*RailIndex < BestRailIndex)))
				{
					BestDistance = Current.Value;
					BestRailIndex = *RailIndex;
				}
				continue;
			}
			const TArray<FString>* Next = Successors.Find(Current.Key);
			if (Next == nullptr)
			{
				continue;
			}
			for (const FString& NextUnitId : *Next)
			{
				const int32 NextDistance = Current.Value + 1;
				const int32* PreviousDistance =
					BestDistanceByUnitId.Find(NextUnitId);
				if (PreviousDistance == nullptr ||
					NextDistance < *PreviousDistance)
				{
					BestDistanceByUnitId.Add(NextUnitId, NextDistance);
					Pending.Emplace(NextUnitId, NextDistance);
				}
			}
		}
		if (BestRailIndex == INDEX_NONE)
		{
			return Failure(
				Explanation, TEXT("F12_DATA_VALUE_PLACEMENT_FAILED"));
		}
		Result.Stations[BestRailIndex].AttachedUnitIds.Add(Unit.Id);
	}

	for (FBlueprintLensF12DataStationDisclosure& Station : Result.Stations)
	{
		const FBlueprintLensUnit* StationUnit =
			Explanation.FindUnit(Station.StationUnitId);
		if (StationUnit == nullptr)
		{
			return Failure(
				Explanation, TEXT("F12_DATA_WRITE_SPINE_COVERAGE_FAILED"));
		}
		Station.DetailLines.Add(FString::Printf(
			TEXT("%s · reason: %s"),
			*BlueprintLensDisplayLabel(*StationUnit),
			*ReasonList(*StationUnit)));
		for (const FString& AttachedUnitId : Station.AttachedUnitIds)
		{
			const FBlueprintLensUnit* AttachedUnit =
				Explanation.FindUnit(AttachedUnitId);
			if (AttachedUnit == nullptr)
			{
				return Failure(
					Explanation, TEXT("F12_DATA_VALUE_PLACEMENT_FAILED"));
			}
			for (const FString& Reason : AttachedUnit->InclusionReasons)
			{
				if (Reason == RequiredDataProducerReason)
				{
					Station.ValueSourceUnitIds.AddUnique(AttachedUnitId);
				}
			}
			Station.DetailLines.Add(FString::Printf(
				TEXT("%s · reason: %s"),
				*BlueprintLensDisplayLabel(*AttachedUnit),
				*ReasonList(*AttachedUnit)));
		}
		const bool bStationIsRequiredProducer =
			HasReason(*StationUnit, RequiredDataProducerReason);
		if (bStationIsRequiredProducer)
		{
			TArray<FString> OtherReasons = StationUnit->InclusionReasons;
			OtherReasons.Remove(RequiredDataProducerReason);
			Station.MarkerText = FString::Printf(
				TEXT("Why this write is included · this unit is itself a required data producer%s"),
				OtherReasons.IsEmpty()
					? TEXT("")
					: *FString::Printf(
						TEXT("; also %s"),
						*FString::Join(OtherReasons, TEXT(", "))));
		}
		else
		{
			Station.MarkerText = FString::Printf(
				TEXT("Why this write is included · %s"),
				*FString::Join(
					StationUnit->InclusionReasons,
					TEXT(", ")));
		}
		Station.bValueSourceDisclosureBounded =
			Station.ValueSourceUnitIds.Num() >
				BlueprintLensF12DataAnswerBounds::
					MaxValueSourcesPerStation;
		if (Station.ValueSourceUnitIds.IsEmpty())
		{
			Station.ValueSourceMarkerText = bStationIsRequiredProducer
				? TEXT("Value sources feeding this write · No other required data producer feeds this write.")
				: TEXT("Value sources feeding this write · No required data producer feeds this write.");
		}
		else
		{
			const int32 DisclosedSourceCount = FMath::Min(
				Station.ValueSourceUnitIds.Num(),
				BlueprintLensF12DataAnswerBounds::
					MaxValueSourcesPerStation);
			Station.ValueSourceMarkerText =
				Station.bValueSourceDisclosureBounded
					? FString::Printf(
						TEXT("Value sources feeding this write · required_data_producer · Open %d of %d producer identities; %d remain outside this station view."),
						DisclosedSourceCount,
						Station.ValueSourceUnitIds.Num(),
						Station.ValueSourceUnitIds.Num() -
							DisclosedSourceCount)
					: FString::Printf(
						TEXT("Value sources feeding this write · required_data_producer · Open %d producer %s."),
						DisclosedSourceCount,
						DisclosedSourceCount == 1
							? TEXT("identity")
							: TEXT("identities"));
			for (int32 SourceIndex = 0;
				 SourceIndex < DisclosedSourceCount;
				 ++SourceIndex)
			{
				const FBlueprintLensUnit* ValueSource =
					Explanation.FindUnit(
						Station.ValueSourceUnitIds[SourceIndex]);
				if (ValueSource == nullptr)
				{
					return Failure(
						Explanation,
						TEXT("F12_DATA_VALUE_PLACEMENT_FAILED"));
				}
				Station.ValueSourceDetailLines.Add(
					ValueSourceReaderLabel(
						*ValueSource,
						Station.ValueSourceUnitIds,
						Explanation,
						SourceIndex));
			}
			if (Station.bValueSourceDisclosureBounded)
			{
				Station.ValueSourceDetailLines.Add(FString::Printf(
					TEXT("Source disclosure limit · %d additional producer identities are not shown here; this answer remains admitted."),
					Station.ValueSourceUnitIds.Num() -
						DisclosedSourceCount));
			}
		}
	}

	Result.DiagnosticCode = TEXT("F12_DATA_ANSWER_COMPLETE");
	if (!Result.IsRenderable(Explanation, Rail))
	{
		return Failure(Explanation, TEXT("F12_DATA_ANSWER_INVARIANT_FAILED"));
	}
	return Result;
}

FBlueprintLensCompositeRailSlots FBlueprintLensF12DataAnswerProjector::Apply(
	const FBlueprintLensF12DataAnswerProjection& Projection,
	const FBlueprintLensCompositeRailSlots& BaseSlots)
{
	FBlueprintLensCompositeRailSlots Candidate = BaseSlots;
	if (Projection.DiagnosticCode != TEXT("F12_DATA_ANSWER_COMPLETE") ||
		Projection.SourceIrSha256 != BaseSlots.SourceIrSha256 ||
		Projection.SourceBlueprintAssetPath !=
			BaseSlots.SourceBlueprintAssetPath)
	{
		Candidate.DiagnosticCode = TEXT("F12_DATA_SLOT_SOURCE_MISMATCH");
		return Candidate;
	}
	Candidate.DataAnswerUnitCount = Projection.AnswerUnitCount;
	Candidate.DataAnswerRelationCount = Projection.AnswerRelationCount;
	Candidate.DataAnswerWriteCount = Projection.AnswerWriteCount;
	for (const FBlueprintLensF12DataStationDisclosure& Disclosure :
		Projection.Stations)
	{
		FBlueprintLensCompositeStationSlot* Station =
			Candidate.FindStation(Disclosure.StationUnitId);
		if (Station == nullptr)
		{
			Candidate.DiagnosticCode = TEXT("F12_DATA_SLOT_COVERAGE_FAILED");
			return Candidate;
		}
		FBlueprintLensCompositeAttachment Attachment;
		Attachment.GrammarId = TEXT("F12");
		Attachment.AttachmentId = TEXT("f12:") + Disclosure.StationUnitId;
		Attachment.MarkerText = Disclosure.MarkerText;
		Attachment.DetailLines = Disclosure.DetailLines;
		FBlueprintLensCompositeAttachment ValueSourceAttachment;
		ValueSourceAttachment.GrammarId = TEXT("LC3");
		ValueSourceAttachment.AttachmentId =
			Disclosure.ValueSourceUnitIds.IsEmpty()
				? TEXT("f12-value-empty:") + Disclosure.StationUnitId
				: Disclosure.bValueSourceDisclosureBounded
					? TEXT("f12-value-bounded:") + Disclosure.StationUnitId
					: TEXT("f12-value:") + Disclosure.StationUnitId;
		ValueSourceAttachment.MarkerText =
			Disclosure.ValueSourceMarkerText;
		ValueSourceAttachment.DetailLines =
			Disclosure.ValueSourceDetailLines;
		FBlueprintLensCompositeTerminalCapSlot* TerminalCap =
			Candidate.TerminalCaps.FindByPredicate(
				[&Disclosure](
					const FBlueprintLensCompositeTerminalCapSlot& Cap)
				{
					return Cap.UnitId == Disclosure.StationUnitId;
				});
		if (TerminalCap != nullptr)
		{
			TerminalCap->Attachments.Add(MoveTemp(Attachment));
			TerminalCap->Attachments.Add(
				MoveTemp(ValueSourceAttachment));
		}
		else
		{
			Station->BesideAttachments.Add(MoveTemp(Attachment));
			Station->BesideAttachments.Add(
				MoveTemp(ValueSourceAttachment));
		}
	}
	return Candidate;
}
