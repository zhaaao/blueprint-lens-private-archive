#include "BlueprintLensLC6Projection.h"

#include "IPlatformCrypto.h"

namespace
{
const TArray<FString> ScenarioOrder = {
	TEXT("LC6_OPAQUE"), TEXT("LC6_UNCERTAIN"),
	TEXT("LC6_UNSUPPORTED"), TEXT("LC6_TRUNCATED")};

void AddUnique(TArray<FString>& Values, const FString& Value)
{
	if (!Value.IsEmpty() && !Values.Contains(Value))
	{
		Values.Add(Value);
	}
}

void AddAllUnique(TArray<FString>& Values, const TArray<FString>& Added)
{
	for (const FString& Value : Added)
	{
		AddUnique(Values, Value);
	}
}

TArray<FString> SortedCopy(const TArray<FString>& Values)
{
	TArray<FString> Result = Values;
	Result.Sort();
	return Result;
}

FString HashCanonical(const FString& Text)
{
	FTCHARToUTF8 Converted(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Converted.Get()),
		Converted.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
}

void BuildCompleteText(
	const FBlueprintLensLC6Profile& Profile,
	FBlueprintLensLC6Projection& Projection)
{
	Projection.CompleteTextLines.Add(
		TEXT("Where and why does analysis stop in each LC6 scenario?"));
	for (const FString& ScenarioId : ScenarioOrder)
	{
		const FBlueprintLensLC6Scenario* Scenario =
			Profile.Scenarios.FindByPredicate([&ScenarioId](const auto& Item)
			{
				return Item.ScenarioId == ScenarioId;
			});
		if (Scenario == nullptr)
		{
			continue;
		}
		Projection.CompleteTextLines.Add(FString::Printf(
			TEXT("%s | owner %s | status %s | reason %s"),
			*Scenario->ScenarioId, *Scenario->TruthOwner,
			*Scenario->Status, *Scenario->Reason));
		Projection.CompleteTextLines.Add(FString::Printf(
			TEXT("root %s | criterion %s"),
			*Scenario->RootNodeId, *Scenario->CriterionNodeId));
		if (!Scenario->BoundaryNodeId.IsEmpty())
		{
			Projection.CompleteTextLines.Add(FString::Printf(
				TEXT("semantic boundary %s"), *Scenario->BoundaryNodeId));
		}
		if (Scenario->ScenarioId == TEXT("LC6_TRUNCATED"))
		{
			Projection.CompleteTextLines.Add(FString::Printf(
				TEXT("budget %d | selected %d/%d | complete %d/%d | omitted %d/%d"),
				Scenario->MaxUpstreamHops,
				Scenario->SliceNodeIds.Num(), Scenario->SliceEdgeIds.Num(),
				Scenario->CompleteNodeIds.Num(), Scenario->CompleteEdgeIds.Num(),
				Scenario->OmittedNodeCount, Scenario->OmittedEdgeCount));
			for (const FBlueprintLensLC6Frontier& Frontier : Scenario->Frontiers)
			{
				Projection.CompleteTextLines.Add(FString::Printf(
					TEXT("Frontier %s | %s -> %s"),
					*Frontier.EdgeId, *Frontier.SourceNodeId,
					*Frontier.TargetNodeId));
			}
		}
	}
}

FBlueprintLensLC6Projection Fallback(
	const FBlueprintLensLC6Profile& Profile,
	const TCHAR* Code)
{
	FBlueprintLensLC6Projection Projection;
	Projection.DiagnosticCode = Code;
	BuildCompleteText(Profile, Projection);
	Projection.Status = Projection.CompleteTextLines.Num() > 1
		? EBlueprintLensLC6ProjectionStatus::CompleteText
		: EBlueprintLensLC6ProjectionStatus::Unavailable;
	return Projection;
}

const FBlueprintLensLC6SourceEdge* FindSourceEdge(
	const FBlueprintLensLC6Profile& Profile,
	const FString& RelationId)
{
	return Profile.SourceEdges.FindByPredicate(
		[&RelationId](const FBlueprintLensLC6SourceEdge& Edge)
		{
			return Edge.EdgeId == RelationId;
		});
}
} // namespace

const FBlueprintLensLC6Track* FBlueprintLensLC6Projection::FindTrack(
	const FString& ScenarioId) const
{
	return Tracks.FindByPredicate([&ScenarioId](const FBlueprintLensLC6Track& Track)
	{
		return Track.ScenarioId == ScenarioId;
	});
}

