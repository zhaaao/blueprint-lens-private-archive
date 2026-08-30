#include "BlueprintLensLC7Profile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR ProfileFormat[] =
	TEXT("blueprint-lens-lc7-static-scc-profile");
constexpr TCHAR ReviewedFormat[] =
	TEXT("blueprint-lens-lc7-reviewed-ground-truth");
constexpr TCHAR ReadinessFormat[] = TEXT("blueprint-lens-lc7-readiness");
constexpr TCHAR ContractVersion[] = TEXT("1.0.0");
constexpr TCHAR ProfileId[] = TEXT("LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1");
constexpr TCHAR ClaimScope[] = TEXT("STATIC_SOURCE_VISIBLE_SCC");
constexpr TCHAR RuntimeIterations[] = TEXT("NOT_CLAIMED");
constexpr TCHAR BlueprintAssetPath[] =
	TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC");
constexpr TCHAR GraphId[] =
	TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph");
constexpr TCHAR AssetSha256[] =
	TEXT("5bb50da585cf5f905b50ff06e2e56251aa538f3fdbc18232a4470778d4780232");
constexpr TCHAR RawSha256[] =
	TEXT("645c7e67174a8752861ef1951fd4b885a9afda09a39d0f27b61cac079d76710a");
constexpr TCHAR SourceSha256[] =
	TEXT("3656c5d860cf5c033d34c36f486ea93bafa234e42c032d5a1f79bfa1d29eeff1");
constexpr TCHAR AuditSha256[] =
	TEXT("db20ddb504276490e8c8d824554cad4e74274b81ac2f82c97d7e576776b7fcf1");
constexpr TCHAR SchemaGateCommit[] =
	TEXT("3b40b1d2dd3b998f989be5ffcaed9cbbc6530ba3");
constexpr TCHAR CriterionNodeId[] =
	TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph::"
		 "node::c0a8dfab-41b4-77d0-0c2f-19b475bd5dac");
constexpr TCHAR EntryExitNodeId[] =
	TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph::"
		 "node::cbd7b169-4f74-9746-29b8-539f9f4310f9");

struct FLC7Binding
{
	FString BlueprintAssetPath;
	FString GraphId;
	FString AssetSha256;
	FString RawSha256;
	FString SourceSha256;
	FString AuditSha256;
};

struct FLC7ReviewedEndpoint
{
	FString EdgeId;
	FString Kind;
	FString SourceNodeId;
	FString SourcePinId;
	FString TargetNodeId;
	FString TargetPinId;
};

FBlueprintLensLC7LoadResult Failure(
	const TCHAR* Code,
	const FString& Detail = FString())
{
	FBlueprintLensLC7LoadResult Result;
	Result.Error = Detail.IsEmpty()
		? FString(Code)
		: FString::Printf(TEXT("%s: %s"), Code, *Detail);
	return Result;
}

bool ParseJson(const FString& Path, TSharedPtr<FJsonObject>& Root)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) &&
		Root.IsValid();
}

bool StringField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& Out,
	const bool bAllowEmpty = false);

bool ObjectField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TSharedPtr<FJsonObject>& Out);

FBlueprintLensLoadResult LoadExplanationWithProjectPaths(
	const FString& ExplanationPath)
{
	TSharedPtr<FJsonObject> Root;
	TSharedPtr<FJsonObject> Source;
	FString IrPath;
	FString SlicePath;
	if (!ParseJson(ExplanationPath, Root) ||
		!ObjectField(Root, TEXT("source"), Source) ||
		!StringField(Source, TEXT("ir_path"), IrPath) ||
		!StringField(Source, TEXT("slice_path"), SlicePath))
	{
		FBlueprintLensLoadResult Result;
		Result.Error = TEXT("LC7_EXPLANATION_SOURCE_INVALID");
		return Result;
	}
	const FString ExpectedIrPath =
		TEXT("artifacts/r1/lc7-static-scc-truth/"
			 "BP_LC7_StaticSCC.ir.v1.json");
	const FString ExpectedSlicePath =
		TEXT("artifacts/r1/lc7-static-scc-truth/"
			 "BP_LC7_StaticSCC.execution.slice.v1.json");
	if (IrPath != ExpectedIrPath || SlicePath != ExpectedSlicePath)
	{
		FBlueprintLensLoadResult Result;
		Result.Error = TEXT("LC7_EXPLANATION_SOURCE_PATH_INVALID");
		return Result;
	}
	const FString RepositoryRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../..")));
	const FString AbsoluteIrPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(RepositoryRoot, IrPath));
	const FString AbsoluteSlicePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(RepositoryRoot, SlicePath));
	if (!FPaths::FileExists(AbsoluteIrPath) ||
		!FPaths::FileExists(AbsoluteSlicePath))
	{
		FBlueprintLensLoadResult Result;
		Result.Error = TEXT("LC7_EXPLANATION_SOURCE_UNREADABLE");
		return Result;
	}
	Source->SetStringField(TEXT("ir_path"), AbsoluteIrPath);
	Source->SetStringField(TEXT("slice_path"), AbsoluteSlicePath);
	FString NormalizedText;
	if (!FJsonSerializer::Serialize(
			Root.ToSharedRef(),
			TJsonWriterFactory<>::Create(&NormalizedText)))
	{
		FBlueprintLensLoadResult Result;
		Result.Error = TEXT("LC7_EXPLANATION_NORMALIZE_FAILED");
		return Result;
	}
	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(), TEXT("BlueprintLensLC7Profile"));
	const FString NormalizedPath = FPaths::Combine(
		Directory, TEXT("BP_LC7_StaticSCC.explanation.normalized.json"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true) ||
		!FFileHelper::SaveStringToFile(
			NormalizedText, *NormalizedPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		FBlueprintLensLoadResult Result;
		Result.Error = TEXT("LC7_EXPLANATION_NORMALIZED_WRITE_FAILED");
		return Result;
	}
	const FBlueprintLensLoadResult Loaded =
		FBlueprintLensExplanationLoader::LoadFile(NormalizedPath);
	if (!Loaded.IsSuccess())
	{
		return Loaded;
	}
	TSharedRef<FBlueprintLensExplanationModel> CanonicalModel =
		MakeShared<FBlueprintLensExplanationModel>(*Loaded.Model);
	CanonicalModel->Source.IrPath = IrPath;
	CanonicalModel->Source.SlicePath = SlicePath;
	FBlueprintLensLoadResult Result;
	Result.Model = CanonicalModel;
	return Result;
}

