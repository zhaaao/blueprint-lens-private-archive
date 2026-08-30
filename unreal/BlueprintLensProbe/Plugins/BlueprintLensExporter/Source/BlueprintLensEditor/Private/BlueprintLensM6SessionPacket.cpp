// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6SessionPacket.h"

#include "Algo/Sort.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensM6SessionPacket
{
namespace
{
using FCanonicalWriter =
	TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

FM6SessionPacketLoadResult Fail(
	const TCHAR* Code,
	const FString& Message,
	const TCHAR* Phase = TEXT("packet_validation"))
{
	FM6Error Error;
	Error.Code = Code;
	Error.Phase = Phase;
	Error.Message = Message;
	Error.bRetryable = false;
	return MakeError(MoveTemp(Error));
}

bool IsLowerSha256(const FString& Value)
{
	if (Value.Len() != 64) return false;
	for (const TCHAR Character : Value)
	{
		if (!((Character >= TEXT('0') && Character <= TEXT('9')) ||
			(Character >= TEXT('a') && Character <= TEXT('f')))) return false;
	}
	return true;
}

bool Sha256(const TArray<uint8>& Bytes, FString& OutHash)
{
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32) return false;
	OutHash = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

bool WriteCanonicalValue(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedRef<FCanonicalWriter>& Writer);

bool WriteCanonicalObject(
	const TSharedPtr<FJsonObject>& Object,
	const TSharedRef<FCanonicalWriter>& Writer)
{
	if (!Object.IsValid()) return false;
	TArray<const TPair<FJsonObject::FStringType, TSharedPtr<FJsonValue>>*> Fields;
	for (const auto& Field : Object->Values) Fields.Add(&Field);
	Algo::Sort(Fields, [](const auto* Left, const auto* Right)
	{
		return FStringView(*Left->Key, Left->Key.Len()).Compare(
			FStringView(*Right->Key, Right->Key.Len()),
			ESearchCase::CaseSensitive) < 0;
	});
	Writer->WriteObjectStart();
	for (const auto* Field : Fields)
	{
		if (Field == nullptr || !Field->Value.IsValid()) return false;
		Writer->WriteIdentifierPrefix(
			FStringView(*Field->Key, Field->Key.Len()));
		if (!WriteCanonicalValue(Field->Value, Writer)) return false;
	}
	Writer->WriteObjectEnd();
	return true;
}

bool WriteCanonicalValue(
	const TSharedPtr<FJsonValue>& Value,
	const TSharedRef<FCanonicalWriter>& Writer)
{
	if (!Value.IsValid()) return false;
	switch (Value->Type)
	{
	case EJson::Null: Writer->WriteNull(); return true;
	case EJson::String: Writer->WriteValue(Value->AsString()); return true;
	case EJson::Number: Writer->WriteValue(Value->AsNumber()); return true;
	case EJson::Boolean: Writer->WriteValue(Value->AsBool()); return true;
	case EJson::Array:
		Writer->WriteArrayStart();
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			if (!WriteCanonicalValue(Item, Writer)) return false;
		}
		Writer->WriteArrayEnd();
		return true;
	case EJson::Object: return WriteCanonicalObject(Value->AsObject(), Writer);
	default: return false;
	}
}

bool CanonicalBytes(
	const TSharedPtr<FJsonValue>& Value,
	TArray<uint8>& OutBytes)
{
	FString Text;
	const TSharedRef<FCanonicalWriter> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	if (!WriteCanonicalValue(Value, Writer) || !Writer->Close()) return false;
	Text += TEXT("\n");
	FTCHARToUTF8 Utf8(*Text);
	OutBytes.Reset();
	OutBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}

bool LoadCanonicalObject(
	const FString& Path,
	TSharedPtr<FJsonObject>& OutObject,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
	{
		OutError = FString::Printf(TEXT("failed to read '%s'"), *Path);
		return false;
	}
	if (OutBytes.Num() < 2 ||
		(OutBytes.Num() >= 3 && OutBytes[0] == 0xef &&
			OutBytes[1] == 0xbb && OutBytes[2] == 0xbf) ||
		OutBytes.Last() != static_cast<uint8>('\n') ||
		(OutBytes.Num() >= 2 && OutBytes[OutBytes.Num() - 2] == static_cast<uint8>('\n')) ||
		OutBytes.Contains(static_cast<uint8>('\r')))
	{
		OutError = FString::Printf(TEXT("'%s' is not canonical UTF-8/LF JSON"), *Path);
		return false;
	}
	FUTF8ToTCHAR Converted(
		reinterpret_cast<const ANSICHAR*>(OutBytes.GetData()), OutBytes.Num());
	const FString Text(Converted.Length(), Converted.Get());
	if (!FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(Text), OutObject) || !OutObject.IsValid())
	{
		OutError = FString::Printf(TEXT("failed to parse '%s'"), *Path);
		return false;
	}
	TArray<uint8> Expected;
	if (!CanonicalBytes(MakeShared<FJsonValueObject>(OutObject), Expected) ||
		Expected != OutBytes)
	{
		OutError = FString::Printf(TEXT("'%s' is not recursively canonical"), *Path);
		return false;
	}
	return true;
}