FBlueprintLensLC6Projection FBlueprintLensLC6Projector::Build(
	const FBlueprintLensLC6Profile& Profile)
{
	if (Profile.Scenarios.IsEmpty())
	{
		return Fallback(Profile, TEXT("LC6_PROJECTION_NO_TRUTH"));
	}
	FBlueprintLensLC6Projection Projection;
	BuildCompleteText(Profile, Projection);

	TSet<FString> SeenScenarios;
	for (const FString& ScenarioId : ScenarioOrder)
	{
		const FBlueprintLensLC6Scenario* Scenario =
			Profile.Scenarios.FindByPredicate([&ScenarioId](const auto& Item)
			{
				return Item.ScenarioId == ScenarioId;
			});
		if (Scenario == nullptr || SeenScenarios.Contains(ScenarioId))
		{
			return Fallback(Profile, TEXT("LC6_PROJECTION_SCENARIO_PARTITION"));
		}
		SeenScenarios.Add(ScenarioId);

		FBlueprintLensLC6Track Track;
		Track.ScenarioId = Scenario->ScenarioId;
		Track.TruthOwner = Scenario->TruthOwner;
		Track.Status = Scenario->Status;
		Track.Reason = Scenario->Reason;
		Track.RootNodeId = Scenario->RootNodeId;
		Track.CriterionNodeId = Scenario->CriterionNodeId;
		Track.BoundaryNodeId = Scenario->BoundaryNodeId;
		Track.RootTitle = Profile.SourceTitles.FindRef(Track.RootNodeId);
		Track.CriterionTitle = Profile.SourceTitles.FindRef(Track.CriterionNodeId);
		Track.BoundaryTitle = Profile.SourceTitles.FindRef(Track.BoundaryNodeId);
		Track.bHasSemanticFence =
			Track.TruthOwner == TEXT("core_node_classification");
		Track.HopDistances = Scenario->HopDistances;
		Track.HopDistances.Sort([](const auto& Left, const auto& Right)
		{
			return Left.Distance < Right.Distance;
		});
		Track.Frontiers = Scenario->Frontiers;
		Track.Frontiers.Sort([](const auto& Left, const auto& Right)
		{
			return Left.EdgeId < Right.EdgeId;
		});
		Track.MaxUpstreamHops = Scenario->MaxUpstreamHops;
		Track.OmittedNodeCount = Scenario->OmittedNodeCount;
		Track.OmittedEdgeCount = Scenario->OmittedEdgeCount;
		Track.bHasOmissionAggregate =
			Track.TruthOwner == TEXT("query_profile") &&
			(Track.OmittedNodeCount > 0 || Track.OmittedEdgeCount > 0);

		if (Track.RootTitle.IsEmpty() || Track.CriterionTitle.IsEmpty() ||
			(Track.bHasSemanticFence && Track.BoundaryTitle.IsEmpty()) ||
			Track.Status.IsEmpty() || Track.Reason.IsEmpty())
		{
			return Fallback(Profile, TEXT("LC6_PROJECTION_SOURCE_LABEL_MISSING"));
		}

		if (Track.TruthOwner == TEXT("core_node_classification"))
		{
			AddUnique(Track.MemberIds, Track.RootNodeId);
			AddAllUnique(Track.MemberIds, Scenario->SliceNodeIds);
			AddAllUnique(Track.RelationIds, Scenario->SliceEdgeIds);
			AddAllUnique(Track.RelationIds, Scenario->IncidentEdgeIds);
			Track.SelectedNodeCount = Scenario->SliceNodeIds.Num();
			Track.SelectedEdgeCount = Scenario->SliceEdgeIds.Num();
			Track.CompleteNodeCount = Track.MemberIds.Num();
			Track.CompleteEdgeCount = Track.RelationIds.Num();
		}
		else if (Track.TruthOwner == TEXT("query_profile"))
		{
			Track.MemberIds = Scenario->CompleteNodeIds;
			Track.RelationIds = Scenario->CompleteEdgeIds;
			Track.SelectedNodeCount = Scenario->SliceNodeIds.Num();
			Track.SelectedEdgeCount = Scenario->SliceEdgeIds.Num();
			Track.CompleteNodeCount = Scenario->CompleteNodeIds.Num();
			Track.CompleteEdgeCount = Scenario->CompleteEdgeIds.Num();
		}
		else
		{
			return Fallback(Profile, TEXT("LC6_PROJECTION_OWNER_INVALID"));
		}
		Track.MemberIds.Sort();
		Track.RelationIds.Sort();
		Track.EvidenceIds = Scenario->SourcePinIds;
		AddAllUnique(Track.EvidenceIds, Track.RelationIds);
		AddUnique(Track.EvidenceIds, Profile.CoreSha256);
		AddUnique(Track.EvidenceIds, Profile.QuerySha256);
		AddUnique(Track.EvidenceIds, Profile.ReadinessSha256);
		AddUnique(Track.EvidenceIds, Profile.RawSha256);
		Track.EvidenceIds.Sort();

		for (const FString& MemberId : Track.MemberIds)
		{
			if (!Profile.SourceTitles.Contains(MemberId) ||
				Projection.AllMemberIds.Contains(MemberId))
			{
				return Fallback(Profile, TEXT("LC6_PROJECTION_MEMBER_PARTITION"));
			}
			Projection.AllMemberIds.Add(MemberId);
		}
		for (const FString& RelationId : Track.RelationIds)
		{
			const FBlueprintLensLC6SourceEdge* Edge =
				FindSourceEdge(Profile, RelationId);
			if (Edge == nullptr || Projection.AllRelationIds.Contains(RelationId) ||
				!Track.MemberIds.Contains(Edge->SourceNodeId) ||
				!Track.MemberIds.Contains(Edge->TargetNodeId))
			{
				return Fallback(Profile, TEXT("LC6_PROJECTION_RELATION_PARTITION"));
			}
			Projection.AllRelationIds.Add(RelationId);
			FBlueprintLensLC6Relation Relation;
			Relation.RelationId = RelationId;
			Relation.ScenarioId = Track.ScenarioId;
			Relation.Kind = Edge->Kind;
			Relation.SourceNodeId = Edge->SourceNodeId;
			Relation.TargetNodeId = Edge->TargetNodeId;
			Projection.Relations.Add(MoveTemp(Relation));
		}
		for (const FString& EvidenceId : Track.EvidenceIds)
		{
			Projection.AllEvidenceIds.Add(EvidenceId);
		}
		Projection.Tracks.Add(MoveTemp(Track));
	}

	if (SeenScenarios.Num() != Profile.Scenarios.Num() ||
		Projection.AllMemberIds.Num() != Profile.SourceTitles.Num() ||
		Projection.AllRelationIds.Num() != Profile.SourceEdges.Num())
	{
		return Fallback(Profile, TEXT("LC6_PROJECTION_COVERAGE_INCOMPLETE"));
	}

	FBlueprintLensLC6OwnerBand Core;
	Core.TruthOwner = TEXT("core_node_classification");
	FBlueprintLensLC6OwnerBand Query;
	Query.TruthOwner = TEXT("query_profile");
	for (const FBlueprintLensLC6Track& Track : Projection.Tracks)
	{
		(Track.TruthOwner == Core.TruthOwner
			? Core.ScenarioIds : Query.ScenarioIds).Add(Track.ScenarioId);
	}
	Projection.OwnerBands = {MoveTemp(Core), MoveTemp(Query)};
	Projection.Relations.Sort([](const auto& Left, const auto& Right)
	{
		return Left.RelationId < Right.RelationId;
	});

	TArray<FString> Canonical;
	Canonical.Add(TEXT("LC6_SPLIT_FRONTIER_ROUTES"));
	for (const FBlueprintLensLC6Track& Track : Projection.Tracks)
	{
		const TArray<FString> TrackIdentity = {
			Track.ScenarioId, Track.TruthOwner, Track.Status, Track.Reason,
			Track.RootNodeId, Track.CriterionNodeId, Track.BoundaryNodeId};
		Canonical.Add(FString::Join(TrackIdentity, TEXT("|")));
		Canonical.Add(FString::Join(SortedCopy(Track.MemberIds), TEXT("|")));
		Canonical.Add(FString::Join(SortedCopy(Track.RelationIds), TEXT("|")));
		Canonical.Add(FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d"),
			Track.MaxUpstreamHops, Track.SelectedNodeCount, Track.SelectedEdgeCount,
			Track.CompleteNodeCount, Track.CompleteEdgeCount,
			Track.OmittedNodeCount, Track.OmittedEdgeCount));
		for (const FBlueprintLensLC6HopDistance& Hop : Track.HopDistances)
		{
			Canonical.Add(FString::Printf(TEXT("hop|%d|%s"), Hop.Distance, *Hop.NodeId));
		}
		for (const FBlueprintLensLC6Frontier& Frontier : Track.Frontiers)
		{
			const TArray<FString> FrontierIdentity = {
				TEXT("frontier"), Frontier.EdgeId,
				Frontier.SourceNodeId, Frontier.TargetNodeId};
			Canonical.Add(FString::Join(FrontierIdentity, TEXT("|")));
		}
	}
	Projection.IntegrityHash = HashCanonical(FString::Join(Canonical, TEXT("\n")));
	if (Projection.IntegrityHash.IsEmpty())
	{
		return Fallback(Profile, TEXT("LC6_PROJECTION_HASH_FAILED"));
	}
	Projection.Status = EBlueprintLensLC6ProjectionStatus::FourTrack;
	return Projection;
}
