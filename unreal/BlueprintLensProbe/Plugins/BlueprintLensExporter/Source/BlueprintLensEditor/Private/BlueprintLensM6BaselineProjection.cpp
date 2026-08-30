// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6BaselineProjection.h"

namespace BlueprintLensM6BaselineProjection
{
namespace
{
FM6BaselineProjectionResult Fail(const TCHAR* Code, const TCHAR* Message)
{
	FM6Error Error;
	Error.Code = Code;
	Error.Phase = TEXT("baseline_projection");
	Error.Message = Message;
	Error.bRetryable = false;
	return MakeError(MoveTemp(Error));
}
bool IsKnownStatus(const FString& Status, const bool bPresentation)
{
	return Status == TEXT("supported") || Status == TEXT("opaque") ||
		Status == TEXT("uncertain") || Status == TEXT("unsupported") ||
		(bPresentation && Status == TEXT("truncated"));
}

TSet<FString> ToSet(const TArray<FString>& Values)
{
	TSet<FString> Result;
	for (const FString& Value : Values) Result.Add(Value);
	return Result;
}

bool SameSet(const TSet<FString>& Left, const TSet<FString>& Right)
{
	if (Left.Num() != Right.Num()) return false;
	for (const FString& Value : Left)
	{
		if (!Right.Contains(Value)) return false;
	}
	return true;
}

bool AddMappedEntity(
	const FString& UnitId,
	const FString& EntityId,
	const TSet<FString>& ExpectedEntityIds,
	TMap<FString, TSet<FString>>& UnitEntityIds,
	TMap<FString, FString>& EntityUnitIds,
	FString& Error)
{
	if (!ExpectedEntityIds.Contains(EntityId))
	{
		Error = FString::Printf(
			TEXT("Explanation unit '%s' references unknown entity '%s'"),
			*UnitId,
			*EntityId);
		return false;
	}
	if (const FString* ExistingUnitId = EntityUnitIds.Find(EntityId))
	{
		if (*ExistingUnitId != UnitId)
		{
			Error = FString::Printf(
			TEXT("entity '%s' is mapped by multiple Explanation units"),
			*EntityId);
			return false;
		}
	}
	else
	{
		EntityUnitIds.Add(EntityId, UnitId);
	}
	UnitEntityIds.FindOrAdd(UnitId).Add(EntityId);
	return true;
}

bool BuildExplanationIdentityMaps(
	const FM6LoadedSessionPacket& Packet,
	TMap<FString, TSet<FString>>& UnitEntityIds,
	TMap<FString, FString>& EntityUnitIds,
	TMap<FString, TSet<FString>>& RelationEdgeIds,
	TMap<FString, FString>& EdgeRelationIds,
	FString& Error)
{
	const TSet<FString> ExpectedEntityIds = ToSet(Packet.BaselineFacts.EntityIds);
	const TSet<FString> ExpectedRelationIds =
		ToSet(Packet.BaselineFacts.RelationIds);
	for (const FBlueprintLensUnit& Unit : Packet.Explanation.Units)
	{
		TSet<FString> MappedEntityIds;
		for (const FBlueprintLensSourceReference& Reference : Unit.SourceReferences)
		{
			if (!AddMappedEntity(
				Unit.Id,
				Reference.SourceNodeId,
				ExpectedEntityIds,
				UnitEntityIds,
				EntityUnitIds,
				Error))
				return false;
			MappedEntityIds.Add(Reference.SourceNodeId);
		}
		if (MappedEntityIds.IsEmpty() && ExpectedEntityIds.Contains(Unit.Id) &&
			!AddMappedEntity(
				Unit.Id,
				Unit.Id,
				ExpectedEntityIds,
				UnitEntityIds,
				EntityUnitIds,
				Error))
			return false;
		if (MappedEntityIds.IsEmpty() && ExpectedEntityIds.Contains(Unit.Id))
			MappedEntityIds.Add(Unit.Id);
		if (MappedEntityIds.IsEmpty())
		{
			Error = FString::Printf(
				TEXT("Explanation unit '%s' has no shared entity identity"), *Unit.Id);
			return false;
		}
	}
	if (EntityUnitIds.Num() != ExpectedEntityIds.Num())
	{
		Error = TEXT("Explanation units do not cover shared entity identities");
		return false;
	}
	for (const FString& EntityId : ExpectedEntityIds)
	{
		if (!EntityUnitIds.Contains(EntityId))
		{
			Error = FString::Printf(
				TEXT("shared entity '%s' is missing from Explanation units"), *EntityId);
			return false;
		}
	}

	for (const FBlueprintLensRelation& Relation : Packet.Explanation.Relations)
	{
		TSet<FString>& MappedEdgeIds = RelationEdgeIds.FindOrAdd(Relation.Id);
		for (const FString& EdgeId : Relation.SourceEdgeIds)
		{
			if (!ExpectedRelationIds.Contains(EdgeId))
			{
				Error = FString::Printf(
					TEXT("Explanation relation '%s' references unknown relation '%s'"),
					*Relation.Id,
					*EdgeId);
				return false;
			}
			if (const FString* ExistingRelationId = EdgeRelationIds.Find(EdgeId))
			{
				if (*ExistingRelationId != Relation.Id)
				{
					Error = FString::Printf(
						TEXT("relation '%s' is mapped by multiple Explanation relations"),
						*EdgeId);
					return false;
				}
			}
			else
			{
				EdgeRelationIds.Add(EdgeId, Relation.Id);
			}
			MappedEdgeIds.Add(EdgeId);
		}
		if (MappedEdgeIds.IsEmpty() && ExpectedRelationIds.Contains(Relation.Id))
		{
			MappedEdgeIds.Add(Relation.Id);
			EdgeRelationIds.Add(Relation.Id, Relation.Id);
		}
		if (MappedEdgeIds.IsEmpty())
		{
			Error = FString::Printf(
				TEXT("Explanation relation '%s' has no shared relation identity"),
				*Relation.Id);
			return false;
		}
		const TSet<FString>* SourceEntities = UnitEntityIds.Find(Relation.SourceUnitId);
		const TSet<FString>* TargetEntities = UnitEntityIds.Find(Relation.TargetUnitId);
		if (SourceEntities == nullptr || TargetEntities == nullptr)
		{
			Error = FString::Printf(
				TEXT("Explanation relation '%s' has an unmapped endpoint"),
				*Relation.Id);
			return false;
		}
		for (const FString& EdgeId : MappedEdgeIds)
		{
			const FM6BaselineRelation* Fact = Packet.BaselineFacts.Relations.FindByPredicate(
				[&EdgeId](const FM6BaselineRelation& Candidate)
				{
					return Candidate.Id == EdgeId;
				});
			if (Fact == nullptr || !SourceEntities->Contains(Fact->SourceEntityId) ||
				!TargetEntities->Contains(Fact->TargetEntityId))
			{
				Error = FString::Printf(
					TEXT("Explanation relation '%s' relabels shared relation '%s'"),
					*Relation.Id,
					*EdgeId);
				return false;
			}
		}
	}
	if (EdgeRelationIds.Num() != ExpectedRelationIds.Num())
	{
		Error = TEXT("Explanation relations do not cover shared relation identities");
		return false;
	}
	for (const FString& RelationId : ExpectedRelationIds)
	{
		if (!EdgeRelationIds.Contains(RelationId))
		{
			Error = FString::Printf(
				TEXT("shared relation '%s' is missing from Explanation relations"),
				*RelationId);
			return false;
		}
	}
	return true;
}

bool ValidateCProjectionCoverage(
	const FBlueprintLensFrameFlowLayoutModel& FrameFlow,
	const FBlueprintLensWeaveProjection& Weave,
	const FBlueprintLensExplanationModel& Explanation,
	const TMap<FString, TSet<FString>>& UnitEntityIds,
	const TMap<FString, TSet<FString>>& RelationEdgeIds,
	const TSet<FString>& ExpectedEntityIds,
	const TSet<FString>& ExpectedRelationIds,
	FString& Error)
{
	TSet<FString> ExplanationUnitIds;
	TSet<FString> ExplanationRelationIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
		ExplanationUnitIds.Add(Unit.Id);
	for (const FBlueprintLensRelation& Relation : Explanation.Relations)
		ExplanationRelationIds.Add(Relation.Id);

	TSet<FString> ProjectedUnitIds;
	TSet<FString> ProjectedRelationIds;
	if (FrameFlow.IsReady())
	{
		for (const FBlueprintLensFrameFlowSegment& Segment : FrameFlow.Segments)
		{
			for (const FString& UnitId : Segment.MemberUnitIds)
				ProjectedUnitIds.Add(UnitId);
			for (const FString& RelationId : Segment.MemberRelationIds)
				ProjectedRelationIds.Add(RelationId);
		}
		for (const FBlueprintLensFrameFlowSegmentEdge& Edge : FrameFlow.SegmentEdges)
			for (const FString& RelationId : Edge.RelationIds)
				ProjectedRelationIds.Add(RelationId);
	}
	if (Weave.IsValid())
	{
		for (const FString& UnitId : Weave.AccountedUnitIds)
			ProjectedUnitIds.Add(UnitId);
		for (const FString& RelationId : Weave.AccountedRelationIds)
			ProjectedRelationIds.Add(RelationId);
	}
	if (!FrameFlow.IsReady() && !Weave.IsValid())
		return true;
	if (!SameSet(ProjectedUnitIds, ExplanationUnitIds) ||
		!SameSet(ProjectedRelationIds, ExplanationRelationIds))
	{
		Error = TEXT("C projection does not cover Explanation identities exactly");
		return false;
	}

	TSet<FString> EntityIds;
	for (const FString& UnitId : ProjectedUnitIds)
	{
		const TSet<FString>* Mapped = UnitEntityIds.Find(UnitId);
		if (Mapped == nullptr)
		{
			Error = FString::Printf(TEXT("C projection invents unit '%s'"), *UnitId);
			return false;
		}
		for (const FString& EntityId : *Mapped)
			EntityIds.Add(EntityId);
	}
	TSet<FString> RelationIds;
	for (const FString& RelationId : ProjectedRelationIds)
	{
		const TSet<FString>* Mapped = RelationEdgeIds.Find(RelationId);
		if (Mapped == nullptr)
		{
			Error = FString::Printf(TEXT("C projection invents relation '%s'"), *RelationId);
			return false;
		}
		for (const FString& EdgeId : *Mapped)
			RelationIds.Add(EdgeId);
	}
	if (!SameSet(EntityIds, ExpectedEntityIds) ||
		!SameSet(RelationIds, ExpectedRelationIds))
	{
		Error = TEXT("C projection does not cover shared identities exactly");
		return false;
	}
	return true;
}
} // namespace
} // namespace BlueprintLensM6BaselineProjection

