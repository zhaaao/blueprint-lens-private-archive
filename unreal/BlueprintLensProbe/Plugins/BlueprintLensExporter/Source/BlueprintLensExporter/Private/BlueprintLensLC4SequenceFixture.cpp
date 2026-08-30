// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC4SequenceFixture.h"

#include "BlueprintLensSequenceFacts.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensLC4SequenceFixture, Log, All);

namespace BlueprintLensLC4SequenceFixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/LensCorpus/BP_LC4_SequenceDisclosure");
		constexpr TCHAR AssetName[] = TEXT("BP_LC4_SequenceDisclosure");

		UEdGraphNode* SpawnFromTemplate(
			UEdGraph& Graph,
			UK2Node& NodeTemplate,
			const FVector2f& Position)
		{
			FEdGraphSchemaAction_K2NewNode Action;
			Action.NodeTemplate = &NodeTemplate;
			return Action.PerformAction(&Graph, nullptr, Position, false);
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

		UK2Node_VariableSet* SpawnBooleanSet(
			UBlueprint& Blueprint,
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName VariableName,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_VariableSet* Template = NewObject<UK2Node_VariableSet>(&TemplateOuter);
			Template->VariableReference.SetSelfMember(VariableName);
			UK2Node_VariableSet* Node = Cast<UK2Node_VariableSet>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(TEXT("Could not spawn Set %s."), *VariableName.ToString());
				return nullptr;
			}
			UEdGraphPin* ValuePin = Node->FindPin(VariableName, EGPD_Input);
			if (ValuePin == nullptr)
			{
				OutError = FString::Printf(TEXT("Set %s has no value pin."), *VariableName.ToString());
				return nullptr;
			}
			ValuePin->DefaultValue = TEXT("true");
			Node->NodeComment = FString::Printf(TEXT("LC4 fixture fact: %s"), *VariableName.ToString());
			return Node;
		}

		UEdGraphPin* ExecInput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		}

		UEdGraphPin* ExecOutput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}
	}

	bool CreateFixture(
		FString& OutAssetObjectPath,
		FString& OutSequenceNodeId,
		FString& OutError)
	{
		OutAssetObjectPath.Reset();
		OutSequenceNodeId.Reset();
		OutError.Reset();
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE enter"));

		if (FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("Refusing to overwrite existing package %s."), PackageName);
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE package_absent"));

		UPackage* Package = CreatePackage(PackageName);
		if (Package == nullptr)
		{
			OutError = FString::Printf(TEXT("Could not create package %s."), PackageName);
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE package_created"));

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Package,
			FName(AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintLensLC4SequenceFixture")));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("Could not create Actor Blueprint.");
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE blueprint_created"));

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("Created Blueprint has no EventGraph.");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("Could not clear the new EventGraph.");
				return false;
			}
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE graph_cleared"));

		const TArray<FName> Variables = {
			TEXT("LC4BranchA1"),
			TEXT("LC4BranchA2"),
			TEXT("LC4BranchB1"),
			TEXT("LC4BranchB2"),
			TEXT("LC4Reconverged"),
			TEXT("LC4Complete"),
			TEXT("LC4SideEffect")};
		FEdGraphPinType BooleanType;
		BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		for (const FName Variable : Variables)
		{
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Variable, BooleanType, TEXT("false")))
			{
				OutError = FString::Printf(TEXT("Could not add member variable %s."), *Variable.ToString());
				return false;
			}
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE variables_added"));

		UEdGraph* TemplateOuter = NewObject<UEdGraph>(Blueprint);
		TemplateOuter->SetFlags(RF_Transient);

		UK2Node_Event* EventTemplate = NewObject<UK2Node_Event>(TemplateOuter);
		EventTemplate->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
		EventTemplate->bOverrideFunction = true;
		UK2Node_Event* BeginPlay = Cast<UK2Node_Event>(
			SpawnFromTemplate(*Graph, *EventTemplate, FVector2f(-1000.0f, 0.0f)));

		UK2Node_ExecutionSequence* SequenceTemplate = NewObject<UK2Node_ExecutionSequence>(TemplateOuter);
		UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(
			SpawnFromTemplate(*Graph, *SequenceTemplate, FVector2f(-700.0f, 0.0f)));
		if (BeginPlay == nullptr || Sequence == nullptr)
		{
			OutError = TEXT("Could not spawn BeginPlay and Sequence roots.");
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE roots_spawned"));
		Sequence->AddInputPin();
		Sequence->AddInputPin();
		Sequence->NodeComment = TEXT("LC4 ordered dispatch root: ordinals 0, 1, 2 connected; ordinal 3 intentionally unconnected");

		UK2Node_VariableSet* BranchA1 = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[0], FVector2f(-400.0f, -300.0f), OutError);
		UK2Node_VariableSet* BranchA2 = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[1], FVector2f(-100.0f, -300.0f), OutError);
		UK2Node_VariableSet* BranchB1 = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[2], FVector2f(-400.0f, 100.0f), OutError);
		UK2Node_VariableSet* BranchB2 = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[3], FVector2f(-100.0f, 100.0f), OutError);
		UK2Node_VariableSet* Reconverged = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[4], FVector2f(250.0f, -100.0f), OutError);
		UK2Node_VariableSet* Complete = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[5], FVector2f(550.0f, -100.0f), OutError);
		UK2Node_VariableSet* SideEffect = SpawnBooleanSet(
			*Blueprint, *Graph, *TemplateOuter, Variables[6], FVector2f(-100.0f, 450.0f), OutError);
		if (BranchA1 == nullptr || BranchA2 == nullptr || BranchB1 == nullptr
			|| BranchB2 == nullptr || Reconverged == nullptr || Complete == nullptr
			|| SideEffect == nullptr)
		{
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE setters_spawned"));

		if (!LinkPins(ExecOutput(*BeginPlay), ExecInput(*Sequence), TEXT("BeginPlay -> Sequence"), OutError)
			|| !LinkPins(Sequence->GetThenPinGivenIndex(0), ExecInput(*BranchA1), TEXT("ordinal 0 -> BranchA1"), OutError)
			|| !LinkPins(ExecOutput(*BranchA1), ExecInput(*BranchA2), TEXT("BranchA1 -> BranchA2"), OutError)
			|| !LinkPins(ExecOutput(*BranchA2), ExecInput(*Reconverged), TEXT("BranchA2 -> Reconverged"), OutError)
			|| !LinkPins(Sequence->GetThenPinGivenIndex(1), ExecInput(*BranchB1), TEXT("ordinal 1 -> BranchB1"), OutError)
			|| !LinkPins(ExecOutput(*BranchB1), ExecInput(*BranchB2), TEXT("BranchB1 -> BranchB2"), OutError)
			|| !LinkPins(ExecOutput(*BranchB2), ExecInput(*Reconverged), TEXT("BranchB2 -> Reconverged"), OutError)
			|| !LinkPins(ExecOutput(*Reconverged), ExecInput(*Complete), TEXT("Reconverged -> Complete"), OutError)
			|| !LinkPins(Sequence->GetThenPinGivenIndex(2), ExecInput(*SideEffect), TEXT("ordinal 2 -> SideEffect"), OutError))
		{
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE links_created"));

		UEdGraphPin* UnconnectedOrdinal = Sequence->GetThenPinGivenIndex(3);
		UEdGraphPin* UnexpectedOrdinal = Sequence->GetThenPinGivenIndex(4);
		if (Graph->Nodes.Num() != 9
			|| UnconnectedOrdinal == nullptr
			|| !UnconnectedOrdinal->LinkedTo.IsEmpty()
			|| UnexpectedOrdinal != nullptr
			|| ExecInput(*Reconverged) == nullptr
			|| ExecInput(*Reconverged)->LinkedTo.Num() != 2)
		{
			OutError = FString::Printf(
				TEXT("Fixture shape assertion failed (nodes=%d, ordinal3=%s, ordinal4=%s, reconvergence_inputs=%d)."),
				Graph->Nodes.Num(),
				UnconnectedOrdinal == nullptr ? TEXT("missing") : TEXT("present"),
				UnexpectedOrdinal == nullptr ? TEXT("absent") : TEXT("present"),
				ExecInput(*Reconverged) == nullptr ? -1 : ExecInput(*Reconverged)->LinkedTo.Num());
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE shape_asserted"));

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE compile_begin"));
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint->Status == BS_Error)
		{
			OutError = TEXT("Blueprint compilation failed.");
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE compile_complete"));

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
			OutError = FString::Printf(TEXT("Could not save fixture package to %s."), *Filename);
			return false;
		}
		UE_LOG(LogBlueprintLensLC4SequenceFixture, Display, TEXT("LC4_SEQUENCE_FIXTURE_STAGE save_complete"));

		OutAssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		OutSequenceNodeId = BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *Sequence);
		return true;
	}
}
