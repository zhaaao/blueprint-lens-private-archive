#include "BlueprintLensLC3ValueConeLayoutSession.h"

namespace
{
void ApplySelectedLedger(
	FBlueprintLensLC3ValueConeLayout& Layout,
	const FBlueprintLensLayoutLedger& Ledger)
{
	Layout.LayoutLedger = Ledger;
	Layout.CanvasSize = Ledger.CanvasSize;
	for (FBlueprintLensLC3ValueConeLayoutNode& Node : Layout.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Placement =
			Ledger.Nodes.FindByPredicate(
				[&Node](const FBlueprintLensLayoutNodePlacement& Candidate)
				{
					return Candidate.UnitId == Node.UnitId;
				});
		if (Placement != nullptr)
		{
			Node.Position = Placement->Position;
			Node.Size = Placement->Size;
		}
	}
}

FBlueprintLensLC3ValueConeLayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLayoutRequest& Request,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC3ValueConeLayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}

	OutLedger = Backend.Layout(Request);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	const bool bComplete = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(Request);
	const bool bFitsTargetWidth =
		OutLedger.CanvasSize.X <= Request.TargetWidth + 0.5f;
	Attempt.bAccepted = bComplete && bFitsTargetWidth;
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = bComplete && !bFitsTargetWidth
			? FString::Printf(
				TEXT("BLUEPRINT_LENS_LAYOUT_BACKEND_CANVAS_EXCEEDS_TARGET:%.2f>%.2f"),
				OutLedger.CanvasSize.X,
				Request.TargetWidth)
			: FString::Printf(
				TEXT("BLUEPRINT_LENS_LAYOUT_BACKEND_LEDGER_INCOMPLETE:%s"),
				*Attempt.DiagnosticCode);
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC3ValueConeLayoutSessionResult::IsRenderable(
	const FBlueprintLensLC3ValueConeProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC3_VALUE_CONE_RIBBON_COMPLETE") &&
		Layout.CoversProjection(Projection) && Layout.HasNoNodeOverlaps() &&
		Layout.HasValidSharedLedger();
}

FString FBlueprintLensLC3ValueConeLayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC3ValueConeLayoutAttempt& Attempt : Attempts)
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

FBlueprintLensLC3ValueConeLayoutSessionResult
FBlueprintLensLC3ValueConeLayoutSession::Build(
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const float TargetWidth,
	const FBlueprintLensLC3ValueConeLayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend ElkBackend(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend GraphvizBackend(Options.Graphviz);
	return BuildWithBackends(
		Projection,
		TargetWidth,
		ElkBackend,
		GraphvizBackend);
}

FBlueprintLensLC3ValueConeLayoutSessionResult
FBlueprintLensLC3ValueConeLayoutSession::BuildWithBackends(
	const FBlueprintLensLC3ValueConeProjection& Projection,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC3ValueConeLayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC3ValueConeLayoutBuilder::Build(
		Projection,
		TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasNoNodeOverlaps() ||
		!Result.Layout.HasValidSharedLedger())
	{
		Result.DiagnosticCode =
			TEXT("LC3_VALUE_CONE_RIBBON_REQUEST_UNAVAILABLE");
		return Result;
	}

	const TArray<EBlueprintLensLayoutBackendKind> CandidateOrder =
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			Result.Layout.LayoutRequest.Profile);
	for (const EBlueprintLensLayoutBackendKind Candidate : CandidateOrder)
	{
		FBlueprintLensLayoutLedger CandidateLedger;
		FBlueprintLensLC3ValueConeLayoutAttempt Attempt;
		if (Candidate == EBlueprintLensLayoutBackendKind::ElkLayered)
		{
			Attempt = TryBackend(
				ElkBackend,
				Result.Layout.LayoutRequest,
				CandidateLedger);
		}
		else if (Candidate == EBlueprintLensLayoutBackendKind::GraphvizDot)
		{
			Attempt = TryBackend(
				GraphvizBackend,
				Result.Layout.LayoutRequest,
				CandidateLedger);
		}
		else
		{
			Attempt.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
			Attempt.bAvailable = true;
			CandidateLedger = Result.Layout.LayoutLedger;
			Attempt.DiagnosticCode = CandidateLedger.DiagnosticCode;
			Attempt.bAccepted =
				CandidateLedger.Backend == Attempt.Backend &&
				CandidateLedger.IsCompleteFor(Result.Layout.LayoutRequest);
		}

		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted)
		{
			continue;
		}

		ApplySelectedLedger(Result.Layout, CandidateLedger);
		Result.DiagnosticCode = TEXT("LC3_VALUE_CONE_RIBBON_COMPLETE");
		return Result;
	}

	Result.DiagnosticCode = TEXT("LC3_VALUE_CONE_RIBBON_NO_COMPLETE_LEDGER");
	return Result;
}