bool HashFile(const FString& Path, FString& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return false;
	}
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32)
	{
		return false;
	}
	Out = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

bool StringField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& Out,
	const bool bAllowEmpty)
{
	return Object.IsValid() && Object->TryGetStringField(Field, Out) &&
		(bAllowEmpty || !Out.IsEmpty());
}

bool IntField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32& Out)
{
	double Number = 0.0;
	if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number) ||
		!FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
	{
		return false;
	}
	Out = FMath::RoundToInt(Number);
	return Out >= 0;
}

bool BoolField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	bool& Out)
{
	return Object.IsValid() && Object->TryGetBoolField(Field, Out);
}

bool ObjectField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TSharedPtr<FJsonObject>& Out)
{
	const TSharedPtr<FJsonObject>* Value = nullptr;
	if (!Object.IsValid() || !Object->TryGetObjectField(Field, Value) ||
		Value == nullptr || !Value->IsValid())
	{
		return false;
	}
	Out = *Value;
	return true;
}

bool ArrayField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	const TArray<TSharedPtr<FJsonValue>>*& Out)
{
	return Object.IsValid() && Object->TryGetArrayField(Field, Out) &&
		Out != nullptr;
}

bool JsonObjectAt(
	const TSharedPtr<FJsonValue>& Value,
	TSharedPtr<FJsonObject>& Out)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	Out = Value->AsObject();
	return Out.IsValid();
}

bool StringArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TArray<FString>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!ArrayField(Object, Field, Values))
	{
		return false;
	}
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Item;
		if (!Value.IsValid() || !Value->TryGetString(Item) || Item.IsEmpty() ||
			Seen.Contains(Item))
		{
			return false;
		}
		Seen.Add(Item);
		Out.Add(MoveTemp(Item));
	}
	return true;
}

bool SameSet(const TArray<FString>& Left, const TArray<FString>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	TSet<FString> LeftSet;
	TSet<FString> RightSet;
	for (const FString& Item : Left)
	{
		LeftSet.Add(Item);
	}
	for (const FString& Item : Right)
	{
		RightSet.Add(Item);
	}
	if (LeftSet.Num() != Left.Num() || RightSet.Num() != Right.Num())
	{
		return false;
	}
	for (const FString& Item : LeftSet)
	{
		if (!RightSet.Contains(Item))
		{
			return false;
		}
	}
	return true;
}

