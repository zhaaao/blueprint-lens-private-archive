#pragma once

#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensLC1PseudocodeProjection.h"
#include "BlueprintLensLC1RegionProjection.h"

enum class EBlueprintLensLC1DisclosureCandidate : uint8
{
	PlainOrderedOutline = 0,
	EvidenceBackedRegions = 1,
	PairedPseudocode = 2
};

struct FBlueprintLensLC1DisclosureProjection
{
	EBlueprintLensLC1DisclosureCandidate Candidate =
		EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline;
	TArray<FString> DisplayedUnitIds;
	TArray<FString> DisplayedRelationIds;
	TArray<FString> SourceActionUnitIds;
	FBlueprintLensFrameFlowDetailWindow DetailWindow;
	FBlueprintLensLC1RegionProjection Region;
	FBlueprintLensLC1PseudocodeProjection Pseudocode;
	FString LayoutModelHash;
	FString Error;

	bool IsValid() const
	{
		const bool bHasAttachedRegion =
			!Region.SourceIrSha256.IsEmpty()
			|| !Region.ProjectorVersion.IsEmpty()
			|| !Region.RegionId.IsEmpty()
			|| !Region.RegionKind.IsEmpty()
			|| !Region.OrderedMemberUnitIds.IsEmpty()
			|| !Region.InternalRelationIds.IsEmpty()
			|| !Region.IncomingRelationIds.IsEmpty()
			|| !Region.OutgoingRelationIds.IsEmpty()
			|| !Region.FirstMemberUnitId.IsEmpty()
			|| !Region.LastMemberUnitId.IsEmpty()
			|| !Region.SummaryTemplateId.IsEmpty()
			|| !Region.SummaryArguments.IsEmpty()
			|| !Region.ClaimEvidence.IsEmpty()
			|| Region.Status
				!= EBlueprintLensLC1RegionProjectionStatus::Unavailable
			|| !Region.DiagnosticCode.IsEmpty()
			|| !Region.ProjectionIntegrityHash.IsEmpty();
		const bool bAllowsNoRegion =
			Candidate
			== EBlueprintLensLC1DisclosureCandidate::
				PlainOrderedOutline;
		const bool bCandidateRegionValid =
			(bAllowsNoRegion && !bHasAttachedRegion)
			|| (Candidate
					== EBlueprintLensLC1DisclosureCandidate::
						EvidenceBackedRegions
				&& Region.IsRenderable())
			|| (Candidate
					== EBlueprintLensLC1DisclosureCandidate::
						PairedPseudocode
				&& Region.IsRenderable());
		const bool bCandidatePseudocodeValid =
			Candidate
				== EBlueprintLensLC1DisclosureCandidate::PairedPseudocode
			? Pseudocode.IsRenderable()
			: !Pseudocode.IsRenderable();
		return Error.IsEmpty()
			&& bCandidateRegionValid
			&& bCandidatePseudocodeValid
			&& DisplayedUnitIds.Num() == 14
			&& DisplayedRelationIds.Num() == 13
			&& SourceActionUnitIds.Num() == 14
			&& DetailWindow.IsValid()
			&& !LayoutModelHash.IsEmpty();
	}
};

class FBlueprintLensLC1DisclosureProjector
{
public:
	static FBlueprintLensLC1DisclosureProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		EBlueprintLensLC1DisclosureCandidate Candidate,
		const FString& DetailAnchorUnitId,
		int32 MaximumVisibleUnits = 3);

	static FBlueprintLensLC1DisclosureProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		EBlueprintLensLC1DisclosureCandidate Candidate,
		const FString& DetailAnchorUnitId,
		int32 MaximumVisibleUnits,
		const FBlueprintLensLC1RegionProjection& Region);

	static FBlueprintLensLC1DisclosureProjection Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		EBlueprintLensLC1DisclosureCandidate Candidate,
		const FString& DetailAnchorUnitId,
		int32 MaximumVisibleUnits,
		const FBlueprintLensLC1RegionProjection& Region,
		const FBlueprintLensLC1PseudocodeProjection& Pseudocode);

	static FString HashLayoutModel(
		const FBlueprintLensFrameFlowLayoutModel& Layout);
};
