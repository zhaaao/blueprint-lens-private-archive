#pragma once

#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensLC1Disclosure.h"
#include "BlueprintLensLC1RailProjection.h"
#include "BlueprintLensLC2GuardOutlineProjection.h"
#include "BlueprintLensLC2GuardSurfaceProjection.h"
#include "BlueprintLensLC3ValueConeProjection.h"
#include "BlueprintLensLC4AsyncProfile.h"
#include "BlueprintLensLC4AsyncProjection.h"
#include "BlueprintLensLC4SequenceProfile.h"
#include "BlueprintLensLC4SequenceProjection.h"
#include "BlueprintLensLC4SequenceLiveAdapter.h"
#include "BlueprintLensLC5Profile.h"
#include "BlueprintLensLC5Projection.h"
#include "BlueprintLensLC6Profile.h"
#include "BlueprintLensLC6Projection.h"
#include "BlueprintLensLC7LayoutSession.h"
#include "BlueprintLensLC7Profile.h"
#include "BlueprintLensLC7Projection.h"
#include "BlueprintLensM6BaselineProjection.h"
#include "BlueprintLensM6NativeGraphBridge.h"
#include "BlueprintLensM6PythonResolver.h"
#include "BlueprintLensM6SessionController.h"
#include "BlueprintLensM6SessionHost.h"
#include "BlueprintLensSourceNavigator.h"
#include "SBlueprintLensLC2GuardCanvas.h"
#include "SBlueprintLensLC1RailCanvas.h"
#include "SBlueprintLensLC3ValueConeCanvas.h"
#include "SBlueprintLensLC4AsyncPartialOrder.h"
#include "SBlueprintLensLC4SequenceRail.h"
#include "SBlueprintLensLC5TypedPortal.h"
#include "SBlueprintLensLC6FourTrack.h"
#include "SBlueprintLensLC7AdaptiveBackbone.h"
#include "SBlueprintLensM6NativeSlice.h"
#include "Framework/Text/TextLayout.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCompoundWidget.h"

class FBlueprintEditor;
class SBox;
class SButton;
class UBlueprint;
class UEdGraph;

ETextWrappingPolicy BlueprintLensLC7DetailWrappingPolicy();

struct FM6PanelCounts
{
	int32 Entities = 0;
	int32 Relations = 0;
	int32 Boundaries = 0;
	int32 Supported = 0;
	int32 Opaque = 0;
	int32 Uncertain = 0;
	int32 Unsupported = 0;
	int32 Truncated = 0;

	bool operator==(const FM6PanelCounts& Other) const
	{
		return Entities == Other.Entities && Relations == Other.Relations &&
			Boundaries == Other.Boundaries && Supported == Other.Supported &&
			Opaque == Other.Opaque && Uncertain == Other.Uncertain &&
			Unsupported == Other.Unsupported && Truncated == Other.Truncated;
	}
};

enum class EM6PanelStatus : uint8
{
	NeedsSetup,
	Ready,
	Running,
	Stale,
	Failed
};

struct FM6DataMemberRow
{
	FString Name;
	FString Type;
	FString Guid;
	bool bUsableInFocusedGraph = false;
	bool bLocal = false;
	bool bCrossAsset = false;
	FString StatusText;

	bool operator==(const FM6DataMemberRow& Other) const;
	bool operator!=(const FM6DataMemberRow& Other) const { return !(*this == Other); }
};

struct FM6PanelActionHandlers
{
	TFunction<void(const FM6QueryInput&)> Run;
	TFunction<void()> Cancel;
	TFunction<void()> Reset;
	TFunction<void(EM6Baseline)> Baseline;
	TFunction<void(const FString&, EM6SelectionOrigin)> Selection;
	TFunction<void(const FString&)> SourceJump;
};

class FM6PanelPresentationModel
{
public:
	FM6PanelPresentationModel() = default;
	explicit FM6PanelPresentationModel(FM6PanelActionHandlers InHandlers);

