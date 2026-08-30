#include "BlueprintLensLC4SequenceProjection.h"

#include "BlueprintLensDisplayLabel.h"

#include "Misc/SecureHash.h"

namespace
{
constexpr TCHAR LC4ProjectorVersion[] =
	TEXT("BlueprintLens.LC4SequenceDisclosureRailProjector.v1");

void AppendValue(
	FString& Canonical,
	const TCHAR* Label,
	const FString& Value)
{
	Canonical += Label;
	Canonical += FString::Printf(TEXT("[%d:%s]"), Value.Len(), *Value);
}

void AppendIds(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<FString>& Values)
{
	Canonical += Label;
	Canonical += TEXT("[");
	for (const FString& Value : Values)
	{
		Canonical += FString::Printf(TEXT("%d:%s;"), Value.Len(), *Value);
	}
	Canonical += TEXT("]");
}

void AppendOrdinals(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<int32>& Values)
{
	TArray<FString> Strings;
	for (const int32 Value : Values)
	{
		Strings.Add(FString::FromInt(Value));
	}
	AppendIds(Canonical, Label, Strings);
}

FString IntegrityHash(const FBlueprintLensLC4SequenceProjection& Projection)
{
	FString Canonical;
	AppendValue(
		Canonical,
		TEXT("live"),
		Projection.bLiveExplanation ? TEXT("true") : TEXT("false"));
	AppendValue(Canonical, TEXT("version"), Projection.ProjectorVersion);
	AppendValue(Canonical, TEXT("profile"), Projection.SourceProfileSha256);
	AppendValue(Canonical, TEXT("ir"), Projection.SourceIrSha256);
	AppendValue(Canonical, TEXT("sequence"), Projection.SequenceUnitId);
	AppendValue(
		Canonical,
		TEXT("sequence-label"),
		Projection.SequenceReaderLabel);
	AppendValue(Canonical, TEXT("criterion"), Projection.CriterionUnitId);
	AppendValue(
		Canonical,
		TEXT("criterion-label"),
		Projection.CriterionReaderLabel);
	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		AppendValue(
			Canonical,
			TEXT("route-ordinal"),
			FString::FromInt(Route.Ordinal));
		AppendValue(Canonical, TEXT("route-pin-id"), Route.SourcePinId);
		AppendValue(Canonical, TEXT("route-pin"), Route.SourcePinName);
		AppendValue(
			Canonical,
			TEXT("route-connection"),
			LexToString(Route.ConnectionState));
		AppendValue(
			Canonical,
			TEXT("route-criterion"),
			LexToString(Route.CriterionRelation));
		AppendValue(Canonical, TEXT("route-reason"), Route.CriterionReason);
		AppendValue(
			Canonical,
			TEXT("route-termination"),
			LexToString(Route.TerminationKind));
		AppendIds(Canonical, TEXT("route-units"), Route.RouteUnitIds);
		AppendIds(Canonical, TEXT("route-labels"), Route.RouteReaderLabels);
		AppendIds(Canonical, TEXT("route-relations"), Route.RouteRelationIds);
		AppendValue(Canonical, TEXT("route-summary"), Route.SummaryText);
	}
	AppendValue(Canonical, TEXT("merge-node"), Projection.Merge.NodeId);
	AppendValue(Canonical, TEXT("merge-label"), Projection.Merge.ReaderLabel);
	AppendOrdinals(
		Canonical,
		TEXT("merge-ordinals"),
		Projection.Merge.IncomingOutputOrdinals);
	AppendIds(
		Canonical,
		TEXT("merge-suffix-units"),
		Projection.Merge.SharedSuffixUnitIds);
	AppendIds(
		Canonical,
		TEXT("merge-suffix-labels"),
		Projection.Merge.SharedSuffixReaderLabels);
	AppendIds(
		Canonical,
		TEXT("merge-suffix-relations"),
		Projection.Merge.SharedSuffixRelationIds);
	AppendValue(
		Canonical,
		TEXT("count-declared"),
		FString::FromInt(Projection.Counts.DeclaredOutputs));
	AppendValue(
		Canonical,
		TEXT("count-connected"),
		FString::FromInt(Projection.Counts.ConnectedOutputs));
	AppendValue(
		Canonical,
		TEXT("count-unconnected"),
		FString::FromInt(Projection.Counts.UnconnectedOutputs));
	AppendValue(
		Canonical,
		TEXT("count-included"),
		FString::FromInt(Projection.Counts.CriterionIncludedOutputs));
	AppendValue(
		Canonical,
		TEXT("count-outside"),
		FString::FromInt(
			Projection.Counts.OutsideCriterionConnectedOutputs));
	AppendValue(
		Canonical,
		TEXT("count-indeterminate"),
		FString::FromInt(Projection.Counts.IndeterminateOutputs));
	AppendIds(Canonical, TEXT("all-units"), Projection.AllUnitIds);
	AppendIds(Canonical, TEXT("all-relations"), Projection.AllRelationIds);
	AppendIds(Canonical, TEXT("boundaries"), Projection.BoundaryNotices);
	AppendValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	AppendValue(Canonical, TEXT("diagnostic"), Projection.DiagnosticCode);
	return FMD5::HashAnsiString(*Canonical);
}

