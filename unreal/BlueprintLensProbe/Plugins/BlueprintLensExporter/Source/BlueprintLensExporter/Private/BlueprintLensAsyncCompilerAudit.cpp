// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncCompilerAudit.h"

#include "BlueprintLensSequenceFacts.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Field.h"
#include "UObject/ObjectVersion.h"

namespace BlueprintLensAsyncCompilerAudit
{
	namespace
	{
		FString Sha256Bytes(const TArray<uint8>& Bytes)
		{
			TUniquePtr<FEncryptionContext> CryptoContext = IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!CryptoContext.IsValid()
				|| !CryptoContext->CalcSHA256(Bytes, Digest)
				|| Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		class FCanonicalBytecodeArchive final : public FArchiveUObject
		{
		public:
			FCanonicalBytecodeArchive()
			{
				SetIsSaving(true);
				SetIsPersistent(false);
				SetUEVer(GPackageFileUEVersion);
				SetLicenseeUEVer(GPackageFileLicenseeUEVersion);
				SetEngineVer(FEngineVersion::Current());
			}

			using FArchiveUObject::operator<<;

			virtual void Serialize(void* Data, const int64 Length) override
			{
				Bytes.Append(static_cast<const uint8*>(Data), Length);
			}

			virtual FArchive& operator<<(FName& Name) override
			{
				AppendIdentity('N', Name.ToString());
				return *this;
			}

			virtual FArchive& operator<<(UObject*& Object) override
			{
				AppendIdentity('O', Object == nullptr ? TEXT("<null>") : Object->GetPathName());
				return *this;
			}

			virtual FArchive& operator<<(FObjectPtr& Object) override
			{
				UObject* Resolved = Object.Get();
				return *this << Resolved;
			}

			virtual FArchive& operator<<(FField*& Field) override
			{
				AppendIdentity('F', Field == nullptr ? TEXT("<null>") : Field->GetPathName());
				return *this;
			}

			const TArray<uint8>& GetBytes() const
			{
				return Bytes;
			}

		private:
			void AppendIdentity(const ANSICHAR Kind, const FString& Value)
			{
				FTCHARToUTF8 Utf8(*Value);
				const FString Header = FString::Printf(TEXT("%c%08x:"), Kind, Utf8.Length());
				FTCHARToUTF8 HeaderUtf8(*Header);
				Bytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
				Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			}

			TArray<uint8> Bytes;
		};

		FString CanonicalCompileSha256(
			const UBlueprintGeneratedClass& GeneratedClass,
			const UFunction& UbergraphFunction)
		{
			FCanonicalBytecodeArchive Archive;
			int32 CodeOffset = 0;
			UFunction& MutableFunction = const_cast<UFunction&>(UbergraphFunction);
			while (CodeOffset < UbergraphFunction.Script.Num())
			{
				MutableFunction.SerializeExpr(CodeOffset, Archive);
			}
			if (CodeOffset != UbergraphFunction.Script.Num())
			{
				return FString();
			}

			FTCHARToUTF8 CompileIdentityUtf8(*FString::Printf(
				TEXT("basis=canonical_bytecode_expression_stream_v1;generated_class=%s;function=%s;engine=%s;"),
				*GeneratedClass.GetPathName(),
				*UbergraphFunction.GetPathName(),
				*FEngineVersion::Current().ToString()));
			TArray<uint8> CompileBytes;
			CompileBytes.Append(
				reinterpret_cast<const uint8*>(CompileIdentityUtf8.Get()),
				CompileIdentityUtf8.Length());
			CompileBytes.Append(Archive.GetBytes());
			return Sha256Bytes(CompileBytes);
		}

		bool CalculateCurrentProvenance(
			const UBlueprint& Blueprint,
			FString& OutAssetSha256,
			FString& OutCompileSha256,
			const UBlueprintGeneratedClass*& OutGeneratedClass,
			const UFunction*& OutUbergraphFunction,
			FString& OutError)
		{
			OutGeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint.GeneratedClass);
			OutUbergraphFunction = OutGeneratedClass == nullptr
				? nullptr
				: OutGeneratedClass->FindFunctionByName(FName(*FString::Printf(
					TEXT("ExecuteUbergraph_%s"),
					*Blueprint.GetName())));
			if (OutGeneratedClass == nullptr || OutUbergraphFunction == nullptr
				|| OutUbergraphFunction->Script.IsEmpty())
			{
				OutError = TEXT("LC4-ASYNC current generated ubergraph bytecode is missing.");
				return false;
			}

			const FString AssetFilename = FPackageName::LongPackageNameToFilename(
				Blueprint.GetOutermost()->GetName(),
				FPackageName::GetAssetPackageExtension());
			TArray<uint8> AssetBytes;
			if (!FFileHelper::LoadFileToArray(AssetBytes, *AssetFilename))
			{
				OutError = FString::Printf(TEXT("Could not independently hash asset: %s"), *AssetFilename);
				return false;
			}
			OutAssetSha256 = Sha256Bytes(AssetBytes);

			OutCompileSha256 = CanonicalCompileSha256(*OutGeneratedClass, *OutUbergraphFunction);
			if (OutAssetSha256.IsEmpty() || OutCompileSha256.IsEmpty())
			{
				OutError = TEXT("Could not independently compute LC4-ASYNC SHA-256 provenance.");
				return false;
			}
			return true;
		}
	}

