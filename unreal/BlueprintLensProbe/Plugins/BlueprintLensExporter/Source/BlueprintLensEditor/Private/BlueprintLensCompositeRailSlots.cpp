#include "BlueprintLensCompositeRailSlots.h"

#include "BlueprintLensDisplayLabel.h"

namespace
{
bool AttachmentIsCollapsed(const FBlueprintLensCompositeAttachment& Attachment)
{
	return Attachment.Disclosure ==
		EBlueprintLensCompositeDisclosure::Collapsed;
}

int32 GuardDepth(
	const FBlueprintLensLC2GuardCompound& Compound,
	const TArray<FBlueprintLensLC2GuardCompound>& Compounds)
{
	int32 Depth = 0;
	FString ParentId = Compound.ParentGroupId;
	TSet<FString> Visited;
	while (!ParentId.IsEmpty() && !Visited.Contains(ParentId))
	{
		Visited.Add(ParentId);
		const FBlueprintLensLC2GuardCompound* Parent =
			Compounds.FindByPredicate(
				[&ParentId](const FBlueprintLensLC2GuardCompound& Candidate)
				{
					return Candidate.GroupId == ParentId;
				});
		if (Parent == nullptr)
		{
			return INDEX_NONE;
		}
		++Depth;
		ParentId = Parent->ParentGroupId;
	}
	return ParentId.IsEmpty() ? Depth : INDEX_NONE;
}

} // namespace

FBlueprintLensCompositeStationSlot*
FBlueprintLensCompositeRailSlots::FindStation(const FString& UnitId)
{
	return Stations.FindByPredicate(
		[&UnitId](const FBlueprintLensCompositeStationSlot& Station)
		{
			return Station.UnitId == UnitId;
		});
}

const FBlueprintLensCompositeStationSlot*
FBlueprintLensCompositeRailSlots::FindStation(const FString& UnitId) const
{
	return Stations.FindByPredicate(
		[&UnitId](const FBlueprintLensCompositeStationSlot& Station)
		{
			return Station.UnitId == UnitId;
		});
}

bool FBlueprintLensCompositeRailSlots::HasGuardStations() const
{
	return Stations.ContainsByPredicate(
		[](const FBlueprintLensCompositeStationSlot& Station)
		{
			return Station.Appearance.Kind ==
				EBlueprintLensCompositeStationAppearanceKind::Guard;
		});
}

bool FBlueprintLensCompositeRailSlots::HasLC3Attachments() const
{
	return Stations.ContainsByPredicate(
		[](const FBlueprintLensCompositeStationSlot& Station)
		{
			return Station.BesideAttachments.ContainsByPredicate(
				[](const FBlueprintLensCompositeAttachment& Attachment)
				{
					return Attachment.GrammarId == TEXT("LC3");
				});
		});
}

bool FBlueprintLensCompositeRailSlots::AreAllAttachmentsCollapsed() const
{
	for (const FBlueprintLensCompositeStationSlot& Station : Stations)
	{
		if (Station.Appearance.Disclosure !=
				EBlueprintLensCompositeDisclosure::Collapsed ||
			Station.BesideAttachments.ContainsByPredicate(
				[](const FBlueprintLensCompositeAttachment& Attachment)
				{
					return !AttachmentIsCollapsed(Attachment);
				}))
		{
			return false;
		}
	}
	for (const FBlueprintLensCompositeBetweenStationsSlot& Between : BetweenStations)
	{
		if (Between.Decorations.ContainsByPredicate(
			[](const FBlueprintLensCompositeAttachment& Attachment)
			{
				return !AttachmentIsCollapsed(Attachment);
			}))
		{
			return false;
		}
	}
	for (const FBlueprintLensCompositeTerminalCapSlot& Cap : TerminalCaps)
	{
		if (Cap.Attachments.ContainsByPredicate(
			[](const FBlueprintLensCompositeAttachment& Attachment)
			{
				return !AttachmentIsCollapsed(Attachment);
			}))
		{
			return false;
		}
	}
	for (const FBlueprintLensCompositeSpanSlot& Span : Spans)
	{
		if (Span.Disclosure != EBlueprintLensCompositeDisclosure::Collapsed ||
			Span.Attachments.ContainsByPredicate(
				[](const FBlueprintLensCompositeAttachment& Attachment)
				{
					return !AttachmentIsCollapsed(Attachment);
				}))
		{
			return false;
		}
	}
	return true;
}

