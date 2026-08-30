// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensExporter.h"

#include "BlueprintLensProductionExporter.h"
#include "BlueprintLensM3Batch.h"
#include "BlueprintLensLC4SequenceFixture.h"
#include "BlueprintLensIntraBpPureAudit.h"
#include "BlueprintLensIntraBpPureFacts.h"
#include "BlueprintLensLC6BoundaryAudit.h"
#include "BlueprintLensLC6BoundaryFacts.h"
#include "BlueprintLensLC6BoundaryFixture.h"
#include "BlueprintLensLC7StaticSCCAudit.h"
#include "BlueprintLensLC7StaticSCCFacts.h"
#include "BlueprintLensLC7StaticSCCFixture.h"
#include "BlueprintLensSequenceCompilerAudit.h"
#include "BlueprintLensSequenceFacts.h"

#include "Algo/Sort.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FBlueprintLensExporterModule"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensExporter, Log, All);

namespace BlueprintLensExporter
{
	struct FCommandStats
	{
		int32 GraphCount = 0;
		int32 NodeCount = 0;
		int32 PinCount = 0;
		int32 EdgeCount = 0;
		int32 UnsupportedNodeCount = 0;
	};

	FString ToObjectPath(const FString& SuppliedPath)
	{
		if (SuppliedPath.Contains(TEXT(".")))
		{
			return SuppliedPath;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(SuppliedPath);
		return AssetName.IsEmpty()
			? SuppliedPath
			: FString::Printf(TEXT("%s.%s"), *SuppliedPath, *AssetName);
	}

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.IsValid()
			? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
			: FString();
	}

	FString PinDirectionToString(const EEdGraphPinDirection Direction)
	{
		return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
	}

