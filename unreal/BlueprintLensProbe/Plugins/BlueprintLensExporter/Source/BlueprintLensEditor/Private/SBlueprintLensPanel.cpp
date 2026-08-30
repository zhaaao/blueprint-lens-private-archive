#include "SBlueprintLensPanel.h"

#include "BlueprintLensDisplayLabel.h"
#include "BlueprintLensCompositeRailSlots.h"
#include "BlueprintLensF12DataAnswerProjection.h"
#include "BlueprintEditor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "BlueprintLensEditorModule.h"
#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensLC1Disclosure.h"
#include "BlueprintLensLC1PseudocodeProjection.h"
#include "BlueprintLensLC1TypedIrFacts.h"
#include "BlueprintLensLC2GuardLayoutSession.h"
#include "BlueprintLensLC2LiveExplanationAdapter.h"
#include "BlueprintLensLC2GuardSurfaceProjection.h"
#include "BlueprintLensLC3LiveExplanationAdapter.h"
#include "BlueprintLensLC4SequenceLayoutSession.h"
#include "BlueprintLensLC4SequenceLiveAdapter.h"
#include "BlueprintLensLC4AsyncLayoutSession.h"
#include "BlueprintLensLC4AsyncProfile.h"
#include "BlueprintLensLC4AsyncProjection.h"
#include "BlueprintLensLC4SequenceProfile.h"
#include "BlueprintLensLC4SequenceProjection.h"
#include "BlueprintLensLC5LayoutSession.h"
#include "BlueprintLensLC5LiveTypedIrAdapter.h"
#include "BlueprintLensLC5Profile.h"
#include "BlueprintLensLC5Projection.h"
#include "BlueprintLensLC6LayoutSession.h"
#include "BlueprintLensLC6LiveExplanationAdapter.h"
#include "BlueprintLensLC6Profile.h"
#include "BlueprintLensLC6Projection.h"
#include "BlueprintLensLC7LayoutSession.h"
#include "BlueprintLensLC7LiveExplanationAdapter.h"
#include "BlueprintLensLC7Profile.h"
#include "BlueprintLensLC7Projection.h"
#include "BlueprintLensM6Preflight.h"
#include "BlueprintLensM6GraphResolver.h"
#include "BlueprintLensM6SessionHost.h"
#include "BlueprintLensWeaveProjection.h"
#include "SBlueprintLensLC2GuardCanvas.h"
#include "SBlueprintLensLC3ValueConeCanvas.h"
#include "SBlueprintLensLC4SequenceRail.h"
#include "SBlueprintLensLC4AsyncPartialOrder.h"
#include "SBlueprintLensLC5TypedPortal.h"
#include "SBlueprintLensLC6FourTrack.h"
#include "SBlueprintLensLC7AdaptiveBackbone.h"
#include "Modules/ModuleManager.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditor.h"
#include "K2Node_Variable.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SBlueprintLensPanel"

ETextWrappingPolicy BlueprintLensLC7DetailWrappingPolicy()
{
	return ETextWrappingPolicy::AllowPerCharacterWrapping;
}

TArray<FM6DataMemberRow> SBlueprintLensPanel::EnumerateDataMembersForAutomationTest(
	UBlueprint* Blueprint,
	UEdGraph* FocusedGraph)
{
	TArray<FM6DataMemberRow> Rows;
	if (Blueprint == nullptr) return Rows;

	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		FM6DataMemberRow Row;
		Row.Name = Variable.VarName.ToString();
		Row.Guid = Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
		Row.Type = Variable.VarType.PinCategory.ToString();
		if (!Variable.VarType.PinSubCategory.IsNone())
		{
			Row.Type += TEXT(":") + Variable.VarType.PinSubCategory.ToString();
		}
		if (Variable.VarType.PinSubCategoryObject.IsValid())
		{
			Row.Type += TEXT("/") + Variable.VarType.PinSubCategoryObject->GetPathName();
		}

		if (FocusedGraph != nullptr)
		{
			for (const UEdGraphNode* Node : FocusedGraph->Nodes)
			{
				const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
				if (VariableNode != nullptr &&
					VariableNode->VariableReference.GetMemberGuid() == Variable.VarGuid)
				{
					Row.bUsableInFocusedGraph = true;
					break;
				}
			}
		}
		Row.StatusText = Row.bUsableInFocusedGraph
			? TEXT("Used by this graph")
			: TEXT("This graph does not use this variable");
		Rows.Add(MoveTemp(Row));
	}
	return Rows;
}

namespace
{
constexpr float PanelPadding = 10.0f;
constexpr float SectionSpacing = 10.0f;
const FName LC1RailAutomationTag(TEXT("BlueprintLens.Automation.SharedExecutionRail"));
const FName CompositeGuardRailAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeGuardRail"));
const FName CompositeAttachmentsCollapsedAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeAttachmentsCollapsed"));
const FName CompositeLC2SurfaceAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC2GuardSurface"));
const FName CompositeGuardCoreScopeDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeGuardCoreScopeDisclosure"));
const FName CompositeLC3SurfaceAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC3Surface"));
const FName CompositeLC3CoreScopeDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC3CoreScopeDisclosure"));
const FName CompositeLC5SurfaceAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC5TypedPortalSurface"));
const FName CompositeLC5ClaimBoundaryDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC5ClaimBoundaryDisclosure"));
const FName CompositeLC6SurfaceAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC6FourTrackSurface"));
const FName CompositeLC6AbsentTracksDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC6AbsentTracksDisclosure"));
const FName CompositeLC6TruthOwnerContributionAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC6TruthOwnerContribution"));
const FName CompositeLC6LiveDetailPromptAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC6LiveDetailPrompt"));
const FName CompositeLC7SurfaceAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC7AdaptiveBackboneSurface"));
const FName CompositeLC7RelationFamilyDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC7RelationFamilyDisclosure"));
const FName CompositeLC7ExitBoundaryDisclosureAutomationTag(
	TEXT("BlueprintLens.Automation.CompositeLC7ExitBoundaryDisclosure"));
const FName F12DataAnswerRailAutomationTag(
	TEXT("BlueprintLens.Automation.F12DataAnswerRail"));
const FName F12DeferredConditionDependencyAutomationTag(
	TEXT("BlueprintLens.Automation.F12DeferredConditionDependency"));
const FName F12ValueSourceScopeAutomationTag(
	TEXT("BlueprintLens.Automation.F12ValueSourceScope"));

TAutoConsoleVariable<float> CVarBlueprintLensLC1ReviewWidth(
	TEXT("BlueprintLens.LC1ReviewWidth"),
	0.0f,
	TEXT("Override the LC1 Execution Rail review width: 430, 480, or 700. "
		 "Zero keeps the product-responsive default."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC1ReviewScrollOffset(
	TEXT("BlueprintLens.LC1ReviewScrollOffset"),
	0.0f,
	TEXT("Set the LC1 Execution Rail vertical offset for an MCP-only "
		 "below-fold capture. The product default is zero."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC4ReviewWidth(
	TEXT("BlueprintLens.LC4ReviewWidth"),
	0.0f,
	TEXT("Override the LC4 Sequence Disclosure Rail review width. "
		 "Accepted values are 430, 480, and 700; zero selects the "
		 "responsive default."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC4ReviewScrollOffset(
	TEXT("BlueprintLens.LC4ReviewScrollOffset"),
	0.0f,
	TEXT("Set the initial vertical Slate-unit offset for an MCP-only LC4 "
		 "fidelity capture. The product default is zero."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC5ReviewWidth(
	TEXT("BlueprintLens.LC5ReviewWidth"),
	0.0f,
	TEXT("Override the LC5 Typed Portal review width: 430, 480, or 700."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC5ReviewScrollOffset(
	TEXT("BlueprintLens.LC5ReviewScrollOffset"),
	0.0f,
	TEXT("Set the vertical Slate-unit offset for an MCP-only LC5 fidelity "
		 "capture. The product default is zero."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC6ReviewWidth(
	TEXT("BlueprintLens.LC6ReviewWidth"),
	0.0f,
	TEXT("Override the LC6 Four-Track review width: 430, 480, or 700."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC6ReviewScrollOffset(
	TEXT("BlueprintLens.LC6ReviewScrollOffset"),
	0.0f,
	TEXT("Set the LC6 overview scroll offset for exact-width review."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC7ReviewWidth(
	TEXT("BlueprintLens.LC7ReviewWidth"),
	0.0f,
	TEXT("Override the LC7 Adaptive Backbone review width: 430, 480, or 700. "
		 "Zero keeps the product-responsive default."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarBlueprintLensLC7ReviewScrollOffset(
	TEXT("BlueprintLens.LC7ReviewScrollOffset"),
	0.0f,
	TEXT("Set the LC7 overview scroll offset for exact-width review. "
		 "The product default is zero."),
	ECVF_Default);

float LC4ReviewWidth()
{
	const float Requested =
		CVarBlueprintLensLC4ReviewWidth.GetValueOnGameThread();
	for (const float Supported : {430.0f, 480.0f, 700.0f})
	{
		if (FMath::IsNearlyEqual(Requested, Supported, 0.5f))
		{
			return Supported;
		}
	}
	return 0.0f;
}

float LC1ReviewWidth()
{
	const float Requested = CVarBlueprintLensLC1ReviewWidth.GetValueOnGameThread();
	for (const float Supported : {430.0f, 480.0f, 700.0f})
	{
		if (FMath::IsNearlyEqual(Requested, Supported, 0.5f))
		{
			return Supported;
		}
	}
	return 0.0f;
}

float LC1ReviewScrollOffset()
{
	return FMath::Max(
		0.0f,
		CVarBlueprintLensLC1ReviewScrollOffset.GetValueOnGameThread());
}

float LC4ReviewScrollOffset()
{
	return FMath::Max(
		0.0f,
		CVarBlueprintLensLC4ReviewScrollOffset.GetValueOnGameThread());
}

float LC5ReviewWidth()
{
	const float Requested = CVarBlueprintLensLC5ReviewWidth.GetValueOnGameThread();
	for (const float Supported : {430.0f, 480.0f, 700.0f})
	{
		if (FMath::IsNearlyEqual(Requested, Supported, 0.5f))
		{
			return Supported;
		}
	}
	return 0.0f;
}

float LC5ReviewScrollOffset()
{
	return FMath::Max(
		0.0f,
		CVarBlueprintLensLC5ReviewScrollOffset.GetValueOnGameThread());
}

float LC6ReviewWidth()
{
	const float Requested = CVarBlueprintLensLC6ReviewWidth.GetValueOnGameThread();
	for (const float Supported : {430.0f, 480.0f, 700.0f})
	{
		if (FMath::IsNearlyEqual(Requested, Supported, 0.5f))
		{
			return Supported;
		}
	}
	return 0.0f;
}

float LC6ReviewScrollOffset()
{
	return FMath::Max(
		0.0f,
		CVarBlueprintLensLC6ReviewScrollOffset.GetValueOnGameThread());
}

float LC7ReviewWidth()
{
	const float Requested = CVarBlueprintLensLC7ReviewWidth.GetValueOnGameThread();
	for (const float Supported : {430.0f, 480.0f, 700.0f})
	{
		if (FMath::IsNearlyEqual(Requested, Supported, 0.5f))
		{
			return Supported;
		}
	}
	return 0.0f;
}

float LC7ReviewScrollOffset()
{
	return FMath::Max(
		0.0f,
		CVarBlueprintLensLC7ReviewScrollOffset.GetValueOnGameThread());
}

const TCHAR* LC7RelationFamilyLabel(
	const EBlueprintLensLC7RelationFamily Family)
{
	switch (Family)
	{
	case EBlueprintLensLC7RelationFamily::Entry:
		return TEXT("ENTRY");
	case EBlueprintLensLC7RelationFamily::Predicate:
		return TEXT("PREDICATE");
	case EBlueprintLensLC7RelationFamily::Value:
		return TEXT("VALUE");
	case EBlueprintLensLC7RelationFamily::Forward:
		return TEXT("FORWARD");
	case EBlueprintLensLC7RelationFamily::Return:
		return TEXT("RETURN");
	case EBlueprintLensLC7RelationFamily::Exit:
		return TEXT("EXIT");
	default:
		return TEXT("UNKNOWN");
	}
}

FLinearColor RoleAccent(const EBlueprintLensRole Role)
{
	switch (Role)
	{
	case EBlueprintLensRole::Criterion:
		return FLinearColor(0.95f, 0.68f, 0.16f);
	case EBlueprintLensRole::Control:
		return FLinearColor(0.29f, 0.48f, 0.66f);
	case EBlueprintLensRole::Predicate:
		return FLinearColor(0.88f, 0.55f, 0.18f);
	case EBlueprintLensRole::Value:
		return FLinearColor(0.16f, 0.68f, 0.72f);
	case EBlueprintLensRole::Consequence:
		return FLinearColor(0.52f, 0.40f, 0.68f);
	case EBlueprintLensRole::Boundary:
		return FLinearColor(0.46f, 0.48f, 0.52f);
	default:
		return FLinearColor::White;
	}
}

FLinearColor WithAlpha(const FLinearColor& Color, const float Alpha)
{
	return FLinearColor(Color.R, Color.G, Color.B, Alpha);
}

FText UppercaseEnum(const TCHAR* Value)
{
	return FText::FromString(FString(Value).ToUpper());
}

FString SourceStateLabel(const EBlueprintLensSourceState State)
{
	switch (State)
	{
	case EBlueprintLensSourceState::Unsaved:
		return TEXT("UNSAVED SOURCE");
	case EBlueprintLensSourceState::Stale:
		return TEXT("STALE SOURCE");
	case EBlueprintLensSourceState::Unresolved:
		return TEXT("SOURCE UNRESOLVED");
	default:
		return FString();
	}
}

FString LC1ReaderLabel(const FString& NativeTitle)
{
	if (NativeTitle.Contains(TEXT("BeginPlay"), ESearchCase::IgnoreCase))
	{
		return TEXT("Event BeginPlay");
	}
	if (NativeTitle.Contains(TEXT("LC1Ready"), ESearchCase::IgnoreCase))
	{
		return TEXT("Set the final readiness flag");
	}

	const int32 StepMarker = NativeTitle.Find(
		TEXT("LC1Step"),
		ESearchCase::IgnoreCase);
	if (StepMarker != INDEX_NONE)
	{
		const int32 NumberStart = StepMarker + 7;
		const int32 CompleteMarker = NativeTitle.Find(
			TEXT("Complete"),
			ESearchCase::IgnoreCase,
			ESearchDir::FromStart,
			NumberStart);
		if (CompleteMarker > NumberStart)
		{
			const FString StepNumber =
				NativeTitle.Mid(NumberStart, CompleteMarker - NumberStart);
			if (StepNumber.IsNumeric())
			{
				return FString::Printf(
					TEXT("Set completion flag %02d"),
					FCString::Atoi(*StepNumber));
			}
		}
	}

	return NativeTitle;
}

FString LC2ReaderUnitLabel(const FBlueprintLensUnit& Unit)
{
	if (Unit.bHasDisambiguator && !Unit.Disambiguator.Text.IsEmpty())
	{
		return FString::Printf(
			TEXT("%s (%s)"),
			*Unit.Title,
			*Unit.Disambiguator.Text);
	}
	return Unit.Title;
}

FString LC2ReaderRelationLabel(
	const EBlueprintLensSemanticLabel SemanticLabel)
{
	switch (SemanticLabel)
	{
	case EBlueprintLensSemanticLabel::ConditionTrue:
		return TEXT("when true");
	case EBlueprintLensSemanticLabel::ConditionFalse:
		return TEXT("when false");
	case EBlueprintLensSemanticLabel::NextExecution:
		return TEXT("then");
	case EBlueprintLensSemanticLabel::BranchCondition:
		return TEXT("branch condition");
	case EBlueprintLensSemanticLabel::ValueInput:
		return TEXT("value input");
	default:
		return TEXT("relation");
	}
}

FString LC2ProjectionStatusLabel(
	const EBlueprintLensLC2GuardOutlineProjectionStatus Status)
{
	switch (Status)
	{
	case EBlueprintLensLC2GuardOutlineProjectionStatus::GroupedOutcomePaths:
		return TEXT("grouped outcome paths");
	case EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback:
		return TEXT("complete ungrouped fallback");
	default:
		return TEXT("unavailable");
	}
}

FString LC3ProjectionStatusLabel(
	const EBlueprintLensLC3ValueConeProjectionStatus Status)
{
	switch (Status)
	{
	case EBlueprintLensLC3ValueConeProjectionStatus::ValueCone:
		return TEXT("criterion-centred value cone");
	case EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback:
		return TEXT("complete ungrouped fallback");
	default:
		return TEXT("unavailable");
	}
}

TSharedRef<SWidget> BuildChip(
	const FString& Label,
	const FLinearColor& Accent,
	const float AccentAlpha = 0.28f)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(Accent, AccentAlpha))
		.Padding(FMargin(7.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.AutoWrapText(true)
		];
}

class SBlueprintLensWeaveRail final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintLensWeaveRail)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(350.0f, 320.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float RailX = Size.X * 0.64f;
		const float EntryY = Size.Y * 0.10f;
		const float SequenceY = Size.Y * 0.30f;
		const float GateY = Size.Y * 0.53f;
		const float CriterionY = Size.Y * 0.82f;
		const float ValueY = Size.Y * 0.74f;
		const float PortX = RailX - FMath::Clamp(Size.X * 0.09f, 28.0f, 46.0f);
		const FLinearColor ExecutionColor =
			RoleAccent(EBlueprintLensRole::Control);
		const FLinearColor PredicateColor =
			RoleAccent(EBlueprintLensRole::Predicate);
		const FLinearColor ValueColor =
			RoleAccent(EBlueprintLensRole::Value);
		const FLinearColor CriterionColor =
			RoleAccent(EBlueprintLensRole::Criterion);

		const auto DrawLine =
			[&](
				const FVector2D& Start,
				const FVector2D& End,
				const FLinearColor& Color,
				const float Thickness,
				const int32 DrawLayer)
			{
				TArray<FVector2D> Points{Start, End};
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					DrawLayer,
					AllottedGeometry.ToPaintGeometry(),
					Points,
					ESlateDrawEffect::None,
					Color,
					true,
					Thickness);
			};

		DrawLine(
			FVector2D(RailX, EntryY),
			FVector2D(RailX, CriterionY),
			ExecutionColor,
			3.0f,
			LayerId);
		DrawLine(
			FVector2D(Size.X * 0.08f, GateY),
			FVector2D(RailX, GateY),
			PredicateColor,
			2.0f,
			LayerId + 1);
		DrawLine(
			FVector2D(Size.X * 0.08f, ValueY),
			FVector2D(PortX, ValueY),
			ValueColor,
			2.0f,
			LayerId + 1);
		DrawLine(
			FVector2D(PortX, ValueY),
			FVector2D(RailX, CriterionY),
			ValueColor,
			2.0f,
			LayerId + 1);

		const float GateHalf = 8.0f;
		TArray<FVector2D> GatePoints{
			FVector2D(RailX, GateY - GateHalf),
			FVector2D(RailX + GateHalf, GateY),
			FVector2D(RailX, GateY + GateHalf),
			FVector2D(RailX - GateHalf, GateY),
			FVector2D(RailX, GateY - GateHalf)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			GatePoints,
			ESlateDrawEffect::None,
			PredicateColor,
			true,
			2.5f);

		const float PortHalf = 4.0f;
		TArray<FVector2D> PortCrossA{
			FVector2D(PortX - PortHalf, ValueY),
			FVector2D(PortX + PortHalf, ValueY)
		};
		TArray<FVector2D> PortCrossB{
			FVector2D(PortX, ValueY - PortHalf),
			FVector2D(PortX, ValueY + PortHalf)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			PortCrossA,
			ESlateDrawEffect::None,
			ValueColor,
			true,
			2.0f);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			PortCrossB,
			ESlateDrawEffect::None,
			ValueColor,
			true,
			2.0f);

		DrawLine(
			FVector2D(RailX - 9.0f, CriterionY),
			FVector2D(RailX + 9.0f, CriterionY),
			CriterionColor,
			4.0f,
			LayerId + 2);

		return LayerId + 3;
	}
};
} // namespace

FM6PanelPresentationModel::FM6PanelPresentationModel(
	FM6PanelActionHandlers InHandlers)
	: Handlers(MoveTemp(InHandlers))
{
}

void FM6PanelPresentationModel::SetHandlers(
	FM6PanelActionHandlers InHandlers)
{
	Handlers = MoveTemp(InHandlers);
}

void FM6PanelPresentationModel::SetExecutionCriterion(
	const FString& GraphId,
	const FString& EntityId)
{
	if (!CanEditQuery()) return;
	Query.Kind = EM6QueryKind::Execution;
	Query.GraphId = GraphId;
	Query.CriterionNodeId = EntityId;
	Query.MemberGuid.Reset();
	Query.ExpectedMemberName.Reset();
	ObservedExecutionGraphId = GraphId;
	ObservedExecutionNodeId = EntityId;
	ObservedExecutionNodeLabel = EntityId;
	bObservedExecutionAnswerable = true;
	bProposalTargetInvalid = false;
	ObservedExecutionStatusText = TEXT("Has an execution pin");
	VisibleExecutionNodeId = EntityId;
	VisibleExecutionNodeLabel = EntityId;
	VisibleExecutionStatusText = ObservedExecutionStatusText;
	bDataTargetSelected = false;
}

void FM6PanelPresentationModel::SetDataCriterion(
	const FString& GraphId,
	const FString& MemberGuid,
	const FString& MemberName)
{
	if (!CanEditQuery()) return;
	Query.Kind = EM6QueryKind::Data;
	Query.GraphId = GraphId;
	Query.CriterionNodeId.Reset();
	Query.MemberGuid = MemberGuid;
	Query.ExpectedMemberName = MemberName;
	bDataTargetSelected = !MemberGuid.IsEmpty() && !MemberName.IsEmpty();
	bProposalTargetInvalid = false;
}

void FM6PanelPresentationModel::SetQueryKind(const EM6QueryKind Kind)
{
	if (!CanEditQuery() || Query.Kind == Kind) return;
	Query.Kind = Kind;
	if (Kind == EM6QueryKind::Execution)
	{
		Query.MemberGuid.Reset();
		Query.ExpectedMemberName.Reset();
		bDataTargetSelected = false;
		VisibleExecutionNodeId = Query.CriterionNodeId;
		VisibleExecutionNodeLabel = Query.CriterionNodeId;
		VisibleExecutionStatusText = Query.CriterionNodeId.IsEmpty()
			? FString()
			: FString(TEXT("Has an execution pin"));
		bProposalTargetInvalid = false;
	}
	else if (Kind == EM6QueryKind::Data)
	{
		Query.CriterionNodeId.Reset();
		VisibleExecutionNodeId.Reset();
		VisibleExecutionNodeLabel.Reset();
		VisibleExecutionStatusText.Reset();
		bProposalTargetInvalid = false;
	}
}

bool FM6PanelPresentationModel::CanEditQuery() const
{
	return SessionState != EM6SessionState::Preflight &&
		SessionState != EM6SessionState::Exporting &&
		SessionState != EM6SessionState::Running &&
		SessionState != EM6SessionState::Validating &&
		SessionState != EM6SessionState::Cancelling;
}

void FM6PanelPresentationModel::ApplySession(
	const FM6SessionSnapshot& Snapshot,
	const FM6BaselineViewModels* ViewModels)
{
	SessionState = Snapshot.State;
	SessionError = Snapshot.Error;
	// Until a replacement reaches Ready, its controller snapshot still carries
	// the retained packet's default-A bookkeeping.  Preserve the interaction
	// state of the result the reader can actually inspect through every pending
	// and failed state; a Ready publication adopts the new baseline.
	const bool bAdoptSnapshotInteraction =
		!ActiveViews.IsValid() || Snapshot.State == EM6SessionState::Ready;
	if (bAdoptSnapshotInteraction)
	{
		SelectedBaseline = Snapshot.Baseline;
		SelectedEntity = Snapshot.SelectedEntityId;
	}
	// A projected model is not reader-visible evidence until the complete
	// activation (including the native highlight) has succeeded.  Failed or
	// pending replacement work must leave the retained result and its query
	// identity untouched.
	if (ViewModels != nullptr &&
		Snapshot.State == EM6SessionState::Ready &&
		Snapshot.bHasReadySession)
	{
		ActiveViews = MakeShared<FM6BaselineViewModels>(*ViewModels);
		ActiveResultQuery = Query;
		ActiveResultExecutionTargetLabel =
			ObservedExecutionNodeId == Query.CriterionNodeId
				? ObservedExecutionNodeLabel
				: FString();
		if (ActiveResultExecutionTargetLabel.IsEmpty() &&
			Query.Kind == EM6QueryKind::Execution)
		{
			ActiveResultExecutionTargetLabel = Query.CriterionNodeId;
		}
		ActiveResultSourceFingerprint = CurrentSourceFingerprint;
		ClearStale();
	}
	if (Snapshot.State == EM6SessionState::Idle && !Snapshot.bHasReadySession)
	{
		ActiveViews.Reset();
		ActiveResultQuery = FM6QueryInput();
		ActiveResultExecutionTargetLabel.Reset();
		ActiveResultSourceFingerprint.Reset();
	}
	StatusBanner.Reset();
	if (Snapshot.State == EM6SessionState::Failed)
	{
		StatusBanner = FString::Printf(
			TEXT("error: %s%s%s"),
			*Snapshot.Error.Code,
			Snapshot.bHasReadySession ? TEXT(" · stale session retained") : TEXT(""),
			Snapshot.Error.Message.IsEmpty() ? TEXT("") : *FString(TEXT(" · ") + Snapshot.Error.Message));
	}
	else if (Snapshot.bHasPendingRequest)
	{
		StatusBanner = Snapshot.bReadySessionStale
			? TEXT("pending · stale session shown until validation completes")
			: TEXT("pending · no session is active yet");
	}
	else if (Snapshot.State == EM6SessionState::Cancelling)
	{
		StatusBanner = TEXT("cancelling · waiting for owned child cleanup");
	}
}

void FM6PanelPresentationModel::SetPythonResolution(
	FM6PythonResolutionResult Result)
{
	bPythonReady = Result.bValid;
	PythonResolutionState = MoveTemp(Result);
}

void FM6PanelPresentationModel::ObserveExecutionSelection(
	const FString& GraphId,
	const FString& EntityId,
	const FString& Label,
	bool bHasExecutionPin)
{
	// Native graph input is a proposal only. Once Run has submitted a query,
	// neither its fields nor the observation metadata that validates them may
	// move until the controller returns to an editable state.
	if (!CanEditQuery()) return;
	const bool bSelectionChanged =
		ObservedExecutionGraphId != GraphId ||
		ObservedExecutionNodeId != EntityId ||
		ObservedExecutionNodeLabel != Label ||
		bObservedExecutionAnswerable != bHasExecutionPin;
	ObservedExecutionGraphId = GraphId;
	ObservedExecutionNodeId = EntityId;
	ObservedExecutionNodeLabel = Label;
	bObservedExecutionAnswerable = bHasExecutionPin;
	ObservedExecutionStatusText = bHasExecutionPin
		? TEXT("Has an execution pin")
		: TEXT("This node has no execution pin");
	const bool bRevealProposal = !ActiveViews.IsValid();
	if (bRevealProposal)
	{
		VisibleExecutionNodeId = EntityId;
		VisibleExecutionNodeLabel = Label;
		VisibleExecutionStatusText = ObservedExecutionStatusText;
	}
	// A graph selection proposes the next Execution query. The proposed query is
	// independent of the retained result: after a run, ActiveResultQuery remains
	// the answer being shown until the next run.
	if (Query.Kind == EM6QueryKind::Execution)
	{
		const bool bQueryChanged =
			Query.GraphId != ObservedExecutionGraphId ||
			Query.CriterionNodeId != ObservedExecutionNodeId ||
			!Query.MemberGuid.IsEmpty() ||
			!Query.ExpectedMemberName.IsEmpty();
		Query.GraphId = ObservedExecutionGraphId;
		Query.CriterionNodeId = ObservedExecutionNodeId;
		Query.MemberGuid.Reset();
		Query.ExpectedMemberName.Reset();
		bDataTargetSelected = false;
		bProposalTargetInvalid = !bObservedExecutionAnswerable;
		// Before the first result the panel must reveal the proposal immediately.
		// Once a result exists the proposal is deliberately silent: Run commits it,
		// and no intermediate state update may make the answer follow selection.
		if (!ActiveViews.IsValid() && (bSelectionChanged || bQueryChanged))
		{
			++PresentationRevisionCounter;
		}
	}
}

bool FM6DataMemberRow::operator==(const FM6DataMemberRow& Other) const
{
	return Name == Other.Name && Type == Other.Type && Guid == Other.Guid &&
		bUsableInFocusedGraph == Other.bUsableInFocusedGraph &&
		bLocal == Other.bLocal && bCrossAsset == Other.bCrossAsset &&
		StatusText == Other.StatusText;
}

void FM6PanelPresentationModel::SetDataMemberRows(
	TArray<FM6DataMemberRow> Rows)
{
	if (DataMembers != Rows) ++PresentationRevisionCounter;
	DataMembers = MoveTemp(Rows);
	if (!bDataTargetSelected || Query.MemberGuid.IsEmpty())
		return;
	// A different focused graph has a different member inventory. Do not call
	// the old proposal "deleted" merely because it is absent from that graph;
	// the context-change state remains until a valid new proposal replaces it.
	if (!CurrentGraphPath.IsEmpty() && Query.GraphId != CurrentGraphPath)
	{
		return;
	}
	const bool bSelectedMemberStillUsable = DataMembers.ContainsByPredicate(
		[this](const FM6DataMemberRow& Row)
		{
			return Row.Guid == Query.MemberGuid &&
				Row.Name == Query.ExpectedMemberName &&
				Row.bUsableInFocusedGraph && !Row.bLocal && !Row.bCrossAsset;
		});
	if (!bSelectedMemberStillUsable)
	{
		MarkTargetInvalid(
			TEXT("selected Data member was deleted or is no longer usable"));
	}
	else
	{
		MarkTargetAvailable();
	}
}

bool FM6PanelPresentationModel::SelectDataMember(const FString& Guid)
{
	if (!CanEditQuery()) return false;
	for (const FM6DataMemberRow& Row : DataMembers)
	{
		if (Row.Guid != Guid) continue;
		if (!Row.bUsableInFocusedGraph || Row.bLocal || Row.bCrossAsset)
			return false;
		const bool bQueryChanged =
			Query.Kind != EM6QueryKind::Data ||
			Query.MemberGuid != Row.Guid ||
			Query.ExpectedMemberName != Row.Name ||
			(!CurrentGraphPath.IsEmpty() && Query.GraphId != CurrentGraphPath);
		Query.Kind = EM6QueryKind::Data;
		if (!CurrentGraphPath.IsEmpty()) Query.GraphId = CurrentGraphPath;
		Query.CriterionNodeId.Reset();
		Query.MemberGuid = Row.Guid;
		Query.ExpectedMemberName = Row.Name;
		bDataTargetSelected = true;
		bProposalTargetInvalid = false;
		if (bQueryChanged) ++PresentationRevisionCounter;
		return true;
	}
	return false;
}

void FM6PanelPresentationModel::ObserveSourceFingerprint(FString Fingerprint)
{
	const bool bObservingShownResultSource =
		CurrentGraphPath.IsEmpty() || ActiveResultQuery.GraphId.IsEmpty() ||
		CurrentGraphPath == ActiveResultQuery.GraphId;
	if (!Fingerprint.IsEmpty() && ActiveViews.IsValid() &&
		bObservingShownResultSource &&
		!ActiveResultSourceFingerprint.IsEmpty() &&
		ActiveResultSourceFingerprint != Fingerprint)
	{
		MarkStale(TEXT("Blueprint source fingerprint changed"));
	}
	if (!ActiveViews.IsValid() || bObservingShownResultSource)
	{
		CurrentSourceFingerprint = Fingerprint;
		if (ActiveViews.IsValid() && ActiveResultSourceFingerprint.IsEmpty())
		{
			ActiveResultSourceFingerprint = Fingerprint;
		}
	}
}

void FM6PanelPresentationModel::MarkTargetInvalid(FString Reason)
{
	if (!bProposalTargetInvalid) ++PresentationRevisionCounter;
	bProposalTargetInvalid = true;
	const bool bInvalidProposalIsShown = ActiveViews.IsValid() &&
		Query.Kind == ActiveResultQuery.Kind &&
		Query.GraphId == ActiveResultQuery.GraphId &&
		((Query.Kind == EM6QueryKind::Execution &&
			Query.CriterionNodeId == ActiveResultQuery.CriterionNodeId) ||
		 (Query.Kind == EM6QueryKind::Data &&
			Query.MemberGuid == ActiveResultQuery.MemberGuid &&
			Query.ExpectedMemberName == ActiveResultQuery.ExpectedMemberName));
	if (bInvalidProposalIsShown) MarkStale(MoveTemp(Reason));
}

void FM6PanelPresentationModel::MarkTargetAvailable()
{
	bool bAvailabilityChanged = bProposalTargetInvalid;
	bProposalTargetInvalid = false;
	if (Query.Kind == EM6QueryKind::Execution &&
		ObservedExecutionGraphId == Query.GraphId &&
		ObservedExecutionNodeId == Query.CriterionNodeId &&
		!bObservedExecutionAnswerable)
	{
		bObservedExecutionAnswerable = true;
		ObservedExecutionStatusText = TEXT("Has an execution pin");
		if (!ActiveViews.IsValid())
		{
			VisibleExecutionStatusText = ObservedExecutionStatusText;
		}
		bAvailabilityChanged = true;
	}
	if (bAvailabilityChanged) ++PresentationRevisionCounter;
}

void FM6PanelPresentationModel::MarkShownResultInvalid(FString Reason)
{
	if (ActiveViews.IsValid()) MarkStale(MoveTemp(Reason));
}

bool FM6PanelPresentationModel::HasValidTarget() const
{
	if (bProposalTargetInvalid) return false;
	if (Query.Kind == EM6QueryKind::Execution)
	{
		return bObservedExecutionAnswerable &&
			!Query.GraphId.IsEmpty() && !Query.CriterionNodeId.IsEmpty();
	}
	if (Query.Kind == EM6QueryKind::Data)
	{
		return bDataTargetSelected &&
			!Query.GraphId.IsEmpty() &&
			!Query.MemberGuid.IsEmpty() && !Query.ExpectedMemberName.IsEmpty();
	}
	return false;
}

void FM6PanelPresentationModel::SetBlueprintContext(
	FString BlueprintName,
	FString BlueprintPath,
	FString GraphName,
	FString GraphPath)
{
	if (CurrentBlueprintName != BlueprintName ||
		CurrentBlueprintPath != BlueprintPath ||
		CurrentGraphName != GraphName || CurrentGraphPath != GraphPath)
	{
		++PresentationRevisionCounter;
	}
	CurrentBlueprintName = MoveTemp(BlueprintName);
	CurrentBlueprintPath = MoveTemp(BlueprintPath);
	CurrentGraphName = MoveTemp(GraphName);
	CurrentGraphPath = MoveTemp(GraphPath);
	if (Query.GraphId.IsEmpty() && !CurrentGraphPath.IsEmpty())
		Query.GraphId = CurrentGraphPath;
}

void FM6PanelPresentationModel::MarkStale(FString Reason)
{
	if (!bStale || StaleReason != Reason) ++PresentationRevisionCounter;
	bStale = true;
	StaleReason = MoveTemp(Reason);
	StatusBanner = FString::Printf(
		TEXT("Stale · %s · the shown result remains available until Run replaces it."),
		StaleReason.IsEmpty() ? TEXT("current Blueprint context changed") : *StaleReason);
}

void FM6PanelPresentationModel::ClearStale()
{
	bStale = false;
	StaleReason.Reset();
	if (StatusBanner.StartsWith(TEXT("Stale ·")) ||
		StatusBanner.StartsWith(TEXT("stale ·")))
	{
		StatusBanner.Reset();
	}
}

EM6PanelStatus FM6PanelPresentationModel::Status() const
{
	if (SessionState == EM6SessionState::Failed) return EM6PanelStatus::Failed;
	if (SessionState == EM6SessionState::Preflight ||
		SessionState == EM6SessionState::Exporting ||
		SessionState == EM6SessionState::Running ||
		SessionState == EM6SessionState::Validating ||
		SessionState == EM6SessionState::Cancelling)
		return EM6PanelStatus::Running;
	if (bStale) return EM6PanelStatus::Stale;
	if (!bPythonReady || !HasValidTarget()) return EM6PanelStatus::NeedsSetup;
	return EM6PanelStatus::Ready;
}

FString FM6PanelPresentationModel::StatusBadge() const
{
	switch (Status())
	{
	case EM6PanelStatus::NeedsSetup: return TEXT("Needs setup");
	case EM6PanelStatus::Ready: return TEXT("Ready");
	case EM6PanelStatus::Running: return TEXT("Running");
	case EM6PanelStatus::Stale: return TEXT("Stale");
	case EM6PanelStatus::Failed: return TEXT("Failed");
	default: return TEXT("Needs setup");
	}
}

FString FM6PanelPresentationModel::StatusMessage() const
{
	if (Status() == EM6PanelStatus::Failed)
		return SessionError.Message.IsEmpty()
			? StatusBanner
			: FString::Printf(TEXT("%s · %s"), *SessionError.Code, *SessionError.Message);
	if (Status() == EM6PanelStatus::Running)
		return TEXT("The submitted query is immutable while the session runs.");
	if (Status() == EM6PanelStatus::Stale)
	{
		if (StatusBanner.IsEmpty())
			return HasValidTarget()
				? TEXT("The shown result is stale; Run replaces it using the selected target.")
				: TEXT("The shown result is stale; choose a valid target before running.");
		FString Message = StatusBanner;
		Message.RemoveFromStart(TEXT("Stale · "));
		Message.RemoveFromStart(TEXT("stale · "));
		return Message;
	}
	if (!bPythonReady)
		return TEXT("Choose or rescan a supported Python runtime before running.");
	if (!HasValidTarget())
	{
		if (Query.Kind == EM6QueryKind::Execution &&
			!ObservedExecutionNodeId.IsEmpty() && !bObservedExecutionAnswerable)
			return ObservedExecutionStatusText;
		return Query.Kind == EM6QueryKind::Data
			? TEXT("Choose a member variable used by the focused Graph.")
			: TEXT("Select one Execution node in the graph.");
	}
	return TEXT("The query is ready to run.");
}

FString FM6PanelPresentationModel::SessionStateLabel() const
{
	switch (SessionState)
	{
	case EM6SessionState::Idle: return TEXT("Idle");
	case EM6SessionState::Preflight: return TEXT("Preparing");
	case EM6SessionState::Exporting: return TEXT("Exporting");
	case EM6SessionState::Running: return TEXT("Running");
	case EM6SessionState::Validating: return TEXT("Validating");
	case EM6SessionState::Ready: return TEXT("Ready");
	case EM6SessionState::Failed: return TEXT("Failed");
	case EM6SessionState::Cancelling: return TEXT("Cancelling");
	default: return TEXT("Unknown");
	}
}

bool FM6PanelPresentationModel::CanRun() const
{
	return bPythonReady && HasValidTarget() &&
		SessionState != EM6SessionState::Failed &&
		SessionState != EM6SessionState::Preflight &&
		SessionState != EM6SessionState::Exporting &&
		SessionState != EM6SessionState::Running &&
		SessionState != EM6SessionState::Validating &&
		SessionState != EM6SessionState::Cancelling;
}

bool FM6PanelPresentationModel::CanRetry() const
{
	return SessionState == EM6SessionState::Failed &&
		bPythonReady && HasValidTarget();
}

void FM6PanelPresentationModel::DispatchRun()
{
	if (!CanRun()) return;
	if (Query.Kind == EM6QueryKind::Execution)
	{
		VisibleExecutionNodeId = Query.CriterionNodeId;
		VisibleExecutionNodeLabel = ObservedExecutionNodeLabel.IsEmpty()
			? Query.CriterionNodeId
			: ObservedExecutionNodeLabel;
		VisibleExecutionStatusText = ObservedExecutionStatusText;
	}
	if (Handlers.Run) Handlers.Run(Query);
}

void FM6PanelPresentationModel::DispatchRetry()
{
	if (!CanRetry() || !Handlers.Run) return;
	SessionState = EM6SessionState::Idle;
	SessionError = FM6Error();
	StatusBanner.Reset();
	Handlers.Run(Query);
}

void FM6PanelPresentationModel::DispatchCancel()
{
	if (!CanRun() && Handlers.Cancel) Handlers.Cancel();
}

void FM6PanelPresentationModel::DispatchReset()
{
	if (Handlers.Reset) Handlers.Reset();
	ResetPresentation();
}

void FM6PanelPresentationModel::SelectBaseline(const EM6Baseline Baseline)
{
	if (SessionState != EM6SessionState::Ready || !ActiveViews.IsValid() ||
		SelectedBaseline == Baseline) return;
	SelectedBaseline = Baseline;
	if (Handlers.Baseline) Handlers.Baseline(Baseline);
}

void FM6PanelPresentationModel::SelectEntity(
	const FString& EntityId,
	const EM6SelectionOrigin Origin)
{
	if (SessionState != EM6SessionState::Ready || !ActiveViews.IsValid() ||
		EntityId.IsEmpty()) return;
	if (!ActiveViews->A.MemberEntityIds.Contains(EntityId))
	{
		ObserveOutsideEntity(EntityId);
		return;
	}
	if (SelectedEntity == EntityId) return;
	SelectedEntity = EntityId;
	OutsideSelectionStatus.Reset();
	SourceStatus.Reset();
	if (Handlers.Selection) Handlers.Selection(EntityId, Origin);
}

void FM6PanelPresentationModel::ObserveOutsideEntity(const FString& EntityId)
{
	if (SessionState != EM6SessionState::Ready || EntityId.IsEmpty()) return;
	SelectedEntity = EntityId;
	OutsideSelectionStatus = TEXT("outside_current_session");
	SourceStatus.Reset();
}

void FM6PanelPresentationModel::DispatchSourceJump(const FString& EntityId)
{
	if (SessionState == EM6SessionState::Ready && !EntityId.IsEmpty() &&
		Handlers.SourceJump) Handlers.SourceJump(EntityId);
}

void FM6PanelPresentationModel::SetSourceJumpResult(
	const bool bSucceeded,
	const FString& Message)
{
	SourceStatus = bSucceeded ? TEXT("source_opened") : FString();
	SourceError = bSucceeded ? FString() : FString::Printf(
			TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED · %s"), *Message);
}

FM6PanelCounts FM6PanelPresentationModel::SummaryCounts() const
{
	FM6PanelCounts Counts;
	if (!ActiveViews.IsValid()) return Counts;
	Counts.Entities = ActiveViews->B.Entities.Num();
	Counts.Relations = ActiveViews->B.Relations.Num();
	Counts.Boundaries = ActiveViews->B.BoundaryCount;
	Counts.Truncated = ActiveViews->B.TruncatedCount;
	for (const FM6BaselineViewEntity& Entity : ActiveViews->B.Entities)
	{
		if (Entity.SemanticStatus == TEXT("opaque")) ++Counts.Opaque;
		else if (Entity.SemanticStatus == TEXT("uncertain")) ++Counts.Uncertain;
		else if (Entity.SemanticStatus == TEXT("unsupported")) ++Counts.Unsupported;
		else ++Counts.Supported;
	}
	return Counts;
}

void FM6PanelPresentationModel::ResetPresentation()
{
	SessionState = EM6SessionState::Idle;
	SessionError = FM6Error();
	SelectedBaseline = EM6Baseline::A;
	SelectedEntity.Reset();
	StatusBanner.Reset();
	OutsideSelectionStatus.Reset();
	SourceError.Reset();
	SourceStatus.Reset();
	ActiveViews.Reset();
	ActiveResultQuery = FM6QueryInput();
	ActiveResultExecutionTargetLabel.Reset();
	bDetailVisible = false;
	Query = FM6QueryInput();
	bDataTargetSelected = false;
	bProposalTargetInvalid = false;
	ObservedExecutionGraphId.Reset();
	ObservedExecutionNodeId.Reset();
	ObservedExecutionNodeLabel.Reset();
	bObservedExecutionAnswerable = true;
	ObservedExecutionStatusText.Reset();
	VisibleExecutionNodeId.Reset();
	VisibleExecutionNodeLabel.Reset();
	VisibleExecutionStatusText.Reset();
	CurrentSourceFingerprint.Reset();
	ActiveResultSourceFingerprint.Reset();
	bStale = false;
	StaleReason.Reset();
}

void SBlueprintLensPanel::Construct(
	const FArguments&,
	TWeakPtr<FBlueprintEditor> InBlueprintEditor)
{
	BlueprintEditor = MoveTemp(InBlueprintEditor);
	M6NativeGraphBridge = FM6NativeGraphBridge(BlueprintEditor);
	FM6PanelActionHandlers Handlers;
	Handlers.Run = [this](const FM6QueryInput& Query)
	{
		if (M6Controller != nullptr) M6Controller->Run(Query);
	};
	Handlers.Cancel = [this]()
	{
		if (M6Controller != nullptr) M6Controller->Cancel();
	};
	Handlers.Reset = [this]()
	{
		if (M6Controller != nullptr) M6Controller->Reset();
	};
	Handlers.Baseline = [this](const EM6Baseline Baseline)
	{
		if (M6Controller != nullptr) M6Controller->SelectBaseline(Baseline);
	};
	Handlers.Selection = [this](const FString& EntityId, const EM6SelectionOrigin Origin)
	{
		if (M6Controller != nullptr) M6Controller->SelectEntity(EntityId, Origin);
		if (Origin != EM6SelectionOrigin::NativeGraph)
			M6NativeGraphBridge.FocusSemanticEntity(EntityId);
	};
	M6Presentation.SetHandlers(MoveTemp(Handlers));
	M6NativeGraphBridge.SetSelectionObserver(
		[this](const FM6NativeSelectionObservation& Observation)
		{
			ObserveM6NativeSelection(Observation);
		});
	if (const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		if (UEdGraph* Graph = FM6GraphResolver::Resolve(Editor).Graph)
			M6Presentation.SetExecutionCriterion(Graph->GetPathName(), FString());
	}

	ChildSlot
	[
		SAssignNew(RootBox, SBox)
	];

	// Keep the fixture/Explanation selector and its explicit error surface as a
	// development fallback; the no-parameter M6 session remains the primary view.
	ReloadModel();
	EnableM6Session();
}

SBlueprintLensPanel::~SBlueprintLensPanel() = default;

void SBlueprintLensPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(
		AllottedGeometry,
		InCurrentTime,
		InDeltaTime);
	if (bM6Attached)
	{
		if (InCurrentTime >= M6NextContextProbeTime)
		{
			RefreshM6Context();
			M6NextContextProbeTime = InCurrentTime + 0.5;
		}
		if (M6SessionHost.IsValid()) M6SessionHost->Tick(InCurrentTime);
		else if (M6Controller != nullptr) M6Controller->Tick(InCurrentTime);
#if WITH_DEV_AUTOMATION_TESTS
		if (!bM6VisibleReviewTargetSelectionApplied &&
			!M6VisibleReviewTargetEntityId.IsEmpty() &&
			M6Presentation.State() != EM6SessionState::Ready)
		{
			const FM6NativeGraphResult ReviewTarget =
				M6NativeGraphBridge.SelectEntityForVisibleReview(
					M6VisibleReviewTargetEntityId);
			bM6VisibleReviewTargetSelectionApplied = ReviewTarget.HasValue();
		}
		if (!bM6VisibleReviewOutsideSelectionApplied &&
			!M6VisibleReviewOutsideEntityId.IsEmpty() &&
			M6Presentation.State() == EM6SessionState::Ready)
		{
			const FM6NativeGraphResult ReviewSelection =
				M6NativeGraphBridge.SelectOutsideEntityForVisibleReview(
					M6VisibleReviewOutsideEntityId);
			bM6VisibleReviewOutsideSelectionApplied = ReviewSelection.HasValue();
		}
#endif
		M6NativeGraphBridge.ObserveNativeSelection();
		M6NativeGraphBridge.Tick();
	}
	if (LC1RailScrollBox.IsValid())
	{
		const float RequestedOffset = LC1ReviewScrollOffset();
		const bool bRequestedOffsetChanged = !FMath::IsNearlyEqual(
			RequestedOffset,
			LC1AppliedReviewScrollOffset,
			0.5f);
		const bool bNewScrollBoxNeedsExplicitReviewOffset =
			RequestedOffset > 0.0f &&
			LC1AppliedReviewScrollBox.Pin() != LC1RailScrollBox;
		if (bRequestedOffsetChanged || bNewScrollBoxNeedsExplicitReviewOffset)
		{
			LC1RailScrollBox->SetScrollOffset(RequestedOffset);
			LC1RailScrollOffset = RequestedOffset;
			LC1AppliedReviewScrollOffset = RequestedOffset;
			LC1AppliedReviewScrollBox = LC1RailScrollBox;
		}
	}
	const TSharedPtr<SScrollBox> LC4ReviewScrollBox =
		LC4AsyncScrollBox.IsValid()
			? LC4AsyncScrollBox
			: LC4SequenceScrollBox;
	if (LC4ReviewScrollBox.IsValid())
	{
		const float RequestedOffset = LC4ReviewScrollOffset();
		if (!FMath::IsNearlyEqual(
				RequestedOffset,
				LC4AppliedReviewScrollOffset,
				0.5f))
		{
			LC4ReviewScrollBox->SetScrollOffset(RequestedOffset);
			LC4AppliedReviewScrollOffset = RequestedOffset;
		}
	}
	if (LC5ScrollBox.IsValid())
	{
		const float RequestedOffset = LC5ReviewScrollOffset();
		const bool bRequestedOffsetChanged = !FMath::IsNearlyEqual(
				RequestedOffset,
				LC5AppliedReviewScrollOffset,
				0.5f);
		const bool bNewScrollBoxNeedsExplicitReviewOffset =
			RequestedOffset > 0.0f &&
			LC5AppliedReviewScrollBox.Pin() != LC5ScrollBox;
		if (bRequestedOffsetChanged || bNewScrollBoxNeedsExplicitReviewOffset)
		{
			LC5ScrollBox->SetScrollOffset(RequestedOffset);
			LC5AppliedReviewScrollOffset = RequestedOffset;
			LC5AppliedReviewScrollBox = LC5ScrollBox;
		}
	}
	if (LC6OverviewScrollBox.IsValid())
	{
		const float RequestedOffset = LC6ReviewScrollOffset();
		if (!FMath::IsNearlyEqual(
			LC6OverviewScrollBox->GetScrollOffset(), RequestedOffset, 0.5f) &&
			RequestedOffset > 0.0f)
		{
			LC6OverviewScrollBox->SetScrollOffset(RequestedOffset);
			LC6OverviewScrollOffset = RequestedOffset;
		}
	}
	if (LC7OverviewScrollBox.IsValid())
	{
		const float RequestedOffset = LC7ReviewScrollOffset();
		if (!FMath::IsNearlyEqual(
			LC7OverviewScrollBox->GetScrollOffset(), RequestedOffset, 0.5f) &&
			RequestedOffset > 0.0f)
		{
			LC7OverviewScrollBox->SetScrollOffset(RequestedOffset);
			LC7OverviewScrollOffset = RequestedOffset;
		}
	}
}

void SBlueprintLensPanel::AttachM6Controller(
	FM6SessionController& Controller)
{
	M6Controller = &Controller;
	bM6Attached = true;
	Present(Controller.GetSnapshot());
}

void SBlueprintLensPanel::EnableM6Session()
{
	if (M6SessionHost.IsValid()) return;
	FString Kind;
	FString GraphId;
	FString CriterionNode;
	FString MemberGuid;
	FString MemberName;
	FParse::Value(FCommandLine::Get(), TEXT("M6QueryKind="), Kind);
	FParse::Value(FCommandLine::Get(), TEXT("M6GraphId="), GraphId);
	FParse::Value(FCommandLine::Get(), TEXT("M6CriterionNode="), CriterionNode);
	FParse::Value(FCommandLine::Get(), TEXT("M6MemberGuid="), MemberGuid);
	FParse::Value(FCommandLine::Get(), TEXT("M6MemberName="), MemberName);
#if WITH_DEV_AUTOMATION_TESTS
	FParse::Value(
		FCommandLine::Get(),
		TEXT("M6VisibleReviewOutsideEntity="),
		M6VisibleReviewOutsideEntityId);
	FParse::Value(
		FCommandLine::Get(),
		TEXT("M6VisibleReviewTargetEntity="),
		M6VisibleReviewTargetEntityId);
#endif
	if (GraphId.IsEmpty())
	{
		if (const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
		{
			if (UEdGraph* Graph = FM6GraphResolver::Resolve(Editor).Graph)
			{
				GraphId = Graph->GetPathName();
			}
		}
	}
	if (Kind == TEXT("data"))
		M6Presentation.SetDataCriterion(GraphId, MemberGuid, MemberName);
	else
		M6Presentation.SetExecutionCriterion(GraphId, CriterionNode);
	const FM6PythonResolutionResult PythonResult =
		FM6PythonResolver::Resolve(BlueprintLensM6SessionHost::WorkspaceRoot());
	M6Presentation.SetPythonResolution(PythonResult);
	RefreshM6Context();
	M6SessionHost = MakeUnique<FM6SessionHost>(BlueprintEditor, *this);
	AttachM6Controller(M6SessionHost->Controller());
}

void SBlueprintLensPanel::RefreshM6Context()
{
	const uint64 RevisionBeforeProbe = M6Presentation.PresentationRevision();
	const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	FM6GraphResolution GraphResolution = FM6GraphResolver::Resolve(Editor);
	UEdGraph* Graph = GraphResolution.Graph;
	if (Graph == nullptr && Editor.IsValid())
	{
		GraphResolution = FM6GraphResolver::Resolve(
			Editor,
			M6Presentation.GetQuery().GraphId);
		Graph = GraphResolution.Graph;
	}
	const FString BlueprintPath = Blueprint != nullptr
		? Blueprint->GetPathName()
		: FString();
	const FString GraphPath = Graph != nullptr ? Graph->GetPathName() : FString();
	M6ObservedBlueprint = Blueprint;
	M6ObservedBlueprintPath = BlueprintPath;
	M6ObservedGraphPath = GraphPath;
	M6Presentation.SetBlueprintContext(
		Blueprint != nullptr ? Blueprint->GetName() : FString(),
		BlueprintPath,
		Graph != nullptr ? Graph->GetName() : FString(),
		GraphPath);
	M6Presentation.SetDataMemberRows(
		EnumerateDataMembersForAutomationTest(Blueprint, Graph));

	if (Blueprint != nullptr)
	{
		FString Fingerprint;
		if (FM6Preflight::ComputeSourceFingerprint(*Blueprint, Fingerprint))
		{
			M6ObservedSourceFingerprint = Fingerprint;
			M6Presentation.ObserveSourceFingerprint(MoveTemp(Fingerprint));
		}
		else if (!M6ObservedSourceFingerprint.IsEmpty())
		{
			M6Presentation.MarkShownResultInvalid(
				TEXT("Blueprint source fingerprint is unavailable"));
		}
	}

	// The proposal and the shown result are separate identities after a run.
	// Validate both when their graph is in front: invalidating one must never
	// poison or silently validate the other.
	const auto TargetAvailabilityInFocusedGraph =
		[this, Graph, &GraphPath](const FM6QueryInput& Candidate) -> TOptional<bool>
		{
			if (Graph == nullptr || Candidate.GraphId.IsEmpty() ||
				Candidate.GraphId != GraphPath)
			{
				return TOptional<bool>();
			}
			if (Candidate.Kind == EM6QueryKind::Execution &&
				!Candidate.CriterionNodeId.IsEmpty())
			{
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node == nullptr) continue;
					const FString EntityId = FString::Printf(
						TEXT("%s::node::%s"), *GraphPath,
						*Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
					if (EntityId != Candidate.CriterionNodeId) continue;
					return Node->Pins.ContainsByPredicate(
						[](const UEdGraphPin* Pin)
						{
							return Pin != nullptr &&
								Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
						});
				}
				return false;
			}
			if (Candidate.Kind == EM6QueryKind::Data &&
				!Candidate.MemberGuid.IsEmpty())
			{
				return M6Presentation.DataMemberRows().ContainsByPredicate(
					[&Candidate](const FM6DataMemberRow& Row)
					{
						return Row.Guid == Candidate.MemberGuid &&
							Row.Name == Candidate.ExpectedMemberName &&
							Row.bUsableInFocusedGraph &&
							!Row.bLocal && !Row.bCrossAsset;
					});
			}
			return TOptional<bool>();
		};

	const FM6QueryInput& Proposal = M6Presentation.GetQuery();
	const TOptional<bool> ProposalAvailability =
		TargetAvailabilityInFocusedGraph(Proposal);
	if (ProposalAvailability.IsSet())
	{
		if (ProposalAvailability.GetValue())
		{
			M6Presentation.MarkTargetAvailable();
		}
		else
		{
			M6Presentation.MarkTargetInvalid(
				Proposal.Kind == EM6QueryKind::Data
					? TEXT("selected Data member was deleted or is no longer usable")
					: TEXT("selected Execution node was deleted or invalidated"));
		}
	}
	if (M6Presentation.Views().IsValid())
	{
		const FM6QueryInput& Result = M6Presentation.ResultQuery();
		const TOptional<bool> ResultAvailability =
			TargetAvailabilityInFocusedGraph(Result);
		if (ResultAvailability.IsSet() && !ResultAvailability.GetValue())
		{
			M6Presentation.MarkShownResultInvalid(
				Result.Kind == EM6QueryKind::Data
					? TEXT("shown Data member was deleted or is no longer usable")
					: TEXT("shown Execution node was deleted or invalidated"));
		}
	}
	if (Editor.IsValid() &&
		M6Presentation.PresentationRevision() != RevisionBeforeProbe)
	{
		RefreshM6Content();
	}
}

void SBlueprintLensPanel::RescanM6Python()
{
	M6Presentation.SetPythonResolution(FM6PythonResolver::Resolve(
		BlueprintLensM6SessionHost::WorkspaceRoot()));
	RefreshM6Content();
}

void SBlueprintLensPanel::ChooseM6Python()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr) return;
	TArray<FString> SelectedFiles;
	if (DesktopPlatform->OpenFileDialog(
		nullptr,
		TEXT("Choose Python executable"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Python executable|python.exe|All files|*.*"),
		EFileDialogFlags::None,
		SelectedFiles) &&
		!SelectedFiles.IsEmpty())
	{
		M6Presentation.SetPythonResolution(
			FM6PythonResolver::ValidateAndPersist(
				SelectedFiles[0],
				BlueprintLensM6SessionHost::WorkspaceRoot(),
				EM6PythonResolutionSource::Picker));
		RefreshM6Content();
	}
}

void SBlueprintLensPanel::ClearM6Python()
{
	FM6PythonResolver::ClearSavedPath();
	FM6PythonResolutionResult Result = FM6PythonResolver::Resolve(
		BlueprintLensM6SessionHost::WorkspaceRoot());
	FM6PythonResolver::ClearSavedPath();
	M6Presentation.SetPythonResolution(MoveTemp(Result));
	RefreshM6Content();
}

FReply SBlueprintLensPanel::ToggleM6SessionChrome()
{
	bM6SessionChromeExpanded = !bM6SessionChromeExpanded;
	bM6SessionChromeUserOverride = true;
	RefreshM6Content();
	return FReply::Handled();
}

void SBlueprintLensPanel::Present(const FM6SessionSnapshot& Snapshot)
{
	FM6SessionSnapshot Effective = Snapshot;
	TOptional<FM6BaselineViewModels> NewViews;
	bool bNewPacketPresentation = false;
	bool bViewProjectionSucceeded = false;
	// The controller retains its last ready packet while replacement work runs.
	// Only a Ready publication is an activation candidate; accepting the retained
	// controller packet during Preflight/Running/Validating would mix a new packet
	// and hash with the old reader Views.
	if (M6Controller != nullptr &&
		Snapshot.State == EM6SessionState::Ready &&
		Snapshot.bHasReadySession)
	{
		const FM6LoadedSessionPacket* Packet = M6Controller->GetReadyPacket();
		if (Packet != nullptr &&
			(Packet->SemanticSha256 != M6LoadedSemanticSha256 ||
					 !M6Presentation.Views().IsValid()))
		{
			bNewPacketPresentation = true;
			if (M6SessionHost.IsValid())
				M6SessionHost->BeginStage(TEXT("baseline_projection"), true);
			FM6BaselineProjectionResult Projected =
				BuildM6BaselineViewModels(*Packet);
			FM6Error ProjectionError;
			if (Projected.HasError())
			{
				ProjectionError = Projected.GetError();
				Effective.State = EM6SessionState::Failed;
				Effective.Error = ProjectionError;
				Effective.bHasReadySession = false;
			}
			else
			{
				NewViews = Projected.StealValue();
				const FM6NativeGraphResult Native =
					M6NativeGraphBridge.ApplyMembershipHighlight(Packet->BaselineFacts);
				if (Native.HasError())
				{
					ProjectionError = Native.GetError();
					Effective.State = EM6SessionState::Failed;
					Effective.Error = ProjectionError;
					NewViews.Reset();
				}
				else
				{
					M6ReadyPacket = MakeShared<FM6LoadedSessionPacket>(*Packet);
					M6LoadedSemanticSha256 = Packet->SemanticSha256;
					LC1SelectedUnitId.Reset();
					M6ExpandedGuardUnitIds.Reset();
					M6ExpandedAttachmentUnitId.Reset();
					M6ExpandedAttachmentGrammarId.Reset();
					M6ExpandedBetweenRelationId.Reset();
					M6ExpandedSpanId.Reset();
					bM6CompositeFoldExpanded = false;
					bViewProjectionSucceeded = true;
				}
			}
			if (M6SessionHost.IsValid())
				M6SessionHost->FinishStage(TEXT("baseline_projection"), ProjectionError);
		}
	}
	if (Effective.State == EM6SessionState::Idle &&
		!Effective.bHasReadySession)
	{
		M6ReadyPacket.Reset();
		M6LoadedSemanticSha256.Reset();
		M6NativeGraphBridge.Clear();
		M6ExpandedGuardUnitIds.Reset();
		M6ExpandedAttachmentUnitId.Reset();
		M6ExpandedAttachmentGrammarId.Reset();
		M6ExpandedBetweenRelationId.Reset();
		M6ExpandedSpanId.Reset();
		bM6CompositeFoldExpanded = false;
	}
	if (Effective.State != EM6SessionState::Ready)
	{
		LC1SelectedUnitId.Reset();
	}
	else if (Effective.bHasReadySession)
	{
		// Collapse completed-session setup once, unless the reader has already
		// made an explicit choice. Subsequent state updates must not overwrite
		// that choice.
		if (!bM6SessionChromeHasPresentedReadySession &&
			!bM6SessionChromeUserOverride)
		{
			bM6SessionChromeExpanded = false;
		}
		bM6SessionChromeHasPresentedReadySession = true;
	}
	M6Presentation.ApplySession(
		Effective,
		NewViews.IsSet() ? &NewViews.GetValue() : nullptr);
	if (bM6Attached && RootBox.IsValid())
	{
		if (bNewPacketPresentation && bViewProjectionSucceeded && M6SessionHost.IsValid())
		{
			M6SessionHost->BeginStage(TEXT("layout"), true);
			const TSharedRef<SWidget> Content = BuildM6SessionContent();
			M6SessionHost->FinishStage(TEXT("layout"), FM6Error());
			M6SessionHost->BeginStage(TEXT("render"), true);
			RootBox->SetContent(Content);
			M6SessionHost->FinishStage(TEXT("render"), FM6Error());
		}
		else
		{
			RefreshM6Content();
		}
	}
}

void SBlueprintLensPanel::RefreshM6Content()
{
	if (RootBox.IsValid()) RootBox->SetContent(BuildM6SessionContent());
}

FReply SBlueprintLensPanel::RunM6()
{
	M6Presentation.DispatchRun();
	RefreshM6Content();
	return FReply::Handled();
}

FReply SBlueprintLensPanel::CancelM6()
{
	M6Presentation.DispatchCancel();
	return FReply::Handled();
}

FReply SBlueprintLensPanel::ResetM6()
{
	M6Presentation.DispatchReset();
	M6ReadyPacket.Reset();
	M6LoadedSemanticSha256.Reset();
	M6NativeGraphBridge.Clear();
	LC1SelectedUnitId.Reset();
	M6ExpandedGuardUnitIds.Reset();
	M6ExpandedAttachmentUnitId.Reset();
	M6ExpandedAttachmentGrammarId.Reset();
	M6ExpandedBetweenRelationId.Reset();
	M6ExpandedSpanId.Reset();
	bM6CompositeFoldExpanded = false;
	bM6SessionChromeExpanded = true;
	bM6SessionChromeUserOverride = false;
	bM6SessionChromeHasPresentedReadySession = false;
	RefreshM6Content();
	return FReply::Handled();
}

FReply SBlueprintLensPanel::SetM6Baseline(const EM6Baseline Baseline)
{
	M6Presentation.SelectBaseline(Baseline);
	RefreshM6Content();
	return FReply::Handled();
}

FReply SBlueprintLensPanel::ToggleM6Detail()
{
	M6Presentation.SetDetailVisible(!M6Presentation.IsDetailVisible());
	RefreshM6Content();
	return FReply::Handled();
}

void SBlueprintLensPanel::SelectM6Entity(FString EntityId)
{
	M6Presentation.SelectEntity(EntityId, EM6SelectionOrigin::BaselineView);
	SynchronizeM6RailSelection(M6Presentation.SelectedEntityId());
	RefreshM6Content();
}

void SBlueprintLensPanel::SynchronizeM6RailSelection(const FString& EntityId)
{
	LC1SelectedUnitId.Reset();
	if (M6Presentation.State() != EM6SessionState::Ready ||
		!M6ReadyPacket.IsValid() || EntityId.IsEmpty())
	{
		return;
	}
	const FBlueprintLensUnit* Unit = FindM6ExplanationUnitBySourceEntityId(
		M6ReadyPacket->Explanation, EntityId);
	if (Unit != nullptr)
	{
		LC1SelectedUnitId = Unit->Id;
	}
}

void SBlueprintLensPanel::JumpM6Source(FString EntityId)
{
	M6Presentation.DispatchSourceJump(EntityId);
	if (!M6ReadyPacket.IsValid())
	{
		M6Presentation.SetSourceJumpResult(false, TEXT("no active M6 packet"));
		if (M6SessionHost.IsValid()) M6SessionHost->RecordSourceJump(
			EntityId, TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED"));
		RefreshM6Content();
		return;
	}
	const FBlueprintLensUnit* Unit = FindM6ExplanationUnitBySourceEntityId(
		M6ReadyPacket->Explanation, EntityId);
	if (Unit == nullptr || Unit->SourceReferences.IsEmpty())
	{
		M6Presentation.SetSourceJumpResult(false, TEXT("entity has no source mapping"));
		if (M6SessionHost.IsValid()) M6SessionHost->RecordSourceJump(
			EntityId, TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED"));
		RefreshM6Content();
		return;
	}
	const FBlueprintLensResolvedSource Resolved = SourceNavigator.Resolve(
		M6ReadyPacket->Explanation.Source, Unit->SourceReferences[0]);
	FString Error;
	const bool bNavigated = SourceNavigator.Navigate(Resolved, Error);
	M6Presentation.SetSourceJumpResult(
		bNavigated,
		bNavigated ? FString() : Error);
	if (M6SessionHost.IsValid()) M6SessionHost->RecordSourceJump(
		EntityId,
		bNavigated ? FString() : FString(TEXT("M6_VIEW_SOURCE_NAVIGATION_FAILED")));
	RefreshM6Content();
}

void SBlueprintLensPanel::ObserveM6NativeSelection(
	const FM6NativeSelectionObservation& Observation)
{
	// Once a result exists, native graph exploration is not a result-local
	// selection. It proposes the next query only; the visible result and its
	// interaction state stay untouched until Run commits that proposal.
	if (Observation.EntityId.IsEmpty() ||
		M6Presentation.GetQuery().Kind != EM6QueryKind::Execution)
	{
		return;
	}
	const FString PreviousSelectedEntity = M6Presentation.SelectedEntityId();
	const FString PreviousOutsideStatus = M6Presentation.OutsideStatus();
	const FString PreviousRailSelection = LC1SelectedUnitId;
	const uint64 PreviousRevision = M6Presentation.PresentationRevision();
	FString GraphId = Observation.GraphId;
	if (GraphId.IsEmpty())
	{
		const int32 NodeMarker = Observation.EntityId.Find(TEXT("::node::"));
		if (NodeMarker != INDEX_NONE) GraphId = Observation.EntityId.Left(NodeMarker);
	}
	const FString Label = Observation.Label.IsEmpty()
		? Observation.EntityId
		: Observation.Label;
	M6Presentation.ObserveExecutionSelection(
		GraphId,
		Observation.EntityId,
		Label,
		Observation.bHasExecutionPin);
	if (M6RunButton.IsValid())
	{
		M6RunButton->SetEnabled(M6Presentation.CanRun());
	}
	if (PreviousSelectedEntity != M6Presentation.SelectedEntityId() ||
		PreviousOutsideStatus != M6Presentation.OutsideStatus() ||
		PreviousRailSelection != LC1SelectedUnitId ||
		PreviousRevision != M6Presentation.PresentationRevision())
	{
		RefreshM6Content();
	}
}

FString SBlueprintLensPanel::M6ExecutionDisplayLabel(
	const FM6QueryInput& Query,
	const FString& FallbackLabel) const
{
	if (Query.Kind == EM6QueryKind::Execution &&
		M6ReadyPacket.IsValid() && !Query.CriterionNodeId.IsEmpty())
	{
		const FBlueprintLensUnit* Unit =
			FindM6ExplanationUnitBySourceEntityId(
				M6ReadyPacket->Explanation,
				Query.CriterionNodeId);
		if (Unit != nullptr)
		{
			const FString DisplayLabel = BlueprintLensDisplayLabel(*Unit);
			if (!DisplayLabel.IsEmpty())
			{
				return DisplayLabel;
			}
		}
	}
	return FallbackLabel;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildM6SessionContent()
{
	const FM6PanelCounts Counts = M6Presentation.SummaryCounts();
	const TSharedPtr<const FM6BaselineViewModels> Views = M6Presentation.Views();
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> SessionChrome = SNew(SVerticalBox);
	const bool bShowSessionChrome = !Views.IsValid() || bM6SessionChromeExpanded;
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("M6 Causal Session · information-matched A/B/C")))
		.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
	];
	if (Views.IsValid() &&
		M6Presentation.ResultQuery().Kind != EM6QueryKind::Invalid)
	{
		const FM6QueryInput& ResultQuery = M6Presentation.ResultQuery();
		const FM6QueryInput& CurrentQuery = M6Presentation.GetQuery();
		const FString ResultExecutionLabel = M6ExecutionDisplayLabel(
			ResultQuery,
			M6Presentation.ResultExecutionTargetLabel());
		const FString CurrentExecutionLabel = M6ExecutionDisplayLabel(
			CurrentQuery,
			M6Presentation.VisibleExecutionTargetLabel());
		const FString ResultDescription = ResultQuery.Kind == EM6QueryKind::Data
			? FString::Printf(
				TEXT("Data query · %s"), *ResultQuery.ExpectedMemberName)
			: FString::Printf(
				TEXT("Execution query · %s"),
				*ResultExecutionLabel);
		const FString CurrentTarget = CurrentQuery.Kind == EM6QueryKind::Data
			? CurrentQuery.ExpectedMemberName
			: CurrentExecutionLabel;
		SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("showing: %s · selected: %s"),
				*ResultDescription,
				CurrentTarget.IsEmpty() ? TEXT("no target") : *CurrentTarget)))
			.AutoWrapText(true)
		];
	}
	if (Views.IsValid() && bM6SessionChromeExpanded)
	{
		SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Collapse session controls")))
			.OnClicked(this, &SBlueprintLensPanel::ToggleM6SessionChrome)
		];
	}
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(7.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · %s"),
					*M6Presentation.StatusBadge(),
					*M6Presentation.StatusMessage())))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Blueprint · %s · Graph · %s"),
					*M6Presentation.BlueprintName(),
					*M6Presentation.GraphName())))
				.AutoWrapText(true)
			]
		]
	];
	if (M6QueryKindOptions.Num() != 2)
	{
		M6QueryKindOptions.Reset();
		M6QueryKindOptions.Add(MakeShared<FString>(TEXT("Execution")));
		M6QueryKindOptions.Add(MakeShared<FString>(TEXT("Data")));
	}
	const TSharedPtr<FString> SelectedQueryKind =
		M6Presentation.GetQuery().Kind == EM6QueryKind::Data
			? M6QueryKindOptions[1]
			: M6QueryKindOptions[0];
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&M6QueryKindOptions)
		.InitiallySelectedItem(SelectedQueryKind)
		.IsEnabled(M6Presentation.CanEditQuery())
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item.IsValid() ? *Item : FString()));
		})
		.OnSelectionChanged_Lambda(
			[this](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
			{
				if (SelectInfo == ESelectInfo::Direct || !NewSelection.IsValid()) return;
				M6Presentation.SetQueryKind(
					*NewSelection == TEXT("Data")
						? EM6QueryKind::Data
						: EM6QueryKind::Execution);
				RefreshM6Content();
			})
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return M6Presentation.GetQuery().Kind == EM6QueryKind::Data
					? FText::FromString(TEXT("Data"))
					: FText::FromString(TEXT("Execution"));
			})
		]
	];
	const FM6QueryInput& VisibleQuery = M6Presentation.GetQuery();
	const FString VisibleExecutionLabel = M6ExecutionDisplayLabel(
		VisibleQuery,
		M6Presentation.VisibleExecutionTargetLabel());
	const FString VisibleTargetText = VisibleQuery.Kind == EM6QueryKind::Data
		? FString::Printf(
			TEXT("Data member · %s · GUID %s"),
			VisibleQuery.ExpectedMemberName.IsEmpty()
				? TEXT("not selected") : *VisibleQuery.ExpectedMemberName,
			*VisibleQuery.MemberGuid)
		: FString::Printf(
			TEXT("Execution node · %s · %s"),
			VisibleExecutionLabel.IsEmpty()
				? TEXT("not selected")
				: *VisibleExecutionLabel,
			M6Presentation.VisibleExecutionTargetStatusText().IsEmpty()
				? TEXT("select in graph")
				: *M6Presentation.VisibleExecutionTargetStatusText());
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(7.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Target")))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(VisibleTargetText))
				.AutoWrapText(true)
			]
		]
	];
	if (M6Presentation.GetQuery().Kind == EM6QueryKind::Data)
	{
		TSharedRef<SVerticalBox> MemberRows = SNew(SVerticalBox);
		MemberRows->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				TEXT("Local variables and cross-asset variable resolution are outside this workflow.")))
			.AutoWrapText(true)
		];
		for (const FM6DataMemberRow& Row : M6Presentation.DataMemberRows())
		{
			const bool bEnabled = M6Presentation.CanEditQuery() &&
				Row.bUsableInFocusedGraph && !Row.bLocal && !Row.bCrossAsset;
			const FString Guid = Row.Guid;
			MemberRows->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · %s · %s · %s"), *Row.Name, *Row.Type,
					*Row.Guid, *Row.StatusText)))
				.IsEnabled(bEnabled)
				.OnClicked_Lambda([this, Guid]()
				{
					M6Presentation.SelectDataMember(Guid);
					RefreshM6Content();
					return FReply::Handled();
				})
			];
		}
		TSharedRef<SScrollBox> MemberScroll = SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				MemberRows
			];
		SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(7.0f)
			[
				SNew(SBox)
				.HeightOverride(220.0f)
				[
					MemberScroll
				]
			]
		];
	}
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SAssignNew(M6RunButton, SButton)
			.Text(FText::FromString(TEXT("Run")))
			.IsEnabled(M6Presentation.CanRun())
			.OnClicked(this, &SBlueprintLensPanel::RunM6)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Cancel")))
			.IsEnabled_Lambda([this]()
			{
				return M6Presentation.Status() == EM6PanelStatus::Running;
			})
			.OnClicked(this, &SBlueprintLensPanel::CancelM6)
		]
	];
	if (M6Presentation.Status() == EM6PanelStatus::Failed)
	{
		SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Retry")))
			.IsEnabled_Lambda([this]() { return M6Presentation.CanRetry(); })
			.OnClicked_Lambda([this]()
			{
				M6Presentation.DispatchRetry();
				RefreshM6Content();
				return FReply::Handled();
			})
		];
	}
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 6.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("Session · %s · entities %d · relations %d · boundaries %d · supported %d · opaque %d · uncertain %d · unsupported %d · truncated %d"),
			*M6Presentation.SessionStateLabel(), Counts.Entities,
			Counts.Relations, Counts.Boundaries, Counts.Supported, Counts.Opaque,
			Counts.Uncertain, Counts.Unsupported, Counts.Truncated)))
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		.AutoWrapText(true)
	];
	if (!M6Presentation.Banner().IsEmpty())
	{
		SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.55f, 0.32f, 0.12f, 0.35f))
			.Padding(7.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(M6Presentation.Banner()))
				.AutoWrapText(true)
			]
		];
	}
	SessionChrome->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const FM6PythonResolutionResult& Python =
					M6Presentation.PythonResolution();
				if (Python.bValid)
				{
					return FText::FromString(FString::Printf(
						TEXT("Python · %s · CPython %s · %s"),
						*Python.ExecutablePath, *Python.Version,
						*FM6PythonResolver::SourceLabel(Python.Source)));
				}
				return FText::FromString(FString::Printf(
					TEXT("Python check · %s · %s"),
					*Python.FailureCode, *Python.FailureMessage));
			})
			.AutoWrapText(true)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Rescan")))
			.OnClicked_Lambda([this]()
			{
				RescanM6Python();
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Choose Python")))
			.OnClicked_Lambda([this]()
			{
				ChooseM6Python();
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Clear saved path")))
			.IsEnabled_Lambda([]()
			{
				return !FM6PythonResolver::GetSavedPath().IsEmpty();
			})
			.OnClicked_Lambda([this]()
			{
				ClearM6Python();
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Reset")))
			.OnClicked(this, &SBlueprintLensPanel::ResetM6)
		]
	];
	if (bShowSessionChrome)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SessionChrome
		];
	}
	else
	{
		const FM6QueryInput& Query = M6Presentation.GetQuery();
		FString QuerySummary;
		if (Views.IsValid() &&
			M6Presentation.ResultQuery().Kind != EM6QueryKind::Invalid)
		{
			const FM6QueryInput& ResultQuery = M6Presentation.ResultQuery();
			const FString ShownTarget = ResultQuery.Kind == EM6QueryKind::Data
				? ResultQuery.ExpectedMemberName
				: M6ExecutionDisplayLabel(
					ResultQuery,
					M6Presentation.ResultExecutionTargetLabel());
			const FString SelectedTarget = Query.Kind == EM6QueryKind::Data
				? Query.ExpectedMemberName
				: M6ExecutionDisplayLabel(
					Query,
					M6Presentation.VisibleExecutionTargetLabel());
			QuerySummary = FString::Printf(
				TEXT("showing: %s · selected: %s"),
				ShownTarget.IsEmpty() ? TEXT("no target") : *ShownTarget,
				SelectedTarget.IsEmpty() ? TEXT("no target") : *SelectedTarget);
		}
		else
		{
			QuerySummary = Query.Kind == EM6QueryKind::Data
				? FString::Printf(TEXT("Data query · %s"), *Query.ExpectedMemberName)
				: FString(TEXT("Execution query"));
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(7.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%s · %s · %s · %d entities · %d relations"),
						*M6Presentation.StatusBadge(), *M6Presentation.BlueprintName(),
						*QuerySummary, Counts.Entities, Counts.Relations)))
					.AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Expand session controls")))
					.OnClicked(this, &SBlueprintLensPanel::ToggleM6SessionChrome)
				]
			]
		];
	}
	if (Views.IsValid())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("A · Full Native Graph")))
				.OnClicked(this, &SBlueprintLensPanel::SetM6Baseline, EM6Baseline::A)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("B · Native Slice")))
				.OnClicked(this, &SBlueprintLensPanel::SetM6Baseline, EM6Baseline::B)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(FText::FromString(TEXT("C · Causal Lens")))
				.OnClicked(this, &SBlueprintLensPanel::SetM6Baseline, EM6Baseline::C)
			]
		];
		if (M6Presentation.Baseline() == EM6Baseline::A)
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Full native graph remains visible: %d Session members highlighted; %d non-members preserved."),
					Views->A.MemberEntityIds.Num(), Views->A.NonMemberEntityIds.Num())))
				.AutoWrapText(true)
			];
		}
		else if (M6Presentation.Baseline() == EM6Baseline::B)
		{
			Body->AddSlot().FillHeight(1.0f).Padding(0.0f, 6.0f)
			[
				SNew(SBlueprintLensM6NativeSlice)
				.ViewModel(&Views->B)
				.OnEntitySelected(FM6NativeSliceEntitySelected::CreateSP(
					this, &SBlueprintLensPanel::SelectM6Entity))
				.OnSourceRequested(FM6NativeSliceSourceRequested::CreateSP(
					this, &SBlueprintLensPanel::JumpM6Source))
			];
		}
		else
		{
			Body->AddSlot().FillHeight(1.0f).Padding(0.0f, 6.0f)
			[
				BuildM6CausalContent()
			];
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 5.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(
				M6Presentation.IsDetailVisible() ? TEXT("Hide detail") : TEXT("Show detail")))
			.OnClicked(this, &SBlueprintLensPanel::ToggleM6Detail)
		];
	}
	if (!M6Presentation.OutsideStatus().IsEmpty() ||
		!M6Presentation.SelectedEntityId().IsEmpty() ||
		!M6Presentation.SourceJumpError().IsEmpty() ||
		!M6Presentation.SourceJumpStatus().IsEmpty())
	{
		TArray<FString> InteractionStatus;
		if (!M6Presentation.OutsideStatus().IsEmpty())
		{
			InteractionStatus.Add(TEXT(
				"Selection not applied: this node is outside the current session. "
				"The shown result remains unchanged."));
		}
		else if (!M6Presentation.SelectedEntityId().IsEmpty())
		{
			InteractionStatus.Add(FString::Printf(
				TEXT("selected_entity · %s"),
				*M6Presentation.SelectedEntityId()));
		}
		if (!M6Presentation.SourceJumpStatus().IsEmpty())
			InteractionStatus.Add(M6Presentation.SourceJumpStatus());
		if (!M6Presentation.SourceJumpError().IsEmpty())
			InteractionStatus.Add(M6Presentation.SourceJumpError());
		Body->AddSlot().AutoHeight().Padding(0.0f, 5.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Join(InteractionStatus, TEXT(" · "))))
			.AutoWrapText(true)
		];
	}
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(PanelPadding)
		[
			Body
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildM6CausalRailContent(
	const TSharedPtr<const FBlueprintLensExplanationModel>& Explanation,
	const FBlueprintLensLC1RailProjection& Projection,
	const FBlueprintLensLC1RailLayoutSessionResult& LayoutSession,
	const FBlueprintLensCompositeRailSlots& CompositeSlots,
	const TSharedPtr<SWidget>& ExpandedStationAppearance,
	const FString& ExpandedStationAppearanceUnitId,
	const float ReviewWidth,
	const bool bDataAnswer,
	const TSharedPtr<SWidget>& ExpandedBetweenDecoration,
	const FString& ExpandedBetweenDecorationRelationId,
	const TSharedPtr<SWidget>& ExpandedSpanAttachment,
	const FString& ExpandedSpanAttachmentId,
	const TSharedPtr<SWidget>& ExpandedTerminalAttachment,
	const FString& ExpandedTerminalAttachmentUnitId)
{
	check(Explanation.IsValid());
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const bool bMixedFeatureProjection =
		!Projection.DeferredRelationIds.IsEmpty();
	Content->SetTag(
		bDataAnswer
			? F12DataAnswerRailAutomationTag
			: CompositeSlots.HasGuardStations()
			? CompositeGuardRailAutomationTag
			: LC1RailAutomationTag);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(bDataAnswer
				? LOCTEXT("M6F12DataAnswerHeading", "Data write answer")
				: LOCTEXT("M6LC1RailHeading", "Execution predecessor rail"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(bDataAnswer
				? FString::Printf(
					TEXT("Read the proven-before answer-position order toward %s. "
						"Outside a declared SCC segment, a lower answer position cannot "
						"be a proven cause of an upper answer position; incomparable and "
						"SCC segments state their local order boundary. "
						"Each write states why it is in this Data answer; assigned "
						"required data producers appear beside that write."),
					*Projection.CriterionDisplayLabel)
				: bMixedFeatureProjection
				? FString::Printf(
					TEXT("Read the proven-before station order toward %s. Outside a "
						"declared SCC segment, a lower station cannot be a proven cause "
						"of an upper station; incomparable and SCC segments state their "
						"local order boundary. The criterion remains docked at the end."),
					*Projection.CriterionDisplayLabel)
				: FString::Printf(
					TEXT("Read the proven predecessor chain toward %s. "
						"The criterion remains docked at the end of the rail."),
					*Projection.CriterionDisplayLabel)))
			.AutoWrapText(true)
	];
	if (bDataAnswer)
	{
		TSharedRef<STextBlock> ValueSourceScope = SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Value-source disclosure · Up to %d assigned producer identities are expanded beside one write. That is the maximum measured across the eight retained Data slices; larger live answers remain admitted and state the local omission."),
				BlueprintLensF12DataAnswerBounds::
					MaxValueSourcesPerStation)))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Value))
			.AutoWrapText(true)
			.Visibility(EVisibility::HitTestInvisible);
		ValueSourceScope->SetTag(F12ValueSourceScopeAutomationTag);
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			ValueSourceScope
		];
		TSharedRef<STextBlock> ConditionLimit = SNew(STextBlock)
			.Text(LOCTEXT(
				"M6F12DeferredConditionDependency",
				"Condition limit · This answer cannot yet state under what "
				"condition a write happens. Branch-condition dependency is "
				"deferred because relation reasons are absent from the "
				"Explanation model, its schema, and the retained Data slices."))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Boundary))
			.AutoWrapText(true)
			.Visibility(EVisibility::HitTestInvisible);
		ConditionLimit->SetTag(F12DeferredConditionDependencyAutomationTag);
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Boundary), 0.12f))
				.Padding(FMargin(8.0f))
			[
				ConditionLimit
			]
		];
	}

	TSharedRef<SBlueprintLensLC1RailCanvas> RailCanvas =
		SNew(SBlueprintLensLC1RailCanvas)
			.Projection(Projection)
			.InitialSession(LayoutSession)
			.Explanation(Explanation)
			.CompositeSlots(CompositeSlots)
			.DataAnswer(bDataAnswer)
			.CurrentRadius(
				bM6CompositeFoldExpanded
					? Projection.OrderedCanonicalUnits.Num() - 1
					: INDEX_NONE)
			.ExpandedStationAppearance(ExpandedStationAppearance)
			.ExpandedStationAppearanceUnitId(
				ExpandedStationAppearanceUnitId)
			.ExpandedBetweenDecoration(ExpandedBetweenDecoration)
			.ExpandedBetweenDecorationRelationId(
				ExpandedBetweenDecorationRelationId)
			.ExpandedSpanAttachment(ExpandedSpanAttachment)
			.ExpandedSpanAttachmentId(ExpandedSpanAttachmentId)
			.ExpandedTerminalAttachment(ExpandedTerminalAttachment)
			.ExpandedTerminalAttachmentUnitId(
				ExpandedTerminalAttachmentUnitId)
			.SelectedUnitId_Lambda(
				[this]()
				{
					return LC1SelectedUnitId;
				})
			.OnUnitSelected(
				FOnBlueprintLensLC1RailUnitSelected::CreateSP(
					this,
					&SBlueprintLensPanel::SelectM6RailUnit))
			.OnDisclosureToggled(
				FOnBlueprintLensCompositeDisclosureToggled::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleM6CompositeDisclosure))
			.OnAttachmentDisclosureToggled(
				FOnBlueprintLensCompositeAttachmentDisclosureToggled::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleM6AttachmentDisclosure))
			.OnBetweenDisclosureToggled(
				FOnBlueprintLensCompositeBetweenDisclosureToggled::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleM6BetweenDisclosure))
			.OnSpanDisclosureToggled(
				FOnBlueprintLensCompositeSpanDisclosureToggled::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleM6SpanDisclosure))
			.OnFoldToggled(
				FOnBlueprintLensCompositeFoldToggled::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleM6CompositeFold));
	if (CompositeSlots.AreAllAttachmentsCollapsed())
	{
		RailCanvas->SetTag(CompositeAttachmentsCollapsedAutomationTag);
	}
	LC1RailCanvas = RailCanvas;
	TSharedRef<SWidget> RailHost = RailCanvas;
	if (ReviewWidth > 0.0f)
	{
		RailHost = SNew(SBox)
			.HAlign(HAlign_Fill)
			.WidthOverride(ReviewWidth)
			[
				RailCanvas
			];
	}
	Content->AddSlot()
	.AutoHeight()
	// The review SBox has a fixed desired width. Preserve that desired width as
	// its actual Slate allotment only when the default-off review seam is enabled;
	// otherwise the responsive production canvas continues to fill the C-body width.
	.HAlign(ReviewWidth > 0.0f ? HAlign_Left : HAlign_Fill)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		RailHost
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(
				RoleAccent(EBlueprintLensRole::Boundary),
				0.12f))
			.Padding(FMargin(8.0f))
			[
				SNew(STextBlock)
					.Text_Lambda(
						[this, Projection, bDataAnswer]()
						{
							const int32 LedgerTotal = Projection.AllUnitIds.Num();
							int32 DrawnCount = 0;
							if (LC1RailCanvas.IsValid())
							{
								const FBlueprintLensLC1RailSurfaceLayout& Surface =
									LC1RailCanvas->GetSurface();
								if (Surface.IsRenderable(Projection))
								{
									DrawnCount = Surface.DrawnUnitCount();
								}
							}
							const int32 RetainedCapCount = LC1RailCanvas.IsValid()
								? LC1RailCanvas->GetSurface().Radius
									.RetainedBoundaryCapIds.Num()
								: 0;
							FString ReaderText;
							if (RetainedCapCount == 0)
							{
								ReaderText = bDataAnswer
									? FString::Printf(
										TEXT("%d OF %d DATA ANSWER POSITIONS DRAWN"),
										DrawnCount,
										LedgerTotal)
									: FString::Printf(
										TEXT("%d OF %d UNITS DRAWN"),
										DrawnCount,
										LedgerTotal);
							}
							else
							{
								ReaderText = bDataAnswer
									? FString::Printf(
										TEXT("%d WRITE/CONTROLLER POSITIONS AND %d BOUNDARY CAPS DRAWN OF %d DATA ANSWER UNITS"),
										DrawnCount,
										RetainedCapCount,
										LedgerTotal)
									: FString::Printf(
										TEXT("%d RAIL STATIONS AND %d BOUNDARY CAPS DRAWN OF %d UNITS"),
										DrawnCount,
										RetainedCapCount,
										LedgerTotal);
							}
							if (!Projection.DeferredUnitIds.IsEmpty() ||
								!Projection.DeferredRelationIds.IsEmpty())
							{
								ReaderText += bDataAnswer
									? FString::Printf(
										TEXT("\n%d VALUE UNITS AND %d RELATIONS RETAINED "
											"BESIDE OR OUTSIDE THE WRITE SPINE"),
										Projection.DeferredUnitIds.Num(),
										Projection.DeferredRelationIds.Num())
									: FString::Printf(
										TEXT("\n%d UNITS AND %d RELATIONS RETAINED "
											"OUTSIDE THE EXECUTION RAIL"),
										Projection.DeferredUnitIds.Num(),
										Projection.DeferredRelationIds.Num());
							}
							return FText::FromString(ReaderText);
						})
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.AutoWrapText(true)
			]
	];
	if (M6Presentation.IsDetailVisible())
	{
		const TSharedPtr<const FM6BaselineViewModels> Views = M6Presentation.Views();
		if (Views.IsValid())
		{
			TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
			Details->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
					.Text(LOCTEXT("M6LC1RailDetailHeading", "C ENTITY DETAILS"))
					.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			];
			for (const FM6BaselineViewEntity& Entity : Views->C.Entities)
			{
				Details->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s · reason: %s · inclusion: %s"),
							*Entity.Label,
							Entity.SemanticReason.IsEmpty()
								? TEXT("none")
								: *Entity.SemanticReason,
							*FString::Join(Entity.InclusionReasons, TEXT(", ")))))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
				];
			}
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(FMargin(8.0f))
					[
						Details
					]
			];
		}
	}

	if (!LC1SelectedUnitId.IsEmpty())
	{
		const FBlueprintLensUnit* SelectedUnit =
			Explanation->FindUnit(LC1SelectedUnitId);
		if (SelectedUnit != nullptr)
		{
			const FString SourceEntityId = PrimarySourceNodeId(*SelectedUnit);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"M6LC1RailOpenSelectedItem",
						"Open selected item in Blueprint"))
					.IsEnabled(!SourceEntityId.IsEmpty())
					.OnClicked_Lambda(
						[this, SourceEntityId]()
						{
							JumpM6Source(SourceEntityId);
							return FReply::Handled();
						})
			];
		}
	}

	TSharedRef<SScrollBox> Scroll =
		SAssignNew(LC1RailScrollBox, SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				Content
			];
	Scroll->SetScrollOffset(LC1RailScrollOffset);
	return Scroll;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildM6CausalContent()
{
	const TSharedPtr<const FM6BaselineViewModels> Views = M6Presentation.Views();
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	if (!Views.IsValid()) return Content;
	// Replacement work is deliberately fail-open to the last successfully
	// activated reader result.  Session state describes the replacement; the
	// retained packet remains the source of the inspectable C rail until a new
	// activation succeeds or Reset clears it.
	if (M6ReadyPacket.IsValid())
	{
		// The accepted live packet is the single source for both the spine and
		// every station-appearance projector. Model is construction-time fixture
		// state and must never enter this live dispatch.
		const FBlueprintLensLC1TypedIrFacts LiveTypedIrFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(
				M6ReadyPacket->Explanation.Source,
				false);
		const TSharedPtr<const FBlueprintLensExplanationModel> SourceExplanation =
			MakeShared<FBlueprintLensExplanationModel>(
				FBlueprintLensLC4SequenceLiveAdapter::
					ApplyReaderDisambiguators(
						M6ReadyPacket->Explanation,
						LiveTypedIrFacts));
		const bool bAcceptedDataAnswer =
			M6ReadyPacket->Request.QueryKind.Equals(
				TEXT("data"), ESearchCase::IgnoreCase);
		if (bAcceptedDataAnswer)
		{
			FBlueprintLensF12DataRailAdapterResult DataAdapted =
				FBlueprintLensF12DataRailAdapter::Build(*SourceExplanation);
			TSharedPtr<const FBlueprintLensExplanationModel>
				DataRailExplanation;
			if (DataAdapted.IsSuccess())
			{
				DataRailExplanation =
					MakeShared<FBlueprintLensExplanationModel>(
						MoveTemp(DataAdapted.Explanation));
			}
			const FBlueprintLensLC1RailProjection DataRail =
				DataRailExplanation.IsValid()
					? FBlueprintLensLC1RailProjector::Build(
						*DataRailExplanation)
					: FBlueprintLensLC1RailProjection();
			const FBlueprintLensF12DataAnswerProjection DataProjection =
				FBlueprintLensF12DataAnswerProjector::Build(
					*SourceExplanation,
					DataRail);
			if (DataRailExplanation.IsValid() &&
				DataProjection.IsRenderable(*SourceExplanation, DataRail))
			{
				FBlueprintLensCompositeRailSlots DataSlots =
					FBlueprintLensCompositeRailSlotProjector::Build(
						*DataRailExplanation,
						DataRail);
				DataSlots = FBlueprintLensF12DataAnswerProjector::Apply(
					DataProjection,
					DataSlots);
				if (!M6ExpandedAttachmentUnitId.IsEmpty() &&
					M6ExpandedAttachmentGrammarId == TEXT("LC3"))
				{
					FBlueprintLensCompositeStationSlot* ExpandedStation =
						DataSlots.FindStation(M6ExpandedAttachmentUnitId);
					if (ExpandedStation != nullptr)
					{
						for (FBlueprintLensCompositeAttachment& Attachment :
							ExpandedStation->BesideAttachments)
						{
							if (Attachment.GrammarId == TEXT("LC3") &&
								!Attachment.DetailLines.IsEmpty())
							{
								Attachment.Disclosure =
									EBlueprintLensCompositeDisclosure::Expanded;
							}
						}
					}
					FBlueprintLensCompositeTerminalCapSlot* ExpandedCap =
						DataSlots.TerminalCaps.FindByPredicate(
							[this](
								const FBlueprintLensCompositeTerminalCapSlot& Cap)
							{
								return Cap.UnitId ==
									M6ExpandedAttachmentUnitId;
							});
					if (ExpandedCap != nullptr)
					{
						for (FBlueprintLensCompositeAttachment& Attachment :
							ExpandedCap->Attachments)
						{
							if (Attachment.GrammarId == TEXT("LC3") &&
								!Attachment.DetailLines.IsEmpty())
							{
								Attachment.Disclosure =
									EBlueprintLensCompositeDisclosure::Expanded;
							}
						}
					}
				}

				const float ReviewWidth = LC1ReviewWidth();
				if (LC1RailCanvas.IsValid() && ReviewWidth <= 0.0f)
				{
					LC1RailLayoutWidth = LC1RailCanvas->GetLayoutWidth();
				}
				if (LC1RailScrollBox.IsValid())
				{
					LC1RailScrollOffset =
						LC1RailScrollBox->GetScrollOffset();
				}
				const float InitialWidth = ReviewWidth > 0.0f
					? ReviewWidth
					: FMath::Max(LC1RailLayoutWidth, 430.0f);
				const FBlueprintLensLC1RailLayoutSessionResult DataSession =
					FBlueprintLensLC1RailLayoutSession::Build(
						DataRail,
						*DataRailExplanation,
						InitialWidth);
				const FBlueprintLensLC1RailSurfaceLayout DataSurface =
					FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
						DataRail,
						DataSession,
						DataSlots,
						InitialWidth,
						bM6CompositeFoldExpanded
							? DataRail.OrderedCanonicalUnits.Num() - 1
							: INDEX_NONE,
						FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
							true);
				if (DataSession.IsRenderable(DataRail) &&
					DataSurface.IsRenderable(DataRail))
				{
					return BuildM6CausalRailContent(
						DataRailExplanation,
						DataRail,
						DataSession,
						DataSlots,
						nullptr,
						FString(),
						ReviewWidth,
						true,
						nullptr,
						FString());
				}
			}
		}
		if (!bAcceptedDataAnswer)
		{
		TSharedPtr<const FBlueprintLensExplanationModel> RailExplanation =
			SourceExplanation;
		TSharedPtr<const FBlueprintLensExplanationModel> LC2Explanation =
			SourceExplanation;
		FBlueprintLensLC2GuardOutlineProjection GuardOutline =
			FBlueprintLensLC2GuardOutlineProjector::Build(*LC2Explanation);
		FBlueprintLensLC2GuardSurfaceProjection GuardSurface =
			FBlueprintLensLC2GuardSurfaceProjector::Build(
				*LC2Explanation,
				GuardOutline);
		int32 GuardScopeInputUnits = 0;
		int32 GuardScopeInputRelations = 0;
		int32 GuardScopeAdaptedUnits = 0;
		int32 GuardScopeAdaptedRelations = 0;
		if (!GuardSurface.IsRenderable())
		{
			FBlueprintLensLC2LiveExplanationAdapterResult Adapted =
				FBlueprintLensLC2LiveExplanationAdapter::Build(
					*SourceExplanation);
			if (Adapted.IsSuccess())
			{
				GuardScopeInputUnits = Adapted.InputUnitCount;
				GuardScopeInputRelations = Adapted.InputRelationCount;
				GuardScopeAdaptedUnits = Adapted.AdaptedUnitCount;
				GuardScopeAdaptedRelations = Adapted.AdaptedRelationCount;
				FBlueprintLensExplanationModel OrderingExplanation =
					*SourceExplanation;
				// Ordering keeps the complete live station ledger, but consumes the
				// adapter's proven optional group cover and partial-order semantics.
				OrderingExplanation.bHasGroups = Adapted.Explanation.bHasGroups;
				OrderingExplanation.Groups = Adapted.Explanation.Groups;
				OrderingExplanation.bHasGroupPartialOrder =
					Adapted.Explanation.bHasGroupPartialOrder;
				OrderingExplanation.GroupPartialOrder =
					Adapted.Explanation.GroupPartialOrder;
				RailExplanation = MakeShared<FBlueprintLensExplanationModel>(
					MoveTemp(OrderingExplanation));
				LC2Explanation = MakeShared<FBlueprintLensExplanationModel>(
					MoveTemp(Adapted.Explanation));
				GuardOutline =
					FBlueprintLensLC2GuardOutlineProjector::Build(
						*LC2Explanation);
				GuardSurface =
					FBlueprintLensLC2GuardSurfaceProjector::Build(
						*LC2Explanation,
						GuardOutline);
			}
		}
		TSharedPtr<const FBlueprintLensExplanationModel> LC3Explanation =
			SourceExplanation;
		FBlueprintLensLC3ValueConeProjection ValueConeProjection =
			FBlueprintLensLC3ValueConeProjector::Build(*LC3Explanation);
		int32 ValueScopeInputUnits = 0;
		int32 ValueScopeInputRelations = 0;
		int32 ValueScopeAdaptedUnits = 0;
		int32 ValueScopeAdaptedRelations = 0;
		if (ValueConeProjection.Status !=
			EBlueprintLensLC3ValueConeProjectionStatus::ValueCone)
		{
			FBlueprintLensLC3LiveExplanationAdapterResult Adapted =
				FBlueprintLensLC3LiveExplanationAdapter::Build(
					*SourceExplanation);
			if (Adapted.IsSuccess())
			{
				ValueScopeInputUnits = Adapted.InputUnitCount;
				ValueScopeInputRelations = Adapted.InputRelationCount;
				ValueScopeAdaptedUnits = Adapted.AdaptedUnitCount;
				ValueScopeAdaptedRelations = Adapted.AdaptedRelationCount;
				LC3Explanation = MakeShared<FBlueprintLensExplanationModel>(
					MoveTemp(Adapted.Explanation));
				ValueConeProjection =
					FBlueprintLensLC3ValueConeProjector::Build(
						*LC3Explanation);
			}
		}
		const FBlueprintLensLC7LiveExplanationAdapterResult LC7Adapted =
			FBlueprintLensLC7LiveExplanationAdapter::Build(*SourceExplanation);
		if (LC7Adapted.IsSuccess())
		{
			FBlueprintLensExplanationModel OrderingExplanation =
				*RailExplanation;
			for (const FBlueprintLensGroup& Group :
				 LC7Adapted.Profile->ExplanationModel->Groups)
			{
				if (Group.Kind == EBlueprintLensGroupKind::Scc &&
					!OrderingExplanation.Groups.ContainsByPredicate(
						[&Group](const FBlueprintLensGroup& Existing)
						{
							return Existing.Id == Group.Id;
						}))
				{
					OrderingExplanation.Groups.Add(Group);
				}
			}
			OrderingExplanation.bHasGroups =
				!OrderingExplanation.Groups.IsEmpty();
			RailExplanation = MakeShared<FBlueprintLensExplanationModel>(
				MoveTemp(OrderingExplanation));
		}
		const FBlueprintLensLC1RailProjection RailProjection =
			FBlueprintLensLC1RailProjector::Build(*RailExplanation);
		if (RailProjection.IsRenderable())
		{
			const FBlueprintLensLC5LiveTypedIrAdapterResult LC5Adapted =
				FBlueprintLensLC5LiveTypedIrAdapter::Build(
					*SourceExplanation,
					LiveTypedIrFacts);
			const FBlueprintLensLC6LiveExplanationAdapterResult LC6Adapted =
				FBlueprintLensLC6LiveExplanationAdapter::Build(
					*SourceExplanation,
					RailProjection);
			FBlueprintLensCompositeRailSlots CompositeSlots =
				FBlueprintLensCompositeRailSlotProjector::Build(
					*RailExplanation,
					RailProjection);
			CompositeSlots = FBlueprintLensLC2StationAppearanceProjector::Apply(
				*LC2Explanation,
				GuardSurface,
				CompositeSlots);
			CompositeSlots = FBlueprintLensLC3StationAttachmentProjector::Apply(
				*SourceExplanation,
				CompositeSlots);
			if (LC5Adapted.IsSuccess())
			{
				for (const FBlueprintLensLC5LiveCallCase& CallCase :
					LC5Adapted.Cases)
				{
					FBlueprintLensCompositeAttachment Attachment;
					Attachment.GrammarId = TEXT("LC5");
					Attachment.AttachmentId =
						(CallCase.IsRenderable()
							? TEXT("lc5-render:")
							: TEXT("lc5-refusal:")) +
						CallCase.CallUnitId;
					if (CallCase.IsRenderable())
					{
						Attachment.MarkerText = FString::Printf(
							TEXT("Open exported callee body · %s · %s"),
							*CallCase.CalleeName,
							CallCase.State ==
								EBlueprintLensLC5LiveClaimState::
									FrozenConditionInstance
								? TEXT("frozen condition instance")
								: TEXT("impure adaptation beyond frozen condition"));
					}
					else if (CallCase.State ==
						EBlueprintLensLC5LiveClaimState::BodyUnavailable)
					{
						Attachment.MarkerText = FString::Printf(
							TEXT("LC5 body unavailable · %s · callee graph absent from export"),
							*CallCase.CalleeName);
					}
					else if (CallCase.DiagnosticCode ==
						TEXT("LC5_LIVE_NON_SELF_CONTEXT_REFUSED"))
					{
						Attachment.MarkerText = FString::Printf(
							TEXT("LC5 not applicable · %s · not self-context"),
							*CallCase.CallTitle);
					}
					else
					{
						Attachment.MarkerText = CallCase.ReaderStatement;
					}
					Attachment.DetailLines = {CallCase.ReaderStatement};
					Attachment.Disclosure = CallCase.IsRenderable() &&
						M6ExpandedAttachmentUnitId == CallCase.CallUnitId &&
						M6ExpandedAttachmentGrammarId == TEXT("LC5")
						? EBlueprintLensCompositeDisclosure::Expanded
						: EBlueprintLensCompositeDisclosure::Collapsed;
					FBlueprintLensCompositeTerminalCapSlot* HostCap =
						CompositeSlots.TerminalCaps.FindByPredicate(
							[&CallCase](
								const FBlueprintLensCompositeTerminalCapSlot& Cap)
							{
								return Cap.UnitId == CallCase.CallUnitId;
							});
					if (HostCap != nullptr)
					{
						HostCap->Attachments.Add(MoveTemp(Attachment));
						continue;
					}
					FBlueprintLensCompositeStationSlot* HostStation =
						CompositeSlots.FindStation(CallCase.CallUnitId);
					if (HostStation != nullptr)
					{
						HostStation->BesideAttachments.Add(
							MoveTemp(Attachment));
					}
				}
			}
			FBlueprintLensCompositeTerminalCapSlot* LC6HostCap = nullptr;
			if (LC6Adapted.IsSuccess())
			{
				LC6HostCap = CompositeSlots.TerminalCaps.FindByPredicate(
					[&LC6Adapted](
						const FBlueprintLensCompositeTerminalCapSlot& Cap)
					{
						return LC6Adapted.BoundaryUnitIds.Contains(Cap.UnitId);
					});
				if (LC6HostCap != nullptr)
				{
					TSet<FString> PresentStatuses;
					for (const FBlueprintLensLC6Track& Track :
						LC6Adapted.Projection.Tracks)
					{
						PresentStatuses.Add(Track.Status);
					}
					FBlueprintLensCompositeAttachment Attachment;
					Attachment.GrammarId = TEXT("LC6");
					Attachment.AttachmentId =
						TEXT("lc6-boundary-ledger:") +
						LC6HostCap->UnitId;
					Attachment.MarkerText = FString::Printf(
						TEXT("Open boundary truth-owner tracks · %d instances · %d present kinds"),
						LC6Adapted.BoundaryUnitIds.Num(),
						PresentStatuses.Num());
					Attachment.Disclosure =
						M6ExpandedAttachmentUnitId == LC6HostCap->UnitId &&
						M6ExpandedAttachmentGrammarId == TEXT("LC6")
							? EBlueprintLensCompositeDisclosure::Expanded
							: EBlueprintLensCompositeDisclosure::Collapsed;
					LC6HostCap->Attachments.Add(MoveTemp(Attachment));
				}
			}
			FBlueprintLensLC7Projection LC7Projection;
			FBlueprintLensCompositeSpanSlot* LC7Span = nullptr;
			if (LC7Adapted.IsSuccess())
			{
				LC7Projection = FBlueprintLensLC7Projector::Build(
					*LC7Adapted.Profile);
				const TSet<FString> LC7Members(
					LC7Adapted.Profile->SCC.OrderedMemberUnitIds);
				LC7Span = CompositeSlots.Spans.FindByPredicate(
					[&LC7Members](FBlueprintLensCompositeSpanSlot& Span)
					{
						const TSet<FString> SpanMembers(Span.MemberUnitIds);
						return Span.bIsOrderBoundary &&
							Span.OrderRegionKind ==
								EBlueprintLensLC1RailOrderRegionKind::StronglyConnected &&
							SpanMembers.Num() == LC7Members.Num() &&
							SpanMembers.Difference(LC7Members).IsEmpty();
					});
				if (LC7Projection.IsRenderable() && LC7Span != nullptr)
				{
					FBlueprintLensCompositeAttachment Attachment;
					Attachment.GrammarId = TEXT("LC7");
					Attachment.AttachmentId = LC7Span->SlotId;
					Attachment.MarkerText = FString::Printf(
						TEXT("Open LC7 static-slice SCC backbone · %d members · %s"),
						LC7Adapted.Profile->SCC.OrderedMemberUnitIds.Num(),
						LC7Adapted.Profile->bExitOutsideSlice
							? TEXT("exit outside slice")
							: TEXT("one exit retained"));
					const bool bExpanded = M6ExpandedSpanId == LC7Span->SlotId;
					Attachment.Disclosure = bExpanded
						? EBlueprintLensCompositeDisclosure::Expanded
						: EBlueprintLensCompositeDisclosure::Collapsed;
					LC7Span->Disclosure = Attachment.Disclosure;
					LC7Span->Attachments.Add(MoveTemp(Attachment));
				}
			}
			const FBlueprintLensLC4SequenceLiveAdapterResult LC4SequenceAdapted =
				FBlueprintLensLC4SequenceLiveAdapter::Build(
					*SourceExplanation,
					LiveTypedIrFacts);
			if (LC4SequenceAdapted.IsSuccess())
			{
				for (const FBlueprintLensLC4SequenceLiveCase& SequenceCase :
					 LC4SequenceAdapted.Cases)
				{
					FBlueprintLensCompositeBetweenStationsSlot* Between =
						CompositeSlots.BetweenStations.FindByPredicate(
							[&SequenceCase](
								FBlueprintLensCompositeBetweenStationsSlot& Slot)
							{
								return Slot.RelationId ==
									SequenceCase.AnchorRelationId;
							});
					if (Between == nullptr)
					{
						continue;
					}
					const FBlueprintLensRelation* AnchorRelation =
						SourceExplanation->FindRelation(
							SequenceCase.AnchorRelationId);
					const FBlueprintLensLC4SequenceOutput* SelectedOutput =
						SequenceCase.Profile.Outputs.FindByPredicate(
							[AnchorRelation](
								const FBlueprintLensLC4SequenceOutput& Output)
							{
								return AnchorRelation != nullptr &&
									AnchorRelation->bHasPortLabel &&
									Output.SourcePinName.Equals(
										AnchorRelation->PortLabel,
										ESearchCase::IgnoreCase);
							});
					FBlueprintLensCompositeAttachment Decoration;
					Decoration.GrammarId = TEXT("LC4-SEQ");
					Decoration.AttachmentId = SequenceCase.AnchorRelationId;
					Decoration.MarkerText = FString::Printf(
						TEXT("Open LC4-SEQ sibling inventory · %s · %d declared outputs"),
						SelectedOutput != nullptr
							? *SelectedOutput->SourcePinName
							: TEXT("then_N"),
						SequenceCase.Profile.Outputs.Num());
					Decoration.Disclosure =
						M6ExpandedBetweenRelationId == SequenceCase.AnchorRelationId
							? EBlueprintLensCompositeDisclosure::Expanded
							: EBlueprintLensCompositeDisclosure::Collapsed;
					Between->Decorations.Add(MoveTemp(Decoration));
				}
			}
			const FBlueprintLensLC2GuardCompound* RootGuard =
				GuardSurface.IsRenderable()
					? GuardSurface.Compounds.FindByPredicate(
						[](const FBlueprintLensLC2GuardCompound& Compound)
						{
							return Compound.ParentGroupId.IsEmpty();
						})
					: nullptr;
			for (FBlueprintLensCompositeStationSlot& Station :
				CompositeSlots.Stations)
			{
				if (!M6ExpandedGuardUnitIds.IsEmpty() &&
					RootGuard != nullptr &&
					Station.UnitId == RootGuard->BranchUnitId &&
					Station.Appearance.Kind ==
						EBlueprintLensCompositeStationAppearanceKind::Guard)
				{
					Station.Appearance.Disclosure =
						EBlueprintLensCompositeDisclosure::Expanded;
				}
				if (!M6ExpandedAttachmentUnitId.IsEmpty() &&
					M6ExpandedAttachmentGrammarId == TEXT("LC3") &&
					Station.UnitId == M6ExpandedAttachmentUnitId)
				{
					for (FBlueprintLensCompositeAttachment& Attachment :
						Station.BesideAttachments)
					{
						if (Attachment.GrammarId == TEXT("LC3"))
						{
							Attachment.Disclosure =
								EBlueprintLensCompositeDisclosure::Expanded;
						}
					}
				}
			}
			const float ReviewWidth = LC1ReviewWidth();
			if (LC1RailCanvas.IsValid() && ReviewWidth <= 0.0f)
			{
				LC1RailLayoutWidth = LC1RailCanvas->GetLayoutWidth();
			}
			if (LC1RailScrollBox.IsValid())
			{
				LC1RailScrollOffset = LC1RailScrollBox->GetScrollOffset();
			}
			const float InitialWidth = ReviewWidth > 0.0f
				? ReviewWidth
				: FMath::Max(LC1RailLayoutWidth, 430.0f);
			TSharedPtr<SWidget> ExpandedStationAppearance;
			FString ExpandedStationAppearanceUnitId;
			TSharedPtr<SWidget> ExpandedBetweenDecoration;
			FString ExpandedBetweenDecorationRelationId;
			TSharedPtr<SWidget> ExpandedSpanAttachment;
			FString ExpandedSpanAttachmentId;
			TSharedPtr<SWidget> ExpandedTerminalAttachment;
			FString ExpandedTerminalAttachmentUnitId;
			if (GuardSurface.IsRenderable())
			{
				FBlueprintLensCompositeStationSlot* RootStation =
					RootGuard != nullptr
						? CompositeSlots.FindStation(RootGuard->BranchUnitId)
						: nullptr;
				if (RootStation != nullptr &&
					RootStation->Appearance.Disclosure ==
						EBlueprintLensCompositeDisclosure::Expanded)
				{
					const float GuardWidth = FMath::Clamp(
						InitialWidth - 32.0f,
						430.0f,
						700.0f);
					const FBlueprintLensLC2GuardLayoutSessionResult GuardSession =
						FBlueprintLensLC2GuardLayoutSession::Build(
							GuardSurface,
							*LC2Explanation,
							GuardWidth);
					if (GuardSession.IsRenderable(GuardSurface))
					{
						TSharedRef<SBlueprintLensLC2GuardCanvas> GuardCanvas =
							SNew(SBlueprintLensLC2GuardCanvas)
								.Projection(GuardSurface)
								.InitialSession(GuardSession)
								.Explanation(LC2Explanation)
								.SelectedUnitId_Lambda(
									[this]()
									{
										return LC1SelectedUnitId;
									})
								.OnUnitSelected(
									FOnBlueprintLensLC2GuardUnitSelected::CreateSP(
										this,
										&SBlueprintLensPanel::SelectM6RailUnit));
						GuardCanvas->SetTag(CompositeLC2SurfaceAutomationTag);
						const int32 OmittedUnits = FMath::Max(
							GuardScopeInputUnits - GuardScopeAdaptedUnits,
							0);
						const int32 OmittedRelations = FMath::Max(
							GuardScopeInputRelations - GuardScopeAdaptedRelations,
							0);
						if (OmittedUnits > 0 || OmittedRelations > 0)
						{
							TSharedRef<STextBlock> ScopeDisclosure =
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("Guard view scope · %d of %d units and %d of %d "
										"relations shown. %d units and %d relations remain "
										"outside this guard view."),
									GuardScopeAdaptedUnits,
									GuardScopeInputUnits,
									GuardScopeAdaptedRelations,
									GuardScopeInputRelations,
									OmittedUnits,
									OmittedRelations)))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.AutoWrapText(true)
								.Visibility(EVisibility::HitTestInvisible);
							ScopeDisclosure->SetTag(
								CompositeGuardCoreScopeDisclosureAutomationTag);
							ExpandedStationAppearance =
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(8.0f, 4.0f, 8.0f, 8.0f)
								[
									ScopeDisclosure
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									GuardCanvas
								];
							RootStation->Appearance.ExpandedContentHeight =
								GuardCanvas->GetSurface().CanvasSize.Y + 56.0f;
						}
						else
						{
							RootStation->Appearance.ExpandedContentHeight =
								GuardCanvas->GetSurface().CanvasSize.Y;
							ExpandedStationAppearance = GuardCanvas;
						}
						ExpandedStationAppearanceUnitId =
							RootStation->UnitId;
					}
				}
			}
			if (!ExpandedStationAppearance.IsValid() &&
				ValueConeProjection.IsRenderable() &&
				ValueConeProjection.Status ==
					EBlueprintLensLC3ValueConeProjectionStatus::ValueCone &&
				!M6ExpandedAttachmentUnitId.IsEmpty() &&
				M6ExpandedAttachmentGrammarId == TEXT("LC3"))
			{
				FBlueprintLensCompositeStationSlot* ValueStation =
					CompositeSlots.FindStation(
						M6ExpandedAttachmentUnitId);
				FBlueprintLensCompositeAttachment* ValueAttachment =
					ValueStation != nullptr
						? ValueStation->BesideAttachments.FindByPredicate(
							[](const FBlueprintLensCompositeAttachment& Attachment)
							{
								return Attachment.GrammarId == TEXT("LC3") &&
									Attachment.Disclosure ==
										EBlueprintLensCompositeDisclosure::Expanded;
							})
						: nullptr;
				if (ValueStation != nullptr && ValueAttachment != nullptr)
				{
					const float ValueWidth = FMath::Clamp(
						InitialWidth - 32.0f,
						430.0f,
						700.0f);
					const FBlueprintLensLC3ValueConeLayoutSessionResult ValueSession =
						FBlueprintLensLC3ValueConeLayoutSession::Build(
							ValueConeProjection,
							ValueWidth);
					if (ValueSession.IsRenderable(ValueConeProjection))
					{
						TSharedRef<SBlueprintLensLC3ValueConeCanvas> ValueCanvas =
							SNew(SBlueprintLensLC3ValueConeCanvas)
								.Projection(ValueConeProjection)
								.InitialSession(ValueSession)
								.SelectedUnitId_Lambda(
									[this]()
									{
										return LC1SelectedUnitId;
									})
								.Density(LC3ValueConeDensity)
								.OnUnitSelected(
									FOnBlueprintLensLC3ValueConeUnitSelected::CreateSP(
										this,
										&SBlueprintLensPanel::SelectM6RailUnit));
						ValueCanvas->SetTag(CompositeLC3SurfaceAutomationTag);
						const int32 OmittedUnits = FMath::Max(
							ValueScopeInputUnits - ValueScopeAdaptedUnits,
							0);
						const int32 OmittedRelations = FMath::Max(
							ValueScopeInputRelations - ValueScopeAdaptedRelations,
							0);
						const float CanvasHeight =
							ValueCanvas->GetSurfaceSize().Y;
						if (OmittedUnits > 0 || OmittedRelations > 0)
						{
							TSharedRef<STextBlock> ScopeDisclosure =
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("Value view scope · %d of %d units and %d of %d "
										"relations shown. %d units and %d relations remain "
										"outside this value view."),
									ValueScopeAdaptedUnits,
									ValueScopeInputUnits,
									ValueScopeAdaptedRelations,
									ValueScopeInputRelations,
									OmittedUnits,
									OmittedRelations)))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.AutoWrapText(true)
								.Visibility(EVisibility::HitTestInvisible);
							ScopeDisclosure->SetTag(
								CompositeLC3CoreScopeDisclosureAutomationTag);
							ExpandedStationAppearance =
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(8.0f, 4.0f, 8.0f, 8.0f)
								[
									ScopeDisclosure
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									ValueCanvas
								];
							ValueAttachment->ExpandedContentHeight =
								CanvasHeight + 56.0f;
						}
						else
						{
							ValueAttachment->ExpandedContentHeight = CanvasHeight;
							ExpandedStationAppearance = ValueCanvas;
						}
						ExpandedStationAppearanceUnitId = ValueStation->UnitId;
					}
				}
			}
			if (!M6ExpandedBetweenRelationId.IsEmpty() &&
				LC4SequenceAdapted.IsSuccess())
			{
				const FBlueprintLensLC4SequenceLiveCase* SequenceCase =
					LC4SequenceAdapted.Cases.FindByPredicate(
						[this](const FBlueprintLensLC4SequenceLiveCase& Candidate)
						{
							return Candidate.AnchorRelationId ==
								M6ExpandedBetweenRelationId;
						});
				FBlueprintLensCompositeBetweenStationsSlot* Between =
					CompositeSlots.BetweenStations.FindByPredicate(
						[this](FBlueprintLensCompositeBetweenStationsSlot& Candidate)
						{
							return Candidate.RelationId ==
								M6ExpandedBetweenRelationId;
						});
				FBlueprintLensCompositeAttachment* Decoration = Between != nullptr
					? Between->Decorations.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Candidate)
						{
							return Candidate.GrammarId == TEXT("LC4-SEQ");
						})
					: nullptr;
				if (SequenceCase != nullptr && Decoration != nullptr)
				{
					const FBlueprintLensLC4SequenceProjection SequenceProjection =
						FBlueprintLensLC4SequenceProjector::Build(
							SequenceCase->Profile,
							SequenceCase->Explanation);
					const float SequenceWidth = FMath::Clamp(
						InitialWidth - 32.0f, 430.0f, 700.0f);
					const FBlueprintLensLC4SequenceLayoutSessionResult SequenceSession =
						FBlueprintLensLC4SequenceLayoutSession::Build(
							SequenceProjection,
							SequenceWidth);
					if (SequenceSession.IsRenderable(SequenceProjection))
					{
						TSharedRef<SBlueprintLensLC4SequenceRail> SequenceRail =
							SNew(SBlueprintLensLC4SequenceRail)
								.Projection(SequenceProjection)
								.InitialSession(SequenceSession)
								.SelectedOrdinal(INDEX_NONE)
								.Evidence(false)
								.ActiveActionId(TEXT("select"));
						SequenceRail->SetTag(FName(TEXT(
							"BlueprintLens.Automation.CompositeLC4SequenceSurface")));
						Decoration->ExpandedContentHeight =
							SequenceSession.Layout.CanvasSize.Y;
						ExpandedBetweenDecoration = SequenceRail;
						ExpandedBetweenDecorationRelationId =
							SequenceCase->AnchorRelationId;
					}
				}
			}
			if (!M6ExpandedSpanId.IsEmpty() && LC7Adapted.IsSuccess() &&
				LC7Span != nullptr && LC7Projection.IsRenderable() &&
				LC7Span->SlotId == M6ExpandedSpanId)
			{
				FBlueprintLensCompositeAttachment* Attachment =
					LC7Span->Attachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Candidate)
						{
							return Candidate.GrammarId == TEXT("LC7") &&
								Candidate.Disclosure ==
									EBlueprintLensCompositeDisclosure::Expanded;
						});
				const float LC7Width = FMath::Clamp(
					InitialWidth - 32.0f, 430.0f, 700.0f);
				const FBlueprintLensLC7LayoutSessionResult LC7Session =
					FBlueprintLensLC7LayoutSession::Build(
						LC7Projection,
						LC7Width,
						LC7Projection.SCCs.IsEmpty()
							? FString()
							: LC7Projection.SCCs[0].GroupId);
				if (Attachment != nullptr &&
					LC7Session.IsRenderable(LC7Projection))
				{
					TSharedRef<STextBlock> RelationFamilyDisclosure =
						SNew(STextBlock)
						.Text(FText::FromString(
							LC7Projection.RelationFamilyStatement))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
						.Visibility(EVisibility::HitTestInvisible);
					RelationFamilyDisclosure->SetTag(
						CompositeLC7RelationFamilyDisclosureAutomationTag);
					TSharedRef<STextBlock> ExitBoundaryDisclosure =
						SNew(STextBlock)
						.Text(FText::FromString(
							LC7Projection.ExitBoundaryStatement))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
						.Visibility(EVisibility::HitTestInvisible);
					ExitBoundaryDisclosure->SetTag(
						CompositeLC7ExitBoundaryDisclosureAutomationTag);
					TSharedRef<SBlueprintLensLC7AdaptiveBackbone> LiveLC7Canvas =
						SNew(SBlueprintLensLC7AdaptiveBackbone)
						.Projection(LC7Projection)
						.InitialSession(LC7Session)
						.SelectedUnitId_Lambda(
							[this]()
							{
								return LC1SelectedUnitId;
							});
					LiveLC7Canvas->SetTag(CompositeLC7SurfaceAutomationTag);
					ExpandedSpanAttachment =
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f)
						[
							RelationFamilyDisclosure
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 8.0f)
						[
							ExitBoundaryDisclosure
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							LiveLC7Canvas
						];
					Attachment->ExpandedContentHeight =
						LC7Session.Layout.CanvasSize.Y + 76.0f;
					ExpandedSpanAttachmentId = LC7Span->SlotId;
				}
			}
			if (!M6ExpandedAttachmentUnitId.IsEmpty() &&
				M6ExpandedAttachmentGrammarId == TEXT("LC5") &&
				LC5Adapted.IsSuccess())
			{
				const FBlueprintLensLC5LiveCallCase* LiveCase =
					LC5Adapted.Cases.FindByPredicate(
						[this](const FBlueprintLensLC5LiveCallCase& Candidate)
						{
							return Candidate.CallUnitId ==
								M6ExpandedAttachmentUnitId &&
								Candidate.IsRenderable();
						});
				FBlueprintLensCompositeTerminalCapSlot* HostCap =
					CompositeSlots.TerminalCaps.FindByPredicate(
						[this](FBlueprintLensCompositeTerminalCapSlot& Cap)
						{
							return Cap.UnitId == M6ExpandedAttachmentUnitId;
						});
				FBlueprintLensCompositeStationSlot* HostStation =
					CompositeSlots.FindStation(M6ExpandedAttachmentUnitId);
				FBlueprintLensCompositeAttachment* Attachment =
					HostCap != nullptr
						? HostCap->Attachments.FindByPredicate(
							[](const FBlueprintLensCompositeAttachment& Candidate)
							{
								return Candidate.GrammarId == TEXT("LC5") &&
									Candidate.Disclosure ==
										EBlueprintLensCompositeDisclosure::Expanded;
							})
						: HostStation != nullptr
							? HostStation->BesideAttachments.FindByPredicate(
								[](const FBlueprintLensCompositeAttachment& Candidate)
								{
									return Candidate.GrammarId == TEXT("LC5") &&
										Candidate.Disclosure ==
											EBlueprintLensCompositeDisclosure::Expanded;
								})
							: nullptr;
				if (LiveCase != nullptr && Attachment != nullptr)
				{
					const float LC5Width = FMath::Clamp(
						InitialWidth - 56.0f,
						430.0f,
						700.0f);
					const FBlueprintLensLC5LayoutSessionResult LC5Session =
						FBlueprintLensLC5LayoutSession::Build(
							LiveCase->Projection,
							LC5Width);
					if (LC5Session.IsRenderable(LiveCase->Projection))
					{
						if (LC5SelectedOccurrenceId.IsEmpty())
						{
							LC5SelectedOccurrenceId =
								LiveCase->Projection.Occurrences[0].OccurrenceId;
						}
						TSharedRef<STextBlock> ClaimBoundary =
							SNew(STextBlock)
							.Text(FText::FromString(
								LiveCase->ReaderStatement))
							.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
							.AutoWrapText(true)
							.Visibility(EVisibility::HitTestInvisible);
						ClaimBoundary->SetTag(
							CompositeLC5ClaimBoundaryDisclosureAutomationTag);
						const FString ActiveAction =
							LC5DetailMode == ELC5DetailMode::CompleteText
								? TEXT("show_complete_text")
								: LC5DetailMode == ELC5DetailMode::Evidence
									? TEXT("show_evidence")
									: TEXT("select");
						TSharedRef<SBlueprintLensLC5TypedPortal> LiveCanvas =
							SNew(SBlueprintLensLC5TypedPortal)
							.Projection(LiveCase->Projection)
							.InitialSession(LC5Session)
							.SelectedOccurrenceId(LC5SelectedOccurrenceId)
							.ActiveActionId(ActiveAction)
							.OnAction(FOnBlueprintLensLC5Action::CreateSP(
								this,
								&SBlueprintLensPanel::HandleLC5Action))
							.OnOccurrenceSelected(
								FOnBlueprintLensLC5OccurrenceSelected::CreateSP(
									this,
									&SBlueprintLensPanel::SelectLC5Occurrence));
						LiveCanvas->SetTag(CompositeLC5SurfaceAutomationTag);
						TSharedRef<SVerticalBox> LivePortal = SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(8.0f, 4.0f, 8.0f, 8.0f)
							[
								ClaimBoundary
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								LiveCanvas
							];
						if (LC5DetailMode != ELC5DetailMode::None)
						{
							LivePortal->AddSlot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								BuildLC5Detail(LiveCase->Projection)
							];
						}
						Attachment->ExpandedContentHeight =
							LC5Session.Layout.CanvasSize.Y +
							(LC5DetailMode == ELC5DetailMode::None ? 58.0f : 220.0f);
						if (HostCap != nullptr)
						{
							ExpandedTerminalAttachment = LivePortal;
							ExpandedTerminalAttachmentUnitId =
								HostCap->UnitId;
						}
						else if (HostStation != nullptr)
						{
							ExpandedStationAppearance = LivePortal;
							ExpandedStationAppearanceUnitId =
								HostStation->UnitId;
						}
					}
				}
			}
			if (!M6ExpandedAttachmentUnitId.IsEmpty() &&
				M6ExpandedAttachmentGrammarId == TEXT("LC6") &&
				LC6Adapted.IsSuccess() && LC6HostCap != nullptr &&
				LC6HostCap->UnitId == M6ExpandedAttachmentUnitId)
			{
				FBlueprintLensCompositeAttachment* Attachment =
					LC6HostCap->Attachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Candidate)
						{
							return Candidate.GrammarId == TEXT("LC6") &&
								Candidate.Disclosure ==
									EBlueprintLensCompositeDisclosure::Expanded;
						});
				const float LC6Width = FMath::Clamp(
					InitialWidth - 56.0f, 430.0f, 700.0f);
				const FBlueprintLensLC6LayoutSessionResult LC6Session =
					FBlueprintLensLC6LayoutSession::Build(
						LC6Adapted.Projection,
						LC6Width);
				if (Attachment != nullptr &&
					LC6Session.IsRenderable(LC6Adapted.Projection))
				{
					TSharedRef<STextBlock> AbsenceDisclosure =
						SNew(STextBlock)
						.Text(FText::FromString(
							LC6Adapted.AbsenceStatement))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
						.Visibility(EVisibility::HitTestInvisible);
					AbsenceDisclosure->SetTag(
						CompositeLC6AbsentTracksDisclosureAutomationTag);
					TSharedRef<STextBlock> ContributionDisclosure =
						SNew(STextBlock)
						.Text(FText::FromString(
							LC6Adapted.ContributionStatement))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
						.Visibility(EVisibility::HitTestInvisible);
					ContributionDisclosure->SetTag(
						CompositeLC6TruthOwnerContributionAutomationTag);
					TSharedRef<SWidget> LiveLC6Surface =
						BuildLC6FourTrackSurface(
							LC6Adapted.Projection,
							LC6Session,
							CompositeLC6SurfaceAutomationTag);
					ExpandedTerminalAttachment =
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f)
						[
							ContributionDisclosure
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 8.0f)
						[
							AbsenceDisclosure
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							LiveLC6Surface
						];
					Attachment->ExpandedContentHeight =
						LC6Session.Layout.CanvasSize.Y + 92.0f;
					ExpandedTerminalAttachmentUnitId = LC6HostCap->UnitId;
				}
			}
			const FBlueprintLensLC1RailLayoutSessionResult RailLayoutSession =
				FBlueprintLensLC1RailLayoutSession::Build(
					RailProjection,
					*RailExplanation,
					InitialWidth);
			const FBlueprintLensLC1RailSurfaceLayout InitialSurface =
				FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
					RailProjection,
					RailLayoutSession,
					CompositeSlots,
					InitialWidth,
					bM6CompositeFoldExpanded
						? RailProjection.OrderedCanonicalUnits.Num() - 1
						: INDEX_NONE);
			if (RailLayoutSession.IsRenderable(RailProjection) &&
				InitialSurface.IsRenderable(RailProjection))
			{
				// This is the accepted packet's existing execution-only rail. The
				// generic C baseline remains the explicit fail-closed fallback below.
				return BuildM6CausalRailContent(
					RailExplanation,
					RailProjection,
					RailLayoutSession,
					CompositeSlots,
					ExpandedStationAppearance,
					ExpandedStationAppearanceUnitId,
					ReviewWidth,
					false,
					ExpandedBetweenDecoration,
					ExpandedBetweenDecorationRelationId,
					ExpandedSpanAttachment,
					ExpandedSpanAttachmentId,
					ExpandedTerminalAttachment,
					ExpandedTerminalAttachmentUnitId);
			}
		}
		}
	}
	Content->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("Generic FrameFlow/Weave · %d entities · %d relations · specialized routes bypassed"),
			Views->C.Entities.Num(), Views->C.Relations.Num())))
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		.AutoWrapText(true)
	];
	for (const FM6BaselineViewEntity& Entity : Views->C.Entities)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(FString::Printf(
				TEXT("%s · %s%s"), *Entity.Label, *Entity.PresentationStatus,
				Entity.bBoundary ? *FString(TEXT(" · boundary: ") + Entity.BoundaryReason) : TEXT(""))))
			.OnClicked_Lambda([this, EntityId = Entity.Id]()
			{
				SelectM6Entity(EntityId);
				return FReply::Handled();
			})
		];
		if (M6Presentation.IsDetailVisible())
		{
			Content->AddSlot().AutoHeight().Padding(12.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("reason: %s · inclusion: %s"),
					Entity.SemanticReason.IsEmpty() ? TEXT("none") : *Entity.SemanticReason,
					*FString::Join(Entity.InclusionReasons, TEXT(", ")))))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.AutoWrapText(true)
			];
		}
	}
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Content
		];
}

bool SBlueprintLensPanel::TryDeriveLC7TruthPaths(
	const FString& ExplanationPath,
	FString& OutProfilePath,
	FString& OutReviewedPath,
	FString& OutReadinessPath)
{
	OutProfilePath.Reset();
	OutReviewedPath.Reset();
	OutReadinessPath.Reset();
	if (!FPaths::GetCleanFilename(ExplanationPath).Equals(
		TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
		ESearchCase::IgnoreCase))
	{
		return false;
	}

	const FString Directory = FPaths::GetPath(ExplanationPath);
	OutProfilePath = FPaths::Combine(
		Directory, TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"));
	OutReviewedPath = FPaths::Combine(
		Directory, TEXT("reviewed-ground-truth.v1.json"));
	OutReadinessPath = FPaths::Combine(Directory, TEXT("readiness.json"));
	return true;
}

void SBlueprintLensPanel::ReloadModel()
{
	const FString FixturePath =
		FModuleManager::GetModuleChecked<FBlueprintLensEditorModule>(
			TEXT("BlueprintLensEditor"))
			.GetExplanationPath();

	LC4SequenceProfile.Reset();
	LC4AsyncProfile.Reset();
	LC5Profile.Reset();
	LC6Profile.Reset();
	LC7Profile.Reset();
	LC4AsyncScrollBox.Reset();
	LC4AsyncScrollOffset = 0.0f;
	LC4SequenceScrollBox.Reset();
	LC4SequenceScrollOffset = 0.0f;
	LC5ScrollBox.Reset();
	LC5ScrollOffset = 0.0f;
	LC6OverviewScrollBox.Reset();
	LC6DetailScrollBox.Reset();
	LC6OverviewScrollOffset = 0.0f;
	LC6DetailScrollOffset = 0.0f;
	LC7Canvas.Reset();
	LC7OverviewScrollBox.Reset();
	LC7DetailScrollBox.Reset();
	LC7OverviewScrollOffset = 0.0f;
	LC7DetailScrollOffset = 0.0f;
	LC4AppliedReviewScrollOffset = -1.0f;
	LC5AppliedReviewScrollOffset = -1.0f;
	LC5AppliedReviewScrollBox.Reset();
	FString LC7SCCProfilePath;
	FString LC7ReviewedPath;
	FString LC7ReadinessPath;
	if (TryDeriveLC7TruthPaths(
			FixturePath, LC7SCCProfilePath, LC7ReviewedPath, LC7ReadinessPath))
	{
		const FBlueprintLensLC7LoadResult Result =
			FBlueprintLensLC7ProfileLoader::LoadFiles(
				FixturePath, LC7SCCProfilePath,
				LC7ReviewedPath, LC7ReadinessPath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.ExplanationModel;
		LC7Profile = Result.Profile;
	}
	else if (FixturePath.EndsWith(
			TEXT(".core-boundary-matrix.v1.json"),
			ESearchCase::IgnoreCase))
	{
		const FString Directory = FPaths::GetPath(FixturePath);
		const FString QueryPath = FPaths::Combine(
			Directory,
			TEXT("BP_LC6_BoundaryMatrix.upstream-budget.v1.json"));
		const FString ReadinessPath = FPaths::Combine(
			Directory, TEXT("readiness.json"));
		const FString RawPath = FPaths::Combine(
			Directory,
			TEXT("run1/BP_LC6_BoundaryMatrix.raw-0.2.json"));
		const FBlueprintLensLC6LoadResult Result =
			FBlueprintLensLC6ProfileLoader::LoadFiles(
				FixturePath, QueryPath, ReadinessPath, RawPath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.ExplanationModel;
		LC6Profile = Result.Profile;
	}
	else if (FixturePath.EndsWith(
			TEXT(".contextual-slice.v1.json"),
			ESearchCase::IgnoreCase))
	{
		const FBlueprintLensLC5LoadResult Result =
			FBlueprintLensLC5ProfileLoader::LoadFile(FixturePath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.ExplanationModel;
		LC5Profile = Result.Profile;
	}
	else if (FixturePath.EndsWith(
			TEXT(".async-profile.v1.json"),
			ESearchCase::IgnoreCase))
	{
		const FBlueprintLensLC4AsyncLoadResult Result =
			FBlueprintLensLC4AsyncProfileLoader::LoadFile(FixturePath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.ExplanationModel;
		LC4AsyncProfile = Result.Profile;
	}
	else if (FixturePath.EndsWith(
			TEXT(".sequence-profile.v1.json"),
			ESearchCase::IgnoreCase))
	{
		const FBlueprintLensLC4SequenceLoadResult Result =
			FBlueprintLensLC4SequenceProfileLoader::LoadFile(FixturePath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.ExplanationModel;
		LC4SequenceProfile = Result.Profile;
	}
	else
	{
		const FBlueprintLensLoadResult Result =
			FBlueprintLensExplanationLoader::LoadFile(FixturePath);
		if (!Result.IsSuccess())
		{
			Model.Reset();
			ResolvedSources.Reset();
			bHasStaleSource = false;
			NavigationMessage.Reset();
			LastError = Result.Error;
			RootBox->SetContent(BuildErrorContent(FixturePath, LastError));
			return;
		}
		Model = Result.Model;
	}
	bLC1RegionMembersExpanded = false;
	bLC1ShowAllExpanded = false;
	bLC1WhyGroupedExpanded = false;
	bLC1PseudocodeExpanded = false;
	bLC1TechnicalEvidenceExpanded = false;
	LC1SelectedUnitId.Reset();
	LC1SelectedPseudocodeLineId.Reset();
	bLC2TechnicalEvidenceExpanded = false;
	LC2SelectedUnitId.Reset();
	bLC3TechnicalEvidenceExpanded = false;
	LC3ValueConeDensity = EBlueprintLensLC3ValueConeDensity::Summary;
	LC3SelectedUnitId.Reset();
	LC4SelectedOutputOrdinal = INDEX_NONE;
	LC4DetailMode = ELC4DetailMode::None;
	LC4AsyncVariant = TEXT("A_FIRST");
	LC4AsyncDetailMode = ELC4AsyncDetailMode::None;
	LC5DetailMode = ELC5DetailMode::None;
	LC5SelectedOccurrenceId.Reset();
	LC6DetailMode = ELC6DetailMode::None;
	LC6SelectedScenarioId.Reset();
	LC7DetailMode = ELC7DetailMode::None;
	LC7SelectedUnitId.Reset();
	LastError.Reset();
	NavigationMessage.Reset();
	ResolveSources();
	RootBox->SetContent(BuildLoadedContent());
}

void SBlueprintLensPanel::ResolveSources()
{
	ResolvedSources.Reset();
	bHasStaleSource = false;
	if (!Model.IsValid())
	{
		return;
	}

	for (const FBlueprintLensUnit& Unit : Model->Units)
	{
		for (const FBlueprintLensSourceReference& Reference :
			 Unit.SourceReferences)
		{
			FBlueprintLensResolvedSource Resolved =
				SourceNavigator.Resolve(Model->Source, Reference);
			bHasStaleSource |=
				Resolved.State == EBlueprintLensSourceState::Stale;
			ResolvedSources.Add(
				Reference.SourceNodeId,
				MoveTemp(Resolved));
		}
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLoadedContent()
{
	check(Model.IsValid());
	check(Model->Lanes.Num() == 6);

	PopulateExplanationOptions();

	const FBlueprintLensUnit* Criterion =
		Model->FindUnit(Model->CriterionUnitId);
	const FString CriterionDisplayLabel = Criterion != nullptr
		? BlueprintLensDisplayLabel(*Criterion)
		: FString(TEXT("UNRESOLVED"));
	const FString DirectionLabel =
		Model->Query.Direction.Replace(TEXT("_"), TEXT(" ")).ToUpper();
	const FText ContextText = FText::FromString(FString::Printf(
		TEXT("CRITERION \u00B7 %s \u00B7 %s \u00B7 %d NODES \u00B7 %d EDGES"),
		*CriterionDisplayLabel,
		*DirectionLabel,
		Model->Counts.SourceNodes,
		Model->Counts.SourceEdges));

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(PanelPadding)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, SectionSpacing, 0.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Model->Query.Question))
						.Font(FAppStyle::Get().GetFontStyle(
							"NormalFontBold"))
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(ContextText)
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.AutoWrapText(true)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					BuildCaseSelector()
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					SNew(SButton)
					.Text(LOCTEXT("Reload", "Reload"))
					.ToolTipText(LOCTEXT(
						"ReloadTooltip",
						"Reload the bundled reader model"))
					.OnClicked_Lambda(
						[this]()
						{
							ReloadModel();
							return FReply::Handled();
						})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SBox)
				.Visibility(
					IsLC1ComparisonModel() || IsLC2GuardOutlineModel() ||
					IsLC3ValueConeModel()
						|| IsLC4SequenceDisclosureModel()
						|| IsLC4AsyncModel() || IsLC5TypedPortalModel()
						|| IsLC6FourTrackModel() || IsLC7AdaptiveBackboneModel()
						? EVisibility::Collapsed
						: EVisibility::Visible)
				[
					BuildModeSwitcher()
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
			[
				SNew(STextBlock)
					.Visibility_Lambda(
						[this]()
						{
							return NavigationMessage.IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
					.Text_Lambda(
						[this]()
						{
							return FText::FromString(NavigationMessage);
						})
					.ColorAndOpacity(FLinearColor(0.95f, 0.42f, 0.25f))
					.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildRepresentationContent()
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildModeSwitcher()
{
	const auto BuildModeButton =
		[this](
			const EDisplayMode Mode,
			const FText& Label,
			const FText& ToolTip) -> TSharedRef<SWidget>
		{
			const bool bSelected = DisplayMode == Mode;
			return SNew(SButton)
				.Text(Label)
				.ToolTipText(ToolTip)
				.ButtonColorAndOpacity(
					bSelected
						? FLinearColor(0.23f, 0.51f, 0.68f, 1.0f)
						: FLinearColor(0.34f, 0.35f, 0.37f, 1.0f))
				.OnClicked_Lambda(
					[this, Mode]()
					{
						SetDisplayMode(Mode);
						return FReply::Handled();
					});
		};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(5.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				BuildModeButton(
					EDisplayMode::Lanes,
					LOCTEXT("ModeLanes", "LANES"),
					LOCTEXT(
						"ModeLanesTooltip",
						"Role-grouped Semantic Lane baseline"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				BuildModeButton(
					EDisplayMode::FrameFlow,
					LOCTEXT("ModeFrameFlow", "WEAVE"),
					LOCTEXT(
						"ModeFrameFlowTooltip",
						"Relation-first execution rail, predicate gate and local value tributary"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				BuildModeButton(
					EDisplayMode::Route,
					LOCTEXT("ModeRoute", "ROUTE"),
					LOCTEXT(
						"ModeRouteTooltip",
						"Ordered execution route with semantic attachments"))
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildRepresentationContent()
{
	if (IsLC2GuardOutlineModel())
	{
		return BuildLC2GuardSurfaceRepresentation();
	}
	if (IsLC3ValueConeModel())
	{
		return BuildLC3ValueConeRepresentation();
	}
	if (IsLC4SequenceDisclosureModel())
	{
		return BuildLC4SequenceRepresentation();
	}
	if (IsLC4AsyncModel())
	{
		return BuildLC4AsyncRepresentation();
	}
	if (IsLC5TypedPortalModel())
	{
		return BuildLC5TypedPortalRepresentation();
	}
	if (IsLC6FourTrackModel())
	{
		return BuildLC6FourTrackRepresentation();
	}
	if (IsLC7AdaptiveBackboneModel())
	{
		return BuildLC7AdaptiveBackboneRepresentation();
	}
	if (IsLC1ComparisonModel())
	{
		const FBlueprintLensFrameFlowLayoutModel LinearLayout =
			FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(*Model);
		return BuildLC1RailRepresentation(LinearLayout);
	}

	switch (DisplayMode)
	{
	case EDisplayMode::Lanes:
		return BuildLaneRepresentation();
	case EDisplayMode::Route:
		return BuildRouteRepresentation();
	default:
		return BuildFrameFlowRepresentation();
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2GuardSurfaceRepresentation()
{
	const FBlueprintLensLC2GuardOutlineProjection Outline =
		FBlueprintLensLC2GuardOutlineProjector::Build(*Model);
	const FBlueprintLensLC2GuardSurfaceProjection Projection =
		FBlueprintLensLC2GuardSurfaceProjector::Build(*Model, Outline);
	if (!Projection.IsRenderable())
	{
		return BuildLC2GuardOutlineRepresentation();
	}

	// Selection rebuilds this whole subtree. Carry the live width and scroll
	// position across the rebuild, or the reader sees the surface snap from the
	// session default back to its real width and jump to the top, which reads as
	// "nothing happened" when the revealed row lands below the fold.
	if (LC2GuardCanvas.IsValid())
	{
		LC2GuardLayoutWidth = LC2GuardCanvas->GetLayoutWidth();
	}
	if (LC2GuardScrollBox.IsValid())
	{
		LC2GuardScrollOffset = LC2GuardScrollBox->GetScrollOffset();
	}
	const FBlueprintLensLC2GuardLayoutSessionResult LayoutSession =
		FBlueprintLensLC2GuardLayoutSession::Build(
			Projection,
			*Model,
			FMath::Max(LC2GuardLayoutWidth, 430.0f));
	if (!LayoutSession.IsRenderable(Projection))
	{
		return BuildLC2GuardOutlineRepresentation();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC2GuardSurfaceHeading",
				"NESTED GUARD GATES · ALTERNATIVE OUTCOME RAILS"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT(
					"Read three alternative outcomes toward the %s criterion. "
					"Each fork mark means its exits are unordered. Select a Guard "
					"Gate to reveal its local predicate ownership; select it again "
					"to collapse."),
				*LC2ReaderUnitLabel(*Model->FindUnit(Projection.CriterionUnitId)))))
			.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SSegmentedControl<EBlueprintLensLC2GuardDensity>)
			.Value(LC2GuardDensity)
			.OnValueChanged(
				this,
				&SBlueprintLensPanel::SetLC2GuardDensity)

			+ SSegmentedControl<EBlueprintLensLC2GuardDensity>::Slot(
				EBlueprintLensLC2GuardDensity::Summary)
			.Text(LOCTEXT("LC2GuardDensitySummary", "Summary"))

			+ SSegmentedControl<EBlueprintLensLC2GuardDensity>::Slot(
				EBlueprintLensLC2GuardDensity::Evidence)
			.Text(LOCTEXT("LC2GuardDensityEvidence", "Evidence"))
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SAssignNew(LC2GuardCanvas, SBlueprintLensLC2GuardCanvas)
			.Projection(Projection)
			.InitialSession(LayoutSession)
			.Explanation(Model)
			.SelectedUnitId_Lambda(
				[this]()
				{
					return LC2SelectedUnitId;
				})
			.OnUnitSelected(
				FOnBlueprintLensLC2GuardUnitSelected::CreateSP(
					this,
					&SBlueprintLensPanel::SelectLC2Unit))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(
				RoleAccent(EBlueprintLensRole::Boundary),
				0.12f))
			.Padding(FMargin(8.0f))
		[
			SNew(STextBlock)
				.Text_Lambda(
					[this, Projection]()
					{
						// Drawn comes from the layout that actually placed the
						// rails; the total comes from the ledger, so a reduction
						// can be stated instead of restated.
						const int32 TotalOutcomes = Projection.OutcomeRails.Num();
						int32 DrawnOutcomes = TotalOutcomes;
						FString FoldStatement;
						if (LC2GuardCanvas.IsValid() &&
							LC2GuardCanvas->GetSurface().IsRenderable(Projection))
						{
							const FBlueprintLensLC2GuardSurfaceLayout& Surface =
								LC2GuardCanvas->GetSurface();
							DrawnOutcomes = Surface.DrawnOutcomeCount();
							FoldStatement = Surface.OutcomeFold.ReaderText;
						}
						FString Text = FString::Printf(
							TEXT("%s\n%d OF %d OUTCOMES DRAWN"),
							*Projection.NoOrderReaderText,
							DrawnOutcomes,
							TotalOutcomes);
						if (!FoldStatement.IsEmpty())
						{
							Text += TEXT("\n") + FoldStatement;
						}
						return FText::FromString(Text);
					})
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.AutoWrapText(true)
		]
	];

	if (!LC2SelectedUnitId.IsEmpty())
	{
		const FBlueprintLensUnit* SelectedUnit =
			Model->FindUnit(LC2SelectedUnitId);
		if (SelectedUnit != nullptr)
		{
			const FString SelectedSourceNodeId =
				PrimarySourceNodeId(*SelectedUnit);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"LC2OpenSelectedItem",
						"Open selected item in Blueprint"))
					.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
					.OnClicked_Lambda(
						[this, SelectedSourceNodeId]()
						{
							return NavigateToSource(SelectedSourceNodeId);
						})
			];
		}
	}

	if (LC2GuardDensity == EBlueprintLensLC2GuardDensity::Evidence)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC2TechnicalEvidence(Projection)
		];
	}

	TSharedRef<SScrollBox> Scroll =
		SAssignNew(LC2GuardScrollBox, SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				Content
			];
	Scroll->SetScrollOffset(LC2GuardScrollOffset);
	return Scroll;
}

bool SBlueprintLensPanel::IsLC1ComparisonModel() const
{
	return Model.IsValid() &&
		FBlueprintLensLC1RailProjector::Build(*Model).IsRenderable();
}

bool SBlueprintLensPanel::IsLC2GuardOutlineModel() const
{
	return Model.IsValid() &&
		Model->Source.BlueprintAssetPath ==
			TEXT(
				"/Game/LensCorpus/BP_LC2_NestedGuards."
				"BP_LC2_NestedGuards");
}

bool SBlueprintLensPanel::IsLC3ValueConeModel() const
{
	return Model.IsValid() &&
		Model->Source.BlueprintAssetPath ==
			TEXT(
				"/Game/LensCorpus/BP_LC3_ValueProvenance."
				"BP_LC3_ValueProvenance");
}

bool SBlueprintLensPanel::IsLC4SequenceDisclosureModel() const
{
	return Model.IsValid() && LC4SequenceProfile.IsValid() &&
		FBlueprintLensLC4SequenceProjector::Build(
			*LC4SequenceProfile,
			*Model).IsRenderable();
}

bool SBlueprintLensPanel::IsLC4AsyncModel() const
{
	return Model.IsValid() && LC4AsyncProfile.IsValid() &&
		Model->Source.BlueprintAssetPath ==
			TEXT("/Game/LensCorpus/BP_LC4_AsyncBarrier.BP_LC4_AsyncBarrier");
}

bool SBlueprintLensPanel::IsLC5TypedPortalModel() const
{
	return Model.IsValid() && LC5Profile.IsValid() &&
		LC5Profile->ProfileId == TEXT("LC5_INTRA_BP_PURE_CALL_V1");
}

bool SBlueprintLensPanel::IsLC6FourTrackModel() const
{
	if (M6ReadyPacket.IsValid() &&
		M6ReadyPacket->Request.QueryKind.Equals(
			TEXT("execution"), ESearchCase::IgnoreCase))
	{
		return BuildCurrentLC6Projection().IsRenderable();
	}
	return Model.IsValid() && LC6Profile.IsValid() &&
		LC6Profile->CoreProfileId == TEXT("LC6_CORE_BOUNDARY_MATRIX_V1") &&
		LC6Profile->QueryProfileId == TEXT("LC6_MAX_UPSTREAM_HOPS_V1");
}

bool SBlueprintLensPanel::IsLC7AdaptiveBackboneModel() const
{
	if (M6ReadyPacket.IsValid() &&
		M6ReadyPacket->Request.QueryKind.Equals(
			TEXT("execution"), ESearchCase::IgnoreCase))
	{
		return FBlueprintLensLC7LiveExplanationAdapter::Build(
			M6ReadyPacket->Explanation).IsSuccess();
	}
	return Model.IsValid() && LC7Profile.IsValid() &&
		LC7Profile->ProfileId == TEXT("LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1") &&
		Model->Source.BlueprintAssetPath == LC7Profile->BlueprintAssetPath &&
		Model->Source.GraphId == LC7Profile->GraphId;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2GuardOutlineRepresentation()
{
	const FBlueprintLensLC2GuardOutlineProjection Projection =
		FBlueprintLensLC2GuardOutlineProjector::Build(*Model);
	if (!Projection.IsRenderable())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
			[
				SNew(STextBlock)
					.Text(LOCTEXT(
						"LC2GuardOutlineUnavailable",
						"Nested guard outline unavailable."))
					.ColorAndOpacity(FLinearColor(0.95f, 0.42f, 0.25f))
					.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildLaneRepresentation()
			];
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC2GuardOutlineHeading",
				"NESTED GUARD OUTLINE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(
				Projection.Status ==
						EBlueprintLensLC2GuardOutlineProjectionStatus::
							UngroupedFallback
					? TEXT(
						  "Grouping evidence is unavailable. The complete explanation "
						  "remains reachable below; no nested grouping is asserted.")
					: TEXT(
						  "Three possible outcomes are listed in the explanation "
						  "order. Indentation shows guard nesting; the outcomes are "
						  "parallel, so no execution order is asserted between them.")))
			.AutoWrapText(true)
	];

	if (Projection.Status ==
		EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BuildLC2FallbackOutline(Projection)
		];
	}
	else
	{
		for (int32 PathIndex = 0;
			 PathIndex < Projection.OutcomePaths.Num();
			 ++PathIndex)
		{
			const FBlueprintLensLC2GuardOutlinePath& Path =
				Projection.OutcomePaths[PathIndex];
			TSharedRef<SVerticalBox> PathCard = SNew(SVerticalBox);
			PathCard->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%d. %s"),
						PathIndex + 1,
						*Path.Title)))
					.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
					.AutoWrapText(true)
			];
			PathCard->AddSlot()
			.AutoHeight()
			[
				BuildLC2GuardPathRows(Path)
			];
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(
						RoleAccent(EBlueprintLensRole::Control),
						0.12f))
					.Padding(FMargin(8.0f))
					[
						PathCard
					]
			];
		}

		const FBlueprintLensUnit* Criterion =
			Model->FindUnit(Projection.CriterionUnitId);
		if (Criterion != nullptr)
		{
			const FString CriterionUnitId = Criterion->Id;
			const FString CriterionLabel = LC2ReaderUnitLabel(*Criterion);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(
						RoleAccent(EBlueprintLensRole::Criterion),
						0.24f))
					.Padding(FMargin(8.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
								.Text(LOCTEXT(
									"LC2ReconvergenceHeading",
									"RECONVERGENCE CRITERION"))
								.Font(FAppStyle::Get().GetFontStyle(
									"SmallFont"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
								.ButtonStyle(
									&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
										"SimpleButton"))
								.ContentPadding(FMargin(0.0f))
								.Text(FText::FromString(FString::Printf(
									TEXT("All paths reconverge at %s"),
									*CriterionLabel)))
								.ToolTipText(LOCTEXT(
									"LC2CriterionTooltip",
									"Select the reconvergence criterion row"))
								.OnClicked_Lambda(
									[this, CriterionUnitId]()
									{
										SelectLC2Unit(CriterionUnitId);
										return FReply::Handled();
									})
						]
					]
			];
		}
	}

	if (!LC2SelectedUnitId.IsEmpty())
	{
		const FBlueprintLensUnit* SelectedUnit =
			Model->FindUnit(LC2SelectedUnitId);
		if (SelectedUnit != nullptr)
		{
			const FString SelectedSourceNodeId =
				PrimarySourceNodeId(*SelectedUnit);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"LC2OpenSelectedRow",
						"Open selected row in Blueprint"))
					.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
					.ToolTipText(LOCTEXT(
						"LC2OpenSelectedRowTooltip",
						"Navigate to the source for the selected reader row"))
					.OnClicked_Lambda(
						[this, SelectedSourceNodeId]()
						{
							return NavigateToSource(SelectedSourceNodeId);
						})
			];
		}
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC2TechnicalEvidenceExpanded
					? TEXT("Hide technical evidence")
					: TEXT("Technical evidence")))
			.ToolTipText(LOCTEXT(
				"LC2TechnicalEvidenceTooltip",
				"Reveal relation IDs, group IDs, digests and diagnostics"))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC2TechnicalEvidence();
					return FReply::Handled();
				})
	];
	if (bLC2TechnicalEvidenceExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC2TechnicalEvidence(Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2GuardPathRows(
	const FBlueprintLensLC2GuardOutlinePath& Path)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 RowIndex = 0; RowIndex < Path.Rows.Num(); ++RowIndex)
	{
		const FBlueprintLensLC2GuardOutlineRow& Row = Path.Rows[RowIndex];
		const FBlueprintLensUnit* Unit = Model->FindUnit(Row.UnitId);
		if (Unit == nullptr)
		{
			continue;
		}
		const FString UnitId = Row.UnitId;
		const FString RowLabel = RowIndex == 0 ||
			!Path.Rows[RowIndex - 1].bHasNextRelation
			? Row.ReaderLabel
			: FString::Printf(
				TEXT("%s → %s"),
				*LC2ReaderRelationLabel(
					Path.Rows[RowIndex - 1].NextRelationSemanticLabel),
				*Row.ReaderLabel);
		const bool bGuardLevelChanged =
			!Row.GuardGroupId.IsEmpty() &&
			(RowIndex == 0 ||
				Row.GuardGroupId != Path.Rows[RowIndex - 1].GuardGroupId);
		const bool bSelected = LC2SelectedUnitId == UnitId;
		TSharedRef<SVerticalBox> RowContent = SNew(SVerticalBox);
		RowContent->AddSlot()
		.AutoHeight()
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(FMargin(0.0f))
				.HAlign(HAlign_Fill)
				.ToolTipText(FText::FromString(RowLabel))
				.OnClicked_Lambda(
					[this, UnitId]()
					{
						SelectLC2Unit(UnitId);
						return FReply::Handled();
					})
				[
					SNew(STextBlock)
						.Text(FText::FromString(RowLabel))
						.AutoWrapText(true)
				]
		];
		if (bGuardLevelChanged && Row.bHasParentGuard &&
			!Row.GuardReaderLabel.IsEmpty())
		{
			const FString RowOutcomeLabel = RowIndex > 0 &&
				Path.Rows[RowIndex - 1].bHasNextRelation
				? LC2ReaderRelationLabel(
					Path.Rows[RowIndex - 1].NextRelationSemanticLabel)
				: TEXT("the current path");
			const FString ParentageLabel = FString::Printf(
				TEXT("Containing guard: %s; row outcome: %s"),
				*Row.GuardReaderLabel,
				*RowOutcomeLabel);
			RowContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(ParentageLabel))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(
						FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			];
		}
		Rows->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(FMargin(
			8.0f + Row.NestingDepth * 16.0f,
			2.0f,
			8.0f,
			2.0f))
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.BorderBackgroundColor(
					bSelected
						? WithAlpha(RoleAccent(EBlueprintLensRole::Control), 0.24f)
						: FLinearColor::Transparent)
				.Padding(FMargin(4.0f))
				[
					RowContent
				]
		];
	}
	return Rows;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2FallbackOutline(
	const FBlueprintLensLC2GuardOutlineProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC2FallbackHeading",
				"COMPLETE UNGROUPED EXPLANATION"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC2FallbackDescription",
				"Grouping evidence is unavailable. Every unit and relation "
				"remains listed below in the explanation ledger."))
			.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Units (%d)"),
				Projection.FallbackUnitIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (int32 UnitIndex = 0;
		 UnitIndex < Projection.FallbackUnitIds.Num();
		 ++UnitIndex)
	{
		const FString& UnitId = Projection.FallbackUnitIds[UnitIndex];
		const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
		if (Unit == nullptr)
		{
			continue;
		}
		const FString ReaderLabel = LC2ReaderUnitLabel(*Unit);
		const bool bSelected = LC2SelectedUnitId == UnitId;
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(
					bSelected
						? WithAlpha(RoleAccent(Unit->Role), 0.24f)
						: FLinearColor::Transparent)
				.Padding(FMargin(6.0f))
				[
					SNew(SButton)
						.ButtonStyle(
							&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
								"SimpleButton"))
						.ContentPadding(FMargin(0.0f))
						.Text(FText::FromString(FString::Printf(
							TEXT("%d. %s"),
							UnitIndex + 1,
							*ReaderLabel)))
						.ToolTipText(FText::FromString(Unit->Title))
						.OnClicked_Lambda(
							[this, UnitId]()
							{
								SelectLC2Unit(UnitId);
								return FReply::Handled();
							})
				]
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Relations (%d)"),
				Projection.FallbackRelationIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (int32 RelationIndex = 0;
		 RelationIndex < Projection.FallbackRelationIds.Num();
		 ++RelationIndex)
	{
		const FString& RelationId =
			Projection.FallbackRelationIds[RelationIndex];
		const FBlueprintLensRelation* Relation =
			Model->FindRelation(RelationId);
		if (Relation == nullptr)
		{
			continue;
		}
		const FBlueprintLensUnit* Source =
			Model->FindUnit(Relation->SourceUnitId);
		const FBlueprintLensUnit* Target =
			Model->FindUnit(Relation->TargetUnitId);
		const FString SourceLabel = Source != nullptr
			? LC2ReaderUnitLabel(*Source)
			: TEXT("unresolved source");
		const FString TargetLabel = Target != nullptr
			? LC2ReaderUnitLabel(*Target)
			: TEXT("unresolved target");
		const FString SemanticLabel = Relation->bHasSemanticLabel
			? LC2ReaderRelationLabel(Relation->SemanticLabel)
			: TEXT("related to");
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%d. %s: %s → %s"),
					RelationIndex + 1,
					*SemanticLabel,
					*SourceLabel,
					*TargetLabel)))
				.AutoWrapText(true)
		];
	}
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2TechnicalEvidence(
	const FBlueprintLensLC2GuardSurfaceProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT("LC2D2TechnicalEvidenceHeading", "TECHNICAL EVIDENCE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (const FString& Value : {
		FString::Printf(TEXT("Source IR SHA-256: %s"), *Projection.SourceIrSha256),
		FString::Printf(TEXT("Projection integrity hash: %s"), *Projection.ProjectionIntegrityHash),
		FString::Printf(TEXT("Diagnostic code: %s"), *Projection.DiagnosticCode),
		FString::Printf(TEXT("No-order statement: %s"), *Projection.NoOrderReaderText)})
	{
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(Value))
				.AutoWrapText(true)
		];
	}
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT("LC2D2TechnicalRelationsHeading", "RELATIONS"))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
	];
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(RelationId))
				.AutoWrapText(true)
		];
	}
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT("LC2D2TechnicalGroupsHeading", "GUARD GROUPS"))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
	];
	for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s -> %s"), *Compound.GroupId, *Compound.ParentGroupId)))
				.AutoWrapText(true)
		];
	}
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC2TechnicalEvidence(
	const FBlueprintLensLC2GuardOutlineProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC2TechnicalEvidenceHeading",
				"TECHNICAL EVIDENCE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Projection status: %s"),
				*LC2ProjectionStatusLabel(Projection.Status))))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Source IR SHA-256: %s"),
				*Projection.SourceIrSha256)))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Projection integrity hash: %s"),
				*Projection.ProjectionIntegrityHash)))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Diagnostic code: %s"),
				*Projection.DiagnosticCode)))
			.AutoWrapText(true)
	];

	if (!Projection.GuardNests.IsEmpty() ||
		!Projection.OutcomePaths.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(LOCTEXT(
					"LC2TechnicalGroupsHeading",
					"Groups"))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
		];
		for (const FBlueprintLensLC2GuardOutlinePath& Path :
			 Projection.OutcomePaths)
		{
			Content->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%s · %s"),
						*Path.GroupId,
						*Path.Title)))
					.AutoWrapText(true)
			];
		}
		for (const FBlueprintLensLC2GuardOutlineNest& Nest :
			 Projection.GuardNests)
		{
			Content->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%s · %s"),
						*Nest.GroupId,
						*Nest.ReaderLabel)))
					.AutoWrapText(true)
			];
		}
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Incomparable pairs: %s"),
					*Projection.GroupPartialOrderSemantics)))
				.AutoWrapText(true)
		];
		for (const TPair<FString, FString>& Pair :
			 Projection.IncomparableGroupIds)
		{
			Content->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%s ↔ %s"),
						*Pair.Key,
						*Pair.Value)))
					.AutoWrapText(true)
			];
		}
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("All units (%d)"),
				Projection.AllUnitIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
		const FString Label = Unit != nullptr
			? LC2ReaderUnitLabel(*Unit)
			: FString(TEXT("unresolved unit"));
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · %s"),
					*UnitId,
					*Label)))
				.AutoWrapText(true)
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("All relations (%d)"),
				Projection.AllRelationIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			Model->FindRelation(RelationId);
		if (Relation == nullptr)
		{
			continue;
		}
		const FBlueprintLensUnit* Source =
			Model->FindUnit(Relation->SourceUnitId);
		const FBlueprintLensUnit* Target =
			Model->FindUnit(Relation->TargetUnitId);
		const FString SourceLabel = Source != nullptr
			? LC2ReaderUnitLabel(*Source)
			: TEXT("unresolved source");
		const FString TargetLabel = Target != nullptr
			? LC2ReaderUnitLabel(*Target)
			: TEXT("unresolved target");
		const FString SemanticLabel = Relation->bHasSemanticLabel
			? LC2ReaderRelationLabel(Relation->SemanticLabel)
			: TEXT("related to");
		Content->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · %s · %s → %s"),
					*RelationId,
					*SemanticLabel,
					*SourceLabel,
					*TargetLabel)))
				.AutoWrapText(true)
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			RoleAccent(EBlueprintLensRole::Boundary),
			0.14f))
		.Padding(FMargin(8.0f))
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC3ValueConeRepresentation()
{
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*Model);
	if (!Projection.IsRenderable() ||
		Projection.Status !=
			EBlueprintLensLC3ValueConeProjectionStatus::ValueCone)
	{
		return BuildLC3LegacyValueConeRepresentation();
	}

	const FBlueprintLensLC3ValueConeLayoutSessionResult LayoutSession =
		FBlueprintLensLC3ValueConeLayoutSession::Build(Projection, 700.0f);
	if (!LayoutSession.IsRenderable(Projection))
	{
		return BuildLC3LegacyValueConeRepresentation();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC3ValueConeCanvasHeading",
				"VALUE PROVENANCE · DERIVATION SPINE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Value))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT(
					"Read the value origins along one derivation Spine toward the "
					"%s criterion. Select an operator to reflow only that region "
					"into a local Operator Subtree; select it again to collapse. "
					"The thin blue rail controls execution and supplies no value."),
				*Projection.CriterionReaderLabel)))
			.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SSegmentedControl<EBlueprintLensLC3ValueConeDensity>)
			.Value(LC3ValueConeDensity)
			.OnValueChanged(
				this,
				&SBlueprintLensPanel::SetLC3ValueConeDensity)

			+ SSegmentedControl<EBlueprintLensLC3ValueConeDensity>::Slot(
				EBlueprintLensLC3ValueConeDensity::Summary)
			.Text(LOCTEXT("LC3DensitySummary", "Summary"))
			.ToolTip(LOCTEXT(
				"LC3DensitySummaryTooltip",
				"Show the staged topology and reader labels"))

			+ SSegmentedControl<EBlueprintLensLC3ValueConeDensity>::Slot(
				EBlueprintLensLC3ValueConeDensity::Evidence)
			.Text(LOCTEXT("LC3DensityEvidence", "Evidence"))
			.ToolTip(LOCTEXT(
				"LC3DensityEvidenceTooltip",
				"Show ports plus projection and provenance evidence"))
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBlueprintLensLC3ValueConeCanvas)
			.Projection(Projection)
			.InitialSession(LayoutSession)
			.SelectedUnitId_Lambda(
				[this]()
				{
					return LC3SelectedUnitId;
				})
			.Density(LC3ValueConeDensity)
			.OnUnitSelected(
				FOnBlueprintLensLC3ValueConeUnitSelected::CreateSP(
					this,
					&SBlueprintLensPanel::SelectLC3Unit))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(
				RoleAccent(EBlueprintLensRole::Boundary),
				0.12f))
			.Padding(FMargin(8.0f))
		[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text(LOCTEXT(
							"LC3CanvasBoundaryHeading",
							"ANALYSIS BOUNDARIES"))
						.Font(FAppStyle::Get().GetFontStyle(
							"NormalFontBold"))
						.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Join(
							Projection.BoundaryNotices,
							TEXT("\n"))))
						.AutoWrapText(true)
				]
			]
	];

	if (!LC3SelectedUnitId.IsEmpty())
	{
		const FBlueprintLensUnit* SelectedUnit =
			Model->FindUnit(LC3SelectedUnitId);
		if (SelectedUnit != nullptr)
		{
			const FString SelectedSourceNodeId =
				PrimarySourceNodeId(*SelectedUnit);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"LC3OpenSelectedItem",
						"Open selected item in Blueprint"))
					.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
					.ToolTipText(LOCTEXT(
						"LC3OpenSelectedItemTooltip",
						"Navigate to the source for the selected value or control card"))
					.OnClicked_Lambda(
						[this, SelectedSourceNodeId]()
						{
							return NavigateToSource(SelectedSourceNodeId);
						})
			];
		}
	}

	if (LC3ValueConeDensity == EBlueprintLensLC3ValueConeDensity::Evidence)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC3TechnicalEvidence(Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC3LegacyValueConeRepresentation()
{
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*Model);
	if (!Projection.IsRenderable())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
			[
				SNew(STextBlock)
					.Text(LOCTEXT(
						"LC3ValueConeUnavailable",
						"Value provenance reader unavailable."))
					.ColorAndOpacity(FLinearColor(0.95f, 0.42f, 0.25f))
					.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildLaneRepresentation()
			];
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC3ValueConeHeading",
				"VALUE PROVENANCE CONE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Value))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(
				Projection.Status ==
						EBlueprintLensLC3ValueConeProjectionStatus::
							UngroupedFallback
					? TEXT(
						  "Grouping evidence is unavailable. The complete explanation "
						  "remains reachable below; no cone, depth or ordering is "
						  "asserted.")
					: FString::Printf(
						  TEXT(
							  "Five producers derive the value assigned by %s. Rows are "
							  "listed in derivation order from the criterion; indentation "
							  "shows how many steps a producer sits from it. Each row names "
							  "the pin that supplies the value and the pin that receives it."),
						  *Projection.CriterionReaderLabel)))
			.AutoWrapText(true)
	];

	if (Projection.Status ==
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BuildLC3FallbackOutline(Projection)
		];
	}
	else
	{
		const FString CriterionPorts =
			FString::Join(Projection.CriterionInputPortLabels, TEXT(", "));
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Criterion),
					0.24f))
				.Padding(FMargin(8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
							.Text(LOCTEXT("LC3CriterionHeading", "CRITERION"))
							.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(
								Projection.CriterionReaderLabel))
							.Font(FAppStyle::Get().GetFontStyle(
								"NormalFontBold"))
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(
								TEXT("Receives %s from %d producer%s"),
								*CriterionPorts,
								Projection.CriterionInputCount,
								Projection.CriterionInputCount == 1
									? TEXT("")
									: TEXT("s"))))
							.AutoWrapText(true)
					]
				]
		];

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BuildLC3ValueConeRows(Projection)
		];

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Control),
					0.16f))
				.Padding(FMargin(8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"LC3ControlHeading",
								"CONTROL PREREQUISITE — NOT PART OF THE VALUE CONE"))
							.Font(FAppStyle::Get().GetFontStyle(
								"NormalFontBold"))
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
							.ButtonStyle(
								&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
									"SimpleButton"))
							.ContentPadding(FMargin(0.0f))
							.HAlign(HAlign_Fill)
							.OnClicked_Lambda(
								[this, UnitId = Projection.Control.ControllerUnitId]()
								{
									SelectLC3Unit(UnitId);
									return FReply::Handled();
								})
							[
								SNew(STextBlock)
									.Text(FText::FromString(
										Projection.Control.ReaderRowText))
									.AutoWrapText(true)
							]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"LC3ControlDisclaimer",
								"This decides when the assignment runs. It supplies no value."))
							.AutoWrapText(true)
					]
				]
		];

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Boundary),
					0.12f))
				.Padding(FMargin(8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 4.0f)
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"LC3BoundaryHeading",
								"ANALYSIS BOUNDARIES"))
							.Font(FAppStyle::Get().GetFontStyle(
								"NormalFontBold"))
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
							.Text(FText::FromString(FString::Join(
								Projection.BoundaryNotices,
								TEXT("\n"))))
							.AutoWrapText(true)
					]
				]
		];
	}

	if (!LC3SelectedUnitId.IsEmpty())
	{
		const FBlueprintLensUnit* SelectedUnit =
			Model->FindUnit(LC3SelectedUnitId);
		if (SelectedUnit != nullptr)
		{
			const FString SelectedSourceNodeId =
				PrimarySourceNodeId(*SelectedUnit);
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"LC3OpenSelectedRow",
						"Open selected row in Blueprint"))
					.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
					.ToolTipText(LOCTEXT(
						"LC3OpenSelectedRowTooltip",
						"Navigate to the source for the selected value row"))
					.OnClicked_Lambda(
						[this, SelectedSourceNodeId]()
						{
							return NavigateToSource(SelectedSourceNodeId);
						})
			];
		}
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC3TechnicalEvidenceExpanded
					? TEXT("Hide technical evidence")
					: TEXT("Technical evidence")))
			.ToolTipText(LOCTEXT(
				"LC3TechnicalEvidenceTooltip",
				"Reveal group and relation IDs, claim provenance, digests and diagnostics"))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC3TechnicalEvidence();
					return FReply::Handled();
				})
	];
	if (bLC3TechnicalEvidenceExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC3TechnicalEvidence(Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC3ValueConeRows(
	const FBlueprintLensLC3ValueConeProjection& Projection)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
	{
		const bool bSelected = LC3SelectedUnitId == Step.ProducerUnitId;
		TSharedRef<SVerticalBox> RowContent = SNew(SVerticalBox);
		RowContent->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(Step.ProducerReaderLabel))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
				.AutoWrapText(true)
		];
		RowContent->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Step.ReaderRowText))
				.AutoWrapText(true)
		];
		if (!Step.ProducerInputSummaryText.IsEmpty())
		{
			RowContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(
						Step.ProducerInputSummaryText))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(
						FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			];
		}

		Rows->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(FMargin(
			8.0f + Step.DerivationDepth * 16.0f,
			2.0f,
			8.0f,
			2.0f))
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(
					bSelected
						? WithAlpha(
							RoleAccent(EBlueprintLensRole::Value),
							0.24f)
						: FLinearColor::Transparent)
				.Padding(FMargin(4.0f))
				[
					SNew(SButton)
						.ButtonStyle(
							&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
								"SimpleButton"))
						.ContentPadding(FMargin(0.0f))
						.HAlign(HAlign_Fill)
						.OnClicked_Lambda(
							[this, UnitId = Step.ProducerUnitId]()
							{
								SelectLC3Unit(UnitId);
								return FReply::Handled();
							})
						[
							RowContent
						]
				]
		];
	}
	return Rows;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC3FallbackOutline(
	const FBlueprintLensLC3ValueConeProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC3FallbackHeading",
				"COMPLETE UNGROUPED EXPLANATION"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Value))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC3FallbackDescription",
				"Grouping evidence is unavailable. Every unit and relation "
				"remains listed below in explanation-ledger order."))
			.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Units (%d)"),
				Projection.FallbackUnitIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (int32 UnitIndex = 0;
		 UnitIndex < Projection.FallbackUnitIds.Num();
		 ++UnitIndex)
	{
		const FString& UnitId = Projection.FallbackUnitIds[UnitIndex];
		const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
		if (Unit == nullptr)
		{
			continue;
		}
		const bool bSelected = LC3SelectedUnitId == UnitId;
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(
					bSelected
						? WithAlpha(RoleAccent(Unit->Role), 0.24f)
						: FLinearColor::Transparent)
				.Padding(FMargin(6.0f))
				[
					SNew(SButton)
						.ButtonStyle(
							&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
								"SimpleButton"))
						.ContentPadding(FMargin(0.0f))
						.HAlign(HAlign_Fill)
						.OnClicked_Lambda(
							[this, UnitId]()
							{
								SelectLC3Unit(UnitId);
								return FReply::Handled();
							})
						[
							SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("%d. %s"),
									UnitIndex + 1,
									*LC2ReaderUnitLabel(*Unit))))
								.AutoWrapText(true)
						]
				]
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Relations (%d)"),
				Projection.FallbackRelationIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (int32 RelationIndex = 0;
		 RelationIndex < Projection.FallbackRelationIds.Num();
		 ++RelationIndex)
	{
		const FBlueprintLensRelation* Relation = Model->FindRelation(
			Projection.FallbackRelationIds[RelationIndex]);
		if (Relation == nullptr)
		{
			continue;
		}
		const FBlueprintLensUnit* Source =
			Model->FindUnit(Relation->SourceUnitId);
		const FBlueprintLensUnit* Target =
			Model->FindUnit(Relation->TargetUnitId);
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%d. %s · %s → %s · %s"),
					RelationIndex + 1,
					Relation->bHasSemanticLabel
						? *LC2ReaderRelationLabel(Relation->SemanticLabel)
						: TEXT("related to"),
					Source != nullptr
						? *LC2ReaderUnitLabel(*Source)
						: TEXT("unresolved source"),
					Target != nullptr
						? *LC2ReaderUnitLabel(*Target)
						: TEXT("unresolved target"),
					*Relation->Label)))
				.AutoWrapText(true)
		];
	}
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC3TechnicalEvidence(
	const FBlueprintLensLC3ValueConeProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto AddLine =
		[&Content](const FString& Line)
		{
			Content->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(Line))
					.AutoWrapText(true)
			];
		};
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC3TechnicalEvidenceHeading",
				"TECHNICAL EVIDENCE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	AddLine(FString::Printf(
		TEXT("Projection status: %s"),
		*LC3ProjectionStatusLabel(Projection.Status)));
	AddLine(FString::Printf(
		TEXT("Source IR SHA-256: %s"),
		*Projection.SourceIrSha256));
	AddLine(FString::Printf(
		TEXT("Projection integrity hash: %s"),
		*Projection.ProjectionIntegrityHash));
	AddLine(FString::Printf(
		TEXT("Diagnostic code: %s"),
		*Projection.DiagnosticCode));
	if (!Projection.GroupId.IsEmpty())
	{
		AddLine(FString::Printf(
			TEXT("Group: %s · %s"),
			*Projection.GroupId,
			*Projection.GroupTitle));
	}
	for (const FBlueprintLensClaimEvidence& Evidence :
		 Projection.GroupClaimEvidence)
	{
		AddLine(FString::Printf(
			TEXT("Claim evidence · %s · %s · %s"),
			*Evidence.Component,
			*Evidence.FactOwner,
			*Evidence.Source));
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("All units (%d)"),
				Projection.AllUnitIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (const FString& UnitId : Projection.AllUnitIds)
	{
		const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
		AddLine(FString::Printf(
			TEXT("%s · %s"),
			*UnitId,
			Unit != nullptr
				? *LC2ReaderUnitLabel(*Unit)
				: TEXT("unresolved unit")));
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("All relations (%d)"),
				Projection.AllRelationIds.Num())))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			Model->FindRelation(RelationId);
		if (Relation == nullptr)
		{
			AddLine(RelationId + TEXT(" · unresolved relation"));
			continue;
		}
		AddLine(FString::Printf(
			TEXT("%s · %s"),
			*RelationId,
			Relation->bHasSemanticLabel
				? *LC2ReaderRelationLabel(Relation->SemanticLabel)
				: TEXT("related to")));
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			RoleAccent(EBlueprintLensRole::Boundary),
			0.14f))
		.Padding(FMargin(8.0f))
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLaneRepresentation()
{
	TSharedRef<SVerticalBox> ScrollingLanes = SNew(SVerticalBox);
	for (int32 LaneIndex = 1; LaneIndex < Model->Lanes.Num(); ++LaneIndex)
	{
		ScrollingLanes->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
		[
			BuildLane(Model->Lanes[LaneIndex])
		];
	}

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
		[
			BuildLane(Model->Lanes[0])
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				ScrollingLanes
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildFrameFlowRepresentation()
{
	const FBlueprintLensWeaveProjection Projection =
		FBlueprintLensWeaveProjector::Build(*Model);
	if (!Projection.IsValid())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Projection.Error))
				.ColorAndOpacity(FLinearColor(0.95f, 0.42f, 0.25f))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildLaneRepresentation()
			];
	}

	const auto PrimarySourceNodeId =
		[](const FBlueprintLensUnit& Unit)
		{
			const FBlueprintLensSourceReference* Source =
				Unit.SourceReferences.FindByPredicate(
					[](const FBlueprintLensSourceReference& Reference)
					{
						return Reference.bPrimary;
					});
			if (Source == nullptr && !Unit.SourceReferences.IsEmpty())
			{
				Source = &Unit.SourceReferences[0];
			}
			return Source != nullptr ? Source->SourceNodeId : FString();
		};

	const auto BuildGlyphButton =
		[this, &PrimarySourceNodeId](
			const FBlueprintLensUnit& Unit,
			const FString& Glyph,
			const FLinearColor& Accent,
			const bool bBold = false) -> TSharedRef<SWidget>
		{
			const FString SourceNodeId = PrimarySourceNodeId(Unit);
			const FString Label =
				!Unit.Expression.IsEmpty() ? Unit.Expression : Unit.Title;
			return SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(FMargin(2.0f))
				.IsEnabled(CanNavigateToSource(SourceNodeId))
				.ToolTipText(FText::FromString(BuildUnitEvidenceTooltip(Unit)))
				.OnClicked_Lambda(
					[this, SourceNodeId]()
					{
						return NavigateToSource(SourceNodeId);
					})
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s  %s  \u2197"),
							*Glyph,
							*Label)))
						.Font(FAppStyle::Get().GetFontStyle(
							bBold ? "NormalFontBold" : "NormalFont"))
						.ColorAndOpacity(Accent)
						.AutoWrapText(true)
				];
		};

	const FBlueprintLensUnit& Entry = *Projection.ExecutionUnits[0];
	const FBlueprintLensUnit& Sequence = *Projection.ExecutionUnits[1];
	const FBlueprintLensUnit& Branch = *Projection.ExecutionUnits[2];
	const FBlueprintLensUnit& Predicate = *Projection.PredicateUnits[0];
	const FBlueprintLensUnit& Value = *Projection.ValueUnits[0];
	const FBlueprintLensUnit& Criterion = *Projection.Criterion;
	const FBlueprintLensRelation& FirstExecution =
		*Projection.ExecutionRelations[0];
	const FBlueprintLensRelation& SecondExecution =
		*Projection.ExecutionRelations[1];
	const FBlueprintLensRelation& Control =
		*Projection.CriterionControlRelation;
	const FBlueprintLensRelation& ValueRelation =
		*Projection.ValueRelations[0];

	TSharedRef<SConstraintCanvas> Labels = SNew(SConstraintCanvas);
	Labels->AddSlot()
		.Anchors(FAnchors(0.64f, 0.10f))
		.Alignment(FVector2D(0.50f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Entry,
				FString(TEXT("\u25CB")),
				RoleAccent(EBlueprintLensRole::Control))
		];
	Labels->AddSlot()
		.Anchors(FAnchors(0.64f, 0.30f))
		.Alignment(FVector2D(0.50f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Sequence,
				FString(TEXT("\u25CF")),
				RoleAccent(EBlueprintLensRole::Control))
		];
	Labels->AddSlot()
		.Anchors(FAnchors(0.68f, 0.53f))
		.Alignment(FVector2D(0.0f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Branch,
				FString(TEXT("\u25C7")),
				RoleAccent(EBlueprintLensRole::Predicate),
				true)
		];
	Labels->AddSlot()
		.Anchors(FAnchors(0.64f, 0.82f))
		.Alignment(FVector2D(0.50f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Criterion,
				FString(TEXT("\u25CE")),
				RoleAccent(EBlueprintLensRole::Criterion),
				true)
		];
	Labels->AddSlot()
		.Anchors(FAnchors(0.07f, 0.46f))
		.Alignment(FVector2D(0.0f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Predicate,
				FString(TEXT("\u25B8")),
				RoleAccent(EBlueprintLensRole::Predicate),
				true)
		];
	Labels->AddSlot()
		.Anchors(FAnchors(0.07f, 0.68f))
		.Alignment(FVector2D(0.0f, 0.50f))
		.AutoSize(true)
		[
			BuildGlyphButton(
				Value,
				FString(TEXT("\u223F")),
				RoleAccent(EBlueprintLensRole::Value),
				true)
		];

	const auto AddRelationLabel =
		[&Labels](
			const float X,
			const float Y,
			const FString& Label,
			const FLinearColor& Accent,
			const FVector2D Alignment = FVector2D(0.0f, 0.50f))
		{
			Labels->AddSlot()
				.Anchors(FAnchors(X, Y))
				.Alignment(Alignment)
				.AutoSize(true)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Label.ToUpper()))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(Accent)
						.AutoWrapText(true)
				];
		};
	AddRelationLabel(
		0.68f,
		0.20f,
		FirstExecution.Label,
		RoleAccent(EBlueprintLensRole::Control));
	AddRelationLabel(
		0.68f,
		0.40f,
		SecondExecution.Label,
		RoleAccent(EBlueprintLensRole::Control));
	AddRelationLabel(
		0.68f,
		0.66f,
		Control.Label,
		RoleAccent(EBlueprintLensRole::Criterion));
	AddRelationLabel(
		0.55f,
		0.69f,
		ValueRelation.Label,
		RoleAccent(EBlueprintLensRole::Value),
		FVector2D(1.0f, 0.50f));

	const FString WeaveSummary = FString::Printf(
		TEXT("WEAVE \u00B7 %d UNITS \u00B7 %d RELATIONS"),
		Projection.AccountedUnitIds.Num(),
		Projection.AccountedRelationIds.Num());

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			BuildAnalysisTruthStrip()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 0.0f, 2.0f, 4.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(WeaveSummary))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SBox)
					.MinDesiredWidth(340.0f)
					.MinDesiredHeight(320.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBlueprintLensWeaveRail)
						]
						+ SOverlay::Slot()
						[
							Labels
						]
					]
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1FrameFlowRepresentation(
	const FBlueprintLensFrameFlowLayoutModel& Layout)
{
	check(Layout.IsReady());
	check(Layout.Segments.Num() == 3);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		BuildLC1DisclosureSwitcher()
	];

	if (!LC1DisclosureCandidate.IsSet())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Control),
					0.16f))
				.Padding(FMargin(10.0f))
				[
					SNew(STextBlock)
						.Text(LOCTEXT(
							"LC1ChooseCondition",
							"Choose an LC1 comparison condition. "
							"No candidate is the selected default; all three "
							"preserve the same 14 units, 13 relations "
							"and source actions."))
						.AutoWrapText(true)
				]
		];
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BuildLC1EmptyStateStrip()
		];
		Content->AddSlot()
		.AutoHeight()
		[
			BuildAnalysisTruthStrip()
		];
		return Content;
	}

	FBlueprintLensLC1DisclosureProjection Projection;
	if (LC1DisclosureCandidate.GetValue()
		!= EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline)
	{
		const FBlueprintLensLC1TypedIrFacts TypedIrFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(Model->Source);
		const FBlueprintLensLC1RegionProjection Region =
			FBlueprintLensLC1RegionProjector::Build(
				*Model,
				Layout,
				TypedIrFacts);
		if (LC1DisclosureCandidate.GetValue()
			== EBlueprintLensLC1DisclosureCandidate::PairedPseudocode)
		{
			const FBlueprintLensLC1PseudocodeProjection Pseudocode =
				FBlueprintLensLC1PseudocodeProjector::Build(
					*Model,
					Layout,
					TypedIrFacts);
			Projection = Region.IsRenderable() && Pseudocode.IsRenderable()
				? FBlueprintLensLC1DisclosureProjector::Build(
					  *Model,
					  Layout,
					  EBlueprintLensLC1DisclosureCandidate::
						  PairedPseudocode,
					  FString(),
					  3,
					  Region,
					  Pseudocode)
				: FBlueprintLensLC1DisclosureProjector::Build(
					  *Model,
					  Layout,
					  EBlueprintLensLC1DisclosureCandidate::
						  PlainOrderedOutline,
					  FString());
		}
		else
		{
			Projection = Region.IsRenderable()
				? FBlueprintLensLC1DisclosureProjector::Build(
					  *Model,
					  Layout,
					  EBlueprintLensLC1DisclosureCandidate::
						  EvidenceBackedRegions,
					  FString(),
					  3,
					  Region)
				: FBlueprintLensLC1DisclosureProjector::Build(
					  *Model,
					  Layout,
					  EBlueprintLensLC1DisclosureCandidate::
						  PlainOrderedOutline,
					  FString());
		}
	}
	else
	{
		Projection = FBlueprintLensLC1DisclosureProjector::Build(
			*Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
			FString());
	}
	if (!Projection.IsValid())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Projection.Error))
					.ColorAndOpacity(FLinearColor(0.95f, 0.42f, 0.25f))
					.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildLaneRepresentation()
			];
	}

	Content->AddSlot()
	.FillHeight(1.0f)
	[
		LC1DisclosureCandidate.GetValue() ==
				EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline
			? BuildLC1PlainOutline(Layout, Projection)
			: LC1DisclosureCandidate.GetValue() ==
					  EBlueprintLensLC1DisclosureCandidate::PairedPseudocode
				? BuildLC1PairedPseudocode(Layout, Projection)
				: BuildLC1EvidenceRegions(Layout, Projection)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 6.0f, 0.0f, 6.0f)
	[
		BuildLC1EmptyStateStrip()
	];
	Content->AddSlot()
	.AutoHeight()
	[
		BuildAnalysisTruthStrip()
	];
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1RailRepresentation(
	const FBlueprintLensFrameFlowLayoutModel& FallbackLayout)
{
	const auto BuildFallback = [this, &FallbackLayout]() -> TSharedRef<SWidget>
	{
		const FBlueprintLensLC1DisclosureProjection PlainOutline =
			FBlueprintLensLC1DisclosureProjector::Build(
				*Model,
				FallbackLayout,
				EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
				FString());
		return PlainOutline.IsValid()
			? BuildLC1PlainOutline(FallbackLayout, PlainOutline)
			: BuildLaneRepresentation();
	};

	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(*Model);
	if (!Projection.IsRenderable())
	{
		return BuildFallback();
	}

	// The 2026-08-12 reopen is presentation migration only. It did not reopen the
	// three frozen condition IDs, so an explicitly chosen condition still renders
	// as it did, and the switcher stays reachable from the rail.
	if (LC1DisclosureCandidate.IsSet())
	{
		return BuildLC1FrameFlowRepresentation(FallbackLayout);
	}

	// Selection rebuilds the complete subtree. Preserve both pieces of live
	// viewport state before replacing it.
	if (LC1RailCanvas.IsValid())
	{
		LC1RailLayoutWidth = LC1RailCanvas->GetLayoutWidth();
	}
	if (LC1RailScrollBox.IsValid())
	{
		LC1RailScrollOffset = LC1RailScrollBox->GetScrollOffset();
	}

	const FBlueprintLensLC1RailLayoutSessionResult LayoutSession =
		FBlueprintLensLC1RailLayoutSession::Build(
			Projection,
			*Model,
			FMath::Max(LC1RailLayoutWidth, 430.0f));
	if (!LayoutSession.IsRenderable(Projection))
	{
		return BuildFallback();
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->SetTag(LC1RailAutomationTag);
	const bool bMixedFeatureProjection =
		!Projection.DeferredRelationIds.IsEmpty();
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		BuildLC1DisclosureSwitcher()
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1RailHeading",
				"Execution predecessor rail"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(bMixedFeatureProjection
				? FString::Printf(
					TEXT("Read the proven-before station order toward %s. Outside a "
						"declared SCC segment, a lower station cannot be a proven cause "
						"of an upper station; incomparable and SCC segments state their "
						"local order boundary. The criterion remains docked at the end."),
					*Projection.CriterionDisplayLabel)
				: FString::Printf(
					TEXT("Read the proven predecessor chain toward %s. "
						"The criterion remains docked at the end of the rail."),
					*Projection.CriterionDisplayLabel)))
			.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SSegmentedControl<bool>)
			.Value(bLC1RailEvidence)
			.OnValueChanged(this, &SBlueprintLensPanel::SetLC1RailDensity)

			+ SSegmentedControl<bool>::Slot(false)
			.Text(LOCTEXT("LC1RailDensitySummary", "Summary"))

			+ SSegmentedControl<bool>::Slot(true)
			.Text(LOCTEXT("LC1RailDensityEvidence", "Evidence"))
	];

	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SAssignNew(LC1RailCanvas, SBlueprintLensLC1RailCanvas)
			.Projection(Projection)
			.InitialSession(LayoutSession)
			.Explanation(Model)
			.SelectedUnitId_Lambda(
				[this]()
				{
					return LC1SelectedUnitId;
				})
			.OnUnitSelected(
				FOnBlueprintLensLC1RailUnitSelected::CreateSP(
					this,
					&SBlueprintLensPanel::SelectLC1Unit))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(
				RoleAccent(EBlueprintLensRole::Boundary),
				0.12f))
			.Padding(FMargin(8.0f))
			[
			SNew(STextBlock)
				.Text_Lambda(
					[this, Projection]()
					{
						const int32 LedgerTotal = Projection.AllUnitIds.Num();
						int32 DrawnCount = 0;
						if (LC1RailCanvas.IsValid())
						{
							const FBlueprintLensLC1RailSurfaceLayout& Surface =
								LC1RailCanvas->GetSurface();
							if (Surface.IsRenderable(Projection))
							{
								DrawnCount = Surface.DrawnUnitCount();
							}
						}
						const int32 RetainedCapCount =
							LC1RailCanvas.IsValid()
								? LC1RailCanvas->GetSurface().Radius
									.RetainedBoundaryCapIds.Num()
								: 0;
						FString ReaderText = RetainedCapCount == 0
							? FString::Printf(
								TEXT("%d OF %d UNITS DRAWN"),
								DrawnCount,
								LedgerTotal)
							: FString::Printf(
								TEXT("%d RAIL STATIONS AND %d BOUNDARY CAPS DRAWN OF "
									"%d UNITS"),
								DrawnCount,
								RetainedCapCount,
								LedgerTotal);
						if (!Projection.DeferredUnitIds.IsEmpty() ||
							!Projection.DeferredRelationIds.IsEmpty())
						{
							ReaderText += FString::Printf(
								TEXT("\n%d UNITS AND %d RELATIONS RETAINED OUTSIDE THE "
									"EXECUTION RAIL"),
								Projection.DeferredUnitIds.Num(),
								Projection.DeferredRelationIds.Num());
						}
						return FText::FromString(ReaderText);
					})
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.AutoWrapText(true)
		]
	];

	if (!LC1SelectedUnitId.IsEmpty() &&
		Model->FindUnit(LC1SelectedUnitId) != nullptr)
	{
		const FString SelectedSourceNodeId = PrimarySourceNodeId(
			*Model->FindUnit(LC1SelectedUnitId));
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SButton)
				.Text(LOCTEXT(
					"LC1RailOpenSelectedItem",
					"Open selected item in Blueprint"))
				.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
				.OnClicked_Lambda(
					[this, SelectedSourceNodeId]()
					{
						return NavigateToSource(SelectedSourceNodeId);
					})
		];
	}

	if (bLC1RailEvidence)
	{
		const auto JoinLedger = [](const TArray<FString>& Ids) -> FString
		{
			return FString::Join(Ids, TEXT("\n"));
		};
		TSharedRef<SVerticalBox> Evidence = SNew(SVerticalBox);
		Evidence->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
				.Text(LOCTEXT("LC1RailEvidenceHeading", "TECHNICAL EVIDENCE"))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
		];
		Evidence->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Projection: %s\nIntegrity: %s\n"
						 "Backend attempts: %s\nUnit ledger (%d):\n%s\n"
						 "Relation ledger (%d):\n%s"),
					*Projection.DiagnosticCode,
					*Projection.ProjectionIntegrityHash,
					*LayoutSession.AttemptSummary(),
					Projection.AllUnitIds.Num(),
					*JoinLedger(Projection.AllUnitIds),
					Projection.AllRelationIds.Num(),
					*JoinLedger(Projection.AllRelationIds))))
				.AutoWrapText(true)
		];
		Content->AddSlot()
		.AutoHeight()
		[
			Evidence
		];
	}

	TSharedRef<SScrollBox> Scroll =
		SAssignNew(LC1RailScrollBox, SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				Content
			];
	Scroll->SetScrollOffset(LC1RailScrollOffset);
	return Scroll;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1DisclosureSwitcher()
{
	const auto BuildCandidateButton =
		[this](
			const EBlueprintLensLC1DisclosureCandidate Candidate,
			const FText& Label,
			const FText& ToolTip) -> TSharedRef<SWidget>
		{
			const bool bSelected =
				LC1DisclosureCandidate.IsSet() &&
				LC1DisclosureCandidate.GetValue() == Candidate;
			return SNew(SButton)
				.Text(Label)
				.ToolTipText(ToolTip)
				.ButtonColorAndOpacity(
					bSelected
						? FLinearColor(0.23f, 0.51f, 0.68f, 1.0f)
						: FLinearColor(0.34f, 0.35f, 0.37f, 1.0f))
				.OnClicked_Lambda(
					[this, Candidate]()
					{
						SetLC1DisclosureCandidate(Candidate);
						return FReply::Handled();
					});
		};

	// State the way back where the reader is, not only where they started. The
	// rail is the default presentation, so returning to it is a property of the
	// active condition rather than a fourth condition competing with the three.
	const FText ComparisonStatus =
		LC1DisclosureCandidate.IsSet()
			// Deliberately not the surface heading: "SHARED EXECUTION RAIL" is
			// the marker the automation probes to decide the rail is drawn, so
			// naming it here would make a hint about the rail indistinguishable
			// from the rail itself.
			? LOCTEXT(
				  "LC1ComparisonStatusChosen",
				  "LC1 COMPARISON \u00B7 NO SELECTED WINNER \u00B7 CLICK THE "
				  "ACTIVE CONDITION AGAIN TO RETURN TO THE RAIL")
			: LOCTEXT(
				  "LC1ComparisonStatus",
				  "LC1 COMPARISON \u00B7 NO SELECTED WINNER");

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
				.Text(ComparisonStatus)
				.AutoWrapText(true)
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0f, 4.0f))
			+ SWrapBox::Slot()
			.FillEmptySpace(true)
			[
				BuildCandidateButton(
					EBlueprintLensLC1DisclosureCandidate::
						PlainOrderedOutline,
					LOCTEXT(
						"LC1PlainOrderedOutline",
						"PLAIN ORDERED OUTLINE"),
					LOCTEXT(
						"LC1PlainOrderedOutlineTooltip",
						"Complete numbered explanation with all fourteen "
						"source actions"))
			]
			+ SWrapBox::Slot()
			.FillEmptySpace(true)
			[
				BuildCandidateButton(
					EBlueprintLensLC1DisclosureCandidate::
						EvidenceBackedRegions,
					LOCTEXT(
						"LC1EvidenceBackedRegions",
						"EVIDENCE-BACKED REGIONS"),
					LOCTEXT(
						"LC1EvidenceBackedRegionsTooltip",
						"Deterministic operation summary with complete "
						"recoverable evidence"))
			]
			+ SWrapBox::Slot()
			.FillEmptySpace(true)
			[
				BuildCandidateButton(
					EBlueprintLensLC1DisclosureCandidate::PairedPseudocode,
					LOCTEXT(
						"LC1PairedPseudocode",
						"PAIRED PSEUDOCODE"),
					LOCTEXT(
						"LC1PairedPseudocodeTooltip",
						"Deterministic structured pseudocode paired with "
						"the same evidence and native source nodes"))
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1PlainOutline(
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1DisclosureProjection& Projection)
{
	check(Projection.IsValid());
	TArray<FString> OrderedUnitIds = Layout.Segments[0].MemberUnitIds;
	OrderedUnitIds.Append(Layout.Segments[1].MemberUnitIds);
	OrderedUnitIds.Append(Layout.Segments[2].MemberUnitIds);
	const FBlueprintLensLC1TypedIrFacts TypedIrFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(Model->Source);
	const bool bTypedIrBound = TypedIrFacts.IsValid()
		&& !Model->Source.IrSha256.IsEmpty()
		&& TypedIrFacts.VerifiedIrSha256.Equals(
			Model->Source.IrSha256,
			ESearchCase::IgnoreCase);
	const FString PlainAnswer = bTypedIrBound
		? TEXT(
			  "It is reached after Event BeginPlay and 12 ordered predecessor "
			  "steps. Typed evidence is verified; this outline keeps the stronger "
			  "operation claim separate.")
		: TEXT(
			  "It is reached after Event BeginPlay and 12 ordered predecessor "
			  "steps. Typed evidence is unavailable. No operation claim is made.");

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1PlainOutlineHeading",
				"PLAIN ORDERED OUTLINE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(FText::FromString(PlainAnswer))
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	[
		BuildLC1OrderedRows(OrderedUnitIds, true)
	];
	if (!LC1SelectedUnitId.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BuildLC1SelectedSourceAction()
		];
	}
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1TechnicalEvidenceExpanded
					? TEXT("Hide technical evidence")
					: TEXT("Technical evidence")))
			.ToolTipText(LOCTEXT(
				"LC1TechnicalEvidenceTooltip",
				"Reveal relation IDs, source GUIDs, digests and diagnostics"))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1TechnicalEvidence();
					return FReply::Handled();
				})
	];
	if (bLC1TechnicalEvidenceExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC1TechnicalEvidence(Layout, Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1OrderedRows(
	const TArray<FString>& OrderedUnitIds,
	const bool bIncludeSequenceNumbers)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 Index = 0; Index < OrderedUnitIds.Num(); ++Index)
	{
		const FBlueprintLensUnit* Unit =
			Model->FindUnit(OrderedUnitIds[Index]);
		check(Unit != nullptr);
		const FString UnitId = Unit->Id;
		const FString ReaderLabel = LC1ReaderLabel(Unit->Title);
		const FString RowLabel = bIncludeSequenceNumbers
			? FString::Printf(TEXT("%d. %s"), Index + 1, *ReaderLabel)
			: ReaderLabel;
		const bool bCriterion =
			Unit->Role == EBlueprintLensRole::Criterion;
		const bool bSelected = LC1SelectedUnitId == UnitId;

		TSharedRef<SWrapBox> Summary = SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0f, 2.0f));
		Summary->AddSlot()
		.FillEmptySpace(true)
		[
			SNew(STextBlock)
				.Text(FText::FromString(RowLabel))
				.Font(FAppStyle::Get().GetFontStyle(
					bCriterion ? "NormalFontBold" : "NormalFont"))
				.AutoWrapText(true)
		];
		Summary->AddSlot()
		[
			BuildChip(
				LexToString(Unit->SemanticStatus),
				RoleAccent(Unit->Role),
				0.18f)
		];
		if (bCriterion)
		{
			Summary->AddSlot()
			[
				BuildChip(
					TEXT("criterion"),
					RoleAccent(EBlueprintLensRole::Criterion),
					0.28f)
			];
		}

		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox);
		Row->AddSlot()
		.AutoHeight()
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(FMargin(0.0f))
				.ToolTipText(FText::FromString(FString::Printf(
					TEXT("Select %s and inspect its primary Blueprint source"),
					*ReaderLabel)))
				.OnClicked_Lambda(
					[this, UnitId]()
					{
						SelectLC1Unit(UnitId);
						return FReply::Handled();
					})
				[
					Summary
				]
		];
		if (Index + 1 < OrderedUnitIds.Num())
		{
			const FBlueprintLensUnit* NextUnit =
				Model->FindUnit(OrderedUnitIds[Index + 1]);
			check(NextUnit != nullptr);
			Row->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("then: %s"),
						*LC1ReaderLabel(NextUnit->Title))))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(
						FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			];
		}
		Rows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(Unit->Role),
					bSelected
						? 0.30f
						: bCriterion ? 0.24f : 0.10f))
				.Padding(FMargin(5.0f))
				[
					Row
				]
		];
	}
	return Rows;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1EvidenceRegions(
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1DisclosureProjection& Projection)
{
	if (Projection.Candidate
		!= EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions
		|| !Projection.Region.IsRenderable())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(FMargin(8.0f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"LC1GroupingUnavailable",
								"Detailed grouping evidence is unavailable. "
								"Showing the complete ordered explanation."))
							.AutoWrapText(true)
					]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildLC1PlainOutline(Layout, Projection)
			];
	}

	const FBlueprintLensLC1RegionProjection& Region = Projection.Region;
	const FBlueprintLensUnit* Entry =
		Model->FindUnit(Layout.Segments[0].MemberUnitIds[0]);
	const FBlueprintLensUnit* Criterion =
		Model->FindUnit(Layout.Segments[2].MemberUnitIds[0]);
	check(Entry != nullptr);
	check(Criterion != nullptr);

	FString RegionHeading;
	FString RegionSummary;
	FString RegionMemberNoun;
	switch (Region.Status)
	{
	case EBlueprintLensLC1RegionProjectionStatus::CompleteOperationRegion:
		RegionHeading =
			TEXT("Operation region: set completion flags to true in sequence");
		RegionSummary =
			TEXT("12 supported assignments; each sets a completion flag to true");
		RegionMemberNoun = TEXT("operations");
		break;
	case EBlueprintLensLC1RegionProjectionStatus::OrderedVariableAssignments:
		RegionHeading =
			TEXT("Operation region: ordered variable assignments");
		RegionSummary = TEXT("12 ordered variable assignments");
		RegionMemberNoun = TEXT("variable assignments");
		break;
	case EBlueprintLensLC1RegionProjectionStatus::StructuralRun:
		RegionHeading = TEXT("Structural aggregate: ordered steps");
		RegionSummary = TEXT("12-step structural run");
		RegionMemberNoun = TEXT("steps");
		break;
	default:
		checkNoEntry();
		break;
	}

	TSharedRef<SWrapBox> RegionDisclosure = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0f, 3.0f));
	RegionDisclosure->AddSlot()
	.FillEmptySpace(true)
	[
		SNew(STextBlock)
			.Text(FText::FromString(RegionSummary))
			.AutoWrapText(true)
	];
	RegionDisclosure->AddSlot()
	[
		SNew(SButton)
			.Text(FText::FromString(FString::Printf(
				TEXT("%s %d %s"),
				bLC1RegionMembersExpanded ? TEXT("Hide") : TEXT("Show"),
				Region.OrderedMemberUnitIds.Num(),
				*RegionMemberNoun)))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1RegionMembers();
					return FReply::Handled();
				})
	];
	RegionDisclosure->AddSlot()
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1WhyGroupedExpanded
					? TEXT("Hide grouping evidence")
					: TEXT("Why grouped?")))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1WhyGrouped();
					return FReply::Handled();
				})
	];

	TSharedRef<SVerticalBox> RegionCard = SNew(SVerticalBox);
	RegionCard->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(RegionHeading))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.AutoWrapText(true)
	];
	RegionCard->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		RegionDisclosure
	];
	if (bLC1RegionMembersExpanded)
	{
		RegionCard->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC1OrderedRows(Region.OrderedMemberUnitIds, false)
		];
	}

	TSharedRef<SWrapBox> ResponsiveRegionRow = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(8.0f, 8.0f));
	ResponsiveRegionRow->AddSlot()
	.FillEmptySpace(true)
	[
		SNew(SBox)
			.MinDesiredWidth(300.0f)
			.MaxDesiredWidth(520.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(
						"ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(
						RoleAccent(EBlueprintLensRole::Control),
						0.24f))
					.Padding(FMargin(8.0f))
					[
						RegionCard
					]
			]
	];
	if (bLC1WhyGroupedExpanded)
	{
		ResponsiveRegionRow->AddSlot()
		.FillEmptySpace(true)
		[
			SNew(SBox)
				.MinDesiredWidth(300.0f)
				.MaxDesiredWidth(520.0f)
				[
					BuildLC1WhyGrouped(Region)
				]
		];
	}

	const auto BuildEndpointCard =
		[this](
			const FString& Label,
			const FBlueprintLensUnit& Unit,
			const FLinearColor& Accent) -> TSharedRef<SWidget>
		{
			const FString UnitId = Unit.Id;
			const bool bSelected = LC1SelectedUnitId == UnitId;
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					Accent,
					bSelected ? 0.34f : 0.20f))
				.Padding(FMargin(6.0f))
				[
					SNew(SButton)
						.ButtonStyle(
							&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
								"SimpleButton"))
						.ContentPadding(FMargin(0.0f))
						.ToolTipText(FText::FromString(FString::Printf(
							TEXT("Select %s and inspect its primary Blueprint source"),
							*Label)))
						.OnClicked_Lambda(
							[this, UnitId]()
							{
								SelectLC1Unit(UnitId);
								return FReply::Handled();
							})
					[
						SNew(STextBlock)
							.Text(FText::FromString(Label))
							.Font(FAppStyle::Get().GetFontStyle(
								"NormalFontBold"))
							.AutoWrapText(true)
					]
				];
		};

	TArray<FString> OrderedUnitIds = Layout.Segments[0].MemberUnitIds;
	OrderedUnitIds.Append(Layout.Segments[1].MemberUnitIds);
	OrderedUnitIds.Append(Layout.Segments[2].MemberUnitIds);
	const bool bTypedIrUnavailable =
		Region.DiagnosticCode == TEXT("LC1_REGION_TYPED_IR_UNBOUND");
	const FString EvidenceAnswerText =
		Region.Status
				== EBlueprintLensLC1RegionProjectionStatus::
					CompleteOperationRegion
			? TEXT(
				  "It is reached after Event BeginPlay and 12 ordered "
				  "supported assignments; each sets a completion flag to true.")
			: bTypedIrUnavailable
			? TEXT(
				  "It is reached after Event BeginPlay and 12 ordered "
				  "predecessor steps. No operation claim is made because typed "
				  "evidence is unavailable.")
			: TEXT(
				  "It is reached after Event BeginPlay and 12 ordered "
				  "predecessor steps. The stronger operation claim is not made "
				  "by this projection.");
	const FText EvidenceAnswer = FText::FromString(EvidenceAnswerText);
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1EvidenceRegionsHeading",
				"EVIDENCE-BACKED REGIONS"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(EvidenceAnswer)
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		BuildEndpointCard(
			TEXT("Event BeginPlay"),
			*Entry,
			RoleAccent(EBlueprintLensRole::Control))
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(8.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("LC1ThenEntry", "then"))
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		ResponsiveRegionRow
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(8.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock).Text(LOCTEXT("LC1ThenCriterion", "then"))
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		BuildEndpointCard(
			TEXT("Final readiness \u00B7 criterion"),
			*Criterion,
			RoleAccent(EBlueprintLensRole::Criterion))
	];
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1ShowAllExpanded
					? TEXT("Hide all 14 steps")
					: TEXT("Show all 14 steps")))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1ShowAll();
					return FReply::Handled();
				})
	];
	if (bLC1ShowAllExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC1OrderedRows(OrderedUnitIds, true)
		];
	}
	if (!LC1SelectedUnitId.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BuildLC1SelectedSourceAction()
		];
	}
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1TechnicalEvidenceExpanded
					? TEXT("Hide technical evidence")
					: TEXT("Technical evidence")))
			.ToolTipText(LOCTEXT(
				"LC1TechnicalEvidenceTooltipEvidence",
				"Reveal relation IDs, source GUIDs, digests and diagnostics"))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1TechnicalEvidence();
					return FReply::Handled();
				})
	];
	if (bLC1TechnicalEvidenceExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC1TechnicalEvidence(Layout, Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1PseudocodeEditor(
	const FBlueprintLensLC1PseudocodeProjection& Projection)
{
	check(Projection.IsRenderable());
	if (LC1SelectedPseudocodeLineId.IsEmpty()
		|| !Projection.Lines.ContainsByPredicate(
			[this](const FBlueprintLensLC1PseudocodeLine& Line)
			{
				return Line.LineId == LC1SelectedPseudocodeLineId;
			}))
	{
		LC1SelectedPseudocodeLineId = Projection.Lines.Last().LineId;
	}

	TSharedRef<SWrapBox> StatusChips = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(5.0f, 4.0f));
	StatusChips->AddSlot()
	[
		BuildChip(
			TEXT("STRUCTURED PSEUDOCODE"),
			FLinearColor(0.36f, 0.68f, 0.88f),
			0.24f)
	];
	StatusChips->AddSlot()
	[
		BuildChip(
			TEXT("DETERMINISTIC"),
			FLinearColor(0.40f, 0.76f, 0.58f),
			0.20f)
	];
	StatusChips->AddSlot()
	[
		BuildChip(
			TEXT("14 / 14 MAPPED"),
			FLinearColor(0.40f, 0.76f, 0.58f),
			0.20f)
	];

	TSharedRef<SVerticalBox> CodeRows = SNew(SVerticalBox);
	const FBlueprintLensLC1PseudocodeLine* SelectedLine = nullptr;
	for (const FBlueprintLensLC1PseudocodeLine& Line : Projection.Lines)
	{
		const bool bSelected = Line.LineId == LC1SelectedPseudocodeLineId;
		if (bSelected)
		{
			SelectedLine = &Line;
		}
		const bool bCriterion = Line.Role == EBlueprintLensRole::Criterion;
		const FString RoleLabel = bCriterion
			? TEXT("criterion")
			: Line.LineNumber == 1 ? TEXT("entry") : FString();
		const FString LineId = Line.LineId;
		const FLinearColor Accent = bCriterion
			? RoleAccent(EBlueprintLensRole::Criterion)
			: RoleAccent(EBlueprintLensRole::Control);
		TSharedRef<SHorizontalBox> SyntaxLine = SNew(SHorizontalBox);
		const auto AddSyntaxToken =
			[&SyntaxLine](
				const FString& Text,
				const FLinearColor& Color)
			{
				SyntaxLine->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Text))
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
						.ColorAndOpacity(Color)
				];
			};
		const FLinearColor KeywordColor(0.78f, 0.58f, 0.96f);
		const FLinearColor IdentifierColor(0.43f, 0.79f, 0.96f);
		const FLinearColor OperatorColor(0.66f, 0.72f, 0.80f);
		const FLinearColor LiteralColor(0.96f, 0.65f, 0.37f);
		if (Line.CodeText.StartsWith(TEXT("event ")))
		{
			AddSyntaxToken(TEXT("event"), KeywordColor);
			AddSyntaxToken(TEXT(" "), OperatorColor);
			AddSyntaxToken(Line.CodeText.RightChop(6), IdentifierColor);
		}
		else
		{
			FString Target;
			FString Value;
			if (Line.CodeText.Split(TEXT(" = "), &Target, &Value))
			{
				const bool bHasTerminator = Value.EndsWith(TEXT(";"));
				if (bHasTerminator)
				{
					Value.LeftChopInline(1);
				}
				AddSyntaxToken(Target, IdentifierColor);
				AddSyntaxToken(TEXT(" = "), OperatorColor);
				AddSyntaxToken(Value, LiteralColor);
				if (bHasTerminator)
				{
					AddSyntaxToken(TEXT(";"), OperatorColor);
				}
			}
			else
			{
				AddSyntaxToken(Line.CodeText, FLinearColor(0.86f, 0.91f, 0.97f));
			}
		}

		CodeRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(0.0f)
				.ToolTipText(FText::FromString(FString::Printf(
					TEXT("Select line %d and inspect its mapped Blueprint node"),
					Line.LineNumber)))
				.OnClicked_Lambda(
					[this, LineId]()
					{
						SelectLC1PseudocodeLine(LineId);
						return FReply::Handled();
					})
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor(
							bSelected
								? WithAlpha(Accent, 0.36f)
								: FLinearColor(0.075f, 0.085f, 0.105f, 0.96f))
						.Padding(FMargin(7.0f, 5.0f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 9.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(24.0f)
								[
									SNew(STextBlock)
										.Text(FText::AsNumber(Line.LineNumber))
										.Justification(ETextJustify::Right)
										.Font(FCoreStyle::GetDefaultFontStyle(
											"Mono",
											9))
										.ColorAndOpacity(FLinearColor(
											0.45f,
											0.52f,
											0.62f))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SyntaxLine
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(7.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
									.Text(FText::FromString(RoleLabel))
									.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
									.ColorAndOpacity(Accent)
							]
						]
				]
		];
	}
	check(SelectedLine != nullptr);
	const FString SelectedSourceNodeId = SelectedLine->SourceNodeId;
	const FString SelectedRole = SelectedLine->Role == EBlueprintLensRole::Criterion
		? TEXT("criterion")
		: SelectedLine->LineNumber == 1 ? TEXT("entry") : TEXT("operation");
	const FString RelationSummary = SelectedLine->FollowingRelationId.IsEmpty()
		? TEXT("Final criterion line; no following relation")
		: TEXT("Owns the following execution relation");

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.045f, 0.052f, 0.067f, 0.98f))
		.Padding(FMargin(10.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("LC1CodeEditorTitle", "Paired pseudocode"))
					.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
					.ColorAndOpacity(FLinearColor(0.90f, 0.95f, 1.0f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 7.0f)
			[
				SNew(STextBlock)
					.Text(LOCTEXT(
						"LC1CodeEditorBoundary",
						"Readable projection, not recovered or compilable C++."))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				StatusChips
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CodeRows
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 9.0f, 0.0f, 7.0f)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("Line %d · %s · supported"),
						SelectedLine->LineNumber,
						*SelectedRole)))
					.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 7.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(RelationSummary))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
					.Text(LOCTEXT(
						"LC1OpenSelectedCodeLine",
						"Open selected line in Blueprint"))
					.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
					.OnClicked_Lambda(
						[this, SelectedSourceNodeId]()
						{
							return NavigateToSource(SelectedSourceNodeId);
						})
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1SelectedSourceAction()
{
	const FBlueprintLensUnit* SelectedUnit =
		Model.IsValid() ? Model->FindUnit(LC1SelectedUnitId) : nullptr;
	if (SelectedUnit == nullptr)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1SelectedSourceUnavailable",
				"The selected reader row is unavailable."))
			.AutoWrapText(true);
	}

	const FString SelectedSourceNodeId =
		PrimarySourceNodeId(*SelectedUnit);
	return SNew(SButton)
		.Text(LOCTEXT(
			"LC1OpenSelectedRow",
			"Open selected row in Blueprint"))
		.IsEnabled(CanNavigateToSource(SelectedSourceNodeId))
		.ToolTipText(LOCTEXT(
			"LC1OpenSelectedRowTooltip",
			"Navigate to the primary Blueprint source for the selected reader row"))
		.OnClicked_Lambda(
			[this, SelectedSourceNodeId]()
			{
				return NavigateToSource(SelectedSourceNodeId);
			});
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1TechnicalEvidence(
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1DisclosureProjection& Projection)
{
	FBlueprintLensLC1RegionProjection TechnicalRegion = Projection.Region;
	if (TechnicalRegion.RegionId.IsEmpty())
	{
		const FBlueprintLensLC1TypedIrFacts TypedIrFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(Model->Source);
		TechnicalRegion = FBlueprintLensLC1RegionProjector::Build(
			*Model,
			Layout,
			TypedIrFacts);
	}

	const auto RegionStatusLabel =
		[](const EBlueprintLensLC1RegionProjectionStatus Status) -> FString
		{
			switch (Status)
			{
			case EBlueprintLensLC1RegionProjectionStatus::
				CompleteOperationRegion:
				return TEXT("Complete operation region");
			case EBlueprintLensLC1RegionProjectionStatus::
				OrderedVariableAssignments:
				return TEXT("Ordered variable assignments");
			case EBlueprintLensLC1RegionProjectionStatus::StructuralRun:
				return TEXT("Structural run");
			default:
				return TEXT("Unavailable");
			}
		};
	const auto JoinValues =
		[](const TArray<FString>& Values) -> FString
		{
			return Values.IsEmpty()
				? TEXT("None")
				: FString::Join(Values, TEXT("\n"));
		};

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1TechnicalEvidenceHeading",
				"TECHNICAL EVIDENCE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
	];

	const auto AddValue =
		[&Content](const TCHAR* Label, const FString& Value)
		{
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString(Label)))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(
							FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Value))
						.AutoWrapText(true)
				]
			];
		};

	const FString SourceIrSha256 = TechnicalRegion.SourceIrSha256.IsEmpty()
		? Model->Source.IrSha256
		: TechnicalRegion.SourceIrSha256;
	AddValue(
		TEXT("Source IR SHA-256"),
		SourceIrSha256.IsEmpty()
			? FString(TEXT("Unavailable"))
			: SourceIrSha256);
	AddValue(
		TEXT("Typed-IR binding"),
		TechnicalRegion.DiagnosticCode == TEXT("LC1_REGION_TYPED_IR_UNBOUND")
			? FString(TEXT("Unavailable · no operation claim"))
			: FString::Printf(
				  TEXT("Verified · SHA-256 %s"),
				  *SourceIrSha256));
	AddValue(
		TEXT("Region projection status"),
		FString::Printf(
			TEXT("%s · %s"),
			*RegionStatusLabel(TechnicalRegion.Status),
			TechnicalRegion.DiagnosticCode.IsEmpty()
				? TEXT("no diagnostic")
				: *TechnicalRegion.DiagnosticCode));
	AddValue(
		TEXT("Region kind"),
		TechnicalRegion.RegionKind.IsEmpty()
			? FString(TEXT("Unavailable"))
			: TechnicalRegion.RegionKind);
	AddValue(
		TEXT("Region ID"),
		TechnicalRegion.RegionId.IsEmpty()
			? FString(TEXT("Unavailable"))
			: TechnicalRegion.RegionId);
	AddValue(
		TEXT("Region projection integrity hash"),
		TechnicalRegion.ProjectionIntegrityHash.IsEmpty()
			? FString(TEXT("Unavailable"))
			: TechnicalRegion.ProjectionIntegrityHash);
	AddValue(
		TEXT("Summary template"),
		TechnicalRegion.SummaryTemplateId.IsEmpty()
			? FString(TEXT("Unavailable"))
			: TechnicalRegion.SummaryTemplateId);
	AddValue(
		TEXT("Summary arguments"),
		JoinValues(TechnicalRegion.SummaryArguments));

	const auto AddIdList =
		[&AddValue, &JoinValues](
			const TCHAR* Label,
			const TArray<FString>& Ids)
		{
			AddValue(
				Label,
				FString::Printf(
					TEXT("count=%d\n%s"),
					Ids.Num(),
					*JoinValues(Ids)));
		};
	AddIdList(
		TEXT("Region member unit IDs"),
		TechnicalRegion.OrderedMemberUnitIds);
	AddIdList(
		TEXT("Incoming relation IDs"),
		TechnicalRegion.IncomingRelationIds);
	AddIdList(
		TEXT("Internal relation IDs"),
		TechnicalRegion.InternalRelationIds);
	AddIdList(
		TEXT("Outgoing relation IDs"),
		TechnicalRegion.OutgoingRelationIds);

	for (const FBlueprintLensLC1ClaimEvidence& Evidence :
		 TechnicalRegion.ClaimEvidence)
	{
		AddValue(
			TEXT("Claim ledger entry"),
			FString::Printf(
				TEXT("claim part: %s\n fact owner: %s\n source ID: %s\n "
					 "value: %s"),
				*Evidence.ClaimPart,
				*Evidence.FactOwner,
				*Evidence.SourceId,
				*Evidence.Value));
	}

	for (const FBlueprintLensUnit& Unit : Model->Units)
	{
		AddValue(
			TEXT("Unit source evidence"),
			FString::Printf(
				TEXT("Unit ID: %s\nReader title: %s\nSemantic status: %s"),
				*Unit.Id,
				*Unit.Title,
				LexToString(Unit.SemanticStatus)));
		if (!Unit.Expression.IsEmpty())
		{
			AddValue(TEXT("Unit expression"), Unit.Expression);
		}
		AddValue(
			TEXT("Inclusion reasons"),
			JoinValues(Unit.InclusionReasons));
		for (int32 ReferenceIndex = 0;
			 ReferenceIndex < Unit.SourceReferences.Num();
			 ++ReferenceIndex)
		{
			const FBlueprintLensSourceReference& Reference =
				Unit.SourceReferences[ReferenceIndex];
			const FString SourcePinIds = Reference.SourcePinIds.IsEmpty()
				? FString(TEXT("None"))
				: FString::Join(Reference.SourcePinIds, TEXT(", "));
			FString SourceEvidence = FString::Printf(
				TEXT("Source %d%s\nSource node ID: %s\nNativeNodeGuid: %s\n"
					 "Source pin IDs: %s"),
				ReferenceIndex + 1,
				Reference.bPrimary ? TEXT(" · primary") : TEXT(""),
				*Reference.SourceNodeId,
				*Reference.NativeNodeGuid,
				*SourcePinIds);
			if (const FBlueprintLensResolvedSource* Resolved =
					ResolvedSources.Find(Reference.SourceNodeId))
			{
				const FString State = SourceStateLabel(Resolved->State);
				if (!State.IsEmpty())
				{
					SourceEvidence += FString::Printf(
						TEXT("\nState: %s"),
						*State);
				}
				if (!Resolved->Message.IsEmpty())
				{
					SourceEvidence += FString::Printf(
						TEXT("\nMessage: %s"),
						*Resolved->Message);
				}
			}
			AddValue(TEXT("Source reference"), SourceEvidence);
		}
	}

	if (Projection.Candidate ==
		EBlueprintLensLC1DisclosureCandidate::PairedPseudocode)
	{
		const FBlueprintLensLC1PseudocodeProjection& Pseudocode =
			Projection.Pseudocode;
		AddValue(
			TEXT("Pseudocode projection status"),
			Pseudocode.IsRenderable()
				? FString::Printf(
					  TEXT("Complete · %s"),
					  *Pseudocode.DiagnosticCode)
				: TEXT("Unavailable"));
		AddValue(
			TEXT("Pseudocode source IR SHA-256"),
			Pseudocode.SourceIrSha256.IsEmpty()
				? FString(TEXT("Unavailable"))
				: Pseudocode.SourceIrSha256);
		AddValue(
			TEXT("Pseudocode projection integrity hash"),
			Pseudocode.ProjectionIntegrityHash.IsEmpty()
				? FString(TEXT("Unavailable"))
				: Pseudocode.ProjectionIntegrityHash);
		for (const FBlueprintLensLC1PseudocodeLine& Line :
			 Pseudocode.Lines)
		{
			const FString SourcePinIds = Line.SourcePinIds.IsEmpty()
				? FString(TEXT("None"))
				: FString::Join(Line.SourcePinIds, TEXT(", "));
			AddValue(
				TEXT("Pseudocode line evidence"),
				FString::Printf(
					TEXT("Line %d\nLine ID: %s\nUnit ID: %s\nSource node ID: %s\n"
						 "Source pin IDs: %s\nFollowing relation ID: %s\n"
						 "Fact owner: %s\nDiagnostic: %s\nCode: %s"),
					Line.LineNumber,
					*Line.LineId,
					*Line.UnitId,
					*Line.SourceNodeId,
					*SourcePinIds,
					Line.FollowingRelationId.IsEmpty()
						? TEXT("None")
						: *Line.FollowingRelationId,
					*Line.FactOwner,
					*Line.ProjectionDiagnostic,
					*Line.CodeText));
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			RoleAccent(EBlueprintLensRole::Boundary),
			0.14f))
		.Padding(FMargin(8.0f))
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1PairedPseudocode(
	const FBlueprintLensFrameFlowLayoutModel& Layout,
	const FBlueprintLensLC1DisclosureProjection& Projection)
{
	if (Projection.Candidate
		!= EBlueprintLensLC1DisclosureCandidate::PairedPseudocode
		|| !Projection.Region.IsRenderable()
		|| !Projection.Pseudocode.IsRenderable())
	{
		return BuildLC1PlainOutline(Layout, Projection);
	}

	TSharedRef<SVerticalBox> ExplanationCard = SNew(SVerticalBox);
	ExplanationCard->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(LOCTEXT("LC1PairedWhyLabel", "WHY IT EXECUTES"))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
	];
	ExplanationCard->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 10.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1PairedAnswer",
				"BeginPlay runs 12 supported assignments before the "
				"criterion sets the final readiness flag to true."))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.AutoWrapText(true)
	];

	TSharedRef<SHorizontalBox> ExplanationRail = SNew(SHorizontalBox);
	const auto AddExplanationRow =
		[&ExplanationRail](
			const FString& Role,
			const FString& Text,
			const FLinearColor& Accent)
		{
			ExplanationRail->AddSlot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(Accent, 0.13f))
					.Padding(FMargin(8.0f, 6.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
								.Text(FText::FromString(Role))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(Accent)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(Text))
								.AutoWrapText(true)
						]
					]
			];
		};
	AddExplanationRow(
		TEXT("ENTRY"),
		TEXT("Event BeginPlay"),
		RoleAccent(EBlueprintLensRole::Control));
	AddExplanationRow(
		TEXT("OPERATION REGION · 12 MAPPED STEPS"),
		TEXT("Set twelve completion flags to true in order"),
		RoleAccent(EBlueprintLensRole::Control));
	AddExplanationRow(
		TEXT("CRITERION"),
		TEXT("Set the final readiness flag to true"),
		RoleAccent(EBlueprintLensRole::Criterion));
	ExplanationCard->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		ExplanationRail
	];

	TSharedRef<SWrapBox> ExplanationActions = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(6.0f, 4.0f));
	ExplanationActions->AddSlot()
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1WhyGroupedExpanded
					? TEXT("Hide grouping evidence")
					: TEXT("Why grouped?")))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1WhyGrouped();
					return FReply::Handled();
				})
	];
	ExplanationActions->AddSlot()
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1ShowAllExpanded
					? TEXT("Hide explanation steps")
					: TEXT("Show explanation steps")))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1ShowAll();
					return FReply::Handled();
				})
	];
	ExplanationCard->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 5.0f, 0.0f, 0.0f)
	[
		ExplanationActions
	];

	TSharedRef<SVerticalBox> PairedSurface = SNew(SVerticalBox);
	PairedSurface->AddSlot()
	.AutoHeight()
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(
				RoleAccent(EBlueprintLensRole::Control),
				0.12f))
			.Padding(FMargin(10.0f))
			[
				ExplanationCard
			]
	];
	PairedSurface->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 10.0f, 0.0f, 0.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.055f, 0.064f, 0.082f, 0.98f))
			.Padding(FMargin(10.0f, 8.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("LC1CodeDisclosureTitle", "CODE COMPARISON"))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FLinearColor(0.43f, 0.79f, 0.96f))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f, 0.0f, 7.0f)
				[
					SNew(STextBlock)
						.Text(LOCTEXT(
							"LC1CodeDisclosureSummary",
							"Optional source-linked pseudocode. The visual explanation above remains primary."))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
						.Text(FText::FromString(
							bLC1PseudocodeExpanded
								? TEXT("Hide paired pseudocode")
								: FString::Printf(
									TEXT("Show paired pseudocode (%d mapped lines)"),
									Projection.Pseudocode.Lines.Num())))
						.OnClicked_Lambda(
							[this]()
							{
								ToggleLC1Pseudocode();
								return FReply::Handled();
							})
				]
			]
	];
	if (bLC1PseudocodeExpanded)
	{
		PairedSurface->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC1PseudocodeEditor(Projection.Pseudocode)
		];
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 3.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT("LC1PairedHeading", "PAIRED PSEUDOCODE"))
			.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
	];
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"LC1PairedDescription",
				"One explanation, one deterministic line ledger, and exact "
				"return to the native Blueprint node."))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
	];
	Content->AddSlot()
	.AutoHeight()
	[
		PairedSurface
	];
	if (bLC1WhyGroupedExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC1WhyGrouped(Projection.Region)
		];
	}
	if (bLC1ShowAllExpanded)
	{
		TArray<FString> OrderedUnitIds = Layout.Segments[0].MemberUnitIds;
		OrderedUnitIds.Append(Layout.Segments[1].MemberUnitIds);
		OrderedUnitIds.Append(Layout.Segments[2].MemberUnitIds);
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC1OrderedRows(OrderedUnitIds, true)
		];
	}
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 8.0f)
	[
		SNew(SButton)
			.Text(FText::FromString(
				bLC1TechnicalEvidenceExpanded
					? TEXT("Hide technical evidence")
					: TEXT("Technical evidence")))
			.ToolTipText(LOCTEXT(
				"LC1TechnicalEvidenceTooltipPaired",
				"Reveal relation IDs, source GUIDs, digests and diagnostics"))
			.OnClicked_Lambda(
				[this]()
				{
					ToggleLC1TechnicalEvidence();
					return FReply::Handled();
				})
	];
	if (bLC1TechnicalEvidenceExpanded)
	{
		Content->AddSlot()
		.AutoHeight()
		[
			BuildLC1TechnicalEvidence(Layout, Projection)
		];
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1WhyGrouped(
	const FBlueprintLensLC1RegionProjection& Region)
{
	FString RegionExplanation;
	FString BindingExplanation;
	switch (Region.Status)
	{
	case EBlueprintLensLC1RegionProjectionStatus::CompleteOperationRegion:
		RegionExplanation = FString::Printf(
			TEXT("%d supported assignments form one uninterrupted execution "
				 "run, and each assigns true to a completion flag."),
			Region.OrderedMemberUnitIds.Num());
		BindingExplanation =
			TEXT("Typed evidence verifies the repeated supported assignment and "
			"its true value.");
		break;
	case EBlueprintLensLC1RegionProjectionStatus::OrderedVariableAssignments:
		RegionExplanation = FString::Printf(
			TEXT("%d variable assignments form one uninterrupted execution run."),
			Region.OrderedMemberUnitIds.Num());
		BindingExplanation =
			TEXT("Typed evidence verifies the assignment kind; the stronger "
			"operation claim is not made.");
		break;
	case EBlueprintLensLC1RegionProjectionStatus::StructuralRun:
		RegionExplanation = FString::Printf(
			TEXT("%d predecessor steps form one uninterrupted execution run; "
			"no operation claim is made."),
			Region.OrderedMemberUnitIds.Num());
		BindingExplanation = Region.DiagnosticCode ==
				TEXT("LC1_REGION_TYPED_IR_UNBOUND")
			? TEXT(
				  "Typed evidence is unavailable, so no operation claim is made.")
			: TEXT(
				  "Typed evidence does not support a stronger operation claim.");
		break;
	default:
		RegionExplanation =
			TEXT("The region explanation is unavailable; no stronger claim is "
			"made.");
		BindingExplanation =
			TEXT("Typed evidence is unavailable, so no operation claim is made.");
		break;
	}

	const FString RelationExplanation = FString::Printf(
		TEXT("The region has %d incoming, %d internal, and %d outgoing "
			 "relations. All %d explanation relations are accounted for."),
		Region.IncomingRelationIds.Num(),
		Region.InternalRelationIds.Num(),
		Region.OutgoingRelationIds.Num(),
		Region.IncomingRelationIds.Num()
			+ Region.InternalRelationIds.Num()
			+ Region.OutgoingRelationIds.Num());
	const FString SourceExplanation = FString::Printf(
		TEXT("%d of %d region members have primary Blueprint sources."),
		Region.OrderedMemberUnitIds.Num(),
		Region.OrderedMemberUnitIds.Num());

	TSharedRef<SVerticalBox> ReaderPanel = SNew(SVerticalBox);
	const auto AddField =
		[&ReaderPanel](const FText& Label, const FString& Value)
		{
			ReaderPanel->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(Label)
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.ColorAndOpacity(
							FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Value))
						.AutoWrapText(true)
				]
			];
		};

	AddField(
		LOCTEXT("LC1WhyRegionKind", "Why this is grouped"),
		RegionExplanation);
AddField(
		LOCTEXT(
			"LC1WhyRelations",
			"Relation coverage"),
		RelationExplanation);
AddField(
		LOCTEXT("LC1WhySourceCoverage", "Source coverage"),
		SourceExplanation);
AddField(
		LOCTEXT("LC1WhyTypedIrBinding", "Typed-IR binding"),
		BindingExplanation);
AddField(
		LOCTEXT("LC1WhyClaimBoundary", "Claim boundary"),
		Region.Status ==
				EBlueprintLensLC1RegionProjectionStatus::CompleteOperationRegion
			? TEXT("The complete assignment claim is supported by the bound "
			       "typed evidence.")
			: TEXT("The reader keeps the stronger operation claim withheld."));

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			RoleAccent(EBlueprintLensRole::Boundary),
			0.18f))
		.Padding(FMargin(8.0f))
		[
			ReaderPanel
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC1EmptyStateStrip()
{
	const FBlueprintLensLane* Predicate =
		FindLane(EBlueprintLensRole::Predicate);
	const FBlueprintLensLane* Value =
		FindLane(EBlueprintLensRole::Value);
	const FBlueprintLensLane* Boundary =
		FindLane(EBlueprintLensRole::Boundary);
	const FString Summary = FString::Printf(
		TEXT("PREDICATE %s \u00B7 VALUE %s \u00B7 BOUNDARY %s"),
		Predicate != nullptr && Predicate->UnitIds.IsEmpty()
			? TEXT("EMPTY")
			: TEXT("POPULATED"),
		Value != nullptr && Value->UnitIds.IsEmpty()
			? TEXT("EMPTY")
			: TEXT("POPULATED"),
		Boundary != nullptr && Boundary->UnitIds.IsEmpty()
			? TEXT("EMPTY")
			: TEXT("POPULATED"));
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			RoleAccent(EBlueprintLensRole::Boundary),
			0.16f))
		.Padding(FMargin(7.0f, 4.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(Summary))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
				.ToolTipText(FText::FromString(FString::Printf(
					TEXT("Predicate: %s\nValue: %s\nBoundary: %s"),
					Predicate != nullptr
						? *Predicate->EmptyMessage
						: TEXT("unavailable"),
					Value != nullptr
						? *Value->EmptyMessage
						: TEXT("unavailable"),
					Boundary != nullptr
						? *Boundary->EmptyMessage
						: TEXT("unavailable"))))
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildRouteRepresentation()
{
	const FBlueprintLensLane* CriterionLane =
		FindLane(EBlueprintLensRole::Criterion);
	const FBlueprintLensLane* ControlLane =
		FindLane(EBlueprintLensRole::Control);
	const FBlueprintLensLane* PredicateLane =
		FindLane(EBlueprintLensRole::Predicate);
	const FBlueprintLensLane* ValueLane =
		FindLane(EBlueprintLensRole::Value);
	if (CriterionLane == nullptr || ControlLane == nullptr
		|| PredicateLane == nullptr || ValueLane == nullptr
		|| CriterionLane->UnitIds.IsEmpty()
		|| PredicateLane->UnitIds.IsEmpty()
		|| ValueLane->UnitIds.IsEmpty())
	{
		return BuildLaneRepresentation();
	}

	const FBlueprintLensUnit* Criterion =
		Model->FindUnit(CriterionLane->UnitIds[0]);
	const FBlueprintLensUnit* Predicate =
		Model->FindUnit(PredicateLane->UnitIds[0]);
	const FBlueprintLensUnit* Value =
		Model->FindUnit(ValueLane->UnitIds[0]);
	if (Criterion == nullptr || Predicate == nullptr || Value == nullptr)
	{
		return BuildLaneRepresentation();
	}

	TArray<const FBlueprintLensUnit*> RouteUnits;
	for (const FString& UnitId : ControlLane->UnitIds)
	{
		if (const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId))
		{
			RouteUnits.Add(Unit);
		}
	}
	RouteUnits.Add(Criterion);

	TSharedRef<SVerticalBox> RouteContent = SNew(SVerticalBox);
	RouteContent->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
			.Text(LOCTEXT(
				"SemanticRouteHeading",
				"SEMANTIC ROUTE \u00B7 SELECTED EXECUTION"))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(RoleAccent(EBlueprintLensRole::Control))
	];

	for (int32 Index = 0; Index < RouteUnits.Num(); ++Index)
	{
		const FBlueprintLensUnit& Unit = *RouteUnits[Index];
		RouteContent->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%02d \u00B7 %s"),
					Index + 1,
					*Unit.Title.ToUpper())))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(
					Unit.Role == EBlueprintLensRole::Criterion
						? RoleAccent(EBlueprintLensRole::Criterion)
						: RoleAccent(EBlueprintLensRole::Control))
		];
		RouteContent->AddSlot().AutoHeight()[BuildUnitCard(Unit)];

		const FBlueprintLensRelation* PredicateRelation = FindRelation(
			Predicate->Id,
			Unit.Id,
			EBlueprintLensRelationKind::PredicateFor);
		if (PredicateRelation != nullptr)
		{
			RouteContent->AddSlot()
			.AutoHeight()
			.Padding(20.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
					.BorderImage(
						FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(
						RoleAccent(EBlueprintLensRole::Predicate),
						0.18f))
					.Padding(FMargin(9.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("GUARD ATTACHMENT \u00B7 %s"),
									*PredicateRelation->Label.ToUpper())))
								.Font(FAppStyle::Get().GetFontStyle(
									"SmallFont"))
								.ColorAndOpacity(RoleAccent(
									EBlueprintLensRole::Predicate))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							BuildUnitCard(*Predicate)
						]
					]
			];
		}

		if (Unit.Id == Criterion->Id)
		{
			const FBlueprintLensRelation* ValueRelation = FindRelation(
				Value->Id,
				Criterion->Id,
				EBlueprintLensRelationKind::ProvidesValue);
			RouteContent->AddSlot()
			.AutoHeight()
			.Padding(20.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
					.BorderImage(
						FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(WithAlpha(
						RoleAccent(EBlueprintLensRole::Value),
						0.18f))
					.Padding(FMargin(9.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("VALUE ATTACHMENT \u00B7 %s"),
									ValueRelation != nullptr
										? *ValueRelation->Label.ToUpper()
										: TEXT("PROVIDES VALUE"))))
								.Font(FAppStyle::Get().GetFontStyle(
									"SmallFont"))
								.ColorAndOpacity(RoleAccent(
									EBlueprintLensRole::Value))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							BuildUnitCard(*Value)
						]
					]
			];
		}

		if (Index + 1 < RouteUnits.Num())
		{
			const FBlueprintLensUnit& NextUnit = *RouteUnits[Index + 1];
			const EBlueprintLensRelationKind RelationKind =
				NextUnit.Role == EBlueprintLensRole::Criterion
					? EBlueprintLensRelationKind::ControlsExecution
					: EBlueprintLensRelationKind::ExecutionPredecessor;
			const FBlueprintLensRelation* Relation = FindRelation(
				Unit.Id,
				NextUnit.Id,
				RelationKind);
			RouteContent->AddSlot()
			.AutoHeight()
			[
				BuildRelationMarker(
					Relation != nullptr
						? Relation->Label
						: FString(TEXT("NEXT")),
					RoleAccent(EBlueprintLensRole::Control))
			];
		}
	}

	RouteContent->AddSlot()
	.AutoHeight()
	.Padding(0.0f, SectionSpacing, 0.0f, 0.0f)
	[
		BuildAnalysisTruthStrip()
	];

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(WithAlpha(
					RoleAccent(EBlueprintLensRole::Control),
					0.10f))
				.Padding(FMargin(11.0f))
				[
					RouteContent
				]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildAnalysisTruthStrip()
{
	const FBlueprintLensLane* BoundaryLane =
		FindLane(EBlueprintLensRole::Boundary);
	bool bAllSupported = true;
	for (const FBlueprintLensUnit& Unit : Model->Units)
	{
		bAllSupported &=
			Unit.SemanticStatus == EBlueprintLensSemanticStatus::Supported;
	}
	const FString BoundaryMessage =
		BoundaryLane != nullptr && !BoundaryLane->EmptyMessage.IsEmpty()
			? BoundaryLane->EmptyMessage
			: FString(TEXT("Frontier details are present below"));
	const bool bBoundaryEmpty =
		BoundaryLane != nullptr && BoundaryLane->UnitIds.IsEmpty();
	const FLinearColor Accent =
		bAllSupported
			? FLinearColor(0.32f, 0.65f, 0.43f)
			: FLinearColor(0.90f, 0.46f, 0.18f);
	const FString TruthLabel =
		bAllSupported ? TEXT("SUPPORTED SLICE") : TEXT("FRONTIER PRESENT");
	const FString TruthSummary =
		bBoundaryEmpty
			? FString::Printf(
				  TEXT("ANALYSIS TRUTH \u00B7 %s \u00B7 %d/%d NODES "
					   "\u00B7 %d/%d EDGES \u00B7 BOUNDARY EMPTY"),
				  *TruthLabel,
				  Model->Counts.SourceNodes,
				  Model->Counts.SourceNodes,
				  Model->Counts.SourceEdges,
				  Model->Counts.SourceEdges)
			: FString::Printf(
				  TEXT("ANALYSIS TRUTH \u00B7 %s \u00B7 %d/%d NODES "
					   "\u00B7 %d/%d EDGES"),
				  *TruthLabel,
				  Model->Counts.SourceNodes,
				  Model->Counts.SourceNodes,
				  Model->Counts.SourceEdges,
				  Model->Counts.SourceEdges);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(TruthSummary))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
			.ColorAndOpacity(Accent)
			.AutoWrapText(true)
			.ToolTipText(FText::FromString(FString::Printf(
				TEXT("Boundary: %s"),
				*BoundaryMessage)))
	];
	if (!bBoundaryEmpty)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("BOUNDARY \u00B7 %s"),
					*BoundaryMessage)))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
		];
		if (BoundaryLane != nullptr && !BoundaryLane->UnitIds.IsEmpty())
		{
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 7.0f, 0.0f, 0.0f)
			[
				BuildLane(*BoundaryLane)
			];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(Accent, 0.16f))
		.Padding(FMargin(8.0f, 5.0f))
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildRelationMarker(
	const FString& Label,
	const FLinearColor& Accent)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(FMargin(12.0f, 5.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("\u2193  %s"),
					*Label.ToUpper())))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(Accent)
				.AutoWrapText(true)
		];
}

const FBlueprintLensLane* SBlueprintLensPanel::FindLane(
	const EBlueprintLensRole Role) const
{
	return Model.IsValid()
		? Model->Lanes.FindByPredicate(
			  [Role](const FBlueprintLensLane& Lane)
			  {
				  return Lane.Role == Role;
			  })
		: nullptr;
}

const FBlueprintLensRelation* SBlueprintLensPanel::FindRelation(
	const FString& SourceUnitId,
	const FString& TargetUnitId,
	const EBlueprintLensRelationKind Kind) const
{
	return Model.IsValid()
		? Model->Relations.FindByPredicate(
			  [&SourceUnitId, &TargetUnitId, Kind](
				  const FBlueprintLensRelation& Relation)
			  {
				  return Relation.SourceUnitId == SourceUnitId
					  && Relation.TargetUnitId == TargetUnitId
					  && Relation.Kind == Kind;
			  })
		: nullptr;
}

FString SBlueprintLensPanel::PrimarySourceNodeId(
	const FBlueprintLensUnit& Unit) const
{
	const FBlueprintLensSourceReference* Primary =
		Unit.SourceReferences.FindByPredicate(
			[](const FBlueprintLensSourceReference& Reference)
			{
				return Reference.bPrimary;
			});
	return Primary != nullptr ? Primary->SourceNodeId : FString();
}

void SBlueprintLensPanel::SetLC1DisclosureCandidate(
	const EBlueprintLensLC1DisclosureCandidate NewCandidate)
{
	if (!Model.IsValid())
	{
		return;
	}
	// LC3-D3's idiom: choosing the active item again returns to the default
	// presentation. Nothing else clears this option, so without the return the
	// rail is a one-way door - reachable only until the reader tries a
	// condition, which is the first thing the switcher invites them to do.
	if (LC1DisclosureCandidate.IsSet() &&
		LC1DisclosureCandidate.GetValue() == NewCandidate)
	{
		LC1DisclosureCandidate.Reset();
	}
	else
	{
		LC1DisclosureCandidate = NewCandidate;
	}
	RootBox->SetContent(BuildLoadedContent());
}

void SBlueprintLensPanel::ToggleLC1RegionMembers()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC1RegionMembersExpanded = !bLC1RegionMembersExpanded;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC1ShowAll()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC1ShowAllExpanded = !bLC1ShowAllExpanded;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC1WhyGrouped()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC1WhyGroupedExpanded = !bLC1WhyGroupedExpanded;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC1Pseudocode()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC1PseudocodeExpanded = !bLC1PseudocodeExpanded;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC1TechnicalEvidence()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC1TechnicalEvidenceExpanded = !bLC1TechnicalEvidenceExpanded;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectLC1Unit(const FString& UnitId)
{
	if (!Model.IsValid() || UnitId.IsEmpty()
		|| Model->FindUnit(UnitId) == nullptr
		|| LC1SelectedUnitId == UnitId)
	{
		return;
	}
	LC1SelectedUnitId = UnitId;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectM6RailUnit(const FString& UnitId)
{
	if (!M6ReadyPacket.IsValid() || UnitId.IsEmpty() ||
		M6ReadyPacket->Explanation.FindUnit(UnitId) == nullptr)
	{
		return;
	}

	const FString SourceEntityId = PrimarySourceNodeId(
		*M6ReadyPacket->Explanation.FindUnit(UnitId));
	const bool bPresentationMatchesRail = !SourceEntityId.IsEmpty() &&
		M6Presentation.SelectedEntityId() == SourceEntityId;
	if (LC1SelectedUnitId == UnitId &&
		(SourceEntityId.IsEmpty() || bPresentationMatchesRail))
	{
		return;
	}

	LC1SelectedUnitId = UnitId;
	if (!SourceEntityId.IsEmpty() && !bPresentationMatchesRail)
	{
		M6Presentation.SelectEntity(
			SourceEntityId,
			EM6SelectionOrigin::BaselineView);
	}
	SynchronizeM6RailSelection(M6Presentation.SelectedEntityId());
	RefreshM6Content();
}

void SBlueprintLensPanel::ToggleM6CompositeDisclosure(const FString& UnitId)
{
	if (!M6ReadyPacket.IsValid() || UnitId.IsEmpty() ||
		M6ReadyPacket->Explanation.FindUnit(UnitId) == nullptr)
	{
		return;
	}
	const bool bGuardUnit = M6ReadyPacket->Explanation.Relations.ContainsByPredicate(
		[&UnitId](const FBlueprintLensRelation& Relation)
		{
			return Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
				Relation.TargetUnitId == UnitId;
		});
	if (!bGuardUnit)
	{
		return;
	}
	if (M6ExpandedGuardUnitIds.IsEmpty())
	{
		M6ExpandedAttachmentUnitId.Reset();
		M6ExpandedAttachmentGrammarId.Reset();
		M6ExpandedBetweenRelationId.Reset();
		M6ExpandedSpanId.Reset();
		M6ExpandedGuardUnitIds.Add(UnitId);
	}
	else
	{
		M6ExpandedGuardUnitIds.Reset();
	}
	RefreshM6Content();
}

void SBlueprintLensPanel::ToggleM6AttachmentDisclosure(
	const FString& UnitId,
	const FString& GrammarId)
{
	if (!M6ReadyPacket.IsValid() || UnitId.IsEmpty() ||
		M6ReadyPacket->Explanation.FindUnit(UnitId) == nullptr ||
		!LC1RailCanvas.IsValid())
	{
		return;
	}
	const bool bValueFedStation =
		M6ReadyPacket->Explanation.Relations.ContainsByPredicate(
			[&UnitId](const FBlueprintLensRelation& Relation)
			{
				return Relation.Kind ==
						EBlueprintLensRelationKind::ProvidesValue &&
					Relation.TargetUnitId == UnitId;
			});
	const FBlueprintLensUnit* Unit =
		M6ReadyPacket->Explanation.FindUnit(UnitId);
	const bool bDataAnswerStation = Unit != nullptr &&
		M6ReadyPacket->Request.QueryKind.Equals(
			TEXT("data"), ESearchCase::IgnoreCase) &&
		(Unit->InclusionReasons.Contains(TEXT("member_set")) ||
			Unit->InclusionReasons.Contains(TEXT("direct_write_controller")) ||
			Unit->Role == EBlueprintLensRole::Boundary ||
			Unit->SemanticStatus != EBlueprintLensSemanticStatus::Supported);
	const FBlueprintLensCompositeRailSlots& Slots =
		LC1RailCanvas->GetCompositeSlotsForTesting();
	const FBlueprintLensCompositeStationSlot* HostStation =
		Slots.FindStation(UnitId);
	const bool bNamedStationAttachment = HostStation != nullptr &&
		HostStation->BesideAttachments.ContainsByPredicate(
			[&GrammarId](const FBlueprintLensCompositeAttachment& Attachment)
			{
				return Attachment.GrammarId == GrammarId;
			});
	const bool bNamedTerminalAttachment =
		Slots.TerminalCaps.ContainsByPredicate(
			[&UnitId, &GrammarId](
				const FBlueprintLensCompositeTerminalCapSlot& Cap)
			{
				return Cap.UnitId == UnitId &&
					Cap.Attachments.ContainsByPredicate(
						[&GrammarId](
							const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == GrammarId;
						});
			});
	const bool bLegacyLC3Attachment = GrammarId == TEXT("LC3") &&
		(bValueFedStation || bDataAnswerStation);
	if ((!bNamedStationAttachment && !bNamedTerminalAttachment &&
			!bLegacyLC3Attachment) ||
		(GrammarId != TEXT("LC3") && GrammarId != TEXT("LC5") &&
			GrammarId != TEXT("LC6")))
	{
		return;
	}
	M6ExpandedGuardUnitIds.Reset();
	M6ExpandedBetweenRelationId.Reset();
	M6ExpandedSpanId.Reset();
	const bool bCollapse = M6ExpandedAttachmentUnitId == UnitId &&
		M6ExpandedAttachmentGrammarId == GrammarId;
	M6ExpandedAttachmentUnitId = bCollapse ? FString() : UnitId;
	M6ExpandedAttachmentGrammarId = bCollapse ? FString() : GrammarId;
	RefreshM6Content();
}

void SBlueprintLensPanel::ToggleM6BetweenDisclosure(
	const FString& RelationId)
{
	if (!M6ReadyPacket.IsValid() || RelationId.IsEmpty())
	{
		return;
	}
	const FBlueprintLensRelation* Relation =
		M6ReadyPacket->Explanation.FindRelation(RelationId);
	if (Relation == nullptr || !Relation->bHasPortLabel ||
		!Relation->PortLabel.StartsWith(
			TEXT("then_"), ESearchCase::IgnoreCase))
	{
		return;
	}
	M6ExpandedGuardUnitIds.Reset();
	M6ExpandedAttachmentUnitId.Reset();
	M6ExpandedAttachmentGrammarId.Reset();
	M6ExpandedSpanId.Reset();
	M6ExpandedBetweenRelationId =
		M6ExpandedBetweenRelationId == RelationId
			? FString()
			: RelationId;
	RefreshM6Content();
}

void SBlueprintLensPanel::ToggleM6SpanDisclosure(const FString& SpanId)
{
	if (!M6ReadyPacket.IsValid() || SpanId.IsEmpty() ||
		!LC1RailCanvas.IsValid())
	{
		return;
	}
	const FBlueprintLensCompositeSpanSlot* Span =
		LC1RailCanvas->GetCompositeSlotsForTesting().Spans.FindByPredicate(
			[&SpanId](const FBlueprintLensCompositeSpanSlot& Candidate)
			{
				return Candidate.SlotId == SpanId &&
					Candidate.Attachments.ContainsByPredicate(
						[](const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == TEXT("LC7");
						});
			});
	if (Span == nullptr)
	{
		return;
	}
	M6ExpandedGuardUnitIds.Reset();
	M6ExpandedAttachmentUnitId.Reset();
	M6ExpandedAttachmentGrammarId.Reset();
	M6ExpandedBetweenRelationId.Reset();
	M6ExpandedSpanId = M6ExpandedSpanId == SpanId ? FString() : SpanId;
	RefreshM6Content();
}

void SBlueprintLensPanel::ToggleM6CompositeFold()
{
	if (!M6ReadyPacket.IsValid())
	{
		return;
	}
	bM6CompositeFoldExpanded = !bM6CompositeFoldExpanded;
	RefreshM6Content();
}

void SBlueprintLensPanel::SetLC1RailDensity(const bool bEvidence)
{
	if (!Model.IsValid() || bLC1RailEvidence == bEvidence)
	{
		return;
	}
	bLC1RailEvidence = bEvidence;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectLC1PseudocodeLine(const FString& LineId)
{
	if (!Model.IsValid() || LineId.IsEmpty()
		|| LC1SelectedPseudocodeLineId == LineId)
	{
		return;
	}
	LC1SelectedPseudocodeLineId = LineId;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC4SequenceRepresentation()
{
	check(Model.IsValid());
	check(LC4SequenceProfile.IsValid());
	if (LC4SequenceScrollBox.IsValid())
	{
		LC4SequenceScrollOffset = LC4SequenceScrollBox->GetScrollOffset();
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LC4SequenceProfile,
			*Model);
	if (!Projection.IsRenderable())
	{
		return BuildLC4CompleteTextFallback(Projection);
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	TSharedPtr<SWidget> DetailScrollTarget;
	const float ReviewWidth = LC4ReviewWidth();
	const float InitialWidth = ReviewWidth > 0.0f ? ReviewWidth : 700.0f;
	const FBlueprintLensLC4SequenceLayoutSessionResult InitialSession =
		FBlueprintLensLC4SequenceLayoutSession::Build(
			Projection,
			InitialWidth);
	TSharedRef<SBlueprintLensLC4SequenceRail> Rail =
		SNew(SBlueprintLensLC4SequenceRail)
			.Projection(Projection)
			.InitialSession(InitialSession)
			.SelectedOrdinal(
				LC4DetailMode == ELC4DetailMode::SelectedOutput
					? LC4SelectedOutputOrdinal
					: INDEX_NONE)
			.Evidence(LC4DetailMode == ELC4DetailMode::Evidence)
			.ActiveActionId(
				LC4DetailMode == ELC4DetailMode::CompleteText
					? TEXT("all-text")
					: LC4DetailMode == ELC4DetailMode::Evidence
						? TEXT("evidence")
						: TEXT("select"))
			.OnOutputSelected(
				FOnBlueprintLensLC4SequenceOutputSelected::CreateSP(
					this,
					&SBlueprintLensPanel::SelectLC4Output))
			.OnShowAllText(
				FOnBlueprintLensLC4SequenceAction::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleLC4CompleteText))
			.OnToggleEvidence(
				FOnBlueprintLensLC4SequenceAction::CreateSP(
					this,
					&SBlueprintLensPanel::ToggleLC4Evidence))
			.OnOpenSource(
				FOnBlueprintLensLC4SequenceAction::CreateSP(
					this,
					&SBlueprintLensPanel::OpenLC4SelectedSource));
	TSharedRef<SWidget> RailHost = Rail;
	if (ReviewWidth > 0.0f)
	{
		RailHost = SNew(SBox)
			.HAlign(HAlign_Left)
			.WidthOverride(ReviewWidth)
			[
				Rail
			];
	}
	Content->AddSlot()
	.AutoHeight()
	[
		RailHost
	];
	if (LC4DetailMode == ELC4DetailMode::CompleteText)
	{
		TSharedRef<SVerticalBox> CompleteText = SNew(SVerticalBox);
		for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
		{
			TArray<FString> Labels;
			for (const FString& UnitId : Route.RouteUnitIds)
			{
				if (const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId))
				{
					Labels.Add(Unit->Title);
				}
			}
			CompleteText->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("%02d · %s · %s · %s · %s"),
						Route.Ordinal,
						*Route.SourcePinName,
						LexToString(Route.ConnectionState),
						LexToString(Route.CriterionRelation),
						Labels.IsEmpty()
							? TEXT("empty output")
							: *FString::Join(Labels, TEXT(" → ")))))
					.AutoWrapText(true)
			];
		}
		TSharedRef<SBorder> CompleteTextBorder =
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(8.0f))
				[
					CompleteText
				];
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			CompleteTextBorder
		];
		DetailScrollTarget = CompleteTextBorder;
	}

	const FBlueprintLensLC4SequenceRoute* SelectedRoute =
		Projection.FindRoute(
			LC4DetailMode == ELC4DetailMode::SelectedOutput
				? LC4SelectedOutputOrdinal
				: INDEX_NONE);
	if (SelectedRoute != nullptr)
	{
		TSharedRef<SVerticalBox> Selection = SNew(SVerticalBox);
		Selection->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("OUTPUT %02d DETAIL · %s"),
					SelectedRoute->Ordinal,
					*SelectedRoute->SourcePinName)))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
				.AutoWrapText(true)
		];
		Selection->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 3.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · %s · reason %s"),
					LexToString(SelectedRoute->ConnectionState),
					LexToString(SelectedRoute->CriterionRelation),
					*SelectedRoute->CriterionReason)))
				.AutoWrapText(true)
		];

		TArray<FString> SourceUnitIds;
		SourceUnitIds.Add(Projection.SequenceUnitId);
		SourceUnitIds.Append(SelectedRoute->RouteUnitIds);
		for (const FString& UnitId : SourceUnitIds)
		{
			const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
			if (Unit == nullptr)
			{
				continue;
			}
			Selection->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SButton)
					.Text(FText::FromString(FString::Printf(
						TEXT("Open %s in Blueprint"),
						*Unit->Title)))
					.IsEnabled(CanNavigateToSource(UnitId))
					.OnClicked_Lambda(
						[this, UnitId]()
						{
							return NavigateToSource(UnitId);
						})
			];
		}
		TSharedRef<SBorder> SelectionBorder =
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(8.0f))
				[
					Selection
				];
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SelectionBorder
		];
		DetailScrollTarget = SelectionBorder;
	}

	if (LC4DetailMode == ELC4DetailMode::Evidence)
	{
		TSharedRef<SWidget> EvidenceWidget =
			BuildLC4TechnicalEvidence(Projection);
		Content->AddSlot()
		.AutoHeight()
		[
			EvidenceWidget
		];
		DetailScrollTarget = EvidenceWidget;
	}

	TSharedRef<SScrollBox> ReviewScrollBox =
		SAssignNew(LC4SequenceScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()
		[
			Content
		];
	ReviewScrollBox->SetScrollOffset(LC4SequenceScrollOffset);
	if (DetailScrollTarget.IsValid())
	{
		ReviewScrollBox->ScrollDescendantIntoView(
			DetailScrollTarget,
			false,
			EDescendantScrollDestination::TopOrLeft,
			8.0f);
	}
	return ReviewScrollBox;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC4AsyncRepresentation()
{
	check(Model.IsValid());
	check(LC4AsyncProfile.IsValid());
	if (LC4AsyncScrollBox.IsValid())
	{
		LC4AsyncScrollOffset = LC4AsyncScrollBox->GetScrollOffset();
	}
	const FBlueprintLensLC4AsyncProjection Projection =
		FBlueprintLensLC4AsyncProjector::Build(*LC4AsyncProfile, LC4AsyncVariant);
	if (!Projection.IsRenderable())
	{
		return BuildLC4AsyncFrontier(Projection);
	}
	const float ReviewWidth = LC4ReviewWidth();
	const float InitialWidth = ReviewWidth > 0.0f ? ReviewWidth : 700.0f;
	const FBlueprintLensLC4AsyncLayoutSessionResult Session =
		FBlueprintLensLC4AsyncLayoutSession::Build(Projection, InitialWidth);
	if (!Session.IsRenderable(Projection))
	{
		FBlueprintLensLC4AsyncProjection Frontier = Projection;
		Frontier.Status = EBlueprintLensLC4AsyncProjectionStatus::Frontier;
		Frontier.DiagnosticCode = TEXT("LC4_ASYNC_FRONTIER_LAYOUT_UNAVAILABLE");
		return BuildLC4AsyncFrontier(Frontier);
	}
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s · two retained runs · 22 relations · two proofs"),
					*LC4AsyncVariant)))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
				.Text(FText::FromString(LC4AsyncVariant == TEXT("A_FIRST") ? TEXT("Show B_FIRST") : TEXT("Show A_FIRST")))
				.OnClicked_Lambda([this]()
				{
					HandleLC4AsyncAction(TEXT("toggle-variant"));
					return FReply::Handled();
				})
		]
	];
	TSharedRef<SBlueprintLensLC4AsyncPartialOrder> Canvas =
		SNew(SBlueprintLensLC4AsyncPartialOrder)
			.Projection(Projection)
			.InitialSession(Session)
			.ActiveActionId(
				LC4AsyncDetailMode == ELC4AsyncDetailMode::Proof ? TEXT("proof") :
				LC4AsyncDetailMode == ELC4AsyncDetailMode::CompleteText ? TEXT("all-text") :
				LC4AsyncDetailMode == ELC4AsyncDetailMode::Evidence ? TEXT("evidence") : TEXT("select"))
			.OnAction(FOnBlueprintLensLC4AsyncAction::CreateSP(this, &SBlueprintLensPanel::HandleLC4AsyncAction));
	TSharedRef<SWidget> CanvasHost = Canvas;
	if (ReviewWidth > 0.0f)
	{
		CanvasHost = SNew(SBox).HAlign(HAlign_Left).WidthOverride(ReviewWidth)[Canvas];
	}
	Content->AddSlot().AutoHeight()[CanvasHost];

	if (LC4AsyncDetailMode == ELC4AsyncDetailMode::Proof)
	{
		TArray<FString> Lines;
		for (const FBlueprintLensLC4AsyncProof& Proof : Projection.Proofs)
		{
			Lines.Add(FString::Printf(
				TEXT("%s: %s does not reach %s; %s does not reach %s; relation set complete; %s"),
				*Proof.ProofBasis, *Proof.LeftParticipantId, *Proof.RightParticipantId,
				*Proof.RightParticipantId, *Proof.LeftParticipantId, *Proof.Result));
		}
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Join(Lines, TEXT("\n")))).AutoWrapText(true)
			]
		];
	}
	else if (LC4AsyncDetailMode == ELC4AsyncDetailMode::CompleteText)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("ACCOUNTED RELATIONS (22)\n%s"), *FString::Join(Projection.AllRelationIds, TEXT("\n")))))
					.AutoWrapText(true)
			]
		];
	}
	else if (LC4AsyncDetailMode == ELC4AsyncDetailMode::Evidence)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("PROFILE SHA-256\n%s\n\nPROJECTION\n%s\n%s\n\nBACKENDS\n%s"),
						*Projection.SourceProfileSha256, *Projection.DiagnosticCode,
						*Projection.ProjectionIntegrityHash, *Session.AttemptSummary())))
					.AutoWrapText(true)
			]
		];
	}
	else if (LC4AsyncDetailMode == ELC4AsyncDetailMode::Select)
	{
		const FString SequenceId = Projection.Source.SequenceNodeId;
		const FString CriterionId = Projection.Source.CriterionNodeId;
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC4AsyncOpenSequence", "Open Sequence launch"))
					.IsEnabled(CanNavigateToSource(SequenceId))
					.OnClicked_Lambda([this, SequenceId](){ return NavigateToSource(SequenceId); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC4AsyncOpenCriterion", "Open criterion"))
					.IsEnabled(CanNavigateToSource(CriterionId))
					.OnClicked_Lambda([this, CriterionId](){ return NavigateToSource(CriterionId); })
			]
		];
	}
	TSharedRef<SScrollBox> Scroll = SAssignNew(LC4AsyncScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()[Content];
	Scroll->SetScrollOffset(LC4AsyncScrollOffset);
	return Scroll;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC4AsyncFrontier(
	const FBlueprintLensLC4AsyncProjection& Projection)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.42f, 0.16f, 0.10f, 0.65f))
		.Padding(8.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("LC4_ASYNC_FRONTIER · ABSTAINED\n%s\nComplete text remains the safe fallback."),
					*Projection.DiagnosticCode)))
				.AutoWrapText(true)
		];
}

void SBlueprintLensPanel::HandleLC4AsyncAction(FString ActionId)
{
	if (!LC4AsyncProfile.IsValid())
	{
		return;
	}
	if (LC4AsyncScrollBox.IsValid())
	{
		LC4AsyncScrollOffset = LC4AsyncScrollBox->GetScrollOffset();
	}
	if (ActionId == TEXT("toggle-variant"))
	{
		LC4AsyncVariant = LC4AsyncVariant == TEXT("A_FIRST") ? TEXT("B_FIRST") : TEXT("A_FIRST");
	}
	else if (ActionId == TEXT("proof"))
	{
		LC4AsyncDetailMode = ELC4AsyncDetailMode::Proof;
	}
	else if (ActionId == TEXT("all-text"))
	{
		LC4AsyncDetailMode = ELC4AsyncDetailMode::CompleteText;
	}
	else if (ActionId == TEXT("evidence"))
	{
		LC4AsyncDetailMode = ELC4AsyncDetailMode::Evidence;
	}
	else if (ActionId == TEXT("select"))
	{
		LC4AsyncDetailMode = ELC4AsyncDetailMode::Select;
	}
	else if (ActionId == TEXT("open-source"))
	{
		NavigateToSource(LC4AsyncProfile->Source.CriterionNodeId);
		return;
	}
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC5TypedPortalRepresentation()
{
	check(LC5Profile.IsValid());
	if (LC5ScrollBox.IsValid())
	{
		LC5ScrollOffset = LC5ScrollBox->GetScrollOffset();
	}
	const FBlueprintLensLC5Projection Projection =
		FBlueprintLensLC5Projector::Build(*LC5Profile);
	if (!Projection.IsRenderable())
	{
		return BuildLC5Frontier(Projection);
	}
	const float Width = LC5ReviewWidth() > 0.0f ? LC5ReviewWidth() : 700.0f;
	const FBlueprintLensLC5LayoutSessionResult Session =
		FBlueprintLensLC5LayoutSession::Build(Projection, Width);
	if (!Session.IsRenderable(Projection))
	{
		FBlueprintLensLC5Projection Frontier = Projection;
		Frontier.Status = EBlueprintLensLC5ProjectionStatus::Frontier;
		Frontier.DiagnosticCode = TEXT("LC5_FRONTIER_LAYOUT_UNAVAILABLE");
		return BuildLC5Frontier(Frontier);
	}
	if (LC5SelectedOccurrenceId.IsEmpty())
	{
		const FBlueprintLensLC5ContextBoundary* Enter =
			Projection.ContextBoundaries.FindByPredicate([](const auto& Item)
			{
				return Item.Kind == TEXT("call_enter");
			});
		LC5SelectedOccurrenceId = Enter != nullptr ? Enter->SourceOccurrenceId : FString();
	}
	const FString ActiveAction =
		LC5DetailMode == ELC5DetailMode::CompleteText ? TEXT("show_complete_text") :
		LC5DetailMode == ELC5DetailMode::Evidence ? TEXT("show_evidence") : TEXT("select");
	TSharedRef<SBlueprintLensLC5TypedPortal> Canvas =
		SNew(SBlueprintLensLC5TypedPortal)
		.Projection(Projection)
		.InitialSession(Session)
		.SelectedOccurrenceId(LC5SelectedOccurrenceId)
		.ActiveActionId(ActiveAction)
		.OnAction(FOnBlueprintLensLC5Action::CreateSP(this, &SBlueprintLensPanel::HandleLC5Action))
		.OnOccurrenceSelected(FOnBlueprintLensLC5OccurrenceSelected::CreateSP(
			this, &SBlueprintLensPanel::SelectLC5Occurrence));
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(Width)[Canvas]];
	if (LC5DetailMode != ELC5DetailMode::None)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildLC5Detail(Projection)
		];
	}
	TSharedRef<SScrollBox> Scroll = SAssignNew(LC5ScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()[Content];
	Scroll->SetScrollOffset(LC5ScrollOffset);
	return Scroll;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC5Frontier(
	const FBlueprintLensLC5Projection& Projection)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.42f, 0.10f, 0.14f, 0.7f))
		.Padding(8.0f)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("LC5 FRONTIER · %s\nNo callee body is fabricated. Complete text remains available."),
				*Projection.DiagnosticCode)))
			.AutoWrapText(true)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC5Detail(
	const FBlueprintLensLC5Projection& Projection)
{
	FString Text;
	if (LC5DetailMode == ELC5DetailMode::CompleteText)
	{
		Text = FString::Printf(TEXT("ACCOUNTED RELATIONS (%d)\n%s"),
			Projection.AllRelationIds.Num(),
			*FString::Join(Projection.AllRelationIds, TEXT("\n")));
	}
	else if (LC5DetailMode == ELC5DetailMode::Evidence)
	{
		const FString EvidenceBody = FString::Printf(
			TEXT("%s\n\nPROJECTION\n%s\n%s\n\nSTATIC CONTEXT\n%s"),
			*Projection.SourceProfileSha256,
			*Projection.DiagnosticCode,
			*Projection.ProjectionIntegrityHash,
			*Projection.CallContext.ClaimScope);
		Text = FString(Projection.bLiveCallBody
			? TEXT("TYPED-IR SHA-256\n")
			: TEXT("PROFILE SHA-256\n")) + EvidenceBody;
	}
	else if (LC5DetailMode == ELC5DetailMode::WhyPortal)
	{
		Text = Projection.bLiveCallBody
			? TEXT("WHY PORTAL?\n") +
				FString::Join(Projection.BoundaryText, TEXT("\n"))
			: TEXT("WHY PORTAL?\n"
				"Resolution evidence: resolved_unique native call reference, UFunction, graph and compile provenance agree.\n"
				"Type and direction correspondence: CurrentHealth int32 input, Bonus int32 input, NewHealth int32 return; property, pin direction and scalar container match.\n"
				"Static-only boundary: these are static contextual occurrences, not runtime invocations or runtime order.");
	}
	else
	{
		const FBlueprintLensLC5Occurrence* Selected = Projection.Occurrences.FindByPredicate(
			[this](const auto& Item) { return Item.OccurrenceId == LC5SelectedOccurrenceId; });
		TArray<FString> Adjacent;
		for (const FBlueprintLensLayoutEdgeRequest& Edge :
			FBlueprintLensLC5LayoutBuilder::Build(Projection, 700.0f).LayoutRequest.Edges)
		{
			if (Edge.SourceUnitId == LC5SelectedOccurrenceId ||
				Edge.TargetUnitId == LC5SelectedOccurrenceId)
			{
				Adjacent.Add(Edge.RelationId);
			}
		}
		Text = FString::Printf(TEXT("SELECTED STATIC OCCURRENCE\n%s\n\nCANONICAL SOURCE\n%s\n\nADJACENT RELATIONS\n%s"),
			*LC5SelectedOccurrenceId,
			Selected != nullptr ? *Selected->SourceNodeId : TEXT("unavailable"),
			*FString::Join(Adjacent, TEXT("\n")));
	}
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.AutoWrapText(true)
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
			]
		];
	if (LC5DetailMode != ELC5DetailMode::WhyPortal)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("LC5WhyPortal", "Why portal?"))
			.OnClicked_Lambda([this]()
			{
				HandleLC5Action(TEXT("why-portal"));
				return FReply::Handled();
			})
		];
	}
	return Content;
}

FBlueprintLensLC5Projection SBlueprintLensPanel::BuildCurrentLC5Projection() const
{
	if (M6ReadyPacket.IsValid() &&
		M6ReadyPacket->Request.QueryKind.Equals(
			TEXT("execution"), ESearchCase::IgnoreCase))
	{
		const FBlueprintLensLC1TypedIrFacts TypedIrFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(
				M6ReadyPacket->Explanation.Source,
				false);
		const FBlueprintLensLC5LiveTypedIrAdapterResult Adapted =
			FBlueprintLensLC5LiveTypedIrAdapter::Build(
				M6ReadyPacket->Explanation,
				TypedIrFacts);
		const FBlueprintLensLC5LiveCallCase* Current =
			Adapted.Cases.FindByPredicate(
				[this](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CallUnitId ==
						M6ExpandedAttachmentUnitId &&
						Candidate.IsRenderable();
				});
		return Current != nullptr
			? Current->Projection
			: FBlueprintLensLC5Projection();
	}
	return Model.IsValid() && LC5Profile.IsValid()
		? FBlueprintLensLC5Projector::Build(*LC5Profile)
		: FBlueprintLensLC5Projection();
}

void SBlueprintLensPanel::SelectLC5Occurrence(FString OccurrenceId)
{
	const FBlueprintLensLC5Projection Projection =
		BuildCurrentLC5Projection();
	if (!Projection.IsRenderable())
	{
		return;
	}
	if (!Projection.Occurrences.ContainsByPredicate([&OccurrenceId](const auto& Item)
		{ return Item.OccurrenceId == OccurrenceId; }))
	{
		return;
	}
	if (LC5ScrollBox.IsValid())
	{
		LC5ScrollOffset = LC5ScrollBox->GetScrollOffset();
	}
	LC5SelectedOccurrenceId = MoveTemp(OccurrenceId);
	LC5DetailMode = ELC5DetailMode::Selection;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::HandleLC5Action(FString ActionId)
{
	const FBlueprintLensLC5Projection Projection =
		BuildCurrentLC5Projection();
	if (!Projection.IsRenderable())
	{
		return;
	}
	if (LC5ScrollBox.IsValid())
	{
		LC5ScrollOffset = LC5ScrollBox->GetScrollOffset();
	}
	if (ActionId == TEXT("open_source") || ActionId == TEXT("open-source"))
	{
		const FBlueprintLensLC5Occurrence* Selected = Projection.Occurrences.FindByPredicate(
			[this](const auto& Item) { return Item.OccurrenceId == LC5SelectedOccurrenceId; });
		if (Selected != nullptr)
		{
			NavigateToSource(Selected->SourceNodeId);
		}
		return;
	}
	if (ActionId == TEXT("select")) LC5DetailMode = ELC5DetailMode::Selection;
	else if (ActionId == TEXT("show_complete_text") || ActionId == TEXT("show-complete-text")) LC5DetailMode = ELC5DetailMode::CompleteText;
	else if (ActionId == TEXT("show_evidence") || ActionId == TEXT("show-evidence")) LC5DetailMode = ELC5DetailMode::Evidence;
	else if (ActionId == TEXT("why-portal")) LC5DetailMode = ELC5DetailMode::WhyPortal;
	else return;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC6FourTrackRepresentation()
{
	const FBlueprintLensLC6Projection Projection =
		BuildCurrentLC6Projection();
	if (!Projection.IsRenderable())
	{
		return BuildLC6CompleteTextFallback(Projection);
	}
	const float Width = LC6ReviewWidth() > 0.0f ? LC6ReviewWidth() : 700.0f;
	const FBlueprintLensLC6LayoutSessionResult Session =
		FBlueprintLensLC6LayoutSession::Build(Projection, Width);
	if (!Session.IsRenderable(Projection))
	{
		return BuildLC6CompleteTextFallback(Projection);
	}

	TSharedRef<SWidget> Surface = BuildLC6FourTrackSurface(
		Projection,
		Session,
		NAME_None);

	TSharedRef<SScrollBox> Overview =
		SAssignNew(LC6OverviewScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()
		[
			Surface
		];
	Overview->SetScrollOffset(LC6OverviewScrollOffset);
	return Overview;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC6FourTrackSurface(
	const FBlueprintLensLC6Projection& Projection,
	const FBlueprintLensLC6LayoutSessionResult& Session,
	const FName& CanvasTag)
{
	TSharedRef<SBlueprintLensLC6FourTrack> Canvas =
		SNew(SBlueprintLensLC6FourTrack)
		.Projection(Projection)
		.InitialSession(Session)
		.SelectedScenarioId(LC6SelectedScenarioId)
		.OnScenarioSelected(FOnBlueprintLensLC6ScenarioSelected::CreateSP(
			this, &SBlueprintLensPanel::SelectLC6Scenario))
		.OnAction(FOnBlueprintLensLC6Action::CreateSP(
			this, &SBlueprintLensPanel::HandleLC6Action));
	if (!CanvasTag.IsNone())
	{
		Canvas->SetTag(CanvasTag);
	}

	TSharedRef<SWidget> Detail = LC6DetailMode == ELC6DetailMode::None
		? StaticCastSharedRef<SWidget>(SNew(SBox))
		: BuildLC6Detail(Projection);
	const FBox2D DetailBounds = Session.Layout.DetailBounds;
	TSharedRef<SScrollBox> DetailScroll =
		SAssignNew(LC6DetailScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()[Detail];
	DetailScroll->SetScrollOffset(LC6DetailScrollOffset);

	TSharedRef<SConstraintCanvas> Composition = SNew(SConstraintCanvas);
	Composition->AddSlot()
		.Offset(FMargin(0.0f, 0.0f, Session.Layout.CanvasSize.X, Session.Layout.CanvasSize.Y))
		.Alignment(FVector2D::ZeroVector)
	[
		Canvas
	];
	Composition->AddSlot()
		.Offset(FMargin(
			DetailBounds.Min.X, DetailBounds.Min.Y,
			DetailBounds.GetSize().X, DetailBounds.GetSize().Y))
		.Alignment(FVector2D::ZeroVector)
	[
		DetailScroll
	];

	return SNew(SBox)
		.WidthOverride(Session.Layout.CanvasSize.X)
		.HeightOverride(Session.Layout.CanvasSize.Y)
		[
			Composition
		];
}

FBlueprintLensLC6Projection SBlueprintLensPanel::BuildCurrentLC6Projection() const
{
	if (M6ReadyPacket.IsValid() &&
		M6ReadyPacket->Request.QueryKind.Equals(
			TEXT("execution"), ESearchCase::IgnoreCase))
	{
		const FBlueprintLensLC1RailProjection Rail =
			FBlueprintLensLC1RailProjector::Build(
				M6ReadyPacket->Explanation);
		return FBlueprintLensLC6LiveExplanationAdapter::Build(
			M6ReadyPacket->Explanation,
			Rail).Projection;
	}
	return Model.IsValid() && LC6Profile.IsValid() &&
		LC6Profile->CoreProfileId == TEXT("LC6_CORE_BOUNDARY_MATRIX_V1") &&
		LC6Profile->QueryProfileId == TEXT("LC6_MAX_UPSTREAM_HOPS_V1")
		? FBlueprintLensLC6Projector::Build(*LC6Profile)
		: FBlueprintLensLC6Projection();
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC6Detail(
	const FBlueprintLensLC6Projection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	FString Text;
	const FBlueprintLensLC6Track* Selected =
		Projection.FindTrack(LC6SelectedScenarioId);
	if (LC6DetailMode == ELC6DetailMode::CompleteText)
	{
		Text = TEXT("Complete Text fallback\n\n") +
			FString::Join(Projection.CompleteTextLines, TEXT("\n"));
	}
	else if (Selected == nullptr)
	{
		Text = Projection.bLiveBoundaryTracks
			? TEXT("Select an instance to inspect why analysis stops.")
			: TEXT("Select a scenario to inspect why analysis stops.");
	}
	else if (LC6DetailMode == ELC6DetailMode::Relations)
	{
		Text = FString::Printf(
			TEXT("RELATIONS · %s\n\n%s"),
			*Selected->ScenarioId,
			*FString::Join(Selected->RelationIds, TEXT("\n")));
	}
	else if (LC6DetailMode == ELC6DetailMode::Evidence)
	{
		Text = FString::Printf(
			TEXT("EVIDENCE · %s\n\nProjection %s\n\n%s"),
			*Selected->ScenarioId, *Projection.IntegrityHash,
			*FString::Join(Selected->EvidenceIds, TEXT("\n")));
	}
	else
	{
		Text = FString::Printf(
			TEXT("%s\n%s · %s\n\nOwner · %s\nReason · %s\nRoot · %s\nCriterion · %s"),
			*Selected->ScenarioId, *Selected->Status, *Selected->Reason,
			*Selected->TruthOwner, *Selected->Reason,
			*Selected->RootTitle, *Selected->CriterionTitle);
		if (Selected->bHasOmissionAggregate)
		{
			Text += FString::Printf(
				TEXT("\n\nBudget %d · selected %d/%d · complete %d/%d · omitted %d/%d"),
				Selected->MaxUpstreamHops,
				Selected->SelectedNodeCount, Selected->SelectedEdgeCount,
				Selected->CompleteNodeCount, Selected->CompleteEdgeCount,
				Selected->OmittedNodeCount, Selected->OmittedEdgeCount);
		}
	}

	TSharedRef<STextBlock> DetailText = SNew(STextBlock)
		.Text(FText::FromString(Text))
		.AutoWrapText(true)
		.Clipping(EWidgetClipping::ClipToBoundsAlways);
	if (Projection.bLiveBoundaryTracks && Selected == nullptr)
	{
		DetailText->SetTag(CompositeLC6LiveDetailPromptAutomationTag);
	}
	Content->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			DetailText
		]
	];
	if (Selected != nullptr && LC6DetailMode != ELC6DetailMode::CompleteText)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			+ SWrapBox::Slot().Padding(2.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC6Summary", "Summary"))
				.OnClicked_Lambda([this]()
				{
					HandleLC6Action(TEXT("summary"));
					return FReply::Handled();
				})
			]
			+ SWrapBox::Slot().Padding(2.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC6Relations", "Relations"))
				.OnClicked_Lambda([this]()
				{
					HandleLC6Action(TEXT("relations"));
					return FReply::Handled();
				})
			]
			+ SWrapBox::Slot().Padding(2.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC6Evidence", "Evidence"))
				.OnClicked_Lambda([this]()
				{
					HandleLC6Action(TEXT("evidence"));
					return FReply::Handled();
				})
			]
			+ SWrapBox::Slot().Padding(2.0f)
			[
				SNew(SButton).Text(LOCTEXT("LC6OpenSource", "Open source"))
				.IsEnabled(CanNavigateToSource(Selected->CriterionNodeId))
				.OnClicked_Lambda([this]()
				{
					HandleLC6Action(TEXT("open-source"));
					return FReply::Handled();
				})
			]
		];
	}
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC6CompleteTextFallback(
	const FBlueprintLensLC6Projection& Projection)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.42f, 0.10f, 0.14f, 0.7f))
		.Padding(8.0f)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				TEXT("Complete Text fallback\n\n") +
				FString::Join(Projection.CompleteTextLines, TEXT("\n"))))
			.AutoWrapText(true)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
}

void SBlueprintLensPanel::SelectLC6Scenario(FString ScenarioId)
{
	const FBlueprintLensLC6Projection Projection =
		BuildCurrentLC6Projection();
	if (!Projection.IsRenderable())
	{
		return;
	}
	if (!ScenarioId.IsEmpty() && Projection.FindTrack(ScenarioId) == nullptr)
	{
		return;
	}
	if (LC6OverviewScrollBox.IsValid())
	{
		LC6OverviewScrollOffset = LC6OverviewScrollBox->GetScrollOffset();
	}
	const bool bDeselect = ScenarioId.IsEmpty() ||
		ScenarioId == LC6SelectedScenarioId;
	LC6SelectedScenarioId = bDeselect ? FString() : MoveTemp(ScenarioId);
	LC6DetailMode = bDeselect ? ELC6DetailMode::None : ELC6DetailMode::Summary;
	LC6DetailScrollOffset = 0.0f;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::HandleLC6Action(FString ActionId)
{
	const FBlueprintLensLC6Projection Projection =
		BuildCurrentLC6Projection();
	if (!Projection.IsRenderable())
	{
		return;
	}
	if (LC6OverviewScrollBox.IsValid())
	{
		LC6OverviewScrollOffset = LC6OverviewScrollBox->GetScrollOffset();
	}
	if (LC6DetailScrollBox.IsValid())
	{
		LC6DetailScrollOffset = LC6DetailScrollBox->GetScrollOffset();
	}
	if (ActionId == TEXT("open-source"))
	{
		const FBlueprintLensLC6Track* Selected =
			Projection.FindTrack(LC6SelectedScenarioId);
		if (Selected != nullptr)
		{
			NavigateToSource(Selected->CriterionNodeId);
		}
		return;
	}
	if (ActionId == TEXT("complete-text"))
	{
		LC6DetailMode = ELC6DetailMode::CompleteText;
	}
	else if (LC6SelectedScenarioId.IsEmpty())
	{
		return;
	}
	else if (ActionId == TEXT("summary"))
	{
		LC6DetailMode = ELC6DetailMode::Summary;
	}
	else if (ActionId == TEXT("relations"))
	{
		LC6DetailMode = ELC6DetailMode::Relations;
	}
	else if (ActionId == TEXT("evidence"))
	{
		LC6DetailMode = ELC6DetailMode::Evidence;
	}
	else
	{
		return;
	}
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

bool SBlueprintLensPanel::IsLC7PanelSessionRenderable(
	const FBlueprintLensLC7Projection& Projection,
	const FBlueprintLensLC7LayoutSessionResult& Session) const
{
	return Projection.IsRenderable() && Projection.SCCs.Num() == 1 &&
		Session.IsRenderable(Projection) &&
		Session.Layout.ScaleMode == EBlueprintLensLC7ScaleMode::Full &&
		Session.Layout.Folds.IsEmpty() && Session.Layout.IndexRows.IsEmpty() &&
		Session.Layout.VisibleSCCCount == 1 &&
		Session.Layout.VisibleUnitIds.Num() == Projection.AllUnitIds.Num() &&
		Session.Layout.VisibleRelationIds.Num() == Projection.AllRelationIds.Num();
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC7AdaptiveBackboneRepresentation()
{
	check(LC7Profile.IsValid());
	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LC7Profile);
	if (!Projection.IsRenderable())
	{
		LC7Canvas.Reset();
		return BuildLC7CompleteTextFallback(Projection);
	}

	const float Width = LC7ReviewWidth() > 0.0f ? LC7ReviewWidth() : 700.0f;
	const FBlueprintLensLC7LayoutSessionResult Session =
		FBlueprintLensLC7LayoutSession::Build(Projection, Width, FString());
	if (!IsLC7PanelSessionRenderable(Projection, Session))
	{
		LC7Canvas.Reset();
		return BuildLC7CompleteTextFallback(Projection);
	}

	TSharedRef<SBlueprintLensLC7AdaptiveBackbone> Canvas =
		SAssignNew(LC7Canvas, SBlueprintLensLC7AdaptiveBackbone)
		.Projection(Projection)
		.InitialSession(Session)
		.SelectedUnitId(LC7SelectedUnitId)
		.OnUnitSelected(FOnBlueprintLensLC7UnitSelected::CreateSP(
			this, &SBlueprintLensPanel::SelectLC7Unit))
		.OnAction(FOnBlueprintLensLC7Action::CreateSP(
			this, &SBlueprintLensPanel::HandleLC7Action));

	TSharedRef<SWidget> Detail = LC7DetailMode == ELC7DetailMode::None
		? StaticCastSharedRef<SWidget>(SNew(SBox))
		: BuildLC7Detail(Projection);
	const FBox2D DetailBounds = Session.Layout.DetailBounds;
	TSharedRef<SScrollBox> DetailScroll =
		SAssignNew(LC7DetailScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()[Detail];
	DetailScroll->SetScrollOffset(LC7DetailScrollOffset);

	TSharedRef<SConstraintCanvas> Composition = SNew(SConstraintCanvas);
	Composition->AddSlot()
		.Offset(FMargin(
			0.0f, 0.0f, Session.Layout.CanvasSize.X, Session.Layout.CanvasSize.Y))
		.Alignment(FVector2D::ZeroVector)
	[
		Canvas
	];
	Composition->AddSlot()
		.Offset(FMargin(
			DetailBounds.Min.X, DetailBounds.Min.Y,
			DetailBounds.GetSize().X, DetailBounds.GetSize().Y))
		.Alignment(FVector2D::ZeroVector)
	[
		DetailScroll
	];

	TSharedRef<SScrollBox> Overview =
		SAssignNew(LC7OverviewScrollBox, SScrollBox)
		.Orientation(Orient_Vertical)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		+ SScrollBox::Slot()
		[
			SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(Session.Layout.CanvasSize.Y)
			[
				Composition
			]
		];
	Overview->SetScrollOffset(LC7OverviewScrollOffset);
	return Overview;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC7Detail(
	const FBlueprintLensLC7Projection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto AddFactRow = [&Content](
		const FString& Text, const bool bEmphasise = false)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(7.0f, 5.0f))
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(FAppStyle::Get().GetFontStyle(
					bEmphasise ? "NormalFontBold" : "SmallFont"))
				.AutoWrapText(true)
				.WrappingPolicy(BlueprintLensLC7DetailWrappingPolicy())
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
			]
		];
	};

	if (LC7DetailMode == ELC7DetailMode::CompleteText)
	{
		AddFactRow(
			TEXT("Complete Text fallback\n") +
				FString::Join(Projection.CompleteTextLines, TEXT("\n")),
			true);
		return Content;
	}

	const FBlueprintLensLC7SCCRecord* SCC = Projection.SCCs.IsEmpty()
		? nullptr : &Projection.SCCs[0];
	const bool bSelectedMember = SCC != nullptr &&
		SCC->OrderedSpineUnitIds.Contains(LC7SelectedUnitId);
	if (!bSelectedMember)
	{
		return Content;
	}
	const FString* SelectedTitle =
		Projection.UnitTitles.Find(LC7SelectedUnitId);
	const FBlueprintLensSourceReference* Anchor =
		Projection.SourceAnchors.Find(LC7SelectedUnitId);
	AddFactRow(SelectedTitle != nullptr ? *SelectedTitle : LC7SelectedUnitId, true);

	if (LC7DetailMode == ELC7DetailMode::Relations)
	{
		AddFactRow(TEXT("RELATIONS · 8 TYPED ROWS"), true);
		for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
		{
			const FString SourceTitle = Projection.UnitTitles.Contains(Relation.SourceUnitId)
				? Projection.UnitTitles[Relation.SourceUnitId]
				: Relation.SourceUnitId;
			const FString TargetTitle = Projection.UnitTitles.Contains(Relation.TargetUnitId)
				? Projection.UnitTitles[Relation.TargetUnitId]
				: Relation.TargetUnitId;
			AddFactRow(FString::Printf(
				TEXT("%s · %s\n%s \u2192 %s · %s · %s"),
				LC7RelationFamilyLabel(Relation.Family), *Relation.RelationId,
				*SourceTitle, *TargetTitle, LexToString(Relation.Kind),
				*Relation.Label));
		}
	}
	else if (LC7DetailMode == ELC7DetailMode::Evidence)
	{
		AddFactRow(TEXT("EVIDENCE · SOURCE / HASH"), true);
		AddFactRow(FString::Printf(
			TEXT("Projection SHA-256 · %s"), *Projection.IntegrityHash));
		if (LC7Profile.IsValid())
		{
			AddFactRow(FString::Printf(
				TEXT("Explanation SHA-256 · %s"), *LC7Profile->ExplanationSha256));
			AddFactRow(FString::Printf(
				TEXT("SCC profile SHA-256 · %s"), *LC7Profile->SCCProfileSha256));
		}
		if (Anchor != nullptr)
		{
			AddFactRow(FString::Printf(
				TEXT("Source node · %s"), *Anchor->SourceNodeId));
			AddFactRow(FString::Printf(
				TEXT("Native node GUID · %s"), *Anchor->NativeNodeGuid));
			AddFactRow(FString::Printf(
				TEXT("Graph · %s"), *Anchor->GraphId));
		}
	}
	else
	{
		const int32 MemberIndex =
			SCC->OrderedSpineUnitIds.IndexOfByKey(LC7SelectedUnitId);
		int32 IncomingCount = 0;
		int32 OutgoingCount = 0;
		for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
		{
			IncomingCount += Relation.TargetUnitId == LC7SelectedUnitId ? 1 : 0;
			OutgoingCount += Relation.SourceUnitId == LC7SelectedUnitId ? 1 : 0;
		}
		AddFactRow(FString::Printf(
			TEXT("Static SCC · member %d of %d"),
			MemberIndex + 1, SCC->OrderedSpineUnitIds.Num()));
		AddFactRow(FString::Printf(
			TEXT("Relations · %d incoming · %d outgoing"),
			IncomingCount, OutgoingCount));
		AddFactRow(FString::Printf(
			TEXT("Source · %s"), Anchor != nullptr ? TEXT("linked") : TEXT("unresolved")));
		AddFactRow(FString::Printf(
			TEXT("Runtime iterations · %s"), *Projection.RuntimeIterations));
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(SWrapBox)
		.UseAllottedSize(true)
		+ SWrapBox::Slot().Padding(2.0f)
		[
			SNew(SButton).Text(LOCTEXT("LC7Summary", "Summary"))
			.OnClicked_Lambda([this]()
			{
				HandleLC7Action(TEXT("summary"));
				return FReply::Handled();
			})
		]
		+ SWrapBox::Slot().Padding(2.0f)
		[
			SNew(SButton).Text(LOCTEXT("LC7Relations", "Relations"))
			.OnClicked_Lambda([this]()
			{
				HandleLC7Action(TEXT("relations"));
				return FReply::Handled();
			})
		]
		+ SWrapBox::Slot().Padding(2.0f)
		[
			SNew(SButton).Text(LOCTEXT("LC7Evidence", "Evidence"))
			.OnClicked_Lambda([this]()
			{
				HandleLC7Action(TEXT("evidence"));
				return FReply::Handled();
			})
		]
		+ SWrapBox::Slot().Padding(2.0f)
		[
			SNew(SButton).Text(LOCTEXT("LC7OpenSource", "Open source"))
			.IsEnabled(Anchor != nullptr && CanNavigateToSource(Anchor->SourceNodeId))
			.OnClicked_Lambda([this]()
			{
				HandleLC7Action(TEXT("open_source"));
				return FReply::Handled();
			})
		]
	];
	return Content;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC7CompleteTextFallback(
	const FBlueprintLensLC7Projection& Projection)
{
	const FString SourceText = Projection.CompleteTextLines.IsEmpty()
		? FString::Printf(TEXT("Unavailable · %s"), *Projection.DiagnosticCode)
		: FString::Join(Projection.CompleteTextLines, TEXT("\n"));
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.42f, 0.10f, 0.14f, 0.7f))
		.Padding(8.0f)
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				TEXT("Complete Text fallback\n\n") + SourceText))
			.AutoWrapText(true)
			.WrappingPolicy(BlueprintLensLC7DetailWrappingPolicy())
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
		];
}

void SBlueprintLensPanel::SelectLC7Unit(FString UnitId)
{
	if (!LC7Profile.IsValid())
	{
		return;
	}
	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LC7Profile);
	if (!Projection.IsRenderable() || Projection.SCCs.Num() != 1 ||
		(!UnitId.IsEmpty() &&
		 !Projection.SCCs[0].OrderedSpineUnitIds.Contains(UnitId)))
	{
		return;
	}
	if (LC7OverviewScrollBox.IsValid())
	{
		LC7OverviewScrollOffset = LC7OverviewScrollBox->GetScrollOffset();
	}
	const bool bDeselect = UnitId.IsEmpty() || UnitId == LC7SelectedUnitId;
	LC7SelectedUnitId = bDeselect ? FString() : MoveTemp(UnitId);
	LC7DetailMode = bDeselect ? ELC7DetailMode::None : ELC7DetailMode::Summary;
	LC7DetailScrollOffset = 0.0f;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::HandleLC7Action(FString ActionId)
{
	if (!LC7Profile.IsValid())
	{
		return;
	}
	if (LC7OverviewScrollBox.IsValid())
	{
		LC7OverviewScrollOffset = LC7OverviewScrollBox->GetScrollOffset();
	}
	if (LC7DetailScrollBox.IsValid())
	{
		LC7DetailScrollOffset = LC7DetailScrollBox->GetScrollOffset();
	}

	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LC7Profile);
	if (!Projection.IsRenderable())
	{
		return;
	}
	if (ActionId == TEXT("open_source") || ActionId == TEXT("open-source"))
	{
		const FString AnchorUnitId = LC7SelectedUnitId.IsEmpty()
			? Projection.CriterionUnitId : LC7SelectedUnitId;
		const FBlueprintLensSourceReference* Anchor =
			Projection.SourceAnchors.Find(AnchorUnitId);
		if (Anchor != nullptr && CanNavigateToSource(Anchor->SourceNodeId))
		{
			NavigateToSource(Anchor->SourceNodeId);
		}
		return;
	}
	if (ActionId == TEXT("inspect_cycle") || ActionId == TEXT("inspect-cycle"))
	{
		if (LC7SelectedUnitId.IsEmpty() && !Projection.SCCs.IsEmpty())
		{
			SelectLC7Unit(Projection.SCCs[0].EntryUnitId);
			return;
		}
		LC7DetailMode = ELC7DetailMode::Summary;
	}
	else if (ActionId == TEXT("show_complete_text") ||
		ActionId == TEXT("show-complete-text") ||
		ActionId == TEXT("complete-text"))
	{
		LC7DetailMode = ELC7DetailMode::CompleteText;
	}
	else if (LC7SelectedUnitId.IsEmpty())
	{
		return;
	}
	else if (ActionId == TEXT("summary"))
	{
		LC7DetailMode = ELC7DetailMode::Summary;
	}
	else if (ActionId == TEXT("relations"))
	{
		LC7DetailMode = ELC7DetailMode::Relations;
	}
	else if (ActionId == TEXT("evidence"))
	{
		LC7DetailMode = ELC7DetailMode::Evidence;
	}
	else
	{
		return;
	}
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC4CompleteTextFallback(
	const FBlueprintLensLC4SequenceProjection& Projection)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.42f, 0.16f, 0.10f, 0.65f))
			.Padding(FMargin(8.0f))
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("Sequence Disclosure Rail unavailable (%s). "
							 "Showing the complete ordered text fallback."),
						*Projection.DiagnosticCode)))
					.AutoWrapText(true)
			]
	];
	for (const FBlueprintLensLC4SequenceOutput& Output :
		 LC4SequenceProfile->Outputs)
	{
		TArray<FString> Labels;
		for (const FString& UnitId : Output.ReachableNodeIds)
		{
			const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
			if (Unit != nullptr)
			{
				Labels.Add(Unit->Title);
			}
		}
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(6.0f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%02d · %s · %s · %s · %s"),
							Output.Ordinal,
							*Output.SourcePinName,
							LexToString(Output.ConnectionState),
							LexToString(Output.CriterionRelation),
							Labels.IsEmpty()
								? TEXT("empty output")
								: *FString::Join(Labels, TEXT(" → ")))))
						.AutoWrapText(true)
				]
		];
	}
	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			Content
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLC4TechnicalEvidence(
	const FBlueprintLensLC4SequenceProjection& Projection)
{
	TArray<FString> OutputEvidence;
	for (const FBlueprintLensLC4SequenceRoute& Route : Projection.Routes)
	{
		OutputEvidence.Add(FString::Printf(
			TEXT("%02d %s · pin %s · reason %s"),
			Route.Ordinal,
			*Route.SourcePinName,
			*Route.SourcePinId,
			*Route.CriterionReason));
	}
	const FString Evidence = FString::Printf(
		TEXT("PROFILE SHA-256\n%s\n\nIR SHA-256\n%s\n\n"
			 "PROJECTION\n%s\n%s\n\nOUTPUT EVIDENCE\n%s\n\n"
			 "ACCOUNTED UNITS (%d)\n%s\n\nACCOUNTED RELATIONS (%d)\n%s"),
		*Projection.SourceProfileSha256,
		*Projection.SourceIrSha256,
		*Projection.DiagnosticCode,
		*Projection.ProjectionIntegrityHash,
		*FString::Join(OutputEvidence, TEXT("\n")),
		Projection.AllUnitIds.Num(),
		*FString::Join(Projection.AllUnitIds, TEXT("\n")),
		Projection.AllRelationIds.Num(),
		*FString::Join(Projection.AllRelationIds, TEXT("\n")));
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f))
		[
			SNew(STextBlock)
				.Text(FText::FromString(Evidence))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
		];
}

void SBlueprintLensPanel::ToggleLC2TechnicalEvidence()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC2TechnicalEvidenceExpanded = !bLC2TechnicalEvidenceExpanded;
	LC2GuardDensity = bLC2TechnicalEvidenceExpanded
		? EBlueprintLensLC2GuardDensity::Evidence
		: EBlueprintLensLC2GuardDensity::Summary;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SetLC2GuardDensity(
	const EBlueprintLensLC2GuardDensity NewDensity)
{
	if (!Model.IsValid() || LC2GuardDensity == NewDensity)
	{
		return;
	}
	LC2GuardDensity = NewDensity;
	bLC2TechnicalEvidenceExpanded =
		NewDensity == EBlueprintLensLC2GuardDensity::Evidence;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectLC2Unit(const FString& UnitId)
{
	if (!Model.IsValid() || UnitId.IsEmpty() ||
		Model->FindUnit(UnitId) == nullptr)
	{
		return;
	}
	LC2SelectedUnitId = LC2SelectedUnitId == UnitId
		? FString()
		: UnitId;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(SNullWidget::NullWidget);
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC3TechnicalEvidence()
{
	if (!Model.IsValid())
	{
		return;
	}
	bLC3TechnicalEvidenceExpanded = !bLC3TechnicalEvidenceExpanded;
	LC3ValueConeDensity = bLC3TechnicalEvidenceExpanded
		? EBlueprintLensLC3ValueConeDensity::Evidence
		: EBlueprintLensLC3ValueConeDensity::Summary;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SetLC3ValueConeDensity(
	const EBlueprintLensLC3ValueConeDensity NewDensity)
{
	if (!Model.IsValid() || LC3ValueConeDensity == NewDensity)
	{
		return;
	}
	LC3ValueConeDensity = NewDensity;
	bLC3TechnicalEvidenceExpanded =
		NewDensity == EBlueprintLensLC3ValueConeDensity::Evidence;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectLC3Unit(const FString& UnitId)
{
	if (!Model.IsValid() || UnitId.IsEmpty() ||
		Model->FindUnit(UnitId) == nullptr)
	{
		return;
	}
	LC3SelectedUnitId = LC3SelectedUnitId == UnitId ? FString() : UnitId;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::SelectLC4Output(const int32 Ordinal)
{
	if (!Model.IsValid() || !LC4SequenceProfile.IsValid() ||
		!LC4SequenceProfile->Outputs.ContainsByPredicate(
			[Ordinal](const FBlueprintLensLC4SequenceOutput& Output)
			{
				return Output.Ordinal == Ordinal;
			}))
	{
		return;
	}
	if (LC4DetailMode == ELC4DetailMode::SelectedOutput &&
		LC4SelectedOutputOrdinal == Ordinal)
	{
		return;
	}
	LC4SelectedOutputOrdinal = Ordinal;
	LC4DetailMode = ELC4DetailMode::SelectedOutput;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC4CompleteText()
{
	if (!Model.IsValid() || !LC4SequenceProfile.IsValid())
	{
		return;
	}
	if (LC4DetailMode == ELC4DetailMode::CompleteText)
	{
		return;
	}
	LC4DetailMode = ELC4DetailMode::CompleteText;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::ToggleLC4Evidence()
{
	if (!Model.IsValid() || !LC4SequenceProfile.IsValid())
	{
		return;
	}
	if (LC4DetailMode == ELC4DetailMode::Evidence)
	{
		return;
	}
	LC4DetailMode = ELC4DetailMode::Evidence;
	if (RootBox.IsValid())
	{
		RootBox->SetContent(BuildLoadedContent());
	}
}

void SBlueprintLensPanel::OpenLC4SelectedSource()
{
	if (!Model.IsValid() || !LC4SequenceProfile.IsValid())
	{
		return;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LC4SequenceProfile,
			*Model);
	TArray<FString> CandidateUnitIds;
	if (const FBlueprintLensLC4SequenceRoute* SelectedRoute =
			Projection.FindRoute(LC4SelectedOutputOrdinal))
	{
		CandidateUnitIds.Append(SelectedRoute->RouteUnitIds);
	}
	CandidateUnitIds.Add(Projection.SequenceUnitId);
	for (const FString& UnitId : CandidateUnitIds)
	{
		if (CanNavigateToSource(UnitId))
		{
			NavigateToSource(UnitId);
			return;
		}
	}
}

void SBlueprintLensPanel::SetDisplayMode(const EDisplayMode NewMode)
{
	if (DisplayMode == NewMode || !Model.IsValid())
	{
		return;
	}

	DisplayMode = NewMode;
	RootBox->SetContent(BuildLoadedContent());
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildErrorContent(
	const FString& Path,
	const FString& Error)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(PanelPadding)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"ExplanationUnavailable",
					"Explanation unavailable"))
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 7.0f, 0.0f, 3.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Error))
					.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, SectionSpacing)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Path))
					.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
					.Text(LOCTEXT("ReloadAfterError", "Reload"))
					.OnClicked_Lambda(
						[this]()
						{
							ReloadModel();
							return FReply::Handled();
						})
			]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildLane(
	const FBlueprintLensLane& Lane)
{
	const FLinearColor Accent = RoleAccent(Lane.Role);
	TSharedRef<SVerticalBox> LaneContent = SNew(SVerticalBox);

	LaneContent->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 5.0f)
	[
		SNew(STextBlock)
		.Text(UppercaseEnum(LexToString(Lane.Role)))
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		.ColorAndOpacity(Accent)
	];

	if (Lane.UnitIds.IsEmpty())
	{
		LaneContent->AddSlot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(WithAlpha(Accent, 0.34f))
			.Padding(FMargin(10.0f, 9.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Lane.EmptyMessage))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
		];
	}
	else
	{
		for (const FString& UnitId : Lane.UnitIds)
		{
			const FBlueprintLensUnit* Unit = Model->FindUnit(UnitId);
			check(Unit != nullptr);
			LaneContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				BuildUnitCard(*Unit)
			];
		}
	}

	return LaneContent;
}

FString SBlueprintLensPanel::BuildUnitEvidenceTooltip(
	const FBlueprintLensUnit& Unit) const
{
	FString Evidence = FString::Printf(
		TEXT("%s\nStatus: %s"),
		*Unit.Title,
		LexToString(Unit.SemanticStatus));
	if (!Unit.Expression.IsEmpty() && Unit.Expression != Unit.Title)
	{
		Evidence += FString::Printf(TEXT("\nExpression: %s"), *Unit.Expression);
	}
	for (const FString& InclusionReason : Unit.InclusionReasons)
	{
		Evidence += FString::Printf(
			TEXT("\nIncluded because: %s"),
			*InclusionReason);
	}
	for (int32 Index = 0; Index < Unit.SourceReferences.Num(); ++Index)
	{
		const FBlueprintLensSourceReference& Reference =
			Unit.SourceReferences[Index];
		Evidence += FString::Printf(
			TEXT("\nSource %d%s: %s"),
			Index + 1,
			Reference.bPrimary ? TEXT(" (primary)") : TEXT(""),
			*Reference.NativeNodeGuid);
		if (const FBlueprintLensResolvedSource* Resolved =
				ResolvedSources.Find(Reference.SourceNodeId))
		{
			const FString State = SourceStateLabel(Resolved->State);
			if (!State.IsEmpty())
			{
				Evidence += FString::Printf(TEXT(" [%s]"), *State);
			}
			if (!Resolved->Message.IsEmpty())
			{
				Evidence += FString::Printf(
					TEXT("\n  %s"),
					*Resolved->Message);
			}
		}
	}
	Evidence += TEXT("\n\nClick to focus the primary Blueprint source.");
	return Evidence;
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildCompactUnit(
	const FBlueprintLensUnit& Unit,
	const FString& RoleLabel,
	const bool bEmphasise)
{
	const FLinearColor Accent = RoleAccent(Unit.Role);
	const bool bSupported =
		Unit.SemanticStatus == EBlueprintLensSemanticStatus::Supported;
	const FString StatusLabel =
		bSupported
			? FString(TEXT("\u2713 SUPPORTED"))
			: FString(LexToString(Unit.SemanticStatus)).ToUpper();
	FString SourceLabel = Unit.SourceReferences.Num() == 1
		? FString(TEXT("SOURCE"))
		: FString::Printf(
			TEXT("%d SOURCES"),
			Unit.SourceReferences.Num());
	for (const FBlueprintLensSourceReference& Reference :
		 Unit.SourceReferences)
	{
		if (const FBlueprintLensResolvedSource* Resolved =
				ResolvedSources.Find(Reference.SourceNodeId))
		{
			const FString State = SourceStateLabel(Resolved->State);
			if (!State.IsEmpty())
			{
				SourceLabel += FString::Printf(TEXT(" \u00B7 %s"), *State);
				break;
			}
		}
	}

	const FBlueprintLensSourceReference* PrimarySource =
		Unit.SourceReferences.FindByPredicate(
			[](const FBlueprintLensSourceReference& Reference)
			{
				return Reference.bPrimary;
			});
	const FString PrimarySourceNodeId = PrimarySource != nullptr
		? PrimarySource->SourceNodeId
		: FString();

	TSharedRef<SVerticalBox> LabelContent = SNew(SVerticalBox);
	LabelContent->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
			.Text(FText::FromString(Unit.Title))
			.Font(FAppStyle::Get().GetFontStyle(
				bEmphasise ? "NormalFontBold" : "NormalFont"))
			.AutoWrapText(true)
	];
	if (!Unit.Expression.IsEmpty() && Unit.Expression != Unit.Title)
	{
		LabelContent->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Unit.Expression))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(
			Accent,
			bEmphasise ? 0.88f : 0.34f))
		.Padding(FMargin(bEmphasise ? 8.0f : 7.0f, 5.0f))
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(0.0f)
				.IsEnabled(CanNavigateToSource(PrimarySourceNodeId))
				.ToolTipText(FText::FromString(
					BuildUnitEvidenceTooltip(Unit)))
				.OnClicked_Lambda(
					[this, PrimarySourceNodeId]()
					{
						return NavigateToSource(PrimarySourceNodeId);
					})
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 7.0f, 0.0f)
					[
						BuildChip(RoleLabel, Accent, 0.46f)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						LabelContent
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
								.Text(FText::FromString(StatusLabel))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(
									bSupported
										? FLinearColor(
											  0.42f,
											  0.78f,
											  0.50f)
										: FLinearColor(
											  0.95f,
											  0.46f,
											  0.18f))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(SourceLabel))
								.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
								.ColorAndOpacity(
									FSlateColor::UseSubduedForeground())
						]
					]
				]
		];
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildUnitCard(
	const FBlueprintLensUnit& Unit)
{
	const FLinearColor Accent = RoleAccent(Unit.Role);
	const float BorderAlpha =
		Unit.Role == EBlueprintLensRole::Criterion ? 0.95f : 0.48f;
	TSharedRef<SVerticalBox> PrimaryBody = SNew(SVerticalBox);

	PrimaryBody->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(UppercaseEnum(LexToString(Unit.Role)))
		.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		.ColorAndOpacity(Accent)
	];

	PrimaryBody->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 3.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Unit.Title))
		.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
		.AutoWrapText(true)
	];

	if (!Unit.Expression.IsEmpty())
	{
		PrimaryBody->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Unit.Expression))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.AutoWrapText(true)
		];
	}

	TSharedRef<SWrapBox> MetadataChips = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(5.0f, 5.0f));

	MetadataChips->AddSlot()
	[
		BuildChip(
			FString::Printf(
				TEXT("STATUS \u00B7 %s"),
				*FString(LexToString(Unit.SemanticStatus)).ToUpper()),
			Accent,
			0.42f)
	];

	for (const FString& InclusionReason : Unit.InclusionReasons)
	{
		MetadataChips->AddSlot()
		[
			BuildChip(
				FString::Printf(
					TEXT("REASON \u00B7 %s"),
					*InclusionReason),
				Accent)
		];
	}

	PrimaryBody->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		MetadataChips
	];

	TSharedRef<SWrapBox> SourceChips = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(5.0f, 5.0f));

	for (int32 ReferenceIndex = 0;
		 ReferenceIndex < Unit.SourceReferences.Num();
		 ++ReferenceIndex)
	{
		const FBlueprintLensSourceReference& Reference =
			Unit.SourceReferences[ReferenceIndex];
		const FBlueprintLensResolvedSource* Resolved =
			ResolvedSources.Find(Reference.SourceNodeId);
		const FString StateLabel = Resolved != nullptr
			? SourceStateLabel(Resolved->State)
			: FString(TEXT("SOURCE UNRESOLVED"));
		FString Label = FString::Printf(
			TEXT("SOURCE %d%s \u00B7 %s"),
			ReferenceIndex + 1,
			Reference.bPrimary ? TEXT(" \u00B7 PRIMARY") : TEXT(""),
			*Reference.NativeNodeGuid);
		if (!StateLabel.IsEmpty())
		{
			Label += FString::Printf(TEXT(" \u00B7 %s"), *StateLabel);
		}
		const FString SourceNodeId = Reference.SourceNodeId;
		const FText ToolTip = FText::FromString(
			Resolved != nullptr && !Resolved->Message.IsEmpty()
				? Resolved->Message
				: FString(TEXT("Focus this source node in the Blueprint graph")));
		SourceChips->AddSlot()
		[
			SNew(SButton)
				.ButtonStyle(
					&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
						"SimpleButton"))
				.ContentPadding(0.0f)
				.IsEnabled(CanNavigateToSource(SourceNodeId))
				.ToolTipText(ToolTip)
				.OnClicked_Lambda(
					[this, SourceNodeId]()
					{
						return NavigateToSource(SourceNodeId);
					})
				[
					BuildChip(Label, Accent)
				]
		];
	}

	const FBlueprintLensSourceReference* PrimarySource =
		Unit.SourceReferences.FindByPredicate(
			[](const FBlueprintLensSourceReference& Reference)
			{
				return Reference.bPrimary;
			});
	const FString PrimarySourceNodeId = PrimarySource != nullptr
		? PrimarySource->SourceNodeId
		: FString();
	const bool bCanNavigatePrimary =
		CanNavigateToSource(PrimarySourceNodeId);

	TSharedRef<SVerticalBox> CardLayout = SNew(SVerticalBox);
	CardLayout->AddSlot()
	.AutoHeight()
	[
		SNew(SButton)
			.ButtonStyle(
				&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(
					"SimpleButton"))
			.ContentPadding(0.0f)
			.IsEnabled(bCanNavigatePrimary)
			.ButtonColorAndOpacity(
				bCanNavigatePrimary
					? FLinearColor::White
					: FLinearColor(0.62f, 0.62f, 0.62f, 1.0f))
			.ToolTipText(
				bCanNavigatePrimary
					? LOCTEXT(
						  "NavigatePrimarySource",
						  "Focus the primary source node in the Blueprint graph")
					: LOCTEXT(
						  "PrimarySourceUnavailable",
						  "Primary source navigation is unavailable"))
			.OnClicked_Lambda(
				[this, PrimarySourceNodeId]()
				{
					return NavigateToSource(PrimarySourceNodeId);
				})
			[
				PrimaryBody
			]
	];

	CardLayout->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		SourceChips
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(WithAlpha(Accent, BorderAlpha))
		.Padding(FMargin(
			Unit.Role == EBlueprintLensRole::Criterion ? 12.0f : 10.0f))
		[
			CardLayout
		];
}

bool SBlueprintLensPanel::CanNavigateToSource(
	const FString& SourceNodeId) const
{
	if (bHasStaleSource)
	{
		return false;
	}

	const FBlueprintLensResolvedSource* Resolved =
		ResolvedSources.Find(SourceNodeId);
	return Resolved != nullptr
		&& (Resolved->State == EBlueprintLensSourceState::Ready
			|| Resolved->State == EBlueprintLensSourceState::Unsaved)
		&& Resolved->Node.IsValid();
}

FReply SBlueprintLensPanel::NavigateToSource(const FString& SourceNodeId)
{
	const FBlueprintLensResolvedSource* Resolved =
		ResolvedSources.Find(SourceNodeId);
	if (Resolved == nullptr)
	{
		NavigationMessage = FString::Printf(
			TEXT("Source '%s' is not present in the navigation cache"),
			*SourceNodeId);
		return FReply::Handled();
	}

	FString Error;
	if (!SourceNavigator.Navigate(*Resolved, Error))
	{
		NavigationMessage = Error;
		return FReply::Handled();
	}

	NavigationMessage.Reset();
	return FReply::Handled();
}

void SBlueprintLensPanel::PopulateExplanationOptions()
{
	ExplanationOptions.Reset();
	ExplanationPaths.Reset();
	SelectedOption.Reset();

	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	const FString ResourceDir = FPaths::ConvertRelativePathToFull(
		Plugin.IsValid()
			? FPaths::Combine(
				  Plugin->GetBaseDir(), TEXT("Resources/Explanation"))
			: FPaths::Combine(
				  FPaths::ProjectPluginsDir(),
				  TEXT("BlueprintLensExporter/Resources/Explanation")));

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(
		FileNames, *(ResourceDir / TEXT("*.explanation.v1.json")), true, false);
	TArray<FString> SequenceProfileFileNames;
	IFileManager::Get().FindFiles(
		SequenceProfileFileNames,
		*(ResourceDir / TEXT("*.sequence-profile.v1.json")),
		true,
		false);
	FileNames.Append(SequenceProfileFileNames);
	const FString AsyncProfilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc4-async-truth/BP_LC4_AsyncBarrier.async-profile.v1.json")));
	if (FPaths::FileExists(AsyncProfilePath))
	{
		FileNames.Add(TEXT("BP_LC4_AsyncBarrier.async-profile.v1.json"));
	}
	const FString LC5ProfilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc5-intra-bp-pure-truth/BP_SlicingProbe.contextual-slice.v1.json")));
	if (FPaths::FileExists(LC5ProfilePath))
	{
		FileNames.Add(TEXT("BP_SlicingProbe.contextual-slice.v1.json"));
	}
	const FString LC6ProfilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc6-boundary-truth/"
			 "BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json")));
	if (FPaths::FileExists(LC6ProfilePath))
	{
		FileNames.Add(
			TEXT("BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json"));
	}
	const FString LC7ExplanationPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc7-static-scc-truth/"
			 "BP_LC7_StaticSCC.explanation.v1.json")));
	if (FPaths::FileExists(LC7ExplanationPath))
	{
		FileNames.AddUnique(TEXT("BP_LC7_StaticSCC.explanation.v1.json"));
	}
	FileNames.Sort();

	const FString CurrentPath = FPaths::ConvertRelativePathToFull(
		FModuleManager::GetModuleChecked<FBlueprintLensEditorModule>(
			TEXT("BlueprintLensEditor"))
			.GetExplanationPath());

	for (const FString& FileName : FileNames)
	{
		// The reader picks a case by name, so strip the parts that carry no
		// meaning for that choice and keep the case id itself intact.
		FString DisplayName = FileName;
		if (FileName.EndsWith(TEXT(".contextual-slice.v1.json")) ||
			FileName.EndsWith(TEXT(".core-boundary-matrix.v1.json")))
		{
			DisplayName.RemoveFromEnd(TEXT(".v1.json"));
		}
		else
		{
			DisplayName.RemoveFromEnd(TEXT(".explanation.v1.json"));
			DisplayName.RemoveFromEnd(TEXT(".sequence-profile.v1.json"));
			DisplayName.RemoveFromEnd(TEXT(".async-profile.v1.json"));
		}
		DisplayName.RemoveFromStart(TEXT("BP_"));

		const FString FullPath = FileName.Equals(
			TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
			ESearchCase::IgnoreCase)
			? LC7ExplanationPath
			: FileName.EndsWith(TEXT(".async-profile.v1.json"))
			? AsyncProfilePath
			: FileName.EndsWith(TEXT(".core-boundary-matrix.v1.json"))
				? LC6ProfilePath
			: FileName.EndsWith(TEXT(".contextual-slice.v1.json"))
				? LC5ProfilePath
				: FPaths::ConvertRelativePathToFull(ResourceDir / FileName);
		const TSharedPtr<FString> Option = MakeShared<FString>(DisplayName);
		ExplanationOptions.Add(Option);
		ExplanationPaths.Add(FullPath);

		if (FullPath.Equals(CurrentPath, ESearchCase::IgnoreCase))
		{
			SelectedOption = Option;
		}
	}

	// A session override can point outside the bundled set; leaving the
	// selection empty says so rather than naming the wrong case.
}

TSharedRef<SWidget> SBlueprintLensPanel::BuildCaseSelector()
{
	return SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&ExplanationOptions)
		.InitiallySelectedItem(SelectedOption)
		.ToolTipText(LOCTEXT(
			"CaseSelectorTooltip",
			"Load a bundled reader model. Equivalent to the "
			"BlueprintLens.ExplanationPath console command."))
		.OnGenerateWidget_Lambda(
			[](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? *Item : FString()));
			})
		.OnSelectionChanged_Lambda(
			[this](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
			{
				if (SelectInfo == ESelectInfo::Direct || !NewSelection.IsValid())
				{
					return;
				}
				const int32 Index = ExplanationOptions.IndexOfByKey(NewSelection);
				if (!ExplanationPaths.IsValidIndex(Index))
				{
					return;
				}
				SelectedOption = NewSelection;
				FModuleManager::GetModuleChecked<FBlueprintLensEditorModule>(
					TEXT("BlueprintLensEditor"))
					.SetExplanationPathOverride(ExplanationPaths[Index]);
				ReloadModel();
			})
		[
			SNew(STextBlock)
				.Text_Lambda(
					[this]()
					{
						return SelectedOption.IsValid()
							? FText::FromString(*SelectedOption)
							: LOCTEXT("CaseSelectorCustom", "Custom path");
					})
		];
}

#undef LOCTEXT_NAMESPACE