bool HasExactFields(
	const FJsonObject& Object,
	std::initializer_list<const TCHAR*> Expected)
{
	if (Object.Values.Num() != static_cast<int32>(Expected.size())) return false;
	for (const TCHAR* Field : Expected)
	{
		if (!Object.Values.Contains(Field)) return false;
	}
	return true;
}

bool ReadStringArray(
	const FJsonObject& Object,
	const TCHAR* Name,
	TArray<FString>& OutValues)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.TryGetArrayField(Name, Values) || Values == nullptr) return false;
	OutValues.Reset();
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Text;
		if (!Value.IsValid() || !Value->TryGetString(Text) || Text.IsEmpty() ||
			Seen.Contains(Text)) return false;
		Seen.Add(Text);
		OutValues.Add(Text);
	}
	return true;
}

bool ReadInteger(
	const FJsonObject& Object,
	const TCHAR* Name,
	int32& OutValue,
	const int32 Minimum)
{
	double Number = 0.0;
	if (!Object.TryGetNumberField(Name, Number) ||
		Number < Minimum || Number > MAX_int32 ||
		Number != FMath::FloorToDouble(Number)) return false;
	OutValue = static_cast<int32>(Number);
	return true;
}

bool ParseRequest(const FJsonObject& Root, FM6Request& Out)
{
	const TSharedPtr<FJsonObject>* Criterion = nullptr;
	const TSharedPtr<FJsonObject>* SemanticBudget = nullptr;
	const TSharedPtr<FJsonObject>* PresentationBudget = nullptr;
	if (!HasExactFields(Root, {
		TEXT("asset_path"), TEXT("criterion"), TEXT("graph_id"),
		TEXT("presentation_budget"), TEXT("query_kind"), TEXT("raw_version"),
		TEXT("renderer_id"), TEXT("schema_name"), TEXT("schema_version"),
		TEXT("semantic_budget"), TEXT("slice_rules_version"),
		TEXT("source_fingerprint"), TEXT("typed_ir_version")}) ||
		!Root.TryGetStringField(TEXT("schema_name"), Out.SchemaName) ||
		!Root.TryGetStringField(TEXT("schema_version"), Out.SchemaVersion) ||
		!Root.TryGetStringField(TEXT("asset_path"), Out.AssetPath) ||
		!Root.TryGetStringField(TEXT("graph_id"), Out.GraphId) ||
		!Root.TryGetStringField(TEXT("source_fingerprint"), Out.SourceFingerprint) ||
		!Root.TryGetStringField(TEXT("query_kind"), Out.QueryKind) ||
		!Root.TryGetStringField(TEXT("raw_version"), Out.RawVersion) ||
		!Root.TryGetStringField(TEXT("typed_ir_version"), Out.TypedIrVersion) ||
		!Root.TryGetStringField(TEXT("slice_rules_version"), Out.SliceRulesVersion) ||
		!Root.TryGetStringField(TEXT("renderer_id"), Out.RendererId) ||
		!Root.TryGetObjectField(TEXT("criterion"), Criterion) || Criterion == nullptr ||
		!Root.TryGetObjectField(TEXT("semantic_budget"), SemanticBudget) || SemanticBudget == nullptr ||
		!Root.TryGetObjectField(TEXT("presentation_budget"), PresentationBudget) || PresentationBudget == nullptr ||
		!HasExactFields(**SemanticBudget, {TEXT("max_selected_nodes"), TEXT("max_selected_relations")}) ||
		!HasExactFields(**PresentationBudget, {TEXT("max_visible_entities"), TEXT("max_visible_relations")}) ||
		!(*Criterion)->TryGetStringField(TEXT("direction"), Out.Direction)) return false;
	FString CriterionGraphId;
	if (!(*Criterion)->TryGetStringField(TEXT("graph_id"), CriterionGraphId) ||
		CriterionGraphId != Out.GraphId ||
		!ReadInteger(**SemanticBudget, TEXT("max_selected_nodes"), Out.MaxSelectedNodes, 1) ||
		!ReadInteger(**SemanticBudget, TEXT("max_selected_relations"), Out.MaxSelectedRelations, 1) ||
		!ReadInteger(**PresentationBudget, TEXT("max_visible_entities"), Out.MaxVisibleEntities, 1) ||
		!ReadInteger(**PresentationBudget, TEXT("max_visible_relations"), Out.MaxVisibleRelations, 1)) return false;
	FString Kind;
	if (!(*Criterion)->TryGetStringField(TEXT("kind"), Kind) || Kind != Out.QueryKind)
		return false;
	if (Out.QueryKind == TEXT("execution"))
		return HasExactFields(**Criterion, {
			TEXT("criterion_node_id"), TEXT("direction"), TEXT("graph_id"), TEXT("kind")}) &&
			(*Criterion)->TryGetStringField(TEXT("criterion_node_id"), Out.CriterionNodeId) &&
			!Out.CriterionNodeId.IsEmpty();
	if (Out.QueryKind == TEXT("data"))
		return HasExactFields(**Criterion, {
			TEXT("direction"), TEXT("expected_member_name"), TEXT("graph_id"),
			TEXT("kind"), TEXT("member_guid")}) &&
			(*Criterion)->TryGetStringField(TEXT("member_guid"), Out.MemberGuid) &&
			(*Criterion)->TryGetStringField(TEXT("expected_member_name"), Out.ExpectedMemberName) &&
			!Out.MemberGuid.IsEmpty() && !Out.ExpectedMemberName.IsEmpty();
	return false;
}

