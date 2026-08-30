// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6SessionPacket.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Sort.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensM6PacketTests
{
namespace
{
using FCanonicalWriter =
	TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

FString RepoPath(const TCHAR* Relative)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(), TEXT("../.."), Relative));
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

bool CanonicalText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
	OutText.Reset();
	const TSharedRef<FCanonicalWriter> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&OutText);
	if (!WriteCanonicalValue(Value, Writer) || !Writer->Close()) return false;
	OutText += TEXT("\n");
	return true;
}

bool LoadObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutObject) &&
		OutObject.IsValid();
}

bool SaveObject(const FString& Path, const TSharedPtr<FJsonObject>& Object)
{
	FString Text;
	return CanonicalText(MakeShared<FJsonValueObject>(Object), Text) &&
		FFileHelper::SaveStringToFile(
			Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool Sha256File(const FString& Path, FString& OutHash)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32) return false;
	OutHash = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

bool RebindManifest(const FString& PacketDirectory)
{
	const FString ManifestPath =
		FPaths::Combine(PacketDirectory, TEXT("manifest.json"));
	TSharedPtr<FJsonObject> Manifest;
	if (!LoadObject(ManifestPath, Manifest)) return false;
	const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("files"), Records) || Records == nullptr)
		return false;
	for (const TSharedPtr<FJsonValue>& RecordValue : *Records)
	{
		const TSharedPtr<FJsonObject> Record = RecordValue->AsObject();
		FString RelativePath;
		FString Hash;
		if (!Record.IsValid() ||
			!Record->TryGetStringField(TEXT("path"), RelativePath) ||
			!Sha256File(FPaths::Combine(PacketDirectory, RelativePath), Hash))
			return false;
		Record->SetStringField(TEXT("sha256"), Hash);
	}
	FString RecordText;
	if (!CanonicalText(MakeShared<FJsonValueArray>(*Records), RecordText))
		return false;
	FTCHARToUTF8 Utf8(*RecordText);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32) return false;
	Manifest->SetStringField(
		TEXT("semantic_sha256"),
		BytesToHex(Digest.GetData(), Digest.Num()).ToLower());
	return SaveObject(ManifestPath, Manifest);
}

bool MutateObject(
	const FString& PacketDirectory,
	const TCHAR* Name,
	TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate,
	const bool bRebind = true)
{
	const FString Path = FPaths::Combine(PacketDirectory, Name);
	TSharedPtr<FJsonObject> Object;
	if (!LoadObject(Path, Object)) return false;
	Mutate(Object.ToSharedRef());
	return SaveObject(Path, Object) &&
		(!bRebind || RebindManifest(PacketDirectory));
}

bool RefreshTypedSliceExplanationHashes(const FString& PacketDirectory)
{
	FString TypedHash;
	if (!Sha256File(FPaths::Combine(PacketDirectory, TEXT("typed-source.json")), TypedHash))
		return false;
	if (!MutateObject(PacketDirectory, TEXT("slice.json"), [&TypedHash](TSharedRef<FJsonObject> RootObject)
	{
		RootObject->SetStringField(TEXT("source_sha256"), TypedHash.ToUpper());
	}, false)) return false;
	FString SliceHash;
	if (!Sha256File(FPaths::Combine(PacketDirectory, TEXT("slice.json")), SliceHash))
		return false;
	if (!MutateObject(PacketDirectory, TEXT("explanation.json"), [&TypedHash, &SliceHash](TSharedRef<FJsonObject> RootObject)
	{
		const TSharedPtr<FJsonObject>* Source = nullptr;
		if (RootObject->TryGetObjectField(TEXT("source"), Source) && Source != nullptr)
		{
			(*Source)->SetStringField(TEXT("ir_sha256"), TypedHash.ToUpper());
			(*Source)->SetStringField(TEXT("slice_sha256"), SliceHash.ToUpper());
		}
	}, false)) return false;
	return RebindManifest(PacketDirectory);
}

