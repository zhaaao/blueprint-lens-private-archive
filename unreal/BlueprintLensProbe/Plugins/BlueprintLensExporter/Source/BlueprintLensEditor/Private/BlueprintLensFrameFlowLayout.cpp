#include "BlueprintLensFrameFlowLayout.h"

namespace
{
FBlueprintLensFrameFlowCounts CountFacts(
	const FBlueprintLensExplanationModel& Explanation,
	const TArray<FString>& UnitIds,
	const int32 RelationCount)
{
	FBlueprintLensFrameFlowCounts Counts;
	Counts.UnitCount = UnitIds.Num();
	Counts.RelationCount = RelationCount;
	TSet<FString> SourceNodeIds;
	for (const FString& UnitId : UnitIds)
	{
		const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
		if (Unit == nullptr)
		{
			continue;
		}
		Counts.SemanticStatusCounts.FindOrAdd(Unit->SemanticStatus)++;
		for (const FBlueprintLensSourceReference& Reference :
			 Unit->SourceReferences)
		{
			SourceNodeIds.Add(Reference.SourceNodeId);
		}
	}
	Counts.UniqueSourceNodeCount = SourceNodeIds.Num();
	return Counts;
}

FBlueprintLensFrameFlowLayoutModel Failure(
	const EBlueprintLensFrameFlowLayoutStatus Status,
	const FString& Diagnostic)
{
	FBlueprintLensFrameFlowLayoutModel Result;
	Result.Status = Status;
	Result.Diagnostics.Add(Diagnostic);
	return Result;
}

const FBlueprintLensLane* FindBoundaryLane(
	const FBlueprintLensExplanationModel& Explanation)
{
	return Explanation.Lanes.FindByPredicate(
		[](const FBlueprintLensLane& Lane)
		{
			return Lane.Role == EBlueprintLensRole::Boundary;
		});
}

bool ValidateCoverage(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	FString& Error)
{
	TSet<FString> OwnedUnitIds;
	TSet<FString> OwnedRelationIds;
	for (const FBlueprintLensFrameFlowSegment& Segment : Layout.Segments)
	{
		for (const FString& UnitId : Segment.MemberUnitIds)
		{
			if (OwnedUnitIds.Contains(UnitId))
			{
				Error =
					FString::Printf(TEXT("unit '%s' has multiple segments"), *UnitId);
				return false;
			}
			OwnedUnitIds.Add(UnitId);
		}
		for (const FString& RelationId : Segment.MemberRelationIds)
		{
			if (OwnedRelationIds.Contains(RelationId))
			{
				Error = FString::Printf(
					TEXT("relation '%s' has multiple owners"),
					*RelationId);
				return false;
			}
			OwnedRelationIds.Add(RelationId);
		}
	}
	for (const FBlueprintLensFrameFlowSegmentEdge& Edge : Layout.SegmentEdges)
	{
		for (const FString& RelationId : Edge.RelationIds)
		{
			if (OwnedRelationIds.Contains(RelationId))
			{
				Error = FString::Printf(
					TEXT("relation '%s' has multiple owners"),
					*RelationId);
				return false;
			}
			OwnedRelationIds.Add(RelationId);
		}
	}

	if (OwnedUnitIds.Num() != Explanation.Units.Num())
	{
		Error = TEXT("segments do not account for every explanation unit");
		return false;
	}
	if (OwnedRelationIds.Num() != Explanation.Relations.Num())
	{
		Error = TEXT("segments do not account for every explanation relation");
		return false;
	}
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (!OwnedUnitIds.Contains(Unit.Id))
		{
			Error = FString::Printf(TEXT("unit '%s' is unaccounted"), *Unit.Id);
			return false;
		}
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!OwnedRelationIds.Contains(Relation.Id))
		{
			Error =
				FString::Printf(TEXT("relation '%s' is unaccounted"), *Relation.Id);
			return false;
		}
	}
	return true;
}
} // namespace