FM6BaselineProjectionResult BuildM6BaselineViewModels(
	const FM6LoadedSessionPacket& Packet)
{
	using namespace BlueprintLensM6BaselineProjection;
	const FM6BaselineFacts& Facts = Packet.BaselineFacts;
	if (Packet.Request.RendererId != TEXT("R1_GENERIC_FRAME_FLOW_V1") ||
		Facts.RendererId != Packet.Request.RendererId)
	{
		return Fail(
			TEXT("M6_VIEW_PROFILE_UNSUPPORTED"),
			TEXT("the M6 session does not declare the generic baseline renderer"));
	}
	if (Facts.GraphId.IsEmpty() || Facts.CriterionEntityId.IsEmpty() ||
		Facts.Entities.IsEmpty() ||
		Facts.Entities.Num() != Facts.EntityIds.Num() ||
		Facts.Relations.Num() != Facts.RelationIds.Num() ||
		Facts.TruncatedCount < 0)
	{
		return Fail(
			TEXT("M6_VIEW_ENTITY_UNMAPPED"),
			TEXT("shared baseline facts are incomplete"));
	}

	FM6BaselineViewModels Result;
	Result.A.GraphId = Facts.GraphId;
	Result.A.CriterionEntityId = Facts.CriterionEntityId;
	Result.A.FullEntityIds = Packet.TypedDocument.NodeIds;
	Result.A.FullRelationIds = Packet.TypedDocument.EdgeIds;
	Result.A.MemberEntityIds = ToSet(Facts.EntityIds);
	Result.A.MemberRelationIds = ToSet(Facts.RelationIds);
	if (!Result.A.MemberEntityIds.Contains(Facts.CriterionEntityId))
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("criterion is outside shared membership"));
	for (const FString& Id : Result.A.MemberEntityIds)
	{
		if (!Result.A.FullEntityIds.Contains(Id))
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("shared entity is absent from the full graph"));
	}
	for (const FString& Id : Result.A.MemberRelationIds)
	{
		if (!Result.A.FullRelationIds.Contains(Id))
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("shared relation is absent from the full graph"));
	}
	for (const FString& Id : Result.A.FullEntityIds)
	{
		if (!Result.A.MemberEntityIds.Contains(Id)) Result.A.NonMemberEntityIds.Add(Id);
	}
	for (const FString& Id : Result.A.FullRelationIds)
	{
		if (!Result.A.MemberRelationIds.Contains(Id)) Result.A.NonMemberRelationIds.Add(Id);
	}

	TMap<FString, const FM6BaselineBoundary*> BoundaryByNode;
	for (const FM6BaselineBoundary& Boundary : Facts.Boundaries)
	{
		if (!Result.A.MemberEntityIds.Contains(Boundary.NodeId) ||
			BoundaryByNode.Contains(Boundary.NodeId) || Boundary.Reason.IsEmpty() ||
			(Boundary.Status != TEXT("opaque") &&
			 Boundary.Status != TEXT("uncertain") &&
			 Boundary.Status != TEXT("unsupported")))
		{
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("shared boundary is invalid"));
		}
		BoundaryByNode.Add(Boundary.NodeId, &Boundary);
	}

	TSet<FString> ParsedEntityIds;
	int32 ObservedTruncated = 0;
	for (const FM6BaselineEntity& Entity : Facts.Entities)
	{
		if (Entity.Id.IsEmpty() || Entity.Label.IsEmpty() ||
			ParsedEntityIds.Contains(Entity.Id) ||
			!Result.A.MemberEntityIds.Contains(Entity.Id) ||
			!IsKnownStatus(Entity.SemanticStatus, false) ||
			!IsKnownStatus(Entity.PresentationStatus, true) ||
			Entity.InclusionReasons.IsEmpty() ||
			Entity.Source.NodeId != Entity.Id ||
			Entity.Source.GraphId != Facts.GraphId)
		{
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("shared entity cannot be projected"));
		}
		FM6BaselineViewEntity View;
		View.Id = Entity.Id;
		View.Label = Entity.Label;
		View.Role = Entity.Role;
		View.SemanticStatus = Entity.SemanticStatus;
		View.SemanticReason = Entity.SemanticReason;
		View.PresentationStatus = Entity.PresentationStatus;
		View.PresentationReason = Entity.PresentationReason;
		View.InclusionReasons = Entity.InclusionReasons;
		if (const FM6BaselineBoundary* const* Boundary = BoundaryByNode.Find(Entity.Id))
		{
			View.bBoundary = true;
			View.BoundaryStatus = (*Boundary)->Status;
			View.BoundaryReason = (*Boundary)->Reason;
			if (View.SemanticStatus != View.BoundaryStatus ||
				View.SemanticReason != View.BoundaryReason)
			{
				return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("boundary and entity semantics disagree"));
			}
		}
		ObservedTruncated += View.PresentationStatus == TEXT("truncated") ? 1 : 0;
		ParsedEntityIds.Add(Entity.Id);
		Result.B.Entities.Add(View);
		Result.C.Entities.Add(MoveTemp(View));
	}
	if (!SameSet(ParsedEntityIds, Result.A.MemberEntityIds) ||
		ObservedTruncated != Facts.TruncatedCount)
	{
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("entity membership or truncation count disagrees"));
	}

	TSet<FString> ParsedRelationIds;
	for (const FM6BaselineRelation& Relation : Facts.Relations)
	{
		if (Relation.Id.IsEmpty() || ParsedRelationIds.Contains(Relation.Id) ||
			!Result.A.MemberRelationIds.Contains(Relation.Id) ||
			!Result.A.MemberEntityIds.Contains(Relation.SourceEntityId) ||
			!Result.A.MemberEntityIds.Contains(Relation.TargetEntityId) ||
			Relation.SourceEdgeId != Relation.Id ||
			!IsKnownStatus(Relation.SemanticStatus, false))
		{
			return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("shared relation cannot be projected"));
		}
		FM6BaselineViewRelation View;
		View.Id = Relation.Id;
		View.SourceEntityId = Relation.SourceEntityId;
		View.TargetEntityId = Relation.TargetEntityId;
		View.Label = Relation.Label;
		View.Kind = Relation.Kind;
		View.SemanticLabel = Relation.SemanticLabel;
		View.SemanticStatus = Relation.SemanticStatus;
		View.SemanticReason = Relation.SemanticReason;
		ParsedRelationIds.Add(Relation.Id);
		Result.B.Relations.Add(View);
		Result.C.Relations.Add(MoveTemp(View));
	}
	if (!SameSet(ParsedRelationIds, Result.A.MemberRelationIds))
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), TEXT("relation membership disagrees"));

	Result.B.RendererId = Facts.RendererId;
	Result.B.CriterionEntityId = Facts.CriterionEntityId;
	Result.B.bReadOnly = true;
	Result.B.bAllLinksInduced = true;
	Result.B.BoundaryCount = Facts.Boundaries.Num();
	Result.B.TruncatedCount = Facts.TruncatedCount;
	Result.C.RendererId = Facts.RendererId;
	Result.C.CriterionEntityId = Facts.CriterionEntityId;
	Result.C.FrameFlow =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(Packet.Explanation);
	Result.C.Weave = FBlueprintLensWeaveProjector::Build(Packet.Explanation);
	TMap<FString, TSet<FString>> UnitEntityIds;
	TMap<FString, FString> EntityUnitIds;
	TMap<FString, TSet<FString>> RelationEdgeIds;
	TMap<FString, FString> EdgeRelationIds;
	FString CoverageError;
	if (!BuildExplanationIdentityMaps(
		Packet,
		UnitEntityIds,
		EntityUnitIds,
		RelationEdgeIds,
		EdgeRelationIds,
		CoverageError))
	{
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), *CoverageError);
	}
	if (!ValidateCProjectionCoverage(
		Result.C.FrameFlow,
		Result.C.Weave,
		Packet.Explanation,
		UnitEntityIds,
		RelationEdgeIds,
		ToSet(Facts.EntityIds),
		ToSet(Facts.RelationIds),
		CoverageError))
	{
		return Fail(TEXT("M6_VIEW_ENTITY_UNMAPPED"), *CoverageError);
	}
	Result.C.bSpecializedRoutesBypassed = true;
	Result.C.BoundaryCount = Facts.Boundaries.Num();
	Result.C.TruncatedCount = Facts.TruncatedCount;
	return MakeValue(MoveTemp(Result));
}

const FBlueprintLensUnit* FindM6ExplanationUnitBySourceEntityId(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& SourceEntityId)
{
	if (SourceEntityId.IsEmpty()) return nullptr;
	const FBlueprintLensUnit* Match = nullptr;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		for (const FBlueprintLensSourceReference& Reference : Unit.SourceReferences)
		{
			if (Reference.SourceNodeId != SourceEntityId) continue;
			if (Match != nullptr && Match != &Unit) return nullptr;
			Match = &Unit;
		}
	}
	return Match;
}
