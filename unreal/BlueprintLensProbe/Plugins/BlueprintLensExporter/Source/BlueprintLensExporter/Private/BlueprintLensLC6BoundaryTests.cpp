// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC6BoundaryFixture.h"
#include "BlueprintLensLC6BoundaryFacts.h"
#include "BlueprintLensLC6BoundaryAudit.h"
#include "BlueprintLensSequenceFacts.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Select.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FObservedComponent
	{
		TSet<const UEdGraphNode*> Nodes;
		int32 EdgeCount = 0;
	};

	TArray<FObservedComponent> BuildComponents(const UEdGraph& Graph)
	{
		TMap<const UEdGraphNode*, TSet<const UEdGraphNode*>> Adjacency;
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node == nullptr)
			{
				continue;
			}
			Adjacency.FindOrAdd(Node);
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin == nullptr)
				{
					continue;
				}
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					const UEdGraphNode* Other = LinkedPin == nullptr
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

		TArray<FObservedComponent> Components;
		TSet<const UEdGraphNode*> Visited;
		for (const TPair<const UEdGraphNode*, TSet<const UEdGraphNode*>>& Pair : Adjacency)
		{
			if (Visited.Contains(Pair.Key))
			{
				continue;
			}
			FObservedComponent Component;
			TArray<const UEdGraphNode*> Pending = {Pair.Key};
			while (!Pending.IsEmpty())
			{
				const UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);
				if (Current == nullptr || Visited.Contains(Current))
				{
					continue;
				}
				Visited.Add(Current);
				Component.Nodes.Add(Current);
				for (const UEdGraphNode* Other : Adjacency.FindChecked(Current))
				{
					if (!Visited.Contains(Other))
					{
						Pending.Add(Other);
					}
				}
			}

			for (const UEdGraphNode* Node : Component.Nodes)
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
						Component.EdgeCount += Target != nullptr && Component.Nodes.Contains(Target)
							? 1
							: 0;
					}
				}
			}
			Components.Add(MoveTemp(Component));
		}
		return Components;
	}

	const UEdGraphNode* FindNodeById(const UEdGraph& Graph, const FString& NodeId)
	{
		const FString GraphId = Graph.GetPathName();
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node != nullptr
				&& BlueprintLensSequenceFacts::MakeNodeId(GraphId, *Node) == NodeId)
			{
				return Node;
			}
		}
		return nullptr;
	}

	const FObservedComponent* FindOwningComponent(
		const TArray<FObservedComponent>& Components,
		const UEdGraphNode* Root,
		const UEdGraphNode* Criterion)
	{
		for (const FObservedComponent& Component : Components)
		{
			if (Component.Nodes.Contains(Root) && Component.Nodes.Contains(Criterion))
			{
				return &Component;
			}
		}
		return nullptr;
	}

	bool HasDirectedEdge(const UEdGraphNode& Source, const UEdGraphNode& Target)
	{
		for (const UEdGraphPin* Pin : Source.Pins)
		{
			if (Pin == nullptr || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin != nullptr && LinkedPin->GetOwningNode() == &Target)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool IsFrozenCoreSupportedNode(const UEdGraphNode& Node)
	{
		return Node.IsA<UK2Node_CustomEvent>() || Node.IsA<UK2Node_VariableSet>();
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

	template <typename ElementType>
	bool SetsEqual(const TSet<ElementType>& Left, const TSet<ElementType>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const ElementType& Element : Left)
		{
			if (!Right.Contains(Element))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6FixtureShapeTest,
	"BlueprintLens.Exporter.LC6FixtureShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6FixtureShapeTest::RunTest(const FString&)
{
	BlueprintLensLC6BoundaryFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC6BoundaryFixture::EnsureFixture(Anchors, Error))
	{
		AddError(FString::Printf(TEXT("LC6_FIXTURE_SHAPE_INVALID: %s"), *Error));
		return false;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (!TestNotNull(TEXT("LC6 boundary fixture loads"), Blueprint))
	{
		return false;
	}
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("LC6 boundary fixture EventGraph"), EventGraph))
	{
		return false;
	}

	TestEqual(TEXT("canonical asset path"), Anchors.AssetObjectPath,
		TEXT("/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix"));
	TestEqual(TEXT("exactly one EventGraph"), Blueprint->UbergraphPages.Num(), 1);
	TestEqual(TEXT("canonical graph identity"), Anchors.GraphId, EventGraph->GetPathName());
	TestEqual(TEXT("fixture scenario count"), Anchors.Scenarios.Num(), 4);
	TestEqual(TEXT("EventGraph node count"), EventGraph->Nodes.Num(), 16);

	const TArray<FObservedComponent> Components = BuildComponents(*EventGraph);
	TestEqual(TEXT("four disconnected source components"), Components.Num(), 4);
	int32 ObservedEdgeCount = 0;
	for (const FObservedComponent& Component : Components)
	{
		ObservedEdgeCount += Component.EdgeCount;
	}
	TestEqual(TEXT("EventGraph edge count"), ObservedEdgeCount, 12);

	const TMap<FString, int32> ExpectedNodeCounts = {
		{TEXT("LC6_OPAQUE"), 3},
		{TEXT("LC6_UNCERTAIN"), 3},
		{TEXT("LC6_UNSUPPORTED"), 3},
		{TEXT("LC6_TRUNCATED"), 7}};
	const TMap<FString, int32> ExpectedEdgeCounts = {
		{TEXT("LC6_OPAQUE"), 2},
		{TEXT("LC6_UNCERTAIN"), 2},
		{TEXT("LC6_UNSUPPORTED"), 2},
		{TEXT("LC6_TRUNCATED"), 6}};
	const TMap<FString, FString> ExpectedCriteria = {
		{TEXT("LC6_OPAQUE"), TEXT("LC6OpaqueDone")},
		{TEXT("LC6_UNCERTAIN"), TEXT("LC6UncertainResult")},
		{TEXT("LC6_UNSUPPORTED"), TEXT("LC6UnsupportedDone")},
		{TEXT("LC6_TRUNCATED"), TEXT("LC6Truncated06")}};
	TSet<FString> ScenarioIds;
	TSet<FString> RootIds;
	TSet<FString> CriterionIds;
	const FObservedComponent* TruncationComponent = nullptr;
	const UEdGraphNode* TruncationCriterion = nullptr;
	for (const BlueprintLensLC6BoundaryFixture::FScenarioAnchors& Scenario : Anchors.Scenarios)
	{
		ScenarioIds.Add(Scenario.ScenarioId);
		RootIds.Add(Scenario.RootNodeId);
		CriterionIds.Add(Scenario.CriterionNodeId);
		const UEdGraphNode* Root = FindNodeById(*EventGraph, Scenario.RootNodeId);
		const UEdGraphNode* Criterion = FindNodeById(*EventGraph, Scenario.CriterionNodeId);
		TestNotNull(*FString::Printf(TEXT("%s root resolves"), *Scenario.ScenarioId), Root);
		TestNotNull(*FString::Printf(TEXT("%s criterion resolves"), *Scenario.ScenarioId), Criterion);
		if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Root))
		{
			TestEqual(*FString::Printf(TEXT("%s root name"), *Scenario.ScenarioId),
				Event->CustomFunctionName.ToString(), Scenario.ScenarioId);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s root is not a Custom Event"), *Scenario.ScenarioId));
		}
		if (const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Criterion))
		{
			TestEqual(*FString::Printf(TEXT("%s criterion variable"), *Scenario.ScenarioId),
				Set->GetVarName().ToString(), ExpectedCriteria.FindChecked(Scenario.ScenarioId));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s criterion is not a Variable Set"), *Scenario.ScenarioId));
		}
		const FObservedComponent* Component = FindOwningComponent(Components, Root, Criterion);
		TestNotNull(*FString::Printf(TEXT("%s owns one component"), *Scenario.ScenarioId), Component);
		if (Component != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s node count"), *Scenario.ScenarioId),
				Component->Nodes.Num(), ExpectedNodeCounts.FindChecked(Scenario.ScenarioId));
			TestEqual(*FString::Printf(TEXT("%s edge count"), *Scenario.ScenarioId),
				Component->EdgeCount, ExpectedEdgeCounts.FindChecked(Scenario.ScenarioId));
			if (Scenario.ScenarioId == TEXT("LC6_TRUNCATED"))
			{
				TruncationComponent = Component;
				TruncationCriterion = Criterion;
			}
		}
	}
	TestEqual(TEXT("four unique scenario ids"), ScenarioIds.Num(), 4);
	TestEqual(TEXT("four unique root ids"), RootIds.Num(), 4);
	TestEqual(TEXT("four unique criterion ids"), CriterionIds.Num(), 4);
	TestTrue(TEXT("scenario identity set"), SetsEqual(ScenarioIds, TSet<FString>({
		TEXT("LC6_OPAQUE"), TEXT("LC6_UNCERTAIN"),
		TEXT("LC6_UNSUPPORTED"), TEXT("LC6_TRUNCATED")})));
	if (TestNotNull(TEXT("truncation component resolves"), TruncationComponent)
		&& TestNotNull(TEXT("truncation criterion resolves"), TruncationCriterion))
	{
		int32 CriterionAnchorsInComponent = 0;
		for (const BlueprintLensLC6BoundaryFixture::FScenarioAnchors& Scenario : Anchors.Scenarios)
		{
			const UEdGraphNode* Candidate = FindNodeById(*EventGraph, Scenario.CriterionNodeId);
			CriterionAnchorsInComponent += Candidate != nullptr
				&& TruncationComponent->Nodes.Contains(Candidate) ? 1 : 0;
		}
		TestEqual(TEXT("Set 06 is the only truncation criterion"), CriterionAnchorsInComponent, 1);
		for (const UEdGraphNode* Node : TruncationComponent->Nodes)
		{
			if (Node != nullptr)
			{
				TestTrue(
					*FString::Printf(TEXT("truncation node %s is core-supported"), *Node->GetName()),
					IsFrozenCoreSupportedNode(*Node));
			}
		}
	}

	int32 PrintStringCount = 0;
	int32 LatentDelayCount = 0;
	int32 SelectCount = 0;
	int32 TruncationSetCount = 0;
	const UK2Node_CustomEvent* OpaqueEvent = nullptr;
	const UK2Node_CallFunction* PrintString = nullptr;
	const UK2Node_VariableSet* OpaqueSet = nullptr;
	const UK2Node_CustomEvent* UncertainEvent = nullptr;
	const UK2Node_Select* Select = nullptr;
	const UK2Node_VariableSet* UncertainSet = nullptr;
	const UK2Node_CustomEvent* UnsupportedEvent = nullptr;
	const UK2Node_CallFunction* Delay = nullptr;
	const UK2Node_VariableSet* UnsupportedSet = nullptr;
	const UK2Node_CustomEvent* TruncatedEvent = nullptr;
	TMap<FName, const UK2Node_VariableSet*> TruncatedSets;
	for (const UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
		{
			OpaqueEvent = Event->CustomFunctionName == TEXT("LC6_OPAQUE") ? Event : OpaqueEvent;
			UncertainEvent = Event->CustomFunctionName == TEXT("LC6_UNCERTAIN") ? Event : UncertainEvent;
			UnsupportedEvent = Event->CustomFunctionName == TEXT("LC6_UNSUPPORTED") ? Event : UnsupportedEvent;
			TruncatedEvent = Event->CustomFunctionName == TEXT("LC6_TRUNCATED") ? Event : TruncatedEvent;
		}
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			if (Function != nullptr && Function->GetFName() == TEXT("PrintString"))
			{
				++PrintStringCount;
				PrintString = Call;
				TestFalse(TEXT("PrintString is non-latent"), Function->HasMetaData(TEXT("Latent")));
			}
			if (Function != nullptr && Function->GetFName() == TEXT("Delay"))
			{
				++LatentDelayCount;
				Delay = Call;
				TestTrue(TEXT("Delay is latent"), Function->HasMetaData(TEXT("Latent")));
			}
		}
		if (const UK2Node_Select* Candidate = Cast<UK2Node_Select>(Node))
		{
			++SelectCount;
			Select = Candidate;
		}
		if (const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
		{
			const FName VariableName = Set->GetVarName();
			OpaqueSet = VariableName == TEXT("LC6OpaqueDone") ? Set : OpaqueSet;
			UncertainSet = VariableName == TEXT("LC6UncertainResult") ? Set : UncertainSet;
			UnsupportedSet = VariableName == TEXT("LC6UnsupportedDone") ? Set : UnsupportedSet;
			if (VariableName.ToString().StartsWith(TEXT("LC6Truncated")))
			{
				++TruncationSetCount;
				TruncatedSets.Add(VariableName, Set);
			}
		}
	}
	TestEqual(TEXT("one non-latent PrintString call"), PrintStringCount, 1);
	TestEqual(TEXT("one latent Delay call"), LatentDelayCount, 1);
	TestEqual(TEXT("one real Select node"), SelectCount, 1);
	TestEqual(TEXT("six supported truncation Sets"), TruncationSetCount, 6);
	if (TestNotNull(TEXT("opaque event"), OpaqueEvent)
		&& TestNotNull(TEXT("PrintString node"), PrintString)
		&& TestNotNull(TEXT("opaque criterion Set"), OpaqueSet))
	{
		TestTrue(TEXT("opaque event -> PrintString"), HasDirectedEdge(*OpaqueEvent, *PrintString));
		TestTrue(TEXT("PrintString -> opaque Set"), HasDirectedEdge(*PrintString, *OpaqueSet));
	}
	if (TestNotNull(TEXT("uncertain event"), UncertainEvent)
		&& TestNotNull(TEXT("uncertain Select"), Select)
		&& TestNotNull(TEXT("uncertain criterion Set"), UncertainSet))
	{
		TestTrue(TEXT("uncertain event -> Set"), HasDirectedEdge(*UncertainEvent, *UncertainSet));
		TestTrue(TEXT("Select -> uncertain Set value"), HasDirectedEdge(*Select, *UncertainSet));
	}
	if (TestNotNull(TEXT("unsupported event"), UnsupportedEvent)
		&& TestNotNull(TEXT("Delay node"), Delay)
		&& TestNotNull(TEXT("unsupported criterion Set"), UnsupportedSet))
	{
		TestTrue(TEXT("unsupported event -> Delay"), HasDirectedEdge(*UnsupportedEvent, *Delay));
		TestTrue(TEXT("Delay -> unsupported Set"), HasDirectedEdge(*Delay, *UnsupportedSet));
	}
	if (TestNotNull(TEXT("truncated event"), TruncatedEvent)
		&& TestEqual(TEXT("six named truncation Sets"), TruncatedSets.Num(), 6))
	{
		const UK2Node_VariableSet* Previous = nullptr;
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			const FName VariableName(*FString::Printf(TEXT("LC6Truncated%02d"), Index));
			const UK2Node_VariableSet* Current = TruncatedSets.FindRef(VariableName);
			if (!TestNotNull(*FString::Printf(TEXT("%s Set exists"), *VariableName.ToString()), Current))
			{
				continue;
			}
			if (Index == 1)
			{
				TestTrue(TEXT("truncated event -> Set 01"), HasDirectedEdge(*TruncatedEvent, *Current));
			}
			else if (Previous != nullptr)
			{
				TestTrue(
					*FString::Printf(TEXT("truncated Set %02d -> Set %02d"), Index - 1, Index),
					HasDirectedEdge(*Previous, *Current));
			}
			Previous = Current;
		}
	}

	const TSet<FName> ExpectedVariables = {
		TEXT("LC6OpaqueDone"), TEXT("LC6UncertainResult"), TEXT("LC6UnsupportedDone"),
		TEXT("LC6Truncated01"), TEXT("LC6Truncated02"), TEXT("LC6Truncated03"),
		TEXT("LC6Truncated04"), TEXT("LC6Truncated05"), TEXT("LC6Truncated06")};
	TSet<FName> ObservedVariables;
	TSet<FGuid> ObservedVariableGuids;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (ExpectedVariables.Contains(Variable.VarName))
		{
			ObservedVariables.Add(Variable.VarName);
			TestTrue(*FString::Printf(TEXT("%s has stable GUID"), *Variable.VarName.ToString()),
				Variable.VarGuid.IsValid());
			ObservedVariableGuids.Add(Variable.VarGuid);
			TestEqual(*FString::Printf(TEXT("%s is Boolean"), *Variable.VarName.ToString()),
				Variable.VarType.PinCategory, UEdGraphSchema_K2::PC_Boolean);
		}
	}
	TestTrue(TEXT("all nine fixture variables exist"),
		SetsEqual(ObservedVariables, ExpectedVariables));
	TestEqual(TEXT("nine unique fixture variable GUIDs"), ObservedVariableGuids.Num(), 9);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6BoundaryProducersTest,
	"BlueprintLens.Exporter.LC6BoundaryProducers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6BoundaryProducersTest::RunTest(const FString&)
{
	BlueprintLensLC6BoundaryFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC6BoundaryFixture::EnsureFixture(Anchors, Error))
	{
		AddError(Error);
		return false;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (!TestNotNull(TEXT("LC6 source Blueprint loads"), Blueprint))
	{
		return false;
	}

	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LC6BoundaryProducers")));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString CaptureCommand = FString::Printf(
		TEXT("BlueprintLens.CaptureLC6BoundaryTruth \"%s\""),
		*OutputDirectory);
	TestTrue(
		TEXT("LC6 capture command dispatches"),
		IConsoleManager::Get().ProcessUserConsoleInput(*CaptureCommand, *GLog, nullptr));

	const FString RawPath = FPaths::Combine(
		OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.raw-0.2.json"));
	const FString CapturedSourcePath = FPaths::Combine(
		OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.boundary-source.json"));
	const FString CapturedAuditPath = FPaths::Combine(
		OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.boundary-audit.tsv"));
	TestTrue(TEXT("capture writes raw export"), IFileManager::Get().FileExists(*RawPath));
	TestTrue(TEXT("capture writes source facts"), IFileManager::Get().FileExists(*CapturedSourcePath));
	TestTrue(TEXT("capture writes independent audit"), IFileManager::Get().FileExists(*CapturedAuditPath));

	const FString DirectDirectory = FPaths::Combine(OutputDirectory, TEXT("direct"));
	FString SourcePath;
	FString AuditPath;
	BlueprintLensLC6BoundaryFacts::FBoundaryFactStats SourceStats;
	BlueprintLensLC6BoundaryAudit::FBoundaryAuditStats AuditStats;
	TestTrue(
		TEXT("source adapter succeeds"),
		BlueprintLensLC6BoundaryFacts::ExportBoundaryFacts(
			*Blueprint, Anchors, RawPath, DirectDirectory, SourcePath, SourceStats, Error));
	TestTrue(
		TEXT("audit adapter succeeds"),
		BlueprintLensLC6BoundaryAudit::AuditBoundarySource(
			*Blueprint, Anchors, RawPath, DirectDirectory, AuditPath, AuditStats, Error));
	TestEqual(TEXT("source scenarios"), SourceStats.ScenarioCount, 4);
	TestEqual(TEXT("source nodes"), SourceStats.NodeCount, 16);
	TestEqual(TEXT("source edges"), SourceStats.EdgeCount, 12);
	TestEqual(TEXT("audit scenarios"), AuditStats.ScenarioCount, 4);
	TestEqual(TEXT("audit nodes"), AuditStats.NodeCount, 16);
	TestEqual(TEXT("audit edges"), AuditStats.EdgeCount, 12);

	FString SourceText;
	FString AuditText;
	TestTrue(TEXT("source JSON is readable"), FFileHelper::LoadFileToString(SourceText, *SourcePath));
	TestTrue(TEXT("audit TSV is readable"), FFileHelper::LoadFileToString(AuditText, *AuditPath));
	TSharedPtr<FJsonObject> SourceJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SourceText);
	TestTrue(TEXT("source JSON parses"), FJsonSerializer::Deserialize(Reader, SourceJson));
	if (SourceJson.IsValid())
	{
		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestEqual(
			TEXT("source format"),
			SourceJson->GetStringField(TEXT("format")),
			TEXT("blueprint-lens-lc6-boundary-source"));
		TestEqual(TEXT("source version"), SourceJson->GetStringField(TEXT("format_version")), TEXT("1.0.0"));
		TestEqual(TEXT("source asset hash"), SourceJson->GetStringField(TEXT("asset_sha256")), Sha256File(AssetPath));
		TestEqual(TEXT("source raw hash"), SourceJson->GetStringField(TEXT("raw_sha256")), Sha256File(RawPath));
		const TSharedPtr<FJsonObject>* Compile = nullptr;
		if (TestTrue(TEXT("source compile provenance exists"),
			SourceJson->TryGetObjectField(TEXT("compile_provenance"), Compile)) && Compile != nullptr)
		{
			TestEqual(TEXT("source compile is up to date"), (*Compile)->GetStringField(TEXT("status")), TEXT("up_to_date"));
		}
	}
	TestTrue(
		TEXT("audit format"),
		AuditText.StartsWith(TEXT("FORMAT\tblueprint-lens-lc6-boundary-audit\t1.0.0")));
	TArray<FString> AuditLines;
	AuditText.ParseIntoArrayLines(AuditLines, false);
	for (const FString& AuditLine : AuditLines)
	{
		TestFalse(TEXT("audit row has no trailing empty field"), AuditLine.EndsWith(TEXT("\t")));
	}
	TestTrue(TEXT("audit compile provenance is up to date"), AuditText.Contains(TEXT("COMPILE\tup_to_date\t")));
	TestTrue(TEXT("source records PrintString"), SourceText.Contains(TEXT("PrintString")));
	TestTrue(TEXT("source records system-library owner"), SourceText.Contains(TEXT("/Script/Engine.KismetSystemLibrary")));
	TestTrue(TEXT("source records non-latent PrintString"), SourceText.Contains(TEXT("\"is_latent\": false")));
	TestTrue(TEXT("source records Delay"), SourceText.Contains(TEXT("Delay")));
	TestTrue(TEXT("source records latent Delay"), SourceText.Contains(TEXT("\"is_latent\": true")));
	TestTrue(TEXT("audit records non-latent PrintString"),
		AuditText.Contains(TEXT("function=PrintString;owner=/Script/Engine.KismetSystemLibrary;latent=0")));
	TestTrue(TEXT("audit records latent Delay"),
		AuditText.Contains(TEXT("function=Delay;owner=/Script/Engine.KismetSystemLibrary;latent=1")));

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	const UEdGraphNode* OpaqueRoot = FindNodeById(*EventGraph, Anchors.Scenarios[0].RootNodeId);
	const UEdGraphNode* UnsupportedCriterion = FindNodeById(*EventGraph, Anchors.Scenarios[2].CriterionNodeId);
	UEdGraphPin* ForgedSource = OpaqueRoot == nullptr
		? nullptr
		: const_cast<UEdGraphNode*>(OpaqueRoot)->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* ForgedTarget = UnsupportedCriterion == nullptr
		? nullptr
		: const_cast<UEdGraphNode*>(UnsupportedCriterion)->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (TestNotNull(TEXT("forged cross-component source pin"), ForgedSource)
		&& TestNotNull(TEXT("forged cross-component target pin"), ForgedTarget))
	{
		ForgedSource->MakeLinkTo(ForgedTarget);
		FString RejectedSourcePath;
		FString RejectedAuditPath;
		BlueprintLensLC6BoundaryFacts::FBoundaryFactStats RejectedSourceStats;
		BlueprintLensLC6BoundaryAudit::FBoundaryAuditStats RejectedAuditStats;
		const FString CrossSourceDir = FPaths::Combine(OutputDirectory, TEXT("reject-cross-source"));
		const FString CrossAuditDir = FPaths::Combine(OutputDirectory, TEXT("reject-cross-audit"));
		const bool bSourceAccepted = BlueprintLensLC6BoundaryFacts::ExportBoundaryFacts(
			*Blueprint, Anchors, RawPath, CrossSourceDir, RejectedSourcePath, RejectedSourceStats, Error);
		const bool bAuditAccepted = BlueprintLensLC6BoundaryAudit::AuditBoundarySource(
			*Blueprint, Anchors, RawPath, CrossAuditDir, RejectedAuditPath, RejectedAuditStats, Error);
		ForgedSource->BreakLinkTo(ForgedTarget);
		TestFalse(TEXT("source rejects forged cross-component edge"), bSourceAccepted);
		TestFalse(TEXT("audit rejects forged cross-component edge"), bAuditAccepted);
		TestFalse(TEXT("source leaves no cross-edge product"),
			IFileManager::Get().FileExists(*FPaths::Combine(CrossSourceDir, TEXT("BP_LC6_BoundaryMatrix.boundary-source.json"))));
		TestFalse(TEXT("audit leaves no cross-edge product"),
			IFileManager::Get().FileExists(*FPaths::Combine(CrossAuditDir, TEXT("BP_LC6_BoundaryMatrix.boundary-audit.tsv"))));
	}

	BlueprintLensLC6BoundaryFixture::FFixtureAnchors DuplicateCriterion = Anchors;
	DuplicateCriterion.Scenarios[1].CriterionNodeId = DuplicateCriterion.Scenarios[0].CriterionNodeId;
	FString RejectedSourcePath;
	FString RejectedAuditPath;
	BlueprintLensLC6BoundaryFacts::FBoundaryFactStats RejectedSourceStats;
	BlueprintLensLC6BoundaryAudit::FBoundaryAuditStats RejectedAuditStats;
	const FString DuplicateSourceDir = FPaths::Combine(OutputDirectory, TEXT("reject-duplicate-source"));
	const FString DuplicateAuditDir = FPaths::Combine(OutputDirectory, TEXT("reject-duplicate-audit"));
	TestFalse(
		TEXT("source rejects duplicate criterion"),
		BlueprintLensLC6BoundaryFacts::ExportBoundaryFacts(
			*Blueprint, DuplicateCriterion, RawPath, DuplicateSourceDir,
			RejectedSourcePath, RejectedSourceStats, Error));
	TestFalse(
		TEXT("audit rejects duplicate criterion"),
		BlueprintLensLC6BoundaryAudit::AuditBoundarySource(
			*Blueprint, DuplicateCriterion, RawPath, DuplicateAuditDir,
			RejectedAuditPath, RejectedAuditStats, Error));
	TestFalse(TEXT("source leaves no duplicate-criterion product"),
		IFileManager::Get().FileExists(*FPaths::Combine(DuplicateSourceDir, TEXT("BP_LC6_BoundaryMatrix.boundary-source.json"))));
	TestFalse(TEXT("audit leaves no duplicate-criterion product"),
		IFileManager::Get().FileExists(*FPaths::Combine(DuplicateAuditDir, TEXT("BP_LC6_BoundaryMatrix.boundary-audit.tsv"))));

	return !HasAnyErrors();
}

#endif
