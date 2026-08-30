#include "BlueprintLensLC4AsyncProfile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR ExpectedFormat[] = TEXT("blueprint-lens-async-profile");
constexpr TCHAR ExpectedSchema[] = TEXT("1.0.0");
constexpr TCHAR ExpectedProfile[] = TEXT("LC4_ASYNC_TWO_DELAY_BARRIER_V1");
constexpr TCHAR ExpectedRules[] = TEXT("async_two_delay_barrier_v1");
constexpr TCHAR ExpectedValidation[] = TEXT("VALIDATED_PROFILE");

FBlueprintLensLC4AsyncLoadResult Failure(const TCHAR* Code, const FString& Detail = FString())
{
	FBlueprintLensLC4AsyncLoadResult Result;
	Result.Error = Detail.IsEmpty() ? FString(Code) : FString::Printf(TEXT("%s: %s"), Code, *Detail);
	return Result;
}

bool StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out)
{
	return Object.IsValid() && Object->TryGetStringField(Field, Out) && !Out.IsEmpty();
}

bool BoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& Out)
{
	return Object.IsValid() && Object->TryGetBoolField(Field, Out);
}

bool IntField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32& Out)
{
	double Value = 0.0;
	if (!Object.IsValid() || !Object->TryGetNumberField(Field, Value) ||
		!FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value)))
	{
		return false;
	}
	Out = FMath::RoundToInt(Value);
	return Out >= 0;
}

bool ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TSharedPtr<FJsonObject>& Out)
{
	const TSharedPtr<FJsonObject>* Value = nullptr;
	if (!Object.IsValid() || !Object->TryGetObjectField(Field, Value) || Value == nullptr || !Value->IsValid())
	{
		return false;
	}
	Out = *Value;
	return true;
}

bool ArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const TArray<TSharedPtr<FJsonValue>>*& Out)
{
	return Object.IsValid() && Object->TryGetArrayField(Field, Out) && Out != nullptr;
}

bool StringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!ArrayField(Object, Field, Values))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Item;
		if (!Value.IsValid() || !Value->TryGetString(Item) || Item.IsEmpty() || Out.Contains(Item))
		{
			return false;
		}
		Out.Add(MoveTemp(Item));
	}
	return true;
}

bool ParseJson(const FString& Path, TSharedPtr<FJsonObject>& Root, FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Error = TEXT("file is unreadable");
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Error = TEXT("invalid JSON");
		return false;
	}
	return true;
}

bool HashFile(const FString& Path, FString& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return false;
	}
	TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
	{
		return false;
	}
	Out = BytesToHex(Digest.GetData(), Digest.Num());
	return true;
}

bool JsonObjectAt(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	Out = Value->AsObject();
	return Out.IsValid();
}

bool ParseRelation(const TSharedPtr<FJsonObject>& Object, FBlueprintLensLC4AsyncRelation& Out)
{
	return StringField(Object, TEXT("relation_id"), Out.RelationId) &&
		StringField(Object, TEXT("relation_type"), Out.RelationType) &&
		StringField(Object, TEXT("from_id"), Out.FromId) &&
		StringField(Object, TEXT("to_id"), Out.ToId) &&
		StringField(Object, TEXT("claim_scope"), Out.ClaimScope) &&
		(Out.ClaimScope == TEXT("source_guaranteed") || Out.ClaimScope == TEXT("observed_invocation"));
}

bool ParseProof(const TSharedPtr<FJsonObject>& Object, FBlueprintLensLC4AsyncProof& Out)
{
	return StringField(Object, TEXT("left_continuation_id"), Out.LeftParticipantId) &&
		StringField(Object, TEXT("right_continuation_id"), Out.RightParticipantId) &&
		StringField(Object, TEXT("left_completion_event_id"), Out.LeftCompletionEventId) &&
		StringField(Object, TEXT("right_completion_event_id"), Out.RightCompletionEventId) &&
		BoolField(Object, TEXT("left_reaches_right"), Out.bLeftReachesRight) &&
		BoolField(Object, TEXT("right_reaches_left"), Out.bRightReachesLeft) &&
		BoolField(Object, TEXT("relation_set_complete"), Out.bRelationSetComplete) &&
		StringField(Object, TEXT("result"), Out.Result) &&
		StringField(Object, TEXT("proof_basis"), Out.ProofBasis) &&
		StringArray(Object, TEXT("evidence_relation_ids"), Out.EvidenceRelationIds) &&
		!Out.bLeftReachesRight && !Out.bRightReachesLeft && Out.bRelationSetComplete &&
		Out.Result == TEXT("incomparable") &&
		Out.ProofBasis == TEXT("pairwise_reachability_plus_completeness") &&
		Out.EvidenceRelationIds.Num() == 11;
}