bool FBlueprintLensCompositeRailSlots::IsRenderable(
	const FBlueprintLensLC1RailProjection& Rail) const
{
	if (!Rail.IsRenderable() || SourceIrSha256 != Rail.SourceIrSha256 ||
		DiagnosticCode != TEXT("COMPOSITE_RAIL_SLOTS_COMPLETE") ||
		Stations.Num() != Rail.OrderedCanonicalUnits.Num() ||
		BetweenStations.Num() != Rail.OrderedExecutionRelations.Num() ||
		TerminalCaps.Num() != Rail.BoundaryCaps.Num())
	{
		return false;
	}
	TSet<FString> SeenStations;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		Rail.OrderedCanonicalUnits)
	{
		const FBlueprintLensCompositeStationSlot* Station =
			FindStation(Unit.UnitId);
		if (Station == nullptr || SeenStations.Contains(Unit.UnitId))
		{
			return false;
		}
		SeenStations.Add(Unit.UnitId);
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		Rail.OrderedExecutionRelations)
	{
		if (!BetweenStations.ContainsByPredicate(
			[&Relation](const FBlueprintLensCompositeBetweenStationsSlot& Slot)
			{
				return Slot.RelationId == Relation.RelationId &&
					Slot.SourceUnitId == Relation.SourceUnitId &&
					Slot.TargetUnitId == Relation.TargetUnitId;
			}))
		{
			return false;
		}
	}
	for (const FBlueprintLensLC1RailOrderRegion& Region : Rail.OrderRegions)
	{
		if (!Spans.ContainsByPredicate(
			[&Region](const FBlueprintLensCompositeSpanSlot& Span)
			{
				return Span.bIsOrderBoundary &&
					Span.SlotId == TEXT("order:") + Region.RegionId &&
					Span.OrderRegionKind == Region.Kind &&
					Span.MemberUnitIds == Region.MemberUnitIds &&
					Span.ReaderText == Region.ReaderText;
			}))
		{
			return false;
		}
	}
	return true;
}