FBlueprintLensFrameFlowLayoutModel
FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
	const FBlueprintLensExplanationModel& Explanation)
{
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Explanation.CriterionUnitId);
	if (Criterion == nullptr)
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::MissingCriterion,
			TEXT("FrameFlow linear layout criterion is missing"));
	}
	if (Explanation.Units.Num() < 3)
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
			TEXT("FrameFlow linear layout requires entry, run, and criterion units"));
	}

	TMap<FString, TArray<const FBlueprintLensRelation*>> Incoming;
	TMap<FString, TArray<const FBlueprintLensRelation*>> Outgoing;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.SemanticStatus != EBlueprintLensSemanticStatus::Supported)
		{
			return Failure(
				EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
				FString::Printf(
					TEXT("FrameFlow linear layout cannot contract non-supported unit '%s'"),
					*Unit.Id));
		}
		Incoming.Add(Unit.Id);
		Outgoing.Add(Unit.Id);
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		if (!Incoming.Contains(Relation.SourceUnitId) ||
			!Incoming.Contains(Relation.TargetUnitId))
		{
			return Failure(
				EBlueprintLensFrameFlowLayoutStatus::DanglingRelation,
				FString::Printf(
					TEXT("FrameFlow relation '%s' has a dangling endpoint"),
					*Relation.Id));
		}
		if (Relation.Kind != EBlueprintLensRelationKind::ExecutionPredecessor)
		{
			return Failure(
				EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
				FString::Printf(
					TEXT("FrameFlow LC1 profile rejects relation '%s' of kind '%s'"),
					*Relation.Id,
					LexToString(Relation.Kind)));
		}
		Outgoing.FindChecked(Relation.SourceUnitId).Add(&Relation);
		Incoming.FindChecked(Relation.TargetUnitId).Add(&Relation);
	}

	TArray<FString> EntryIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		const int32 InDegree = Incoming.FindChecked(Unit.Id).Num();
		const int32 OutDegree = Outgoing.FindChecked(Unit.Id).Num();
		if (InDegree > 1 || OutDegree > 1)
		{
			return Failure(
				EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
				FString::Printf(
					TEXT("FrameFlow LC1 profile rejects fan-out or reconvergence at '%s'"),
					*Unit.Id));
		}
		if (InDegree == 0)
		{
			EntryIds.Add(Unit.Id);
		}
	}
	EntryIds.Sort();
	if (EntryIds.Num() != 1)
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
			FString::Printf(
				TEXT("FrameFlow linear layout requires one entry; found %d"),
				EntryIds.Num()));
	}
	if (!Outgoing.FindChecked(Criterion->Id).IsEmpty())
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
			TEXT("FrameFlow linear criterion must terminate the chain"));
	}

	TArray<const FBlueprintLensUnit*> OrderedUnits;
	TArray<const FBlueprintLensRelation*> OrderedRelations;
	TSet<FString> Visited;
	const FBlueprintLensUnit* Current =
		Explanation.FindUnit(EntryIds[0]);
	while (Current != nullptr)
	{
		if (Visited.Contains(Current->Id))
		{
			return Failure(
				EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
				TEXT("FrameFlow LC1 profile rejects cycles"));
		}
		Visited.Add(Current->Id);
		OrderedUnits.Add(Current);
		const TArray<const FBlueprintLensRelation*>& Next =
			Outgoing.FindChecked(Current->Id);
		if (Next.IsEmpty())
		{
			break;
		}
		OrderedRelations.Add(Next[0]);
		Current = Explanation.FindUnit(Next[0]->TargetUnitId);
	}
	if (OrderedUnits.Last() != Criterion ||
		OrderedUnits.Num() != Explanation.Units.Num() ||
		OrderedRelations.Num() != Explanation.Relations.Num())
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
			TEXT("FrameFlow linear chain must cover all facts and end at the criterion"));
	}

	FBlueprintLensFrameFlowLayoutModel Result;
	Result.Question = Explanation.Query.Question;
	Result.CriterionUnitId = Explanation.CriterionUnitId;

	FBlueprintLensFrameFlowSegment Entry;
	Entry.Id = TEXT("segment.entry.") + OrderedUnits[0]->Id;
	Entry.Kind = EBlueprintLensFrameFlowSegmentKind::Entry;
	Entry.MemberUnitIds.Add(OrderedUnits[0]->Id);
	Entry.OutgoingRelationIds.Add(OrderedRelations[0]->Id);
	Entry.DisplayRank = 0;
	Entry.Counts = CountFacts(Explanation, Entry.MemberUnitIds, 0);

	FBlueprintLensFrameFlowSegment Run;
	Run.Id = FString::Printf(
		TEXT("segment.straight-run.%s.%s"),
		*OrderedUnits[1]->Id,
		*OrderedUnits[OrderedUnits.Num() - 2]->Id);
	Run.Kind = EBlueprintLensFrameFlowSegmentKind::StraightRun;
	for (int32 Index = 1; Index < OrderedUnits.Num() - 1; ++Index)
	{
		Run.MemberUnitIds.Add(OrderedUnits[Index]->Id);
	}
	for (int32 Index = 1; Index < OrderedRelations.Num() - 1; ++Index)
	{
		Run.MemberRelationIds.Add(OrderedRelations[Index]->Id);
	}
	Run.IncomingRelationIds.Add(OrderedRelations[0]->Id);
	Run.OutgoingRelationIds.Add(OrderedRelations.Last()->Id);
	Run.DisplayRank = 1;
	Run.bCollapsible = true;
	Run.bCollapsedByDefault = true;
	Run.Counts = CountFacts(
		Explanation,
		Run.MemberUnitIds,
		Run.MemberRelationIds.Num());

	FBlueprintLensFrameFlowSegment CriterionSegment;
	CriterionSegment.Id = TEXT("segment.criterion.") + Criterion->Id;
	CriterionSegment.Kind = EBlueprintLensFrameFlowSegmentKind::CriterionFocus;
	CriterionSegment.MemberUnitIds.Add(Criterion->Id);
	CriterionSegment.IncomingRelationIds.Add(OrderedRelations.Last()->Id);
	CriterionSegment.DisplayRank = 2;
	CriterionSegment.Counts =
		CountFacts(Explanation, CriterionSegment.MemberUnitIds, 0);

	Result.Segments = {Entry, Run, CriterionSegment};

	FBlueprintLensFrameFlowSegmentEdge EntryToRun;
	EntryToRun.SourceSegmentId = Entry.Id;
	EntryToRun.TargetSegmentId = Run.Id;
	EntryToRun.RelationIds.Add(OrderedRelations[0]->Id);
	EntryToRun.Kind = OrderedRelations[0]->Kind;
	EntryToRun.Label = OrderedRelations[0]->Label;

	FBlueprintLensFrameFlowSegmentEdge RunToCriterion;
	RunToCriterion.SourceSegmentId = Run.Id;
	RunToCriterion.TargetSegmentId = CriterionSegment.Id;
	RunToCriterion.RelationIds.Add(OrderedRelations.Last()->Id);
	RunToCriterion.Kind = OrderedRelations.Last()->Kind;
	RunToCriterion.Label = OrderedRelations.Last()->Label;
	Result.SegmentEdges = {EntryToRun, RunToCriterion};

	TArray<FString> AllUnitIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		AllUnitIds.Add(Unit.Id);
	}
	Result.TruthCounts =
		CountFacts(Explanation, AllUnitIds, Explanation.Relations.Num());
	const FBlueprintLensLane* Boundary = FindBoundaryLane(Explanation);
	if (Boundary != nullptr)
	{
		Result.BoundaryLaneState = Boundary->State;
		Result.BoundaryMessage = Boundary->EmptyMessage;
	}
	Result.DefaultSelectedSegmentId = CriterionSegment.Id;
	Result.Status = EBlueprintLensFrameFlowLayoutStatus::Ready;

	FString CoverageError;
	if (!ValidateCoverage(Explanation, Result, CoverageError))
	{
		return Failure(
			EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape,
			CoverageError);
	}
	return Result;
}