	bool AuditAsyncCompilerLinkage(
		const UBlueprint& Blueprint,
		const FString& SourceFactsPath,
		FString& OutFilePath,
		FAsyncCompilerAuditStats& OutStats,
		FString& OutError)
	{
		OutFilePath.Reset();
		OutStats = FAsyncCompilerAuditStats();
		OutError.Reset();
		FString JsonText;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(JsonText, *SourceFactsPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root)
			|| Root == nullptr)
		{
			OutError = TEXT("Could not read LC4-ASYNC source facts for compiler audit.");
			return false;
		}
		const FString GraphId = Root->GetStringField(TEXT("graph_id"));
		const TSharedPtr<FJsonObject>* Provenance = nullptr;
		if (!Root->TryGetObjectField(TEXT("provenance"), Provenance)
			|| Provenance == nullptr
			|| (*Provenance)->GetStringField(TEXT("asset_sha256")).IsEmpty()
			|| (*Provenance)->GetStringField(TEXT("compile_sha256")).IsEmpty())
		{
			OutError = TEXT("LC4-ASYNC source facts contain no provenance hashes.");
			return false;
		}
		struct FSourceContinuation
		{
			FString ContinuationId;
			FString NodeId;
			FString SourceNodeGuid;
			FString Duration;
			int32 LatentUuid = 0;
			TArray<int32> ResumeCodeOffsets;
		};
		TArray<FSourceContinuation> SourceContinuations;
		const TArray<TSharedPtr<FJsonValue>>* Continuations = nullptr;
		if (!Root->TryGetArrayField(TEXT("continuations"), Continuations) || Continuations == nullptr)
		{
			OutError = TEXT("LC4-ASYNC source facts contain no continuations.");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Continuations)
		{
			const TSharedPtr<FJsonObject> Continuation = Value->AsObject();
			FSourceContinuation& Source = SourceContinuations.AddDefaulted_GetRef();
			Source.ContinuationId = Continuation->GetStringField(TEXT("continuation_id"));
			Source.NodeId = Continuation->GetStringField(TEXT("node_id"));
			Source.SourceNodeGuid = Continuation->GetStringField(TEXT("source_node_guid"));
			Source.Duration = Continuation->GetStringField(TEXT("duration"));
			Source.LatentUuid = static_cast<int32>(Continuation->GetNumberField(TEXT("latent_uuid")));
			for (const TSharedPtr<FJsonValue>& Offset : Continuation->GetArrayField(TEXT("resume_code_offsets")))
			{
				Source.ResumeCodeOffsets.Add(static_cast<int32>(Offset->AsNumber()));
			}
			Source.ResumeCodeOffsets.Sort();
		}