FBlueprintLensCompositeRailSlots FBlueprintLensCompositeRailSlotProjector::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1RailProjection& Rail)
{
	FBlueprintLensCompositeRailSlots Result;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	Result.SourceBlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	if (!Rail.IsRenderable() || Result.SourceIrSha256 != Rail.SourceIrSha256)
	{
		Result.DiagnosticCode = TEXT("COMPOSITE_RAIL_SOURCE_MISMATCH");
		return Result;
	}

	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		Rail.OrderedCanonicalUnits)
	{
		FBlueprintLensCompositeStationSlot Station;
		Station.UnitId = Unit.UnitId;
		Result.Stations.Add(MoveTemp(Station));
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		Rail.OrderedExecutionRelations)
	{
		FBlueprintLensCompositeBetweenStationsSlot Between;
		Between.RelationId = Relation.RelationId;
		Between.SourceUnitId = Relation.SourceUnitId;
		Between.TargetUnitId = Relation.TargetUnitId;
		Result.BetweenStations.Add(MoveTemp(Between));
	}
	for (const FBlueprintLensLC1RailBoundaryCap& Cap : Rail.BoundaryCaps)
	{
		FBlueprintLensCompositeTerminalCapSlot Terminal;
		Terminal.UnitId = Cap.UnitId;
		Result.TerminalCaps.Add(MoveTemp(Terminal));
	}
	for (const FBlueprintLensLC1RailOrderRegion& Region : Rail.OrderRegions)
	{
		FBlueprintLensCompositeSpanSlot OrderBoundary;
		OrderBoundary.SlotId = TEXT("order:") + Region.RegionId;
		OrderBoundary.MemberUnitIds = Region.MemberUnitIds;
		OrderBoundary.bIsOrderBoundary = true;
		OrderBoundary.OrderRegionKind = Region.Kind;
		OrderBoundary.ReaderText = Region.ReaderText;
		Result.Spans.Add(MoveTemp(OrderBoundary));
	}

	const int32 FirstVisible = FMath::Max(
		0,
		Rail.OrderedCanonicalUnits.Num() -
			(FBlueprintLensCompositeRailSlots::DefaultFoldRadius + 1));
	if (FirstVisible > 0)
	{
		FBlueprintLensCompositeSpanSlot RadiusFold;
		RadiusFold.SlotId = TEXT("radius-fold");
		for (int32 Index = 0; Index < FirstVisible; ++Index)
		{
			const FString& UnitId = Rail.OrderedCanonicalUnits[Index].UnitId;
			if (!Rail.BoundaryCaps.ContainsByPredicate(
				[&UnitId](const FBlueprintLensLC1RailBoundaryCap& Cap)
				{
					return Cap.UnitId == UnitId;
				}))
			{
				RadiusFold.MemberUnitIds.Add(UnitId);
			}
		}
		if (!RadiusFold.MemberUnitIds.IsEmpty())
		{
			Result.Spans.Add(MoveTemp(RadiusFold));
		}
	}

	Result.DiagnosticCode = TEXT("COMPOSITE_RAIL_SLOTS_COMPLETE");
	return Result;
}

FBlueprintLensCompositeRailSlots
FBlueprintLensLC2StationAppearanceProjector::Apply(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC2GuardSurfaceProjection& GuardSurface,
	const FBlueprintLensCompositeRailSlots& BaseSlots)
{
	if (BaseSlots.SourceIrSha256 != Explanation.Source.IrSha256 ||
		BaseSlots.SourceBlueprintAssetPath != Explanation.Source.BlueprintAssetPath)
	{
		return BaseSlots;
	}
	if (!GuardSurface.IsRenderable() ||
		GuardSurface.SourceIrSha256 != Explanation.Source.IrSha256)
	{
		// LC2 has one rendering path. If the accepted projector cannot prove a
		// surface, leave the station plain instead of inventing a second guard
		// grammar from partial live facts.
		return BaseSlots;
	}

	FBlueprintLensCompositeRailSlots Candidate = BaseSlots;
	for (const FBlueprintLensLC2GuardCompound& Compound : GuardSurface.Compounds)
	{
		FBlueprintLensCompositeStationSlot* Station =
			Candidate.FindStation(Compound.BranchUnitId);
		const int32 Depth = GuardDepth(Compound, GuardSurface.Compounds);
		const FBlueprintLensLC2GuardCanonicalUnit* Predicate =
			GuardSurface.FindCanonicalUnit(Compound.PredicateUnitId);
		const FBlueprintLensLC2GuardForkMark* Fork =
			GuardSurface.ForkMarks.FindByPredicate(
				[&Compound](const FBlueprintLensLC2GuardForkMark& Mark)
				{
					return Mark.BranchUnitId == Compound.BranchUnitId;
				});
		if (Station == nullptr || Depth == INDEX_NONE || Predicate == nullptr ||
			Fork == nullptr)
		{
			return BaseSlots;
		}

		Station->Appearance.Kind =
			EBlueprintLensCompositeStationAppearanceKind::Guard;
		Station->Appearance.GrammarId = TEXT("LC2");
		Station->Appearance.GroupId = Compound.GroupId;
		Station->Appearance.ParentGroupId = Compound.ParentGroupId;
		Station->Appearance.PredicateUnitId = Compound.PredicateUnitId;
		Station->Appearance.MarkerText = FString::Printf(
			TEXT("GUARD GATE · %s"), *Compound.GuardReaderText);
		Station->Appearance.GuardReaderText = Compound.GuardReaderText;
		Station->Appearance.NestingDepth = Depth;
		Station->Appearance.ForkReaderText = Fork->ReaderText;
		if (!Compound.ParentGroupId.IsEmpty())
		{
			const FBlueprintLensLC2GuardCompound* Parent =
				GuardSurface.FindCompound(Compound.ParentGroupId);
			if (Parent == nullptr)
			{
				return BaseSlots;
			}
			Station->Appearance.ParentGuardReaderText =
				Parent->GuardReaderText;
		}
		Station->Appearance.DetailLines = {
			FString::Printf(TEXT("PREDICATE · %s"), *Predicate->ReaderLabel),
			FString::Printf(
				TEXT("HOLDS · %d EXCLUSIVE UNITS"),
				Compound.ExclusiveMemberUnitIds.Num()),
			FString::Printf(
				TEXT("OUTCOMES · %d · NO ORDER PROVEN"),
				Fork->OutcomeGroupIds.Num())};
	}
	return Candidate;
}

