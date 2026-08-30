#include "BlueprintLensLC1Disclosure.h"

#include "Misc/SecureHash.h"

namespace
{
void AppendIds(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<FString>& Ids)
{
	Canonical += Label;
	Canonical += TEXT("[");
	for (const FString& Id : Ids)
	{
		Canonical += FString::Printf(TEXT("%d:%s;"), Id.Len(), *Id);
	}
	Canonical += TEXT("]");
}

TSet<FString> ToSet(const TArray<FString>& Values)
{
	TSet<FString> Result;
	for (const FString& Value : Values)
	{
		Result.Add(Value);
	}
	return Result;
}

bool SetsEqual(
	const TSet<FString>& Left,
	const TSet<FString>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	for (const FString& Value : Left)
	{
		if (!Right.Contains(Value))
		{
			return false;
		}
	}
	return true;
}
} // namespace

FString FBlueprintLensLC1DisclosureProjector::HashLayoutModel(
	const FBlueprintLensFrameFlowLayoutModel& Layout)
{
	FString Canonical = FString::Printf(
		TEXT("question=%d:%s;criterion=%d:%s;default=%d:%s;status=%d;")
		TEXT("truth=%d,%d,%d;boundary=%d:%s;"),
		Layout.Question.Len(),
		*Layout.Question,
		Layout.CriterionUnitId.Len(),
		*Layout.CriterionUnitId,
		Layout.DefaultSelectedSegmentId.Len(),
		*Layout.DefaultSelectedSegmentId,
		static_cast<int32>(Layout.Status),
		Layout.TruthCounts.UnitCount,
		Layout.TruthCounts.RelationCount,
		Layout.TruthCounts.UniqueSourceNodeCount,
		static_cast<int32>(Layout.BoundaryLaneState),
		*Layout.BoundaryMessage);

	for (const FBlueprintLensFrameFlowSegment& Segment : Layout.Segments)
	{
		Canonical += FString::Printf(
			TEXT("segment=%d:%s,%d,%d,%d,%d,%d;"),
			Segment.Id.Len(),
			*Segment.Id,
			static_cast<int32>(Segment.Kind),
			Segment.DisplayRank,
			Segment.bDisplayOrderIsSemantic ? 1 : 0,
			Segment.bCollapsible ? 1 : 0,
			Segment.bCollapsedByDefault ? 1 : 0);
		AppendIds(Canonical, TEXT("units"), Segment.MemberUnitIds);
		AppendIds(
			Canonical,
			TEXT("relations"),
			Segment.MemberRelationIds);
		AppendIds(
			Canonical,
			TEXT("incoming"),
			Segment.IncomingRelationIds);
		AppendIds(
			Canonical,
			TEXT("outgoing"),
			Segment.OutgoingRelationIds);
	}
	for (const FBlueprintLensFrameFlowSegmentEdge& Edge :
		 Layout.SegmentEdges)
	{
		Canonical += FString::Printf(
			TEXT("edge=%d:%s>%d:%s,%d:%s,%d;"),
			Edge.SourceSegmentId.Len(),
			*Edge.SourceSegmentId,
			Edge.TargetSegmentId.Len(),
			*Edge.TargetSegmentId,
			Edge.Label.Len(),
			*Edge.Label,
			static_cast<int32>(Edge.Kind));
		AppendIds(Canonical, TEXT("edge-relations"), Edge.RelationIds);
	}
	AppendIds(Canonical, TEXT("diagnostics"), Layout.Diagnostics);
	return FMD5::HashAnsiString(*Canonical);
}

FBlueprintLensLC1DisclosureProjection
FBlueprintLensLC1DisclosureProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const EBlueprintLensLC1DisclosureCandidate Candidate,
	const FString& DetailAnchorUnitId,
	const int32 MaximumVisibleUnits)
{
	return Build(
		Explanation,
		Layout,
		Candidate,
		DetailAnchorUnitId,
		MaximumVisibleUnits,
		FBlueprintLensLC1RegionProjection(),
		FBlueprintLensLC1PseudocodeProjection());
}

FBlueprintLensLC1DisclosureProjection
FBlueprintLensLC1DisclosureProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const EBlueprintLensLC1DisclosureCandidate Candidate,
	const FString& DetailAnchorUnitId,
	const int32 MaximumVisibleUnits,
	const FBlueprintLensLC1RegionProjection& Region)
{
	return Build(
		Explanation,
		Layout,
		Candidate,
		DetailAnchorUnitId,
		MaximumVisibleUnits,
		Region,
		FBlueprintLensLC1PseudocodeProjection());
}

