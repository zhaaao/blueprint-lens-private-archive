#include "BlueprintLensLC5Projection.h"

#include "IPlatformCrypto.h"

namespace
{
FString Sha256(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num());
}

template <typename T, typename Predicate>
void SortBy(TArray<T>& Values, Predicate PredicateFn)
{
	Values.Sort(PredicateFn);
}
} // namespace

bool FBlueprintLensLC5Projection::IsRenderable() const
{
	if (bLiveCallBody)
	{
		return Status == EBlueprintLensLC5ProjectionStatus::TypedPortalBridge &&
			DiagnosticCode == TEXT("LC5_LIVE_TYPED_PORTAL_COMPLETE") &&
			!SourceBlueprintAssetPath.IsEmpty() && !CallUnitId.IsEmpty() &&
			!CallerGraphId.IsEmpty() && !CalleeGraphName.IsEmpty() &&
			!ClaimState.IsEmpty() &&
			!ClaimBoundaryStatement.IsEmpty() && !StaticOrderStatement.IsEmpty() &&
			!LegendEntries.IsEmpty() &&
			Occurrences.Num() >= 2 &&
			Occurrences.Num() <= 17 && Bindings.IsEmpty() &&
			ContextBoundaries.Num() == 1 &&
			AllRelationIds.Num() ==
				InternalRelations.Num() + ContextBoundaries.Num() &&
			SourceNodeIds.Num() == Occurrences.Num() && ActionIds.Num() == 4 &&
			LiveOccurrenceLabels.Num() == Occurrences.Num() &&
			LiveStaticRanks.Num() == Occurrences.Num() - 1 &&
			!ProjectionIntegrityHash.IsEmpty();
	}
	return Status == EBlueprintLensLC5ProjectionStatus::TypedPortalBridge &&
		DiagnosticCode == TEXT("LC5_TYPED_PORTAL_BRIDGE_COMPLETE") &&
		Occurrences.Num() == 4 && Bindings.Num() == 3 &&
		InternalRelations.Num() == 4 && ContextBoundaries.Num() == 2 &&
		AllRelationIds.Num() == 9 && SourceNodeIds.Num() == 4 &&
		ActionIds.Num() == 4 && BoundaryText.Num() == 5 &&
		!ProjectionIntegrityHash.IsEmpty();
}

FBlueprintLensLC5Projection FBlueprintLensLC5Projector::Build(
	const FBlueprintLensLC5Profile& Profile)
{
	FBlueprintLensLC5Projection Result;
	Result.ProjectorVersion = TEXT("BlueprintLens.LC5Projector.v1");
	Result.SourceProfileSha256 = Profile.ProfileSha256;
	if (Profile.Status != TEXT("resolved_unique"))
	{
		Result.Status = EBlueprintLensLC5ProjectionStatus::Frontier;
		Result.DiagnosticCode = FString::Printf(TEXT("LC5_FRONTIER_%s"),
			Profile.Status.IsEmpty() ? TEXT("UNKNOWN") : *Profile.Status.ToUpper());
		Result.BoundaryText.Add(Profile.Reason.IsEmpty() ?
			TEXT("Frontier: this call context is not resolved uniquely.") : Profile.Reason);
		return Result;
	}
	if (!Profile.IsValid())
	{
		Result.Status = EBlueprintLensLC5ProjectionStatus::Frontier;
		Result.DiagnosticCode = TEXT("LC5_FRONTIER_PROFILE_INVARIANT_FAILED");
		Result.BoundaryText.Add(TEXT("Frontier: frozen profile invariants are incomplete or inconsistent."));
		return Result;
	}

	Result.SourceIdentity = Profile.SourceIdentity;
	Result.CallContext = Profile.CallContext;
	Result.Occurrences = Profile.Occurrences;
	Result.Bindings = Profile.Bindings;
	Result.InternalRelations = Profile.InternalRelations;
	Result.ContextBoundaries = Profile.ContextBoundaries;
	SortBy(Result.Occurrences, [](const auto& A, const auto& B)
	{
		if (A.Role != B.Role)
		{
			return A.Role == TEXT("call_site");
		}
		return A.SourceNodeId < B.SourceNodeId;
	});
	SortBy(Result.Bindings, [](const auto& A, const auto& B)
	{
		return A.Ordinal < B.Ordinal;
	});
	SortBy(Result.InternalRelations, [](const auto& A, const auto& B)
	{
		return A.RelationId < B.RelationId;
	});
	SortBy(Result.ContextBoundaries, [](const auto& A, const auto& B)
	{
		return A.Kind == TEXT("call_enter") && B.Kind != TEXT("call_enter");
	});
	for (const FBlueprintLensLC5Occurrence& Occurrence : Result.Occurrences)
	{
		Result.SourceNodeIds.Add(Occurrence.SourceNodeId);
	}
	for (const FBlueprintLensLC5Binding& Binding : Result.Bindings)
	{
		Result.AllRelationIds.Add(Binding.RelationId);
	}
	for (const FBlueprintLensLC5ContextBoundary& Boundary : Result.ContextBoundaries)
	{
		Result.AllRelationIds.Add(Boundary.RelationId);
	}
	for (const FBlueprintLensLC5InternalRelation& Relation : Result.InternalRelations)
	{
		Result.AllRelationIds.Add(Relation.RelationId);
	}
	Result.ActionIds = {
		TEXT("select"), TEXT("show_complete_text"),
		TEXT("show_evidence"), TEXT("open_source")};
	Result.BoundaryText = {
		TEXT("Occurrences are static contextual occurrences and do not prove runtime invocations."),
		TEXT("Macro, impure, latent, cross-Blueprint and dynamic-dispatch calls remain outside this profile."),
		TEXT("Core-v1 remains opaque/function_body_not_expanded."),
		TEXT("No visual condition, Slate portal, comprehension, scalability or product-default claim follows."),
		TEXT("Frontier · depth 1 · macro, impure, latent, cross-Blueprint and dynamic dispatch excluded")};
	TArray<FString> Ledger;
	Ledger.Append(Result.SourceNodeIds);
	Ledger.Append(Result.AllRelationIds);
	Ledger.Append(Result.ActionIds);
	Ledger.Append(Result.BoundaryText);
	Result.ProjectionIntegrityHash = Sha256(FString::Printf(TEXT("%s\n%s\n%s"),
		*Result.ProjectorVersion, *Result.SourceProfileSha256,
		*FString::Join(Ledger, TEXT("\n"))));
	Result.Status = EBlueprintLensLC5ProjectionStatus::TypedPortalBridge;
	Result.DiagnosticCode = TEXT("LC5_TYPED_PORTAL_BRIDGE_COMPLETE");
	return Result;
}