	void SetHandlers(FM6PanelActionHandlers InHandlers);
	void SetExecutionCriterion(const FString& GraphId, const FString& EntityId);
	void SetDataCriterion(
		const FString& GraphId,
		const FString& MemberGuid,
		const FString& MemberName);
	void SetQueryKind(EM6QueryKind Kind);
	bool CanEditQuery() const;
	void SetGraphId(FString GraphId) { Query.GraphId = MoveTemp(GraphId); }
	void SetCriterionNodeId(FString EntityId)
	{
		Query.CriterionNodeId = MoveTemp(EntityId);
	}
	void SetMemberGuid(FString MemberGuid) { Query.MemberGuid = MoveTemp(MemberGuid); }
	void SetMemberName(FString MemberName)
	{
		Query.ExpectedMemberName = MoveTemp(MemberName);
	}
	void ApplySession(
		const FM6SessionSnapshot& Snapshot,
		const FM6BaselineViewModels* ViewModels);
	void SetPythonReady(bool bReady) { bPythonReady = bReady; }
	void SetPythonResolution(FM6PythonResolutionResult Result);
	bool IsPythonReady() const { return bPythonReady; }
	const FM6PythonResolutionResult& PythonResolution() const
	{
		return PythonResolutionState;
	}
	// bHasExecutionPin mirrors execution_slice.py's criterion rule: any pin of
	// kind "execution", which the exporter derives from PinCategory == "exec".
	// It defaults true for callers that provide a semantic selection without a
	// live native node.
	void ObserveExecutionSelection(
		const FString& GraphId,
		const FString& EntityId,
		const FString& Label,
		bool bHasExecutionPin = true);
	const FString& ExecutionTargetGraphId() const
	{
		return ObservedExecutionGraphId;
	}
	const FString& ExecutionTargetNodeId() const
	{
		return ObservedExecutionNodeId;
	}
	const FString& ExecutionTargetLabel() const
	{
		return ObservedExecutionNodeLabel;
	}
	bool IsExecutionTargetAnswerable() const
	{
		return bObservedExecutionAnswerable;
	}
	const FString& ExecutionTargetStatusText() const
	{
		return ObservedExecutionStatusText;
	}
	const FString& VisibleExecutionTargetNodeId() const
	{
		return VisibleExecutionNodeId;
	}
	const FString& VisibleExecutionTargetLabel() const
	{
		return VisibleExecutionNodeLabel;
	}
	const FString& VisibleExecutionTargetStatusText() const
	{
		return VisibleExecutionStatusText;
	}
	void SetDataMemberRows(TArray<FM6DataMemberRow> Rows);
	bool SelectDataMember(const FString& Guid);
	void ObserveSourceFingerprint(FString Fingerprint);
	void MarkTargetInvalid(FString Reason);
	void MarkTargetAvailable();
	void MarkShownResultInvalid(FString Reason);
	const TArray<FM6DataMemberRow>& DataMemberRows() const
	{
		return DataMembers;
	}
	bool HasValidTarget() const;
	void SetBlueprintContext(
		FString BlueprintName,
		FString BlueprintPath,
		FString GraphName,
		FString GraphPath);
	const FString& BlueprintName() const { return CurrentBlueprintName; }
	const FString& BlueprintPath() const { return CurrentBlueprintPath; }
	const FString& GraphName() const { return CurrentGraphName; }
	const FString& GraphPath() const { return CurrentGraphPath; }
	void MarkStale(FString Reason);
	uint64 PresentationRevision() const { return PresentationRevisionCounter; }
	void ClearStale();
	bool IsStale() const { return bStale; }
	EM6PanelStatus Status() const;
	FString StatusBadge() const;
	FString StatusMessage() const;
	FString SessionStateLabel() const;
	bool CanRun() const;
	bool CanRetry() const;
	void DispatchRun();
	void DispatchRetry();
	void DispatchCancel();
	void DispatchReset();
	void SelectBaseline(EM6Baseline Baseline);
	void SelectEntity(const FString& EntityId, EM6SelectionOrigin Origin);
	void ObserveOutsideEntity(const FString& EntityId);
	void DispatchSourceJump(const FString& EntityId);
	void SetSourceJumpResult(bool bSucceeded, const FString& Message);
	void SetDetailVisible(bool bVisible) { bDetailVisible = bVisible; }