		const UBlueprintGeneratedClass* GeneratedClass = nullptr;
		const UFunction* UbergraphFunction = nullptr;
		FString CurrentAssetSha256;
		FString CurrentCompileSha256;
		if (!CalculateCurrentProvenance(
			Blueprint,
			CurrentAssetSha256,
			CurrentCompileSha256,
			GeneratedClass,
			UbergraphFunction,
			OutError))
		{
			return false;
		}
		const FString SourceAssetSha256 = (*Provenance)->GetStringField(TEXT("asset_sha256"));
		const FString SourceCompileSha256 = (*Provenance)->GetStringField(TEXT("compile_sha256"));
		if ((*Provenance)->GetStringField(TEXT("compile_hash_basis"))
			!= TEXT("canonical_bytecode_expression_stream_v1"))
		{
			OutError = TEXT("LC4-ASYNC source compile hash basis is unsupported.");
			return false;
		}
		if (CurrentAssetSha256 != SourceAssetSha256 || CurrentCompileSha256 != SourceCompileSha256)
		{
			OutError = FString::Printf(
				TEXT("LC4-ASYNC provenance mismatch: asset=%s compile=%s."),
				CurrentAssetSha256 == SourceAssetSha256 ? TEXT("match") : TEXT("stale"),
				CurrentCompileSha256 == SourceCompileSha256 ? TEXT("match") : TEXT("stale"));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		TMap<FString, UK2Node_CallFunction*> DelayByNodeId;
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph == nullptr || Graph->GetPathName() != GraphId)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
				const UFunction* Function = Call == nullptr ? nullptr : Call->GetTargetFunction();
				if (Function != nullptr && Function->GetFName() == TEXT("Delay")
					&& Function->HasMetaData(TEXT("Latent")))
				{
					DelayByNodeId.Add(
						BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Call),
						Call);
				}
			}
		}
		TArray<FString> Lines;
		Lines.Add(TEXT("FORMAT\tblueprint-lens-async-compiler-linkage\t1.0.0"));
		Lines.Add(FString::Printf(TEXT("BLUEPRINT\t%s"), *Blueprint.GetPathName()));
		Lines.Add(FString::Printf(
			TEXT("SOURCE_ASSET_SHA256\t%s"),
			*CurrentAssetSha256));
		Lines.Add(FString::Printf(
			TEXT("SOURCE_COMPILE_SHA256\t%s"),
			*CurrentCompileSha256));
		Lines.Add(TEXT("COMPILE_HASH_BASIS\tcanonical_bytecode_expression_stream_v1"));
		TMap<FString, double> CurrentDurationByParticipant;
		for (const FSourceContinuation& Source : SourceContinuations)
		{
			UK2Node_CallFunction* const* DelayPtr = DelayByNodeId.Find(Source.NodeId);
			UK2Node_CallFunction* Delay = DelayPtr == nullptr ? nullptr : *DelayPtr;
			if (Delay == nullptr)
			{
				OutError = FString::Printf(
					TEXT("Compiler audit could not resolve source continuation: %s"),
					*Source.NodeId);
				return false;
			}
			UEdGraphPin* ResumePin = Delay->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			TArray<int32> CurrentResumeCodeOffsets;
			if (ResumePin != nullptr)
			{
				GeneratedClass->GetDebugData().FindAllCodeLocationsFromSourcePin(
					ResumePin,
					const_cast<UFunction*>(UbergraphFunction),
					CurrentResumeCodeOffsets);
				CurrentResumeCodeOffsets.Sort();
			}
			UEdGraphNode* ReverseNode = GeneratedClass->GetDebugData().FindNodeFromUUID(Source.LatentUuid);
			const FString NodeId = BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Delay);
			const FString SourceNodeGuid = Delay->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
			UEdGraphPin* DurationPin = Delay->FindPin(TEXT("Duration"), EGPD_Input);
			const FString CurrentDuration = DurationPin == nullptr ? FString() : DurationPin->DefaultValue;
			CurrentDurationByParticipant.Add(Source.ContinuationId, FCString::Atod(*CurrentDuration));
			const bool bSourceMatched = NodeId == Source.NodeId
				&& SourceNodeGuid == Source.SourceNodeGuid
				&& FMath::IsNearlyEqual(FCString::Atod(*Source.Duration), FCString::Atod(*CurrentDuration));
			const bool bDebugMatched = ReverseNode == Delay
				&& CurrentResumeCodeOffsets == Source.ResumeCodeOffsets;
			FString ResumeOffsetsText;
			for (const int32 Offset : CurrentResumeCodeOffsets)
			{
				if (!ResumeOffsetsText.IsEmpty())
				{
					ResumeOffsetsText += TEXT(",");
				}
				ResumeOffsetsText += FString::FromInt(Offset);
			}
			Lines.Add(FString::Printf(
				TEXT("CONTINUATION\t%s\tSOURCE_GUID=%s\tLATENT_UUID=%d\tRESUME_CODE_OFFSETS=%s\tSOURCE_MATCH=%d\tDEBUG_MATCH=%d"),
				*NodeId,
				*SourceNodeGuid,
				Source.LatentUuid,
				*ResumeOffsetsText,
				bSourceMatched ? 1 : 0,
				bDebugMatched ? 1 : 0));
			++OutStats.LinkageCount;
			OutStats.MatchedSourceCount += bSourceMatched && bDebugMatched ? 1 : 0;
		}
		const double DurationA = CurrentDurationByParticipant.FindRef(TEXT("A"));
		const double DurationB = CurrentDurationByParticipant.FindRef(TEXT("B"));
		const FString CurrentVariant =
			FMath::IsNearlyEqual(DurationA, 0.1) && FMath::IsNearlyEqual(DurationB, 0.2)
			? TEXT("A_FIRST")
			: FMath::IsNearlyEqual(DurationA, 0.2) && FMath::IsNearlyEqual(DurationB, 0.1)
			? TEXT("B_FIRST")
			: FString();
		const FString CurrentOverlayCanonical = FString::Printf(
			TEXT("schedule_variant=%s;duration_a_seconds=%.6f;duration_b_seconds=%.6f"),
			*CurrentVariant,
			DurationA,
			DurationB);
		FTCHARToUTF8 OverlayUtf8(*CurrentOverlayCanonical);
		TArray<uint8> OverlayBytes;
		OverlayBytes.Append(reinterpret_cast<const uint8*>(OverlayUtf8.Get()), OverlayUtf8.Length());
		const FString CurrentOverlaySha256 = Sha256Bytes(OverlayBytes);
		const TSharedPtr<FJsonObject>* SourceOverlay = nullptr;
		if (CurrentVariant.IsEmpty()
			|| !(*Provenance)->TryGetObjectField(TEXT("schedule_overlay"), SourceOverlay)
			|| SourceOverlay == nullptr
			|| (*SourceOverlay)->GetStringField(TEXT("schedule_variant")) != CurrentVariant
			|| (*SourceOverlay)->GetStringField(TEXT("canonical_input")) != CurrentOverlayCanonical
			|| (*SourceOverlay)->GetStringField(TEXT("overlay_sha256")) != CurrentOverlaySha256)
		{
			OutError = TEXT("LC4-ASYNC schedule overlay provenance mismatch.");
			return false;
		}
		const FString ActiveStateCanonical = FString::Printf(
			TEXT("base_asset_sha256=%s;schedule_overlay_sha256=%s;compile_sha256=%s"),
			*CurrentAssetSha256,
			*CurrentOverlaySha256,
			*CurrentCompileSha256);
		FTCHARToUTF8 ActiveStateUtf8(*ActiveStateCanonical);
		TArray<uint8> ActiveStateBytes;
		ActiveStateBytes.Append(
			reinterpret_cast<const uint8*>(ActiveStateUtf8.Get()),
			ActiveStateUtf8.Length());
		const FString CurrentActiveStateSha256 = Sha256Bytes(ActiveStateBytes);
		if ((*Provenance)->GetStringField(TEXT("base_asset_sha256")) != CurrentAssetSha256
			|| (*Provenance)->GetStringField(TEXT("asset_state")) != TEXT("base_asset_plus_schedule_overlay")
			|| (*Provenance)->GetStringField(TEXT("active_state_sha256")) != CurrentActiveStateSha256)
		{
			OutError = TEXT("LC4-ASYNC active base-plus-overlay state provenance mismatch.");
			return false;
		}
		Lines.Add(FString::Printf(TEXT("SCHEDULE_OVERLAY\t%s\t%s"), *CurrentVariant, *CurrentOverlaySha256));
		Lines.Add(FString::Printf(TEXT("ACTIVE_STATE_SHA256\t%s"), *CurrentActiveStateSha256));
		if (OutStats.LinkageCount != 2 || OutStats.MatchedSourceCount != 2)
		{
			OutError = FString::Printf(
				TEXT("Compiler linkage audit mismatch: linkages=%d matched=%d"),
				OutStats.LinkageCount,
				OutStats.MatchedSourceCount);
			return false;
		}
		Lines.Add(FString::Printf(TEXT("COUNTS\t%d\t%d"), OutStats.LinkageCount, OutStats.MatchedSourceCount));
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("AsyncAudits"),
			FString::Printf(TEXT("%s.async-compiler-linkage.tsv"), *Blueprint.GetName())));
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFilePath), true)
			|| !FFileHelper::SaveStringArrayToFile(
				Lines,
				*OutFilePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write LC4-ASYNC compiler audit: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}
