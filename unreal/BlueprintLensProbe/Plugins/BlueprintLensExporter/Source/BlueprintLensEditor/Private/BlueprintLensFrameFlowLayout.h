#pragma once

#include "BlueprintLensExplanationModel.h"

enum class EBlueprintLensFrameFlowSegmentKind : uint8
{
	Entry,
	StraightRun,
	CriterionFocus
};

enum class EBlueprintLensFrameFlowLayoutStatus : uint8
{
	Ready,
	MissingCriterion,
	DanglingRelation,
	UnsupportedLayoutShape
};

struct FBlueprintLensFrameFlowCounts
{
	int32 UnitCount = 0;
	int32 RelationCount = 0;
	int32 UniqueSourceNodeCount = 0;
	TMap<EBlueprintLensSemanticStatus, int32> SemanticStatusCounts;
};

struct FBlueprintLensFrameFlowSegment
{
	FString Id;
	EBlueprintLensFrameFlowSegmentKind Kind =
		EBlueprintLensFrameFlowSegmentKind::Entry;
	TArray<FString> MemberUnitIds;
	TArray<FString> MemberRelationIds;
	TArray<FString> IncomingRelationIds;
	TArray<FString> OutgoingRelationIds;
	FBlueprintLensFrameFlowCounts Counts;
	int32 DisplayRank = 0;
	bool bDisplayOrderIsSemantic = true;
	bool bCollapsible = false;
	bool bCollapsedByDefault = false;
};

struct FBlueprintLensFrameFlowSegmentEdge
{
	FString SourceSegmentId;
	FString TargetSegmentId;
	TArray<FString> RelationIds;
	EBlueprintLensRelationKind Kind =
		EBlueprintLensRelationKind::ExecutionPredecessor;
	FString Label;
};

struct FBlueprintLensFrameFlowLayoutModel
{
	FString Question;
	FString CriterionUnitId;
	FString DefaultSelectedSegmentId;
	TArray<FBlueprintLensFrameFlowSegment> Segments;
	TArray<FBlueprintLensFrameFlowSegmentEdge> SegmentEdges;
	FBlueprintLensFrameFlowCounts TruthCounts;
	EBlueprintLensLaneState BoundaryLaneState =
		EBlueprintLensLaneState::Empty;
	FString BoundaryMessage;
	EBlueprintLensFrameFlowLayoutStatus Status =
		EBlueprintLensFrameFlowLayoutStatus::UnsupportedLayoutShape;
	TArray<FString> Diagnostics;

	bool IsReady() const
	{
		return Status == EBlueprintLensFrameFlowLayoutStatus::Ready;
	}
};

struct FBlueprintLensFrameFlowDetailWindow
{
	FString SegmentId;
	FString AnchorUnitId;
	TArray<FString> VisibleUnitIds;
	TArray<FString> VisibleRelationIds;
	TArray<FString> HiddenPrefixUnitIds;
	TArray<FString> HiddenPrefixRelationIds;
	TArray<FString> HiddenSuffixUnitIds;
	TArray<FString> HiddenSuffixRelationIds;
	TArray<FString> AdjacentAnchorUnitIds;
	FString Error;

	bool IsValid() const
	{
		return Error.IsEmpty() && !VisibleUnitIds.IsEmpty();
	}
};

class FBlueprintLensFrameFlowLayoutBuilder
{
public:
	static FBlueprintLensFrameFlowLayoutModel BuildLinear(
		const FBlueprintLensExplanationModel& Explanation);

	static FBlueprintLensFrameFlowDetailWindow BuildDetailWindow(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensFrameFlowLayoutModel& Layout,
		const FString& SegmentId,
		const FString& AnchorUnitId,
		int32 MaximumVisibleUnits);
};
