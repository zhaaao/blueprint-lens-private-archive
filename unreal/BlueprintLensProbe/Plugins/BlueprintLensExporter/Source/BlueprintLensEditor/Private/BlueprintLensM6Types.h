// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EM6QueryKind : uint8
{
	Invalid,
	Execution,
	Data
};

struct FM6Error
{
	FString Code;
	FString Phase;
	FString Message;
	bool bRetryable = false;

	bool IsSet() const
	{
		return !Code.IsEmpty();
	}
};

struct FM6QueryInput
{
	EM6QueryKind Kind = EM6QueryKind::Invalid;
	FString GraphId;
	FString CriterionNodeId;
	FString MemberGuid;
	FString ExpectedMemberName;
	FString Direction = TEXT("backward");
	int32 MaxSelectedNodes = 4096;
	int32 MaxSelectedRelations = 8192;
	int32 MaxVisibleEntities = 24;
	int32 MaxVisibleRelations = 96;
};

struct FM6Request
{
	FString SchemaName = TEXT("blueprint-lens-m6-request");
	FString SchemaVersion = TEXT("1.0.0");
	FString AssetPath;
	FString GraphId;
	FString SourceFingerprint;
	FString QueryKind;
	FString CriterionNodeId;
	FString MemberGuid;
	FString ExpectedMemberName;
	FString Direction = TEXT("backward");
	int32 MaxSelectedNodes = 0;
	int32 MaxSelectedRelations = 0;
	int32 MaxVisibleEntities = 0;
	int32 MaxVisibleRelations = 0;
	FString RawVersion = TEXT("0.2");
	FString TypedIrVersion = TEXT("1.0.0");
	FString SliceRulesVersion = TEXT("1.0.0");
	FString RendererId = TEXT("R1_GENERIC_FRAME_FLOW_V1");
};

struct FM6PreflightResult
{
	bool bSucceeded = false;
	FM6Error Error;
	FM6Request Request;
	FString OwnedStagingDirectory;
};