const TArray<FString>& ExpectedMemberNodeIds()
{
	static const TArray<FString> Values = {
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::0805eaa8-47d4-f0af-bd2f-ada703333412"),
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::08b1b7d1-4cfa-e2de-8089-b5832da5a751"),
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::cbd7b169-4f74-9746-29b8-539f9f4310f9")};
	return Values;
}

const TArray<FString>& ExpectedMemberUnitIds()
{
	static const TArray<FString> Values = {
		TEXT("unit.control.0805eaa8-47d4-f0af-bd2f-ada703333412"),
		TEXT("unit.control.08b1b7d1-4cfa-e2de-8089-b5832da5a751"),
		TEXT("unit.control.cbd7b169-4f74-9746-29b8-539f9f4310f9")};
	return Values;
}

const TArray<FString>& ExpectedOrderedRelationIds()
{
	static const TArray<FString> Values = {
		TEXT("relation.controls_execution.79caf299c113811f"),
		TEXT("relation.execution_predecessor.06a3c388470810d1"),
		TEXT("relation.execution_predecessor.920427473f69bbb9")};
	return Values;
}

bool ParseBinding(
	const TSharedPtr<FJsonObject>& Object,
	FLC7Binding& Out)
{
	return StringField(
			Object, TEXT("blueprint_asset_path"), Out.BlueprintAssetPath) &&
		StringField(Object, TEXT("graph_id"), Out.GraphId) &&
		StringField(Object, TEXT("asset_sha256"), Out.AssetSha256) &&
		StringField(Object, TEXT("raw_sha256"), Out.RawSha256) &&
		StringField(Object, TEXT("source_sha256"), Out.SourceSha256) &&
		StringField(Object, TEXT("audit_sha256"), Out.AuditSha256);
}

bool IsExpectedBinding(const FLC7Binding& Binding)
{
	return Binding.BlueprintAssetPath == BlueprintAssetPath &&
		Binding.GraphId == GraphId &&
		Binding.AssetSha256.Equals(AssetSha256, ESearchCase::IgnoreCase) &&
		Binding.RawSha256.Equals(RawSha256, ESearchCase::IgnoreCase) &&
		Binding.SourceSha256.Equals(SourceSha256, ESearchCase::IgnoreCase) &&
		Binding.AuditSha256.Equals(AuditSha256, ESearchCase::IgnoreCase);
}

bool SameBinding(const FLC7Binding& Left, const FLC7Binding& Right)
{
	return Left.BlueprintAssetPath == Right.BlueprintAssetPath &&
		Left.GraphId == Right.GraphId &&
		Left.AssetSha256.Equals(Right.AssetSha256, ESearchCase::IgnoreCase) &&
		Left.RawSha256.Equals(Right.RawSha256, ESearchCase::IgnoreCase) &&
		Left.SourceSha256.Equals(Right.SourceSha256, ESearchCase::IgnoreCase) &&
		Left.AuditSha256.Equals(Right.AuditSha256, ESearchCase::IgnoreCase);
}

bool ParseSCC(
	const TSharedPtr<FJsonObject>& Object,
	FBlueprintLensLC7SCC& Out)
{
	return StringField(Object, TEXT("entry_node_id"), Out.EntryNodeId) &&
		StringField(Object, TEXT("exit_node_id"), Out.ExitNodeId) &&
		StringArray(Object, TEXT("member_node_ids"), Out.MemberNodeIds) &&
		StringArray(Object, TEXT("internal_edge_ids"), Out.InternalEdgeIds) &&
		StringArray(Object, TEXT("incoming_edge_ids"), Out.IncomingEdgeIds) &&
		StringArray(Object, TEXT("outgoing_edge_ids"), Out.OutgoingEdgeIds) &&
		StringArray(Object, TEXT("returning_edge_ids"), Out.ReturningEdgeIds);
}

bool SameSourceSCC(
	const FBlueprintLensLC7SCC& Left,
	const FBlueprintLensLC7SCC& Right)
{
	return Left.EntryNodeId == Right.EntryNodeId &&
		Left.ExitNodeId == Right.ExitNodeId &&
		Left.MemberNodeIds == Right.MemberNodeIds &&
		Left.InternalEdgeIds == Right.InternalEdgeIds &&
		Left.IncomingEdgeIds == Right.IncomingEdgeIds &&
		Left.OutgoingEdgeIds == Right.OutgoingEdgeIds &&
		Left.ReturningEdgeIds == Right.ReturningEdgeIds;
}

bool ParseProfile(
	const TSharedPtr<FJsonObject>& Root,
	FBlueprintLensLC7Profile& Profile,
	FLC7Binding& Binding)
{
	FString Format;
	FString Version;
	TSharedPtr<FJsonObject> Counts;
	TSharedPtr<FJsonObject> SourceBinding;
	TSharedPtr<FJsonObject> SCC;
	int32 SCCMembers = 0;
	int32 InternalEdges = 0;
	int32 IncomingEdges = 0;
	int32 OutgoingEdges = 0;
	if (!StringField(Root, TEXT("format"), Format) || Format != ProfileFormat ||
		!StringField(Root, TEXT("format_version"), Version) ||
		Version != ContractVersion ||
		!StringField(Root, TEXT("profile_id"), Profile.ProfileId) ||
		Profile.ProfileId != ProfileId ||
		!StringField(Root, TEXT("claim_scope"), Profile.ClaimScope) ||
		Profile.ClaimScope != ClaimScope ||
		!StringField(
			Root, TEXT("runtime_iterations"), Profile.RuntimeIterations) ||
		Profile.RuntimeIterations != RuntimeIterations ||
		!StringField(Root, TEXT("criterion_node_id"), Profile.CriterionNodeId) ||
		Profile.CriterionNodeId != CriterionNodeId ||
		!ObjectField(Root, TEXT("counts"), Counts) ||
		!IntField(Counts, TEXT("nodes"), Profile.SourceNodeCount) ||
		Profile.SourceNodeCount != 10 ||
		!IntField(Counts, TEXT("edges"), Profile.SourceEdgeCount) ||
		Profile.SourceEdgeCount != 10 ||
		!IntField(Counts, TEXT("scc_members"), SCCMembers) || SCCMembers != 3 ||
		!IntField(Counts, TEXT("internal_edges"), InternalEdges) ||
		InternalEdges != 3 ||
		!IntField(Counts, TEXT("incoming_edges"), IncomingEdges) ||
		IncomingEdges != 1 ||
		!IntField(Counts, TEXT("outgoing_edges"), OutgoingEdges) ||
		OutgoingEdges != 1 ||
		!ObjectField(Root, TEXT("source_binding"), SourceBinding) ||
		!ParseBinding(SourceBinding, Binding) || !IsExpectedBinding(Binding) ||
		!ObjectField(Root, TEXT("scc"), SCC) || !ParseSCC(SCC, Profile.SCC))
	{
		return false;
	}
	return Profile.SCC.EntryNodeId == EntryExitNodeId &&
		Profile.SCC.ExitNodeId == EntryExitNodeId &&
		Profile.SCC.MemberNodeIds == ExpectedMemberNodeIds() &&
		Profile.SCC.InternalEdgeIds.Num() == InternalEdges &&
		Profile.SCC.IncomingEdgeIds.Num() == IncomingEdges &&
		Profile.SCC.OutgoingEdgeIds.Num() == OutgoingEdges &&
		Profile.SCC.ReturningEdgeIds.Num() == 1 &&
		Profile.SCC.MemberNodeIds.Num() == SCCMembers &&
		Profile.SCC.InternalEdgeIds.Contains(Profile.SCC.ReturningEdgeIds[0]);
}

bool ParseReviewedEndpoints(
	const TSharedPtr<FJsonObject>& Root,
	TMap<FString, FLC7ReviewedEndpoint>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!ArrayField(Root, TEXT("source_pin_endpoints"), Values) ||
		Values->Num() != 8)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		TSharedPtr<FJsonObject> Object;
		FLC7ReviewedEndpoint Endpoint;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("id"), Endpoint.EdgeId) ||
			!StringField(Object, TEXT("kind"), Endpoint.Kind) ||
			!StringField(Object, TEXT("source_node_id"), Endpoint.SourceNodeId) ||
			!StringField(Object, TEXT("source_pin_id"), Endpoint.SourcePinId) ||
			!StringField(Object, TEXT("target_node_id"), Endpoint.TargetNodeId) ||
			!StringField(Object, TEXT("target_pin_id"), Endpoint.TargetPinId) ||
			(Endpoint.Kind != TEXT("execution") && Endpoint.Kind != TEXT("data")) ||
			Out.Contains(Endpoint.EdgeId))
		{
			return false;
		}
		Out.Add(Endpoint.EdgeId, MoveTemp(Endpoint));
	}
	return true;
}

