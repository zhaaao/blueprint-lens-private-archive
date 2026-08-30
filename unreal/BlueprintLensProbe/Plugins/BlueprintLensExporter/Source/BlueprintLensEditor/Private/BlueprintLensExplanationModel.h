#pragma once

#include "CoreMinimal.h"

enum class EBlueprintLensRole : uint8
{
	Criterion,
	Control,
	Predicate,
	Value,
	Consequence,
	Boundary
};

enum class EBlueprintLensLaneState : uint8
{
	Populated,
	NotEnabled,
	Empty
};

enum class EBlueprintLensUnitKind : uint8
{
	Node,
	Expression,
	Summary
};

enum class EBlueprintLensSemanticStatus : uint8
{
	Supported,
	Opaque,
	Uncertain,
	Unsupported
};

enum class EBlueprintLensRelationKind : uint8
{
	ExecutionPredecessor,
	ControlsExecution,
	PredicateFor,
	ProvidesValue
};

// Optional Explanation semantic extension v1.1. See
// Explanation semantic-extension contract v1.
enum class EBlueprintLensSemanticLabel : uint8
{
	ConditionTrue,
	ConditionFalse,
	NextExecution,
	BranchCondition,
	ValueInput
};

enum class EBlueprintLensGroupKind : uint8
{
	OutcomePath,
	GuardNest,
	ValueCone,
	FanoutBranch,
	PortalScope,
	FrontierGroup,
	Scc
};

enum class EBlueprintLensProjectionStatus : uint8
{
	Complete,
	StructuralOnly,
	Abstained
};

const TCHAR* LexToString(EBlueprintLensRole Value);
const TCHAR* LexToString(EBlueprintLensLaneState Value);
const TCHAR* LexToString(EBlueprintLensUnitKind Value);
const TCHAR* LexToString(EBlueprintLensSemanticStatus Value);
const TCHAR* LexToString(EBlueprintLensRelationKind Value);
const TCHAR* LexToString(EBlueprintLensSemanticLabel Value);
const TCHAR* LexToString(EBlueprintLensGroupKind Value);
const TCHAR* LexToString(EBlueprintLensProjectionStatus Value);

struct FBlueprintLensSource
{
	FString IrPath;
	FString IrSha256;
	FString SlicePath;
	FString SliceSha256;
	FString BlueprintAssetPath;
	FString BlueprintPackageSha256;
	FString GraphId;
};

struct FBlueprintLensQuery
{
	FString Question;
	FString Direction;
	FString CriterionSourceNodeId;
};

struct FBlueprintLensSourceReference
{
	FString BlueprintAssetPath;
	FString GraphId;
	FString SourceNodeId;
	FString NativeNodeGuid;
	TArray<FString> SourcePinIds;
	bool bPrimary = false;
};

struct FBlueprintLensDisambiguator
{
	FString Text;
	FString RuleId;
	TArray<FString> EvidenceRelationIds;
};

struct FBlueprintLensUnit
{
	FString Id;
	EBlueprintLensRole Role = EBlueprintLensRole::Criterion;
	EBlueprintLensUnitKind Kind = EBlueprintLensUnitKind::Node;
	FString Title;
	FString Expression;
	EBlueprintLensSemanticStatus SemanticStatus =
		EBlueprintLensSemanticStatus::Supported;
	TArray<FString> InclusionReasons;
	TArray<FBlueprintLensSourceReference> SourceReferences;
	bool bHasDisambiguator = false;
	FBlueprintLensDisambiguator Disambiguator;
};

struct FBlueprintLensLane
{
	EBlueprintLensRole Role = EBlueprintLensRole::Criterion;
	EBlueprintLensLaneState State = EBlueprintLensLaneState::Empty;
	TArray<FString> UnitIds;
	FString EmptyMessage;
};

struct FBlueprintLensSourceEdgeEndpoint
{
	FString SourceEdgeId;
	FString SourceNodeId;
	FString SourcePinId;
	FString SourcePortLabel;
	FString TargetNodeId;
	FString TargetPinId;
	FString TargetPortLabel;
};

struct FBlueprintLensRelation
{
	FString Id;
	FString SourceUnitId;
	FString TargetUnitId;
	EBlueprintLensRelationKind Kind =
		EBlueprintLensRelationKind::ExecutionPredecessor;
	FString Label;
	TArray<FString> SourceEdgeIds;
	bool bHasSourceEdgeEndpoints = false;
	TArray<FBlueprintLensSourceEdgeEndpoint> SourceEdgeEndpoints;
	bool bHasPortLabel = false;
	FString PortLabel;
	bool bHasSemanticLabel = false;
	EBlueprintLensSemanticLabel SemanticLabel =
		EBlueprintLensSemanticLabel::NextExecution;
};

struct FBlueprintLensClaimEvidence
{
	FString Component;
	FString FactOwner;
	FString Source;
};

struct FBlueprintLensGroup
{
	FString Id;
	EBlueprintLensGroupKind Kind = EBlueprintLensGroupKind::OutcomePath;
	FString Title;
	TArray<FString> OrderedUnitIds;
	TArray<FString> OrderedRelationIds;
	FString EntryUnitId;
	FString ExitUnitId;
	bool bHasExitUnitId = false;
	bool bHasParent = false;
	FString ParentGroupId;
	bool bHasEnteredBy = false;
	EBlueprintLensSemanticLabel EnteredBy =
		EBlueprintLensSemanticLabel::NextExecution;
	int32 MemberCount = 0;
	EBlueprintLensProjectionStatus ProjectionStatus =
		EBlueprintLensProjectionStatus::Abstained;
	FString DiagnosticCode;
	TArray<FBlueprintLensClaimEvidence> ClaimEvidence;
};

struct FBlueprintLensGroupPartialOrder
{
	TArray<TPair<FString, FString>> IncomparableGroupIds;
	FString Semantics;
};

struct FBlueprintLensCounts
{
	int32 Lanes = 0;
	int32 Units = 0;
	int32 Relations = 0;
	int32 SourceNodes = 0;
	int32 SourceEdges = 0;
};

struct FBlueprintLensExplanationModel
{
	FString Format;
	FString SchemaVersion;
	FString RulesVersion;
	FBlueprintLensSource Source;
	FBlueprintLensQuery Query;
	FString CriterionUnitId;
	TArray<FBlueprintLensLane> Lanes;
	TArray<FBlueprintLensUnit> Units;
	TArray<FBlueprintLensRelation> Relations;
	FBlueprintLensCounts Counts;
	// Groups are an optional, overlapping, non-exhaustive cover. Unlike lanes
	// and source references they are deliberately not validated as a partition.
	bool bHasGroups = false;
	TArray<FBlueprintLensGroup> Groups;
	bool bHasGroupPartialOrder = false;
	FBlueprintLensGroupPartialOrder GroupPartialOrder;

	const FBlueprintLensUnit* FindUnit(const FString& UnitId) const;
	const FBlueprintLensRelation* FindRelation(const FString& RelationId) const;
	const FBlueprintLensGroup* FindGroup(const FString& GroupId) const;
	const FBlueprintLensSourceReference* FindSourceReference(
		const FString& SourceNodeId) const;
};

struct FBlueprintLensLoadResult
{
	TSharedPtr<const FBlueprintLensExplanationModel> Model;
	FString Error;

	bool IsSuccess() const
	{
		return Model.IsValid() && Error.IsEmpty();
	}
};

class FBlueprintLensExplanationLoader
{
public:
	static FBlueprintLensLoadResult LoadFile(const FString& FilePath);
};