FBlueprintLensLC4SequenceProjection Unavailable(
	FBlueprintLensLC4SequenceProjection Projection,
	const TCHAR* Diagnostic)
{
	Projection.Status = EBlueprintLensLC4SequenceProjectionStatus::Unavailable;
	Projection.DiagnosticCode = Diagnostic;
	Projection.ProjectionIntegrityHash = IntegrityHash(Projection);
	return Projection;
}

FString RouteSummary(
	const FBlueprintLensLC4SequenceRoute& Route,
	const FString& MergeLabel,
	const bool bLiveExplanation)
{
	if (Route.ConnectionState ==
		EBlueprintLensLC4ConnectionState::Unconnected)
	{
		return bLiveExplanation
			? TEXT("declared output has no connected execution edge")
			: TEXT("EMPTY OUTPUT · no connected execution edge");
	}
	FString RouteText = FString::Join(Route.RouteReaderLabels, TEXT(" → "));
	if (Route.TerminationKind ==
		EBlueprintLensLC4TerminationKind::OrdinaryReconvergence)
	{
		RouteText += FString::Printf(TEXT(" → ○ M1 %s"), *MergeLabel);
	}
	else if (bLiveExplanation && Route.CriterionRelation ==
		EBlueprintLensLC4CriterionRelation::Included)
	{
		RouteText += TEXT(" · included in this static answer");
	}
	else if (bLiveExplanation)
	{
		RouteText += TEXT(" · outside this static answer");
	}
	else
	{
		RouteText += TEXT(" · terminal outside criterion");
	}
	return RouteText;
}
} // namespace

bool FBlueprintLensLC4SequenceProjection::HasValidIntegrity() const
{
	return ProjectorVersion == LC4ProjectorVersion &&
		!SourceProfileSha256.IsEmpty() && !SourceIrSha256.IsEmpty() &&
		!ProjectionIntegrityHash.IsEmpty() &&
		ProjectionIntegrityHash.Equals(
			IntegrityHash(*this),
			ESearchCase::IgnoreCase);
}

bool FBlueprintLensLC4SequenceProjection::IsRenderable() const
{
	if (bLiveExplanation)
	{
		if (Status != EBlueprintLensLC4SequenceProjectionStatus::DisclosureRail ||
			!HasValidIntegrity() || Routes.IsEmpty() || Routes.Num() > 4 ||
			Counts.DeclaredOutputs != Routes.Num() ||
			Counts.ConnectedOutputs + Counts.UnconnectedOutputs != Routes.Num() ||
			Counts.CriterionIncludedOutputs +
				Counts.OutsideCriterionConnectedOutputs !=
				Counts.ConnectedOutputs ||
			Counts.IndeterminateOutputs != 0 || AllUnitIds.IsEmpty())
		{
			return false;
		}
		int32 PreviousOrdinal = INDEX_NONE;
		for (const FBlueprintLensLC4SequenceRoute& Route : Routes)
		{
			if (Route.Ordinal < 0 || Route.Ordinal <= PreviousOrdinal ||
				Route.SourcePinName.IsEmpty() ||
				Route.RouteReaderLabels.Num() != Route.RouteUnitIds.Num())
			{
				return false;
			}
			PreviousOrdinal = Route.Ordinal;
		}
		return true;
	}
	return Status ==
			EBlueprintLensLC4SequenceProjectionStatus::DisclosureRail &&
		HasValidIntegrity() && Routes.Num() == 4 &&
		Routes[0].Ordinal == 0 && Routes[1].Ordinal == 1 &&
		Routes[2].Ordinal == 2 && Routes[3].Ordinal == 3 &&
		Merge.IncomingOutputOrdinals == TArray<int32>({0, 1}) &&
		Merge.SharedSuffixUnitIds.Num() == 2 &&
		Merge.SharedSuffixRelationIds.Num() == 1 && AllUnitIds.Num() == 8 &&
		AllRelationIds.Num() == 8 && Counts.DeclaredOutputs == 4 &&
		Counts.ConnectedOutputs == 3 && Counts.UnconnectedOutputs == 1 &&
		Counts.CriterionIncludedOutputs == 2 &&
		Counts.OutsideCriterionConnectedOutputs == 1 &&
		Counts.IndeterminateOutputs == 0;
}