FBlueprintLensFrameFlowDetailWindow
FBlueprintLensFrameFlowLayoutBuilder::BuildDetailWindow(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FString& SegmentId,
	const FString& AnchorUnitId,
	const int32 MaximumVisibleUnits)
{
	FBlueprintLensFrameFlowDetailWindow Result;
	Result.SegmentId = SegmentId;
	Result.AnchorUnitId = AnchorUnitId;
	if (!Layout.IsReady())
	{
		Result.Error = TEXT("FrameFlow detail requires a ready layout");
		return Result;
	}
	const FBlueprintLensFrameFlowSegment* Segment =
		Layout.Segments.FindByPredicate(
			[&SegmentId](const FBlueprintLensFrameFlowSegment& Candidate)
			{
				return Candidate.Id == SegmentId;
			});
	if (Segment == nullptr ||
		Segment->Kind != EBlueprintLensFrameFlowSegmentKind::StraightRun)
	{
		Result.Error = TEXT("FrameFlow detail requires a straight-run segment");
		return Result;
	}
	const int32 AnchorIndex =
		Segment->MemberUnitIds.IndexOfByKey(AnchorUnitId);
	if (AnchorIndex == INDEX_NONE)
	{
		Result.Error = TEXT("FrameFlow detail anchor is not in the segment");
		return Result;
	}
	if (MaximumVisibleUnits <= 0)
	{
		Result.Error = TEXT("FrameFlow detail capacity must be positive");
		return Result;
	}

	const int32 StartIndex =
		FMath::Max(0, AnchorIndex - MaximumVisibleUnits + 1);
	const int32 EndIndex = AnchorIndex;
	for (int32 Index = 0; Index < Segment->MemberUnitIds.Num(); ++Index)
	{
		TArray<FString>* Owner =
			Index < StartIndex
				? &Result.HiddenPrefixUnitIds
				: Index <= EndIndex
				? &Result.VisibleUnitIds
				: &Result.HiddenSuffixUnitIds;
		Owner->Add(Segment->MemberUnitIds[Index]);
	}
	for (int32 Index = 0; Index < Segment->MemberRelationIds.Num(); ++Index)
	{
		TArray<FString>* Owner =
			Index < StartIndex
				? &Result.HiddenPrefixRelationIds
				: Index <= EndIndex
				? &Result.VisibleRelationIds
				: &Result.HiddenSuffixRelationIds;
		Owner->Add(Segment->MemberRelationIds[Index]);
	}
	if (!Result.HiddenPrefixUnitIds.IsEmpty())
	{
		Result.AdjacentAnchorUnitIds.Add(
			Result.HiddenPrefixUnitIds.Last());
	}
	if (!Result.HiddenSuffixUnitIds.IsEmpty())
	{
		Result.AdjacentAnchorUnitIds.Add(
			Result.HiddenSuffixUnitIds[0]);
	}

	const int32 PartitionedRelations =
		Result.VisibleRelationIds.Num() +
		Result.HiddenPrefixRelationIds.Num() +
		Result.HiddenSuffixRelationIds.Num();
	if (PartitionedRelations != Segment->MemberRelationIds.Num())
	{
		Result.Error =
			TEXT("FrameFlow detail relation partition is incomplete");
	}
	return Result;
}