bool ParseDocument(
	const FJsonObject& Root,
	const FString& GraphId,
	FM6TypedDocument& Out,
	const bool bRaw)
{
	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!Root.TryGetStringField(TEXT("format"), Out.Format) ||
		!(bRaw ? Root.TryGetStringField(TEXT("format_version"), Out.SchemaVersion)
		       : Root.TryGetStringField(TEXT("schema_version"), Out.SchemaVersion)) ||
		!Root.TryGetObjectField(TEXT("blueprint"), Blueprint) || Blueprint == nullptr ||
		!(*Blueprint)->TryGetStringField(TEXT("path"), Out.BlueprintPath) ||
		!(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs) || Graphs == nullptr)
		return false;
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
		FString Id;
		if (!Graph.IsValid() || !Graph->TryGetStringField(TEXT("id"), Id)) return false;
		if (Id != GraphId) continue;
		Out.GraphId = Id;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
		if (!Graph->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr ||
			!Graph->TryGetArrayField(TEXT("edges"), Edges) || Edges == nullptr) return false;
		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			FString NodeId;
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (!Node.IsValid() || !Node->TryGetStringField(TEXT("id"), NodeId) ||
				Out.NodeIds.Contains(NodeId)) return false;
			Out.NodeIds.Add(NodeId);
		}
		for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
		{
			FString EdgeId;
			const TSharedPtr<FJsonObject> Edge = EdgeValue->AsObject();
			if (!Edge.IsValid() || !Edge->TryGetStringField(TEXT("id"), EdgeId) ||
				Out.EdgeIds.Contains(EdgeId)) return false;
			Out.EdgeIds.Add(EdgeId);
		}
		return true;
	}
	return false;
}

bool ParseSlice(const FJsonObject& Root, FM6SliceProduct& Out)
{
	return Root.TryGetStringField(TEXT("format"), Out.Format) &&
		Root.TryGetStringField(TEXT("schema_version"), Out.SchemaVersion) &&
		Root.TryGetStringField(TEXT("rules_version"), Out.RulesVersion) &&
		Root.TryGetStringField(TEXT("slice_kind"), Out.SliceKind) &&
		Root.TryGetStringField(TEXT("graph_id"), Out.GraphId) &&
		Root.TryGetStringField(TEXT("source_sha256"), Out.SourceSha256) &&
		ReadStringArray(Root, TEXT("node_ids"), Out.NodeIds) &&
		ReadStringArray(Root, TEXT("edge_ids"), Out.EdgeIds);
}

