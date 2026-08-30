// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensIntraBpPureAudit.h"
#include "BlueprintLensIntraBpPureFacts.h"

#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensIntraBpPureProducersTest,
	"BlueprintLens.Exporter.LC5IntraBpPureProducers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensIntraBpPureProducersTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe"));
	if (!TestNotNull(TEXT("real slicing probe"), Blueprint))
	{
		return false;
	}

	const FString CallNodeId = TEXT(
		"/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph::node::"
		"efbd1d7a-47d3-fd55-7775-26bd488ee92d");
	const FString RawExportPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT(".."),
		TEXT(".."),
		TEXT("fixtures"),
		TEXT("raw"),
		TEXT("BP_SlicingProbe.raw-0.2.json")));
	FString SourcePath;
	FString AuditPath;
	FString Error;
	BlueprintLensIntraBpPureFacts::FIntraBpPureFactStats SourceStats;
	BlueprintLensIntraBpPureAudit::FIntraBpPureAuditStats AuditStats;
	if (!TestTrue(
		TEXT("native LC5 source producer succeeds"),
		BlueprintLensIntraBpPureFacts::ExportIntraBpPureCallFacts(
			*Blueprint,
			CallNodeId,
			RawExportPath,
			SourcePath,
			SourceStats,
			Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(
		TEXT("independent LC5 audit succeeds"),
		BlueprintLensIntraBpPureAudit::AuditIntraBpPureCall(
			*Blueprint,
			CallNodeId,
			RawExportPath,
			AuditPath,
			AuditStats,
			Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("one native candidate"), SourceStats.CandidateCount, 1);
	TestEqual(TEXT("three native bindings"), SourceStats.BindingCount, 3);
	TestEqual(TEXT("one independently audited candidate"), AuditStats.CandidateCount, 1);
	TestEqual(TEXT("three independently audited bindings"), AuditStats.BindingCount, 3);
	TestTrue(TEXT("source file exists"), IFileManager::Get().FileExists(*SourcePath));
	TestTrue(TEXT("audit file exists"), IFileManager::Get().FileExists(*AuditPath));
	return !HasAnyErrors();
}

#endif
