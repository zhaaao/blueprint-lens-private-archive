// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintLensLC7StaticSCCFixture.h"
#include "BlueprintLensLC7StaticSCCAudit.h"
#include "BlueprintLensLC7StaticSCCFacts.h"

#include "BlueprintLensSequenceFacts.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	UEdGraphNode* FindNodeById(
		UEdGraph& Graph,
		const FString& GraphId,
		const FString& NodeId)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node != nullptr
				&& BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Node) == NodeId)
			{
				return Node;
			}
		}
		return nullptr;
	}

	int32 CountDirectedEdges(const UEdGraph& Graph, bool bExecution)
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
				if (Pin == nullptr || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				const bool bPinExecution = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
				if (bPinExecution != bExecution)
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

	bool PinsAreLinked(const UEdGraphPin* Source, const UEdGraphPin* Target)
	{
		return Source != nullptr && Target != nullptr
			&& Source->LinkedTo.Contains(Target)
			&& Target->LinkedTo.Contains(Source);
	}

	UK2Node_VariableGet* LinkedVariableGet(const UEdGraphPin* InputPin)
	{
		if (InputPin == nullptr || InputPin->LinkedTo.Num() != 1)
		{
			return nullptr;
		}
		return Cast<UK2Node_VariableGet>(InputPin->LinkedTo[0]->GetOwningNode());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7StaticSCCFixtureShapeTest,
	"BlueprintLens.Exporter.LC7StaticSCC.FixtureShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7StaticSCCFixtureShapeTest::RunTest(const FString&)
{
	BlueprintLensLC7StaticSCCFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC7StaticSCCFixture::EnsureFixture(Anchors, Error))
	{
		AddError(Error);
		return false;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (!TestNotNull(TEXT("LC7 static SCC fixture loads"), Blueprint))
	{
		return false;
	}
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("LC7 static SCC EventGraph"), Graph))
	{
		return false;
	}

	TestEqual(
		TEXT("canonical asset path"),
		Anchors.AssetObjectPath,
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC"));
	TestEqual(TEXT("one EventGraph"), Blueprint->UbergraphPages.Num(), 1);
	TestEqual(TEXT("canonical graph identity"), Anchors.GraphId, Graph->GetPathName());
	TestEqual(TEXT("fixture node count"), Graph->Nodes.Num(), 10);
	TestEqual(TEXT("fixture execution edge count"), CountDirectedEdges(*Graph, true), 6);
	TestEqual(TEXT("fixture data edge count"), CountDirectedEdges(*Graph, false), 4);
	TestTrue(TEXT("fixture compiled without error"), Blueprint->Status != BS_Error);

	UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.EventNodeId));
	UK2Node_VariableSet* Initialise = Cast<UK2Node_VariableSet>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.InitialiseNodeId));
	UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.BranchNodeId));
	UK2Node_VariableSet* Body = Cast<UK2Node_VariableSet>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.BodyNodeId));
	UK2Node_VariableSet* Advance = Cast<UK2Node_VariableSet>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.AdvanceNodeId));
	UK2Node_VariableSet* Criterion = Cast<UK2Node_VariableSet>(
		FindNodeById(*Graph, Anchors.GraphId, Anchors.CriterionNodeId));
	if (!TestNotNull(TEXT("event anchor"), Event)
		|| !TestNotNull(TEXT("initialise anchor"), Initialise)
		|| !TestNotNull(TEXT("branch anchor"), Branch)
		|| !TestNotNull(TEXT("body anchor"), Body)
		|| !TestNotNull(TEXT("advance anchor"), Advance)
		|| !TestNotNull(TEXT("criterion anchor"), Criterion))
	{
		return false;
	}

	TestEqual(TEXT("event name"), Event->CustomFunctionName, FName(TEXT("LC7_STATIC_SCC")));
	TestEqual(TEXT("initialise variable"), Initialise->GetVarName(), FName(TEXT("LC7Counter")));
	TestEqual(TEXT("body variable"), Body->GetVarName(), FName(TEXT("LC7Visited")));
	TestEqual(TEXT("advance variable"), Advance->GetVarName(), FName(TEXT("LC7Counter")));
	TestEqual(TEXT("criterion variable"), Criterion->GetVarName(), FName(TEXT("LC7Complete")));

	UK2Node_CallFunction* Less = nullptr;
	UK2Node_CallFunction* Add = nullptr;
	int32 VariableGetCount = 0;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node))
		{
			++VariableGetCount;
			TestEqual(TEXT("both Get nodes read LC7Counter"), Get->GetVarName(), FName(TEXT("LC7Counter")));
		}
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			if (Function != nullptr && Function->GetFName() == TEXT("Less_IntInt"))
			{
				Less = Call;
			}
			if (Function != nullptr && Function->GetFName() == TEXT("Add_IntInt"))
			{
				Add = Call;
			}
		}
	}
	TestEqual(TEXT("two variable Get nodes"), VariableGetCount, 2);
	if (!TestNotNull(TEXT("Less_IntInt node"), Less)
		|| !TestNotNull(TEXT("Add_IntInt node"), Add))
	{
		return false;
	}

	const UK2Node_VariableGet* PredicateGet = LinkedVariableGet(
		Less->FindPin(TEXT("A"), EGPD_Input));
	const UK2Node_VariableGet* AdvanceGet = LinkedVariableGet(
		Add->FindPin(TEXT("A"), EGPD_Input));
	TestNotNull(TEXT("predicate Get -> Less A"), PredicateGet);
	TestNotNull(TEXT("advance Get -> Add A"), AdvanceGet);
	TestTrue(TEXT("predicate and advance Gets are distinct"), PredicateGet != AdvanceGet);

	TestTrue(TEXT("event -> initialise"), PinsAreLinked(
		Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
		Initialise->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("initialise -> branch"), PinsAreLinked(
		Initialise->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
		Branch->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("branch true -> body"), PinsAreLinked(
		Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
		Body->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("body -> advance"), PinsAreLinked(
		Body->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
		Advance->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("advance -> branch return"), PinsAreLinked(
		Advance->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output),
		Branch->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("branch false -> criterion"), PinsAreLinked(
		Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output),
		Criterion->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)));
	TestTrue(TEXT("Less result -> branch condition"), PinsAreLinked(
		Less->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
		Branch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input)));
	TestTrue(TEXT("Add result -> advance value"), PinsAreLinked(
		Add->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
		Advance->FindPin(TEXT("LC7Counter"), EGPD_Input)));

	TMap<FName, FName> ExpectedMembers = {
		{TEXT("LC7Counter"), UEdGraphSchema_K2::PC_Int},
		{TEXT("LC7Visited"), UEdGraphSchema_K2::PC_Boolean},
		{TEXT("LC7Complete"), UEdGraphSchema_K2::PC_Boolean}};
	TSet<FGuid> MemberGuids;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (const FName* ExpectedType = ExpectedMembers.Find(Variable.VarName))
		{
			TestEqual(
				*FString::Printf(TEXT("%s type"), *Variable.VarName.ToString()),
				Variable.VarType.PinCategory,
				*ExpectedType);
			TestTrue(
				*FString::Printf(TEXT("%s GUID"), *Variable.VarName.ToString()),
				Variable.VarGuid.IsValid());
			MemberGuids.Add(Variable.VarGuid);
			ExpectedMembers.Remove(Variable.VarName);
		}
	}
	TestEqual(TEXT("all fixture variables exist"), ExpectedMembers.Num(), 0);
	TestEqual(TEXT("three unique fixture variable GUIDs"), MemberGuids.Num(), 3);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7StaticSCCFactsAndAuditTest,
	"BlueprintLens.Exporter.LC7StaticSCC.FactsAndAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7StaticSCCFactsAndAuditTest::RunTest(const FString&)
{
	BlueprintLensLC7StaticSCCFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC7StaticSCCFixture::EnsureFixture(Anchors, Error))
	{
		AddError(Error);
		return false;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (!TestNotNull(TEXT("LC7 source Blueprint loads"), Blueprint))
	{
		return false;
	}
	UPackage* SourcePackage = Blueprint->GetOutermost();
	const bool bSourcePackageWasDirty = SourcePackage->IsDirty();
	TestFalse(TEXT("mutation source package starts clean"), bSourcePackageWasDirty);

	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LC7StaticSCCFacts")));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString RawPath = FPaths::Combine(OutputDirectory, TEXT("fixture.raw-0.2.json"));
	TestTrue(
		TEXT("raw provenance placeholder writes"),
		FFileHelper::SaveStringToFile(
			TEXT("{\"format\":\"blueprint-lens-raw\"}\n"),
			*RawPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FString SourcePath;
	FString AuditPath;
	BlueprintLensLC7StaticSCCFacts::FSCCFactStats SourceStats;
	BlueprintLensLC7StaticSCCAudit::FSCCAuditStats AuditStats;
	TestTrue(
		TEXT("LC7 source facts succeed"),
		BlueprintLensLC7StaticSCCFacts::ExportSCCFacts(
			*Blueprint,
			Anchors,
			RawPath,
			OutputDirectory,
			SourcePath,
			SourceStats,
			Error));
	if (!Error.IsEmpty())
	{
		AddInfo(Error);
	}
	TestTrue(
		TEXT("LC7 independent audit succeeds"),
		BlueprintLensLC7StaticSCCAudit::AuditSCCSource(
			*Blueprint,
			Anchors,
			RawPath,
			OutputDirectory,
			AuditPath,
			AuditStats,
			Error));
	if (!Error.IsEmpty())
	{
		AddInfo(Error);
	}

	for (const TPair<FString, int32>& Expected : TMap<FString, int32>{
		{TEXT("source nodes"), SourceStats.NodeCount},
		{TEXT("audit nodes"), AuditStats.NodeCount}})
	{
		TestEqual(*Expected.Key, Expected.Value, 10);
	}
	for (const TPair<FString, int32>& Expected : TMap<FString, int32>{
		{TEXT("source edges"), SourceStats.EdgeCount},
		{TEXT("audit edges"), AuditStats.EdgeCount}})
	{
		TestEqual(*Expected.Key, Expected.Value, 10);
	}
	TestEqual(TEXT("source SCC members"), SourceStats.MemberCount, 3);
	TestEqual(TEXT("source SCC internal edges"), SourceStats.InternalEdgeCount, 3);
	TestEqual(TEXT("source SCC incoming edges"), SourceStats.IncomingEdgeCount, 1);
	TestEqual(TEXT("source SCC outgoing edges"), SourceStats.OutgoingEdgeCount, 1);
	TestEqual(TEXT("audit SCC members"), AuditStats.MemberCount, 3);
	TestEqual(TEXT("audit SCC internal edges"), AuditStats.InternalEdgeCount, 3);
	TestEqual(TEXT("audit SCC incoming edges"), AuditStats.IncomingEdgeCount, 1);
	TestEqual(TEXT("audit SCC outgoing edges"), AuditStats.OutgoingEdgeCount, 1);

	FString SourceText;
	FString AuditText;
	TestTrue(TEXT("source JSON is readable"), FFileHelper::LoadFileToString(SourceText, *SourcePath));
	TestTrue(TEXT("audit TSV is readable"), FFileHelper::LoadFileToString(AuditText, *AuditPath));
	TSharedPtr<FJsonObject> SourceJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SourceText);
	TestTrue(TEXT("source JSON parses"), FJsonSerializer::Deserialize(Reader, SourceJson));
	if (SourceJson.IsValid())
	{
		TestEqual(
			TEXT("source format"),
			SourceJson->GetStringField(TEXT("format")),
			TEXT("blueprint-lens-lc7-static-scc-source"));
		TestEqual(
			TEXT("source criterion"),
			SourceJson->GetStringField(TEXT("criterion_node_id")),
			Anchors.CriterionNodeId);
		const TSharedPtr<FJsonObject>* SCC = nullptr;
		if (TestTrue(TEXT("source SCC object"), SourceJson->TryGetObjectField(TEXT("scc"), SCC))
			&& SCC != nullptr)
		{
			TestEqual(TEXT("source SCC entry"), (*SCC)->GetStringField(TEXT("entry_node_id")), Anchors.BranchNodeId);
			TestEqual(TEXT("source SCC exit"), (*SCC)->GetStringField(TEXT("exit_node_id")), Anchors.BranchNodeId);
			TestEqual(TEXT("source SCC member array"), (*SCC)->GetArrayField(TEXT("member_node_ids")).Num(), 3);
			TestEqual(TEXT("source SCC internal array"), (*SCC)->GetArrayField(TEXT("internal_edge_ids")).Num(), 3);
			TestEqual(TEXT("source SCC incoming array"), (*SCC)->GetArrayField(TEXT("incoming_edge_ids")).Num(), 1);
			TestEqual(TEXT("source SCC outgoing array"), (*SCC)->GetArrayField(TEXT("outgoing_edge_ids")).Num(), 1);
			TestEqual(TEXT("source SCC returning array"), (*SCC)->GetArrayField(TEXT("returning_edge_ids")).Num(), 1);
		}
	}
	TestTrue(
		TEXT("audit format"),
		AuditText.StartsWith(TEXT("FORMAT\tblueprint-lens-lc7-static-scc-audit\t1.0.0")));
	TestTrue(TEXT("audit entry is Branch"), AuditText.Contains(*FString::Printf(TEXT("SCC_ENTRY\t%s"), *Anchors.BranchNodeId)));
	TestTrue(TEXT("audit exit is Branch"), AuditText.Contains(*FString::Printf(TEXT("SCC_EXIT\t%s"), *Anchors.BranchNodeId)));
	TestTrue(TEXT("audit criterion remains outside"), AuditText.Contains(*FString::Printf(TEXT("CRITERION\t%s"), *Anchors.CriterionNodeId)));
	TestTrue(TEXT("audit counts"), AuditText.Contains(TEXT("COUNTS\t10\t10\t3\t3\t1\t1")));

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	UEdGraphNode* Advance = FindNodeById(*Graph, Anchors.GraphId, Anchors.AdvanceNodeId);
	UEdGraphNode* Branch = FindNodeById(*Graph, Anchors.GraphId, Anchors.BranchNodeId);
	UEdGraphNode* Event = FindNodeById(*Graph, Anchors.GraphId, Anchors.EventNodeId);
	UEdGraphNode* Body = FindNodeById(*Graph, Anchors.GraphId, Anchors.BodyNodeId);
	UEdGraphNode* Criterion = FindNodeById(*Graph, Anchors.GraphId, Anchors.CriterionNodeId);
	if (!TestNotNull(TEXT("mutation advance"), Advance)
		|| !TestNotNull(TEXT("mutation branch"), Branch)
		|| !TestNotNull(TEXT("mutation event"), Event)
		|| !TestNotNull(TEXT("mutation body"), Body)
		|| !TestNotNull(TEXT("mutation criterion"), Criterion))
	{
		return false;
	}

	auto RequireBothReject = [this, Blueprint, &Anchors, &RawPath, &OutputDirectory](
		const TCHAR* Description)
	{
		FString MutationError;
		FString MutationPath;
		BlueprintLensLC7StaticSCCFacts::FSCCFactStats MutationSourceStats;
		BlueprintLensLC7StaticSCCAudit::FSCCAuditStats MutationAuditStats;
		const bool bFactsAccepted = BlueprintLensLC7StaticSCCFacts::ExportSCCFacts(
			*Blueprint,
			Anchors,
			RawPath,
			FPaths::Combine(OutputDirectory, TEXT("mutation-facts")),
			MutationPath,
			MutationSourceStats,
			MutationError);
		const bool bAuditAccepted = BlueprintLensLC7StaticSCCAudit::AuditSCCSource(
			*Blueprint,
			Anchors,
			RawPath,
			FPaths::Combine(OutputDirectory, TEXT("mutation-audit")),
			MutationPath,
			MutationAuditStats,
			MutationError);
		TestFalse(FString::Printf(TEXT("facts reject %s"), Description), bFactsAccepted);
		TestFalse(FString::Printf(TEXT("audit rejects %s"), Description), bAuditAccepted);
	};

	UEdGraphPin* ReturnSource = Advance->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* BranchInput = Branch->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (TestNotNull(TEXT("return source"), ReturnSource)
		&& TestNotNull(TEXT("branch input"), BranchInput))
	{
		ReturnSource->BreakLinkTo(BranchInput);
		RequireBothReject(TEXT("missing return edge"));
		ReturnSource->MakeLinkTo(BranchInput);
	}

	UEdGraphPin* EventOutput = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if (TestNotNull(TEXT("extra incoming source"), EventOutput))
	{
		EventOutput->MakeLinkTo(BranchInput);
		RequireBothReject(TEXT("extra incoming edge"));
		EventOutput->BreakLinkTo(BranchInput);
	}

	UEdGraphPin* BodyOutput = Body->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* CriterionInput = Criterion->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (TestNotNull(TEXT("extra outgoing source"), BodyOutput)
		&& TestNotNull(TEXT("criterion input"), CriterionInput))
	{
		BodyOutput->MakeLinkTo(CriterionInput);
		RequireBothReject(TEXT("extra outgoing edge"));
		BodyOutput->BreakLinkTo(CriterionInput);
	}

	UEdGraphPin* CriterionOutput = Criterion->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if (TestNotNull(TEXT("criterion output"), CriterionOutput))
	{
		CriterionOutput->MakeLinkTo(BranchInput);
		RequireBothReject(TEXT("criterion inside SCC"));
		CriterionOutput->BreakLinkTo(BranchInput);
	}

	BlueprintLensLC7StaticSCCFixture::FFixtureAnchors WrongBranch = Anchors;
	WrongBranch.BranchNodeId = Anchors.BodyNodeId;
	FString RejectedPath;
	BlueprintLensLC7StaticSCCFacts::FSCCFactStats RejectedSourceStats;
	BlueprintLensLC7StaticSCCAudit::FSCCAuditStats RejectedAuditStats;
	TestFalse(
		TEXT("facts reject wrong entry/exit anchor"),
		BlueprintLensLC7StaticSCCFacts::ExportSCCFacts(
			*Blueprint,
			WrongBranch,
			RawPath,
			FPaths::Combine(OutputDirectory, TEXT("wrong-anchor-facts")),
			RejectedPath,
			RejectedSourceStats,
			Error));
	TestFalse(
		TEXT("audit rejects wrong entry/exit anchor"),
		BlueprintLensLC7StaticSCCAudit::AuditSCCSource(
			*Blueprint,
			WrongBranch,
			RawPath,
			FPaths::Combine(OutputDirectory, TEXT("wrong-anchor-audit")),
			RejectedPath,
			RejectedAuditStats,
			Error));
	SourcePackage->SetDirtyFlag(bSourcePackageWasDirty);
	TestFalse(
		TEXT("mutation matrix leaves the source package clean"),
		SourcePackage->IsDirty());

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7StaticSCCCaptureCommandTest,
	"BlueprintLens.Exporter.LC7StaticSCC.CaptureCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7StaticSCCCaptureCommandTest::RunTest(const FString&)
{
	IConsoleObject* CaptureObject = IConsoleManager::Get().FindConsoleObject(
		TEXT("BlueprintLens.CaptureLC7StaticSCCTruth"));
	if (!TestNotNull(TEXT("LC7 capture command is registered"), CaptureObject))
	{
		return false;
	}

	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LC7StaticSCCCaptureCommand")));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString CaptureCommand = FString::Printf(
		TEXT("BlueprintLens.CaptureLC7StaticSCCTruth \"%s\""),
		*OutputDirectory);
	TestTrue(
		TEXT("LC7 capture command dispatches"),
		IConsoleManager::Get().ProcessUserConsoleInput(*CaptureCommand, *GLog, nullptr));

	for (const FString& ProductName : TArray<FString>{
		TEXT("BP_LC7_StaticSCC.raw-0.2.json"),
		TEXT("BP_LC7_StaticSCC.scc-source.json"),
		TEXT("BP_LC7_StaticSCC.scc-audit.tsv")})
	{
		const FString ProductPath = FPaths::Combine(OutputDirectory, ProductName);
		TestTrue(
			*FString::Printf(TEXT("capture writes nonempty %s"), *ProductName),
			IFileManager::Get().FileSize(*ProductPath) > 0);
	}

	AddExpectedError(
		TEXT("LC7_FIXTURE_SHAPE_INVALID: Usage: BlueprintLens.CaptureLC7StaticSCCTruth <absolute-output-directory>"),
		EAutomationExpectedErrorFlags::Exact,
		3);
	TestTrue(
		TEXT("LC7 capture command dispatches zero-argument rejection"),
		IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("BlueprintLens.CaptureLC7StaticSCCTruth"), *GLog, nullptr));
	TestTrue(
		TEXT("LC7 capture command dispatches multiple-argument rejection"),
		IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("BlueprintLens.CaptureLC7StaticSCCTruth C:\\first C:\\second"), *GLog, nullptr));
	TestTrue(
		TEXT("LC7 capture command dispatches relative-path rejection"),
		IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("BlueprintLens.CaptureLC7StaticSCCTruth relative-output"), *GLog, nullptr));

	return !HasAnyErrors();
}

#endif