	FString PinContainerToString(const EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array:
			return TEXT("array");
		case EPinContainerType::Set:
			return TEXT("set");
		case EPinContainerType::Map:
			return TEXT("map");
		case EPinContainerType::None:
		default:
			return TEXT("none");
		}
	}

	int32 PinSameNameOccurrence(const UEdGraphPin& Pin, const int32 PinIndex)
	{
		int32 SameNameOccurrence = 0;
		const UEdGraphNode* OwningNode = Pin.GetOwningNode();
		if (OwningNode != nullptr)
		{
			for (int32 EarlierIndex = 0; EarlierIndex < PinIndex; ++EarlierIndex)
			{
				const UEdGraphPin* EarlierPin = OwningNode->Pins[EarlierIndex];
				if (EarlierPin != nullptr
					&& EarlierPin->Direction == Pin.Direction
					&& EarlierPin->PinName == Pin.PinName)
				{
					++SameNameOccurrence;
				}
			}
		}
		return SameNameOccurrence;
	}

	FString PinRole(const UEdGraphNode& Node, const UEdGraphPin& Pin)
	{
		if (const UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(&Node))
		{
			const UEdGraphPin* ValuePin = VariableSet->FindPin(
				VariableSet->GetVarName(),
				EGPD_Input);
			if (ValuePin == &Pin)
			{
				return TEXT("variable_set_value");
			}
		}

		if (const UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(&Node))
		{
			if (Branch->GetConditionPin() == &Pin)
			{
				return TEXT("branch_condition");
			}
		}

		return TEXT("none");
	}

	bool ExportBlueprintToJson(
		const UBlueprint& Blueprint,
		FString& OutFilePath,
		FCommandStats& OutStats,
		FString& OutError)
	{
		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("Exports"));
		const FString RequestedOutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory,
			FString::Printf(TEXT("%s.json"), *Blueprint.GetName())));

		BlueprintLensProductionExporter::FExportRequest Request;
		Request.Blueprint = &Blueprint;
		Request.OutputPath = RequestedOutputPath;
		BlueprintLensProductionExporter::FExportResult Result;
		BlueprintLensProductionExporter::FExportError Error;
		if (!BlueprintLensProductionExporter::ExportRawDocument(Request, Result, Error))
		{
			OutError = Error.Message;
			return false;
		}

		OutFilePath = Result.OutputPath;
		OutStats.GraphCount = Result.GraphCount;
		OutStats.NodeCount = Result.NodeCount;
		OutStats.PinCount = Result.PinCount;
		OutStats.EdgeCount = Result.EdgeCount;
		OutStats.UnsupportedNodeCount = Result.UnsupportedNodeCount;
		return true;
	}

	bool AuditBlueprintToTsv(
		const UBlueprint& Blueprint,
		FString& OutFilePath,
		FCommandStats& OutStats,
		FString& OutError)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		Algo::Sort(Graphs, [](const UEdGraph* Left, const UEdGraph* Right)
		{
			return Left->GetPathName() < Right->GetPathName();
		});

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("BLUEPRINT\t%s"), *Blueprint.GetPathName()));
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph == nullptr)
			{
				continue;
			}
			++OutStats.GraphCount;
			Lines.Add(FString::Printf(
				TEXT("GRAPH\t%s\t%s"),
				*Graph->GetPathName(),
				*Graph->GetName()));

			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}
				++OutStats.NodeCount;
				const FString NodeGuid = GuidToString(Node->NodeGuid);
				Lines.Add(FString::Printf(
					TEXT("NODE\t%s\t%s\t%s"),
					*Graph->GetPathName(),
					*NodeGuid,
					*Node->GetClass()->GetPathName()));

				if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Function = CallNode->GetTargetFunction();
					if (Function != nullptr && Function->HasMetaData(TEXT("Latent")))
					{
						++OutStats.UnsupportedNodeCount;
					}
				}

				for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
				{
					const UEdGraphPin* Pin = Node->Pins[PinIndex];
					if (Pin == nullptr)
					{
						continue;
					}
					++OutStats.PinCount;
					Lines.Add(FString::Printf(
						TEXT("PIN\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s"),
						*Graph->GetPathName(),
						*NodeGuid,
						*Pin->PinName.ToString(),
						*PinDirectionToString(Pin->Direction),
						PinSameNameOccurrence(*Pin, PinIndex),
						Pin->PinType.PinCategory == TEXT("exec")
							? TEXT("execution") : TEXT("data"),
						*PinRole(*Node, *Pin),
						*Pin->PinType.PinCategory.ToString(),
						*Pin->PinType.PinSubCategory.ToString(),
						Pin->PinType.PinSubCategoryObject.IsValid()
							? *Pin->PinType.PinSubCategoryObject->GetPathName()
							: TEXT(""),
						*PinContainerToString(Pin->PinType.ContainerType),
						Pin->PinType.bIsReference ? TEXT("true") : TEXT("false"),
						Pin->PinType.bIsConst ? TEXT("true") : TEXT("false"),
						Pin->PinType.bIsWeakPointer ? TEXT("true") : TEXT("false"),
						Pin->PinType.bIsUObjectWrapper ? TEXT("true") : TEXT("false"),
						Pin->PinType.bSerializeAsSinglePrecisionFloat
							? TEXT("true") : TEXT("false")));

					if (Pin->Direction != EGPD_Output)
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (LinkedPin == nullptr || LinkedPin->Direction != EGPD_Input)
						{
							continue;
						}
						const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
						if (LinkedNode == nullptr)
						{
							continue;
						}
						++OutStats.EdgeCount;
						const int32 LinkedPinIndex = LinkedNode->Pins.IndexOfByKey(LinkedPin);
						Lines.Add(FString::Printf(
							TEXT("EDGE\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%d\t%s"),
							*Graph->GetPathName(),
							*NodeGuid,
							*PinDirectionToString(Pin->Direction),
							*Pin->PinName.ToString(),
							PinSameNameOccurrence(*Pin, PinIndex),
							*GuidToString(LinkedNode->NodeGuid),
							*PinDirectionToString(LinkedPin->Direction),
							*LinkedPin->PinName.ToString(),
							PinSameNameOccurrence(*LinkedPin, LinkedPinIndex),
							Pin->PinType.PinCategory == TEXT("exec")
								? TEXT("execution") : TEXT("data")));
					}
				}
			}
		}

		Lines.Add(FString::Printf(
			TEXT("COUNTS\t%d\t%d\t%d\t%d\t%d"),
			OutStats.GraphCount,
			OutStats.NodeCount,
			OutStats.PinCount,
			OutStats.EdgeCount,
			OutStats.UnsupportedNodeCount));

		const FString OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens"),
			TEXT("Audits"));
		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(
				TEXT("Could not create audit directory: %s"),
				*OutputDirectory);
			return false;
		}
		OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			OutputDirectory,
			FString::Printf(TEXT("%s.inventory.tsv"), *Blueprint.GetName())));
		if (!FFileHelper::SaveStringArrayToFile(
			Lines,
			*OutFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write audit TSV: %s"), *OutFilePath);
			return false;
		}
		return true;
	}
}

