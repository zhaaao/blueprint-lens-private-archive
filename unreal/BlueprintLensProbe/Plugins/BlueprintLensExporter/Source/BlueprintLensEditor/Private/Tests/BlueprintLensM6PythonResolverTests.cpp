// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6PythonResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SBlueprintLensPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6PythonResolverTest,
	"BlueprintLens.M6.PythonResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6PythonResolverTest::RunTest(const FString&)
{
	const TArray<FM6PythonCandidate> Candidates = {
		{TEXT("saved-python"), EM6PythonResolutionSource::Saved},
		{TEXT("workspace-dot-venv"), EM6PythonResolutionSource::ProjectDotVenv},
		{TEXT("workspace-venv"), EM6PythonResolutionSource::ProjectVenv},
		{TEXT("system-python"), EM6PythonResolutionSource::SystemPython},
		{TEXT("windows-py"), EM6PythonResolutionSource::WindowsPy},
		{TEXT("picked-python"), EM6PythonResolutionSource::Picker}
	};

	const FM6PythonResolutionResult Selected =
		FM6PythonResolver::ResolveCandidates(
			Candidates,
			[](const FM6PythonCandidate& Candidate)
			{
				FM6PythonValidationResult Result;
				Result.ExecutablePath = Candidate.ExecutablePath;
				Result.Source = Candidate.Source;
				if (Candidate.Source == EM6PythonResolutionSource::ProjectVenv)
				{
					Result.bValid = true;
					Result.Version = TEXT("3.13");
				}
				else
				{
					Result.FailureCode = TEXT("M6_PYTHON_UNAVAILABLE");
					Result.FailureMessage = TEXT("candidate rejected");
				}
				return Result;
			});

	TestTrue(TEXT("resolver selects the first valid candidate"), Selected.bValid);
	TestEqual(
		TEXT("resolver preserves fixed candidate priority"),
		Selected.ExecutablePath,
		FString(TEXT("workspace-venv")));
	TestEqual(TEXT("resolver records rejected candidates"), Selected.Attempts.Num(), 3);
	TestEqual(
		TEXT("resolver records the first rejection reason"),
		Selected.Attempts[0].FailureCode,
		FString(TEXT("M6_PYTHON_UNAVAILABLE")));

	const FM6PythonResolutionResult Unresolved =
		FM6PythonResolver::ResolveCandidates(
			Candidates,
			[](const FM6PythonCandidate& Candidate)
			{
				FM6PythonValidationResult Result;
				Result.ExecutablePath = Candidate.ExecutablePath;
				Result.Source = Candidate.Source;
				Result.FailureCode = TEXT("M6_PYTHON_IMPORT_FAILED");
				Result.FailureMessage = TEXT("analysis import failed");
				return Result;
			});

	TestFalse(TEXT("no valid runtime leaves setup unresolved"), Unresolved.bValid);
	TestEqual(
		TEXT("unresolved result retains every rejection"),
		Unresolved.Attempts.Num(),
		Candidates.Num());
	TestEqual(
		TEXT("unresolved result exposes the final failure"),
		Unresolved.FailureCode,
		FString(TEXT("M6_PYTHON_IMPORT_FAILED")));

	const FString OriginalSavedPath = FM6PythonResolver::GetSavedPath();
	FM6PythonResolver::SavePath(TEXT("relative-test-python.exe"));
	TestEqual(
		TEXT("saved Python path is persisted in normalized form"),
		FM6PythonResolver::GetSavedPath(),
		FPaths::ConvertRelativePathToFull(TEXT("relative-test-python.exe")));
	FM6PythonResolver::ClearSavedPath();
	TestTrue(
		TEXT("clear removes the saved Python path"),
		FM6PythonResolver::GetSavedPath().IsEmpty());
	if (!OriginalSavedPath.IsEmpty())
		FM6PythonResolver::SavePath(OriginalSavedPath);

	FM6PanelPresentationModel Panel;
	Panel.SetPythonReady(false);
	Panel.SetExecutionCriterion(TEXT("Graph"), TEXT("Graph::node::guid"));
	TestFalse(
		TEXT("Run remains disabled when Python cannot be resolved"),
		Panel.CanRun());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