	const FM6QueryInput& GetQuery() const { return Query; }
	const FM6QueryInput& ResultQuery() const { return ActiveResultQuery; }
	const FString& ResultExecutionTargetLabel() const
	{
		return ActiveResultExecutionTargetLabel;
	}
	EM6SessionState State() const { return SessionState; }
	EM6Baseline Baseline() const { return SelectedBaseline; }
	const FString& SelectedEntityId() const { return SelectedEntity; }
	const FString& Banner() const { return StatusBanner; }
	const FString& OutsideStatus() const { return OutsideSelectionStatus; }
	const FString& SourceJumpError() const { return SourceError; }
	const FString& SourceJumpStatus() const { return SourceStatus; }
	bool IsDetailVisible() const { return bDetailVisible; }
	FM6PanelCounts SummaryCounts() const;
	FM6PanelCounts DetailCounts() const { return SummaryCounts(); }
	const TSharedPtr<const FM6BaselineViewModels>& Views() const
	{
		return ActiveViews;
	}

private:
	void ResetPresentation();

	FM6PanelActionHandlers Handlers;
	FM6QueryInput Query;
	EM6SessionState SessionState = EM6SessionState::Idle;
	FM6Error SessionError;
	EM6Baseline SelectedBaseline = EM6Baseline::A;
	FString SelectedEntity;
	FString StatusBanner;
	FString OutsideSelectionStatus;
	FString SourceError;
	FString SourceStatus;
	TSharedPtr<const FM6BaselineViewModels> ActiveViews;
	FM6QueryInput ActiveResultQuery;
	FString ActiveResultExecutionTargetLabel;
	FString ActiveResultSourceFingerprint;
	bool bDetailVisible = false;
	bool bPythonReady = true;
	FM6PythonResolutionResult PythonResolutionState;
	FString ObservedExecutionGraphId;
	FString ObservedExecutionNodeId;
	FString ObservedExecutionNodeLabel;
	bool bObservedExecutionAnswerable = true;
	FString ObservedExecutionStatusText;
	FString VisibleExecutionNodeId;
	FString VisibleExecutionNodeLabel;
	FString VisibleExecutionStatusText;
	bool bDataTargetSelected = false;
	bool bProposalTargetInvalid = false;
	TArray<FM6DataMemberRow> DataMembers;
	FString CurrentBlueprintName;
	FString CurrentBlueprintPath;
	FString CurrentGraphName;
	FString CurrentGraphPath;
	uint64 PresentationRevisionCounter = 0;
	FString CurrentSourceFingerprint;
	bool bStale = false;
	FString StaleReason;
};

class SBlueprintLensPanel final : public SCompoundWidget, public IM6SessionView
{
public:
	enum class EDisplayMode : uint8
	{
		Lanes,
		FrameFlow,
		Route
	};

	SLATE_BEGIN_ARGS(SBlueprintLensPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		TWeakPtr<FBlueprintEditor> InBlueprintEditor);
	virtual ~SBlueprintLensPanel() override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;
	virtual void Present(const FM6SessionSnapshot& Snapshot) override;
	void AttachM6Controller(FM6SessionController& Controller);
	void EnableM6Session();
	static TArray<FM6DataMemberRow> EnumerateDataMembersForAutomationTest(
		UBlueprint* Blueprint,
		UEdGraph* FocusedGraph);

private:
	enum class ELC4DetailMode : uint8
	{
		None,
		SelectedOutput,
		CompleteText,
		Evidence
	};

	enum class ELC4AsyncDetailMode : uint8
	{
		None,
		Select,
		Proof,
		CompleteText,
		Evidence
	};

	enum class ELC5DetailMode : uint8
	{
		None,
		Selection,
		CompleteText,
		Evidence,
		WhyPortal
	};

	enum class ELC6DetailMode : uint8
	{
		None,
		Summary,
		Relations,
		Evidence,
		CompleteText
	};

	enum class ELC7DetailMode : uint8
	{
		None,
		Summary,
		Relations,
		Evidence,
		CompleteText
	};

