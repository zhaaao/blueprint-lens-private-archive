// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6Preflight.h"

#include "BlueprintEditor.h"
#include "BlueprintLensM6GraphResolver.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
FM6PreflightResult Failure(const TCHAR* Code, FString Message)
{
	FM6PreflightResult Result;
	Result.Error.Code = Code;
	Result.Error.Phase = TEXT("preflight");
	Result.Error.Message = MoveTemp(Message);
	Result.Error.bRetryable = false;
	return Result;
}

bool IsPortableGameObjectPath(const FString& ObjectPath)
{
	return ObjectPath.StartsWith(TEXT("/Game/"))
		&& ObjectPath.Contains(TEXT("."))
		&& !ObjectPath.Contains(TEXT(".."))
		&& !ObjectPath.Contains(TEXT("\\"));
}

bool HashPackage(const UBlueprint& Blueprint, FString& OutSha256)
{
	OutSha256.Reset();
	FString PackageFilename;
	if (!FPackageName::DoesPackageExist(
			Blueprint.GetOutermost()->GetName(),
			&PackageFilename))
	{
		return false;
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *PackageFilename))
	{
		return false;
	}
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid()
		|| !Context->CalcSHA256(Bytes, Digest)
		|| Digest.Num() != 32)
	{
		return false;
	}
	OutSha256 = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
	return true;
}

bool BlueprintContainsGraph(const UBlueprint& Blueprint, const UEdGraph* Graph)
{
	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(Graphs);
	return Graph != nullptr && Graphs.Contains(Graph);
}

UEdGraph* ResolveQueryGraph(
	UBlueprint* Blueprint,
	UEdGraph* FocusedGraph,
	const FString& GraphId)
{
	if (Blueprint == nullptr || GraphId.IsEmpty())
	{
		return nullptr;
	}
	if (BlueprintContainsGraph(*Blueprint, FocusedGraph)
		&& FocusedGraph->GetPathName() == GraphId)
	{
		return FocusedGraph;
	}
	UEdGraph* Match = nullptr;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph == nullptr || Graph->GetPathName() != GraphId)
		{
			continue;
		}
		if (Match != nullptr)
		{
			return nullptr;
		}
		Match = Graph;
	}
	return Match;
}

bool HasExecutionCriterion(
	const UEdGraph& Graph,
	const FString& CriterionNodeId)
{
	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node == nullptr || !Node->NodeGuid.IsValid())
		{
			continue;
		}
		const FString NodeId = FString::Printf(
			TEXT("%s::node::%s"),
			*Graph.GetPathName(),
			*Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		if (NodeId == CriterionNodeId)
		{
			return true;
		}
	}
	return false;
}

bool HasDataCriterion(
	const UBlueprint& Blueprint,
	const FString& MemberGuid,
	const FString& ExpectedMemberName)
{
	FGuid ParsedGuid;
	if (!FGuid::ParseExact(
			MemberGuid,
			EGuidFormats::DigitsWithHyphensLower,
			ParsedGuid))
	{
		return false;
	}
	const FBPVariableDescription* GuidMatch = nullptr;
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		if (Variable.VarGuid == ParsedGuid)
		{
			GuidMatch = &Variable;
			break;
		}
	}
	return GuidMatch != nullptr
		&& GuidMatch->VarName.ToString() == ExpectedMemberName;
}

bool HasValidBudgets(const FM6QueryInput& Query)
{
	return Query.MaxSelectedNodes > 0
		&& Query.MaxSelectedRelations > 0
		&& Query.MaxVisibleEntities > 0
		&& Query.MaxVisibleRelations > 0;
}

