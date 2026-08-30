#include "BlueprintLensLC1RailLayoutSession.h"

namespace
{
void ApplyLedger(FBlueprintLensLC1RailLayout& Layout, const FBlueprintLensLayoutLedger& Ledger)
{
	Layout.LayoutLedger = Ledger;
	Layout.CanvasSize = Ledger.CanvasSize;
	for (auto& Node : Layout.Nodes)
	{
		if (const auto* Placement = Ledger.Nodes.FindByPredicate([&Node](const auto& P){ return P.UnitId == Node.UnitId; }))
		{
			Node.Position = Placement->Position;
			Node.Size = Placement->Size;
		}
	}
}

FBlueprintLensLC1RailLayoutAttempt TryBackend(const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC1RailLayout& Layout, const FBlueprintLensLC1RailProjection& Projection,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC1RailLayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable) return Attempt;
	OutLedger = Backend.Layout(Layout.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	Attempt.bAccepted = OutLedger.Backend == Attempt.Backend && OutLedger.IsCompleteFor(Layout.LayoutRequest) &&
		OutLedger.CanvasSize.X <= Layout.LayoutRequest.TargetWidth + 0.5f;
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = FString::Printf(TEXT("LC1_RAIL_LAYOUT_BACKEND_REJECTED:%s"), *Attempt.DiagnosticCode);
	}
	return Attempt;
}
}

bool FBlueprintLensLC1RailLayoutSessionResult::IsRenderable(const FBlueprintLensLC1RailProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC1_RAIL_LAYOUT_SESSION_COMPLETE") && Layout.CoversProjection(Projection) && Layout.HasValidSharedLedger();
}

FString FBlueprintLensLC1RailLayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const auto& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(TEXT("%s:%s:%s"), BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") : Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"), *Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC1RailLayoutSessionResult FBlueprintLensLC1RailLayoutSession::Build(
	const FBlueprintLensLC1RailProjection& Projection, const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth, const FBlueprintLensLC1RailLayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, Explanation, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC1RailLayoutSessionResult FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
	const FBlueprintLensLC1RailProjection& Projection, const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth, const IBlueprintLensLayoutBackend& Elk, const IBlueprintLensLayoutBackend& Graphviz)
{
	FBlueprintLensLC1RailLayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC1RailLayoutBuilder::Build(Projection, Explanation, TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) || !Result.Layout.HasValidSharedLedger())
	{
		Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_SESSION_REQUEST_UNAVAILABLE");
		return Result;
	}
	const TArray<EBlueprintLensLayoutBackendKind> PolicyOrder = {
		EBlueprintLensLayoutBackendKind::ElkLayered,
		EBlueprintLensLayoutBackendKind::GraphvizDot,
		EBlueprintLensLayoutBackendKind::Deterministic};
	for (const EBlueprintLensLayoutBackendKind Candidate : PolicyOrder)
	{
		FBlueprintLensLC1RailLayoutAttempt Attempt;
		FBlueprintLensLayoutLedger Ledger;
		if (Candidate == EBlueprintLensLayoutBackendKind::ElkLayered) Attempt = TryBackend(Elk, Result.Layout, Projection, Ledger);
		else if (Candidate == EBlueprintLensLayoutBackendKind::GraphvizDot) Attempt = TryBackend(Graphviz, Result.Layout, Projection, Ledger);
		else
		{
			Attempt.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
			Attempt.bAvailable = true;
			Ledger = Result.Layout.LayoutLedger;
			Attempt.DiagnosticCode = Ledger.DiagnosticCode;
			Attempt.bAccepted = Ledger.Backend == Attempt.Backend && Ledger.IsCompleteFor(Result.Layout.LayoutRequest);
		}
		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted) continue;
		ApplyLedger(Result.Layout, Ledger);
		Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_SESSION_COMPLETE");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC1_RAIL_LAYOUT_SESSION_UNAVAILABLE");
	return Result;
}
