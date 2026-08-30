// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM3Fixture.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensM3Fixture, Log, All);

namespace BlueprintLensM3Fixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/LensCorpus/BP_M3_MultiSCCRisk");
		constexpr TCHAR AssetName[] = TEXT("BP_M3_MultiSCCRisk");
		constexpr TCHAR AuthorCommandName[] = TEXT("BlueprintLens.AuthorM3MultiSCCFixture");

		struct FExpectedLink
		{
			FString SourceComment;
			FName SourcePin;
			FString TargetComment;
			FName TargetPin;
		};

		const TArray<FName> BooleanMembers = {
			TEXT("M3WorkflowStarted"),
			TEXT("M3PriorityParcel"),
			TEXT("M3PriorityRoute"),
			TEXT("M3StandardRoute"),
			TEXT("M3IntakeReady"),
			TEXT("M3IntakeRetry"),
			TEXT("M3IntakeAccepted"),
			TEXT("M3ValidationRework"),
			TEXT("M3ValidationAccepted"),
			TEXT("M3DispatchRecovery"),
			TEXT("M3DispatchAccepted"),
			TEXT("M3ParcelComplete")};

		const TArray<FName> IntegerMembers = {
			TEXT("M3IntakeAttempts"),
			TEXT("M3ValidationAttempts"),
			TEXT("M3DispatchAttempts")};

		UEdGraphNode* SpawnFromTemplate(
			UEdGraph& Graph,
			UK2Node& NodeTemplate,
			const FVector2f& Position)
		{
			FEdGraphSchemaAction_K2NewNode Action;
			Action.NodeTemplate = &NodeTemplate;
			return Action.PerformAction(&Graph, nullptr, Position, false);
		}

		UK2Node_CustomEvent* SpawnCustomEvent(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CustomEvent* Template = NewObject<UK2Node_CustomEvent>(&TemplateOuter);
			Template->CustomFunctionName = TEXT("M3_PARCEL_WORKFLOW");
			UK2Node_CustomEvent* Node = Cast<UK2Node_CustomEvent>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn parcel workflow event");
				return nullptr;
			}
			Node->CustomFunctionName = TEXT("M3_PARCEL_WORKFLOW");
			Node->NodeComment = TEXT("M3_EVENT_PARCEL_WORKFLOW");
			return Node;
		}

		UK2Node_VariableSet* SpawnVariableSet(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName VariableName,
			const FString& DefaultValue,
			const FString& NodeComment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_VariableSet* Template = NewObject<UK2Node_VariableSet>(&TemplateOuter);
			Template->VariableReference.SetSelfMember(VariableName);
			UK2Node_VariableSet* Node = Cast<UK2Node_VariableSet>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn Set %s"),
					*VariableName.ToString());
				return nullptr;
			}
			UEdGraphPin* ValuePin = Node->FindPin(VariableName, EGPD_Input);
			if (ValuePin == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: Set %s has no value pin"),
					*VariableName.ToString());
				return nullptr;
			}
			ValuePin->DefaultValue = DefaultValue;
			Node->NodeComment = NodeComment;
			return Node;
		}

		UK2Node_VariableGet* SpawnVariableGet(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName VariableName,
			const FString& NodeComment,
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
					TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn Get %s"),
					*VariableName.ToString());
				return nullptr;
			}
			Node->NodeComment = NodeComment;
			return Node;
		}

		UK2Node_CallFunction* SpawnMathCall(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName FunctionName,
			const FString& SecondOperand,
			const FString& NodeComment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(FunctionName, UKismetMathLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn %s"),
					*FunctionName.ToString());
				return nullptr;
			}
			UEdGraphPin* B = Node->FindPin(TEXT("B"), EGPD_Input);
			if (B == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: %s has no B input"),
					*FunctionName.ToString());
				return nullptr;
			}
			B->DefaultValue = SecondOperand;
			Node->NodeComment = NodeComment;
			return Node;
		}

		UK2Node_CallFunction* SpawnOpaquePrint(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(
				TEXT("PrintString"), UKismetSystemLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn PrintString boundary");
				return nullptr;
			}
			if (UEdGraphPin* InString = Node->FindPin(TEXT("InString"), EGPD_Input))
			{
				InString->DefaultValue = TEXT("Dispatch recovery remains opaque at core-v1");
			}
			Node->NodeComment = TEXT("M3_OPAQUE_BOUNDARY");
			return Node;
		}

		UK2Node_IfThenElse* SpawnBranch(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FString& NodeComment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_IfThenElse* Template = NewObject<UK2Node_IfThenElse>(&TemplateOuter);
			UK2Node_IfThenElse* Node = Cast<UK2Node_IfThenElse>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: could not spawn branch %s"),
					*NodeComment);
				return nullptr;
			}
			Node->NodeComment = NodeComment;
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
					TEXT("M3_FIXTURE_SHAPE_INVALID: missing pin for %s"), Description);
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
						TEXT("M3_FIXTURE_SHAPE_INVALID: could not link %s"), Description);
					return false;
				}
			}
			if (!Source->LinkedTo.Contains(Target) || !Target->LinkedTo.Contains(Source))
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: link was not retained for %s"), Description);
				return false;
			}
			return true;
		}

		bool PinsAreLinked(const UEdGraphPin* Source, const UEdGraphPin* Target)
		{
			return Source != nullptr && Target != nullptr
				&& Source->LinkedTo.Contains(Target)
				&& Target->LinkedTo.Contains(Source);
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
			if (Blueprint.GetPathName() != FString::Printf(TEXT("%s.%s"), PackageName, AssetName)
				|| Blueprint.Status == BS_Error || Blueprint.GeneratedClass == nullptr)
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: asset identity or compile state is invalid");
				return false;
			}
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
			if (Graph == nullptr || Blueprint.UbergraphPages.Num() != 1
				|| Graph->GetPathName() != FString::Printf(TEXT("%s.%s:EventGraph"), PackageName, AssetName))
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: fixture must own the exact single EventGraph");
				return false;
			}
			if (Graph->Nodes.Num() != 33 || CountEdges(*Graph, true) != 23
				|| CountEdges(*Graph, false) != 13)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: expected nodes=33 exec=23 data=13; found nodes=%d exec=%d data=%d"),
					Graph->Nodes.Num(), CountEdges(*Graph, true), CountEdges(*Graph, false));
				return false;
			}

			TMap<FString, UEdGraphNode*> Nodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node == nullptr || Node->NodeComment.IsEmpty() || Nodes.Contains(Node->NodeComment))
				{
					OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: every node must have one unique semantic label");
					return false;
				}
				Nodes.Add(Node->NodeComment, Node);
			}

			const TArray<FExpectedLink> Links = {
				{TEXT("M3_EVENT_PARCEL_WORKFLOW"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_WORKFLOW_STARTED"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_WORKFLOW_STARTED"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_ROUTE_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_ROUTE_PRIORITY_GET"), TEXT("M3PriorityParcel"),
					TEXT("M3_ROUTE_BRANCH"), UEdGraphSchema_K2::PN_Condition},
				{TEXT("M3_ROUTE_BRANCH"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_ROUTE_PRIORITY"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_ROUTE_BRANCH"), UEdGraphSchema_K2::PN_Else,
					TEXT("M3_ROUTE_STANDARD"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_ROUTE_PRIORITY"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_INTAKE_RECONVERGED"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_ROUTE_STANDARD"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_INTAKE_RECONVERGED"), UEdGraphSchema_K2::PN_Execute},

				{TEXT("M3_INTAKE_RECONVERGED"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_INTAKE_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_INTAKE_PREDICATE_GET"), TEXT("M3IntakeAttempts"),
					TEXT("M3_DATA_INTAKE_LESS"), TEXT("A")},
				{TEXT("M3_DATA_INTAKE_LESS"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_INTAKE_BRANCH"), UEdGraphSchema_K2::PN_Condition},
				{TEXT("M3_SCC_INTAKE_BRANCH"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_INTAKE_RETRY"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_INTAKE_RETRY"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_INTAKE_ADVANCE"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_INTAKE_ADVANCE_GET"), TEXT("M3IntakeAttempts"),
					TEXT("M3_DATA_INTAKE_ADD"), TEXT("A")},
				{TEXT("M3_DATA_INTAKE_ADD"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_INTAKE_ADVANCE"), TEXT("M3IntakeAttempts")},
				{TEXT("M3_SCC_INTAKE_ADVANCE"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_INTAKE_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_INTAKE_BRANCH"), UEdGraphSchema_K2::PN_Else,
					TEXT("M3_INTAKE_ACCEPTED"), UEdGraphSchema_K2::PN_Execute},

				{TEXT("M3_INTAKE_ACCEPTED"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_VALIDATION_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_VALIDATION_PREDICATE_GET"), TEXT("M3ValidationAttempts"),
					TEXT("M3_DATA_VALIDATION_LESS"), TEXT("A")},
				{TEXT("M3_DATA_VALIDATION_LESS"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_VALIDATION_BRANCH"), UEdGraphSchema_K2::PN_Condition},
				{TEXT("M3_SCC_VALIDATION_BRANCH"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_VALIDATION_REWORK"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_VALIDATION_REWORK"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_VALIDATION_ADVANCE"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_VALIDATION_ADVANCE_GET"), TEXT("M3ValidationAttempts"),
					TEXT("M3_DATA_VALIDATION_ADD"), TEXT("A")},
				{TEXT("M3_DATA_VALIDATION_ADD"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_VALIDATION_ADVANCE"), TEXT("M3ValidationAttempts")},
				{TEXT("M3_SCC_VALIDATION_ADVANCE"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_VALIDATION_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_VALIDATION_BRANCH"), UEdGraphSchema_K2::PN_Else,
					TEXT("M3_VALIDATION_ACCEPTED"), UEdGraphSchema_K2::PN_Execute},

				{TEXT("M3_VALIDATION_ACCEPTED"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_DISPATCH_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_DISPATCH_PREDICATE_GET"), TEXT("M3DispatchAttempts"),
					TEXT("M3_DATA_DISPATCH_LESS"), TEXT("A")},
				{TEXT("M3_DATA_DISPATCH_LESS"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_DISPATCH_BRANCH"), UEdGraphSchema_K2::PN_Condition},
				{TEXT("M3_SCC_DISPATCH_BRANCH"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_OPAQUE_BOUNDARY"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_OPAQUE_BOUNDARY"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_DISPATCH_RECOVERY"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_DISPATCH_RECOVERY"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_DISPATCH_ADVANCE"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DATA_DISPATCH_ADVANCE_GET"), TEXT("M3DispatchAttempts"),
					TEXT("M3_DATA_DISPATCH_ADD"), TEXT("A")},
				{TEXT("M3_DATA_DISPATCH_ADD"), UEdGraphSchema_K2::PN_ReturnValue,
					TEXT("M3_SCC_DISPATCH_ADVANCE"), TEXT("M3DispatchAttempts")},
				{TEXT("M3_SCC_DISPATCH_ADVANCE"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_SCC_DISPATCH_BRANCH"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_SCC_DISPATCH_BRANCH"), UEdGraphSchema_K2::PN_Else,
					TEXT("M3_DISPATCH_ACCEPTED"), UEdGraphSchema_K2::PN_Execute},
				{TEXT("M3_DISPATCH_ACCEPTED"), UEdGraphSchema_K2::PN_Then,
					TEXT("M3_PARCEL_COMPLETE"), UEdGraphSchema_K2::PN_Execute}};

			for (const FExpectedLink& Link : Links)
			{
				UEdGraphNode* const* Source = Nodes.Find(Link.SourceComment);
				UEdGraphNode* const* Target = Nodes.Find(Link.TargetComment);
				if (Source == nullptr || Target == nullptr
					|| !PinsAreLinked(
						(*Source)->FindPin(Link.SourcePin, EGPD_Output),
						(*Target)->FindPin(Link.TargetPin, EGPD_Input)))
				{
					OutError = FString::Printf(
						TEXT("M3_FIXTURE_SHAPE_INVALID: missing exact link %s.%s -> %s.%s"),
						*Link.SourceComment,
						*Link.SourcePin.ToString(),
						*Link.TargetComment,
						*Link.TargetPin.ToString());
					return false;
				}
			}

			int32 EventCount = 0;
			int32 BranchCount = 0;
			int32 GetCount = 0;
			int32 SetCount = 0;
			int32 LessCount = 0;
			int32 AddCount = 0;
			int32 PrintCount = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				EventCount += Cast<UK2Node_CustomEvent>(Node) != nullptr ? 1 : 0;
				BranchCount += Cast<UK2Node_IfThenElse>(Node) != nullptr ? 1 : 0;
				GetCount += Cast<UK2Node_VariableGet>(Node) != nullptr ? 1 : 0;
				SetCount += Cast<UK2Node_VariableSet>(Node) != nullptr ? 1 : 0;
				if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					LessCount += Function != nullptr && Function->GetFName() == TEXT("Less_IntInt") ? 1 : 0;
					AddCount += Function != nullptr && Function->GetFName() == TEXT("Add_IntInt") ? 1 : 0;
					PrintCount += Function != nullptr && Function->GetFName() == TEXT("PrintString") ? 1 : 0;
				}
			}
			if (EventCount != 1 || BranchCount != 4 || GetCount != 7 || SetCount != 14
				|| LessCount != 3 || AddCount != 3 || PrintCount != 1)
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: semantic node-role counts differ from the fixture contract");
				return false;
			}

			TMap<FName, FName> ExpectedMembers;
			for (const FName Name : BooleanMembers)
			{
				ExpectedMembers.Add(Name, UEdGraphSchema_K2::PC_Boolean);
			}
			for (const FName Name : IntegerMembers)
			{
				ExpectedMembers.Add(Name, UEdGraphSchema_K2::PC_Int);
			}
			TSet<FGuid> Guids;
			for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
			{
				const FName* ExpectedType = ExpectedMembers.Find(Variable.VarName);
				if (ExpectedType == nullptr || !Variable.VarGuid.IsValid()
					|| Variable.VarType.PinCategory != *ExpectedType)
				{
					OutError = FString::Printf(
						TEXT("M3_FIXTURE_SHAPE_INVALID: unexpected or invalid member %s"),
						*Variable.VarName.ToString());
					return false;
				}
				Guids.Add(Variable.VarGuid);
				ExpectedMembers.Remove(Variable.VarName);
			}
			if (!ExpectedMembers.IsEmpty() || Blueprint.NewVariables.Num() != 15 || Guids.Num() != 15)
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: fixture members are incomplete or duplicate");
				return false;
			}

			OutSummary.AssetObjectPath = Blueprint.GetPathName();
			OutSummary.GraphId = Graph->GetPathName();
			OutSummary.NodeCount = Graph->Nodes.Num();
			OutSummary.ExecutionEdgeCount = CountEdges(*Graph, true);
			OutSummary.DataEdgeCount = CountEdges(*Graph, false);
			return true;
		}

		bool AddFixtureVariables(UBlueprint& Blueprint, FString& OutError)
		{
			FEdGraphPinType BooleanType;
			BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			FEdGraphPinType IntegerType;
			IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
			for (const FName Name : BooleanMembers)
			{
				if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, BooleanType, TEXT("false")))
				{
					OutError = FString::Printf(
						TEXT("M3_FIXTURE_SHAPE_INVALID: could not add boolean member %s"),
						*Name.ToString());
					return false;
				}
			}
			for (const FName Name : IntegerMembers)
			{
				if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, IntegerType, TEXT("0")))
				{
					OutError = FString::Printf(
						TEXT("M3_FIXTURE_SHAPE_INVALID: could not add integer member %s"),
						*Name.ToString());
					return false;
				}
			}
			return true;
		}

		void AuthorFixtureCommand()
		{
			FFixtureSummary Summary;
			EEnsureResult Result = EEnsureResult::Unchanged;
			FString Error;
			if (!EnsureFixture(Summary, Result, Error))
			{
				UE_LOG(LogBlueprintLensM3Fixture, Error, TEXT("%s"), *Error);
				return;
			}
			UE_LOG(
				LogBlueprintLensM3Fixture,
				Display,
				TEXT("M3_MULTI_SCC_FIXTURE_READY result=%s asset=%s graph=%s nodes=%d exec=%d data=%d"),
				Result == EEnsureResult::Created ? TEXT("created") : TEXT("unchanged"),
				*Summary.AssetObjectPath,
				*Summary.GraphId,
				Summary.NodeCount,
				Summary.ExecutionEdgeCount,
				Summary.DataEdgeCount);
		}

		FAutoConsoleCommand AuthorFixtureConsoleCommand(
			AuthorCommandName,
			TEXT("Create or strictly validate the real M3 multi-SCC fixture."),
			FConsoleCommandDelegate::CreateStatic(&AuthorFixtureCommand));
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
			if (Existing == nullptr)
			{
				OutError = FString::Printf(
					TEXT("M3_FIXTURE_SHAPE_INVALID: could not load existing %s"),
					*AssetObjectPath);
				return false;
			}
			return InspectFixture(*Existing, OutSummary, OutError);
		}

		UPackage* Package = CreatePackage(PackageName);
		if (Package == nullptr)
		{
			OutError = FString::Printf(
				TEXT("M3_FIXTURE_SHAPE_INVALID: could not create %s"), PackageName);
			return false;
		}
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Package,
			FName(AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintLensM3Fixture")));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: could not create Actor Blueprint");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: created Blueprint has no EventGraph");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: could not clear EventGraph");
				return false;
			}
		}
		if (!AddFixtureVariables(*Blueprint, OutError))
		{
			return false;
		}

		UEdGraph* TemplateOuter = NewObject<UEdGraph>(Blueprint);
		TemplateOuter->SetFlags(RF_Transient);
		UK2Node_CustomEvent* Event = SpawnCustomEvent(
			*Graph, *TemplateOuter, FVector2f(-2600.0f, 0.0f), OutError);
		UK2Node_VariableSet* WorkflowStarted = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3WorkflowStarted"), TEXT("true"),
			TEXT("M3_WORKFLOW_STARTED"), FVector2f(-2300.0f, 0.0f), OutError);
		UK2Node_VariableGet* PriorityGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3PriorityParcel"), TEXT("M3_ROUTE_PRIORITY_GET"),
			FVector2f(-2250.0f, -380.0f), OutError);
		UK2Node_IfThenElse* RouteBranch = SpawnBranch(
			*Graph, *TemplateOuter, TEXT("M3_ROUTE_BRANCH"), FVector2f(-1950.0f, 0.0f), OutError);
		UK2Node_VariableSet* PriorityRoute = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3PriorityRoute"), TEXT("true"),
			TEXT("M3_ROUTE_PRIORITY"), FVector2f(-1620.0f, -180.0f), OutError);
		UK2Node_VariableSet* StandardRoute = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3StandardRoute"), TEXT("true"),
			TEXT("M3_ROUTE_STANDARD"), FVector2f(-1620.0f, 180.0f), OutError);
		UK2Node_VariableSet* IntakeReady = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3IntakeReady"), TEXT("true"),
			TEXT("M3_INTAKE_RECONVERGED"), FVector2f(-1260.0f, 0.0f), OutError);

		UK2Node_VariableGet* IntakePredicateGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3IntakeAttempts"), TEXT("M3_DATA_INTAKE_PREDICATE_GET"),
			FVector2f(-1180.0f, -420.0f), OutError);
		UK2Node_CallFunction* IntakeLess = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Less_IntInt"), TEXT("2"), TEXT("M3_DATA_INTAKE_LESS"),
			FVector2f(-900.0f, -420.0f), OutError);
		UK2Node_IfThenElse* IntakeBranch = SpawnBranch(
			*Graph, *TemplateOuter, TEXT("M3_SCC_INTAKE_BRANCH"), FVector2f(-880.0f, 0.0f), OutError);
		UK2Node_VariableSet* IntakeRetry = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3IntakeRetry"), TEXT("true"),
			TEXT("M3_SCC_INTAKE_RETRY"), FVector2f(-540.0f, 0.0f), OutError);
		UK2Node_VariableGet* IntakeAdvanceGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3IntakeAttempts"), TEXT("M3_DATA_INTAKE_ADVANCE_GET"),
			FVector2f(-500.0f, 350.0f), OutError);
		UK2Node_CallFunction* IntakeAdd = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Add_IntInt"), TEXT("1"), TEXT("M3_DATA_INTAKE_ADD"),
			FVector2f(-220.0f, 350.0f), OutError);
		UK2Node_VariableSet* IntakeAdvance = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3IntakeAttempts"), TEXT("0"),
			TEXT("M3_SCC_INTAKE_ADVANCE"), FVector2f(-180.0f, 0.0f), OutError);
		UK2Node_VariableSet* IntakeAccepted = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3IntakeAccepted"), TEXT("true"),
			TEXT("M3_INTAKE_ACCEPTED"), FVector2f(-520.0f, -520.0f), OutError);

		UK2Node_VariableGet* ValidationPredicateGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3ValidationAttempts"), TEXT("M3_DATA_VALIDATION_PREDICATE_GET"),
			FVector2f(-120.0f, -820.0f), OutError);
		UK2Node_CallFunction* ValidationLess = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Less_IntInt"), TEXT("2"), TEXT("M3_DATA_VALIDATION_LESS"),
			FVector2f(160.0f, -820.0f), OutError);
		UK2Node_IfThenElse* ValidationBranch = SpawnBranch(
			*Graph, *TemplateOuter, TEXT("M3_SCC_VALIDATION_BRANCH"), FVector2f(160.0f, -420.0f), OutError);
		UK2Node_VariableSet* ValidationRework = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3ValidationRework"), TEXT("true"),
			TEXT("M3_SCC_VALIDATION_REWORK"), FVector2f(500.0f, -420.0f), OutError);
		UK2Node_VariableGet* ValidationAdvanceGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3ValidationAttempts"), TEXT("M3_DATA_VALIDATION_ADVANCE_GET"),
			FVector2f(540.0f, -70.0f), OutError);
		UK2Node_CallFunction* ValidationAdd = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Add_IntInt"), TEXT("1"), TEXT("M3_DATA_VALIDATION_ADD"),
			FVector2f(820.0f, -70.0f), OutError);
		UK2Node_VariableSet* ValidationAdvance = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3ValidationAttempts"), TEXT("0"),
			TEXT("M3_SCC_VALIDATION_ADVANCE"), FVector2f(860.0f, -420.0f), OutError);
		UK2Node_VariableSet* ValidationAccepted = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3ValidationAccepted"), TEXT("true"),
			TEXT("M3_VALIDATION_ACCEPTED"), FVector2f(520.0f, -940.0f), OutError);

		UK2Node_VariableGet* DispatchPredicateGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3DispatchAttempts"), TEXT("M3_DATA_DISPATCH_PREDICATE_GET"),
			FVector2f(920.0f, -1240.0f), OutError);
		UK2Node_CallFunction* DispatchLess = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Less_IntInt"), TEXT("2"), TEXT("M3_DATA_DISPATCH_LESS"),
			FVector2f(1200.0f, -1240.0f), OutError);
		UK2Node_IfThenElse* DispatchBranch = SpawnBranch(
			*Graph, *TemplateOuter, TEXT("M3_SCC_DISPATCH_BRANCH"), FVector2f(1200.0f, -840.0f), OutError);
		UK2Node_CallFunction* OpaquePrint = SpawnOpaquePrint(
			*Graph, *TemplateOuter, FVector2f(1520.0f, -840.0f), OutError);
		UK2Node_VariableSet* DispatchRecovery = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3DispatchRecovery"), TEXT("true"),
			TEXT("M3_SCC_DISPATCH_RECOVERY"), FVector2f(1840.0f, -840.0f), OutError);
		UK2Node_VariableGet* DispatchAdvanceGet = SpawnVariableGet(
			*Graph, *TemplateOuter, TEXT("M3DispatchAttempts"), TEXT("M3_DATA_DISPATCH_ADVANCE_GET"),
			FVector2f(1880.0f, -490.0f), OutError);
		UK2Node_CallFunction* DispatchAdd = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Add_IntInt"), TEXT("1"), TEXT("M3_DATA_DISPATCH_ADD"),
			FVector2f(2160.0f, -490.0f), OutError);
		UK2Node_VariableSet* DispatchAdvance = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3DispatchAttempts"), TEXT("0"),
			TEXT("M3_SCC_DISPATCH_ADVANCE"), FVector2f(2200.0f, -840.0f), OutError);
		UK2Node_VariableSet* DispatchAccepted = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3DispatchAccepted"), TEXT("true"),
			TEXT("M3_DISPATCH_ACCEPTED"), FVector2f(1540.0f, -1360.0f), OutError);
		UK2Node_VariableSet* ParcelComplete = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("M3ParcelComplete"), TEXT("true"),
			TEXT("M3_PARCEL_COMPLETE"), FVector2f(1880.0f, -1360.0f), OutError);

		const TArray<UEdGraphNode*> RequiredNodes = {
			Event, WorkflowStarted, PriorityGet, RouteBranch, PriorityRoute, StandardRoute,
			IntakeReady, IntakePredicateGet, IntakeLess, IntakeBranch, IntakeRetry,
			IntakeAdvanceGet, IntakeAdd, IntakeAdvance, IntakeAccepted,
			ValidationPredicateGet, ValidationLess, ValidationBranch, ValidationRework,
			ValidationAdvanceGet, ValidationAdd, ValidationAdvance, ValidationAccepted,
			DispatchPredicateGet, DispatchLess, DispatchBranch, OpaquePrint, DispatchRecovery,
			DispatchAdvanceGet, DispatchAdd, DispatchAdvance, DispatchAccepted, ParcelComplete};
		if (RequiredNodes.Contains(nullptr))
		{
			return false;
		}

		if (!LinkPins(ExecOutput(*Event), ExecInput(*WorkflowStarted), TEXT("event -> workflow"), OutError)
			|| !LinkPins(ExecOutput(*WorkflowStarted), ExecInput(*RouteBranch), TEXT("workflow -> route"), OutError)
			|| !LinkPins(PriorityGet->FindPin(TEXT("M3PriorityParcel"), EGPD_Output),
				RouteBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input), TEXT("priority -> route condition"), OutError)
			|| !LinkPins(RouteBranch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*PriorityRoute), TEXT("route true"), OutError)
			|| !LinkPins(RouteBranch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*StandardRoute), TEXT("route false"), OutError)
			|| !LinkPins(ExecOutput(*PriorityRoute), ExecInput(*IntakeReady), TEXT("priority reconvergence"), OutError, true)
			|| !LinkPins(ExecOutput(*StandardRoute), ExecInput(*IntakeReady), TEXT("standard reconvergence"), OutError, true)

			|| !LinkPins(ExecOutput(*IntakeReady), ExecInput(*IntakeBranch), TEXT("intake entry"), OutError)
			|| !LinkPins(IntakePredicateGet->FindPin(TEXT("M3IntakeAttempts"), EGPD_Output), IntakeLess->FindPin(TEXT("A"), EGPD_Input), TEXT("intake predicate"), OutError)
			|| !LinkPins(IntakeLess->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), IntakeBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input), TEXT("intake condition"), OutError)
			|| !LinkPins(IntakeBranch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*IntakeRetry), TEXT("intake retry"), OutError)
			|| !LinkPins(ExecOutput(*IntakeRetry), ExecInput(*IntakeAdvance), TEXT("intake advance"), OutError)
			|| !LinkPins(IntakeAdvanceGet->FindPin(TEXT("M3IntakeAttempts"), EGPD_Output), IntakeAdd->FindPin(TEXT("A"), EGPD_Input), TEXT("intake advance get"), OutError)
			|| !LinkPins(IntakeAdd->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), IntakeAdvance->FindPin(TEXT("M3IntakeAttempts"), EGPD_Input), TEXT("intake advance value"), OutError)
			|| !LinkPins(ExecOutput(*IntakeAdvance), ExecInput(*IntakeBranch), TEXT("intake return"), OutError)
			|| !LinkPins(IntakeBranch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*IntakeAccepted), TEXT("intake exit"), OutError)

			|| !LinkPins(ExecOutput(*IntakeAccepted), ExecInput(*ValidationBranch), TEXT("validation entry"), OutError)
			|| !LinkPins(ValidationPredicateGet->FindPin(TEXT("M3ValidationAttempts"), EGPD_Output), ValidationLess->FindPin(TEXT("A"), EGPD_Input), TEXT("validation predicate"), OutError)
			|| !LinkPins(ValidationLess->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), ValidationBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input), TEXT("validation condition"), OutError)
			|| !LinkPins(ValidationBranch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*ValidationRework), TEXT("validation rework"), OutError)
			|| !LinkPins(ExecOutput(*ValidationRework), ExecInput(*ValidationAdvance), TEXT("validation advance"), OutError)
			|| !LinkPins(ValidationAdvanceGet->FindPin(TEXT("M3ValidationAttempts"), EGPD_Output), ValidationAdd->FindPin(TEXT("A"), EGPD_Input), TEXT("validation advance get"), OutError)
			|| !LinkPins(ValidationAdd->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), ValidationAdvance->FindPin(TEXT("M3ValidationAttempts"), EGPD_Input), TEXT("validation advance value"), OutError)
			|| !LinkPins(ExecOutput(*ValidationAdvance), ExecInput(*ValidationBranch), TEXT("validation return"), OutError)
			|| !LinkPins(ValidationBranch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*ValidationAccepted), TEXT("validation exit"), OutError)

			|| !LinkPins(ExecOutput(*ValidationAccepted), ExecInput(*DispatchBranch), TEXT("dispatch entry"), OutError)
			|| !LinkPins(DispatchPredicateGet->FindPin(TEXT("M3DispatchAttempts"), EGPD_Output), DispatchLess->FindPin(TEXT("A"), EGPD_Input), TEXT("dispatch predicate"), OutError)
			|| !LinkPins(DispatchLess->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), DispatchBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input), TEXT("dispatch condition"), OutError)
			|| !LinkPins(DispatchBranch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*OpaquePrint), TEXT("dispatch opaque boundary"), OutError)
			|| !LinkPins(ExecOutput(*OpaquePrint), ExecInput(*DispatchRecovery), TEXT("dispatch recovery"), OutError)
			|| !LinkPins(ExecOutput(*DispatchRecovery), ExecInput(*DispatchAdvance), TEXT("dispatch advance"), OutError)
			|| !LinkPins(DispatchAdvanceGet->FindPin(TEXT("M3DispatchAttempts"), EGPD_Output), DispatchAdd->FindPin(TEXT("A"), EGPD_Input), TEXT("dispatch advance get"), OutError)
			|| !LinkPins(DispatchAdd->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), DispatchAdvance->FindPin(TEXT("M3DispatchAttempts"), EGPD_Input), TEXT("dispatch advance value"), OutError)
			|| !LinkPins(ExecOutput(*DispatchAdvance), ExecInput(*DispatchBranch), TEXT("dispatch return"), OutError)
			|| !LinkPins(DispatchBranch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*DispatchAccepted), TEXT("dispatch exit"), OutError)
			|| !LinkPins(ExecOutput(*DispatchAccepted), ExecInput(*ParcelComplete), TEXT("dispatch accepted -> complete"), OutError))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint->Status == BS_Error || Blueprint->GeneratedClass == nullptr)
		{
			OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: Blueprint compilation failed");
			return false;
		}
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
			OutError = FString::Printf(
				TEXT("M3_FIXTURE_SHAPE_INVALID: could not save %s"), *Filename);
			return false;
		}

		UPackage* ReloadedPackage = ReloadPackage(Package, LOAD_None);
		UBlueprint* Reloaded = ReloadedPackage != nullptr
			? FindObject<UBlueprint>(ReloadedPackage, AssetName)
			: nullptr;
		FFixtureSummary ReloadedSummary;
		if (Reloaded == nullptr || !InspectFixture(*Reloaded, ReloadedSummary, OutError)
			|| ReloadedSummary.NodeCount != OutSummary.NodeCount
			|| ReloadedSummary.ExecutionEdgeCount != OutSummary.ExecutionEdgeCount
			|| ReloadedSummary.DataEdgeCount != OutSummary.DataEdgeCount)
		{
			OutError = TEXT("M3_FIXTURE_SHAPE_INVALID: saved fixture did not re-observe identically");
			return false;
		}
		OutSummary = ReloadedSummary;
		OutResult = EEnsureResult::Created;
		return true;
	}
}