void FBlueprintLensExporterModule::StartupModule()
{
	ExportConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.Export"),
		TEXT("Exports a Blueprint to deterministic raw JSON. Usage: BlueprintLens.Export /Game/Path/Asset"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleExportCommand),
		ECVF_Default);
	ExportBatchConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.ExportBatch"),
		TEXT("Exports a versioned M3 corpus manifest. Usage: BlueprintLens.ExportBatch <CorpusManifestPath> <OutputDirectory>"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleExportBatchCommand),
		ECVF_Default);
	AuditConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.Audit"),
		TEXT("Writes an independent Blueprint inventory TSV. Usage: BlueprintLens.Audit /Game/Path/Asset"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleAuditCommand),
		ECVF_Default);
	ExportSequenceFactsConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.ExportSequenceFacts"),
		TEXT("Exports source-owned Sequence output facts. Usage: BlueprintLens.ExportSequenceFacts /Game/Path/Asset SequenceNodeId"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleExportSequenceFactsCommand),
		ECVF_Default);
	AuditSequenceCompilerOrderConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.AuditSequenceCompilerOrder"),
		TEXT("Audits compiler-equivalent connected Sequence output order. Usage: BlueprintLens.AuditSequenceCompilerOrder /Game/Path/Asset SequenceNodeId"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleAuditSequenceCompilerOrderCommand),
		ECVF_Default);
	CreateLC4SequenceFixtureConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.CreateLC4SequenceFixture"),
		TEXT("Creates the frozen LC4-SEQ source-truth Blueprint fixture. Usage: BlueprintLens.CreateLC4SequenceFixture"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleCreateLC4SequenceFixtureCommand),
		ECVF_Default);
	ExportIntraBpPureCallFactsConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.ExportIntraBpPureCallFacts"),
		TEXT("Exports one bounded LC5 intra-BP pure-call source product. Usage: BlueprintLens.ExportIntraBpPureCallFacts /Game/Path/Asset CallNodeId RawExportPath"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleExportIntraBpPureCallFactsCommand),
		ECVF_Default);
	AuditIntraBpPureCallConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.AuditIntraBpPureCall"),
		TEXT("Independently audits one bounded LC5 intra-BP pure call. Usage: BlueprintLens.AuditIntraBpPureCall /Game/Path/Asset CallNodeId RawExportPath"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleAuditIntraBpPureCallCommand),
		ECVF_Default);
	CaptureLC6BoundaryTruthConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.CaptureLC6BoundaryTruth"),
		TEXT("Captures the bounded LC6 boundary source seam. Usage: BlueprintLens.CaptureLC6BoundaryTruth <absolute-output-directory>"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleCaptureLC6BoundaryTruthCommand),
		ECVF_Default);
	CaptureLC7StaticSCCTruthConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("BlueprintLens.CaptureLC7StaticSCCTruth"),
		TEXT("Captures the bounded LC7 static SCC source truth. Usage: BlueprintLens.CaptureLC7StaticSCCTruth <absolute-output-directory>"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBlueprintLensExporterModule::HandleCaptureLC7StaticSCCTruthCommand),
		ECVF_Default);

	UE_LOG(LogBlueprintLensExporter, Log, TEXT("BlueprintLensExporter editor module started."));
}

void FBlueprintLensExporterModule::ShutdownModule()
{
	if (ExportConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ExportConsoleCommand);
		ExportConsoleCommand = nullptr;
	}
	if (ExportBatchConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ExportBatchConsoleCommand);
		ExportBatchConsoleCommand = nullptr;
	}
	if (AuditConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AuditConsoleCommand);
		AuditConsoleCommand = nullptr;
	}
	if (ExportSequenceFactsConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ExportSequenceFactsConsoleCommand);
		ExportSequenceFactsConsoleCommand = nullptr;
	}
	if (AuditSequenceCompilerOrderConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AuditSequenceCompilerOrderConsoleCommand);
		AuditSequenceCompilerOrderConsoleCommand = nullptr;
	}
	if (CreateLC4SequenceFixtureConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CreateLC4SequenceFixtureConsoleCommand);
		CreateLC4SequenceFixtureConsoleCommand = nullptr;
	}
	if (ExportIntraBpPureCallFactsConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ExportIntraBpPureCallFactsConsoleCommand);
		ExportIntraBpPureCallFactsConsoleCommand = nullptr;
	}
	if (AuditIntraBpPureCallConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AuditIntraBpPureCallConsoleCommand);
		AuditIntraBpPureCallConsoleCommand = nullptr;
	}
	if (CaptureLC6BoundaryTruthConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CaptureLC6BoundaryTruthConsoleCommand);
		CaptureLC6BoundaryTruthConsoleCommand = nullptr;
	}
	if (CaptureLC7StaticSCCTruthConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CaptureLC7StaticSCCTruthConsoleCommand);
		CaptureLC7StaticSCCTruthConsoleCommand = nullptr;
	}

	UE_LOG(LogBlueprintLensExporter, Log, TEXT("BlueprintLensExporter editor module shut down."));
}

