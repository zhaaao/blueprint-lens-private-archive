#include "BlueprintLensLC5Profile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr TCHAR ExpectedFormat[] = TEXT("blueprint-lens-intra-bp-pure-call-resolution");
constexpr TCHAR ExpectedVersion[] = TEXT("1.0.0");
constexpr TCHAR ExpectedProfile[] = TEXT("LC5_INTRA_BP_PURE_CALL_V1");
constexpr TCHAR ExpectedStatus[] = TEXT("resolved_unique");

FBlueprintLensLC5LoadResult Failure(const TCHAR* Code, const FString& Detail = FString())
{
	FBlueprintLensLC5LoadResult Result;
	Result.Error = Detail.IsEmpty() ? FString(Code) :
		FString::Printf(TEXT("%s: %s"), Code, *Detail);
	return Result;
}

bool StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out, const bool bAllowEmpty = false)
{
	return Object.IsValid() && Object->TryGetStringField(Field, Out) &&
		(bAllowEmpty || !Out.IsEmpty());
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
	if (!Object.IsValid() || !Object->TryGetObjectField(Field, Value) ||
		Value == nullptr || !Value->IsValid())
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

bool JsonObjectAt(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	Out = Value->AsObject();
	return Out.IsValid();
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

FString BindingRelationId(const FBlueprintLensLC5Binding& Binding)
{
	return FString::Printf(TEXT("binding:%s:%d:%s"),
		*Binding.Kind, Binding.Ordinal, *Binding.PropertyPath);
}

FString BoundaryRelationId(const FBlueprintLensLC5ContextBoundary& Boundary)
{
	return FString::Printf(TEXT("context:%s:%s->%s"),
		*Boundary.Kind, *Boundary.SourceOccurrenceId, *Boundary.TargetOccurrenceId);
}

TSharedPtr<FBlueprintLensExplanationModel> BuildExplanation(
	const FBlueprintLensLC5Profile& Profile)
{
	TSharedRef<FBlueprintLensExplanationModel> Model = MakeShared<FBlueprintLensExplanationModel>();
	Model->Format = TEXT("blueprint-lens-explanation");
	Model->SchemaVersion = TEXT("0.2.0");
	Model->RulesVersion = TEXT("lc5_intra_bp_pure_call_v1");
	Model->Source.BlueprintAssetPath = TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe");
	Model->Source.BlueprintPackageSha256 = Profile.BlueprintAssetSha256.ToUpper();
	Model->Source.GraphId = Profile.SourceIdentity.CallGraphId;
	Model->Query.Question = TEXT("How does CalculateRecovery use CurrentHealth and Bonus to produce NewHealth?");
	Model->Query.Direction = TEXT("static_interprocedural_context");
	Model->Query.CriterionSourceNodeId = Profile.SourceIdentity.CallSiteNodeId;
	Model->CriterionUnitId = Profile.SourceIdentity.CallSiteNodeId;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Profile.Occurrences)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = Occurrence.SourceNodeId;
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Role = Occurrence.Role == TEXT("call_site")
			? EBlueprintLensRole::Criterion : EBlueprintLensRole::Value;
		Unit.Title = Occurrence.Role == TEXT("call_site")
			? TEXT("CalculateRecovery call") : TEXT("CalculateRecovery callee occurrence");
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons.Add(TEXT("validated_lc5_static_context"));
		FBlueprintLensSourceReference Reference;
		Reference.BlueprintAssetPath = Model->Source.BlueprintAssetPath;
		Reference.GraphId = Occurrence.Role == TEXT("call_site")
			? Profile.SourceIdentity.CallGraphId
			: TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery");
		Reference.SourceNodeId = Occurrence.SourceNodeId;
		const FString Marker = TEXT("::node::");
		const int32 MarkerIndex = Occurrence.SourceNodeId.Find(Marker);
		if (MarkerIndex != INDEX_NONE)
		{
			Reference.NativeNodeGuid = Occurrence.SourceNodeId.Mid(MarkerIndex + Marker.Len()).Left(36);
		}
		Reference.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Reference));
		Model->Units.Add(MoveTemp(Unit));
	}
	for (const EBlueprintLensRole Role : {
		EBlueprintLensRole::Criterion, EBlueprintLensRole::Control,
		EBlueprintLensRole::Predicate, EBlueprintLensRole::Value,
		EBlueprintLensRole::Consequence, EBlueprintLensRole::Boundary})
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
		Lane.State = Lane.UnitIds.IsEmpty()
			? EBlueprintLensLaneState::Empty : EBlueprintLensLaneState::Populated;
		Lane.EmptyMessage = Lane.UnitIds.IsEmpty()
			? TEXT("No facts in this LC5 static profile role") : FString();
		Model->Lanes.Add(MoveTemp(Lane));
	}
	Model->Counts.Lanes = 6;
	Model->Counts.Units = Model->Units.Num();
	Model->Counts.SourceNodes = Model->Units.Num();
	return Model;
}
} // namespace

