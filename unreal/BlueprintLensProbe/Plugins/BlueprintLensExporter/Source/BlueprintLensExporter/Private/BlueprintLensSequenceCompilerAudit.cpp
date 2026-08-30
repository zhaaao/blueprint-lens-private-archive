// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensSequenceCompilerAudit.h"

#include "BlueprintLensSequenceFacts.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace BlueprintLensSequenceCompilerAudit
{
	namespace
	{
		bool FindSequenceNode(
			const UBlueprint& Blueprint,
			const FString& SequenceNodeId,
			UK2Node_ExecutionSequence*& OutSequence,
			UEdGraph*& OutGraph,
			FString& OutError)
		{
			TArray<UEdGraph*> Graphs;
			Blueprint.GetAllGraphs(Graphs);
			int32 Matches = 0;
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph == nullptr)
				{
					continue;
				}
				const FString GraphId = Graph->GetPathName();
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node);
					if (Sequence != nullptr
						&& BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Sequence) == SequenceNodeId)
					{
						++Matches;
						OutSequence = Sequence;
						OutGraph = Graph;
					}
				}
			}
			if (Matches != 1 || OutSequence == nullptr || OutGraph == nullptr)
			{
				OutError = FString::Printf(
					TEXT("Sequence root must resolve exactly once: %s (matches=%d)"),
					*SequenceNodeId,
					Matches);
				return false;
			}
			return true;
		}
	}

	bool AuditSequenceCompilerOrder(
		const UBlueprint& Blueprint,
		const FString& SequenceNodeId,
		FString& OutFilePath,
		FSequenceCompilerAuditStats& OutStats,
		FString& OutError)
	{
		OutStats = FSequenceCompilerAuditStats();
		OutFilePath.Reset();
		OutError.Reset();
		UK2Node_ExecutionSequence* Sequence = nullptr;
		UEdGraph* Graph = nullptr;
		if (!FindSequenceNode(Blueprint, SequenceNodeId, Sequence, Graph, OutError))
		{
			return false;
		}

		TArray<FString> Lines;
		Lines.Add(TEXT("FORMAT\tblueprint-lens-sequence-compiler-order\t1.0.0"));
		Lines.Add(FString::Printf(TEXT("BLUEPRINT\t%s"), *Blueprint.GetPathName()));
		Lines.Add(FString::Printf(TEXT("GRAPH\t%s"), *Graph->GetPathName()));
		Lines.Add(FString::Printf(TEXT("SEQUENCE\t%s"), *SequenceNodeId));

		// Deliberately mirrors FKCHandler_ExecutionSequence in UE 5.8.1. Do not
		// replace this traversal with GetThenPinGivenIndex or a shared order helper.
		for (UEdGraphPin* CurrentPin : Sequence->Pins)
		{
			if (CurrentPin != nullptr
				&& CurrentPin->Direction == EGPD_Output
				&& CurrentPin->LinkedTo.Num() > 0
				&& CurrentPin->PinName.ToString().StartsWith(UEdGraphSchema_K2::PN_Then.ToString()))
			{
				const FString PinId = BlueprintLensSequenceFacts::MakePinId(
					SequenceNodeId,
					*CurrentPin);
				Lines.Add(FString::Printf(
					TEXT("OUTPUT\t%d\t%s\t%s\t%d"),
					OutStats.ConnectedOutputCount,
					*PinId,
					*CurrentPin->PinName.ToString(),
					CurrentPin->LinkedTo.Num()));
				++OutStats.ConnectedOutputCount;
			}
		}
		Lines.Add(FString::Printf(TEXT("COUNTS\t%d"), OutStats.ConnectedOutputCount));

		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("SequenceAudits"));
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(TEXT("Could not create Sequence audit directory: %s"), *OutputDirectory);
			return false;
		}
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory,
			FString::Printf(TEXT("%s.sequence-compiler-order.tsv"), *Blueprint.GetName())));
		if (!FFileHelper::SaveStringArrayToFile(
			Lines,
			*OutFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write Sequence compiler audit TSV: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UK2Node_IfThenElse* AddBranchTarget(UEdGraph& Graph)
	{
		UK2Node_IfThenElse* Target = NewObject<UK2Node_IfThenElse>(&Graph);
		Graph.AddNode(Target, false, false);
		Target->CreateNewGuid();
		Target->PostPlacedNewNode();
		Target->AllocateDefaultPins();
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensSequenceOrderProducersTest,
	"BlueprintLens.Exporter.SequenceOrderProducers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensSequenceOrderProducersTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>(
		GetTransientPackage(),
		TEXT("BlueprintLensSequenceOrderTest"),
		RF_Transient);
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		TEXT("EventGraph"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	if (!TestNotNull(TEXT("transient EventGraph"), Graph))
	{
		return false;
	}
	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);

	UK2Node_ExecutionSequence* Sequence = NewObject<UK2Node_ExecutionSequence>(Graph);
	Graph->AddNode(Sequence, false, false);
	Sequence->CreateNewGuid();
	Sequence->PostPlacedNewNode();
	Sequence->AllocateDefaultPins();
	Sequence->AddInputPin();
	Sequence->AddInputPin();

	UK2Node_IfThenElse* Target0 = AddBranchTarget(*Graph);
	UK2Node_IfThenElse* Target2 = AddBranchTarget(*Graph);
	UEdGraphPin* Target0Exec = Target0->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	UEdGraphPin* Target2Exec = Target2->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (!TestNotNull(TEXT("target 0 exec input"), Target0Exec)
		|| !TestNotNull(TEXT("target 2 exec input"), Target2Exec))
	{
		return false;
	}
	Sequence->GetThenPinGivenIndex(0)->MakeLinkTo(Target0Exec);
	Sequence->GetThenPinGivenIndex(2)->MakeLinkTo(Target2Exec);

	const FString SequenceNodeId = BlueprintLensSequenceFacts::MakeNodeId(
		Graph->GetPathName(),
		*Sequence);
	FString FactsPath;
	FString AuditPath;
	FString Error;
	BlueprintLensSequenceFacts::FSequenceFactStats FactStats;
	BlueprintLensSequenceCompilerAudit::FSequenceCompilerAuditStats AuditStats;
	if (!TestTrue(
		TEXT("Sequence facts export succeeds"),
		BlueprintLensSequenceFacts::ExportSequenceFacts(
			*Blueprint,
			SequenceNodeId,
			FactsPath,
			FactStats,
			Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(
		TEXT("Sequence compiler audit succeeds"),
		BlueprintLensSequenceCompilerAudit::AuditSequenceCompilerOrder(
			*Blueprint,
			SequenceNodeId,
			AuditPath,
			AuditStats,
			Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("four declared outputs"), FactStats.DeclaredOutputCount, 4);
	TestEqual(TEXT("two connected outputs in source facts"), FactStats.ConnectedOutputCount, 2);
	TestEqual(TEXT("two unconnected outputs in source facts"), FactStats.UnconnectedOutputCount, 2);
	TestEqual(TEXT("two connected compiler outputs"), AuditStats.ConnectedOutputCount, 2);

	FString JsonText;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("Sequence source JSON readable"), FFileHelper::LoadFileToString(JsonText, *FactsPath)))
	{
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!TestTrue(
			TEXT("Sequence source JSON parses"),
			FJsonSerializer::Deserialize(Reader, Root))
		|| !TestNotNull(TEXT("Sequence source JSON root"), Root.Get()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	if (!TestTrue(TEXT("Sequence source outputs exist"), Root->TryGetArrayField(TEXT("outputs"), Outputs))
		|| Outputs == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Sequence source output record count"), Outputs->Num(), 4);
	const FString SourcePin0 = (*Outputs)[0]->AsObject()->GetStringField(TEXT("source_pin_id"));
	const FString SourcePin2 = (*Outputs)[2]->AsObject()->GetStringField(TEXT("source_pin_id"));
	TestEqual(
		TEXT("ordinal 1 stays unconnected"),
		(*Outputs)[1]->AsObject()->GetStringField(TEXT("connection_state")),
		TEXT("unconnected"));

	TArray<FString> Lines;
	if (!TestTrue(TEXT("Sequence compiler TSV readable"), FFileHelper::LoadFileToStringArray(Lines, *AuditPath)))
	{
		return false;
	}
	TArray<TArray<FString>> OutputRows;
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("OUTPUT\t")))
		{
			TArray<FString> Fields;
			Line.ParseIntoArray(Fields, TEXT("\t"), false);
			OutputRows.Add(Fields);
		}
	}
	TestEqual(TEXT("compiler audit output rows"), OutputRows.Num(), 2);
	if (OutputRows.Num() == 2)
	{
		TestEqual(TEXT("compiler rank 0 matches API ordinal 0"), OutputRows[0][2], SourcePin0);
		TestEqual(TEXT("compiler rank 1 matches API ordinal 2"), OutputRows[1][2], SourcePin2);
	}
	return !HasAnyErrors();
}

#endif
