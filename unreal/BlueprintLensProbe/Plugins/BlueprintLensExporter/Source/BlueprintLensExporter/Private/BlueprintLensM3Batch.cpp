// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM3Batch.h"

#include "BlueprintLensProductionExporter.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BlueprintLensM3Batch
{
	namespace
	{
		struct FRegressionMembership
		{
			FString Id;
			FString ObjectPath;
		};

		struct FCandidateMembership
		{
			FString Id;
			FString ObjectPath;
			FString GraphId;
			FString Band;
			TArray<FString> RiskDimensions;
		};

		struct FAssetMembership
		{
			FString ObjectPath;
			TArray<FString> RegressionIds;
			TArray<FCandidateMembership> Candidates;
		};

		FString Sha256Bytes(const TArray<uint8>& Bytes)
		{
			TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		FString Sha256File(const FString& Path)
		{
			TArray<uint8> Bytes;
			return FFileHelper::LoadFileToArray(Bytes, *Path) ? Sha256Bytes(Bytes) : FString();
		}

		void SetError(FString& OutErrorCode, FString& OutError, const TCHAR* Code, const FString& Message)
		{
			OutErrorCode = Code;
			OutError = Message;
		}

		bool IsCorpusObjectPath(const FString& ObjectPath)
		{
			return ObjectPath.StartsWith(TEXT("/Game/")) && ObjectPath.Contains(TEXT("."));
		}

		bool IsRiskDimension(const FString& Value)
		{
			static const TSet<FString> Allowed = {
				TEXT("source_traceability_and_progressive_disclosure"),
				TEXT("branching_and_incomparable_outcomes"),
				TEXT("data_provenance_fan_in_fan_out"),
				TEXT("sequence_async_completion_and_synchronization"),
				TEXT("call_and_context"),
				TEXT("opaque_unsupported_and_query_budget_boundaries"),
				TEXT("cycles_and_multiple_sccs"),
				TEXT("small_medium_large_scale")};
			return Allowed.Contains(Value);
		}

		bool ReadRequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue)
		{
			return Object.IsValid() && Object->TryGetStringField(Field, OutValue) && !OutValue.IsEmpty();
		}

		bool LoadManifest(
			const FString& ManifestPath,
			const bool bAllowReducedCardinality,
			TArray<FAssetMembership>& OutAssets,
			FString& OutSourceSha256,
			FString& OutErrorCode,
			FString& OutError)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_READ_FAILED"), FString::Printf(TEXT("Could not read corpus manifest: %s"), *ManifestPath));
				return false;
			}
			OutSourceSha256 = Sha256File(ManifestPath);
			if (OutSourceSha256.IsEmpty())
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_HASH_FAILED"), TEXT("Could not hash corpus manifest."));
				return false;
			}

			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_PARSE_FAILED"), TEXT("Corpus manifest is not a JSON object."));
				return false;
			}
			FString SchemaName;
			FString SchemaVersion;
			if (!Root->TryGetStringField(TEXT("schema_name"), SchemaName) || SchemaName != TEXT("blueprint-lens-m3-corpus")
				|| !Root->TryGetStringField(TEXT("schema_version"), SchemaVersion) || SchemaVersion != TEXT("1.0.0"))
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Corpus schema_name/schema_version is invalid."));
				return false;
			}
			static const TSet<FString> RootFields = { TEXT("schema_name"), TEXT("schema_version"), TEXT("regression_assets"), TEXT("candidate_graphs") };
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
			{
				if (!RootFields.Contains(Pair.Key)) { SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Unknown root field.")); return false; }
			}

			const TArray<TSharedPtr<FJsonValue>>* RegressionValues = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* CandidateValues = nullptr;
			if (!Root->TryGetArrayField(TEXT("regression_assets"), RegressionValues) || RegressionValues == nullptr
				|| !Root->TryGetArrayField(TEXT("candidate_graphs"), CandidateValues) || CandidateValues == nullptr)
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Corpus manifest arrays are required."));
				return false;
			}
			if (!bAllowReducedCardinality && (RegressionValues->Num() != 8 || CandidateValues->Num() != 8))
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_CARDINALITY_INVALID"), TEXT("Production corpus requires exactly eight regression assets and eight candidate graphs."));
				return false;
			}

			TMap<FString, FAssetMembership> AssetsByPath;
			TSet<FString> RegressionIds;
			TSet<FString> RegressionPaths;
			TSet<FString> GlobalIds;
			for (const TSharedPtr<FJsonValue>& Value : *RegressionValues)
			{
				const TSharedPtr<FJsonObject> Entry = Value->AsObject();
				FString Id;
				FString ObjectPath;
				if (Entry.IsValid() && Entry->Values.Num() != 2) { SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Unknown regression field.")); return false; }
				if (!ReadRequiredString(Entry, TEXT("id"), Id) || !ReadRequiredString(Entry, TEXT("object_path"), ObjectPath))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Regression asset fields are required."));
					return false;
				}
				if (RegressionIds.Contains(Id))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_REGRESSION_ID"), Id);
					return false;
				}
				RegressionIds.Add(Id);
				if (GlobalIds.Contains(Id)) { SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_GLOBAL_ID"), Id); return false; }
				GlobalIds.Add(Id);
				if (RegressionPaths.Contains(ObjectPath))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_REGRESSION_OBJECT_PATH"), ObjectPath);
					return false;
				}
				RegressionPaths.Add(ObjectPath);
				if (!IsCorpusObjectPath(ObjectPath))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_INVALID_OBJECT_PATH"), ObjectPath);
					return false;
				}
				FAssetMembership& Asset = AssetsByPath.FindOrAdd(ObjectPath);
				Asset.ObjectPath = ObjectPath;
				Asset.RegressionIds.Add(Id);
			}

			TSet<FString> CandidateIds;
			TSet<FString> CandidateGraphIds;
			for (const TSharedPtr<FJsonValue>& Value : *CandidateValues)
			{
				const TSharedPtr<FJsonObject> Entry = Value->AsObject();
				FCandidateMembership Candidate;
				if (Entry.IsValid() && Entry->Values.Num() != 5) { SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Unknown candidate field.")); return false; }
				if (!ReadRequiredString(Entry, TEXT("id"), Candidate.Id)
					|| !ReadRequiredString(Entry, TEXT("object_path"), Candidate.ObjectPath)
					|| !ReadRequiredString(Entry, TEXT("graph_id"), Candidate.GraphId)
					|| !ReadRequiredString(Entry, TEXT("band"), Candidate.Band))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Candidate graph fields are required."));
					return false;
				}
				const TArray<TSharedPtr<FJsonValue>>* RiskValues = nullptr;
				if (!Entry->TryGetArrayField(TEXT("risk_dimensions"), RiskValues) || RiskValues == nullptr || RiskValues->IsEmpty())
				{
					SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_SCHEMA_INVALID"), TEXT("Candidate risk_dimensions are required."));
					return false;
				}
				if (CandidateIds.Contains(Candidate.Id))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_CANDIDATE_ID"), Candidate.Id);
					return false;
				}
				CandidateIds.Add(Candidate.Id);
				if (GlobalIds.Contains(Candidate.Id)) { SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_GLOBAL_ID"), Candidate.Id); return false; }
				GlobalIds.Add(Candidate.Id);
				if (CandidateGraphIds.Contains(Candidate.GraphId))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_DUPLICATE_CANDIDATE_GRAPH_ID"), Candidate.GraphId);
					return false;
				}
				CandidateGraphIds.Add(Candidate.GraphId);
				if (!IsCorpusObjectPath(Candidate.ObjectPath))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_INVALID_OBJECT_PATH"), Candidate.ObjectPath);
					return false;
				}
				if (Candidate.Band != TEXT("small") && Candidate.Band != TEXT("medium") && Candidate.Band != TEXT("large"))
				{
					SetError(OutErrorCode, OutError, TEXT("M3_INVALID_BAND"), Candidate.Band);
					return false;
				}
				TSet<FString> Risks;
				for (const TSharedPtr<FJsonValue>& RiskValue : *RiskValues)
				{
					FString Risk;
					if (!RiskValue->TryGetString(Risk) || !IsRiskDimension(Risk) || Risks.Contains(Risk))
					{
						SetError(OutErrorCode, OutError, TEXT("M3_INVALID_RISK_DIMENSION"), TEXT("Candidate risk dimension is invalid or duplicated."));
						return false;
					}
					Risks.Add(Risk);
					Candidate.RiskDimensions.Add(Risk);
				}
				Algo::Sort(Candidate.RiskDimensions);
				FAssetMembership& Asset = AssetsByPath.FindOrAdd(Candidate.ObjectPath);
				Asset.ObjectPath = Candidate.ObjectPath;
				Asset.Candidates.Add(MoveTemp(Candidate));
			}

		if (AssetsByPath.IsEmpty())
		{
			SetError(OutErrorCode, OutError, TEXT("M3_MANIFEST_EMPTY"), TEXT("Corpus manifest contains no assets."));
			return false;
		}
		AssetsByPath.GenerateValueArray(OutAssets);
		Algo::Sort(OutAssets, [](const FAssetMembership& Left, const FAssetMembership& Right)
		{
			return Left.ObjectPath < Right.ObjectPath;
		});
		return true;
		}

		FString PortableRawName(const FString& ObjectPath)
		{
			FTCHARToUTF8 Utf8(*ObjectPath);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			return FString::Printf(TEXT("raw/%s.raw-0.2.json"), *Sha256Bytes(Bytes));
		}

		void AddCandidateJson(const FCandidateMembership& Candidate, TArray<TSharedPtr<FJsonValue>>& OutValues)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("band"), Candidate.Band);
			Json->SetStringField(TEXT("graph_id"), Candidate.GraphId);
			Json->SetStringField(TEXT("id"), Candidate.Id);
			TArray<TSharedPtr<FJsonValue>> Risks;
			for (const FString& Risk : Candidate.RiskDimensions)
			{
				Risks.Add(MakeShared<FJsonValueString>(Risk));
			}
			Json->SetArrayField(TEXT("risk_dimensions"), Risks);
			OutValues.Add(MakeShared<FJsonValueObject>(Json));
		}

		bool ReconcileCandidateGraphs(const FString& RawPath, const FAssetMembership& Asset)
		{
			if (Asset.Candidates.IsEmpty()) { return true; }
			FString Text; TSharedPtr<FJsonObject> Root;
			if (!FFileHelper::LoadFileToString(Text, *RawPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { return false; }
			const TSharedPtr<FJsonObject>* Blueprint = nullptr; const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
			if (!Root->TryGetObjectField(TEXT("blueprint"), Blueprint) || Blueprint == nullptr || !(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs) || Graphs == nullptr) { return false; }
			TSet<FString> GraphIds;
			for (const TSharedPtr<FJsonValue>& Value : *Graphs) { FString Id; if (const TSharedPtr<FJsonObject> Graph = Value->AsObject(); Graph.IsValid() && Graph->TryGetStringField(TEXT("id"), Id)) { GraphIds.Add(Id); } }
			for (const FCandidateMembership& Candidate : Asset.Candidates) { if (!GraphIds.Contains(Candidate.GraphId)) { return false; } }
			return true;
		}

		using FCanonicalJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

		bool WriteCanonicalJsonValue(const TSharedPtr<FJsonValue>& Value, const TSharedRef<FCanonicalJsonWriter>& Writer);

		bool WriteCanonicalJsonObject(const TSharedPtr<FJsonObject>& Object, const TSharedRef<FCanonicalJsonWriter>& Writer)
		{
			if (!Object.IsValid())
			{
				return false;
			}
			TArray<const TPair<FJsonObject::FStringType, TSharedPtr<FJsonValue>>*> Fields;
			for (const TPair<FJsonObject::FStringType, TSharedPtr<FJsonValue>>& Field : Object->Values)
			{
				Fields.Add(&Field);
			}
			Algo::Sort(Fields, [](const auto* Left, const auto* Right)
			{
				return FStringView(*Left->Key, Left->Key.Len()).Compare(
					FStringView(*Right->Key, Right->Key.Len()), ESearchCase::CaseSensitive) < 0;
			});
			Writer->WriteObjectStart();
			for (const TPair<FJsonObject::FStringType, TSharedPtr<FJsonValue>>* Field : Fields)
			{
				if (Field == nullptr || !Field->Value.IsValid())
				{
					return false;
				}
				Writer->WriteIdentifierPrefix(FStringView(*Field->Key, Field->Key.Len()));
				if (!WriteCanonicalJsonValue(Field->Value, Writer))
				{
					return false;
				}
			}
			Writer->WriteObjectEnd();
			return true;
		}

		bool WriteCanonicalJsonValue(const TSharedPtr<FJsonValue>& Value, const TSharedRef<FCanonicalJsonWriter>& Writer)
		{
			if (!Value.IsValid())
			{
				return false;
			}
			switch (Value->Type)
			{
			case EJson::Null:
				Writer->WriteNull();
				return true;
			case EJson::String:
				Writer->WriteValue(Value->AsString());
				return true;
			case EJson::Number:
				Writer->WriteValue(Value->AsNumber());
				return true;
			case EJson::Boolean:
				Writer->WriteValue(Value->AsBool());
				return true;
			case EJson::Array:
				Writer->WriteArrayStart();
				for (const TSharedPtr<FJsonValue>& Element : Value->AsArray())
				{
					if (!WriteCanonicalJsonValue(Element, Writer))
					{
						return false;
					}
				}
				Writer->WriteArrayEnd();
				return true;
			case EJson::Object:
				return WriteCanonicalJsonObject(Value->AsObject(), Writer);
			case EJson::None:
			default:
				return false;
			}
		}

		bool SerializeCanonicalJson(const TSharedPtr<FJsonObject>& Root, FString& OutJsonText)
		{
			OutJsonText.Reset();
			const TSharedRef<FCanonicalJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonText);
			if (!WriteCanonicalJsonObject(Root, Writer) || !Writer->Close() || OutJsonText.Contains(TEXT("\r")) || OutJsonText.EndsWith(TEXT("\n")))
			{
				OutJsonText.Reset();
				return false;
			}
			OutJsonText += TEXT("\n");
			return true;
		}

		bool FailAfterOwnedOutputDirectory(
			const FString& OutputDirectory,
			FBatchResult& OutResult,
			FString& OutErrorCode,
			FString& OutError)
		{
			const FString OriginalErrorCode = OutErrorCode;
			if (!IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true)
				|| IFileManager::Get().DirectoryExists(*OutputDirectory))
			{
				SetError(
					OutErrorCode,
					OutError,
					TEXT("M3_OUTPUT_CLEANUP_FAILED"),
					FString::Printf(TEXT("Could not remove call-owned output directory after %s: %s"), *OriginalErrorCode, *OutputDirectory));
			}
			OutResult = FBatchResult();
			return false;
		}

		bool ExportBatchInternal(const FBatchRequest& Request, FBatchResult& OutResult, FString& OutErrorCode, FString& OutError, const bool bAllowReducedCardinality)
		{
		OutResult = FBatchResult();
		OutErrorCode.Reset();
		OutError.Reset();
		if (Request.CorpusManifestPath.IsEmpty() || Request.OutputDirectory.IsEmpty()
			|| FPaths::IsRelative(Request.CorpusManifestPath) || FPaths::IsRelative(Request.OutputDirectory))
		{
			SetError(OutErrorCode, OutError, TEXT("M3_INVALID_REQUEST"), TEXT("Absolute corpus manifest and output paths are required."));
			return false;
		}
		const FString OutputDirectory = FPaths::ConvertRelativePathToFull(Request.OutputDirectory);
		if (IFileManager::Get().FileExists(*OutputDirectory) || IFileManager::Get().DirectoryExists(*OutputDirectory))
		{
			SetError(OutErrorCode, OutError, TEXT("M3_OUTPUT_EXISTS"), FString::Printf(TEXT("Output path already exists: %s"), *OutputDirectory));
			return false;
		}
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			SetError(OutErrorCode, OutError, TEXT("M3_OUTPUT_FAILED"), FString::Printf(TEXT("Could not create output directory: %s"), *OutputDirectory));
			return false;
		}

		TArray<FAssetMembership> Assets;
		FString SourceSha256;
		if (!LoadManifest(Request.CorpusManifestPath, bAllowReducedCardinality, Assets, SourceSha256, OutErrorCode, OutError))
		{
			return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
		}
		OutResult.RequestedAssetCount = Assets.Num();
		TArray<TSharedPtr<FJsonValue>> Results;
		for (FAssetMembership& Asset : Assets)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Asset.ObjectPath);
			if (Blueprint == nullptr)
			{
				SetError(OutErrorCode, OutError, TEXT("M3_MISSING_ASSET"), Asset.ObjectPath);
				return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
			}
			if ((Blueprint->Status != BS_UpToDate && Blueprint->Status != BS_UpToDateWithWarnings) || Blueprint->GeneratedClass == nullptr)
			{
				SetError(OutErrorCode, OutError, TEXT("M3_COMPILE_FAILED"), Asset.ObjectPath);
				return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
			}
			const FString PackageFilename = FPackageName::LongPackageNameToFilename(
				Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			const FGuid PersistentGuid = Blueprint->GetOutermost()->GetPersistentGuid();
			const FString PackageSha256 = Sha256File(PackageFilename);
			const FString PackageGuid = PersistentGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
			if (Blueprint->GetOutermost()->IsDirty() || !PersistentGuid.IsValid() || PackageSha256.IsEmpty())
			{
				SetError(OutErrorCode, OutError, TEXT("M3_PROVENANCE_FAILED"), Asset.ObjectPath);
				return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
			}

			Algo::Sort(Asset.RegressionIds);
			Algo::Sort(Asset.Candidates, [](const FCandidateMembership& Left, const FCandidateMembership& Right)
			{
				return Left.Id < Right.Id;
			});
			const FString RelativeRawPath = PortableRawName(Asset.ObjectPath);
			const FString AbsoluteRawPath = FPaths::Combine(OutputDirectory, RelativeRawPath);
			BlueprintLensProductionExporter::FExportRequest RawRequest;
			RawRequest.Blueprint = Blueprint;
			RawRequest.OutputPath = AbsoluteRawPath;
			BlueprintLensProductionExporter::FExportResult RawResult;
			BlueprintLensProductionExporter::FExportError RawError;
			if (!BlueprintLensProductionExporter::ExportRawDocument(RawRequest, RawResult, RawError))
			{
				SetError(
					OutErrorCode,
					OutError,
					RawError.Code == BlueprintLensProductionExporter::EExportErrorCode::WriteFailed ? TEXT("M3_OUTPUT_FAILED") : TEXT("M3_EXPORT_FAILED"),
					RawError.Message);
				return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
			}
			if (!ReconcileCandidateGraphs(AbsoluteRawPath, Asset))
			{
				SetError(OutErrorCode, OutError, TEXT("M3_DANGLING_CANDIDATE_GRAPH"), Asset.ObjectPath);
				return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
			}

			TSharedPtr<FJsonObject> ResultAsset = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> CandidateJson;
			for (const FCandidateMembership& Candidate : Asset.Candidates)
			{
				AddCandidateJson(Candidate, CandidateJson);
			}
			ResultAsset->SetArrayField(TEXT("candidate_membership"), CandidateJson);
			ResultAsset->SetStringField(TEXT("compile_status"), TEXT("up_to_date"));
			ResultAsset->SetNumberField(TEXT("edge_count"), RawResult.EdgeCount);
			ResultAsset->SetStringField(TEXT("generated_class_path"), Blueprint->GeneratedClass->GetPathName());
			ResultAsset->SetNumberField(TEXT("graph_count"), RawResult.GraphCount);
			ResultAsset->SetNumberField(TEXT("node_count"), RawResult.NodeCount);
			ResultAsset->SetStringField(TEXT("object_path"), Asset.ObjectPath);
			ResultAsset->SetStringField(TEXT("package_guid"), PackageGuid);
			ResultAsset->SetStringField(TEXT("package_source_sha256"), PackageSha256);
			ResultAsset->SetNumberField(TEXT("pin_count"), RawResult.PinCount);
			ResultAsset->SetStringField(TEXT("raw_relative_path"), RelativeRawPath);
			ResultAsset->SetStringField(TEXT("raw_sha256"), RawResult.Sha256);
			TArray<TSharedPtr<FJsonValue>> RegressionJson;
			for (const FString& Id : Asset.RegressionIds) { RegressionJson.Add(MakeShared<FJsonValueString>(Id)); }
			ResultAsset->SetArrayField(TEXT("regression_membership"), RegressionJson);
			Results.Add(MakeShared<FJsonValueObject>(ResultAsset));
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("assets"), Results);
		Root->SetStringField(TEXT("schema_name"), TEXT("blueprint-lens-m3-batch-result"));
		Root->SetStringField(TEXT("schema_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("source_manifest_sha256"), SourceSha256);
		FString JsonText;
		if (!SerializeCanonicalJson(Root, JsonText))
		{
			SetError(OutErrorCode, OutError, TEXT("M3_RESULT_SERIALIZATION_FAILED"), TEXT("Could not serialize result manifest."));
			return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
		}
		const FString ResultPath = FPaths::Combine(OutputDirectory, TEXT("batch-result.v1.json"));
		const FString TemporaryResultPath = ResultPath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(JsonText, *TemporaryResultPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !IFileManager::Get().Move(*ResultPath, *TemporaryResultPath, true, true, false, true))
		{
			SetError(OutErrorCode, OutError, TEXT("M3_OUTPUT_FAILED"), TEXT("Could not atomically publish result manifest."));
			return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
		}
		OutResult.ResultManifestPath = ResultPath;
		OutResult.ResultManifestSha256 = Sha256File(ResultPath);
		if (OutResult.ResultManifestSha256.IsEmpty())
		{
			SetError(OutErrorCode, OutError, TEXT("M3_OUTPUT_FAILED"), TEXT("Could not hash published result manifest."));
			return FailAfterOwnedOutputDirectory(OutputDirectory, OutResult, OutErrorCode, OutError);
		}
		OutResult.ExportedAssetCount = Results.Num();
		return true;
	}
	}

	bool ExportBatch(const FBatchRequest& Request, FBatchResult& OutResult, FString& OutErrorCode, FString& OutError)
	{
		return ExportBatchInternal(Request, OutResult, OutErrorCode, OutError, false);
	}

#if WITH_DEV_AUTOMATION_TESTS
	bool ExportBatchForAutomationTest(const FBatchRequest& Request, FBatchResult& OutResult, FString& OutErrorCode, FString& OutError)
	{
		return ExportBatchInternal(Request, OutResult, OutErrorCode, OutError, true);
	}

	bool SerializeCanonicalJsonForAutomationTest(const TSharedPtr<FJsonObject>& Root, FString& OutJsonText)
	{
		return SerializeCanonicalJson(Root, OutJsonText);
	}
#endif
}
