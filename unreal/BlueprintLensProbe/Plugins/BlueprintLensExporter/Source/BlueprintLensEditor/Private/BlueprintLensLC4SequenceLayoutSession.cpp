#include "BlueprintLensLC4SequenceLayoutSession.h"

namespace
{
FBlueprintLensLC4SequenceLayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC4SequenceLayout& OracleLayout,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC4SequenceLayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}

	OutLedger = Backend.Layout(OracleLayout.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	FBlueprintLensLC4SequenceLayout Candidate = OracleLayout;
	Candidate.LayoutLedger = OutLedger;
	Candidate.CanvasSize = OutLedger.CanvasSize;
	const bool bComplete = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(OracleLayout.LayoutRequest);
	const bool bExactCanvas = OutLedger.CanvasSize.Equals(
		OracleLayout.VisualOracleLedger.CanvasSize,
		0.5f);
	const bool bFidelityMatched = bComplete && bExactCanvas &&
		Candidate.MatchesVisualOracle(1.0f);
	Attempt.bAccepted = bFidelityMatched;
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = bComplete
			? TEXT("LC4_SEQUENCE_LAYOUT_BACKEND_FIDELITY_MISMATCH")
			: FString::Printf(
				TEXT("LC4_SEQUENCE_LAYOUT_BACKEND_INCOMPLETE:%s"),
				*OutLedger.DiagnosticCode);
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC4SequenceLayoutSessionResult::IsRenderable(
	const FBlueprintLensLC4SequenceProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC4_SEQUENCE_DISCLOSURE_RAIL_COMPLETE") &&
		Layout.CoversProjection(Projection) &&
		Layout.HasValidSharedLedger() &&
		Layout.MatchesVisualOracle(1.0f) &&
		Layout.HasNoLabelRouteCollisions();
}

FString FBlueprintLensLC4SequenceLayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC4SequenceLayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(
			TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted
				? TEXT("accepted")
				: Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC4SequenceLayoutSessionResult
FBlueprintLensLC4SequenceLayoutSession::Build(
	const FBlueprintLensLC4SequenceProjection& Projection,
	const float TargetWidth,
	const FBlueprintLensLC4SequenceLayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC4SequenceLayoutSessionResult
FBlueprintLensLC4SequenceLayoutSession::BuildWithBackends(
	const FBlueprintLensLC4SequenceProjection& Projection,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC4SequenceLayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC4SequenceLayoutBuilder::Build(
		Projection,
		TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasValidSharedLedger() ||
		!Result.Layout.MatchesVisualOracle(1.0f))
	{
		Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LAYOUT_REQUEST_UNAVAILABLE");
		return Result;
	}

	for (const EBlueprintLensLayoutBackendKind Candidate :
		 FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			 EBlueprintLensLayoutProfile::LayeredPorts))
	{
		FBlueprintLensLC4SequenceLayoutAttempt Attempt;
		FBlueprintLensLayoutLedger CandidateLedger;
		if (Candidate == EBlueprintLensLayoutBackendKind::ElkLayered)
		{
			Attempt = TryBackend(ElkBackend, Result.Layout, CandidateLedger);
		}
		else if (Candidate == EBlueprintLensLayoutBackendKind::GraphvizDot)
		{
			Attempt = TryBackend(GraphvizBackend, Result.Layout, CandidateLedger);
		}
		else
		{
			Attempt.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
			Attempt.bAvailable = true;
			CandidateLedger = Result.Layout.VisualOracleLedger;
			Attempt.DiagnosticCode = CandidateLedger.DiagnosticCode;
			Attempt.bAccepted = CandidateLedger.IsCompleteFor(
				Result.Layout.LayoutRequest);
		}
		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted)
		{
			continue;
		}
		Result.Layout.LayoutLedger = CandidateLedger;
		Result.Layout.CanvasSize = CandidateLedger.CanvasSize;
		Result.DiagnosticCode = TEXT("LC4_SEQUENCE_DISCLOSURE_RAIL_COMPLETE");
		return Result;
	}

	Result.DiagnosticCode = TEXT("LC4_SEQUENCE_LAYOUT_NO_FIDELITY_LEDGER");
	return Result;
}