bool FBlueprintLensLC5Profile::IsValid() const
{
	if (Format != ExpectedFormat || FormatVersion != ExpectedVersion ||
		ProfileId != ExpectedProfile || Status != ExpectedStatus || !Reason.IsEmpty() ||
		MaxCallDepth != 1 || ProfilePath.IsEmpty() || ProfileSha256.IsEmpty() ||
		BlueprintAssetSha256 != FrozenBlueprintAssetSha256 ||
		SourceIdentity.CallGraphId.IsEmpty() || SourceIdentity.CallSiteNodeId.IsEmpty() ||
		CallContext.Id.IsEmpty() || CallContext.ParentId.IsEmpty() ||
		CallContext.Id == CallContext.ParentId ||
		CallContext.ClaimScope != TEXT("static_contextual_occurrence_not_runtime_invocation") ||
		CallContext.CallSiteStack != TArray<FString>({SourceIdentity.CallSiteNodeId}) ||
		Occurrences.Num() != 4 || Bindings.Num() != 3 ||
		InternalRelations.Num() != 4 || ContextBoundaries.Num() != 2)
	{
		return false;
	}

	TSet<FString> OccurrenceIds;
	TSet<FString> SourceNodeIds;
	int32 CallSiteCount = 0;
	for (const FBlueprintLensLC5Occurrence& Occurrence : Occurrences)
	{
		if (Occurrence.OccurrenceId.IsEmpty() || Occurrence.SourceNodeId.IsEmpty() ||
			Occurrence.CallContextId.IsEmpty() || OccurrenceIds.Contains(Occurrence.OccurrenceId) ||
			SourceNodeIds.Contains(Occurrence.SourceNodeId) ||
			!Occurrence.OccurrenceId.Contains(Occurrence.CallContextId))
		{
			return false;
		}
		if (Occurrence.Role == TEXT("call_site"))
		{
			++CallSiteCount;
			if (Occurrence.SourceNodeId != SourceIdentity.CallSiteNodeId ||
				Occurrence.CallContextId != CallContext.ParentId)
			{
				return false;
			}
		}
		else if (Occurrence.Role != TEXT("callee") || Occurrence.CallContextId != CallContext.Id)
		{
			return false;
		}
		OccurrenceIds.Add(Occurrence.OccurrenceId);
		SourceNodeIds.Add(Occurrence.SourceNodeId);
	}
	if (CallSiteCount != 1)
	{
		return false;
	}

	TSet<int32> Ordinals;
	TSet<FString> RelationIds;
	TSet<FString> PropertyNames;
	for (const FBlueprintLensLC5Binding& Binding : Bindings)
	{
		const bool bArgument = Binding.Kind == TEXT("argument") &&
			Binding.RelationKind == TEXT("argument_bind") &&
			Binding.Direction == TEXT("input") && Binding.Ordinal < 2;
		const bool bResult = Binding.Kind == TEXT("result") &&
			Binding.RelationKind == TEXT("result_bind") &&
			Binding.Direction == TEXT("return") && Binding.Ordinal == 2;
		if ((!bArgument && !bResult) || Binding.CppType != TEXT("int32") ||
			Binding.Category != TEXT("int") || Binding.Container != TEXT("none") ||
			Binding.CallPinId.IsEmpty() || Binding.FormalPinId.IsEmpty() ||
			!OccurrenceIds.Contains(Binding.SourceOccurrenceId) ||
			!OccurrenceIds.Contains(Binding.TargetOccurrenceId) ||
			Ordinals.Contains(Binding.Ordinal) || PropertyNames.Contains(Binding.PropertyName) ||
			Binding.RelationId != BindingRelationId(Binding) || RelationIds.Contains(Binding.RelationId))
		{
			return false;
		}
		Ordinals.Add(Binding.Ordinal);
		PropertyNames.Add(Binding.PropertyName);
		RelationIds.Add(Binding.RelationId);
	}
	if (PropertyNames.Num() != 3 ||
		!PropertyNames.Contains(TEXT("CurrentHealth")) ||
		!PropertyNames.Contains(TEXT("Bonus")) ||
		!PropertyNames.Contains(TEXT("NewHealth")))
	{
		return false;
	}

	for (const FBlueprintLensLC5InternalRelation& Relation : InternalRelations)
	{
		if ((Relation.Kind != TEXT("data") && Relation.Kind != TEXT("execution")) ||
			Relation.SourceEdgeId.IsEmpty() ||
			!OccurrenceIds.Contains(Relation.SourceOccurrenceId) ||
			!OccurrenceIds.Contains(Relation.TargetOccurrenceId) ||
			Relation.RelationId != TEXT("internal:") + Relation.SourceEdgeId ||
			RelationIds.Contains(Relation.RelationId))
		{
			return false;
		}
		RelationIds.Add(Relation.RelationId);
	}

	TSet<FString> BoundaryKinds;
	for (const FBlueprintLensLC5ContextBoundary& Boundary : ContextBoundaries)
	{
		if ((Boundary.Kind != TEXT("call_enter") && Boundary.Kind != TEXT("call_return")) ||
			Boundary.ClaimScope != TEXT("static_context_boundary_not_runtime_event") ||
			!OccurrenceIds.Contains(Boundary.SourceOccurrenceId) ||
			!OccurrenceIds.Contains(Boundary.TargetOccurrenceId) ||
			BoundaryKinds.Contains(Boundary.Kind) ||
			Boundary.RelationId != BoundaryRelationId(Boundary) ||
			RelationIds.Contains(Boundary.RelationId))
		{
			return false;
		}
		BoundaryKinds.Add(Boundary.Kind);
		RelationIds.Add(Boundary.RelationId);
	}
	return BoundaryKinds.Num() == 2 && RelationIds.Num() == 9;
}

