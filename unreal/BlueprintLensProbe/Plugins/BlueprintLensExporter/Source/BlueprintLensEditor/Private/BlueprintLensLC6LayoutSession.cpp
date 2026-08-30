#include "BlueprintLensLC6LayoutSession.h"

namespace
{
FBlueprintLensLC6LayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC6Layout& Oracle,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC6LayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}
	OutLedger = Backend.Layout(Oracle.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	FBlueprintLensLC6Layout Candidate = Oracle;
	Candidate.LayoutLedger = OutLedger;
	Candidate.CanvasSize = OutLedger.CanvasSize;
	const bool bComplete = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(Oracle.LayoutRequest);
	Attempt.bAccepted = bComplete && Candidate.MatchesVisualOracle(1.0f) &&
		Candidate.HasNoTextOrRouteCollisions();
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = bComplete
			? TEXT("LC6_LAYOUT_BACKEND_FIDELITY_MISMATCH")
			: TEXT("LC6_LAYOUT_BACKEND_INCOMPLETE");
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC6LayoutSessionResult::IsRenderable(
	const FBlueprintLensLC6Projection& Projection) const
{
	return DiagnosticCode == TEXT("LC6_FOUR_TRACK_LAYOUT_COMPLETE") &&
		Layout.CoversProjection(Projection) && Layout.HasValidSharedLedger() &&
		Layout.MatchesVisualOracle(1.0f) && Layout.HasNoTextOrRouteCollisions();
}

FString FBlueprintLensLC6LayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC6LayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") :
				Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC6LayoutSessionResult FBlueprintLensLC6LayoutSession::Build(
	const FBlueprintLensLC6Projection& Projection,
	const float TargetWidth,
	const FBlueprintLensLC6LayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC6LayoutSessionResult FBlueprintLensLC6LayoutSession::BuildWithBackends(
	const FBlueprintLensLC6Projection& Projection,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC6LayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC6LayoutBuilder::Build(Projection, TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasValidSharedLedger() ||
		!Result.Layout.HasNoTextOrRouteCollisions())
	{
		Result.DiagnosticCode = TEXT("LC6_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const EBlueprintLensLayoutBackendKind Candidate :
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			EBlueprintLensLayoutProfile::LayeredPorts))
	{
		FBlueprintLensLC6LayoutAttempt Attempt;
		FBlueprintLensLayoutLedger Ledger;
		if (Candidate == EBlueprintLensLayoutBackendKind::ElkLayered)
		{
			Attempt = TryBackend(ElkBackend, Result.Layout, Ledger);
		}
		else if (Candidate == EBlueprintLensLayoutBackendKind::GraphvizDot)
		{
			Attempt = TryBackend(GraphvizBackend, Result.Layout, Ledger);
		}
		else
		{
			Attempt.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
			Attempt.bAvailable = true;
			Ledger = Result.Layout.VisualOracleLedger;
			Attempt.DiagnosticCode = Ledger.DiagnosticCode;
			Attempt.bAccepted = Ledger.IsCompleteFor(Result.Layout.LayoutRequest);
		}
		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted)
		{
			continue;
		}
		Result.Layout.LayoutLedger = MoveTemp(Ledger);
		Result.Layout.CanvasSize = Result.Layout.LayoutLedger.CanvasSize;
		Result.DiagnosticCode = TEXT("LC6_FOUR_TRACK_LAYOUT_COMPLETE");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC6_LAYOUT_NO_COMPLETE_LEDGER");
	return Result;
}
