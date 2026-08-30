#include "BlueprintLensLC6LiveExplanationAdapter.h"

#include "BlueprintLensBoundaryFacts.h"
#include "BlueprintLensDisplayLabel.h"
#include "IPlatformCrypto.h"

namespace
{
FString HashCanonical(const TArray<FString>& Lines)
{
	const FString Text = FString::Join(Lines, TEXT("\n"));
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

FString ScenarioName(const EBlueprintLensSemanticStatus Status)
{
	switch (Status)
	{
	case EBlueprintLensSemanticStatus::Opaque:
		return TEXT("LC6_OPAQUE");
	case EBlueprintLensSemanticStatus::Uncertain:
		return TEXT("LC6_UNCERTAIN");
	case EBlueprintLensSemanticStatus::Unsupported:
		return TEXT("LC6_UNSUPPORTED");
	default:
		return FString();
	}
}

FString ReaderName(const FString& ScenarioId)
{
	return ScenarioId == TEXT("LC6_OPAQUE")
		? TEXT("opaque")
		: ScenarioId == TEXT("LC6_UNCERTAIN")
			? TEXT("uncertain")
			: ScenarioId == TEXT("LC6_UNSUPPORTED")
				? TEXT("unsupported")
				: TEXT("query-budget truncation");
}

FBlueprintLensLC6LiveExplanationAdapterResult Failure(const TCHAR* Code)
{
	FBlueprintLensLC6LiveExplanationAdapterResult Result;
	Result.DiagnosticCode = Code;
	return Result;
}
} // namespace

FBlueprintLensLC6LiveExplanationAdapterResult
FBlueprintLensLC6LiveExplanationAdapter::Build(
	const FBlueprintLensExplanationModel& Explanation,
	const FBlueprintLensLC1RailProjection& Rail)
{
	FBlueprintLensLC6LiveExplanationAdapterResult Result;
	Result.SourceBlueprintAssetPath = Explanation.Source.BlueprintAssetPath;
	Result.SourceIrSha256 = Explanation.Source.IrSha256;
	if (Result.SourceBlueprintAssetPath.IsEmpty() ||
		Result.SourceIrSha256.IsEmpty() ||
		!Rail.IsRenderable() ||
		Rail.SourceIrSha256 != Result.SourceIrSha256)
	{
		return Failure(TEXT("LC6_LIVE_SOURCE_MISMATCH"));
	}
	const FBlueprintLensUnit* Criterion =
		Explanation.FindUnit(Explanation.CriterionUnitId);
	if (Criterion == nullptr ||
		Criterion->Role != EBlueprintLensRole::Criterion)
	{
		return Failure(TEXT("LC6_LIVE_CRITERION_MISSING"));
	}

	TSet<FString> ExpectedBoundaryUnitIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (BlueprintLensIsBoundarySemanticStatus(Unit.SemanticStatus))
		{
			ExpectedBoundaryUnitIds.Add(Unit.Id);
		}
	}
	if (ExpectedBoundaryUnitIds.IsEmpty())
	{
		return Failure(TEXT("LC6_LIVE_NO_BOUNDARY_INSTANCES"));
	}
	if (ExpectedBoundaryUnitIds.Num() > MaxLiveBoundaryTracks)
	{
		return Failure(TEXT("LC6_LIVE_BOUND_EXCEEDED"));
	}
	if (Rail.BoundaryCaps.Num() != ExpectedBoundaryUnitIds.Num())
	{
		return Failure(TEXT("LC6_LIVE_CAP_RECONCILIATION_FAILED"));
	}

