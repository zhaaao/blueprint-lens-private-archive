#include "BlueprintLensLC5LayoutSession.h"

namespace
{
FBlueprintLensLC5LayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC5Layout& Oracle,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC5LayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}
	OutLedger = Backend.Layout(Oracle.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	const bool bComplete = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(Oracle.LayoutRequest);
	FBlueprintLensLC5Layout Candidate = Oracle;
	Candidate.LayoutLedger = OutLedger;
	Candidate.CanvasSize = OutLedger.CanvasSize;
	const bool bFidelityMatched = bComplete &&
		OutLedger.CanvasSize.Equals(Oracle.VisualOracleLedger.CanvasSize, 0.5f) &&
		Candidate.MatchesVisualOracle(1.0f) &&
		Candidate.HasNoTextOrRouteCollisions() &&
		Candidate.HasCompleteEndpointGlyphs() &&
		Candidate.HasStrictStaticRankOrder();
	Attempt.bAccepted = bFidelityMatched;
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = bComplete
			? TEXT("LC5_LAYOUT_BACKEND_FIDELITY_MISMATCH")
			: FString::Printf(TEXT("LC5_LAYOUT_BACKEND_INCOMPLETE:%s"), *OutLedger.DiagnosticCode);
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC5LayoutSessionResult::IsRenderable(
	const FBlueprintLensLC5Projection& Projection) const
{
	return DiagnosticCode == TEXT("LC5_TYPED_PORTAL_BRIDGE_COMPLETE") &&
		Layout.CoversProjection(Projection) && Layout.HasValidSharedLedger() &&
		Layout.HasNoTextOrRouteCollisions() &&
		Layout.HasCompleteEndpointGlyphs() &&
		Layout.HasStrictStaticRankOrder();
}

FString FBlueprintLensLC5LayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC5LayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") :
				Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC5LayoutSessionResult FBlueprintLensLC5LayoutSession::Build(
	const FBlueprintLensLC5Projection& Projection,
	const float TargetWidth,
	const FBlueprintLensLC5LayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC5LayoutSessionResult FBlueprintLensLC5LayoutSession::BuildWithBackends(
	const FBlueprintLensLC5Projection& Projection,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC5LayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC5LayoutBuilder::Build(Projection, TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasValidSharedLedger() ||
		!Result.Layout.HasNoTextOrRouteCollisions() ||
		!Result.Layout.HasCompleteEndpointGlyphs() ||
		!Result.Layout.HasStrictStaticRankOrder())
	{
		Result.DiagnosticCode = TEXT("LC5_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const EBlueprintLensLayoutBackendKind Candidate :
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(EBlueprintLensLayoutProfile::LayeredPorts))
	{
		FBlueprintLensLC5LayoutAttempt Attempt;
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
		Result.DiagnosticCode = TEXT("LC5_TYPED_PORTAL_BRIDGE_COMPLETE");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC5_LAYOUT_NO_COMPLETE_LEDGER");
	return Result;
}
