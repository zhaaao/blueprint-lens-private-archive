#include "BlueprintLensLC2GuardLayoutSession.h"

#include "BlueprintLensLC2GuardSurfaceLayout.h"

namespace
{
void ApplyLedger(
	FBlueprintLensLC2GuardLayout& Layout,
	const FBlueprintLensLayoutLedger& Ledger)
{
	Layout.LayoutLedger = Ledger;
	Layout.CanvasSize = Ledger.CanvasSize;
	for (FBlueprintLensLC2GuardLayoutNode& Node : Layout.Nodes)
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

FBlueprintLensLC2GuardLayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC2GuardLayout& Layout,
	const FBlueprintLensLC2GuardSurfaceProjection& Projection,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC2GuardLayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}
	OutLedger = Backend.Layout(Layout.LayoutRequest);
	Attempt.DiagnosticCode = OutLedger.DiagnosticCode;
	const bool bComplete = OutLedger.Backend == Attempt.Backend &&
		OutLedger.IsCompleteFor(Layout.LayoutRequest);
	const bool bFits = OutLedger.CanvasSize.X <=
		Layout.LayoutRequest.TargetWidth + 0.5f;
	Attempt.bAccepted = bComplete && bFits &&
		Layout.HasExclusiveCompoundOwnership(Projection);
	if (Attempt.bAccepted)
	{
		FBlueprintLensLC2GuardLayout CandidateLayout = Layout;
		ApplyLedger(CandidateLayout, OutLedger);
		FBlueprintLensLC2GuardLayoutSessionResult CandidateSession;
		CandidateSession.Layout = MoveTemp(CandidateLayout);
		CandidateSession.DiagnosticCode =
			TEXT("LC2_GUARD_LAYOUT_SESSION_COMPLETE");
		const FBlueprintLensLC2GuardSurfaceLayout CandidateSurface =
			FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
				Projection,
				CandidateSession,
				Layout.LayoutRequest.TargetWidth,
				FString());
		Attempt.bAccepted = CandidateSurface.IsRenderable(Projection);
		if (!Attempt.bAccepted)
		{
			Attempt.DiagnosticCode = FString::Printf(
				TEXT("LC2_GUARD_LAYOUT_BACKEND_SURFACE_REJECTED:%s"),
				*CandidateSurface.DiagnosticCode);
		}
	}
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = bComplete && !bFits
			? FString::Printf(
				TEXT("LC2_GUARD_LAYOUT_BACKEND_CANVAS_EXCEEDS_TARGET:%.2f>%.2f"),
				OutLedger.CanvasSize.X, Layout.LayoutRequest.TargetWidth)
			: FString::Printf(
				TEXT("LC2_GUARD_LAYOUT_BACKEND_LEDGER_INCOMPLETE:%s"),
				*Attempt.DiagnosticCode);
	}
	return Attempt;
}
} // namespace

bool FBlueprintLensLC2GuardLayoutSessionResult::IsRenderable(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection) const
{
	return DiagnosticCode == TEXT("LC2_GUARD_LAYOUT_SESSION_COMPLETE") &&
		Layout.CoversProjection(Projection) &&
		Layout.HasExclusiveCompoundOwnership(Projection) &&
		Layout.HasValidSharedLedger();
}

FString FBlueprintLensLC2GuardLayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC2GuardLayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(
			TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") :
				Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC2GuardLayoutSessionResult
FBlueprintLensLC2GuardLayoutSession::Build(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection,
	const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth,
	const FBlueprintLensLC2GuardLayoutSessionOptions& Options)
{
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(Projection, Explanation, TargetWidth, Elk, Graphviz);
}

FBlueprintLensLC2GuardLayoutSessionResult
FBlueprintLensLC2GuardLayoutSession::BuildWithBackends(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection,
	const FBlueprintLensExplanationModel& Explanation,
	const float TargetWidth,
	const IBlueprintLensLayoutBackend& Elk,
	const IBlueprintLensLayoutBackend& Graphviz)
{
	FBlueprintLensLC2GuardLayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC2GuardLayoutBuilder::Build(
		Projection, Explanation, TargetWidth);
	if (!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasExclusiveCompoundOwnership(Projection) ||
		!Result.Layout.HasValidSharedLedger())
	{
		Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_SESSION_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const EBlueprintLensLayoutBackendKind Candidate :
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			EBlueprintLensLayoutProfile::Compound))
	{
		FBlueprintLensLC2GuardLayoutAttempt Attempt;
		FBlueprintLensLayoutLedger Ledger;
		if (Candidate == EBlueprintLensLayoutBackendKind::ElkLayered)
		{
			Attempt = TryBackend(Elk, Result.Layout, Projection, Ledger);
		}
		else if (Candidate == EBlueprintLensLayoutBackendKind::GraphvizDot)
		{
			Attempt = TryBackend(Graphviz, Result.Layout, Projection, Ledger);
		}
		else
		{
			Attempt.Backend = EBlueprintLensLayoutBackendKind::Deterministic;
			Attempt.bAvailable = true;
			Ledger = Result.Layout.LayoutLedger;
			Attempt.DiagnosticCode = Ledger.DiagnosticCode;
			Attempt.bAccepted = Ledger.Backend == Attempt.Backend &&
				Ledger.IsCompleteFor(Result.Layout.LayoutRequest);
			if (Attempt.bAccepted)
			{
				FBlueprintLensLC2GuardLayoutSessionResult CandidateSession;
				CandidateSession.Layout = Result.Layout;
				CandidateSession.DiagnosticCode =
					TEXT("LC2_GUARD_LAYOUT_SESSION_COMPLETE");
				const FBlueprintLensLC2GuardSurfaceLayout CandidateSurface =
					FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
						Projection,
						CandidateSession,
						TargetWidth,
						FString());
				Attempt.bAccepted = CandidateSurface.IsRenderable(Projection);
				if (!Attempt.bAccepted)
				{
					Attempt.DiagnosticCode = FString::Printf(
						TEXT("LC2_GUARD_LAYOUT_BACKEND_SURFACE_REJECTED:%s"),
						*CandidateSurface.DiagnosticCode);
				}
			}
		}
		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted)
		{
			continue;
		}
		ApplyLedger(Result.Layout, Ledger);
		Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_SESSION_COMPLETE");
		return Result;
	}
	Result.DiagnosticCode = TEXT("LC2_GUARD_LAYOUT_SESSION_NO_COMPLETE_LEDGER");
	return Result;
}