const FBlueprintLensLC4SequenceRoute*
FBlueprintLensLC4SequenceProjection::FindRoute(const int32 Ordinal) const
{
	return Routes.FindByPredicate(
		[Ordinal](const FBlueprintLensLC4SequenceRoute& Route)
		{
			return Route.Ordinal == Ordinal;
		});
}

FBlueprintLensLC4SequenceProjection FBlueprintLensLC4SequenceProjector::Build(
	const FBlueprintLensLC4SequenceProfile& Profile,
	const FBlueprintLensExplanationModel& Explanation)
{
	FBlueprintLensLC4SequenceProjection Result;
	Result.bLiveExplanation = Profile.bLiveExplanation;
	Result.ProjectorVersion = LC4ProjectorVersion;
	Result.SourceProfileSha256 = Profile.ProfileSha256;
	Result.SourceIrSha256 = Profile.Source.IrSha256;
	Result.SequenceUnitId = Profile.Source.SequenceNodeId;
	Result.CriterionUnitId = Profile.Source.CriterionNodeId;
	Result.Counts = Profile.Counts;
	Result.AllUnitIds = Profile.AccountedUnitIds;
	Result.AllRelationIds = Profile.AccountedRelationIds;

	if (!Profile.IsValid() ||
		Explanation.Source.BlueprintAssetPath !=
			Profile.Source.BlueprintAssetPath ||
		Explanation.Source.GraphId != Profile.Source.GraphId ||
		!Explanation.Source.IrSha256.Equals(
			Profile.Source.IrSha256,
			ESearchCase::IgnoreCase))
	{
		return Unavailable(
			MoveTemp(Result),
			TEXT("LC4_SEQUENCE_SOURCE_BINDING_INVALID"));
	}
	const FBlueprintLensUnit* Sequence =
		Explanation.FindUnit(Result.SequenceUnitId);
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Result.CriterionUnitId);
	if (Sequence == nullptr || Criterion == nullptr)
	{
		return Unavailable(
			MoveTemp(Result),
			TEXT("LC4_SEQUENCE_ANCHOR_UNIT_MISSING"));
	}
	Result.SequenceReaderLabel = BlueprintLensDisplayLabel(*Sequence);
	Result.CriterionReaderLabel = BlueprintLensDisplayLabel(*Criterion);

	Result.Merge.NodeId = Profile.Reconvergence.NodeId;
	Result.Merge.IncomingOutputOrdinals =
		Profile.Reconvergence.IncomingOutputOrdinals;
	Result.Merge.SharedSuffixUnitIds =
		Profile.Reconvergence.SharedReachableNodeIds;
	Result.Merge.SharedSuffixRelationIds =
		Profile.Reconvergence.SharedReachableEdgeIds;
	const FBlueprintLensUnit* MergeUnit = Result.bLiveExplanation
		? nullptr
		: Explanation.FindUnit(Result.Merge.NodeId);
	if (!Result.bLiveExplanation && MergeUnit == nullptr)
	{
		return Unavailable(
			MoveTemp(Result),
			TEXT("LC4_SEQUENCE_MERGE_UNIT_MISSING"));
	}
	Result.Merge.ReaderLabel = MergeUnit != nullptr
		? BlueprintLensDisplayLabel(*MergeUnit)
		: FString();
	for (const FString& UnitId : Result.Merge.SharedSuffixUnitIds)
	{
		const FBlueprintLensUnit* Unit = Explanation.FindUnit(UnitId);
		if (Unit == nullptr)
		{
			return Unavailable(
				MoveTemp(Result),
				TEXT("LC4_SEQUENCE_SHARED_SUFFIX_INVALID"));
		}
		Result.Merge.SharedSuffixReaderLabels.Add(
			BlueprintLensDisplayLabel(*Unit));
	}

	for (const FBlueprintLensLC4SequenceOutput& Output : Profile.Outputs)
	{
		FBlueprintLensLC4SequenceRoute Route;
		Route.Ordinal = Output.Ordinal;
		Route.SourcePinId = Output.SourcePinId;
		Route.SourcePinName = Output.SourcePinName;
		Route.ConnectionState = Output.ConnectionState;
		Route.CriterionRelation = Output.CriterionRelation;
		Route.CriterionReason = Output.CriterionReason;
		Route.TerminationKind = Output.TerminationKind;
		for (const FString& NodeId : Output.ReachableNodeIds)
		{
			if (Result.Merge.SharedSuffixUnitIds.Contains(NodeId))
			{
				continue;
			}
			const FBlueprintLensUnit* Unit = Explanation.FindUnit(NodeId);
			if (Unit == nullptr)
			{
				return Unavailable(
					MoveTemp(Result),
					TEXT("LC4_SEQUENCE_ROUTE_UNIT_INVALID"));
			}
			Route.RouteUnitIds.Add(NodeId);
			Route.RouteReaderLabels.Add(BlueprintLensDisplayLabel(*Unit));
		}
		for (const FString& RelationId : Output.ReachableEdgeIds)
		{
			if (Result.Merge.SharedSuffixRelationIds.Contains(RelationId))
			{
				continue;
			}
			if (Explanation.FindRelation(RelationId) == nullptr)
			{
				return Unavailable(
					MoveTemp(Result),
					TEXT("LC4_SEQUENCE_ROUTE_RELATION_INVALID"));
			}
			Route.RouteRelationIds.Add(RelationId);
		}
		Route.SummaryText = RouteSummary(
			Route,
			Result.Merge.ReaderLabel,
			Result.bLiveExplanation);
		Result.Routes.Add(MoveTemp(Route));
	}

	for (const FString& UnitId : Result.AllUnitIds)
	{
		if (Explanation.FindUnit(UnitId) == nullptr)
		{
			return Unavailable(
				MoveTemp(Result),
				TEXT("LC4_SEQUENCE_UNIT_LEDGER_INVALID"));
		}
	}
	for (const FString& RelationId : Result.AllRelationIds)
	{
		if (Explanation.FindRelation(RelationId) == nullptr)
		{
			return Unavailable(
				MoveTemp(Result),
				TEXT("LC4_SEQUENCE_RELATION_LEDGER_INVALID"));
		}
	}

	Result.BoundaryNotices = Result.bLiveExplanation
		? TArray<FString>({
			TEXT("LC4-SEQ outputs are disclosed in their declared then_N order."),
			TEXT("Connected siblings outside the static slice and unconnected siblings are named as excluded from this answer."),
			TEXT("This static LC4-SEQ path makes no runtime completion-order claim.")})
		: TArray<FString>({
			TEXT("Outputs run synchronously in ordinal order; route geometry does not imply parallel execution."),
			TEXT("M1 is an ordinary multi-predecessor merge: no wait, no AND barrier, no token, and no single-fire claim."),
			TEXT("This is a complete Sequence fan-out overview, not the criterion slice."),
			TEXT("LC4-ASYNC remains deferred outside core-v1.")});
	Result.Status =
		EBlueprintLensLC4SequenceProjectionStatus::DisclosureRail;
	Result.DiagnosticCode = Result.bLiveExplanation
		? TEXT("LC4_SEQUENCE_LIVE_DISCLOSURE_RAIL_COMPLETE")
		: TEXT("LC4_SEQUENCE_DISCLOSURE_RAIL_COMPLETE");
	Result.ProjectionIntegrityHash = IntegrityHash(Result);
	return Result;
}
