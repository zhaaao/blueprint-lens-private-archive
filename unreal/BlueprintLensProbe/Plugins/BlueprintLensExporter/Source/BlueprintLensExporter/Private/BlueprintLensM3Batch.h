// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace BlueprintLensM3Batch
{
	struct FBatchRequest
	{
		FString CorpusManifestPath;
		FString OutputDirectory;
	};

	struct FBatchResult
	{
		FString ResultManifestPath;
		FString ResultManifestSha256;
		int32 RequestedAssetCount = 0;
		int32 ExportedAssetCount = 0;
	};

	bool ExportBatch(const FBatchRequest& Request, FBatchResult& OutResult, FString& OutErrorCode, FString& OutError);

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only reduced-cardinality ingress; never registered as a console command. */
	bool ExportBatchForAutomationTest(const FBatchRequest& Request, FBatchResult& OutResult, FString& OutErrorCode, FString& OutError);

	/** Test-only canonical result serialization seam; never registered as a console command. */
	bool SerializeCanonicalJsonForAutomationTest(const TSharedPtr<FJsonObject>& Root, FString& OutJsonText);
#endif
}
