// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensM6ProcessRunner.h"
#include "BlueprintLensM6SessionController.h"
#include "BlueprintLensM6Telemetry.h"
#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensF12DataAnswerProjection.h"
#include "BlueprintLensLC2GuardLayoutSession.h"
#include "BlueprintLensLC2GuardSurfaceLayout.h"
#include "BlueprintLensLC2LiveExplanationAdapter.h"
#include "BlueprintLensLC3LiveExplanationAdapter.h"
#include "BlueprintLensLC4SequenceLiveAdapter.h"
#include "BlueprintLensLC5Layout.h"
#include "BlueprintLensLC5LiveTypedIrAdapter.h"
#include "BlueprintLensLC6LiveExplanationAdapter.h"
#include "BlueprintLensLC6Layout.h"
#include "BlueprintLensLC7LayoutSession.h"
#include "BlueprintLensLC7LiveExplanationAdapter.h"
#include "BlueprintLensLC7Projection.h"
#include "BlueprintLensEditorModule.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "GraphEditor.h"
#include "IPlatformCrypto.h"
#include "SBlueprintLensPanel.h"
#include "SBlueprintLensLC5TypedPortal.h"
#include "SBlueprintLensLC6FourTrack.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
void CollectSlateWidgets(
	const TSharedRef<SWidget>& Root,
	TArray<TSharedRef<SWidget>>& OutWidgets)
{
	OutWidgets.Add(Root);
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		CollectSlateWidgets(Children->GetChildAt(Index), OutWidgets);
	}
}

FString SlateWidgetText(const TSharedRef<SWidget>& Root)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	TArray<FString> Texts;
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock"))
		{
			Texts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
		}
	}
	return FString::Join(Texts, TEXT("\n"));
}

bool SlateHasWidgetTag(
	const TSharedRef<SWidget>& Root,
	const FName Tag)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	return Widgets.ContainsByPredicate(
		[Tag](const TSharedRef<SWidget>& Widget)
		{
			return Widget->GetTag() == Tag;
		});
}

TSharedPtr<SWidget> SlateWidgetWithTag(
	const TSharedRef<SWidget>& Root,
	const FName Tag)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	const TSharedRef<SWidget>* Match = Widgets.FindByPredicate(
		[Tag](const TSharedRef<SWidget>& Widget)
		{
			return Widget->GetTag() == Tag;
		});
	return Match != nullptr ? TSharedPtr<SWidget>(*Match) : nullptr;
}

TArray<TSharedRef<SWidget>> SlateWidgetsWithTag(
	const TSharedRef<SWidget>& Root,
	const FName Tag)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	return Widgets.FilterByPredicate(
		[Tag](const TSharedRef<SWidget>& Widget)
		{
			return Widget->GetTag() == Tag;
		});
}

FString Sha256Text(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	TUniquePtr<FEncryptionContext> Context =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!Context.IsValid() || !Context->CalcSHA256(Bytes, Digest) ||
		Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num());
}

const TCHAR* LC5RelationFamilyName(
	const EBlueprintLensLayoutRelationFamily Family)
{
	switch (Family)
	{
	case EBlueprintLensLayoutRelationFamily::Value:
		return TEXT("value");
	case EBlueprintLensLayoutRelationFamily::Predicate:
		return TEXT("predicate");
	case EBlueprintLensLayoutRelationFamily::Portal:
		return TEXT("portal");
	case EBlueprintLensLayoutRelationFamily::Frontier:
		return TEXT("frontier");
	case EBlueprintLensLayoutRelationFamily::BackEdge:
		return TEXT("back_edge");
	case EBlueprintLensLayoutRelationFamily::Execution:
	default:
		return TEXT("execution");
	}
}

TSharedRef<FJsonObject> LC5PointJson(const FVector2D& Point)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("x"), Point.X);
	Result->SetNumberField(TEXT("y"), Point.Y);
	return Result;
}

TSharedRef<FJsonObject> LC5BoundsJson(
	const FVector2D& Position,
	const FVector2D& Size)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("x"), Position.X);
	Result->SetNumberField(TEXT("y"), Position.Y);
	Result->SetNumberField(TEXT("width"), Size.X);
	Result->SetNumberField(TEXT("height"), Size.Y);
	return Result;
}

TSharedRef<FJsonObject> LC5LayoutJson(
	const FBlueprintLensLC5Layout& Layout,
	const float Width)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("viewport_width"), Width);
	Result->SetObjectField(
		TEXT("canvas_size"),
		LC5PointJson(Layout.CanvasSize));
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (const FBlueprintLensLayoutNodePlacement& Node : Layout.LayoutLedger.Nodes)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("unit_id"), Node.UnitId);
		Row->SetNumberField(
			TEXT("static_rank"),
			Layout.StaticRanks.Contains(Node.UnitId)
				? Layout.StaticRanks.FindChecked(Node.UnitId)
				: -1);
		Row->SetObjectField(
			TEXT("bounds"),
			LC5BoundsJson(Node.Position, Node.Size));
		Nodes.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("nodes"), Nodes);

	TArray<TSharedPtr<FJsonValue>> Ports;
	for (const FBlueprintLensLayoutPortPlacement& Port : Layout.LayoutLedger.Ports)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("unit_id"), Port.UnitId);
		Row->SetStringField(TEXT("port_id"), Port.Label);
		Row->SetStringField(
			TEXT("direction"),
			Port.bInput ? TEXT("input") : TEXT("output"));
		Row->SetObjectField(TEXT("position"), LC5PointJson(Port.Position));
		Ports.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("ports"), Ports);

	TArray<TSharedPtr<FJsonValue>> Relations;
	for (const FBlueprintLensLayoutEdgePlacement& Edge : Layout.LayoutLedger.Edges)
	{
		const FBlueprintLensLayoutPortPlacement* Source =
			Layout.LayoutLedger.FindPort(
				Edge.SourceUnitId,
				Edge.SourcePortLabel,
				false);
		const FBlueprintLensLayoutPortPlacement* Target =
			Layout.LayoutLedger.FindPort(
				Edge.TargetUnitId,
				Edge.TargetPortLabel,
				true);
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("relation_id"), Edge.RelationId);
		Row->SetStringField(TEXT("family"), LC5RelationFamilyName(Edge.Family));
		Row->SetStringField(TEXT("source_unit_id"), Edge.SourceUnitId);
		Row->SetStringField(TEXT("target_unit_id"), Edge.TargetUnitId);
		Row->SetStringField(TEXT("source_port_id"), Edge.SourcePortLabel);
		Row->SetStringField(TEXT("target_port_id"), Edge.TargetPortLabel);
		if (Source != nullptr)
		{
			Row->SetObjectField(
				TEXT("source_endpoint"),
				LC5PointJson(Source->Position));
		}
		if (Target != nullptr)
		{
			Row->SetObjectField(
				TEXT("target_endpoint"),
				LC5PointJson(Target->Position));
		}
		TArray<TSharedPtr<FJsonValue>> Bends;
		for (const FVector2D& Bend : Edge.BendPoints)
		{
			Bends.Add(MakeShared<FJsonValueObject>(LC5PointJson(Bend)));
		}
		Row->SetArrayField(TEXT("bends"), Bends);
		Relations.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("relations"), Relations);
	TArray<TSharedPtr<FJsonValue>> Labels;
	for (const FBlueprintLensLC5Label& Label : Layout.Labels)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("label_id"), Label.Id);
		Row->SetStringField(TEXT("text"), Label.Text);
		Row->SetObjectField(
			TEXT("bounds"),
			LC5BoundsJson(Label.Bounds.Min, Label.Bounds.GetSize()));
		Labels.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("labels"), Labels);
	return Result;
}

bool WriteLC5LayoutSeed(
	const FBlueprintLensLC5Projection& Projection,
	const TArray<TPair<float, FBlueprintLensLC5Layout>>& Layouts,
	FString& OutPath)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_name"),
		TEXT("blueprint-lens-lc5-runencounter-layout-seed"));
	Root->SetStringField(TEXT("schema_version"), TEXT("1.0.0"));
	Root->SetStringField(
		TEXT("source_asset"),
		Projection.SourceBlueprintAssetPath);
	Root->SetStringField(
		TEXT("caller_graph_id"),
		Projection.CallerGraphId);
	Root->SetStringField(
		TEXT("callee_graph_name"),
		Projection.CalleeGraphName);
	Root->SetStringField(
		TEXT("call_occurrence_id"),
		Layouts[0].Value.CallOccurrenceId);
	TArray<TSharedPtr<FJsonValue>> LayoutValues;
	for (const TPair<float, FBlueprintLensLC5Layout>& Layout : Layouts)
	{
		LayoutValues.Add(MakeShared<FJsonValueObject>(
			LC5LayoutJson(Layout.Value, Layout.Key)));
	}
	Root->SetArrayField(TEXT("layouts"), LayoutValues);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("BlueprintLens/M10/lc5-runencounter-layout-ledger.seed.json")));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	return FFileHelper::SaveStringToFile(
		Json + TEXT("\n"),
		*OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AppendLC1MutationValue(
	FString& Canonical,
	const TCHAR* Label,
	const FString& Value)
{
	Canonical += Label;
	Canonical += FString::Printf(TEXT("[%d:%s]"), Value.Len(), *Value);
}

void AppendLC1MutationIds(
	FString& Canonical,
	const TCHAR* Label,
	const TArray<FString>& Ids)
{
	Canonical += Label;
	Canonical += TEXT("[");
	for (const FString& Id : Ids)
	{
		Canonical += FString::Printf(TEXT("%d:%s;"), Id.Len(), *Id);
	}
	Canonical += TEXT("]");
}

// Mutation tests deliberately alter a projection after Build(). Re-signing the
// complete public ledger keeps integrity valid so IsRenderable() must exercise
// the semantic ordering invariant instead of failing early on the hash.
void ResignLC1RailProjectionForMutation(
	FBlueprintLensLC1RailProjection& Projection)
{
	FString Canonical;
	AppendLC1MutationValue(
		Canonical, TEXT("version"), Projection.ProjectorVersion);
	AppendLC1MutationValue(
		Canonical, TEXT("source-ir"), Projection.SourceIrSha256);
	AppendLC1MutationValue(
		Canonical, TEXT("criterion"), Projection.CriterionUnitId);
	AppendLC1MutationValue(
		Canonical, TEXT("criterion-label"), Projection.CriterionReaderLabel);
	AppendLC1MutationValue(
		Canonical, TEXT("criterion-display"), Projection.CriterionDisplayLabel);
	AppendLC1MutationIds(Canonical, TEXT("all-units"), Projection.AllUnitIds);
	AppendLC1MutationIds(
		Canonical, TEXT("all-relations"), Projection.AllRelationIds);
	AppendLC1MutationIds(
		Canonical, TEXT("deferred-units"), Projection.DeferredUnitIds);
	AppendLC1MutationIds(
		Canonical, TEXT("deferred-relations"), Projection.DeferredRelationIds);
	AppendLC1MutationIds(
		Canonical, TEXT("fallback-units"), Projection.FallbackUnitIds);
	AppendLC1MutationIds(
		Canonical, TEXT("fallback-relations"), Projection.FallbackRelationIds);
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		 Projection.OrderedCanonicalUnits)
	{
		AppendLC1MutationValue(Canonical, TEXT("unit-id"), Unit.UnitId);
		AppendLC1MutationValue(Canonical, TEXT("unit-label"), Unit.ReaderLabel);
		AppendLC1MutationValue(Canonical, TEXT("unit-display"), Unit.DisplayLabel);
		AppendLC1MutationValue(
			Canonical,
			TEXT("unit-criterion"),
			Unit.bIsCriterion ? TEXT("1") : TEXT("0"));
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 Projection.OrderedExecutionRelations)
	{
		AppendLC1MutationValue(
			Canonical, TEXT("relation-id"), Relation.RelationId);
		AppendLC1MutationValue(
			Canonical, TEXT("relation-source"), Relation.SourceUnitId);
		AppendLC1MutationValue(
			Canonical, TEXT("relation-target"), Relation.TargetUnitId);
	}
	for (const FBlueprintLensLC1RailExecutionRelation& Relation :
		 Projection.StationOrderRelations)
	{
		AppendLC1MutationValue(
			Canonical, TEXT("order-relation-id"), Relation.RelationId);
		AppendLC1MutationValue(
			Canonical, TEXT("order-relation-source"), Relation.SourceUnitId);
		AppendLC1MutationValue(
			Canonical, TEXT("order-relation-target"), Relation.TargetUnitId);
	}
	for (const FBlueprintLensLC1RailOrderRegion& Region :
		 Projection.OrderRegions)
	{
		AppendLC1MutationValue(
			Canonical, TEXT("order-region-id"), Region.RegionId);
		AppendLC1MutationValue(
			Canonical,
			TEXT("order-region-kind"),
			FString::FromInt(static_cast<int32>(Region.Kind)));
		AppendLC1MutationIds(
			Canonical, TEXT("order-region-members"), Region.MemberUnitIds);
		AppendLC1MutationValue(
			Canonical, TEXT("order-region-reader"), Region.ReaderText);
	}
	for (const FBlueprintLensLC1RailBoundaryCap& Cap : Projection.BoundaryCaps)
	{
		AppendLC1MutationValue(Canonical, TEXT("cap-unit"), Cap.UnitId);
		AppendLC1MutationValue(
			Canonical,
			TEXT("cap-status"),
			FString::FromInt(static_cast<int32>(Cap.SemanticStatus)));
		AppendLC1MutationValue(Canonical, TEXT("cap-title"), Cap.Title);
		AppendLC1MutationValue(
			Canonical, TEXT("cap-disclosure"), Cap.Disclosure);
	}
	AppendLC1MutationValue(
		Canonical,
		TEXT("status"),
		FString::FromInt(static_cast<int32>(Projection.Status)));
	AppendLC1MutationValue(
		Canonical, TEXT("diagnostic"), Projection.DiagnosticCode);
	FTCHARToUTF8 Utf8(*Canonical);
	FMD5 Hash;
	Hash.Update(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		Utf8.Length());
	uint8 Digest[16];
	Hash.Final(Digest);
	Projection.ProjectionIntegrityHash =
		BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
}

FString LC1FixturePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/Explanation/"
				 "BP_LC1_LongChain.explanation.v1.json"))
		: FString();
}

FString LC2FixturePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/Explanation/"
				 "BP_LC2_NestedGuards.explanation.v1.json"))
		: FString();
}

FString LC3FixturePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/Explanation/"
				 "BP_LC3_ValueProvenance.explanation.v1.json"))
		: FString();
}

FString M7MotifScaleExplanationPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/m10/ue/"
			 "motif-measurement-packet-20260826/explanation.json")));
}

FString M7CorpusMainTypedIrPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/m7/export/run2/typed-ir/"
			 "8ca0b6a00f2943b8672a1308934442b81e1e88fb2cc366c0805668e872ba7661."
			 "blueprint-lens-v1.json")));
}

FString M7SlicingProbeTypedIrPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/m7/export/run2/typed-ir/"
			 "60fb3c769ebf10e0a24a0217d14cfd19c168b7e294ca7d9f9c26cb10b59e6386."
			 "blueprint-lens-v1.json")));
}

FBlueprintLensExplanationModel BuildLC6LiveBoundaryShape(
	const FString& BlueprintAssetPath,
	const FString& IdentityPrefix,
	const TArray<EBlueprintLensSemanticStatus>& BoundaryStatuses,
	const EBlueprintLensSemanticStatus CriterionStatus =
		EBlueprintLensSemanticStatus::Supported)
{
	FBlueprintLensExplanationModel Result;
	Result.Format = TEXT("blueprint-lens-explanation");
	Result.SchemaVersion = TEXT("1.0.0");
	Result.RulesVersion = TEXT("1.0.0");
	Result.Source.BlueprintAssetPath = BlueprintAssetPath;
	Result.Source.BlueprintPackageSha256 = FString::ChrN(64, TEXT('6'));
	Result.Source.GraphId = BlueprintAssetPath + TEXT(":EventGraph");
	Result.Source.IrPath = TEXT("lc6-live-typed-ir.json");
	Result.Source.IrSha256 = FString::ChrN(64, TEXT('b'));
	Result.Source.SlicePath = TEXT("lc6-live-slice.json");
	Result.Source.SliceSha256 = FString::ChrN(64, TEXT('c'));
	Result.Query.Question = TEXT("Why does the selected boundary criterion execute?");
	Result.Query.Direction = TEXT("backward_only");

	const auto StatusName = [](const EBlueprintLensSemanticStatus Status)
	{
		switch (Status)
		{
		case EBlueprintLensSemanticStatus::Opaque:
			return FString(TEXT("Opaque"));
		case EBlueprintLensSemanticStatus::Uncertain:
			return FString(TEXT("Uncertain"));
		case EBlueprintLensSemanticStatus::Unsupported:
			return FString(TEXT("Unsupported"));
		default:
			return FString(TEXT("Supported"));
		}
	};
	const auto AddUnit = [&Result, &IdentityPrefix](
		const FString& Id,
		const FString& Title,
		const EBlueprintLensRole Role,
		const EBlueprintLensSemanticStatus Status)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = Id;
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Title = Title;
		Unit.Role = Role;
		Unit.SemanticStatus = Status;
		Unit.InclusionReasons = {
			Role == EBlueprintLensRole::Criterion
				? TEXT("criterion")
				: TEXT("execution_predecessor")};
		FBlueprintLensSourceReference Source;
		Source.BlueprintAssetPath = Result.Source.BlueprintAssetPath;
		Source.GraphId = Result.Source.GraphId;
		Source.SourceNodeId = FString::Printf(
			TEXT("%s::node::%s"), *Result.Source.GraphId, *Id);
		Source.NativeNodeGuid = IdentityPrefix + TEXT("-") + Id;
		Source.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Source));
		Result.Units.Add(MoveTemp(Unit));
	};

	for (int32 Index = 0; Index < BoundaryStatuses.Num(); ++Index)
	{
		const FString UnitId = FString::Printf(TEXT("unit.boundary.%d"), Index);
		AddUnit(
			UnitId,
			FString::Printf(
				TEXT("%s %s Boundary %d"),
				*IdentityPrefix,
				*StatusName(BoundaryStatuses[Index]),
				Index),
			EBlueprintLensRole::Boundary,
			BoundaryStatuses[Index]);
	}
	AddUnit(
		TEXT("unit.criterion"),
		IdentityPrefix + TEXT(" Criterion"),
		EBlueprintLensRole::Criterion,
		CriterionStatus);
	Result.CriterionUnitId = TEXT("unit.criterion");
	Result.Query.CriterionSourceNodeId =
		Result.Units.Last().SourceReferences[0].SourceNodeId;

	for (int32 Index = 0; Index < Result.Units.Num() - 1; ++Index)
	{
		FBlueprintLensRelation Relation;
		Relation.Id = FString::Printf(TEXT("relation.execution.%d"), Index);
		Relation.SourceUnitId = Result.Units[Index].Id;
		Relation.TargetUnitId = Result.Units[Index + 1].Id;
		Relation.Kind = EBlueprintLensRelationKind::ExecutionPredecessor;
		Relation.Label = TEXT("then");
		Relation.bHasSemanticLabel = true;
		Relation.SemanticLabel = EBlueprintLensSemanticLabel::NextExecution;
		Relation.SourceEdgeIds = {
			FString::Printf(TEXT("source-edge.%d"), Index)};
		Result.Relations.Add(MoveTemp(Relation));
	}
	Result.Counts.Units = Result.Units.Num();
	Result.Counts.Relations = Result.Relations.Num();
	Result.Counts.SourceNodes = Result.Units.Num();
	Result.Counts.SourceEdges = Result.Relations.Num();
	return Result;
}

FBlueprintLensExplanationModel BuildLC5LiveCallShape(
	const FString& BlueprintAssetPath,
	const FString& TypedIrPath,
	const FString& TypedIrSha256,
	const FString& CallTitle,
	const FString& CallSourceNodeId,
	const FString& IdentityPrefix)
{
	FBlueprintLensExplanationModel Result = BuildLC6LiveBoundaryShape(
		BlueprintAssetPath,
		IdentityPrefix,
		{EBlueprintLensSemanticStatus::Opaque});
	Result.Source.IrPath = TypedIrPath;
	Result.Source.IrSha256 = TypedIrSha256;
	FBlueprintLensUnit& Call = Result.Units[0];
	Call.Title = CallTitle;
	Call.SourceReferences[0].SourceNodeId = CallSourceNodeId;
	Call.SourceReferences[0].GraphId = Result.Source.GraphId;
	Call.SourceReferences[0].BlueprintAssetPath = BlueprintAssetPath;
	Call.SourceReferences[0].NativeNodeGuid = CallSourceNodeId.RightChop(
		CallSourceNodeId.Find(TEXT("::node::"), ESearchCase::CaseSensitive,
			ESearchDir::FromEnd) + 8);
	Result.Query.CriterionSourceNodeId =
		Result.Units.Last().SourceReferences[0].SourceNodeId;
	return Result;
}

FBlueprintLensExplanationModel BuildLC7LiveEngineSampleShape(
	const FString& BlueprintAssetPath,
	const FString& GateTitle)
{
	FBlueprintLensExplanationModel Result;
	Result.Format = TEXT("blueprint-lens-explanation");
	Result.SchemaVersion = TEXT("1.0.0");
	Result.RulesVersion = TEXT("1.0.0");
	Result.Source.BlueprintAssetPath = BlueprintAssetPath;
	Result.Source.BlueprintPackageSha256 = FString::ChrN(64, TEXT('e'));
	Result.Source.GraphId = BlueprintAssetPath + TEXT(":EventGraph");
	Result.Source.IrPath = TEXT("typed-source.json");
	Result.Source.IrSha256 = FString::ChrN(64, TEXT('1'));
	Result.Source.SlicePath = TEXT("slice.json");
	Result.Source.SliceSha256 = FString::ChrN(64, TEXT('2'));
	Result.Query.Question = TEXT("Why does the selected Gate execute?");
	Result.Query.Direction = TEXT("backward_only");

	auto AddUnit = [&Result](
		const TCHAR* Id,
		const TCHAR* NativeId,
		const FString& Title,
		const EBlueprintLensRole Role,
		const EBlueprintLensSemanticStatus Status)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = Id;
		Unit.Role = Role;
		Unit.Kind = EBlueprintLensUnitKind::Node;
		Unit.Title = Title;
		Unit.SemanticStatus = Status;
		Unit.InclusionReasons = {
			Role == EBlueprintLensRole::Criterion
				? TEXT("criterion")
				: Role == EBlueprintLensRole::Boundary
					? TEXT("opaque_boundary")
					: TEXT("execution_predecessor")};
		FBlueprintLensSourceReference Source;
		Source.BlueprintAssetPath = Result.Source.BlueprintAssetPath;
		Source.GraphId = Result.Source.GraphId;
		Source.SourceNodeId = FString::Printf(
			TEXT("%s::node::%s"), *Result.Source.GraphId, NativeId);
		Source.NativeNodeGuid = NativeId;
		Source.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Source));
		Result.Units.Add(MoveTemp(Unit));
	};
	AddUnit(
		TEXT("unit.boundary.predicate"), TEXT("predicate"),
		TEXT("Not Equal (Object)"), EBlueprintLensRole::Boundary,
		EBlueprintLensSemanticStatus::Opaque);
	AddUnit(
		TEXT("unit.boundary.value"), TEXT("value"), TEXT("GetGameMode"),
		EBlueprintLensRole::Boundary,
		EBlueprintLensSemanticStatus::Opaque);
	AddUnit(
		TEXT("unit.boundary.switch"), TEXT("switch"),
		TEXT("Switch Has Authority"), EBlueprintLensRole::Boundary,
		EBlueprintLensSemanticStatus::Opaque);
	AddUnit(
		TEXT("unit.control.reroute-a"), TEXT("reroute-a"),
		TEXT("Reroute Node"), EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.control.branch"), TEXT("branch"), TEXT("Branch"),
		EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.control.set"), TEXT("set"), TEXT("Set StoredGameMode"),
		EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.control.begin"), TEXT("begin"), TEXT("Event BeginPlay"),
		EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.control.reroute-b"), TEXT("reroute-b"),
		TEXT("Reroute Node"), EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.control.tick"), TEXT("tick"), TEXT("Event Tick"),
		EBlueprintLensRole::Control,
		EBlueprintLensSemanticStatus::Supported);
	AddUnit(
		TEXT("unit.criterion.gate"), TEXT("gate"), GateTitle,
		EBlueprintLensRole::Criterion,
		EBlueprintLensSemanticStatus::Opaque);
	Result.CriterionUnitId = TEXT("unit.criterion.gate");
	Result.Query.CriterionSourceNodeId =
		Result.Units.Last().SourceReferences[0].SourceNodeId;

	auto AddRelation = [&Result](
		const TCHAR* Id,
		const TCHAR* SourceUnitId,
		const TCHAR* TargetUnitId,
		const EBlueprintLensRelationKind Kind,
		const TCHAR* PortLabel)
	{
		const FBlueprintLensUnit* SourceUnit = Result.FindUnit(SourceUnitId);
		const FBlueprintLensUnit* TargetUnit = Result.FindUnit(TargetUnitId);
		FBlueprintLensRelation Relation;
		Relation.Id = Id;
		Relation.SourceUnitId = SourceUnitId;
		Relation.TargetUnitId = TargetUnitId;
		Relation.Kind = Kind;
		Relation.Label = PortLabel;
		Relation.SourceEdgeIds = {FString::Printf(TEXT("edge.%s"), Id)};
		Relation.bHasSourceEdgeEndpoints = true;
		FBlueprintLensSourceEdgeEndpoint Endpoint;
		Endpoint.SourceEdgeId = Relation.SourceEdgeIds[0];
		Endpoint.SourceNodeId = SourceUnit != nullptr
			? SourceUnit->SourceReferences[0].SourceNodeId : FString();
		Endpoint.SourcePinId = Endpoint.SourceNodeId + TEXT("::pin::out");
		Endpoint.SourcePortLabel = PortLabel;
		Endpoint.TargetNodeId = TargetUnit != nullptr
			? TargetUnit->SourceReferences[0].SourceNodeId : FString();
		Endpoint.TargetPinId = Endpoint.TargetNodeId + TEXT("::pin::in");
		Endpoint.TargetPortLabel = TEXT("execute");
		Relation.SourceEdgeEndpoints.Add(MoveTemp(Endpoint));
		Relation.bHasPortLabel = true;
		Relation.PortLabel = PortLabel;
		Relation.bHasSemanticLabel = true;
		Relation.SemanticLabel =
			Kind == EBlueprintLensRelationKind::PredicateFor
				? EBlueprintLensSemanticLabel::BranchCondition
				: Kind == EBlueprintLensRelationKind::ProvidesValue
					? EBlueprintLensSemanticLabel::ValueInput
					: EBlueprintLensSemanticLabel::NextExecution;
		Result.Relations.Add(MoveTemp(Relation));
	};
	// The exact live EngineSample shape: six execution/control members form the
	// SCC, two events enter it, no execution/control relation exits it, one
	// predicate reaches a member, and one value relation remains outside it.
	AddRelation(
		TEXT("r.control.branch-set"), TEXT("unit.control.branch"),
		TEXT("unit.control.set"), EBlueprintLensRelationKind::ControlsExecution,
		TEXT("then"));
	AddRelation(
		TEXT("r.exec.reroute-a-reroute-b"), TEXT("unit.control.reroute-a"),
		TEXT("unit.control.reroute-b"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("OutputPin"));
	AddRelation(
		TEXT("r.exec.reroute-b-gate"), TEXT("unit.control.reroute-b"),
		TEXT("unit.criterion.gate"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("OutputPin"));
	AddRelation(
		TEXT("r.exec.gate-switch"), TEXT("unit.criterion.gate"),
		TEXT("unit.boundary.switch"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("Exit"));
	AddRelation(
		TEXT("r.exec.tick-gate"), TEXT("unit.control.tick"),
		TEXT("unit.criterion.gate"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("then"));
	AddRelation(
		TEXT("r.exec.begin-gate"), TEXT("unit.control.begin"),
		TEXT("unit.criterion.gate"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("then"));
	AddRelation(
		TEXT("r.exec.set-reroute-a"), TEXT("unit.control.set"),
		TEXT("unit.control.reroute-a"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("then"));
	AddRelation(
		TEXT("r.exec.switch-branch"), TEXT("unit.boundary.switch"),
		TEXT("unit.control.branch"),
		EBlueprintLensRelationKind::ExecutionPredecessor, TEXT("Authority"));
	AddRelation(
		TEXT("r.predicate"), TEXT("unit.boundary.predicate"),
		TEXT("unit.control.branch"), EBlueprintLensRelationKind::PredicateFor,
		TEXT("ReturnValue"));
	AddRelation(
		TEXT("r.value-outside"), TEXT("unit.boundary.value"),
		TEXT("unit.boundary.predicate"), EBlueprintLensRelationKind::ProvidesValue,
		TEXT("ReturnValue"));
	Result.Counts.Units = Result.Units.Num();
	Result.Counts.Relations = Result.Relations.Num();
	Result.Counts.SourceNodes = Result.Units.Num();
	Result.Counts.SourceEdges = Result.Relations.Num();
	return Result;
}

FM6LoadedSessionPacket MakeRealActivationPacket(
	UEdGraph& Graph,
	const TCHAR HashDigit)
{
	FM6LoadedSessionPacket Result;
	Result.Request.QueryKind = TEXT("execution");
	Result.Request.RendererId = TEXT("R1_GENERIC_FRAME_FLOW_V1");
	Result.Request.GraphId = Graph.GetPathName();
	Result.Request.SourceFingerprint = FString::ChrN(64, TEXT('a'));
	Result.SemanticSha256 = FString::ChrN(64, HashDigit);
	Result.BaselineFacts.Format = TEXT("blueprint-lens-m6-baseline-facts");
	Result.BaselineFacts.SchemaVersion = TEXT("1.0.0");
	Result.BaselineFacts.GraphId = Graph.GetPathName();
	Result.BaselineFacts.RendererId = Result.Request.RendererId;
	Result.Explanation.Format = TEXT("blueprint-lens-explanation");
	Result.Explanation.SchemaVersion = TEXT("1.0.0");
	Result.Explanation.RulesVersion = TEXT("1.0.0");
	Result.Explanation.Query.Question = TEXT("Why does the criterion execute?");
	Result.Explanation.Query.Direction = TEXT("backward_only");

	TArray<UEdGraphNode*> Nodes;
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node != nullptr) Nodes.Add(Node);
		if (Nodes.Num() == 2) break;
	}
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		UEdGraphNode* Node = Nodes[Index];
		const FString EntityId = FString::Printf(
			TEXT("%s::node::%s"),
			*Graph.GetPathName(),
			*Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		FM6BaselineEntity Entity;
		Entity.Id = EntityId;
		Entity.Label = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		Entity.Role = Index == Nodes.Num() - 1 ? TEXT("criterion") : TEXT("control");
		Entity.SemanticStatus = TEXT("supported");
		Entity.PresentationStatus = TEXT("supported");
		Entity.InclusionReasons = {
			Index == Nodes.Num() - 1 ? TEXT("criterion") : TEXT("execution_predecessor")};
		Entity.Source.AssetPath = Graph.GetOuter()->GetPathName();
		Entity.Source.GraphId = Graph.GetPathName();
		Entity.Source.NodeId = EntityId;
		Entity.Source.NativeNodeGuid =
			Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
		Result.BaselineFacts.Entities.Add(Entity);
		Result.BaselineFacts.EntityIds.Add(EntityId);
		Result.BaselineFacts.EntitySourceNodeIds.Add(EntityId, EntityId);
		Result.TypedDocument.NodeIds.Add(EntityId);

		FBlueprintLensUnit Unit;
		Unit.Id = EntityId;
		Unit.Title = Entity.Label;
		Unit.Role = Index == Nodes.Num() - 1
			? EBlueprintLensRole::Criterion
			: EBlueprintLensRole::Control;
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons = Entity.InclusionReasons;
		FBlueprintLensSourceReference Source;
		Source.BlueprintAssetPath = Entity.Source.AssetPath;
		Source.GraphId = Entity.Source.GraphId;
		Source.SourceNodeId = EntityId;
		Source.NativeNodeGuid = Entity.Source.NativeNodeGuid;
		Source.bPrimary = true;
		Unit.SourceReferences.Add(MoveTemp(Source));
		Result.Explanation.Units.Add(MoveTemp(Unit));
	}
	if (!Result.BaselineFacts.Entities.IsEmpty())
	{
		Result.BaselineFacts.CriterionEntityId =
			Result.BaselineFacts.Entities.Last().Id;
		Result.Explanation.CriterionUnitId =
			Result.BaselineFacts.CriterionEntityId;
		Result.Explanation.Query.CriterionSourceNodeId =
			Result.BaselineFacts.CriterionEntityId;
	}
	if (Result.BaselineFacts.Entities.Num() == 2)
	{
		FM6BaselineRelation Relation;
		Relation.Id = TEXT("real.activation.relation");
		Relation.SourceEntityId = Result.BaselineFacts.Entities[0].Id;
		Relation.TargetEntityId = Result.BaselineFacts.Entities[1].Id;
		Relation.SourceEdgeId = Relation.Id;
		Relation.Label = TEXT("then");
		Relation.Kind = TEXT("execution_predecessor");
		Relation.SemanticLabel = TEXT("next_execution");
		Relation.SemanticStatus = TEXT("supported");
		Result.BaselineFacts.Relations.Add(Relation);
		Result.BaselineFacts.RelationIds.Add(Relation.Id);
		Result.TypedDocument.EdgeIds.Add(Relation.Id);

		FBlueprintLensRelation ExplanationRelation;
		ExplanationRelation.Id = Relation.Id;
		ExplanationRelation.SourceUnitId = Relation.SourceEntityId;
		ExplanationRelation.TargetUnitId = Relation.TargetEntityId;
		ExplanationRelation.Kind =
			EBlueprintLensRelationKind::ExecutionPredecessor;
		ExplanationRelation.Label = Relation.Label;
		ExplanationRelation.SourceEdgeIds.Add(Relation.Id);
		Result.Explanation.Relations.Add(MoveTemp(ExplanationRelation));
	}
	return Result;
}

TSharedPtr<SWidget> FindSlateButton(
	const TSharedRef<SWidget>& Root,
	const FString& Label)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() != TEXT("SButton")) continue;
		if (!SlateWidgetText(Widget).Contains(Label)) continue;
		return Widget;
	}
	return nullptr;
}

TSharedPtr<SWidget> FindSlateWidgetByType(
	const TSharedRef<SWidget>& Root,
	const FString& Type)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == Type) return Widget;
	}
	return nullptr;
}

int32 SlateWidgetCountByType(
	const TSharedRef<SWidget>& Root,
	const FString& Type)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	int32 Count = 0;
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == Type) ++Count;
	}
	return Count;
}

bool InvokeSlateButton(
	const TSharedRef<SWidget>& Root,
	const FString& Label)
{
	const TSharedPtr<SWidget> Found = FindSlateButton(Root, Label);
	if (Found.IsValid())
	{
		const TSharedRef<SButton> Button =
			StaticCastSharedRef<SButton>(Found.ToSharedRef());
	#if !UE_BUILD_SHIPPING
		Button->SimulateClick();
		return true;
	#else
		const FKeyEvent AcceptEvent(
			FKey(TEXT("Enter")), FModifierKeysState(), 0, false, 0, 0);
		Button->OnKeyDown(FGeometry(), AcceptEvent);
		return Button->OnKeyUp(FGeometry(), AcceptEvent).IsEventHandled();
	#endif
	}
	return false;
}

int32 SlateButtonTraversalIndex(
	const TSharedRef<SWidget>& Root,
	const FString& Label)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
	{
		const TSharedRef<SWidget>& Widget = Widgets[Index];
		if (Widget->GetTypeAsString() == TEXT("SButton") &&
			SlateWidgetText(Widget).Contains(Label))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 SlateWidgetTraversalIndexByType(
	const TSharedRef<SWidget>& Root,
	const FString& Type)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
	{
		if (Widgets[Index]->GetTypeAsString() == Type) return Index;
	}
	return INDEX_NONE;
}

bool SlateButtonHasAncestorType(
	const TSharedRef<SWidget>& Root,
	const FString& Label,
	const FString& AncestorType,
	const bool bAncestorMatched = false)
{
	const bool bMatchedHere = bAncestorMatched ||
		Root->GetTypeAsString() == AncestorType;
	if (Root->GetTypeAsString() == TEXT("SButton") &&
		SlateWidgetText(Root).Contains(Label))
	{
		return bMatchedHere;
	}
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (SlateButtonHasAncestorType(
				Children->GetChildAt(Index), Label, AncestorType, bMatchedHere))
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<SBlueprintLensPanel> OpenRealM6Panel(
	FAutomationTestBase& Test,
	const TCHAR* AssetPath,
	UBlueprint*& OutBlueprint,
	TSharedPtr<FBlueprintEditor>& OutEditor)
{
	OutBlueprint = LoadObject<UBlueprint>(nullptr, AssetPath);
	Test.TestNotNull(TEXT("real M6 Blueprint loads"), OutBlueprint);
	if (OutBlueprint == nullptr || GEditor == nullptr) return nullptr;

	UAssetEditorSubsystem* AssetEditors =
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Test.TestNotNull(TEXT("asset editor subsystem is available"), AssetEditors);
	if (AssetEditors == nullptr) return nullptr;
	Test.TestTrue(
		TEXT("real Blueprint editor opens without a command-line M6 session"),
		AssetEditors->OpenEditorForAsset(
			OutBlueprint,
			EToolkitMode::Standalone,
			TSharedPtr<IToolkitHost>(),
			false));

	FBlueprintEditorModule& BlueprintEditorModule =
		FModuleManager::LoadModuleChecked<FBlueprintEditorModule>(TEXT("Kismet"));
	for (const TSharedRef<IBlueprintEditor>& InterfaceEditor :
		BlueprintEditorModule.GetBlueprintEditors())
	{
		const TSharedPtr<FBlueprintEditor> Candidate =
			StaticCastSharedRef<FBlueprintEditor>(InterfaceEditor);
		if (Candidate.IsValid() && Candidate->GetBlueprintObj() == OutBlueprint)
		{
			OutEditor = Candidate;
			break;
		}
	}
	Test.TestTrue(TEXT("real FBlueprintEditor is discoverable"), OutEditor.IsValid());
	if (!OutEditor.IsValid()) return nullptr;

	UEdGraph* EventGraph = nullptr;
	TArray<UEdGraph*> Graphs;
	OutBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Candidate : Graphs)
	{
		if (Candidate != nullptr && Candidate->GetFName() == TEXT("EventGraph"))
		{
			EventGraph = Candidate;
			break;
		}
	}
	Test.TestNotNull(TEXT("real EventGraph exists"), EventGraph);
	if (EventGraph == nullptr) return nullptr;
	Test.TestTrue(
		TEXT("real EventGraph opens before the Lens tab is activated"),
		OutEditor->OpenGraphAndBringToFront(EventGraph, true).IsValid());
	OutEditor->InvokeTab(FBlueprintLensEditorModule::SemanticLaneTabId);

	const TSharedPtr<FTabManager> TabManager = OutEditor->GetAssociatedTabManager();
	Test.TestTrue(TEXT("real Blueprint editor has a tab manager"), TabManager.IsValid());
	if (!TabManager.IsValid()) return nullptr;
	const TSharedPtr<SDockTab> LensTab = TabManager->FindExistingLiveTab(
		FTabId(FBlueprintLensEditorModule::SemanticLaneTabId, ETabIdFlags::SaveLayout));
	Test.TestTrue(TEXT("real Blueprint Lens tab is rendered"), LensTab.IsValid());
	if (!LensTab.IsValid()) return nullptr;

	const TSharedRef<SWidget> TabContent = LensTab->GetContent();
	Test.TestEqual(
		TEXT("real Lens tab content is the M6 panel"),
		TabContent->GetTypeAsString(),
		FString(TEXT("SBlueprintLensPanel")));
	if (TabContent->GetTypeAsString() != TEXT("SBlueprintLensPanel")) return nullptr;
	return StaticCastSharedRef<SBlueprintLensPanel>(TabContent);
}

