// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensLC7StaticSCCAudit.h"

#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace BlueprintLensLC7StaticSCCAudit
{
	namespace
	{
		struct FAuditEdge
		{
			FString Id;
			FString SourceNodeId;
			FString TargetNodeId;
			FString Kind;
		};

		FString GuidText(const FGuid& Guid)
		{
			return Guid.IsValid()
				? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
				: FString();
		}

		FString Sha256File(const FString& Path)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				return FString();
			}
			TUniquePtr<FEncryptionContext> Context = IPlatformCrypto::Get().CreateContext();
			TArray<uint8> Digest;
			if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) || Digest.Num() != 32)
			{
				return FString();
			}
			return BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
		}

		FString NodeId(const FString& GraphId, const UEdGraphNode& Node)
		{
			return BlueprintLensSequenceFacts::MakeNodeId(GraphId, Node);
		}

		FString EdgeId(
			const FString& GraphId,
			const UEdGraphPin& Source,
			const UEdGraphPin& Target)
		{
			const FString SourceNodeId = NodeId(GraphId, *Source.GetOwningNode());
			const FString TargetNodeId = NodeId(GraphId, *Target.GetOwningNode());
			return FString::Printf(
				TEXT("%s::edge::%s->%s"),
				*GraphId,
				*BlueprintLensSequenceFacts::MakePinId(SourceNodeId, Source),
				*BlueprintLensSequenceFacts::MakePinId(TargetNodeId, Target));
		}

		bool PublishLines(
			const TArray<FString>& Lines,
			const FString& FinalPath,
			FString& OutError)
		{
			IFileManager::Get().Delete(*FinalPath, false, true, true);
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit output directory could not be created");
				return false;
			}
			const FString TemporaryPath = FinalPath + TEXT(".tmp");
			IFileManager::Get().Delete(*TemporaryPath, false, true, true);
			if (!FFileHelper::SaveStringArrayToFile(
				Lines,
				*TemporaryPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				|| !IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, true, false, true))
			{
				IFileManager::Get().Delete(*TemporaryPath, false, true, true);
				IFileManager::Get().Delete(*FinalPath, false, true, true);
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit output could not be published");
				return false;
			}
			return true;
		}

		TSet<FString> Reachable(
			const FString& Start,
			const TMap<FString, TArray<FString>>& Adjacency)
		{
			TSet<FString> Visited;
			TArray<FString> Pending = {Start};
			while (!Pending.IsEmpty())
			{
				const FString Current = Pending.Pop(EAllowShrinking::No);
				if (Visited.Contains(Current))
				{
					continue;
				}
				Visited.Add(Current);
				if (const TArray<FString>* Targets = Adjacency.Find(Current))
				{
					for (int32 Index = Targets->Num() - 1; Index >= 0; --Index)
					{
						if (!Visited.Contains((*Targets)[Index]))
						{
							Pending.Add((*Targets)[Index]);
						}
					}
				}
			}
			return Visited;
		}
	}

	bool AuditSCCSource(
		const UBlueprint& Blueprint,
		const BlueprintLensLC7StaticSCCFixture::FFixtureAnchors& Anchors,
		const FString& RawExportPath,
		const FString& OutputDirectory,
		FString& OutFilePath,
		FSCCAuditStats& OutStats,
		FString& OutError)
	{
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory, TEXT("BP_LC7_StaticSCC.scc-audit.tsv")));
		OutStats = FSCCAuditStats();
		OutError.Reset();
		IFileManager::Get().Delete(*OutFilePath, false, true, true);
		if ((Blueprint.Status != BS_UpToDate && Blueprint.Status != BS_UpToDateWithWarnings)
			|| Blueprint.GeneratedClass == nullptr || Blueprint.UbergraphPages.Num() != 1
			|| Anchors.AssetObjectPath != Blueprint.GetPathName())
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit Blueprint/anchor identity is invalid");
			return false;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(const_cast<UBlueprint*>(&Blueprint));
		if (Graph == nullptr || Graph->GetPathName() != Anchors.GraphId || Graph->Nodes.Num() != 10)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit graph identity or node count is invalid");
			return false;
		}

		TMap<FString, UEdGraphNode*> Nodes;
		TMap<FString, TArray<FString>> Forward;
		TMap<FString, TArray<FString>> Reverse;
		TArray<FAuditEdge> Edges;
		TArray<FString> NodeLines;
		TArray<FString> PinLines;
		TArray<FString> EdgeLines;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node == nullptr || !Node->NodeGuid.IsValid())
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit found invalid node identity");
				return false;
			}
			const FString Id = NodeId(Graph->GetPathName(), *Node);
			if (Nodes.Contains(Id))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit found duplicate node identity");
				return false;
			}
			Nodes.Add(Id, Node);
			Forward.Add(Id, {});
			Reverse.Add(Id, {});
			FString Detail = TEXT("-");
			if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
			{
				Detail = FString::Printf(TEXT("event=%s"), *Event->CustomFunctionName.ToString());
			}
			else if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
			{
				Detail = FString::Printf(
					TEXT("member=%s;guid=%s"),
					*Variable->GetVarName().ToString(),
					*GuidText(Variable->VariableReference.GetMemberGuid()));
			}
			else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				const UFunction* Function = Call->GetTargetFunction();
				Detail = FString::Printf(
					TEXT("function=%s;owner=%s"),
					Function == nullptr ? TEXT("") : *Function->GetName(),
					Function == nullptr || Function->GetOwnerClass() == nullptr
						? TEXT("")
						: *Function->GetOwnerClass()->GetPathName());
			}
			NodeLines.Add(FString::Printf(
				TEXT("NODE\t%s\t%s\t%s\t%s"),
				*Id,
				*GuidText(Node->NodeGuid),
				*Node->GetClass()->GetPathName(),
				*Detail));
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin == nullptr)
				{
					continue;
				}
				PinLines.Add(FString::Printf(
					TEXT("PIN\t%s\t%s\t%s\t%s\t%s"),
					*BlueprintLensSequenceFacts::MakePinId(Id, *Pin),
					*Id,
					*Pin->PinName.ToString(),
					Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"),
					*Pin->PinType.PinCategory.ToString()));
			}
		}

		for (const TPair<FString, UEdGraphNode*>& Pair : Nodes)
		{
			for (UEdGraphPin* Pin : Pair.Value->Pins)
			{
				if (Pin == nullptr || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* Target = LinkedPin == nullptr ? nullptr : LinkedPin->GetOwningNode();
					if (LinkedPin == nullptr || Target == nullptr)
					{
						OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit found a dangling edge");
						return false;
					}
					const FString TargetId = NodeId(Graph->GetPathName(), *Target);
					if (!Nodes.Contains(TargetId))
					{
						OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit edge leaves the fixture graph");
						return false;
					}
					FAuditEdge Edge;
					Edge.Id = EdgeId(Graph->GetPathName(), *Pin, *LinkedPin);
					Edge.SourceNodeId = Pair.Key;
					Edge.TargetNodeId = TargetId;
					Edge.Kind = Pin->PinType.PinCategory == TEXT("exec")
						? TEXT("execution")
						: TEXT("data");
					Edges.Add(Edge);
					EdgeLines.Add(FString::Printf(
						TEXT("EDGE\t%s\t%s\t%s\t%s"),
						*Edge.Id,
						*Edge.SourceNodeId,
						*Edge.TargetNodeId,
						*Edge.Kind));
					if (Edge.Kind == TEXT("execution"))
					{
						Forward.FindChecked(Edge.SourceNodeId).Add(Edge.TargetNodeId);
						Reverse.FindChecked(Edge.TargetNodeId).Add(Edge.SourceNodeId);
					}
				}
			}
		}
		Algo::Sort(Edges, [](const FAuditEdge& Left, const FAuditEdge& Right)
		{
			return Left.Id < Right.Id;
		});
		for (TPair<FString, TArray<FString>>& Pair : Forward)
		{
			Pair.Value.Sort();
		}
		for (TPair<FString, TArray<FString>>& Pair : Reverse)
		{
			Pair.Value.Sort();
		}
		if (Nodes.Num() != 10 || Edges.Num() != 10)
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit totals differ from nodes=10 edges=10");
			return false;
		}
		for (const FString& RequiredId : {
			Anchors.EventNodeId,
			Anchors.InitialiseNodeId,
			Anchors.BranchNodeId,
			Anchors.BodyNodeId,
			Anchors.AdvanceNodeId,
			Anchors.CriterionNodeId})
		{
			if (!Nodes.Contains(RequiredId))
			{
				OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit anchor does not resolve");
				return false;
			}
		}

		const TSet<FString> ForwardReach = Reachable(Anchors.BranchNodeId, Forward);
		const TSet<FString> ReverseReach = Reachable(Anchors.BranchNodeId, Reverse);
		TSet<FString> Members;
		for (const FString& Id : ForwardReach)
		{
			if (ReverseReach.Contains(Id))
			{
				Members.Add(Id);
			}
		}
		const TSet<FString> ExpectedMembers = {
			Anchors.BranchNodeId, Anchors.BodyNodeId, Anchors.AdvanceNodeId};
		if (Members.Num() != 3 || !Members.Includes(ExpectedMembers)
			|| Members.Contains(Anchors.CriterionNodeId))
		{
			OutError = TEXT("LC7_SCC_MEMBERSHIP_INVALID: reachability intersection differs from Branch/Body/Advance");
			return false;
		}

		TArray<FString> Internal;
		TArray<FString> Incoming;
		TArray<FString> Outgoing;
		TArray<FString> Returning;
		FString EntryNodeId;
		FString ExitNodeId;
		for (const FAuditEdge& Edge : Edges)
		{
			if (Edge.Kind != TEXT("execution"))
			{
				continue;
			}
			const bool bSourceMember = Members.Contains(Edge.SourceNodeId);
			const bool bTargetMember = Members.Contains(Edge.TargetNodeId);
			if (bSourceMember && bTargetMember)
			{
				Internal.Add(Edge.Id);
				if (Edge.SourceNodeId == Anchors.AdvanceNodeId
					&& Edge.TargetNodeId == Anchors.BranchNodeId)
				{
					Returning.Add(Edge.Id);
				}
			}
			else if (!bSourceMember && bTargetMember)
			{
				Incoming.Add(Edge.Id);
				EntryNodeId = Edge.TargetNodeId;
			}
			else if (bSourceMember && !bTargetMember)
			{
				Outgoing.Add(Edge.Id);
				ExitNodeId = Edge.SourceNodeId;
			}
		}
		Internal.Sort();
		Incoming.Sort();
		Outgoing.Sort();
		Returning.Sort();
		if (Internal.Num() != 3 || Returning.Num() != 1)
		{
			OutError = TEXT("LC7_SCC_EDGE_OWNERSHIP_INVALID: audit internal/returning inventory differs from 3/1");
			return false;
		}
		if (Incoming.Num() != 1 || Outgoing.Num() != 1
			|| EntryNodeId != Anchors.BranchNodeId || ExitNodeId != Anchors.BranchNodeId)
		{
			OutError = TEXT("LC7_SCC_BOUNDARY_INVALID: audit expected one Branch entry and one Branch exit");
			return false;
		}

		const FString AssetPath = FPackageName::LongPackageNameToFilename(
			Blueprint.GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		const FString AssetHash = Sha256File(AssetPath);
		const FString RawHash = Sha256File(RawExportPath);
		if (AssetHash.IsEmpty() || RawHash.IsEmpty())
		{
			OutError = TEXT("LC7_FIXTURE_SHAPE_INVALID: audit provenance could not be hashed");
			return false;
		}

		TArray<FString> MemberIds = Members.Array();
		MemberIds.Sort();
		Algo::Sort(NodeLines);
		Algo::Sort(PinLines);
		Algo::Sort(EdgeLines);
		TArray<FString> Lines;
		Lines.Add(TEXT("FORMAT\tblueprint-lens-lc7-static-scc-audit\t1.0.0"));
		Lines.Add(FString::Printf(
			TEXT("BLUEPRINT\t%s\t%s"),
			*Blueprint.GetPathName(),
			*Graph->GetPathName()));
		Lines.Add(FString::Printf(
			TEXT("COMPILE\tup_to_date\t%s\t%s\t%s\t%s"),
			*GuidText(Blueprint.GetOutermost()->GetPersistentGuid()),
			*Blueprint.GeneratedClass->GetPathName(),
			*AssetHash,
			*RawHash));
		Lines.Add(FString::Printf(TEXT("CRITERION\t%s"), *Anchors.CriterionNodeId));
		Lines.Append(NodeLines);
		Lines.Append(PinLines);
		Lines.Append(EdgeLines);
		for (const FString& Id : MemberIds)
		{
			Lines.Add(FString::Printf(TEXT("SCC_MEMBER\t%s"), *Id));
		}
		for (const FString& Id : Internal)
		{
			Lines.Add(FString::Printf(TEXT("SCC_INTERNAL\t%s"), *Id));
		}
		for (const FString& Id : Incoming)
		{
			Lines.Add(FString::Printf(TEXT("SCC_INCOMING\t%s"), *Id));
		}
		for (const FString& Id : Outgoing)
		{
			Lines.Add(FString::Printf(TEXT("SCC_OUTGOING\t%s"), *Id));
		}
		for (const FString& Id : Returning)
		{
			Lines.Add(FString::Printf(TEXT("SCC_RETURN\t%s"), *Id));
		}
		Lines.Add(FString::Printf(TEXT("SCC_ENTRY\t%s"), *EntryNodeId));
		Lines.Add(FString::Printf(TEXT("SCC_EXIT\t%s"), *ExitNodeId));

		OutStats.NodeCount = Nodes.Num();
		OutStats.EdgeCount = Edges.Num();
		OutStats.MemberCount = Members.Num();
		OutStats.InternalEdgeCount = Internal.Num();
		OutStats.IncomingEdgeCount = Incoming.Num();
		OutStats.OutgoingEdgeCount = Outgoing.Num();
		Lines.Add(FString::Printf(
			TEXT("COUNTS\t%d\t%d\t%d\t%d\t%d\t%d"),
			OutStats.NodeCount,
			OutStats.EdgeCount,
			OutStats.MemberCount,
			OutStats.InternalEdgeCount,
			OutStats.IncomingEdgeCount,
			OutStats.OutgoingEdgeCount));
		return PublishLines(Lines, OutFilePath, OutError);
	}
}