void FBlueprintLensExporterModule::HandleExportIntraBpPureCallFactsCommand(
	const TArray<FString>& Args)
{
	if (Args.Num() != 3)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Usage: BlueprintLens.ExportIntraBpPureCallFacts /Game/Path/BlueprintAsset CallNodeId RawExportPath"));
		return;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		*BlueprintLensExporter::ToObjectPath(Args[0]));
	if (Blueprint == nullptr)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC5 source Blueprint could not be loaded: %s"), *Args[0]);
		return;
	}
	FString OutputPath;
	FString Error;
	BlueprintLensIntraBpPureFacts::FIntraBpPureFactStats Stats;
	if (!BlueprintLensIntraBpPureFacts::ExportIntraBpPureCallFacts(
		*Blueprint,
		Args[1],
		FPaths::ConvertRelativePathToFull(Args[2]),
		OutputPath,
		Stats,
		Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC5 source export failed: %s"), *Error);
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("LC5_SOURCE_EXPORT_SUCCESS path=\"%s\" candidates=%d bindings=%d"),
		*OutputPath,
		Stats.CandidateCount,
		Stats.BindingCount);
}

void FBlueprintLensExporterModule::HandleAuditIntraBpPureCallCommand(
	const TArray<FString>& Args)
{
	if (Args.Num() != 3)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Usage: BlueprintLens.AuditIntraBpPureCall /Game/Path/BlueprintAsset CallNodeId RawExportPath"));
		return;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		*BlueprintLensExporter::ToObjectPath(Args[0]));
	if (Blueprint == nullptr)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC5 audit Blueprint could not be loaded: %s"), *Args[0]);
		return;
	}
	FString OutputPath;
	FString Error;
	BlueprintLensIntraBpPureAudit::FIntraBpPureAuditStats Stats;
	if (!BlueprintLensIntraBpPureAudit::AuditIntraBpPureCall(
		*Blueprint,
		Args[1],
		FPaths::ConvertRelativePathToFull(Args[2]),
		OutputPath,
		Stats,
		Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC5 independent audit failed: %s"), *Error);
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("LC5_AUDIT_SUCCESS path=\"%s\" candidates=%d bindings=%d"),
		*OutputPath,
		Stats.CandidateCount,
		Stats.BindingCount);
}

void FBlueprintLensExporterModule::HandleCaptureLC6BoundaryTruthCommand(
	const TArray<FString>& Args)
{
	if (Args.Num() != 1 || FPaths::IsRelative(Args[0]))
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("LC6_FIXTURE_SHAPE_INVALID: Usage: BlueprintLens.CaptureLC6BoundaryTruth <absolute-output-directory>"));
		return;
	}
	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(Args[0]);
	BlueprintLensLC6BoundaryFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC6BoundaryFixture::EnsureFixture(Anchors, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (Blueprint == nullptr || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC6_FIXTURE_SHAPE_INVALID: capture input/output is unavailable"));
		return;
	}
	FString CanonicalRawPath;
	BlueprintLensExporter::FCommandStats RawStats;
	if (!BlueprintLensExporter::ExportBlueprintToJson(*Blueprint, CanonicalRawPath, RawStats, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC6_FIXTURE_SHAPE_INVALID: %s"), *Error);
		return;
	}
	const FString CapturedRawPath = FPaths::Combine(
		OutputDirectory, TEXT("BP_LC6_BoundaryMatrix.raw-0.2.json"));
	if (IFileManager::Get().Copy(*CapturedRawPath, *CanonicalRawPath, true, true) != COPY_OK)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC6_FIXTURE_SHAPE_INVALID: raw export copy failed"));
		return;
	}
	FString SourcePath;
	FString AuditPath;
	BlueprintLensLC6BoundaryFacts::FBoundaryFactStats SourceStats;
	BlueprintLensLC6BoundaryAudit::FBoundaryAuditStats AuditStats;
	if (!BlueprintLensLC6BoundaryFacts::ExportBoundaryFacts(
			*Blueprint, Anchors, CapturedRawPath, OutputDirectory,
			SourcePath, SourceStats, Error))
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}
	if (!BlueprintLensLC6BoundaryAudit::AuditBoundarySource(
			*Blueprint, Anchors, CapturedRawPath, OutputDirectory,
			AuditPath, AuditStats, Error))
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		IFileManager::Get().Delete(*SourcePath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}
	if (!IFileManager::Get().FileExists(*CapturedRawPath)
		|| !IFileManager::Get().FileExists(*SourcePath)
		|| !IFileManager::Get().FileExists(*AuditPath)
		|| SourceStats.ScenarioCount != 4 || SourceStats.NodeCount != 16 || SourceStats.EdgeCount != 12
		|| AuditStats.ScenarioCount != 4 || AuditStats.NodeCount != 16 || AuditStats.EdgeCount != 12)
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		IFileManager::Get().Delete(*SourcePath, false, true, true);
		IFileManager::Get().Delete(*AuditPath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC6_SOURCE_AUDIT_MISMATCH: capture products or totals differ"));
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("LC6_BOUNDARY_CAPTURE_SUCCESS scenarios=4 nodes=16 edges=12"));
}