bool ParseReviewed(
	const TSharedPtr<FJsonObject>& Root,
	const FBlueprintLensLC7Profile& Profile,
	const FLC7Binding& ProfileBinding,
	const FString& ExplanationSha256,
	const FString& SCCProfileSha256,
	TMap<FString, FLC7ReviewedEndpoint>& Endpoints)
{
	FString Format;
	FString Version;
	FString ReviewedProfileId;
	FString ReviewedScope;
	FString ReviewedRuntime;
	FString ReviewedCriterion;
	TSharedPtr<FJsonObject> BindingObject;
	TSharedPtr<FJsonObject> SCCObject;
	TSharedPtr<FJsonObject> ProductHashes;
	TSharedPtr<FJsonObject> Review;
	TSharedPtr<FJsonObject> NativeAgreement;
	FLC7Binding Binding;
	FBlueprintLensLC7SCC ReviewedSCC;
	if (!StringField(Root, TEXT("format"), Format) || Format != ReviewedFormat ||
		!StringField(Root, TEXT("format_version"), Version) ||
		Version != ContractVersion ||
		!StringField(Root, TEXT("profile_id"), ReviewedProfileId) ||
		ReviewedProfileId != Profile.ProfileId ||
		!StringField(Root, TEXT("claim_scope"), ReviewedScope) ||
		ReviewedScope != Profile.ClaimScope ||
		!StringField(Root, TEXT("runtime_iterations"), ReviewedRuntime) ||
		ReviewedRuntime != Profile.RuntimeIterations ||
		!StringField(Root, TEXT("criterion_node_id"), ReviewedCriterion) ||
		ReviewedCriterion != Profile.CriterionNodeId ||
		!ObjectField(Root, TEXT("binding"), BindingObject) ||
		!ParseBinding(BindingObject, Binding) ||
		!SameBinding(Binding, ProfileBinding) || !IsExpectedBinding(Binding) ||
		!ObjectField(Root, TEXT("scc"), SCCObject) ||
		!ParseSCC(SCCObject, ReviewedSCC) ||
		!SameSourceSCC(Profile.SCC, ReviewedSCC) ||
		!ObjectField(Root, TEXT("product_hashes"), ProductHashes) ||
		!ObjectField(Root, TEXT("review"), Review) ||
		!ObjectField(Root, TEXT("native_run_agreement"), NativeAgreement))
	{
		return false;
	}
	FString BoundExplanation;
	FString BoundProfile;
	FString ReviewStatus;
	FString ReviewKind;
	FString HumanReview;
	bool bByteIdentical = false;
	TSharedPtr<FJsonObject> NativeHashes;
	const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
	if (!StringField(
			ProductHashes, TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
			BoundExplanation) ||
		!StringField(
			ProductHashes, TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"),
			BoundProfile) ||
		!BoundExplanation.Equals(ExplanationSha256, ESearchCase::IgnoreCase) ||
		!BoundProfile.Equals(SCCProfileSha256, ESearchCase::IgnoreCase) ||
		!StringField(Review, TEXT("status"), ReviewStatus) ||
		ReviewStatus != TEXT("frozen") ||
		!StringField(Review, TEXT("kind"), ReviewKind) ||
		ReviewKind != TEXT("independent_engineering_source_review") ||
		!StringField(
			Review, TEXT("human_comprehension_review"), HumanReview) ||
		HumanReview != RuntimeIterations ||
		!BoolField(NativeAgreement, TEXT("byte_identical"), bByteIdentical) ||
		!bByteIdentical ||
		!ObjectField(NativeAgreement, TEXT("hashes"), NativeHashes) ||
		!ArrayField(NativeAgreement, TEXT("runs"), Runs) || Runs->Num() != 2 ||
		(*Runs)[0]->AsString() != TEXT("run1") ||
		(*Runs)[1]->AsString() != TEXT("run2"))
	{
		return false;
	}
	FString NativeRaw;
	FString NativeSource;
	FString NativeAudit;
	return StringField(
			NativeHashes, TEXT("BP_LC7_StaticSCC.raw-0.2.json"), NativeRaw) &&
		StringField(
			NativeHashes, TEXT("BP_LC7_StaticSCC.scc-source.json"), NativeSource) &&
		StringField(
			NativeHashes, TEXT("BP_LC7_StaticSCC.scc-audit.tsv"), NativeAudit) &&
		NativeRaw.Equals(Profile.RawSha256, ESearchCase::IgnoreCase) &&
		NativeSource.Equals(Profile.SourceSha256, ESearchCase::IgnoreCase) &&
		NativeAudit.Equals(Profile.AuditSha256, ESearchCase::IgnoreCase) &&
		ParseReviewedEndpoints(Root, Endpoints);
}