	void ReloadModel();
	void RefreshM6Context();
	void RescanM6Python();
	void ChooseM6Python();
	void ClearM6Python();
	void ResolveSources();
	TSharedRef<SWidget> BuildLoadedContent();
	TSharedRef<SWidget> BuildM6SessionContent();
	TSharedRef<SWidget> BuildM6CausalContent();
	void RefreshM6Content();
	FReply RunM6();
	FReply CancelM6();
	FReply ResetM6();
	FReply SetM6Baseline(EM6Baseline Baseline);
	FReply ToggleM6Detail();
	FReply ToggleM6SessionChrome();
	void SelectM6Entity(FString EntityId);
	void SynchronizeM6RailSelection(const FString& EntityId);
	void JumpM6Source(FString EntityId);
	void ObserveM6NativeSelection(const FM6NativeSelectionObservation& Observation);
	FString M6ExecutionDisplayLabel(
		const FM6QueryInput& Query,
		const FString& FallbackLabel) const;
	TSharedRef<SWidget> BuildModeSwitcher();
	TSharedRef<SWidget> BuildRepresentationContent();
	bool IsLC1ComparisonModel() const;
	bool IsLC2GuardOutlineModel() const;
	bool IsLC3ValueConeModel() const;
	bool IsLC4AsyncModel() const;
	bool IsLC4SequenceDisclosureModel() const;
	bool IsLC5TypedPortalModel() const;
	bool IsLC6FourTrackModel() const;
	bool IsLC7AdaptiveBackboneModel() const;
	TSharedRef<SWidget> BuildLC2GuardOutlineRepresentation();
	TSharedRef<SWidget> BuildLC2GuardSurfaceRepresentation();
	TSharedRef<SWidget> BuildLC2GuardPathRows(
		const FBlueprintLensLC2GuardOutlinePath& Path);
	TSharedRef<SWidget> BuildLC2FallbackOutline(
		const FBlueprintLensLC2GuardOutlineProjection& Projection);
	TSharedRef<SWidget> BuildLC2TechnicalEvidence(
		const FBlueprintLensLC2GuardOutlineProjection& Projection);
	TSharedRef<SWidget> BuildLC2TechnicalEvidence(
		const FBlueprintLensLC2GuardSurfaceProjection& Projection);
	TSharedRef<SWidget> BuildLC3ValueConeRepresentation();
	TSharedRef<SWidget> BuildLC3LegacyValueConeRepresentation();
	TSharedRef<SWidget> BuildLC3ValueConeRows(
		const FBlueprintLensLC3ValueConeProjection& Projection);
	TSharedRef<SWidget> BuildLC3FallbackOutline(
		const FBlueprintLensLC3ValueConeProjection& Projection);
	TSharedRef<SWidget> BuildLC3TechnicalEvidence(
		const FBlueprintLensLC3ValueConeProjection& Projection);
	TSharedRef<SWidget> BuildLC4SequenceRepresentation();
	TSharedRef<SWidget> BuildLC4AsyncRepresentation();
	TSharedRef<SWidget> BuildLC4AsyncFrontier(
		const FBlueprintLensLC4AsyncProjection& Projection);
	TSharedRef<SWidget> BuildLC5TypedPortalRepresentation();
	TSharedRef<SWidget> BuildLC5Frontier(
		const FBlueprintLensLC5Projection& Projection);
	TSharedRef<SWidget> BuildLC5Detail(
		const FBlueprintLensLC5Projection& Projection);
	FBlueprintLensLC5Projection BuildCurrentLC5Projection() const;
	TSharedRef<SWidget> BuildLC6FourTrackRepresentation();
	TSharedRef<SWidget> BuildLC6FourTrackSurface(
		const FBlueprintLensLC6Projection& Projection,
		const FBlueprintLensLC6LayoutSessionResult& Session,
		const FName& CanvasTag);
	FBlueprintLensLC6Projection BuildCurrentLC6Projection() const;
	TSharedRef<SWidget> BuildLC6Detail(
		const FBlueprintLensLC6Projection& Projection);
	TSharedRef<SWidget> BuildLC6CompleteTextFallback(
		const FBlueprintLensLC6Projection& Projection);
	TSharedRef<SWidget> BuildLC7AdaptiveBackboneRepresentation();
	TSharedRef<SWidget> BuildLC7Detail(
		const FBlueprintLensLC7Projection& Projection);
	TSharedRef<SWidget> BuildLC7CompleteTextFallback(
		const FBlueprintLensLC7Projection& Projection);
	bool IsLC7PanelSessionRenderable(
		const FBlueprintLensLC7Projection& Projection,
		const FBlueprintLensLC7LayoutSessionResult& Session) const;
	TSharedRef<SWidget> BuildLC4CompleteTextFallback(
		const FBlueprintLensLC4SequenceProjection& Projection);
	TSharedRef<SWidget> BuildLC4TechnicalEvidence(
		const FBlueprintLensLC4SequenceProjection& Projection);
	TSharedRef<SWidget> BuildLaneRepresentation();
	TSharedRef<SWidget> BuildFrameFlowRepresentation();
	TSharedRef<SWidget> BuildLC1FrameFlowRepresentation(
		const FBlueprintLensFrameFlowLayoutModel& Layout);
	TSharedRef<SWidget> BuildLC1RailRepresentation(
		const FBlueprintLensFrameFlowLayoutModel& FallbackLayout);
	TSharedRef<SWidget> BuildM6CausalRailContent(
		const TSharedPtr<const FBlueprintLensExplanationModel>& Explanation,
		const FBlueprintLensLC1RailProjection& Projection,
		const FBlueprintLensLC1RailLayoutSessionResult& LayoutSession,
		const FBlueprintLensCompositeRailSlots& CompositeSlots,
		const TSharedPtr<SWidget>& ExpandedStationAppearance,
		const FString& ExpandedStationAppearanceUnitId,
		float ReviewWidth,
		bool bDataAnswer = false,
		const TSharedPtr<SWidget>& ExpandedBetweenDecoration = nullptr,
		const FString& ExpandedBetweenDecorationRelationId = FString(),
		const TSharedPtr<SWidget>& ExpandedSpanAttachment = nullptr,
		const FString& ExpandedSpanAttachmentId = FString(),
		const TSharedPtr<SWidget>& ExpandedTerminalAttachment = nullptr,
		const FString& ExpandedTerminalAttachmentUnitId = FString());
	void ToggleM6CompositeDisclosure(const FString& UnitId);
	void ToggleM6AttachmentDisclosure(
		const FString& UnitId,
		const FString& GrammarId);
	void ToggleM6BetweenDisclosure(const FString& RelationId);
	void ToggleM6SpanDisclosure(const FString& SpanId);
	void ToggleM6CompositeFold();
	TSharedRef<SWidget> BuildLC1DisclosureSwitcher();
	TSharedRef<SWidget> BuildLC1PlainOutline(
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1DisclosureProjection& Projection);
	TSharedRef<SWidget> BuildLC1EvidenceRegions(
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1DisclosureProjection& Projection);
	TSharedRef<SWidget> BuildLC1PairedPseudocode(
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1DisclosureProjection& Projection);
	TSharedRef<SWidget> BuildLC1PseudocodeEditor(
		const FBlueprintLensLC1PseudocodeProjection& Projection);
	TSharedRef<SWidget> BuildLC1TechnicalEvidence(
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FBlueprintLensLC1DisclosureProjection& Projection);
	TSharedRef<SWidget> BuildLC1SelectedSourceAction();
	TSharedRef<SWidget> BuildLC1OrderedRows(
		const TArray<FString>& OrderedUnitIds,
		bool bIncludeSequenceNumbers);
	TSharedRef<SWidget> BuildLC1WhyGrouped(
		const FBlueprintLensLC1RegionProjection& Region);
	TSharedRef<SWidget> BuildLC1EmptyStateStrip();
	TSharedRef<SWidget> BuildRouteRepresentation();
	TSharedRef<SWidget> BuildAnalysisTruthStrip();
	TSharedRef<SWidget> BuildErrorContent(
		const FString& Path,
		const FString& Error);
	TSharedRef<SWidget> BuildLane(const FBlueprintLensLane& Lane);
	TSharedRef<SWidget> BuildUnitCard(const FBlueprintLensUnit& Unit);
	TSharedRef<SWidget> BuildCompactUnit(
		const FBlueprintLensUnit& Unit,
		const FString& RoleLabel,
		bool bEmphasise = false);
	TSharedRef<SWidget> BuildRelationMarker(
		const FString& Label,
		const FLinearColor& Accent);
	FString BuildUnitEvidenceTooltip(const FBlueprintLensUnit& Unit) const;
	const FBlueprintLensLane* FindLane(EBlueprintLensRole Role) const;
	const FBlueprintLensRelation* FindRelation(
		const FString& SourceUnitId,
		const FString& TargetUnitId,
		EBlueprintLensRelationKind Kind) const;
	void SetDisplayMode(EDisplayMode NewMode);
	void SetLC1DisclosureCandidate(
		EBlueprintLensLC1DisclosureCandidate NewCandidate);
	void ToggleLC1RegionMembers();
	void ToggleLC1ShowAll();
	void ToggleLC1WhyGrouped();
	void ToggleLC1Pseudocode();
	void ToggleLC1TechnicalEvidence();
	void SelectLC1Unit(const FString& UnitId);
	void SelectM6RailUnit(const FString& UnitId);
	void SetLC1RailDensity(bool bEvidence);
	void SelectLC1PseudocodeLine(const FString& LineId);
	void ToggleLC2TechnicalEvidence();
	void SetLC2GuardDensity(EBlueprintLensLC2GuardDensity NewDensity);
	void SelectLC2Unit(const FString& UnitId);
	void ToggleLC3TechnicalEvidence();
	void SetLC3ValueConeDensity(
		EBlueprintLensLC3ValueConeDensity NewDensity);
	void SelectLC3Unit(const FString& UnitId);
	void SelectLC4Output(int32 Ordinal);
	void ToggleLC4CompleteText();
	void ToggleLC4Evidence();
	void OpenLC4SelectedSource();
	void HandleLC4AsyncAction(FString ActionId);
	void HandleLC5Action(FString ActionId);
	void SelectLC5Occurrence(FString OccurrenceId);
	void SelectLC6Scenario(FString ScenarioId);
	void HandleLC6Action(FString ActionId);
	void SelectLC7Unit(FString UnitId);
	void HandleLC7Action(FString ActionId);
	static bool TryDeriveLC7TruthPaths(
		const FString& ExplanationPath,
		FString& OutProfilePath,
		FString& OutReviewedPath,
		FString& OutReadinessPath);
	FString PrimarySourceNodeId(const FBlueprintLensUnit& Unit) const;
	bool CanNavigateToSource(const FString& SourceNodeId) const;
	FReply NavigateToSource(const FString& SourceNodeId);
	void PopulateExplanationOptions();
	TSharedRef<SWidget> BuildCaseSelector();