void FBlueprintLensExporterModule::HandleCaptureLC7StaticSCCTruthCommand(
	const TArray<FString>& Args)
{
	if (Args.Num() != 1 || FPaths::IsRelative(Args[0]))
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("LC7_FIXTURE_SHAPE_INVALID: Usage: BlueprintLens.CaptureLC7StaticSCCTruth <absolute-output-directory>"));
		return;
	}

	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(Args[0]);
	BlueprintLensLC7StaticSCCFixture::FFixtureAnchors Anchors;
	FString Error;
	if (!BlueprintLensLC7StaticSCCFixture::EnsureFixture(Anchors, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Anchors.AssetObjectPath);
	if (Blueprint == nullptr || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC7_FIXTURE_SHAPE_INVALID: capture input/output is unavailable"));
		return;
	}

	FString CanonicalRawPath;
	BlueprintLensExporter::FCommandStats RawStats;
	if (!BlueprintLensExporter::ExportBlueprintToJson(*Blueprint, CanonicalRawPath, RawStats, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC7_FIXTURE_SHAPE_INVALID: %s"), *Error);
		return;
	}
	const FString CapturedRawPath = FPaths::Combine(
		OutputDirectory, TEXT("BP_LC7_StaticSCC.raw-0.2.json"));
	if (IFileManager::Get().Copy(*CapturedRawPath, *CanonicalRawPath, true, true) != COPY_OK)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC7_FIXTURE_SHAPE_INVALID: raw export copy failed"));
		return;
	}

	FString SourcePath;
	FString AuditPath;
	BlueprintLensLC7StaticSCCFacts::FSCCFactStats SourceStats;
	BlueprintLensLC7StaticSCCAudit::FSCCAuditStats AuditStats;
	if (!BlueprintLensLC7StaticSCCFacts::ExportSCCFacts(
			*Blueprint,
			Anchors,
			CapturedRawPath,
			OutputDirectory,
			SourcePath,
			SourceStats,
			Error))
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}
	if (!BlueprintLensLC7StaticSCCAudit::AuditSCCSource(
			*Blueprint,
			Anchors,
			CapturedRawPath,
			OutputDirectory,
			AuditPath,
			AuditStats,
			Error))
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		IFileManager::Get().Delete(*SourcePath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("%s"), *Error);
		return;
	}

	const bool bSourceCountsMatch = SourceStats.NodeCount == 10
		&& SourceStats.EdgeCount == 10
		&& SourceStats.MemberCount == 3
		&& SourceStats.InternalEdgeCount == 3
		&& SourceStats.IncomingEdgeCount == 1
		&& SourceStats.OutgoingEdgeCount == 1;
	const bool bAuditCountsMatch = AuditStats.NodeCount == SourceStats.NodeCount
		&& AuditStats.EdgeCount == SourceStats.EdgeCount
		&& AuditStats.MemberCount == SourceStats.MemberCount
		&& AuditStats.InternalEdgeCount == SourceStats.InternalEdgeCount
		&& AuditStats.IncomingEdgeCount == SourceStats.IncomingEdgeCount
		&& AuditStats.OutgoingEdgeCount == SourceStats.OutgoingEdgeCount;
	if (!IFileManager::Get().FileExists(*CapturedRawPath)
		|| !IFileManager::Get().FileExists(*SourcePath)
		|| !IFileManager::Get().FileExists(*AuditPath)
		|| !bSourceCountsMatch
		|| !bAuditCountsMatch)
	{
		IFileManager::Get().Delete(*CapturedRawPath, false, true, true);
		IFileManager::Get().Delete(*SourcePath, false, true, true);
		IFileManager::Get().Delete(*AuditPath, false, true, true);
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC7_SOURCE_AUDIT_MISMATCH: capture products or totals differ"));
		return;
	}

	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("LC7_STATIC_SCC_CAPTURE_SUCCESS nodes=10 edges=10 scc_members=3 internal_edges=3 incoming_edges=1 outgoing_edges=1"));
}

void FBlueprintLensExporterModule::HandleExportCommand(const TArray<FString>& Args)
{
	if (Args.Num() != 1)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Usage: BlueprintLens.Export /Game/Path/BlueprintAsset"));
		return;
	}

	const FString ObjectPath = BlueprintLensExporter::ToObjectPath(Args[0]);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (Blueprint == nullptr)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Could not load Blueprint at '%s'."),
			*ObjectPath);
		return;
	}

	FString OutputPath;
	FString Error;
	BlueprintLensExporter::FCommandStats Stats;
	if (!BlueprintLensExporter::ExportBlueprintToJson(*Blueprint, OutputPath, Stats, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Export failed: %s"), *Error);
		return;
	}

	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("JSON_EXPORT_SUCCESS path=\"%s\" graphs=%d nodes=%d pins=%d edges=%d unsupported=%d"),
		*OutputPath,
		Stats.GraphCount,
		Stats.NodeCount,
		Stats.PinCount,
		Stats.EdgeCount,
		Stats.UnsupportedNodeCount);
}