bool HashPairMatches(
	const TSharedPtr<FJsonObject>& Hashes,
	const TCHAR* Basename,
	const TCHAR* ArtifactPath,
	const FString& Expected)
{
	FString BasenameHash;
	FString ArtifactHash;
	return StringField(Hashes, Basename, BasenameHash) &&
		StringField(Hashes, ArtifactPath, ArtifactHash) &&
		BasenameHash.Equals(Expected, ESearchCase::IgnoreCase) &&
		ArtifactHash.Equals(Expected, ESearchCase::IgnoreCase);
}

bool ParseReadiness(
	const TSharedPtr<FJsonObject>& Root,
	FBlueprintLensLC7Profile& Profile)
{
	FString Format;
	FString Version;
	FString ReadinessProfileId;
	FString ReadinessScope;
	FString Commit;
	int32 ChecksPassed = 0;
	int32 ChecksTotal = 0;
	int32 RequiredFileCount = 0;
	TSharedPtr<FJsonObject> Hashes;
	TSharedPtr<FJsonObject> Checks;
	const TArray<TSharedPtr<FJsonValue>>* NotAuthorized = nullptr;
	if (!StringField(Root, TEXT("format"), Format) || Format != ReadinessFormat ||
		!StringField(Root, TEXT("format_version"), Version) ||
		Version != ContractVersion ||
		!StringField(Root, TEXT("profile_id"), ReadinessProfileId) ||
		ReadinessProfileId != Profile.ProfileId ||
		!StringField(Root, TEXT("claim_scope"), ReadinessScope) ||
		ReadinessScope != Profile.ClaimScope ||
		!StringField(Root, TEXT("status"), Profile.ReadinessStatus) ||
		Profile.ReadinessStatus != TEXT("TRUTH_FROZEN") ||
		!StringField(Root, TEXT("schema_gate_commit"), Commit) ||
		Commit != SchemaGateCommit ||
		!IntField(Root, TEXT("checks_passed"), ChecksPassed) ||
		ChecksPassed != 22 ||
		!IntField(Root, TEXT("checks_total"), ChecksTotal) || ChecksTotal != 22 ||
		!IntField(Root, TEXT("required_file_count"), RequiredFileCount) ||
		RequiredFileCount != 58 ||
		!ObjectField(Root, TEXT("hashes"), Hashes) || Hashes->Values.Num() != 66 ||
		!ObjectField(Root, TEXT("checks"), Checks) || Checks->Values.Num() != 22 ||
		!ArrayField(Root, TEXT("not_authorized"), NotAuthorized) ||
		NotAuthorized->Num() != 7)
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Checks->Values)
	{
		bool bPassed = false;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(bPassed) || !bPassed)
		{
			return false;
		}
	}
	return HashPairMatches(
			Hashes,
			TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "BP_LC7_StaticSCC.explanation.v1.json"),
			Profile.ExplanationSha256) &&
		HashPairMatches(
			Hashes,
			TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "BP_LC7_StaticSCC.scc-profile.v1.json"),
			Profile.SCCProfileSha256) &&
		HashPairMatches(
			Hashes,
			TEXT("reviewed-ground-truth.v1.json"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "reviewed-ground-truth.v1.json"),
			Profile.ReviewedSha256) &&
		HashPairMatches(
			Hashes,
			TEXT("BP_LC7_StaticSCC.raw-0.2.json"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "BP_LC7_StaticSCC.raw-0.2.json"),
			Profile.RawSha256) &&
		HashPairMatches(
			Hashes,
			TEXT("BP_LC7_StaticSCC.scc-source.json"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "BP_LC7_StaticSCC.scc-source.json"),
			Profile.SourceSha256) &&
		HashPairMatches(
			Hashes,
			TEXT("BP_LC7_StaticSCC.scc-audit.tsv"),
			TEXT("artifacts/r1/lc7-static-scc-truth/"
				 "BP_LC7_StaticSCC.scc-audit.tsv"),
			Profile.AuditSha256);
}

bool IsExecutionRelation(const EBlueprintLensRelationKind Kind)
{
	return Kind == EBlueprintLensRelationKind::ExecutionPredecessor ||
		Kind == EBlueprintLensRelationKind::ControlsExecution;
}

