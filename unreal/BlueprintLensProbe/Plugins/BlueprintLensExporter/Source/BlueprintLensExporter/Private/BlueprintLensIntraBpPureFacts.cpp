// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensIntraBpPureFacts.h"

#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace BlueprintLensIntraBpPureFacts
{
	namespace
	{
		FString GuidText(const FGuid& Guid)
		{
			return Guid.IsValid()
				? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
				: FString();
		}

		FString Sha256File(const FString& Path)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				return FString();
			}
			TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		FString ContainerText(const EPinContainerType Value)
		{
			switch (Value)
			{
			case EPinContainerType::Array: return TEXT("array");
			case EPinContainerType::Set: return TEXT("set");
			case EPinContainerType::Map: return TEXT("map");
			case EPinContainerType::None:
			default: return TEXT("none");
			}
		}

		TSharedPtr<FJsonObject> PinTypeJson(const FEdGraphPinType& Type)
		{
			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("category"), Type.PinCategory.ToString());
			Json->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
			Json->SetStringField(
				TEXT("object_path"),
				Type.PinSubCategoryObject.IsValid()
					? Type.PinSubCategoryObject->GetPathName()
					: FString());
			Json->SetStringField(TEXT("container"), ContainerText(Type.ContainerType));
			Json->SetBoolField(TEXT("is_reference"), Type.bIsReference);
			Json->SetBoolField(TEXT("is_const"), Type.bIsConst);
			return Json;
		}

		UK2Node_CallFunction* FindCall(
			const UBlueprint& Blueprint,
			const FString& CallNodeId,
			UEdGraph*& OutGraph,
			FString& OutError)
		{
			TArray<UEdGraph*> Graphs;
			Blueprint.GetAllGraphs(Graphs);
			UK2Node_CallFunction* Match = nullptr;
			int32 MatchCount = 0;
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph == nullptr)
				{
					continue;
				}
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
					if (Call != nullptr
						&& BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *Call) == CallNodeId)
					{
						++MatchCount;
						Match = Call;
						OutGraph = Graph;
					}
				}
			}
			if (MatchCount != 1 || Match == nullptr || OutGraph == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC5 call site must resolve exactly once: %s (matches=%d)"),
					*CallNodeId,
					MatchCount);
				return nullptr;
			}
			return Match;
		}

		struct FResolvedTarget
		{
			const UFunction* Function = nullptr;
			UEdGraph* Graph = nullptr;
			UK2Node_FunctionEntry* Entry = nullptr;
			UK2Node_FunctionResult* Result = nullptr;
		};

		bool ResolveTarget(
			const UBlueprint& Blueprint,
			const UK2Node_CallFunction& Call,
			FResolvedTarget& Out,
			FString& OutError)
		{
			const UFunction* ResolvedFunction = Call.GetTargetFunction();
			Out.Function = ResolvedFunction == nullptr || Blueprint.GeneratedClass == nullptr
				? nullptr
				: Blueprint.GeneratedClass->FindFunctionByName(ResolvedFunction->GetFName());
			if (ResolvedFunction == nullptr || Out.Function == nullptr
				|| !Call.FunctionReference.IsSelfContext()
				|| Out.Function->GetOwnerClass() != Blueprint.GeneratedClass
				|| Out.Function->GetFName() != ResolvedFunction->GetFName()
				|| Out.Function->HasAnyFunctionFlags(FUNC_BlueprintPure)
					!= ResolvedFunction->HasAnyFunctionFlags(FUNC_BlueprintPure)
				|| Out.Function->HasMetaData(TEXT("Latent"))
					!= ResolvedFunction->HasMetaData(TEXT("Latent")))
			{
				OutError = TEXT("LC5 resolved and generated UFunction identities/properties do not agree.");
				return false;
			}
			FGuid GeneratedFunctionGuid;
			if (!UBlueprint::GetGuidFromClassByFieldName<UFunction>(
				Blueprint.GeneratedClass,
				Out.Function->GetFName(),
				GeneratedFunctionGuid)
				|| GeneratedFunctionGuid != Call.FunctionReference.GetMemberGuid())
			{
				OutError = TEXT("LC5 generated-class function GUID differs from call reference.");
				return false;
			}
			int32 CandidateCount = 0;
			for (const TObjectPtr<UEdGraph>& GraphValue : Blueprint.FunctionGraphs)
			{
				UEdGraph* Graph = GraphValue.Get();
				UK2Node_FunctionEntry* Entry = Graph == nullptr
					? nullptr
					: Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
				if (Graph == nullptr || Entry == nullptr
					|| Graph->GetFName() != Out.Function->GetFName())
				{
					continue;
				}
				UK2Node_FunctionResult* Result = nullptr;
				int32 ResultCount = 0;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (UK2Node_FunctionResult* Candidate = Cast<UK2Node_FunctionResult>(Node))
					{
						++ResultCount;
						Result = Candidate;
					}
				}
				if (ResultCount != 1 || Result == nullptr)
				{
					continue;
				}
				++CandidateCount;
				Out.Graph = Graph;
				Out.Entry = Entry;
				Out.Result = Result;
			}
			if (CandidateCount != 1 || Out.Graph == nullptr || Out.Entry == nullptr || Out.Result == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC5 target graph must resolve uniquely by UFunction/reference identity (candidates=%d)."),
					CandidateCount);
				return false;
			}
			return true;
		}

		UEdGraphPin* FindTypedPin(
			UEdGraphNode& Node,
			const FName Name,
			const EEdGraphPinDirection Direction,
			const FEdGraphPinType& ExpectedType)
		{
			UEdGraphPin* Pin = Node.FindPin(Name, Direction);
			return Pin != nullptr && Pin->PinType == ExpectedType ? Pin : nullptr;
		}
	}

	bool ExportIntraBpPureCallFacts(
		const UBlueprint& Blueprint,
		const FString& CallNodeId,
		const FString& RawExportPath,
		FString& OutFilePath,
		FIntraBpPureFactStats& OutStats,
		FString& OutError)
	{
		OutStats = FIntraBpPureFactStats();
		OutFilePath.Reset();
		OutError.Reset();
		if (Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
		{
			OutError = TEXT("LC5 source Blueprint compile state is not up to date.");
			return false;
		}
		UEdGraph* CallGraph = nullptr;
		UK2Node_CallFunction* Call = FindCall(Blueprint, CallNodeId, CallGraph, OutError);
		FResolvedTarget Target;
		if (Call == nullptr || !ResolveTarget(Blueprint, *Call, Target, OutError))
		{
			return false;
		}
		if (!Target.Function->HasAnyFunctionFlags(FUNC_BlueprintPure)
			|| Target.Function->HasMetaData(TEXT("Latent")))
		{
			OutError = TEXT("LC5 first profile requires a pure, non-latent target.");
			return false;
		}

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		TArray<TSharedPtr<FJsonValue>> Bindings;
		int32 Ordinal = 0;
		for (TFieldIterator<FProperty> It(Target.Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bInput = !Property->HasAnyPropertyFlags(CPF_ReturnParm)
				&& (!Property->HasAnyPropertyFlags(CPF_OutParm)
					|| Property->HasAnyPropertyFlags(CPF_ReferenceParm));
			FEdGraphPinType PropertyPinType;
			if (!Schema->ConvertPropertyToPinType(Property, PropertyPinType))
			{
				OutError = FString::Printf(TEXT("LC5 property type cannot convert to pin type: %s"), *Property->GetPathName());
				return false;
			}
			UEdGraphPin* CallPin = FindTypedPin(
				*Call,
				Property->GetFName(),
				bInput ? EGPD_Input : EGPD_Output,
				PropertyPinType);
			UEdGraphNode* FormalNode = bInput
				? static_cast<UEdGraphNode*>(Target.Entry)
				: static_cast<UEdGraphNode*>(Target.Result);
			UEdGraphPin* FormalPin = FindTypedPin(
				*FormalNode,
				Property->GetFName(),
				bInput ? EGPD_Output : EGPD_Input,
				PropertyPinType);
			if (CallPin == nullptr || FormalPin == nullptr)
			{
				OutError = FString::Printf(TEXT("LC5 property/pin binding mismatch: %s"), *Property->GetPathName());
				return false;
			}
			const FString CallPinId = BlueprintLensSequenceFacts::MakePinId(CallNodeId, *CallPin);
			const FString FormalNodeId = BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *FormalNode);
			const FString FormalPinId = BlueprintLensSequenceFacts::MakePinId(FormalNodeId, *FormalPin);
			TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
			Binding->SetNumberField(TEXT("ordinal"), Ordinal++);
			Binding->SetStringField(TEXT("kind"), bInput ? TEXT("argument") : TEXT("result"));
			TSharedPtr<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
			PropertyJson->SetStringField(TEXT("path"), Property->GetPathName());
			PropertyJson->SetStringField(TEXT("name"), Property->GetName());
			PropertyJson->SetStringField(TEXT("direction"), bInput ? TEXT("input") : TEXT("return"));
			PropertyJson->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
			PropertyJson->SetObjectField(TEXT("pin_type"), PinTypeJson(PropertyPinType));
			Binding->SetObjectField(TEXT("property"), PropertyJson);
			Binding->SetStringField(TEXT("call_pin_id"), CallPinId);
			Binding->SetStringField(TEXT("formal_pin_id"), FormalPinId);
			Bindings.Add(MakeShared<FJsonValueObject>(Binding));
		}

		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		const FString AssetSha256 = Sha256File(AssetPath);
		const FString RawSha256 = Sha256File(RawExportPath);
		if (AssetSha256.IsEmpty() || RawSha256.IsEmpty())
		{
			OutError = TEXT("LC5 source could not hash asset and raw export provenance.");
			return false;
		}

		OutStats.CandidateCount = 1;
		OutStats.BindingCount = Bindings.Num();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("format"), TEXT("blueprint-lens-intra-bp-pure-call-source"));
		Root->SetStringField(TEXT("format_version"), TEXT("1.0.0"));
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Root->SetStringField(TEXT("blueprint_asset_path"), Blueprint.GetPathName());
		Root->SetStringField(TEXT("asset_sha256"), AssetSha256);
		Root->SetStringField(TEXT("raw_sha256"), RawSha256);
		TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
		Compile->SetStringField(TEXT("status"), TEXT("up_to_date"));
		Compile->SetStringField(TEXT("package_guid"), GuidText(Blueprint.GetOutermost()->GetPersistentGuid()));
		Compile->SetStringField(TEXT("generated_class_path"), Blueprint.GeneratedClass->GetPathName());
		Root->SetObjectField(TEXT("compile_provenance"), Compile);
		TSharedPtr<FJsonObject> CallJson = MakeShared<FJsonObject>();
		CallJson->SetStringField(TEXT("graph_id"), CallGraph->GetPathName());
		CallJson->SetStringField(TEXT("node_id"), CallNodeId);
		TSharedPtr<FJsonObject> Reference = MakeShared<FJsonObject>();
		Reference->SetStringField(TEXT("name"), Call->FunctionReference.GetMemberName().ToString());
		Reference->SetStringField(TEXT("guid"), GuidText(Call->FunctionReference.GetMemberGuid()));
		Reference->SetStringField(
			TEXT("parent_class"),
			Call->FunctionReference.GetMemberParentClass() == nullptr
				? FString()
				: Call->FunctionReference.GetMemberParentClass()->GetPathName());
		Reference->SetBoolField(TEXT("is_self_context"), Call->FunctionReference.IsSelfContext());
		CallJson->SetObjectField(TEXT("function_reference"), Reference);
		Root->SetObjectField(TEXT("call_site"), CallJson);
		TSharedPtr<FJsonObject> TargetJson = MakeShared<FJsonObject>();
		TargetJson->SetStringField(TEXT("function_path"), Target.Function->GetPathName());
		TargetJson->SetStringField(TEXT("owner_class_path"), Target.Function->GetOwnerClass()->GetPathName());
		TargetJson->SetStringField(TEXT("name"), Target.Function->GetName());
		TargetJson->SetStringField(TEXT("guid"), GuidText(Call->FunctionReference.GetMemberGuid()));
		TargetJson->SetBoolField(TEXT("is_pure"), Target.Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
		TargetJson->SetBoolField(TEXT("is_latent"), Target.Function->HasMetaData(TEXT("Latent")));
		TargetJson->SetStringField(TEXT("graph_id"), Target.Graph->GetPathName());
		TargetJson->SetStringField(TEXT("graph_guid"), GuidText(Target.Graph->GraphGuid));
		TargetJson->SetStringField(TEXT("owner_blueprint_path"), Blueprint.GetPathName());
		TargetJson->SetStringField(TEXT("entry_node_id"), BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *Target.Entry));
		TargetJson->SetStringField(TEXT("result_node_id"), BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *Target.Result));
		TArray<TSharedPtr<FJsonValue>> Targets;
		Targets.Add(MakeShared<FJsonValueObject>(TargetJson));
		Root->SetArrayField(TEXT("targets"), Targets);
		Root->SetArrayField(TEXT("bindings"), Bindings);

		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			OutError = TEXT("LC5 source facts JSON serialization failed.");
			return false;
		}
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("IntraBpPureFacts"),
			FString::Printf(TEXT("%s.intra-bp-pure-source.json"), *Blueprint.GetName())));
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFilePath), true)
			|| !FFileHelper::SaveStringToFile(JsonText, *OutFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("LC5 source facts write failed: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}
