// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6Preflight.h"
#include "BlueprintLensProductionExporter.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace BlueprintLensM6PreflightTests
{
namespace
{
struct FBlueprintStateRestorer
{
	UBlueprint* Blueprint = nullptr;
	bool bWasDirty = false;
	EBlueprintStatus Status = BS_Unknown;

	~FBlueprintStateRestorer()
	{
		if (IsValid(Blueprint))
		{
			Blueprint->Status = Status;
			Blueprint->GetOutermost()->SetDirtyFlag(bWasDirty);
		}
	}
};

UBlueprint* LoadBlueprint(const TCHAR* ObjectPath)
{
	return LoadObject<UBlueprint>(nullptr, ObjectPath);
}

UEdGraph* FindEventGraph(UBlueprint& Blueprint)
{
	for (UEdGraph* Graph : Blueprint.UbergraphPages)
	{
		if (Graph != nullptr && Graph->GetFName() == TEXT("EventGraph"))
		{
			return Graph;
		}
	}
	return nullptr;
}

FString FirstNodeId(const UEdGraph& Graph)
{
	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node != nullptr && Node->NodeGuid.IsValid())
		{
			return FString::Printf(
				TEXT("%s::node::%s"),
				*Graph.GetPathName(),
				*Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
	}
	return FString();
}

FM6QueryInput ExecutionQuery(const UEdGraph& Graph)
{
	FM6QueryInput Query;
	Query.Kind = EM6QueryKind::Execution;
	Query.GraphId = Graph.GetPathName();
	Query.CriterionNodeId = FirstNodeId(Graph);
	Query.Direction = TEXT("backward");
	return Query;
}

FM6QueryInput DataQuery(const UBlueprint& Blueprint, const UEdGraph& Graph)
{
	FM6QueryInput Query;
	Query.Kind = EM6QueryKind::Data;
	Query.GraphId = Graph.GetPathName();
	Query.Direction = TEXT("backward");
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		if (Variable.VarName == TEXT("LC3Score"))
		{
			Query.MemberGuid = Variable.VarGuid.ToString(
				EGuidFormats::DigitsWithHyphensLower);
			Query.ExpectedMemberName = Variable.VarName.ToString();
			break;
		}
	}
	return Query;
}

void TestUnchanged(
	FAutomationTestBase& Test,
	const TCHAR* Label,
	const UBlueprint& Blueprint,
	const bool bDirty,
	const EBlueprintStatus Status)
{
	Test.TestEqual(
		FString::Printf(TEXT("%s preserves dirty state"), Label),
		Blueprint.GetOutermost()->IsDirty(),
		bDirty);
	Test.TestEqual(
		FString::Printf(TEXT("%s preserves compile state"), Label),
		Blueprint.Status,
		Status);
}

void TestFailure(
	FAutomationTestBase& Test,
	const TCHAR* Label,
	const FM6PreflightResult& Result,
	const TCHAR* ExpectedCode)
{
	Test.TestFalse(FString::Printf(TEXT("%s fails"), Label), Result.bSucceeded);
	Test.TestEqual(
		FString::Printf(TEXT("%s stable code"), Label),
		Result.Error.Code,
		FString(ExpectedCode));
	Test.TestTrue(
		FString::Printf(TEXT("%s has no staging"), Label),
		Result.OwnedStagingDirectory.IsEmpty());
}
} // namespace
} // namespace BlueprintLensM6PreflightTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6PreflightTest,
	"BlueprintLens.M6.Preflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6PreflightTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6PreflightTests;

	UBlueprint* ExecutionBlueprint = LoadBlueprint(
		TEXT("/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"));
	UBlueprint* DataBlueprint = LoadBlueprint(
		TEXT("/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance"));
	TestNotNull(TEXT("Execution Blueprint loads"), ExecutionBlueprint);
	TestNotNull(TEXT("Data Blueprint loads"), DataBlueprint);
	if (ExecutionBlueprint == nullptr || DataBlueprint == nullptr)
	{
		return false;
	}

	UEdGraph* ExecutionGraph = FindEventGraph(*ExecutionBlueprint);
	UEdGraph* DataGraph = FindEventGraph(*DataBlueprint);
	TestNotNull(TEXT("Execution graph resolves"), ExecutionGraph);
	TestNotNull(TEXT("Data graph resolves"), DataGraph);
	if (ExecutionGraph == nullptr || DataGraph == nullptr)
	{
		return false;
	}

	FBlueprintStateRestorer RestoreExecution{
		ExecutionBlueprint,
		ExecutionBlueprint->GetOutermost()->IsDirty(),
		ExecutionBlueprint->Status};
	FBlueprintStateRestorer RestoreData{
		DataBlueprint,
		DataBlueprint->GetOutermost()->IsDirty(),
		DataBlueprint->Status};
	ExecutionBlueprint->GetOutermost()->SetDirtyFlag(false);
	ExecutionBlueprint->Status = BS_UpToDate;
	DataBlueprint->GetOutermost()->SetDirtyFlag(false);
	DataBlueprint->Status = BS_UpToDate;

	const FString StagingRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6PreflightTests")));
	IFileManager::Get().MakeDirectory(*StagingRoot, true);
	const FM6QueryInput ValidExecution = ExecutionQuery(*ExecutionGraph);
	const FM6QueryInput ValidData = DataQuery(*DataBlueprint, *DataGraph);
	TestFalse(TEXT("Execution criterion is present"), ValidExecution.CriterionNodeId.IsEmpty());
	TestFalse(TEXT("Data member criterion is present"), ValidData.MemberGuid.IsEmpty());
	TestEqual(TEXT("Semantic node default is shared by execution and data queries"), ValidExecution.MaxSelectedNodes, ValidData.MaxSelectedNodes);
	TestEqual(TEXT("Semantic relation default is shared by execution and data queries"), ValidExecution.MaxSelectedRelations, ValidData.MaxSelectedRelations);
	TestEqual(TEXT("Presentation entity default is shared by execution and data queries"), ValidExecution.MaxVisibleEntities, ValidData.MaxVisibleEntities);
	TestEqual(TEXT("Presentation relation default is shared by execution and data queries"), ValidExecution.MaxVisibleRelations, ValidData.MaxVisibleRelations);
	TestEqual(
		TEXT("Query graph resolves when the lens tab owns focus"),
		FM6Preflight::ResolveQueryGraphForAutomationTest(
			ExecutionBlueprint,
			nullptr,
			ValidExecution.GraphId),
		ExecutionGraph);
	TestNull(
		TEXT("Unknown query graph remains fail closed"),
		FM6Preflight::ResolveQueryGraphForAutomationTest(
			ExecutionBlueprint,
			DataGraph,
			TEXT("/Game/LensCorpus/Unknown.Unknown:EventGraph")));

	const FM6PreflightResult NoBlueprint = FM6Preflight::Evaluate(
		TSharedPtr<FBlueprintEditor>(),
		ValidExecution,
		StagingRoot);
	TestFailure(*this, TEXT("No Blueprint"), NoBlueprint, TEXT("M6_PRECONDITION_NO_BLUEPRINT"));

	const FM6PreflightResult InvalidGraph = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		nullptr,
		ValidExecution,
		StagingRoot);
	TestFailure(*this, TEXT("Invalid graph"), InvalidGraph, TEXT("M6_PRECONDITION_GRAPH_INVALID"));

	ExecutionBlueprint->GetOutermost()->SetDirtyFlag(true);
	const bool bDirtyBefore = ExecutionBlueprint->GetOutermost()->IsDirty();
	const EBlueprintStatus DirtyStatusBefore = ExecutionBlueprint->Status;
	const FM6PreflightResult Dirty = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		ValidExecution,
		StagingRoot);
	TestFailure(*this, TEXT("Dirty source"), Dirty, TEXT("M6_PRECONDITION_DIRTY_SOURCE"));
	TestUnchanged(*this, TEXT("Dirty source"), *ExecutionBlueprint, bDirtyBefore, DirtyStatusBefore);
	ExecutionBlueprint->GetOutermost()->SetDirtyFlag(false);

	ExecutionBlueprint->Status = BS_Error;
	const bool bCompileDirtyBefore = ExecutionBlueprint->GetOutermost()->IsDirty();
	const EBlueprintStatus CompileStatusBefore = ExecutionBlueprint->Status;
	const FM6PreflightResult CompileFailed = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		ValidExecution,
		StagingRoot);
	TestFailure(*this, TEXT("Compile failed"), CompileFailed, TEXT("M6_PRECONDITION_COMPILE_FAILED"));
	TestUnchanged(*this, TEXT("Compile failed"), *ExecutionBlueprint, bCompileDirtyBefore, CompileStatusBefore);
	ExecutionBlueprint->Status = BS_UpToDate;

	FM6QueryInput Mixed = ValidExecution;
	Mixed.MemberGuid = TEXT("175c3370-4752-21d1-da22-4e94033ccbe5");
	Mixed.ExpectedMemberName = TEXT("LC3Score");
	const FM6PreflightResult MixedResult = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		Mixed,
		StagingRoot);
	TestFailure(*this, TEXT("Mixed query"), MixedResult, TEXT("M6_PRECONDITION_QUERY_INVALID"));

	FM6QueryInput Invalid = ValidExecution;
	Invalid.Direction = TEXT("forward");
	const FM6PreflightResult InvalidResult = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		Invalid,
		StagingRoot);
	TestFailure(*this, TEXT("Invalid query"), InvalidResult, TEXT("M6_PRECONDITION_QUERY_INVALID"));

	const FString UnwritableRoot = FPaths::Combine(StagingRoot, TEXT("occupied-file"));
	TestTrue(
		TEXT("Unwritable root fixture is a file"),
		FFileHelper::SaveStringToFile(TEXT("occupied"), *UnwritableRoot));
	const FM6PreflightResult Unwritable = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		ValidExecution,
		UnwritableRoot);
	TestFailure(*this, TEXT("Unwritable staging"), Unwritable, TEXT("M6_PRECONDITION_STAGING_UNAVAILABLE"));
	IFileManager::Get().Delete(*UnwritableRoot, false, true);

	const bool bExecutionDirtyBefore = ExecutionBlueprint->GetOutermost()->IsDirty();
	const EBlueprintStatus ExecutionStatusBefore = ExecutionBlueprint->Status;
	const FM6PreflightResult Execution = FM6Preflight::EvaluateResolvedForAutomationTest(
		ExecutionBlueprint,
		ExecutionGraph,
		ValidExecution,
		StagingRoot);
	TestTrue(TEXT("Execution preflight succeeds"), Execution.bSucceeded);
	TestEqual(TEXT("Execution request kind"), Execution.Request.QueryKind, FString(TEXT("execution")));
	TestEqual(TEXT("Execution graph identity"), Execution.Request.GraphId, ExecutionGraph->GetPathName());
	TestEqual(TEXT("Execution renderer"), Execution.Request.RendererId, FString(TEXT("R1_GENERIC_FRAME_FLOW_V1")));
	TestEqual(TEXT("Execution source fingerprint length"), Execution.Request.SourceFingerprint.Len(), 64);
	TestTrue(TEXT("Execution owns one staging directory"), IFileManager::Get().DirectoryExists(*Execution.OwnedStagingDirectory));
	TestUnchanged(*this, TEXT("Execution success"), *ExecutionBlueprint, bExecutionDirtyBefore, ExecutionStatusBefore);

	FBlueprintLensExportResult ExportResult;
	const FString RawPath = FPaths::Combine(Execution.OwnedStagingDirectory, TEXT("raw-source.json"));
	TestTrue(
		TEXT("Editor module links and invokes public exporter seam"),
		BlueprintLens::Production::ExportRawDocument(
			ExecutionBlueprint,
			RawPath,
			ExportResult));
	TestTrue(TEXT("Public exporter writes raw source"), IFileManager::Get().FileExists(*RawPath));
	TestEqual(TEXT("Public exporter source identity"), ExportResult.BlueprintObjectPath, ExecutionBlueprint->GetPathName());

	const bool bDataDirtyBefore = DataBlueprint->GetOutermost()->IsDirty();
	const EBlueprintStatus DataStatusBefore = DataBlueprint->Status;
	const FM6PreflightResult Data = FM6Preflight::EvaluateResolvedForAutomationTest(
		DataBlueprint,
		DataGraph,
		ValidData,
		StagingRoot);
	TestTrue(TEXT("Data preflight succeeds"), Data.bSucceeded);
	TestEqual(TEXT("Data request kind"), Data.Request.QueryKind, FString(TEXT("data")));
	TestEqual(TEXT("Data member identity"), Data.Request.MemberGuid, ValidData.MemberGuid);
	TestUnchanged(*this, TEXT("Data success"), *DataBlueprint, bDataDirtyBefore, DataStatusBefore);

	if (!Execution.OwnedStagingDirectory.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(*Execution.OwnedStagingDirectory, false, true);
	}
	if (!Data.OwnedStagingDirectory.IsEmpty())
	{
		IFileManager::Get().DeleteDirectory(*Data.OwnedStagingDirectory, false, true);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