bool ParseInvocation(const TSharedPtr<FJsonObject>& Object, FBlueprintLensLC4AsyncInvocation& Out)
{
	if (!StringField(Object, TEXT("product_id"), Out.ProductId) ||
		!StringField(Object, TEXT("schedule_variant"), Out.Variant) ||
		!StringField(Object, TEXT("invocation_id"), Out.InvocationId) ||
		!StringField(Object, TEXT("instance_id"), Out.InstanceId) ||
		!StringField(Object, TEXT("run_id"), Out.RunId) ||
		!StringField(Object, TEXT("trace_id"), Out.TraceId) ||
		!StringArray(Object, TEXT("launch_event_ids"), Out.LaunchEventIds) ||
		!StringArray(Object, TEXT("completion_event_ids"), Out.CompletionEventIds) ||
		!StringArray(Object, TEXT("arrival_event_ids"), Out.ArrivalEventIds) ||
		!StringField(Object, TEXT("barrier_release_event_id"), Out.ReleaseEventId) ||
		!StringField(Object, TEXT("criterion_event_id"), Out.CriterionEventId) ||
		!StringArray(Object, TEXT("completion_order"), Out.CompletionOrder) ||
		!BoolField(Object, TEXT("complete"), Out.bComplete) ||
		!StringField(Object, TEXT("close_reason"), Out.CloseReason))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Proofs = nullptr;
	if (!ArrayField(Object, TEXT("relations"), Relations) || !ArrayField(Object, TEXT("incomparability_checks"), Proofs))
	{
		return false;
	}
	TSet<FString> RelationIds;
	for (const TSharedPtr<FJsonValue>& Value : *Relations)
	{
		TSharedPtr<FJsonObject> RelationObject;
		FBlueprintLensLC4AsyncRelation Relation;
		if (!JsonObjectAt(Value, RelationObject) || !ParseRelation(RelationObject, Relation) || RelationIds.Contains(Relation.RelationId))
		{
			return false;
		}
		RelationIds.Add(Relation.RelationId);
		Out.Relations.Add(MoveTemp(Relation));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Proofs)
	{
		TSharedPtr<FJsonObject> ProofObject;
		FBlueprintLensLC4AsyncProof Proof;
		if (!JsonObjectAt(Value, ProofObject) || !ParseProof(ProofObject, Proof))
		{
			return false;
		}
		for (const FString& RelationId : Proof.EvidenceRelationIds)
		{
			if (!RelationIds.Contains(RelationId))
			{
				return false;
			}
		}
		Out.Proofs.Add(MoveTemp(Proof));
	}
	return (Out.Variant == TEXT("A_FIRST") || Out.Variant == TEXT("B_FIRST")) &&
		Out.bComplete && Out.CloseReason == TEXT("complete") &&
		Out.LaunchEventIds.Num() == 2 && Out.CompletionEventIds.Num() == 2 &&
		Out.ArrivalEventIds.Num() == 2 && Out.CompletionOrder.Num() == 2 &&
		Out.Relations.Num() == 11 && Out.Proofs.Num() == 1;
}

