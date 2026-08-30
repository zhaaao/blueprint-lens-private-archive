// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC4AsyncFixture.h"

#include "BlueprintLensAsyncBarrier.h"
#include "BlueprintLensSequenceFacts.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensLC4AsyncFixture, Log, All);

namespace BlueprintLensLC4AsyncFixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/LensCorpus/BP_LC4_AsyncBarrier");
		constexpr TCHAR AssetName[] = TEXT("BP_LC4_AsyncBarrier");
		constexpr TCHAR InvocationId[] = TEXT("LC4_RUN");

		UEdGraphNode* SpawnFromTemplate(
			UEdGraph& Graph,
			UK2Node& NodeTemplate,
			const FVector2f& Position)
		{
			FEdGraphSchemaAction_K2NewNode Action;
			Action.NodeTemplate = &NodeTemplate;
			return Action.PerformAction(&Graph, nullptr, Position, false);
		}

		UK2Node_CallFunction* SpawnCall(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			UClass& OwnerClass,
			const FName FunctionName,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(FunctionName, &OwnerClass);
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = FString::Printf(TEXT("Could not spawn function %s."), *FunctionName.ToString());
				return nullptr;
			}
			return Node;
		}

		bool LinkPins(
			UEdGraphPin* Source,
			UEdGraphPin* Target,
			const TCHAR* Description,
			FString& OutError)
		{
			if (Source == nullptr || Target == nullptr)
			{
				OutError = FString::Printf(TEXT("Missing pin for %s."), Description);
				return false;
			}
			Source->MakeLinkTo(Target);
			if (!Source->LinkedTo.Contains(Target) || !Target->LinkedTo.Contains(Source))
			{
				OutError = FString::Printf(TEXT("Could not link %s."), Description);
				return false;
			}
			return true;
		}

		UEdGraphPin* ExecInput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		}

		UEdGraphPin* ExecOutput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}

		UK2Node_CallFunction* FollowSingleExec(
			UEdGraphNode& Node,
			const FName ExpectedFunction,
			FString& OutError)
		{
			UEdGraphPin* ThenPin = ExecOutput(Node);
			UK2Node_CallFunction* Target = ThenPin == nullptr || ThenPin->LinkedTo.Num() != 1
				? nullptr
				: Cast<UK2Node_CallFunction>(ThenPin->LinkedTo[0]->GetOwningNode());
			const UFunction* Function = Target == nullptr ? nullptr : Target->GetTargetFunction();
			if (Function == nullptr || Function->GetFName() != ExpectedFunction)
			{
				OutError = FString::Printf(
					TEXT("Node %s must uniquely lead to %s."),
					*Node.GetName(),
					*ExpectedFunction.ToString());
				return nullptr;
			}
			return Target;
		}

		bool SetDefault(UEdGraphNode& Node, const FName PinName, const FString& Value, FString& OutError)
		{
			UEdGraphPin* Pin = Node.FindPin(PinName, EGPD_Input);
			if (Pin == nullptr)
			{
				OutError = FString::Printf(
					TEXT("Node %s has no input pin %s."),
					*Node.GetName(),
					*PinName.ToString());
				return false;
			}
			Pin->DefaultValue = Value;
			return true;
		}

		bool InspectFixture(
			UBlueprint& Blueprint,
			FString& OutSequenceNodeId,
			FString& OutCriterionNodeId,
			FString& OutError)
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
			if (Graph == nullptr)
			{
				OutError = TEXT("LC4-ASYNC fixture has no EventGraph.");
				return false;
			}
			UK2Node_ExecutionSequence* Sequence = nullptr;
			UK2Node_VariableSet* Criterion = nullptr;
			UK2Node_CallFunction* CriterionRecord = nullptr;
			int32 SequenceCount = 0;
			int32 DelayCount = 0;
			int32 ArrivalCount = 0;
			int32 CriterionCount = 0;
			int32 CriterionRecordCount = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_ExecutionSequence* Candidate = Cast<UK2Node_ExecutionSequence>(Node))
				{
					++SequenceCount;
					Sequence = Candidate;
				}
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					if (Function == nullptr)
					{
						continue;
					}
					DelayCount += Function->GetFName() == TEXT("Delay") ? 1 : 0;
					ArrivalCount += Function->GetFName() == TEXT("ArriveAtLC4Barrier") ? 1 : 0;
					if (Function->GetFName() == TEXT("RecordLC4AsyncCriterion"))
					{
						++CriterionRecordCount;
						CriterionRecord = Call;
					}
				}
				if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
				{
					if (SetNode->GetVarName() == TEXT("LC4AsyncComplete"))
					{
						++CriterionCount;
						Criterion = SetNode;
					}
				}
			}
			if (SequenceCount != 1 || Sequence == nullptr || DelayCount != 2
				|| ArrivalCount != 2 || CriterionCount != 1 || Criterion == nullptr
				|| CriterionRecordCount != 1 || CriterionRecord == nullptr
				|| FindFProperty<FBoolProperty>(Blueprint.GeneratedClass, TEXT("LC4AsyncComplete")) == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC4-ASYNC fixture shape mismatch (sequence=%d delay=%d arrival=%d set=%d record=%d)."),
					SequenceCount,
					DelayCount,
					ArrivalCount,
					CriterionCount,
					CriterionRecordCount);
				return false;
			}
			if (Sequence->GetThenPinGivenIndex(0) == nullptr
				|| Sequence->GetThenPinGivenIndex(1) == nullptr
				|| Sequence->GetThenPinGivenIndex(2) != nullptr)
			{
				OutError = TEXT("LC4-ASYNC fixture must declare exactly two Sequence outputs.");
				return false;
			}
			UEdGraphPin* CriterionValue = Criterion->FindPin(TEXT("LC4AsyncComplete"), EGPD_Input);
			if (CriterionValue == nullptr
				|| !CriterionValue->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				OutError = TEXT("LC4-ASYNC canonical Set must assign LC4AsyncComplete=true.");
				return false;
			}
			UEdGraphPin* SetThen = ExecOutput(*Criterion);
			UEdGraphPin* RecordExecute = ExecInput(*CriterionRecord);
			if (SetThen == nullptr || RecordExecute == nullptr
				|| SetThen->LinkedTo.Num() != 1 || SetThen->LinkedTo[0] != RecordExecute
				|| RecordExecute->LinkedTo.Num() != 1 || RecordExecute->LinkedTo[0] != SetThen)
			{
				OutError = TEXT("LC4-ASYNC canonical Set must uniquely precede criterion Record.");
				return false;
			}
			const FString GraphId = Graph->GetPathName();
			OutSequenceNodeId = BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Sequence);
			OutCriterionNodeId = BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Criterion);
			return true;
		}
	}

	bool EnsureFixture(
		FString& OutAssetObjectPath,
		FString& OutSequenceNodeId,
		FString& OutCriterionNodeId,
		FString& OutError)
	{
		OutAssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		OutSequenceNodeId.Reset();
		OutCriterionNodeId.Reset();
		OutError.Reset();
		UPackage* Package = nullptr;
		UBlueprint* Blueprint = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Blueprint = LoadObject<UBlueprint>(nullptr, *OutAssetObjectPath);
			if (Blueprint != nullptr
				&& InspectFixture(*Blueprint, OutSequenceNodeId, OutCriterionNodeId, OutError))
			{
				return true;
			}
			if (Blueprint == nullptr)
			{
				OutError = FString::Printf(TEXT("Could not load existing package %s."), PackageName);
				return false;
			}
			Package = Blueprint->GetOutermost();
			OutError.Reset();
		}
		else
		{
			Package = CreatePackage(PackageName);
			if (Package == nullptr)
			{
				OutError = FString::Printf(TEXT("Could not create package %s."), PackageName);
				return false;
			}
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				AActor::StaticClass(),
				Package,
				FName(AssetName),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				FName(TEXT("BlueprintLensLC4AsyncFixture")));
			if (Blueprint == nullptr)
			{
				OutError = TEXT("Could not create LC4-ASYNC Actor Blueprint.");
				return false;
			}
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, TEXT("LC4AsyncComplete")) == INDEX_NONE)
		{
			FEdGraphPinType BoolType;
			BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			if (!FBlueprintEditorUtils::AddMemberVariable(
				Blueprint,
				TEXT("LC4AsyncComplete"),
				BoolType,
				TEXT("false")))
			{
				OutError = TEXT("Could not add LC4AsyncComplete bool property.");
				return false;
			}
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("Created LC4-ASYNC Blueprint has no EventGraph.");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("Could not clear the LC4-ASYNC EventGraph.");
				return false;
			}
		}

		UEdGraph* TemplateOuter = NewObject<UEdGraph>(Blueprint);
		TemplateOuter->SetFlags(RF_Transient);
		UK2Node_Event* EventTemplate = NewObject<UK2Node_Event>(TemplateOuter);
		EventTemplate->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
		EventTemplate->bOverrideFunction = true;
		UK2Node_Event* BeginPlay = Cast<UK2Node_Event>(
			SpawnFromTemplate(*Graph, *EventTemplate, FVector2f(-1300.0f, 0.0f)));
		UK2Node_CallFunction* BeginInvocation = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("BeginLC4AsyncInvocation"), FVector2f(-1050.0f, 0.0f), OutError);
		UK2Node_ExecutionSequence* SequenceTemplate = NewObject<UK2Node_ExecutionSequence>(TemplateOuter);
		UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(
			SpawnFromTemplate(*Graph, *SequenceTemplate, FVector2f(-750.0f, 0.0f)));
		if (BeginPlay == nullptr || BeginInvocation == nullptr || Sequence == nullptr)
		{
			OutError = TEXT("Could not spawn LC4-ASYNC roots.");
			return false;
		}
		Sequence->NodeComment = TEXT("LC4 async source launch order: A then B");

		UK2Node_CallFunction* LaunchA = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("RecordLC4AsyncLaunch"), FVector2f(-450.0f, -300.0f), OutError);
		UK2Node_CallFunction* LaunchB = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("RecordLC4AsyncLaunch"), FVector2f(-450.0f, 200.0f), OutError);
		UK2Node_CallFunction* DelayA = SpawnCall(
			*Graph, *TemplateOuter, *UKismetSystemLibrary::StaticClass(),
			TEXT("Delay"), FVector2f(-100.0f, -300.0f), OutError);
		UK2Node_CallFunction* DelayB = SpawnCall(
			*Graph, *TemplateOuter, *UKismetSystemLibrary::StaticClass(),
			TEXT("Delay"), FVector2f(-100.0f, 200.0f), OutError);
		UK2Node_CallFunction* ArriveA = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("ArriveAtLC4Barrier"), FVector2f(250.0f, -300.0f), OutError);
		UK2Node_CallFunction* ArriveB = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("ArriveAtLC4Barrier"), FVector2f(250.0f, 200.0f), OutError);
		UK2Node_VariableSet* CriterionTemplate = NewObject<UK2Node_VariableSet>(TemplateOuter);
		CriterionTemplate->VariableReference.SetSelfMember(TEXT("LC4AsyncComplete"));
		UK2Node_VariableSet* Criterion = Cast<UK2Node_VariableSet>(
			SpawnFromTemplate(*Graph, *CriterionTemplate, FVector2f(650.0f, -50.0f)));
		UK2Node_CallFunction* CriterionRecord = SpawnCall(
			*Graph, *TemplateOuter, *UBlueprintLensAsyncBarrierLibrary::StaticClass(),
			TEXT("RecordLC4AsyncCriterion"), FVector2f(950.0f, -50.0f), OutError);
		if (LaunchA == nullptr || LaunchB == nullptr || DelayA == nullptr || DelayB == nullptr
			|| ArriveA == nullptr || ArriveB == nullptr || Criterion == nullptr || CriterionRecord == nullptr)
		{
			return false;
		}

		BeginInvocation->NodeComment = TEXT("LC4 async invocation controller; participants A and B");
		LaunchA->NodeComment = TEXT("LC4 async launch A");
		LaunchB->NodeComment = TEXT("LC4 async launch B");
		DelayA->NodeComment = TEXT("LC4 async continuation A; schedule A_FIRST duration 0.100");
		DelayB->NodeComment = TEXT("LC4 async continuation B; schedule A_FIRST duration 0.200");
		ArriveA->NodeComment = TEXT("LC4 async barrier participant A");
		ArriveB->NodeComment = TEXT("LC4 async barrier participant B");
		Criterion->NodeComment = TEXT("LC4 async canonical criterion: Set LC4AsyncComplete after release");
		CriterionRecord->NodeComment = TEXT("LC4 async criterion trace record after canonical Set");

		for (UK2Node_CallFunction* Call : {BeginInvocation, LaunchA, LaunchB, ArriveA, ArriveB, CriterionRecord})
		{
			if (!SetDefault(*Call, TEXT("InvocationId"), InvocationId, OutError))
			{
				return false;
			}
		}
		if (!SetDefault(*LaunchA, TEXT("ParticipantId"), TEXT("A"), OutError)
			|| !SetDefault(*LaunchB, TEXT("ParticipantId"), TEXT("B"), OutError)
			|| !SetDefault(*ArriveA, TEXT("ParticipantId"), TEXT("A"), OutError)
			|| !SetDefault(*ArriveB, TEXT("ParticipantId"), TEXT("B"), OutError)
			|| !SetDefault(*DelayA, TEXT("Duration"), TEXT("0.100000"), OutError)
			|| !SetDefault(*DelayB, TEXT("Duration"), TEXT("0.200000"), OutError)
			|| !SetDefault(*Criterion, TEXT("LC4AsyncComplete"), TEXT("true"), OutError))
		{
			return false;
		}

		UEdGraphPin* ReleasedA = ArriveA->FindPin(TEXT("Released"), EGPD_Output);
		UEdGraphPin* ReleasedB = ArriveB->FindPin(TEXT("Released"), EGPD_Output);
		if (!LinkPins(ExecOutput(*BeginPlay), ExecInput(*BeginInvocation), TEXT("BeginPlay -> BeginInvocation"), OutError)
			|| !LinkPins(ExecOutput(*BeginInvocation), ExecInput(*Sequence), TEXT("BeginInvocation -> Sequence"), OutError)
			|| !LinkPins(Sequence->GetThenPinGivenIndex(0), ExecInput(*LaunchA), TEXT("Sequence A -> LaunchA"), OutError)
			|| !LinkPins(ExecOutput(*LaunchA), ExecInput(*DelayA), TEXT("LaunchA -> DelayA"), OutError)
			|| !LinkPins(ExecOutput(*DelayA), ExecInput(*ArriveA), TEXT("DelayA -> ArriveA"), OutError)
			|| !LinkPins(Sequence->GetThenPinGivenIndex(1), ExecInput(*LaunchB), TEXT("Sequence B -> LaunchB"), OutError)
			|| !LinkPins(ExecOutput(*LaunchB), ExecInput(*DelayB), TEXT("LaunchB -> DelayB"), OutError)
			|| !LinkPins(ExecOutput(*DelayB), ExecInput(*ArriveB), TEXT("DelayB -> ArriveB"), OutError)
			|| !LinkPins(ReleasedA, ExecInput(*Criterion), TEXT("released A -> canonical Set"), OutError)
			|| !LinkPins(ReleasedB, ExecInput(*Criterion), TEXT("released B -> canonical Set"), OutError)
			|| !LinkPins(ExecOutput(*Criterion), ExecInput(*CriterionRecord), TEXT("canonical Set -> criterion Record"), OutError))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint->Status == BS_Error)
		{
			OutError = TEXT("LC4-ASYNC Blueprint compilation failed.");
			return false;
		}
		if (!InspectFixture(*Blueprint, OutSequenceNodeId, OutCriterionNodeId, OutError))
		{
			return false;
		}

		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GLog;
		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Could not save LC4-ASYNC fixture to %s."), *Filename);
			return false;
		}
		UE_LOG(
			LogBlueprintLensLC4AsyncFixture,
			Display,
			TEXT("LC4_ASYNC_FIXTURE_SUCCESS asset=\"%s\" sequence=\"%s\" criterion=\"%s\""),
			*OutAssetObjectPath,
			*OutSequenceNodeId,
			*OutCriterionNodeId);
		return true;
	}

	bool ApplyScheduleVariant(
		UBlueprint& Blueprint,
		const FString& ScheduleVariant,
		FString& OutError)
	{
		OutError.Reset();
		if (ScheduleVariant != TEXT("A_FIRST") && ScheduleVariant != TEXT("B_FIRST"))
		{
			OutError = FString::Printf(TEXT("Unsupported LC4-ASYNC schedule variant: %s"), *ScheduleVariant);
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("LC4-ASYNC fixture has no EventGraph for schedule configuration.");
			return false;
		}
		TMap<FString, UK2Node_CallFunction*> DelayByParticipant;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* Launch = Cast<UK2Node_CallFunction>(Node);
			const UFunction* Function = Launch == nullptr ? nullptr : Launch->GetTargetFunction();
			if (Function == nullptr || Function->GetFName() != TEXT("RecordLC4AsyncLaunch"))
			{
				continue;
			}
			const FString ParticipantId = Launch->FindPin(TEXT("ParticipantId"), EGPD_Input) == nullptr
				? FString()
				: Launch->FindPin(TEXT("ParticipantId"), EGPD_Input)->DefaultValue;
			UK2Node_CallFunction* Delay = FollowSingleExec(*Launch, TEXT("Delay"), OutError);
			if (ParticipantId.IsEmpty() || Delay == nullptr)
			{
				return false;
			}
			DelayByParticipant.Add(ParticipantId, Delay);
		}
		UK2Node_CallFunction* DelayA = DelayByParticipant.FindRef(TEXT("A"));
		UK2Node_CallFunction* DelayB = DelayByParticipant.FindRef(TEXT("B"));
		if (DelayA == nullptr || DelayB == nullptr
			|| !SetDefault(
				*DelayA,
				TEXT("Duration"),
				ScheduleVariant == TEXT("A_FIRST") ? TEXT("0.100000") : TEXT("0.200000"),
				OutError)
			|| !SetDefault(
				*DelayB,
				TEXT("Duration"),
				ScheduleVariant == TEXT("A_FIRST") ? TEXT("0.200000") : TEXT("0.100000"),
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("LC4-ASYNC schedule could not resolve both Delay participants.");
			}
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(
			&Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint.Status == BS_Error)
		{
			OutError = FString::Printf(TEXT("LC4-ASYNC %s schedule compilation failed."), *ScheduleVariant);
			return false;
		}
		return true;
	}
}
