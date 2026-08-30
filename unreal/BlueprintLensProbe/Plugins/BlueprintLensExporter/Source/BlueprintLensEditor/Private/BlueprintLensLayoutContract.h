#pragma once

#include "CoreMinimal.h"

enum class EBlueprintLensLayoutBackendKind : uint8
{
	Deterministic,
	GraphvizDot,
	ElkLayered
};

enum class EBlueprintLensLayoutProfile : uint8
{
	Linear,
	LayeredPorts,
	Compound,
	Cyclic
};

enum class EBlueprintLensLayoutRelationFamily : uint8
{
	Value,
	Execution,
	Predicate,
	Portal,
	Frontier,
	BackEdge
};

struct FBlueprintLensLayoutPortRequest
{
	FString Label;
	bool bInput = false;
	int32 Order = 0;
};

struct FBlueprintLensLayoutNodeRequest
{
	FString UnitId;
	FVector2D DesiredSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLayoutPortRequest> Ports;
};

struct FBlueprintLensLayoutEdgeRequest
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourcePortLabel;
	FString TargetPortLabel;
	EBlueprintLensLayoutRelationFamily Family =
		EBlueprintLensLayoutRelationFamily::Execution;
	bool bParticipatesInRank = true;
};

struct FBlueprintLensLayoutGroupRequest
{
	FString GroupId;
	FString ParentGroupId;
	TArray<FString> MemberUnitIds;
};

struct FBlueprintLensLayoutRequest
{
	FString GraphKey;
	EBlueprintLensLayoutProfile Profile = EBlueprintLensLayoutProfile::Linear;
	float TargetWidth = 0.0f;
	TArray<FBlueprintLensLayoutNodeRequest> Nodes;
	TArray<FBlueprintLensLayoutEdgeRequest> Edges;
	TArray<FBlueprintLensLayoutGroupRequest> Groups;

	bool IsValid() const;
};

struct FBlueprintLensLayoutNodePlacement
{
	FString UnitId;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

struct FBlueprintLensLayoutPortPlacement
{
	FString UnitId;
	FString Label;
	bool bInput = false;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FBlueprintLensLayoutEdgePlacement
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	FString SourcePortLabel;
	FString TargetPortLabel;
	EBlueprintLensLayoutRelationFamily Family =
		EBlueprintLensLayoutRelationFamily::Execution;
	TArray<FVector2D> BendPoints;
};

struct FBlueprintLensLayoutLedger
{
	EBlueprintLensLayoutBackendKind Backend =
		EBlueprintLensLayoutBackendKind::Deterministic;
	FString BackendVersion;
	FString ConfigurationFingerprint;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLayoutNodePlacement> Nodes;
	TArray<FBlueprintLensLayoutPortPlacement> Ports;
	TArray<FBlueprintLensLayoutEdgePlacement> Edges;
	FString DiagnosticCode;

	const FBlueprintLensLayoutPortPlacement* FindPort(
		const FString& UnitId,
		const FString& Label,
		bool bInput) const;
	bool HasNoNodeOverlaps() const;
	bool IsCompleteFor(const FBlueprintLensLayoutRequest& Request) const;
};

class IBlueprintLensLayoutBackend
{
public:
	virtual ~IBlueprintLensLayoutBackend() = default;

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const = 0;
	virtual bool IsAvailable(FString& OutDiagnostic) const = 0;
	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest& Request) const = 0;
};

class FBlueprintLensLayoutBackendPolicy
{
public:
	static EBlueprintLensLayoutBackendKind PreferredBackend(
		EBlueprintLensLayoutProfile Profile);
	static TArray<EBlueprintLensLayoutBackendKind> CandidateOrder(
		EBlueprintLensLayoutProfile Profile);
};

const TCHAR* BlueprintLensLayoutBackendName(
	EBlueprintLensLayoutBackendKind Backend);
const TCHAR* BlueprintLensLayoutProfileName(
	EBlueprintLensLayoutProfile Profile);