bool RefreshSliceExplanationHash(const FString& PacketDirectory)
{
	FString SliceHash;
	if (!Sha256File(FPaths::Combine(PacketDirectory, TEXT("slice.json")), SliceHash))
		return false;
	if (!MutateObject(PacketDirectory, TEXT("explanation.json"), [&SliceHash](TSharedRef<FJsonObject> RootObject)
	{
		const TSharedPtr<FJsonObject>* Source = nullptr;
		if (RootObject->TryGetObjectField(TEXT("source"), Source) && Source != nullptr)
			(*Source)->SetStringField(TEXT("slice_sha256"), SliceHash.ToUpper());
	}, false)) return false;
	return RebindManifest(PacketDirectory);
}

bool CopyPacketFiles(const FString& Source, const FString& Destination)
{
	if (!IFileManager::Get().MakeDirectory(*Destination, true)) return false;
	static const TCHAR* Names[] = {
		TEXT("request.json"), TEXT("raw-source.json"), TEXT("typed-source.json"),
		TEXT("slice.json"), TEXT("explanation.json"),
		TEXT("baseline-facts.json"), TEXT("manifest.json")};
	for (const TCHAR* FileName : Names)
	{
		if (IFileManager::Get().Copy(
			*FPaths::Combine(Destination, FileName),
			*FPaths::Combine(Source, FileName), true, true) != COPY_OK)
			return false;
	}
	return true;
}

bool BuildPacket(
	const FString& ScenarioId,
	const TCHAR* RawRelativePath,
	const FString& PacketDirectory,
	FString& OutFingerprint,
	FString& OutError)
{
	(void)RawRelativePath;
	TSharedPtr<FJsonObject> Registry;
	if (!LoadObject(
		RepoPath(TEXT("fixtures/m6/m6-controlled-scenarios.v1.json")),
		Registry))
	{
		OutError = TEXT("controlled scenario registry could not be read");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Scenarios = nullptr;
	if (!Registry->TryGetArrayField(TEXT("scenarios"), Scenarios) ||
		Scenarios == nullptr)
	{
		OutError = TEXT("controlled scenario registry has no scenarios");
		return false;
	}
	TSharedPtr<FJsonObject> Request;
	for (const TSharedPtr<FJsonValue>& ScenarioValue : *Scenarios)
	{
		const TSharedPtr<FJsonObject> Scenario = ScenarioValue->AsObject();
		FString CandidateId;
		const TSharedPtr<FJsonObject>* CandidateRequest = nullptr;
		if (Scenario.IsValid() &&
			Scenario->TryGetStringField(TEXT("scenario_id"), CandidateId) &&
			CandidateId == ScenarioId &&
			Scenario->TryGetObjectField(TEXT("request"), CandidateRequest) &&
			CandidateRequest != nullptr)
		{
			Request = *CandidateRequest;
			break;
		}
	}
	if (!Request.IsValid() ||
		!Request->TryGetStringField(TEXT("source_fingerprint"), OutFingerprint))
	{
		OutError = TEXT("controlled request could not be resolved");
		return false;
	}

	FString SourceDirectory;
	const TCHAR* CommandLineKey = ScenarioId == TEXT("M6-E01")
		? TEXT("-M6ExecutionPacket=") : TEXT("-M6DataPacket=");
	if (!FParse::Value(FCommandLine::Get(), CommandLineKey, SourceDirectory) ||
		SourceDirectory.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("runner did not provide %s"), CommandLineKey);
		return false;
	}
	if (!CopyPacketFiles(SourceDirectory, PacketDirectory))
	{
		OutError = TEXT("runner packet fixture could not be copied");
		return false;
	}
	return true;
}

FString ClonePacket(const FString& Source, const FString& Root, const TCHAR* Name)
{
	const FString Destination = FPaths::Combine(Root, Name);
	return CopyPacketFiles(Source, Destination) ? Destination : FString();
}

