// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensIntraBpPureAudit.h"

#include "BlueprintLensSequenceFacts.h"

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
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace BlueprintLensIntraBpPureAudit
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

		UK2Node_CallFunction* FindCallIndependently(
			const UBlueprint& Blueprint,
			const FString& CallNodeId,
			UEdGraph*& OutGraph,
			FString& OutError)
		{
			TArray<UEdGraph*> Graphs;
			Blueprint.GetAllGraphs(Graphs);
			UK2Node_CallFunction* Result = nullptr;
			int32 Matches = 0;
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
						++Matches;
						Result = Call;
						OutGraph = Graph;
					}
				}
			}
			if (Matches != 1 || Result == nullptr || OutGraph == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC5 audit call site resolution failed: %s (matches=%d)"),
					*CallNodeId,
					Matches);
				return nullptr;
			}
			return Result;
		}

		struct FAuditedTarget
		{
			const UFunction* Function = nullptr;
			UEdGraph* Graph = nullptr;
			UK2Node_FunctionEntry* Entry = nullptr;
			UK2Node_FunctionResult* Result = nullptr;
			int32 CandidateCount = 0;
		};

		bool AuditTarget(
			const UBlueprint& Blueprint,
			const UK2Node_CallFunction& Call,
			FAuditedTarget& Out,
			FString& OutError)
		{
			const UFunction* ReferenceFunction =
				Call.FunctionReference.ResolveMember<UFunction>(Blueprint.GeneratedClass);
			const UFunction* ApiFunction = Call.GetTargetFunction();
			const FName FunctionName = ApiFunction == nullptr
				? NAME_None
				: ApiFunction->GetFName();
			Out.Function = Blueprint.GeneratedClass == nullptr || FunctionName.IsNone()
				? nullptr
				: Blueprint.GeneratedClass->FindFunctionByName(FunctionName);
			if (ReferenceFunction == nullptr || ApiFunction == nullptr || Out.Function == nullptr
				|| ReferenceFunction->GetFName() != FunctionName
				|| Out.Function->GetFName() != FunctionName
				|| Out.Function->HasAnyFunctionFlags(FUNC_BlueprintPure)
					!= ApiFunction->HasAnyFunctionFlags(FUNC_BlueprintPure)
				|| Out.Function->HasMetaData(TEXT("Latent"))
					!= ApiFunction->HasMetaData(TEXT("Latent")))
			{
				OutError = TEXT("LC5 audit reference/API/generated UFunction identities do not agree.");
				return false;
			}
			FGuid GeneratedFunctionGuid;
			if (!UBlueprint::GetGuidFromClassByFieldName<UFunction>(
				Blueprint.GeneratedClass,
				Out.Function->GetFName(),
				GeneratedFunctionGuid)
				|| GeneratedFunctionGuid != Call.FunctionReference.GetMemberGuid())
			{
				OutError = TEXT("LC5 audit generated function GUID differs from reference.");
				return false;
			}
			for (const TObjectPtr<UEdGraph>& GraphValue : Blueprint.FunctionGraphs)
			{
				UEdGraph* Graph = GraphValue.Get();
				if (Graph == nullptr || Graph->GetFName() != Out.Function->GetFName())
				{
					continue;
				}
				UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(
					FBlueprintEditorUtils::GetEntryNode(Graph));
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
				if (Entry != nullptr && ResultCount == 1 && Result != nullptr)
				{
					++Out.CandidateCount;
					Out.Graph = Graph;
					Out.Entry = Entry;
					Out.Result = Result;
				}
			}
			if (Out.CandidateCount != 1 || Out.Graph == nullptr || Out.Entry == nullptr || Out.Result == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC5 audit target candidate count is not one: %d"),
					Out.CandidateCount);
				return false;
			}
			return true;
		}

		UEdGraphPin* MatchPin(
			UEdGraphNode& Node,
			const FProperty& Property,
			const EEdGraphPinDirection Direction,
			const FEdGraphPinType& Type)
		{
			UEdGraphPin* Pin = Node.FindPin(Property.GetFName(), Direction);
			return Pin != nullptr && Pin->PinType == Type ? Pin : nullptr;
		}
	}

	bool AuditIntraBpPureCall(
		const UBlueprint& Blueprint,
		const FString& CallNodeId,
		const FString& RawExportPath,
		FString& OutFilePath,
		FIntraBpPureAuditStats& OutStats,
		FString& OutError)
	{
		OutStats = FIntraBpPureAuditStats();
		OutFilePath.Reset();
		OutError.Reset();
		if (Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
		{
			OutError = TEXT("LC5 audit refuses a stale Blueprint compile state.");
			return false;
		}
		UEdGraph* CallGraph = nullptr;
		UK2Node_CallFunction* Call = FindCallIndependently(Blueprint, CallNodeId, CallGraph, OutError);
		FAuditedTarget Target;
		if (Call == nullptr || !AuditTarget(Blueprint, *Call, Target, OutError))
		{
			return false;
		}
		if (!Call->FunctionReference.IsSelfContext()
			|| Target.Function->GetOwnerClass() != Blueprint.GeneratedClass
			|| !Target.Function->HasAnyFunctionFlags(FUNC_BlueprintPure)
			|| Target.Function->HasMetaData(TEXT("Latent")))
		{
			OutError = TEXT("LC5 audit target is outside the first profile.");
			return false;
		}

		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		const FString AssetSha256 = Sha256File(AssetPath);
		const FString RawSha256 = Sha256File(RawExportPath);
		if (AssetSha256.IsEmpty() || RawSha256.IsEmpty())
		{
			OutError = TEXT("LC5 audit could not independently hash source products.");
			return false;
		}

		TArray<FString> Lines;
		Lines.Add(TEXT("FORMAT\tblueprint-lens-intra-bp-pure-call-audit\t1.0.0"));
		Lines.Add(FString::Printf(TEXT("BLUEPRINT\t%s"), *Blueprint.GetPathName()));
		Lines.Add(FString::Printf(
			TEXT("COMPILE\tup_to_date\t%s\t%s\t%s\t%s"),
			*GuidText(Blueprint.GetOutermost()->GetPersistentGuid()),
			*Blueprint.GeneratedClass->GetPathName(),
			*AssetSha256,
			*RawSha256));
		Lines.Add(FString::Printf(TEXT("CALL\t%s\t%s"), *CallGraph->GetPathName(), *CallNodeId));
		Lines.Add(FString::Printf(
			TEXT("REFERENCE\t%s\t%s\t%s\t%d"),
			*Call->FunctionReference.GetMemberName().ToString(),
			*GuidText(Call->FunctionReference.GetMemberGuid()),
			Call->FunctionReference.GetMemberParentClass() == nullptr
				? TEXT("")
				: *Call->FunctionReference.GetMemberParentClass()->GetPathName(),
			Call->FunctionReference.IsSelfContext() ? 1 : 0));
		Lines.Add(FString::Printf(
			TEXT("TARGET\t%s\t%s\t%s\t%s\t%d\t%d"),
			*Target.Function->GetPathName(),
			*Target.Function->GetOwnerClass()->GetPathName(),
			*Target.Function->GetName(),
			*GuidText(Call->FunctionReference.GetMemberGuid()),
			Target.Function->HasAnyFunctionFlags(FUNC_BlueprintPure) ? 1 : 0,
			Target.Function->HasMetaData(TEXT("Latent")) ? 1 : 0));
		Lines.Add(FString::Printf(
			TEXT("GRAPH\t%s\t%s\t%s\t%s\t%s"),
			*Target.Graph->GetPathName(),
			*GuidText(Target.Graph->GraphGuid),
			*Blueprint.GetPathName(),
			*BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *Target.Entry),
			*BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *Target.Result)));
		Lines.Add(FString::Printf(TEXT("CANDIDATES\t%d"), Target.CandidateCount));

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		int32 Ordinal = 0;
		for (TFieldIterator<FProperty> It(Target.Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bInput = !Property->HasAnyPropertyFlags(CPF_ReturnParm)
				&& (!Property->HasAnyPropertyFlags(CPF_OutParm)
					|| Property->HasAnyPropertyFlags(CPF_ReferenceParm));
			FEdGraphPinType Type;
			if (!Schema->ConvertPropertyToPinType(Property, Type))
			{
				OutError = FString::Printf(TEXT("LC5 audit property type conversion failed: %s"), *Property->GetPathName());
				return false;
			}
			UEdGraphPin* CallPin = MatchPin(
				*Call,
				*Property,
				bInput ? EGPD_Input : EGPD_Output,
				Type);
			UEdGraphNode* FormalNode = bInput
				? static_cast<UEdGraphNode*>(Target.Entry)
				: static_cast<UEdGraphNode*>(Target.Result);
			UEdGraphPin* FormalPin = MatchPin(
				*FormalNode,
				*Property,
				bInput ? EGPD_Output : EGPD_Input,
				Type);
			if (CallPin == nullptr || FormalPin == nullptr)
			{
				OutError = FString::Printf(TEXT("LC5 audit property/pin mismatch: %s"), *Property->GetPathName());
				return false;
			}
			const FString CallPinId = BlueprintLensSequenceFacts::MakePinId(CallNodeId, *CallPin);
			const FString FormalNodeId = BlueprintLensSequenceFacts::MakeNodeId(Target.Graph->GetPathName(), *FormalNode);
			const FString FormalPinId = BlueprintLensSequenceFacts::MakePinId(FormalNodeId, *FormalPin);
			Lines.Add(FString::Printf(
				TEXT("BINDING\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%s\t%s"),
				Ordinal++,
				bInput ? TEXT("argument") : TEXT("result"),
				*Property->GetPathName(),
				*Property->GetName(),
				bInput ? TEXT("input") : TEXT("return"),
				*Property->GetCPPType(),
				*Type.PinCategory.ToString(),
				*Type.PinSubCategory.ToString(),
				Type.PinSubCategoryObject.IsValid() ? *Type.PinSubCategoryObject->GetPathName() : TEXT(""),
				*ContainerText(Type.ContainerType),
				Type.bIsReference ? 1 : 0,
				Type.bIsConst ? 1 : 0,
				*CallPinId,
				*FormalPinId));
		}
		OutStats.CandidateCount = Target.CandidateCount;
		OutStats.BindingCount = Ordinal;
		Lines.Add(FString::Printf(TEXT("COUNTS\t%d"), OutStats.BindingCount));
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("IntraBpPureAudits"),
			FString::Printf(TEXT("%s.intra-bp-pure-audit.tsv"), *Blueprint.GetName())));
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFilePath), true)
			|| !FFileHelper::SaveStringArrayToFile(
				Lines,
				*OutFilePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("LC5 audit write failed: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}