void CloseRealM6Editor(UBlueprint* Blueprint)
{
	if (GEditor != nullptr && Blueprint != nullptr)
	{
		if (UAssetEditorSubsystem* AssetEditors =
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditors->CloseAllEditorsForAsset(Blueprint);
		}
	}
}
} // namespace

namespace BlueprintLensM6ControllerTests
{
namespace
{

FM6Error TestError(const TCHAR* Code)
{
	FM6Error Result;
	Result.Code = Code;
	Result.Phase = TEXT("test");
	Result.Message = Code;
	return Result;
}

FM6LoadedSessionPacket Packet(const TCHAR* Kind, const TCHAR* HashDigit)
{
	FM6LoadedSessionPacket Result;
	Result.Request.QueryKind = Kind;
	Result.Request.SourceFingerprint = FString::ChrN(64, TEXT('a'));
	Result.SemanticSha256 = FString::ChrN(64, HashDigit[0]);
	return Result;
}

class FFakePreflight final : public IM6PreflightProvider
{
public:
	FM6PreflightResult Next;
	int32 Calls = 0;

	virtual FM6PreflightResult Evaluate(const FM6QueryInput&) override
	{
		++Calls;
		return Next;
	}
};

class FFakeExporter final : public IM6ExportProvider
{
public:
	FM6ExportPreparation Next;
	int32 Calls = 0;

	virtual FM6ExportPreparation Prepare(const FM6PreflightResult&) override
	{
		++Calls;
		return Next;
	}
};

class FFakeRunner final : public IM6ProcessRunner
{
public:
	TArray<FM6ProcessComplete> Callbacks;
	TSet<int32> ActiveCallbacks;
	int32 Starts = 0;
	int32 Cancels = 0;
	int32 Ticks = 0;

	virtual void Start(
		const FM6ProcessInvocation&,
		FM6ProcessComplete OnComplete) override
	{
		++Starts;
		Callbacks.Add(MoveTemp(OnComplete));
		ActiveCallbacks.Add(Callbacks.Num() - 1);
	}

	virtual void Cancel() override { ++Cancels; }
	virtual void Tick(double) override { ++Ticks; }
	virtual bool IsActive() const override { return !ActiveCallbacks.IsEmpty(); }

	void Complete(const FM6ProcessResult& Result, const int32 Index = INDEX_NONE)
	{
		const int32 ActualIndex = Index == INDEX_NONE ? Callbacks.Num() - 1 : Index;
		ActiveCallbacks.Remove(ActualIndex);
		Callbacks[ActualIndex](Result);
	}
};

class FFakeLoader final : public IM6PacketProvider
{
public:
	TArray<FM6SessionPacketLoadResult> Results;
	int32 Calls = 0;

	virtual FM6SessionPacketLoadResult Load(
		const FString&,
		const FString&) override
	{
		const int32 Index = FMath::Min(Calls++, Results.Num() - 1);
		if (Index < 0) return MakeError(TestError(TEXT("M6_PACKET_SCHEMA_INVALID")));
		return MoveTemp(Results[Index]);
	}
};

class FFakeTelemetry final : public IM6SessionTelemetry
{
public:
	TArray<FString> Events;
	int32 Seals = 0;

	virtual void RecordState(EM6SessionState State, const FM6Error& InError) override
	{
		Events.Add(FString::Printf(TEXT("state:%d:%s"), static_cast<int32>(State), *InError.Code));
	}
	virtual void RecordBaseline(EM6Baseline Baseline) override
	{
		Events.Add(FString::Printf(TEXT("baseline:%d"), static_cast<int32>(Baseline)));
	}
	virtual void RecordSelection(const FString& EntityId, EM6SelectionOrigin) override
	{
		Events.Add(TEXT("select:") + EntityId);
	}
	virtual void RecordReset() override { Events.Add(TEXT("reset")); }
	virtual void Seal() override { ++Seals; }
};

class FFakeView final : public IM6SessionView
{
public:
	TArray<FM6SessionSnapshot> Snapshots;
	TFunction<void(const FM6SessionSnapshot&)> Reenter;

	virtual void Present(const FM6SessionSnapshot& Snapshot) override
	{
		Snapshots.Add(Snapshot);
		if (Reenter) Reenter(Snapshot);
	}
};

struct FControllerHarness
{
	FFakePreflight Preflight;
	FFakeExporter Exporter;
	FFakeRunner Runner;
	FFakeLoader Loader;
	FFakeTelemetry Telemetry;
	FFakeView View;
	TUniquePtr<FM6SessionController> Controller;

	FControllerHarness()
	{
		Preflight.Next.bSucceeded = true;
		Preflight.Next.Request.SourceFingerprint = FString::ChrN(64, TEXT('a'));
		Preflight.Next.OwnedStagingDirectory = TEXT("owned-staging");
		Exporter.Next.bSucceeded = true;
		Exporter.Next.PacketDirectory = TEXT("packet");
		Exporter.Next.ExpectedSourceFingerprint = FString::ChrN(64, TEXT('a'));
		Exporter.Next.Invocation.ExecutablePath = TEXT("python.exe");
		Exporter.Next.Invocation.Arguments = {TEXT("run_m6_session.py")};
		Controller = MakeUnique<FM6SessionController>(
			Preflight, Exporter, Runner, Loader, Telemetry, View);
	}

	void ReachRunning(const EM6QueryKind Kind = EM6QueryKind::Execution)
	{
		FM6QueryInput Query;
		Query.Kind = Kind;
		Controller->Run(Query);
		Controller->Tick(1.0);
		Controller->Tick(2.0);
	}

	void CompleteReady(const TCHAR* Kind, const TCHAR* HashDigit)
	{
		CompleteReadyPacket(Packet(Kind, HashDigit));
	}

