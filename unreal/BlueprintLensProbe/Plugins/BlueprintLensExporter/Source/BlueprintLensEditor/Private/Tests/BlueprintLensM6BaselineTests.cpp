// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6BaselineProjection.h"
#include "BlueprintLensM6NativeGraphBridge.h"
#include "SBlueprintLensM6NativeSlice.h"
#include "SBlueprintLensPanel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace BlueprintLensM6BaselineTests
{
namespace
{
FM6BaselineEntity Entity(
	const TCHAR* Id,
	const TCHAR* Label,
	const TCHAR* SemanticStatus = TEXT("supported"),
	const TCHAR* PresentationStatus = TEXT("supported"))
{
	FM6BaselineEntity Result;
	Result.Id = Id;
	Result.Label = Label;
	Result.Role = Id[0] == TEXT('c') ? TEXT("criterion") : TEXT("control");
	Result.SemanticStatus = SemanticStatus;
	Result.SemanticReason = SemanticStatus[0] == TEXT('s') ? TEXT("") : TEXT("semantic reason");
	Result.PresentationStatus = PresentationStatus;
	Result.PresentationReason = PresentationStatus[0] == TEXT('t') ? TEXT("visible budget") : TEXT("");
	Result.InclusionReasons = {TEXT("controlled fixture")};
	Result.Source.AssetPath = TEXT("/Game/Test/BP_M6.BP_M6");
	Result.Source.GraphId = TEXT("/Game/Test/BP_M6.BP_M6:EventGraph");
	Result.Source.NodeId = Id;
	Result.Source.NativeNodeGuid = FString::Printf(
		TEXT("00000000-0000-0000-0000-%012d"), Id[0]);
	return Result;
}

FM6BaselineRelation Relation(
	const TCHAR* Id,
	const TCHAR* Source,
	const TCHAR* Target,
	const TCHAR* Label)
{
	FM6BaselineRelation Result;
	Result.Id = Id;
	Result.SourceEntityId = Source;
	Result.TargetEntityId = Target;
	Result.SourceEdgeId = Id;
	Result.Label = Label;
	Result.Kind = TEXT("execution_predecessor");
	Result.SemanticLabel = TEXT("next_execution");
	Result.SemanticStatus = TEXT("supported");
	return Result;
}

FM6LoadedSessionPacket Packet()
{
	FM6LoadedSessionPacket Result;
	Result.Request.QueryKind = TEXT("execution");
	Result.Request.RendererId = TEXT("R1_GENERIC_FRAME_FLOW_V1");
	Result.Request.GraphId = TEXT("/Game/Test/BP_M6.BP_M6:EventGraph");
	Result.SemanticSha256 = FString::ChrN(64, TEXT('a'));
	Result.TypedDocument.NodeIds = {TEXT("entry"), TEXT("control"), TEXT("criterion"), TEXT("outside")};
	Result.TypedDocument.EdgeIds = {TEXT("r1"), TEXT("r2"), TEXT("outside-r")};
	Result.BaselineFacts.Format = TEXT("blueprint-lens-m6-baseline-facts");
	Result.BaselineFacts.SchemaVersion = TEXT("1.0.0");
	Result.BaselineFacts.GraphId = Result.Request.GraphId;
	Result.BaselineFacts.RendererId = Result.Request.RendererId;
	Result.BaselineFacts.CriterionEntityId = TEXT("criterion");
	Result.BaselineFacts.Entities = {
		Entity(TEXT("entry"), TEXT("Event BeginPlay")),
		Entity(TEXT("control"), TEXT("Guard"), TEXT("opaque")),
		Entity(TEXT("criterion"), TEXT("Set Result"), TEXT("supported"), TEXT("truncated"))};
	Result.BaselineFacts.Relations = {
		Relation(TEXT("r1"), TEXT("entry"), TEXT("control"), TEXT("then")),
		Relation(TEXT("r2"), TEXT("control"), TEXT("criterion"), TEXT("when true"))};
	for (const FM6BaselineEntity& Item : Result.BaselineFacts.Entities)
	{
		Result.BaselineFacts.EntityIds.Add(Item.Id);
		Result.BaselineFacts.EntitySourceNodeIds.Add(Item.Id, Item.Source.NodeId);
	}
	for (const FM6BaselineRelation& Item : Result.BaselineFacts.Relations)
		Result.BaselineFacts.RelationIds.Add(Item.Id);
	FM6BaselineBoundary Boundary;
	Boundary.NodeId = TEXT("control");
	Boundary.Status = TEXT("opaque");
	Boundary.Reason = TEXT("semantic reason");
	Result.BaselineFacts.Boundaries.Add(Boundary);
	Result.BaselineFacts.TruncatedCount = 1;

	Result.Explanation.Format = TEXT("blueprint-lens-explanation");
	Result.Explanation.SchemaVersion = TEXT("1.0.0");
	Result.Explanation.RulesVersion = TEXT("1.0.0");
	Result.Explanation.CriterionUnitId = TEXT("criterion");
	Result.Explanation.Query.Question = TEXT("Why does Result execute?");
	for (const FM6BaselineEntity& Fact : Result.BaselineFacts.Entities)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = Fact.Id;
		Unit.Title = Fact.Label;
		Unit.Role = Fact.Id == TEXT("criterion")
			? EBlueprintLensRole::Criterion : EBlueprintLensRole::Control;
		Unit.SemanticStatus = Fact.SemanticStatus == TEXT("opaque")
			? EBlueprintLensSemanticStatus::Opaque
			: EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons = Fact.InclusionReasons;
		Result.Explanation.Units.Add(MoveTemp(Unit));
	}
	for (const FM6BaselineRelation& Fact : Result.BaselineFacts.Relations)
	{
		FBlueprintLensRelation Link;
		Link.Id = Fact.Id;
		Link.SourceUnitId = Fact.SourceEntityId;
		Link.TargetUnitId = Fact.TargetEntityId;
		Link.Label = Fact.Label;
		Result.Explanation.Relations.Add(MoveTemp(Link));
	}
	return Result;
}

FM6LoadedSessionPacket ControlCriterionPacket()
{
	FM6LoadedSessionPacket Result = Packet();
	Result.Request.CriterionNodeId = TEXT("control");
	Result.BaselineFacts.CriterionEntityId = TEXT("control");
	Result.BaselineFacts.Entities.RemoveAll(
		[](const FM6BaselineEntity& Item)
		{
			return Item.Id == TEXT("criterion");
		});
	Result.BaselineFacts.EntityIds = {TEXT("entry"), TEXT("control")};
	Result.BaselineFacts.EntitySourceNodeIds.Remove(TEXT("criterion"));
	Result.BaselineFacts.Relations.RemoveAll(
		[](const FM6BaselineRelation& Item)
		{
			return Item.Id == TEXT("r2");
		});
	Result.BaselineFacts.RelationIds = {TEXT("r1")};
	Result.BaselineFacts.TruncatedCount = 0;
	Result.Explanation.CriterionUnitId = TEXT("control");
	Result.Explanation.Units.RemoveAll(
		[](const FBlueprintLensUnit& Item)
		{
			return Item.Id == TEXT("criterion");
		});
	for (FBlueprintLensUnit& Item : Result.Explanation.Units)
	{
		if (Item.Id == TEXT("control")) Item.Role = EBlueprintLensRole::Criterion;
	}
	Result.Explanation.Relations.RemoveAll(
		[](const FBlueprintLensRelation& Item)
		{
			return Item.Id == TEXT("r2");
		});
	return Result;
}

TSet<FString> EntityIds(const TArray<FM6BaselineViewEntity>& Values)
{
	TSet<FString> Result;
	for (const FM6BaselineViewEntity& Value : Values) Result.Add(Value.Id);
	return Result;
}

TSet<FString> RelationIds(const TArray<FM6BaselineViewRelation>& Values)
{
	TSet<FString> Result;
	for (const FM6BaselineViewRelation& Value : Values) Result.Add(Value.Id);
	return Result;
}

bool SameSet(const TSet<FString>& Left, const TSet<FString>& Right)
{
	if (Left.Num() != Right.Num()) return false;
	for (const FString& Value : Left)
	{
		if (!Right.Contains(Value)) return false;
	}
	return true;
}
} // namespace
} // namespace BlueprintLensM6BaselineTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6BaselinesTest,
	"BlueprintLens.M6.Baselines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6BaselinesTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6BaselineTests;
	FM6LoadedSessionPacket Source = Packet();
	const FM6BaselineProjectionResult Built = BuildM6BaselineViewModels(Source);
	TestTrue(TEXT("Matched baseline projection succeeds"), Built.HasValue());
	if (Built.HasError()) AddError(Built.GetError().Code + TEXT(": ") + Built.GetError().Message);
	if (Built.HasError()) return false;
	const FM6BaselineViewModels& Views = Built.GetValue();
	const TSet<FString> ExpectedEntities = {TEXT("entry"), TEXT("control"), TEXT("criterion")};
	const TSet<FString> ExpectedRelations = {TEXT("r1"), TEXT("r2")};
	TestTrue(TEXT("A highlighted entities match facts"), SameSet(Views.A.MemberEntityIds, ExpectedEntities));
	TestTrue(TEXT("B entities match A"), SameSet(EntityIds(Views.B.Entities), Views.A.MemberEntityIds));
	TestTrue(TEXT("C entities match A"), SameSet(EntityIds(Views.C.Entities), Views.A.MemberEntityIds));
	TestTrue(TEXT("A highlighted relations match facts"), SameSet(Views.A.MemberRelationIds, ExpectedRelations));
	TestTrue(TEXT("B relations match A"), SameSet(RelationIds(Views.B.Relations), Views.A.MemberRelationIds));
	TestTrue(TEXT("C relations match A"), SameSet(RelationIds(Views.C.Relations), Views.A.MemberRelationIds));
	TestEqual(TEXT("B and C semantic entities agree"), Views.B.Entities, Views.C.Entities);
	TestEqual(TEXT("B and C semantic relations agree"), Views.B.Relations, Views.C.Relations);
	TestTrue(TEXT("A preserves full-context non-member"), Views.A.NonMemberEntityIds.Contains(TEXT("outside")));
	TestTrue(TEXT("B is read-only"), Views.B.bReadOnly);
	TestTrue(TEXT("B links are induced by selected endpoints"), Views.B.bAllLinksInduced);
	TestEqual(TEXT("C selects generic renderer"), Views.C.RendererId, FString(TEXT("R1_GENERIC_FRAME_FLOW_V1")));
	TestTrue(TEXT("C bypasses LC specialized routes"), Views.C.bSpecializedRoutesBypassed);
	TestEqual(TEXT("Truncated is explicit in B"), Views.B.TruncatedCount, 1);
	TestEqual(TEXT("Truncated is explicit in C"), Views.C.TruncatedCount, 1);
	TestEqual(TEXT("Opaque boundary preserved"), Views.B.BoundaryCount, 1);

	FM6LoadedSessionPacket MissingExplanationUnit = Packet();
	MissingExplanationUnit.Explanation.Units.RemoveAt(0);
	const FM6BaselineProjectionResult MissingUnitProjection =
		BuildM6BaselineViewModels(MissingExplanationUnit);
	TestTrue(
		TEXT("C projection rejects an Explanation missing a shared unit"),
		MissingUnitProjection.HasError());

	FBlueprintLensExplanationModel NavigationModel;
	FBlueprintLensUnit NavigationUnit;
	NavigationUnit.Id = TEXT("unit.criterion.fixture");
	FBlueprintLensSourceReference NavigationSource;
	NavigationSource.SourceNodeId = TEXT("criterion");
	NavigationSource.bPrimary = true;
	NavigationUnit.SourceReferences.Add(NavigationSource);
	NavigationModel.Units.Add(NavigationUnit);
	const FBlueprintLensUnit* NavigationMatch =
		FindM6ExplanationUnitBySourceEntityId(NavigationModel, TEXT("criterion"));
	TestNotNull(TEXT("Baseline entity resolves through explanation source identity"), NavigationMatch);
	if (NavigationMatch != nullptr)
		TestEqual(TEXT("Source identity resolves the owning explanation unit"), NavigationMatch->Id, NavigationUnit.Id);
	TestNull(
		TEXT("Unknown baseline entity has no explanation source mapping"),
		FindM6ExplanationUnitBySourceEntityId(NavigationModel, TEXT("missing")));

	FM6NativeGraphBridge Bridge;
	FM6Error BridgeError;
	TestTrue(TEXT("Automation membership applies"), Bridge.ApplyMembershipForAutomationTest(Source.BaselineFacts, Source.TypedDocument.NodeIds.Array(), BridgeError));
	TestTrue(TEXT("Native membership selects all session nodes"), SameSet(Bridge.GetAppliedSelectionForAutomationTest(), Views.A.MemberEntityIds));
	FM6BaselineFacts RejectedReplacement = Source.BaselineFacts;
	RejectedReplacement.Entities[0].Id = TEXT("not-in-full-graph");
	FM6Error RejectedReplacementError;
	TestFalse(
		TEXT("an invalid replacement membership is rejected"),
		Bridge.ApplyMembershipForAutomationTest(
			RejectedReplacement,
			Source.TypedDocument.NodeIds.Array(),
			RejectedReplacementError));
	TestTrue(
		TEXT("a rejected replacement preserves the previous native membership"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			Views.A.MemberEntityIds));
	const FM6NativeSelectionObservation Outside = Bridge.ObserveSelectionForAutomationTest({TEXT("outside")});
	TestTrue(TEXT("Native non-member is reported outside session"), Outside.bOutsideCurrentSession);
	TestEqual(TEXT("Outside selection identity is stable"), Outside.EntityId, FString(TEXT("outside")));
	Bridge.TickForAutomationTest();
	TestTrue(
		TEXT("Reader selection persists across the next bridge tick"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			TSet<FString>({TEXT("outside")})));
	TestTrue(
		TEXT("Explicit baseline highlight repaints membership after reader selection"),
		Bridge.ApplyMembershipForAutomationTest(
			Source.BaselineFacts,
			Source.TypedDocument.NodeIds.Array(),
			BridgeError) &&
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			Views.A.MemberEntityIds));
	TestTrue(TEXT("Known semantic focus succeeds"), Bridge.FocusSemanticEntity(TEXT("control")).HasValue());
	TestTrue(
		TEXT("Panel semantic focus temporarily selects its one entity"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			TSet<FString>({TEXT("control")})));
	TestTrue(TEXT("Unknown semantic focus fails"), Bridge.FocusSemanticEntity(TEXT("missing")).HasError());
	Bridge.TickForAutomationTest();
	TestTrue(
		TEXT("Panel semantic focus restores baseline membership one tick later"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			Views.A.MemberEntityIds));

	TSharedRef<SBlueprintLensM6NativeSlice> Widget =
		SNew(SBlueprintLensM6NativeSlice).ViewModel(&Views.B);
	TestTrue(TEXT("Native slice widget is read-only"), Widget->IsReadOnlyForAutomationTest());
	TestEqual(TEXT("Native slice exposes exact entity count"), Widget->EntityCountForAutomationTest(), 3);
	TestEqual(TEXT("Native slice exposes exact relation count"), Widget->RelationCountForAutomationTest(), 2);

	FM6LoadedSessionPacket Unsupported = Packet();
	Unsupported.BaselineFacts.RendererId = TEXT("LC2_SPECIALIZED");
	const FM6BaselineProjectionResult Rejected = BuildM6BaselineViewModels(Unsupported);
	TestTrue(TEXT("Unsupported renderer fails closed"), Rejected.HasError());
	if (Rejected.HasError())
		TestEqual(TEXT("Unsupported renderer stable code"), Rejected.GetError().Code, FString(TEXT("M6_VIEW_PROFILE_UNSUPPORTED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6ReaderSelectionPersistenceTest,
	"BlueprintLens.M6.ReaderSelectionPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6ReaderSelectionPersistenceTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6BaselineTests;
	FM6LoadedSessionPacket Source = Packet();
	const FM6BaselineProjectionResult Built = BuildM6BaselineViewModels(Source);
	TestTrue(TEXT("reader-selection fixture projects"), Built.HasValue());
	if (Built.HasError()) return false;
	const FM6BaselineViewModels& Views = Built.GetValue();
	const FM6BaselineProjectionResult ReplacementBuilt =
		BuildM6BaselineViewModels(ControlCriterionPacket());
	TestTrue(TEXT("replacement reader-selection fixture projects"), ReplacementBuilt.HasValue());
	if (ReplacementBuilt.HasError()) return false;
	const FM6BaselineViewModels& ReplacementViews = ReplacementBuilt.GetValue();

	TSharedRef<SBlueprintLensPanel> Surface =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	FM6PanelPresentationModel& Panel = Surface->M6Presentation;
	FM6SessionSnapshot Ready;
	Ready.State = EM6SessionState::Ready;
	Ready.bHasReadySession = true;
	FM6QueryInput Submitted;
	int32 RunCount = 0;
	FM6PanelActionHandlers Handlers;
	Handlers.Run = [&Panel, &Ready, &ReplacementViews, &RunCount, &Submitted](
		const FM6QueryInput& Query)
	{
		++RunCount;
		Submitted = Query;
		if (RunCount == 2) Panel.ApplySession(Ready, &ReplacementViews);
	};
	Panel.SetHandlers(MoveTemp(Handlers));
	Panel.SetPythonReady(true);
	Panel.SetQueryKind(EM6QueryKind::Execution);
	Panel.ObserveExecutionSelection(
		Source.Request.GraphId,
		Source.BaselineFacts.CriterionEntityId,
		TEXT("Set Result"));
	Panel.DispatchRun();
	TestEqual(
		TEXT("Run submits the shown criterion before graph exploration"),
		Submitted.CriterionNodeId,
		Source.BaselineFacts.CriterionEntityId);
	Panel.ApplySession(Ready, &Views);
	const FString ShownCriterion = Panel.ResultQuery().CriterionNodeId;
	const FM6PanelCounts FirstCounts = Panel.SummaryCounts();
	TestEqual(TEXT("first result has the literal fixture entity count"), FirstCounts.Entities, 3);
	TestEqual(TEXT("first result has the literal fixture relation count"), FirstCounts.Relations, 2);

	FM6NativeGraphBridge& Bridge = Surface->M6NativeGraphBridge;
	FM6Error BridgeError;
	TestTrue(
		TEXT("A and B share the membership painted before reader selection"),
		SameSet(Views.A.MemberEntityIds, EntityIds(Views.B.Entities)) &&
			Bridge.ApplyMembershipForAutomationTest(
				Source.BaselineFacts,
				Source.TypedDocument.NodeIds.Array(),
				BridgeError) &&
			SameSet(
				Bridge.GetAppliedSelectionForAutomationTest(),
				Views.A.MemberEntityIds));
	const TSet<FString> OutsideSelection = {TEXT("outside")};
	const FM6NativeSelectionObservation Outside =
		Bridge.ObserveSelectionForAutomationTest(OutsideSelection.Array());
	TestTrue(
		TEXT("outside reader selection is observed outside the current slice"),
		Outside.bOutsideCurrentSession);
	Surface->RefreshM6Content();
	Bridge.TickForAutomationTest();
	TestTrue(
		TEXT("outside reader selection survives panel refresh and the next bridge tick"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			OutsideSelection));
	TestEqual(
		TEXT("outside reader selection does not change the shown answer"),
		Panel.ResultQuery().CriterionNodeId,
		ShownCriterion);

	TestTrue(
		TEXT("an explicit A or B highlight can still repaint membership"),
		Bridge.ApplyMembershipForAutomationTest(
			Source.BaselineFacts,
			Source.TypedDocument.NodeIds.Array(),
			BridgeError) &&
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			Views.A.MemberEntityIds));
	const TSet<FString> InsideSelection = {TEXT("control")};
	const FM6NativeSelectionObservation Inside =
		Bridge.ObserveSelectionForAutomationTest(InsideSelection.Array());
	TestFalse(
		TEXT("inside reader selection is observed inside the current slice"),
		Inside.bOutsideCurrentSession);
	Surface->RefreshM6Content();
	Bridge.TickForAutomationTest();
	TestTrue(
		TEXT("inside reader selection survives panel refresh and the next bridge tick"),
		SameSet(
			Bridge.GetAppliedSelectionForAutomationTest(),
			InsideSelection));
	TestEqual(
		TEXT("inside reader selection does not change the shown answer"),
		Panel.ResultQuery().CriterionNodeId,
		ShownCriterion);
	TestTrue(TEXT("inside reader selection enables the next Run"), Panel.CanRun());
	TestEqual(
		TEXT("inside reader selection is the proposed target for the next Run"),
		Panel.GetQuery().CriterionNodeId,
		FString(TEXT("control")));
	Panel.DispatchRun();
	TestEqual(TEXT("the closed loop dispatches exactly two Runs"), RunCount, 2);
	TestEqual(
		TEXT("the second Run publishes the selected criterion as the new result"),
		Panel.ResultQuery().CriterionNodeId,
		FString(TEXT("control")));
	const FM6PanelCounts SecondCounts = Panel.SummaryCounts();
	TestEqual(TEXT("second result has the literal replacement entity count"), SecondCounts.Entities, 2);
	TestEqual(TEXT("second result has the literal replacement relation count"), SecondCounts.Relations, 1);
	TestTrue(
		TEXT("the second Run replaces the first result counts"),
		SecondCounts.Entities != FirstCounts.Entities &&
			SecondCounts.Relations != FirstCounts.Relations);

	Panel.SetQueryKind(EM6QueryKind::Data);
	Panel.SetDataCriterion(Source.Request.GraphId, TEXT("member-guid"), TEXT("Member"));
	FM6NativeSelectionObservation IgnoredExecutionSelection;
	IgnoredExecutionSelection.EntityId = TEXT("ignored-execution-node");
	IgnoredExecutionSelection.GraphId = Source.Request.GraphId;
	IgnoredExecutionSelection.Label = TEXT("Ignored Execution Node");
	Surface->ObserveM6NativeSelection(IgnoredExecutionSelection);
	TestTrue(
		TEXT("Data mode ignores native node selection and retains its member target"),
		Panel.GetQuery().Kind == EM6QueryKind::Data &&
			Panel.GetQuery().CriterionNodeId.IsEmpty() &&
			Panel.GetQuery().MemberGuid == TEXT("member-guid") &&
			Panel.GetQuery().ExpectedMemberName == TEXT("Member"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6PanelTest,
	"BlueprintLens.M6.Panel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6PanelTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6BaselineTests;
	const FM6BaselineProjectionResult Built = BuildM6BaselineViewModels(Packet());
	if (!Built.HasValue()) return false;
	int32 Runs = 0;
	int32 BaselineChanges = 0;
	int32 Selections = 0;
	int32 SourceJumps = 0;
	int32 Resets = 0;
	FM6PanelActionHandlers Handlers;
	Handlers.Run = [&Runs](const FM6QueryInput&) { ++Runs; };
	Handlers.Baseline = [&BaselineChanges](EM6Baseline) { ++BaselineChanges; };
	Handlers.Selection = [&Selections](const FString&, EM6SelectionOrigin) { ++Selections; };
	Handlers.SourceJump = [&SourceJumps](const FString&) { ++SourceJumps; };
	Handlers.Reset = [&Resets]() { ++Resets; };
	FM6PanelPresentationModel Panel(MoveTemp(Handlers));
	Panel.SetExecutionCriterion(TEXT("Graph"), TEXT("criterion"));
	TestEqual(TEXT("Query control chooses execution"), Panel.GetQuery().Kind, EM6QueryKind::Execution);
	TestTrue(TEXT("Run enabled from Idle"), Panel.CanRun());
	Panel.DispatchRun();
	TestEqual(TEXT("Run dispatches once"), Runs, 1);

	FM6SessionSnapshot Active;
	Active.State = EM6SessionState::Running;
	Active.bHasPendingRequest = true;
	Panel.ApplySession(Active, nullptr);
	TestFalse(TEXT("Run disabled during activity"), Panel.CanRun());
	TestTrue(TEXT("Pending banner visible"), Panel.Banner().Contains(TEXT("pending")));
	FM6SessionSnapshot Ready;
	Ready.State = EM6SessionState::Ready;
	Ready.bHasReadySession = true;
	Panel.ApplySession(Ready, &Built.GetValue());
	TestEqual(TEXT("Summary exposes entity count"), Panel.SummaryCounts().Entities, 3);
	TestEqual(TEXT("Summary exposes relation count"), Panel.SummaryCounts().Relations, 2);
	TestEqual(TEXT("Summary exposes opaque count"), Panel.SummaryCounts().Opaque, 1);
	TestEqual(TEXT("Summary exposes truncated count"), Panel.SummaryCounts().Truncated, 1);
	Panel.SetDetailVisible(true);
	TestEqual(TEXT("Detail uses same counts"), Panel.DetailCounts(), Panel.SummaryCounts());

	Panel.SelectBaseline(EM6Baseline::B);
	TestEqual(TEXT("Baseline changes once"), BaselineChanges, 1);
	TestEqual(TEXT("Baseline switch does not rerun"), Runs, 1);
	Panel.SelectEntity(TEXT("control"), EM6SelectionOrigin::BaselineView);
	Panel.SelectEntity(TEXT("control"), EM6SelectionOrigin::NativeGraph);
	TestEqual(TEXT("Re-entrant semantic selection emits once"), Selections, 1);
	const FString CommittedCriterion = Panel.ResultQuery().CriterionNodeId;
	Panel.SelectEntity(TEXT("outside"), EM6SelectionOrigin::NativeGraph);
	TestEqual(
		TEXT("outside-slice selection states that it was refused"),
		Panel.OutsideStatus(),
		FString(TEXT("outside_current_session")));
	TestEqual(
		TEXT("outside-slice result selection never reassigns the committed criterion"),
		Panel.ResultQuery().CriterionNodeId,
		CommittedCriterion);
	Panel.ObserveOutsideEntity(TEXT("outside"));
	TestEqual(TEXT("Outside selection status visible"), Panel.OutsideStatus(), FString(TEXT("outside_current_session")));
	Panel.SetSourceJumpResult(false, TEXT("node unavailable"));
	Panel.DispatchSourceJump(TEXT("control"));
	TestEqual(TEXT("Source jump semantic action emits once"), SourceJumps, 1);
	TestTrue(TEXT("Source jump failure visible"), Panel.SourceJumpError().Contains(TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED")));
	Panel.SetSourceJumpResult(true, FString());
	TestEqual(
		TEXT("Source jump success is re-observable"),
		Panel.SourceJumpStatus(),
		FString(TEXT("source_opened")));

	FM6SessionSnapshot Stale = Active;
	Stale.bHasReadySession = true;
	Stale.bReadySessionStale = true;
	Panel.ApplySession(Stale, &Built.GetValue());
	TestTrue(TEXT("Stale banner visible"), Panel.Banner().Contains(TEXT("stale")));
	FM6SessionSnapshot Failed = Ready;
	Failed.State = EM6SessionState::Failed;
	Failed.Error.Code = TEXT("M6_RUNNER_NONZERO_EXIT");
	Panel.ApplySession(Failed, &Built.GetValue());
	TestTrue(TEXT("Error banner exposes stable code"), Panel.Banner().Contains(TEXT("M6_RUNNER_NONZERO_EXIT")));

	Panel.DispatchReset();
	TestEqual(TEXT("Reset dispatches once"), Resets, 1);
	TestEqual(TEXT("Reset returns baseline A"), Panel.Baseline(), EM6Baseline::A);
	TestTrue(TEXT("Reset clears selection"), Panel.SelectedEntityId().IsEmpty());
	TestTrue(TEXT("Reset clears outside status"), Panel.OutsideStatus().IsEmpty());
	TestTrue(TEXT("Reset clears source error"), Panel.SourceJumpError().IsEmpty());
	TestTrue(TEXT("Reset clears source status"), Panel.SourceJumpStatus().IsEmpty());
	TestFalse(TEXT("Reset clears details"), Panel.IsDetailVisible());
	TestEqual(TEXT("Reset returns deterministic Idle"), Panel.State(), EM6SessionState::Idle);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
