// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM7CorpusFixture.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensM7CorpusFixture, Log, All);

namespace BlueprintLensM7CorpusFixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/M7Corpus/BP_M7_MotifScale");
		constexpr TCHAR AssetName[] = TEXT("BP_M7_MotifScale");
		constexpr int32 BranchSpineCount = 16;
		constexpr int32 DataClusterCount = 12;
		constexpr int32 LoopClusterCount = 4;
		constexpr int32 LoopBodyCount = 6;
		constexpr int32 BooleanVariableCount = 96;
		constexpr int32 IntegerVariableCount = 80;
		constexpr TCHAR EngineSamplePackageName[] = TEXT("/Game/M7Corpus/BP_M7_EngineSample");
		constexpr TCHAR EngineSampleAssetName[] = TEXT("BP_M7_EngineSample");
		constexpr TCHAR EngineSampleSourceObjectPath[] =
			TEXT("/Engine/Tutorial/BlueprintTutorials/TutorialAssets/Tutorial_BP_Class.Tutorial_BP_Class");

		UEdGraphNode* SpawnFromTemplate(
			UEdGraph& Graph,
			UK2Node& NodeTemplate,
			const FVector2f& Position)
		{
			FEdGraphSchemaAction_K2NewNode Action;
			Action.NodeTemplate = &NodeTemplate;
			return Action.PerformAction(&Graph, nullptr, Position, false);
		}

		UEdGraphPin* ExecInput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		}

		UEdGraphPin* ExecOutput(UEdGraphNode& Node)
		{
			return Node.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}

		bool LinkPins(
			UEdGraphPin* Source,
			UEdGraphPin* Target,
			const TCHAR* Description,
			FString& OutError,
			const bool bDirect = false)
		{
			if (Source == nullptr || Target == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: missing pin for %s"), Description);
				return false;
			}
			if (bDirect)
			{
				Source->MakeLinkTo(Target);
			}
			else
			{
				const UEdGraphSchema* Schema = Source->GetSchema();
				if (Schema == nullptr || !Schema->TryCreateConnection(Source, Target))
				{
					OutError = FString::Printf(
						TEXT("M7_FIXTURE_SHAPE_INVALID: could not link %s"), Description);
					return false;
				}
			}
			if (!Source->LinkedTo.Contains(Target) || !Target->LinkedTo.Contains(Source))
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: link was not retained for %s"), Description);
				return false;
			}
			return true;
		}

		UK2Node_CustomEvent* SpawnEvent(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CustomEvent* Template = NewObject<UK2Node_CustomEvent>(&TemplateOuter);
			Template->CustomFunctionName = TEXT("M7_MOTIF_SCALE");
			UK2Node_CustomEvent* Node = Cast<UK2Node_CustomEvent>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn root event");
				return nullptr;
			}
			Node->CustomFunctionName = TEXT("M7_MOTIF_SCALE");
			bool bRetainedDelegatePin = false;
			for (int32 PinIndex = Node->Pins.Num() - 1; PinIndex >= 0; --PinIndex)
			{
				UEdGraphPin* Pin = Node->Pins[PinIndex];
				if (Pin == nullptr || Pin->PinName != TEXT("OutputDelegate"))
				{
					continue;
				}
				if (!bRetainedDelegatePin)
				{
					bRetainedDelegatePin = true;
					continue;
				}
				Node->RemovePin(Pin);
			}
			Node->NodeComment = TEXT("M7_ROOT_MOTIF_COMPOSITION");
			return Node;
		}

		UK2Node_VariableSet* SpawnSet(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName VariableName,
			const FString& Comment,
			const FVector2f& Position,
			FString& OutError,
			const TCHAR* DefaultValue = TEXT("true"))
		{
			UK2Node_VariableSet* Template = NewObject<UK2Node_VariableSet>(&TemplateOuter);
			Template->VariableReference.SetSelfMember(VariableName);
			UK2Node_VariableSet* Node = Cast<UK2Node_VariableSet>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn Set %s"),
					*VariableName.ToString());
				return nullptr;
			}
			UEdGraphPin* ValuePin = Node->FindPin(VariableName, EGPD_Input);
			if (ValuePin == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: Set %s has no value pin"),
					*VariableName.ToString());
				return nullptr;
			}
			ValuePin->DefaultValue = DefaultValue;
			Node->NodeComment = Comment;
			return Node;
		}

		UK2Node_VariableGet* SpawnGet(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName VariableName,
			const FString& Comment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_VariableGet* Template = NewObject<UK2Node_VariableGet>(&TemplateOuter);
			Template->VariableReference.SetSelfMember(VariableName);
			UK2Node_VariableGet* Node = Cast<UK2Node_VariableGet>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->FindPin(VariableName, EGPD_Output) == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn Get %s"),
					*VariableName.ToString());
				return nullptr;
			}
			Node->NodeComment = Comment;
			return Node;
		}

		UK2Node_IfThenElse* SpawnBranch(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FString& Comment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_IfThenElse* Template = NewObject<UK2Node_IfThenElse>(&TemplateOuter);
			UK2Node_IfThenElse* Node = Cast<UK2Node_IfThenElse>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn branch %s"), *Comment);
				return nullptr;
			}
			Node->NodeComment = Comment;
			return Node;
		}

		UK2Node_CallFunction* SpawnMath(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName FunctionName,
			const FString& Comment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(
				FunctionName, UKismetMathLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn math call %s"),
					*FunctionName.ToString());
				return nullptr;
			}
			Node->NodeComment = Comment;
			return Node;
		}

		UK2Node_CallFunction* SpawnExecutionCall(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName FunctionName,
			UClass* FunctionClass,
			const FString& Comment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(FunctionName, FunctionClass);
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr
				|| ExecInput(*Node) == nullptr || ExecOutput(*Node) == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn execution call %s"),
					*FunctionName.ToString());
				return nullptr;
			}
			if (UEdGraphPin* TextPin = Node->FindPin(TEXT("InString"), EGPD_Input))
			{
				TextPin->DefaultValue = Comment;
			}
			Node->NodeComment = Comment;
			return Node;
		}

		UK2Node_CallFunction* SpawnOpaqueBoundary(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(
				TEXT("Delay"), UKismetSystemLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn unsupported boundary");
				return nullptr;
			}
			if (UEdGraphPin* Duration = Node->FindPin(TEXT("Duration"), EGPD_Input))
			{
				Duration->DefaultValue = TEXT("0.0");
			}
			Node->NodeComment = TEXT("M7_OPAQUE_UNSUPPORTED_BOUNDARY_MOTIF");
			return Node;
		}

		UK2Node_ExecutionSequence* SpawnSequence(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_ExecutionSequence* Template = NewObject<UK2Node_ExecutionSequence>(&TemplateOuter);
			UK2Node_ExecutionSequence* Node = Cast<UK2Node_ExecutionSequence>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not spawn sequence");
				return nullptr;
			}
			Node->NodeComment = TEXT("M7_SEQUENCE_MOTIF");
			return Node;
		}

		int32 CountEdges(const UEdGraph& Graph, const bool bExecution)
		{
			int32 Count = 0;
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output
						|| (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) != bExecution)
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						Count += LinkedPin != nullptr
							&& Graph.Nodes.Contains(LinkedPin->GetOwningNode()) ? 1 : 0;
					}
				}
			}
			return Count;
		}

		bool InspectFixture(
			UBlueprint& Blueprint,
			FFixtureSummary& OutSummary,
			FString& OutError)
		{
			OutSummary = FFixtureSummary();
			OutSummary.AssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
			OutSummary.GraphId = FString::Printf(TEXT("%s:EventGraph"), *OutSummary.AssetObjectPath);
			if (Blueprint.GetPathName() != OutSummary.AssetObjectPath
				|| Blueprint.Status == BS_Error || Blueprint.GeneratedClass == nullptr)
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: asset identity or compile state is invalid");
				return false;
			}
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
			if (Graph == nullptr || Blueprint.UbergraphPages.Num() != 1
				|| Graph->GetPathName() != OutSummary.GraphId)
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: fixture must own one exact EventGraph");
				return false;
			}
			OutSummary.NodeCount = Graph->Nodes.Num();
			OutSummary.ExecutionEdgeCount = CountEdges(*Graph, true);
			OutSummary.DataEdgeCount = CountEdges(*Graph, false);
			if (OutSummary.NodeCount < 250 || OutSummary.NodeCount > 500
				|| OutSummary.ExecutionEdgeCount <= 0 || OutSummary.DataEdgeCount <= 0)
			{
				OutError = FString::Printf(
					TEXT("M7_FIXTURE_SHAPE_INVALID: expected 250-500 nodes and both edge kinds, observed nodes=%d exec=%d data=%d"),
					OutSummary.NodeCount,
					OutSummary.ExecutionEdgeCount,
					OutSummary.DataEdgeCount);
				return false;
			}
			return true;
		}

		bool AddVariables(UBlueprint& Blueprint, FString& OutError)
		{
			FEdGraphPinType BooleanType;
			BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			for (int32 Index = 0; Index < BooleanVariableCount; ++Index)
			{
				const FName Name = FName(*FString::Printf(TEXT("M7Flag%03d"), Index));
				if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, BooleanType, TEXT("false")))
				{
					OutError = FString::Printf(
						TEXT("M7_FIXTURE_SHAPE_INVALID: could not add variable %s"),
					*Name.ToString());
					return false;
				}
			}
			FEdGraphPinType IntegerType;
			IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
			for (int32 Index = 0; Index < IntegerVariableCount; ++Index)
			{
				const FName Name = FName(*FString::Printf(TEXT("M7Counter%03d"), Index));
				if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, IntegerType, TEXT("0")))
				{
					OutError = FString::Printf(
						TEXT("M7_FIXTURE_SHAPE_INVALID: could not add counter %s"),
					*Name.ToString());
					return false;
				}
			}
			return true;
		}

		bool BuildBranchSpine(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			UEdGraphPin* StartPin,
			const int32 Count,
			const int32 VariableOffset,
			const FVector2f& Origin,
			UEdGraphPin*& OutPin,
			FString& OutError)
		{
			UEdGraphPin* Current = StartPin;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				UK2Node_IfThenElse* Branch = SpawnBranch(
					Graph,
					TemplateOuter,
					FString::Printf(TEXT("M7_GUARDED_BRANCH_%03d"), Index),
					FVector2f(Origin.X + Index * 240.0f, Origin.Y),
					OutError);
				const int32 FirstVariable = (VariableOffset + Index * 2) % BooleanVariableCount;
				const int32 SecondVariable = (FirstVariable + 1) % BooleanVariableCount;
				UK2Node_VariableSet* TrueSet = SpawnSet(
					Graph,
					TemplateOuter,
					FName(*FString::Printf(TEXT("M7Flag%03d"), FirstVariable)),
					FString::Printf(TEXT("M7_BRANCH_TRUE_%03d"), Index),
					FVector2f(Origin.X + Index * 240.0f + 80.0f, Origin.Y - 220.0f),
					OutError);
				UK2Node_VariableSet* FalseSet = SpawnSet(
					Graph,
					TemplateOuter,
					FName(*FString::Printf(TEXT("M7Flag%03d"), SecondVariable)),
					FString::Printf(TEXT("M7_BRANCH_FALSE_%03d"), Index),
					FVector2f(Origin.X + Index * 240.0f + 80.0f, Origin.Y + 220.0f),
					OutError);
				UK2Node_CallFunction* MergeCall = SpawnExecutionCall(
					Graph,
					TemplateOuter,
					TEXT("PrintString"),
					UKismetSystemLibrary::StaticClass(),
					FString::Printf(TEXT("M7_BRANCH_MERGE_%03d"), Index),
					FVector2f(Origin.X + Index * 240.0f + 160.0f, Origin.Y),
					OutError);
				if (Branch == nullptr || TrueSet == nullptr || FalseSet == nullptr || MergeCall == nullptr
					|| !LinkPins(Current, ExecInput(*Branch), TEXT("branch spine entry"), OutError)
					|| !LinkPins(
						Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
						ExecInput(*TrueSet), TEXT("branch true path"), OutError)
					|| !LinkPins(
						Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output),
						ExecInput(*FalseSet), TEXT("branch false path"), OutError)
					|| !LinkPins(ExecOutput(*TrueSet), ExecInput(*MergeCall), TEXT("branch true merge"), OutError)
					|| !LinkPins(ExecOutput(*FalseSet), ExecInput(*MergeCall), TEXT("branch false merge"), OutError, true))
				{
					return false;
				}
				Current = ExecOutput(*MergeCall);
			}
			OutPin = Current;
			return OutPin != nullptr;
		}

		bool BuildDataSpine(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			UEdGraphPin* StartPin,
			const int32 ClusterCount,
			const int32 CounterOffset,
			const FVector2f& Origin,
			UEdGraphPin*& OutPin,
			FString& OutError)
		{
			UK2Node_ExecutionSequence* Sequence = SpawnSequence(
				Graph, TemplateOuter, Origin, OutError);
			if (Sequence == nullptr
				|| !LinkPins(StartPin, ExecInput(*Sequence), TEXT("data spine sequence"), OutError))
			{
				return false;
			}
			UEdGraphPin* Current = Sequence->GetThenPinGivenIndex(0);
			for (int32 Index = 0; Index < ClusterCount; ++Index)
			{
				UK2Node_VariableGet* GetA = SpawnGet(
					Graph, TemplateOuter, TEXT("M7Counter000"),
					FString::Printf(TEXT("M7_DATA_CLUSTER_%03d_A"), Index),
					FVector2f(Origin.X + Index * 250.0f, Origin.Y - 240.0f), OutError);
				UK2Node_VariableGet* GetB = SpawnGet(
					Graph, TemplateOuter, TEXT("M7Counter001"),
					FString::Printf(TEXT("M7_DATA_CLUSTER_%03d_B"), Index),
					FVector2f(Origin.X + Index * 250.0f + 80.0f, Origin.Y - 240.0f), OutError);
				UK2Node_CallFunction* Add = SpawnMath(
					Graph, TemplateOuter, TEXT("Add_IntInt"),
					FString::Printf(TEXT("M7_DATA_CLUSTER_%03d_FAN_IN"), Index),
					FVector2f(Origin.X + Index * 250.0f + 160.0f, Origin.Y - 240.0f), OutError);
				TArray<UK2Node_VariableSet*> Targets;
				for (int32 TargetIndex = 0; TargetIndex < 3; ++TargetIndex)
				{
					const int32 CounterIndex = 2 + CounterOffset + Index * 3 + TargetIndex;
					Targets.Add(SpawnSet(
						Graph,
						TemplateOuter,
						FName(*FString::Printf(TEXT("M7Counter%03d"), CounterIndex)),
						FString::Printf(TEXT("M7_DATA_CLUSTER_%03d_FAN_OUT_%d"), Index, TargetIndex),
						FVector2f(Origin.X + Index * 250.0f + TargetIndex * 80.0f, Origin.Y + 180.0f),
						OutError,
						TEXT("0")));
				}
				if (GetA == nullptr || GetB == nullptr || Add == nullptr
					|| Targets.Num() != 3 || Targets.Contains(nullptr)
					|| !LinkPins(Current, ExecInput(*Targets[0]), TEXT("data cluster execution"), OutError)
					|| !LinkPins(ExecOutput(*Targets[0]), ExecInput(*Targets[1]), TEXT("data cluster chain"), OutError)
					|| !LinkPins(ExecOutput(*Targets[1]), ExecInput(*Targets[2]), TEXT("data cluster chain"), OutError)
					|| !LinkPins(
						GetA->FindPin(TEXT("M7Counter000"), EGPD_Output),
						Add->FindPin(TEXT("A"), EGPD_Input), TEXT("data fan-in A"), OutError)
					|| !LinkPins(
						GetB->FindPin(TEXT("M7Counter001"), EGPD_Output),
						Add->FindPin(TEXT("B"), EGPD_Input), TEXT("data fan-in B"), OutError))
				{
					return false;
				}
				for (UK2Node_VariableSet* Target : Targets)
				{
					if (!LinkPins(
						Add->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
						Target->FindPin(Target->VariableReference.GetMemberName(), EGPD_Input),
						TEXT("data fan-out"), OutError))
					{
						return false;
					}
				}
				Current = ExecOutput(*Targets[2]);
			}
			OutPin = Current;
			return OutPin != nullptr;
		}

		bool BuildLoopCluster(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			UEdGraphPin* EntryPin,
			const int32 LoopIndex,
			const int32 VariableOffset,
			const FVector2f& Origin,
			FString& OutError)
		{
			UK2Node_IfThenElse* LoopBranch = SpawnBranch(
				Graph,
				TemplateOuter,
				FString::Printf(TEXT("M7_SEPARATE_SCC_%02d"), LoopIndex),
				Origin,
				OutError);
			if (LoopBranch == nullptr
				|| !LinkPins(EntryPin, ExecInput(*LoopBranch), TEXT("loop cluster entry"), OutError))
			{
				return false;
			}
			UEdGraphPin* Current = LoopBranch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			for (int32 Index = 0; Index < LoopBodyCount; ++Index)
			{
				UK2Node_VariableSet* Body = SpawnSet(
					Graph,
					TemplateOuter,
					FName(*FString::Printf(TEXT("M7Flag%03d"), VariableOffset + Index)),
					FString::Printf(TEXT("M7_SCC_%02d_BODY_%02d"), LoopIndex, Index),
					FVector2f(Origin.X + 220.0f + Index * 220.0f, Origin.Y),
					OutError);
				UK2Node_CallFunction* BodyCall = SpawnExecutionCall(
					Graph,
					TemplateOuter,
					TEXT("PrintString"),
					UKismetSystemLibrary::StaticClass(),
					FString::Printf(TEXT("M7_SCC_%02d_CALL_%02d"), LoopIndex, Index),
					FVector2f(Origin.X + 300.0f + Index * 220.0f, Origin.Y + 180.0f),
					OutError);
				if (Body == nullptr || BodyCall == nullptr
					|| !LinkPins(Current, ExecInput(*Body), TEXT("SCC body entry"), OutError)
					|| !LinkPins(ExecOutput(*Body), ExecInput(*BodyCall), TEXT("SCC body call"), OutError))
				{
					return false;
				}
				Current = ExecOutput(*BodyCall);
			}
			if (!LinkPins(Current, ExecInput(*LoopBranch), TEXT("separate SCC back edge"), OutError, true))
			{
				return false;
			}
			UK2Node_CallFunction* Opaque = SpawnOpaqueBoundary(
				Graph, TemplateOuter, FVector2f(Origin.X + 1600.0f, Origin.Y - 220.0f), OutError);
			UK2Node_VariableSet* Complete = SpawnSet(
				Graph,
				TemplateOuter,
				FName(*FString::Printf(TEXT("M7Flag%03d"), VariableOffset + LoopBodyCount)),
				FString::Printf(TEXT("M7_SCC_%02d_COMPLETION"), LoopIndex),
				FVector2f(Origin.X + 1800.0f, Origin.Y - 220.0f),
				OutError);
			return Opaque != nullptr && Complete != nullptr
				&& LinkPins(
					LoopBranch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output),
					ExecInput(*Opaque), TEXT("SCC exit boundary"), OutError)
				&& LinkPins(ExecOutput(*Opaque), ExecInput(*Complete), TEXT("SCC completion"), OutError);
		}

		bool BuildMotifs(UBlueprint& Blueprint, UEdGraph& Graph, FString& OutError)
		{
			UEdGraph* TemplateOuter = NewObject<UEdGraph>(&Blueprint);
			TemplateOuter->SetFlags(RF_Transient);
			UK2Node_CustomEvent* Root = SpawnEvent(
				Graph, *TemplateOuter, FVector2f(-5000.0f, 0.0f), OutError);
			UK2Node_ExecutionSequence* RootSequence = SpawnSequence(
				Graph, *TemplateOuter, FVector2f(-4700.0f, 0.0f), OutError);
			UK2Node_ExecutionSequence* LeftSequence = SpawnSequence(
				Graph, *TemplateOuter, FVector2f(-4400.0f, -600.0f), OutError);
			UK2Node_ExecutionSequence* RightSequence = SpawnSequence(
				Graph, *TemplateOuter, FVector2f(-4400.0f, 600.0f), OutError);
			if (Root == nullptr || RootSequence == nullptr || LeftSequence == nullptr || RightSequence == nullptr
				|| !LinkPins(ExecOutput(*Root), ExecInput(*RootSequence), TEXT("root -> sequence"), OutError)
				|| !LinkPins(
					RootSequence->GetThenPinGivenIndex(0), ExecInput(*LeftSequence),
					TEXT("root sequence -> left sequence"), OutError)
				|| !LinkPins(
					RootSequence->GetThenPinGivenIndex(1), ExecInput(*RightSequence),
					TEXT("root sequence -> right sequence"), OutError))
			{
				return false;
			}
		UEdGraphPin* BranchEndA = nullptr;
		UEdGraphPin* DataEndA = nullptr;
		UEdGraphPin* BranchEndB = nullptr;
		UEdGraphPin* DataEndB = nullptr;
		if (!BuildBranchSpine(
				Graph, *TemplateOuter, LeftSequence->GetThenPinGivenIndex(0), BranchSpineCount, 0,
				FVector2f(-3900.0f, -900.0f), BranchEndA, OutError)
			|| !BuildDataSpine(
				Graph, *TemplateOuter, LeftSequence->GetThenPinGivenIndex(1), DataClusterCount, 0,
				FVector2f(-3900.0f, -300.0f), DataEndA, OutError)
			|| !BuildBranchSpine(
				Graph, *TemplateOuter, RightSequence->GetThenPinGivenIndex(0), BranchSpineCount, 32,
				FVector2f(-3900.0f, 300.0f), BranchEndB, OutError)
			|| !BuildDataSpine(
				Graph, *TemplateOuter, RightSequence->GetThenPinGivenIndex(1), DataClusterCount, 36,
				FVector2f(-3900.0f, 900.0f), DataEndB, OutError))
		{
			return false;
		}
		return BuildLoopCluster(
				Graph, *TemplateOuter, BranchEndA, 0, 64, FVector2f(600.0f, -1100.0f), OutError)
			&& BuildLoopCluster(
				Graph, *TemplateOuter, DataEndA, 1, 72, FVector2f(600.0f, -350.0f), OutError)
			&& BuildLoopCluster(
				Graph, *TemplateOuter, BranchEndB, 2, 80, FVector2f(600.0f, 350.0f), OutError)
			&& BuildLoopCluster(
				Graph, *TemplateOuter, DataEndB, 3, 88, FVector2f(600.0f, 1100.0f), OutError);
		}

		bool InspectEngineSample(
			UBlueprint& Blueprint,
			FFixtureSummary& OutSummary,
			FString& OutError)
		{
			OutSummary = FFixtureSummary();
			OutSummary.AssetObjectPath = FString::Printf(
				TEXT("%s.%s"), EngineSamplePackageName, EngineSampleAssetName);
			OutSummary.GraphId = FString::Printf(TEXT("%s:EventGraph"), *OutSummary.AssetObjectPath);
			if (Blueprint.GetPathName() != OutSummary.AssetObjectPath
				|| Blueprint.Status == BS_Error || Blueprint.GeneratedClass == nullptr)
			{
				OutError = TEXT("M7_SOURCE_FIXTURE_INVALID: duplicated engine Blueprint is not compiled");
				return false;
			}
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
			if (Graph == nullptr)
			{
				OutError = TEXT("M7_SOURCE_FIXTURE_INVALID: duplicated engine Blueprint has no EventGraph");
				return false;
			}
			OutSummary.GraphId = Graph->GetPathName();
			OutSummary.NodeCount = Graph->Nodes.Num();
			OutSummary.ExecutionEdgeCount = CountEdges(*Graph, true);
			OutSummary.DataEdgeCount = CountEdges(*Graph, false);
			return true;
		}

		bool SaveBlueprint(
			UPackage& Package,
			UBlueprint& Blueprint,
			const TCHAR* PackagePath,
			FString& OutError)
		{
			FAssetRegistryModule::AssetCreated(&Blueprint);
			Package.MarkPackageDirty();
			const FString Filename = FPackageName::LongPackageNameToFilename(
				PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			SaveArgs.Error = GLog;
			if (!UPackage::SavePackage(&Package, &Blueprint, *Filename, SaveArgs))
			{
				OutError = FString::Printf(TEXT("M7_SOURCE_FIXTURE_INVALID: could not save %s"), *Filename);
				return false;
			}
			return true;
		}
	}

	bool EnsureFixture(
		FFixtureSummary& OutSummary,
		EEnsureResult& OutResult,
		FString& OutError)
	{
		OutSummary = FFixtureSummary();
		OutResult = EEnsureResult::Unchanged;
		OutError.Reset();
		const FString AssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
			if (Existing == nullptr || !InspectFixture(*Existing, OutSummary, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not load existing fixture");
				}
				return false;
			}
			return true;
		}

		UPackage* Package = CreatePackage(PackageName);
		if (Package == nullptr)
		{
			OutError = FString::Printf(TEXT("M7_FIXTURE_SHAPE_INVALID: could not create %s"), PackageName);
			return false;
		}
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(), Package, FName(AssetName), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintLensM7CorpusFixture")));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not create Blueprint");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: Blueprint has no EventGraph");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("M7_FIXTURE_SHAPE_INVALID: could not clear EventGraph");
				return false;
			}
		}
		if (!AddVariables(*Blueprint, OutError) || !BuildMotifs(*Blueprint, *Graph, OutError))
		{
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (!InspectFixture(*Blueprint, OutSummary, OutError))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GLog;
		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("M7_FIXTURE_SHAPE_INVALID: could not save %s"), *Filename);
			return false;
		}
		OutResult = EEnsureResult::Created;
		return true;
	}

	bool EnsureEngineSampleFixture(
		FFixtureSummary& OutSummary,
		EEnsureResult& OutResult,
		FString& OutError)
	{
		OutSummary = FFixtureSummary();
		OutResult = EEnsureResult::Unchanged;
		OutError.Reset();
		const FString AssetObjectPath = FString::Printf(
			TEXT("%s.%s"), EngineSamplePackageName, EngineSampleAssetName);
		if (FPackageName::DoesPackageExist(EngineSamplePackageName))
		{
			UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
			return Existing != nullptr && InspectEngineSample(*Existing, OutSummary, OutError);
		}

		UBlueprint* Source = LoadObject<UBlueprint>(nullptr, EngineSampleSourceObjectPath);
		if (Source == nullptr)
		{
			OutError = FString::Printf(
				TEXT("M7_SOURCE_FIXTURE_INVALID: could not load engine source %s"),
				EngineSampleSourceObjectPath);
			return false;
		}
		UPackage* Package = CreatePackage(EngineSamplePackageName);
		if (Package == nullptr)
		{
			OutError = TEXT("M7_SOURCE_FIXTURE_INVALID: could not create engine sample package");
			return false;
		}
		UBlueprint* Blueprint = DuplicateObject<UBlueprint>(
			Source, Package, FName(EngineSampleAssetName));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("M7_SOURCE_FIXTURE_INVALID: could not duplicate engine source Blueprint");
			return false;
		}
		Blueprint->SetFlags(RF_Public | RF_Standalone);
		if (!InspectEngineSample(*Blueprint, OutSummary, OutError)
			|| !SaveBlueprint(*Package, *Blueprint, EngineSamplePackageName, OutError))
		{
			return false;
		}
		OutResult = EEnsureResult::Created;
		return true;
	}
}