FBlueprintLensLC1DisclosureProjection
FBlueprintLensLC1DisclosureProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const EBlueprintLensLC1DisclosureCandidate Candidate,
	const FString& DetailAnchorUnitId,
	const int32 MaximumVisibleUnits,
	const FBlueprintLensLC1RegionProjection& Region,
	const FBlueprintLensLC1PseudocodeProjection& Pseudocode)
{
	FBlueprintLensLC1DisclosureProjection Result;
	Result.Candidate = Candidate;
	if (!Layout.IsReady())
	{
		Result.Error = TEXT("LC1 disclosure requires a ready layout");
		return Result;
	}
	if (Explanation.Units.Num() != 14 ||
		Explanation.Relations.Num() != 13 ||
		Layout.TruthCounts.UnitCount != 14 ||
		Layout.TruthCounts.RelationCount != 13 ||
		Layout.Segments.Num() != 3 ||
		Layout.Segments[0].Kind !=
			EBlueprintLensFrameFlowSegmentKind::Entry ||
		Layout.Segments[1].Kind !=
			EBlueprintLensFrameFlowSegmentKind::StraightRun ||
		Layout.Segments[2].Kind !=
			EBlueprintLensFrameFlowSegmentKind::CriterionFocus ||
		Layout.Segments[1].MemberUnitIds.Num() != 12)
	{
		Result.Error =
			TEXT("LC1 disclosure requires the frozen 14/13 linear profile");
		return Result;
	}

	const FBlueprintLensFrameFlowSegment& Run = Layout.Segments[1];
	if (Candidate
		== EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions
		|| Candidate
			== EBlueprintLensLC1DisclosureCandidate::PairedPseudocode)
	{
		if (!Region.HasValidIntegrity()
			|| !Region.IsRenderable()
			|| !Region.SourceIrSha256.Equals(
				Explanation.Source.IrSha256,
				ESearchCase::IgnoreCase)
			|| Region.OrderedMemberUnitIds != Run.MemberUnitIds
			|| Region.InternalRelationIds != Run.MemberRelationIds
			|| Region.IncomingRelationIds != Run.IncomingRelationIds
			|| Region.OutgoingRelationIds != Run.OutgoingRelationIds
			|| Region.FirstMemberUnitId != Run.MemberUnitIds[0]
			|| Region.LastMemberUnitId != Run.MemberUnitIds.Last())
		{
			Result.Error =
				TEXT("LC1 evidence disclosure requires the current "
					 "layout region projection");
			return Result;
		}
		Result.Region = Region;
		if (Candidate
			== EBlueprintLensLC1DisclosureCandidate::PairedPseudocode)
		{
			if (!Pseudocode.IsRenderable()
				|| !Pseudocode.SourceIrSha256.Equals(
					Explanation.Source.IrSha256,
					ESearchCase::IgnoreCase))
			{
				Result.Error =
					TEXT("LC1 paired disclosure requires the current "
						 "pseudocode projection");
				return Result;
			}
			Result.Pseudocode = Pseudocode;
		}
	}
	else if (Candidate
		!= EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline)
	{
		Result.Error = TEXT("LC1 disclosure candidate is unsupported");
		return Result;
	}

	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		Result.DisplayedUnitIds.Add(Unit.Id);
		const bool bHasPrimarySource =
			Unit.SourceReferences.ContainsByPredicate(
				[](const FBlueprintLensSourceReference& Reference)
				{
					return Reference.bPrimary;
				});
		if (bHasPrimarySource)
		{
			Result.SourceActionUnitIds.Add(Unit.Id);
		}
	}
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
	{
		Result.DisplayedRelationIds.Add(Relation.Id);
	}

	TSet<FString> OwnedUnitIds;
	TSet<FString> OwnedRelationIds;
	for (const FBlueprintLensFrameFlowSegment& Segment : Layout.Segments)
	{
		for (const FString& UnitId : Segment.MemberUnitIds)
		{
			OwnedUnitIds.Add(UnitId);
		}
		for (const FString& RelationId : Segment.MemberRelationIds)
		{
			OwnedRelationIds.Add(RelationId);
		}
	}
	for (const FBlueprintLensFrameFlowSegmentEdge& Edge :
		 Layout.SegmentEdges)
	{
		for (const FString& RelationId : Edge.RelationIds)
		{
			OwnedRelationIds.Add(RelationId);
		}
	}
	if (!SetsEqual(OwnedUnitIds, ToSet(Result.DisplayedUnitIds)) ||
		!SetsEqual(
			OwnedRelationIds,
			ToSet(Result.DisplayedRelationIds)))
	{
		Result.Error =
			TEXT("LC1 disclosure coverage differs from layout ownership");
		return Result;
	}
	if (Result.SourceActionUnitIds.Num() !=
		Result.DisplayedUnitIds.Num())
	{
		Result.Error =
			TEXT("LC1 disclosure requires one source action per unit");
		return Result;
	}

	const FString Anchor = DetailAnchorUnitId.IsEmpty()
		? Run.MemberUnitIds.Last()
		: DetailAnchorUnitId;
	Result.DetailWindow =
		FBlueprintLensFrameFlowLayoutBuilder::BuildDetailWindow(
			Explanation,
			Layout,
			Run.Id,
			Anchor,
			MaximumVisibleUnits);
	if (!Result.DetailWindow.IsValid())
	{
		Result.Error = Result.DetailWindow.Error;
		return Result;
	}
	Result.LayoutModelHash = HashLayoutModel(Layout);
	return Result;
}
