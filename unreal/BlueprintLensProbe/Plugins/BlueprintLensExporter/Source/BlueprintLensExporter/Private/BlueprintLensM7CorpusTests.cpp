// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM7CorpusFixture.h"

#include "BlueprintLensM3Batch.h"

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM7CorpusFixtureTest,
	"BlueprintLens.Exporter.M7.CorpusFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM7CorpusFixtureTest::RunTest(const FString& Parameters)
{
	BlueprintLensM7CorpusFixture::FFixtureSummary Summary;
	BlueprintLensM7CorpusFixture::EEnsureResult Result =
		BlueprintLensM7CorpusFixture::EEnsureResult::Unchanged;
	FString Error;
	if (!BlueprintLensM7CorpusFixture::EnsureFixture(Summary, Result, Error))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("M7 graph reaches hundreds of nodes"), Summary.NodeCount >= 250);
	TestTrue(TEXT("M7 graph has execution edges"), Summary.ExecutionEdgeCount > 0);
	TestTrue(TEXT("M7 graph has data edges"), Summary.DataEdgeCount > 0);
	BlueprintLensM7CorpusFixture::FFixtureSummary EngineSummary;
	BlueprintLensM7CorpusFixture::EEnsureResult EngineResult =
		BlueprintLensM7CorpusFixture::EEnsureResult::Unchanged;
	if (!BlueprintLensM7CorpusFixture::EnsureEngineSampleFixture(EngineSummary, EngineResult, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("M7_SOURCE_FIXTURE_READY result=%s asset=%s graph=%s nodes=%d exec=%d data=%d"),
		EngineResult == BlueprintLensM7CorpusFixture::EEnsureResult::Created
			? TEXT("created") : TEXT("unchanged"),
		*EngineSummary.AssetObjectPath,
		*EngineSummary.GraphId,
		EngineSummary.NodeCount,
		EngineSummary.ExecutionEdgeCount,
		EngineSummary.DataEdgeCount));
	FString CorpusManifestPath;
	FString OutputRoot;
	FString RunId;
	// The capture branch owns its output root exclusively and refuses a root that
	// already exists. It therefore cannot share M3's argument namespace: in a full
	// BlueprintLens run, BlueprintLens.Exporter.M3.BatchCaptureCommand consumes
	// M3OutputRoot roughly a second earlier and creates the directory, so this test
	// found it non-fresh and failed. Measured 2026-08-24: M3 capture succeeded at
	// 06:38:31.289, this test failed at 06:38:32.250 on the same root. Its own
	// namespace lets the full run skip the capture branch and exercise the fixture
	// assertions, while tools/run_m7_export.ps1 supplies M7* for the capture path.
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Value(CommandLine, TEXT("M7CorpusManifest="), CorpusManifestPath)
		&& FParse::Value(CommandLine, TEXT("M7OutputRoot="), OutputRoot)
		&& FParse::Value(CommandLine, TEXT("M7RunId="), RunId))
	{
		CorpusManifestPath = FPaths::ConvertRelativePathToFull(CorpusManifestPath);
		OutputRoot = FPaths::ConvertRelativePathToFull(OutputRoot);
		if (!FPaths::FileExists(CorpusManifestPath)
			|| IFileManager::Get().DirectoryExists(*OutputRoot)
			|| IFileManager::Get().FileExists(*OutputRoot))
		{
			AddError(TEXT("M7_CAPTURE_ARGUMENT_INVALID: corpus must exist and output root must be fresh"));
			return false;
		}
		BlueprintLensM3Batch::FBatchRequest Request;
		Request.CorpusManifestPath = CorpusManifestPath;
		Request.OutputDirectory = OutputRoot;
		BlueprintLensM3Batch::FBatchResult BatchResult;
		FString ErrorCode;
		if (!BlueprintLensM3Batch::ExportBatchForAutomationTest(
			Request, BatchResult, ErrorCode, Error))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *ErrorCode, *Error));
			return false;
		}
		AddInfo(FString::Printf(
			TEXT("M7_CAPTURE_SUCCESS run_id=%s requested=%d exported=%d result=%s sha256=%s"),
			*RunId,
			BatchResult.RequestedAssetCount,
			BatchResult.ExportedAssetCount,
			*BatchResult.ResultManifestPath,
			*BatchResult.ResultManifestSha256));
	}
	AddInfo(FString::Printf(
		TEXT("M7_FIXTURE_READY result=%s asset=%s graph=%s nodes=%d exec=%d data=%d"),
		Result == BlueprintLensM7CorpusFixture::EEnsureResult::Created
			? TEXT("created") : TEXT("unchanged"),
		*Summary.AssetObjectPath,
		*Summary.GraphId,
		Summary.NodeCount,
		Summary.ExecutionEdgeCount,
		Summary.DataEdgeCount));
	return !HasAnyErrors();
}