	TWeakPtr<FBlueprintEditor> BlueprintEditor;
	TSharedPtr<SBox> RootBox;
	TSharedPtr<SButton> M6RunButton;
	TSharedPtr<const FBlueprintLensExplanationModel> Model;
	TSharedPtr<const FBlueprintLensLC4AsyncProfile> LC4AsyncProfile;
	TSharedPtr<const FBlueprintLensLC4SequenceProfile> LC4SequenceProfile;
	TSharedPtr<const FBlueprintLensLC5Profile> LC5Profile;
	TSharedPtr<const FBlueprintLensLC6Profile> LC6Profile;
	TSharedPtr<const FBlueprintLensLC7Profile> LC7Profile;
	FBlueprintLensSourceNavigator SourceNavigator;
	FM6SessionController* M6Controller = nullptr;
	TUniquePtr<FM6SessionHost> M6SessionHost;
	FM6PanelPresentationModel M6Presentation;
	FM6NativeGraphBridge M6NativeGraphBridge;
	TSharedPtr<const FM6LoadedSessionPacket> M6ReadyPacket;
	FString M6LoadedSemanticSha256;
	bool bM6Attached = false;
	bool bM6SessionChromeExpanded = true;
	bool bM6SessionChromeUserOverride = false;
	bool bM6SessionChromeHasPresentedReadySession = false;
	TWeakObjectPtr<UBlueprint> M6ObservedBlueprint;
	FString M6ObservedBlueprintPath;
	FString M6ObservedGraphPath;
	FString M6ObservedSourceFingerprint;
	double M6NextContextProbeTime = 0.0;
#if WITH_DEV_AUTOMATION_TESTS
	FString M6VisibleReviewOutsideEntityId;
	bool bM6VisibleReviewOutsideSelectionApplied = false;
	FString M6VisibleReviewTargetEntityId;
	bool bM6VisibleReviewTargetSelectionApplied = false;
#endif
	TMap<FString, FBlueprintLensResolvedSource> ResolvedSources;
	TArray<TSharedPtr<FString>> ExplanationOptions;
	TArray<FString> ExplanationPaths;
	TSharedPtr<FString> SelectedOption;
	TArray<TSharedPtr<FString>> M6QueryKindOptions;
	EDisplayMode DisplayMode = EDisplayMode::FrameFlow;
	TOptional<EBlueprintLensLC1DisclosureCandidate>
		LC1DisclosureCandidate;
	bool bLC1RegionMembersExpanded = false;
	bool bLC1ShowAllExpanded = false;
	bool bLC1WhyGroupedExpanded = false;
	bool bLC1PseudocodeExpanded = false;
	bool bLC1TechnicalEvidenceExpanded = false;
	FString LC1SelectedUnitId;
	TSet<FString> M6ExpandedGuardUnitIds;
	FString M6ExpandedAttachmentUnitId;
	FString M6ExpandedAttachmentGrammarId;
	FString M6ExpandedBetweenRelationId;
	FString M6ExpandedSpanId;
	bool bM6CompositeFoldExpanded = false;
	FString LC1SelectedPseudocodeLineId;
	TSharedPtr<SBlueprintLensLC1RailCanvas> LC1RailCanvas;
	TSharedPtr<SScrollBox> LC1RailScrollBox;
	float LC1RailLayoutWidth = 700.0f;
	float LC1RailScrollOffset = 0.0f;
	float LC1AppliedReviewScrollOffset = -1.0f;
	TWeakPtr<SScrollBox> LC1AppliedReviewScrollBox;
	bool bLC1RailEvidence = false;
	bool bLC2TechnicalEvidenceExpanded = false;
	EBlueprintLensLC2GuardDensity LC2GuardDensity =
		EBlueprintLensLC2GuardDensity::Summary;
	FString LC2SelectedUnitId;
	TSharedPtr<SBlueprintLensLC2GuardCanvas> LC2GuardCanvas;
	TSharedPtr<SScrollBox> LC2GuardScrollBox;
	float LC2GuardLayoutWidth = 700.0f;
	float LC2GuardScrollOffset = 0.0f;
	bool bLC3TechnicalEvidenceExpanded = false;
	EBlueprintLensLC3ValueConeDensity LC3ValueConeDensity =
		EBlueprintLensLC3ValueConeDensity::Summary;
	FString LC3SelectedUnitId;
	int32 LC4SelectedOutputOrdinal = INDEX_NONE;
	TSharedPtr<SScrollBox> LC4SequenceScrollBox;
	float LC4SequenceScrollOffset = 0.0f;
	float LC4AppliedReviewScrollOffset = -1.0f;
	ELC4DetailMode LC4DetailMode = ELC4DetailMode::None;
	FString LC4AsyncVariant = TEXT("A_FIRST");
	ELC4AsyncDetailMode LC4AsyncDetailMode = ELC4AsyncDetailMode::None;
	TSharedPtr<SScrollBox> LC4AsyncScrollBox;
	float LC4AsyncScrollOffset = 0.0f;
	ELC5DetailMode LC5DetailMode = ELC5DetailMode::None;
	FString LC5SelectedOccurrenceId;
	TSharedPtr<SScrollBox> LC5ScrollBox;
	float LC5ScrollOffset = 0.0f;
	float LC5AppliedReviewScrollOffset = -1.0f;
	TWeakPtr<SScrollBox> LC5AppliedReviewScrollBox;
	ELC6DetailMode LC6DetailMode = ELC6DetailMode::None;
	FString LC6SelectedScenarioId;
	TSharedPtr<SScrollBox> LC6OverviewScrollBox;
	TSharedPtr<SScrollBox> LC6DetailScrollBox;
	float LC6OverviewScrollOffset = 0.0f;
	float LC6DetailScrollOffset = 0.0f;
	ELC7DetailMode LC7DetailMode = ELC7DetailMode::None;
	FString LC7SelectedUnitId;
	TSharedPtr<SBlueprintLensLC7AdaptiveBackbone> LC7Canvas;
	TSharedPtr<SScrollBox> LC7OverviewScrollBox;
	TSharedPtr<SScrollBox> LC7DetailScrollBox;
	float LC7OverviewScrollOffset = 0.0f;
	float LC7DetailScrollOffset = 0.0f;
	bool bGuardExpanded = true;
	bool bHasStaleSource = false;
	FString LastError;
	FString NavigationMessage;

	friend class FBlueprintLensLC1DisclosureStateTest;
	friend class FBlueprintLensLC1RailStateTest;
	friend class FBlueprintLensLC2GuardOutlineProjectionTest;
	friend class FBlueprintLensLC2GuardSurfaceStateTest;
	friend class FBlueprintLensLC3ValueConeStateTest;
	friend class FBlueprintLensLC4SequenceStateTest;
	friend class FBlueprintLensLC4AsyncPanelStateTest;
	friend class FBlueprintLensLC5PanelStateTest;
	friend class FBlueprintLensLC6PanelStateTest;
	friend class FBlueprintLensLC7PanelStateTest;
	friend class FBlueprintLensM6PanelEntryTest;
	friend class FBlueprintLensM10CompositeSlotsTest;
	friend class FBlueprintLensM6ReaderSelectionPersistenceTest;
};