	void CompleteReadyPacket(FM6LoadedSessionPacket PacketValue)
	{
		Loader.Results.Add(MakeValue(MoveTemp(PacketValue)));
		FM6ProcessResult Process;
		Process.bStarted = true;
		Process.bCleanupSucceeded = true;
		Process.ReturnCode = 0;
		Runner.Complete(Process);
		Controller->Tick(3.0);
		Controller->Tick(4.0);
	}
};

void TestState(
	FAutomationTestBase& Test,
	const TCHAR* Label,
	const FControllerHarness& Harness,
	const EM6SessionState Expected)
{
	Test.TestEqual(Label, Harness.Controller->GetSnapshot().State, Expected);
}

void AddRequiredStages(FM6TelemetryRecorder& Recorder)
{
	for (const TCHAR* Stage : FM6TelemetryRecorder::RequiredStages())
	{
		FM6TelemetryCounts Counts;
		FM6Error StageError;
		Recorder.RecordStage(Stage, true, Counts, FString(), StageError);
	}
}
} // namespace
} // namespace BlueprintLensM6ControllerTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6ControllerTest,
	"BlueprintLens.M6.Controller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6ControllerTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6ControllerTests;
	FControllerHarness Harness;
	TestState(*this, TEXT("Initial state is Idle"), Harness, EM6SessionState::Idle);
	Harness.ReachRunning();
	TestState(*this, TEXT("Run reaches Running"), Harness, EM6SessionState::Running);
	TestEqual(TEXT("Preflight called once"), Harness.Preflight.Calls, 1);
	TestEqual(TEXT("Exporter called once"), Harness.Exporter.Calls, 1);
	TestEqual(TEXT("Runner started once"), Harness.Runner.Starts, 1);

	Harness.Controller->Run(FM6QueryInput());
	TestEqual(TEXT("Single-flight rejects second start"), Harness.Runner.Starts, 1);
	TestState(*this, TEXT("Single-flight preserves Running"), Harness, EM6SessionState::Running);
	Harness.CompleteReady(TEXT("execution"), TEXT("b"));
	TestState(*this, TEXT("Valid packet becomes Ready"), Harness, EM6SessionState::Ready);
	TestTrue(TEXT("Ready owns packet"), Harness.Controller->GetSnapshot().bHasReadySession);
	TestEqual(TEXT("Ready packet kind"), Harness.Controller->GetReadyPacket()->Request.QueryKind, FString(TEXT("execution")));

	Harness.ReachRunning(EM6QueryKind::Data);
	TestTrue(TEXT("Old Ready session remains visible while pending"), Harness.Controller->GetSnapshot().bHasReadySession);
	TestTrue(TEXT("Old Ready session is stale-labelled"), Harness.Controller->GetSnapshot().bReadySessionStale);
	FM6ProcessResult NonZero;
	NonZero.bStarted = true;
	NonZero.bCleanupSucceeded = true;
	NonZero.ReturnCode = 2;
	Harness.Runner.Complete(NonZero);
	Harness.Controller->Tick(5.0);
	TestState(*this, TEXT("Non-zero result fails"), Harness, EM6SessionState::Failed);
	TestEqual(TEXT("Non-zero stable code"), Harness.Controller->GetSnapshot().Error.Code, FString(TEXT("M6_RUNNER_NONZERO_EXIT")));
	TestEqual(TEXT("Failure preserves old packet"), Harness.Controller->GetReadyPacket()->Request.QueryKind, FString(TEXT("execution")));
	Harness.Controller->Reset();
	TestState(*this, TEXT("Reset from Failed is Idle"), Harness, EM6SessionState::Idle);

	for (const TCHAR* Code : {TEXT("M6_PACKET_SCHEMA_INVALID"), TEXT("M6_PACKET_HASH_MISMATCH"), TEXT("M6_PACKET_SOURCE_STALE")})
	{
		Harness.ReachRunning();
		Harness.Loader.Results.Add(MakeError(TestError(Code)));
		FM6ProcessResult Success;
		Success.bStarted = true;
		Success.bCleanupSucceeded = true;
		Success.ReturnCode = 0;
		Harness.Runner.Complete(Success);
		Harness.Controller->Tick(6.0);
		Harness.Controller->Tick(7.0);
		TestState(*this, TEXT("Loader failure enters Failed"), Harness, EM6SessionState::Failed);
		TestEqual(TEXT("Loader error remains stable"), Harness.Controller->GetSnapshot().Error.Code, FString(Code));
		Harness.Controller->Reset();
	}

	Harness.ReachRunning();
	Harness.Controller->Cancel();
	TestState(*this, TEXT("Cancel enters Cancelling"), Harness, EM6SessionState::Cancelling);
	TestEqual(TEXT("Cancel targets runner once"), Harness.Runner.Cancels, 1);
	FM6ProcessResult Cancelled;
	Cancelled.bStarted = true;
	Cancelled.bCancelled = true;
	Cancelled.bCleanupSucceeded = true;
	Cancelled.Error = TestError(TEXT("M6_RUNNER_CANCELLED"));
	Harness.Runner.Complete(Cancelled);
	Harness.Controller->Tick(8.0);
	TestState(*this, TEXT("Cancel without old session returns Idle"), Harness, EM6SessionState::Idle);

	Harness.ReachRunning();
	Harness.Controller->Reset();
	TestState(*this, TEXT("Reset during Running enters Cancelling"), Harness, EM6SessionState::Cancelling);
	Harness.Runner.Complete(Cancelled);
	Harness.Controller->Tick(9.0);
	TestState(*this, TEXT("Reset completes after cleanup"), Harness, EM6SessionState::Idle);
	TestTrue(TEXT("Reset seals telemetry"), Harness.Telemetry.Seals > 0);

	Harness.ReachRunning();
	FM6ProcessResult CleanupFailure = Cancelled;
	CleanupFailure.bCleanupSucceeded = false;
	CleanupFailure.Error = TestError(TEXT("M6_RUNNER_CLEANUP_FAILED"));
	Harness.Controller->Cancel();
	Harness.Runner.Complete(CleanupFailure);
	Harness.Controller->Tick(10.0);
	TestState(*this, TEXT("Cleanup failure is Failed"), Harness, EM6SessionState::Failed);
	TestEqual(TEXT("Cleanup stable code"), Harness.Controller->GetSnapshot().Error.Code, FString(TEXT("M6_RUNNER_CLEANUP_FAILED")));
	Harness.Controller->Reset();

	Harness.ReachRunning();
	const int32 OldCallback = Harness.Runner.Callbacks.Num() - 1;
	Harness.Controller->Reset();
	Harness.Runner.Complete(Cancelled, OldCallback);
	Harness.Controller->Tick(11.0);
	Harness.ReachRunning(EM6QueryKind::Data);
	Harness.Runner.Complete(Cancelled, OldCallback);
	TestState(*this, TEXT("Late callback cannot replace new generation"), Harness, EM6SessionState::Running);
	Harness.CompleteReady(TEXT("data"), TEXT("c"));
	TestEqual(TEXT("Data after execution is data"), Harness.Controller->GetReadyPacket()->Request.QueryKind, FString(TEXT("data")));

	const int32 BeforeSelection = Harness.Telemetry.Events.Num();
	Harness.View.Reenter = [&Harness](const FM6SessionSnapshot& Snapshot)
	{
		if (!Snapshot.SelectedEntityId.IsEmpty())
			Harness.Controller->SelectEntity(Snapshot.SelectedEntityId, EM6SelectionOrigin::NativeGraph);
	};
	Harness.Controller->SelectEntity(TEXT("entity.one"), EM6SelectionOrigin::BaselineView);
	TestEqual(TEXT("Selection re-entry emits one telemetry event"), Harness.Telemetry.Events.Num(), BeforeSelection + 1);
	Harness.Controller->SelectBaseline(EM6Baseline::C);
	TestEqual(TEXT("Baseline switches without rerun"), Harness.Runner.Starts, 10);
	TestEqual(TEXT("Baseline state changes"), Harness.Controller->GetSnapshot().Baseline, EM6Baseline::C);

	TestEqual(
		TEXT("Argument quoting preserves exact values"),
		FM6ProcessRunner::BuildCommandLineForAutomationTest({TEXT("plain"), TEXT("space value"), TEXT("quote\"value")}),
		FString(TEXT("plain \"space value\" \"quote\\\"value\"")));

	{
		FControllerHarness Sequence;
		FM6QueryInput Query;
		Sequence.Controller->Run(Query);
		TestState(*this, TEXT("Run enters Preflight"), Sequence, EM6SessionState::Preflight);
		Sequence.Controller->Tick(20.0);
		TestState(*this, TEXT("Successful preflight enters Exporting"), Sequence, EM6SessionState::Exporting);
		Sequence.Controller->Tick(21.0);
		TestState(*this, TEXT("Successful export enters Running"), Sequence, EM6SessionState::Running);
		FM6ProcessResult Success;
		Success.bStarted = true;
		Success.bCleanupSucceeded = true;
		Success.ReturnCode = 0;
		Sequence.Loader.Results.Add(MakeValue(Packet(TEXT("execution"), TEXT("d"))));
		Sequence.Runner.Complete(Success);
		Sequence.Controller->Tick(22.0);
		TestState(*this, TEXT("Successful process enters Validating"), Sequence, EM6SessionState::Validating);
		Sequence.Controller->Tick(23.0);
		TestState(*this, TEXT("Atomic validation enters Ready"), Sequence, EM6SessionState::Ready);
		Sequence.Controller->Cancel();
		TestState(*this, TEXT("Cancel is ignored from Ready"), Sequence, EM6SessionState::Ready);
	}

	{
		FControllerHarness Failure;
		Failure.Preflight.Next.bSucceeded = false;
		Failure.Preflight.Next.Error = TestError(TEXT("M6_PRECONDITION_DIRTY_SOURCE"));
		Failure.Controller->Run(FM6QueryInput());
		Failure.Controller->Tick(30.0);
		TestState(*this, TEXT("Preflight failure is Failed"), Failure, EM6SessionState::Failed);
		TestEqual(TEXT("Preflight error is stable"), Failure.Controller->GetSnapshot().Error.Code, FString(TEXT("M6_PRECONDITION_DIRTY_SOURCE")));
		Failure.Preflight.Next.bSucceeded = true;
		Failure.Controller->Run(FM6QueryInput());
		TestState(*this, TEXT("Failed state supports retry"), Failure, EM6SessionState::Preflight);
	}

	{
		FControllerHarness Failure;
		Failure.Exporter.Next.bSucceeded = false;
		Failure.Exporter.Next.Error = TestError(TEXT("M6_EXPORT_FAILED"));
		Failure.Controller->Run(FM6QueryInput());
		Failure.Controller->Tick(31.0);
		Failure.Controller->Tick(32.0);
		TestState(*this, TEXT("Export failure is Failed"), Failure, EM6SessionState::Failed);
		TestEqual(TEXT("Export error is stable"), Failure.Controller->GetSnapshot().Error.Code, FString(TEXT("M6_EXPORT_FAILED")));
	}

	{
		FControllerHarness Timeout;
		Timeout.ReachRunning();
		FM6ProcessResult Result;
		Result.bStarted = true;
		Result.bTimedOut = true;
		Result.bCleanupSucceeded = true;
		Timeout.Runner.Complete(Result);
		Timeout.Controller->Tick(33.0);
		TestState(*this, TEXT("Timeout is Failed"), Timeout, EM6SessionState::Failed);
		TestEqual(TEXT("Timeout error is stable"), Timeout.Controller->GetSnapshot().Error.Code, FString(TEXT("M6_RUNNER_TIMEOUT")));
	}

	{
		FControllerHarness ResetIdle;
		ResetIdle.Controller->Reset();
		TestState(*this, TEXT("Reset during Idle remains Idle"), ResetIdle, EM6SessionState::Idle);
		TestEqual(TEXT("Idle reset seals once"), ResetIdle.Telemetry.Seals, 1);
	}
	{
		FControllerHarness ResetPreflight;
		ResetPreflight.Controller->Run(FM6QueryInput());
		ResetPreflight.Controller->Reset();
		TestState(*this, TEXT("Reset during Preflight cancels"), ResetPreflight, EM6SessionState::Cancelling);
		ResetPreflight.Controller->Tick(40.0);
		TestState(*this, TEXT("Preflight reset returns Idle"), ResetPreflight, EM6SessionState::Idle);
	}
	{
		FControllerHarness ResetExporting;
		ResetExporting.Controller->Run(FM6QueryInput());
		ResetExporting.Controller->Tick(41.0);
		ResetExporting.Controller->Reset();
		TestState(*this, TEXT("Reset during Exporting cancels"), ResetExporting, EM6SessionState::Cancelling);
		ResetExporting.Controller->Tick(42.0);
		TestState(*this, TEXT("Export reset returns Idle"), ResetExporting, EM6SessionState::Idle);
	}
	{
		FControllerHarness ResetValidating;
		ResetValidating.ReachRunning();
		FM6ProcessResult Success;
		Success.bStarted = true;
		Success.bCleanupSucceeded = true;
		Success.ReturnCode = 0;
		ResetValidating.Runner.Complete(Success);
		ResetValidating.Controller->Tick(43.0);
		TestState(*this, TEXT("Fixture reaches Validating"), ResetValidating, EM6SessionState::Validating);
		ResetValidating.Controller->Reset();
		ResetValidating.Controller->Tick(44.0);
		TestState(*this, TEXT("Validation reset returns Idle"), ResetValidating, EM6SessionState::Idle);
		TestEqual(TEXT("Validation reset never loads"), ResetValidating.Loader.Calls, 0);
	}
	{
		FControllerHarness ResetReady;
		ResetReady.ReachRunning();
		ResetReady.CompleteReady(TEXT("execution"), TEXT("e"));
		ResetReady.Controller->Reset();
		TestState(*this, TEXT("Reset during Ready returns Idle"), ResetReady, EM6SessionState::Idle);
		TestFalse(TEXT("Ready reset clears packet"), ResetReady.Controller->GetSnapshot().bHasReadySession);
	}
	{
		FControllerHarness ResetCancelling;
		ResetCancelling.ReachRunning();
		ResetCancelling.Controller->Cancel();
		ResetCancelling.Controller->Reset();
		FM6ProcessResult CancelledResult;
		CancelledResult.bStarted = true;
		CancelledResult.bCancelled = true;
		CancelledResult.bCleanupSucceeded = true;
		ResetCancelling.Runner.Complete(CancelledResult);
		ResetCancelling.Controller->Tick(45.0);
		TestState(*this, TEXT("Reset during Cancelling returns Idle"), ResetCancelling, EM6SessionState::Idle);
	}

	{
		FControllerHarness DataThenExecution;
		DataThenExecution.ReachRunning(EM6QueryKind::Data);
		DataThenExecution.CompleteReady(TEXT("data"), TEXT("f"));
		DataThenExecution.ReachRunning(EM6QueryKind::Execution);
		DataThenExecution.CompleteReady(TEXT("execution"), TEXT("1"));
		TestEqual(TEXT("Data then execution ends in execution"), DataThenExecution.Controller->GetReadyPacket()->Request.QueryKind, FString(TEXT("execution")));

		FControllerHarness ExecutionThenData;
		ExecutionThenData.ReachRunning(EM6QueryKind::Execution);
		ExecutionThenData.CompleteReady(TEXT("execution"), TEXT("2"));
		ExecutionThenData.ReachRunning(EM6QueryKind::Data);
		ExecutionThenData.CompleteReady(TEXT("data"), TEXT("3"));
		TestEqual(TEXT("Execution then data ends in data"), ExecutionThenData.Controller->GetReadyPacket()->Request.QueryKind, FString(TEXT("data")));
		TestEqual(TEXT("Order-independent runs each start twice"), DataThenExecution.Runner.Starts, ExecutionThenData.Runner.Starts);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6ProposalCommitTest,
	"BlueprintLens.M6.PanelProposalCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6ProposalCommitTest::RunTest(const FString&)
{
	FM6QueryInput Submitted;
	FM6PanelActionHandlers Handlers;
	Handlers.Run = [&Submitted](const FM6QueryInput& Query)
	{
		Submitted = Query;
	};
	FM6PanelPresentationModel Panel(MoveTemp(Handlers));
	Panel.SetPythonReady(true);
	Panel.SetQueryKind(EM6QueryKind::Execution);
	Panel.SetGraphId(TEXT("GraphA"));
	Panel.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-a"), TEXT("Target A"));

	TestTrue(TEXT("an answerable graph selection proposes the Execution target"),
		Panel.CanRun());
	Panel.DispatchRun();
	TestEqual(
		TEXT("Run commits the proposed Execution target"),
		Submitted.CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));
	FM6BaselineViewModels Views;
	FM6SessionSnapshot Ready;
	Ready.State = EM6SessionState::Ready;
	Ready.bHasReadySession = true;
	Panel.ApplySession(Ready, &Views);

	Panel.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"));
	TestEqual(
		TEXT("later selection does not reassign the shown result criterion"),
		Panel.ResultQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));
	TestEqual(
		TEXT("later selection remains the proposal for the next Run"),
		Panel.GetQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-b")));
	TestEqual(
		TEXT("ordinary post-result selection is neutral rather than Stale"),
		Panel.Status(), EM6PanelStatus::Ready);
	TestEqual(
		TEXT("post-result proposal remains hidden until Run"),
		Panel.VisibleExecutionTargetLabel(), FString(TEXT("Target A")));
	Panel.DispatchRun();
	TestEqual(
		TEXT("the next Run commits the silent proposal"),
		Submitted.CriterionNodeId, FString(TEXT("GraphA::node::guid-b")));
	TestEqual(
		TEXT("Run is the boundary that reveals the committed proposal"),
		Panel.VisibleExecutionTargetLabel(), FString(TEXT("Target B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6ExecutionAnswerabilityTest,
	"BlueprintLens.M6.PanelExecutionAnswerability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6ExecutionAnswerabilityTest::RunTest(const FString&)
{
	// 23 per cent of the admitted corpus - every VariableGet, pure CallFunction,
	// PromotableOperator, Knot and Select - has no execution pin and is refused
	// by execution_slice.py as M4_CRITERION_INVALID. The panel must refuse it at
	// selection time rather than after a full export-and-run round trip, in the
	// shape the data-side member picker already uses.
	const FString GraphId = TEXT("/Game/Probe/BP.BP:EventGraph");
	const FString NodeId = GraphId + TEXT("::node::a");

	FM6PanelPresentationModel Panel;
	Panel.SetPythonReady(true);
	Panel.SetQueryKind(EM6QueryKind::Execution);

	Panel.ObserveExecutionSelection(GraphId, NodeId, TEXT("Get MyVar"), false);
	TestFalse(TEXT("an unanswerable criterion is not answerable"),
		Panel.IsExecutionTargetAnswerable());
	TestEqual(
		TEXT("the refusal states the fact, as the member picker does"),
		Panel.ExecutionTargetStatusText(),
		FString(TEXT("This node has no execution pin")));
	TestFalse(TEXT("Run stays disabled for an unanswerable criterion"),
		Panel.CanRun());

	// The positive control matters: a gate that refuses everything would pass
	// the assertions above and break the product.
	Panel.ObserveExecutionSelection(GraphId, NodeId, TEXT("Branch"), true);
	TestTrue(TEXT("an answerable criterion reports as answerable"),
		Panel.IsExecutionTargetAnswerable());
	TestEqual(
		TEXT("the answerable status text is its own string"),
		Panel.ExecutionTargetStatusText(),
		FString(TEXT("Has an execution pin")));
	TestTrue(TEXT("Run is enabled for an answerable criterion"), Panel.CanRun());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6MemberPickerTest,
	"BlueprintLens.M6.MemberPicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6MemberPickerTest::RunTest(const FString&)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance"));
	TestNotNull(TEXT("data Blueprint loads"), Blueprint);
	if (Blueprint == nullptr) return false;

	UEdGraph* Graph = nullptr;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Candidate : Graphs)
	{
		if (Candidate != nullptr && Candidate->GetFName() == TEXT("EventGraph"))
		{
			Graph = Candidate;
			break;
		}
	}
	TestNotNull(TEXT("focused data graph exists"), Graph);
	if (Graph == nullptr) return false;

	const TArray<FBPVariableDescription> OriginalVariables = Blueprint->NewVariables;
	FBPVariableDescription UnusedVariable;
	UnusedVariable.VarName = TEXT("M6UnusedMemberForPickerTest");
	UnusedVariable.VarGuid = FGuid::NewGuid();
	UnusedVariable.VarType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	Blueprint->NewVariables.Add(UnusedVariable);

	const TArray<FM6DataMemberRow> Rows =
		SBlueprintLensPanel::EnumerateDataMembersForAutomationTest(Blueprint, Graph);
	TestEqual(TEXT("picker enumerates every Blueprint member"), Rows.Num(), Blueprint->NewVariables.Num());

	const FM6DataMemberRow* Used = nullptr;
	const FM6DataMemberRow* Unused = nullptr;
	for (const FM6DataMemberRow& Row : Rows)
	{
		if (Row.bUsableInFocusedGraph && Used == nullptr) Used = &Row;
		if (!Row.bUsableInFocusedGraph && Unused == nullptr) Unused = &Row;
	}
	TestNotNull(TEXT("picker exposes a usable member"), Used);
	TestNotNull(TEXT("picker keeps an unused member row"), Unused);
	if (Used == nullptr || Unused == nullptr)
	{
		Blueprint->NewVariables = OriginalVariables;
		return false;
	}
	TestEqual(
		TEXT("unused row uses the exact disabled explanation"),
		Unused->StatusText,
		FString(TEXT("This graph does not use this variable")));

	FM6PanelPresentationModel Panel;
	Panel.SetPythonReady(true);
	Panel.SetQueryKind(EM6QueryKind::Data);
	Panel.SetGraphId(Graph->GetPathName());
	Panel.SetDataMemberRows(Rows);
	TestFalse(TEXT("unused member cannot be selected"), Panel.SelectDataMember(Unused->Guid));
	TestTrue(TEXT("usable member selects by GUID"), Panel.SelectDataMember(Used->Guid));
	TestEqual(TEXT("selected member identity is its GUID"), Panel.GetQuery().MemberGuid, Used->Guid);
	TestEqual(TEXT("selected expected name is display consistency"), Panel.GetQuery().ExpectedMemberName, Used->Name);

	Panel.SetDataCriterion(Graph->GetPathName(), FString(), Used->Name);
	TestFalse(TEXT("name-only match cannot run"), Panel.CanRun());
	Blueprint->NewVariables = OriginalVariables;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6PanelEntryTest,
	"BlueprintLens.M6.PanelEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6PanelEntryTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6ControllerTests;
	UBlueprint* RealBlueprint = nullptr;
	TSharedPtr<FBlueprintEditor> RealEditor;
	const TSharedPtr<SBlueprintLensPanel> RealPanel = OpenRealM6Panel(
		*this,
		TEXT("/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"),
		RealBlueprint,
		RealEditor);
	if (RealPanel.IsValid() && RealEditor.IsValid() && RealBlueprint != nullptr)
	{
		UEdGraph* RealEventGraph = nullptr;
		TArray<UEdGraph*> RealGraphs;
		RealBlueprint->GetAllGraphs(RealGraphs);
		for (UEdGraph* Candidate : RealGraphs)
		{
			if (Candidate != nullptr && Candidate->GetFName() == TEXT("EventGraph"))
			{
				RealEventGraph = Candidate;
				break;
			}
		}
		RealPanel->RefreshM6Context();
		const TSharedRef<SWidget> RealPanelRoot = RealPanel.ToSharedRef();
		const FString RealContextText = SlateWidgetText(RealPanelRoot);
		TestTrue(
			TEXT("real panel resolves the focused EventGraph identity"),
			RealEventGraph != nullptr &&
			RealPanel->M6Presentation.GraphPath() == RealEventGraph->GetPathName() &&
			RealPanel->M6Presentation.GraphName() == RealEventGraph->GetName());
		TestTrue(
			TEXT("real panel renders the focused EventGraph name"),
			RealEventGraph != nullptr &&
			RealContextText.Contains(RealEventGraph->GetName()));

		TestEqual(
			TEXT("real panel renders exactly one query-kind dropdown"),
			SlateWidgetCountByType(
				RealPanelRoot, TEXT("SComboBox<TSharedPtr<FString>>")), 1);
		RealPanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
		RealPanel->RefreshM6Content();
		const FString RealDataText = SlateWidgetText(RealPanelRoot);
		TestTrue(
			TEXT("real Data mode renders its workflow disclosure"),
			RealDataText.Contains(
				TEXT("Local variables and cross-asset variable resolution are outside this workflow.")));
		TestFalse(
			TEXT("real Data mode removes the Execution target surface"),
			RealDataText.Contains(TEXT("Execution node ·")));

		// A quiet context poll must not rebuild the panel. When it did, every
		// button was destroyed and recreated about twice a second, references
		// went stale before a dispatch could reach them, and a press spanning a
		// rebuild was lost.
		// Navigating away from the target's graph is a context change, not a
		// deletion. Asserting only that the panel goes Stale would pass with the
		// wrong reason attached, which is how the wrong banner survived.
		RealPanel->M6Presentation.SetExecutionCriterion(
			RealEventGraph != nullptr ? RealEventGraph->GetPathName() : FString(),
			TEXT("never-deleted-node"));
		UEdGraph* ConstructionScript = nullptr;
		TArray<UEdGraph*> AllGraphs;
		RealBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Candidate : AllGraphs)
		{
			if (Candidate != nullptr && Candidate != RealEventGraph)
			{
				ConstructionScript = Candidate;
				break;
			}
		}
		if (ConstructionScript != nullptr)
		{
			RealEditor->OpenGraphAndBringToFront(ConstructionScript, true);
			RealPanel->RefreshM6Context();
			TestFalse(
				TEXT("navigating away does not report the target as deleted"),
				RealPanel->M6Presentation.Banner().Contains(TEXT("deleted")));
			RealEditor->OpenGraphAndBringToFront(RealEventGraph, true);
			RealPanel->RefreshM6Context();
		}

		const TSharedPtr<SWidget> BeforePoll =
			FindSlateButton(RealPanelRoot, TEXT("Reset"));
		TestTrue(TEXT("the Reset button is present before the poll"), BeforePoll.IsValid());
		const uint64 RevisionBefore = RealPanel->M6Presentation.PresentationRevision();
		for (int32 Poll = 0; Poll < 4; ++Poll) RealPanel->RefreshM6Context();
		TestEqual(
			TEXT("a quiet context poll does not change the presentation revision"),
			RealPanel->M6Presentation.PresentationRevision(),
			RevisionBefore);
		TestTrue(
			TEXT("a quiet context poll keeps the same button widget identity"),
			BeforePoll == FindSlateButton(RealPanelRoot, TEXT("Reset")));

		TArray<UEdGraphNode*> RealNodes;
		if (RealEventGraph != nullptr)
		{
			for (UEdGraphNode* Node : RealEventGraph->Nodes)
			{
				if (Node != nullptr) RealNodes.Add(Node);
				if (RealNodes.Num() == 2) break;
			}
		}
		TestTrue(
			TEXT("real activation fixture has two native nodes"),
			RealEventGraph != nullptr && RealNodes.Num() == 2);
		if (RealEventGraph != nullptr && RealNodes.Num() == 2)
		{
			FM6LoadedSessionPacket RealPacketA =
				MakeRealActivationPacket(*RealEventGraph, TEXT('a'));
			const FM6BaselineProjectionResult RealProjectionA =
				BuildM6BaselineViewModels(RealPacketA);
			TestTrue(
				TEXT("real activation packet projects before native application"),
				RealProjectionA.HasValue());

			FM6NativeGraphBridge AtomicBridge(RealEditor);
			const FM6NativeGraphResult InitialMembership =
				AtomicBridge.ApplyMembershipHighlight(RealPacketA.BaselineFacts);
			TestTrue(
				TEXT("production native bridge applies the initial membership"),
				InitialMembership.HasValue());
			TestTrue(
				TEXT("the initial production membership resolves its first native entity"),
				AtomicBridge.FocusSemanticEntity(
					RealPacketA.BaselineFacts.Entities[0].Id).HasValue());
			TestTrue(
				TEXT("the initial production membership resolves its second native entity"),
				AtomicBridge.FocusSemanticEntity(
					RealPacketA.BaselineFacts.Entities[1].Id).HasValue());
			FM6BaselineFacts DuplicateReplacement = RealPacketA.BaselineFacts;
			const FM6BaselineEntity FirstReplacementEntity =
				DuplicateReplacement.Entities[0];
			DuplicateReplacement.Entities = {
				FirstReplacementEntity, FirstReplacementEntity};
			TestTrue(
				TEXT("production native bridge rejects a duplicate replacement"),
				AtomicBridge.ApplyMembershipHighlight(DuplicateReplacement).HasError());
			// The production bridge owns both native-node mappings.  An entrance
			// Clear() or partial [E0,E0] commit makes at least one of these real
			// focus operations fail, even in headless automation where the Slate
			// graph implementation does not expose a paintable selection set.
			TestTrue(
				TEXT("a rejected production replacement preserves the first old native mapping"),
				AtomicBridge.FocusSemanticEntity(
					RealPacketA.BaselineFacts.Entities[0].Id).HasValue());
			TestTrue(
				TEXT("a rejected production replacement preserves the second old native mapping"),
				AtomicBridge.FocusSemanticEntity(
					RealPacketA.BaselineFacts.Entities[1].Id).HasValue());
			AtomicBridge.Clear();

			if (RealProjectionA.HasValue())
			{
				TSharedRef<SBlueprintLensPanel> LifecyclePanel =
					SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
				FM6DataMemberRow OldMember;
				OldMember.Name = TEXT("Old data result");
				OldMember.Guid = TEXT("old-data-guid");
				OldMember.bUsableInFocusedGraph = true;
				LifecyclePanel->M6Presentation.SetPythonReady(true);
				LifecyclePanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
				LifecyclePanel->M6Presentation.SetDataMemberRows({OldMember});
				LifecyclePanel->M6Presentation.SetDataCriterion(
					RealEventGraph->GetPathName(), OldMember.Guid, OldMember.Name);
				FM6SessionSnapshot OldReady;
				OldReady.State = EM6SessionState::Ready;
				OldReady.Baseline = EM6Baseline::C;
				OldReady.bHasReadySession = true;
				LifecyclePanel->M6Presentation.ApplySession(
					OldReady, &RealProjectionA.GetValue());
				LifecyclePanel->M6ReadyPacket =
					MakeShared<FM6LoadedSessionPacket>(RealPacketA);
				LifecyclePanel->M6LoadedSemanticSha256 =
					RealPacketA.SemanticSha256;

				FControllerHarness Lifecycle;
				Lifecycle.ReachRunning();
				FM6LoadedSessionPacket RealPacketB =
					MakeRealActivationPacket(*RealEventGraph, TEXT('b'));
				Lifecycle.CompleteReadyPacket(RealPacketB);
				LifecyclePanel->M6Controller = Lifecycle.Controller.Get();
				const auto TestRetainedActivation =
					[this, &LifecyclePanel, &RealPacketA, &OldMember](
						const TCHAR* Phase)
					{
						TestEqual(
							*FString::Printf(TEXT("%s preserves the local packet hash"), Phase),
							LifecyclePanel->M6LoadedSemanticSha256,
							RealPacketA.SemanticSha256);
						TestTrue(
							*FString::Printf(TEXT("%s preserves the local packet"), Phase),
							LifecyclePanel->M6ReadyPacket.IsValid() &&
								LifecyclePanel->M6ReadyPacket->SemanticSha256 ==
									RealPacketA.SemanticSha256);
						TestEqual(
							*FString::Printf(TEXT("%s preserves the old result identity"), Phase),
							LifecyclePanel->M6Presentation.ResultQuery().ExpectedMemberName,
							OldMember.Name);
						TestEqual(
							*FString::Printf(TEXT("%s preserves the old C baseline"), Phase),
							LifecyclePanel->M6Presentation.Baseline(),
							EM6Baseline::C);
					};
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestRetainedActivation(TEXT("native activation failure"));

				LifecyclePanel->M6NativeGraphBridge =
					FM6NativeGraphBridge(RealEditor);
				const FString ReplacementEntityId =
					RealPacketB.BaselineFacts.CriterionEntityId;
				LifecyclePanel->M6Presentation.SetExecutionCriterion(
					RealEventGraph->GetPathName(), ReplacementEntityId);
				const FM6QueryInput ReplacementQuery =
					LifecyclePanel->M6Presentation.GetQuery();
				Lifecycle.Controller->Run(ReplacementQuery);
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestRetainedActivation(TEXT("Preflight"));
				Lifecycle.Controller->Tick(5.0);
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestRetainedActivation(TEXT("Exporting"));
				Lifecycle.Controller->Tick(6.0);
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestRetainedActivation(TEXT("Running"));

				Lifecycle.Loader.Results.Add(MakeValue(
					MakeRealActivationPacket(*RealEventGraph, TEXT('b'))));
				FM6ProcessResult RetryProcess;
				RetryProcess.bStarted = true;
				RetryProcess.bCleanupSucceeded = true;
				RetryProcess.ReturnCode = 0;
				Lifecycle.Runner.Complete(RetryProcess);
				Lifecycle.Controller->Tick(7.0);
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestRetainedActivation(TEXT("Validating"));
				Lifecycle.Controller->Tick(8.0);
				LifecyclePanel->Present(Lifecycle.Controller->GetSnapshot());
				TestEqual(
					TEXT("same-hash Retry commits only at the final Ready activation"),
					LifecyclePanel->M6LoadedSemanticSha256,
					FString::ChrN(64, TEXT('b')));
				TestTrue(
					TEXT("final Ready activation commits the replacement packet"),
					LifecyclePanel->M6ReadyPacket.IsValid() &&
						LifecyclePanel->M6ReadyPacket->SemanticSha256 ==
							FString::ChrN(64, TEXT('b')));
				TestEqual(
					TEXT("final Ready activation adopts the replacement result identity"),
					LifecyclePanel->M6Presentation.ResultQuery().CriterionNodeId,
					ReplacementEntityId);
				TestEqual(
					TEXT("final Ready activation resets the new result to baseline A"),
					LifecyclePanel->M6Presentation.Baseline(),
					EM6Baseline::A);
				LifecyclePanel->M6Controller = nullptr;
				LifecyclePanel->M6NativeGraphBridge.Clear();
			}
		}
		CloseRealM6Editor(RealBlueprint);
	}

	const TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	TestTrue(TEXT("M6 attaches without -M6Session"), Panel->bM6Attached);

	FM6DataMemberRow SurfaceMember;
	SurfaceMember.Name = TEXT("SurfaceMember");
	SurfaceMember.Type = TEXT("Boolean");
	SurfaceMember.Guid = TEXT("surface-member-guid");
	SurfaceMember.bUsableInFocusedGraph = true;
	SurfaceMember.StatusText = TEXT("used by focused Graph");
	Panel->M6Presentation.SetPythonReady(true);
	Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
	Panel->M6Presentation.SetGraphId(TEXT("/Game/Test.BP:EventGraph"));
	TArray<FM6DataMemberRow> SurfaceMembers;
	SurfaceMembers.Add(SurfaceMember);
	for (int32 Index = 1; Index < 48; ++Index)
	{
		FM6DataMemberRow Member = SurfaceMember;
		Member.Name = FString::Printf(TEXT("SurfaceMember%02d"), Index);
		Member.Guid = FString::Printf(TEXT("surface-member-guid-%02d"), Index);
		SurfaceMembers.Add(MoveTemp(Member));
	}
	Panel->M6Presentation.SetDataMemberRows(MoveTemp(SurfaceMembers));
	Panel->RootBox->SetContent(Panel->BuildM6SessionContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	TestEqual(
		TEXT("the initial Execution surface uses exactly one query-kind dropdown"),
		SlateWidgetCountByType(
			PanelRoot, TEXT("SComboBox<TSharedPtr<FString>>")), 1);
	const TSharedPtr<SWidget> InitialRun = FindSlateButton(PanelRoot, TEXT("Run"));
	TestTrue(TEXT("Run is rendered before an Execution target exists"),
		InitialRun.IsValid());
	TestFalse(TEXT("Run is disabled until an Execution target exists"),
		InitialRun.IsValid() && InitialRun->IsEnabled());
	TestFalse(TEXT("the removed target-lock action is absent"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Set as query target")));
	TestFalse(TEXT("the removed target-unlock action is absent"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Unlock target")));
	Panel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
	Panel->RefreshM6Content();
	const FString DataSurfaceText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("rendered Data mode exposes the workflow disclosure"),
		DataSurfaceText.Contains(
			TEXT("Local variables and cross-asset variable resolution are outside this workflow.")));
	TestTrue(
		TEXT("rendered Data mode exposes the member picker row"),
		DataSurfaceText.Contains(TEXT("SurfaceMember")));
	const int32 QueryKindIndex = SlateWidgetTraversalIndexByType(
		PanelRoot, TEXT("SComboBox<TSharedPtr<FString>>"));
	const int32 RunIndex = SlateButtonTraversalIndex(
		PanelRoot, TEXT("Run"));
	const int32 FirstMemberIndex = SlateButtonTraversalIndex(
		PanelRoot, TEXT("SurfaceMember ·"));
	TestTrue(
		TEXT("a long Data picker keeps the query-kind dropdown before the member rows"),
		QueryKindIndex != INDEX_NONE && FirstMemberIndex != INDEX_NONE &&
			QueryKindIndex < FirstMemberIndex);
	TestTrue(
		TEXT("the Data target step is visibly ordered before Run"),
		RunIndex != INDEX_NONE && FirstMemberIndex != INDEX_NONE &&
			FirstMemberIndex < RunIndex);
	TestTrue(
		TEXT("a long Data picker scrolls its member rows inside a bounded viewport"),
		SlateButtonHasAncestorType(
			PanelRoot, TEXT("SurfaceMember47"), TEXT("SScrollBox")));
	TestFalse(
		TEXT("rendered Data mode removes the Execution target surface"),
		DataSurfaceText.Contains(TEXT("Execution node ·")));
	TestFalse(
		TEXT("Run is disabled before a Data member is chosen"),
		Panel->M6Presentation.CanRun());
	TestTrue(
		TEXT("a usable Data member can be chosen"),
		InvokeSlateButton(PanelRoot, TEXT("SurfaceMember ·")));
	TestTrue(
		TEXT("choosing a usable Data member enables Run directly"),
		Panel->M6Presentation.CanRun());
	TestFalse(
		TEXT("Data mode exposes no extra target-set action after member selection"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Set as query target")));
	TestFalse(
		TEXT("Data mode exposes no target-unset action after member selection"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Unlock target")));
	Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
	Panel->RefreshM6Content();
	TestTrue(
		TEXT("Data mode returns to Execution through the same dropdown without closing"),
		Panel->M6Presentation.GetQuery().Kind == EM6QueryKind::Execution &&
			FindSlateWidgetByType(
				PanelRoot, TEXT("SComboBox<TSharedPtr<FString>>")).IsValid());
	Panel->M6Presentation.SetExecutionCriterion(
		TEXT("/Game/Test.BP:EventGraph"),
		TEXT("/Game/Test.BP:EventGraph::node::guid-a"));

	FM6BaselineViewModels ReadyViews;
	ReadyViews.A.MemberEntityIds = {
		TEXT("/Game/Test.BP:EventGraph::node::guid-a"),
		TEXT("/Game/Test.BP:EventGraph::node::guid-b")};
	FM6SessionSnapshot ReadySurface;
	ReadySurface.State = EM6SessionState::Ready;
	ReadySurface.bHasReadySession = true;
	Panel->M6Presentation.ApplySession(ReadySurface, &ReadyViews);
	Panel->bM6SessionChromeExpanded = false;
	Panel->RootBox->SetContent(Panel->BuildM6SessionContent());
	const FString ReadySurfaceText = SlateWidgetText(PanelRoot);
	TestFalse(
		TEXT("reader-facing Ready state never exposes the raw enum index"),
		ReadySurfaceText.Contains(FString::Printf(
			TEXT("state %d"), static_cast<int32>(EM6SessionState::Ready))));
	TestTrue(
		TEXT("A/B/C comparison controls remain visible below collapsed setup"),
		ReadySurfaceText.Contains(TEXT("A · Full Native Graph")) &&
			ReadySurfaceText.Contains(TEXT("B · Native Slice")) &&
			ReadySurfaceText.Contains(TEXT("C · Causal Lens")));
	TestTrue(
		TEXT("collapsed Ready chrome can be expanded"),
		InvokeSlateButton(PanelRoot, TEXT("Expand session controls")));
	Panel->Present(ReadySurface);
	TestTrue(
		TEXT("a Ready state update preserves the reader's manual expansion"),
		Panel->bM6SessionChromeExpanded);
	TestTrue(
		TEXT("expanded Ready chrome exposes a route back to the collapsed rail"),
		InvokeSlateButton(PanelRoot, TEXT("Collapse session controls")) &&
			SlateWidgetText(PanelRoot).Contains(TEXT("Expand session controls")));
	FM6SessionSnapshot RunningSurface = ReadySurface;
	RunningSurface.State = EM6SessionState::Running;
	RunningSurface.bHasPendingRequest = true;
	Panel->Present(RunningSurface);
	TestFalse(
		TEXT("a Running state update preserves the reader's manual collapse"),
		Panel->bM6SessionChromeExpanded);
	TestTrue(
		TEXT("the retained Running surface remains visibly collapsed"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Expand session controls")) &&
			!SlateWidgetText(PanelRoot).Contains(TEXT("Collapse session controls")));
	Panel->Present(ReadySurface);
	TestFalse(
		TEXT("returning to Ready preserves the reader's manual collapse"),
		Panel->bM6SessionChromeExpanded);
	TestTrue(
		TEXT("the returned Ready surface remains visibly collapsed"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Expand session controls")) &&
			!SlateWidgetText(PanelRoot).Contains(TEXT("Collapse session controls")));
	const TSharedPtr<SWidget> BeforePostRunSelection = FindSlateButton(
		PanelRoot, TEXT("Expand session controls"));
	const FString BeforePostRunText = SlateWidgetText(PanelRoot);
	FM6NativeSelectionObservation InsideObservation;
	InsideObservation.EntityId =
		TEXT("/Game/Test.BP:EventGraph::node::guid-b");
	InsideObservation.GraphId = TEXT("/Game/Test.BP:EventGraph");
	InsideObservation.Label = TEXT("Target B");
	Panel->ObserveM6NativeSelection(InsideObservation);
	TestTrue(
		TEXT("post-Run in-session node selection does not rebuild any panel content"),
		BeforePostRunSelection.IsValid() &&
			BeforePostRunSelection == FindSlateButton(
				PanelRoot, TEXT("Expand session controls")));
	TestTrue(
		TEXT("post-Run graph selection is not reassigned as result-local selection"),
		Panel->M6Presentation.SelectedEntityId().IsEmpty());
	TestEqual(
		TEXT("post-Run graph selection remains the proposal committed by the next Run"),
		Panel->M6Presentation.GetQuery().CriterionNodeId,
		InsideObservation.EntityId);
	TestEqual(
		TEXT("post-Run graph selection preserves the currently shown criterion"),
		Panel->M6Presentation.ResultQuery().CriterionNodeId,
		FString(TEXT("/Game/Test.BP:EventGraph::node::guid-a")));
	TestEqual(
		TEXT("post-Run graph selection changes no visible text"),
		SlateWidgetText(PanelRoot), BeforePostRunText);
	FM6NativeSelectionObservation OutsideObservation;
	OutsideObservation.EntityId = TEXT("outside-after-run");
	OutsideObservation.bOutsideCurrentSession = true;
	Panel->ObserveM6NativeSelection(OutsideObservation);
	TestTrue(
		TEXT("post-Run native selection does not rebuild any panel content"),
		BeforePostRunSelection.IsValid() &&
			BeforePostRunSelection == FindSlateButton(
				PanelRoot, TEXT("Expand session controls")));
	TestTrue(
		TEXT("post-Run outside selection does not create a result-status refusal"),
		Panel->M6Presentation.OutsideStatus().IsEmpty());
	Panel->RefreshM6Content();
	TestFalse(
		TEXT("an unrelated rebuild does not reveal the silent Execution proposal"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Target B")) ||
			SlateWidgetText(PanelRoot).Contains(TEXT("outside-after-run")));

	FM6PanelPresentationModel Model;
	FM6PythonResolutionResult Python;
	Python.bValid = true;
	Python.ExecutablePath = TEXT("C:/Python/python.exe");
	Python.Version = TEXT("3.13");
	Model.SetPythonResolution(MoveTemp(Python));
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	TestEqual(TEXT("valid target is Ready"), Model.Status(), EM6PanelStatus::Ready);
	TestTrue(TEXT("valid target enables Run"), Model.CanRun());

	FM6BaselineViewModels Views;
	FM6SessionSnapshot Ready;
	Ready.State = EM6SessionState::Ready;
	Ready.bHasReadySession = true;
	Model.ApplySession(Ready, &Views);
	Model.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/BP.BP"), TEXT("EventGraph"), TEXT("GraphA"));
	Model.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/BP.BP"), TEXT("OtherGraph"), TEXT("GraphB"));
	TestEqual(
		TEXT("focused Graph exploration is neutral for the shown answer"),
		Model.Status(), EM6PanelStatus::Ready);
	TestTrue(
		TEXT("focused Graph exploration does not strand the next Run"),
		Model.CanRun());

	Model.DispatchReset();
	TestTrue(
		TEXT("Reset preserves the validated Python resolution"),
		Model.IsPythonReady() &&
		Model.PythonResolution().bValid &&
		Model.PythonResolution().ExecutablePath == TEXT("C:/Python/python.exe") &&
		Model.PythonResolution().Version == TEXT("3.13"));
	TestEqual(
		TEXT("Reset leaves the setup message about the target, not the runtime"),
		Model.StatusMessage(),
		FString(TEXT("Select one Execution node in the graph.")));
	FM6PythonResolutionResult PythonAgain;
	PythonAgain.bValid = true;
	Model.SetPythonResolution(MoveTemp(PythonAgain));
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ApplySession(Ready, &Views);
	Model.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/BP.BP"), TEXT("EventGraph"), TEXT("GraphA"));
	Model.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/Other.BP"), TEXT("EventGraph"), TEXT("GraphA"));
	TestEqual(
		TEXT("focused Blueprint exploration is neutral for the shown answer"),
		Model.Status(), EM6PanelStatus::Ready);

	Model.DispatchReset();
	Model.SetPythonReady(true);
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ApplySession(Ready, &Views);
	Model.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("B"));
	TestEqual(TEXT("post-result selection proposes the next Execution node"),
		Model.GetQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-b")));
	TestEqual(TEXT("post-result selection preserves the shown criterion"),
		Model.ResultQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));
	TestEqual(TEXT("post-result selection is neutral"),
		Model.Status(), EM6PanelStatus::Ready);

	Model.DispatchReset();
	Model.SetPythonReady(true);
	FM6DataMemberRow MemberA;
	MemberA.Name = TEXT("A");
	MemberA.Guid = TEXT("guid-a");
	MemberA.bUsableInFocusedGraph = true;
	FM6DataMemberRow MemberB = MemberA;
	MemberB.Name = TEXT("B");
	MemberB.Guid = TEXT("guid-b");
	Model.SetQueryKind(EM6QueryKind::Data);
	Model.SetGraphId(TEXT("GraphA"));
	Model.SetDataMemberRows({MemberA, MemberB});
	TestTrue(TEXT("identity test selects initial member"), Model.SelectDataMember(TEXT("guid-a")));
	Model.ApplySession(Ready, &Views);
	TestTrue(TEXT("changed member can be selected"), Model.SelectDataMember(TEXT("guid-b")));
	TestEqual(TEXT("changed member preserves the shown Data result"),
		Model.ResultQuery().ExpectedMemberName, FString(TEXT("A")));
	TestEqual(TEXT("changed member is a neutral next-Run proposal"),
		Model.Status(), EM6PanelStatus::Ready);

	FM6PanelPresentationModel DataRerun;
	DataRerun.SetPythonReady(true);
	DataRerun.SetQueryKind(EM6QueryKind::Data);
	DataRerun.SetGraphId(TEXT("GraphA"));
	DataRerun.SetDataMemberRows({MemberA, MemberB});
	TestTrue(TEXT("Data rerun selects the first member"),
		DataRerun.SelectDataMember(TEXT("guid-a")));
	DataRerun.ApplySession(Ready, &Views);
	TestTrue(TEXT("the first Data result is visible before retargeting"),
		DataRerun.Views().IsValid());
	TestTrue(TEXT("Data rerun selects a replacement member directly"),
		DataRerun.SelectDataMember(TEXT("guid-b")));
	TestTrue(
		TEXT("selecting a replacement Data member keeps the prior answer inspectable"),
		DataRerun.Views().IsValid());
	TestEqual(
		TEXT("the prior Data answer keeps its committed target"),
		DataRerun.ResultQuery().ExpectedMemberName, FString(TEXT("A")));
	TestTrue(
		TEXT("a replacement Data target can run after a prior result"),
		DataRerun.CanRun());
	TestEqual(
		TEXT("a replacement proposal does not turn the old answer into a warning"),
		DataRerun.Status(), EM6PanelStatus::Ready);

	FM6PanelPresentationModel CrossGraphData;
	CrossGraphData.SetPythonReady(true);
	CrossGraphData.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/BP.BP"), TEXT("Graph A"), TEXT("GraphA"));
	CrossGraphData.SetQueryKind(EM6QueryKind::Data);
	CrossGraphData.SetGraphId(TEXT("GraphA"));
	CrossGraphData.SetDataMemberRows({MemberA});
	TestTrue(TEXT("cross-Graph setup selects Data A"),
		CrossGraphData.SelectDataMember(TEXT("guid-a")));
	CrossGraphData.ApplySession(Ready, &Views);
	CrossGraphData.SetBlueprintContext(
		TEXT("BP"), TEXT("/Game/BP.BP"), TEXT("Graph B"), TEXT("GraphB"));
	CrossGraphData.SetDataMemberRows({MemberB});
	TestTrue(TEXT("a member from the newly focused Graph can be selected"),
		CrossGraphData.SelectDataMember(TEXT("guid-b")));
	TestEqual(
		TEXT("selecting a Data member binds the query to its focused Graph"),
		CrossGraphData.GetQuery().GraphId,
		FString(TEXT("GraphB")));
	TestTrue(TEXT("the cross-Graph replacement is ready to run"),
		CrossGraphData.CanRun());

	FM6PanelPresentationModel FailedActivation;
	FailedActivation.SetPythonReady(true);
	FailedActivation.SetQueryKind(EM6QueryKind::Data);
	FailedActivation.SetGraphId(TEXT("GraphA"));
	FailedActivation.SetDataMemberRows({MemberA, MemberB});
	FailedActivation.SelectDataMember(TEXT("guid-a"));
	FM6BaselineViewModels OldViews;
	OldViews.C.RendererId = TEXT("old-result");
	FM6SessionSnapshot ReadyOldC = Ready;
	ReadyOldC.Baseline = EM6Baseline::C;
	FailedActivation.ApplySession(ReadyOldC, &OldViews);
	FailedActivation.SelectDataMember(TEXT("guid-b"));
	FM6BaselineViewModels RejectedViews;
	RejectedViews.C.RendererId = TEXT("rejected-replacement");
	FM6SessionSnapshot FailedReplacement = Ready;
	FailedReplacement.State = EM6SessionState::Failed;
	FailedReplacement.Baseline = EM6Baseline::A;
	FailedReplacement.Error.Code = TEXT("M6_NATIVE_GRAPH_NOT_FOUND");
	FailedReplacement.Error.Message = TEXT("native activation failed");
	FailedActivation.ApplySession(FailedReplacement, &RejectedViews);
	TestTrue(
		TEXT("a failed replacement activation preserves the old visible result"),
		FailedActivation.Views().IsValid() &&
			FailedActivation.Views()->C.RendererId == TEXT("old-result"));
	TestEqual(
		TEXT("a failed replacement activation preserves the old result identity"),
		FailedActivation.ResultQuery().ExpectedMemberName,
		FString(TEXT("A")));
	TestEqual(
		TEXT("a failed replacement activation preserves the old C baseline"),
		FailedActivation.Baseline(),
		EM6Baseline::C);

	FM6PanelPresentationModel ExecutionReturn;
	ExecutionReturn.SetPythonReady(true);
	ExecutionReturn.SetQueryKind(EM6QueryKind::Data);
	ExecutionReturn.SetGraphId(TEXT("GraphA"));
	ExecutionReturn.SetDataMemberRows({MemberA});
	TestTrue(TEXT("Execution return starts from a selected Data member"),
		ExecutionReturn.SelectDataMember(TEXT("guid-a")));
	ExecutionReturn.ApplySession(Ready, &Views);
	ExecutionReturn.SetQueryKind(EM6QueryKind::Execution);
	TestTrue(
		TEXT("returning to Execution keeps the prior Data answer inspectable"),
		ExecutionReturn.Views().IsValid());
	TestEqual(
		TEXT("returning to Execution is a neutral mode change"),
		ExecutionReturn.Status(), EM6PanelStatus::NeedsSetup);
	ExecutionReturn.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::exec-guid"), TEXT("Exec"), true);
	TestTrue(
		TEXT("one answerable Execution selection enables Run after returning from Data"),
		ExecutionReturn.CanRun());
	TestEqual(
		TEXT("the prior Data answer remains inspectable without a Stale warning"),
		ExecutionReturn.Status(), EM6PanelStatus::Ready);

	TSharedRef<SBlueprintLensPanel> ExecutionReturnPanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	ExecutionReturnPanel->M6Presentation.SetPythonReady(true);
	ExecutionReturnPanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
	ExecutionReturnPanel->M6Presentation.SetGraphId(TEXT("GraphA"));
	ExecutionReturnPanel->M6Presentation.SetDataMemberRows({MemberA});
	TestTrue(TEXT("rendered Execution return starts with Data A"),
		ExecutionReturnPanel->M6Presentation.SelectDataMember(TEXT("guid-a")));
	ExecutionReturnPanel->M6Presentation.ApplySession(Ready, &Views);
	ExecutionReturnPanel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
	ExecutionReturnPanel->RootBox->SetContent(
		ExecutionReturnPanel->BuildM6SessionContent());
	const TSharedPtr<SWidget> ReturnRun = FindSlateButton(
		ExecutionReturnPanel, TEXT("Run"));
	const FString ReturnTextBeforeSelection = SlateWidgetText(ExecutionReturnPanel);
	TestFalse(
		TEXT("rendered Run is disabled after Data returns to targetless Execution"),
		ReturnRun.IsValid() && ReturnRun->IsEnabled());
	FM6NativeSelectionObservation ReturnObservation;
	ReturnObservation.GraphId = TEXT("GraphA");
	ReturnObservation.EntityId = TEXT("GraphA::node::exec-guid");
	ReturnObservation.Label = TEXT("Exec");
	ReturnObservation.bHasExecutionPin = true;
	ExecutionReturnPanel->ObserveM6NativeSelection(ReturnObservation);
	TestTrue(
		TEXT("a valid silent proposal enables the existing Run control"),
		ReturnRun.IsValid() && ReturnRun == FindSlateButton(
			ExecutionReturnPanel, TEXT("Run")) && ReturnRun->IsEnabled());
	TestEqual(
		TEXT("enabling Run after the silent proposal changes no visible text"),
		SlateWidgetText(ExecutionReturnPanel), ReturnTextBeforeSelection);
	TestEqual(
		TEXT("the prior shown Data criterion survives the silent Execution proposal"),
		ExecutionReturnPanel->M6Presentation.ResultQuery().ExpectedMemberName,
		FString(TEXT("A")));

	FM6PanelPresentationModel SameMode;
	SameMode.SetPythonReady(true);
	SameMode.SetQueryKind(EM6QueryKind::Data);
	SameMode.SetGraphId(TEXT("GraphA"));
	SameMode.SetDataMemberRows({MemberA});
	TestTrue(TEXT("same-mode setup selects Data A"),
		SameMode.SelectDataMember(TEXT("guid-a")));
	SameMode.ApplySession(Ready, &Views);
	SameMode.SetQueryKind(EM6QueryKind::Data);
	TestTrue(
		TEXT("choosing the active mode is idempotent for the visible result"),
		SameMode.Views().IsValid());
	TestEqual(
		TEXT("choosing the active mode does not mark its result Stale"),
		SameMode.Status(), EM6PanelStatus::Ready);

	FM6SessionSnapshot Running = Ready;
	Running.State = EM6SessionState::Running;
	Running.bHasPendingRequest = true;
	SameMode.ApplySession(Running, nullptr);
	SameMode.SetQueryKind(EM6QueryKind::Execution);
	TestEqual(
		TEXT("the submitted Data query is immutable while Running"),
		SameMode.GetQuery().Kind, EM6QueryKind::Data);
	TestTrue(
		TEXT("the prior ready result remains inspectable while replacement work runs"),
		SameMode.Views().IsValid());

	TSharedRef<SBlueprintLensPanel> RunningPanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	RunningPanel->M6Presentation.SetPythonReady(true);
	RunningPanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
	RunningPanel->M6Presentation.SetGraphId(TEXT("GraphA"));
	RunningPanel->M6Presentation.SetDataMemberRows({MemberA});
	RunningPanel->M6Presentation.SelectDataMember(TEXT("guid-a"));
	RunningPanel->M6Presentation.ApplySession(Ready, &Views);
	RunningPanel->M6Presentation.ApplySession(Running, nullptr);
	TestFalse(
		TEXT("the rendered panel model refuses query editing while Running"),
		RunningPanel->M6Presentation.CanEditQuery());
	RunningPanel->RootBox->SetContent(RunningPanel->BuildM6SessionContent());
	const TSharedPtr<SWidget> RunningQueryKind = FindSlateWidgetByType(
		RunningPanel, TEXT("SComboBox<TSharedPtr<FString>>"));
	TestEqual(
		TEXT("one query-kind dropdown remains rendered while Running"),
		SlateWidgetCountByType(
			RunningPanel, TEXT("SComboBox<TSharedPtr<FString>>")), 1);
	TestFalse(
		TEXT("the query-kind dropdown is disabled while the submitted query is immutable"),
		RunningQueryKind.IsValid() && RunningQueryKind->IsEnabled());

	const FBlueprintLensLoadResult RailLoad =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("the retained C-rail fixture loads"), RailLoad.IsSuccess());
	if (RailLoad.IsSuccess())
	{
		TSharedRef<SBlueprintLensPanel> RetainedRailPanel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		RetainedRailPanel->M6Presentation.SetPythonReady(true);
		RetainedRailPanel->M6Presentation.SetExecutionCriterion(
			TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
		FM6SessionSnapshot ReadyC = Ready;
		ReadyC.Baseline = EM6Baseline::C;
		RetainedRailPanel->M6Presentation.ApplySession(ReadyC, &Views);
		FM6LoadedSessionPacket RetainedPacket;
		RetainedPacket.Explanation = *RailLoad.Model;
		RetainedPacket.SemanticSha256 = FString::ChrN(64, TEXT('a'));
		RetainedRailPanel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(RetainedPacket));

		FM6SessionSnapshot ReplacementPending = ReadyC;
		ReplacementPending.State = EM6SessionState::Running;
		ReplacementPending.bHasPendingRequest = true;
		ReplacementPending.bReadySessionStale = true;
		RetainedRailPanel->M6Presentation.ApplySession(
			ReplacementPending, nullptr);
		RetainedRailPanel->RootBox->SetContent(
			RetainedRailPanel->BuildM6SessionContent());
		const FName RailTag(
			TEXT("BlueprintLens.Automation.SharedExecutionRail"));
		TestTrue(
			TEXT("the retained C rail remains inspectable while replacement work runs"),
			SlateHasWidgetTag(RetainedRailPanel, RailTag));

		FM6SessionSnapshot ReplacementFailed = Ready;
		ReplacementFailed.State = EM6SessionState::Failed;
		ReplacementFailed.Baseline = EM6Baseline::A;
		ReplacementFailed.Error.Code = TEXT("M6_NATIVE_GRAPH_NOT_FOUND");
		RetainedRailPanel->M6Presentation.ApplySession(
			ReplacementFailed, nullptr);
		RetainedRailPanel->RootBox->SetContent(
			RetainedRailPanel->BuildM6SessionContent());
		TestTrue(
			TEXT("the retained C rail remains inspectable after replacement failure"),
			SlateHasWidgetTag(RetainedRailPanel, RailTag));
		TestEqual(
			TEXT("replacement failure cannot switch the retained C result to A"),
			RetainedRailPanel->M6Presentation.Baseline(),
			EM6Baseline::C);
	}

	TSharedRef<SBlueprintLensPanel> ProposalPanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	ProposalPanel->M6Presentation.SetPythonReady(true);
	ProposalPanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
	ProposalPanel->M6Presentation.SetGraphId(TEXT("GraphA"));
	ProposalPanel->M6Presentation.SetDataMemberRows({MemberA, MemberB});
	ProposalPanel->M6Presentation.SelectDataMember(TEXT("guid-a"));
	ProposalPanel->M6Presentation.ApplySession(Ready, &Views);
	ProposalPanel->M6Presentation.SelectDataMember(TEXT("guid-b"));
	ProposalPanel->bM6SessionChromeExpanded = false;
	ProposalPanel->RootBox->SetContent(ProposalPanel->BuildM6SessionContent());
	const FString ProposalSummary = SlateWidgetText(ProposalPanel);
	TestTrue(
		TEXT("neutral result context labels the shown Data target"),
		ProposalSummary.Contains(TEXT("showing: A")));
	TestTrue(
		TEXT("neutral result context distinguishes the next selected Data target"),
		ProposalSummary.Contains(TEXT("selected: B")));
	TestFalse(
		TEXT("ordinary retargeting emits no Stale warning"),
		ProposalSummary.Contains(TEXT("Stale")));

	Model.DispatchReset();
	Model.SetPythonReady(true);
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ObserveSourceFingerprint(TEXT("fingerprint-a"));
	Model.ApplySession(Ready, &Views);
	Model.ObserveSourceFingerprint(TEXT("fingerprint-b"));
	TestEqual(TEXT("source fingerprint change is Stale"), Model.Status(), EM6PanelStatus::Stale);
	TestTrue(
		TEXT("a genuine source invalidation can be replaced by Run"),
		Model.CanRun());

	Model.DispatchReset();
	Model.SetPythonReady(true);
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ApplySession(Ready, &Views);
	Model.MarkTargetInvalid(TEXT("submitted target node was deleted"));
	TestEqual(TEXT("deleted target is Stale"), Model.Status(), EM6PanelStatus::Stale);
	TestFalse(
		TEXT("a deleted proposal cannot be submitted again"),
		Model.CanRun());
	TestEqual(
		TEXT("the deleted result remains the answer being shown"),
		Model.ResultQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));
	Model.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"), true);
	TestTrue(
		TEXT("a valid replacement proposal unstrands Run"),
		Model.CanRun());
	TestEqual(
		TEXT("the valid replacement remains only a proposal until Run"),
		Model.ResultQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));

	Model.DispatchReset();
	Model.SetPythonReady(true);
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ApplySession(Ready, &Views);
	Model.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"), true);
	Model.MarkShownResultInvalid(TEXT("shown Execution node was deleted"));
	TestEqual(
		TEXT("deleting shown Execution A marks only the shown answer Stale"),
		Model.Status(), EM6PanelStatus::Stale);
	TestTrue(
		TEXT("valid Execution proposal B remains runnable when shown A disappears"),
		Model.CanRun());
	TestEqual(
		TEXT("shown invalidation preserves proposed Execution B"),
		Model.GetQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-b")));

	Model.DispatchReset();
	Model.SetPythonReady(true);
	Model.SetExecutionCriterion(TEXT("GraphA"), TEXT("GraphA::node::guid-a"));
	Model.ApplySession(Ready, &Views);
	Model.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"), true);
	const uint64 ValidProposalRevision = Model.PresentationRevision();
	Model.MarkTargetInvalid(TEXT("selected Execution node was deleted"));
	TestTrue(
		TEXT("background Execution invalidation changes the presentation revision"),
		Model.PresentationRevision() > ValidProposalRevision);
	TestFalse(
		TEXT("deleting proposed Execution B disables Run while A remains shown"),
		Model.CanRun());
	TestEqual(
		TEXT("invalid proposed Execution B does not mark shown A Stale"),
		Model.Status(), EM6PanelStatus::NeedsSetup);
	TestEqual(
		TEXT("invalid proposed Execution B preserves shown A"),
		Model.ResultQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-a")));
	const uint64 InvalidProposalRevision = Model.PresentationRevision();
	Model.MarkTargetAvailable();
	TestTrue(
		TEXT("background Execution recovery changes the presentation revision"),
		Model.PresentationRevision() > InvalidProposalRevision);
	TestTrue(
		TEXT("background Execution recovery re-enables Run"),
		Model.CanRun());

	FM6PanelPresentationModel AnswerabilityRecovery;
	AnswerabilityRecovery.SetPythonReady(true);
	AnswerabilityRecovery.SetQueryKind(EM6QueryKind::Execution);
	AnswerabilityRecovery.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"), false);
	TestFalse(TEXT("same-node recovery begins unanswerable"),
		AnswerabilityRecovery.CanRun());
	const uint64 UnanswerableRevision =
		AnswerabilityRecovery.PresentationRevision();
	AnswerabilityRecovery.MarkTargetAvailable();
	TestTrue(
		TEXT("same-node execution-pin recovery updates answerability"),
		AnswerabilityRecovery.IsExecutionTargetAnswerable() &&
			AnswerabilityRecovery.CanRun());
	TestTrue(
		TEXT("same-node execution-pin recovery refreshes the presentation"),
		AnswerabilityRecovery.PresentationRevision() > UnanswerableRevision);

	FM6PanelPresentationModel RunningObservation;
	RunningObservation.SetPythonReady(true);
	RunningObservation.SetQueryKind(EM6QueryKind::Execution);
	RunningObservation.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"), TEXT("Target B"), true);
	FM6SessionSnapshot RunningImmutable = Ready;
	RunningImmutable.State = EM6SessionState::Running;
	RunningImmutable.bHasPendingRequest = true;
	RunningObservation.ApplySession(RunningImmutable, nullptr);
	RunningObservation.ObserveExecutionSelection(
		TEXT("GraphA"), TEXT("GraphA::node::guid-c"), TEXT("Target C"), false);
	TestEqual(
		TEXT("Running native selection cannot replace the submitted query"),
		RunningObservation.GetQuery().CriterionNodeId,
		FString(TEXT("GraphA::node::guid-b")));
	TestEqual(
		TEXT("Running native selection cannot replace submitted observation identity"),
		RunningObservation.ExecutionTargetNodeId(),
		FString(TEXT("GraphA::node::guid-b")));
	TestTrue(
		TEXT("Running native selection cannot poison submitted answerability"),
		RunningObservation.IsExecutionTargetAnswerable());
	RunningObservation.ApplySession(Ready, &Views);
	TestTrue(
		TEXT("completed submitted B remains runnable after ignored Running selection"),
		RunningObservation.CanRun());
	FM6SessionSnapshot FailedImmutable = Ready;
	FailedImmutable.State = EM6SessionState::Failed;
	FailedImmutable.bHasReadySession = true;
	FailedImmutable.Error.Code = TEXT("M6_TEST_FAILURE");
	RunningObservation.ApplySession(FailedImmutable, nullptr);
	TestTrue(
		TEXT("failed submitted B remains retryable after ignored Running selection"),
		RunningObservation.CanRetry());

	TSharedRef<SBlueprintLensPanel> AvailabilityPanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	AvailabilityPanel->M6Presentation.SetPythonReady(true);
	AvailabilityPanel->M6Presentation.SetExecutionCriterion(
		TEXT("GraphA"), TEXT("GraphA::node::guid-b"));
	AvailabilityPanel->RootBox->SetContent(
		AvailabilityPanel->BuildM6SessionContent());
	TestTrue(
		TEXT("rendered background-availability control begins enabled"),
		FindSlateButton(AvailabilityPanel, TEXT("Run")).IsValid() &&
			FindSlateButton(AvailabilityPanel, TEXT("Run"))->IsEnabled());
	AvailabilityPanel->M6Presentation.MarkTargetInvalid(
		TEXT("selected Execution node was deleted"));
	AvailabilityPanel->RefreshM6Content();
	TestFalse(
		TEXT("rendered Run disables after background target invalidation"),
		FindSlateButton(AvailabilityPanel, TEXT("Run")).IsValid() &&
			FindSlateButton(AvailabilityPanel, TEXT("Run"))->IsEnabled());
	AvailabilityPanel->M6Presentation.MarkTargetAvailable();
	AvailabilityPanel->RefreshM6Content();
	TestTrue(
		TEXT("rendered Run re-enables after background target recovery"),
		FindSlateButton(AvailabilityPanel, TEXT("Run")).IsValid() &&
			FindSlateButton(AvailabilityPanel, TEXT("Run"))->IsEnabled());

	FM6PanelPresentationModel DataAvailability;
	DataAvailability.SetPythonReady(true);
	DataAvailability.SetQueryKind(EM6QueryKind::Data);
	DataAvailability.SetGraphId(TEXT("GraphA"));
	DataAvailability.SetDataMemberRows({MemberA, MemberB});
	TestTrue(TEXT("Data availability selects shown A"),
		DataAvailability.SelectDataMember(TEXT("guid-a")));
	DataAvailability.ApplySession(Ready, &Views);
	TestTrue(TEXT("Data availability proposes B"),
		DataAvailability.SelectDataMember(TEXT("guid-b")));
	DataAvailability.SetDataMemberRows({MemberA});
	TestFalse(
		TEXT("deleting proposed Data B disables Run while A remains shown"),
		DataAvailability.CanRun());
	TestEqual(
		TEXT("invalid proposed Data B preserves shown A without a Stale claim"),
		DataAvailability.Status(), EM6PanelStatus::NeedsSetup);
	DataAvailability.SetDataMemberRows({MemberA, MemberB});
	TestTrue(
		TEXT("restoring proposed Data B restores Run without changing shown A"),
		DataAvailability.CanRun());
	DataAvailability.MarkShownResultInvalid(TEXT("shown Data member was deleted"));
	TestTrue(
		TEXT("valid Data proposal B remains runnable when shown A disappears"),
		DataAvailability.CanRun());
	TestEqual(
		TEXT("shown Data invalidation preserves proposed B"),
		DataAvailability.GetQuery().ExpectedMemberName,
		FString(TEXT("B")));
	TestEqual(
		TEXT("shown Data invalidation preserves result identity A"),
		DataAvailability.ResultQuery().ExpectedMemberName,
		FString(TEXT("A")));

	Model.DispatchReset();
	TestTrue(TEXT("Reset preserves validated Python"), Model.IsPythonReady());
	TestEqual(TEXT("Reset clears the active query"), Model.GetQuery().Kind, EM6QueryKind::Invalid);
	TestEqual(TEXT("Reset returns to setup state"), Model.Status(), EM6PanelStatus::NeedsSetup);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM10CompositeSlotsTest,
	"BlueprintLens.M10.CompositeSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM10CompositeSlotsTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Load =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("the nested-guard Explanation loads"), Load.IsSuccess());
	if (!Load.IsSuccess())
	{
		return false;
	}

	FBlueprintLensExplanationModel LiveExplanation = *Load.Model;
	FBlueprintLensExplanationModel ConstructionFixture = *Load.Model;
	const FString CurrentBlueprint =
		TEXT("/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards");
	const FString StaleBlueprint =
		TEXT("/Game/LensCorpus/BP_ConstructionFixture.BP_ConstructionFixture");
	LiveExplanation.Source.BlueprintAssetPath = CurrentBlueprint;
	ConstructionFixture.Source.BlueprintAssetPath = StaleBlueprint;
	// A production M6 Explanation carries generic predicate/control relations,
	// not the authored R1 groups or disambiguators. Exercise that real shape.
	LiveExplanation.Groups.Reset();
	LiveExplanation.bHasGroupPartialOrder = false;
	for (FBlueprintLensUnit& Unit : LiveExplanation.Units)
	{
		Unit.bHasDisambiguator = false;
		Unit.Disambiguator = FBlueprintLensDisambiguator();
	}
	for (FBlueprintLensRelation& Relation : LiveExplanation.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
			Relation.bHasPortLabel && Relation.PortLabel == TEXT("OuterEnabled"))
		{
			Relation.PortLabel = TEXT("CURRENT BLUEPRINT GUARD");
			if (Relation.bHasSourceEdgeEndpoints &&
				Relation.SourceEdgeEndpoints.Num() == 1)
			{
				Relation.SourceEdgeEndpoints[0].SourcePortLabel =
					TEXT("CURRENT BLUEPRINT GUARD");
			}
		}
	}
	for (FBlueprintLensUnit& Unit : ConstructionFixture.Units)
	{
		if (Unit.Title == TEXT("Branch") && Unit.bHasDisambiguator &&
			Unit.Disambiguator.Text == TEXT("OuterEnabled"))
		{
			Unit.Disambiguator.Text = TEXT("STALE CONSTRUCTION FIXTURE GUARD");
		}
	}

	TestEqual(
		TEXT("the Explanation offered to the live projector names the current Blueprint"),
		LiveExplanation.Source.BlueprintAssetPath,
		CurrentBlueprint);

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(
		MoveTemp(ConstructionFixture));
	const FString NativeLabelSentinel = TEXT("LOCALIZED_FIXTURE_LABEL");
	Panel->M6Presentation.SetPythonReady(true);
	Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
	Panel->M6Presentation.SetGraphId(LiveExplanation.Source.GraphId);
	Panel->M6Presentation.ObserveExecutionSelection(
		LiveExplanation.Source.GraphId,
		LiveExplanation.Query.CriterionSourceNodeId,
		NativeLabelSentinel,
		true);
	FM6BaselineViewModels Views;
	FM6SessionSnapshot Ready;
	Ready.State = EM6SessionState::Ready;
	Ready.bHasReadySession = true;
	Ready.Baseline = EM6Baseline::C;
	Panel->M6Presentation.ApplySession(Ready, &Views);
	FM6LoadedSessionPacket LivePacket;
	LivePacket.Explanation = MoveTemp(LiveExplanation);
	LivePacket.SemanticSha256 = FString::ChrN(64, TEXT('a'));
	Panel->M6ReadyPacket =
		MakeShared<FM6LoadedSessionPacket>(MoveTemp(LivePacket));
	const FBlueprintLensExplanationModel& AcceptedLiveExplanation =
		Panel->M6ReadyPacket->Explanation;
	const FBlueprintLensLC1RailProjection LiveRail =
		FBlueprintLensLC1RailProjector::Build(AcceptedLiveExplanation);
	TestTrue(
		*FString::Printf(
			TEXT("live LC1 rail is renderable: %s"),
			*LiveRail.DiagnosticCode),
		LiveRail.IsRenderable());
	TestTrue(
		TEXT("the branch-order RED really exercises a mixed projection"),
		!LiveRail.DeferredRelationIds.IsEmpty());
	FBlueprintLensLC1RailProjection ReversedMixedRail = LiveRail;
	const FBlueprintLensLC1RailExecutionRelation* CrossComponentRelation =
		ReversedMixedRail.OrderedExecutionRelations.FindByPredicate(
			[&ReversedMixedRail](
				const FBlueprintLensLC1RailExecutionRelation& Relation)
			{
				return Relation.TargetUnitId !=
					ReversedMixedRail.CriterionUnitId;
			});
	TestNotNull(
		TEXT("the mixed branch probe contains a non-criterion execution edge"),
		CrossComponentRelation);
	if (CrossComponentRelation != nullptr)
	{
		const int32 SourceIndex =
			ReversedMixedRail.AllUnitIds.IndexOfByKey(
				CrossComponentRelation->SourceUnitId);
		const int32 TargetIndex =
			ReversedMixedRail.AllUnitIds.IndexOfByKey(
				CrossComponentRelation->TargetUnitId);
		TestTrue(
			TEXT("the built mixed projection orders that execution edge forward"),
			SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE &&
				SourceIndex < TargetIndex);
		if (SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE &&
			SourceIndex < TargetIndex)
		{
			ReversedMixedRail.AllUnitIds.Swap(SourceIndex, TargetIndex);
			ReversedMixedRail.FallbackUnitIds.Swap(SourceIndex, TargetIndex);
			ReversedMixedRail.OrderedCanonicalUnits.Swap(
				SourceIndex, TargetIndex);
			ResignLC1RailProjectionForMutation(ReversedMixedRail);
			TestTrue(
				TEXT("the mixed-order mutation retains a valid integrity signature"),
				ReversedMixedRail.HasValidIntegrity());
			TestFalse(
				TEXT("a re-signed mixed projection rejects an inverted inter-component execution edge"),
				ReversedMixedRail.IsRenderable());
		}
	}
	const FBlueprintLensRelation* DeferredStationRelation =
		AcceptedLiveExplanation.Relations.FindByPredicate(
			[&LiveRail](const FBlueprintLensRelation& Relation)
			{
				return Relation.Kind ==
						EBlueprintLensRelationKind::ControlsExecution &&
					LiveRail.AllUnitIds.Contains(Relation.SourceUnitId) &&
					LiveRail.AllUnitIds.Contains(Relation.TargetUnitId) &&
					LiveRail.DeferredRelationIds.Contains(Relation.Id);
			});
	TestNotNull(
		TEXT("the deferred-order RED uses a causal relation whose endpoints are both rail stations"),
		DeferredStationRelation);
	if (DeferredStationRelation != nullptr)
	{
		FBlueprintLensLC1RailProjection DeferredInversion = LiveRail;
		int32 SourceIndex = DeferredInversion.AllUnitIds.IndexOfByKey(
			DeferredStationRelation->SourceUnitId);
		int32 TargetIndex = DeferredInversion.AllUnitIds.IndexOfByKey(
			DeferredStationRelation->TargetUnitId);
		if (SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE &&
			SourceIndex < TargetIndex)
		{
			DeferredInversion.AllUnitIds.Swap(SourceIndex, TargetIndex);
			DeferredInversion.FallbackUnitIds.Swap(SourceIndex, TargetIndex);
			DeferredInversion.OrderedCanonicalUnits.Swap(
				SourceIndex, TargetIndex);
			SourceIndex = DeferredInversion.AllUnitIds.IndexOfByKey(
				DeferredStationRelation->SourceUnitId);
			TargetIndex = DeferredInversion.AllUnitIds.IndexOfByKey(
				DeferredStationRelation->TargetUnitId);
		}
		TestTrue(
			TEXT("the deferred-order mutation really places a proven cause after its station target"),
			SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE &&
				SourceIndex >= TargetIndex);
		ResignLC1RailProjectionForMutation(DeferredInversion);
		TestTrue(
			TEXT("the deferred-order mutation retains a valid integrity signature"),
			DeferredInversion.HasValidIntegrity());
		TestFalse(
			TEXT("a re-signed projection rejects an inverted deferred causal relation between stations"),
			DeferredInversion.IsRenderable());
	}
	const FBlueprintLensCompositeRailSlots BaseSlots =
		FBlueprintLensCompositeRailSlotProjector::Build(
			AcceptedLiveExplanation,
			LiveRail);
	TestTrue(
		*FString::Printf(
			TEXT("base composite slots are renderable: %s"),
			*BaseSlots.DiagnosticCode),
		BaseSlots.IsRenderable(LiveRail));
	const FBlueprintLensLC2GuardOutlineProjection LiveOutline =
		FBlueprintLensLC2GuardOutlineProjector::Build(AcceptedLiveExplanation);
	TestEqual(
		TEXT("the group-free live Explanation remains the LC2 ungrouped fallback"),
		LiveOutline.Status,
		EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback);
	const FBlueprintLensLC2GuardSurfaceProjection LiveGuardSurface =
		FBlueprintLensLC2GuardSurfaceProjector::Build(
			AcceptedLiveExplanation,
			LiveOutline);
	TestFalse(
		TEXT("the group-free live Explanation does not masquerade as the authored R1 guard surface"),
		LiveGuardSurface.IsRenderable());
	const FBlueprintLensCompositeRailSlots UngroupedSlots =
		FBlueprintLensLC2StationAppearanceProjector::Apply(
			AcceptedLiveExplanation,
			LiveGuardSurface,
			BaseSlots);
	TestFalse(
		TEXT("LC2 station appearance fails closed instead of using a second guard renderer"),
		UngroupedSlots.HasGuardStations());
	const FBlueprintLensLC2LiveExplanationAdapterResult AdaptedLive =
		FBlueprintLensLC2LiveExplanationAdapter::Build(
			AcceptedLiveExplanation);
	TestTrue(
		*FString::Printf(
			TEXT("the live relation ledger adapts into the accepted LC2 projector: %s"),
			*AdaptedLive.DiagnosticCode),
		AdaptedLive.IsSuccess());
	TestEqual(
		TEXT("the LC2 adapter retains the current Blueprint source"),
		AdaptedLive.Explanation.Source.BlueprintAssetPath,
		CurrentBlueprint);

	FBlueprintLensExplanationModel LargerLiveExplanation =
		AcceptedLiveExplanation;
	FBlueprintLensUnit ExtraPureUnit;
	ExtraPureUnit.Id = TEXT("unit.value.m10.adapter.extra-pure");
	ExtraPureUnit.Role = EBlueprintLensRole::Value;
	ExtraPureUnit.Kind = EBlueprintLensUnitKind::Expression;
	ExtraPureUnit.Title = TEXT("Unrelated pure value");
	ExtraPureUnit.Expression = TEXT("M10_EXTRA_PURE_VALUE");
	LargerLiveExplanation.Units.Add(ExtraPureUnit);
	LargerLiveExplanation.Counts.Units = LargerLiveExplanation.Units.Num();
	for (FBlueprintLensLane& Lane : LargerLiveExplanation.Lanes)
	{
		if (Lane.Role == EBlueprintLensRole::Value)
		{
			Lane.State = EBlueprintLensLaneState::Populated;
			Lane.UnitIds.Add(ExtraPureUnit.Id);
			Lane.EmptyMessage.Reset();
			break;
		}
	}
	const FBlueprintLensLC2LiveExplanationAdapterResult AdaptedLargerLive =
		FBlueprintLensLC2LiveExplanationAdapter::Build(
			LargerLiveExplanation);
	TestTrue(
		*FString::Printf(
			TEXT("a structurally valid LC2 core is admitted inside a non-9/10 live Explanation: %s"),
			*AdaptedLargerLive.DiagnosticCode),
		AdaptedLargerLive.IsSuccess());
	if (AdaptedLargerLive.IsSuccess())
	{
		TestEqual(
			TEXT("the adapter extracts the two-guard core for the accepted D2 projector"),
			AdaptedLargerLive.Explanation.Units.Num(),
			9);
		TestNull(
			TEXT("the unrelated pure unit remains outside the adapted LC2 core"),
			AdaptedLargerLive.Explanation.FindUnit(ExtraPureUnit.Id));
	}
	const FBlueprintLensLC2GuardOutlineProjection AdaptedOutline =
		FBlueprintLensLC2GuardOutlineProjector::Build(
			AdaptedLive.Explanation);
	const FBlueprintLensLC2GuardSurfaceProjection AdaptedGuardSurface =
		FBlueprintLensLC2GuardSurfaceProjector::Build(
			AdaptedLive.Explanation,
			AdaptedOutline);
	TestTrue(
		*FString::Printf(
			TEXT("the original LC2 guard-surface projector accepts adapted live truth: %s"),
			*AdaptedGuardSurface.DiagnosticCode),
		AdaptedGuardSurface.IsRenderable());
	const FBlueprintLensCompositeRailSlots GuardSlots =
		FBlueprintLensLC2StationAppearanceProjector::Apply(
			AdaptedLive.Explanation,
			AdaptedGuardSurface,
			BaseSlots);
	TestTrue(
		TEXT("LC2 fills at least one live station-appearance slot"),
		GuardSlots.HasGuardStations());
	TestTrue(
		TEXT("the incomparable-boundary RED reaches the accepted three-pair partial order"),
		AdaptedLive.Explanation.bHasGroupPartialOrder &&
			AdaptedLive.Explanation.GroupPartialOrder.IncomparableGroupIds.Num() == 3 &&
			!AdaptedLive.Explanation.GroupPartialOrder.Semantics.IsEmpty());
	const FBlueprintLensLC1RailProjection PartialOrderRail =
		FBlueprintLensLC1RailProjector::Build(AdaptedLive.Explanation);
	TMap<FString, int32> LC2StationIndexByUnitId;
	for (int32 Index = 0;
		 Index < PartialOrderRail.OrderedCanonicalUnits.Num();
		 ++Index)
	{
		const FBlueprintLensLC1RailCanonicalUnit& Unit =
			PartialOrderRail.OrderedCanonicalUnits[Index];
		LC2StationIndexByUnitId.Add(Unit.UnitId, Index);
		AddInfo(FString::Printf(
			TEXT("M10_MEASUREMENT_LC2_STATION index=%d label=%s unit_id=%s"),
			Index,
			*Unit.DisplayLabel,
			*Unit.UnitId));
	}
	int32 LC2StrictStationRelations = 0;
	int32 LC2NonStationRelations = 0;
	for (const FBlueprintLensRelation& Relation :
		 AdaptedLive.Explanation.Relations)
	{
		const int32* SourceIndex =
			LC2StationIndexByUnitId.Find(Relation.SourceUnitId);
		const int32* TargetIndex =
			LC2StationIndexByUnitId.Find(Relation.TargetUnitId);
		if (SourceIndex == nullptr || TargetIndex == nullptr)
		{
			++LC2NonStationRelations;
			AddInfo(FString::Printf(
				TEXT("M10_MEASUREMENT_LC2_RELATION id=%s topology=not_applicable "
					"reason=non_station_endpoint source=%s target=%s"),
				*Relation.Id,
				*Relation.SourceUnitId,
				*Relation.TargetUnitId));
			continue;
		}
		const bool bInsideDeclaredScc =
			PartialOrderRail.OrderRegions.ContainsByPredicate(
				[&Relation](const FBlueprintLensLC1RailOrderRegion& Region)
				{
					return Region.Kind ==
							EBlueprintLensLC1RailOrderRegionKind::StronglyConnected &&
						Region.MemberUnitIds.Contains(Relation.SourceUnitId) &&
						Region.MemberUnitIds.Contains(Relation.TargetUnitId);
				});
		const bool bTopologyHolds = bInsideDeclaredScc ||
			*SourceIndex < *TargetIndex;
		LC2StrictStationRelations += bInsideDeclaredScc ? 0 : 1;
		TestTrue(
			*FString::Printf(
				TEXT("LC2 station order is topological for %s"),
				*Relation.Id),
			bTopologyHolds);
		AddInfo(FString::Printf(
			TEXT("M10_MEASUREMENT_LC2_RELATION id=%s source_index=%d "
				"target_index=%d topology=%s scc_exempt=%s"),
			*Relation.Id,
			*SourceIndex,
			*TargetIndex,
			bTopologyHolds ? TEXT("true") : TEXT("false"),
			bInsideDeclaredScc ? TEXT("true") : TEXT("false")));
	}
	TestEqual(
		TEXT("LC2 measures all eight station-to-station relations against strict topology"),
		LC2StrictStationRelations,
		8);
	TestEqual(
		TEXT("LC2 accounts for both predicate bindings as non-station order relations"),
		LC2NonStationRelations,
		2);
	const FBlueprintLensCompositeRailSlots PartialOrderBaseSlots =
		FBlueprintLensCompositeRailSlotProjector::Build(
			AdaptedLive.Explanation,
			PartialOrderRail);
	const FBlueprintLensCompositeRailSlots PartialOrderSlots =
		FBlueprintLensLC2StationAppearanceProjector::Apply(
			AdaptedLive.Explanation,
			AdaptedGuardSurface,
			PartialOrderBaseSlots);
	const FBlueprintLensLC1RailLayoutSessionResult PartialOrderSession =
		FBlueprintLensLC1RailLayoutSession::Build(
			PartialOrderRail,
			AdaptedLive.Explanation,
			700.0f);
	const TSharedRef<SBlueprintLensLC1RailCanvas> PartialOrderCanvas =
		SNew(SBlueprintLensLC1RailCanvas)
		.Projection(PartialOrderRail)
		.InitialSession(PartialOrderSession)
		.Explanation(MakeShared<FBlueprintLensExplanationModel>(
			AdaptedLive.Explanation))
		.CompositeSlots(PartialOrderSlots);
	TestTrue(
		TEXT("mutually exclusive outcome stations have a visible incomparable boundary on the spine"),
		SlateHasWidgetTag(
			PartialOrderCanvas,
			FName(TEXT("BlueprintLens.Automation.CompositeOrderBoundary.Incomparable"))));

	const FBlueprintLensLC2GuardCompound* SccOuterGuard =
		AdaptedGuardSurface.Compounds.FindByPredicate(
			[](const FBlueprintLensLC2GuardCompound& Compound)
			{
				return Compound.ParentGroupId.IsEmpty();
			});
	const FBlueprintLensLC2GuardCompound* SccInnerGuard =
		AdaptedGuardSurface.Compounds.FindByPredicate(
			[](const FBlueprintLensLC2GuardCompound& Compound)
			{
				return !Compound.ParentGroupId.IsEmpty();
			});
	TestNotNull(TEXT("the SCC RED resolves the outer guard station"), SccOuterGuard);
	TestNotNull(TEXT("the SCC RED resolves the inner guard station"), SccInnerGuard);
	if (SccOuterGuard != nullptr && SccInnerGuard != nullptr)
	{
		FBlueprintLensExplanationModel SccExplanation = AdaptedLive.Explanation;
		FBlueprintLensRelation ReturnRelation;
		ReturnRelation.Id = TEXT("relation.execution_predecessor.m10-scc-return");
		ReturnRelation.SourceUnitId = SccInnerGuard->BranchUnitId;
		ReturnRelation.TargetUnitId = SccOuterGuard->BranchUnitId;
		ReturnRelation.Kind =
			EBlueprintLensRelationKind::ExecutionPredecessor;
		ReturnRelation.Label = TEXT("return");
		SccExplanation.Relations.Add(ReturnRelation);
		SccExplanation.Counts.Relations = SccExplanation.Relations.Num();
		const FBlueprintLensLC1RailProjection SccRail =
			FBlueprintLensLC1RailProjector::Build(SccExplanation);
		TestTrue(
			*FString::Printf(
				TEXT("the SCC RED reaches a renderable cyclic station projection: %s"),
				*SccRail.DiagnosticCode),
			SccRail.IsRenderable());
		const FBlueprintLensCompositeRailSlots SccSlots =
			FBlueprintLensCompositeRailSlotProjector::Build(
				SccExplanation,
				SccRail);
		const FBlueprintLensLC1RailLayoutSessionResult SccSession =
			FBlueprintLensLC1RailLayoutSession::Build(
				SccRail,
				SccExplanation,
				700.0f);
		const TSharedRef<SBlueprintLensLC1RailCanvas> SccCanvas =
			SNew(SBlueprintLensLC1RailCanvas)
			.Projection(SccRail)
			.InitialSession(SccSession)
			.Explanation(MakeShared<FBlueprintLensExplanationModel>(
				SccExplanation))
			.CompositeSlots(SccSlots);
		TestTrue(
			TEXT("an accepted SCC station segment visibly declares its internal order boundary"),
			SlateHasWidgetTag(
				SccCanvas,
				FName(TEXT("BlueprintLens.Automation.CompositeOrderBoundary.SCC"))));
	}
	const FBlueprintLensCompositeStationSlot* NestedGuardSlot =
		GuardSlots.Stations.FindByPredicate(
			[](const FBlueprintLensCompositeStationSlot& Station)
			{
				return Station.Appearance.Kind ==
						EBlueprintLensCompositeStationAppearanceKind::Guard &&
					Station.Appearance.NestingDepth == 1;
			});
	TestNotNull(
		TEXT("the live station appearance retains the nested guard"),
		NestedGuardSlot);
	if (NestedGuardSlot != nullptr)
	{
		TestEqual(
			TEXT("the nested station names its current-Blueprint parent guard"),
			NestedGuardSlot->Appearance.ParentGuardReaderText,
			FString(TEXT("CURRENT BLUEPRINT GUARD")));
		TestEqual(
			TEXT("the nested station retains its local fork mark"),
			NestedGuardSlot->Appearance.ForkReaderText,
			FString(TEXT("NO ORDER PROVEN BETWEEN THESE EXITS")));
	}
	const FBlueprintLensLC1RailLayoutSessionResult LiveSession =
		FBlueprintLensLC1RailLayoutSession::Build(
			LiveRail,
			AcceptedLiveExplanation,
			700.0f);
	const FBlueprintLensLC1RailSurfaceLayout LiveSurface =
		FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
			LiveRail,
			LiveSession,
			GuardSlots,
			700.0f);
	TestTrue(
		*FString::Printf(
			TEXT("live composite surface is renderable: %s"),
			*LiveSurface.DiagnosticCode),
		LiveSurface.IsRenderable(LiveRail));
	const FBlueprintLensLC1RailSurfaceLabel* MixedScaleLabel =
		LiveSurface.Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("scale-rule");
			});
	const FBlueprintLensLC1RailSurfaceLabel* MixedStageLabel =
		LiveSurface.Labels.FindByPredicate(
			[](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("stage");
			});
	TestNotNull(TEXT("mixed rail emits one scale disclosure"), MixedScaleLabel);
	TestNotNull(TEXT("mixed rail emits one stage disclosure"), MixedStageLabel);
	if (MixedScaleLabel != nullptr)
	{
		TestFalse(
			TEXT("mixed scale disclosure does not call a deferred branch ledger a complete route"),
			MixedScaleLabel->Text.Contains(TEXT("complete route")));
		TestTrue(
			TEXT("mixed scale disclosure names rail-station folding at its own scale"),
			MixedScaleLabel->Text.Contains(TEXT("rail station")));
	}
	if (MixedStageLabel != nullptr)
	{
		TestFalse(
			TEXT("mixed stage disclosure does not call deterministic coverage a predecessor route"),
			MixedStageLabel->Text.Contains(TEXT("proven predecessor")));
		TestTrue(
			TEXT("mixed stage count uses the rail relation ledger"),
			MixedStageLabel->Text.Contains(FString::Printf(
				TEXT("%d rail relations"),
				LiveRail.AllRelationIds.Num())));
	}
	const FBlueprintLensLC1RailSurfaceLayout PlainSurface =
		FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
			LiveRail,
			LiveSession,
			BaseSlots,
			700.0f);
	TestEqual(
		TEXT("collapsed station appearances add no default rail height"),
		LiveSurface.CanvasSize.Y,
		PlainSurface.CanvasSize.Y);
	TestEqual(
		TEXT("the composite fold radius remains thirteen hops"),
		FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
		13);
	TestTrue(
		TEXT("all live guard attachments begin collapsed"),
		GuardSlots.AreAllAttachmentsCollapsed());

	FBlueprintLensCompositeRailSlots DisclosureSlots = GuardSlots;
	FBlueprintLensCompositeStationSlot* DisclosureOuterGuard =
		DisclosureSlots.Stations.FindByPredicate(
			[](const FBlueprintLensCompositeStationSlot& Station)
			{
				return Station.Appearance.Kind ==
						EBlueprintLensCompositeStationAppearanceKind::Guard &&
					Station.Appearance.NestingDepth == 0;
			});
	FBlueprintLensCompositeStationSlot* DisclosureNestedGuard =
		DisclosureSlots.Stations.FindByPredicate(
			[](const FBlueprintLensCompositeStationSlot& Station)
			{
				return Station.Appearance.Kind ==
						EBlueprintLensCompositeStationAppearanceKind::Guard &&
					Station.Appearance.NestingDepth == 1;
			});
	const FString OuterMarkerSentinel = TEXT("M10_OUTER_MARKER_SENTINEL");
	const FString NestedMarkerSentinel = TEXT("M10_NESTED_MARKER_SENTINEL");
	TestNotNull(
		TEXT("the disclosure RED plants data into the outer guard station"),
		DisclosureOuterGuard);
	TestNotNull(
		TEXT("the disclosure RED plants data into a nested guard station"),
		DisclosureNestedGuard);
	if (DisclosureOuterGuard != nullptr)
	{
		DisclosureOuterGuard->Appearance.MarkerText = OuterMarkerSentinel;
	}
	if (DisclosureNestedGuard != nullptr)
	{
		DisclosureNestedGuard->Appearance.MarkerText = NestedMarkerSentinel;
	}
	const TSharedRef<SBlueprintLensLC1RailCanvas> DisclosureCanvas =
		SNew(SBlueprintLensLC1RailCanvas)
		.Projection(LiveRail)
		.InitialSession(LiveSession)
		.Explanation(MakeShared<FBlueprintLensExplanationModel>(
			AcceptedLiveExplanation))
		.CompositeSlots(DisclosureSlots);
	const TArray<TSharedRef<SWidget>> VisibleDisclosures = SlateWidgetsWithTag(
		DisclosureCanvas,
		FName(TEXT("BlueprintLens.Automation.CompositeGuardDisclosure")));
	TestEqual(
		TEXT("every clickable collapsed guard station exposes a separately tagged visible affordance"),
		VisibleDisclosures.Num(),
		2);
	FString VisibleDisclosureText;
	for (const TSharedRef<SWidget>& VisibleDisclosure : VisibleDisclosures)
	{
		TestEqual(
			TEXT("the visible collapsed disclosure is hit-test visible"),
			VisibleDisclosure->GetVisibility(),
			EVisibility::HitTestInvisible);
		VisibleDisclosureText += SlateWidgetText(VisibleDisclosure);
		VisibleDisclosureText += TEXT("\n");
	}
	TestTrue(
		TEXT("guard affordances use each guard's reader data"),
		VisibleDisclosureText.Contains(TEXT("CURRENT BLUEPRINT GUARD")) &&
			VisibleDisclosureText.Contains(TEXT("InnerEnabled")));
	TestFalse(
		TEXT("guard affordances do not parse their reader data back out of marker captions"),
		VisibleDisclosureText.Contains(OuterMarkerSentinel) ||
			VisibleDisclosureText.Contains(NestedMarkerSentinel));

	TSharedRef<SBlueprintLensPanel> ScopePanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	ScopePanel->M6Presentation.SetPythonReady(true);
	ScopePanel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
	ScopePanel->M6Presentation.SetGraphId(LargerLiveExplanation.Source.GraphId);
	ScopePanel->M6Presentation.ApplySession(Ready, &Views);
	FM6LoadedSessionPacket ScopePacket;
	ScopePacket.Explanation = LargerLiveExplanation;
	ScopePacket.SemanticSha256 = FString::ChrN(64, TEXT('b'));
	ScopePanel->M6ReadyPacket =
		MakeShared<FM6LoadedSessionPacket>(MoveTemp(ScopePacket));
	if (SccOuterGuard != nullptr)
	{
		ScopePanel->ToggleM6CompositeDisclosure(SccOuterGuard->BranchUnitId);
	}
	const TSharedRef<SWidget> ExpandedScope =
		ScopePanel->BuildM6CausalContent();
	const TSharedPtr<SWidget> VisibleScopeDisclosure = SlateWidgetWithTag(
		ExpandedScope,
		FName(TEXT("BlueprintLens.Automation.CompositeGuardCoreScopeDisclosure")));
	TestTrue(
		TEXT("an expanded extracted guard core visibly discloses its omitted source units"),
		VisibleScopeDisclosure.IsValid());
	if (VisibleScopeDisclosure.IsValid())
	{
		const FString ScopeText =
			SlateWidgetText(VisibleScopeDisclosure.ToSharedRef());
		TestTrue(
			TEXT("the scope disclosure carries the seeded input and adapted unit counts"),
			ScopeText.Contains(FString::FromInt(
				LargerLiveExplanation.Units.Num())) &&
				ScopeText.Contains(FString::FromInt(
					AdaptedLargerLive.Explanation.Units.Num())));
	}

	const TSharedRef<SWidget> Composite = Panel->BuildM6CausalContent();
	const FString CompositeText = SlateWidgetText(Composite);
	TestTrue(
		TEXT("the live composite rail canvas exists"),
		Panel->LC1RailCanvas.IsValid());
	if (Panel->LC1RailCanvas.IsValid())
	{
		TestEqual(
			TEXT("the Explanation handed to the composite projector names the current Blueprint"),
			Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
				.SourceBlueprintAssetPath,
			CurrentBlueprint);
	}
	TestTrue(
		TEXT("the live LC2 result uses the composite guard rail"),
		SlateHasWidgetTag(
			Composite,
			FName(TEXT("BlueprintLens.Automation.CompositeGuardRail"))));
	TestTrue(
		TEXT("a predicate branch is rendered as a guard station from the live Explanation"),
		CompositeText.Contains(TEXT("GUARD GATE · CURRENT BLUEPRINT GUARD")));
	TestFalse(
		TEXT("the constructor fixture cannot supply the live guard station"),
		CompositeText.Contains(TEXT("GUARD GATE · STALE CONSTRUCTION FIXTURE GUARD")));
	TestFalse(
		TEXT("a mixed branch caption does not promise a predecessor chain"),
		CompositeText.Contains(TEXT("proven predecessor chain")));
	TestTrue(
		TEXT("the mixed rail still discloses facts retained outside its station ledger"),
		CompositeText.Contains(TEXT("RETAINED OUTSIDE THE EXECUTION RAIL")));
	TestTrue(
		TEXT("attachments advertise their collapsed default state"),
		SlateHasWidgetTag(
			Composite,
			FName(TEXT("BlueprintLens.Automation.CompositeAttachmentsCollapsed"))));
	TestFalse(
		TEXT("collapsed guard stations do not expand outcome-path detail by default"),
		CompositeText.Contains(TEXT("Both guards passed")) ||
			CompositeText.Contains(TEXT("InnerEnabled was false")) ||
			CompositeText.Contains(TEXT("OuterEnabled was false")));
	const FBlueprintLensCompositeStationSlot* FirstGuard =
		GuardSlots.Stations.FindByPredicate(
			[](const FBlueprintLensCompositeStationSlot& Station)
			{
				return Station.Appearance.Kind ==
						EBlueprintLensCompositeStationAppearanceKind::Guard &&
					Station.Appearance.ParentGroupId.IsEmpty();
			});
	TestNotNull(TEXT("the live composition has a guard disclosure target"), FirstGuard);
	if (FirstGuard != nullptr)
	{
		Panel->ToggleM6CompositeDisclosure(FirstGuard->UnitId);
		const TSharedRef<SWidget> Expanded = Panel->BuildM6CausalContent();
		TestTrue(
			TEXT("a guard attachment opens on demand"),
			SlateHasWidgetTag(
				Expanded,
				FName(TEXT("BlueprintLens.Automation.CompositeLC2GuardSurface"))));
		const FString ExpandedText = SlateWidgetText(Expanded);
		TestTrue(
			TEXT("the expanded station reuses the original nested Guard Gate canvas"),
			ExpandedText.Contains(TEXT("GUARD GATE · CURRENT BLUEPRINT GUARD")) &&
				ExpandedText.Contains(TEXT("GUARD GATE · InnerEnabled")) &&
				ExpandedText.Contains(TEXT("Set OuterRejected")) &&
				ExpandedText.Contains(TEXT("Set InnerRejected")) &&
				ExpandedText.Contains(TEXT("Set Accepted")));
		TestFalse(
			TEXT("expanded content no longer advertises the all-collapsed state"),
			SlateHasWidgetTag(
				Expanded,
				FName(TEXT("BlueprintLens.Automation.CompositeAttachmentsCollapsed"))));
		Panel->ToggleM6CompositeDisclosure(FirstGuard->UnitId);
		const TSharedRef<SWidget> CollapsedAgain = Panel->BuildM6CausalContent();
		TestFalse(
			TEXT("the same guard attachment has a way back to collapsed"),
			SlateHasWidgetTag(
				CollapsedAgain,
				FName(TEXT("BlueprintLens.Automation.CompositeLC2GuardSurface"))));
		if (NestedGuardSlot != nullptr)
		{
			Panel->ToggleM6CompositeDisclosure(NestedGuardSlot->UnitId);
			const TSharedRef<SWidget> ExpandedFromNested =
				Panel->BuildM6CausalContent();
			TestTrue(
				TEXT("either nested guard marker opens the one reused LC2 surface"),
				SlateHasWidgetTag(
					ExpandedFromNested,
					FName(TEXT("BlueprintLens.Automation.CompositeLC2GuardSurface"))));
			Panel->ToggleM6CompositeDisclosure(NestedGuardSlot->UnitId);
		}
	}

	const FString SessionText =
		SlateWidgetText(Panel->BuildM6SessionContent());
	TestTrue(
		TEXT("the showing header uses the same display label as the live rail"),
		SessionText.Contains(FString::Printf(
			TEXT("showing: Execution query · %s"),
			*LiveRail.CriterionDisplayLabel)));
	TestTrue(
		TEXT("the selected header uses the same display label as the live rail"),
		SessionText.Contains(FString::Printf(
			TEXT("selected: %s"),
			*LiveRail.CriterionDisplayLabel)));
	TestTrue(
		TEXT("the Target row uses the same display label as the live rail"),
		SessionText.Contains(FString::Printf(
			TEXT("Execution node · %s"),
			*LiveRail.CriterionDisplayLabel)));
	TestFalse(
		TEXT("the native localized fixture label is not duplicated beside the projected display label"),
		SessionText.Contains(NativeLabelSentinel));

	const FBlueprintLensLoadResult MotifLoad =
		FBlueprintLensExplanationLoader::LoadFile(
			M7MotifScaleExplanationPath());
	AddInfo(FString::Printf(
		TEXT("M10_MEASUREMENT_MOTIF_LOAD path=%s error=%s"),
		*M7MotifScaleExplanationPath(),
		*MotifLoad.Error));
	TestTrue(
		TEXT("the retained 44-unit MotifScale Explanation loads for measurement"),
		MotifLoad.IsSuccess());
	if (MotifLoad.IsSuccess())
	{
		const FBlueprintLensLC1RailProjection MotifRail =
			FBlueprintLensLC1RailProjector::Build(*MotifLoad.Model);
		TMap<FString, int32> MotifStationIndexByUnitId;
		for (int32 Index = 0;
			 Index < MotifRail.OrderedCanonicalUnits.Num();
			 ++Index)
		{
			MotifStationIndexByUnitId.Add(
				MotifRail.OrderedCanonicalUnits[Index].UnitId,
				Index);
		}
		int32 MotifTopologyViolations = 0;
		for (const FBlueprintLensLC1RailExecutionRelation& Relation :
			 MotifRail.StationOrderRelations)
		{
			const int32 SourceIndex =
				MotifStationIndexByUnitId.FindChecked(Relation.SourceUnitId);
			const int32 TargetIndex =
				MotifStationIndexByUnitId.FindChecked(Relation.TargetUnitId);
			const bool bInsideDeclaredScc =
				MotifRail.OrderRegions.ContainsByPredicate(
					[&Relation](const FBlueprintLensLC1RailOrderRegion& Region)
					{
						return Region.Kind ==
								EBlueprintLensLC1RailOrderRegionKind::StronglyConnected &&
							Region.MemberUnitIds.Contains(Relation.SourceUnitId) &&
							Region.MemberUnitIds.Contains(Relation.TargetUnitId);
					});
			MotifTopologyViolations +=
				!bInsideDeclaredScc && SourceIndex >= TargetIndex ? 1 : 0;
		}
		const int32 MotifSccSegments =
			MotifRail.OrderRegions.FilterByPredicate(
				[](const FBlueprintLensLC1RailOrderRegion& Region)
				{
					return Region.Kind ==
						EBlueprintLensLC1RailOrderRegionKind::StronglyConnected;
				}).Num();
		const int32 MotifIncomparableSegments =
			MotifRail.OrderRegions.FilterByPredicate(
				[](const FBlueprintLensLC1RailOrderRegion& Region)
				{
					return Region.Kind ==
						EBlueprintLensLC1RailOrderRegionKind::Incomparable;
				}).Num();
		AddInfo(FString::Printf(
			TEXT("M10_MEASUREMENT_MOTIF_ORDER stations=%d "
				"station_relations=%d topology_violations=%d scc_segments=%d "
				"incomparable_segments=%d"),
			MotifRail.OrderedCanonicalUnits.Num(),
			MotifRail.StationOrderRelations.Num(),
			MotifTopologyViolations,
			MotifSccSegments,
			MotifIncomparableSegments));
		TestEqual(
			TEXT("MotifScale station order has no inter-SCC topology violation"),
			MotifTopologyViolations,
			0);
		const FBlueprintLensCompositeRailSlots MotifSlots =
			FBlueprintLensCompositeRailSlotProjector::Build(
				*MotifLoad.Model,
				MotifRail);
		const FBlueprintLensLC1RailLayoutSessionResult MotifSession =
			FBlueprintLensLC1RailLayoutSession::Build(
				MotifRail,
				*MotifLoad.Model,
				700.0f);
		const FBlueprintLensLC1RailSurfaceLayout MotifSurface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				MotifRail,
				MotifSession,
				MotifSlots,
				700.0f);
		TestTrue(
			TEXT("the 44-unit MotifScale measurement reaches the composite surface"),
			MotifRail.IsRenderable() &&
				MotifSlots.IsRenderable(MotifRail) &&
				MotifSurface.IsRenderable(MotifRail));
		if (MotifSurface.IsRenderable(MotifRail) &&
			LiveSurface.SpineRoute.Num() >= 2 &&
			MotifSurface.SpineRoute.Num() >= 2)
		{
			const float LC2SpineHeight =
				LiveSurface.SpineRoute.Last().Y -
				LiveSurface.SpineRoute[0].Y;
			const float MotifSpineHeight =
				MotifSurface.SpineRoute.Last().Y -
				MotifSurface.SpineRoute[0].Y;
			AddInfo(FString::Printf(
				TEXT("M10_MEASUREMENT_SPINE_HEIGHT LC2_units=%d LC2_rail_stations=%d "
					"LC2_spine_height=%.3f Motif_units=%d Motif_rail_stations=%d "
					"Motif_drawn_stations=%d Motif_spine_height=%.3f"),
				AcceptedLiveExplanation.Units.Num(),
				LiveRail.AllUnitIds.Num(),
				LC2SpineHeight,
				MotifLoad.Model->Units.Num(),
				MotifRail.AllUnitIds.Num(),
				MotifSurface.Radius.DrawnUnitIds.Num(),
				MotifSpineHeight));
		}
		TestEqual(
			TEXT("the default surface executes radius thirteen"),
			MotifSurface.Radius.CurrentRadius,
			FBlueprintLensCompositeRailSlots::DefaultFoldRadius);
		TestEqual(
			TEXT("radius thirteen draws the criterion plus thirteen earlier stations"),
			MotifSurface.Radius.DrawnUnitIds.Num(),
			FBlueprintLensCompositeRailSlots::DefaultFoldRadius + 1);
		TestTrue(
			TEXT("radius thirteen actually produces a counted fold on MotifScale"),
			!MotifSurface.Radius.FoldedUnitIds.IsEmpty() &&
				MotifSurface.Radius.FoldBoundaryBounds.bIsValid &&
				!MotifSlots.Spans.IsEmpty());
		const int32 RequestVisibleEntityBudget =
			FM6QueryInput().MaxVisibleEntities;
		const bool bVisibleBudgetAbsorbedByRadius =
			MotifSurface.Radius.DrawnUnitIds.Num() <=
				RequestVisibleEntityBudget;
		AddInfo(FString::Printf(
			TEXT("M10_MEASUREMENT_VISIBLE_BUDGET request_max_visible=%d "
				"station_slots=%d drawn_stations=%d absorbed_by_radius=%s"),
			RequestVisibleEntityBudget,
			MotifSlots.Stations.Num(),
			MotifSurface.Radius.DrawnUnitIds.Num(),
			bVisibleBudgetAbsorbedByRadius ? TEXT("true") : TEXT("false")));
		TestTrue(
			TEXT("the budget measurement exercises a retained station ledger larger than the request display budget"),
			MotifSlots.Stations.Num() >
				RequestVisibleEntityBudget);
		TestTrue(
			TEXT("the default radius keeps actually drawn rail stations inside the request display budget"),
			bVisibleBudgetAbsorbedByRadius);
	}

	const FBlueprintLensLoadResult LC3Load =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("the LC3 value-provenance Explanation loads"), LC3Load.IsSuccess());
	{
		const FString TypedIrJson =
			TEXT("{\"blueprint\":{\"graphs\":[{\"id\":\"graph.live.sequence\",")
			TEXT("\"nodes\":[")
			TEXT("{\"id\":\"node.event\",\"class\":\"/Script/BlueprintGraph.K2Node_Event\",\"title\":\"Current live event\",\"pins\":[{\"id\":\"pin.event.then\",\"name\":\"then\",\"pin_role\":\"none\",\"direction\":\"output\",\"kind\":\"execution\"}]} ,")
			TEXT("{\"id\":\"node.sequence.a\",\"class\":\"/Script/BlueprintGraph.K2Node_ExecutionSequence\",\"title\":\"CURRENT LIVE SEQUENCE A\",\"pins\":[{\"id\":\"pin.a.in\",\"name\":\"execute\",\"pin_role\":\"none\",\"direction\":\"input\",\"kind\":\"execution\"},{\"id\":\"pin.a.0\",\"name\":\"then_0\",\"pin_role\":\"none\",\"direction\":\"output\",\"kind\":\"execution\"},{\"id\":\"pin.a.1\",\"name\":\"then_1\",\"pin_role\":\"none\",\"direction\":\"output\",\"kind\":\"execution\"}]} ,")
			TEXT("{\"id\":\"node.sequence.b\",\"class\":\"/Script/BlueprintGraph.K2Node_ExecutionSequence\",\"title\":\"CURRENT LIVE SEQUENCE B\",\"pins\":[{\"id\":\"pin.b.in\",\"name\":\"execute\",\"pin_role\":\"none\",\"direction\":\"input\",\"kind\":\"execution\"},{\"id\":\"pin.b.0\",\"name\":\"then_0\",\"pin_role\":\"none\",\"direction\":\"output\",\"kind\":\"execution\"},{\"id\":\"pin.b.1\",\"name\":\"then_1\",\"pin_role\":\"none\",\"direction\":\"output\",\"kind\":\"execution\"}]} ,")
			TEXT("{\"id\":\"node.criterion\",\"class\":\"/Script/BlueprintGraph.K2Node_VariableSet\",\"title\":\"CURRENT LIVE CRITERION\",\"pins\":[{\"id\":\"pin.criterion.in\",\"name\":\"execute\",\"pin_role\":\"none\",\"direction\":\"input\",\"kind\":\"execution\"}]} ,")
			TEXT("{\"id\":\"node.outside\",\"class\":\"/Script/BlueprintGraph.K2Node_CallFunction\",\"title\":\"CONNECTED OUTSIDE SENTINEL\",\"pins\":[{\"id\":\"pin.outside.in\",\"name\":\"execute\",\"pin_role\":\"none\",\"direction\":\"input\",\"kind\":\"execution\"}]}" )
			TEXT("],\"edges\":[")
			TEXT("{\"id\":\"edge.event.a\",\"kind\":\"execution\",\"direction_is_valid\":true,\"source_node_id\":\"node.event\",\"source_pin_id\":\"pin.event.then\",\"target_node_id\":\"node.sequence.a\",\"target_pin_id\":\"pin.a.in\"},")
			TEXT("{\"id\":\"edge.a.selected\",\"kind\":\"execution\",\"direction_is_valid\":true,\"source_node_id\":\"node.sequence.a\",\"source_pin_id\":\"pin.a.0\",\"target_node_id\":\"node.sequence.b\",\"target_pin_id\":\"pin.b.in\"},")
			TEXT("{\"id\":\"edge.a.outside\",\"kind\":\"execution\",\"direction_is_valid\":true,\"source_node_id\":\"node.sequence.a\",\"source_pin_id\":\"pin.a.1\",\"target_node_id\":\"node.outside\",\"target_pin_id\":\"pin.outside.in\"},")
			TEXT("{\"id\":\"edge.b.selected\",\"kind\":\"execution\",\"direction_is_valid\":true,\"source_node_id\":\"node.sequence.b\",\"source_pin_id\":\"pin.b.1\",\"target_node_id\":\"node.criterion\",\"target_pin_id\":\"pin.criterion.in\"}")
			TEXT("]}]}}");
		const FString TypedIrSha256 = Sha256Text(TypedIrJson);
		const FString LC4TempRoot = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens/M10LC4SequenceRed"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		IFileManager::Get().MakeDirectory(*LC4TempRoot, true);
		const FString TypedIrPath = FPaths::Combine(
			LC4TempRoot, TEXT("typed-source.json"));
		TestTrue(
			TEXT("the live LC4-SEQ RED writes its full-graph typed-IR sidecar"),
			!TypedIrSha256.IsEmpty() && FFileHelper::SaveStringToFile(
				TypedIrJson,
				*TypedIrPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		FBlueprintLensExplanationModel LiveLC4;
		LiveLC4.Format = TEXT("blueprint-lens-explanation");
		LiveLC4.SchemaVersion = TEXT("1.0.0");
		LiveLC4.RulesVersion = TEXT("1.0.0");
		LiveLC4.Source.IrPath = TypedIrPath;
		LiveLC4.Source.IrSha256 = TypedIrSha256;
		LiveLC4.Source.BlueprintAssetPath =
			TEXT("/Game/M7Corpus/BP_LiveSequenceRED.BP_LiveSequenceRED");
		LiveLC4.Source.BlueprintPackageSha256 = FString::ChrN(64, TEXT('8'));
		LiveLC4.Source.GraphId = TEXT("graph.live.sequence");
		LiveLC4.Query.Question = TEXT("Why does CURRENT LIVE CRITERION execute?");
		LiveLC4.Query.Direction = TEXT("backward_only");
		LiveLC4.Query.CriterionSourceNodeId = TEXT("node.criterion");
		auto AddLiveLC4Unit = [&LiveLC4](
			const TCHAR* UnitId,
			const TCHAR* SourceNodeId,
			const TCHAR* Title,
			const EBlueprintLensRole Role)
		{
			FBlueprintLensUnit Unit;
			Unit.Id = UnitId;
			Unit.Role = Role;
			Unit.Kind = EBlueprintLensUnitKind::Node;
			Unit.Title = Title;
			Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
			Unit.InclusionReasons = {
				Role == EBlueprintLensRole::Criterion
					? TEXT("criterion")
					: TEXT("execution_predecessor")};
			FBlueprintLensSourceReference Source;
			Source.BlueprintAssetPath = LiveLC4.Source.BlueprintAssetPath;
			Source.GraphId = LiveLC4.Source.GraphId;
			Source.SourceNodeId = SourceNodeId;
			Source.NativeNodeGuid = SourceNodeId;
			Source.bPrimary = true;
			Unit.SourceReferences.Add(MoveTemp(Source));
			LiveLC4.Units.Add(MoveTemp(Unit));
		};
		AddLiveLC4Unit(
			TEXT("unit.live.event"), TEXT("node.event"),
			TEXT("Current live event"), EBlueprintLensRole::Control);
		AddLiveLC4Unit(
			TEXT("unit.live.sequence.a"), TEXT("node.sequence.a"),
			TEXT("CURRENT LIVE SEQUENCE A"), EBlueprintLensRole::Control);
		AddLiveLC4Unit(
			TEXT("unit.live.sequence.b"), TEXT("node.sequence.b"),
			TEXT("CURRENT LIVE SEQUENCE B"), EBlueprintLensRole::Control);
		AddLiveLC4Unit(
			TEXT("unit.live.criterion"), TEXT("node.criterion"),
			TEXT("CURRENT LIVE CRITERION"), EBlueprintLensRole::Criterion);
		LiveLC4.CriterionUnitId = TEXT("unit.live.criterion");
		auto AddLiveLC4Relation = [&LiveLC4](
			const TCHAR* RelationId,
			const TCHAR* SourceUnitId,
			const TCHAR* TargetUnitId,
			const TCHAR* PortLabel)
		{
			FBlueprintLensRelation Relation;
			Relation.Id = RelationId;
			Relation.SourceUnitId = SourceUnitId;
			Relation.TargetUnitId = TargetUnitId;
			Relation.Kind = EBlueprintLensRelationKind::ExecutionPredecessor;
			Relation.Label = PortLabel;
			Relation.SourceEdgeIds = {RelationId};
			Relation.bHasPortLabel = PortLabel[0] != TEXT('\0');
			Relation.PortLabel = PortLabel;
			Relation.bHasSemanticLabel = true;
			Relation.SemanticLabel = EBlueprintLensSemanticLabel::NextExecution;
			LiveLC4.Relations.Add(MoveTemp(Relation));
		};
		AddLiveLC4Relation(
			TEXT("edge.event.a"), TEXT("unit.live.event"),
			TEXT("unit.live.sequence.a"), TEXT(""));
		AddLiveLC4Relation(
			TEXT("edge.a.selected"), TEXT("unit.live.sequence.a"),
			TEXT("unit.live.sequence.b"), TEXT("then_0"));
		AddLiveLC4Relation(
			TEXT("edge.b.selected"), TEXT("unit.live.sequence.b"),
			TEXT("unit.live.criterion"), TEXT("then_1"));
		LiveLC4.Counts.Units = LiveLC4.Units.Num();
		LiveLC4.Counts.Relations = LiveLC4.Relations.Num();
		const FBlueprintLensLC1TypedIrFacts TwoOutputFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(
				LiveLC4.Source,
				false);
		TestTrue(
			TEXT("the LC4-SEQ bound regression exercises the generic typed-IR fact loader"),
			TwoOutputFacts.IsValid());
		FBlueprintLensLC1TypedIrFacts FourOutputFacts = TwoOutputFacts;
		FBlueprintLensLC1NodeFact* FourOutputSequence =
			FourOutputFacts.NodesBySourceNodeId.Find(TEXT("node.sequence.a"));
		TestNotNull(
			TEXT("the bound regression resolves the planted live Sequence root"),
			FourOutputSequence);
		if (FourOutputSequence != nullptr)
		{
			FourOutputSequence->Pins.Add({
				TEXT("pin.a.2"), TEXT("then_2"),
				TEXT("output"), TEXT("execution")});
			FourOutputSequence->Pins.Add({
				TEXT("pin.a.3"), TEXT("then_3"),
				TEXT("output"), TEXT("execution")});
		}
		const FBlueprintLensLC4SequenceLiveAdapterResult FourOutputAdapted =
			FBlueprintLensLC4SequenceLiveAdapter::Build(
				LiveLC4,
				FourOutputFacts);
		TestTrue(
			TEXT("a live Sequence root at the four-output upper bound is admitted"),
			FourOutputAdapted.Cases.ContainsByPredicate(
				[](const FBlueprintLensLC4SequenceLiveCase& Candidate)
				{
					return Candidate.SequenceUnitId ==
						TEXT("unit.live.sequence.a") &&
						Candidate.Profile.Outputs.Num() == 4;
				}));
		FBlueprintLensLC1TypedIrFacts FiveOutputFacts = FourOutputFacts;
		if (FBlueprintLensLC1NodeFact* FiveOutputSequence =
			FiveOutputFacts.NodesBySourceNodeId.Find(TEXT("node.sequence.a")))
		{
			FiveOutputSequence->Pins.Add({
				TEXT("pin.a.4"), TEXT("then_4"),
				TEXT("output"), TEXT("execution")});
		}
		const FBlueprintLensLC4SequenceLiveAdapterResult FiveOutputAdapted =
			FBlueprintLensLC4SequenceLiveAdapter::Build(
				LiveLC4,
				FiveOutputFacts);
		TestTrue(
			TEXT("the live adapter executes a strictly-greater-than-four rejection at five outputs"),
			FiveOutputAdapted.RejectedSequenceRootCount > 0 &&
				!FiveOutputAdapted.Cases.ContainsByPredicate(
					[](const FBlueprintLensLC4SequenceLiveCase& Candidate)
					{
						return Candidate.SequenceUnitId ==
							TEXT("unit.live.sequence.a");
					}));

		FBlueprintLensExplanationModel StaleLC4 = LiveLC4;
		StaleLC4.Source.BlueprintAssetPath =
			TEXT("/Game/LensCorpus/BP_ConstructionFixture.BP_ConstructionFixture");
		for (FBlueprintLensUnit& Unit : StaleLC4.Units)
		{
			if (Unit.Id == TEXT("unit.live.sequence.a"))
			{
				Unit.Title = TEXT("STALE CONSTRUCTION SEQUENCE A");
			}
		}

		TSharedRef<SBlueprintLensPanel> LC4Panel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		LC4Panel->Model =
			MakeShared<FBlueprintLensExplanationModel>(MoveTemp(StaleLC4));
		LC4Panel->M6Presentation.SetPythonReady(true);
		LC4Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
		LC4Panel->M6Presentation.SetGraphId(LiveLC4.Source.GraphId);
		LC4Panel->M6Presentation.ApplySession(Ready, &Views);
		FM6LoadedSessionPacket LC4Packet;
		LC4Packet.Request.QueryKind = TEXT("execution");
		LC4Packet.Explanation = LiveLC4;
		LC4Packet.SemanticSha256 = FString::ChrN(64, TEXT('4'));
		LC4Panel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(LC4Packet));

		const TSharedRef<SWidget> CollapsedLC4 =
			LC4Panel->BuildM6CausalContent();
		const TArray<TSharedRef<SWidget>> LC4Actions = SlateWidgetsWithTag(
			CollapsedLC4,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC4SequenceDisclosureAction")));
		TestEqual(
			TEXT("both live then_N relations create visible default-collapsed between-station LC4-SEQ actions"),
			LC4Actions.Num(),
			2);
		TestTrue(
			TEXT("default-collapsed LC4-SEQ decorations preserve the composite collapsed invariant"),
			LC4Panel->LC1RailCanvas.IsValid() &&
				LC4Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
					.AreAllAttachmentsCollapsed());

		auto OpenLC4Action = [&LC4Panel, &LC4Actions](const int32 Index)
			-> TSharedRef<SWidget>
		{
			if (LC4Actions.IsValidIndex(Index) &&
				LC4Actions[Index]->GetTypeAsString() == TEXT("SButton"))
			{
				StaticCastSharedRef<SButton>(LC4Actions[Index])->SimulateClick();
			}
			return LC4Panel->BuildM6CausalContent();
		};
		const TSharedRef<SWidget> ExpandedConnectedOutside = OpenLC4Action(0);
		const TSharedPtr<SWidget> AcceptedLiveRail = SlateWidgetWithTag(
			ExpandedConnectedOutside,
			FName(TEXT("BlueprintLens.Automation.CompositeLC4SequenceSurface")));
		TestTrue(
			TEXT("a non-fixture live Blueprint reaches the accepted LC4-SEQ Disclosure Rail widget"),
			AcceptedLiveRail.IsValid() &&
				AcceptedLiveRail->GetTypeAsString() ==
					TEXT("SBlueprintLensLC4SequenceRail"));
		if (AcceptedLiveRail.IsValid())
		{
			const FString LiveRailText =
				SlateWidgetText(AcceptedLiveRail.ToSharedRef());
			TestTrue(
				TEXT("the LC4-SEQ chain receives the current packet's planted Sequence identity"),
				LiveRailText.Contains(TEXT("CURRENT LIVE SEQUENCE A")));
			TestFalse(
				TEXT("the LC4-SEQ chain never receives the construction fixture's planted Sequence identity"),
				LiveRailText.Contains(TEXT("STALE CONSTRUCTION SEQUENCE A")));
			TestTrue(
				TEXT("the accepted rail displays the declared ordinal order from the live Sequence root"),
				LiveRailText.Find(TEXT("then_0")) != INDEX_NONE &&
					LiveRailText.Find(TEXT("then_1")) >
						LiveRailText.Find(TEXT("then_0")));
		}
		const TArray<TSharedRef<SWidget>> ConnectedExclusions =
			SlateWidgetsWithTag(
				ExpandedConnectedOutside,
				FName(TEXT(
					"BlueprintLens.Automation.LC4SequenceExcludedSibling")));
		FString ConnectedExclusionText;
		for (const TSharedRef<SWidget>& Widget : ConnectedExclusions)
		{
			ConnectedExclusionText += SlateWidgetText(Widget) + TEXT("\n");
		}
		TestTrue(
			TEXT("a full-graph connected sibling outside the live slice is named and marked excluded"),
			ConnectedExclusionText.Contains(
				TEXT("CONNECTED OUTSIDE SENTINEL")) &&
				ConnectedExclusionText.Contains(
					TEXT("EXCLUDED"), ESearchCase::IgnoreCase));

		// Return the first relation to collapsed, then open the second relation.
		OpenLC4Action(0);
		const TSharedRef<SWidget> CollapsedBetweenCases =
			LC4Panel->BuildM6CausalContent();
		const TArray<TSharedRef<SWidget>> RebuiltLC4Actions =
			SlateWidgetsWithTag(
				CollapsedBetweenCases,
				FName(TEXT(
					"BlueprintLens.Automation.CompositeLC4SequenceDisclosureAction")));
		if (RebuiltLC4Actions.IsValidIndex(1) &&
			RebuiltLC4Actions[1]->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(RebuiltLC4Actions[1])->SimulateClick();
		}
		const TSharedRef<SWidget> ExpandedUnconnected =
			LC4Panel->BuildM6CausalContent();
		const TArray<TSharedRef<SWidget>> UnconnectedExclusions =
			SlateWidgetsWithTag(
				ExpandedUnconnected,
				FName(TEXT(
					"BlueprintLens.Automation.LC4SequenceExcludedSibling")));
		FString UnconnectedExclusionText;
		for (const TSharedRef<SWidget>& Widget : UnconnectedExclusions)
		{
			UnconnectedExclusionText += SlateWidgetText(Widget) + TEXT("\n");
		}
		TestTrue(
			TEXT("the declared unconnected sibling is named as excluded rather than absent"),
			UnconnectedExclusionText.Contains(TEXT("then_0")) &&
				UnconnectedExclusionText.Contains(
					TEXT("EXCLUDED"), ESearchCase::IgnoreCase));

		FBlueprintLensExplanationModel DuplicateTitleLC4 = LiveLC4;
		for (FBlueprintLensUnit& Unit : DuplicateTitleLC4.Units)
		{
			if (Unit.Id == TEXT("unit.live.sequence.a") ||
				Unit.Id == TEXT("unit.live.sequence.b") ||
				Unit.Id == DuplicateTitleLC4.CriterionUnitId)
			{
				Unit.Title = TEXT("Sequence");
				Unit.bHasDisambiguator = false;
				Unit.Disambiguator = FBlueprintLensDisambiguator();
			}
		}
		const FBlueprintLensLC4SequenceLiveAdapterResult DuplicateAdapted =
			FBlueprintLensLC4SequenceLiveAdapter::Build(
				DuplicateTitleLC4,
				TwoOutputFacts);
		const FBlueprintLensLC4SequenceLiveCase* DuplicateCase =
			DuplicateAdapted.Cases.FindByPredicate(
				[](const FBlueprintLensLC4SequenceLiveCase& Candidate)
				{
					return Candidate.SequenceUnitId ==
						TEXT("unit.live.sequence.a");
				});
		TestNotNull(
			TEXT("the duplicate-title LC4-SEQ regression reaches the live adapter"),
			DuplicateCase);
		if (DuplicateCase != nullptr)
		{
			const FBlueprintLensUnit* Upstream =
				DuplicateCase->Explanation.FindUnit(
					TEXT("unit.live.sequence.a"));
			const FBlueprintLensUnit* DirectFeeder =
				DuplicateCase->Explanation.FindUnit(
					TEXT("unit.live.sequence.b"));
			const FBlueprintLensUnit* SelectedTarget =
				DuplicateCase->Explanation.FindUnit(
					DuplicateTitleLC4.CriterionUnitId);
			TestTrue(
				TEXT("LC4-SEQ plants semantic-role Disambiguators on every repeated Sequence identity"),
				Upstream != nullptr && Upstream->bHasDisambiguator &&
					DirectFeeder != nullptr && DirectFeeder->bHasDisambiguator &&
					SelectedTarget != nullptr &&
						SelectedTarget->bHasDisambiguator &&
					Upstream->Disambiguator.RuleId ==
						TEXT("lc4_sequence_duplicate_title_role_v1") &&
					DirectFeeder->Disambiguator.RuleId ==
						TEXT("lc4_sequence_duplicate_title_role_v1") &&
					SelectedTarget->Disambiguator.RuleId ==
						TEXT("lc4_sequence_duplicate_title_role_v1"));
			TestTrue(
				TEXT("the repeated stations carry three non-ordinal reader roles that separate at a glance"),
				Upstream != nullptr &&
					Upstream->Disambiguator.Text == TEXT("upstream fan-out") &&
					DirectFeeder != nullptr &&
					DirectFeeder->Disambiguator.Text == TEXT("direct feeder") &&
					SelectedTarget != nullptr &&
					SelectedTarget->Disambiguator.Text == TEXT("selected target") &&
					!Upstream->Disambiguator.Text.Contains(TEXT("then_")) &&
					!DirectFeeder->Disambiguator.Text.Contains(TEXT("then_")) &&
					!SelectedTarget->Disambiguator.Text.Contains(TEXT("then_")));
		}

		TSharedRef<SBlueprintLensPanel> DuplicateTitlePanel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		DuplicateTitlePanel->M6Presentation.SetPythonReady(true);
		DuplicateTitlePanel->M6Presentation.SetQueryKind(
			EM6QueryKind::Execution);
		DuplicateTitlePanel->M6Presentation.SetGraphId(
			DuplicateTitleLC4.Source.GraphId);
		DuplicateTitlePanel->M6Presentation.ApplySession(Ready, &Views);
		FM6LoadedSessionPacket DuplicateTitlePacket;
		DuplicateTitlePacket.Request.QueryKind = TEXT("execution");
		DuplicateTitlePacket.Explanation = DuplicateTitleLC4;
		DuplicateTitlePacket.SemanticSha256 = FString::ChrN(64, TEXT('6'));
		DuplicateTitlePanel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(DuplicateTitlePacket));
		const TSharedRef<SWidget> DuplicateCollapsed =
			DuplicateTitlePanel->BuildM6CausalContent();
		TestTrue(
			TEXT("the tagged composite rail visibly distinguishes all three repeated Sequence stations"),
			SlateHasWidgetTag(
				DuplicateCollapsed,
				FName(TEXT(
					"BlueprintLens.Automation.CompositeAttachmentsCollapsed"))) &&
				SlateWidgetText(DuplicateCollapsed).Contains(
					TEXT("Sequence (upstream fan-out)")) &&
				SlateWidgetText(DuplicateCollapsed).Contains(
					TEXT("Sequence (direct feeder)")) &&
				SlateWidgetText(DuplicateCollapsed).Contains(
					TEXT("Sequence (selected target)")));
		const TSharedPtr<SWidget> DuplicateAction = SlateWidgetWithTag(
			DuplicateCollapsed,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC4SequenceDisclosureAction")));
		if (DuplicateAction.IsValid() &&
			DuplicateAction->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(DuplicateAction.ToSharedRef())
				->SimulateClick();
		}
		const TSharedRef<SWidget> DuplicateExpanded =
			DuplicateTitlePanel->BuildM6CausalContent();
		const TSharedPtr<SWidget> DuplicateSurface = SlateWidgetWithTag(
			DuplicateExpanded,
			FName(TEXT("BlueprintLens.Automation.CompositeLC4SequenceSurface")));
		TestTrue(
			TEXT("the reused LC4-SEQ surface carries the same glance-distinct reader labels"),
			DuplicateSurface.IsValid() &&
				SlateWidgetText(DuplicateSurface.ToSharedRef()).Contains(
					TEXT("Sequence (upstream fan-out)")) &&
				SlateWidgetText(DuplicateSurface.ToSharedRef()).Contains(
					TEXT("Sequence (direct feeder)")) &&
				SlateWidgetText(DuplicateSurface.ToSharedRef()).Contains(
					TEXT("Sequence (selected target)")));
	}

	if (LC3Load.IsSuccess())
	{
		FBlueprintLensExplanationModel LiveLC3 = *LC3Load.Model;
		FBlueprintLensExplanationModel StaleLC3 = *LC3Load.Model;
		const FString CurrentLC3Blueprint =
			TEXT("/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance");
		const FString StaleLC3Blueprint =
			TEXT("/Game/LensCorpus/BP_ConstructionFixture.BP_ConstructionFixture");
		const FString LiveValueOrigin = TEXT("Current packet result producer");
		const FString StaleValueOrigin =
			TEXT("Construction fixture result producer");
		LiveLC3.Source.BlueprintAssetPath = CurrentLC3Blueprint;
		StaleLC3.Source.BlueprintAssetPath = StaleLC3Blueprint;
		LiveLC3.Groups.Reset();
		LiveLC3.bHasGroups = false;
		LiveLC3.bHasGroupPartialOrder = false;
		const FBlueprintLensRelation* CriterionValueRelation =
			LiveLC3.Relations.FindByPredicate(
				[&LiveLC3](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind ==
							EBlueprintLensRelationKind::ProvidesValue &&
						Relation.TargetUnitId == LiveLC3.CriterionUnitId;
				});
		TestNotNull(
			TEXT("the live LC3 RED resolves the planted value producer"),
			CriterionValueRelation);
		if (CriterionValueRelation != nullptr)
		{
			if (FBlueprintLensUnit* LiveProducer =
				LiveLC3.Units.FindByPredicate(
					[CriterionValueRelation](const FBlueprintLensUnit& Unit)
					{
						return Unit.Id ==
							CriterionValueRelation->SourceUnitId;
					}))
			{
				LiveProducer->Title = LiveValueOrigin;
			}
			if (FBlueprintLensUnit* StaleProducer =
				StaleLC3.Units.FindByPredicate(
					[CriterionValueRelation](const FBlueprintLensUnit& Unit)
					{
						return Unit.Id ==
							CriterionValueRelation->SourceUnitId;
					}))
			{
				StaleProducer->Title = StaleValueOrigin;
			}
			const FBlueprintLensUnit* PlantedLiveProducer =
				LiveLC3.FindUnit(CriterionValueRelation->SourceUnitId);
			TestTrue(
				TEXT("the differential LC3 RED plants its current-packet producer data"),
				PlantedLiveProducer != nullptr &&
					PlantedLiveProducer->Title == LiveValueOrigin);
		}

		const FBlueprintLensLC3ValueConeProjection GroupFreeProjection =
			FBlueprintLensLC3ValueConeProjector::Build(LiveLC3);
		TestEqual(
			TEXT("the live group-free LC3 packet reaches the accepted projector's optional-cover boundary"),
			GroupFreeProjection.Status,
			EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback);

		FBlueprintLensExplanationModel FourUnitConeLC3 = LiveLC3;
		TSet<FString> FourUnitConeUnitIds = {LiveLC3.CriterionUnitId};
		TSet<FString> FourUnitConeRelationIds;
		const FBlueprintLensRelation* FourUnitRootRelation =
			LiveLC3.Relations.FindByPredicate(
				[&LiveLC3](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind ==
							EBlueprintLensRelationKind::ProvidesValue &&
						Relation.TargetUnitId == LiveLC3.CriterionUnitId;
				});
		TestNotNull(
			TEXT("the four-unit LC3 RED resolves the criterion value relation"),
			FourUnitRootRelation);
		if (FourUnitRootRelation != nullptr)
		{
			FourUnitConeUnitIds.Add(FourUnitRootRelation->SourceUnitId);
			FourUnitConeRelationIds.Add(FourUnitRootRelation->Id);
			for (const FBlueprintLensRelation& Relation : LiveLC3.Relations)
			{
				if (Relation.Kind == EBlueprintLensRelationKind::ProvidesValue &&
					Relation.TargetUnitId == FourUnitRootRelation->SourceUnitId)
				{
					FourUnitConeUnitIds.Add(Relation.SourceUnitId);
					FourUnitConeRelationIds.Add(Relation.Id);
				}
			}
		}
		const FBlueprintLensRelation* FourUnitControlRelation =
			LiveLC3.Relations.FindByPredicate(
				[&LiveLC3](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind ==
							EBlueprintLensRelationKind::ExecutionPredecessor &&
						Relation.TargetUnitId == LiveLC3.CriterionUnitId;
				});
		TestNotNull(
			TEXT("the four-unit LC3 RED resolves the execution controller"),
			FourUnitControlRelation);
		if (FourUnitControlRelation != nullptr)
		{
			FourUnitConeUnitIds.Add(FourUnitControlRelation->SourceUnitId);
			FourUnitConeRelationIds.Add(FourUnitControlRelation->Id);
		}
		FourUnitConeLC3.Units.RemoveAll(
			[&FourUnitConeUnitIds](const FBlueprintLensUnit& Unit)
			{
				return !FourUnitConeUnitIds.Contains(Unit.Id);
			});
		FourUnitConeLC3.Relations.RemoveAll(
			[&FourUnitConeRelationIds](const FBlueprintLensRelation& Relation)
			{
				return !FourUnitConeRelationIds.Contains(Relation.Id);
			});
		for (FBlueprintLensLane& Lane : FourUnitConeLC3.Lanes)
		{
			Lane.UnitIds.RemoveAll(
				[&FourUnitConeUnitIds](const FString& UnitId)
				{
					return !FourUnitConeUnitIds.Contains(UnitId);
				});
			if (Lane.UnitIds.IsEmpty())
			{
				Lane.State = EBlueprintLensLaneState::Empty;
				Lane.EmptyMessage = TEXT("No units in the four-unit LC3 test cone.");
			}
		}
		FourUnitConeLC3.Counts.Units = FourUnitConeLC3.Units.Num();
		FourUnitConeLC3.Counts.Relations = FourUnitConeLC3.Relations.Num();
		const int32 FourUnitValueRelationCount =
			FourUnitConeLC3.Relations.FilterByPredicate(
				[](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind ==
						EBlueprintLensRelationKind::ProvidesValue;
				}).Num();
		TestEqual(
			TEXT("the LC3 RED plants four cone units plus one controller"),
			FourUnitConeLC3.Units.Num(),
			5);
		TestEqual(
			TEXT("the LC3 RED plants exactly three value relations"),
			FourUnitValueRelationCount,
			3);

		const FBlueprintLensLC3LiveExplanationAdapterResult AdaptedFourUnitLC3 =
			FBlueprintLensLC3LiveExplanationAdapter::Build(FourUnitConeLC3);
		TestTrue(
			*FString::Printf(
				TEXT("a four-unit/three-relation live cone is admitted by the declared LC3 bound: %s"),
				*AdaptedFourUnitLC3.DiagnosticCode),
			AdaptedFourUnitLC3.IsSuccess());
		if (AdaptedFourUnitLC3.IsSuccess())
		{
			TestEqual(
				TEXT("the four-unit LC3 adapter retains the cone plus controller"),
				AdaptedFourUnitLC3.AdaptedUnitCount,
				5);
			TestEqual(
				TEXT("the four-unit LC3 adapter retains three value relations plus control"),
				AdaptedFourUnitLC3.AdaptedRelationCount,
				4);
			const FBlueprintLensLC3ValueConeProjection FourUnitProjection =
				FBlueprintLensLC3ValueConeProjector::Build(
					AdaptedFourUnitLC3.Explanation);
			const FBlueprintLensLC3ValueConeLayoutSessionResult FourUnitSession =
				FBlueprintLensLC3ValueConeLayoutSession::Build(
					FourUnitProjection,
					700.0f);
			TestTrue(
				*FString::Printf(
					TEXT("the accepted four-unit live cone reaches the reused LC3 surface: %s"),
					*FourUnitSession.DiagnosticCode),
				FourUnitSession.IsRenderable(FourUnitProjection));
		}

		if (FourUnitControlRelation != nullptr)
		{
			FBlueprintLensExplanationModel DuplicateControllerLC3 =
				FourUnitConeLC3;
			FBlueprintLensRelation DuplicateController =
				*FourUnitControlRelation;
			DuplicateController.Id += TEXT(".duplicate");
			DuplicateControllerLC3.Relations.Add(MoveTemp(DuplicateController));
			DuplicateControllerLC3.Counts.Relations =
				DuplicateControllerLC3.Relations.Num();
			const FBlueprintLensLC3LiveExplanationAdapterResult RejectedDuplicate =
				FBlueprintLensLC3LiveExplanationAdapter::Build(
					DuplicateControllerLC3);
			TestEqual(
				TEXT("controller uniqueness remains enforced independently of core cardinality"),
				RejectedDuplicate.DiagnosticCode,
				FString(TEXT("LC3_LIVE_ADAPTER_CONTROL_INVALID")));
		}
		const FBlueprintLensLC3LiveExplanationAdapterResult AdaptedLC3 =
			FBlueprintLensLC3LiveExplanationAdapter::Build(LiveLC3);
		TestTrue(
			*FString::Printf(
				TEXT("the relation-derived LC3 cover satisfies its declared D3 bound: %s"),
				*AdaptedLC3.DiagnosticCode),
			AdaptedLC3.IsSuccess());
		if (AdaptedLC3.IsSuccess())
		{
			const FBlueprintLensLC3ValueConeProjection AdaptedProjection =
				FBlueprintLensLC3ValueConeProjector::Build(
					AdaptedLC3.Explanation);
			TestTrue(
				*FString::Printf(
					TEXT("the original LC3 projector accepts the rebuilt live cover: %s"),
					*AdaptedProjection.DiagnosticCode),
				AdaptedProjection.IsRenderable() &&
					AdaptedProjection.Status ==
						EBlueprintLensLC3ValueConeProjectionStatus::ValueCone);
		}
		const FBlueprintLensLC1RailProjection LC3Rail =
			FBlueprintLensLC1RailProjector::Build(LiveLC3);
		FBlueprintLensCompositeRailSlots LC3DirectSlots =
			FBlueprintLensCompositeRailSlotProjector::Build(LiveLC3, LC3Rail);
		LC3DirectSlots = FBlueprintLensLC3StationAttachmentProjector::Apply(
			LiveLC3,
			LC3DirectSlots);
		const FBlueprintLensLC1RailLayoutSessionResult LC3RailSession =
			FBlueprintLensLC1RailLayoutSession::Build(
				LC3Rail,
				LiveLC3,
				700.0f);
		const FBlueprintLensLC1RailSurfaceLayout LC3DirectSurface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				LC3Rail,
				LC3RailSession,
				LC3DirectSlots,
				700.0f);
		TestTrue(
			*FString::Printf(
				TEXT("the LC3 beside-station slots preserve a renderable live rail surface: %s"),
				*LC3DirectSurface.DiagnosticCode),
			LC3DirectSurface.IsRenderable(LC3Rail));
		const FBlueprintLensCompositeRailSlots LC3PlainSlots =
			FBlueprintLensCompositeRailSlotProjector::Build(LiveLC3, LC3Rail);
		const FBlueprintLensLC1RailSurfaceLayout LC3PlainSurface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				LC3Rail,
				LC3RailSession,
				LC3PlainSlots,
				700.0f);
		TestEqual(
			TEXT("collapsed LC3 attachments add no vertical height to the station-count rail"),
			LC3DirectSurface.CanvasSize.Y,
			LC3PlainSurface.CanvasSize.Y);
		for (const float Width : {430.0f, 480.0f})
		{
			const FBlueprintLensLC1RailLayoutSessionResult WidthSession =
				FBlueprintLensLC1RailLayoutSession::Build(
					LC3Rail,
					LiveLC3,
					Width);
			const FBlueprintLensLC1RailSurfaceLayout WidthSurface =
				FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
					LC3Rail,
					WidthSession,
					LC3DirectSlots,
					Width);
			TestTrue(
				*FString::Printf(
					TEXT("LC3 beside-station geometry remains renderable at %.0f px: %s"),
					Width,
					*WidthSurface.DiagnosticCode),
				WidthSurface.IsRenderable(LC3Rail));
		}

		TSharedRef<SBlueprintLensPanel> LC3Panel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		LC3Panel->Model = MakeShared<FBlueprintLensExplanationModel>(StaleLC3);
		LC3Panel->M6Presentation.SetPythonReady(true);
		LC3Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
		LC3Panel->M6Presentation.SetGraphId(LiveLC3.Source.GraphId);
		LC3Panel->M6Presentation.ApplySession(Ready, &Views);
		FM6LoadedSessionPacket LC3Packet;
		LC3Packet.Explanation = LiveLC3;
		LC3Packet.SemanticSha256 = FString::ChrN(64, TEXT('c'));
		LC3Panel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(LC3Packet));

		const TSharedRef<SWidget> CollapsedLC3 =
			LC3Panel->BuildM6CausalContent();
		TestTrue(
			TEXT("the live LC3 composition creates the rail canvas"),
			LC3Panel->LC1RailCanvas.IsValid());
		if (LC3Panel->LC1RailCanvas.IsValid())
		{
			const FBlueprintLensCompositeRailSlots& LC3Slots =
				LC3Panel->LC1RailCanvas->GetCompositeSlotsForTesting();
			TestEqual(
				TEXT("the Explanation handed to the LC3 chain names the current Blueprint"),
				LC3Slots.SourceBlueprintAssetPath,
				CurrentLC3Blueprint);
			const FBlueprintLensCompositeStationSlot* LiveProducerAttachment =
				LC3Slots.Stations.FindByPredicate(
					[&LiveValueOrigin](
						const FBlueprintLensCompositeStationSlot& Station)
					{
						return Station.BesideAttachments.ContainsByPredicate(
							[&LiveValueOrigin](
								const FBlueprintLensCompositeAttachment& Attachment)
							{
								return Attachment.GrammarId == TEXT("LC3") &&
									(Attachment.MarkerText.Contains(LiveValueOrigin) ||
										Attachment.DetailLines.Contains(LiveValueOrigin));
							});
					});
			TestNotNull(
				TEXT("a live value producer occupies a beside-station LC3 attachment"),
				LiveProducerAttachment);
			TestTrue(
				TEXT("all LC3 beside-station attachments begin collapsed"),
				LC3Slots.AreAllAttachmentsCollapsed());
		}
		const TArray<TSharedRef<SWidget>> LC3Affordances = SlateWidgetsWithTag(
			CollapsedLC3,
			FName(TEXT("BlueprintLens.Automation.CompositeLC3AttachmentDisclosure")));
		TestTrue(
			TEXT("a collapsed LC3 attachment has a visible tagged affordance"),
			!LC3Affordances.IsEmpty());
		FString LC3AffordanceText;
		for (const TSharedRef<SWidget>& Affordance : LC3Affordances)
		{
			LC3AffordanceText += SlateWidgetText(Affordance);
			LC3AffordanceText += TEXT("\n");
		}
		TestTrue(
			TEXT("the visible LC3 affordance is built from the current packet's producer data"),
			LC3AffordanceText.Contains(LiveValueOrigin));
		TestFalse(
			TEXT("the construction fixture cannot supply the live LC3 affordance"),
			LC3AffordanceText.Contains(StaleValueOrigin));

		const TSharedPtr<SWidget> FirstLC3Disclosure = SlateWidgetWithTag(
			CollapsedLC3,
			FName(TEXT("BlueprintLens.Automation.CompositeLC3AttachmentAction")));
		TestTrue(
			TEXT("the visible LC3 affordance is actionable"),
			FirstLC3Disclosure.IsValid());
		if (FirstLC3Disclosure.IsValid() &&
			FirstLC3Disclosure->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(FirstLC3Disclosure.ToSharedRef())
				->SimulateClick();
			const TSharedRef<SWidget> ExpandedLC3 =
				LC3Panel->BuildM6CausalContent();
			TestTrue(
				TEXT("opening the live attachment mounts the one accepted LC3 D3 canvas"),
				SlateHasWidgetTag(
					ExpandedLC3,
					FName(TEXT("BlueprintLens.Automation.CompositeLC3Surface"))));
			StaticCastSharedRef<SButton>(FirstLC3Disclosure.ToSharedRef())
				->SimulateClick();
			const TSharedRef<SWidget> CollapsedAgain =
				LC3Panel->BuildM6CausalContent();
			TestFalse(
				TEXT("the LC3 attachment has a way back to its collapsed state"),
				SlateHasWidgetTag(
					CollapsedAgain,
					FName(TEXT("BlueprintLens.Automation.CompositeLC3Surface"))));
		}
	}

	if (MotifLoad.IsSuccess())
	{
		FBlueprintLensExplanationModel FoldExplanation = *MotifLoad.Model;
		const FBlueprintLensLC1RailProjection SeedRail =
			FBlueprintLensLC1RailProjector::Build(FoldExplanation);
		TestTrue(
			TEXT("the fold RED starts from the accepted MotifScale station ledger"),
			SeedRail.IsRenderable() && SeedRail.OrderedCanonicalUnits.Num() > 20);
		if (SeedRail.IsRenderable() && SeedRail.OrderedCanonicalUnits.Num() > 20)
		{
			FBlueprintLensUnit FoldProducer = FoldExplanation.Units[0];
			FoldProducer.Id = TEXT("unit.value.m10.lc3-fold-origin");
			FoldProducer.Role = EBlueprintLensRole::Value;
			FoldProducer.Kind = EBlueprintLensUnitKind::Expression;
			FoldProducer.Title = TEXT("Seeded folded value origin");
			FoldProducer.bHasDisambiguator = false;
			FoldProducer.Disambiguator = FBlueprintLensDisambiguator();
			FoldExplanation.Units.Add(FoldProducer);
			const int32 SeededTargetCount = 8;
			for (int32 Index = 1; Index <= SeededTargetCount; ++Index)
			{
				FBlueprintLensRelation ValueRelation;
				ValueRelation.Id = FString::Printf(
					TEXT("relation.provides_value.m10-lc3-fold-%02d"),
					Index);
				ValueRelation.SourceUnitId = FoldProducer.Id;
				ValueRelation.TargetUnitId =
					SeedRail.OrderedCanonicalUnits[Index].UnitId;
				ValueRelation.Kind = EBlueprintLensRelationKind::ProvidesValue;
				ValueRelation.Label = TEXT("SeededValueInput");
				ValueRelation.bHasPortLabel = true;
				ValueRelation.PortLabel = TEXT("Seeded folded value origin");
				ValueRelation.bHasSemanticLabel = true;
				ValueRelation.SemanticLabel =
					EBlueprintLensSemanticLabel::ValueInput;
				FoldExplanation.Relations.Add(MoveTemp(ValueRelation));
			}
			FoldExplanation.Counts.Units = FoldExplanation.Units.Num();
			FoldExplanation.Counts.Relations = FoldExplanation.Relations.Num();
			for (FBlueprintLensLane& Lane : FoldExplanation.Lanes)
			{
				if (Lane.Role == EBlueprintLensRole::Value)
				{
					Lane.State = EBlueprintLensLaneState::Populated;
					Lane.UnitIds.Add(FoldProducer.Id);
					Lane.EmptyMessage.Reset();
				}
			}

			TSharedRef<SBlueprintLensPanel> FoldPanel =
				SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
			FoldPanel->M6Presentation.SetPythonReady(true);
			FoldPanel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
			FoldPanel->M6Presentation.SetGraphId(FoldExplanation.Source.GraphId);
			FoldPanel->M6Presentation.ApplySession(Ready, &Views);
			FM6LoadedSessionPacket FoldPacket;
			FoldPacket.Explanation = MoveTemp(FoldExplanation);
			FoldPacket.SemanticSha256 = FString::ChrN(64, TEXT('d'));
			FoldPanel->M6ReadyPacket =
				MakeShared<FM6LoadedSessionPacket>(MoveTemp(FoldPacket));
			const TSharedRef<SWidget> CollapsedFold =
				FoldPanel->BuildM6CausalContent();
			TestTrue(
				TEXT("the attachment fold RED creates a live rail canvas"),
				FoldPanel->LC1RailCanvas.IsValid());
			int32 FoldedAttachmentStations = 0;
			if (FoldPanel->LC1RailCanvas.IsValid())
			{
				const FBlueprintLensLC1RailSurfaceLayout& FoldSurface =
					FoldPanel->LC1RailCanvas->GetSurfaceForTesting();
				const FBlueprintLensCompositeRailSlots& FoldSlots =
					FoldPanel->LC1RailCanvas->GetCompositeSlotsForTesting();
				for (const FBlueprintLensCompositeStationSlot& Station :
					FoldSlots.Stations)
				{
					if (!Station.BesideAttachments.IsEmpty() &&
						FoldSurface.Radius.FoldedUnitIds.Contains(Station.UnitId))
					{
						++FoldedAttachmentStations;
					}
				}
			}
			TestTrue(
				TEXT("the seeded value relations produce attachment-carrying stations inside the radius fold"),
				FoldedAttachmentStations > 0);
			const TSharedPtr<SWidget> FoldDisclosure = SlateWidgetWithTag(
				CollapsedFold,
				FName(TEXT("BlueprintLens.Automation.CompositeAttachmentFoldDisclosure")));
			TestTrue(
				TEXT("a fold over attachment carriers has a visible tagged disclosure"),
				FoldDisclosure.IsValid());
			if (FoldDisclosure.IsValid())
			{
				TestTrue(
					TEXT("the fold disclosure carries the seeded attachment-station count"),
					SlateWidgetText(FoldDisclosure.ToSharedRef()).Contains(
						FString::FromInt(FoldedAttachmentStations)));
			}
			const TSharedPtr<SWidget> FoldAction = SlateWidgetWithTag(
				CollapsedFold,
				FName(TEXT("BlueprintLens.Automation.CompositeAttachmentFoldAction")));
			TestTrue(
				TEXT("the attachment-carrying fold exposes an action that restores its members"),
				FoldAction.IsValid());
			if (FoldAction.IsValid() &&
				FoldAction->GetTypeAsString() == TEXT("SButton"))
			{
				StaticCastSharedRef<SButton>(FoldAction.ToSharedRef())->SimulateClick();
				const TSharedRef<SWidget> OpenedFold =
					FoldPanel->BuildM6CausalContent();
				TestTrue(
					TEXT("opening the fold removes the radius omission"),
					FoldPanel->LC1RailCanvas.IsValid() &&
						FoldPanel->LC1RailCanvas->GetSurfaceForTesting()
							.Radius.FoldedUnitIds.IsEmpty());
				TestTrue(
					TEXT("opening the fold restores the seeded members' LC3 affordances"),
					SlateWidgetsWithTag(
						OpenedFold,
						FName(TEXT(
							"BlueprintLens.Automation.CompositeLC3AttachmentDisclosure")))
						.Num() >= FoldedAttachmentStations);
			}
		}
	}

	if (LC3Load.IsSuccess())
	{
		FBlueprintLensExplanationModel DataAnswer = *LC3Load.Model;
		const FString DataReasonSentinel =
			TEXT("f12_red_required_data_producer");
		const FString DataProducerIdentitySentinel =
			TEXT("F12 seeded value source");
		int32 SeededProducerIndex = 0;
		for (FBlueprintLensUnit& Unit : DataAnswer.Units)
		{
			if (Unit.Id == DataAnswer.CriterionUnitId)
			{
				Unit.InclusionReasons = {TEXT("member_set")};
			}
			else if (Unit.Role == EBlueprintLensRole::Control)
			{
				Unit.InclusionReasons = {TEXT("direct_write_controller")};
			}
			else
			{
				Unit.InclusionReasons = {
					TEXT("required_data_producer"),
					DataReasonSentinel};
				Unit.Title = FString::Printf(
					TEXT("%s %d"),
					*DataProducerIdentitySentinel,
					++SeededProducerIndex);
			}
		}
		const FBlueprintLensRelation* ImmediateValueInput =
			DataAnswer.Relations.FindByPredicate(
				[&DataAnswer](const FBlueprintLensRelation& Relation)
				{
					return Relation.Kind ==
							EBlueprintLensRelationKind::ProvidesValue &&
						Relation.TargetUnitId == DataAnswer.CriterionUnitId;
				});
		if (ImmediateValueInput != nullptr)
		{
			FBlueprintLensUnit* BoundaryValue =
				DataAnswer.Units.FindByPredicate(
					[ImmediateValueInput](const FBlueprintLensUnit& Unit)
					{
						return Unit.Id ==
							ImmediateValueInput->SourceUnitId;
					});
			if (BoundaryValue != nullptr)
			{
				BoundaryValue->Role = EBlueprintLensRole::Boundary;
				BoundaryValue->SemanticStatus =
					EBlueprintLensSemanticStatus::Opaque;
				BoundaryValue->InclusionReasons.AddUnique(
					TEXT("opaque_boundary"));
			}
		}
		for (FBlueprintLensRelation& Relation : DataAnswer.Relations)
		{
			if (Relation.Kind ==
				EBlueprintLensRelationKind::ExecutionPredecessor)
			{
				Relation.Kind =
					EBlueprintLensRelationKind::ControlsExecution;
				break;
			}
		}
		DataAnswer.Query.Question =
			TEXT("Where does this member's written value come from?");

		// Reproduce the smallest retained MotifScale Data packet: one direct
		// write controller, one member-set criterion, and the real
		// controls_execution relation between them.  This keeps the F12
		// regression independent of the larger LC3-shaped construction above.
		FBlueprintLensExplanationModel TwoUnitDataAnswer = DataAnswer;
		FBlueprintLensUnit TwoUnitController;
		TwoUnitController.Id = TEXT("unit.control.f12-corpus-two-unit");
		TwoUnitController.Role = EBlueprintLensRole::Control;
		TwoUnitController.Kind = EBlueprintLensUnitKind::Node;
		TwoUnitController.Title = TEXT("Branch");
		TwoUnitController.InclusionReasons = {
			TEXT("direct_write_controller")};
		FBlueprintLensUnit TwoUnitCriterion;
		TwoUnitCriterion.Id = TEXT("unit.criterion.f12-corpus-two-unit");
		TwoUnitCriterion.Role = EBlueprintLensRole::Criterion;
		TwoUnitCriterion.Kind = EBlueprintLensUnitKind::Node;
		TwoUnitCriterion.Title = TEXT("Set M7Flag003");
		TwoUnitCriterion.InclusionReasons = {TEXT("member_set")};
		FBlueprintLensRelation TwoUnitRelation;
		TwoUnitRelation.Id =
			TEXT("relation.controls_execution.f12-corpus-two-unit");
		TwoUnitRelation.SourceUnitId = TwoUnitController.Id;
		TwoUnitRelation.TargetUnitId = TwoUnitCriterion.Id;
		TwoUnitRelation.Kind =
			EBlueprintLensRelationKind::ControlsExecution;
		TwoUnitRelation.Label = TEXT("else");
		TwoUnitDataAnswer.Units = {TwoUnitController, TwoUnitCriterion};
		TwoUnitDataAnswer.Relations = {TwoUnitRelation};
		TwoUnitDataAnswer.CriterionUnitId = TwoUnitCriterion.Id;
		TwoUnitDataAnswer.Counts.Units = TwoUnitDataAnswer.Units.Num();
		TwoUnitDataAnswer.Counts.Relations =
			TwoUnitDataAnswer.Relations.Num();
		const FBlueprintLensF12DataRailAdapterResult TwoUnitAdapted =
			FBlueprintLensF12DataRailAdapter::Build(TwoUnitDataAnswer);
		TestTrue(
			*FString::Printf(
				TEXT("the retained 2/1 Data shape reaches the rail adapter: %s"),
				*TwoUnitAdapted.DiagnosticCode),
			TwoUnitAdapted.IsSuccess());
		if (TwoUnitAdapted.IsSuccess())
		{
			const FBlueprintLensLC1RailProjection TwoUnitRail =
				FBlueprintLensLC1RailProjector::Build(
					TwoUnitAdapted.Explanation);
			TestTrue(
				*FString::Printf(
					TEXT("the retained 2/1 Data shape reaches the shared rail: %s"),
					*TwoUnitRail.DiagnosticCode),
				TwoUnitRail.IsRenderable());
			const FBlueprintLensF12DataAnswerProjection TwoUnitProjection =
				FBlueprintLensF12DataAnswerProjector::Build(
					TwoUnitDataAnswer,
					TwoUnitRail);
			TestTrue(
				*FString::Printf(
					TEXT("the retained 2/1 Data shape reaches the answer projector: %s"),
					*TwoUnitProjection.DiagnosticCode),
				TwoUnitProjection.IsRenderable(
					TwoUnitDataAnswer,
					TwoUnitRail));
			const FBlueprintLensCompositeRailSlots TwoUnitBaseSlots =
				FBlueprintLensCompositeRailSlotProjector::Build(
					TwoUnitAdapted.Explanation,
					TwoUnitRail);
			const FBlueprintLensCompositeRailSlots TwoUnitSlots =
				FBlueprintLensF12DataAnswerProjector::Apply(
					TwoUnitProjection,
					TwoUnitBaseSlots);

			FBlueprintLensExplanationModel MultiSetDataAnswer =
				TwoUnitDataAnswer;
			FBlueprintLensUnit SecondWrite = TwoUnitCriterion;
			SecondWrite.Id = TEXT("unit.criterion.f12-multiset-second");
			SecondWrite.Title = TEXT("Set MultiSetAnswer");
			MultiSetDataAnswer.Units.Add(SecondWrite);
			FBlueprintLensRelation SecondWriteControl = TwoUnitRelation;
			SecondWriteControl.Id =
				TEXT("relation.controls_execution.f12-multiset-second");
			SecondWriteControl.SourceUnitId = TwoUnitCriterion.Id;
			SecondWriteControl.TargetUnitId = SecondWrite.Id;
			MultiSetDataAnswer.Relations.Add(SecondWriteControl);
			FBlueprintLensUnit MultiSetProducer;
			MultiSetProducer.Id = TEXT("unit.value.f12-multiset-source");
			MultiSetProducer.Role = EBlueprintLensRole::Value;
			MultiSetProducer.Kind = EBlueprintLensUnitKind::Expression;
			MultiSetProducer.Title = TEXT("Multi-Set value source");
			MultiSetProducer.InclusionReasons = {
				TEXT("required_data_producer")};
			MultiSetDataAnswer.Units.Add(MultiSetProducer);
			FBlueprintLensRelation MultiSetValueRelation;
			MultiSetValueRelation.Id =
				TEXT("relation.provides_value.f12-multiset-source");
			MultiSetValueRelation.SourceUnitId = MultiSetProducer.Id;
			MultiSetValueRelation.TargetUnitId = SecondWrite.Id;
			MultiSetValueRelation.Kind =
				EBlueprintLensRelationKind::ProvidesValue;
			MultiSetValueRelation.Label = TEXT("assigned value");
			MultiSetDataAnswer.Relations.Add(MultiSetValueRelation);
			MultiSetDataAnswer.Counts.Units =
				MultiSetDataAnswer.Units.Num();
			MultiSetDataAnswer.Counts.Relations =
				MultiSetDataAnswer.Relations.Num();
			const FBlueprintLensF12DataRailAdapterResult MultiSetAdapted =
				FBlueprintLensF12DataRailAdapter::Build(MultiSetDataAnswer);
			TestTrue(
				TEXT("a member-level Data answer retains both Set writes in the rail adapter"),
				MultiSetAdapted.IsSuccess());
			if (MultiSetAdapted.IsSuccess())
			{
				TestEqual(
					TEXT("the rail-only adapter docks the deterministic terminal write"),
					MultiSetAdapted.Explanation.CriterionUnitId,
					SecondWrite.Id);
				TestEqual(
					TEXT("the accepted packet keeps its deterministic member anchor"),
					MultiSetDataAnswer.CriterionUnitId,
					TwoUnitCriterion.Id);
				const FBlueprintLensLC1RailProjection MultiSetRail =
					FBlueprintLensLC1RailProjector::Build(
						MultiSetAdapted.Explanation);
				const FBlueprintLensF12DataAnswerProjection MultiSetProjection =
					FBlueprintLensF12DataAnswerProjector::Build(
						MultiSetDataAnswer,
						MultiSetRail);
				FBlueprintLensCompositeRailSlots MultiSetSlots =
					FBlueprintLensCompositeRailSlotProjector::Build(
						MultiSetAdapted.Explanation,
						MultiSetRail);
				MultiSetSlots = FBlueprintLensF12DataAnswerProjector::Apply(
					MultiSetProjection,
					MultiSetSlots);
				const FBlueprintLensLC1RailLayoutSessionResult MultiSetSession =
					FBlueprintLensLC1RailLayoutSession::Build(
						MultiSetRail,
						MultiSetAdapted.Explanation,
						700.0f);
				const FBlueprintLensLC1RailSurfaceLayout MultiSetSurface =
					FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
						MultiSetRail,
						MultiSetSession,
						MultiSetSlots,
						700.0f,
						INDEX_NONE,
						FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
						true);
				const FBlueprintLensLC1RailSurfaceLabel* MultiSetStage =
					MultiSetSurface.Labels.FindByPredicate(
						[](const FBlueprintLensLC1RailSurfaceLabel& Label)
						{
							return Label.Key == TEXT("stage");
						});
				TestTrue(
					TEXT("the real write-to-write multi-Set Data answer remains renderable and exposes its complete two-write count"),
					MultiSetProjection.IsRenderable(
						MultiSetDataAnswer,
						MultiSetRail) &&
						MultiSetSurface.IsRenderable(MultiSetRail) &&
						MultiSetStage != nullptr &&
						MultiSetStage->Text.Contains(TEXT("2 writes")));
			}
			FBlueprintLensExplanationModel OneSourceDataAnswer =
				TwoUnitDataAnswer;
			FBlueprintLensUnit OneSourceProducer;
			OneSourceProducer.Id =
				TEXT("unit.value.f12-one-source");
			OneSourceProducer.Role = EBlueprintLensRole::Value;
			OneSourceProducer.Kind = EBlueprintLensUnitKind::Expression;
			OneSourceProducer.Title = DataProducerIdentitySentinel;
			OneSourceProducer.InclusionReasons = {
				TEXT("required_data_producer")};
			OneSourceDataAnswer.Units.Add(OneSourceProducer);
			FBlueprintLensRelation OneSourceRelation;
			OneSourceRelation.Id =
				TEXT("relation.provides_value.f12-one-source");
			OneSourceRelation.SourceUnitId = OneSourceProducer.Id;
			OneSourceRelation.TargetUnitId = TwoUnitCriterion.Id;
			OneSourceRelation.Kind =
				EBlueprintLensRelationKind::ProvidesValue;
			OneSourceRelation.Label = TEXT("assigned value");
			OneSourceDataAnswer.Relations.Add(OneSourceRelation);
			OneSourceDataAnswer.Counts.Units =
				OneSourceDataAnswer.Units.Num();
			OneSourceDataAnswer.Counts.Relations =
				OneSourceDataAnswer.Relations.Num();
			const FBlueprintLensF12DataRailAdapterResult OneSourceAdapted =
				FBlueprintLensF12DataRailAdapter::Build(OneSourceDataAnswer);
			TestTrue(
				TEXT("the one-source Data RED reaches the structural rail adapter"),
				OneSourceAdapted.IsSuccess());
			if (OneSourceAdapted.IsSuccess())
			{
				const FBlueprintLensLC1RailProjection OneSourceRail =
					FBlueprintLensLC1RailProjector::Build(
						OneSourceAdapted.Explanation);
				const FBlueprintLensF12DataAnswerProjection OneSourceProjection =
					FBlueprintLensF12DataAnswerProjector::Build(
						OneSourceDataAnswer,
						OneSourceRail);
				FBlueprintLensCompositeRailSlots OneSourceSlots =
					FBlueprintLensCompositeRailSlotProjector::Build(
						OneSourceAdapted.Explanation,
						OneSourceRail);
				OneSourceSlots = FBlueprintLensF12DataAnswerProjector::Apply(
					OneSourceProjection,
					OneSourceSlots);
				const FBlueprintLensCompositeStationSlot* SourceWriteStation =
					OneSourceSlots.FindStation(TwoUnitCriterion.Id);
				const FBlueprintLensCompositeAttachment* SourceAttachment =
					SourceWriteStation != nullptr
						? SourceWriteStation->BesideAttachments.FindByPredicate(
							[](const FBlueprintLensCompositeAttachment& Attachment)
							{
								return Attachment.GrammarId == TEXT("LC3");
							})
						: nullptr;
				TestTrue(
					TEXT("a write station with one required_data_producer owns the reused LC3 beside-station disclosure"),
					SourceAttachment != nullptr &&
						FString::Join(
							SourceAttachment->DetailLines,
							TEXT("\n")).Contains(
							DataProducerIdentitySentinel));
			}

			FBlueprintLensExplanationModel SelfProducerDataAnswer =
				TwoUnitDataAnswer;
			SelfProducerDataAnswer.Units[0].InclusionReasons.AddUnique(
				TEXT("required_data_producer"));
			const FBlueprintLensF12DataRailAdapterResult SelfProducerAdapted =
				FBlueprintLensF12DataRailAdapter::Build(SelfProducerDataAnswer);
			const FBlueprintLensLC1RailProjection SelfProducerRail =
				SelfProducerAdapted.IsSuccess()
					? FBlueprintLensLC1RailProjector::Build(
						SelfProducerAdapted.Explanation)
					: FBlueprintLensLC1RailProjection();
			const FBlueprintLensF12DataAnswerProjection SelfProducerProjection =
				FBlueprintLensF12DataAnswerProjector::Build(
					SelfProducerDataAnswer,
					SelfProducerRail);
			const FBlueprintLensF12DataStationDisclosure* SelfProducerStation =
				SelfProducerProjection.Stations.FindByPredicate(
					[&TwoUnitController](
						const FBlueprintLensF12DataStationDisclosure& Station)
					{
						return Station.StationUnitId == TwoUnitController.Id;
					});
			TestTrue(
				TEXT("a station that is itself a required producer distinguishes that role from producers feeding the station"),
				SelfProducerStation != nullptr &&
					SelfProducerStation->MarkerText.Contains(
						TEXT("itself"), ESearchCase::IgnoreCase) &&
					SelfProducerStation->ValueSourceMarkerText.Contains(
						TEXT("feeds this write"), ESearchCase::IgnoreCase));

			FBlueprintLensExplanationModel DuplicateSourceDataAnswer =
				TwoUnitDataAnswer;
			for (int32 DuplicateIndex = 0; DuplicateIndex < 2; ++DuplicateIndex)
			{
				FBlueprintLensUnit Producer;
				Producer.Id = FString::Printf(
					TEXT("unit.value.f12-duplicate-%d"), DuplicateIndex);
				Producer.Role = EBlueprintLensRole::Value;
				Producer.Kind = EBlueprintLensUnitKind::Expression;
				Producer.Title = TEXT("Duplicate producer");
				Producer.InclusionReasons = {
					TEXT("required_data_producer")};
				FBlueprintLensSourceReference Source;
				Source.SourceNodeId = FString::Printf(
					TEXT("node.duplicate.%d"), DuplicateIndex);
				Source.NativeNodeGuid = DuplicateIndex == 0
					? TEXT("AAAA1111-0000-0000-0000-000000000000")
					: TEXT("BBBB2222-0000-0000-0000-000000000000");
				Source.bPrimary = true;
				Producer.SourceReferences.Add(MoveTemp(Source));
				DuplicateSourceDataAnswer.Units.Add(MoveTemp(Producer));
				FBlueprintLensRelation Relation;
				Relation.Id = FString::Printf(
					TEXT("relation.provides_value.f12-duplicate-%d"),
					DuplicateIndex);
				Relation.SourceUnitId = FString::Printf(
					TEXT("unit.value.f12-duplicate-%d"), DuplicateIndex);
				Relation.TargetUnitId = TwoUnitCriterion.Id;
				Relation.Kind = EBlueprintLensRelationKind::ProvidesValue;
				DuplicateSourceDataAnswer.Relations.Add(MoveTemp(Relation));
			}
			DuplicateSourceDataAnswer.Counts.Units =
				DuplicateSourceDataAnswer.Units.Num();
			DuplicateSourceDataAnswer.Counts.Relations =
				DuplicateSourceDataAnswer.Relations.Num();
			const FBlueprintLensF12DataRailAdapterResult DuplicateSourceAdapted =
				FBlueprintLensF12DataRailAdapter::Build(DuplicateSourceDataAnswer);
			const FBlueprintLensLC1RailProjection DuplicateSourceRail =
				DuplicateSourceAdapted.IsSuccess()
					? FBlueprintLensLC1RailProjector::Build(
						DuplicateSourceAdapted.Explanation)
					: FBlueprintLensLC1RailProjection();
			const FBlueprintLensF12DataAnswerProjection DuplicateSourceProjection =
				FBlueprintLensF12DataAnswerProjector::Build(
					DuplicateSourceDataAnswer,
					DuplicateSourceRail);
			const FBlueprintLensF12DataStationDisclosure* DuplicateDisclosure =
				DuplicateSourceProjection.Stations.FindByPredicate(
					[&TwoUnitCriterion](
						const FBlueprintLensF12DataStationDisclosure& Station)
					{
						return Station.StationUnitId == TwoUnitCriterion.Id;
					});
			FString DuplicateDetails = DuplicateDisclosure != nullptr
				? FString::Join(
					DuplicateDisclosure->ValueSourceDetailLines,
					TEXT("\n"))
				: FString();
			TestTrue(
				TEXT("duplicate producer titles carry distinct source-owned disambiguators"),
				DuplicateDisclosure != nullptr &&
					DuplicateDetails.Contains(TEXT("AAAA1111")) &&
					DuplicateDetails.Contains(TEXT("BBBB2222")));
			TestFalse(
				TEXT("the invariant required_data_producer reason is not repeated on every producer identity line"),
				DuplicateDetails.Contains(TEXT("required_data_producer")));
			TestTrue(
				TEXT("the grouped value-source marker states the common required_data_producer role once"),
				DuplicateDisclosure != nullptr &&
					DuplicateDisclosure->ValueSourceMarkerText.Contains(
						TEXT("required_data_producer")));
			if (DuplicateSourceProjection.IsRenderable(
				DuplicateSourceDataAnswer,
				DuplicateSourceRail))
			{
				FBlueprintLensCompositeRailSlots DuplicateSlots =
					FBlueprintLensCompositeRailSlotProjector::Build(
						DuplicateSourceAdapted.Explanation,
						DuplicateSourceRail);
				DuplicateSlots = FBlueprintLensF12DataAnswerProjector::Apply(
					DuplicateSourceProjection,
					DuplicateSlots);
				const FBlueprintLensLC1RailLayoutSessionResult DuplicateSession =
					FBlueprintLensLC1RailLayoutSession::Build(
						DuplicateSourceRail,
						DuplicateSourceAdapted.Explanation,
						700.0f);
				const FBlueprintLensLC1RailSurfaceLayout DuplicateSurface =
					FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
						DuplicateSourceRail,
						DuplicateSession,
						DuplicateSlots,
						700.0f,
						INDEX_NONE,
						FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
						true);
				const FBlueprintLensLC1RailSurfaceLabel* DuplicateStage =
					DuplicateSurface.Labels.FindByPredicate(
						[](const FBlueprintLensLC1RailSurfaceLabel& Label)
						{
							return Label.Key == TEXT("stage");
						});
				TestTrue(
					TEXT("Data answer totals count write positions plus the planted value sources named by the sentence"),
					DuplicateStage != nullptr &&
						DuplicateStage->Text.Contains(TEXT("4 answer units")) &&
						DuplicateStage->Text.Contains(TEXT("3 Data relations")));
			}
			for (const int32 ProducerCount : {5, 6})
			{
				FBlueprintLensExplanationModel BoundedDataAnswer =
					TwoUnitDataAnswer;
				for (int32 ProducerIndex = 0;
					 ProducerIndex < ProducerCount;
					 ++ProducerIndex)
				{
					FBlueprintLensUnit Producer;
					Producer.Id = FString::Printf(
						TEXT("unit.value.f12-bound-%d-%d"),
						ProducerCount,
						ProducerIndex);
					Producer.Role = EBlueprintLensRole::Value;
					Producer.Kind = EBlueprintLensUnitKind::Expression;
					Producer.Title = FString::Printf(
						TEXT("Bound source %d"), ProducerIndex);
					Producer.InclusionReasons = {
						TEXT("required_data_producer")};
					BoundedDataAnswer.Units.Add(Producer);
					FBlueprintLensRelation ProducerRelation;
					ProducerRelation.Id = FString::Printf(
						TEXT("relation.provides_value.f12-bound-%d-%d"),
						ProducerCount,
						ProducerIndex);
					ProducerRelation.SourceUnitId = Producer.Id;
					ProducerRelation.TargetUnitId = TwoUnitCriterion.Id;
					ProducerRelation.Kind =
						EBlueprintLensRelationKind::ProvidesValue;
					ProducerRelation.Label = TEXT("assigned value");
					BoundedDataAnswer.Relations.Add(ProducerRelation);
				}
				BoundedDataAnswer.Counts.Units =
					BoundedDataAnswer.Units.Num();
				BoundedDataAnswer.Counts.Relations =
					BoundedDataAnswer.Relations.Num();
				const FBlueprintLensF12DataRailAdapterResult BoundedAdapted =
					FBlueprintLensF12DataRailAdapter::Build(BoundedDataAnswer);
				const FBlueprintLensLC1RailProjection BoundedRail =
					BoundedAdapted.IsSuccess()
						? FBlueprintLensLC1RailProjector::Build(
							BoundedAdapted.Explanation)
						: FBlueprintLensLC1RailProjection();
				const FBlueprintLensF12DataAnswerProjection BoundedProjection =
					FBlueprintLensF12DataAnswerProjector::Build(
						BoundedDataAnswer,
						BoundedRail);
				TestTrue(
					*FString::Printf(
						TEXT("a %d-source answer remains structurally admitted and renderable"),
						ProducerCount),
					BoundedAdapted.IsSuccess() &&
						BoundedProjection.IsRenderable(
							BoundedDataAnswer,
							BoundedRail));
				const FBlueprintLensF12DataStationDisclosure* BoundedDisclosure =
					BoundedProjection.Stations.FindByPredicate(
						[&TwoUnitCriterion](
							const FBlueprintLensF12DataStationDisclosure& Disclosure)
						{
							return Disclosure.StationUnitId ==
								TwoUnitCriterion.Id;
						});
				TestTrue(
					*FString::Printf(
						TEXT("the local disclosure bound is false at five and true above five for %d sources"),
						ProducerCount),
					BoundedDisclosure != nullptr &&
						BoundedDisclosure->ValueSourceUnitIds.Num() ==
							ProducerCount &&
						BoundedDisclosure->bValueSourceDisclosureBounded ==
							(ProducerCount >
								BlueprintLensF12DataAnswerBounds::
									MaxValueSourcesPerStation));
			}
			const FBlueprintLensLC1RailLayoutSessionResult TwoUnitSession =
				FBlueprintLensLC1RailLayoutSession::Build(
					TwoUnitRail,
					TwoUnitAdapted.Explanation,
					700.0f);
			const FBlueprintLensLC1RailSurfaceLayout TwoUnitSurface =
				FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
					TwoUnitRail,
					TwoUnitSession,
					TwoUnitSlots,
					700.0f,
					INDEX_NONE,
					FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
					true);
			TestTrue(
				*FString::Printf(
					TEXT("the retained 2/1 Data answer preserves label clearance: %s"),
					*TwoUnitSurface.DiagnosticCode),
				TwoUnitSurface.IsRenderable(TwoUnitRail));
			TestFalse(
				TEXT("the Data answer does not inherit execution entry/then annotations"),
				TwoUnitSurface.Labels.ContainsByPredicate(
					[](const FBlueprintLensLC1RailSurfaceLabel& Label)
					{
						return Label.Key == TEXT("relation-annotation");
					}));
			const FBlueprintLensLC1RailSurfaceLabel* TwoUnitStage =
				TwoUnitSurface.Labels.FindByPredicate(
					[](const FBlueprintLensLC1RailSurfaceLabel& Label)
					{
						return Label.Key == TEXT("stage");
					});
			TestTrue(
				TEXT("the one-relation Data answer uses singular relation grammar"),
				TwoUnitStage != nullptr &&
					TwoUnitStage->Text.Contains(TEXT("1 Data relation")) &&
					!TwoUnitStage->Text.Contains(TEXT("1 Data relations")));
			TSharedRef<SBlueprintLensPanel> NoSourcePanel =
				SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
			NoSourcePanel->M6Presentation.SetPythonReady(true);
			NoSourcePanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
			NoSourcePanel->M6Presentation.SetGraphId(
				TwoUnitDataAnswer.Source.GraphId);
			NoSourcePanel->M6Presentation.ApplySession(Ready, &Views);
			FM6LoadedSessionPacket NoSourcePacket;
			NoSourcePacket.Request.QueryKind = TEXT("data");
			NoSourcePacket.Explanation = TwoUnitDataAnswer;
			NoSourcePacket.SemanticSha256 = FString::ChrN(64, TEXT('e'));
			NoSourcePanel->M6ReadyPacket =
				MakeShared<FM6LoadedSessionPacket>(MoveTemp(NoSourcePacket));
			const TSharedRef<SWidget> NoSourceSurface =
				NoSourcePanel->BuildM6CausalContent();
			TestTrue(
				TEXT("a write with no required_data_producer states that absence on the Data surface"),
				SlateHasWidgetTag(
					NoSourceSurface,
					FName(TEXT(
						"BlueprintLens.Automation.F12NoValueSource"))));
			FBlueprintLensCompositeRailSlots TwoUnitExpandedSlots =
				TwoUnitSlots;
			FBlueprintLensCompositeStationSlot* ExpandedCriterion =
				TwoUnitExpandedSlots.FindStation(TwoUnitCriterion.Id);
			TestNotNull(
				TEXT("the retained 2/1 Data criterion owns its answer disclosure"),
				ExpandedCriterion);
			if (ExpandedCriterion != nullptr &&
				!ExpandedCriterion->BesideAttachments.IsEmpty())
			{
				ExpandedCriterion->BesideAttachments[0].Disclosure =
					EBlueprintLensCompositeDisclosure::Expanded;
				const FBlueprintLensLC1RailSurfaceLayout TwoUnitExpandedSurface =
					FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
						TwoUnitRail,
						TwoUnitSession,
						TwoUnitExpandedSlots,
						700.0f,
						INDEX_NONE,
						FBlueprintLensCompositeRailSlots::DefaultFoldRadius,
						true);
				TestTrue(
					*FString::Printf(
						TEXT("the retained 2/1 Data disclosure expands without losing the rail: %s"),
						*TwoUnitExpandedSurface.DiagnosticCode),
					TwoUnitExpandedSurface.IsRenderable(TwoUnitRail));
			}
		}

		TSharedRef<SBlueprintLensPanel> DataPanel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		DataPanel->M6Presentation.SetPythonReady(true);
		DataPanel->M6Presentation.SetQueryKind(EM6QueryKind::Data);
		DataPanel->M6Presentation.SetGraphId(DataAnswer.Source.GraphId);
		FM6DataMemberRow DataMember;
		DataMember.Name = TEXT("F12Member");
		DataMember.Guid = TEXT("f12-member-guid");
		DataMember.bUsableInFocusedGraph = true;
		DataPanel->M6Presentation.SetDataMemberRows({DataMember});
		TestTrue(
			TEXT("the F12 RED selects a Data query before publishing its packet"),
			DataPanel->M6Presentation.SelectDataMember(DataMember.Guid));
		DataPanel->M6Presentation.ApplySession(Ready, &Views);
		// The current proposal may move after Run. Representation dispatch belongs
		// to the immutable accepted packet/result, not the next query proposal.
		DataPanel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
		FM6LoadedSessionPacket DataPacket;
		DataPacket.Request.QueryKind = TEXT("data");
		DataPacket.Explanation = MoveTemp(DataAnswer);
		DataPacket.SemanticSha256 = FString::ChrN(64, TEXT('d'));
		DataPanel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(DataPacket));

		const TSharedRef<SWidget> DataSurface =
			DataPanel->BuildM6CausalContent();
		TestTrue(
			TEXT("an accepted Data packet reaches the dedicated F12 answer surface"),
			SlateHasWidgetTag(
				DataSurface,
				FName(TEXT("BlueprintLens.Automation.F12DataAnswerRail"))));
		TestFalse(
			TEXT("a Data result is not represented by the execution-rail surface or its caption"),
			SlateHasWidgetTag(
				DataSurface,
				FName(TEXT("BlueprintLens.Automation.SharedExecutionRail"))) ||
				SlateHasWidgetTag(
					DataSurface,
					FName(TEXT("BlueprintLens.Automation.CompositeGuardRail"))));

		const TArray<TSharedRef<SWidget>> VisibleReasonWidgets =
			SlateWidgetsWithTag(
				DataSurface,
				FName(TEXT("BlueprintLens.Automation.F12InclusionReason")));
		TestTrue(
			TEXT("the Data surface exposes tagged per-unit inclusion-reason widgets"),
			!VisibleReasonWidgets.IsEmpty());
		FString VisibleReasonText;
		for (const TSharedRef<SWidget>& ReasonWidget : VisibleReasonWidgets)
		{
			VisibleReasonText += SlateWidgetText(ReasonWidget);
			VisibleReasonText += TEXT("\n");
		}
		TestTrue(
			TEXT("a reason planted in the accepted packet reaches a visible tagged Data station disclosure"),
			VisibleReasonText.Contains(DataReasonSentinel));
		TestTrue(
			TEXT("the absent condition-dependency fact is declared as a visible Data answer limit"),
			SlateHasWidgetTag(
				DataSurface,
				FName(TEXT(
					"BlueprintLens.Automation.F12DeferredConditionDependency"))));
		const TSharedPtr<SWidget> DataScaleSummary = SlateWidgetWithTag(
			DataSurface,
			FName(TEXT("BlueprintLens.Automation.F12ScaleSummary")));
		const TSharedPtr<SWidget> DataOrderSummary = SlateWidgetWithTag(
			DataSurface,
			FName(TEXT("BlueprintLens.Automation.F12OrderSummary")));
		TestTrue(
			TEXT("the Data scale and order summaries are tagged at their semantic source"),
			DataScaleSummary.IsValid() && DataOrderSummary.IsValid());
		if (DataScaleSummary.IsValid() && DataOrderSummary.IsValid())
		{
			const FString DataNarration =
				SlateWidgetText(DataScaleSummary.ToSharedRef()) + TEXT("\n") +
				SlateWidgetText(DataOrderSummary.ToSharedRef());
			TestFalse(
				TEXT("tagged Data narration does not print execution rail-station vocabulary"),
				DataNarration.Contains(
					TEXT("rail stations"), ESearchCase::IgnoreCase));
			TestFalse(
				TEXT("tagged Data narration does not call write dependencies station-to-station relations"),
				DataNarration.Contains(
					TEXT("station-to-station relations"),
					ESearchCase::IgnoreCase));
			TestFalse(
				TEXT("tagged Data narration does not call answer entities rail units"),
				DataNarration.Contains(
					TEXT("rail units"), ESearchCase::IgnoreCase));
			const FString OrderNarration =
				SlateWidgetText(DataOrderSummary.ToSharedRef());
			TestTrue(
				TEXT("the tagged Data reading instruction states the shared vertical proven-before guarantee in Data vocabulary"),
				OrderNarration.Contains(
					TEXT("lower answer position"), ESearchCase::IgnoreCase) &&
					OrderNarration.Contains(
						TEXT("upper answer position"), ESearchCase::IgnoreCase));
		}
		TestTrue(
			TEXT("the Data answer creates the reused composite rail canvas"),
			DataPanel->LC1RailCanvas.IsValid());
		if (DataPanel->LC1RailCanvas.IsValid())
		{
			const FBlueprintLensCompositeRailSlots& DataSlots =
				DataPanel->LC1RailCanvas->GetCompositeSlotsForTesting();
			int32 DisclosedUnitCount = 0;
			TSet<FString> F12DisclosureHostIds;
			TSet<FString> LC3ValueSourceHostIds;
			FString ProjectedValueSourceText;
			for (const FBlueprintLensCompositeStationSlot& Station :
				DataSlots.Stations)
			{
				const FBlueprintLensCompositeAttachment* F12Attachment =
					Station.BesideAttachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == TEXT("F12");
						});
				if (F12Attachment != nullptr)
				{
					F12DisclosureHostIds.Add(Station.UnitId);
					DisclosedUnitCount += F12Attachment->DetailLines.Num();
				}
				const FBlueprintLensCompositeAttachment* LC3Attachment =
					Station.BesideAttachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == TEXT("LC3");
						});
				if (LC3Attachment != nullptr &&
					!LC3Attachment->DetailLines.IsEmpty())
				{
					LC3ValueSourceHostIds.Add(Station.UnitId);
					ProjectedValueSourceText += LC3Attachment->MarkerText;
					ProjectedValueSourceText += TEXT("\n");
					ProjectedValueSourceText += FString::Join(
						LC3Attachment->DetailLines, TEXT("\n"));
				}
			}
			for (const FBlueprintLensCompositeTerminalCapSlot& Cap :
				DataSlots.TerminalCaps)
			{
				const FBlueprintLensCompositeAttachment* F12Attachment =
					Cap.Attachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == TEXT("F12");
						});
				if (F12Attachment != nullptr)
				{
					F12DisclosureHostIds.Add(Cap.UnitId);
					DisclosedUnitCount += F12Attachment->DetailLines.Num();
				}
				const FBlueprintLensCompositeAttachment* LC3Attachment =
					Cap.Attachments.FindByPredicate(
						[](const FBlueprintLensCompositeAttachment& Attachment)
						{
							return Attachment.GrammarId == TEXT("LC3");
						});
				if (LC3Attachment != nullptr &&
					!LC3Attachment->DetailLines.IsEmpty())
				{
					LC3ValueSourceHostIds.Add(Cap.UnitId);
					ProjectedValueSourceText += LC3Attachment->MarkerText;
					ProjectedValueSourceText += TEXT("\n");
					ProjectedValueSourceText += FString::Join(
						LC3Attachment->DetailLines, TEXT("\n"));
				}
			}
			TestTrue(
				TEXT("every write-spine station or terminal cap owns one F12 disclosure"),
				F12DisclosureHostIds.Num() == DataSlots.Stations.Num());
			TestEqual(
				TEXT("the station disclosures cover every unit from the accepted Data Explanation exactly once"),
				DisclosedUnitCount,
				DataPanel->M6ReadyPacket->Explanation.Units.Num());
			TestTrue(
				TEXT("all F12 beside-station disclosures begin collapsed"),
				DataSlots.AreAllAttachmentsCollapsed());
			TestTrue(
				TEXT("required_data_producer units run through the reused LC3 beside-station slot mechanism"),
				!LC3ValueSourceHostIds.IsEmpty());
			TestTrue(
				TEXT("the LC3 beside-station projection carries planted producer identities from the accepted Data packet"),
				ProjectedValueSourceText.Contains(
					DataProducerIdentitySentinel));

			const FBlueprintLensLC1RailSurfaceLayout& DataLayout =
				DataPanel->LC1RailCanvas->GetSurfaceForTesting();
			const FBlueprintLensLC1RailSurfaceLabel* ReasonLabel = nullptr;
			const FBlueprintLensLC1RailSurfaceLabel* StationNameLabel = nullptr;
			for (const FBlueprintLensLC1RailSurfaceLabel& CandidateReason :
				DataLayout.Labels)
			{
				if (!CandidateReason.Key.StartsWith(TEXT("attachment:F12:")))
				{
					continue;
				}
				const FBlueprintLensLC1RailSurfaceLabel* CandidateStationName =
					DataLayout.Labels.FindByPredicate(
						[&CandidateReason](
							const FBlueprintLensLC1RailSurfaceLabel& Label)
						{
							return Label.UnitId == CandidateReason.UnitId &&
								(Label.Key == CandidateReason.UnitId ||
									Label.Key == TEXT("criterion"));
						});
				if (CandidateStationName != nullptr)
				{
					ReasonLabel = &CandidateReason;
					StationNameLabel = CandidateStationName;
					break;
				}
			}
			TestTrue(
				TEXT("a Data station's answer reason receives more horizontal space than its node title"),
				ReasonLabel != nullptr && StationNameLabel != nullptr &&
					ReasonLabel->MeasuredBounds.GetSize().X >
						StationNameLabel->MeasuredBounds.GetSize().X);
		}
		TestFalse(
			TEXT("the always-visible inclusion reason is not presented as a disclosure action"),
			SlateHasWidgetTag(
				DataSurface,
				FName(TEXT(
					"BlueprintLens.Automation.F12DataAnswerDisclosureAction"))));
		const TSharedPtr<SWidget> DataDisclosureAction = SlateWidgetWithTag(
			DataSurface,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC3AttachmentAction")));
		TestTrue(
			TEXT("a Data station with assigned producers exposes the reused LC3 disclosure action"),
			DataDisclosureAction.IsValid());
		if (DataDisclosureAction.IsValid() &&
			DataDisclosureAction->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(DataDisclosureAction.ToSharedRef())
				->SimulateClick();
			const TSharedRef<SWidget> ExpandedData =
				DataPanel->BuildM6CausalContent();
			const TSharedPtr<SWidget> ExpandedValueSourceDetail =
				SlateWidgetWithTag(
					ExpandedData,
					FName(TEXT(
						"BlueprintLens.Automation.F12ValueSourceDetail")));
			TestTrue(
				TEXT("expanding a Data value-source action adds a tagged detail region"),
				ExpandedValueSourceDetail.IsValid());
			if (ExpandedValueSourceDetail.IsValid())
			{
				TestTrue(
					TEXT("expanded Data detail adds producer identity rather than restating the collapsed reason"),
					SlateWidgetText(
						ExpandedValueSourceDetail.ToSharedRef()).Contains(
						DataProducerIdentitySentinel));
			}
			TestFalse(
				TEXT("opening a Data value-source disclosure removes the all-attachments-collapsed state"),
				SlateHasWidgetTag(
					ExpandedData,
					FName(TEXT(
						"BlueprintLens.Automation.CompositeAttachmentsCollapsed"))));
			StaticCastSharedRef<SButton>(DataDisclosureAction.ToSharedRef())
				->SimulateClick();
			const TSharedRef<SWidget> CollapsedDataAgain =
				DataPanel->BuildM6CausalContent();
			TestTrue(
				TEXT("the Data value-source action returns to the default-collapsed state"),
				SlateHasWidgetTag(
					CollapsedDataAgain,
					FName(TEXT(
						"BlueprintLens.Automation.CompositeAttachmentsCollapsed"))));
		}
	}

	{
		const FString LiveLC6Blueprint =
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main");
		const FString StaleLC6Blueprint =
			TEXT("/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix");
		FString CorpusMainTypedIrJson;
		TestTrue(
			TEXT("the LC6 identity regression loads the real corpus typed-IR sidecar"),
			FFileHelper::LoadFileToString(
				CorpusMainTypedIrJson,
				*M7CorpusMainTypedIrPath()));
		const FString CorpusMainTypedIrSha256 =
			Sha256Text(CorpusMainTypedIrJson);
		FBlueprintLensExplanationModel OneBoundaryLC6 =
			BuildLC6LiveBoundaryShape(
				LiveLC6Blueprint,
				TEXT("CURRENT LIVE LC6"),
				{EBlueprintLensSemanticStatus::Opaque});
		const FBlueprintLensLC1RailProjection OneBoundaryRail =
			FBlueprintLensLC1RailProjector::Build(OneBoundaryLC6);
		TestTrue(
			TEXT("the one-boundary live regression reaches the ordinary rail before LC6 adaptation"),
			OneBoundaryRail.IsRenderable());
		const FBlueprintLensLC6LiveExplanationAdapterResult OneBoundaryAdapted =
			FBlueprintLensLC6LiveExplanationAdapter::Build(
				OneBoundaryLC6,
				OneBoundaryRail);
		TestTrue(
			TEXT("a one-track live boundary shape is admitted instead of being rejected for not having four tracks"),
			OneBoundaryAdapted.IsSuccess() &&
				OneBoundaryAdapted.Projection.Tracks.Num() == 1);

		FBlueprintLensExplanationModel FourBoundaryLC6 =
			BuildLC6LiveBoundaryShape(
				LiveLC6Blueprint,
				TEXT("FOUR LIVE LC6"),
				{
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Unsupported});
		const FBlueprintLensLC1RailProjection FourBoundaryRail =
			FBlueprintLensLC1RailProjector::Build(FourBoundaryLC6);
		const FBlueprintLensLC6LiveExplanationAdapterResult FourBoundaryAdapted =
			FBlueprintLensLC6LiveExplanationAdapter::Build(
				FourBoundaryLC6,
				FourBoundaryRail);
		TestTrue(
			TEXT("the measured four-instance live upper bound is admitted after the greater-than gate runs"),
			FourBoundaryAdapted.IsSuccess() &&
				FourBoundaryAdapted.Projection.Tracks.Num() ==
					FBlueprintLensLC6LiveExplanationAdapter::MaxLiveBoundaryTracks);
		FBlueprintLensExplanationModel FiveBoundaryLC6 =
			BuildLC6LiveBoundaryShape(
				LiveLC6Blueprint,
				TEXT("FIVE LIVE LC6"),
				{
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Unsupported});
		const FBlueprintLensLC1RailProjection FiveBoundaryRail =
			FBlueprintLensLC1RailProjector::Build(FiveBoundaryLC6);
		const FBlueprintLensLC6LiveExplanationAdapterResult FiveBoundaryAdapted =
			FBlueprintLensLC6LiveExplanationAdapter::Build(
				FiveBoundaryLC6,
				FiveBoundaryRail);
		TestTrue(
			TEXT("the numeric live LC6 admission gate rejects only after the measured upper bound is exceeded"),
			!FiveBoundaryAdapted.IsSuccess() &&
				FiveBoundaryAdapted.DiagnosticCode.Contains(TEXT("BOUND_EXCEEDED")));

		FBlueprintLensExplanationModel CriterionBoundaryLC6 =
			BuildLC6LiveBoundaryShape(
				LiveLC6Blueprint,
				TEXT("CRITERION STATUS LC6"),
				{EBlueprintLensSemanticStatus::Opaque},
				EBlueprintLensSemanticStatus::Unsupported);
		const FBlueprintLensLC1RailProjection CriterionBoundaryRail =
			FBlueprintLensLC1RailProjector::Build(CriterionBoundaryLC6);
		TestEqual(
			TEXT("rail caps cover every non-supported semantic-status unit including the criterion"),
			CriterionBoundaryRail.BoundaryCaps.Num(),
			2);
		const FBlueprintLensLC6LiveExplanationAdapterResult CriterionBoundaryAdapted =
			FBlueprintLensLC6LiveExplanationAdapter::Build(
				CriterionBoundaryLC6,
				CriterionBoundaryRail);
		TestTrue(
			TEXT("the live LC6 ledger reconciles the exact cap batch before rendering"),
			CriterionBoundaryAdapted.IsSuccess() &&
				CriterionBoundaryAdapted.BoundaryUnitIds.Num() ==
					CriterionBoundaryRail.BoundaryCaps.Num());
		if (CriterionBoundaryAdapted.IsSuccess())
		{
			for (const FBlueprintLensLC1RailBoundaryCap& Cap :
				CriterionBoundaryRail.BoundaryCaps)
			{
				const FBlueprintLensLC6Track* Track =
					CriterionBoundaryAdapted.Projection.Tracks.FindByPredicate(
						[&Cap](const FBlueprintLensLC6Track& Candidate)
						{
							return Candidate.BoundaryNodeId == Cap.UnitId;
						});
				TestTrue(
					TEXT("each LC6 track carries the same status and reason as its matching rail cap"),
					Track != nullptr &&
						Track->Status.Equals(
							Cap.SemanticStatus == EBlueprintLensSemanticStatus::Opaque
								? TEXT("opaque")
								: Cap.SemanticStatus == EBlueprintLensSemanticStatus::Unsupported
									? TEXT("unsupported")
									: TEXT("uncertain"),
							ESearchCase::IgnoreCase) &&
						Track->Reason == Cap.Disclosure);
			}
		}

		FBlueprintLensExplanationModel LiveLC6 =
			BuildLC6LiveBoundaryShape(
				LiveLC6Blueprint,
				TEXT("CURRENT LIVE LC6"),
				{
					EBlueprintLensSemanticStatus::Opaque,
					EBlueprintLensSemanticStatus::Opaque});
		LiveLC6.Source.IrPath = M7CorpusMainTypedIrPath();
		LiveLC6.Source.IrSha256 = CorpusMainTypedIrSha256;
		const TArray<FString> DuplicateOpaqueSourceNodeIds = {
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:EventGraph::node::26cf4339-4ef2-59b7-538e-24832ae84f54"),
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:EventGraph::node::ee199906-42a7-ef05-cc77-99b0754e3844")};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			LiveLC6.Units[Index].Title = TEXT("CURRENT LIVE LC6 PrintString");
			LiveLC6.Units[Index].SourceReferences[0].SourceNodeId =
				DuplicateOpaqueSourceNodeIds[Index];
			LiveLC6.Units[Index].SourceReferences[0].NativeNodeGuid =
				DuplicateOpaqueSourceNodeIds[Index].Right(36);
		}
		FBlueprintLensExplanationModel StaleLC6 =
			BuildLC6LiveBoundaryShape(
				StaleLC6Blueprint,
				TEXT("STALE FIXTURE LC6"),
				{EBlueprintLensSemanticStatus::Uncertain});
		TSharedRef<SBlueprintLensPanel> LC6Panel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		LC6Panel->Model =
			MakeShared<FBlueprintLensExplanationModel>(MoveTemp(StaleLC6));
		LC6Panel->M6Presentation.SetPythonReady(true);
		LC6Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
		LC6Panel->M6Presentation.SetGraphId(LiveLC6.Source.GraphId);
		LC6Panel->M6Presentation.ApplySession(Ready, &Views);
		FM6LoadedSessionPacket LC6Packet;
		LC6Packet.Request.QueryKind = TEXT("execution");
		LC6Packet.Explanation = LiveLC6;
		LC6Packet.SemanticSha256 = FString::ChrN(64, TEXT('6'));
		LC6Panel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(LC6Packet));

		const TSharedRef<SWidget> CollapsedLC6 =
			LC6Panel->BuildM6CausalContent();
		TestTrue(
			TEXT("the live LC6 packet reaches the ordinary composite rail before the cap attachment"),
			LC6Panel->LC1RailCanvas.IsValid() &&
				LC6Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
					.SourceBlueprintAssetPath == LiveLC6Blueprint);
		const TArray<TSharedRef<SWidget>> LC6CapActions = SlateWidgetsWithTag(
			CollapsedLC6,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC6TerminalDisclosureAction")));
		TestEqual(
			TEXT("one live boundary ledger exposes one default-collapsed terminal-cap action"),
			LC6CapActions.Num(),
			1);
		const FBlueprintLensCompositeTerminalCapSlot* LC6HostCap = nullptr;
		if (LC6Panel->LC1RailCanvas.IsValid())
		{
			LC6HostCap = LC6Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
				.TerminalCaps.FindByPredicate(
					[](const FBlueprintLensCompositeTerminalCapSlot& Cap)
					{
						return Cap.Attachments.ContainsByPredicate(
							[](const FBlueprintLensCompositeAttachment& Attachment)
							{
								return Attachment.GrammarId == TEXT("LC6");
							});
					});
		}
		TestTrue(
			TEXT("the LC6 terminal-cap attachment itself begins collapsed"),
			LC6HostCap != nullptr &&
				LC6HostCap->Attachments.ContainsByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC6") &&
							Attachment.Disclosure ==
								EBlueprintLensCompositeDisclosure::Collapsed;
					}));
		TestFalse(
			TEXT("the accepted Four-Track widget is absent before the terminal-cap action"),
			SlateHasWidgetTag(
				CollapsedLC6,
				FName(TEXT(
					"BlueprintLens.Automation.CompositeLC6FourTrackSurface"))));
		if (LC6CapActions.Num() == 1 &&
			LC6CapActions[0]->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(LC6CapActions[0])->SimulateClick();
		}
		const TSharedRef<SWidget> ExpandedLC6 =
			LC6Panel->BuildM6CausalContent();
		const TSharedPtr<SWidget> LC6Surface = SlateWidgetWithTag(
			ExpandedLC6,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC6FourTrackSurface")));
		TestTrue(
			TEXT("the admitted live boundary ledger renders through the one accepted Four-Track widget"),
			LC6Surface.IsValid() &&
				LC6Surface->GetTypeAsString() == TEXT("SBlueprintLensLC6FourTrack"));
		if (LC6Surface.IsValid() &&
			LC6Surface->GetTypeAsString() == TEXT("SBlueprintLensLC6FourTrack"))
		{
			const FBlueprintLensLC6Layout& LiveLC6Layout =
				StaticCastSharedPtr<SBlueprintLensLC6FourTrack>(LC6Surface)
					->GetLayoutForTesting();
			TestFalse(
				TEXT("the live layout omits the query-budget owner band when that kind has zero instances"),
				LiveLC6Layout.Labels.ContainsByPredicate(
					[](const FBlueprintLensLC6Label& Label)
					{
						return Label.Id == TEXT("overview.query.owner");
					}));
			const FBlueprintLensLC6Label* const LiveDetailPrompt =
				LiveLC6Layout.Labels.FindByPredicate(
					[](const FBlueprintLensLC6Label& Label)
					{
						return Label.Id == TEXT("detail.empty.title");
					});
			TestTrue(
				TEXT("the accepted live Four-Track surface names instances rather than fixture scenarios"),
				LiveDetailPrompt != nullptr &&
					LiveDetailPrompt->Text.Contains(
						TEXT("instance"), ESearchCase::IgnoreCase) &&
					!LiveDetailPrompt->Text.Contains(
						TEXT("scenario"), ESearchCase::IgnoreCase));
			int32 VisibleIdentityLabels = 0;
			for (const FBlueprintLensLC6Label& Label : LiveLC6Layout.Labels)
			{
				if (Label.Id.EndsWith(TEXT(".boundary")) &&
					(Label.Text.Contains(TEXT("26CF4339")) ||
						Label.Text.Contains(TEXT("EE199906"))) &&
					Label.Bounds.GetSize().X >= 120.0f)
				{
					++VisibleIdentityLabels;
				}
			}
			TestEqual(
				TEXT("both duplicate live opaque identities receive visible-width labels on the accepted surface"),
				VisibleIdentityLabels,
				2);
		}
		const FString LC6SurfaceText = LC6Surface.IsValid()
			? SlateWidgetText(LC6Surface.ToSharedRef())
			: FString();
		TestTrue(
			TEXT("the Explanation reaching the LC6 chain is the current packet rather than the construction fixture"),
			LC6Surface.IsValid() &&
				LC6SurfaceText.Contains(TEXT("CURRENT LIVE LC6")) &&
				!LC6SurfaceText.Contains(TEXT("STALE FIXTURE LC6")));
		TestTrue(
			TEXT("duplicate live opaque instances retain two distinct source identities through the display-label mechanism"),
			LC6Surface.IsValid() &&
				LC6SurfaceText.Contains(TEXT("26CF4339")) &&
				LC6SurfaceText.Contains(TEXT("EE199906")));
		const TSharedRef<SWidget> LiveLC6Detail = LC6Panel->BuildLC6Detail(
			LC6Panel->BuildCurrentLC6Projection());
		const TSharedPtr<SWidget> LiveLC6DetailPrompt = SlateWidgetWithTag(
			LiveLC6Detail,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC6LiveDetailPrompt")));
		TestTrue(
			TEXT("the live detail prompt names track instances rather than fixture scenarios"),
			LiveLC6DetailPrompt.IsValid() &&
				SlateWidgetText(LiveLC6DetailPrompt.ToSharedRef()).Contains(
					TEXT("instance"), ESearchCase::IgnoreCase) &&
				!SlateWidgetText(LiveLC6DetailPrompt.ToSharedRef()).Contains(
					TEXT("scenario"), ESearchCase::IgnoreCase));
		const TSharedPtr<SWidget> AbsentTracks = SlateWidgetWithTag(
			ExpandedLC6,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC6AbsentTracksDisclosure")));
		TestTrue(
			TEXT("absent boundary kinds are visibly disclosed rather than drawn as empty tracks"),
			AbsentTracks.IsValid() &&
				AbsentTracks->GetVisibility() == EVisibility::HitTestInvisible &&
				SlateWidgetText(AbsentTracks.ToSharedRef()).Contains(
					TEXT("uncertain"), ESearchCase::IgnoreCase) &&
				SlateWidgetText(AbsentTracks.ToSharedRef()).Contains(
					TEXT("query"), ESearchCase::IgnoreCase));
		const TSharedPtr<SWidget> TruthOwnerContribution = SlateWidgetWithTag(
			ExpandedLC6,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC6TruthOwnerContribution")));
		TestTrue(
			TEXT("the live surface visibly states the truth-owner separation it adds over the caps"),
			TruthOwnerContribution.IsValid() &&
				TruthOwnerContribution->GetVisibility() ==
					EVisibility::HitTestInvisible &&
				SlateWidgetText(TruthOwnerContribution.ToSharedRef()).Contains(
					TEXT("core"), ESearchCase::IgnoreCase) &&
				SlateWidgetText(TruthOwnerContribution.ToSharedRef()).Contains(
					TEXT("query"), ESearchCase::IgnoreCase));
	}

	{
		FString CorpusMainTypedIrJson;
		FString SlicingProbeTypedIrJson;
		TestTrue(
			TEXT("the LC5 RED loads the real BP_LensCorpus_Main typed-IR sidecar"),
			FFileHelper::LoadFileToString(
				CorpusMainTypedIrJson,
				*M7CorpusMainTypedIrPath()));
		TestTrue(
			TEXT("the LC5 RED loads the real BP_SlicingProbe typed-IR sidecar"),
			FFileHelper::LoadFileToString(
				SlicingProbeTypedIrJson,
				*M7SlicingProbeTypedIrPath()));
		const FString CorpusMainTypedIrSha256 =
			Sha256Text(CorpusMainTypedIrJson);
		const FString SlicingProbeTypedIrSha256 =
			Sha256Text(SlicingProbeTypedIrJson);
		const FString CorpusMainBlueprint =
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main");
		const FString SlicingProbeBlueprint =
			TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe");
		const FString RunEncounterSourceNodeId =
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:EventGraph::node::7e42649a-4ac6-143c-3d34-368d850b902b");
		const FString RunSecondaryPathSourceNodeId =
			TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph::node::b99ff9f7-4b6c-e4cc-66d9-50830a517a8f");
		const FString NonSelfPrintStringSourceNodeId =
			TEXT("/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:EventGraph::node::26cf4339-4ef2-59b7-538e-24832ae84f54");
		const FString CalculateRecoverySourceNodeId =
			TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph::node::efbd1d7a-47d3-fd55-7775-26bd488ee92d");

		const auto BuildLC5Panel = [&Ready, &Views](
			FBlueprintLensExplanationModel LiveExplanation,
			const FString& StaleIdentity)
		{
			TSharedRef<SBlueprintLensPanel> Result =
				SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
			FBlueprintLensExplanationModel StaleModel =
				BuildLC6LiveBoundaryShape(
					TEXT("/Game/LensCorpus/BP_ConstructionFixture.BP_ConstructionFixture"),
					StaleIdentity,
					{EBlueprintLensSemanticStatus::Opaque});
			Result->Model = MakeShared<FBlueprintLensExplanationModel>(
				MoveTemp(StaleModel));
			Result->M6Presentation.SetPythonReady(true);
			Result->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
			Result->M6Presentation.SetGraphId(LiveExplanation.Source.GraphId);
			Result->M6Presentation.ApplySession(Ready, &Views);
			FM6LoadedSessionPacket Packet;
			Packet.Request.QueryKind = TEXT("execution");
			Packet.Explanation = MoveTemp(LiveExplanation);
			Packet.SemanticSha256 = FString::ChrN(64, TEXT('5'));
			Result->M6ReadyPacket =
				MakeShared<FM6LoadedSessionPacket>(MoveTemp(Packet));
			return Result;
		};

		FBlueprintLensExplanationModel RunEncounter = BuildLC5LiveCallShape(
			CorpusMainBlueprint,
			M7CorpusMainTypedIrPath(),
			CorpusMainTypedIrSha256,
			TEXT("RunEncounter"),
			RunEncounterSourceNodeId,
			TEXT("CURRENT LIVE RUN ENCOUNTER"));
		const FBlueprintLensLC1TypedIrFacts CorpusFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(
				RunEncounter.Source,
				false);
		const FBlueprintLensLC5LiveTypedIrAdapterResult AtBound =
			FBlueprintLensLC5LiveTypedIrAdapter::Build(
				RunEncounter,
				CorpusFacts);
		TestTrue(
			TEXT("the strictly-greater LC5 body bound admits the measured 16-node RunEncounter body"),
			AtBound.Cases.ContainsByPredicate(
				[](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CalleeName == TEXT("RunEncounter") &&
						Candidate.IsRenderable();
				}));
		const FBlueprintLensLC5LiveCallCase* RunEncounterCase =
			AtBound.Cases.FindByPredicate(
				[](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CalleeName == TEXT("RunEncounter") &&
						Candidate.IsRenderable();
				});
		TestNotNull(
			TEXT("the LC5 order RED resolves the real renderable RunEncounter case"),
			RunEncounterCase);
		if (RunEncounterCase != nullptr)
		{
			TestEqual(
				TEXT("the LC5 caller identity comes from the current Explanation source graph"),
				RunEncounterCase->Projection.CallerGraphId,
				RunEncounter.Source.GraphId);
			TestNotEqual(
				TEXT("the LC5 projection keeps caller and callee graph identities separate"),
				RunEncounterCase->Projection.CallerGraphId,
				RunEncounterCase->Projection.CalleeGraphName);
			TSet<FString> LegendSemanticIds;
			for (const FBlueprintLensLC5LegendEntry& Entry :
				RunEncounterCase->Projection.LegendEntries)
			{
				LegendSemanticIds.Add(Entry.SemanticId);
				if (Entry.SemanticId == TEXT("value_relation"))
				{
					TestTrue(
						TEXT("the dynamic LC5 value legend is backed by a retained data relation"),
						RunEncounterCase->Projection.InternalRelations.ContainsByPredicate(
							[](const FBlueprintLensLC5InternalRelation& Relation)
							{
								return Relation.Kind == TEXT("data");
							}));
				}
			}
			TestEqual(
				TEXT("the dynamic LC5 legend has one record per present semantic family"),
				LegendSemanticIds.Num(),
				RunEncounterCase->Projection.LegendEntries.Num());
			TArray<TPair<float, FBlueprintLensLC5Layout>> RunEncounterLayouts;
			for (const float Width : {430.0f, 480.0f, 700.0f})
			{
				RunEncounterLayouts.Emplace(
					Width,
					FBlueprintLensLC5LayoutBuilder::Build(
						RunEncounterCase->Projection,
						Width));
			}
			for (const TPair<float, FBlueprintLensLC5Layout>& WidthLayout :
				RunEncounterLayouts)
			{
				const FString WidthLabel = FString::Printf(
					TEXT("%.0fpx"),
					WidthLayout.Key);
				TestEqual(
					*FString::Printf(
						TEXT("the %s LC5 layout completes all geometry invariants"),
						*WidthLabel),
					WidthLayout.Value.DiagnosticCode,
					FString(TEXT("LC5_LAYOUT_COMPLETE")));
				TestTrue(
					*FString::Printf(
						TEXT("the %s LC5 layout has no label collisions"),
						*WidthLabel),
					WidthLayout.Value.HasNoLabelCollisions());
				TestTrue(
					*FString::Printf(
						TEXT("the %s LC5 layout has no route-node collisions"),
						*WidthLabel),
					WidthLayout.Value.HasNoRouteNodeCollisions());
				TestTrue(
					*FString::Printf(
						TEXT("the %s LC5 layout has complete endpoint glyphs"),
						*WidthLabel),
					WidthLayout.Value.HasCompleteEndpointGlyphs());
				TestTrue(
					*FString::Printf(
						TEXT("the %s LC5 layout preserves strict static ranks"),
						*WidthLabel),
					WidthLayout.Value.HasStrictStaticRankOrder());
			}
			const FBlueprintLensLC5Layout& RealRunEncounterLayout =
				RunEncounterLayouts.Last().Value;
			FString LC5LayoutSeedPath;
			TestTrue(
				TEXT("the LC5 geometry authority is exported for 430, 480 and 700 evidence"),
				WriteLC5LayoutSeed(
					RunEncounterCase->Projection,
					RunEncounterLayouts,
					LC5LayoutSeedPath));
			AddInfo(FString::Printf(
				TEXT("LC5_LAYOUT_SEED=%s"),
				*LC5LayoutSeedPath));
			int32 CheckedInternalRelations = 0;
			TArray<FString> NonForwardRelationIds;
			for (const FBlueprintLensLC5InternalRelation& Relation :
				RunEncounterCase->Projection.InternalRelations)
			{
				const FBlueprintLensLayoutNodePlacement* Source =
					RealRunEncounterLayout.LayoutLedger.Nodes.FindByPredicate(
						[&Relation](const FBlueprintLensLayoutNodePlacement& Node)
						{
							return Node.UnitId == Relation.SourceOccurrenceId;
						});
				const FBlueprintLensLayoutNodePlacement* Target =
					RealRunEncounterLayout.LayoutLedger.Nodes.FindByPredicate(
						[&Relation](const FBlueprintLensLayoutNodePlacement& Node)
						{
							return Node.UnitId == Relation.TargetOccurrenceId;
						});
				++CheckedInternalRelations;
				if (Source == nullptr || Target == nullptr ||
					Source->Position.Y >= Target->Position.Y)
				{
					NonForwardRelationIds.Add(Relation.RelationId);
				}
			}
			TestEqual(
				TEXT("the strict-order RED checks all 15 real RunEncounter internal relations"),
				CheckedInternalRelations,
				15);
			TestTrue(
				*FString::Printf(
					TEXT("every real RunEncounter internal relation advances to a lower static rank; offenders=%s"),
					*FString::Join(NonForwardRelationIds, TEXT(","))),
				NonForwardRelationIds.IsEmpty());
			TestTrue(
				TEXT("the accepted live LC5 layout has no label collisions"),
				RealRunEncounterLayout.HasNoLabelCollisions());
			TestTrue(
				TEXT("the accepted live LC5 layout has no route-node collisions"),
				RealRunEncounterLayout.HasNoRouteNodeCollisions());
			TestTrue(
				TEXT("the accepted live LC5 layout accounts source and target glyphs for every relation"),
				RealRunEncounterLayout.HasCompleteEndpointGlyphs());
			TestTrue(
				TEXT("the accepted live LC5 layout enforces strict static rank order"),
				RealRunEncounterLayout.HasStrictStaticRankOrder());

			const FBlueprintLensLC5ContextBoundary* CallEnter =
				RunEncounterCase->Projection.ContextBoundaries.FindByPredicate(
					[](const FBlueprintLensLC5ContextBoundary& Boundary)
					{
						return Boundary.Kind == TEXT("call_enter");
					});
			const FBlueprintLensLayoutEdgeRequest* CallEnterEdge =
				CallEnter != nullptr
					? RealRunEncounterLayout.LayoutRequest.Edges.FindByPredicate(
						[CallEnter](const FBlueprintLensLayoutEdgeRequest& Edge)
						{
							return Edge.RelationId == CallEnter->RelationId;
						})
					: nullptr;
			TestTrue(
				TEXT("the call-enter relation is classified as a Portal rather than execution"),
				CallEnterEdge != nullptr &&
					CallEnterEdge->Family == EBlueprintLensLayoutRelationFamily::Portal);

			FBlueprintLensLC5Layout PlantedRouteCollision =
				RealRunEncounterLayout;
			if (!PlantedRouteCollision.LayoutLedger.Edges.IsEmpty())
			{
				FBlueprintLensLayoutEdgePlacement& Edge =
					PlantedRouteCollision.LayoutLedger.Edges[0];
				const FBlueprintLensLayoutNodePlacement* UnrelatedNode =
					PlantedRouteCollision.LayoutLedger.Nodes.FindByPredicate(
						[&Edge](const FBlueprintLensLayoutNodePlacement& Node)
						{
							return Node.UnitId != Edge.SourceUnitId &&
								Node.UnitId != Edge.TargetUnitId;
						});
				if (UnrelatedNode != nullptr)
				{
					Edge.BendPoints = {
						UnrelatedNode->Position + UnrelatedNode->Size * 0.5f};
				}
				TestFalse(
					TEXT("the LC5 geometry invariant rejects a planted route through an unrelated node"),
					PlantedRouteCollision.HasNoTextOrRouteCollisions());
			}
		}
		FBlueprintLensLC1TypedIrFacts CyclicFacts = CorpusFacts;
		FBlueprintLensLC1GraphFact* CyclicRunEncounterGraph =
			CyclicFacts.Graphs.FindByPredicate(
				[](const FBlueprintLensLC1GraphFact& Graph)
				{
					return Graph.GraphName == TEXT("RunEncounter");
				});
		TestNotNull(
			TEXT("the cyclic LC5 regression resolves the real RunEncounter graph"),
			CyclicRunEncounterGraph);
		if (CyclicRunEncounterGraph != nullptr)
		{
			TSet<FString> SourceNodeIds;
			TSet<FString> TargetNodeIds;
			for (const FBlueprintLensLC1EdgeFact& Edge : CyclicFacts.Edges)
			{
				if (Edge.GraphId == CyclicRunEncounterGraph->GraphId)
				{
					SourceNodeIds.Add(Edge.SourceNodeId);
					TargetNodeIds.Add(Edge.TargetNodeId);
				}
			}
			const FString* EntryNodeId =
				CyclicRunEncounterGraph->NodeIds.FindByPredicate(
					[&TargetNodeIds](const FString& NodeId)
					{
						return !TargetNodeIds.Contains(NodeId);
					});
			const FString* ExitNodeId =
				CyclicRunEncounterGraph->NodeIds.FindByPredicate(
					[&SourceNodeIds](const FString& NodeId)
					{
						return !SourceNodeIds.Contains(NodeId);
					});
			TestNotNull(
				TEXT("the cyclic LC5 regression resolves the body entry"),
				EntryNodeId);
			TestNotNull(
				TEXT("the cyclic LC5 regression resolves a body exit"),
				ExitNodeId);
			if (EntryNodeId != nullptr && ExitNodeId != nullptr)
			{
				FBlueprintLensLC1EdgeFact BackEdge;
				BackEdge.EdgeId = TEXT("synthetic:RunEncounter::cycle");
				BackEdge.GraphId = CyclicRunEncounterGraph->GraphId;
				BackEdge.Kind = TEXT("execution");
				BackEdge.SourceNodeId = *ExitNodeId;
				BackEdge.SourcePinId = TEXT("synthetic:cycle:out");
				BackEdge.TargetNodeId = *EntryNodeId;
				BackEdge.TargetPinId = TEXT("synthetic:cycle:in");
				BackEdge.bDirectionIsValid = true;
				CyclicRunEncounterGraph->EdgeIds.Add(BackEdge.EdgeId);
				CyclicFacts.Edges.Add(MoveTemp(BackEdge));
			}
		}
		const FBlueprintLensLC5LiveTypedIrAdapterResult CyclicResult =
			FBlueprintLensLC5LiveTypedIrAdapter::Build(
				RunEncounter,
				CyclicFacts);
		TestTrue(
			TEXT("a cyclic exported body fails closed with an explicit no-order diagnostic"),
			CyclicResult.Cases.ContainsByPredicate(
				[](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CalleeName == TEXT("RunEncounter") &&
						Candidate.State == EBlueprintLensLC5LiveClaimState::Refused &&
						Candidate.DiagnosticCode ==
							TEXT("LC5_LIVE_CALLEE_BODY_CYCLIC") &&
						!Candidate.ReaderStatement.IsEmpty() &&
						!Candidate.IsRenderable();
				}));
		FBlueprintLensLC1TypedIrFacts BeyondBoundFacts = CorpusFacts;
		FBlueprintLensLC1GraphFact* RunEncounterGraph =
			BeyondBoundFacts.Graphs.FindByPredicate(
				[](const FBlueprintLensLC1GraphFact& Graph)
				{
					return Graph.GraphName == TEXT("RunEncounter");
				});
		TestNotNull(
			TEXT("the bound regression resolves the real RunEncounter graph"),
			RunEncounterGraph);
		if (RunEncounterGraph != nullptr)
		{
			const FString ExtraNodeId =
				TEXT("synthetic:RunEncounter::node::beyond-live-bound");
			RunEncounterGraph->NodeIds.Add(ExtraNodeId);
			FBlueprintLensLC1NodeFact ExtraNode;
			ExtraNode.SourceNodeId = ExtraNodeId;
			ExtraNode.GraphId = RunEncounterGraph->GraphId;
			ExtraNode.NodeClass =
				TEXT("/Script/BlueprintGraph.K2Node_Knot");
			ExtraNode.NativeTitle = TEXT("BOUND SENTINEL");
			BeyondBoundFacts.NodesBySourceNodeId.Add(
				ExtraNodeId,
				MoveTemp(ExtraNode));
		}
		const FBlueprintLensLC5LiveTypedIrAdapterResult BeyondBound =
			FBlueprintLensLC5LiveTypedIrAdapter::Build(
				RunEncounter,
				BeyondBoundFacts);
		TestTrue(
			TEXT("the LC5 adapter executes a strictly-greater-than-16 rejection at 17 body nodes"),
			BeyondBound.Cases.ContainsByPredicate(
				[](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CalleeName == TEXT("RunEncounter") &&
						Candidate.DiagnosticCode ==
							TEXT("LC5_LIVE_CALLEE_BODY_BOUND_EXCEEDED") &&
						!Candidate.IsRenderable();
				}));

		const FBlueprintLensExplanationModel CalculateRecovery =
			BuildLC5LiveCallShape(
				SlicingProbeBlueprint,
				M7SlicingProbeTypedIrPath(),
				SlicingProbeTypedIrSha256,
				TEXT("CalculateRecovery"),
				CalculateRecoverySourceNodeId,
				TEXT("CURRENT LIVE PURE CALL"));
		const FBlueprintLensLC1TypedIrFacts SlicingProbeFacts =
			FBlueprintLensLC1TypedIrFactLoader::LoadFile(
				CalculateRecovery.Source,
				false);
		const FBlueprintLensLC5LiveTypedIrAdapterResult PureCall =
			FBlueprintLensLC5LiveTypedIrAdapter::Build(
				CalculateRecovery,
				SlicingProbeFacts);
		TestTrue(
			TEXT("the real pure non-latent self-context call is declared as an LC5_INTRA_BP_PURE_CALL_V1 instance"),
			PureCall.Cases.ContainsByPredicate(
				[](const FBlueprintLensLC5LiveCallCase& Candidate)
				{
					return Candidate.CalleeName == TEXT("CalculateRecovery") &&
						Candidate.State ==
							EBlueprintLensLC5LiveClaimState::FrozenConditionInstance &&
						Candidate.ReaderStatement.Contains(
							TEXT("LC5_INTRA_BP_PURE_CALL_V1")) &&
						Candidate.IsRenderable();
				}));
		TSharedRef<SBlueprintLensPanel> RunEncounterPanel = BuildLC5Panel(
			MoveTemp(RunEncounter),
			TEXT("STALE CONSTRUCTION FIXTURE LC5"));
		const TSharedRef<SWidget> CollapsedRunEncounter =
			RunEncounterPanel->BuildM6CausalContent();
		TestTrue(
			TEXT("the RunEncounter RED reaches a real opaque-call terminal cap before LC5 adaptation"),
			RunEncounterPanel->LC1RailCanvas.IsValid() &&
				RunEncounterPanel->LC1RailCanvas->GetCompositeSlotsForTesting()
					.TerminalCaps.ContainsByPredicate(
						[](const FBlueprintLensCompositeTerminalCapSlot& Cap)
						{
							return Cap.UnitId == TEXT("unit.boundary.0");
						}));
		const TArray<TSharedRef<SWidget>> LC5Actions = SlateWidgetsWithTag(
			CollapsedRunEncounter,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC5TerminalDisclosureAction")));
		TestEqual(
			TEXT("RunEncounter exposes one default-collapsed LC5 terminal disclosure action"),
			LC5Actions.Num(),
			1);
		if (LC5Actions.Num() == 1 &&
			LC5Actions[0]->GetTypeAsString() == TEXT("SButton"))
		{
			StaticCastSharedRef<SButton>(LC5Actions[0])->SimulateClick();
		}
		const TSharedRef<SWidget> ExpandedRunEncounter =
			RunEncounterPanel->BuildM6CausalContent();
		const TSharedPtr<SWidget> LC5Surface = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC5TypedPortalSurface")));
		TestTrue(
			TEXT("RunEncounter renders through the one accepted Typed Portal widget"),
			LC5Surface.IsValid() &&
				LC5Surface->GetTypeAsString() ==
					TEXT("SBlueprintLensLC5TypedPortal"));
		if (LC5Surface.IsValid() &&
			LC5Surface->GetTypeAsString() ==
				TEXT("SBlueprintLensLC5TypedPortal"))
		{
			const FBlueprintLensLC5Layout& Layout =
				StaticCastSharedPtr<SBlueprintLensLC5TypedPortal>(LC5Surface)
					->GetLayoutForTesting();
			TestEqual(
				TEXT("the accepted portal receives the call occurrence plus all 16 RunEncounter body nodes"),
				Layout.LayoutRequest.Nodes.Num(),
				17);
		}
		const TSharedPtr<SWidget> LC5ClaimBoundary = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC5ClaimBoundaryDisclosure")));
		const FString LC5ClaimText = LC5ClaimBoundary.IsValid()
			? SlateWidgetText(LC5ClaimBoundary.ToSharedRef())
			: FString();
		TestTrue(
			TEXT("the current live packet rather than Model reaches LC5 and states the impure-call claim boundary"),
			LC5ClaimBoundary.IsValid() &&
				LC5ClaimText.Contains(TEXT("BP_LensCorpus_Main")) &&
				LC5ClaimText.Contains(TEXT("RunEncounter")) &&
				LC5ClaimText.Contains(TEXT("LC5_INTRA_BP_PURE_CALL_V1")) &&
				LC5ClaimText.Contains(TEXT("impure"), ESearchCase::IgnoreCase) &&
				!LC5ClaimText.Contains(TEXT("STALE CONSTRUCTION FIXTURE LC5")));
		const TSharedPtr<SWidget> StaticOrderDisclosure = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT("BlueprintLens.Automation.LC5StaticOrderDisclosure")));
		const TSharedPtr<SWidget> ReadingKey = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT("BlueprintLens.Automation.LC5ReadingKey")));
		const TSharedPtr<SWidget> CallerRole = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT("BlueprintLens.Automation.LC5CallerRole")));
		const TSharedPtr<SWidget> CalleeRole = SlateWidgetWithTag(
			ExpandedRunEncounter,
			FName(TEXT("BlueprintLens.Automation.LC5CalleeRole")));
		TestTrue(
			TEXT("the live LC5 surface visibly discloses its static vertical-order guarantee"),
			StaticOrderDisclosure.IsValid());
		TestTrue(
			TEXT("the live LC5 surface carries an in-surface reading key"),
			ReadingKey.IsValid());
		TestTrue(
			TEXT("the caller role is visibly tagged and names the seeded EventGraph identity"),
			CallerRole.IsValid() &&
				SlateWidgetText(CallerRole.ToSharedRef()).Contains(TEXT("EventGraph")));
		TestTrue(
			TEXT("the callee role is visibly tagged and names the seeded RunEncounter identity"),
			CalleeRole.IsValid() &&
				SlateWidgetText(CalleeRole.ToSharedRef()).Contains(TEXT("RunEncounter")));

		FBlueprintLensExplanationModel RunSecondaryPath = BuildLC5LiveCallShape(
			SlicingProbeBlueprint,
			M7SlicingProbeTypedIrPath(),
			SlicingProbeTypedIrSha256,
			TEXT("RunSecondaryPath"),
			RunSecondaryPathSourceNodeId,
			TEXT("CURRENT LIVE MISSING BODY"));
		TSharedRef<SBlueprintLensPanel> MissingBodyPanel = BuildLC5Panel(
			MoveTemp(RunSecondaryPath),
			TEXT("STALE MISSING BODY FIXTURE"));
		const TSharedRef<SWidget> MissingBodySurface =
			MissingBodyPanel->BuildM6CausalContent();
		const TSharedPtr<SWidget> MissingBodyDisclosure = SlateWidgetWithTag(
			MissingBodySurface,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC5RefusalDisclosure")));
		TestTrue(
			TEXT("RunSecondaryPath fails closed with a visible callee-body-unavailable reason"),
			MissingBodyDisclosure.IsValid() &&
				SlateWidgetText(MissingBodyDisclosure.ToSharedRef()).Contains(
					TEXT("RunSecondaryPath")) &&
				SlateWidgetText(MissingBodyDisclosure.ToSharedRef()).Contains(
					TEXT("callee graph"), ESearchCase::IgnoreCase) &&
				!SlateHasWidgetTag(
					MissingBodySurface,
					FName(TEXT(
						"BlueprintLens.Automation.CompositeLC5TypedPortalSurface"))));

		FBlueprintLensExplanationModel NonSelfCall = BuildLC5LiveCallShape(
			CorpusMainBlueprint,
			M7CorpusMainTypedIrPath(),
			CorpusMainTypedIrSha256,
			TEXT("PrintString"),
			NonSelfPrintStringSourceNodeId,
			TEXT("CURRENT LIVE NON SELF"));
		TSharedRef<SBlueprintLensPanel> NonSelfPanel = BuildLC5Panel(
			MoveTemp(NonSelfCall),
			TEXT("STALE NON SELF FIXTURE"));
		const TSharedRef<SWidget> NonSelfSurface =
			NonSelfPanel->BuildM6CausalContent();
		const TSharedPtr<SWidget> NonSelfDisclosure = SlateWidgetWithTag(
			NonSelfSurface,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC5RefusalDisclosure")));
		TestTrue(
			TEXT("a non-self-context call is refused with a visible family-boundary reason"),
			NonSelfDisclosure.IsValid() &&
				SlateWidgetText(NonSelfDisclosure.ToSharedRef()).Contains(
					TEXT("PrintString")) &&
				SlateWidgetText(NonSelfDisclosure.ToSharedRef()).Contains(
					TEXT("self-context"), ESearchCase::IgnoreCase) &&
				!SlateHasWidgetTag(
					NonSelfSurface,
					FName(TEXT(
						"BlueprintLens.Automation.CompositeLC5TypedPortalSurface"))));
	}

	{
		const FString DemoComposedPath =
			FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(),
				TEXT("../../artifacts/m10/ue/m10-composition-demo/"
					"composed-execution.explanation.json")));
		const FBlueprintLensLoadResult DemoComposedLoad =
			FBlueprintLensExplanationLoader::LoadFile(DemoComposedPath);
		TestTrue(
			TEXT("the retained composed-execution Explanation loads for the criterion-title geometry regression"),
			DemoComposedLoad.IsSuccess());
		if (DemoComposedLoad.IsSuccess())
		{
			const FBlueprintLensLC2LiveExplanationAdapterResult DemoComposedAdapted =
				FBlueprintLensLC2LiveExplanationAdapter::Build(*DemoComposedLoad.Model);
			TestTrue(
				TEXT("the retained composed-execution Explanation reaches the accepted live LC2 adapter"),
				DemoComposedAdapted.IsSuccess());
			if (DemoComposedAdapted.IsSuccess())
			{
				const FBlueprintLensLC2GuardOutlineProjection DemoComposedOutline =
					FBlueprintLensLC2GuardOutlineProjector::Build(
						DemoComposedAdapted.Explanation);
				const FBlueprintLensLC2GuardSurfaceProjection DemoComposedProjection =
					FBlueprintLensLC2GuardSurfaceProjector::Build(
						DemoComposedAdapted.Explanation,
						DemoComposedOutline);
				const FBlueprintLensLC2GuardLayoutSessionResult DemoComposedSession =
					FBlueprintLensLC2GuardLayoutSession::Build(
						DemoComposedProjection,
						DemoComposedAdapted.Explanation,
						700.0f);
				const FBlueprintLensLC2GuardSurfaceLayout DemoComposedSurface =
					FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
						DemoComposedProjection,
						DemoComposedSession,
						700.0f,
						FString());
				AddInfo(FString::Printf(
					TEXT("M10_COMPOSED_CRITERION_LAYOUT session=%s attempts=%s surface=%s labels=%d"),
					*DemoComposedSession.DiagnosticCode,
					*DemoComposedSession.AttemptSummary(),
					*DemoComposedSurface.DiagnosticCode,
					DemoComposedSurface.Labels.Num()));
				const FBlueprintLensLC2GuardSurfaceLabel* CriterionLabel =
					DemoComposedSurface.Labels.FindByPredicate(
						[](const FBlueprintLensLC2GuardSurfaceLabel& Label)
						{
							return Label.Key == TEXT("criterion");
						});
				const FBlueprintLensLC2GuardCanonicalUnit* CriterionUnit =
					DemoComposedProjection.FindCanonicalUnit(
						DemoComposedProjection.CriterionUnitId);
				TestNotNull(
					TEXT("the composed guard surface retains its criterion label geometry"),
					CriterionLabel);
				TestNotNull(
					TEXT("the composed guard surface resolves its criterion title"),
					CriterionUnit);
				if (CriterionLabel != nullptr && CriterionUnit != nullptr)
				{
					const TSharedRef<FSlateFontMeasure> FontMeasure =
						FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
					const FVector2D MeasuredCriterion = FontMeasure->Measure(
						CriterionUnit->ReaderLabel,
						FAppStyle::Get().GetFontStyle("NormalFontBold"));
					float WidestUnbreakableCriterionSegment = 0.0f;
					TArray<FString> CriterionSegments;
					CriterionUnit->ReaderLabel.ParseIntoArrayWS(CriterionSegments);
					for (const FString& Segment : CriterionSegments)
					{
						WidestUnbreakableCriterionSegment = FMath::Max(
							WidestUnbreakableCriterionSegment,
							FontMeasure->Measure(
								Segment,
								FAppStyle::Get().GetFontStyle("NormalFontBold")).X);
					}
					AddInfo(FString::Printf(
						TEXT("M10_COMPOSED_CRITERION_GEOMETRY measured=%.2f widest_unbreakable=%.2f reserved=%.2f max_x=%.2f canvas=%.2f"),
						MeasuredCriterion.X,
						WidestUnbreakableCriterionSegment,
						CriterionLabel->ExclusionBounds.GetSize().X,
						CriterionLabel->ExclusionBounds.Max.X,
						DemoComposedSurface.CanvasSize.X));
					TestTrue(
						TEXT("the composed CRITERION reserves its widest unbreakable title segment"),
						CriterionLabel->ExclusionBounds.GetSize().X + 0.5f >=
							WidestUnbreakableCriterionSegment);
					TestTrue(
						TEXT("the composed CRITERION remains inside the fixed canvas"),
						CriterionLabel->ExclusionBounds.Max.X <=
							DemoComposedSurface.CanvasSize.X);
				}
			}
		}
	}

	{
		const FString DemoLC7ExitPresentPath =
			FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(),
				TEXT("../../artifacts/m10/ue/m10-composition-demo/"
					"lc7-exit-present.explanation.json")));
		const FBlueprintLensLoadResult DemoLC7Load =
			FBlueprintLensExplanationLoader::LoadFile(DemoLC7ExitPresentPath);
		TestTrue(
			TEXT("the retained demo LC7 exit-present Explanation loads for the real interaction regression"),
			DemoLC7Load.IsSuccess());
		if (!DemoLC7Load.IsSuccess())
		{
			AddError(DemoLC7Load.Error);
		}
		else
		{
			const FBlueprintLensLC7LiveExplanationAdapterResult DemoLC7Adapted =
				FBlueprintLensLC7LiveExplanationAdapter::Build(*DemoLC7Load.Model);
			TestTrue(
				TEXT("the retained demo size-three SCC passes the structural live LC7 adapter"),
				DemoLC7Adapted.IsSuccess());
			if (DemoLC7Adapted.IsSuccess())
			{
				const FBlueprintLensLC7Projection DemoLC7Projection =
					FBlueprintLensLC7Projector::Build(*DemoLC7Adapted.Profile);
				TestTrue(
					TEXT("the retained demo size-three SCC produces an accountable LC7 projection"),
					DemoLC7Projection.IsRenderable());
				const FString FocusedSCCId = DemoLC7Projection.SCCs.IsEmpty()
					? FString()
					: DemoLC7Projection.SCCs[0].GroupId;
				const FBlueprintLensLC7LayoutSessionResult DemoLC7Session =
					FBlueprintLensLC7LayoutSession::Build(
						DemoLC7Projection, 700.0f, FocusedSCCId);
				const FBlueprintLensLC7TextMetrics DemoLC7Metrics =
					FBlueprintLensLC7TextMetrics::MeasuredForProjection(
						DemoLC7Projection);
				AddInfo(FString::Printf(
					TEXT("M10_LC7_DEMO_LAYOUT_DIAGNOSTIC session=%s layout=%s "
						"attempts=%s units=%d relations=%d scc_members=%d"),
					*DemoLC7Session.DiagnosticCode,
					*DemoLC7Session.Layout.DiagnosticCode,
					*DemoLC7Session.AttemptSummary(),
					DemoLC7Projection.AllUnitIds.Num(),
					DemoLC7Projection.AllRelationIds.Num(),
					DemoLC7Projection.SCCs.IsEmpty()
						? 0
						: DemoLC7Projection.SCCs[0].OrderedSpineUnitIds.Num()));
				AddInfo(FString::Printf(
					TEXT("M10_LC7_DEMO_LAYOUT_PREDICATES nodes=%d routes=%d "
						"actions=%d request_valid=%s shared_ledger=%s "
						"hit_targets=%s labels=%s attachments=%s collinear=%s "
						"bend_budget=%s collisions=%s oracle=%s"),
					DemoLC7Session.Layout.Nodes.Num(),
					DemoLC7Session.Layout.Routes.Num(),
					DemoLC7Session.Layout.Actions.Num(),
					DemoLC7Session.Layout.LayoutRequest.IsValid()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasValidSharedLedger()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasNonOverlappingHitTargets()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasInBoundsMeasuredLabels()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasDistinctRelationAttachments()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasZeroCollinearRouteOverlap()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasValidBendBudget()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.HasNoTextOrRouteCollisions()
						? TEXT("true") : TEXT("false"),
					DemoLC7Session.Layout.MatchesVisualOracle(1.0f)
						? TEXT("true") : TEXT("false")));
				TestTrue(
					TEXT("the admitted demo LC7 span owns a renderable accepted A3 layout session before exposing an expansion action"),
					DemoLC7Session.IsRenderable(DemoLC7Projection));
				int32 CheckedDemoLC7Titles = 0;
				for (const FBlueprintLensLC7NodeLayout& Node :
					 DemoLC7Session.Layout.Nodes)
				{
					const FVector2D* Measured =
						DemoLC7Metrics.UnitLabelSizes.Find(Node.UnitId);
					TestNotNull(
						TEXT("every live A3 node resolves retained title metrics"),
						Measured);
					if (Measured != nullptr)
					{
						++CheckedDemoLC7Titles;
						AddInfo(FString::Printf(
							TEXT("M10_LC7_DEMO_TITLE_GEOMETRY unit=%s measured=%.2f label=%.2f node=%.2f"),
							*Node.UnitId,
							Measured->X,
							Node.LabelBounds.GetSize().X,
							Node.Bounds.GetSize().X));
						TestTrue(
							TEXT("the live A3 label retains the full measured title width"),
							Node.LabelBounds.GetSize().X + 0.5f >= Measured->X);
						TestTrue(
							TEXT("the live A3 node reserves measured title width plus horizontal padding"),
							Node.Bounds.GetSize().X + 0.5f >= Measured->X + 18.0f);
					}
				}
				TestEqual(
					TEXT("the live A3 title regression checks every rendered node"),
					CheckedDemoLC7Titles,
					DemoLC7Projection.AllUnitIds.Num());
				FBlueprintLensLC7TextMetrics TooWideDemoLC7Metrics = DemoLC7Metrics;
				TooWideDemoLC7Metrics.UnitLabelSizes.FindChecked(
					DemoLC7Projection.SCCs[0].OrderedSpineUnitIds[0]).X = 415.0f;
				const FBlueprintLensLC7Layout TooWideDemoLC7Layout =
					FBlueprintLensLC7LayoutBuilder::Build(
						DemoLC7Projection,
						700.0f,
						FocusedSCCId,
						TooWideDemoLC7Metrics);
				TestEqual(
					TEXT("a live A3 title wider than the fixed overview is recorded rather than clipped"),
					TooWideDemoLC7Layout.DiagnosticCode,
					FString(TEXT("LC7_LAYOUT_LIVE_LABEL_WIDTH_UNAVAILABLE")));
				TestFalse(
					TEXT("a live A3 title that cannot fit the fixed overview is not rendered partially"),
					TooWideDemoLC7Layout.CoversProjection(DemoLC7Projection));

				TSharedRef<SBlueprintLensPanel> DemoLC7Panel =
					SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
				DemoLC7Panel->Model = MakeShared<FBlueprintLensExplanationModel>(
					BuildLC7LiveEngineSampleShape(
						TEXT("/Game/LensCorpus/BP_LC7_StaticSCC."
							"BP_LC7_StaticSCC"),
						TEXT("STALE DEMO INTERACTION FIXTURE")));
				DemoLC7Panel->M6Presentation.SetPythonReady(true);
				DemoLC7Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
				DemoLC7Panel->M6Presentation.SetGraphId(
					DemoLC7Load.Model->Source.GraphId);
				DemoLC7Panel->M6Presentation.ApplySession(Ready, &Views);
				FM6LoadedSessionPacket DemoLC7Packet;
				DemoLC7Packet.Request.QueryKind = TEXT("execution");
				DemoLC7Packet.Explanation = *DemoLC7Load.Model;
				DemoLC7Packet.SemanticSha256 = FString::ChrN(64, TEXT('8'));
				DemoLC7Panel->M6ReadyPacket =
					MakeShared<FM6LoadedSessionPacket>(MoveTemp(DemoLC7Packet));

				const TSharedRef<SWidget> DemoCollapsed =
					DemoLC7Panel->BuildM6CausalContent();
				const TArray<TSharedRef<SWidget>> DemoActions = SlateWidgetsWithTag(
					DemoCollapsed,
					FName(TEXT(
						"BlueprintLens.Automation.CompositeLC7SpanDisclosureAction")));
				TestEqual(
					TEXT("the admitted demo LC7 span exposes exactly one real expansion action"),
					DemoActions.Num(),
					1);
				if (DemoActions.Num() == 1 &&
					DemoActions[0]->GetTypeAsString() == TEXT("SButton"))
				{
					TSharedRef<SButton> DemoAction =
						StaticCastSharedRef<SButton>(DemoActions[0]);
					TSet<FKey> PressedButtons;
					PressedButtons.Add(EKeys::LeftMouseButton);
					const FPointerEvent PressEvent(
						0,
						FVector2D::ZeroVector,
						FVector2D::ZeroVector,
						PressedButtons,
						EKeys::LeftMouseButton,
						0.0f,
						FModifierKeysState());
					DemoAction->OnMouseButtonDown(FGeometry(), PressEvent);
				}
				const TSharedRef<SWidget> DemoExpanded =
					DemoLC7Panel->BuildM6CausalContent();
				TestTrue(
					TEXT("pressing the retained demo LC7 action renders the accepted A3 backbone rather than only refreshing the rail"),
					SlateHasWidgetTag(
						DemoExpanded,
						FName(TEXT(
							"BlueprintLens.Automation.CompositeLC7AdaptiveBackboneSurface"))));
			}
		}

		const FString LiveLC7Blueprint =
			TEXT("/Game/M7Corpus/BP_M7_EngineSample.BP_M7_EngineSample");
		const FString StaleLC7Blueprint =
			TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC");
		FBlueprintLensExplanationModel LiveLC7 =
			BuildLC7LiveEngineSampleShape(
				LiveLC7Blueprint,
				TEXT("CURRENT PACKET ENGINE SAMPLE GATE"));
		FBlueprintLensExplanationModel StaleLC7 =
			BuildLC7LiveEngineSampleShape(
				StaleLC7Blueprint,
				TEXT("STALE CONSTRUCTION FIXTURE GATE"));
		TestEqual(
			TEXT("the live LC7 regression carries the measured EngineSample unit ledger"),
			LiveLC7.Units.Num(),
			10);
		TestEqual(
			TEXT("the live LC7 regression carries the measured EngineSample relation ledger"),
			LiveLC7.Relations.Num(),
			10);
		const FBlueprintLensLC7LiveExplanationAdapterResult DirectLC7 =
			FBlueprintLensLC7LiveExplanationAdapter::Build(LiveLC7);
		TestTrue(
			TEXT("the size-six EngineSample shape passes the structural live LC7 adapter"),
			DirectLC7.IsSuccess());
		if (DirectLC7.IsSuccess())
		{
			TestEqual(
				TEXT("the live LC7 adapter preserves all ten input units"),
				DirectLC7.Profile->ExplanationModel->Units.Num(),
				LiveLC7.Units.Num());
			TestEqual(
				TEXT("the live LC7 adapter preserves all ten input relations"),
				DirectLC7.Profile->ExplanationModel->Relations.Num(),
				LiveLC7.Relations.Num());
		}
		FBlueprintLensExplanationModel OversizedLC7 = LiveLC7;
		FBlueprintLensUnit ExtraUnit = OversizedLC7.Units[0];
		ExtraUnit.Id = TEXT("unit.boundary.over-bound");
		ExtraUnit.SourceReferences[0].SourceNodeId =
			OversizedLC7.Source.GraphId + TEXT("::node::over-bound");
		ExtraUnit.SourceReferences[0].NativeNodeGuid = TEXT("over-bound");
		OversizedLC7.Units.Add(MoveTemp(ExtraUnit));
		OversizedLC7.Counts.Units = OversizedLC7.Units.Num();
		OversizedLC7.Counts.SourceNodes = OversizedLC7.Units.Num();
		const FBlueprintLensLC7LiveExplanationAdapterResult OversizedResult =
			FBlueprintLensLC7LiveExplanationAdapter::Build(OversizedLC7);
		TestTrue(
			TEXT("the numeric LC7 admission gate runs as a greater-than upper bound"),
			!OversizedResult.IsSuccess() &&
				OversizedResult.DiagnosticCode.Contains(TEXT("BOUND_EXCEEDED")));

		TSharedRef<SBlueprintLensPanel> LC7Panel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		LC7Panel->Model =
			MakeShared<FBlueprintLensExplanationModel>(MoveTemp(StaleLC7));
		LC7Panel->M6Presentation.SetPythonReady(true);
		LC7Panel->M6Presentation.SetQueryKind(EM6QueryKind::Execution);
		LC7Panel->M6Presentation.SetGraphId(LiveLC7.Source.GraphId);
		LC7Panel->M6Presentation.ApplySession(Ready, &Views);
		FM6LoadedSessionPacket LC7Packet;
		LC7Packet.Request.QueryKind = TEXT("execution");
		LC7Packet.Explanation = LiveLC7;
		LC7Packet.SemanticSha256 = FString::ChrN(64, TEXT('7'));
		LC7Panel->M6ReadyPacket =
			MakeShared<FM6LoadedSessionPacket>(MoveTemp(LC7Packet));

		const TSharedRef<SWidget> CollapsedLC7 =
			LC7Panel->BuildM6CausalContent();
		TestTrue(
			TEXT("the EngineSample live packet reaches the ordinary composite execution rail before LC7 attachment"),
			LC7Panel->LC1RailCanvas.IsValid() &&
				LC7Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
					.SourceBlueprintAssetPath == LiveLC7Blueprint);
		const TArray<TSharedRef<SWidget>> LC7SpanActions = SlateWidgetsWithTag(
			CollapsedLC7,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC7SpanDisclosureAction")));
		TestEqual(
			TEXT("the size-six SCC is admitted on the non-fixture Blueprint and exposes one default-collapsed span action"),
			LC7SpanActions.Num(),
			1);
		const FBlueprintLensCompositeSpanSlot* LC7Span = nullptr;
		if (LC7Panel->LC1RailCanvas.IsValid())
		{
			LC7Span = LC7Panel->LC1RailCanvas->GetCompositeSlotsForTesting()
				.Spans.FindByPredicate(
					[](const FBlueprintLensCompositeSpanSlot& Span)
					{
						return Span.Attachments.ContainsByPredicate(
							[](const FBlueprintLensCompositeAttachment& Attachment)
							{
								return Attachment.GrammarId == TEXT("LC7");
							});
					});
		}
		TestTrue(
			TEXT("the admitted LC7 span itself begins collapsed rather than passing on unrelated attachments"),
			LC7Span != nullptr &&
				LC7Span->Disclosure ==
					EBlueprintLensCompositeDisclosure::Collapsed &&
				LC7Span->Attachments.ContainsByPredicate(
					[](const FBlueprintLensCompositeAttachment& Attachment)
					{
						return Attachment.GrammarId == TEXT("LC7") &&
							Attachment.Disclosure ==
								EBlueprintLensCompositeDisclosure::Collapsed;
					}));
		TestFalse(
			TEXT("the accepted A3 backbone is not expanded before its span action"),
			SlateHasWidgetTag(
				CollapsedLC7,
				FName(TEXT(
					"BlueprintLens.Automation.CompositeLC7AdaptiveBackboneSurface"))));
		if (LC7SpanActions.Num() == 1 &&
			LC7SpanActions[0]->GetTypeAsString() == TEXT("SButton"))
		{
			TSharedRef<SButton> LC7SpanAction =
				StaticCastSharedRef<SButton>(LC7SpanActions[0]);
			TSet<FKey> PressedButtons;
			PressedButtons.Add(EKeys::LeftMouseButton);
			const FPointerEvent PressEvent(
				0,
				FVector2D::ZeroVector,
				FVector2D::ZeroVector,
				PressedButtons,
				EKeys::LeftMouseButton,
				0.0f,
				FModifierKeysState());
			LC7SpanAction->OnMouseButtonDown(FGeometry(), PressEvent);
		}
		const TSharedRef<SWidget> ExpandedLC7 =
			LC7Panel->BuildM6CausalContent();
		const TSharedPtr<SWidget> LC7Surface = SlateWidgetWithTag(
			ExpandedLC7,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC7AdaptiveBackboneSurface")));
		TestTrue(
			TEXT("the admitted EngineSample SCC renders through the one accepted A3 backbone widget"),
			LC7Surface.IsValid() &&
				LC7Surface->GetTypeAsString() ==
					TEXT("SBlueprintLensLC7AdaptiveBackbone"));
		const FString LC7SurfaceText = LC7Surface.IsValid()
			? SlateWidgetText(LC7Surface.ToSharedRef())
			: FString();
		TestTrue(
			TEXT("the Explanation reaching the LC7 chain is the current EngineSample packet rather than the construction fixture"),
			LC7Surface.IsValid() &&
				LC7SurfaceText.Contains(
					TEXT("CURRENT PACKET ENGINE SAMPLE GATE")) &&
				!LC7SurfaceText.Contains(
					TEXT("STALE CONSTRUCTION FIXTURE GATE")));
		const TSharedPtr<SWidget> RelationFamilyDisclosure = SlateWidgetWithTag(
			ExpandedLC7,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC7RelationFamilyDisclosure")));
		const TSharedPtr<SWidget> ExitBoundaryDisclosure = SlateWidgetWithTag(
			ExpandedLC7,
			FName(TEXT(
				"BlueprintLens.Automation.CompositeLC7ExitBoundaryDisclosure")));
		TestTrue(
			TEXT("the expanded live backbone visibly declares the SCC relation family"),
			RelationFamilyDisclosure.IsValid() &&
				RelationFamilyDisclosure->GetVisibility() ==
					EVisibility::HitTestInvisible &&
				SlateWidgetText(RelationFamilyDisclosure.ToSharedRef()).Contains(
					TEXT("execution_predecessor")) &&
				SlateWidgetText(RelationFamilyDisclosure.ToSharedRef()).Contains(
					TEXT("controls_execution")));
		TestTrue(
			TEXT("the exit-zero SCC remains renderable and visibly declares its outside-slice exit boundary"),
			ExitBoundaryDisclosure.IsValid() &&
				ExitBoundaryDisclosure->GetVisibility() ==
					EVisibility::HitTestInvisible &&
				SlateWidgetText(ExitBoundaryDisclosure.ToSharedRef()).Contains(
					TEXT("outside"), ESearchCase::IgnoreCase) &&
				SlateWidgetText(ExitBoundaryDisclosure.ToSharedRef()).Contains(
					TEXT("static slice"), ESearchCase::IgnoreCase));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6TelemetryTest,
	"BlueprintLens.M6.Telemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6TelemetryTest::RunTest(const FString&)
{
	using namespace BlueprintLensM6ControllerTests;
	const FString Root = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6TelemetryTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString Path = FPaths::Combine(Root, TEXT("record.telemetry.v1.jsonl"));
	const FString SemanticHash = FString::ChrN(64, TEXT('d'));
	FM6TelemetryRecorder Recorder;
	FM6Error TelemetryError;
	TestTrue(TEXT("Telemetry begins"), Recorder.Begin(Path, TEXT("run-1"), SemanticHash, TelemetryError));
	AddRequiredStages(Recorder);
	TestTrue(TEXT("Baseline event accepted"), Recorder.RecordBaseline(EM6Baseline::C, TelemetryError));
	TestTrue(TEXT("Selection event accepted"), Recorder.RecordSelection(TEXT("/Game/Test:Graph::node::one"), TelemetryError));
	TestTrue(TEXT("Expand event accepted"), Recorder.RecordExpansion(TEXT("/Game/Test:Graph::node::two"), true, TelemetryError));
	TestTrue(TEXT("Collapse event accepted"), Recorder.RecordExpansion(TEXT("/Game/Test:Graph::node::two"), false, TelemetryError));
	TestFalse(TEXT("Private absolute path rejected"), Recorder.RecordSelection(TEXT("C:\\Users\\private"), TelemetryError));
	TestEqual(TEXT("Privacy stable code"), TelemetryError.Code, FString(TEXT("M6_TELEMETRY_SCHEMA_INVALID")));
	FString SealHash;
	TestTrue(TEXT("Telemetry seals"), Recorder.Seal(SealHash, TelemetryError));
	TestEqual(TEXT("Seal hash is lowercase SHA-256"), SealHash.Len(), 64);
	TestFalse(TEXT("Append after seal rejected"), Recorder.RecordBaseline(EM6Baseline::A, TelemetryError));

	const FM6TelemetryReplayResult Replay = FM6TelemetryRecorder::Replay(Path, SemanticHash);
	TestTrue(TEXT("Sealed telemetry replays"), Replay.HasValue());
	if (Replay.HasValue())
	{
		TestEqual(TEXT("Replay baseline"), Replay.GetValue().Baseline, EM6Baseline::C);
		TestEqual(TEXT("Replay selection"), Replay.GetValue().SelectedEntityId, FString(TEXT("/Game/Test:Graph::node::one")));
		TestEqual(TEXT("Replay expansions empty"), Replay.GetValue().ExpandedEntityIds.Num(), 0);
	}

	FString Text;
	TestTrue(TEXT("Telemetry file readable"), FFileHelper::LoadFileToString(Text, *Path));
	TestFalse(TEXT("Telemetry has no CR"), Text.Contains(TEXT("\r")));
	TestFalse(TEXT("Telemetry has no private path"), Text.Contains(TEXT("C:\\Users")));
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	TestTrue(TEXT("Telemetry owns lifecycle plus interaction plus seal events"), Lines.Num() > 20);

	const FString MissingStagePath = FPaths::Combine(Root, TEXT("missing-stage.jsonl"));
	FM6TelemetryRecorder MissingStage;
	TestTrue(TEXT("Second recorder begins"), MissingStage.Begin(MissingStagePath, TEXT("run-2"), SemanticHash, TelemetryError));
	TestTrue(TEXT("One stage records"), MissingStage.RecordStage(TEXT("request"), true, FM6TelemetryCounts(), FString(), TelemetryError));
	TestFalse(TEXT("Missing stages cannot seal"), MissingStage.Seal(SealHash, TelemetryError));
	TestEqual(TEXT("Missing stage stable code"), TelemetryError.Code, FString(TEXT("M6_TELEMETRY_SEQUENCE_INVALID")));

	const FString TamperedPath = FPaths::Combine(Root, TEXT("tampered.jsonl"));
	IFileManager::Get().Copy(*TamperedPath, *Path, true, true);
	FString Tampered;
	FFileHelper::LoadFileToString(Tampered, *TamperedPath);
	Tampered.ReplaceInline(TEXT("node::one"), TEXT("node::changed"));
	FFileHelper::SaveStringToFile(Tampered, *TamperedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const FM6TelemetryReplayResult TamperedReplay = FM6TelemetryRecorder::Replay(TamperedPath, SemanticHash);
	TestTrue(TEXT("Tampered record fails"), TamperedReplay.HasError());
	if (TamperedReplay.HasError())
		TestEqual(TEXT("Tamper stable code"), TamperedReplay.GetError().Code, FString(TEXT("M6_TELEMETRY_REPLAY_MISMATCH")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensM6MeasuredTelemetryTest,
	"BlueprintLens.M6.MeasuredTelemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensM6MeasuredTelemetryTest::RunTest(const FString&)
{
	const FString Path = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens/M6Tests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-measured.jsonl"));
	const FString SemanticHash = FString::ChrN(64, TEXT('a'));
	FM6TelemetryRecorder Recorder;
	FM6Error Error;
	TestTrue(
		TEXT("Measured telemetry recorder begins"),
		Recorder.Begin(Path, TEXT("measured-run"), SemanticHash, Error));
	for (const TCHAR* Stage : FM6TelemetryRecorder::RequiredStages())
	{
		FM6TelemetryStageMeasurement Measurement;
		Measurement.Stage = Stage;
		Measurement.StartTimestamp = TEXT("2026-08-20T12:00:00.000000Z");
		Measurement.ResultTimestamp = TEXT("2026-08-20T12:00:00.001000Z");
		Measurement.DurationMs = 1.0;
		Measurement.ErrorCode = FString(Stage) == TEXT("explanation")
			? TEXT("M6_PIPELINE_EXPLANATION_FAILED") : FString();
		TestTrue(
			TEXT("Measured stage is recorded with its outcome"),
			Recorder.RecordMeasuredStage(Measurement, Error));
	}
	FString PriorHash;
	TestTrue(TEXT("Measured telemetry seals"), Recorder.Seal(PriorHash, Error));
	FString Text;
	TestTrue(TEXT("Measured telemetry is readable"), FFileHelper::LoadFileToString(Text, *Path));
	TestTrue(TEXT("Measured telemetry includes timestamps"), Text.Contains(TEXT("timestamp")));
	TestTrue(TEXT("Measured telemetry includes duration"), Text.Contains(TEXT("duration_ms")));
	TestTrue(TEXT("Measured telemetry includes stage error"), Text.Contains(TEXT("M6_PIPELINE_EXPLANATION_FAILED")));
	const FM6TelemetryReplayResult Replay = FM6TelemetryRecorder::Replay(Path, SemanticHash);
	TestTrue(TEXT("Measured telemetry replays"), Replay.HasValue());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