bool ParseBaseline(const FJsonObject& Root, FM6BaselineFacts& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Boundaries = nullptr;
	if (!Root.TryGetStringField(TEXT("format"), Out.Format) ||
		!Root.TryGetStringField(TEXT("schema_version"), Out.SchemaVersion) ||
		!Root.TryGetStringField(TEXT("graph_id"), Out.GraphId) ||
		!Root.TryGetStringField(TEXT("renderer_id"), Out.RendererId) ||
		!Root.TryGetStringField(TEXT("criterion_entity_id"), Out.CriterionEntityId) ||
		!Root.TryGetArrayField(TEXT("entities"), Entities) || Entities == nullptr ||
		!Root.TryGetArrayField(TEXT("relations"), Relations) || Relations == nullptr ||
		!Root.TryGetArrayField(TEXT("boundaries"), Boundaries) || Boundaries == nullptr)
		return false;
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& EntityValue : *Entities)
	{
		const TSharedPtr<FJsonObject> Entity = EntityValue->AsObject();
		const TSharedPtr<FJsonObject>* Source = nullptr;
		const TSharedPtr<FJsonObject>* Analysis = nullptr;
		FM6BaselineEntity Parsed;
		if (!Entity.IsValid() || !HasExactFields(*Entity, {
				TEXT("analysis"), TEXT("id"), TEXT("inclusion_reasons"),
				TEXT("label"), TEXT("presentation_reason"),
				TEXT("presentation_status"), TEXT("role"),
				TEXT("semantic_reason"), TEXT("semantic_status"), TEXT("source")}) ||
			!Entity->TryGetStringField(TEXT("id"), Parsed.Id) ||
			Seen.Contains(Parsed.Id) ||
			!Entity->TryGetStringField(TEXT("label"), Parsed.Label) ||
			!Entity->TryGetStringField(TEXT("role"), Parsed.Role) ||
			!Entity->TryGetStringField(TEXT("semantic_status"), Parsed.SemanticStatus) ||
			!Entity->TryGetStringField(TEXT("semantic_reason"), Parsed.SemanticReason) ||
			!Entity->TryGetStringField(TEXT("presentation_status"), Parsed.PresentationStatus) ||
			!Entity->TryGetStringField(TEXT("presentation_reason"), Parsed.PresentationReason) ||
			!ReadStringArray(*Entity, TEXT("inclusion_reasons"), Parsed.InclusionReasons) ||
			!Entity->TryGetObjectField(TEXT("analysis"), Analysis) || Analysis == nullptr ||
			!HasExactFields(**Analysis, {TEXT("class_path"), TEXT("position_x"), TEXT("position_y"), TEXT("symbol")}) ||
			!(*Analysis)->TryGetStringField(TEXT("class_path"), Parsed.ClassPath) ||
			!ReadInteger(**Analysis, TEXT("position_x"), Parsed.PositionX, MIN_int32) ||
			!ReadInteger(**Analysis, TEXT("position_y"), Parsed.PositionY, MIN_int32) ||
			!Entity->TryGetObjectField(TEXT("source"), Source) || Source == nullptr ||
			!HasExactFields(**Source, {TEXT("asset_path"), TEXT("graph_id"), TEXT("native_node_guid"), TEXT("node_id"), TEXT("pin_ids")}) ||
			!(*Source)->TryGetStringField(TEXT("asset_path"), Parsed.Source.AssetPath) ||
			!(*Source)->TryGetStringField(TEXT("graph_id"), Parsed.Source.GraphId) ||
			!(*Source)->TryGetStringField(TEXT("node_id"), Parsed.Source.NodeId) ||
			!(*Source)->TryGetStringField(TEXT("native_node_guid"), Parsed.Source.NativeNodeGuid) ||
			!ReadStringArray(**Source, TEXT("pin_ids"), Parsed.Source.PinIds))
			return false;
		Seen.Add(Parsed.Id);
		Out.EntityIds.Add(Parsed.Id);
		Out.EntitySourceNodeIds.Add(Parsed.Id, Parsed.Source.NodeId);
		Out.Entities.Add(MoveTemp(Parsed));
	}
	Seen.Reset();
	for (const TSharedPtr<FJsonValue>& RelationValue : *Relations)
	{
		const TSharedPtr<FJsonObject> Relation = RelationValue->AsObject();
		const TSharedPtr<FJsonObject>* Source = nullptr;
		FM6BaselineRelation Parsed;
		if (!Relation.IsValid() || !HasExactFields(*Relation, {
				TEXT("id"), TEXT("kind"), TEXT("label"), TEXT("semantic_label"),
				TEXT("semantic_reason"), TEXT("semantic_status"), TEXT("source"),
				TEXT("source_entity_id"), TEXT("target_entity_id")}) ||
			!Relation->TryGetStringField(TEXT("id"), Parsed.Id) ||
			Seen.Contains(Parsed.Id) ||
			!Relation->TryGetStringField(TEXT("label"), Parsed.Label) ||
			!Relation->TryGetStringField(TEXT("kind"), Parsed.Kind) ||
			!Relation->TryGetStringField(TEXT("semantic_label"), Parsed.SemanticLabel) ||
			!Relation->TryGetStringField(TEXT("semantic_status"), Parsed.SemanticStatus) ||
			!Relation->TryGetStringField(TEXT("semantic_reason"), Parsed.SemanticReason) ||
			!Relation->TryGetStringField(TEXT("source_entity_id"), Parsed.SourceEntityId) ||
			!Relation->TryGetStringField(TEXT("target_entity_id"), Parsed.TargetEntityId) ||
			!Relation->TryGetObjectField(TEXT("source"), Source) || Source == nullptr ||
			!(*Source)->TryGetStringField(TEXT("edge_id"), Parsed.SourceEdgeId)) return false;
		Seen.Add(Parsed.Id);
		Out.RelationIds.Add(Parsed.Id);
		Out.Relations.Add(MoveTemp(Parsed));
	}
	Seen.Reset();
	for (const TSharedPtr<FJsonValue>& BoundaryValue : *Boundaries)
	{
		const TSharedPtr<FJsonObject> Boundary = BoundaryValue->AsObject();
		FM6BaselineBoundary Parsed;
		if (!Boundary.IsValid() || !HasExactFields(*Boundary, {
				TEXT("node_id"), TEXT("reason"), TEXT("status")}) ||
			!Boundary->TryGetStringField(TEXT("node_id"), Parsed.NodeId) ||
			Seen.Contains(Parsed.NodeId) ||
			!Boundary->TryGetStringField(TEXT("status"), Parsed.Status) ||
			!Boundary->TryGetStringField(TEXT("reason"), Parsed.Reason)) return false;
		Seen.Add(Parsed.NodeId);
		Out.Boundaries.Add(MoveTemp(Parsed));
	}
	return true;
}

