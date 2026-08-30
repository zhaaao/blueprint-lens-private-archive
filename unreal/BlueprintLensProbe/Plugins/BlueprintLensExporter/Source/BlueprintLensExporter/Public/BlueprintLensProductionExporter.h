// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

namespace BlueprintLensProductionExporter
{
	enum class EExportErrorCode : uint8
	{
		InvalidRequest,
		SerializationFailed,
		WriteFailed
	};

	struct FExportRequest
	{
		const UBlueprint* Blueprint = nullptr;
		FString OutputPath;
	};

	struct FExportResult
	{
		FString BlueprintObjectPath;
		FString OutputPath;
		FString Sha256;
		int32 GraphCount = 0;
		int32 NodeCount = 0;
		int32 PinCount = 0;
		int32 EdgeCount = 0;
		int32 UnsupportedNodeCount = 0;
	};

	struct FExportError
	{
		EExportErrorCode Code = EExportErrorCode::InvalidRequest;
		FString Message;
	};

	BLUEPRINTLENSEXPORTER_API bool ExportRawDocument(
		const FExportRequest& Request,
		FExportResult& OutResult,
		FExportError& OutError);
}

using FBlueprintLensExportResult =
	BlueprintLensProductionExporter::FExportResult;

namespace BlueprintLens::Production
{
	BLUEPRINTLENSEXPORTER_API bool ExportRawDocument(
		UBlueprint* Blueprint,
		const FString& OutputPath,
		FBlueprintLensExportResult& OutResult);
}