TSharedPtr<FBlueprintLensExplanationModel> BuildExplanation(const FBlueprintLensLC4AsyncProfile& Profile)
{
	TSharedRef<FBlueprintLensExplanationModel> Model = MakeShared<FBlueprintLensExplanationModel>();
	Model->Format = TEXT("blueprint-lens-explanation");
	Model->SchemaVersion = TEXT("0.2.0");
	Model->RulesVersion = Profile.RulesVersion;
	Model->Source.BlueprintAssetPath = Profile.Source.AssetPath;
	Model->Source.BlueprintPackageSha256 = Profile.Source.AssetSha256.ToUpper();
	Model->Source.GraphId = Profile.Source.GraphId;
	Model->Query.Question = TEXT("Why only after both complete, and can either finish first?");
	Model->Query.Direction = TEXT("async_partial_order");
	Model->Query.CriterionSourceNodeId = Profile.Source.CriterionNodeId;
	Model->CriterionUnitId = Profile.Source.CriterionNodeId;

	auto AddUnit = [&Model, &Profile](const FString& Id, const FString& Title, const EBlueprintLensRole Role)
	{
		if (Id.IsEmpty() || Model->FindUnit(Id) != nullptr)
		{
			return;
		}
		FBlueprintLensUnit Unit;
		Unit.Id = Id;
		Unit.Role = Role;
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Title = Title;
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons.Add(TEXT("validated_async_profile"));
		FBlueprintLensSourceReference Reference;
		Reference.BlueprintAssetPath = Profile.Source.AssetPath;
		Reference.GraphId = Profile.Source.GraphId;
		Reference.SourceNodeId = Id;
		const FString Marker = TEXT("::node::");
		const int32 MarkerIndex = Id.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (MarkerIndex != INDEX_NONE)
		{
			Reference.NativeNodeGuid = Id.Mid(MarkerIndex + Marker.Len()).Left(36);
		}
		Reference.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Reference));
		Model->Units.Add(MoveTemp(Unit));
	};
	AddUnit(Profile.Source.SequenceNodeId, TEXT("Sequence launch A then B"), EBlueprintLensRole::Control);
	for (const FBlueprintLensLC4AsyncLaunch& Launch : Profile.Launches)
	{
		AddUnit(Launch.NodeId, FString::Printf(TEXT("Launch %s"), *Launch.ParticipantId), EBlueprintLensRole::Control);
	}
	for (const FBlueprintLensLC4AsyncContinuation& Continuation : Profile.Continuations)
	{
		AddUnit(Continuation.NodeId, FString::Printf(TEXT("Delay %s continuation"), *Continuation.ParticipantId), EBlueprintLensRole::Consequence);
	}
	for (const FBlueprintLensLC4AsyncArrival& Arrival : Profile.Arrivals)
	{
		AddUnit(Arrival.NodeId, FString::Printf(TEXT("Barrier arrival %s"), *Arrival.ParticipantId), EBlueprintLensRole::Boundary);
	}
	AddUnit(Profile.Source.CriterionNodeId, Profile.Source.CriterionSourceAction, EBlueprintLensRole::Criterion);
	for (const EBlueprintLensRole Role : {EBlueprintLensRole::Criterion, EBlueprintLensRole::Control, EBlueprintLensRole::Predicate, EBlueprintLensRole::Value, EBlueprintLensRole::Consequence, EBlueprintLensRole::Boundary})
	{
		FBlueprintLensLane Lane;
		Lane.Role = Role;
		for (const FBlueprintLensUnit& Unit : Model->Units)
		{
			if (Unit.Role == Role)
			{
				Lane.UnitIds.Add(Unit.Id);
			}
		}
		Lane.State = Lane.UnitIds.IsEmpty() ? EBlueprintLensLaneState::Empty : EBlueprintLensLaneState::Populated;
		Lane.EmptyMessage = Lane.UnitIds.IsEmpty() ? TEXT("No facts in this async profile role") : FString();
		Model->Lanes.Add(MoveTemp(Lane));
	}
	Model->Counts.Lanes = 6;
	Model->Counts.Units = Model->Units.Num();
	Model->Counts.SourceNodes = Model->Units.Num();
	return Model;
}
} // namespace

