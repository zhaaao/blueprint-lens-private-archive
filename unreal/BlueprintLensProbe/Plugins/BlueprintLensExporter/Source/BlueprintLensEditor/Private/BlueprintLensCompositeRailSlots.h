#pragma once

#include "BlueprintLensLC1RailProjection.h"
#include "BlueprintLensLC2GuardSurfaceProjection.h"

enum class EBlueprintLensCompositeDisclosure : uint8
{
	Collapsed,
	Expanded
};

enum class EBlueprintLensCompositeStationAppearanceKind : uint8
{
	Plain,
	Guard
};

struct FBlueprintLensCompositeAttachment
{
	FString GrammarId;
	FString AttachmentId;
	FString MarkerText;
	TArray<FString> DetailLines;
	float ExpandedContentHeight = 0.0f;
	EBlueprintLensCompositeDisclosure Disclosure =
		EBlueprintLensCompositeDisclosure::Collapsed;
};

struct FBlueprintLensCompositeStationAppearance
{
	EBlueprintLensCompositeStationAppearanceKind Kind =
		EBlueprintLensCompositeStationAppearanceKind::Plain;
	FString GrammarId;
	FString GroupId;
	FString ParentGroupId;
	FString PredicateUnitId;
	FString MarkerText;
	FString GuardReaderText;
	FString ParentGuardReaderText;
	FString ForkReaderText;
	TArray<FString> DetailLines;
	float ExpandedContentHeight = 0.0f;
	int32 NestingDepth = 0;
	EBlueprintLensCompositeDisclosure Disclosure =
		EBlueprintLensCompositeDisclosure::Collapsed;
};

struct FBlueprintLensCompositeStationSlot
{
	FString UnitId;
	FBlueprintLensCompositeStationAppearance Appearance;
	TArray<FBlueprintLensCompositeAttachment> BesideAttachments;
};

struct FBlueprintLensCompositeBetweenStationsSlot
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	TArray<FBlueprintLensCompositeAttachment> Decorations;
};

struct FBlueprintLensCompositeTerminalCapSlot
{
	FString UnitId;
	TArray<FBlueprintLensCompositeAttachment> Attachments;
};

struct FBlueprintLensCompositeSpanSlot
{
	FString SlotId;
	TArray<FString> MemberUnitIds;
	bool bIsOrderBoundary = false;
	EBlueprintLensLC1RailOrderRegionKind OrderRegionKind =
		EBlueprintLensLC1RailOrderRegionKind::Incomparable;
	FString ReaderText;
	TArray<FBlueprintLensCompositeAttachment> Attachments;
	EBlueprintLensCompositeDisclosure Disclosure =
		EBlueprintLensCompositeDisclosure::Collapsed;
};

struct FBlueprintLensCompositeRailSlots
{
	static constexpr int32 DefaultFoldRadius = 13;

	FString SourceIrSha256;
	FString SourceBlueprintAssetPath;
	int32 DataAnswerWriteCount = 0;
	int32 DataAnswerUnitCount = 0;
	int32 DataAnswerRelationCount = 0;
	TArray<FBlueprintLensCompositeStationSlot> Stations;
	TArray<FBlueprintLensCompositeBetweenStationsSlot> BetweenStations;
	TArray<FBlueprintLensCompositeTerminalCapSlot> TerminalCaps;
	TArray<FBlueprintLensCompositeSpanSlot> Spans;
	FString DiagnosticCode;

	bool IsRenderable(const FBlueprintLensLC1RailProjection& Rail) const;
	bool HasGuardStations() const;
	bool HasLC3Attachments() const;
	bool AreAllAttachmentsCollapsed() const;
	FBlueprintLensCompositeStationSlot* FindStation(const FString& UnitId);
	const FBlueprintLensCompositeStationSlot* FindStation(
		const FString& UnitId) const;
};

class FBlueprintLensCompositeRailSlotProjector
{
public:
	static FBlueprintLensCompositeRailSlots Build(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC1RailProjection& Rail);
};

class FBlueprintLensLC2StationAppearanceProjector
{
public:
	static FBlueprintLensCompositeRailSlots Apply(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensLC2GuardSurfaceProjection& GuardSurface,
		const FBlueprintLensCompositeRailSlots& BaseSlots);
};

class FBlueprintLensLC3StationAttachmentProjector
{
public:
	static FBlueprintLensCompositeRailSlots Apply(
		const FBlueprintLensExplanationModel& Explanation,
		const FBlueprintLensCompositeRailSlots& BaseSlots);
};