FBlueprintLensLC5LoadResult FBlueprintLensLC5ProfileLoader::LoadFile(const FString& ProfilePath)
{
	TSharedPtr<FJsonObject> Root;
	FString Error;
	if (!ParseJson(ProfilePath, Root, Error))
	{
		return Failure(TEXT("LC5_PROFILE_UNREADABLE"), Error);
	}
	TSharedRef<FBlueprintLensLC5Profile> Profile = MakeShared<FBlueprintLensLC5Profile>();
	Profile->ProfilePath = ProfilePath;
	if (!StringField(Root, TEXT("format"), Profile->Format) ||
		!StringField(Root, TEXT("format_version"), Profile->FormatVersion) ||
		!StringField(Root, TEXT("profile_id"), Profile->ProfileId) ||
		!StringField(Root, TEXT("status"), Profile->Status) ||
		!StringField(Root, TEXT("reason"), Profile->Reason, true) ||
		!IntField(Root, TEXT("max_call_depth"), Profile->MaxCallDepth))
	{
		return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("identity fields"));
	}

	TSharedPtr<FJsonObject> SourceIdentity;
	TSharedPtr<FJsonObject> CallContext;
	if (!ObjectField(Root, TEXT("source_identity"), SourceIdentity) ||
		!ObjectField(Root, TEXT("call_context"), CallContext) ||
		!StringField(SourceIdentity, TEXT("call_graph_id"), Profile->SourceIdentity.CallGraphId) ||
		!StringField(SourceIdentity, TEXT("call_site_node_id"), Profile->SourceIdentity.CallSiteNodeId) ||
		!StringField(CallContext, TEXT("id"), Profile->CallContext.Id) ||
		!StringField(CallContext, TEXT("parent_id"), Profile->CallContext.ParentId) ||
		!StringField(CallContext, TEXT("claim_scope"), Profile->CallContext.ClaimScope) ||
		!StringArray(CallContext, TEXT("call_site_stack"), Profile->CallContext.CallSiteStack))
	{
		return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("identity contract"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Occurrences = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* InternalRelations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ContextRelations = nullptr;
	if (!ArrayField(Root, TEXT("occurrences"), Occurrences) ||
		!ArrayField(Root, TEXT("bindings"), Bindings) ||
		!ArrayField(Root, TEXT("internal_relations"), InternalRelations) ||
		!ArrayField(Root, TEXT("context_relations"), ContextRelations))
	{
		return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("contract arrays"));
	}

	for (const TSharedPtr<FJsonValue>& Value : *Occurrences)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC5Occurrence Item;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("occurrence_id"), Item.OccurrenceId) ||
			!StringField(Object, TEXT("source_node_id"), Item.SourceNodeId) ||
			!StringField(Object, TEXT("call_context_id"), Item.CallContextId) ||
			!StringField(Object, TEXT("role"), Item.Role))
		{
			return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("occurrence"));
		}
		Profile->Occurrences.Add(MoveTemp(Item));
	}

	for (const TSharedPtr<FJsonValue>& Value : *Bindings)
	{
		TSharedPtr<FJsonObject> Object;
		TSharedPtr<FJsonObject> Property;
		TSharedPtr<FJsonObject> PinType;
		FBlueprintLensLC5Binding Item;
		if (!JsonObjectAt(Value, Object) || !ObjectField(Object, TEXT("property"), Property) ||
			!ObjectField(Property, TEXT("pin_type"), PinType) ||
			!IntField(Object, TEXT("ordinal"), Item.Ordinal) ||
			!StringField(Object, TEXT("kind"), Item.Kind) ||
			!StringField(Object, TEXT("relation_kind"), Item.RelationKind) ||
			!StringField(Object, TEXT("source_occurrence_id"), Item.SourceOccurrenceId) ||
			!StringField(Object, TEXT("target_occurrence_id"), Item.TargetOccurrenceId) ||
			!StringField(Object, TEXT("call_pin_id"), Item.CallPinId) ||
			!StringField(Object, TEXT("formal_pin_id"), Item.FormalPinId) ||
			!StringField(Property, TEXT("path"), Item.PropertyPath) ||
			!StringField(Property, TEXT("name"), Item.PropertyName) ||
			!StringField(Property, TEXT("cpp_type"), Item.CppType) ||
			!StringField(Property, TEXT("direction"), Item.Direction) ||
			!StringField(PinType, TEXT("category"), Item.Category) ||
			!StringField(PinType, TEXT("container"), Item.Container))
		{
			return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("binding"));
		}
		Item.RelationId = BindingRelationId(Item);
		Profile->Bindings.Add(MoveTemp(Item));
	}

	for (const TSharedPtr<FJsonValue>& Value : *InternalRelations)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC5InternalRelation Item;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("kind"), Item.Kind) ||
			!StringField(Object, TEXT("source_edge_id"), Item.SourceEdgeId) ||
			!StringField(Object, TEXT("source_occurrence_id"), Item.SourceOccurrenceId) ||
			!StringField(Object, TEXT("target_occurrence_id"), Item.TargetOccurrenceId))
		{
			return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("internal relation"));
		}
		Item.RelationId = TEXT("internal:") + Item.SourceEdgeId;
		Profile->InternalRelations.Add(MoveTemp(Item));
	}

	for (const TSharedPtr<FJsonValue>& Value : *ContextRelations)
	{
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLC5ContextBoundary Item;
		if (!JsonObjectAt(Value, Object) ||
			!StringField(Object, TEXT("kind"), Item.Kind) ||
			!StringField(Object, TEXT("claim_scope"), Item.ClaimScope) ||
			!StringField(Object, TEXT("source_occurrence_id"), Item.SourceOccurrenceId) ||
			!StringField(Object, TEXT("target_occurrence_id"), Item.TargetOccurrenceId))
		{
			return Failure(TEXT("LC5_PROFILE_MALFORMED"), TEXT("context relation"));
		}
		if (Item.Kind == TEXT("call_enter") || Item.Kind == TEXT("call_return"))
		{
			Item.RelationId = BoundaryRelationId(Item);
			Profile->ContextBoundaries.Add(MoveTemp(Item));
		}
	}

	if (!HashFile(ProfilePath, Profile->ProfileSha256) || !Profile->IsValid())
	{
		return Failure(TEXT("LC5_PROFILE_INVARIANT_FAILED"));
	}
	FBlueprintLensLC5LoadResult Result;
	Result.Profile = Profile;
	Result.ExplanationModel = BuildExplanation(*Profile);
	return Result;
}
