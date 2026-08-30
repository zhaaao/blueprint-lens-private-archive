// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensM6Types.h"
#include "Templates/ValueOrError.h"

struct FM6TypedDocument
{
	FString Format;
	FString SchemaVersion;
	FString BlueprintPath;
	FString GraphId;
	TSet<FString> NodeIds;
	TSet<FString> EdgeIds;
};

struct FM6SliceProduct
{
	FString Format;
	FString SchemaVersion;
	FString RulesVersion;
	FString SliceKind;
	FString GraphId;
	FString SourceSha256;
	TArray<FString> NodeIds;
	TArray<FString> EdgeIds;
};

struct FM6BaselineSource
{
	FString AssetPath;
	FString GraphId;
	FString NodeId;
	FString NativeNodeGuid;
	TArray<FString> PinIds;
};

struct FM6BaselineEntity
{
	FString Id;
	FString Label;
	FString Role;
	FString SemanticStatus;
	FString SemanticReason;
	FString PresentationStatus;
	FString PresentationReason;
	TArray<FString> InclusionReasons;
	FString ClassPath;
	int32 PositionX = 0;
	int32 PositionY = 0;
	FM6BaselineSource Source;
};

struct FM6BaselineRelation
{
	FString Id;
	FString Label;
	FString Kind;
	FString SemanticLabel;
	FString SemanticStatus;
	FString SemanticReason;
	FString SourceEntityId;
	FString TargetEntityId;
	FString SourceEdgeId;
};

struct FM6BaselineBoundary
{
	FString NodeId;
	FString Status;
	FString Reason;
};

struct FM6BaselineFacts
{
	FString Format;
	FString SchemaVersion;
	FString GraphId;
	FString RendererId;
	FString CriterionEntityId;
	TArray<FString> EntityIds;
	TArray<FString> RelationIds;
	TMap<FString, FString> EntitySourceNodeIds;
	TArray<FM6BaselineEntity> Entities;
	TArray<FM6BaselineRelation> Relations;
	TArray<FM6BaselineBoundary> Boundaries;
	int32 TruncatedCount = 0;
};

struct FM6LoadedSessionPacket
{
	FM6Request Request;
	FString SemanticSha256;
	FM6TypedDocument TypedDocument;
	FM6SliceProduct Slice;
	FBlueprintLensExplanationModel Explanation;
	FM6BaselineFacts BaselineFacts;
};

using FM6SessionPacketLoadResult =
	TValueOrError<FM6LoadedSessionPacket, FM6Error>;

class FM6SessionPacketLoader
{
public:
	static FM6SessionPacketLoadResult Load(
		const FString& PacketDirectory,
		const FString& ExpectedSourceFingerprint);
};