void FBlueprintLensExporterModule::HandleExportBatchCommand(const TArray<FString>& Args)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Usage: BlueprintLens.ExportBatch <CorpusManifestPath> <OutputDirectory>"));
		return;
	}
	BlueprintLensM3Batch::FBatchRequest Request;
	Request.CorpusManifestPath = Args[0];
	Request.OutputDirectory = Args[1];
	BlueprintLensM3Batch::FBatchResult Result;
	FString ErrorCode;
	FString Error;
	if (!BlueprintLensM3Batch::ExportBatch(Request, Result, ErrorCode, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("M3_BATCH_FAILED code=%s error=%s"), *ErrorCode, *Error);
		return;
	}
	UE_LOG(LogBlueprintLensExporter, Display, TEXT("M3_BATCH_SUCCESS result=%s sha256=%s requested=%d exported=%d"), *Result.ResultManifestPath, *Result.ResultManifestSha256, Result.RequestedAssetCount, Result.ExportedAssetCount);
}

void FBlueprintLensExporterModule::HandleAuditCommand(const TArray<FString>& Args)
{
	if (Args.Num() != 1)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Usage: BlueprintLens.Audit /Game/Path/BlueprintAsset"));
		return;
	}

	const FString ObjectPath = BlueprintLensExporter::ToObjectPath(Args[0]);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (Blueprint == nullptr)
	{
		UE_LOG(
			LogBlueprintLensExporter,
			Error,
			TEXT("Could not load Blueprint at '%s'."),
			*ObjectPath);
		return;
	}

	FString OutputPath;
	FString Error;
	BlueprintLensExporter::FCommandStats Stats;
	if (!BlueprintLensExporter::AuditBlueprintToTsv(
		*Blueprint,
		OutputPath,
		Stats,
		Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("AUDIT_FAILED: %s"), *Error);
		return;
	}

	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("AUDIT_SUCCESS path=\"%s\" graphs=%d nodes=%d pins=%d edges=%d unsupported=%d"),
		*OutputPath,
		Stats.GraphCount,
		Stats.NodeCount,
		Stats.PinCount,
		Stats.EdgeCount,
		Stats.UnsupportedNodeCount);
}

void FBlueprintLensExporterModule::HandleExportSequenceFactsCommand(const TArray<FString>& Args)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Usage: BlueprintLens.ExportSequenceFacts /Game/Path/BlueprintAsset SequenceNodeId"));
		return;
	}
	const FString ObjectPath = BlueprintLensExporter::ToObjectPath(Args[0]);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (Blueprint == nullptr)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Could not load Blueprint at '%s'."), *ObjectPath);
		return;
	}
	FString OutputPath;
	FString Error;
	BlueprintLensSequenceFacts::FSequenceFactStats Stats;
	if (!BlueprintLensSequenceFacts::ExportSequenceFacts(
		*Blueprint,
		Args[1],
		OutputPath,
		Stats,
		Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("SEQUENCE_FACTS_FAILED: %s"), *Error);
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("SEQUENCE_FACTS_SUCCESS path=\"%s\" declared=%d connected=%d unconnected=%d"),
		*OutputPath,
		Stats.DeclaredOutputCount,
		Stats.ConnectedOutputCount,
		Stats.UnconnectedOutputCount);
}

void FBlueprintLensExporterModule::HandleAuditSequenceCompilerOrderCommand(const TArray<FString>& Args)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Usage: BlueprintLens.AuditSequenceCompilerOrder /Game/Path/BlueprintAsset SequenceNodeId"));
		return;
	}
	const FString ObjectPath = BlueprintLensExporter::ToObjectPath(Args[0]);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (Blueprint == nullptr)
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Could not load Blueprint at '%s'."), *ObjectPath);
		return;
	}
	FString OutputPath;
	FString Error;
	BlueprintLensSequenceCompilerAudit::FSequenceCompilerAuditStats Stats;
	if (!BlueprintLensSequenceCompilerAudit::AuditSequenceCompilerOrder(
		*Blueprint,
		Args[1],
		OutputPath,
		Stats,
		Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("SEQUENCE_COMPILER_AUDIT_FAILED: %s"), *Error);
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("SEQUENCE_COMPILER_AUDIT_SUCCESS path=\"%s\" connected=%d"),
		*OutputPath,
		Stats.ConnectedOutputCount);
}