bool ValidateExplanation(
	const FBlueprintLensExplanationModel& Model,
	FBlueprintLensLC7Profile& Profile,
	const TMap<FString, FLC7ReviewedEndpoint>& ReviewedEndpoints)
{
	if (Model.Source.BlueprintAssetPath != Profile.BlueprintAssetPath ||
		Model.Source.GraphId != Profile.GraphId ||
		!Model.Source.BlueprintPackageSha256.Equals(
			Profile.AssetSha256, ESearchCase::IgnoreCase) ||
		Model.Query.CriterionSourceNodeId != Profile.CriterionNodeId ||
		Model.CriterionUnitId.IsEmpty() || Model.Units.Num() != 8 ||
		Model.Relations.Num() != 8 || Model.Counts.Units != 8 ||
		Model.Counts.Relations != 8)
	{
		return false;
	}

	TMap<FString, FString> UnitToNode;
	TMap<FString, FString> NodeToUnit;
	TMap<FString, TSet<FString>> UnitPins;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		if (Unit.Id.IsEmpty() || Unit.Title.IsEmpty() ||
			Unit.SourceReferences.Num() != 1 || UnitToNode.Contains(Unit.Id))
		{
			return false;
		}
		const FBlueprintLensSourceReference& Reference = Unit.SourceReferences[0];
		if (!Reference.bPrimary || Reference.SourceNodeId.IsEmpty() ||
			Reference.NativeNodeGuid.IsEmpty() ||
			Reference.BlueprintAssetPath != Profile.BlueprintAssetPath ||
			Reference.GraphId != Profile.GraphId ||
			Reference.SourcePinIds.IsEmpty() ||
			NodeToUnit.Contains(Reference.SourceNodeId))
		{
			return false;
		}
		TSet<FString> Pins;
		for (const FString& PinId : Reference.SourcePinIds)
		{
			if (PinId.IsEmpty() || Pins.Contains(PinId))
			{
				return false;
			}
			Pins.Add(PinId);
		}
		UnitToNode.Add(Unit.Id, Reference.SourceNodeId);
		NodeToUnit.Add(Reference.SourceNodeId, Unit.Id);
		UnitPins.Add(Unit.Id, MoveTemp(Pins));
	}

	const FBlueprintLensUnit* Criterion = Model.FindUnit(Model.CriterionUnitId);
	if (Criterion == nullptr || Criterion->Role != EBlueprintLensRole::Criterion ||
		UnitToNode.FindRef(Model.CriterionUnitId) != Profile.CriterionNodeId)
	{
		return false;
	}
	Profile.CriterionUnitId = Model.CriterionUnitId;

	if (!Model.bHasGroups || Model.Groups.Num() != 1)
	{
		return false;
	}
	const FBlueprintLensGroup& Group = Model.Groups[0];
	if (Group.Kind != EBlueprintLensGroupKind::Scc ||
		Group.ProjectionStatus != EBlueprintLensProjectionStatus::StructuralOnly ||
		Group.MemberCount != 3 || Group.OrderedUnitIds != ExpectedMemberUnitIds() ||
		Group.OrderedRelationIds != ExpectedOrderedRelationIds() ||
		!Group.bHasExitUnitId || Group.EntryUnitId.IsEmpty() ||
		Group.ExitUnitId.IsEmpty())
	{
		return false;
	}
	Profile.StructuralSCCCount = 1;
	Profile.SCC.GroupId = Group.Id;
	Profile.SCC.OrderedMemberUnitIds = Group.OrderedUnitIds;
	Profile.SCC.OrderedRelationIds = Group.OrderedRelationIds;
	Profile.SCC.EntryUnitId = Group.EntryUnitId;
	Profile.SCC.ExitUnitId = Group.ExitUnitId;
	if (Profile.SCC.EntryUnitId != Profile.SCC.ExitUnitId ||
		UnitToNode.FindRef(Profile.SCC.EntryUnitId) != Profile.SCC.EntryNodeId ||
		UnitToNode.FindRef(Profile.SCC.ExitUnitId) != Profile.SCC.ExitNodeId)
	{
		return false;
	}
	for (int32 Index = 0; Index < Profile.SCC.MemberNodeIds.Num(); ++Index)
	{
		if (NodeToUnit.FindRef(Profile.SCC.MemberNodeIds[Index]) !=
			Profile.SCC.OrderedMemberUnitIds[Index])
		{
			return false;
		}
	}

	TSet<FString> MemberNodes;
	for (const FString& NodeId : Profile.SCC.MemberNodeIds)
	{
		MemberNodes.Add(NodeId);
	}
	TMap<FString, FString> EdgeToRelation;
	TSet<FString> RelationIds;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Id.IsEmpty() || RelationIds.Contains(Relation.Id) ||
			Relation.SourceEdgeIds.Num() != 1 ||
			!Relation.bHasSourceEdgeEndpoints ||
			Relation.SourceEdgeEndpoints.Num() != 1 ||
			!UnitToNode.Contains(Relation.SourceUnitId) ||
			!UnitToNode.Contains(Relation.TargetUnitId))
		{
			return false;
		}
		RelationIds.Add(Relation.Id);
		const FBlueprintLensSourceEdgeEndpoint& Endpoint =
			Relation.SourceEdgeEndpoints[0];
		const FString& EdgeId = Relation.SourceEdgeIds[0];
		const FLC7ReviewedEndpoint* Reviewed = ReviewedEndpoints.Find(EdgeId);
		if (EdgeId.IsEmpty() || EdgeToRelation.Contains(EdgeId) ||
			Endpoint.SourceEdgeId != EdgeId || Reviewed == nullptr ||
			Endpoint.SourceNodeId != UnitToNode.FindRef(Relation.SourceUnitId) ||
			Endpoint.TargetNodeId != UnitToNode.FindRef(Relation.TargetUnitId) ||
			Endpoint.SourceNodeId != Reviewed->SourceNodeId ||
			Endpoint.SourcePinId != Reviewed->SourcePinId ||
			Endpoint.TargetNodeId != Reviewed->TargetNodeId ||
			Endpoint.TargetPinId != Reviewed->TargetPinId ||
			!UnitPins.FindRef(Relation.SourceUnitId).Contains(Endpoint.SourcePinId) ||
			!UnitPins.FindRef(Relation.TargetUnitId).Contains(Endpoint.TargetPinId) ||
			(IsExecutionRelation(Relation.Kind) !=
				(Reviewed->Kind == TEXT("execution"))))
		{
			return false;
		}
		FBlueprintLensLC7RelationBinding Binding;
		Binding.RelationId = Relation.Id;
		Binding.SourceEdgeId = EdgeId;
		Binding.SourceUnitId = Relation.SourceUnitId;
		Binding.TargetUnitId = Relation.TargetUnitId;
		Binding.SourceNodeId = Endpoint.SourceNodeId;
		Binding.TargetNodeId = Endpoint.TargetNodeId;
		Binding.SourcePinId = Endpoint.SourcePinId;
		Binding.TargetPinId = Endpoint.TargetPinId;
		Binding.bReturning = Profile.SCC.ReturningEdgeIds.Contains(EdgeId);
		Profile.Relations.Add(MoveTemp(Binding));
		EdgeToRelation.Add(EdgeId, Relation.Id);
	}
	if (EdgeToRelation.Num() != ReviewedEndpoints.Num())
	{
		return false;
	}

	TArray<FString> InternalRelationIds;
	for (const FString& EdgeId : Profile.SCC.InternalEdgeIds)
	{
		const FBlueprintLensLC7RelationBinding* Binding =
			Profile.Relations.FindByPredicate(
				[&EdgeId](const FBlueprintLensLC7RelationBinding& Candidate)
				{
					return Candidate.SourceEdgeId == EdgeId;
				});
		if (Binding == nullptr || !MemberNodes.Contains(Binding->SourceNodeId) ||
			!MemberNodes.Contains(Binding->TargetNodeId))
		{
			return false;
		}
		InternalRelationIds.Add(Binding->RelationId);
	}
	for (const FString& EdgeId : Profile.SCC.IncomingEdgeIds)
	{
		const FBlueprintLensLC7RelationBinding* Binding =
			Profile.Relations.FindByPredicate(
				[&EdgeId](const FBlueprintLensLC7RelationBinding& Candidate)
				{
					return Candidate.SourceEdgeId == EdgeId;
				});
		if (Binding == nullptr || MemberNodes.Contains(Binding->SourceNodeId) ||
			Binding->TargetNodeId != Profile.SCC.EntryNodeId)
		{
			return false;
		}
	}
	for (const FString& EdgeId : Profile.SCC.OutgoingEdgeIds)
	{
		const FBlueprintLensLC7RelationBinding* Binding =
			Profile.Relations.FindByPredicate(
				[&EdgeId](const FBlueprintLensLC7RelationBinding& Candidate)
				{
					return Candidate.SourceEdgeId == EdgeId;
				});
		if (Binding == nullptr || Binding->SourceNodeId != Profile.SCC.ExitNodeId ||
			Binding->TargetNodeId != Profile.CriterionNodeId)
		{
			return false;
		}
	}
	const FString& ReturningEdgeId = Profile.SCC.ReturningEdgeIds[0];
	const FBlueprintLensLC7RelationBinding* Returning =
		Profile.Relations.FindByPredicate(
			[&ReturningEdgeId](const FBlueprintLensLC7RelationBinding& Candidate)
			{
				return Candidate.SourceEdgeId == ReturningEdgeId;
			});
	return Returning != nullptr && Returning->bReturning &&
		Returning->TargetNodeId == Profile.SCC.EntryNodeId &&
		Returning->SourceNodeId != Profile.SCC.EntryNodeId &&
		SameSet(InternalRelationIds, Profile.SCC.OrderedRelationIds);
}
} // namespace

