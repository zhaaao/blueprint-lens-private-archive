#include "BlueprintLensLC7LayoutSession.h"

namespace
{
const FBlueprintLensLayoutNodePlacement* FindNode(
	const FBlueprintLensLayoutLedger& Ledger,
	const FString& UnitId)
{
	return Ledger.Nodes.FindByPredicate([&UnitId](const auto& Node)
	{
		return Node.UnitId == UnitId;
	});
}

bool SameLedger(
	const FBlueprintLensLayoutLedger& Left,
	const FBlueprintLensLayoutLedger& Right,
	const float Tolerance)
{
	if (!Left.CanvasSize.Equals(Right.CanvasSize, Tolerance) ||
		Left.Nodes.Num() != Right.Nodes.Num() ||
		Left.Ports.Num() != Right.Ports.Num() ||
		Left.Edges.Num() != Right.Edges.Num())
	{
		return false;
	}
	for (const FBlueprintLensLayoutNodePlacement& Node : Left.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Other = FindNode(Right, Node.UnitId);
		if (Other == nullptr || !Node.Position.Equals(Other->Position, Tolerance) ||
			!Node.Size.Equals(Other->Size, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : Left.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Other =
			Right.FindPort(Port.UnitId, Port.Label, Port.bInput);
		if (Other == nullptr || !Port.Position.Equals(Other->Position, Tolerance))
		{
			return false;
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Left.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Other =
			Right.Edges.FindByPredicate([&Edge](const auto& Candidate)
			{
				return Candidate.RelationId == Edge.RelationId;
			});
		if (Other == nullptr || Edge.SourceUnitId != Other->SourceUnitId ||
			Edge.TargetUnitId != Other->TargetUnitId ||
			Edge.Family != Other->Family ||
			Edge.BendPoints.Num() != Other->BendPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Edge.BendPoints.Num(); ++Index)
		{
			if (!Edge.BendPoints[Index].Equals(
				Other->BendPoints[Index], Tolerance))
			{
				return false;
			}
		}
	}
	return true;
}

FBlueprintLensLC7LayoutAttempt TryBackend(
	const IBlueprintLensLayoutBackend& Backend,
	const FBlueprintLensLC7Layout& Oracle,
	FBlueprintLensLayoutLedger& OutLedger)
{
	FBlueprintLensLC7LayoutAttempt Attempt;
	Attempt.Backend = Backend.GetBackendKind();
	Attempt.bAvailable = Backend.IsAvailable(Attempt.DiagnosticCode);
	if (!Attempt.bAvailable)
	{
		return Attempt;
	}
	const FBlueprintLensLayoutLedger First = Backend.Layout(Oracle.LayoutRequest);
	Attempt.DiagnosticCode = First.DiagnosticCode;
	const bool bComplete = First.Backend == Attempt.Backend &&
		First.IsCompleteFor(Oracle.LayoutRequest);
	if (!bComplete)
	{
		if (!Attempt.DiagnosticCode.Contains(TEXT("TIMEOUT")) &&
			!Attempt.DiagnosticCode.Contains(TEXT("MALFORMED")))
		{
			Attempt.DiagnosticCode = TEXT("LC7_LAYOUT_BACKEND_INCOMPLETE");
		}
		return Attempt;
	}
	const FBlueprintLensLayoutLedger Second = Backend.Layout(Oracle.LayoutRequest);
	if (!Second.IsCompleteFor(Oracle.LayoutRequest) ||
		!SameLedger(First, Second, 0.0f))
	{
		Attempt.DiagnosticCode = TEXT("LC7_LAYOUT_BACKEND_NONDETERMINISTIC");
		return Attempt;
	}
	FBlueprintLensLC7Layout Candidate = Oracle;
	Candidate.LayoutLedger = First;
	Candidate.CanvasSize = First.CanvasSize;
	Attempt.bAccepted = Candidate.HasValidSharedLedger() &&
		Candidate.MatchesVisualOracle(1.0f) &&
		Candidate.HasNoTextOrRouteCollisions();
	if (!Attempt.bAccepted)
	{
		Attempt.DiagnosticCode = TEXT("LC7_LAYOUT_BACKEND_FIDELITY_MISMATCH");
		return Attempt;
	}
	OutLedger = First;
	return Attempt;
}
} // namespace

bool FBlueprintLensLC7LayoutSessionResult::IsRenderable(
	const FBlueprintLensLC7Projection& Projection) const
{
	return DiagnosticCode == TEXT("LC7_A3_LAYOUT_SESSION_COMPLETE") &&
		Layout.ScaleMode != EBlueprintLensLC7ScaleMode::CompleteText &&
		Layout.CoversProjection(Projection) &&
		Layout.HasValidSharedLedger() &&
		Layout.HasValidRecoverability(Projection) &&
		Layout.HasNoTextOrRouteCollisions() &&
		Layout.MatchesVisualOracle(1.0f);
}

FString FBlueprintLensLC7LayoutSessionResult::AttemptSummary() const
{
	TArray<FString> Parts;
	for (const FBlueprintLensLC7LayoutAttempt& Attempt : Attempts)
	{
		Parts.Add(FString::Printf(TEXT("%s:%s:%s"),
			BlueprintLensLayoutBackendName(Attempt.Backend),
			Attempt.bAccepted ? TEXT("accepted") :
				Attempt.bAvailable ? TEXT("rejected") : TEXT("unavailable"),
			*Attempt.DiagnosticCode));
	}
	return FString::Join(Parts, TEXT(" -> "));
}

FBlueprintLensLC7LayoutSessionResult FBlueprintLensLC7LayoutSession::Build(
	const FBlueprintLensLC7Projection& Projection,
	const float TargetWidth,
	const FString& FocusedSCCId,
	const FBlueprintLensLC7LayoutSessionOptions& Options)
{
	FBlueprintLensLC7TextMetrics Metrics = Options.TextMetrics;
	if (Metrics.UnitLabelSizes.IsEmpty())
	{
		Metrics = FBlueprintLensLC7TextMetrics::MeasuredForProjection(Projection);
	}
	const FBlueprintLensElkLayoutBackend Elk(Options.Elk);
	const FBlueprintLensGraphvizLayoutBackend Graphviz(Options.Graphviz);
	return BuildWithBackends(
		Projection, TargetWidth, FocusedSCCId, Metrics, Elk, Graphviz);
}

FBlueprintLensLC7LayoutSessionResult FBlueprintLensLC7LayoutSession::BuildWithBackends(
	const FBlueprintLensLC7Projection& Projection,
	const float TargetWidth,
	const FString& FocusedSCCId,
	const FBlueprintLensLC7TextMetrics& Metrics,
	const IBlueprintLensLayoutBackend& ElkBackend,
	const IBlueprintLensLayoutBackend& GraphvizBackend)
{
	FBlueprintLensLC7LayoutSessionResult Result;
	Result.Layout = FBlueprintLensLC7LayoutBuilder::Build(
		Projection, TargetWidth, FocusedSCCId, Metrics);
	if (Result.Layout.ScaleMode != EBlueprintLensLC7ScaleMode::Full ||
		!Result.Layout.CoversProjection(Projection) ||
		!Result.Layout.HasValidSharedLedger() ||
		!Result.Layout.HasValidRecoverability(Projection) ||
		!Result.Layout.HasNoTextOrRouteCollisions() ||
		!Result.Layout.MatchesVisualOracle(1.0f))
	{
		Result.Layout.ScaleMode = EBlueprintLensLC7ScaleMode::CompleteText;
		Result.DiagnosticCode = TEXT("LC7_LAYOUT_SESSION_REQUEST_UNAVAILABLE");
		return Result;
	}
	for (const EBlueprintLensLayoutBackendKind Candidate :
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			EBlueprintLensLayoutProfile::Cyclic))
	{
		FBlueprintLensLC7LayoutAttempt Attempt;
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
			Attempt.bAccepted = Ledger.IsCompleteFor(Result.Layout.LayoutRequest);
			Attempt.DiagnosticCode = Attempt.bAccepted
				? TEXT("LC7_LAYOUT_DETERMINISTIC_A3_ACCEPTED")
				: TEXT("LC7_LAYOUT_DETERMINISTIC_A3_INCOMPLETE");
		}
		Result.Attempts.Add(Attempt);
		if (!Attempt.bAccepted)
		{
			continue;
		}
		Result.Layout.LayoutLedger = MoveTemp(Ledger);
		Result.Layout.CanvasSize = Result.Layout.LayoutLedger.CanvasSize;
		Result.DiagnosticCode = TEXT("LC7_A3_LAYOUT_SESSION_COMPLETE");
		return Result;
	}
	Result.Layout.ScaleMode = EBlueprintLensLC7ScaleMode::CompleteText;
	Result.DiagnosticCode = TEXT("LC7_LAYOUT_SESSION_NO_PROVABLE_LEDGER");
	return Result;
}