	TSet<FString> SeenCapUnitIds;
	TSet<FString> PresentScenarioIds;
	FBlueprintLensLC6OwnerBand CoreBand;
	CoreBand.TruthOwner = TEXT("core_node_classification");
	TArray<FString> Canonical = {
		TEXT("BlueprintLens.LC6LiveExplanationAdapter.v1"),
		Result.SourceBlueprintAssetPath,
		Result.SourceIrSha256,
		Explanation.CriterionUnitId};
	for (int32 Index = 0; Index < Rail.BoundaryCaps.Num(); ++Index)
	{
		const FBlueprintLensLC1RailBoundaryCap& Cap = Rail.BoundaryCaps[Index];
		const FBlueprintLensUnit* Unit = Explanation.FindUnit(Cap.UnitId);
		if (Unit == nullptr || SeenCapUnitIds.Contains(Cap.UnitId) ||
			!ExpectedBoundaryUnitIds.Contains(Cap.UnitId) ||
			Unit->SemanticStatus != Cap.SemanticStatus ||
			Cap.Disclosure !=
				BlueprintLensBoundaryDisclosure(Unit->SemanticStatus))
		{
			return Failure(TEXT("LC6_LIVE_CAP_RECONCILIATION_FAILED"));
		}
		SeenCapUnitIds.Add(Cap.UnitId);
		Result.BoundaryUnitIds.Add(Cap.UnitId);

		const FString CanonicalScenarioId = ScenarioName(Unit->SemanticStatus);
		if (CanonicalScenarioId.IsEmpty())
		{
			return Failure(TEXT("LC6_LIVE_STATUS_UNSUPPORTED"));
		}
		PresentScenarioIds.Add(CanonicalScenarioId);
		FBlueprintLensLC6Track Track;
		Track.ScenarioId = FString::Printf(
			TEXT("%s_LIVE_%d"), *CanonicalScenarioId, Index + 1);
		Track.TruthOwner = CoreBand.TruthOwner;
		Track.Status = BlueprintLensBoundaryStatusName(Unit->SemanticStatus);
		Track.Reason = Cap.Disclosure;
		Track.RootNodeId = Unit->Id;
		Track.RootTitle = BlueprintLensDisplayLabel(*Unit);
		Track.CriterionNodeId = Criterion->Id;
		Track.CriterionTitle = BlueprintLensDisplayLabel(*Criterion);
		Track.BoundaryNodeId = Unit->Id;
		Track.BoundaryTitle = BlueprintLensDisplayLabel(*Unit);
		Track.MemberIds.Add(Unit->Id);
		Track.EvidenceIds.Add(Result.SourceIrSha256);
		for (const FBlueprintLensSourceReference& Source :
			Unit->SourceReferences)
		{
			if (!Source.SourceNodeId.IsEmpty())
			{
				Track.EvidenceIds.AddUnique(Source.SourceNodeId);
			}
			if (!Source.NativeNodeGuid.IsEmpty())
			{
				Track.EvidenceIds.AddUnique(Source.NativeNodeGuid);
			}
		}
		Track.EvidenceIds.Sort();
		Track.bHasSemanticFence = true;
		Track.SelectedNodeCount = 1;
		Track.CompleteNodeCount = 1;
		Result.Projection.AllMemberIds.Add(Unit->Id);
		for (const FString& EvidenceId : Track.EvidenceIds)
		{
			Result.Projection.AllEvidenceIds.Add(EvidenceId);
		}
		CoreBand.ScenarioIds.Add(Track.ScenarioId);
		Canonical.Add(FString::Printf(
			TEXT("%s|%s|%s|%s|%s"),
			*Track.ScenarioId,
			*Track.Status,
			*Track.BoundaryNodeId,
			*Track.CriterionNodeId,
			*Track.Reason));
		Result.Projection.Tracks.Add(MoveTemp(Track));
	}
	if (SeenCapUnitIds.Num() != ExpectedBoundaryUnitIds.Num())
	{
		return Failure(TEXT("LC6_LIVE_CAP_RECONCILIATION_FAILED"));
	}

	for (const FString& ScenarioId : {
		FString(TEXT("LC6_OPAQUE")),
		FString(TEXT("LC6_UNCERTAIN")),
		FString(TEXT("LC6_UNSUPPORTED")),
		FString(TEXT("LC6_TRUNCATED"))})
	{
		if (!PresentScenarioIds.Contains(ScenarioId))
		{
			Result.AbsentScenarioIds.Add(ScenarioId);
		}
	}
	TArray<FString> AbsentReaderNames;
	for (const FString& ScenarioId : Result.AbsentScenarioIds)
	{
		AbsentReaderNames.Add(ReaderName(ScenarioId));
	}
	Result.AbsenceStatement = FString::Printf(
		TEXT("Absent tracks in this slice · %s have no represented instance. "
			"No empty tracks or owner bands are drawn for absent kinds."),
		*FString::Join(AbsentReaderNames, TEXT(", ")));
	Result.ContributionStatement = FString::Printf(
		TEXT("Truth-owner separation · These %d boundary instances and their "
			"reasons are the same ledger as the rail caps. This view separates "
			"core node classification from query-budget truncation; no "
			"query-owned truncation instance is represented here."),
		Result.BoundaryUnitIds.Num());

	Result.Projection.bLiveBoundaryTracks = true;
	Result.Projection.SourceBlueprintAssetPath =
		Result.SourceBlueprintAssetPath;
	Result.Projection.SourceIrSha256 = Result.SourceIrSha256;
	Result.Projection.BoundaryUnitIds = Result.BoundaryUnitIds;
	Result.Projection.AbsentScenarioIds = Result.AbsentScenarioIds;
	Result.Projection.AbsenceStatement = Result.AbsenceStatement;
	Result.Projection.ContributionStatement = Result.ContributionStatement;
	Result.Projection.OwnerBands.Add(MoveTemp(CoreBand));
	Result.Projection.CompleteTextLines.Add(FString::Printf(
		TEXT("Live boundary tracks for %s"),
		*Result.SourceBlueprintAssetPath));
	Result.Projection.CompleteTextLines.Add(Result.ContributionStatement);
	Result.Projection.CompleteTextLines.Add(Result.AbsenceStatement);
	for (const FBlueprintLensLC6Track& Track : Result.Projection.Tracks)
	{
		Result.Projection.CompleteTextLines.Add(FString::Printf(
			TEXT("%s | %s | %s"),
			*Track.BoundaryTitle,
			*Track.Status,
			*Track.Reason));
	}
	Canonical.Add(Result.AbsenceStatement);
	Canonical.Add(Result.ContributionStatement);
	Result.Projection.IntegrityHash = HashCanonical(Canonical);
	if (Result.Projection.IntegrityHash.IsEmpty())
	{
		return Failure(TEXT("LC6_LIVE_HASH_FAILED"));
	}
	Result.Projection.Status = EBlueprintLensLC6ProjectionStatus::FourTrack;
	Result.Projection.DiagnosticCode = TEXT("LC6_LIVE_PROJECTION_COMPLETE");
	Result.DiagnosticCode = TEXT("LC6_LIVE_ADAPTER_COMPLETE");
	return Result;
}
