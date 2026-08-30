// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncFacts.h"

#include "BlueprintLensSequenceFacts.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Event.h"
#include "K2Node_VariableSet.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Field.h"
#include "UObject/ObjectVersion.h"

namespace BlueprintLensAsyncFacts
{
	namespace
	{
		bool IsFunction(const UEdGraphNode& Node, const FName Name)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node);
			return Call != nullptr
				&& Call->GetTargetFunction() != nullptr
				&& Call->GetTargetFunction()->GetFName() == Name;
		}

		FString DefaultValue(const UEdGraphNode& Node, const FName PinName)
		{
			const UEdGraphPin* Pin = Node.FindPin(PinName, EGPD_Input);
			return Pin == nullptr ? FString() : Pin->DefaultValue;
		}

		UK2Node_CallFunction* FollowSingleExec(
			UEdGraphNode& Node,
			const FName ExpectedFunction,
			FString& OutError)
		{
			UEdGraphPin* ThenPin = Node.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			if (ThenPin == nullptr || ThenPin->LinkedTo.Num() != 1
				|| ThenPin->LinkedTo[0] == nullptr)
			{
				OutError = FString::Printf(
					TEXT("Node %s must have exactly one outgoing exec link."),
					*Node.GetName());
				return nullptr;
			}
			UK2Node_CallFunction* Target = Cast<UK2Node_CallFunction>(
				ThenPin->LinkedTo[0]->GetOwningNode());
			if (Target == nullptr || !IsFunction(*Target, ExpectedFunction))
			{
				OutError = FString::Printf(
					TEXT("Node %s must lead to function %s."),
					*Node.GetName(),
					*ExpectedFunction.ToString());
				return nullptr;
			}
			return Target;
		}

		bool IsUniqueExecLink(
			UEdGraphNode& Source,
			const FName SourcePinName,
			UEdGraphNode& Target,
			FString& OutError)
		{
			UEdGraphPin* SourcePin = Source.FindPin(SourcePinName, EGPD_Output);
			UEdGraphPin* TargetPin = Target.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (SourcePin == nullptr || TargetPin == nullptr
				|| SourcePin->LinkedTo.Num() != 1 || SourcePin->LinkedTo[0] != TargetPin
				|| !TargetPin->LinkedTo.Contains(SourcePin))
			{
				OutError = FString::Printf(
					TEXT("Expected unique exec link %s.%s -> %s.Execute."),
					*Source.GetName(),
					*SourcePinName.ToString(),
					*Target.GetName());
				return false;
			}
			return true;
		}

		FString JsonOutputPath(const UBlueprint& Blueprint)
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("BlueprintLens"),
				TEXT("AsyncFacts"),
				FString::Printf(TEXT("%s.async-source.json"), *Blueprint.GetName())));
		}

		FString Sha256Bytes(const TArray<uint8>& Bytes)
		{
			TUniquePtr<FEncryptionContext> CryptoContext =
				IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!CryptoContext.IsValid()
				|| !CryptoContext->CalcSHA256(Bytes, Digest)
				|| Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		FString Sha256String(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			return Sha256Bytes(Bytes);
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

		TSharedPtr<FJsonValue> MakeBoundary(
			const TCHAR* Kind,
			const TCHAR* Support,
			const TCHAR* Detail)
		{
			TSharedPtr<FJsonObject> Boundary = MakeShared<FJsonObject>();
			Boundary->SetStringField(TEXT("boundary_kind"), Kind);
			Boundary->SetStringField(TEXT("support"), Support);
			Boundary->SetStringField(TEXT("detail"), Detail);
			return MakeShared<FJsonValueObject>(Boundary);
		}
	}

	bool ExportAsyncFacts(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		const FString& CriterionNodeId,
		FString& OutFilePath,
		FAsyncFactStats& OutStats,
		FString& OutError)
	{
		OutFilePath.Reset();
		OutStats = FAsyncFactStats();
		OutError.Reset();
		UEdGraph* Graph = nullptr;
		UK2Node_ExecutionSequence* Sequence = nullptr;
		UK2Node_Event* BeginPlay = nullptr;
		UK2Node_CallFunction* InvocationController = nullptr;
		UK2Node_VariableSet* Criterion = nullptr;
		UK2Node_CallFunction* CriterionRecord = nullptr;
		int32 DelayInventoryCount = 0;
		int32 ArrivalInventoryCount = 0;
		int32 CriterionInventoryCount = 0;
		int32 CriterionRecordInventoryCount = 0;
		int32 InvocationControllerCount = 0;
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		for (UEdGraph* CandidateGraph : Graphs)
		{
			if (CandidateGraph == nullptr)
			{
				continue;
			}
			const FString GraphId = CandidateGraph->GetPathName();
			for (UEdGraphNode* Node : CandidateGraph->Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				const FString NodeId = BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Node);
				if (NodeId == SequenceNodeId)
				{
					Sequence = Cast<UK2Node_ExecutionSequence>(Node);
					Graph = CandidateGraph;
				}
				if (NodeId == CriterionNodeId)
				{
					Criterion = Cast<UK2Node_VariableSet>(Node);
				}
				if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
				{
					if (Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
					{
						BeginPlay = Event;
					}
				}
				if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
				{
					if (SetNode->GetVarName() == TEXT("LC4AsyncComplete"))
					{
						++CriterionInventoryCount;
					}
				}
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					if (Function != nullptr && Function->GetFName() == TEXT("Delay")
						&& Function->HasMetaData(TEXT("Latent")))
					{
						++DelayInventoryCount;
					}
					if (Function != nullptr && Function->GetFName() == TEXT("ArriveAtLC4Barrier"))
					{
						++ArrivalInventoryCount;
					}
					if (Function != nullptr && Function->GetFName() == TEXT("BeginLC4AsyncInvocation"))
					{
						++InvocationControllerCount;
						InvocationController = Call;
					}
					if (Function != nullptr && Function->GetFName() == TEXT("RecordLC4AsyncCriterion"))
					{
						++CriterionRecordInventoryCount;
						CriterionRecord = Call;
					}
				}
			}
		}
		if (Graph == nullptr || Sequence == nullptr || BeginPlay == nullptr
			|| InvocationController == nullptr || InvocationControllerCount != 1
			|| Criterion == nullptr || Criterion->GetVarName() != TEXT("LC4AsyncComplete")
			|| CriterionInventoryCount != 1 || CriterionRecord == nullptr
			|| CriterionRecordInventoryCount != 1
			|| DelayInventoryCount != 2 || ArrivalInventoryCount != 2
			|| FindFProperty<FBoolProperty>(Blueprint.GeneratedClass, TEXT("LC4AsyncComplete")) == nullptr)
		{
			OutError = TEXT("LC4-ASYNC source inventory does not match the bounded fixture.");
			return false;
		}
		if (!IsUniqueExecLink(*BeginPlay, UEdGraphSchema_K2::PN_Then, *InvocationController, OutError)
			|| !IsUniqueExecLink(*InvocationController, UEdGraphSchema_K2::PN_Then, *Sequence, OutError)
			|| !IsUniqueExecLink(*Criterion, UEdGraphSchema_K2::PN_Then, *CriterionRecord, OutError))
		{
			return false;
		}
		int32 DeclaredSequenceOutputCount = 0;
		while (Sequence->GetThenPinGivenIndex(DeclaredSequenceOutputCount) != nullptr)
		{
			++DeclaredSequenceOutputCount;
		}
		if (DeclaredSequenceOutputCount != 2)
		{
			OutError = FString::Printf(
				TEXT("LC4-ASYNC requires exactly two declared Sequence outputs; found %d."),
				DeclaredSequenceOutputCount);
			return false;
		}
		UEdGraphPin* CriterionExecute = Criterion->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* CriterionRecordExecute = CriterionRecord->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* CriterionValue = Criterion->FindPin(Criterion->GetVarName(), EGPD_Input);
		if (CriterionExecute == nullptr || CriterionExecute->LinkedTo.Num() != 2
			|| CriterionRecordExecute == nullptr || CriterionRecordExecute->LinkedTo.Num() != 1
			|| CriterionValue == nullptr
			|| !CriterionValue->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("LC4-ASYNC criterion requires two release inputs, one Record successor and Set LC4AsyncComplete=true.");
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> Launches;
		TArray<TSharedPtr<FJsonValue>> Continuations;
		TArray<TSharedPtr<FJsonValue>> Participants;
		TSet<UEdGraphPin*> BoundReleasePins;
		TMap<FString, FString> DurationByParticipant;
		FString InvocationId;
		for (int32 Ordinal = 0; Ordinal < 2; ++Ordinal)
		{
			UEdGraphPin* Output = Sequence->GetThenPinGivenIndex(Ordinal);
			if (Output == nullptr || Output->LinkedTo.Num() != 1)
			{
				OutError = FString::Printf(TEXT("Async launch ordinal %d must have one target."), Ordinal);
				return false;
			}
			const FString ParticipantId = Ordinal == 0 ? TEXT("A") : TEXT("B");
			UK2Node_CallFunction* LaunchNode = Cast<UK2Node_CallFunction>(
				Output->LinkedTo[0]->GetOwningNode());
			if (LaunchNode == nullptr || !IsFunction(*LaunchNode, TEXT("RecordLC4AsyncLaunch"))
				|| DefaultValue(*LaunchNode, TEXT("ParticipantId")) != ParticipantId)
			{
				OutError = FString::Printf(
					TEXT("Async launch ordinal %d is not bound to participant %s."),
					Ordinal,
					*ParticipantId);
				return false;
			}
			UK2Node_CallFunction* Delay = FollowSingleExec(*LaunchNode, TEXT("Delay"), OutError);
			UK2Node_CallFunction* Arrival = Delay == nullptr
				? nullptr
				: FollowSingleExec(*Delay, TEXT("ArriveAtLC4Barrier"), OutError);
			if (Delay == nullptr || Arrival == nullptr
				|| DefaultValue(*Arrival, TEXT("ParticipantId")) != ParticipantId)
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Async ordinal %d arrival does not match participant %s."),
						Ordinal,
						*ParticipantId);
				}
				return false;
			}
			const FString BranchInvocationId = DefaultValue(*LaunchNode, TEXT("InvocationId"));
			if (BranchInvocationId.IsEmpty()
				|| DefaultValue(*Arrival, TEXT("InvocationId")) != BranchInvocationId
				|| DefaultValue(*CriterionRecord, TEXT("InvocationId")) != BranchInvocationId
				|| DefaultValue(*InvocationController, TEXT("InvocationId")) != BranchInvocationId
				|| (!InvocationId.IsEmpty() && InvocationId != BranchInvocationId))
			{
				OutError = FString::Printf(TEXT("LC4-ASYNC invocation identity mismatch for %s."), *ParticipantId);
				return false;
			}
			InvocationId = BranchInvocationId;
			UEdGraphPin* ReleasePin = Arrival->FindPin(TEXT("Released"), EGPD_Output);
			if (ReleasePin == nullptr || ReleasePin->LinkedTo.Num() != 1
				|| ReleasePin->LinkedTo[0] != CriterionExecute)
			{
				OutError = FString::Printf(TEXT("LC4-ASYNC Released %s must uniquely target canonical Set."), *ParticipantId);
				return false;
			}
			BoundReleasePins.Add(ReleasePin);
			const FString LaunchNodeId = BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *LaunchNode);
			TSharedPtr<FJsonObject> LaunchRecord = MakeShared<FJsonObject>();
			LaunchRecord->SetStringField(TEXT("launch_site_id"), FString::Printf(TEXT("launch-%s"), *ParticipantId));
			LaunchRecord->SetStringField(TEXT("launch_node_id"), LaunchNodeId);
			LaunchRecord->SetStringField(TEXT("source_identity"), LaunchNodeId);
			LaunchRecord->SetStringField(
				TEXT("source_node_guid"),
				LaunchNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			LaunchRecord->SetNumberField(TEXT("ordinal"), Ordinal);
			LaunchRecord->SetStringField(
				TEXT("source_pin_id"),
				BlueprintLensSequenceFacts::MakePinId(SequenceNodeId, *Output));
			LaunchRecord->SetStringField(TEXT("participant_id"), ParticipantId);
			LaunchRecord->SetStringField(
				TEXT("connected_target_node_id"),
				LaunchNodeId);
			Launches.Add(MakeShared<FJsonValueObject>(LaunchRecord));

			const FString NodeId = BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *Delay);
			UEdGraphPin* ResumePin = Delay->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			TSharedPtr<FJsonObject> Continuation = MakeShared<FJsonObject>();
			Continuation->SetStringField(TEXT("continuation_id"), ParticipantId);
			Continuation->SetStringField(TEXT("node_id"), NodeId);
			Continuation->SetStringField(TEXT("node_family"), TEXT("UKismetSystemLibrary::Delay"));
			Continuation->SetStringField(TEXT("latent_function_name"), TEXT("Delay"));
			Continuation->SetStringField(
				TEXT("source_node_guid"),
				Delay->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			const int32 LatentUuid = GetTypeHash(Delay->NodeGuid);
			Continuation->SetNumberField(TEXT("latent_uuid"), LatentUuid);
			Continuation->SetStringField(
				TEXT("resume_pin_id"),
				ResumePin == nullptr
					? FString()
					: BlueprintLensSequenceFacts::MakePinId(NodeId, *ResumePin));
			Continuation->SetStringField(TEXT("duration"), DefaultValue(*Delay, TEXT("Duration")));
			DurationByParticipant.Add(ParticipantId, DefaultValue(*Delay, TEXT("Duration")));
			const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint.GeneratedClass);
			const UFunction* UbergraphFunction = GeneratedClass == nullptr
				? nullptr
				: GeneratedClass->FindFunctionByName(FName(*FString::Printf(
					TEXT("ExecuteUbergraph_%s"),
					*Blueprint.GetName())));
			TArray<int32> ResumeCodeOffsets;
			if (GeneratedClass == nullptr || UbergraphFunction == nullptr || ResumePin == nullptr)
			{
				OutError = TEXT("LC4-ASYNC generated ubergraph or resume pin is missing.");
				return false;
			}
			GeneratedClass->GetDebugData().FindAllCodeLocationsFromSourcePin(
				ResumePin,
				const_cast<UFunction*>(UbergraphFunction),
				ResumeCodeOffsets);
			ResumeCodeOffsets.Sort();
			if (GeneratedClass->GetDebugData().FindNodeFromUUID(LatentUuid) != Delay
				|| ResumeCodeOffsets.IsEmpty())
			{
				OutError = FString::Printf(TEXT("LC4-ASYNC compiler binding is missing for %s."), *ParticipantId);
				return false;
			}
			TArray<TSharedPtr<FJsonValue>> ResumeOffsetsJson;
			for (const int32 Offset : ResumeCodeOffsets)
			{
				ResumeOffsetsJson.Add(MakeShared<FJsonValueNumber>(Offset));
			}
			Continuation->SetArrayField(TEXT("resume_code_offsets"), ResumeOffsetsJson);
			Continuations.Add(MakeShared<FJsonValueObject>(Continuation));

			TSharedPtr<FJsonObject> Participant = MakeShared<FJsonObject>();
			Participant->SetStringField(TEXT("participant_id"), DefaultValue(*Arrival, TEXT("ParticipantId")));
			const FString ArrivalNodeId = BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *Arrival);
			Participant->SetStringField(
				TEXT("arrival_node_id"),
				ArrivalNodeId);
			UEdGraphPin* ArrivalExecute = Arrival->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			Participant->SetStringField(
				TEXT("arrival_execute_pin_id"),
				ArrivalExecute == nullptr
					? FString()
					: BlueprintLensSequenceFacts::MakePinId(ArrivalNodeId, *ArrivalExecute));
			Participant->SetStringField(
				TEXT("release_pin_id"),
				ReleasePin == nullptr
					? FString()
					: BlueprintLensSequenceFacts::MakePinId(ArrivalNodeId, *ReleasePin));
			Participants.Add(MakeShared<FJsonValueObject>(Participant));
		}
		if (BoundReleasePins.Num() != 2)
		{
			OutError = TEXT("LC4-ASYNC release inputs do not bind two unique arrival sites.");
			return false;
		}

		const FString CriterionExecutePinId = [Criterion, &CriterionNodeId]()
		{
			UEdGraphPin* Pin = Criterion->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			return Pin == nullptr
				? FString()
				: BlueprintLensSequenceFacts::MakePinId(CriterionNodeId, *Pin);
		}();
		if (InvocationController == nullptr || CriterionExecutePinId.IsEmpty())
		{
			OutError = TEXT("LC4-ASYNC controller or criterion execute pin is missing.");
			return false;
		}

		const FString AssetFilename = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		TArray<uint8> AssetBytes;
		if (!FFileHelper::LoadFileToArray(AssetBytes, *AssetFilename))
		{
			OutError = FString::Printf(TEXT("Could not hash LC4-ASYNC asset: %s"), *AssetFilename);
			return false;
		}
		const FString AssetSha256 = Sha256Bytes(AssetBytes);
		const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint.GeneratedClass);
		const UFunction* UbergraphFunction = GeneratedClass == nullptr
			? nullptr
			: GeneratedClass->FindFunctionByName(FName(*FString::Printf(
				TEXT("ExecuteUbergraph_%s"),
				*Blueprint.GetName())));
		if (GeneratedClass == nullptr || UbergraphFunction == nullptr || UbergraphFunction->Script.IsEmpty())
		{
			OutError = TEXT("LC4-ASYNC generated ubergraph bytecode is missing.");
			return false;
		}
		const FString CompileSha256 = CanonicalCompileSha256(*GeneratedClass, *UbergraphFunction);
		const double DurationA = FCString::Atod(*DurationByParticipant.FindRef(TEXT("A")));
		const double DurationB = FCString::Atod(*DurationByParticipant.FindRef(TEXT("B")));
		const FString ActiveScheduleVariant =
			FMath::IsNearlyEqual(DurationA, 0.1) && FMath::IsNearlyEqual(DurationB, 0.2)
			? TEXT("A_FIRST")
			: FMath::IsNearlyEqual(DurationA, 0.2) && FMath::IsNearlyEqual(DurationB, 0.1)
			? TEXT("B_FIRST")
			: FString();
		const FString OverlayCanonical = FString::Printf(
			TEXT("schedule_variant=%s;duration_a_seconds=%.6f;duration_b_seconds=%.6f"),
			*ActiveScheduleVariant,
			DurationA,
			DurationB);
		const FString OverlaySha256 = ActiveScheduleVariant.IsEmpty()
			? FString()
			: Sha256String(OverlayCanonical);
		const FString ActiveStateSha256 = Sha256String(FString::Printf(
			TEXT("base_asset_sha256=%s;schedule_overlay_sha256=%s;compile_sha256=%s"),
			*AssetSha256,
			*OverlaySha256,
			*CompileSha256));
		if (AssetSha256.IsEmpty() || CompileSha256.IsEmpty()
			|| OverlaySha256.IsEmpty() || ActiveStateSha256.IsEmpty())
		{
			OutError = TEXT("Could not compute LC4-ASYNC base-asset, overlay or bytecode provenance.");
			return false;
		}

		OutStats.LaunchCount = Launches.Num();
		OutStats.ContinuationCount = Continuations.Num();
		OutStats.ParticipantCount = Participants.Num();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-async-source"));
		Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("rules_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("blueprint_asset_path"), Blueprint.GetPathName());
		Root->SetStringField(TEXT("graph_id"), Graph->GetPathName());
		Root->SetStringField(TEXT("sequence_node_id"), SequenceNodeId);
		Root->SetStringField(TEXT("criterion_node_id"), CriterionNodeId);
		Root->SetStringField(TEXT("barrier_contract"), TEXT("participants=A,B;release=all;single_fire=true;implicit_reset=false"));
		TSharedPtr<FJsonObject> Provenance = MakeShared<FJsonObject>();
		Provenance->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Provenance->SetStringField(TEXT("asset_path"), Blueprint.GetPathName());
		Provenance->SetStringField(TEXT("asset_sha256"), AssetSha256);
		Provenance->SetStringField(TEXT("base_asset_sha256"), AssetSha256);
		Provenance->SetStringField(TEXT("asset_state"), TEXT("base_asset_plus_schedule_overlay"));
		Provenance->SetStringField(TEXT("graph_id"), Graph->GetPathName());
		Provenance->SetStringField(TEXT("generated_class_path"), GeneratedClass->GetPathName());
		Provenance->SetStringField(TEXT("ubergraph_function_path"), UbergraphFunction->GetPathName());
		Provenance->SetNumberField(TEXT("ubergraph_bytecode_size"), UbergraphFunction->Script.Num());
		Provenance->SetStringField(
			TEXT("compile_state"),
			Blueprint.Status == BS_UpToDate || Blueprint.Status == BS_UpToDateWithWarnings
				? TEXT("up_to_date")
				: TEXT("not_up_to_date"));
		Provenance->SetStringField(TEXT("compile_sha256"), CompileSha256);
		Provenance->SetStringField(
			TEXT("compile_hash_basis"),
			TEXT("canonical_bytecode_expression_stream_v1"));
		TSharedPtr<FJsonObject> ScheduleOverlay = MakeShared<FJsonObject>();
		ScheduleOverlay->SetStringField(TEXT("format"), TEXT("blueprint-lens-async-schedule-overlay"));
		ScheduleOverlay->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		ScheduleOverlay->SetStringField(TEXT("schedule_variant"), ActiveScheduleVariant);
		ScheduleOverlay->SetNumberField(TEXT("duration_a_seconds"), DurationA);
		ScheduleOverlay->SetNumberField(TEXT("duration_b_seconds"), DurationB);
		ScheduleOverlay->SetStringField(TEXT("canonical_input"), OverlayCanonical);
		ScheduleOverlay->SetStringField(TEXT("overlay_sha256"), OverlaySha256);
		Provenance->SetObjectField(TEXT("schedule_overlay"), ScheduleOverlay);
		Provenance->SetStringField(TEXT("active_state_sha256"), ActiveStateSha256);
		Root->SetObjectField(TEXT("provenance"), Provenance);

		TSharedPtr<FJsonObject> CriterionJson = MakeShared<FJsonObject>();
		CriterionJson->SetStringField(TEXT("node_id"), CriterionNodeId);
		CriterionJson->SetStringField(TEXT("execute_pin_id"), CriterionExecutePinId);
		CriterionJson->SetStringField(
			TEXT("source_action"),
			FString::Printf(TEXT("Set %s"), *Criterion->GetVarName().ToString()));
		CriterionJson->SetBoolField(TEXT("assigned_value"), true);
		Root->SetObjectField(TEXT("criterion"), CriterionJson);
		Root->SetArrayField(TEXT("launches"), Launches);
		Root->SetArrayField(TEXT("continuations"), Continuations);
		Root->SetArrayField(TEXT("participants"), Participants);
		TSharedPtr<FJsonObject> Barrier = MakeShared<FJsonObject>();
		Barrier->SetStringField(TEXT("barrier_site_id"), TEXT("BlueprintLensAsyncBarrier:LC4_RUN"));
		Barrier->SetStringField(TEXT("barrier_object_identity"), TEXT("FBlueprintLensAsyncBarrierState"));
		Barrier->SetStringField(
			TEXT("begin_invocation_node_id"),
			BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *InvocationController));
		Barrier->SetArrayField(TEXT("arrival_call_sites"), Participants);
		TArray<TSharedPtr<FJsonValue>> ReleaseOutputs;
		for (const TSharedPtr<FJsonValue>& Participant : Participants)
		{
			ReleaseOutputs.Add(MakeShared<FJsonValueString>(
				Participant->AsObject()->GetStringField(TEXT("release_pin_id"))));
		}
		Barrier->SetArrayField(TEXT("release_output_pin_ids"), ReleaseOutputs);
		Barrier->SetStringField(TEXT("release_site_id"), TEXT("BlueprintLensAsyncBarrier:release"));
		Barrier->SetStringField(TEXT("reset_policy"), TEXT("explicit_only"));
		Barrier->SetStringField(TEXT("cancel_policy"), TEXT("closes_invocation"));
		Barrier->SetBoolField(TEXT("single_fire_guarantee"), true);
		Root->SetObjectField(TEXT("barrier"), Barrier);

		TArray<TSharedPtr<FJsonValue>> ScheduleVariants;
		for (const TTuple<const TCHAR*, double, double>& Variant : {
			TTuple<const TCHAR*, double, double>(TEXT("A_FIRST"), 0.1, 0.2),
			TTuple<const TCHAR*, double, double>(TEXT("B_FIRST"), 0.2, 0.1)})
		{
			TSharedPtr<FJsonObject> Schedule = MakeShared<FJsonObject>();
			Schedule->SetStringField(TEXT("schedule_variant"), Variant.Get<0>());
			Schedule->SetNumberField(TEXT("duration_a_seconds"), Variant.Get<1>());
			Schedule->SetNumberField(TEXT("duration_b_seconds"), Variant.Get<2>());
			ScheduleVariants.Add(MakeShared<FJsonValueObject>(Schedule));
		}
		Root->SetArrayField(TEXT("schedule_variants"), ScheduleVariants);

		TArray<TSharedPtr<FJsonValue>> Boundaries;
		Boundaries.Add(MakeBoundary(TEXT("scheduler"), TEXT("observed_only"), TEXT("Delay completion order is observed per retained run and is not a source guarantee.")));
		Boundaries.Add(MakeBoundary(TEXT("world_tick"), TEXT("bounded_harness"), TEXT("Fixed 0.050 second world ticks; deadline eight ticks.")));
		Boundaries.Add(MakeBoundary(TEXT("external_service"), TEXT("not_present"), TEXT("The bounded fixture invokes no external service.")));
		Boundaries.Add(MakeBoundary(TEXT("cancellation"), TEXT("supported_negative_boundary"), TEXT("Cancelled or destroyed invocations cannot produce positive evidence.")));
		Root->SetArrayField(TEXT("boundaries"), Boundaries);
		TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
		Bounds->SetNumberField(TEXT("fixed_world_delta_seconds"), 0.05);
		Bounds->SetNumberField(TEXT("deadline_ticks"), 8);
		Bounds->SetNumberField(TEXT("trace_capacity"), 64);
		Root->SetObjectField(TEXT("trace_bounds"), Bounds);

		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			OutError = TEXT("Could not serialize LC4-ASYNC source facts.");
			return false;
		}
		OutFilePath = JsonOutputPath(Blueprint);
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFilePath), true)
			|| !FFileHelper::SaveStringToFile(
				JsonText,
				*OutFilePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write LC4-ASYNC source facts: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}
