#include "BlueprintLensLC4AsyncLayoutSession.h"

namespace
{
FBlueprintLensLC4AsyncLayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC4AsyncLayout& Oracle,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC4AsyncLayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}
	OutLedger = Backend.Layout(Oracle.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	FBlueprintLensLC4AsyncLayout Candidate = Oracle;
	Candidate.LayoutLedger = OutLedger;
	Candidate.CanvasSize = OutLedger.CanvasSize;
	Attempt.bAccepted = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(Oracle.LayoutRequest) &&
		OutLedger.CanvasSize.Equals(Oracle.VisualOracleLedger.CanvasSize, 0.5f) &&
		Candidate.MatchesVisualOracle(1.0f) && Candidate.HasNoTextOrRouteCollisions();
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = TEXT("LC4_ASYNC_LAYOUT_BACKEND_FIDELITY_MISMATCH");
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC4AsyncLayoutSessionResult::IsRenderable(
	const FBlueprintLensLC4AsyncProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC4_ASYNC_PARTIAL_ORDER_JOIN_COMPLETE") &&
		Layout.CoversProjection(Projection) && Layout.HasValidSharedLedger() &&
		Layout.MatchesVisualOracle(1.0f) && Layout.HasNoTextOrRouteCollisions();
}

FString FBlueprintLensLC4AsyncLayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC4AsyncLayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(
			TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") : Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC4AsyncLayoutSessionResult FBlueprintLensLC4AsyncLayoutSession::Build(
	const FBlueprintLensLC4AsyncProjection& Projection,
	const float TargetWidth,
	const FBlueprintLensLC4AsyncLayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC4AsyncLayoutSessionResult FBlueprintLensLC4AsyncLayoutSession::BuildWithBackends(
	const FBlueprintLensLC4AsyncProjection& Projection,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC4AsyncLayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC4AsyncLayoutBuilder::Build(Projection, TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) || !Result.Layout.HasValidSharedLedger() ||
		!Result.Layout.MatchesVisualOracle(1.0f) || !Result.Layout.HasNoTextOrRouteCollisions())
	{
		Result.DiagnosticCode = TEXT("LC4_ASYNC_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const EBlueprintLensLayoutBackendKind Candidate :
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(EBlueprintLensLayoutProfile::LayeredPorts))
	{
		FBlueprintLensLC4AsyncLayoutAttempt Attempt;
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
		Result.DiagnosticCode = TEXT("LC4_ASYNC_PARTIAL_ORDER_JOIN_COMPLETE");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC4_ASYNC_LAYOUT_NO_FIDELITY_LEDGER");
	return Result;
}
