// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC6BoundaryFixture.h"

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
#include "K2Node_Select.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensLC6BoundaryFixture, Log, All);

namespace BlueprintLensLC6BoundaryFixture
{
	namespace
	{
		constexpr TCHAR PackageName[] = TEXT("/Game/LensCorpus/BP_LC6_BoundaryMatrix");
		constexpr TCHAR AssetName[] = TEXT("BP_LC6_BoundaryMatrix");
		const TArray<FName> BooleanMembers = {
			TEXT("LC6OpaqueDone"),
			TEXT("LC6UncertainResult"),
			TEXT("LC6UnsupportedDone"),
			TEXT("LC6Truncated01"),
			TEXT("LC6Truncated02"),
			TEXT("LC6Truncated03"),
			TEXT("LC6Truncated04"),
			TEXT("LC6Truncated05"),
			TEXT("LC6Truncated06")};

		struct FExpectedScenario
		{
			const TCHAR* ScenarioId;
			FName CriterionVariable;
			int32 NodeCount;
			int32 EdgeCount;
		};

		const TArray<FExpectedScenario> ExpectedScenarios = {
			{TEXT("LC6_OPAQUE"), TEXT("LC6OpaqueDone"), 3, 2},
			{TEXT("LC6_UNCERTAIN"), TEXT("LC6UncertainResult"), 3, 2},
			{TEXT("LC6_UNSUPPORTED"), TEXT("LC6UnsupportedDone"), 3, 2},
			{TEXT("LC6_TRUNCATED"), TEXT("LC6Truncated06"), 7, 6}};

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
			const FName EventName,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CustomEvent* Template = NewObject<UK2Node_CustomEvent>(&TemplateOuter);
			Template->CustomFunctionName = EventName;
			UK2Node_CustomEvent* Node = Cast<UK2Node_CustomEvent>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not spawn Custom Event %s"),
					*EventName.ToString());
				return nullptr;
			}
			Node->CustomFunctionName = EventName;
			Node->NodeComment = EventName.ToString();
			return Node;
		}

		UK2Node_CallFunction* SpawnCall(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FName FunctionName,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(&TemplateOuter);
			Template->FunctionReference.SetExternalMember(
				FunctionName,
				UKismetSystemLibrary::StaticClass());
			UK2Node_CallFunction* Node = Cast<UK2Node_CallFunction>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetTargetFunction() == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not spawn %s"),
					*FunctionName.ToString());
				return nullptr;
			}
			return Node;
		}

		UK2Node_VariableSet* SpawnBooleanSet(
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
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not spawn Set %s"),
					*VariableName.ToString());
				return nullptr;
			}
			UEdGraphPin* ValuePin = Node->FindPin(VariableName, EGPD_Input);
			if (ValuePin == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: Set %s has no value pin"),
					*VariableName.ToString());
				return nullptr;
			}
			ValuePin->DefaultValue = TEXT("true");
			return Node;
		}

		UK2Node_Select* SpawnBooleanSelect(
			UEdGraph& Graph,
			UEdGraph& TemplateOuter,
			const FVector2f& Position,
			FString& OutError)
		{
			UK2Node_Select* Template = NewObject<UK2Node_Select>(&TemplateOuter);
			UK2Node_Select* Node = Cast<UK2Node_Select>(
				SpawnFromTemplate(Graph, *Template, Position));
			if (Node == nullptr || Node->GetReturnValuePin() == nullptr)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: could not spawn Select");
				return nullptr;
			}
			UEdGraphPin* IndexPin = Node->GetIndexPin();
			if (IndexPin == nullptr)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: Select has no index pin");
				return nullptr;
			}
			FEdGraphPinType BooleanType;
			BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			IndexPin->PinType = BooleanType;
			Node->ChangePinType(IndexPin);
			IndexPin = Node->GetIndexPin();
			UEdGraphPin* ReturnPin = Node->GetReturnValuePin();
			if (IndexPin == nullptr || ReturnPin == nullptr)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: Select reconstruction lost required pins");
				return nullptr;
			}
			IndexPin->DefaultValue = TEXT("false");
			ReturnPin->PinType = BooleanType;
			Node->ChangePinType(ReturnPin);
			TArray<UEdGraphPin*> Options;
			Node->GetOptionPins(Options);
			if (Options.Num() != 2 || Node->GetIndexPin() == nullptr
				|| Node->GetReturnValuePin() == nullptr)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: Select must expose two options");
				return nullptr;
			}
			Options[0]->DefaultValue = TEXT("false");
			Options[1]->DefaultValue = TEXT("true");
			Node->NodeComment = TEXT("LC6_UNCERTAIN");
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
					TEXT("LC6_FIXTURE_SHAPE_INVALID: missing pin for %s"),
					Description);
				return false;
			}
			const UEdGraphSchema* Schema = Source->GetSchema();
			if (Schema == nullptr || !Schema->TryCreateConnection(Source, Target)
				|| !Source->LinkedTo.Contains(Target) || !Target->LinkedTo.Contains(Source))
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not link %s"),
					Description);
				return false;
			}
			return true;
		}

		TArray<TSet<UEdGraphNode*>> BuildComponents(UEdGraph& Graph)
		{
			TMap<UEdGraphNode*, TSet<UEdGraphNode*>> Adjacency;
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				Adjacency.FindOrAdd(Node);
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr)
					{
						continue;
					}
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* Other = LinkedPin == nullptr
							? nullptr
							: LinkedPin->GetOwningNode();
						if (Other != nullptr && Other != Node)
						{
							Adjacency.FindOrAdd(Node).Add(Other);
							Adjacency.FindOrAdd(Other).Add(Node);
						}
					}
				}
			}

			TArray<TSet<UEdGraphNode*>> Components;
			TSet<UEdGraphNode*> Visited;
			for (const TPair<UEdGraphNode*, TSet<UEdGraphNode*>>& Pair : Adjacency)
			{
				if (Visited.Contains(Pair.Key))
				{
					continue;
				}
				TSet<UEdGraphNode*> Component;
				TArray<UEdGraphNode*> Pending = {Pair.Key};
				while (!Pending.IsEmpty())
				{
					UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);
					if (Current == nullptr || Visited.Contains(Current))
					{
						continue;
					}
					Visited.Add(Current);
					Component.Add(Current);
					for (UEdGraphNode* Other : Adjacency.FindChecked(Current))
					{
						if (!Visited.Contains(Other))
						{
							Pending.Add(Other);
						}
					}
				}
				Components.Add(MoveTemp(Component));
			}
			return Components;
		}

		int32 CountComponentEdges(const TSet<UEdGraphNode*>& Component)
		{
			int32 Count = 0;
			for (const UEdGraphNode* Node : Component)
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output)
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						const UEdGraphNode* Target = LinkedPin == nullptr
							? nullptr
							: LinkedPin->GetOwningNode();
						Count += Target != nullptr && Component.Contains(Target) ? 1 : 0;
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
			if (Graph == nullptr)
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: fixture has no EventGraph");
				return false;
			}
			if (Graph->Nodes.Num() != 16)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: expected 16 nodes, found %d"),
					Graph->Nodes.Num());
				return false;
			}

			const TArray<TSet<UEdGraphNode*>> Components = BuildComponents(*Graph);
			if (Components.Num() != 4)
			{
				OutError = FString::Printf(
					TEXT("LC6_COMPONENT_ISOLATION_INVALID: expected four components, found %d"),
					Components.Num());
				return false;
			}

			TMap<FString, UK2Node_CustomEvent*> Roots;
			TMap<FName, UK2Node_VariableSet*> Criteria;
			int32 PrintStringCount = 0;
			int32 DelayCount = 0;
			int32 SelectCount = 0;
			int32 TruncationSetCount = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
				{
					Roots.Add(Event->CustomFunctionName.ToString(), Event);
				}
				if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
				{
					Criteria.Add(Set->GetVarName(), Set);
					TruncationSetCount += Set->GetVarName().ToString().StartsWith(TEXT("LC6Truncated"))
						? 1
						: 0;
				}
				SelectCount += Node != nullptr && Node->IsA<UK2Node_Select>() ? 1 : 0;
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = Call->GetTargetFunction();
					PrintStringCount += Function != nullptr && Function->GetFName() == TEXT("PrintString")
						? 1
						: 0;
					DelayCount += Function != nullptr && Function->GetFName() == TEXT("Delay")
						&& Function->HasMetaData(TEXT("Latent"))
						? 1
						: 0;
				}
			}
			if (Roots.Num() != 4 || PrintStringCount != 1 || DelayCount != 1
				|| SelectCount != 1 || TruncationSetCount != 6)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: roots=%d print=%d delay=%d select=%d truncation_sets=%d"),
					Roots.Num(),
					PrintStringCount,
					DelayCount,
					SelectCount,
					TruncationSetCount);
				return false;
			}

			OutAnchors = FFixtureAnchors();
			OutAnchors.AssetObjectPath = Blueprint.GetPathName();
			OutAnchors.GraphId = Graph->GetPathName();
			for (const FExpectedScenario& Expected : ExpectedScenarios)
			{
				UK2Node_CustomEvent* const* Root = Roots.Find(Expected.ScenarioId);
				UK2Node_VariableSet* const* Criterion = Criteria.Find(Expected.CriterionVariable);
				if (Root == nullptr || *Root == nullptr || Criterion == nullptr || *Criterion == nullptr)
				{
					OutError = FString::Printf(
						TEXT("LC6_FIXTURE_SHAPE_INVALID: missing anchors for %s"),
						Expected.ScenarioId);
					return false;
				}
				const TSet<UEdGraphNode*>* OwningComponent = nullptr;
				for (const TSet<UEdGraphNode*>& Component : Components)
				{
					if (Component.Contains(*Root) && Component.Contains(*Criterion))
					{
						OwningComponent = &Component;
						break;
					}
				}
				if (OwningComponent == nullptr
					|| OwningComponent->Num() != Expected.NodeCount
					|| CountComponentEdges(*OwningComponent) != Expected.EdgeCount)
				{
					OutError = FString::Printf(
						TEXT("LC6_COMPONENT_ISOLATION_INVALID: %s has the wrong component shape"),
						Expected.ScenarioId);
					return false;
				}
				OutAnchors.Scenarios.Add({
					Expected.ScenarioId,
					BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, **Root),
					BlueprintLensSequenceFacts::MakeNodeId(OutAnchors.GraphId, **Criterion)});
			}
			OutAnchors.Scenarios.Sort(
				[](const FScenarioAnchors& Left, const FScenarioAnchors& Right)
				{
					return Left.ScenarioId < Right.ScenarioId;
				});

			TSet<FGuid> FixtureVariableGuids;
			int32 FixtureVariableCount = 0;
			for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
			{
				if (BooleanMembers.Contains(Variable.VarName))
				{
					if (!Variable.VarGuid.IsValid()
						|| Variable.VarType.PinCategory != UEdGraphSchema_K2::PC_Boolean)
					{
						OutError = FString::Printf(
							TEXT("LC6_FIXTURE_SHAPE_INVALID: invalid member identity/type for %s"),
							*Variable.VarName.ToString());
						return false;
					}
					++FixtureVariableCount;
					FixtureVariableGuids.Add(Variable.VarGuid);
				}
			}
			if (FixtureVariableCount != BooleanMembers.Num()
				|| FixtureVariableGuids.Num() != BooleanMembers.Num())
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: fixture member identities are incomplete or duplicate");
				return false;
			}
			return true;
		}
	}

	bool EnsureFixture(FFixtureAnchors& OutAnchors, FString& OutError)
	{
		OutAnchors = FFixtureAnchors();
		OutError.Reset();
		const FString AssetObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		UPackage* Package = nullptr;
		UBlueprint* Blueprint = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
			if (Blueprint == nullptr)
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not load %s"),
					*AssetObjectPath);
				return false;
			}
			return InspectFixture(*Blueprint, OutAnchors, OutError);
		}

		Package = CreatePackage(PackageName);
		if (Package == nullptr)
		{
			OutError = FString::Printf(
				TEXT("LC6_FIXTURE_SHAPE_INVALID: could not create %s"),
				PackageName);
			return false;
		}
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Package,
			FName(AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintLensLC6BoundaryFixture")));
		if (Blueprint == nullptr)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: could not create Actor Blueprint");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (Graph == nullptr)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: created Blueprint has no EventGraph");
			return false;
		}
		while (!Graph->Nodes.IsEmpty())
		{
			if (!Graph->RemoveNode(Graph->Nodes.Last()))
			{
				OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: could not clear EventGraph");
				return false;
			}
		}

		FEdGraphPinType BooleanType;
		BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		for (const FName VariableName : BooleanMembers)
		{
			if (!FBlueprintEditorUtils::AddMemberVariable(
				Blueprint,
				VariableName,
				BooleanType,
				TEXT("false")))
			{
				OutError = FString::Printf(
					TEXT("LC6_FIXTURE_SHAPE_INVALID: could not add member %s"),
					*VariableName.ToString());
				return false;
			}
		}

		UEdGraph* TemplateOuter = NewObject<UEdGraph>(Blueprint);
		TemplateOuter->SetFlags(RF_Transient);

		UK2Node_CustomEvent* OpaqueEvent = SpawnCustomEvent(
			*Graph, *TemplateOuter, TEXT("LC6_OPAQUE"), FVector2f(-1200.0f, -700.0f), OutError);
		UK2Node_CallFunction* PrintString = SpawnCall(
			*Graph, *TemplateOuter, TEXT("PrintString"), FVector2f(-900.0f, -700.0f), OutError);
		UK2Node_VariableSet* OpaqueSet = SpawnBooleanSet(
			*Graph, *TemplateOuter, TEXT("LC6OpaqueDone"), FVector2f(-550.0f, -700.0f), OutError);

		UK2Node_CustomEvent* UncertainEvent = SpawnCustomEvent(
			*Graph, *TemplateOuter, TEXT("LC6_UNCERTAIN"), FVector2f(-1200.0f, -200.0f), OutError);
		UK2Node_Select* Select = SpawnBooleanSelect(
			*Graph, *TemplateOuter, FVector2f(-900.0f, 0.0f), OutError);
		UK2Node_VariableSet* UncertainSet = SpawnBooleanSet(
			*Graph, *TemplateOuter, TEXT("LC6UncertainResult"), FVector2f(-550.0f, -200.0f), OutError);

		UK2Node_CustomEvent* UnsupportedEvent = SpawnCustomEvent(
			*Graph, *TemplateOuter, TEXT("LC6_UNSUPPORTED"), FVector2f(-1200.0f, 350.0f), OutError);
		UK2Node_CallFunction* Delay = SpawnCall(
			*Graph, *TemplateOuter, TEXT("Delay"), FVector2f(-900.0f, 350.0f), OutError);
		UK2Node_VariableSet* UnsupportedSet = SpawnBooleanSet(
			*Graph, *TemplateOuter, TEXT("LC6UnsupportedDone"), FVector2f(-550.0f, 350.0f), OutError);

		UK2Node_CustomEvent* TruncatedEvent = SpawnCustomEvent(
			*Graph, *TemplateOuter, TEXT("LC6_TRUNCATED"), FVector2f(-1200.0f, 900.0f), OutError);
		TArray<UK2Node_VariableSet*> TruncatedSets;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			TruncatedSets.Add(SpawnBooleanSet(
				*Graph,
				*TemplateOuter,
				BooleanMembers[3 + Index],
				FVector2f(-900.0f + Index * 300.0f, 900.0f),
				OutError));
		}

		if (OpaqueEvent == nullptr || PrintString == nullptr || OpaqueSet == nullptr
			|| UncertainEvent == nullptr || Select == nullptr || UncertainSet == nullptr
			|| UnsupportedEvent == nullptr || Delay == nullptr || UnsupportedSet == nullptr
			|| TruncatedEvent == nullptr || TruncatedSets.Contains(nullptr))
		{
			return false;
		}

		if (!LinkPins(ExecOutput(*OpaqueEvent), ExecInput(*PrintString),
				TEXT("LC6_OPAQUE event -> PrintString"), OutError)
			|| !LinkPins(ExecOutput(*PrintString), ExecInput(*OpaqueSet),
				TEXT("LC6_OPAQUE PrintString -> Set"), OutError)
			|| !LinkPins(ExecOutput(*UncertainEvent), ExecInput(*UncertainSet),
				TEXT("LC6_UNCERTAIN event -> Set"), OutError)
			|| !LinkPins(Select->GetReturnValuePin(),
				UncertainSet->FindPin(TEXT("LC6UncertainResult"), EGPD_Input),
				TEXT("LC6_UNCERTAIN Select -> Set value"), OutError)
			|| !LinkPins(ExecOutput(*UnsupportedEvent), ExecInput(*Delay),
				TEXT("LC6_UNSUPPORTED event -> Delay"), OutError)
			|| !LinkPins(ExecOutput(*Delay), ExecInput(*UnsupportedSet),
				TEXT("LC6_UNSUPPORTED Delay -> Set"), OutError)
			|| !LinkPins(ExecOutput(*TruncatedEvent), ExecInput(*TruncatedSets[0]),
				TEXT("LC6_TRUNCATED event -> Set 01"), OutError))
		{
			return false;
		}
		for (int32 Index = 0; Index < TruncatedSets.Num() - 1; ++Index)
		{
			if (!LinkPins(
				ExecOutput(*TruncatedSets[Index]),
				ExecInput(*TruncatedSets[Index + 1]),
				TEXT("LC6_TRUNCATED Set chain"),
				OutError))
			{
				return false;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
		if (Blueprint->Status == BS_Error)
		{
			OutError = TEXT("LC6_FIXTURE_SHAPE_INVALID: Blueprint compilation failed");
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
				TEXT("LC6_FIXTURE_SHAPE_INVALID: could not save %s"),
				*Filename);
			return false;
		}
		UE_LOG(
			LogBlueprintLensLC6BoundaryFixture,
			Display,
			TEXT("LC6_BOUNDARY_FIXTURE_READY asset=%s scenarios=%d"),
			*OutAnchors.AssetObjectPath,
			OutAnchors.Scenarios.Num());
		return true;
	}
}