bool FBlueprintLensLC7Profile::IsValid() const
{
	if (bLiveExplanation)
	{
		return ProfileId == ::ProfileId &&
			ClaimScope == TEXT("STATIC_SLICE_SCC_ADAPTATION") &&
			RuntimeIterations == ::RuntimeIterations &&
			ReadinessStatus == TEXT("LIVE_EXPLANATION_STRUCTURAL_ADAPTATION") &&
			!BlueprintAssetPath.IsEmpty() && !GraphId.IsEmpty() &&
			!CriterionNodeId.IsEmpty() && !CriterionUnitId.IsEmpty() &&
			ExplanationUnitCount > 0 && ExplanationUnitCount <= 10 &&
			ExplanationRelationCount > 0 && ExplanationRelationCount <= 10 &&
			StructuralSCCCount == 1 &&
			SCC.MemberNodeIds.Num() >= 2 && SCC.MemberNodeIds.Num() <= 6 &&
			SCC.OrderedMemberUnitIds.Num() == SCC.MemberNodeIds.Num() &&
			SCC.InternalEdgeIds.Num() >= SCC.MemberNodeIds.Num() &&
			SCC.InternalEdgeIds.Num() <= 6 &&
			SCC.OrderedRelationIds.Num() == SCC.InternalEdgeIds.Num() &&
			SCC.IncomingEdgeIds.Num() >= 1 && SCC.IncomingEdgeIds.Num() <= 2 &&
			SCC.OutgoingEdgeIds.Num() <= 1 &&
			SCC.ReturningEdgeIds.Num() <= 1 &&
			(bExitOutsideSlice == SCC.OutgoingEdgeIds.IsEmpty()) &&
			!SCC.EntryUnitId.IsEmpty() &&
			(bExitOutsideSlice
				? SCC.ExitUnitId.IsEmpty()
				: !SCC.ExitUnitId.IsEmpty()) &&
			Relations.Num() == ExplanationRelationCount &&
			ExplanationModel.IsValid() &&
			!RelationFamilyStatement.IsEmpty() &&
			!ExitBoundaryStatement.IsEmpty();
	}
	return ProfileId == ::ProfileId && ClaimScope == ::ClaimScope &&
		RuntimeIterations == ::RuntimeIterations &&
		ReadinessStatus == TEXT("TRUTH_FROZEN") &&
		BlueprintAssetPath == ::BlueprintAssetPath && GraphId == ::GraphId &&
		AssetSha256.Equals(::AssetSha256, ESearchCase::IgnoreCase) &&
		RawSha256.Equals(::RawSha256, ESearchCase::IgnoreCase) &&
		SourceSha256.Equals(::SourceSha256, ESearchCase::IgnoreCase) &&
		AuditSha256.Equals(::AuditSha256, ESearchCase::IgnoreCase) &&
		CriterionNodeId == ::CriterionNodeId && !CriterionUnitId.IsEmpty() &&
		SourceNodeCount == 10 && SourceEdgeCount == 10 &&
		ExplanationUnitCount == 8 && ExplanationRelationCount == 8 &&
		StructuralSCCCount == 1 && SCC.MemberNodeIds.Num() == 3 &&
		SCC.InternalEdgeIds.Num() == 3 && SCC.IncomingEdgeIds.Num() == 1 &&
		SCC.OutgoingEdgeIds.Num() == 1 && SCC.ReturningEdgeIds.Num() == 1 &&
		SCC.OrderedMemberUnitIds.Num() == 3 &&
		SCC.OrderedRelationIds.Num() == 3 && Relations.Num() == 8 &&
		ExplanationModel.IsValid() &&
		!ExplanationPath.IsEmpty() && !SCCProfilePath.IsEmpty() &&
		!ReviewedPath.IsEmpty() && !ReadinessPath.IsEmpty() &&
		!ExplanationSha256.IsEmpty() && !SCCProfileSha256.IsEmpty() &&
		!ReviewedSha256.IsEmpty() && !ReadinessSha256.IsEmpty();
}

