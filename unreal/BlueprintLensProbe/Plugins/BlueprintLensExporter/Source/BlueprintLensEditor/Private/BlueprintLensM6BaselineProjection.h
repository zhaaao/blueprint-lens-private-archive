// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensM6SessionPacket.h"
#include "BlueprintLensWeaveProjection.h"
#include "Templates/ValueOrError.h"

struct FM6BaselineViewEntity
{
	FString Id;
	FString Label;
	FString Role;
	FString SemanticStatus;
	FString SemanticReason;
	FString PresentationStatus;
	FString PresentationReason;
	TArray<FString> InclusionReasons;
	bool bBoundary = false;
	FString BoundaryStatus;
	FString BoundaryReason;

	bool operator==(const FM6BaselineViewEntity& Other) const
	{
		return Id == Other.Id && Label == Other.Label && Role == Other.Role &&
			SemanticStatus == Other.SemanticStatus &&
			SemanticReason == Other.SemanticReason &&
			PresentationStatus == Other.PresentationStatus &&
			PresentationReason == Other.PresentationReason &&
			InclusionReasons == Other.InclusionReasons &&
			bBoundary == Other.bBoundary &&
			BoundaryStatus == Other.BoundaryStatus &&
			BoundaryReason == Other.BoundaryReason;
	}
};
struct FM6BaselineViewRelation
{
	FString Id;
	FString SourceEntityId;
	FString TargetEntityId;
	FString Label;
	FString Kind;
	FString SemanticLabel;
	FString SemanticStatus;
	FString SemanticReason;

	bool operator==(const FM6BaselineViewRelation& Other) const
	{
		return Id == Other.Id && SourceEntityId == Other.SourceEntityId &&
			TargetEntityId == Other.TargetEntityId && Label == Other.Label &&
			Kind == Other.Kind && SemanticLabel == Other.SemanticLabel &&
			SemanticStatus == Other.SemanticStatus &&
			SemanticReason == Other.SemanticReason;
	}
};

struct FM6FullGraphHighlightModel
{
	FString GraphId;
	FString CriterionEntityId;
	TSet<FString> FullEntityIds;
	TSet<FString> FullRelationIds;
	TSet<FString> MemberEntityIds;
	TSet<FString> MemberRelationIds;
	TSet<FString> NonMemberEntityIds;
	TSet<FString> NonMemberRelationIds;
};

struct FM6NativeSliceViewModel
{
	FString RendererId;
	FString CriterionEntityId;
	TArray<FM6BaselineViewEntity> Entities;
	TArray<FM6BaselineViewRelation> Relations;
	bool bReadOnly = true;
	bool bAllLinksInduced = false;
	int32 BoundaryCount = 0;
	int32 TruncatedCount = 0;
};

struct FM6CausalLensViewModel
{
	FString RendererId;
	FString CriterionEntityId;
	TArray<FM6BaselineViewEntity> Entities;
	TArray<FM6BaselineViewRelation> Relations;
	FBlueprintLensFrameFlowLayoutModel FrameFlow;
	FBlueprintLensWeaveProjection Weave;
	bool bSpecializedRoutesBypassed = true;
	int32 BoundaryCount = 0;
	int32 TruncatedCount = 0;
};

struct FM6BaselineViewModels
{
	FM6FullGraphHighlightModel A;
	FM6NativeSliceViewModel B;
	FM6CausalLensViewModel C;
};

using FM6BaselineProjectionResult =
	TValueOrError<FM6BaselineViewModels, FM6Error>;

FM6BaselineProjectionResult BuildM6BaselineViewModels(
	const FM6LoadedSessionPacket& Packet);

const FBlueprintLensUnit* FindM6ExplanationUnitBySourceEntityId(
	const FBlueprintLensExplanationModel& Explanation,
	const FString& SourceEntityId);