bool FBlueprintLensLC4AsyncProfile::IsValid() const
{
	return Format == ExpectedFormat && SchemaVersion == ExpectedSchema &&
		ProfileId == ExpectedProfile && RulesVersion == ExpectedRules &&
		ValidationState == ExpectedValidation && !ProfilePath.IsEmpty() &&
		!ProfileSha256.IsEmpty() && Counts.InvocationCount == 4 &&
		Counts.RelationCount == 44 && Counts.IncomparabilityCheckCount == 4 &&
		Counts.ContinuationCount == 2 && Counts.ParticipantCount == 2 &&
		Counts.ScheduleVariantCount == 2 && Source.AssetPath.StartsWith(TEXT("/Game/")) &&
		!Source.GraphId.IsEmpty() && !Source.SequenceNodeId.IsEmpty() &&
		!Source.CriterionNodeId.IsEmpty() && Continuations.Num() == 2 &&
		Launches.Num() == 2 && Arrivals.Num() == 2 && Invocations.Num() == 4 &&
		Boundaries.Num() == 4 && ParticipantIds == TArray<FString>({TEXT("A"), TEXT("B")}) &&
		bBarrierSingleFire && ResetPolicy == TEXT("explicit_only") &&
		CancelPolicy == TEXT("closes_invocation");
}