FBlueprintLensLC7LoadResult FBlueprintLensLC7ProfileLoader::LoadFiles(
	const FString& ExplanationPath,
	const FString& SCCProfilePath,
	const FString& ReviewedPath,
	const FString& ReadinessPath)
{
	const FBlueprintLensLoadResult Explanation =
		LoadExplanationWithProjectPaths(ExplanationPath);
	if (!Explanation.IsSuccess())
	{
		return Failure(TEXT("LC7_EXPLANATION_INVALID"), Explanation.Error);
	}

	TSharedPtr<FJsonObject> SCCProfileRoot;
	TSharedPtr<FJsonObject> ReviewedRoot;
	TSharedPtr<FJsonObject> ReadinessRoot;
	if (!ParseJson(SCCProfilePath, SCCProfileRoot) ||
		!ParseJson(ReviewedPath, ReviewedRoot) ||
		!ParseJson(ReadinessPath, ReadinessRoot))
	{
		return Failure(TEXT("LC7_PROFILE_UNREADABLE"));
	}

	TSharedRef<FBlueprintLensLC7Profile> Profile =
		MakeShared<FBlueprintLensLC7Profile>();
	Profile->ExplanationPath = ExplanationPath;
	Profile->SCCProfilePath = SCCProfilePath;
	Profile->ReviewedPath = ReviewedPath;
	Profile->ReadinessPath = ReadinessPath;
	if (!HashFile(ExplanationPath, Profile->ExplanationSha256) ||
		!HashFile(SCCProfilePath, Profile->SCCProfileSha256) ||
		!HashFile(ReviewedPath, Profile->ReviewedSha256) ||
		!HashFile(ReadinessPath, Profile->ReadinessSha256))
	{
		return Failure(TEXT("LC7_PROFILE_HASH_FAILED"));
	}

	FLC7Binding Binding;
	if (!ParseProfile(SCCProfileRoot, *Profile, Binding))
	{
		return Failure(TEXT("LC7_SCC_PROFILE_INVALID"));
	}
	Profile->BlueprintAssetPath = Binding.BlueprintAssetPath;
	Profile->GraphId = Binding.GraphId;
	Profile->AssetSha256 = Binding.AssetSha256.ToLower();
	Profile->RawSha256 = Binding.RawSha256.ToLower();
	Profile->SourceSha256 = Binding.SourceSha256.ToLower();
	Profile->AuditSha256 = Binding.AuditSha256.ToLower();

	TMap<FString, FLC7ReviewedEndpoint> ReviewedEndpoints;
	if (!ParseReviewed(
			ReviewedRoot, *Profile, Binding, Profile->ExplanationSha256,
			Profile->SCCProfileSha256, ReviewedEndpoints))
	{
		return Failure(TEXT("LC7_REVIEWED_TRUTH_INVALID"));
	}
	if (!ParseReadiness(ReadinessRoot, *Profile))
	{
		return Failure(TEXT("LC7_READINESS_INVALID"));
	}
	if (!ValidateExplanation(*Explanation.Model, *Profile, ReviewedEndpoints))
	{
		return Failure(TEXT("LC7_EXPLANATION_BINDING_INVALID"));
	}
	Profile->ExplanationUnitCount = Explanation.Model->Units.Num();
	Profile->ExplanationRelationCount = Explanation.Model->Relations.Num();
	Profile->ExplanationModel = Explanation.Model;
	if (!Profile->IsValid())
	{
		return Failure(TEXT("LC7_PROFILE_FINAL_INVALID"));
	}

	FBlueprintLensLC7LoadResult Result;
	Result.Profile = Profile;
	Result.ExplanationModel = Explanation.Model;
	return Result;
}
