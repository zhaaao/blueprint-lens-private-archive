#include "BlueprintLensSourceNavigator.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "IPlatformCrypto.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FBlueprintLensResolvedSource Unresolved(const FString& Message)
{
	FBlueprintLensResolvedSource Result;
	Result.State = EBlueprintLensSourceState::Unresolved;
	Result.Message = Message;
	return Result;
}

FBlueprintLensResolvedSource Stale(const FString& Message)
{
	FBlueprintLensResolvedSource Result;
	Result.State = EBlueprintLensSourceState::Stale;
	Result.Message = Message;
	return Result;
}
} // namespace

FBlueprintLensResolvedSource FBlueprintLensSourceNavigator::Resolve(
	const FBlueprintLensSource& Source,
	const FBlueprintLensSourceReference& Reference) const
{
	const FString PackageName =
		FPackageName::ObjectPathToPackageName(Source.BlueprintAssetPath);
	if (PackageName.IsEmpty())
	{
		return Unresolved(FString::Printf(
			TEXT("Source asset path '%s' could not be converted to a package"),
			*Source.BlueprintAssetPath));
	}

	FString PackageFilename;
	if (!FPackageName::DoesPackageExist(PackageName, &PackageFilename))
	{
		return Stale(FString::Printf(
			TEXT("Source asset package '%s' does not exist"),
			*PackageName));
	}

	TArray<uint8> PackageBytes;
	if (!FFileHelper::LoadFileToArray(PackageBytes, *PackageFilename))
	{
		return Stale(FString::Printf(
			TEXT("Source asset package '%s' could not be read"),
			*PackageFilename));
	}

	TUniquePtr<FEncryptionContext> CryptoContext =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!CryptoContext.IsValid()
		|| !CryptoContext->CalcSHA256(PackageBytes, Digest)
		|| Digest.Num() != 32)
	{
		return Stale(FString::Printf(
			TEXT("SHA-256 could not be computed for source asset package '%s'"),
			*PackageFilename));
	}

	const FString ActualSha256 = BytesToHex(Digest.GetData(), Digest.Num());
	if (!ActualSha256.Equals(
			Source.BlueprintPackageSha256,
			ESearchCase::IgnoreCase))
	{
		FBlueprintLensResolvedSource Result;
		Result.State = EBlueprintLensSourceState::Stale;
		Result.Message = FString::Printf(
			TEXT("Source asset package '%s' is stale (expected %s, found %s)"),
			*PackageFilename,
			*Source.BlueprintPackageSha256,
			*ActualSha256);
		return Result;
	}

	UBlueprint* Blueprint =
		LoadObject<UBlueprint>(nullptr, *Reference.BlueprintAssetPath);
	if (Blueprint == nullptr)
	{
		return Unresolved(FString::Printf(
			TEXT("Source Blueprint asset '%s' could not be loaded"),
			*Reference.BlueprintAssetPath));
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* MatchingGraph = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph != nullptr && Graph->GetPathName() == Reference.GraphId)
		{
			if (MatchingGraph != nullptr)
			{
				FBlueprintLensResolvedSource Result = Unresolved(
					FString::Printf(
						TEXT("Source graph path '%s' matched more than one graph"),
						*Reference.GraphId));
				Result.Blueprint = Blueprint;
				return Result;
			}
			MatchingGraph = Graph;
		}
	}
	if (MatchingGraph == nullptr)
	{
		FBlueprintLensResolvedSource Result = Unresolved(FString::Printf(
			TEXT("Source graph path '%s' was not found in asset '%s'"),
			*Reference.GraphId,
			*Reference.BlueprintAssetPath));
		Result.Blueprint = Blueprint;
		return Result;
	}

	FGuid NativeNodeGuid;
	if (!FGuid::Parse(Reference.NativeNodeGuid, NativeNodeGuid))
	{
		FBlueprintLensResolvedSource Result = Unresolved(FString::Printf(
			TEXT("Source node GUID '%s' is invalid"),
			*Reference.NativeNodeGuid));
		Result.Blueprint = Blueprint;
		Result.Graph = MatchingGraph;
		return Result;
	}

	UEdGraphNode* MatchingNode = nullptr;
	for (UEdGraphNode* Node : MatchingGraph->Nodes)
	{
		if (Node != nullptr && Node->NodeGuid == NativeNodeGuid)
		{
			if (MatchingNode != nullptr)
			{
				FBlueprintLensResolvedSource Result = Unresolved(
					FString::Printf(
						TEXT("Source node GUID '%s' matched more than one node in graph '%s'"),
						*Reference.NativeNodeGuid,
						*Reference.GraphId));
				Result.Blueprint = Blueprint;
				Result.Graph = MatchingGraph;
				return Result;
			}
			MatchingNode = Node;
		}
	}
	if (MatchingNode == nullptr)
	{
		FBlueprintLensResolvedSource Result = Unresolved(FString::Printf(
			TEXT("Source node GUID '%s' was not found in graph '%s'"),
			*Reference.NativeNodeGuid,
			*Reference.GraphId));
		Result.Blueprint = Blueprint;
		Result.Graph = MatchingGraph;
		return Result;
	}

	FBlueprintLensResolvedSource Result;
	Result.State = Blueprint->GetOutermost()->IsDirty()
		? EBlueprintLensSourceState::Unsaved
		: EBlueprintLensSourceState::Ready;
	Result.Blueprint = Blueprint;
	Result.Graph = MatchingGraph;
	Result.Node = MatchingNode;
	if (Result.State == EBlueprintLensSourceState::Unsaved)
	{
		Result.Message = FString::Printf(
			TEXT("Source Blueprint asset '%s' has unsaved changes"),
			*Reference.BlueprintAssetPath);
	}
	return Result;
}

bool FBlueprintLensSourceNavigator::Navigate(
	const FBlueprintLensResolvedSource& Source,
	FString& OutError) const
{
	OutError.Reset();
	if (Source.State != EBlueprintLensSourceState::Ready
		&& Source.State != EBlueprintLensSourceState::Unsaved)
	{
		OutError = Source.Message.IsEmpty()
			? TEXT("Source navigation is unavailable")
			: Source.Message;
		return false;
	}
	if (!Source.Node.IsValid())
	{
		OutError = Source.Message.IsEmpty()
			? TEXT("The resolved source node is no longer valid")
			: Source.Message;
		return false;
	}

	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(
		Source.Node.Get(), false);
	return true;
}