FBlueprintLensCompositeRailSlots
FBlueprintLensLC3StationAttachmentProjector::Apply(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensCompositeRailSlots& BaseSlots)
{
	if (BaseSlots.SourceIrSha256 != Explanation.Source.IrSha256 ||
		BaseSlots.SourceBlueprintAssetPath !=
			Explanation.Source.BlueprintAssetPath)
	{
		return BaseSlots;
	}

	FBlueprintLensCompositeRailSlots Candidate = BaseSlots;
	for (FBlueprintLensCompositeStationSlot& Station : Candidate.Stations)
	{
		TArray<const FBlueprintLensRelation*> ValueInputs;
		for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		{
			if (Relation.Kind == EBlueprintLensRelationKind::ProvidesValue &&
				Relation.TargetUnitId == Station.UnitId &&
				Explanation.FindUnit(Relation.SourceUnitId) != nullptr)
			{
				ValueInputs.Add(&Relation);
			}
		}
		if (ValueInputs.IsEmpty())
		{
			continue;
		}
		ValueInputs.Sort(
			[](const FBlueprintLensRelation& Left,
				const FBlueprintLensRelation& Right)
			{
				return Left.Id < Right.Id;
			});

		FBlueprintLensCompositeAttachment Attachment;
		Attachment.GrammarId = TEXT("LC3");
		Attachment.AttachmentId = TEXT("lc3:") + Station.UnitId;
		TArray<FString> ProducerLabels;
		for (const FBlueprintLensRelation* Relation : ValueInputs)
		{
			const FBlueprintLensUnit* Producer =
				Explanation.FindUnit(Relation->SourceUnitId);
			if (Producer == nullptr)
			{
				return BaseSlots;
			}
			const FString ProducerLabel =
				BlueprintLensDisplayLabel(*Producer);
			ProducerLabels.Add(ProducerLabel);
			const FString InputLabel = Relation->bHasPortLabel &&
				!Relation->PortLabel.IsEmpty()
				? Relation->PortLabel
				: TEXT("value input");
			Attachment.DetailLines.Add(FString::Printf(
				TEXT("%s · %s"),
				*ProducerLabel,
				*InputLabel));
		}
		Attachment.MarkerText = ValueInputs.Num() == 1
			? FString::Printf(
				TEXT("Open value provenance · %s"),
				*ProducerLabels[0])
			: FString::Printf(
				TEXT("Open %d value origins · %s"),
				ValueInputs.Num(),
				*FString::Join(ProducerLabels, TEXT(", ")));
		Station.BesideAttachments.Add(MoveTemp(Attachment));
	}
	return Candidate;
}