void ExpectError(
	FAutomationTestBase& Test,
	const TCHAR* Label,
	const FString& Directory,
	const FString& Fingerprint,
	const TCHAR* ExpectedCode)
{
	const FM6SessionPacketLoadResult Result =
		FM6SessionPacketLoader::Load(Directory, Fingerprint);
	Test.TestTrue(FString::Printf(TEXT("%s has error"), Label), Result.HasError());
	Test.TestFalse(FString::Printf(TEXT("%s has no partial value"), Label), Result.HasValue());
	if (Result.HasError())
	{
		Test.TestEqual(
			FString::Printf(TEXT("%s stable code"), Label),
			Result.GetError().Code,
			FString(ExpectedCode));
	}
}
} // namespace
} // namespace BlueprintLensM6PacketTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6PacketTest,
	"BlueprintLens.M6.Packet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6PacketTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6PacketTests;
	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6PacketTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Root, true);

	const FString ExecutionDirectory = FPaths::Combine(Root, TEXT("execution"));
	const FString DataDirectory = FPaths::Combine(Root, TEXT("data"));
	FString ExecutionFingerprint;
	FString DataFingerprint;
	FString SetupError;
	TestTrue(TEXT("Execution packet generation succeeds"), BuildPacket(
		TEXT("M6-E01"),
		TEXT("artifacts/r1/lc2-guard-truth/BP_LC2_NestedGuards.raw-0.2.json"),
		ExecutionDirectory, ExecutionFingerprint, SetupError));
	TestTrue(TEXT("Data packet generation succeeds"), BuildPacket(
		TEXT("M6-D01"),
		TEXT("artifacts/r1/lc3-value-truth/BP_LC3_ValueProvenance.raw-0.2.json"),
		DataDirectory, DataFingerprint, SetupError));
	if (!SetupError.IsEmpty())
	{
		AddError(SetupError);
		return false;
	}

	const FM6SessionPacketLoadResult Execution =
		FM6SessionPacketLoader::Load(ExecutionDirectory, ExecutionFingerprint);
	const FM6SessionPacketLoadResult Data =
		FM6SessionPacketLoader::Load(DataDirectory, DataFingerprint);
	TestTrue(TEXT("Execution packet loads"), Execution.HasValue());
	TestTrue(TEXT("Data packet loads"), Data.HasValue());
	if (Execution.HasError())
	{
		AddError(FString::Printf(
			TEXT("Execution load failed: %s: %s"),
			*Execution.GetError().Code, *Execution.GetError().Message));
	}
	if (Data.HasError())
	{
		AddError(FString::Printf(
			TEXT("Data load failed: %s: %s"),
			*Data.GetError().Code, *Data.GetError().Message));
	}
	if (Execution.HasValue())
	{
		TestEqual(TEXT("Execution request kind"), Execution.GetValue().Request.QueryKind, FString(TEXT("execution")));
		TestEqual(TEXT("Execution slice nodes"), Execution.GetValue().Slice.NodeIds.Num(), 9);
		TestEqual(TEXT("Execution baseline entities"), Execution.GetValue().BaselineFacts.EntityIds.Num(), 9);
	}
	if (Data.HasValue())
	{
		TestEqual(TEXT("Data request kind"), Data.GetValue().Request.QueryKind, FString(TEXT("data")));
		TestEqual(TEXT("Data slice nodes"), Data.GetValue().Slice.NodeIds.Num(), 7);
		TestEqual(TEXT("Data baseline relations"), Data.GetValue().BaselineFacts.RelationIds.Num(), 6);
	}

	const FString Missing = ClonePacket(ExecutionDirectory, Root, TEXT("missing"));
	IFileManager::Get().Delete(*FPaths::Combine(Missing, TEXT("slice.json")));
	ExpectError(*this, TEXT("Missing file"), Missing, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString Extra = ClonePacket(ExecutionDirectory, Root, TEXT("extra"));
	FFileHelper::SaveStringToFile(TEXT("{}\n"), *FPaths::Combine(Extra, TEXT("extra.json")));
	ExpectError(*this, TEXT("Extra file"), Extra, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString Duplicate = ClonePacket(ExecutionDirectory, Root, TEXT("duplicate"));
	MutateObject(Duplicate, TEXT("manifest.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
		if (RootObject->TryGetArrayField(TEXT("files"), Files) && Files != nullptr)
		{
			TArray<TSharedPtr<FJsonValue>> Changed = *Files;
			const TSharedPtr<FJsonValue> DuplicateRecord = Changed[0];
			Changed.Add(DuplicateRecord);
			RootObject->SetArrayField(TEXT("files"), MoveTemp(Changed));
		}
	}, false);
	ExpectError(*this, TEXT("Duplicate record"), Duplicate, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString Version = ClonePacket(ExecutionDirectory, Root, TEXT("version"));
	MutateObject(Version, TEXT("manifest.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		RootObject->SetStringField(TEXT("schema_version"), TEXT("2.0.0"));
	}, false);
	ExpectError(*this, TEXT("Unsupported version"), Version, ExecutionFingerprint, TEXT("M6_PACKET_VERSION_UNSUPPORTED"));

	const FString Profile = ClonePacket(ExecutionDirectory, Root, TEXT("profile"));
	MutateObject(Profile, TEXT("manifest.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		RootObject->SetStringField(TEXT("renderer_id"), TEXT("UNKNOWN"));
	}, false);
	ExpectError(*this, TEXT("Unsupported profile"), Profile, ExecutionFingerprint, TEXT("M6_PACKET_VERSION_UNSUPPORTED"));

	const FString Crlf = ClonePacket(ExecutionDirectory, Root, TEXT("crlf"));
	FString RequestText;
	FFileHelper::LoadFileToString(RequestText, *FPaths::Combine(Crlf, TEXT("request.json")));
	RequestText.ReplaceInline(TEXT("\n"), TEXT("\r\n"));
	FFileHelper::SaveStringToFile(RequestText, *FPaths::Combine(Crlf, TEXT("request.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ExpectError(*this, TEXT("CRLF"), Crlf, ExecutionFingerprint, TEXT("M6_PACKET_CANONICAL_INVALID"));

	const FString Bom = ClonePacket(ExecutionDirectory, Root, TEXT("bom"));
	FFileHelper::LoadFileToString(RequestText, *FPaths::Combine(Bom, TEXT("request.json")));
	FFileHelper::SaveStringToFile(RequestText, *FPaths::Combine(Bom, TEXT("request.json")), FFileHelper::EEncodingOptions::ForceUTF8);
	ExpectError(*this, TEXT("BOM"), Bom, ExecutionFingerprint, TEXT("M6_PACKET_CANONICAL_INVALID"));

	const FString NonCanonical = ClonePacket(ExecutionDirectory, Root, TEXT("noncanonical"));
	FFileHelper::LoadFileToString(RequestText, *FPaths::Combine(NonCanonical, TEXT("request.json")));
	RequestText = TEXT(" ") + RequestText;
	FFileHelper::SaveStringToFile(RequestText, *FPaths::Combine(NonCanonical, TEXT("request.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ExpectError(*this, TEXT("Noncanonical JSON"), NonCanonical, ExecutionFingerprint, TEXT("M6_PACKET_CANONICAL_INVALID"));

	const FString Hash = ClonePacket(ExecutionDirectory, Root, TEXT("hash"));
	MutateObject(Hash, TEXT("raw-source.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		RootObject->SetStringField(TEXT("engine_version"), TEXT("changed"));
	}, false);
	ExpectError(*this, TEXT("Hash change"), Hash, ExecutionFingerprint, TEXT("M6_PACKET_HASH_MISMATCH"));

	const FString ProductVersion = ClonePacket(ExecutionDirectory, Root, TEXT("product-version"));
	MutateObject(ProductVersion, TEXT("typed-source.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		RootObject->SetStringField(TEXT("schema_version"), TEXT("2.0.0"));
	});
	ExpectError(*this, TEXT("Unsupported product version"), ProductVersion, ExecutionFingerprint, TEXT("M6_PACKET_VERSION_UNSUPPORTED"));

	const FString TypedRef = ClonePacket(ExecutionDirectory, Root, TEXT("typed-ref"));
	MutateObject(TypedRef, TEXT("typed-source.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TSharedPtr<FJsonObject>* Blueprint = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (RootObject->TryGetObjectField(TEXT("blueprint"), Blueprint) && Blueprint != nullptr &&
			(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs) && Graphs != nullptr)
		{
			const TSharedPtr<FJsonObject> Graph = (*Graphs)[0]->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (Graph.IsValid() && Graph->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes != nullptr)
				(*Nodes)[0]->AsObject()->SetStringField(TEXT("id"), TEXT("missing.typed.node"));
		}
	}, false);
	RefreshTypedSliceExplanationHashes(TypedRef);
	ExpectError(*this, TEXT("Dangling typed reference"), TypedRef, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString SliceRef = ClonePacket(ExecutionDirectory, Root, TEXT("slice-ref"));
	MutateObject(SliceRef, TEXT("slice.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
		if (RootObject->TryGetArrayField(TEXT("node_ids"), NodeIds) && NodeIds != nullptr)
		{
			TArray<TSharedPtr<FJsonValue>> Changed = *NodeIds;
			Changed.Add(MakeShared<FJsonValueString>(TEXT("missing.slice.node")));
			RootObject->SetArrayField(TEXT("node_ids"), MoveTemp(Changed));
		}
		const TSharedPtr<FJsonObject>* Counts = nullptr;
		if (RootObject->TryGetObjectField(TEXT("counts"), Counts) && Counts != nullptr)
		{
			double Nodes = 0.0;
			if ((*Counts)->TryGetNumberField(TEXT("nodes"), Nodes))
				(*Counts)->SetNumberField(TEXT("nodes"), Nodes + 1.0);
		}
	}, false);
	MutateObject(SliceRef, TEXT("manifest.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TSharedPtr<FJsonObject>* Counts = nullptr;
		if (RootObject->TryGetObjectField(TEXT("counts"), Counts) && Counts != nullptr)
		{
			double Entities = 0.0;
			if ((*Counts)->TryGetNumberField(TEXT("selected_entities"), Entities))
				(*Counts)->SetNumberField(TEXT("selected_entities"), Entities + 1.0);
		}
	}, false);
	RefreshSliceExplanationHash(SliceRef);
	ExpectError(*this, TEXT("Dangling slice reference"), SliceRef, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString ExplanationRef = ClonePacket(ExecutionDirectory, Root, TEXT("explanation-ref"));
	MutateObject(ExplanationRef, TEXT("explanation.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
		if (RootObject->TryGetArrayField(TEXT("units"), Units) && Units != nullptr)
		{
			const TSharedPtr<FJsonObject> Unit = (*Units)[0]->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* References = nullptr;
			if (Unit.IsValid() && Unit->TryGetArrayField(TEXT("source_references"), References) && References != nullptr)
				(*References)[0]->AsObject()->SetStringField(TEXT("source_node_id"), TEXT("missing.node"));
		}
	});
	ExpectError(*this, TEXT("Dangling explanation reference"), ExplanationRef, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString BaselineRef = ClonePacket(ExecutionDirectory, Root, TEXT("baseline-ref"));
	MutateObject(BaselineRef, TEXT("baseline-facts.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr;
		if (RootObject->TryGetArrayField(TEXT("entities"), Entities) && Entities != nullptr)
			(*Entities)[0]->AsObject()->SetStringField(TEXT("id"), TEXT("missing.entity"));
	});
	ExpectError(*this, TEXT("Membership disagreement"), BaselineRef, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	const FString SharedFactRef = ClonePacket(ExecutionDirectory, Root, TEXT("shared-fact-ref"));
	MutateObject(SharedFactRef, TEXT("baseline-facts.json"), [](TSharedRef<FJsonObject> RootObject)
	{
		const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
		if (RootObject->TryGetArrayField(TEXT("relations"), Relations) && Relations != nullptr)
			(*Relations)[0]->AsObject()->SetStringField(TEXT("source_entity_id"), TEXT("missing.shared.entity"));
	});
	ExpectError(*this, TEXT("Dangling shared fact"), SharedFactRef, ExecutionFingerprint, TEXT("M6_PACKET_REFERENCE_INVALID"));

	ExpectError(*this, TEXT("Stale source"), ExecutionDirectory, FString::ChrN(64, TEXT('0')), TEXT("M6_PACKET_SOURCE_STALE"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
