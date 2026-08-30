#include "BlueprintLensLC4AsyncProjection.h"

#include "IPlatformCrypto.h"

namespace
{
FString Sha256(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num());
}

void AddUnique(TArray<FString>& Values, const FString& Value)
{
	if (!Value.IsEmpty())
	{
		Values.AddUnique(Value);
	}
}
} // namespace

bool FBlueprintLensLC4AsyncProjection::IsRenderable() const
{
	return Status == EBlueprintLensLC4AsyncProjectionStatus::PartialOrderJoin &&
		DiagnosticCode == TEXT("LC4_ASYNC_PARTIAL_ORDER_JOIN_COMPLETE") &&
		(Variant == TEXT("A_FIRST") || Variant == TEXT("B_FIRST")) &&
		Invocations.Num() == 2 && AllRelationIds.Num() == 22 && Proofs.Num() == 2 &&
		Continuations.Num() == 2 && Launches.Num() == 2 && Arrivals.Num() == 2 &&
		Boundaries.Num() == 4 && !StructuralSignature.IsEmpty() &&
		!EvidenceSignature.IsEmpty() && !ProjectionIntegrityHash.IsEmpty();
}

FBlueprintLensLC4AsyncProjection FBlueprintLensLC4AsyncProjector::Build(
	const FBlueprintLensLC4AsyncProfile& Profile,
	const FString& Variant)
{
	FBlueprintLensLC4AsyncProjection Result;
	Result.ProjectorVersion = TEXT("BlueprintLens.LC4AsyncProjector.v1");
	Result.SourceProfileSha256 = Profile.ProfileSha256;
	Result.Variant = Variant;
	if (!Profile.IsValid())
	{
		Result.DiagnosticCode = TEXT("LC4_ASYNC_PROFILE_UNAVAILABLE");
		return Result;
	}
	if (Variant != TEXT("A_FIRST") && Variant != TEXT("B_FIRST"))
	{
		Result.Status = EBlueprintLensLC4AsyncProjectionStatus::Frontier;
		Result.DiagnosticCode = TEXT("LC4_ASYNC_FRONTIER");
		return Result;
	}
	Result.Source = Profile.Source;
	Result.Continuations = Profile.Continuations;
	Result.Launches = Profile.Launches;
	Result.Arrivals = Profile.Arrivals;
	Result.Boundaries = Profile.Boundaries;
	AddUnique(Result.SourceEntityIds, Profile.Source.SequenceNodeId);
	AddUnique(Result.SourceEntityIds, Profile.Source.CriterionNodeId);
	for (const FBlueprintLensLC4AsyncLaunch& Launch : Result.Launches)
	{
		AddUnique(Result.SourceEntityIds, Launch.NodeId);
		AddUnique(Result.SourceEntityIds, Launch.SourcePinId);
	}
	for (const FBlueprintLensLC4AsyncContinuation& Continuation : Result.Continuations)
	{
		AddUnique(Result.SourceEntityIds, Continuation.NodeId);
		AddUnique(Result.SourceEntityIds, Continuation.ResumePinId);
	}
	for (const FBlueprintLensLC4AsyncArrival& Arrival : Result.Arrivals)
	{
		AddUnique(Result.SourceEntityIds, Arrival.NodeId);
		AddUnique(Result.SourceEntityIds, Arrival.ExecutePinId);
		AddUnique(Result.SourceEntityIds, Arrival.ReleasePinId);
	}
	for (const FBlueprintLensLC4AsyncInvocation& Invocation : Profile.Invocations)
	{
		if (Invocation.Variant != Variant)
		{
			continue;
		}
		Result.Invocations.Add(Invocation);
		Result.Proofs.Append(Invocation.Proofs);
		for (const FBlueprintLensLC4AsyncRelation& Relation : Invocation.Relations)
		{
			Result.AllRelationIds.Add(Relation.RelationId);
		}
	}
	if (Result.Invocations.Num() != 2 || Result.AllRelationIds.Num() != 22 || Result.Proofs.Num() != 2)
	{
		Result.Status = EBlueprintLensLC4AsyncProjectionStatus::Frontier;
		Result.DiagnosticCode = TEXT("LC4_ASYNC_FRONTIER_INCOMPLETE_VARIANT");
		return Result;
	}
	for (const FBlueprintLensLC4AsyncProof& Proof : Result.Proofs)
	{
		if (Proof.bLeftReachesRight || Proof.bRightReachesLeft || !Proof.bRelationSetComplete ||
			Proof.Result != TEXT("incomparable"))
		{
			Result.Status = EBlueprintLensLC4AsyncProjectionStatus::Frontier;
			Result.DiagnosticCode = TEXT("LC4_ASYNC_FRONTIER_PROOF_INVALID");
			return Result;
		}
	}
	const FString StructuralLedger = TEXT("launch:A>B|A:continuation>complete>arrival>socket|B:continuation>complete>arrival>socket|proof:A!>B,B!>A,complete|barrier:2/2>release_once|criterion:after_release|boundaries:4");
	Result.StructuralSignature = Sha256(StructuralLedger);
	TArray<FString> Evidence;
	for (const FBlueprintLensLC4AsyncInvocation& Invocation : Result.Invocations)
	{
		Evidence.Add(Invocation.ProductId);
		Evidence.Add(Invocation.InvocationId);
		Evidence.Add(Invocation.TraceId);
		Evidence.Append(Invocation.CompletionOrder);
	}
	Result.EvidenceSignature = Sha256(FString::Join(Evidence, TEXT("\n")));
	Result.ProjectionIntegrityHash = Sha256(FString::Printf(
		TEXT("%s\n%s\n%s\n%s"),
		*Result.ProjectorVersion,
		*Result.SourceProfileSha256,
		*Result.StructuralSignature,
		*Result.EvidenceSignature));
	Result.Status = EBlueprintLensLC4AsyncProjectionStatus::PartialOrderJoin;
	Result.DiagnosticCode = TEXT("LC4_ASYNC_PARTIAL_ORDER_JOIN_COMPLETE");
	return Result;
}
