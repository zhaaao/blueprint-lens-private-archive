// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensProductionExporter.h"
#include "BlueprintLensM3Batch.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace BlueprintLensM3ExporterTests
{
	namespace
	{
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

		bool LoadJson(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				return false;
			}
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		void RemoveVolatileFields(const TSharedPtr<FJsonObject>& Root)
		{
			Root->RemoveField(TEXT("engine_version"));
			const TSharedPtr<FJsonObject>* Blueprint = nullptr;
			if (!Root->TryGetObjectField(TEXT("blueprint"), Blueprint) || Blueprint == nullptr)
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
			if (!(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs) || Graphs == nullptr)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
			{
				const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if (!Graph.IsValid() || !Graph->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
				{
					const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
					if (Node.IsValid())
					{
						Node->RemoveField(TEXT("title"));
					}
				}
			}
		}

		FString CondensedJson(const TSharedPtr<FJsonObject>& Object)
		{
			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer) ? Text : FString();
		}

		FString BatchManifestJson(const FString& RegressionObjectPath, const FString& CandidateObjectPath, const FString& CandidateGraphId)
		{
			const FString Candidates = FString::Printf(
				TEXT("{\"id\":\"M3-C01\",\"object_path\":\"%s\",\"graph_id\":\"%s:CalculateRecovery\",\"band\":\"medium\",\"risk_dimensions\":[\"source_traceability_and_progressive_disclosure\"]},{\"id\":\"M3-C02\",\"object_path\":\"%s\",\"graph_id\":\"%s\",\"band\":\"small\",\"risk_dimensions\":[\"cycles_and_multiple_sccs\"]}"),
				*RegressionObjectPath, *RegressionObjectPath, *CandidateObjectPath, *CandidateGraphId);
			return FString::Printf(
				TEXT("{\"schema_name\":\"blueprint-lens-m3-corpus\",\"schema_version\":\"1.0.0\",\"regression_assets\":[{\"id\":\"M3-R01\",\"object_path\":\"%s\"}],\"candidate_graphs\":[%s]}"),
				*RegressionObjectPath,
				*Candidates);
		}

		bool SaveBatchManifest(const FString& Path, const FString& RegressionObjectPath, const FString& CandidateObjectPath)
		{
			return FFileHelper::SaveStringToFile(
				BatchManifestJson(RegressionObjectPath, CandidateObjectPath, CandidateObjectPath + TEXT(":EventGraph")),
				*Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		bool SaveBatchManifestWithCandidateGraph(
			const FString& Path,
			const FString& RegressionObjectPath,
			const FString& CandidateObjectPath,
			const FString& CandidateGraphId)
		{
			return FFileHelper::SaveStringToFile(
				BatchManifestJson(RegressionObjectPath, CandidateObjectPath, CandidateGraphId),
				*Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		bool HasCanonicalUtf8LfBytes(const TArray<uint8>& Bytes)
		{
			if (Bytes.Num() < 2 || Bytes.Last() != static_cast<uint8>('\n') || Bytes[Bytes.Num() - 2] == static_cast<uint8>('\n'))
			{
				return false;
			}
			if (Bytes.Num() >= 3 && Bytes[0] == 0xef && Bytes[1] == 0xbb && Bytes[2] == 0xbf)
			{
				return false;
			}
			int32 LineFeedCount = 0;
			for (const uint8 Byte : Bytes)
			{
				if (Byte == static_cast<uint8>('\r'))
				{
					return false;
				}
				if (Byte == static_cast<uint8>('\n'))
				{
					++LineFeedCount;
				}
			}
			return LineFeedCount == 1;
		}

		struct FExecutionShape
		{
			TArray<UEdGraphNode*> Nodes;
			TArray<TArray<int32>> Successors;
			TArray<TPair<int32, int32>> Edges;
		};

		struct FSourceCounts
		{
			int32 Graphs = 0;
			int32 Nodes = 0;
			int32 Pins = 0;
			int32 Edges = 0;
		};

		bool HasInGraphLink(const UEdGraph& Graph, const UEdGraphNode& Node)
		{
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				for (const UEdGraphPin* LinkedPin : Pin != nullptr ? Pin->LinkedTo : TArray<UEdGraphPin*>())
				{
					if (LinkedPin != nullptr && Graph.Nodes.Contains(LinkedPin->GetOwningNode()))
					{
						return true;
					}
				}
			}
			return false;
		}

		FExecutionShape BuildExecutionShape(UEdGraph& Graph)
		{
			FExecutionShape Shape;
			TMap<UEdGraphNode*, int32> NodeIndices;
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				const bool bHasExecutionPin = Node->Pins.ContainsByPredicate(
					[](const UEdGraphPin* Pin)
					{
						return Pin != nullptr && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
					});
				if (bHasExecutionPin)
				{
					NodeIndices.Add(Node, Shape.Nodes.Add(Node));
				}
			}
			Shape.Successors.SetNum(Shape.Nodes.Num());
			for (UEdGraphNode* Node : Shape.Nodes)
			{
				const int32 SourceIndex = NodeIndices.FindChecked(Node);
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr || Pin->Direction != EGPD_Output
						|| Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (LinkedPin == nullptr)
						{
							continue;
						}
						if (const int32* TargetIndex = NodeIndices.Find(LinkedPin->GetOwningNode()))
						{
							Shape.Successors[SourceIndex].AddUnique(*TargetIndex);
							Shape.Edges.AddUnique(TPair<int32, int32>(SourceIndex, *TargetIndex));
						}
					}
				}
			}
			return Shape;
		}

		TArray<TSet<int32>> FindNonTrivialSCCs(const FExecutionShape& Shape)
		{
			TArray<int32> Indices;
			Indices.Init(INDEX_NONE, Shape.Nodes.Num());
			TArray<int32> LowLinks;
			LowLinks.Init(INDEX_NONE, Shape.Nodes.Num());
			TArray<bool> OnStack;
			OnStack.Init(false, Shape.Nodes.Num());
			TArray<int32> Stack;
			TArray<TSet<int32>> Components;
			int32 NextIndex = 0;
			TFunction<void(int32)> StrongConnect = [&](const int32 NodeIndex)
			{
				Indices[NodeIndex] = NextIndex;
				LowLinks[NodeIndex] = NextIndex;
				++NextIndex;
				Stack.Add(NodeIndex);
				OnStack[NodeIndex] = true;
				for (const int32 Successor : Shape.Successors[NodeIndex])
				{
					if (Indices[Successor] == INDEX_NONE)
					{
						StrongConnect(Successor);
						LowLinks[NodeIndex] = FMath::Min(LowLinks[NodeIndex], LowLinks[Successor]);
					}
					else if (OnStack[Successor])
					{
						LowLinks[NodeIndex] = FMath::Min(LowLinks[NodeIndex], Indices[Successor]);
					}
				}
				if (LowLinks[NodeIndex] != Indices[NodeIndex])
				{
					return;
				}
				TSet<int32> Component;
				int32 Member = INDEX_NONE;
				do
				{
					Member = Stack.Pop(EAllowShrinking::No);
					OnStack[Member] = false;
					Component.Add(Member);
				}
				while (Member != NodeIndex);
				if (Component.Num() >= 2)
				{
					Components.Add(MoveTemp(Component));
				}
			};
			for (int32 NodeIndex = 0; NodeIndex < Shape.Nodes.Num(); ++NodeIndex)
			{
				if (Indices[NodeIndex] == INDEX_NONE)
				{
					StrongConnect(NodeIndex);
				}
			}
			return Components;
		}

		bool IsExecutionWorkflowConnected(const FExecutionShape& Shape)
		{
			if (Shape.Nodes.IsEmpty())
			{
				return false;
			}
			TArray<TArray<int32>> Neighbours;
			Neighbours.SetNum(Shape.Nodes.Num());
			for (const TPair<int32, int32>& Edge : Shape.Edges)
			{
				Neighbours[Edge.Key].AddUnique(Edge.Value);
				Neighbours[Edge.Value].AddUnique(Edge.Key);
			}
			TSet<int32> Visited;
			TArray<int32> Pending = {0};
			while (!Pending.IsEmpty())
			{
				const int32 Current = Pending.Pop(EAllowShrinking::No);
				if (Visited.Contains(Current))
				{
					continue;
				}
				Visited.Add(Current);
				for (const int32 Neighbour : Neighbours[Current])
				{
					Pending.Add(Neighbour);
				}
			}
			return Visited.Num() == Shape.Nodes.Num();
		}

		FString SCCStage(const UEdGraphNode& Node)
		{
			for (const FString Stage : {TEXT("INTAKE"), TEXT("VALIDATION"), TEXT("DISPATCH")})
			{
				if (Node.NodeComment.StartsWith(FString::Printf(TEXT("M3_SCC_%s_"), *Stage)))
				{
					return Stage;
				}
			}
			return FString();
		}

		bool SCCsHaveBoundedStageMembership(
			const FExecutionShape& Shape,
			const TArray<TSet<int32>>& Components)
		{
			TSet<FString> ObservedStages;
			TSet<int32> ObservedMembers;
			for (const TSet<int32>& Component : Components)
			{
				TSet<FString> ComponentStages;
				bool bHasIncoming = false;
				bool bHasOutgoing = false;
				for (const int32 Member : Component)
				{
					if (ObservedMembers.Contains(Member))
					{
						return false;
					}
					ObservedMembers.Add(Member);
					const FString Stage = SCCStage(*Shape.Nodes[Member]);
					if (!Stage.IsEmpty())
					{
						ComponentStages.Add(Stage);
					}
				}
				for (const TPair<int32, int32>& Edge : Shape.Edges)
				{
					bHasIncoming |= !Component.Contains(Edge.Key) && Component.Contains(Edge.Value);
					bHasOutgoing |= Component.Contains(Edge.Key) && !Component.Contains(Edge.Value);
				}
				if (ComponentStages.Num() != 1 || !bHasIncoming || !bHasOutgoing)
				{
					return false;
				}
				ObservedStages.Add(*ComponentStages.CreateConstIterator());
			}
			return Components.Num() == 3
				&& ObservedStages.Num() == 3
				&& ObservedStages.Contains(TEXT("INTAKE"))
				&& ObservedStages.Contains(TEXT("VALIDATION"))
				&& ObservedStages.Contains(TEXT("DISPATCH"));
		}

		FSourceCounts CountBlueprintSource(const UBlueprint& Blueprint)
		{
			FSourceCounts Counts;
			TArray<UEdGraph*> Graphs;
			Blueprint.GetAllGraphs(Graphs);
			for (const UEdGraph* Graph : Graphs)
			{
				if (Graph == nullptr)
				{
					continue;
				}
				++Counts.Graphs;
				Counts.Nodes += Graph->Nodes.Num();
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node == nullptr)
					{
						continue;
					}
					Counts.Pins += Node->Pins.Num();
					for (const UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin != nullptr && Pin->Direction == EGPD_Output)
						{
							Counts.Edges += Pin->LinkedTo.Num();
						}
					}
				}
			}
			return Counts;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM3ExporterParityTest,
	"BlueprintLens.Exporter.M3.ExporterParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM3ExporterParityTest::RunTest(const FString& Parameters)
{
	using namespace BlueprintLensM3ExporterTests;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe"));
	if (!TestNotNull(TEXT("BP_SlicingProbe loads"), Blueprint))
	{
		return false;
	}

	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("M3Tests"), TEXT("ExporterParity")));
	const FString FirstPath = FPaths::Combine(OutputDirectory, TEXT("run1.json"));
	const FString SecondPath = FPaths::Combine(OutputDirectory, TEXT("run2.json"));
	IFileManager::Get().Delete(*FirstPath, false, true, true);
	IFileManager::Get().Delete(*SecondPath, false, true, true);

	BlueprintLensProductionExporter::FExportError Error;
	BlueprintLensProductionExporter::FExportResult FirstResult;
	BlueprintLensProductionExporter::FExportRequest FirstRequest;
	FirstRequest.Blueprint = Blueprint;
	FirstRequest.OutputPath = FirstPath;
	if (!TestTrue(
			TEXT("first production export succeeds"),
			BlueprintLensProductionExporter::ExportRawDocument(FirstRequest, FirstResult, Error)))
	{
		AddError(Error.Message);
		return false;
	}

	TestEqual(TEXT("blueprint object path"), FirstResult.BlueprintObjectPath, Blueprint->GetPathName());
	TestEqual(TEXT("graph count"), FirstResult.GraphCount, 3);
	TestEqual(TEXT("node count"), FirstResult.NodeCount, 28);
	TestEqual(TEXT("pin count"), FirstResult.PinCount, 122);
	TestEqual(TEXT("edge count"), FirstResult.EdgeCount, 24);
	TestEqual(TEXT("unsupported node count"), FirstResult.UnsupportedNodeCount, 1);
	TestEqual(TEXT("reported first SHA"), FirstResult.Sha256, Sha256File(FirstPath));

	BlueprintLensProductionExporter::FExportResult SecondResult;
	BlueprintLensProductionExporter::FExportRequest SecondRequest;
	SecondRequest.Blueprint = Blueprint;
	SecondRequest.OutputPath = SecondPath;
	if (!TestTrue(
			TEXT("second production export succeeds"),
			BlueprintLensProductionExporter::ExportRawDocument(SecondRequest, SecondResult, Error)))
	{
		AddError(Error.Message);
		return false;
	}

	FString FirstText;
	FString SecondText;
	TestTrue(TEXT("first output is readable"), FFileHelper::LoadFileToString(FirstText, *FirstPath));
	TestTrue(TEXT("second output is readable"), FFileHelper::LoadFileToString(SecondText, *SecondPath));
	TestEqual(TEXT("current-engine exports are byte-identical"), SecondText, FirstText);
	TestEqual(TEXT("current-engine SHAs are identical"), SecondResult.Sha256, FirstResult.Sha256);
	TestEqual(TEXT("reported second SHA"), SecondResult.Sha256, Sha256File(SecondPath));

	const FString RepositoryRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(), TEXT(".."), TEXT("..")));
	const FString FrozenFixturePath = FPaths::Combine(
		RepositoryRoot, TEXT("fixtures"), TEXT("raw"), TEXT("BP_SlicingProbe.raw-0.2.json"));
	TSharedPtr<FJsonObject> CurrentJson;
	TSharedPtr<FJsonObject> FrozenJson;
	TestTrue(TEXT("current raw JSON parses"), LoadJson(FirstPath, CurrentJson));
	TestTrue(TEXT("frozen raw JSON parses"), LoadJson(FrozenFixturePath, FrozenJson));
	if (CurrentJson.IsValid() && FrozenJson.IsValid())
	{
		TestEqual(
			TEXT("raw format"),
			CurrentJson->GetStringField(TEXT("format")),
			TEXT("blueprint-lens-raw-probe"));
		TestEqual(
			TEXT("raw format version"),
			CurrentJson->GetStringField(TEXT("format_version")),
			TEXT("0.2"));
		RemoveVolatileFields(CurrentJson);
		RemoveVolatileFields(FrozenJson);
		TestEqual(
			TEXT("stable raw structure matches frozen fixture"),
			CondensedJson(CurrentJson),
			CondensedJson(FrozenJson));
	}

	BlueprintLensProductionExporter::FExportRequest InvalidRequest;
	InvalidRequest.OutputPath = FirstPath;
	BlueprintLensProductionExporter::FExportResult InvalidResult;
	BlueprintLensProductionExporter::FExportError InvalidError;
	TestFalse(
		TEXT("null Blueprint is rejected"),
		BlueprintLensProductionExporter::ExportRawDocument(InvalidRequest, InvalidResult, InvalidError));
	TestEqual(
		TEXT("invalid request error code"),
		static_cast<int32>(InvalidError.Code),
		static_cast<int32>(BlueprintLensProductionExporter::EExportErrorCode::InvalidRequest));

	const FString DirectoryAsFile = FPaths::Combine(OutputDirectory, TEXT("write-failure-target"));
	IFileManager::Get().MakeDirectory(*DirectoryAsFile, true);
	BlueprintLensProductionExporter::FExportRequest WriteFailureRequest;
	WriteFailureRequest.Blueprint = Blueprint;
	WriteFailureRequest.OutputPath = DirectoryAsFile;
	BlueprintLensProductionExporter::FExportResult WriteFailureResult;
	BlueprintLensProductionExporter::FExportError WriteFailureError;
	TestFalse(
		TEXT("directory output is rejected as a file"),
		BlueprintLensProductionExporter::ExportRawDocument(
			WriteFailureRequest, WriteFailureResult, WriteFailureError));
	TestEqual(
		TEXT("write failure error code"),
		static_cast<int32>(WriteFailureError.Code),
		static_cast<int32>(BlueprintLensProductionExporter::EExportErrorCode::WriteFailed));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM3BatchManifestTest,
	"BlueprintLens.Exporter.M3.BatchManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM3BatchManifestTest::RunTest(const FString& Parameters)
{
	using namespace BlueprintLensM3ExporterTests;

	TSharedPtr<FJsonObject> CanonicalRoot = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> CanonicalNested = MakeShared<FJsonObject>();
	CanonicalNested->SetStringField(TEXT("z_nested"), TEXT("last"));
	CanonicalNested->SetStringField(TEXT("a_nested"), TEXT("first"));
	CanonicalRoot->SetStringField(TEXT("z_root"), TEXT("last"));
	CanonicalRoot->SetObjectField(TEXT("nested"), CanonicalNested);
	CanonicalRoot->SetStringField(TEXT("a_root"), TEXT("first"));
	FString CanonicalJsonText;
	if (!TestTrue(
		TEXT("recursive canonical JSON seam serializes reverse-inserted nested objects"),
		BlueprintLensM3Batch::SerializeCanonicalJsonForAutomationTest(CanonicalRoot, CanonicalJsonText)))
	{
		return false;
	}
	TestEqual(
		TEXT("recursive canonical JSON sorts every object key and has one terminal LF"),
		CanonicalJsonText,
		TEXT("{\"a_root\":\"first\",\"nested\":{\"a_nested\":\"first\",\"z_nested\":\"last\"},\"z_root\":\"last\"}\n"));

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("M3Tests"), TEXT("BatchManifest")));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString ManifestPath = FPaths::Combine(Root, TEXT("source-manifest.json"));
	const FString OutputDirectory = FPaths::Combine(Root, TEXT("outputs"));
	const FString RepeatOutputDirectory = FPaths::Combine(Root, TEXT("repeat-output"));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	IFileManager::Get().DeleteDirectory(*RepeatOutputDirectory, false, true);
	const FString ProbePath = TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe");
	const FString SequencePath = TEXT("/Game/LensCorpus/BP_LC4_SequenceDisclosure.BP_LC4_SequenceDisclosure");
	if (!TestTrue(TEXT("two-asset source manifest writes"), SaveBatchManifest(ManifestPath, ProbePath, SequencePath)))
	{
		return false;
	}

	BlueprintLensM3Batch::FBatchRequest Request;
	Request.CorpusManifestPath = ManifestPath;
	Request.OutputDirectory = OutputDirectory;
	BlueprintLensM3Batch::FBatchResult Result;
	FString ErrorCode;
	FString Error;
	TestFalse(TEXT("public batch rejects reduced automation manifest"), BlueprintLensM3Batch::ExportBatch(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("public reduced-manifest error code"), ErrorCode, TEXT("M3_MANIFEST_CARDINALITY_INVALID"));
	TestFalse(TEXT("public reduced-manifest failure owns no output directory"), IFileManager::Get().DirectoryExists(*OutputDirectory));
	ErrorCode.Empty();
	Error.Empty();
	if (!TestTrue(TEXT("two-asset automation seam export succeeds"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error)))
	{
		AddError(FString::Printf(TEXT("%s: %s"), *ErrorCode, *Error));
		return false;
	}
	TestEqual(TEXT("two unique assets requested"), Result.RequestedAssetCount, 2);
	TestEqual(TEXT("two unique assets exported"), Result.ExportedAssetCount, 2);
	TestTrue(TEXT("result manifest exists"), FPaths::FileExists(Result.ResultManifestPath));
	TestEqual(TEXT("result hash is complete"), Result.ResultManifestSha256.Len(), 64);
	TArray<uint8> FirstResultBytes;
	TestTrue(TEXT("result manifest bytes load"), FFileHelper::LoadFileToArray(FirstResultBytes, *Result.ResultManifestPath));
	TestTrue(TEXT("result manifest uses canonical UTF-8 LF bytes"), HasCanonicalUtf8LfBytes(FirstResultBytes));

	TSharedPtr<FJsonObject> ResultJson;
	if (!TestTrue(TEXT("result manifest parses"), LoadJson(Result.ResultManifestPath, ResultJson)))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Assets = ResultJson->GetArrayField(TEXT("assets"));
	TestEqual(TEXT("result records each unique asset once"), Assets.Num(), 2);
	FString PreviousPath;
	for (const TSharedPtr<FJsonValue>& Value : Assets)
	{
		const TSharedPtr<FJsonObject> Asset = Value->AsObject();
		if (!TestTrue(TEXT("asset result is an object"), Asset.IsValid()))
		{
			continue;
		}
		const FString ObjectPath = Asset->GetStringField(TEXT("object_path"));
		TestTrue(TEXT("results are object-path sorted"), PreviousPath.IsEmpty() || PreviousPath < ObjectPath);
		PreviousPath = ObjectPath;
		const FString RawRelativePath = Asset->GetStringField(TEXT("raw_relative_path"));
		TestTrue(TEXT("raw path is relative"), FPaths::IsRelative(RawRelativePath));
		TestFalse(TEXT("raw path has no drive prefix"), RawRelativePath.Contains(TEXT(":")));
		TestEqual(TEXT("raw SHA-256 is complete"), Asset->GetStringField(TEXT("raw_sha256")).Len(), 64);
		TestTrue(TEXT("package GUID is populated"), !Asset->GetStringField(TEXT("package_guid")).IsEmpty());
		TestEqual(TEXT("package source SHA-256 is complete"), Asset->GetStringField(TEXT("package_source_sha256")).Len(), 64);
		TestTrue(TEXT("generated class path is populated"), !Asset->GetStringField(TEXT("generated_class_path")).IsEmpty());
		TestEqual(TEXT("compile status is accepted"), Asset->GetStringField(TEXT("compile_status")), TEXT("up_to_date"));
		TestTrue(TEXT("graph count is non-negative"), Asset->GetIntegerField(TEXT("graph_count")) >= 0);
		TestTrue(TEXT("node count is non-negative"), Asset->GetIntegerField(TEXT("node_count")) >= 0);
		TestTrue(TEXT("pin count is non-negative"), Asset->GetIntegerField(TEXT("pin_count")) >= 0);
		TestTrue(TEXT("edge count is non-negative"), Asset->GetIntegerField(TEXT("edge_count")) >= 0);
	}

	const FString FirstResultHash = Result.ResultManifestSha256;
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	TestFalse(TEXT("existing successful output directory is rejected"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("existing output directory error code"), ErrorCode, TEXT("M3_OUTPUT_EXISTS"));
	TArray<uint8> ReusedResultBytes;
	TestTrue(TEXT("existing result bytes remain readable after rejection"), FFileHelper::LoadFileToArray(ReusedResultBytes, *FPaths::Combine(OutputDirectory, TEXT("batch-result.v1.json"))));
	TestTrue(TEXT("existing result bytes remain unchanged after rejection"), ReusedResultBytes == FirstResultBytes);
	TestEqual(TEXT("existing result hash remains unchanged after rejection"), Sha256File(FPaths::Combine(OutputDirectory, TEXT("batch-result.v1.json"))), FirstResultHash);

	Request.OutputDirectory = RepeatOutputDirectory;
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	if (!TestTrue(TEXT("second fresh output directory export succeeds"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error)))
	{
		AddError(FString::Printf(TEXT("%s: %s"), *ErrorCode, *Error));
		return false;
	}
	TArray<uint8> RepeatedExportBytes;
	TestTrue(TEXT("second fresh result bytes load"), FFileHelper::LoadFileToArray(RepeatedExportBytes, *Result.ResultManifestPath));
	TestTrue(TEXT("repeated export result bytes are stable"), RepeatedExportBytes == FirstResultBytes);
	TestEqual(TEXT("repeated export result hash is stable"), Result.ResultManifestSha256, FirstResultHash);

	const FString MissingManifestPath = FPaths::Combine(Root, TEXT("missing-asset-manifest.json"));
	SaveBatchManifest(MissingManifestPath, ProbePath, TEXT("/Game/ZDoesNotExist/BP_Missing.BP_Missing"));
	Request.CorpusManifestPath = MissingManifestPath;
	Request.OutputDirectory = FPaths::Combine(Root, TEXT("missing-output"));
	IFileManager::Get().DeleteDirectory(*Request.OutputDirectory, false, true);
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	TestFalse(TEXT("missing asset fails closed"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("missing asset error code"), ErrorCode, TEXT("M3_MISSING_ASSET"));
	TestFalse(TEXT("second-asset missing failure removes the owned output directory"), IFileManager::Get().DirectoryExists(*Request.OutputDirectory));

	const FString DanglingManifestPath = FPaths::Combine(Root, TEXT("dangling-graph-manifest.json"));
	SaveBatchManifestWithCandidateGraph(
		DanglingManifestPath,
		ProbePath,
		SequencePath,
		SequencePath + TEXT(":MissingGraph"));
	Request.CorpusManifestPath = DanglingManifestPath;
	Request.OutputDirectory = FPaths::Combine(Root, TEXT("dangling-graph-output"));
	IFileManager::Get().DeleteDirectory(*Request.OutputDirectory, false, true);
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	TestFalse(TEXT("dangling candidate graph fails closed"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("dangling candidate graph error code"), ErrorCode, TEXT("M3_DANGLING_CANDIDATE_GRAPH"));
	TestFalse(TEXT("dangling candidate graph removes the owned output directory"), IFileManager::Get().DirectoryExists(*Request.OutputDirectory));

	const FString CompileFailureObjectPath = TEXT("/Game/ZBlueprintLensM3Tests/BP_BatchCompileFailure.BP_BatchCompileFailure");
	UPackage* CompileFailurePackage = CreatePackage(TEXT("/Game/ZBlueprintLensM3Tests/BP_BatchCompileFailure"));
	UBlueprint* CompileFailureBlueprint = NewObject<UBlueprint>(
		CompileFailurePackage,
		UBlueprint::StaticClass(),
		TEXT("BP_BatchCompileFailure"),
		RF_Public | RF_Standalone);
	CompileFailureBlueprint->Status = BS_Error;
	const FString CompileFailureManifestPath = FPaths::Combine(Root, TEXT("compile-failure-manifest.json"));
	SaveBatchManifest(CompileFailureManifestPath, ProbePath, CompileFailureObjectPath);
	Request.CorpusManifestPath = CompileFailureManifestPath;
	Request.OutputDirectory = FPaths::Combine(Root, TEXT("compile-failure-output"));
	IFileManager::Get().DeleteDirectory(*Request.OutputDirectory, false, true);
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	TestFalse(TEXT("compile failure fails through real batch path"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("compile failure error code"), ErrorCode, TEXT("M3_COMPILE_FAILED"));
	TestFalse(TEXT("compile failure removes the owned output directory"), IFileManager::Get().DirectoryExists(*Request.OutputDirectory));

	const FString OutputAsFile = FPaths::Combine(Root, TEXT("output-is-a-file"));
	FFileHelper::SaveStringToFile(TEXT("not a directory"), *OutputAsFile);
	Request.CorpusManifestPath = ManifestPath;
	Request.OutputDirectory = OutputAsFile;
	Result = BlueprintLensM3Batch::FBatchResult();
	ErrorCode.Empty();
	Error.Empty();
	TestFalse(TEXT("output failure fails closed"), BlueprintLensM3Batch::ExportBatchForAutomationTest(Request, Result, ErrorCode, Error));
	TestEqual(TEXT("output failure error code"), ErrorCode, TEXT("M3_OUTPUT_EXISTS"));
	TestFalse(TEXT("output failure leaves no result manifest"), FPaths::FileExists(FPaths::Combine(OutputAsFile, TEXT("batch-result.v1.json"))));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM3BatchCaptureCommandTest,
	"BlueprintLens.Exporter.M3.BatchCaptureCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM3BatchCaptureCommandTest::RunTest(const FString& Parameters)
{
	FString CorpusManifestPath;
	FString OutputRoot;
	FString RunId;
	const TCHAR* CommandLine = FCommandLine::Get();
	if (!FParse::Value(CommandLine, TEXT("M3CorpusManifest="), CorpusManifestPath)
		|| !FParse::Value(CommandLine, TEXT("M3OutputRoot="), OutputRoot)
		|| !FParse::Value(CommandLine, TEXT("M3RunId="), RunId)
		|| CorpusManifestPath.IsEmpty() || OutputRoot.IsEmpty() || RunId.IsEmpty())
	{
		AddError(TEXT("M3_CAPTURE_ARGUMENT_INVALID: -M3CorpusManifest, -M3OutputRoot and -M3RunId are required"));
		return false;
	}
	CorpusManifestPath = FPaths::ConvertRelativePathToFull(CorpusManifestPath);
	OutputRoot = FPaths::ConvertRelativePathToFull(OutputRoot);
	if (!FPaths::FileExists(CorpusManifestPath) || IFileManager::Get().DirectoryExists(*OutputRoot)
		|| IFileManager::Get().FileExists(*OutputRoot))
	{
		AddError(TEXT("M3_CAPTURE_ARGUMENT_INVALID: corpus must exist and output root must be fresh"));
		return false;
	}

	BlueprintLensM3Batch::FBatchRequest Request;
	Request.CorpusManifestPath = CorpusManifestPath;
	Request.OutputDirectory = OutputRoot;
	BlueprintLensM3Batch::FBatchResult Result;
	FString ErrorCode;
	FString Error;
	if (!TestTrue(TEXT("real production corpus batch succeeds"),
		BlueprintLensM3Batch::ExportBatch(Request, Result, ErrorCode, Error)))
	{
		AddError(FString::Printf(TEXT("%s: %s"), *ErrorCode, *Error));
		return false;
	}
	TestEqual(TEXT("deduplicated 8x8 corpus owns ten unique assets"), Result.RequestedAssetCount, 10);
	TestEqual(TEXT("all ten unique assets export"), Result.ExportedAssetCount, 10);
	TestEqual(
		TEXT("capture publishes the versioned batch result path"),
		FPaths::GetCleanFilename(Result.ResultManifestPath),
		FString(TEXT("batch-result.v1.json")));
	TestTrue(TEXT("capture result manifest exists"), FPaths::FileExists(Result.ResultManifestPath));
	TestEqual(TEXT("capture result hash is complete"), Result.ResultManifestSha256.Len(), 64);
	AddInfo(FString::Printf(
		TEXT("M3_CAPTURE_SUCCESS run_id=%s requested=%d exported=%d result=%s sha256=%s"),
		*RunId,
		Result.RequestedAssetCount,
		Result.ExportedAssetCount,
		*Result.ResultManifestPath,
		*Result.ResultManifestSha256));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM3MultiSCCFixtureShapeTest,
	"BlueprintLens.Exporter.M3.MultiSCCFixtureShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM3MultiSCCFixtureShapeTest::RunTest(const FString& Parameters)
{
	using namespace BlueprintLensM3ExporterTests;

	constexpr TCHAR AssetObjectPath[] = TEXT(
		"/Game/LensCorpus/BP_M3_MultiSCCRisk.BP_M3_MultiSCCRisk");
	constexpr TCHAR EventGraphPath[] = TEXT(
		"/Game/LensCorpus/BP_M3_MultiSCCRisk.BP_M3_MultiSCCRisk:EventGraph");
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, AssetObjectPath);
	if (!TestNotNull(TEXT("real M3 multi-SCC fixture loads"), Blueprint))
	{
		return false;
	}
	TestEqual(TEXT("fixture object path is exact"), Blueprint->GetPathName(), FString(AssetObjectPath));
	TestTrue(
		TEXT("fixture compile status is current"),
		(Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
			&& Blueprint->GeneratedClass != nullptr);

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("fixture EventGraph exists"), Graph))
	{
		return false;
	}
	TestEqual(TEXT("fixture graph path is exact"), Graph->GetPathName(), FString(EventGraphPath));
	TestTrue(TEXT("fixture is in the declared large engineering band"),
		Graph->Nodes.Num() >= 32 && Graph->Nodes.Num() <= 64);

	bool bEveryNodeIsConnected = true;
	int32 BranchCount = 0;
	int32 ReconvergenceCount = 0;
	int32 VariableGetCount = 0;
	int32 VariableSetCount = 0;
	bool bVariableGetFeedsData = false;
	bool bVariableSetReceivesData = false;
	UK2Node_CallFunction* OpaqueBoundary = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node == nullptr)
		{
			bEveryNodeIsConnected = false;
			continue;
		}
		bEveryNodeIsConnected &= HasInGraphLink(*Graph, *Node);
		BranchCount += Cast<UK2Node_IfThenElse>(Node) != nullptr ? 1 : 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin != nullptr && Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				&& Pin->LinkedTo.Num() >= 2)
			{
				++ReconvergenceCount;
			}
		}
		if (UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node))
		{
			++VariableGetCount;
			for (const UEdGraphPin* Pin : Get->Pins)
			{
				bVariableGetFeedsData |= Pin != nullptr
					&& Pin->Direction == EGPD_Output
					&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
					&& !Pin->LinkedTo.IsEmpty();
			}
		}
		if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
		{
			++VariableSetCount;
			for (const UEdGraphPin* Pin : Set->Pins)
			{
				bVariableSetReceivesData |= Pin != nullptr
					&& Pin->Direction == EGPD_Input
					&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
					&& !Pin->LinkedTo.IsEmpty();
			}
		}
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			if (Function != nullptr && Function->GetFName() == TEXT("PrintString")
				&& Call->NodeComment == TEXT("M3_OPAQUE_BOUNDARY"))
			{
				OpaqueBoundary = Call;
			}
		}
	}
	TestTrue(TEXT("every source node participates in the workflow"), bEveryNodeIsConnected);
	TestTrue(TEXT("fixture contains branch pressure"), BranchCount >= 4);
	TestTrue(TEXT("fixture contains an explicit reconvergence"), ReconvergenceCount >= 1);
	TestTrue(TEXT("fixture contains variable Get/Set data flow"),
		VariableGetCount >= 6 && VariableSetCount >= 12
			&& bVariableGetFeedsData && bVariableSetReceivesData);
	if (TestNotNull(TEXT("fixture contains the explicit opaque boundary"), OpaqueBoundary))
	{
		const UEdGraphPin* Input = OpaqueBoundary->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		const UEdGraphPin* Output = OpaqueBoundary->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		TestTrue(TEXT("opaque boundary is execution-connected between supported nodes"),
			Input != nullptr && Output != nullptr
				&& Input->LinkedTo.ContainsByPredicate(
					[](const UEdGraphPin* Pin)
					{
						return Pin != nullptr && Cast<UK2Node_IfThenElse>(Pin->GetOwningNode()) != nullptr;
					})
				&& Output->LinkedTo.ContainsByPredicate(
					[](const UEdGraphPin* Pin)
					{
						return Pin != nullptr && Cast<UK2Node_VariableSet>(Pin->GetOwningNode()) != nullptr;
					}));
	}

	const FExecutionShape Shape = BuildExecutionShape(*Graph);
	const TArray<TSet<int32>> Components = FindNonTrivialSCCs(Shape);
	TestTrue(TEXT("execution workflow is one connected component"), IsExecutionWorkflowConnected(Shape));
	TestEqual(TEXT("fixture owns exactly three non-trivial execution SCCs"), Components.Num(), 3);
	TestTrue(TEXT("SCC membership and entry/exit relations match the three workflow stages"),
		SCCsHaveBoundedStageMembership(Shape, Components));

	const FSourceCounts SourceCounts = CountBlueprintSource(*Blueprint);
	const FString OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("M3Tests"),
		TEXT("MultiSCCFixtureShape"), TEXT("raw.json")));
	IFileManager::Get().Delete(*OutputPath, false, true, true);
	BlueprintLensProductionExporter::FExportRequest Request;
	Request.Blueprint = Blueprint;
	Request.OutputPath = OutputPath;
	BlueprintLensProductionExporter::FExportResult Result;
	BlueprintLensProductionExporter::FExportError Error;
	if (!TestTrue(TEXT("fixture production raw export succeeds"),
		BlueprintLensProductionExporter::ExportRawDocument(Request, Result, Error)))
	{
		AddError(Error.Message);
		return false;
	}
	TestEqual(TEXT("raw export object path matches source"), Result.BlueprintObjectPath, FString(AssetObjectPath));
	TestEqual(TEXT("raw export graph count matches source traversal"), Result.GraphCount, SourceCounts.Graphs);
	TestEqual(TEXT("raw export node count matches source traversal"), Result.NodeCount, SourceCounts.Nodes);
	TestEqual(TEXT("raw export pin count matches source traversal"), Result.PinCount, SourceCounts.Pins);
	TestEqual(TEXT("raw export edge count matches source traversal"), Result.EdgeCount, SourceCounts.Edges);
	TestEqual(TEXT("raw export SHA is file-bound"), Result.Sha256, Sha256File(OutputPath));
	return !HasAnyErrors();
}

#endif
