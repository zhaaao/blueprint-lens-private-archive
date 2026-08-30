// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC7StaticSCCFixture.h"

#include "BlueprintLensSequenceFacts.h"

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
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensLC7StaticSCCFixture, Log, All);

namespace BlueprintLensLC7StaticSCCFixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/LensCorpus/BP_LC7_StaticSCC");
		constexpr TCHAR AssetName[] = TEXT("BP_LC7_StaticSCC");

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
			Template->CustomFunctionName = TEXT("LC7_STATIC_SCC");
			UK2Node_CustomEvent* Node = Cast<UK2Node_CustomEvent>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not spawn Custom Event");
				return nullptr;
			}
			Node->CustomFunctionName = TEXT("LC7_STATIC_SCC");
			Node->NodeComment = TEXT("LC7_EVENT");
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
					TEXT("LC7_FIXTURE_SHAPE_INVALID: could not spawn Set %s"),
					*VariableName.ToString());
				return nullptr;
			}
			UEdGraphPin* ValuePin = Node->FindPin(VariableName, EGPD_Input);
			if (ValuePin == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: Set %s has no value pin"),
					*VariableName.ToString());
				return nullptr;
			}
			ValuePin->DefaultValue = DefaultValue;
			Node->NodeComment = NodeComment;
			return Node;
		}

		UK2Node_VariableGet* SpawnCounterGet(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FString& NodeComment,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_VariableGet* Template = NewObject<UK2Node_VariableGet>(&TemplateOuter);
			Template->VariableReference.SetSelfMember(TEXT("LC7Counter"));
			UK2Node_VariableGet* Node = Cast<UK2Node_VariableGet>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->FindPin(TEXT("LC7Counter"), EGPD_Output) == nullptr)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not spawn Get LC7Counter");
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
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(
				FunctionName,
				UKismetMathLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: could not spawn %s"),
					*FunctionName.ToString());
				return nullptr;
			}
			UEdGraphPin* B = Node->FindPin(TEXT("B"), EGPD_Input);
			if (B == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: %s has no B input"),
					*FunctionName.ToString());
				return nullptr;
			}
			B->DefaultValue = SecondOperand;
			return Node;
		}

		UK2Node_IfThenElse* SpawnBranch(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_IfThenElse* Template = NewObject<UK2Node_IfThenElse>(&TemplateOuter);
			UK2Node_IfThenElse* Node = Cast<UK2Node_IfThenElse>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not spawn Branch");
				return nullptr;
			}
			Node->NodeComment = TEXT("LC7_SCC_BRANCH");
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
			FString& OutError)
		{
			if (Source == nullptr || Target == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: missing pin for %s"),
					Description);
				return false;
			}
			const UEdGraphSchema* Schema = Source->GetSchema();
			if (Schema == nullptr || !Schema->TryCreateConnection(Source, Target)
				|| !Source->LinkedTo.Contains(Target) || !Target->LinkedTo.Contains(Source))
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: could not link %s"),
					Description);
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
			FFixtureAnchors& OutAnchors,
			FString& OutError)
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
			if (Graph == nullptr || Blueprint.UbergraphPages.Num() != 1)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: fixture must own exactly one EventGraph");
				return false;
			}
			if (Graph->Nodes.Num() != 10 || CountEdges(*Graph, true) != 6
				|| CountEdges(*Graph, false) != 4)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: expected nodes=10 exec=6 data=4; found nodes=%d exec=%d data=%d"),
					Graph->Nodes.Num(),
					CountEdges(*Graph, true),
					CountEdges(*Graph, false));
				return false;
			}

			UK2Node_CustomEvent* Event = nullptr;
			UK2Node_IfThenElse* Branch = nullptr;
			UK2Node_VariableSet* Body = nullptr;
			UK2Node_VariableSet* Criterion = nullptr;
			TArray<UK2Node_VariableSet*> CounterSets;
			TArray<UK2Node_VariableGet*> CounterGets;
			UK2Node_CallFunction* Less = nullptr;
			UK2Node_CallFunction* Add = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_CustomEvent* Candidate = Cast<UK2Node_CustomEvent>(Node))
				{
					if (Candidate->CustomFunctionName == TEXT("LC7_STATIC_SCC"))
					{
						Event = Candidate;
					}
				}
				if (UK2Node_IfThenElse* Candidate = Cast<UK2Node_IfThenElse>(Node))
				{
					Branch = Candidate;
				}
				if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
				{
					if (Set->GetVarName() == TEXT("LC7Counter"))
					{
						CounterSets.Add(Set);
					}
					else if (Set->GetVarName() == TEXT("LC7Visited"))
					{
						Body = Set;
					}
					else if (Set->GetVarName() == TEXT("LC7Complete"))
					{
						Criterion = Set;
					}
				}
				if (UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node))
				{
					if (Get->GetVarName() == TEXT("LC7Counter"))
					{
						CounterGets.Add(Get);
					}
				}
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					if (Function != nullptr && Function->GetFName() == TEXT("Less_IntInt"))
					{
						Less = Call;
					}
					else if (Function != nullptr && Function->GetFName() == TEXT("Add_IntInt"))
					{
						Add = Call;
					}
				}
			}
			if (Event == nullptr || Branch == nullptr || Body == nullptr || Criterion == nullptr
				|| CounterSets.Num() != 2 || CounterGets.Num() != 2
				|| Less == nullptr || Add == nullptr)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: required node roles are missing or duplicated");
				return false;
			}

			UK2Node_VariableSet* Initialise = nullptr;
			UK2Node_VariableSet* Advance = nullptr;
			for (UK2Node_VariableSet* CounterSet : CounterSets)
			{
				if (PinsAreLinked(ExecOutput(*Event), ExecInput(*CounterSet)))
				{
					Initialise = CounterSet;
				}
				if (PinsAreLinked(
					Add->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
					CounterSet->FindPin(TEXT("LC7Counter"), EGPD_Input)))
				{
					Advance = CounterSet;
				}
			}
			if (Initialise == nullptr || Advance == nullptr || Initialise == Advance)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: initialise/advance Sets do not resolve uniquely");
				return false;
			}

			UK2Node_VariableGet* PredicateGet = nullptr;
			UK2Node_VariableGet* AdvanceGet = nullptr;
			for (UK2Node_VariableGet* Get : CounterGets)
			{
				PredicateGet = PinsAreLinked(
					Get->FindPin(TEXT("LC7Counter"), EGPD_Output),
					Less->FindPin(TEXT("A"), EGPD_Input)) ? Get : PredicateGet;
				AdvanceGet = PinsAreLinked(
					Get->FindPin(TEXT("LC7Counter"), EGPD_Output),
					Add->FindPin(TEXT("A"), EGPD_Input)) ? Get : AdvanceGet;
			}
			if (PredicateGet == nullptr || AdvanceGet == nullptr || PredicateGet == AdvanceGet
				|| !PinsAreLinked(ExecOutput(*Initialise), ExecInput(*Branch))
				|| !PinsAreLinked(Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*Body))
				|| !PinsAreLinked(ExecOutput(*Body), ExecInput(*Advance))
				|| !PinsAreLinked(ExecOutput(*Advance), ExecInput(*Branch))
				|| !PinsAreLinked(Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*Criterion))
				|| !PinsAreLinked(Less->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
					Branch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input)))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: fixture connections differ from the accepted SCC contract");
				return false;
			}

			TMap<FName, FName> ExpectedMembers = {
				{TEXT("LC7Counter"), UEdGraphSchema_K2::PC_Int},
				{TEXT("LC7Visited"), UEdGraphSchema_K2::PC_Boolean},
				{TEXT("LC7Complete"), UEdGraphSchema_K2::PC_Boolean}};
			TSet<FGuid> VariableGuids;
			for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
			{
				if (const FName* ExpectedType = ExpectedMembers.Find(Variable.VarName))
				{
					if (!Variable.VarGuid.IsValid() || Variable.VarType.PinCategory != *ExpectedType)
					{
						OutError = FString::Printf(
							TEXT("LC7_FIXTURE_SHAPE_INVALID: invalid member identity/type for %s"),
							*Variable.VarName.ToString());
						return false;
					}
					VariableGuids.Add(Variable.VarGuid);
					ExpectedMembers.Remove(Variable.VarName);
				}
			}
			if (!ExpectedMembers.IsEmpty() || VariableGuids.Num() != 3)
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: fixture member identities are incomplete or duplicate");
				return false;
			}

			OutAnchors = FFixtureAnchors();
			OutAnchors.AssetObjectPath = Blueprint.GetPathName();
			OutAnchors.GraphId = Graph->GetPathName();
			OutAnchors.EventNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Event);
			OutAnchors.InitialiseNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Initialise);
			OutAnchors.BranchNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Branch);
			OutAnchors.BodyNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Body);
			OutAnchors.AdvanceNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Advance);
			OutAnchors.CriterionNodeId = BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, *Criterion);
			return true;
		}
	}

	bool EnsureFixture(FFixtureAnchors& OutAnchors, FString& OutError)
	{
		OutAnchors = FFixtureAnchors();
		OutError.Reset();
		const FString AssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
			if (Existing == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC7_FIXTURE_SHAPE_INVALID: could not load %s"),
					*AssetObjectPath);
				return false;
			}
			return InspectFixture(*Existing, OutAnchors, OutError);
		}

		UPackage* Package = CreatePackage(PackageName);
		if (Package == nullptr)
		{
			OutError = FString::Printf(
				TEXT("LC7_FIXTURE_SHAPE_INVALID: could not create %s"),
				PackageName);
			return false;
		}
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Package,
			FName(AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintLensLC7StaticSCCFixture")));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not create Actor Blueprint");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: created Blueprint has no EventGraph");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not clear EventGraph");
				return false;
			}
		}

		FEdGraphPinType IntegerType;
		IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
		FEdGraphPinType BooleanType;
		BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		if (!FBlueprintEditorUtils::AddMemberVariable(
				Blueprint, TEXT("LC7Counter"), IntegerType, TEXT("0"))
			|| !FBlueprintEditorUtils::AddMemberVariable(
				Blueprint, TEXT("LC7Visited"), BooleanType, TEXT("false"))
			|| !FBlueprintEditorUtils::AddMemberVariable(
				Blueprint, TEXT("LC7Complete"), BooleanType, TEXT("false")))
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: could not add fixture members");
			return false;
		}

		UEdGraph* TemplateOuter = NewObject<UEdGraph>(Blueprint);
		TemplateOuter->SetFlags(RF_Transient);
		UK2Node_CustomEvent* Event = SpawnCustomEvent(
			*Graph, *TemplateOuter, FVector2f(-1450.0f, -50.0f), OutError);
		UK2Node_VariableSet* Initialise = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("LC7Counter"), TEXT("0"), TEXT("LC7_INITIALISE"),
			FVector2f(-1120.0f, -50.0f), OutError);
		UK2Node_VariableGet* PredicateGet = SpawnCounterGet(
			*Graph, *TemplateOuter, TEXT("LC7_PREDICATE_GET"),
			FVector2f(-1030.0f, -390.0f), OutError);
		UK2Node_CallFunction* Less = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Less_IntInt"), TEXT("3"),
			FVector2f(-730.0f, -390.0f), OutError);
		UK2Node_IfThenElse* Branch = SpawnBranch(
			*Graph, *TemplateOuter, FVector2f(-410.0f, -50.0f), OutError);
		UK2Node_VariableSet* Body = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("LC7Visited"), TEXT("true"), TEXT("LC7_SCC_BODY"),
			FVector2f(-60.0f, -50.0f), OutError);
		UK2Node_VariableGet* AdvanceGet = SpawnCounterGet(
			*Graph, *TemplateOuter, TEXT("LC7_ADVANCE_GET"),
			FVector2f(20.0f, 260.0f), OutError);
		UK2Node_CallFunction* Add = SpawnMathCall(
			*Graph, *TemplateOuter, TEXT("Add_IntInt"), TEXT("1"),
			FVector2f(320.0f, 260.0f), OutError);
		UK2Node_VariableSet* Advance = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("LC7Counter"), TEXT("0"), TEXT("LC7_SCC_ADVANCE"),
			FVector2f(620.0f, -50.0f), OutError);
		UK2Node_VariableSet* Criterion = SpawnVariableSet(
			*Graph, *TemplateOuter, TEXT("LC7Complete"), TEXT("true"), TEXT("LC7_CRITERION"),
			FVector2f(-60.0f, -520.0f), OutError);
		if (Event == nullptr || Initialise == nullptr || PredicateGet == nullptr
			|| Less == nullptr || Branch == nullptr || Body == nullptr
			|| AdvanceGet == nullptr || Add == nullptr || Advance == nullptr
			|| Criterion == nullptr)
		{
			return false;
		}

		if (!LinkPins(ExecOutput(*Event), ExecInput(*Initialise),
				TEXT("event -> initialise"), OutError)
			|| !LinkPins(ExecOutput(*Initialise), ExecInput(*Branch),
				TEXT("initialise -> branch"), OutError)
			|| !LinkPins(Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output), ExecInput(*Body),
				TEXT("branch true -> body"), OutError)
			|| !LinkPins(ExecOutput(*Body), ExecInput(*Advance),
				TEXT("body -> advance"), OutError)
			|| !LinkPins(ExecOutput(*Advance), ExecInput(*Branch),
				TEXT("advance -> branch return"), OutError)
			|| !LinkPins(Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), ExecInput(*Criterion),
				TEXT("branch false -> criterion"), OutError)
			|| !LinkPins(PredicateGet->FindPin(TEXT("LC7Counter"), EGPD_Output),
				Less->FindPin(TEXT("A"), EGPD_Input), TEXT("predicate Get -> Less A"), OutError)
			|| !LinkPins(Less->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
				Branch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input),
				TEXT("Less result -> Branch condition"), OutError)
			|| !LinkPins(AdvanceGet->FindPin(TEXT("LC7Counter"), EGPD_Output),
				Add->FindPin(TEXT("A"), EGPD_Input), TEXT("advance Get -> Add A"), OutError)
			|| !LinkPins(Add->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
				Advance->FindPin(TEXT("LC7Counter"), EGPD_Input),
				TEXT("Add result -> advance value"), OutError))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint->Status == BS_Error)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: Blueprint compilation failed");
			return false;
		}
		if (!InspectFixture(*Blueprint, OutAnchors, OutError))
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
			OutError = FString::Printf(
				TEXT("LC7_FIXTURE_SHAPE_INVALID: could not save %s"),
				*Filename);
			return false;
		}

		UE_LOG(
			LogBlueprintLensLC7StaticSCCFixture,
			Display,
			TEXT("LC7_STATIC_SCC_FIXTURE_READY asset=%s nodes=10 edges=10"),
			*OutAnchors.AssetObjectPath);
		return true;
	}
}