bool CreateOwnedStagingDirectory(
	const FString& Root,
	FString& OutDirectory)
{
	OutDirectory.Reset();
	const FString AbsoluteRoot = FPaths::ConvertRelativePathToFull(Root);
	if (Root.IsEmpty()
		|| FPaths::IsRelative(Root)
		|| !IFileManager::Get().DirectoryExists(*AbsoluteRoot))
	{
		return false;
	}
	const FString Candidate = FPaths::Combine(
		AbsoluteRoot,
		FString::Printf(
			TEXT(".m6-session-%s.staging"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	if (!IFileManager::Get().MakeDirectory(*Candidate, false))
	{
		return false;
	}
	const FString ProbePath = FPaths::Combine(Candidate, TEXT(".write-probe"));
	if (!FFileHelper::SaveStringToFile(
			TEXT("m6\n"),
			*ProbePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().DeleteDirectory(*Candidate, false, true);
		return false;
	}
	IFileManager::Get().Delete(*ProbePath, false, true);
	OutDirectory = Candidate;
	return true;
}

FM6PreflightResult EvaluateResolved(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FM6QueryInput& Query,
	const FString& OwnedStagingRoot)
{
	if (Blueprint == nullptr)
	{
		return Failure(
			TEXT("M6_PRECONDITION_NO_BLUEPRINT"),
			TEXT("One current Blueprint is required."));
	}

	if (Graph == nullptr
		|| !BlueprintContainsGraph(*Blueprint, Graph)
		|| Query.GraphId != Graph->GetPathName())
	{
		return Failure(
			TEXT("M6_PRECONDITION_GRAPH_INVALID"),
			TEXT("The current query graph is missing or does not belong to the Blueprint."));
	}

	const FString AssetPath = Blueprint->GetPathName();
	FString PackageFilename;
	if (!IsPortableGameObjectPath(AssetPath)
		|| Blueprint->GetOutermost()->IsDirty()
		|| !FPackageName::DoesPackageExist(
			Blueprint->GetOutermost()->GetName(),
			&PackageFilename))
	{
		return Failure(
			TEXT("M6_PRECONDITION_DIRTY_SOURCE"),
			TEXT("The current Blueprint must be saved with no dirty edits."));
	}

	if (Blueprint->Status != BS_UpToDate
		&& Blueprint->Status != BS_UpToDateWithWarnings)
	{
		return Failure(
			TEXT("M6_PRECONDITION_COMPILE_FAILED"),
			TEXT("The current Blueprint compile status is not successful."));
	}

	const bool bExecution = Query.Kind == EM6QueryKind::Execution
		&& !Query.CriterionNodeId.IsEmpty()
		&& Query.MemberGuid.IsEmpty()
		&& Query.ExpectedMemberName.IsEmpty()
		&& HasExecutionCriterion(*Graph, Query.CriterionNodeId);
	const bool bData = Query.Kind == EM6QueryKind::Data
		&& Query.CriterionNodeId.IsEmpty()
		&& !Query.MemberGuid.IsEmpty()
		&& !Query.ExpectedMemberName.IsEmpty()
		&& HasDataCriterion(
			*Blueprint,
			Query.MemberGuid,
			Query.ExpectedMemberName);
	if ((!bExecution && !bData)
		|| Query.Direction != TEXT("backward")
		|| !HasValidBudgets(Query))
	{
		return Failure(
			TEXT("M6_PRECONDITION_QUERY_INVALID"),
			TEXT("Exactly one valid backward execution or data criterion is required."));
	}

	FString SourceFingerprint;
	if (!HashPackage(*Blueprint, SourceFingerprint))
	{
		return Failure(
			TEXT("M6_PRECONDITION_DIRTY_SOURCE"),
			TEXT("The saved Blueprint package could not be fingerprinted."));
	}

	FString StagingDirectory;
	if (!CreateOwnedStagingDirectory(OwnedStagingRoot, StagingDirectory))
	{
		return Failure(
			TEXT("M6_PRECONDITION_STAGING_UNAVAILABLE"),
			TEXT("A writable absolute owned staging root is required."));
	}

	FM6PreflightResult Result;
	Result.bSucceeded = true;
	Result.OwnedStagingDirectory = MoveTemp(StagingDirectory);
	Result.Request.AssetPath = AssetPath;
	Result.Request.GraphId = Query.GraphId;
	Result.Request.SourceFingerprint = MoveTemp(SourceFingerprint);
	Result.Request.QueryKind = bExecution ? TEXT("execution") : TEXT("data");
	Result.Request.CriterionNodeId = Query.CriterionNodeId;
	Result.Request.MemberGuid = Query.MemberGuid;
	Result.Request.ExpectedMemberName = Query.ExpectedMemberName;
	Result.Request.Direction = Query.Direction;
	Result.Request.MaxSelectedNodes = Query.MaxSelectedNodes;
	Result.Request.MaxSelectedRelations = Query.MaxSelectedRelations;
	Result.Request.MaxVisibleEntities = Query.MaxVisibleEntities;
	Result.Request.MaxVisibleRelations = Query.MaxVisibleRelations;
	return Result;
}
} // namespace

FM6PreflightResult FM6Preflight::Evaluate(
	const TSharedPtr<FBlueprintEditor>& BlueprintEditor,
	const FM6QueryInput& Query,
	const FString& OwnedStagingRoot)
{
	UBlueprint* Blueprint =
		BlueprintEditor.IsValid() ? BlueprintEditor->GetBlueprintObj() : nullptr;
	UEdGraph* FocusedGraph = FM6GraphResolver::Resolve(BlueprintEditor).Graph;
	if (FocusedGraph == nullptr && BlueprintEditor.IsValid())
	{
		FocusedGraph = FM6GraphResolver::Resolve(
			BlueprintEditor,
			Query.GraphId).Graph;
	}
	return EvaluateResolved(
		Blueprint,
		ResolveQueryGraph(Blueprint, FocusedGraph, Query.GraphId),
		Query,
		OwnedStagingRoot);
}

bool FM6Preflight::ComputeSourceFingerprint(
	const UBlueprint& Blueprint,
	FString& OutSha256)
{
	return HashPackage(Blueprint, OutSha256);
}

#if WITH_DEV_AUTOMATION_TESTS
UEdGraph* FM6Preflight::ResolveQueryGraphForAutomationTest(
	UBlueprint* Blueprint,
	UEdGraph* FocusedGraph,
	const FString& GraphId)
{
	return ResolveQueryGraph(Blueprint, FocusedGraph, GraphId);
}

FM6PreflightResult FM6Preflight::EvaluateResolvedForAutomationTest(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FM6QueryInput& Query,
	const FString& OwnedStagingRoot)
{
	return EvaluateResolved(Blueprint, Graph, Query, OwnedStagingRoot);
}
#endif