bool SameSet(const TArray<FString>& Values, const TSet<FString>& Expected)
{
	if (Values.Num() != Expected.Num()) return false;
	for (const FString& Value : Values)
	{
		if (!Expected.Contains(Value)) return false;
	}
	return true;
}

TSet<FString> ToSet(const TArray<FString>& Values)
{
	TSet<FString> Result;
	for (const FString& Value : Values) Result.Add(Value);
	return Result;
}

bool IsSubset(const TArray<FString>& Values, const TSet<FString>& Superset)
{
	for (const FString& Value : Values)
	{
		if (!Superset.Contains(Value)) return false;
	}
	return true;
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
} // namespace
} // namespace BlueprintLensM6SessionPacket

FM6SessionPacketLoadResult FM6SessionPacketLoader::Load(
	const FString& PacketDirectory,
	const FString& ExpectedSourceFingerprint)
{
	using namespace BlueprintLensM6SessionPacket;
	static const TCHAR* ExpectedNames[] = {
		TEXT("request.json"), TEXT("raw-source.json"), TEXT("typed-source.json"),
		TEXT("slice.json"), TEXT("explanation.json"),
		TEXT("baseline-facts.json"), TEXT("manifest.json")};
	static const TCHAR* ExpectedRoles[] = {
		TEXT("request"), TEXT("raw_source"), TEXT("typed_source"),
		TEXT("slice"), TEXT("explanation"), TEXT("baseline_facts")};

	TArray<FString> Files;
	IFileManager::Get().FindFiles(
		Files, *FPaths::Combine(PacketDirectory, TEXT("*")), true, false);
	TArray<FString> Directories;
	IFileManager::Get().FindFiles(
		Directories, *FPaths::Combine(PacketDirectory, TEXT("*")), false, true);
	TSet<FString> Actual;
	for (const FString& File : Files) Actual.Add(File);
	if (Directories.Num() != 0 || Files.Num() != UE_ARRAY_COUNT(ExpectedNames))
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("packet must contain exactly seven files and no directories"));
	for (const TCHAR* Name : ExpectedNames)
	{
		if (!Actual.Contains(Name))
			return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), FString::Printf(TEXT("packet is missing '%s'"), Name));
	}

	TSharedPtr<FJsonObject> Manifest;
	TArray<uint8> ManifestBytes;
	FString CanonicalError;
	if (!LoadCanonicalObject(
		FPaths::Combine(PacketDirectory, TEXT("manifest.json")),
		Manifest, ManifestBytes, CanonicalError))
		return Fail(TEXT("M6_PACKET_CANONICAL_INVALID"), CanonicalError);
	if (!HasExactFields(*Manifest, {
		TEXT("counts"), TEXT("files"), TEXT("format"), TEXT("generator_version"),
		TEXT("query_kind"), TEXT("renderer_id"), TEXT("schema_version"),
		TEXT("semantic_sha256"), TEXT("source_fingerprint"), TEXT("versions")}))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("manifest fields are not exact"));
	FString Format;
	FString SchemaVersion;
	FString GeneratorVersion;
	FString QueryKind;
	FString RendererId;
	FString SourceFingerprint;
	FString SemanticSha256;
	const TSharedPtr<FJsonObject>* Versions = nullptr;
	const TSharedPtr<FJsonObject>* ManifestCounts = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
	if (!Manifest->TryGetStringField(TEXT("format"), Format) ||
		!Manifest->TryGetStringField(TEXT("schema_version"), SchemaVersion) ||
		!Manifest->TryGetStringField(TEXT("generator_version"), GeneratorVersion) ||
		!Manifest->TryGetStringField(TEXT("query_kind"), QueryKind) ||
		!Manifest->TryGetStringField(TEXT("renderer_id"), RendererId) ||
		!Manifest->TryGetStringField(TEXT("source_fingerprint"), SourceFingerprint) ||
		!Manifest->TryGetStringField(TEXT("semantic_sha256"), SemanticSha256) ||
		!Manifest->TryGetObjectField(TEXT("versions"), Versions) || Versions == nullptr ||
		!Manifest->TryGetObjectField(TEXT("counts"), ManifestCounts) || ManifestCounts == nullptr ||
		!Manifest->TryGetArrayField(TEXT("files"), Records) || Records == nullptr)
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("manifest fields have invalid types"));
	FString RawVersion;
	FString TypedVersion;
	FString SliceVersion;
	FString ExplanationVersion;
	FString BaselineVersion;
	int32 BoundFiles = 0;
	int32 PacketFiles = 0;
	int32 SelectedEntities = 0;
	int32 SelectedRelations = 0;
	if (!HasExactFields(**Versions, {
		TEXT("baseline_facts"), TEXT("explanation"), TEXT("raw"),
		TEXT("slice_rules"), TEXT("typed_ir")}) ||
		!HasExactFields(**ManifestCounts, {
		TEXT("bound_files"), TEXT("packet_files"),
		TEXT("selected_entities"), TEXT("selected_relations")}) ||
		!ReadInteger(**ManifestCounts, TEXT("bound_files"), BoundFiles, 6) || BoundFiles != 6 ||
		!ReadInteger(**ManifestCounts, TEXT("packet_files"), PacketFiles, 7) || PacketFiles != 7 ||
		!ReadInteger(**ManifestCounts, TEXT("selected_entities"), SelectedEntities, 1) ||
		!ReadInteger(**ManifestCounts, TEXT("selected_relations"), SelectedRelations, 0))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("manifest versions or counts are invalid"));
	if (Format != TEXT("blueprint-lens-m6-session-manifest") ||
		SchemaVersion != TEXT("1.0.0") ||
		GeneratorVersion != TEXT("m6-session-pipeline-v1") ||
		RendererId != TEXT("R1_GENERIC_FRAME_FLOW_V1") ||
		(QueryKind != TEXT("execution") && QueryKind != TEXT("data")) ||
		!(*Versions)->TryGetStringField(TEXT("raw"), RawVersion) || RawVersion != TEXT("0.2") ||
		!(*Versions)->TryGetStringField(TEXT("typed_ir"), TypedVersion) || TypedVersion != TEXT("1.0.0") ||
		!(*Versions)->TryGetStringField(TEXT("slice_rules"), SliceVersion) || SliceVersion != TEXT("1.0.0") ||
		!(*Versions)->TryGetStringField(TEXT("explanation"), ExplanationVersion) || ExplanationVersion != TEXT("1.0.0") ||
		!(*Versions)->TryGetStringField(TEXT("baseline_facts"), BaselineVersion) || BaselineVersion != TEXT("1.0.0"))
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("manifest uses an unsupported format, version, query or renderer"));
	if (!IsLowerSha256(SourceFingerprint) || !IsLowerSha256(SemanticSha256))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("manifest hashes must be lowercase SHA-256"));
	if (SourceFingerprint != ExpectedSourceFingerprint.ToLower())
		return Fail(TEXT("M6_PACKET_SOURCE_STALE"), TEXT("packet source fingerprint is stale"));
	if (Records->Num() != 6)
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("manifest must bind exactly six products"));

	TArray<TArray<uint8>> ProductBytes;
	TArray<TSharedPtr<FJsonObject>> ProductRoots;
	for (int32 Index = 0; Index < Records->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Record = (*Records)[Index]->AsObject();
		FString Role;
		FString Path;
		FString DeclaredHash;
		if (!Record.IsValid() || !HasExactFields(*Record, {TEXT("path"), TEXT("role"), TEXT("sha256")}) ||
			!Record->TryGetStringField(TEXT("role"), Role) || Role != ExpectedRoles[Index] ||
			!Record->TryGetStringField(TEXT("path"), Path) || Path != ExpectedNames[Index] ||
			!Record->TryGetStringField(TEXT("sha256"), DeclaredHash) || !IsLowerSha256(DeclaredHash))
			return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("manifest record order or identity is invalid"));
		TSharedPtr<FJsonObject> Root;
		TArray<uint8> Bytes;
		if (!LoadCanonicalObject(FPaths::Combine(PacketDirectory, Path), Root, Bytes, CanonicalError))
			return Fail(TEXT("M6_PACKET_CANONICAL_INVALID"), CanonicalError);
		FString ActualHash;
		if (!Sha256(Bytes, ActualHash) || ActualHash != DeclaredHash)
			return Fail(TEXT("M6_PACKET_HASH_MISMATCH"), FString::Printf(TEXT("hash mismatch for '%s'"), *Path));
		ProductRoots.Add(Root);
		ProductBytes.Add(MoveTemp(Bytes));
	}
	TArray<uint8> CanonicalRecords;
	if (!CanonicalBytes(MakeShared<FJsonValueArray>(*Records), CanonicalRecords))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("manifest records cannot be canonicalized"));
	FString ActualSemanticSha;
	if (!Sha256(CanonicalRecords, ActualSemanticSha) || ActualSemanticSha != SemanticSha256)
		return Fail(TEXT("M6_PACKET_HASH_MISMATCH"), TEXT("semantic aggregate hash mismatch"));

	FM6LoadedSessionPacket Loaded;
	Loaded.SemanticSha256 = SemanticSha256;
	if (!ParseRequest(*ProductRoots[0], Loaded.Request))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("request contract is invalid"));
	if (Loaded.Request.SchemaName != TEXT("blueprint-lens-m6-request") ||
		Loaded.Request.SchemaVersion != TEXT("1.0.0") ||
		Loaded.Request.RawVersion != TEXT("0.2") ||
		Loaded.Request.TypedIrVersion != TEXT("1.0.0") ||
		Loaded.Request.SliceRulesVersion != TEXT("1.0.0") ||
		Loaded.Request.RendererId != TEXT("R1_GENERIC_FRAME_FLOW_V1"))
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("request uses an unsupported version or renderer"));
	if (
		Loaded.Request.QueryKind != QueryKind ||
		Loaded.Request.SourceFingerprint != SourceFingerprint ||
		Loaded.Request.RendererId != RendererId ||
		Loaded.Request.RawVersion != RawVersion ||
		Loaded.Request.TypedIrVersion != TypedVersion ||
		Loaded.Request.SliceRulesVersion != SliceVersion ||
		Loaded.Request.Direction != TEXT("backward") ||
		Loaded.Request.MaxSelectedNodes <= 0 || Loaded.Request.MaxSelectedRelations <= 0 ||
		Loaded.Request.MaxVisibleEntities <= 0 || Loaded.Request.MaxVisibleRelations <= 0)
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("request contract is invalid"));

	FM6TypedDocument RawDocument;
	if (!ParseDocument(*ProductRoots[1], Loaded.Request.GraphId, RawDocument, true) ||
		!ParseDocument(*ProductRoots[2], Loaded.Request.GraphId, Loaded.TypedDocument, false))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("raw or typed document structure is invalid"));
	if (RawDocument.Format != TEXT("blueprint-lens-raw-probe") || RawDocument.SchemaVersion != RawVersion ||
		Loaded.TypedDocument.Format != TEXT("blueprint-lens") || Loaded.TypedDocument.SchemaVersion != TypedVersion)
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("raw or typed document version is unsupported"));
	if (
		RawDocument.BlueprintPath != Loaded.Request.AssetPath ||
		Loaded.TypedDocument.BlueprintPath != Loaded.Request.AssetPath ||
		!SameSet(RawDocument.NodeIds, Loaded.TypedDocument.NodeIds) ||
		!SameSet(RawDocument.EdgeIds, Loaded.TypedDocument.EdgeIds))
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("raw and typed document identities disagree"));
	if (!ParseSlice(*ProductRoots[3], Loaded.Slice))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("slice structure is invalid"));
	if (Loaded.Slice.Format != TEXT("blueprint-lens-slice") ||
		Loaded.Slice.SchemaVersion != TEXT("1.0.0") ||
		Loaded.Slice.RulesVersion != SliceVersion)
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("slice version is unsupported"));
	const TSharedPtr<FJsonObject>* SliceCounts = nullptr;
	int32 SliceNodes = 0;
	int32 SliceEdges = 0;
	FString TypedHash;
	const TSharedPtr<FJsonObject>* ExplanationSource = nullptr;
	FString ExplanationIrSha;
	if (!ProductRoots[3]->TryGetObjectField(TEXT("counts"), SliceCounts) || SliceCounts == nullptr ||
		!HasExactFields(**SliceCounts, {TEXT("edges"), TEXT("nodes")}) ||
		!ReadInteger(**SliceCounts, TEXT("nodes"), SliceNodes, 1) ||
		!ReadInteger(**SliceCounts, TEXT("edges"), SliceEdges, 0) ||
		SliceNodes != Loaded.Slice.NodeIds.Num() || SliceEdges != Loaded.Slice.EdgeIds.Num() ||
		SelectedEntities != SliceNodes || SelectedRelations != SliceEdges ||
		!Sha256(ProductBytes[2], TypedHash) ||
		!ProductRoots[4]->TryGetObjectField(TEXT("source"), ExplanationSource) || ExplanationSource == nullptr ||
		!(*ExplanationSource)->TryGetStringField(TEXT("ir_sha256"), ExplanationIrSha))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("slice or explanation source metadata is invalid"));
	if (
		Loaded.Slice.GraphId != Loaded.Request.GraphId ||
		Loaded.Slice.SourceSha256 != TypedHash.ToUpper() ||
		Loaded.Slice.SourceSha256 != ExplanationIrSha ||
		!IsSubset(Loaded.Slice.NodeIds, Loaded.TypedDocument.NodeIds) ||
		!IsSubset(Loaded.Slice.EdgeIds, Loaded.TypedDocument.EdgeIds))
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("slice references are invalid"));
	FString ExplanationFormat;
	FString ExplanationSchemaVersion;
	FString ExplanationRulesVersion;
	if (!ProductRoots[4]->TryGetStringField(TEXT("format"), ExplanationFormat) ||
		!ProductRoots[4]->TryGetStringField(TEXT("schema_version"), ExplanationSchemaVersion) ||
		!ProductRoots[4]->TryGetStringField(TEXT("rules_version"), ExplanationRulesVersion))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("explanation version fields are invalid"));
	if (ExplanationFormat != TEXT("blueprint-lens-explanation") ||
		ExplanationSchemaVersion != ExplanationVersion ||
		ExplanationRulesVersion != SliceVersion)
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("explanation version is unsupported"));

	const FBlueprintLensLoadResult ExplanationResult =
		FBlueprintLensExplanationLoader::LoadFile(
			FPaths::Combine(PacketDirectory, TEXT("explanation.json")));
	if (!ExplanationResult.IsSuccess())
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), ExplanationResult.Error);
	Loaded.Explanation = *ExplanationResult.Model;
	if (Loaded.Explanation.Source.BlueprintAssetPath != Loaded.Request.AssetPath ||
		Loaded.Explanation.Source.GraphId != Loaded.Request.GraphId ||
		Loaded.Explanation.Source.BlueprintPackageSha256 != SourceFingerprint.ToUpper() ||
		Loaded.Explanation.Source.IrSha256 != Loaded.Slice.SourceSha256 ||
		Loaded.Explanation.Query.Direction != TEXT("backward_only"))
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("explanation identity disagrees with the packet"));
	for (const FBlueprintLensUnit& Unit : Loaded.Explanation.Units)
	{
		for (const FBlueprintLensSourceReference& Reference : Unit.SourceReferences)
		{
			if (!Loaded.TypedDocument.NodeIds.Contains(Reference.SourceNodeId) ||
				!ToSet(Loaded.Slice.NodeIds).Contains(Reference.SourceNodeId))
				return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("explanation contains a dangling source reference"));
		}
	}

	if (!ParseBaseline(*ProductRoots[5], Loaded.BaselineFacts))
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("baseline facts structure is invalid"));
	if (Loaded.BaselineFacts.Format != TEXT("blueprint-lens-m6-baseline-facts") ||
		Loaded.BaselineFacts.SchemaVersion != BaselineVersion)
		return Fail(TEXT("M6_PACKET_VERSION_UNSUPPORTED"), TEXT("baseline facts version is unsupported"));
	const TSharedPtr<FJsonObject>* BaselineCounts = nullptr;
	int32 BaselineEntities = 0;
	int32 BaselineRelations = 0;
	int32 BaselineBoundaries = 0;
	int32 BaselineTruncated = 0;
	if (!ProductRoots[5]->TryGetObjectField(TEXT("counts"), BaselineCounts) || BaselineCounts == nullptr ||
		!HasExactFields(**BaselineCounts, {
			TEXT("boundaries"), TEXT("entities"), TEXT("relations"), TEXT("truncated")}) ||
		!ReadInteger(**BaselineCounts, TEXT("entities"), BaselineEntities, 1) ||
		!ReadInteger(**BaselineCounts, TEXT("relations"), BaselineRelations, 0) ||
		!ReadInteger(**BaselineCounts, TEXT("boundaries"), BaselineBoundaries, 0) ||
		!ReadInteger(**BaselineCounts, TEXT("truncated"), BaselineTruncated, 0) ||
		BaselineEntities != Loaded.BaselineFacts.EntityIds.Num() ||
		BaselineRelations != Loaded.BaselineFacts.RelationIds.Num() ||
		BaselineBoundaries != Loaded.BaselineFacts.Boundaries.Num())
		return Fail(TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("baseline counts are invalid"));
	Loaded.BaselineFacts.TruncatedCount = BaselineTruncated;
	if (
		Loaded.BaselineFacts.GraphId != Loaded.Request.GraphId ||
		Loaded.BaselineFacts.RendererId != RendererId ||
		!SameSet(Loaded.BaselineFacts.EntityIds, ToSet(Loaded.Slice.NodeIds)) ||
		!SameSet(Loaded.BaselineFacts.RelationIds, ToSet(Loaded.Slice.EdgeIds)) ||
		!ToSet(Loaded.BaselineFacts.EntityIds).Contains(Loaded.BaselineFacts.CriterionEntityId))
		return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("baseline membership disagrees with the slice"));
	const TSet<FString> BaselineEntityIds = ToSet(Loaded.BaselineFacts.EntityIds);
	for (const auto& Pair : Loaded.BaselineFacts.EntitySourceNodeIds)
	{
		if (!Loaded.TypedDocument.NodeIds.Contains(Pair.Value) || Pair.Key != Pair.Value)
			return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("baseline entity source is dangling"));
	}
	for (const FM6BaselineRelation& Relation : Loaded.BaselineFacts.Relations)
	{
		if (!BaselineEntityIds.Contains(Relation.SourceEntityId) ||
			!BaselineEntityIds.Contains(Relation.TargetEntityId) ||
			!Loaded.TypedDocument.EdgeIds.Contains(Relation.SourceEdgeId) ||
			Relation.Id != Relation.SourceEdgeId)
			return Fail(TEXT("M6_PACKET_REFERENCE_INVALID"), TEXT("baseline relation source is dangling"));
	}
	return MakeValue(MoveTemp(Loaded));
}