void FBlueprintLensExporterModule::HandleCreateLC4SequenceFixtureCommand(const TArray<FString>& Args)
{
	if (!Args.IsEmpty())
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("Usage: BlueprintLens.CreateLC4SequenceFixture"));
		return;
	}
	FString AssetObjectPath;
	FString SequenceNodeId;
	FString Error;
	if (!BlueprintLensLC4SequenceFixture::CreateFixture(AssetObjectPath, SequenceNodeId, Error))
	{
		UE_LOG(LogBlueprintLensExporter, Error, TEXT("LC4_SEQUENCE_FIXTURE_FAILED: %s"), *Error);
		return;
	}
	UE_LOG(
		LogBlueprintLensExporter,
		Display,
		TEXT("LC4_SEQUENCE_FIXTURE_SUCCESS asset=\"%s\" sequence_node_id=\"%s\""),
		*AssetObjectPath,
		*SequenceNodeId);
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensCreateLC4SequenceFixtureTest,
	"BlueprintLens.Exporter.CreateLC4SequenceFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensCreateLC4SequenceFixtureTest::RunTest(const FString& Parameters)
{
	UBlueprint* Existing = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LensCorpus/BP_LC4_SequenceDisclosure.BP_LC4_SequenceDisclosure"));
	if (Existing != nullptr)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Existing);
		UK2Node_ExecutionSequence* Sequence = nullptr;
		UK2Node_VariableSet* Reconverged = nullptr;
		int32 SequenceCount = 0;
		if (EventGraph != nullptr)
		{
			for (UEdGraphNode* Node : EventGraph->Nodes)
			{
				if (UK2Node_ExecutionSequence* Candidate = Cast<UK2Node_ExecutionSequence>(Node))
				{
					++SequenceCount;
					Sequence = Candidate;
				}
				if (UK2Node_VariableSet* Candidate = Cast<UK2Node_VariableSet>(Node);
					Candidate != nullptr && Candidate->GetVarName() == TEXT("LC4Reconverged"))
				{
					Reconverged = Candidate;
				}
			}
		}
		UEdGraphPin* ReconvergenceInput = Reconverged == nullptr
			? nullptr
			: Reconverged->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		const bool bShapeValid = EventGraph != nullptr
			&& EventGraph->Nodes.Num() == 9
			&& SequenceCount == 1
			&& Sequence != nullptr
			&& Sequence->GetThenPinGivenIndex(0) != nullptr
			&& Sequence->GetThenPinGivenIndex(0)->LinkedTo.Num() == 1
			&& Sequence->GetThenPinGivenIndex(1) != nullptr
			&& Sequence->GetThenPinGivenIndex(1)->LinkedTo.Num() == 1
			&& Sequence->GetThenPinGivenIndex(2) != nullptr
			&& Sequence->GetThenPinGivenIndex(2)->LinkedTo.Num() == 1
			&& Sequence->GetThenPinGivenIndex(3) != nullptr
			&& Sequence->GetThenPinGivenIndex(3)->LinkedTo.IsEmpty()
			&& Sequence->GetThenPinGivenIndex(4) == nullptr
			&& ReconvergenceInput != nullptr
			&& ReconvergenceInput->LinkedTo.Num() == 2;
		if (!bShapeValid)
		{
			AddError(TEXT("Existing LC4-SEQ fixture does not match the frozen source shape."));
			return false;
		}
		AddInfo(TEXT("LC4_SEQUENCE_FIXTURE_EXISTING_SHAPE_VERIFIED"));
		return true;
	}

	FString AssetObjectPath;
	FString SequenceNodeId;
	FString Error;
	if (!BlueprintLensLC4SequenceFixture::CreateFixture(AssetObjectPath, SequenceNodeId, Error))
	{
		AddError(FString::Printf(TEXT("LC4_SEQUENCE_FIXTURE_FAILED: %s"), *Error));
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("LC4_SEQUENCE_FIXTURE_SUCCESS asset=\"%s\" sequence_node_id=\"%s\""),
		*AssetObjectPath,
		*SequenceNodeId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensExportLC4SequenceFixtureTest,
	"BlueprintLens.Exporter.ExportLC4SequenceFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensExportLC4SequenceFixtureTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LensCorpus/BP_LC4_SequenceDisclosure.BP_LC4_SequenceDisclosure"));
	if (Blueprint == nullptr)
	{
		AddError(TEXT("Could not load the LC4-SEQ fixture."));
		return false;
	}

	UK2Node_ExecutionSequence* Sequence = nullptr;
	UEdGraph* SequenceGraph = nullptr;
	int32 SequenceCount = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph == nullptr)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_ExecutionSequence* Candidate = Cast<UK2Node_ExecutionSequence>(Node))
			{
				++SequenceCount;
				Sequence = Candidate;
				SequenceGraph = Graph;
			}
		}
	}
	if (SequenceCount != 1 || Sequence == nullptr || SequenceGraph == nullptr)
	{
		AddError(FString::Printf(
			TEXT("LC4-SEQ fixture must contain exactly one Sequence node (found %d)."),
			SequenceCount));
		return false;
	}

	const FString SequenceNodeId = BlueprintLensSequenceFacts::MakeNodeId(
		SequenceGraph->GetPathName(),
		*Sequence);
	FString RawPath;
	FString SourcePath;
	FString AuditPath;
	FString Error;
	BlueprintLensExporter::FCommandStats RawStats;
	BlueprintLensSequenceFacts::FSequenceFactStats SourceStats;
	BlueprintLensSequenceCompilerAudit::FSequenceCompilerAuditStats AuditStats;
	if (!BlueprintLensExporter::ExportBlueprintToJson(*Blueprint, RawPath, RawStats, Error))
	{
		AddError(FString::Printf(TEXT("LC4 raw export failed: %s"), *Error));
		return false;
	}
	if (!BlueprintLensSequenceFacts::ExportSequenceFacts(
		*Blueprint, SequenceNodeId, SourcePath, SourceStats, Error))
	{
		AddError(FString::Printf(TEXT("LC4 Sequence source export failed: %s"), *Error));
		return false;
	}
	if (!BlueprintLensSequenceCompilerAudit::AuditSequenceCompilerOrder(
		*Blueprint, SequenceNodeId, AuditPath, AuditStats, Error))
	{
		AddError(FString::Printf(TEXT("LC4 compiler-order audit failed: %s"), *Error));
		return false;
	}

	AddInfo(FString::Printf(
		TEXT("LC4_SEQUENCE_EXPORT_SUCCESS sequence_node_id=\"%s\" raw=\"%s\" source=\"%s\" audit=\"%s\" graphs=%d nodes=%d edges=%d declared=%d connected=%d unconnected=%d audited_connected=%d"),
		*SequenceNodeId,
		*RawPath,
		*SourcePath,
		*AuditPath,
		RawStats.GraphCount,
		RawStats.NodeCount,
		RawStats.EdgeCount,
		SourceStats.DeclaredOutputCount,
		SourceStats.ConnectedOutputCount,
		SourceStats.UnconnectedOutputCount,
		AuditStats.ConnectedOutputCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensAuditPinTypeFactsTest,
	"BlueprintLens.Exporter.AuditPinTypeFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensAuditPinTypeFactsTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"));
	if (!TestNotNull(TEXT("LC2 Blueprint loads"), Blueprint))
	{
		return false;
	}

	FString OutputPath;
	FString Error;
	BlueprintLensExporter::FCommandStats Stats;
	if (!TestTrue(
			TEXT("independent audit succeeds"),
			BlueprintLensExporter::AuditBlueprintToTsv(
				*Blueprint,
				OutputPath,
				Stats,
				Error)))
	{
		AddError(Error);
		return false;
	}

	TArray<FString> Lines;
	if (!TestTrue(
			TEXT("audit TSV is readable"),
			FFileHelper::LoadFileToStringArray(Lines, *OutputPath)))
	{
		return false;
	}

	const TSet<FString> PredicatePins = {
		TEXT("c0444d2c-4728-7756-b082-8b924827bfcb|OuterEnabled"),
		TEXT("71d3417e-4477-b800-5599-aeb5cf167aab|Condition"),
		TEXT("887cb668-4875-a4f3-881d-bdbdc8f9ada4|InnerEnabled"),
		TEXT("4dd89d06-4808-31de-0aac-3482198a04f8|Condition")};
	int32 PinRows = 0;
	int32 PredicateRows = 0;
	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("PIN\t")))
		{
			continue;
		}
		++PinRows;
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT("\t"), false);
		TestEqual(TEXT("PIN row includes exact type facts"), Fields.Num(), 17);
		if (Fields.Num() != 17)
		{
			continue;
		}

		if (PredicatePins.Contains(Fields[2] + TEXT("|") + Fields[3]))
		{
			++PredicateRows;
			TestEqual(TEXT("predicate category"), Fields[8], TEXT("bool"));
			TestEqual(TEXT("predicate subcategory"), Fields[9], TEXT("None"));
			TestEqual(TEXT("predicate object path"), Fields[10], TEXT(""));
			TestEqual(TEXT("predicate container"), Fields[11], TEXT("none"));
			TestEqual(TEXT("predicate reference flag"), Fields[12], TEXT("false"));
			TestEqual(TEXT("predicate const flag"), Fields[13], TEXT("false"));
			TestEqual(TEXT("predicate weak flag"), Fields[14], TEXT("false"));
			TestEqual(TEXT("predicate wrapper flag"), Fields[15], TEXT("false"));
			TestEqual(TEXT("predicate float flag"), Fields[16], TEXT("false"));
		}
	}

	TestEqual(TEXT("all pins independently audited"), PinRows, 35);
	TestEqual(TEXT("all predicate endpoints independently audited"), PredicateRows, 4);
	return !HasAnyErrors();
}

#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintLensExporterModule, BlueprintLensExporter)