FBlueprintLensLC4AsyncLoadResult FBlueprintLensLC4AsyncProfileLoader::LoadFile(const FString& ProfilePath)
{
	TSharedPtr<FJsonObject> Root;
	FString Error;
	if (!ParseJson(ProfilePath, Root, Error))
	{
		return Failure(TEXT("LC4_ASYNC_PROFILE_UNREADABLE"), Error);
	}
	TSharedRef<FBlueprintLensLC4AsyncProfile> Profile = MakeShared<FBlueprintLensLC4AsyncProfile>();
	Profile->ProfilePath = ProfilePath;
	if (!StringField(Root, TEXT("format"), Profile->Format) ||
		!StringField(Root, TEXT("schema_version"), Profile->SchemaVersion) ||
		!StringField(Root, TEXT("profile_id"), Profile->ProfileId) ||
		!StringField(Root, TEXT("rules_version"), Profile->RulesVersion) ||
		!StringField(Root, TEXT("validation_state"), Profile->ValidationState))
	{
		return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("identity fields"));
	}
	TSharedPtr<FJsonObject> Counts;
	TSharedPtr<FJsonObject> Source;
	TSharedPtr<FJsonObject> Barrier;
	if (!ObjectField(Root, TEXT("counts"), Counts) || !ObjectField(Root, TEXT("source"), Source) || !ObjectField(Root, TEXT("barrier"), Barrier) ||
		!IntField(Counts, TEXT("invocation_count"), Profile->Counts.InvocationCount) ||
		!IntField(Counts, TEXT("relation_count"), Profile->Counts.RelationCount) ||
		!IntField(Counts, TEXT("incomparability_check_count"), Profile->Counts.IncomparabilityCheckCount) ||
		!IntField(Counts, TEXT("continuation_count"), Profile->Counts.ContinuationCount) ||
		!IntField(Counts, TEXT("participant_count"), Profile->Counts.ParticipantCount) ||
		!IntField(Counts, TEXT("schedule_variant_count"), Profile->Counts.ScheduleVariantCount) ||
		!StringField(Source, TEXT("asset_path"), Profile->Source.AssetPath) ||
		!StringField(Source, TEXT("asset_sha256"), Profile->Source.AssetSha256) ||
		!StringField(Source, TEXT("graph_id"), Profile->Source.GraphId) ||
		!StringField(Source, TEXT("sequence_node_id"), Profile->Source.SequenceNodeId) ||
		!StringField(Source, TEXT("criterion_node_id"), Profile->Source.CriterionNodeId) ||
		!StringField(Source, TEXT("criterion_source_action"), Profile->Source.CriterionSourceAction) ||
		!StringField(Barrier, TEXT("barrier_site_id"), Profile->BarrierSiteId) ||
		!StringField(Barrier, TEXT("release_site_id"), Profile->ReleaseSiteId) ||
		!StringField(Barrier, TEXT("reset_policy"), Profile->ResetPolicy) ||
		!StringField(Barrier, TEXT("cancel_policy"), Profile->CancelPolicy) ||
		!BoolField(Barrier, TEXT("single_fire_guarantee"), Profile->bBarrierSingleFire) ||
		!StringArray(Barrier, TEXT("participant_ids"), Profile->ParticipantIds))
	{
		return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("contract fields"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Continuations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Launches = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Arrivals = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Invocations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Boundaries = nullptr;
	if (!ArrayField(Root, TEXT("continuations"), Continuations) || !ArrayField(Root, TEXT("launches"), Launches) ||
		!ArrayField(Barrier, TEXT("arrival_call_sites"), Arrivals) || !ArrayField(Root, TEXT("invocations"), Invocations) ||
		!ArrayField(Root, TEXT("boundaries"), Boundaries))
	{
		return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("contract arrays"));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Continuations)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC4AsyncContinuation Item;
		if (!JsonObjectAt(Value, Object) || !StringField(Object, TEXT("continuation_id"), Item.ParticipantId) ||
			!StringField(Object, TEXT("node_id"), Item.NodeId) || !StringField(Object, TEXT("resume_pin_id"), Item.ResumePinId))
		{
			return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("continuation"));
		}
		Profile->Continuations.Add(MoveTemp(Item));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Launches)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC4AsyncLaunch Item;
		if (!JsonObjectAt(Value, Object) || !IntField(Object, TEXT("ordinal"), Item.Ordinal) ||
			!StringField(Object, TEXT("participant_id"), Item.ParticipantId) ||
			!StringField(Object, TEXT("launch_node_id"), Item.NodeId) || !StringField(Object, TEXT("source_pin_id"), Item.SourcePinId))
		{
			return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("launch"));
		}
		Profile->Launches.Add(MoveTemp(Item));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Arrivals)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC4AsyncArrival Item;
		if (!JsonObjectAt(Value, Object) || !StringField(Object, TEXT("participant_id"), Item.ParticipantId) ||
			!StringField(Object, TEXT("arrival_node_id"), Item.NodeId) ||
			!StringField(Object, TEXT("arrival_execute_pin_id"), Item.ExecutePinId) ||
			!StringField(Object, TEXT("release_pin_id"), Item.ReleasePinId))
		{
			return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("arrival"));
		}
		Profile->Arrivals.Add(MoveTemp(Item));
	}
	TSet<FString> InvocationIds;
	TSet<FString> AllRelationIds;
	for (const TSharedPtr<FJsonValue>& Value : *Invocations)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC4AsyncInvocation Item;
		if (!JsonObjectAt(Value, Object) || !ParseInvocation(Object, Item) || InvocationIds.Contains(Item.InvocationId))
		{
			return Failure(TEXT("LC4_ASYNC_PROFILE_INVARIANT_FAILED"), TEXT("invocation"));
		}
		InvocationIds.Add(Item.InvocationId);
		for (const FBlueprintLensLC4AsyncRelation& Relation : Item.Relations)
		{
			if (AllRelationIds.Contains(Relation.RelationId))
			{
				return Failure(TEXT("LC4_ASYNC_PROFILE_INVARIANT_FAILED"), TEXT("duplicate relation identity"));
			}
			AllRelationIds.Add(Relation.RelationId);
		}
		Profile->Invocations.Add(MoveTemp(Item));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Boundaries)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC4AsyncBoundary Item;
		if (!JsonObjectAt(Value, Object) || !StringField(Object, TEXT("boundary_kind"), Item.Kind) ||
			!StringField(Object, TEXT("detail"), Item.Detail) || !StringField(Object, TEXT("support"), Item.Support))
		{
			return Failure(TEXT("LC4_ASYNC_PROFILE_MALFORMED"), TEXT("boundary"));
		}
		Profile->Boundaries.Add(MoveTemp(Item));
	}
	if (!HashFile(ProfilePath, Profile->ProfileSha256) || !Profile->IsValid() ||
		AllRelationIds.Num() != Profile->Counts.RelationCount)
	{
		return Failure(TEXT("LC4_ASYNC_PROFILE_INVARIANT_FAILED"));
	}
	FBlueprintLensLC4AsyncLoadResult Result;
	Result.ExplanationModel = BuildExplanation(*Profile);
	Result.Profile = Profile;
	return Result;
}
