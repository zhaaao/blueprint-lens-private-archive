#include "BlueprintLensExplanationModel.h"
#include "BlueprintLensEditorModule.h"
#include "BlueprintLensFrameFlowLayout.h"
#include "BlueprintLensLC1Disclosure.h"
#include "BlueprintLensLC1PseudocodeProjection.h"
#include "BlueprintLensLC1RailLayout.h"
#include "BlueprintLensLC1RailLayoutSession.h"
#include "BlueprintLensLC1RailProjection.h"
#include "BlueprintLensLC1RailSurfaceLayout.h"
#include "BlueprintLensLC1RegionProjection.h"
#include "BlueprintLensLC1TypedIrFacts.h"
#include "BlueprintLensLC2GuardOutlineProjection.h"
#include "BlueprintLensLC2GuardLayoutSession.h"
#include "BlueprintLensLC2GuardSurfaceLayout.h"
#include "BlueprintLensLC2GuardSurfaceProjection.h"
#include "BlueprintLensLC3DerivationSpineLayout.h"
#include "BlueprintLensLC3ValueConeLayout.h"
#include "BlueprintLensLC3ValueConeLayoutSession.h"
#include "BlueprintLensLC3ValueConeProjection.h"
#include "BlueprintLensLC4SequenceLayout.h"
#include "BlueprintLensLC4SequenceLayoutSession.h"
#include "BlueprintLensLC4SequenceProfile.h"
#include "BlueprintLensLC4SequenceProjection.h"
#include "BlueprintLensExternalLayoutProcess.h"
#include "BlueprintLensElkLayoutBackend.h"
#include "BlueprintLensGraphvizLayoutBackend.h"
#include "BlueprintLensLayoutContract.h"
#include "BlueprintLensSourceNavigator.h"
#include "BlueprintLensWeaveProjection.h"
#include "SBlueprintLensLC1RailCanvas.h"
#include "SBlueprintLensLC4SequenceRail.h"
#include "SBlueprintLensPanel.h"

#include "BlueprintLensLC4AsyncLayout.h"
#include "BlueprintLensLC4AsyncLayoutSession.h"
#include "BlueprintLensLC4AsyncProfile.h"
#include "BlueprintLensLC4AsyncProjection.h"
#include "SBlueprintLensLC4AsyncPartialOrder.h"
#include "BlueprintLensLC5Profile.h"
#include "BlueprintLensLC5Projection.h"
#include "BlueprintLensLC5Layout.h"
#include "BlueprintLensLC5LayoutSession.h"
#include "SBlueprintLensLC5TypedPortal.h"
#include "BlueprintLensLC6Profile.h"
#include "BlueprintLensLC6Projection.h"
#include "BlueprintLensLC6Layout.h"
#include "BlueprintLensLC6LayoutSession.h"
#include "SBlueprintLensLC6FourTrack.h"
#include "BlueprintLensLC7Profile.h"
#include "BlueprintLensLC7Projection.h"
#include "BlueprintLensLC7Layout.h"
#include "BlueprintLensLC7LayoutSession.h"
#include "SBlueprintLensLC7AdaptiveBackbone.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Fonts/FontMeasure.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "IPlatformCrypto.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FString CanonicalFixturePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	return Plugin.IsValid()
		? FPaths::Combine(
			  Plugin->GetBaseDir(),
			  TEXT("Resources/Explanation/"
				   "BP_SlicingProbe.set-health.explanation.v1.json"))
		: FString();
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

FString LC4ProfilePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	return Plugin.IsValid()
		? FPaths::Combine(
			  Plugin->GetBaseDir(),
			  TEXT("Resources/Explanation/"
				   "BP_LC4_SequenceDisclosure.sequence-profile.v1.json"))
		: FString();
}

FString LC4AsyncProfilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc4-async-truth/"
			 "BP_LC4_AsyncBarrier.async-profile.v1.json")));
}

FString LC5ProfilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc5-intra-bp-pure-truth/"
			 "BP_SlicingProbe.contextual-slice.v1.json")));
}

FString LC6TruthPath(const TCHAR* RelativePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc6-boundary-truth"),
		RelativePath));
}

FString LC6CoreProfilePath()
{
	return LC6TruthPath(
		TEXT("BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json"));
}

FString LC6QueryProfilePath()
{
	return LC6TruthPath(
		TEXT("BP_LC6_BoundaryMatrix.upstream-budget.v1.json"));
}

FString LC6ReadinessPath()
{
	return LC6TruthPath(TEXT("readiness.json"));
}

FString LC6RawPath()
{
	return LC6TruthPath(
		TEXT("run1/BP_LC6_BoundaryMatrix.raw-0.2.json"));
}

FString LC7TruthPath(const TCHAR* RelativePath)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../artifacts/r1/lc7-static-scc-truth"),
		RelativePath));
}

FString LC7ExplanationPath()
{
	return LC7TruthPath(TEXT("BP_LC7_StaticSCC.explanation.v1.json"));
}

FString LC7SCCProfilePath()
{
	return LC7TruthPath(TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"));
}

FString LC7ReviewedPath()
{
	return LC7TruthPath(TEXT("reviewed-ground-truth.v1.json"));
}

FString LC7ReadinessPath()
{
	return LC7TruthPath(TEXT("readiness.json"));
}

FString WriteLC6Mutation(
	const FString& SourcePath,
	const FString& Name,
	TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *SourcePath) ||
		!FJsonSerializer::Deserialize(
			TJsonReaderFactory<>::Create(Text), Root) ||
		!Root.IsValid())
	{
		return FString();
	}
	Mutate(Root.ToSharedRef());
	FString Mutated;
	if (!FJsonSerializer::Serialize(
		Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Mutated)))
	{
		return FString();
	}
	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(), TEXT("BlueprintLensTests/LC6"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		return FString();
	}
	const FString OutputPath = FPaths::Combine(Directory, Name + TEXT(".json"));
	return FFileHelper::SaveStringToFile(
		Mutated, *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		? OutputPath : FString();
}

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
			Texts.Add(
				StaticCastSharedRef<STextBlock>(Widget)
					->GetText()
					.ToString());
		}
		else if (Widget->GetTypeAsString() == TEXT("SEditableTextBox"))
		{
			Texts.Add(
				StaticCastSharedRef<SEditableTextBox>(Widget)
					->GetText()
					.ToString());
		}
		else if (
			Widget->GetTypeAsString()
			== TEXT("SMultiLineEditableTextBox"))
		{
			Texts.Add(
				StaticCastSharedRef<SMultiLineEditableTextBox>(Widget)
					->GetText()
					.ToString());
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

bool ContainsCjkCodePoint(const FString& Text)
{
	for (int32 Index = 0; Index < Text.Len();)
	{
		uint32 CodePoint = static_cast<uint32>(Text[Index++]);
		if (CodePoint >= 0xD800u && CodePoint <= 0xDBFFu &&
			Index < Text.Len())
		{
			const uint32 Trail = static_cast<uint32>(Text[Index]);
			if (Trail >= 0xDC00u && Trail <= 0xDFFFu)
			{
				++Index;
				CodePoint = 0x10000u +
					((CodePoint - 0xD800u) << 10u) +
					(Trail - 0xDC00u);
			}
		}
		if (CodePoint >= 0x4E00u && CodePoint <= 0x9FFFu)
		{
			return true;
		}
	}
	return false;
}

FVector2D MeasureLC1SurfaceLabelText(
	const FBlueprintLensLC1RailSurfaceLabel& Label)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC1 rail surface tests require Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const bool bCriterion = Label.Key == TEXT("criterion");
	const FSlateFontInfo Font = FAppStyle::Get().GetFontStyle(
		bCriterion ? "NormalFontBold" : "SmallFont");
	return FontMeasure->Measure(Label.Text, Font);
}

FVector2D MeasureLC4AsyncLabelText(
	const FBlueprintLensLC4AsyncLabel& Label)
{
	checkf(
		FSlateApplication::IsInitialized(),
		TEXT("LC4 async surface tests require Slate font measurement"));
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Font = BlueprintLensLC4AsyncFont(
		Label.FontSize,
		Label.Weight);
	return FontMeasure->Measure(Label.Text, Font);
}

TArray<TSharedRef<SButton>> SlateButtonsWithLabel(
	const TSharedRef<SWidget>& Root,
	const FString& Label)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	TArray<TSharedRef<SButton>> Buttons;
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == TEXT("SButton")
			&& SlateWidgetText(Widget).Contains(Label))
		{
			Buttons.Add(StaticCastSharedRef<SButton>(Widget));
		}
	}
	return Buttons;
}

bool InvokeSlateButton(
	const TSharedRef<SWidget>& Root,
	const FString& Label)
{
	const TArray<TSharedRef<SButton>> Buttons =
		SlateButtonsWithLabel(Root, Label);
	if (Buttons.IsEmpty())
	{
		return false;
	}
	const FKeyEvent AcceptEvent(
		FKey(TEXT("Enter")),
		FModifierKeysState(),
		0,
		false,
		0,
		0);
	Buttons[0]->OnKeyDown(FGeometry(), AcceptEvent);
	return Buttons[0]->OnKeyUp(FGeometry(), AcceptEvent).IsEventHandled();
}

int32 CountSlateTextWithPrefix(
	const TSharedRef<SWidget>& Root,
	const FString& Prefix)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	int32 Count = 0;
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock")
			&& StaticCastSharedRef<STextBlock>(Widget)
				->GetText()
				.ToString()
				.StartsWith(Prefix))
		{
			++Count;
		}
	}
	return Count;
}

int32 CountSlateTextEqual(
	const TSharedRef<SWidget>& Root,
	const FString& Expected)
{
	TArray<TSharedRef<SWidget>> Widgets;
	CollectSlateWidgets(Root, Widgets);
	int32 Count = 0;
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock") &&
			StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Expected)
		{
			++Count;
		}
	}
	return Count;
}

TSharedPtr<SWidget> FindDeepestSlateContainer(
	const TSharedRef<SWidget>& Root,
	const FString& Type,
	const TArray<FString>& RequiredText)
{
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		const TSharedPtr<SWidget> Match = FindDeepestSlateContainer(
			Children->GetChildAt(Index),
			Type,
			RequiredText);
		if (Match.IsValid())
		{
			return Match;
		}
	}
	if (Root->GetTypeAsString() != Type)
	{
		return nullptr;
	}
	const FString Text = SlateWidgetText(Root);
	for (const FString& Required : RequiredText)
	{
		if (!Text.Contains(Required))
		{
			return nullptr;
		}
	}
	return Root;
}

FString Sha256ForJsonText(const FString& JsonText)
{
	FTCHARToUTF8 Utf8Json(*JsonText);
	TArray<uint8> JsonBytes;
	JsonBytes.Append(
		reinterpret_cast<const uint8*>(Utf8Json.Get()),
		Utf8Json.Length());

	TUniquePtr<FEncryptionContext> CryptoContext =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!CryptoContext.IsValid()
		|| !CryptoContext->CalcSHA256(JsonBytes, Digest)
		|| Digest.Num() != 32)
	{
		return FString();
	}
	return BytesToHex(Digest.GetData(), Digest.Num());
}

struct FLC7MutationPacket
{
	FString ExplanationPath;
	FString SCCProfilePath;
	FString ReviewedPath;
	FString ReadinessPath;

	bool IsValid() const
	{
		return !ExplanationPath.IsEmpty() && !SCCProfilePath.IsEmpty() &&
			!ReviewedPath.IsEmpty() && !ReadinessPath.IsEmpty();
	}
};

bool LoadLC7Json(
	const FString& Path,
	TSharedPtr<FJsonObject>& OutRoot)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) &&
		FJsonSerializer::Deserialize(
			TJsonReaderFactory<>::Create(Text), OutRoot) &&
		OutRoot.IsValid();
}

bool SerializeLC7Json(
	const TSharedRef<FJsonObject>& Root,
	FString& OutText,
	FString& OutSha256)
{
	OutText.Reset();
	if (!FJsonSerializer::Serialize(
		Root, TJsonWriterFactory<>::Create(&OutText)))
	{
		return false;
	}
	OutSha256 = Sha256ForJsonText(OutText).ToLower();
	return !OutSha256.IsEmpty();
}

void SetLC7BoundHash(
	const TSharedRef<FJsonObject>& Hashes,
	const TCHAR* Basename,
	const TCHAR* ArtifactPath,
	const FString& Sha256)
{
	Hashes->SetStringField(Basename, Sha256);
	Hashes->SetStringField(ArtifactPath, Sha256);
}

FLC7MutationPacket WriteLC7MutationPacket(
	const FString& Name,
	TFunction<void(TSharedRef<FJsonObject>)> MutateExplanation = {},
	TFunction<void(TSharedRef<FJsonObject>)> MutateProfile = {},
	TFunction<void(TSharedRef<FJsonObject>)> MutateReviewed = {},
	TFunction<void(TSharedRef<FJsonObject>)> MutateReadiness = {})
{
	TSharedPtr<FJsonObject> Explanation;
	TSharedPtr<FJsonObject> Profile;
	TSharedPtr<FJsonObject> Reviewed;
	TSharedPtr<FJsonObject> Readiness;
	if (!LoadLC7Json(LC7ExplanationPath(), Explanation) ||
		!LoadLC7Json(LC7SCCProfilePath(), Profile) ||
		!LoadLC7Json(LC7ReviewedPath(), Reviewed) ||
		!LoadLC7Json(LC7ReadinessPath(), Readiness))
	{
		return {};
	}

	if (MutateExplanation)
	{
		MutateExplanation(Explanation.ToSharedRef());
	}
	if (MutateProfile)
	{
		MutateProfile(Profile.ToSharedRef());
	}
	if (MutateReviewed)
	{
		MutateReviewed(Reviewed.ToSharedRef());
	}

	FString ExplanationText;
	FString ExplanationSha256;
	FString ProfileText;
	FString ProfileSha256;
	if (!SerializeLC7Json(
			Explanation.ToSharedRef(), ExplanationText, ExplanationSha256) ||
		!SerializeLC7Json(Profile.ToSharedRef(), ProfileText, ProfileSha256))
	{
		return {};
	}

	const TSharedPtr<FJsonObject>* ProductHashes = nullptr;
	if (!Reviewed->TryGetObjectField(TEXT("product_hashes"), ProductHashes) ||
		ProductHashes == nullptr || !ProductHashes->IsValid())
	{
		return {};
	}
	(*ProductHashes)->SetStringField(
		TEXT("BP_LC7_StaticSCC.explanation.v1.json"), ExplanationSha256);
	(*ProductHashes)->SetStringField(
		TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"), ProfileSha256);

	FString ReviewedText;
	FString ReviewedSha256;
	if (!SerializeLC7Json(
			Reviewed.ToSharedRef(), ReviewedText, ReviewedSha256))
	{
		return {};
	}

	const TSharedPtr<FJsonObject>* ReadinessHashes = nullptr;
	if (!Readiness->TryGetObjectField(TEXT("hashes"), ReadinessHashes) ||
		ReadinessHashes == nullptr || !ReadinessHashes->IsValid())
	{
		return {};
	}
	SetLC7BoundHash(
		(*ReadinessHashes).ToSharedRef(),
		TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
		TEXT("artifacts/r1/lc7-static-scc-truth/"
			 "BP_LC7_StaticSCC.explanation.v1.json"),
		ExplanationSha256);
	SetLC7BoundHash(
		(*ReadinessHashes).ToSharedRef(),
		TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"),
		TEXT("artifacts/r1/lc7-static-scc-truth/"
			 "BP_LC7_StaticSCC.scc-profile.v1.json"),
		ProfileSha256);
	SetLC7BoundHash(
		(*ReadinessHashes).ToSharedRef(),
		TEXT("reviewed-ground-truth.v1.json"),
		TEXT("artifacts/r1/lc7-static-scc-truth/"
			 "reviewed-ground-truth.v1.json"),
		ReviewedSha256);
	if (MutateReadiness)
	{
		MutateReadiness(Readiness.ToSharedRef());
	}

	FString ReadinessText;
	FString ReadinessSha256;
	if (!SerializeLC7Json(
			Readiness.ToSharedRef(), ReadinessText, ReadinessSha256))
	{
		return {};
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(), TEXT("BlueprintLensTests/LC7"), Name);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		return {};
	}
	FLC7MutationPacket Packet;
	Packet.ExplanationPath = FPaths::Combine(
		Directory, TEXT("BP_LC7_StaticSCC.explanation.v1.json"));
	Packet.SCCProfilePath = FPaths::Combine(
		Directory, TEXT("BP_LC7_StaticSCC.scc-profile.v1.json"));
	Packet.ReviewedPath = FPaths::Combine(
		Directory, TEXT("reviewed-ground-truth.v1.json"));
	Packet.ReadinessPath = FPaths::Combine(Directory, TEXT("readiness.json"));
	if (!FFileHelper::SaveStringToFile(
			ExplanationText, *Packet.ExplanationPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!FFileHelper::SaveStringToFile(
			ProfileText, *Packet.SCCProfilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!FFileHelper::SaveStringToFile(
			ReviewedText, *Packet.ReviewedPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!FFileHelper::SaveStringToFile(
			ReadinessText, *Packet.ReadinessPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return {};
	}
	return Packet;
}

bool MutateLC1IrJson(
	const FString& JsonText,
	TFunctionRef<bool(TSharedRef<FJsonObject>)> Mutate,
	FString& OutMutatedJson,
	FString& OutMutatedSha256)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
		|| !Mutate(Root.ToSharedRef()))
	{
		return false;
	}

	OutMutatedJson.Reset();
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutMutatedJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		return false;
	}
	OutMutatedSha256 = Sha256ForJsonText(OutMutatedJson);
	return !OutMutatedSha256.IsEmpty();
}

TSharedPtr<FJsonObject> FindLC1StepVariableSetNode(
	const TSharedRef<FJsonObject>& Root)
{
	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	if (!Root->TryGetObjectField(TEXT("blueprint"), Blueprint)
		|| Blueprint == nullptr || !Blueprint->IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs)
		|| Graphs == nullptr)
	{
		return nullptr;
	}
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
		if (!Graph.IsValid())
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Graph->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (!Node.IsValid())
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!Node->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
			{
				const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
				FString PinRole;
				FString PinName;
				if (Pin.IsValid()
					&& Pin->TryGetStringField(TEXT("pin_role"), PinRole)
					&& PinRole == TEXT("variable_set_value")
					&& Pin->TryGetStringField(TEXT("name"), PinName)
					&& PinName.StartsWith(TEXT("LC1Step"))
					&& PinName.EndsWith(TEXT("Complete")))
				{
					return Node;
				}
			}
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FindVariableSetValuePin(
	const TSharedPtr<FJsonObject>& Node)
{
	if (!Node.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
	if (!Node->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
	{
		return nullptr;
	}
	for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
	{
		const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
		FString PinRole;
		if (Pin.IsValid()
			&& Pin->TryGetStringField(TEXT("pin_role"), PinRole)
			&& PinRole == TEXT("variable_set_value"))
		{
			return Pin;
		}
	}
	return nullptr;
}

struct FMutatedFixtureLoad
{
	bool bSetupSucceeded = false;
	FBlueprintLensLoadResult LoadResult;
	FString SetupError;
	FString OutputPath;
};

FMutatedFixtureLoad LoadMutatedFixtureFrom(
	const FString& FixturePath,
	const FString& Name,
	TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate,
	const bool bRetainScenario = false)
{
	FMutatedFixtureLoad Result;
	if (FixturePath.IsEmpty())
	{
		Result.SetupError =
			TEXT("BlueprintLensExporter plugin could not be located");
		return Result;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FixturePath))
	{
		Result.SetupError = FString::Printf(
			TEXT("Could not read canonical fixture '%s'"), *FixturePath);
		return Result;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.SetupError = TEXT("Could not parse canonical fixture");
		return Result;
	}

	FString OriginalJson;
	const TSharedRef<TJsonWriter<>> OriginalWriter =
		TJsonWriterFactory<>::Create(&OriginalJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), OriginalWriter))
	{
		Result.SetupError =
			TEXT("Could not serialize canonical fixture before mutation");
		return Result;
	}

	Mutate(Root.ToSharedRef());
	FString MutatedJson;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&MutatedJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		Result.SetupError = TEXT("Could not serialize mutated fixture");
		return Result;
	}
	if (MutatedJson == OriginalJson)
	{
		Result.SetupError =
			TEXT("Malformed-fixture mutation did not change the payload");
		return Result;
	}

	const FString OutputDirectory = bRetainScenario
		? FPaths::Combine(
			  FPaths::ProjectSavedDir(),
			  TEXT("BlueprintLens/Scenarios"))
		: FPaths::Combine(
			  FPaths::ProjectIntermediateDir(),
			  TEXT("BlueprintLensTests"));
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		Result.SetupError = FString::Printf(
			TEXT("Could not create mutated fixture directory '%s'"),
			*OutputDirectory);
		return Result;
	}

	const FString OutputPath =
		FPaths::Combine(OutputDirectory, Name + TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(
			MutatedJson,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Result.SetupError = FString::Printf(
			TEXT("Could not save mutated fixture '%s'"), *OutputPath);
		return Result;
	}

	Result.bSetupSucceeded = true;
	Result.OutputPath = OutputPath;
	Result.LoadResult = FBlueprintLensExplanationLoader::LoadFile(OutputPath);
	return Result;
}

FMutatedFixtureLoad LoadMutatedFixture(
	const FString& Name,
	TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate,
	const bool bRetainScenario = false)
{
	return LoadMutatedFixtureFrom(
		CanonicalFixturePath(), Name, Mutate, bRetainScenario);
}

FMutatedFixtureLoad LoadMutatedLC2Fixture(
	const FString& Name,
	TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate)
{
	return LoadMutatedFixtureFrom(LC2FixturePath(), Name, Mutate);
}

FMutatedFixtureLoad LoadGeneralLaneFixture()
{
	return LoadMutatedFixture(
		TEXT("m6-general-lane-arrangement"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Lanes = nullptr;
			if (!Root->TryGetArrayField(TEXT("units"), Units) || Units == nullptr ||
				!Root->TryGetArrayField(TEXT("lanes"), Lanes) || Lanes == nullptr ||
				Lanes->Num() != 6)
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> BoundaryUnitIds;
			for (const TSharedPtr<FJsonValue>& UnitValue : *Units)
			{
				const TSharedPtr<FJsonObject> Unit = UnitValue->AsObject();
				FString Role;
				FString Id;
				if (Unit.IsValid() && Unit->TryGetStringField(TEXT("role"), Role) &&
					Role == TEXT("control") && Unit->TryGetStringField(TEXT("id"), Id))
				{
					Unit->SetStringField(TEXT("role"), TEXT("boundary"));
					BoundaryUnitIds.Add(MakeShared<FJsonValueString>(Id));
				}
			}

			const TSharedPtr<FJsonObject> ControlLane = (*Lanes)[1]->AsObject();
			const TSharedPtr<FJsonObject> BoundaryLane = (*Lanes)[5]->AsObject();
			if (ControlLane.IsValid() && BoundaryLane.IsValid())
			{
				ControlLane->SetStringField(TEXT("state"), TEXT("empty"));
				ControlLane->SetArrayField(TEXT("unit_ids"), {});
				ControlLane->SetStringField(
					TEXT("empty_message"),
					TEXT("No control facts in this explanation"));
				BoundaryLane->SetStringField(TEXT("state"), TEXT("populated"));
				BoundaryLane->SetArrayField(TEXT("unit_ids"), MoveTemp(BoundaryUnitIds));
				BoundaryLane->SetStringField(TEXT("empty_message"), TEXT(""));
			}
		});
}

bool AddExactEndpointLedgers(TSharedRef<FJsonObject> Explanation)
{
	const TSharedPtr<FJsonObject>* Source = nullptr;
	if (!Explanation->TryGetObjectField(TEXT("source"), Source)
		|| Source == nullptr || !Source->IsValid())
	{
		return false;
	}
	FString IrPath;
	FString GraphId;
	if (!(*Source)->TryGetStringField(TEXT("ir_path"), IrPath)
		|| !(*Source)->TryGetStringField(TEXT("graph_id"), GraphId))
	{
		return false;
	}

	FString IrJson;
	TSharedPtr<FJsonObject> IrRoot;
	if (!FFileHelper::LoadFileToString(IrJson, *IrPath)
		|| !FJsonSerializer::Deserialize(
			TJsonReaderFactory<>::Create(IrJson), IrRoot)
		|| !IrRoot.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!IrRoot->TryGetObjectField(TEXT("blueprint"), Blueprint)
		|| Blueprint == nullptr || !Blueprint->IsValid()
		|| !(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs)
		|| Graphs == nullptr)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Graph;
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Candidate = GraphValue->AsObject();
		FString CandidateId;
		if (Candidate.IsValid()
			&& Candidate->TryGetStringField(TEXT("id"), CandidateId)
			&& CandidateId == GraphId)
		{
			Graph = Candidate;
			break;
		}
	}
	if (!Graph.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (!Graph->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr
		|| !Graph->TryGetArrayField(TEXT("edges"), Edges) || Edges == nullptr)
	{
		return false;
	}
	TMap<FString, TSharedPtr<FJsonObject>> PinsById;
	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!Node.IsValid()
			|| !Node->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
		{
			const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
			FString PinId;
			if (!Pin.IsValid()
				|| !Pin->TryGetStringField(TEXT("id"), PinId))
			{
				return false;
			}
			PinsById.Add(PinId, Pin);
		}
	}
	TMap<FString, TSharedPtr<FJsonObject>> EdgesById;
	for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
	{
		const TSharedPtr<FJsonObject> Edge = EdgeValue->AsObject();
		FString EdgeId;
		if (!Edge.IsValid()
			|| !Edge->TryGetStringField(TEXT("id"), EdgeId))
		{
			return false;
		}
		EdgesById.Add(EdgeId, Edge);
	}

	const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
	if (!Explanation->TryGetArrayField(TEXT("relations"), Relations)
		|| Relations == nullptr)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& RelationValue : *Relations)
	{
		const TSharedPtr<FJsonObject> Relation = RelationValue->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* SourceEdgeIds = nullptr;
		if (!Relation.IsValid()
			|| !Relation->TryGetArrayField(
				TEXT("source_edge_ids"), SourceEdgeIds)
			|| SourceEdgeIds == nullptr)
		{
			return false;
		}
		TArray<TSharedPtr<FJsonValue>> EndpointValues;
		for (const TSharedPtr<FJsonValue>& SourceEdgeIdValue : *SourceEdgeIds)
		{
			FString EdgeId;
			if (!SourceEdgeIdValue.IsValid()
				|| !SourceEdgeIdValue->TryGetString(EdgeId))
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* Edge = EdgesById.Find(EdgeId);
			if (Edge == nullptr || !Edge->IsValid())
			{
				return false;
			}
			const FString SourcePinId =
				(*Edge)->GetStringField(TEXT("source_pin_id"));
			const FString TargetPinId =
				(*Edge)->GetStringField(TEXT("target_pin_id"));
			const TSharedPtr<FJsonObject>* SourcePin = PinsById.Find(SourcePinId);
			const TSharedPtr<FJsonObject>* TargetPin = PinsById.Find(TargetPinId);
			if (SourcePin == nullptr || !SourcePin->IsValid()
				|| TargetPin == nullptr || !TargetPin->IsValid())
			{
				return false;
			}
			TSharedRef<FJsonObject> Endpoint = MakeShared<FJsonObject>();
			Endpoint->SetStringField(TEXT("source_edge_id"), EdgeId);
			Endpoint->SetStringField(
				TEXT("source_node_id"),
				(*Edge)->GetStringField(TEXT("source_node_id")));
			Endpoint->SetStringField(TEXT("source_pin_id"), SourcePinId);
			Endpoint->SetStringField(
				TEXT("source_port_label"),
				(*SourcePin)->GetStringField(TEXT("name")));
			Endpoint->SetStringField(
				TEXT("target_node_id"),
				(*Edge)->GetStringField(TEXT("target_node_id")));
			Endpoint->SetStringField(TEXT("target_pin_id"), TargetPinId);
			Endpoint->SetStringField(
				TEXT("target_port_label"),
				(*TargetPin)->GetStringField(TEXT("name")));
			EndpointValues.Add(MakeShared<FJsonValueObject>(Endpoint));
		}
		Relation->SetArrayField(
			TEXT("source_edge_endpoints"), MoveTemp(EndpointValues));
	}
	return true;
}

struct FPackageDirtyFlagRestorer
{
	UPackage* Package = nullptr;
	bool bOriginalDirtyFlag = false;

	~FPackageDirtyFlagRestorer()
	{
		if (IsValid(Package))
		{
			Package->SetDirtyFlag(bOriginalDirtyFlag);
		}
	}
};

FBlueprintLensExplanationModel MakeLinearExplanation(
	const int32 IntermediateUnitCount = 12)
{
	FBlueprintLensExplanationModel Model;
	Model.Query.Question = TEXT("Why does LC1 Ready execute?");
	Model.Query.Direction = TEXT("backward_only");

	const int32 UnitCount = IntermediateUnitCount + 2;
	for (int32 Index = 0; Index < UnitCount; ++Index)
	{
		FBlueprintLensUnit Unit;
		Unit.Id = FString::Printf(TEXT("unit.%02d"), Index);
		Unit.Role = Index == UnitCount - 1
			? EBlueprintLensRole::Criterion
			: EBlueprintLensRole::Control;
		Unit.Title = Index == 0
			? TEXT("Event BeginPlay")
			: Index == UnitCount - 1
			? TEXT("Set LC1 Ready")
			: FString::Printf(TEXT("Set LC1 Step %02d Complete"), Index);
		Unit.SemanticStatus = EBlueprintLensSemanticStatus::Supported;
		Unit.InclusionReasons.Add(
			Index == UnitCount - 1
				? TEXT("criterion")
				: TEXT("execution_predecessor"));
		FBlueprintLensSourceReference Reference;
		Reference.BlueprintAssetPath =
			TEXT("/Game/LensCorpus/BP_LC1_LongChain."
				 "BP_LC1_LongChain");
		Reference.GraphId =
			TEXT("/Game/LensCorpus/BP_LC1_LongChain."
				 "BP_LC1_LongChain:EventGraph");
		Reference.SourceNodeId =
			FString::Printf(TEXT("source.node.%02d"), Index);
		Reference.NativeNodeGuid =
			FString::Printf(TEXT("00000000-0000-0000-0000-%012d"), Index);
		Reference.bPrimary = true;
		Unit.SourceReferences.Add(Reference);
		Model.Units.Add(Unit);
	}
	Model.CriterionUnitId = Model.Units.Last().Id;
	Model.Query.CriterionSourceNodeId =
		Model.Units.Last().SourceReferences[0].SourceNodeId;

	for (int32 Index = 0; Index < UnitCount - 1; ++Index)
	{
		FBlueprintLensRelation Relation;
		Relation.Id = FString::Printf(TEXT("relation.%02d"), Index);
		Relation.SourceUnitId = Model.Units[Index].Id;
		Relation.TargetUnitId = Model.Units[Index + 1].Id;
		Relation.Kind = EBlueprintLensRelationKind::ExecutionPredecessor;
		Relation.Label = TEXT("then");
		Relation.SourceEdgeIds.Add(
			FString::Printf(TEXT("source.edge.%02d"), Index));
		Model.Relations.Add(Relation);
	}

	FBlueprintLensLane CriterionLane;
	CriterionLane.Role = EBlueprintLensRole::Criterion;
	CriterionLane.State = EBlueprintLensLaneState::Populated;
	CriterionLane.UnitIds.Add(Model.CriterionUnitId);
	Model.Lanes.Add(CriterionLane);

	FBlueprintLensLane ControlLane;
	ControlLane.Role = EBlueprintLensRole::Control;
	ControlLane.State = EBlueprintLensLaneState::Populated;
	for (int32 Index = 0; Index < UnitCount - 1; ++Index)
	{
		ControlLane.UnitIds.Add(Model.Units[Index].Id);
	}
	Model.Lanes.Add(ControlLane);

	FBlueprintLensLane PredicateLane;
	PredicateLane.Role = EBlueprintLensRole::Predicate;
	PredicateLane.State = EBlueprintLensLaneState::Empty;
	PredicateLane.EmptyMessage =
		TEXT("No predicate facts in this explanation");
	Model.Lanes.Add(PredicateLane);

	FBlueprintLensLane ValueLane;
	ValueLane.Role = EBlueprintLensRole::Value;
	ValueLane.State = EBlueprintLensLaneState::Empty;
	ValueLane.EmptyMessage = TEXT("No value facts in this explanation");
	Model.Lanes.Add(ValueLane);

	FBlueprintLensLane ConsequenceLane;
	ConsequenceLane.Role = EBlueprintLensRole::Consequence;
	ConsequenceLane.State = EBlueprintLensLaneState::NotEnabled;
	ConsequenceLane.EmptyMessage =
		TEXT("Not enabled in this backward-only query");
	Model.Lanes.Add(ConsequenceLane);

	FBlueprintLensLane BoundaryLane;
	BoundaryLane.Role = EBlueprintLensRole::Boundary;
	BoundaryLane.State = EBlueprintLensLaneState::Empty;
	BoundaryLane.EmptyMessage = TEXT("All selected constructs supported");
	Model.Lanes.Add(BoundaryLane);
	return Model;
}

FString CanonicalRegionProjection(
	const FBlueprintLensLC1RegionProjection& Region)
{
	FString Result = FString::Printf(
		TEXT("%s|%s|%s|%s|%s|%s|%s|%d|%s|"),
		*Region.SourceIrSha256,
		*Region.ProjectorVersion,
		*Region.RegionId,
		*Region.RegionKind,
		*Region.FirstMemberUnitId,
		*Region.LastMemberUnitId,
		*Region.SummaryTemplateId,
		static_cast<int32>(Region.Status),
		*Region.ProjectionIntegrityHash);
	const auto AppendValues =
		[&Result](const TArray<FString>& Values)
		{
			for (const FString& Value : Values)
			{
				Result += FString::Printf(
					TEXT("%d:%s;"),
					Value.Len(),
					*Value);
			}
			Result += TEXT("|");
		};

	AppendValues(Region.OrderedMemberUnitIds);
	AppendValues(Region.InternalRelationIds);
	AppendValues(Region.IncomingRelationIds);
	AppendValues(Region.OutgoingRelationIds);
	AppendValues(Region.SummaryArguments);
	for (const FBlueprintLensLC1ClaimEvidence& Evidence :
		 Region.ClaimEvidence)
	{
		Result += FString::Printf(
			TEXT("%d:%s;%d:%s;%d:%s;%d:%s;"),
			Evidence.ClaimPart.Len(),
			*Evidence.ClaimPart,
			Evidence.FactOwner.Len(),
			*Evidence.FactOwner,
			Evidence.SourceId.Len(),
			*Evidence.SourceId,
			Evidence.Value.Len(),
			*Evidence.Value);
	}
	return Result;
}

FString CanonicalPseudocodeProjection(
	const FBlueprintLensLC1PseudocodeProjection& Projection)
{
	FString Result = FString::Printf(
		TEXT("%s|%s|%d|%s|%s|"),
		*Projection.SourceIrSha256,
		*Projection.ProjectorVersion,
		static_cast<int32>(Projection.Status),
		*Projection.DiagnosticCode,
		*Projection.ProjectionIntegrityHash);
	for (const FBlueprintLensLC1PseudocodeLine& Line : Projection.Lines)
	{
		Result += FString::Printf(
			TEXT("%s|%d|%s|%d|%d|%s|%s|%s|%s|%s|"),
			*Line.LineId,
			Line.LineNumber,
			*Line.CodeText,
			static_cast<int32>(Line.Role),
			static_cast<int32>(Line.SemanticStatus),
			*Line.UnitId,
			*Line.FollowingRelationId,
			*Line.SourceNodeId,
			*Line.FactOwner,
			*Line.ProjectionDiagnostic);
		for (const FString& PinId : Line.SourcePinIds)
		{
			Result += FString::Printf(TEXT("%d:%s;"), PinId.Len(), *PinId);
		}
		Result += TEXT("|");
	}
	return Result;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensEditorModuleLoadedTest,
	"BlueprintLens.Editor.ModuleLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensEditorModuleLoadedTest::RunTest(const FString&)
{
	FBlueprintLensEditorModule& Module =
		FModuleManager::LoadModuleChecked<FBlueprintLensEditorModule>(
			TEXT("BlueprintLensEditor"));
	TestEqual(
		TEXT("Stable tab ID"),
		FBlueprintLensEditorModule::SemanticLaneTabId,
		FName(TEXT("BlueprintLens.SemanticLane")));
	TestTrue(
		TEXT("Module is registered"),
		Module.IsTabExtensionRegistered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensExplanationPathCommandTest,
	"BlueprintLens.Editor.ExplanationPathCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensExplanationPathCommandTest::RunTest(const FString&)
{
	FBlueprintLensEditorModule& Module =
		FModuleManager::LoadModuleChecked<FBlueprintLensEditorModule>(
			TEXT("BlueprintLensEditor"));
	Module.HandleExplanationPathCommand({});
	TestEqual(
		TEXT("Zero arguments select the canonical fixture"),
		Module.GetExplanationPath(),
		FPaths::ConvertRelativePathToFull(CanonicalFixturePath()));

	const FString AbsoluteScenarioPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("BlueprintLens/Scenarios/stale-package-hash.json")));
	Module.HandleExplanationPathCommand({AbsoluteScenarioPath});
	TestEqual(
		TEXT("One absolute argument sets the session override"),
		Module.GetExplanationPath(),
		AbsoluteScenarioPath);

	Module.HandleExplanationPathCommand(
		{AbsoluteScenarioPath, AbsoluteScenarioPath});
	TestEqual(
		TEXT("More than one argument leaves the override unchanged"),
		Module.GetExplanationPath(),
		AbsoluteScenarioPath);

	Module.HandleExplanationPathCommand({TEXT("relative.json")});
	TestEqual(
		TEXT("A relative argument leaves the override unchanged"),
		Module.GetExplanationPath(),
		AbsoluteScenarioPath);

	Module.HandleExplanationPathCommand({});
	TestEqual(
		TEXT("Final reset restores the canonical fixture"),
		Module.GetExplanationPath(),
		FPaths::ConvertRelativePathToFull(CanonicalFixturePath()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensCanonicalFixtureTest,
	"BlueprintLens.Explanation.LoadCanonicalFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensCanonicalFixtureTest::RunTest(const FString&)
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	TestTrue(TEXT("Plugin is discoverable"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString FixturePath = CanonicalFixturePath();
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(FixturePath);
	TestTrue(TEXT("Canonical fixture loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("Lane count"), Result.Model->Lanes.Num(), 6);
	TestEqual(TEXT("Unit count"), Result.Model->Units.Num(), 6);
	TestEqual(TEXT("Relation count"), Result.Model->Relations.Num(), 5);
	TestEqual(TEXT("Source node count"), Result.Model->Counts.SourceNodes, 8);
	TestEqual(TEXT("Source edge count"), Result.Model->Counts.SourceEdges, 7);
	for (const FBlueprintLensRelation& Relation : Result.Model->Relations)
	{
		TestFalse(
			TEXT("Legacy relation retains absent endpoint-ledger state"),
			Relation.bHasSourceEdgeEndpoints);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensGeneralLaneArrangementTest,
	"BlueprintLens.Explanation.GeneralLaneArrangement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensGeneralLaneArrangementTest::RunTest(const FString&)
{
	const FMutatedFixtureLoad Result = LoadGeneralLaneFixture();
	TestTrue(TEXT("General lane fixture setup succeeds"), Result.bSetupSucceeded);
	if (!Result.bSetupSucceeded)
	{
		AddError(Result.SetupError);
		return false;
	}
	TestTrue(
		TEXT("Empty control and populated boundary lanes load"),
		Result.LoadResult.IsSuccess());
	if (!Result.LoadResult.IsSuccess())
	{
		AddError(Result.LoadResult.Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLoadRealLC2FixtureTest,
	"BlueprintLens.Explanation.LoadRealLC2Fixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLoadRealLC2FixtureTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("Real LC2 Explanation loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("LC2 lane count"), Result.Model->Lanes.Num(), 6);
	TestEqual(TEXT("LC2 unit count"), Result.Model->Units.Num(), 9);
	TestEqual(TEXT("LC2 relation count"), Result.Model->Relations.Num(), 10);
	TestEqual(TEXT("LC2 source node count"), Result.Model->Counts.SourceNodes, 9);
	TestEqual(TEXT("LC2 source edge count"), Result.Model->Counts.SourceEdges, 10);

	int32 CriterionUnits = 0;
	int32 ControlUnits = 0;
	int32 PredicateUnits = 0;
	TSet<FString> SourceNodeIds;
	for (const FBlueprintLensUnit& Unit : Result.Model->Units)
	{
		CriterionUnits += Unit.Role == EBlueprintLensRole::Criterion ? 1 : 0;
		ControlUnits += Unit.Role == EBlueprintLensRole::Control ? 1 : 0;
		PredicateUnits += Unit.Role == EBlueprintLensRole::Predicate ? 1 : 0;
		TestEqual(
			TEXT("Each LC2 unit owns one source navigation reference"),
			Unit.SourceReferences.Num(),
			1);
		if (Unit.SourceReferences.Num() == 1)
		{
			const FBlueprintLensSourceReference& Reference =
				Unit.SourceReferences[0];
			TestFalse(
				TEXT("LC2 source node navigation ID is present"),
				Reference.SourceNodeId.IsEmpty());
			TestFalse(
				TEXT("LC2 native source GUID is present"),
				Reference.NativeNodeGuid.IsEmpty());
			TestNotNull(
				TEXT("LC2 source navigation ID resolves"),
				Result.Model->FindSourceReference(Reference.SourceNodeId));
			SourceNodeIds.Add(Reference.SourceNodeId);
		}
	}
	TestEqual(TEXT("LC2 criterion units"), CriterionUnits, 1);
	TestEqual(TEXT("LC2 control units"), ControlUnits, 6);
	TestEqual(TEXT("LC2 predicate units"), PredicateUnits, 2);
	TestEqual(TEXT("LC2 unique source navigation IDs"), SourceNodeIds.Num(), 9);

	int32 ExecutionPredecessors = 0;
	int32 ControlsExecution = 0;
	int32 Predicates = 0;
	int32 ThenLabels = 0;
	int32 ElseLabels = 0;
	for (const FBlueprintLensRelation& Relation : Result.Model->Relations)
	{
		ExecutionPredecessors +=
			Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor
			? 1
			: 0;
		ControlsExecution +=
			Relation.Kind == EBlueprintLensRelationKind::ControlsExecution
			? 1
			: 0;
		Predicates +=
			Relation.Kind == EBlueprintLensRelationKind::PredicateFor ? 1 : 0;
		ThenLabels += Relation.Kind == EBlueprintLensRelationKind::ControlsExecution
			&& Relation.Label == TEXT("then")
			? 1
			: 0;
		ElseLabels += Relation.Kind == EBlueprintLensRelationKind::ControlsExecution
			&& Relation.Label == TEXT("else")
			? 1
			: 0;
		TestTrue(
			TEXT("LC2 relation retains mandatory endpoint ledger"),
			Relation.bHasSourceEdgeEndpoints);
		TestEqual(
			TEXT("LC2 relation owns exactly one source edge"),
			Relation.SourceEdgeIds.Num(),
			1);
		TestEqual(
			TEXT("LC2 relation owns exactly one endpoint fact"),
			Relation.SourceEdgeEndpoints.Num(),
			1);
		if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor)
		{
			TestEqual(
				TEXT("LC2 predicate label is exact Condition target port"),
				Relation.Label,
				FString(TEXT("Condition")));
		}
	}
	TestEqual(TEXT("LC2 execution predecessor relations"), ExecutionPredecessors, 4);
	TestEqual(TEXT("LC2 controls-execution relations"), ControlsExecution, 4);
	TestEqual(TEXT("LC2 predicate relations"), Predicates, 2);
	TestEqual(TEXT("LC2 then labels"), ThenLabels, 2);
	TestEqual(TEXT("LC2 else labels"), ElseLabels, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLoadRealLC3FixtureTest,
	"BlueprintLens.Explanation.LoadRealLC3Fixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLoadRealLC3FixtureTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("Real LC3 Explanation loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("LC3 lane count"), Result.Model->Lanes.Num(), 6);
	TestEqual(TEXT("LC3 unit count"), Result.Model->Units.Num(), 7);
	TestEqual(TEXT("LC3 relation count"), Result.Model->Relations.Num(), 6);
	TestEqual(TEXT("LC3 source node count"), Result.Model->Counts.SourceNodes, 7);
	TestEqual(TEXT("LC3 source edge count"), Result.Model->Counts.SourceEdges, 6);

	TSet<FString> SourceNodeIds;
	for (const FBlueprintLensUnit& Unit : Result.Model->Units)
	{
		TestEqual(
			TEXT("Each LC3 unit owns one source navigation reference"),
			Unit.SourceReferences.Num(),
			1);
		if (Unit.SourceReferences.Num() == 1)
		{
			SourceNodeIds.Add(Unit.SourceReferences[0].SourceNodeId);
		}
	}
	TestEqual(TEXT("LC3 unique source navigation IDs"), SourceNodeIds.Num(), 7);

	TSet<FString> ValuePortPairs;
	for (const FBlueprintLensRelation& Relation : Result.Model->Relations)
	{
		TestTrue(
			TEXT("LC3 relation retains mandatory endpoint ledger"),
			Relation.bHasSourceEdgeEndpoints);
		TestEqual(
			TEXT("LC3 relation owns exactly one source edge"),
			Relation.SourceEdgeIds.Num(),
			1);
		TestEqual(
			TEXT("LC3 relation owns exactly one endpoint fact"),
			Relation.SourceEdgeEndpoints.Num(),
			1);
		if (Relation.Kind != EBlueprintLensRelationKind::ProvidesValue ||
			Relation.SourceEdgeEndpoints.Num() != 1)
		{
			continue;
		}
		const FBlueprintLensUnit* Target =
			Result.Model->FindUnit(Relation.TargetUnitId);
		TestNotNull(TEXT("LC3 value consumer resolves"), Target);
		if (Target != nullptr)
		{
			const FBlueprintLensSourceEdgeEndpoint& Endpoint =
				Relation.SourceEdgeEndpoints[0];
			ValuePortPairs.Add(FString::Printf(
				TEXT("%s->%s.%s"),
				*Endpoint.SourcePortLabel,
				*Target->Title,
				*Endpoint.TargetPortLabel));
		}
	}
	for (const TCHAR* ExpectedPair : {
		TEXT("ReturnValue->Set LC3Score.LC3Score"),
		TEXT("ReturnValue->Subtract_IntInt.A"),
		TEXT("BaseScore->Add_IntInt.A"),
		TEXT("BonusScore->Add_IntInt.B"),
		TEXT("Penalty->Subtract_IntInt.B")})
	{
		TestTrue(
			FString::Printf(TEXT("LC3 exact value endpoint is loaded: %s"), ExpectedPair),
			ValuePortPairs.Contains(ExpectedPair));
	}
	TestEqual(TEXT("LC3 has five exact value endpoint pairs"), ValuePortPairs.Num(), 5);
	return true;
}

// Optional Explanation semantic extension v1.1. These cases cover the
// published LC2 load path and selected structural, provenance and partial-order
// failures; they are not exhaustive rule-by-rule coverage of Sections 4-6.
FString SemanticLabelFor(
	const FString& Kind, const FString& PortLabel)
{
	if (Kind == TEXT("controls_execution"))
	{
		return PortLabel == TEXT("then") ? TEXT("condition_true")
										 : TEXT("condition_false");
	}
	if (Kind == TEXT("predicate_for"))
	{
		return TEXT("branch_condition");
	}
	if (Kind == TEXT("provides_value"))
	{
		return TEXT("value_input");
	}
	return TEXT("next_execution");
}

// Labels every relation, disambiguates each guarded Branch from its single
// predicate attachment, and adds two incomparable outcome paths taken from the
// inner guard's own outlets.
void AddSemanticExtension(TSharedRef<FJsonObject> Explanation)
{
	const TArray<TSharedPtr<FJsonValue>> Relations =
		Explanation->GetArrayField(TEXT("relations"));
	TMap<FString, int32> PredicateTargets;
	TSet<FString> ControlTargets;
	for (const TSharedPtr<FJsonValue>& Value : Relations)
	{
		const TSharedPtr<FJsonObject> Relation = Value->AsObject();
		const FString Kind = Relation->GetStringField(TEXT("kind"));
		const FString PortLabel = Relation->GetArrayField(
										  TEXT("source_edge_endpoints"))[0]
									  ->AsObject()
									  ->GetStringField(TEXT("source_port_label"));
		Relation->SetStringField(TEXT("port_label"), PortLabel);
		Relation->SetStringField(
			TEXT("semantic_label"), SemanticLabelFor(Kind, PortLabel));
		if (Kind == TEXT("predicate_for"))
		{
			PredicateTargets.FindOrAdd(
				Relation->GetStringField(TEXT("target_unit_id")))++;
		}
		else if (Kind == TEXT("controls_execution"))
		{
			ControlTargets.Add(Relation->GetStringField(TEXT("target_unit_id")));
		}
	}

	for (const TSharedPtr<FJsonValue>& Value :
		 Explanation->GetArrayField(TEXT("units")))
	{
		const TSharedPtr<FJsonObject> Unit = Value->AsObject();
		const FString UnitId = Unit->GetStringField(TEXT("id"));
		const int32* Count = PredicateTargets.Find(UnitId);
		if (Count == nullptr || *Count != 1)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& RelationValue : Relations)
		{
			const TSharedPtr<FJsonObject> Relation = RelationValue->AsObject();
			if (Relation->GetStringField(TEXT("kind")) != TEXT("predicate_for") ||
				Relation->GetStringField(TEXT("target_unit_id")) != UnitId)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Disambiguator =
				MakeShared<FJsonObject>();
			Disambiguator->SetStringField(
				TEXT("text"),
				Relation->GetArrayField(TEXT("source_edge_endpoints"))[0]
					->AsObject()
					->GetStringField(TEXT("source_port_label")));
			Disambiguator->SetStringField(
				TEXT("rule_id"), TEXT("unit.branch.from_predicate_for"));
			TArray<TSharedPtr<FJsonValue>> Evidence;
			Evidence.Add(MakeShared<FJsonValueString>(
				Relation->GetStringField(TEXT("id"))));
			Disambiguator->SetArrayField(
				TEXT("evidence_relation_ids"), MoveTemp(Evidence));
			Unit->SetObjectField(TEXT("disambiguator"), Disambiguator);
			break;
		}
	}

	// The inner guard is the only Branch that is itself entered by a guarded
	// outcome, so its two outlets terminate two genuinely incomparable paths.
	FString InnerGuardId;
	for (const TSharedPtr<FJsonValue>& Value : Relations)
	{
		const TSharedPtr<FJsonObject> Relation = Value->AsObject();
		if (Relation->GetStringField(TEXT("kind")) != TEXT("controls_execution"))
		{
			continue;
		}
		const FString SourceId = Relation->GetStringField(TEXT("source_unit_id"));
		if (ControlTargets.Contains(SourceId))
		{
			InnerGuardId = SourceId;
			break;
		}
	}

	TArray<TSharedPtr<FJsonValue>> Groups;
	TArray<TSharedPtr<FJsonValue>> ExitPair;
	int32 PathIndex = 0;
	for (const TSharedPtr<FJsonValue>& Value : Relations)
	{
		const TSharedPtr<FJsonObject> Relation = Value->AsObject();
		if (Relation->GetStringField(TEXT("kind")) != TEXT("controls_execution") ||
			Relation->GetStringField(TEXT("source_unit_id")) != InnerGuardId)
		{
			continue;
		}
		const FString Entry = Relation->GetStringField(TEXT("source_unit_id"));
		const FString Exit = Relation->GetStringField(TEXT("target_unit_id"));
		const FString GroupId =
			FString::Printf(TEXT("group.outcome_path.%d"), PathIndex++);
		const TSharedRef<FJsonObject> Group = MakeShared<FJsonObject>();
		Group->SetStringField(TEXT("id"), GroupId);
		Group->SetStringField(TEXT("kind"), TEXT("outcome_path"));
		Group->SetStringField(TEXT("title"), TEXT(""));
		TArray<TSharedPtr<FJsonValue>> Units;
		Units.Add(MakeShared<FJsonValueString>(Entry));
		Units.Add(MakeShared<FJsonValueString>(Exit));
		Group->SetArrayField(TEXT("ordered_unit_ids"), MoveTemp(Units));
		TArray<TSharedPtr<FJsonValue>> GroupRelations;
		GroupRelations.Add(
			MakeShared<FJsonValueString>(Relation->GetStringField(TEXT("id"))));
		Group->SetArrayField(
			TEXT("ordered_relation_ids"), MoveTemp(GroupRelations));
		Group->SetStringField(TEXT("entry_unit_id"), Entry);
		Group->SetStringField(TEXT("exit_unit_id"), Exit);
		Group->SetField(TEXT("parent_group_id"), MakeShared<FJsonValueNull>());
		Group->SetField(TEXT("entered_by"), MakeShared<FJsonValueNull>());
		Group->SetNumberField(TEXT("member_count"), 2);
		Group->SetStringField(TEXT("projection_status"), TEXT("STRUCTURAL_ONLY"));
		Group->SetStringField(TEXT("diagnostic_code"), TEXT(""));
		Group->SetArrayField(
			TEXT("claim_evidence"), TArray<TSharedPtr<FJsonValue>>());
		Groups.Add(MakeShared<FJsonValueObject>(Group));
		ExitPair.Add(MakeShared<FJsonValueString>(GroupId));
	}
	Explanation->SetArrayField(TEXT("groups"), Groups);

	const TSharedRef<FJsonObject> PartialOrder = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Pairs;
	Pairs.Add(MakeShared<FJsonValueArray>(ExitPair));
	PartialOrder->SetArrayField(TEXT("incomparable_group_ids"), MoveTemp(Pairs));
	PartialOrder->SetStringField(
		TEXT("semantics"),
		TEXT("no execution order is proven between these groups"));
	Explanation->SetObjectField(TEXT("group_partial_order"), PartialOrder);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensSemanticExtensionTest,
	"BlueprintLens.Explanation.SemanticExtension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensSemanticExtensionTest::RunTest(const FString&)
{
	// The published LC2 fixture carries the v1.1 extension since repair Task 4.
	// These assertions prove the loader reads the real projector output, not
	// only the synthetic augmentation built below.
	const FBlueprintLensLoadResult Frozen =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("Published LC2 fixture loads"), Frozen.IsSuccess());
	if (Frozen.IsSuccess())
	{
		const FBlueprintLensExplanationModel& Model = *Frozen.Model;
		TestTrue(TEXT("Published LC2 declares groups"), Model.bHasGroups);
		TestEqual(TEXT("Published LC2 group count"), Model.Groups.Num(), 5);

		int32 OutcomePaths = 0;
		int32 GuardNests = 0;
		for (const FBlueprintLensGroup& Group : Model.Groups)
		{
			OutcomePaths +=
				Group.Kind == EBlueprintLensGroupKind::OutcomePath ? 1 : 0;
			GuardNests +=
				Group.Kind == EBlueprintLensGroupKind::GuardNest ? 1 : 0;
		}
		TestEqual(TEXT("Published LC2 outcome paths"), OutcomePaths, 3);
		TestEqual(TEXT("Published LC2 guard nests"), GuardNests, 2);
		for (const FBlueprintLensGroup& Group : Model.Groups)
		{
			if (Group.Kind == EBlueprintLensGroupKind::GuardNest)
			{
				TestFalse(
					TEXT("Published LC2 guard nest has no fabricated exit"),
					Group.bHasExitUnitId);
			}
		}

		// Each outcome path must exit at its own outcome Set. Collapsing the
		// exits onto the shared criterion would make the Section 6.2
		// incomparability check vacuous while still reporting success.
		TSet<FString> OutcomeExits;
		for (const FBlueprintLensGroup& Group : Model.Groups)
		{
			if (Group.Kind == EBlueprintLensGroupKind::OutcomePath)
			{
				OutcomeExits.Add(Group.ExitUnitId);
			}
		}
		TestEqual(TEXT("Outcome paths exit at distinct units"),
			OutcomeExits.Num(), 3);

		TestTrue(TEXT("Published LC2 declares a group partial order"),
			Model.bHasGroupPartialOrder);
		TestEqual(TEXT("Published LC2 incomparable pairs"),
			Model.GroupPartialOrder.IncomparableGroupIds.Num(), 3);

		int32 Disambiguated = 0;
		for (const FBlueprintLensUnit& Unit : Model.Units)
		{
			Disambiguated += Unit.bHasDisambiguator ? 1 : 0;
		}
		TestEqual(TEXT("Published LC2 disambiguates both guards"),
			Disambiguated, 2);

		// The repaired defect: `then` previously labelled both a guard's true
		// outlet and an ordinary execution pin. The two meanings must now be
		// distinguishable from the model alone.
		int32 ThenConditionTrue = 0;
		int32 ThenNextExecution = 0;
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			TestTrue(
				TEXT("Published LC2 relation carries a semantic label"),
				Relation.bHasSemanticLabel);
			if (Relation.PortLabel != TEXT("then"))
			{
				continue;
			}
			ThenConditionTrue +=
				Relation.SemanticLabel ==
					EBlueprintLensSemanticLabel::ConditionTrue ? 1 : 0;
			ThenNextExecution +=
				Relation.SemanticLabel ==
					EBlueprintLensSemanticLabel::NextExecution ? 1 : 0;
		}
		TestEqual(TEXT("`then` guard outlets"), ThenConditionTrue, 2);
		TestEqual(TEXT("`then` ordinary execution pins"), ThenNextExecution, 4);
	}

	const FMutatedFixtureLoad Valid = LoadMutatedLC2Fixture(
		TEXT("semantic-extension-valid"), AddSemanticExtension);
	TestTrue(TEXT("Augmented setup succeeds"), Valid.bSetupSucceeded);
	TestTrue(TEXT("Augmented LC2 loads"), Valid.LoadResult.IsSuccess());
	if (Valid.LoadResult.IsSuccess())
	{
		const FBlueprintLensExplanationModel& Model = *Valid.LoadResult.Model;
		TestTrue(TEXT("Groups are present"), Model.bHasGroups);
		TestEqual(TEXT("Two guarded outcome paths"), Model.Groups.Num(), 2);
		TestTrue(
			TEXT("Group partial order is present"), Model.bHasGroupPartialOrder);
		int32 Disambiguated = 0;
		for (const FBlueprintLensUnit& Unit : Model.Units)
		{
			Disambiguated += Unit.bHasDisambiguator ? 1 : 0;
		}
		TestEqual(TEXT("Both guards are disambiguated"), Disambiguated, 2);
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			TestTrue(
				TEXT("Every relation carries a semantic label"),
				Relation.bHasSemanticLabel);
		}
	}
	else if (Valid.bSetupSucceeded)
	{
		AddError(Valid.LoadResult.Error);
	}

	const auto VerifyRejected =
		[this](
			const TCHAR* Name,
			const TCHAR* ExpectedDiagnostic,
			TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate)
		{
			const FMutatedFixtureLoad Scenario =
				LoadMutatedLC2Fixture(Name, Mutate);
			TestTrue(
				*FString::Printf(TEXT("%s setup succeeds"), Name),
				Scenario.bSetupSucceeded);
			if (!Scenario.bSetupSucceeded)
			{
				AddError(Scenario.SetupError);
				return;
			}
			TestFalse(
				*FString::Printf(TEXT("%s is rejected"), Name),
				Scenario.LoadResult.IsSuccess());
			TestTrue(
				*FString::Printf(TEXT("%s diagnostic is stable"), Name),
				Scenario.LoadResult.Error.Contains(ExpectedDiagnostic));
		};

	VerifyRejected(
		TEXT("semantic-extension-label-mismatch"),
		TEXT("relation semantic_label must be"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			for (const TSharedPtr<FJsonValue>& Value :
				 Root->GetArrayField(TEXT("relations")))
			{
				const TSharedPtr<FJsonObject> Relation = Value->AsObject();
				if (Relation->GetStringField(TEXT("semantic_label")) ==
					TEXT("condition_true"))
				{
					Relation->SetStringField(
						TEXT("semantic_label"), TEXT("condition_false"));
					break;
				}
			}
		});

	VerifyRejected(
		TEXT("semantic-extension-port-label-forged"),
		TEXT("port_label disagrees with endpoint provenance"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("relations"))[0]->AsObject()->SetStringField(
				TEXT("port_label"), TEXT("not-the-exported-pin"));
		});

	VerifyRejected(
		TEXT("semantic-extension-disambiguator-forged"),
		TEXT("disambiguator text must equal the exported source port label"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			for (const TSharedPtr<FJsonValue>& Value :
				 Root->GetArrayField(TEXT("units")))
			{
				const TSharedPtr<FJsonObject> Unit = Value->AsObject();
				const TSharedPtr<FJsonObject>* Disambiguator = nullptr;
				if (Unit->TryGetObjectField(TEXT("disambiguator"), Disambiguator))
				{
					(*Disambiguator)->SetStringField(
						TEXT("text"), TEXT("SomethingElse"));
					break;
				}
			}
		});

	VerifyRejected(
		TEXT("semantic-extension-disambiguator-role"),
		TEXT("disambiguator rule requires control role"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			FString DisambiguatedUnitId;
			for (const TSharedPtr<FJsonValue>& Value :
				 Root->GetArrayField(TEXT("units")))
			{
				const TSharedPtr<FJsonObject> Unit = Value->AsObject();
				if (Unit->Values.Contains(TEXT("disambiguator")))
				{
					DisambiguatedUnitId = Unit->GetStringField(TEXT("id"));
					Unit->SetStringField(TEXT("role"), TEXT("predicate"));
					break;
				}
			}
			for (const TSharedPtr<FJsonValue>& Value :
				 Root->GetArrayField(TEXT("lanes")))
			{
				const TSharedPtr<FJsonObject> Lane = Value->AsObject();
				const FString LaneRole = Lane->GetStringField(TEXT("role"));
				TArray<TSharedPtr<FJsonValue>> UnitIds =
					Lane->GetArrayField(TEXT("unit_ids"));
				if (LaneRole == TEXT("control"))
				{
					UnitIds.RemoveAll(
						[&](const TSharedPtr<FJsonValue>& Item)
						{
							return Item->AsString() == DisambiguatedUnitId;
						});
				}
				else if (LaneRole == TEXT("predicate"))
				{
					UnitIds.Add(
						MakeShared<FJsonValueString>(DisambiguatedUnitId));
				}
				Lane->SetArrayField(TEXT("unit_ids"), MoveTemp(UnitIds));
			}
		});

	VerifyRejected(
		TEXT("semantic-extension-guard-exit-forbidden"),
		TEXT("guard_nest must not declare exit_unit_id"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("groups"))[0]
				->AsObject()
				->SetStringField(TEXT("kind"), TEXT("guard_nest"));
		});

	VerifyRejected(
		TEXT("semantic-extension-outcome-exit-required"),
		TEXT("outcome_path requires exit_unit_id"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("groups"))[0]
				->AsObject()
				->RemoveField(TEXT("exit_unit_id"));
		});

	VerifyRejected(
		TEXT("semantic-extension-claim-coverage-requires-branch-outcome-prefix"),
		TEXT("does not cover stated claim component 'branch_outcome'"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> Group =
				Root->GetArrayField(TEXT("groups"))[0]->AsObject();
			Group->SetStringField(TEXT("projection_status"), TEXT("COMPLETE"));
			Group->SetStringField(TEXT("diagnostic_code"), TEXT(""));
			// A COMPLETE group requires a title, and the helper now builds
			// STRUCTURAL_ONLY groups with an empty one. Without this the status
			// rule fires first and the coverage rule under test never runs.
			Group->SetStringField(TEXT("title"), TEXT("Guarded outcome"));
			const TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
			Evidence->SetStringField(
				TEXT("component"), TEXT("predicate_label.0"));
			Evidence->SetStringField(TEXT("fact_owner"), TEXT("fixture"));
			Evidence->SetStringField(TEXT("source"), TEXT("fixture"));
			TArray<TSharedPtr<FJsonValue>> EvidenceValues;
			EvidenceValues.Add(MakeShared<FJsonValueObject>(Evidence));
			Group->SetArrayField(
				TEXT("claim_evidence"),
				MoveTemp(EvidenceValues));
		});

	VerifyRejected(
		TEXT("semantic-extension-claim-coverage-requires-predicate-label-prefix"),
		TEXT("does not cover predicate_label prefix"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> Group =
				Root->GetArrayField(TEXT("groups"))[0]->AsObject();
			Group->SetStringField(TEXT("projection_status"), TEXT("COMPLETE"));
			Group->SetStringField(TEXT("diagnostic_code"), TEXT(""));
			Group->SetStringField(TEXT("title"), TEXT("Guarded outcome"));
			const TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
			Evidence->SetStringField(
				TEXT("component"), TEXT("branch_outcome.0"));
			Evidence->SetStringField(TEXT("fact_owner"), TEXT("fixture"));
			Evidence->SetStringField(TEXT("source"), TEXT("fixture"));
			TArray<TSharedPtr<FJsonValue>> EvidenceValues;
			EvidenceValues.Add(MakeShared<FJsonValueObject>(Evidence));
			Group->SetArrayField(
				TEXT("claim_evidence"),
				MoveTemp(EvidenceValues));
		});

	VerifyRejected(
		TEXT("semantic-extension-claim-coverage-prefixes-must-match"),
		TEXT("does not cover stated claim component 'branch_outcome.0'"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> Group =
				Root->GetArrayField(TEXT("groups"))[0]->AsObject();
			Group->SetStringField(TEXT("projection_status"), TEXT("COMPLETE"));
			Group->SetStringField(TEXT("diagnostic_code"), TEXT(""));
			Group->SetStringField(TEXT("title"), TEXT("Guarded outcome"));
			TArray<TSharedPtr<FJsonValue>> EvidenceValues;
			for (const TCHAR* Component :
				 {TEXT("predicate_label.0"), TEXT("branch_outcome.1")})
			{
				const TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
				Evidence->SetStringField(TEXT("component"), Component);
				Evidence->SetStringField(TEXT("fact_owner"), TEXT("fixture"));
				Evidence->SetStringField(TEXT("source"), TEXT("fixture"));
				EvidenceValues.Add(MakeShared<FJsonValueObject>(Evidence));
			}
			Group->SetArrayField(
				TEXT("claim_evidence"),
				MoveTemp(EvidenceValues));
		});

	VerifyRejected(
		TEXT("semantic-extension-collapsed-outcome-exits"),
		TEXT("outcome_path exit_unit_id must not equal criterion_unit_id"),
		[](TSharedRef<FJsonObject> Root)
		{
			const FString Criterion =
				Root->GetStringField(TEXT("criterion_unit_id"));
			for (const TSharedPtr<FJsonValue>& GroupValue :
				 Root->GetArrayField(TEXT("groups")))
			{
				const TSharedPtr<FJsonObject> Group = GroupValue->AsObject();
				if (Group->GetStringField(TEXT("kind")) != TEXT("outcome_path"))
				{
					continue;
				}
				const FString Exit = Group->GetStringField(TEXT("exit_unit_id"));
				FString ReconvergenceId;
				for (const TSharedPtr<FJsonValue>& RelationValue :
					 Root->GetArrayField(TEXT("relations")))
				{
					const TSharedPtr<FJsonObject> Relation =
						RelationValue->AsObject();
					if (Relation->GetStringField(TEXT("kind")) ==
							TEXT("execution_predecessor") &&
						Relation->GetStringField(TEXT("source_unit_id")) == Exit &&
						Relation->GetStringField(TEXT("target_unit_id")) == Criterion)
					{
						ReconvergenceId = Relation->GetStringField(TEXT("id"));
						break;
					}
				}
				TArray<TSharedPtr<FJsonValue>> Units =
					Group->GetArrayField(TEXT("ordered_unit_ids"));
				Units.Add(MakeShared<FJsonValueString>(Criterion));
				Group->SetArrayField(TEXT("ordered_unit_ids"), MoveTemp(Units));
				TArray<TSharedPtr<FJsonValue>> Relations =
					Group->GetArrayField(TEXT("ordered_relation_ids"));
				Relations.Add(MakeShared<FJsonValueString>(ReconvergenceId));
				Group->SetArrayField(
					TEXT("ordered_relation_ids"), MoveTemp(Relations));
				Group->SetStringField(TEXT("exit_unit_id"), Criterion);
				Group->SetNumberField(
					TEXT("member_count"),
					Group->GetArrayField(TEXT("ordered_unit_ids")).Num());
			}
		});

	VerifyRejected(
		TEXT("semantic-extension-coincident-incomparable-exits"),
		TEXT("incomparable groups must have distinct exit_unit_id values"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> First =
				Root->GetArrayField(TEXT("groups"))[0]->AsObject();
			const TSharedPtr<FJsonObject> Second =
				Root->GetArrayField(TEXT("groups"))[1]->AsObject();
			TArray<TSharedPtr<FJsonValue>> FirstUnits =
				First->GetArrayField(TEXT("ordered_unit_ids"));
			TArray<TSharedPtr<FJsonValue>> FirstRelations =
				First->GetArrayField(TEXT("ordered_relation_ids"));
			Second->SetArrayField(
				TEXT("ordered_unit_ids"),
				MoveTemp(FirstUnits));
			Second->SetArrayField(
				TEXT("ordered_relation_ids"),
				MoveTemp(FirstRelations));
			Second->SetStringField(
				TEXT("entry_unit_id"),
				First->GetStringField(TEXT("entry_unit_id")));
			Second->SetStringField(
				TEXT("exit_unit_id"),
				First->GetStringField(TEXT("exit_unit_id")));
			Second->SetNumberField(
				TEXT("member_count"),
				Second->GetArrayField(TEXT("ordered_unit_ids")).Num());
		});

	VerifyRejected(
		TEXT("semantic-extension-group-unit-missing"),
		TEXT("group unit does not resolve"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			TArray<TSharedPtr<FJsonValue>> Units =
				Root->GetArrayField(TEXT("groups"))[0]->AsObject()->GetArrayField(
					TEXT("ordered_unit_ids"));
			Units[0] = MakeShared<FJsonValueString>(TEXT("unit.does.not.exist"));
			Root->GetArrayField(TEXT("groups"))[0]->AsObject()->SetArrayField(
				TEXT("ordered_unit_ids"), MoveTemp(Units));
		});

	VerifyRejected(
		TEXT("semantic-extension-group-member-count"),
		TEXT("group member_count does not match members"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("groups"))[0]->AsObject()->SetNumberField(
				TEXT("member_count"), 3);
		});

	VerifyRejected(
		TEXT("semantic-extension-structural-claims-evidence"),
		TEXT("STRUCTURAL_ONLY group requires an empty title and empty claim_evidence"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("component"), TEXT("c"));
			Entry->SetStringField(TEXT("fact_owner"), TEXT("f"));
			Entry->SetStringField(TEXT("source"), TEXT("s"));
			TArray<TSharedPtr<FJsonValue>> Evidence;
			Evidence.Add(MakeShared<FJsonValueObject>(Entry));
			Root->GetArrayField(TEXT("groups"))[0]->AsObject()->SetArrayField(
				TEXT("claim_evidence"), MoveTemp(Evidence));
		});

	VerifyRejected(
		TEXT("semantic-extension-structural-title"),
		TEXT("STRUCTURAL_ONLY group requires an empty title and empty claim_evidence"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("groups"))[0]
				->AsObject()
				->SetStringField(TEXT("title"), TEXT("Structural label"));
		});

	VerifyRejected(
		TEXT("semantic-extension-partial-order-semantics"),
		TEXT("group_partial_order semantics does not match frozen constant"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetObjectField(TEXT("group_partial_order"))
				->SetStringField(TEXT("semantics"), TEXT("forged semantics"));
		});

	VerifyRejected(
		TEXT("semantic-extension-incomparable-pair-exact-duplicate"),
		TEXT("incomparable pair is duplicated"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> PartialOrder =
				Root->GetObjectField(TEXT("group_partial_order"));
			TArray<TSharedPtr<FJsonValue>> Pairs =
				PartialOrder->GetArrayField(TEXT("incomparable_group_ids"));
			// Copy out before adding. `Pairs.Add(Pairs[0])` passes a reference
			// into the array being grown, and TArray asserts on that aliasing
			// because the reallocation invalidates it — which hard-crashed the
			// Editor twice before the log named this line.
			const TSharedPtr<FJsonValue> FirstPair = Pairs[0];
			Pairs.Add(FirstPair);
			PartialOrder->SetArrayField(
				TEXT("incomparable_group_ids"), MoveTemp(Pairs));
		});

	VerifyRejected(
		TEXT("semantic-extension-incomparable-pair-reversed-duplicate"),
		TEXT("incomparable pair is duplicated"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			const TSharedPtr<FJsonObject> PartialOrder =
				Root->GetObjectField(TEXT("group_partial_order"));
			const TArray<TSharedPtr<FJsonValue>>& ExistingPairs =
				PartialOrder->GetArrayField(TEXT("incomparable_group_ids"));
			const TArray<TSharedPtr<FJsonValue>>& ExistingPair =
				ExistingPairs[0]->AsArray();
			TArray<TSharedPtr<FJsonValue>> ReversedPair;
			ReversedPair.Add(ExistingPair[1]);
			ReversedPair.Add(ExistingPair[0]);
			TArray<TSharedPtr<FJsonValue>> Pairs = ExistingPairs;
			Pairs.Add(MakeShared<FJsonValueArray>(MoveTemp(ReversedPair)));
			PartialOrder->SetArrayField(
				TEXT("incomparable_group_ids"), MoveTemp(Pairs));
		});

	VerifyRejected(
		TEXT("semantic-extension-unknown-group-kind"),
		TEXT("has unknown group kind"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->GetArrayField(TEXT("groups"))[0]->AsObject()->SetStringField(
				TEXT("kind"), TEXT("proximity_cluster"));
		});

	VerifyRejected(
		TEXT("semantic-extension-partial-order-without-groups"),
		TEXT("group_partial_order requires groups"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			Root->RemoveField(TEXT("groups"));
		});

	VerifyRejected(
		TEXT("semantic-extension-ordered-pair-declared-incomparable"),
		TEXT("declared incomparable groups are ordered"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddSemanticExtension(Root);
			// Re-anchor the first path so the pair becomes genuinely ordered
			// while staying clear of the two stricter rules that now guard this
			// check. The exits must differ from each other, and an outcome path
			// may not exit at the criterion — so anchoring on any relation, or
			// on one leaving the second path's exit (whose next execution is the
			// reconvergence criterion), trips a different rule and never proves
			// reachability. The outer guard's outlet into the inner guard works:
			// its exit is the inner Branch, which differs from the second path's
			// Set, is not the criterion, and demonstrably reaches that Set.
			const FString InnerGuard = Root->GetArrayField(TEXT("groups"))[1]
										   ->AsObject()
										   ->GetStringField(TEXT("entry_unit_id"));
			for (const TSharedPtr<FJsonValue>& Value :
				 Root->GetArrayField(TEXT("relations")))
			{
				const TSharedPtr<FJsonObject> Relation = Value->AsObject();
				if (Relation->GetStringField(TEXT("kind")) !=
						TEXT("controls_execution") ||
					Relation->GetStringField(TEXT("target_unit_id")) !=
						InnerGuard)
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Group =
					Root->GetArrayField(TEXT("groups"))[0]->AsObject();
				const FString Entry =
					Relation->GetStringField(TEXT("source_unit_id"));
				const FString Exit =
					Relation->GetStringField(TEXT("target_unit_id"));
				TArray<TSharedPtr<FJsonValue>> Units;
				Units.Add(MakeShared<FJsonValueString>(Entry));
				Units.Add(MakeShared<FJsonValueString>(Exit));
				Group->SetArrayField(TEXT("ordered_unit_ids"), MoveTemp(Units));
				TArray<TSharedPtr<FJsonValue>> GroupRelations;
				GroupRelations.Add(MakeShared<FJsonValueString>(
					Relation->GetStringField(TEXT("id"))));
				Group->SetArrayField(
					TEXT("ordered_relation_ids"), MoveTemp(GroupRelations));
				Group->SetStringField(TEXT("entry_unit_id"), Entry);
				Group->SetStringField(TEXT("exit_unit_id"), Exit);
				break;
			}
		});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC2GuardOutlineProjectionTest,
	"BlueprintLens.LC2.GuardOutlineProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC2GuardOutlineProjectionTest::RunTest(
	const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("LC2 fixture loads for guard outline"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC2GuardOutlineProjection Projection =
		FBlueprintLensLC2GuardOutlineProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC2 guard outline is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	TestEqual(
		TEXT("LC2 guard outline status is grouped"),
		Projection.Status,
		EBlueprintLensLC2GuardOutlineProjectionStatus::GroupedOutcomePaths);
	TestTrue(
		TEXT("LC2 guard outline integrity is valid"),
		Projection.HasValidIntegrity());
	TestEqual(
		TEXT("Grouped state retains all nine units"),
		Projection.AllUnitIds.Num(),
		9);
	TestEqual(
		TEXT("Grouped state retains all ten relations"),
		Projection.AllRelationIds.Num(),
		10);
	TestEqual(
		TEXT("Fallback unit ledger equals complete unit ledger"),
		Projection.FallbackUnitIds,
		Projection.AllUnitIds);
	TestEqual(
		TEXT("Fallback relation ledger equals complete relation ledger"),
		Projection.FallbackRelationIds,
		Projection.AllRelationIds);

	const TCHAR* ExpectedPathIds[] = {
		TEXT("group.outcome_path.accepted"),
		TEXT("group.outcome_path.inner_rejected"),
		TEXT("group.outcome_path.outer_rejected")};
	const TCHAR* ExpectedPathTitles[] = {
		TEXT("Both guards passed"),
		TEXT("InnerEnabled was false"),
		TEXT("OuterEnabled was false")};
	const int32 ExpectedDepths[][4] = {
		{0, 1, 2, 3},
		{0, 1, 2, 3},
		{0, 1, 2, 0}};
	const TCHAR* ExpectedGuardGroupIds[][4] = {
		{TEXT(""),
		 TEXT("group.guard_nest.outer_guard"),
		 TEXT("group.guard_nest.inner_guard"),
		 TEXT("group.guard_nest.inner_guard")},
		{TEXT(""),
		 TEXT("group.guard_nest.outer_guard"),
		 TEXT("group.guard_nest.inner_guard"),
		 TEXT("group.guard_nest.inner_guard")},
		{TEXT(""),
		 TEXT("group.guard_nest.outer_guard"),
		 TEXT("group.guard_nest.outer_guard"),
		 TEXT("")}};
	const int32 ExpectedRowCounts[] = {4, 4, 3};
	TestEqual(TEXT("Three numbered outcome paths are projected"),
		Projection.OutcomePaths.Num(),
		3);
	for (int32 PathIndex = 0;
		 PathIndex < Projection.OutcomePaths.Num();
		 ++PathIndex)
	{
		const FBlueprintLensLC2GuardOutlinePath& Path =
			Projection.OutcomePaths[PathIndex];
		TestEqual(
			FString::Printf(TEXT("Outcome path %d keeps ledger order"), PathIndex + 1),
			Path.GroupId,
			FString(ExpectedPathIds[PathIndex]));
		TestEqual(
			FString::Printf(TEXT("Outcome path %d keeps reader title"), PathIndex + 1),
			Path.Title,
			FString(ExpectedPathTitles[PathIndex]));
		TestEqual(
			FString::Printf(TEXT("Outcome path %d row count"), PathIndex + 1),
			Path.Rows.Num(),
			ExpectedRowCounts[PathIndex]);
		for (int32 RowIndex = 0; RowIndex < Path.Rows.Num(); ++RowIndex)
		{
			TestEqual(
				FString::Printf(
					TEXT("Outcome path %d row %d has innermost guard group id"),
					PathIndex + 1,
					RowIndex + 1),
				Path.Rows[RowIndex].GuardGroupId,
				FString(ExpectedGuardGroupIds[PathIndex][RowIndex]));
			TestEqual(
				FString::Printf(
					TEXT("Outcome path %d row %d has corrected innermost guard depth"),
					PathIndex + 1,
					RowIndex + 1),
				Path.Rows[RowIndex].NestingDepth,
				ExpectedDepths[PathIndex][RowIndex]);
		}
	}

	const FBlueprintLensLC2GuardOutlineNest* OuterNest =
		Projection.GuardNests.FindByPredicate(
			[](const FBlueprintLensLC2GuardOutlineNest& Nest)
			{
				return Nest.GroupId == TEXT("group.guard_nest.outer_guard");
			});
	const FBlueprintLensLC2GuardOutlineNest* InnerNest =
		Projection.GuardNests.FindByPredicate(
			[](const FBlueprintLensLC2GuardOutlineNest& Nest)
			{
				return Nest.GroupId == TEXT("group.guard_nest.inner_guard");
			});
	TestNotNull(TEXT("Outer guard nest is projected"), OuterNest);
	TestNotNull(TEXT("Inner guard nest is projected"), InnerNest);
	if (OuterNest != nullptr && InnerNest != nullptr)
	{
		TestEqual(
			TEXT("Outer guard uses its disambiguator"),
			OuterNest->ReaderLabel,
			FString(TEXT("Branch (OuterEnabled)")));
		TestEqual(
			TEXT("Inner guard uses its disambiguator"),
			InnerNest->ReaderLabel,
			FString(TEXT("Branch (InnerEnabled)")));
		TestFalse(
			TEXT("Outer guard has no parent"),
			OuterNest->bHasParent);
		TestTrue(
			TEXT("Inner guard has a parent"),
			InnerNest->bHasParent);
		TestEqual(
			TEXT("Inner guard parent comes from guard_nest"),
			InnerNest->ParentGroupId,
			FString(TEXT("group.guard_nest.outer_guard")));
		TestTrue(
			TEXT("Inner guard records its entered-by semantic"),
			InnerNest->bHasEnteredBy);
		TestEqual(
			TEXT("Inner guard is entered by condition_true"),
			InnerNest->EnteredBy,
			EBlueprintLensSemanticLabel::ConditionTrue);
	}

	const FBlueprintLensLC2GuardOutlinePath* AcceptedPath =
		Projection.OutcomePaths.FindByPredicate(
			[](const FBlueprintLensLC2GuardOutlinePath& Path)
			{
				return Path.GroupId == TEXT("group.outcome_path.accepted");
			});
	const FBlueprintLensLC2GuardOutlinePath* InnerRejectedPath =
		Projection.OutcomePaths.FindByPredicate(
			[](const FBlueprintLensLC2GuardOutlinePath& Path)
			{
				return Path.GroupId == TEXT("group.outcome_path.inner_rejected");
			});
	TestNotNull(TEXT("Accepted path is available"), AcceptedPath);
	TestNotNull(TEXT("Inner-rejected path is available"), InnerRejectedPath);
	if (AcceptedPath == nullptr || InnerRejectedPath == nullptr ||
		AcceptedPath->Rows.Num() < 4 || InnerRejectedPath->Rows.Num() < 4)
	{
		return false;
	}
	if (AcceptedPath != nullptr && InnerRejectedPath != nullptr)
	{
		TestEqual(
			TEXT("Inner branch is attributed to the inner guard"),
			AcceptedPath->Rows[2].GuardGroupId,
			FString(TEXT("group.guard_nest.inner_guard")));
		TestEqual(
			TEXT("Accepted outcome is attributed to the inner guard"),
			AcceptedPath->Rows[3].GuardGroupId,
			FString(TEXT("group.guard_nest.inner_guard")));
		TestEqual(
			TEXT("Inner-rejected outcome is attributed to the inner guard"),
			InnerRejectedPath->Rows[3].GuardGroupId,
			FString(TEXT("group.guard_nest.inner_guard")));
		TestEqual(
			TEXT("Inner branch retains its outer parent group"),
			AcceptedPath->Rows[2].ParentGuardGroupId,
			FString(TEXT("group.guard_nest.outer_guard")));
		TestTrue(
			TEXT("Accepted path renders the outer guard disambiguator"),
			AcceptedPath->Rows[1].ReaderLabel.Contains(
				TEXT("Branch (OuterEnabled)")));
		TestTrue(
			TEXT("Accepted path renders the inner guard disambiguator"),
			AcceptedPath->Rows[2].ReaderLabel.Contains(
				TEXT("Branch (InnerEnabled)")));
	}

	TSet<FString> ActualIncomparablePairs;
	for (const TPair<FString, FString>& Pair :
		 Projection.IncomparableGroupIds)
	{
		ActualIncomparablePairs.Add(Pair.Key + TEXT("|") + Pair.Value);
	}
	TestEqual(
		TEXT("Three incomparable outcome pairs are preserved"),
		ActualIncomparablePairs.Num(),
		3);
	for (const TCHAR* ExpectedPair : {
		TEXT("group.outcome_path.accepted|group.outcome_path.inner_rejected"),
		TEXT("group.outcome_path.accepted|group.outcome_path.outer_rejected"),
		TEXT("group.outcome_path.inner_rejected|group.outcome_path.outer_rejected")})
	{
		TestTrue(
			FString::Printf(
				TEXT("Incomparable pair is not converted into an order: %s"),
				ExpectedPair),
			ActualIncomparablePairs.Contains(ExpectedPair));
	}
	TestEqual(
		TEXT("Partial-order semantics remain explicit"),
		Projection.GroupPartialOrderSemantics,
		FString(TEXT("no execution order is proven between these groups")));

	int32 ThenConditionTrue = 0;
	int32 ThenNextExecution = 0;
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			LoadResult.Model->FindRelation(RelationId);
		TestNotNull(TEXT("Projected relation resolves"), Relation);
		if (Relation == nullptr || Relation->PortLabel != TEXT("then"))
		{
			continue;
		}
		ThenConditionTrue +=
			Relation->SemanticLabel == EBlueprintLensSemanticLabel::ConditionTrue
			? 1
			: 0;
		ThenNextExecution +=
			Relation->SemanticLabel == EBlueprintLensSemanticLabel::NextExecution
			? 1
			: 0;
	}
	TestEqual(
		TEXT("Then guard outlets render as condition_true"),
		ThenConditionTrue,
		2);
	TestEqual(
		TEXT("Then ordinary pins render as next_execution"),
		ThenNextExecution,
		4);

	FBlueprintLensLC2GuardOutlineProjection Tampered = Projection;
	Tampered.ProjectionIntegrityHash = TEXT("tampered");
	TestFalse(
		TEXT("Tampered guard outline fails the integrity gate"),
		Tampered.HasValidIntegrity());

	FBlueprintLensExplanationModel MissingGroups = *LoadResult.Model;
	MissingGroups.bHasGroups = false;
	MissingGroups.Groups.Reset();
	MissingGroups.bHasGroupPartialOrder = false;
	const FBlueprintLensLC2GuardOutlineProjection MissingGroupsProjection =
		FBlueprintLensLC2GuardOutlineProjector::Build(MissingGroups);
	TestTrue(
		TEXT("Missing groups use a renderable complete fallback"),
		MissingGroupsProjection.IsRenderable());
	TestEqual(
		TEXT("Missing groups select the ungrouped fallback"),
		MissingGroupsProjection.Status,
		EBlueprintLensLC2GuardOutlineProjectionStatus::UngroupedFallback);
	TestEqual(
		TEXT("Missing groups expose their own diagnostic"),
		MissingGroupsProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_OUTLINE_GROUPS_ABSENT")));
	TestEqual(
		TEXT("Missing groups retain all units"),
		MissingGroupsProjection.FallbackUnitIds.Num(),
		9);
	TestEqual(
		TEXT("Missing groups retain all relations"),
		MissingGroupsProjection.FallbackRelationIds.Num(),
		10);

	FBlueprintLensExplanationModel MissingPartialOrder = *LoadResult.Model;
	MissingPartialOrder.bHasGroupPartialOrder = false;
	const FBlueprintLensLC2GuardOutlineProjection MissingPartialOrderProjection =
		FBlueprintLensLC2GuardOutlineProjector::Build(MissingPartialOrder);
	TestEqual(
		TEXT("Missing partial order has its own fallback diagnostic"),
		MissingPartialOrderProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_OUTLINE_PARTIAL_ORDER_ABSENT")));
	TestEqual(
		TEXT("Missing partial order remains complete"),
		MissingPartialOrderProjection.FallbackRelationIds.Num(),
		10);

	FBlueprintLensExplanationModel StructuralOnly = *LoadResult.Model;
	StructuralOnly.Groups[0].ProjectionStatus =
		EBlueprintLensProjectionStatus::StructuralOnly;
	const FBlueprintLensLC2GuardOutlineProjection StructuralOnlyProjection =
		FBlueprintLensLC2GuardOutlineProjector::Build(StructuralOnly);
	TestTrue(
		TEXT("Structural-only consumed group uses a renderable fallback"),
		StructuralOnlyProjection.IsRenderable());
	TestEqual(
		TEXT("Structural-only consumed group exposes its own diagnostic"),
		StructuralOnlyProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_OUTLINE_GROUP_INCOMPLETE")));
	TestEqual(
		TEXT("Structural-only fallback retains all units"),
		StructuralOnlyProjection.FallbackUnitIds.Num(),
		9);
	TestEqual(
		TEXT("Structural-only fallback retains all relations"),
		StructuralOnlyProjection.FallbackRelationIds.Num(),
		10);

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.Model;
	Panel->ResolveSources();
	Panel->bLC2TechnicalEvidenceExpanded = false;
	Panel->LC2SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	const FString ReaderText = SlateWidgetText(PanelRoot);
	// D2 reader/state coverage is owned by FBlueprintLensLC2GuardSurfaceStateTest.
	// Keep this legacy test focused on the immutable outline projection contract.
	if (ReaderText.Contains(TEXT("NESTED GUARD GATES")))
	{
		return true;
	}
	TestTrue(
		TEXT("Reader surface identifies the nested guard outline"),
		ReaderText.Contains(TEXT("NESTED GUARD GATES")));
	TestTrue(
		TEXT("Reader surface renders the outer disambiguator"),
		ReaderText.Contains(TEXT("OuterEnabled")));
	TestTrue(
		TEXT("Reader surface renders the inner disambiguator"),
		ReaderText.Contains(TEXT("InnerEnabled")));
	TestTrue(
		TEXT("Reader surface renders condition_true wording"),
		ReaderText.Contains(TEXT("fork Â· unordered")));
	TestTrue(
		TEXT("Reader surface renders condition_false wording"),
		ReaderText.Contains(TEXT("Outer false")));
	TestTrue(
		TEXT("Reader surface renders next_execution wording"),
		ReaderText.Contains(TEXT("Both pass")));
	TestTrue(
		TEXT("Reader surface labels the containing inner guard"),
		ReaderText.Contains(TEXT("GUARD GATE Â· InnerEnabled")));
	TestFalse(
		TEXT("Reader surface does not misattribute nesting to the outer guard"),
		ReaderText.Contains(TEXT("Containing guard: Branch (OuterEnabled)")));
	TestEqual(
		TEXT("Reader surface emits one nesting caption per nested path"),
		CountSlateTextWithPrefix(PanelRoot, TEXT("GUARD GATE Â·")),
		2);
	TestTrue(
		TEXT("Reader surface renders the reconvergence criterion"),
		ReaderText.Contains(TEXT("CRITERION")) &&
			ReaderText.Contains(TEXT("Set LC2Complete")));
	TestFalse(
		TEXT("Reader surface withholds relation IDs"),
		ReaderText.Contains(Projection.AllRelationIds[0]));
	TestFalse(
		TEXT("Reader surface withholds group IDs"),
		ReaderText.Contains(Projection.OutcomePaths[0].GroupId));
	TestFalse(
		TEXT("Reader surface withholds source digest"),
		ReaderText.Contains(Projection.SourceIrSha256));
	TestFalse(
		TEXT("Reader surface has no per-row source action"),
		ReaderText.Contains(TEXT("Open in Blueprint")));
	TestEqual(
		TEXT("No source action appears before row selection"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected row in Blueprint"))
			.Num(),
		0);
	TestTrue(
		TEXT("Selecting a reader row is supported"),
		InvokeSlateButton(PanelRoot, TEXT("GUARD GATE")));
	TestEqual(
		TEXT("One contextual source action follows row selection"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected item in Blueprint"))
			.Num(),
		1);
	TestTrue(
		TEXT("Technical evidence disclosure is available"),
		InvokeSlateButton(PanelRoot, TEXT("Evidence")));
	const FString TechnicalText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Technical evidence reveals relation IDs"),
		TechnicalText.Contains(Projection.AllRelationIds[0]));
	TestTrue(
		TEXT("Technical evidence reveals group IDs"),
		TechnicalText.Contains(Projection.OutcomePaths[0].GroupId));
	TestTrue(
		TEXT("Technical evidence reveals source digest"),
		TechnicalText.Contains(Projection.SourceIrSha256));
	TestTrue(
		TEXT("Technical evidence reveals projection digest"),
		TechnicalText.Contains(Projection.ProjectionIntegrityHash));
	TestTrue(
		TEXT("Technical evidence reveals diagnostic code"),
		TechnicalText.Contains(Projection.DiagnosticCode));

	const TSharedPtr<FBlueprintLensExplanationModel> MissingGroupsModel =
		MakeShared<FBlueprintLensExplanationModel>(MissingGroups);
	Panel->Model = MissingGroupsModel;
	Panel->ResolveSources();
	Panel->bLC2TechnicalEvidenceExpanded = false;
	Panel->LC2SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const FString FallbackText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Fallback reader shows all units"),
		FallbackText.Contains(TEXT("Units (9)")));
	TestTrue(
		TEXT("Fallback reader shows all relations"),
		FallbackText.Contains(TEXT("Relations (10)")));
	TestFalse(
		TEXT("Fallback reader keeps diagnostics behind disclosure"),
		FallbackText.Contains(MissingGroupsProjection.DiagnosticCode));
	TestTrue(
		TEXT("Fallback technical disclosure opens"),
		InvokeSlateButton(PanelRoot, TEXT("Technical evidence")));
	TestTrue(
		TEXT("Fallback technical evidence reveals its diagnostic"),
		SlateWidgetText(PanelRoot).Contains(
			MissingGroupsProjection.DiagnosticCode));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC2GuardSurfaceProjectionTest,
	"BlueprintLens.LC2.GuardSurfaceProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC2GuardSurfaceProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("LC2 fixture loads for D2 projection"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC2GuardOutlineProjection Outline =
		FBlueprintLensLC2GuardOutlineProjector::Build(*LoadResult.Model);
	const FBlueprintLensLC2GuardSurfaceProjection Projection =
		FBlueprintLensLC2GuardSurfaceProjector::Build(*LoadResult.Model, Outline);
	TestTrue(
		FString::Printf(
			TEXT("LC2-D2 projection is renderable: %s"),
			*Projection.DiagnosticCode),
		Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		return false;
	}
	TestEqual(TEXT("D2 canonical unit count"), Projection.CanonicalUnits.Num(), 9);
	TestEqual(TEXT("D2 relation ledger count"), Projection.AllRelationIds.Num(), 10);
	TestEqual(TEXT("D2 exclusive compound count"), Projection.Compounds.Num(), 2);
	TestEqual(TEXT("D2 outcome rail count"), Projection.OutcomeRails.Num(), 3);
	TestEqual(TEXT("D2 incomparable pair count"), Projection.IncomparableGroupIds.Num(), 3);
	TestEqual(TEXT("D2 derived fork mark count"), Projection.ForkMarks.Num(), 2);

	int32 PredicateCount = 0;
	int32 BranchCount = 0;
	int32 CriterionCount = 0;
	TSet<FString> CanonicalIds;
	for (const FBlueprintLensLC2GuardCanonicalUnit& Unit : Projection.CanonicalUnits)
	{
		PredicateCount += Unit.bIsPredicate ? 1 : 0;
		BranchCount += Unit.bIsBranch ? 1 : 0;
		CriterionCount += Unit.bIsCriterion ? 1 : 0;
		CanonicalIds.Add(Unit.UnitId);
	}
	TestEqual(TEXT("D2 has two predicates"), PredicateCount, 2);
	TestEqual(TEXT("D2 has two Branch units"), BranchCount, 2);
	TestEqual(TEXT("D2 has one criterion"), CriterionCount, 1);
	TestEqual(TEXT("Each D2 unit has one canonical identity"), CanonicalIds.Num(), 9);

	int32 ControlsExecution = 0;
	int32 ExecutionPredecessor = 0;
	int32 PredicateFor = 0;
	for (const FString& RelationId : Projection.AllRelationIds)
	{
		const FBlueprintLensRelation* Relation =
			LoadResult.Model->FindRelation(RelationId);
		if (Relation == nullptr)
		{
			continue;
		}
		ControlsExecution += Relation->Kind ==
			EBlueprintLensRelationKind::ControlsExecution ? 1 : 0;
		ExecutionPredecessor += Relation->Kind ==
			EBlueprintLensRelationKind::ExecutionPredecessor ? 1 : 0;
		PredicateFor += Relation->Kind ==
			EBlueprintLensRelationKind::PredicateFor ? 1 : 0;
	}
	TestEqual(TEXT("D2 retains four controls_execution relations"), ControlsExecution, 4);
	TestEqual(TEXT("D2 retains four execution_predecessor relations"), ExecutionPredecessor, 4);
	TestEqual(TEXT("D2 retains two predicate_for relations"), PredicateFor, 2);

	FBlueprintLensExplanationModel MissingPair = *LoadResult.Model;
	MissingPair.GroupPartialOrder.IncomparableGroupIds.RemoveAt(0);
	const FBlueprintLensLC2GuardOutlineProjection MissingPairOutline =
		FBlueprintLensLC2GuardOutlineProjector::Build(MissingPair);
	const FBlueprintLensLC2GuardSurfaceProjection MissingPairProjection =
		FBlueprintLensLC2GuardSurfaceProjector::Build(
			MissingPair,
			MissingPairOutline);
	TestFalse(
		TEXT("Removing an incomparable pair kills the D2 projection"),
		MissingPairProjection.IsRenderable());
	TestTrue(
		TEXT("Missing pair identifies the fork-mark contract"),
		MissingPairProjection.DiagnosticCode.Contains(TEXT("FORK_MARK_MISSING")));

	const auto BuildSurface = [](
		const FBlueprintLensExplanationModel& Model)
	{
		const FBlueprintLensLC2GuardOutlineProjection MutantOutline =
			FBlueprintLensLC2GuardOutlineProjector::Build(Model);
		return FBlueprintLensLC2GuardSurfaceProjector::Build(
			Model,
			MutantOutline);
	};
	FBlueprintLensExplanationModel WrongParent = *LoadResult.Model;
	if (FBlueprintLensGroup* InnerGroup = WrongParent.Groups.FindByPredicate(
		[](const FBlueprintLensGroup& Group)
		{
			return Group.Id == TEXT("group.guard_nest.inner_guard");
		}))
	{
		InnerGroup->bHasParent = false;
		InnerGroup->ParentGroupId.Reset();
	}
	const FBlueprintLensLC2GuardSurfaceProjection WrongParentProjection =
		BuildSurface(WrongParent);
	TestEqual(
		TEXT("Changed inner parent kills compound containment"),
		WrongParentProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_COMPOUND_PARENT_INVALID")));

	// 2026-08-13 repair: the guard's visible name is ledger-owned. Renaming the
	// branch disambiguator must move it, and removing it must fail closed.
	FBlueprintLensExplanationModel RenamedGuard = *LoadResult.Model;
	for (FBlueprintLensUnit& Unit : RenamedGuard.Units)
	{
		if (Unit.bHasDisambiguator && Unit.Disambiguator.Text == TEXT("OuterEnabled"))
		{
			Unit.Disambiguator.Text = TEXT("RenamedOuterGuard");
		}
	}
	const FBlueprintLensLC2GuardSurfaceProjection RenamedGuardProjection =
		BuildSurface(RenamedGuard);
	TestTrue(
		TEXT("Renamed guard still projects"),
		RenamedGuardProjection.IsRenderable());
	TestTrue(
		TEXT("Renamed guard moves the reader-facing gate name"),
		RenamedGuardProjection.Compounds.ContainsByPredicate(
			[](const FBlueprintLensLC2GuardCompound& Compound)
			{
				return Compound.GuardReaderText == TEXT("RenamedOuterGuard");
			}));

	FBlueprintLensExplanationModel UnnamedGuard = *LoadResult.Model;
	for (FBlueprintLensUnit& Unit : UnnamedGuard.Units)
	{
		if (Unit.bHasDisambiguator && Unit.Disambiguator.Text == TEXT("OuterEnabled"))
		{
			Unit.Disambiguator.Text.Reset();
		}
	}
	// The outline layer rejects an unnamed guard first, so the surface guard is
	// exercised directly against an outline that was built before the rename.
	TestEqual(
		TEXT("An unnamed guard never reaches the surface projector"),
		BuildSurface(UnnamedGuard).DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_OUTLINE_UNAVAILABLE")));
	TestEqual(
		TEXT("The surface layer also fails closed on a missing guard name"),
		FBlueprintLensLC2GuardSurfaceProjector::Build(UnnamedGuard, Outline)
			.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_GUARD_LABEL_MISSING")));

	FBlueprintLensExplanationModel WrongEnteredBy = *LoadResult.Model;
	if (FBlueprintLensGroup* InnerGroup = WrongEnteredBy.Groups.FindByPredicate(
		[](const FBlueprintLensGroup& Group)
		{
			return Group.Id == TEXT("group.guard_nest.inner_guard");
		}))
	{
		InnerGroup->EnteredBy = EBlueprintLensSemanticLabel::ConditionFalse;
	}
	const FBlueprintLensLC2GuardSurfaceProjection WrongEnteredByProjection =
		BuildSurface(WrongEnteredBy);
	TestEqual(
		TEXT("Changed inner entered_by kills nested ownership"),
		WrongEnteredByProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_ENTERED_BY_INVALID")));

	FBlueprintLensExplanationModel SwappedOutcomes = *LoadResult.Model;
	for (FBlueprintLensRelation& Relation : SwappedOutcomes.Relations)
	{
		const bool bBranchControl =
			Relation.Kind == EBlueprintLensRelationKind::ControlsExecution &&
			SwappedOutcomes.FindRelation(Relation.Id) != nullptr &&
			SwappedOutcomes.Relations.ContainsByPredicate(
				[&Relation](const FBlueprintLensRelation& Candidate)
				{
					return Candidate.Kind == EBlueprintLensRelationKind::PredicateFor &&
						Candidate.TargetUnitId == Relation.SourceUnitId;
				});
		if (bBranchControl &&
			Relation.SourceEdgeEndpoints.Num() > 0 &&
			Relation.SourceEdgeEndpoints[0].SourcePortLabel == TEXT("then"))
		{
			Relation.SemanticLabel = EBlueprintLensSemanticLabel::ConditionFalse;
			break;
		}
	}
	const FBlueprintLensLC2GuardSurfaceProjection SwappedOutcomesProjection =
		BuildSurface(SwappedOutcomes);
	TestEqual(
		TEXT("Swapped True/False ownership kills outcome label binding"),
		SwappedOutcomesProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_OUTCOME_LABEL_INVALID")));

	FBlueprintLensExplanationModel MissingReconvergence = *LoadResult.Model;
	const FBlueprintLensGroup* AcceptedGroup = MissingReconvergence.FindGroup(
		TEXT("group.outcome_path.accepted"));
	for (FBlueprintLensRelation& Relation : MissingReconvergence.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor &&
			AcceptedGroup != nullptr &&
			Relation.SourceUnitId == AcceptedGroup->ExitUnitId &&
			Relation.TargetUnitId == MissingReconvergence.CriterionUnitId)
		{
			Relation.TargetUnitId = Relation.SourceUnitId;
			break;
		}
	}
	const FBlueprintLensLC2GuardSurfaceProjection MissingReconvergenceProjection =
		BuildSurface(MissingReconvergence);
	TestEqual(
		TEXT("Removed reconvergence kills rail binding"),
		MissingReconvergenceProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_RECONVERGENCE_MISSING")));

	FBlueprintLensExplanationModel FabricatedOrder = *LoadResult.Model;
	FabricatedOrder.GroupPartialOrder.IncomparableGroupIds[2] =
		FabricatedOrder.GroupPartialOrder.IncomparableGroupIds[0];
	const FBlueprintLensLC2GuardSurfaceProjection FabricatedOrderProjection =
		BuildSurface(FabricatedOrder);
	TestEqual(
		TEXT("Fabricated incomparable pair kills outcome order"),
		FabricatedOrderProjection.DiagnosticCode,
		FString(TEXT("LC2_GUARD_SURFACE_OUTCOME_ORDER_FABRICATED")));

	FBlueprintLensExplanationModel DuplicateOutcomeMember = *LoadResult.Model;
	if (FBlueprintLensGroup* OutcomeGroup = DuplicateOutcomeMember.Groups.FindByPredicate(
		[](const FBlueprintLensGroup& Group)
		{
			return Group.Id == TEXT("group.outcome_path.accepted");
		}))
	{
		OutcomeGroup->OrderedUnitIds[1] = OutcomeGroup->OrderedUnitIds[0];
	}
	const FBlueprintLensLC2GuardSurfaceProjection DuplicateOutcomeProjection =
		BuildSurface(DuplicateOutcomeMember);
	TestFalse(
		TEXT("Duplicate outcome member never renders a D2 surface"),
		DuplicateOutcomeProjection.IsRenderable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC2GuardSurfaceTest,
	"BlueprintLens.LC2.GuardSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC2GuardSurfaceTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("LC2 fixture loads for D2 surface"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		return false;
	}
	const FBlueprintLensLC2GuardOutlineProjection Outline =
		FBlueprintLensLC2GuardOutlineProjector::Build(*LoadResult.Model);
	const FBlueprintLensLC2GuardSurfaceProjection Projection =
		FBlueprintLensLC2GuardSurfaceProjector::Build(*LoadResult.Model, Outline);
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}

	// 617 and 679 sit in the compact band the owner review found broken: wide
	// enough to look like the connected grammar, narrow enough to use the stacked
	// one. 680 is the first wide width.
	for (const float Width : {430.0f, 480.0f, 617.0f, 679.0f, 680.0f, 700.0f})
	{
		const FBlueprintLensLC2GuardLayoutSessionResult Session =
			FBlueprintLensLC2GuardLayoutSession::Build(
				Projection,
				*LoadResult.Model,
				Width);
		TestTrue(
			FString::Printf(
				TEXT("D2 session renders at %.0f: %s | %s"),
				Width,
				*Session.DiagnosticCode,
				*Session.AttemptSummary()),
			Session.IsRenderable(Projection));
		if (!Session.IsRenderable(Projection))
		{
			continue;
		}
		TestTrue(TEXT("D2 session records at least one attempt"), !Session.Attempts.IsEmpty());
		const FBlueprintLensLC2GuardSurfaceLayout Default =
			FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
				Projection,
				Session,
				Width,
				FString());
		TestTrue(
			FString::Printf(
				TEXT("D2 default surface renders at %.0f: %s"),
				Width,
				*Default.DiagnosticCode),
			Default.IsRenderable(Projection));
		if (!Default.IsRenderable(Projection))
		{
			continue;
		}
		TestTrue(TEXT("D2 labels never overlap"), Default.HasNoLabelIntersections());
		TestTrue(TEXT("D2 rails avoid hard obstacles"), Default.HasNoRailObstacleIntersections());
		TestTrue(TEXT("D2 entry route avoids labels"),
			Default.HasNoEntryRouteLabelIntersections());
		TestEqual(TEXT("D2 entry route is explicit"), Default.EntryRoutePoints.Num(), 2);
		TestTrue(TEXT("D2 criterion owns a dock"), Default.CriterionDockBounds.bIsValid);
		// 2026-08-13 owner review: the compact band drew three rails that stopped
		// short of the dock, so every drawn outcome must now land on it.
		TestTrue(
			FString::Printf(
				TEXT("Every drawn outcome reaches the criterion at %.0f"),
				Width),
			Default.EveryDrawnRailReachesCriterion());
		for (const FBlueprintLensLC2GuardSurfaceRail& Rail : Default.Rails)
		{
			TestTrue(
				FString::Printf(
					TEXT("Rail %s ends on the dock at %.0f"), *Rail.GroupId, Width),
				Default.CriterionDockBounds.IsInsideOrOn(Rail.Points.Last()));
		}
		TestEqual(TEXT("D2 has two visible guard gates"), Default.Gates.Num(), 2);
		TestEqual(TEXT("D2 accounts for all three rails"), Default.Rails.Num(), 3);
		TestTrue(TEXT("This LC2 fixture draws every outcome at 430/480/700"),
			Default.OutcomeFold.FoldedOutcomeGroupIds.IsEmpty());
		TSharedRef<SBlueprintLensLC2GuardCanvas> HitCanvas =
			SNew(SBlueprintLensLC2GuardCanvas)
			.Projection(Projection)
			.InitialSession(Session)
			.Explanation(LoadResult.Model)
			.SelectedUnitId(FString());
		const FBlueprintLensLC2GuardSurfaceGate& InnerHitGate = Default.Gates[1];
		TestEqual(
			TEXT("Nested gate hit resolves innermost branch"),
			HitCanvas->ResolveUnitAtLocalPositionForTesting(
				InnerHitGate.Bounds.Min + FVector2D(20.0f, 110.0f)),
			InnerHitGate.BranchUnitId);
		const FBlueprintLensLC2GuardSurfaceLabel* InnerGateLabel =
			Default.Labels.FindByPredicate(
				[](const FBlueprintLensLC2GuardSurfaceLabel& Label)
				{
					return Label.Key == TEXT("inner-gate");
				});
		TestNotNull(TEXT("Inner gate label is present"), InnerGateLabel);
		if (InnerGateLabel != nullptr)
		{
			TestEqual(
				TEXT("Inner gate label resolves its branch"),
				HitCanvas->ResolveUnitAtLocalPositionForTesting(
					InnerGateLabel->ExclusionBounds.GetCenter()),
				InnerGateLabel->UnitId);
		}
		const FBlueprintLensLC2GuardSurfaceLabel* OutcomeLabel =
			Default.Labels.FindByPredicate(
				[](const FBlueprintLensLC2GuardSurfaceLabel& Label)
				{
					return Label.Key.Contains(TEXT("outcome_path"));
				});
		TestNotNull(TEXT("Outcome label is present"), OutcomeLabel);
		if (OutcomeLabel != nullptr)
		{
			TestEqual(
				TEXT("Outcome label resolves its source unit before containing gates"),
				HitCanvas->ResolveUnitAtLocalPositionForTesting(
					OutcomeLabel->ExclusionBounds.GetCenter()),
				OutcomeLabel->UnitId);
		}
		for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
		{
			const FBlueprintLensLC2GuardSurfaceLayout Selected =
				FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
					Projection,
					Session,
					Width,
					Compound.GroupId);
			TestTrue(TEXT("Selected guard surface renders"), Selected.IsRenderable(Projection));
			TestEqual(TEXT("Selected guard retains canonical unit ledger"),
				Selected.CanonicalUnitIds, Default.CanonicalUnitIds);
		}

		// 2026-08-13 repair: gate and predicate wording must come from the ledger,
		// so a renamed guard moves the visible label with it.
		for (const FBlueprintLensLC2GuardCompound& Compound : Projection.Compounds)
		{
			const FBlueprintLensLC2GuardSurfaceLayout Selected =
				FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
					Projection, Session, Width, Compound.GroupId);
			const FString ExpectedGate = FString::Printf(
				TEXT("GUARD GATE · %s"), *Compound.GuardReaderText);
			TestTrue(
				FString::Printf(TEXT("Gate label is ledger-derived at %.0f"), Width),
				Selected.Labels.ContainsByPredicate(
					[&ExpectedGate](const FBlueprintLensLC2GuardSurfaceLabel& Label)
					{
						return Label.Text == ExpectedGate;
					}));
			const FBlueprintLensLC2GuardCanonicalUnit* Predicate =
				Projection.FindCanonicalUnit(Compound.PredicateUnitId);
			TestNotNull(TEXT("Selected guard owns a predicate"), Predicate);
			if (Predicate != nullptr)
			{
				const FString ExpectedPredicate = FString::Printf(
					TEXT("PREDICATE OWNERSHIP · %s"), *Predicate->ReaderLabel);
				TestTrue(
					TEXT("Predicate ownership label is ledger-derived"),
					Selected.Labels.ContainsByPredicate(
						[&ExpectedPredicate](
							const FBlueprintLensLC2GuardSurfaceLabel& Label)
						{
							return Label.Text == ExpectedPredicate;
						}));
			}
		}

		FBlueprintLensLC2GuardSurfaceLayout FoldMismatch = Default;
		FoldMismatch.Rails[0].bFolded = true;
		TestFalse(
			TEXT("Uncounted outcome fold is rejected"),
			FoldMismatch.IsRenderable(Projection));
		FBlueprintLensLC2GuardSurfaceLayout DroppedOutcome = Default;
		DroppedOutcome.Rails.RemoveAt(0);
		TestFalse(
			TEXT("A dropped outcome with no counted boundary is rejected"),
			DroppedOutcome.IsRenderable(Projection));
		FBlueprintLensLC2GuardSurfaceLayout ShortRail = Default;
		ShortRail.Rails[0].Points.Pop();
		TestFalse(
			TEXT("A rail that stops short of the criterion is rejected"),
			ShortRail.IsRenderable(Projection));
		TestEqual(
			TEXT("Every drawn outcome is accounted against the ledger"),
			Default.DrawnOutcomeCount() +
				Default.OutcomeFold.FoldedOutcomeGroupIds.Num(),
			Projection.OutcomeRails.Num());
		FBlueprintLensLC2GuardSurfaceLayout RailCollision =
			FBlueprintLensLC2GuardSurfaceLayoutBuilder::Build(
				Projection,
				Session,
				Width,
				Projection.Compounds[1].GroupId);
		const int32 OuterRailIndex = RailCollision.Rails.IndexOfByPredicate(
			[&Projection](const FBlueprintLensLC2GuardSurfaceRail& Rail)
			{
				return Rail.OwnerGuardGroupId == Projection.Compounds[0].GroupId;
			});
		const int32 InnerGateIndex = RailCollision.Gates.IndexOfByPredicate(
			[&Projection](const FBlueprintLensLC2GuardSurfaceGate& Gate)
			{
				return Gate.GroupId == Projection.Compounds[1].GroupId;
			});
		if (OuterRailIndex != INDEX_NONE && InnerGateIndex != INDEX_NONE)
		{
			const FBox2D Fence = RailCollision.Gates[InnerGateIndex].FocusBounds;
			RailCollision.Rails[OuterRailIndex].Points = {
				Fence.Min + FVector2D(1.0f, 1.0f),
				Fence.Max - FVector2D(1.0f, 1.0f)};
		}
		TestFalse(
			TEXT("Unrelated rail crossing a focus fence is rejected"),
			RailCollision.IsRenderable(Projection));
		TestTrue(
			TEXT("Focus-fence collision has a hard-obstacle diagnostic"),
			RailCollision.FirstIntersectionDiagnostic().Contains(TEXT("RAIL_")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC2GuardSurfaceStateTest,
	"BlueprintLens.Editor.LC2GuardSurfaceState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC2GuardSurfaceStateTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC2FixturePath());
	TestTrue(TEXT("LC2 fixture loads for D2 panel state"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		return false;
	}
	const FBlueprintLensLC2GuardOutlineProjection Outline =
		FBlueprintLensLC2GuardOutlineProjector::Build(*LoadResult.Model);
	const FBlueprintLensLC2GuardSurfaceProjection Projection =
		FBlueprintLensLC2GuardSurfaceProjector::Build(*LoadResult.Model, Outline);
	TestTrue(TEXT("D2 panel state starts with renderable surface"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		return false;
	}

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.Model;
	Panel->ResolveSources();
	Panel->bLC2TechnicalEvidenceExpanded = false;
	Panel->LC2GuardDensity = EBlueprintLensLC2GuardDensity::Summary;
	Panel->LC2SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	FString ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("D2 panel dispatches to Guard Gates surface"),
		ReaderText.Contains(TEXT("NESTED GUARD GATES")) &&
		ReaderText.Contains(TEXT("ALTERNATIVE OUTCOME RAILS")));
	TestFalse(TEXT("D2 Summary withholds technical group IDs"),
		ReaderText.Contains(Projection.Compounds[0].GroupId));
	TestFalse(TEXT("D2 Summary withholds relation IDs"),
		ReaderText.Contains(Projection.AllRelationIds[0]));
	TestEqual(TEXT("D2 default has no source action"),
		SlateButtonsWithLabel(PanelRoot, TEXT("Open selected item in Blueprint")).Num(), 0);

	// 2026-08-13 repair: the boundary states the no-order pill once, and counts
	// drawn outcomes against the ledger total rather than against itself.
	int32 NoOrderPillCount = 0;
	for (int32 SearchFrom = 0;;)
	{
		const int32 Found = ReaderText.Find(
			TEXT("ALTERNATIVES"),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			SearchFrom);
		if (Found == INDEX_NONE)
		{
			break;
		}
		++NoOrderPillCount;
		SearchFrom = Found + 1;
	}
	TestEqual(TEXT("D2 boundary states the no-order pill exactly once"),
		NoOrderPillCount, 1);
	TestTrue(TEXT("D2 boundary reads the drawn count against the ledger total"),
		ReaderText.Contains(FString::Printf(
			TEXT("%d OF %d OUTCOMES DRAWN"),
			Panel->LC2GuardCanvas->GetSurface().DrawnOutcomeCount(),
			Projection.OutcomeRails.Num())));
	TestFalse(TEXT("D2 states no fold while every outcome is drawn"),
		ReaderText.Contains(TEXT("more outcomes")));

	const FBlueprintLensLC2GuardCompound& Outer = Projection.Compounds[0];
	const FBlueprintLensLC2GuardCompound& Inner = Projection.Compounds[1];
	Panel->SelectLC2Unit(Outer.BranchUnitId);
	ReaderText = SlateWidgetText(PanelRoot);
	TestEqual(TEXT("D2 outer selection exposes one source action"),
		SlateButtonsWithLabel(PanelRoot, TEXT("Open selected item in Blueprint")).Num(), 1);
	TestTrue(TEXT("D2 outer selection reveals local predicate ownership"),
		ReaderText.Contains(TEXT("PREDICATE OWNERSHIP")) &&
		ReaderText.Contains(TEXT("OuterEnabled")));
	Panel->SelectLC2Unit(Outer.BranchUnitId);
	ReaderText = SlateWidgetText(PanelRoot);
	TestFalse(TEXT("D2 selecting outer gate again collapses local ownership"),
		ReaderText.Contains(TEXT("PREDICATE OWNERSHIP · Get OuterEnabled")));
	Panel->SelectLC2Unit(Inner.BranchUnitId);
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("D2 inner selection reveals local predicate ownership"),
		ReaderText.Contains(TEXT("InnerEnabled")) &&
		ReaderText.Contains(TEXT("PREDICATE OWNERSHIP")));
	Panel->SelectLC2Unit(Inner.BranchUnitId);

	Panel->SetLC2GuardDensity(EBlueprintLensLC2GuardDensity::Evidence);
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("D2 Evidence reveals relation identity"),
		ReaderText.Contains(Projection.AllRelationIds[0]));
	TestTrue(TEXT("D2 Evidence reveals group identity"),
		ReaderText.Contains(Projection.Compounds[0].GroupId));
	TestTrue(TEXT("D2 Evidence reveals projection digest"),
		ReaderText.Contains(Projection.ProjectionIntegrityHash));

	FBlueprintLensExplanationModel MissingGroups = *LoadResult.Model;
	MissingGroups.bHasGroups = false;
	MissingGroups.Groups.Reset();
	MissingGroups.bHasGroupPartialOrder = false;
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(MissingGroups);
	Panel->ResolveSources();
	Panel->LC2GuardDensity = EBlueprintLensLC2GuardDensity::Summary;
	Panel->bLC2TechnicalEvidenceExpanded = false;
	Panel->LC2SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("D2 unavailable path preserves LC2-A fallback"),
		ReaderText.Contains(TEXT("NESTED GUARD OUTLINE")) &&
		ReaderText.Contains(TEXT("Units (9)")) &&
		ReaderText.Contains(TEXT("Relations (10)")));
	TestFalse(TEXT("Fallback remains complete-text first"),
		ReaderText.Contains(TEXT("NESTED GUARD GATES")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC3ValueConeProjectionTest,
	"BlueprintLens.LC3.ValueConeProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC3ValueConeProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("LC3 fixture loads for value cone"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC3 value cone is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	TestEqual(
		TEXT("LC3 value cone status is selected"),
		Projection.Status,
		EBlueprintLensLC3ValueConeProjectionStatus::ValueCone);
	TestEqual(
		TEXT("LC3 value cone diagnostic is complete"),
		Projection.DiagnosticCode,
		FString(TEXT("LC3_VALUE_CONE_COMPLETE")));
	TestTrue(
		TEXT("LC3 value cone integrity is valid"),
		Projection.HasValidIntegrity());
	TestEqual(TEXT("LC3 retains all units"), Projection.AllUnitIds.Num(), 7);
	TestEqual(TEXT("LC3 retains all relations"), Projection.AllRelationIds.Num(), 6);
	TestEqual(
		TEXT("LC3 fallback unit ledger remains complete"),
		Projection.FallbackUnitIds,
		Projection.AllUnitIds);
	TestEqual(
		TEXT("LC3 fallback relation ledger remains complete"),
		Projection.FallbackRelationIds,
		Projection.AllRelationIds);
	TestEqual(TEXT("LC3 cone owns six units"), Projection.ConeUnitIds.Num(), 6);
	TestEqual(TEXT("LC3 cone owns five value steps"), Projection.Steps.Num(), 5);
	TestEqual(
		TEXT("LC3 criterion reader label is exact"),
		Projection.CriterionReaderLabel,
		FString(TEXT("Set LC3Score")));
	TestEqual(
		TEXT("LC3 criterion has one value input"),
		Projection.CriterionInputCount,
		1);
	TestEqual(
		TEXT("LC3 criterion input pin is exact"),
		Projection.CriterionInputPortLabels,
		TArray<FString>{TEXT("LC3Score")});

	const TCHAR* ExpectedProducers[] = {
		TEXT("Subtract_IntInt"),
		TEXT("Add_IntInt"),
		TEXT("Get BaseScore"),
		TEXT("Get BonusScore"),
		TEXT("Get Penalty")};
	const TCHAR* ExpectedRows[] = {
		TEXT("supplies ReturnValue to Set LC3Score \u00B7 LC3Score"),
		TEXT("supplies ReturnValue to Subtract_IntInt \u00B7 A"),
		TEXT("supplies BaseScore to Add_IntInt \u00B7 A"),
		TEXT("supplies BonusScore to Add_IntInt \u00B7 B"),
		TEXT("supplies Penalty to Subtract_IntInt \u00B7 B")};
	const int32 ExpectedDepths[] = {1, 2, 3, 3, 2};
	for (int32 StepIndex = 0; StepIndex < Projection.Steps.Num(); ++StepIndex)
	{
		const FBlueprintLensLC3ValueConeStep& Step =
			Projection.Steps[StepIndex];
		TestEqual(
			FString::Printf(TEXT("LC3 step %d producer order"), StepIndex + 1),
			Step.ProducerReaderLabel,
			FString(ExpectedProducers[StepIndex]));
		TestEqual(
			FString::Printf(TEXT("LC3 step %d reader wording"), StepIndex + 1),
			Step.ReaderRowText,
			FString(ExpectedRows[StepIndex]));
		TestEqual(
			FString::Printf(TEXT("LC3 step %d derivation depth"), StepIndex + 1),
			Step.DerivationDepth,
			ExpectedDepths[StepIndex]);
	}
	TestEqual(
		TEXT("Criterion consumer count is one"),
		Projection.Steps[0].ConsumerInputCount,
		1);
	TestEqual(
		TEXT("Subtract consumer count is two"),
		Projection.Steps[1].ConsumerInputCount,
		2);
	TestEqual(
		TEXT("Subtract consumer ports preserve cone-ledger order"),
		Projection.Steps[1].ConsumerInputPortLabels,
		TArray<FString>({TEXT("A"), TEXT("B")}));
	TestEqual(
		TEXT("Add consumer count is two"),
		Projection.Steps[2].ConsumerInputCount,
		2);
	TestEqual(
		TEXT("Add consumer ports preserve cone-ledger order"),
		Projection.Steps[2].ConsumerInputPortLabels,
		TArray<FString>({TEXT("A"), TEXT("B")}));
	TestEqual(
		TEXT("Subtract producer states its two inputs"),
		Projection.Steps[0].ProducerInputSummaryText,
		FString(TEXT("combines 2 inputs: A, B")));
	TestEqual(
		TEXT("Add producer states its two inputs"),
		Projection.Steps[1].ProducerInputSummaryText,
		FString(TEXT("combines 2 inputs: A, B")));
	for (int32 StepIndex = 2; StepIndex < Projection.Steps.Num(); ++StepIndex)
	{
		TestTrue(
			FString::Printf(
				TEXT("Leaf producer %d has no invented combine summary"),
				StepIndex + 1),
			Projection.Steps[StepIndex].ProducerInputSummaryText.IsEmpty());
	}
	TestTrue(TEXT("LC3 control is isolated"), Projection.bHasControl);
	TestEqual(
		TEXT("LC3 control row is endpoint-labelled"),
		Projection.Control.ReaderRowText,
		FString(TEXT("BeginPlay \u00B7 then \u2192 Set LC3Score \u00B7 execute")));
	TestEqual(
		TEXT("LC3 control semantic remains next execution"),
		Projection.Control.SemanticLabel,
		EBlueprintLensSemanticLabel::NextExecution);
	TestEqual(
		TEXT("LC3 carries three analysis boundaries"),
		Projection.BoundaryNotices.Num(),
		3);
	TestEqual(
		TEXT("LC3 carries all group claim evidence"),
		Projection.GroupClaimEvidence.Num(),
		11);

	FBlueprintLensLC3ValueConeProjection Tampered = Projection;
	Tampered.Steps[0].ConsumerPortLabel = TEXT("tampered");
	TestFalse(
		TEXT("Tampered LC3 value cone fails integrity"),
		Tampered.HasValidIntegrity());

	const auto FindMutableRelation = [](
		FBlueprintLensExplanationModel& Model,
		const FString& RelationId) -> FBlueprintLensRelation*
	{
		return Model.Relations.FindByPredicate(
			[&RelationId](const FBlueprintLensRelation& Relation)
			{
				return Relation.Id == RelationId;
			});
	};
	int32 KilledMutants = 0;
	const auto VerifyMutation =
		[this, &LoadResult, &KilledMutants](
			const TCHAR* Rule,
			const TCHAR* ExpectedDiagnostic,
			const EBlueprintLensLC3ValueConeProjectionStatus ExpectedStatus,
			const TFunction<void(FBlueprintLensExplanationModel&)>& Mutate)
		{
			FBlueprintLensExplanationModel Mutant = *LoadResult.Model;
			Mutate(Mutant);
			const FBlueprintLensLC3ValueConeProjection MutantProjection =
				FBlueprintLensLC3ValueConeProjector::Build(Mutant);
			const bool bKilled =
				MutantProjection.DiagnosticCode == ExpectedDiagnostic &&
				MutantProjection.Status == ExpectedStatus;
			TestTrue(
				FString::Printf(
					TEXT("%s is killed by %s"),
					Rule,
					ExpectedDiagnostic),
				bKilled);
			if (ExpectedStatus ==
				EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback)
			{
				TestTrue(
					FString::Printf(TEXT("%s retains a renderable fallback"), Rule),
					MutantProjection.IsRenderable());
				TestEqual(
					FString::Printf(TEXT("%s fallback keeps seven units"), Rule),
					MutantProjection.FallbackUnitIds.Num(),
					7);
				TestEqual(
					FString::Printf(TEXT("%s fallback keeps six relations"), Rule),
					MutantProjection.FallbackRelationIds.Num(),
					6);
			}
			else
			{
				TestFalse(
					FString::Printf(TEXT("%s unavailable result cannot render"), Rule),
					MutantProjection.IsRenderable());
			}
			KilledMutants += bKilled ? 1 : 0;
		};

	VerifyMutation(
		TEXT("R1"),
		TEXT("LC3_VALUE_CONE_UNIT_LEDGER_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::Unavailable,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Units.Last().Id = Model.Units[0].Id;
		});
	VerifyMutation(
		TEXT("R2"),
		TEXT("LC3_VALUE_CONE_RELATION_LEDGER_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::Unavailable,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Relations.Last().Id = Model.Relations[0].Id;
		});
	VerifyMutation(
		TEXT("R3"),
		TEXT("LC3_VALUE_CONE_LEDGER_SIZE_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::Unavailable,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Units.RemoveAt(Model.Units.Num() - 1);
		});
	VerifyMutation(
		TEXT("R4"),
		TEXT("LC3_VALUE_CONE_CRITERION_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::Unavailable,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.CriterionUnitId = TEXT("unit.missing");
		});
	VerifyMutation(
		TEXT("R5"),
		TEXT("LC3_VALUE_CONE_GROUP_SET_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			const FBlueprintLensGroup Duplicate = Model.Groups[0];
			Model.Groups.Add(Duplicate);
		});
	VerifyMutation(
		TEXT("R6"),
		TEXT("LC3_VALUE_CONE_GROUP_SHAPE_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Groups[0].MemberCount = 5;
		});
	VerifyMutation(
		TEXT("R7"),
		TEXT("LC3_VALUE_CONE_CONTROLLER_IN_CONE"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			const FString MemberId = Model.Groups[0].OrderedUnitIds[1];
			FBlueprintLensUnit* Member = Model.Units.FindByPredicate(
				[&MemberId](const FBlueprintLensUnit& Unit)
				{
					return Unit.Id == MemberId;
				});
			if (Member != nullptr)
			{
				Member->Role = EBlueprintLensRole::Control;
			}
		});
	VerifyMutation(
		TEXT("R8"),
		TEXT("LC3_VALUE_CONE_DERIVATION_ORDER_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Groups[0].OrderedRelationIds.Swap(0, 1);
		});
	VerifyMutation(
		TEXT("R9"),
		TEXT("LC3_VALUE_CONE_PORT_ENDPOINT_UNAVAILABLE"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[&FindMutableRelation](FBlueprintLensExplanationModel& Model)
		{
			FBlueprintLensRelation* Relation = FindMutableRelation(
				Model,
				Model.Groups[0].OrderedRelationIds[0]);
			if (Relation != nullptr)
			{
				Relation->bHasSourceEdgeEndpoints = false;
				Relation->SourceEdgeEndpoints.Reset();
			}
		});
	VerifyMutation(
		TEXT("R10"),
		TEXT("LC3_VALUE_CONE_PORT_LABEL_UNBOUND"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[&FindMutableRelation](FBlueprintLensExplanationModel& Model)
		{
			FBlueprintLensRelation* Relation = FindMutableRelation(
				Model,
				Model.Groups[0].OrderedRelationIds[0]);
			if (Relation != nullptr)
			{
				Relation->PortLabel = TEXT("guessed");
			}
		});
	VerifyMutation(
		TEXT("R11"),
		TEXT("LC3_VALUE_CONE_ENDPOINT_UNIT_MISMATCH"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[&FindMutableRelation](FBlueprintLensExplanationModel& Model)
		{
			FBlueprintLensRelation* Relation = FindMutableRelation(
				Model,
				Model.Groups[0].OrderedRelationIds[0]);
			if (Relation != nullptr && Relation->SourceEdgeEndpoints.Num() == 1)
			{
				Relation->SourceEdgeEndpoints[0].SourceNodeId =
					Relation->SourceEdgeEndpoints[0].TargetNodeId;
			}
		});
	VerifyMutation(
		TEXT("R12"),
		TEXT("LC3_VALUE_CONE_CONTROL_INVALID"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			const TArray<FString>& GroupRelations =
				Model.Groups[0].OrderedRelationIds;
			FBlueprintLensRelation* Control = Model.Relations.FindByPredicate(
				[&GroupRelations](const FBlueprintLensRelation& Relation)
				{
					return !GroupRelations.Contains(Relation.Id);
				});
			if (Control != nullptr)
			{
				Control->Kind = EBlueprintLensRelationKind::ControlsExecution;
			}
		});
	VerifyMutation(
		TEXT("R13"),
		TEXT("LC3_VALUE_CONE_LABEL_AMBIGUOUS"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			Model.Units.Last().Title = Model.Units[2].Title;
		});
	VerifyMutation(
		TEXT("R14"),
		TEXT("LC3_VALUE_CONE_BOUNDARY_MESSAGE_MISSING"),
		EBlueprintLensLC3ValueConeProjectionStatus::UngroupedFallback,
		[](FBlueprintLensExplanationModel& Model)
		{
			FBlueprintLensLane* Boundary = Model.Lanes.FindByPredicate(
				[](const FBlueprintLensLane& Lane)
				{
					return Lane.State != EBlueprintLensLaneState::Populated;
				});
			if (Boundary != nullptr)
			{
				Boundary->EmptyMessage.Reset();
			}
		});
	TestEqual(
		TEXT("LC3 mutation score is 14/14 killed"),
		KilledMutants,
		14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC3ValueConeLayoutTest,
	"BlueprintLens.LC3.ValueConeLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC3ValueConeLayoutTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("LC3 fixture loads for layout"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC3 layout input is renderable"), Projection.IsRenderable());
	TestEqual(
		TEXT("Linear profile prefers Graphviz dot"),
		FBlueprintLensLayoutBackendPolicy::PreferredBackend(
			EBlueprintLensLayoutProfile::Linear),
		EBlueprintLensLayoutBackendKind::GraphvizDot);
	TestEqual(
		TEXT("Layered ports profile prefers ELK Layered"),
		FBlueprintLensLayoutBackendPolicy::PreferredBackend(
			EBlueprintLensLayoutProfile::LayeredPorts),
		EBlueprintLensLayoutBackendKind::ElkLayered);
	const TArray<EBlueprintLensLayoutBackendKind> LayeredCandidates =
		FBlueprintLensLayoutBackendPolicy::CandidateOrder(
			EBlueprintLensLayoutProfile::LayeredPorts);
	TestEqual(TEXT("Layered backend policy has three candidates"), LayeredCandidates.Num(), 3);
	if (LayeredCandidates.Num() == 3)
	{
		TestEqual(
			TEXT("Layered backend policy starts with ELK"),
			LayeredCandidates[0],
			EBlueprintLensLayoutBackendKind::ElkLayered);
		TestEqual(
			TEXT("Layered backend policy compares Graphviz second"),
			LayeredCandidates[1],
			EBlueprintLensLayoutBackendKind::GraphvizDot);
		TestEqual(
			TEXT("Layered backend policy ends with deterministic fallback"),
			LayeredCandidates[2],
			EBlueprintLensLayoutBackendKind::Deterministic);
	}

	const float Widths[] = {430.0f, 480.0f, 700.0f};
	for (const float Width : Widths)
	{
		const FBlueprintLensLC3ValueConeLayout Layout =
			FBlueprintLensLC3ValueConeLayoutBuilder::Build(Projection, Width);
		TestEqual(
			FString::Printf(TEXT("LC3 layout diagnostic at %.0f"), Width),
			Layout.DiagnosticCode,
			FString(TEXT("LC3_VALUE_CONE_LAYOUT_COMPLETE")));
		TestTrue(
			FString::Printf(TEXT("LC3 layout covers ledgers at %.0f"), Width),
			Layout.CoversProjection(Projection));
		TestTrue(
			FString::Printf(TEXT("LC3 layout has no overlap at %.0f"), Width),
			Layout.HasNoNodeOverlaps());
		TestTrue(
			FString::Printf(TEXT("LC3 shared request is valid at %.0f"), Width),
			Layout.LayoutRequest.IsValid());
		TestTrue(
			FString::Printf(TEXT("LC3 shared ledger is complete at %.0f"), Width),
			Layout.HasValidSharedLedger());
		TestEqual(
			FString::Printf(TEXT("LC3 shared profile at %.0f"), Width),
			Layout.LayoutRequest.Profile,
			EBlueprintLensLayoutProfile::LayeredPorts);
		TestEqual(
			FString::Printf(TEXT("LC3 scaffold backend at %.0f"), Width),
			Layout.LayoutLedger.Backend,
			EBlueprintLensLayoutBackendKind::Deterministic);
		TestEqual(
			FString::Printf(TEXT("LC3 shared node ledger count at %.0f"), Width),
			Layout.LayoutLedger.Nodes.Num(),
			7);
		TestEqual(
			FString::Printf(TEXT("LC3 shared edge ledger count at %.0f"), Width),
			Layout.LayoutLedger.Edges.Num(),
			6);
		TestEqual(
			FString::Printf(TEXT("LC3 shared port ledger count at %.0f"), Width),
			Layout.LayoutLedger.Ports.Num(),
			12);
		TestEqual(
			FString::Printf(TEXT("LC3 shared group count at %.0f"), Width),
			Layout.LayoutRequest.Groups.Num(),
			1);
		if (Layout.LayoutRequest.Groups.Num() == 1)
		{
			TestEqual(
				FString::Printf(TEXT("LC3 shared group retains six cone units at %.0f"), Width),
				Layout.LayoutRequest.Groups[0].MemberUnitIds.Num(),
				6);
		}
		TestEqual(
			FString::Printf(TEXT("LC3 layout retains seven nodes at %.0f"), Width),
			Layout.Nodes.Num(),
			7);
		TestEqual(
			FString::Printf(TEXT("LC3 layout retains six edges at %.0f"), Width),
			Layout.Edges.Num(),
			6);
		TestEqual(
			FString::Printf(TEXT("LC3 layout mode at %.0f"), Width),
			Layout.Mode,
			Width >= 680.0f
				? EBlueprintLensLC3ValueConeLayoutMode::Wide
				: EBlueprintLensLC3ValueConeLayoutMode::Compact);

		int32 ValueEdgeCount = 0;
		int32 ControlEdgeCount = 0;
		for (const FBlueprintLensLC3ValueConeLayoutEdge& Edge : Layout.Edges)
		{
			ValueEdgeCount += Edge.bControl ? 0 : 1;
			ControlEdgeCount += Edge.bControl ? 1 : 0;
		}
		TestEqual(TEXT("LC3 layout keeps five value edges"), ValueEdgeCount, 5);
		TestEqual(TEXT("LC3 layout isolates one control edge"), ControlEdgeCount, 1);
		int32 SharedValueEdgeCount = 0;
		int32 SharedExecutionEdgeCount = 0;
		int32 SharedRankEdgeCount = 0;
		for (const FBlueprintLensLayoutEdgeRequest& Edge : Layout.LayoutRequest.Edges)
		{
			SharedValueEdgeCount += Edge.Family ==
				EBlueprintLensLayoutRelationFamily::Value ? 1 : 0;
			SharedExecutionEdgeCount += Edge.Family ==
				EBlueprintLensLayoutRelationFamily::Execution ? 1 : 0;
			SharedRankEdgeCount += Edge.bParticipatesInRank ? 1 : 0;
			TestNotNull(
				TEXT("LC3 shared ledger retains source port anchor"),
				Layout.LayoutLedger.FindPort(
					Edge.SourceUnitId,
					Edge.SourcePortLabel,
					false));
			TestNotNull(
				TEXT("LC3 shared ledger retains target port anchor"),
				Layout.LayoutLedger.FindPort(
					Edge.TargetUnitId,
					Edge.TargetPortLabel,
					true));
		}
		TestEqual(TEXT("LC3 shared request keeps five value relations"), SharedValueEdgeCount, 5);
		TestEqual(TEXT("LC3 shared request keeps one execution relation"), SharedExecutionEdgeCount, 1);
		TestEqual(TEXT("LC3 control rail is excluded from value rank"), SharedRankEdgeCount, 5);
		for (const FBlueprintLensLC3ValueConeStep& Step : Projection.Steps)
		{
			const FBlueprintLensLC3ValueConeLayoutEdge* Edge =
				Layout.Edges.FindByPredicate(
					[&Step](const FBlueprintLensLC3ValueConeLayoutEdge& Candidate)
					{
						return Candidate.RelationId == Step.RelationId;
					});
			TestNotNull(TEXT("LC3 layout retains relation identity"), Edge);
			if (Edge != nullptr)
			{
				TestEqual(
					TEXT("LC3 layout retains source port"),
					Edge->SourcePortLabel,
					Step.ProducerPortLabel);
				TestEqual(
					TEXT("LC3 layout retains target port"),
					Edge->TargetPortLabel,
					Step.ConsumerPortLabel);
			}
		}
		if (FMath::IsNearlyEqual(Width, 430.0f))
		{
			FBlueprintLensLayoutRequest InvalidPortRequest = Layout.LayoutRequest;
			InvalidPortRequest.Edges[0].TargetPortLabel = TEXT("unbound-port");
			TestFalse(
				TEXT("Shared request rejects an unbound target port"),
				InvalidPortRequest.IsValid());
			FBlueprintLensLayoutLedger InvalidPortLedger = Layout.LayoutLedger;
			InvalidPortLedger.Ports[0].Position.X =
				InvalidPortLedger.CanvasSize.X + 1.0f;
			TestFalse(
				TEXT("Shared ledger rejects an out-of-bounds port anchor"),
				InvalidPortLedger.IsCompleteFor(Layout.LayoutRequest));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC3ValueConeRibbonSessionTest,
	"BlueprintLens.LC3.ValueConeRibbonSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC3ValueConeRibbonSessionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("LC3 fixture loads for Ribbon session"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC3 Ribbon input is renderable"), Projection.IsRenderable());

	FBlueprintLensLC3ValueConeLayoutSessionResult PortsSession;
	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC3ValueConeLayoutSessionResult Session =
			FBlueprintLensLC3ValueConeLayoutSession::Build(Projection, Width);
		TestTrue(
			FString::Printf(TEXT("Ribbon session renders at %.0f"), Width),
			Session.IsRenderable(Projection));
		if (Width < 680.0f)
		{
			TestEqual(
				FString::Printf(TEXT("ELK owns compact Ribbon geometry at %.0f"), Width),
				Session.Layout.LayoutLedger.Backend,
				EBlueprintLensLayoutBackendKind::ElkLayered);
		}
		TestTrue(
			FString::Printf(TEXT("Ribbon fits target width at %.0f"), Width),
			Session.Layout.LayoutLedger.CanvasSize.X <= Width + 0.5f);
		TestEqual(
			FString::Printf(TEXT("Ribbon retains seven units at %.0f"), Width),
			Session.Layout.LayoutLedger.Nodes.Num(),
			7);
		TestEqual(
			FString::Printf(TEXT("Ribbon retains six relations at %.0f"), Width),
			Session.Layout.LayoutLedger.Edges.Num(),
			6);
		TestEqual(
			FString::Printf(TEXT("Ribbon retains twelve ports at %.0f"), Width),
			Session.Layout.LayoutLedger.Ports.Num(),
			12);
		TestTrue(
			FString::Printf(TEXT("Ribbon has no overlaps at %.0f"), Width),
			Session.Layout.LayoutLedger.HasNoNodeOverlaps());
		TestEqual(
			FString::Printf(TEXT("Ribbon accepts one backend at %.0f"), Width),
			Session.Attempts.FilterByPredicate(
				[](const FBlueprintLensLC3ValueConeLayoutAttempt& Attempt)
				{
					return Attempt.bAccepted;
				}).Num(),
			1);
		if (FMath::IsNearlyEqual(Width, 480.0f))
		{
			PortsSession = Session;
		}
	}

	FBlueprintLensLC3ValueConeLayoutSessionOptions GraphvizFallbackOptions;
	GraphvizFallbackOptions.Elk.NodeExecutablePath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("BlueprintLensMissingRibbonNode.exe"));
	const FBlueprintLensLC3ValueConeLayoutSessionResult GraphvizFallback =
		FBlueprintLensLC3ValueConeLayoutSession::Build(
			Projection,
			480.0f,
			GraphvizFallbackOptions);
	TestTrue(
		TEXT("Ribbon remains renderable when ELK is unavailable"),
		GraphvizFallback.IsRenderable(Projection));
	TestEqual(
		TEXT("Graphviz owns the complete fallback ledger"),
		GraphvizFallback.Layout.LayoutLedger.Backend,
		EBlueprintLensLayoutBackendKind::GraphvizDot);
	TestTrue(
		TEXT("Graphviz fallback records rejected ELK attempt"),
		GraphvizFallback.Attempts.Num() == 2 &&
			!GraphvizFallback.Attempts[0].bAvailable &&
			GraphvizFallback.Attempts[1].bAccepted);

	FBlueprintLensLC3ValueConeLayoutSessionOptions DeterministicFallbackOptions =
		GraphvizFallbackOptions;
	DeterministicFallbackOptions.Graphviz.ExecutablePath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("BlueprintLensMissingRibbonDot.exe"));
	const FBlueprintLensLC3ValueConeLayoutSessionResult DeterministicFallback =
		FBlueprintLensLC3ValueConeLayoutSession::Build(
			Projection,
			480.0f,
			DeterministicFallbackOptions);
	TestTrue(
		TEXT("Ribbon remains renderable without external backends"),
		DeterministicFallback.IsRenderable(Projection));
	TestEqual(
		TEXT("Deterministic backend owns the final fallback ledger"),
		DeterministicFallback.Layout.LayoutLedger.Backend,
		EBlueprintLensLayoutBackendKind::Deterministic);
	TestTrue(
		TEXT("Deterministic fallback records the complete policy trace"),
		DeterministicFallback.Attempts.Num() == 3 &&
			DeterministicFallback.Attempts[2].bAccepted);

	class FIncompleteElkBackend final : public IBlueprintLensLayoutBackend
	{
	public:
		virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override
		{
			return EBlueprintLensLayoutBackendKind::ElkLayered;
		}

		virtual bool IsAvailable(FString& OutDiagnostic) const override
		{
			OutDiagnostic = TEXT("TEST_INCOMPLETE_ELK_AVAILABLE");
			return true;
		}

		virtual FBlueprintLensLayoutLedger Layout(
			const FBlueprintLensLayoutRequest&) const override
		{
			FBlueprintLensLayoutLedger Ledger;
			Ledger.Backend = EBlueprintLensLayoutBackendKind::ElkLayered;
			Ledger.BackendVersion = TEXT("test-incomplete");
			Ledger.ConfigurationFingerprint = TEXT("test-incomplete");
			Ledger.CanvasSize = FVector2D(10.0f, 10.0f);
			Ledger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
			return Ledger;
		}
	};
	class FUnavailableGraphvizBackend final : public IBlueprintLensLayoutBackend
	{
	public:
		virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override
		{
			return EBlueprintLensLayoutBackendKind::GraphvizDot;
		}

		virtual bool IsAvailable(FString& OutDiagnostic) const override
		{
			OutDiagnostic = TEXT("TEST_GRAPHVIZ_UNAVAILABLE");
			return false;
		}

		virtual FBlueprintLensLayoutLedger Layout(
			const FBlueprintLensLayoutRequest&) const override
		{
			return FBlueprintLensLayoutLedger();
		}
	};
	const FIncompleteElkBackend IncompleteElk;
	const FUnavailableGraphvizBackend UnavailableGraphviz;
	const FBlueprintLensLC3ValueConeLayoutSessionResult IncompleteFallback =
		FBlueprintLensLC3ValueConeLayoutSession::BuildWithBackends(
			Projection,
			480.0f,
			IncompleteElk,
			UnavailableGraphviz);
	TestEqual(
		TEXT("Incomplete external ledger cannot own Ribbon geometry"),
		IncompleteFallback.Layout.LayoutLedger.Backend,
		EBlueprintLensLayoutBackendKind::Deterministic);
	TestTrue(
		TEXT("Incomplete external ledger is explicitly rejected"),
		IncompleteFallback.Attempts.Num() == 3 &&
			IncompleteFallback.Attempts[0].bAvailable &&
			!IncompleteFallback.Attempts[0].bAccepted &&
			IncompleteFallback.Attempts[0].DiagnosticCode.Contains(
				TEXT("LEDGER_INCOMPLETE")));
	TestTrue(
		TEXT("Wide Ribbon rejects an over-width preferred ledger"),
		FBlueprintLensLC3ValueConeLayoutSession::Build(Projection, 700.0f)
			.AttemptSummary()
			.Contains(TEXT("CANVAS_EXCEEDS_TARGET")));

	EBlueprintLensLC3ValueConeDensity Density =
		EBlueprintLensLC3ValueConeDensity::Ports;
	const TSharedRef<SBlueprintLensLC3ValueConeCanvas> Ribbon =
		SNew(SBlueprintLensLC3ValueConeCanvas)
			.Projection(Projection)
			.InitialSession(PortsSession)
			.SelectedUnitId(FString())
			.Density_Lambda([&Density]() { return Density; });
	const FBlueprintLensLayoutLedger GeometryBefore =
		Ribbon->GetLayoutForTesting().LayoutLedger;
	Density = EBlueprintLensLC3ValueConeDensity::Evidence;
	const FBlueprintLensLayoutLedger& GeometryAfter =
		Ribbon->GetLayoutForTesting().LayoutLedger;
	TestEqual(
		TEXT("Density keeps selected backend"),
		GeometryAfter.Backend,
		GeometryBefore.Backend);
	TestEqual(
		TEXT("Density keeps node count"),
		GeometryAfter.Nodes.Num(),
		GeometryBefore.Nodes.Num());
	TestEqual(
		TEXT("Density keeps edge count"),
		GeometryAfter.Edges.Num(),
		GeometryBefore.Edges.Num());
	for (const FBlueprintLensLayoutNodePlacement& Node : GeometryBefore.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* After =
			GeometryAfter.Nodes.FindByPredicate(
				[&Node](const FBlueprintLensLayoutNodePlacement& Candidate)
				{
					return Candidate.UnitId == Node.UnitId;
				});
		TestNotNull(TEXT("Density retains every node"), After);
		if (After != nullptr)
		{
			TestTrue(
				TEXT("Density retains node geometry"),
				Node.Position.Equals(After->Position, 0.01f) &&
					Node.Size.Equals(After->Size, 0.01f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC3DerivationSpineTest,
	"BlueprintLens.LC3.DerivationSpine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC3DerivationSpineTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("LC3 fixture loads for D3"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("D3 input projection is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		return false;
	}

	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC3ValueConeLayoutSessionResult Session =
			FBlueprintLensLC3ValueConeLayoutSession::Build(Projection, Width);
		TestTrue(
			FString::Printf(TEXT("D3 base session renders at %.0f"), Width),
			Session.IsRenderable(Projection));
		if (!Session.IsRenderable(Projection))
		{
			continue;
		}

		const FBlueprintLensLC3DerivationSpineLayout DefaultLayout =
			FBlueprintLensLC3DerivationSpineLayoutBuilder::Build(
				Projection,
				Session.Layout,
				Width,
				FString());
		TestTrue(
			FString::Printf(TEXT("D3 default renders at %.0f"), Width),
			DefaultLayout.IsRenderable(Projection));
		TestEqual(
			TEXT("D3 default retains seven canonical units"),
			DefaultLayout.Elements.Num(),
			7);
		TestEqual(
			TEXT("D3 default retains six relations"),
			DefaultLayout.Routes.Num(),
			6);
		TestTrue(
			TEXT("D3 default has no route-label intersections"),
			DefaultLayout.HasNoRouteLabelIntersections());
		TestTrue(
			TEXT("D3 default has no label-label intersections"),
			DefaultLayout.HasNoLabelIntersections());
		TestFalse(
			TEXT("D3 default has no permanent subtree"),
			DefaultLayout.bHasLocalSubtree);
		TestEqual(
			TEXT("D3 default hides qualified endpoints"),
			DefaultLayout.Labels.FilterByPredicate(
				[](const FBlueprintLensLC3DerivationSpineLabel& Label)
				{
					return Label.bSelectedEndpoint;
				}).Num(),
			0);

		for (const FString OperatorName : {TEXT("Add"), TEXT("Subtract")})
		{
			const FBlueprintLensLC3DerivationSpineElement* DefaultOperator =
				DefaultLayout.Elements.FindByPredicate(
					[&OperatorName](
						const FBlueprintLensLC3DerivationSpineElement& Element)
					{
						return Element.DisplayLabel == OperatorName;
					});
			TestNotNull(
				FString::Printf(TEXT("D3 finds %s operator"), *OperatorName),
				DefaultOperator);
			if (DefaultOperator == nullptr)
			{
				continue;
			}

			const FBlueprintLensLC3DerivationSpineLayout SelectedLayout =
				FBlueprintLensLC3DerivationSpineLayoutBuilder::Build(
					Projection,
					Session.Layout,
					Width,
					DefaultOperator->UnitId);
			TestTrue(
				FString::Printf(
					TEXT("D3 selected %s renders at %.0f"),
					*OperatorName,
					Width),
				SelectedLayout.IsRenderable(Projection));
			TestTrue(
				TEXT("D3 selection opens one local subtree"),
				SelectedLayout.bHasLocalSubtree);
			TestEqual(
				TEXT("D3 selection retains canonical element count"),
				SelectedLayout.Elements.Num(),
				DefaultLayout.Elements.Num());
			TestEqual(
				TEXT("D3 selection retains relation count"),
				SelectedLayout.Routes.Num(),
				DefaultLayout.Routes.Num());
			TestTrue(
				TEXT("D3 selected routes avoid every visible label"),
				SelectedLayout.HasNoRouteLabelIntersections());
			TestTrue(
				TEXT("D3 selected labels avoid each other"),
				SelectedLayout.HasNoLabelIntersections());

			const TArray<FBlueprintLensLC3DerivationSpineLabel> EndpointLabels =
				SelectedLayout.Labels.FilterByPredicate(
					[](const FBlueprintLensLC3DerivationSpineLabel& Label)
					{
						return Label.bSelectedEndpoint;
					});
			TestEqual(
				TEXT("D3 selection reveals three qualified endpoints"),
				EndpointLabels.Num(),
				3);
			for (const FBlueprintLensLC3DerivationSpineLabel& Label :
				 EndpointLabels)
			{
				TestTrue(
					TEXT("D3 endpoint is qualified by selected operator"),
					Label.Text.StartsWith(OperatorName + TEXT(".")));
			}

			const FBlueprintLensLC3DerivationSpineLayout CollapsedLayout =
				FBlueprintLensLC3DerivationSpineLayoutBuilder::Build(
					Projection,
					Session.Layout,
					Width,
					FString());
			const FBlueprintLensLC3DerivationSpineElement* CollapsedOperator =
				CollapsedLayout.FindElement(DefaultOperator->UnitId);
			TestNotNull(TEXT("D3 collapse retains operator"), CollapsedOperator);
			if (CollapsedOperator != nullptr)
			{
				TestTrue(
					TEXT("D3 collapse restores operator mental-map anchor"),
					CollapsedOperator->Bounds.Min.Equals(
						DefaultOperator->Bounds.Min,
						0.01f));
			}
		}

		FBlueprintLensLC3DerivationSpineLayout ClearanceMutation =
			DefaultLayout;
		if (!ClearanceMutation.Labels.IsEmpty() &&
			!ClearanceMutation.Routes.IsEmpty())
		{
			const FVector2D SegmentMidpoint =
				(ClearanceMutation.Routes[0].Points[0] +
				 ClearanceMutation.Routes[0].Points[1]) * 0.5f;
			ClearanceMutation.Labels[0].ExclusionBounds = FBox2D(
				SegmentMidpoint - FVector2D(8.0f, 8.0f),
				SegmentMidpoint + FVector2D(8.0f, 8.0f));
			TestFalse(
				TEXT("D3 clearance audit detects a route crossing"),
				ClearanceMutation.HasNoRouteLabelIntersections());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensGraphvizLayoutBackendTest,
	"BlueprintLens.Layout.GraphvizBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensGraphvizLayoutBackendTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("Graphviz fixture loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("Graphviz projection is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		return false;
	}

	FBlueprintLensGraphvizLayoutBackend Backend;
	FString Availability;
	const bool bAvailable = Backend.IsAvailable(Availability);
	TestTrue(TEXT("Graphviz user-level executable is available"), bAvailable);
	if (!bAvailable)
	{
		AddWarning(Availability);
		return true;
	}

	const float Widths[] = {430.0f, 480.0f, 700.0f};
	for (const float Width : Widths)
	{
		const FBlueprintLensLC3ValueConeLayout SourceLayout =
			FBlueprintLensLC3ValueConeLayoutBuilder::Build(Projection, Width);
		const FBlueprintLensLayoutLedger Ledger =
			Backend.Layout(SourceLayout.LayoutRequest);
		TestEqual(
			FString::Printf(TEXT("Graphviz diagnostic at %.0f"), Width),
			Ledger.DiagnosticCode,
			FString(TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE")));
		TestTrue(
			FString::Printf(TEXT("Graphviz ledger complete at %.0f"), Width),
			Ledger.IsCompleteFor(SourceLayout.LayoutRequest));
		TestEqual(
			FString::Printf(TEXT("Graphviz backend at %.0f"), Width),
			Ledger.Backend,
			EBlueprintLensLayoutBackendKind::GraphvizDot);
		TestEqual(
			FString::Printf(TEXT("Graphviz retains seven nodes at %.0f"), Width),
			Ledger.Nodes.Num(),
			7);
		TestEqual(
			FString::Printf(TEXT("Graphviz retains six relations at %.0f"), Width),
			Ledger.Edges.Num(),
			6);
		TestEqual(
			FString::Printf(TEXT("Graphviz retains twelve ports at %.0f"), Width),
			Ledger.Ports.Num(),
			12);
		TestTrue(
			FString::Printf(TEXT("Graphviz has no node overlap at %.0f"), Width),
			Ledger.HasNoNodeOverlaps());
		TestFalse(
			FString::Printf(TEXT("Graphviz version is not blank at %.0f"), Width),
			Ledger.BackendVersion.IsEmpty());
		TestEqual(
			FString::Printf(TEXT("Graphviz pinned version at %.0f"), Width),
			Ledger.BackendVersion,
			FString(TEXT("15.1.1")));
		TestFalse(
			FString::Printf(TEXT("Graphviz fingerprint is not blank at %.0f"), Width),
			Ledger.ConfigurationFingerprint.IsEmpty());
	}

	const FBlueprintLensLC3ValueConeLayout DeterministicLayout =
		FBlueprintLensLC3ValueConeLayoutBuilder::Build(Projection, 480.0f);
	const FBlueprintLensLayoutLedger First =
		Backend.Layout(DeterministicLayout.LayoutRequest);
	const FBlueprintLensLayoutLedger Second =
		Backend.Layout(DeterministicLayout.LayoutRequest);
	TestEqual(TEXT("Repeated Graphviz node count is stable"), First.Nodes.Num(), Second.Nodes.Num());
	TestEqual(TEXT("Repeated Graphviz edge count is stable"), First.Edges.Num(), Second.Edges.Num());
	TestEqual(TEXT("Repeated Graphviz port count is stable"), First.Ports.Num(), Second.Ports.Num());
	TestTrue(TEXT("Repeated Graphviz canvas is stable"), First.CanvasSize.Equals(Second.CanvasSize, 0.01f));
	TestEqual(TEXT("Repeated Graphviz version is stable"), First.BackendVersion, Second.BackendVersion);
	TestEqual(
		TEXT("Repeated Graphviz fingerprint is stable"),
		First.ConfigurationFingerprint,
		Second.ConfigurationFingerprint);
	for (const FBlueprintLensLayoutNodePlacement& Node : First.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Other = Second.Nodes.FindByPredicate(
			[&Node](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId == Node.UnitId;
			});
		TestNotNull(TEXT("Repeated Graphviz node identity is retained"), Other);
		if (Other != nullptr)
		{
			TestTrue(TEXT("Repeated Graphviz node position is stable"), Node.Position.Equals(Other->Position, 0.01f));
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : First.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Other = Second.Ports.FindByPredicate(
			[&Port](const FBlueprintLensLayoutPortPlacement& Candidate)
			{
				return Candidate.UnitId == Port.UnitId &&
					Candidate.Label == Port.Label &&
					Candidate.bInput == Port.bInput;
			});
		TestNotNull(TEXT("Repeated Graphviz port identity is retained"), Other);
		if (Other != nullptr)
		{
			TestTrue(TEXT("Repeated Graphviz port position is stable"), Port.Position.Equals(Other->Position, 0.01f));
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : First.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Other = Second.Edges.FindByPredicate(
			[&Edge](const FBlueprintLensLayoutEdgePlacement& Candidate)
			{
				return Candidate.RelationId == Edge.RelationId;
			});
		TestNotNull(TEXT("Repeated Graphviz edge identity is retained"), Other);
		if (Other == nullptr)
		{
			continue;
		}
		TestEqual(
			TEXT("Repeated Graphviz bend count is stable"),
			Edge.BendPoints.Num(),
			Other->BendPoints.Num());
		for (int32 PointIndex = 0;
			PointIndex < Edge.BendPoints.Num() && PointIndex < Other->BendPoints.Num();
			++PointIndex)
		{
			TestTrue(
				TEXT("Repeated Graphviz bend point is stable"),
				Edge.BendPoints[PointIndex].Equals(Other->BendPoints[PointIndex], 0.01f));
		}
	}

	FBlueprintLensGraphvizLayoutOptions MissingOptions;
	MissingOptions.ExecutablePath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("BlueprintLens"),
		TEXT("missing-dot.exe"));
	FBlueprintLensGraphvizLayoutBackend MissingBackend(MissingOptions);
	FString MissingDiagnostic;
	TestFalse(TEXT("Missing Graphviz executable is unavailable"), MissingBackend.IsAvailable(MissingDiagnostic));
	TestTrue(TEXT("Missing Graphviz diagnostic is explicit"), MissingDiagnostic.Contains(TEXT("MISSING")));
	const FBlueprintLensLayoutLedger MissingLedger = MissingBackend.Layout(
		DeterministicLayout.LayoutRequest);
	TestTrue(TEXT("Missing Graphviz fails closed"), MissingLedger.DiagnosticCode.Contains(TEXT("MISSING")));

	FBlueprintLensLayoutLedger NormalizedLedger;
	FString NormalizeDiagnostic;
	TestFalse(
		TEXT("Malformed Graphviz JSON is rejected"),
		FBlueprintLensGraphvizLayoutBackend::NormalizeJson(
			TEXT("not-json"),
			DeterministicLayout.LayoutRequest,
			TEXT("test"),
			TEXT("test"),
			NormalizedLedger,
			NormalizeDiagnostic));
	TestTrue(TEXT("Malformed Graphviz JSON diagnostic is explicit"), NormalizeDiagnostic.Contains(TEXT("MALFORMED")));
	TestFalse(
		TEXT("Graphviz coverage mismatch is rejected"),
		FBlueprintLensGraphvizLayoutBackend::NormalizeJson(
			TEXT("{\"bb\":\"0,0,10,10\",\"objects\":[],\"edges\":[]}"),
			DeterministicLayout.LayoutRequest,
			TEXT("test"),
			TEXT("test"),
			NormalizedLedger,
			NormalizeDiagnostic));
	TestTrue(TEXT("Graphviz coverage diagnostic is explicit"), NormalizeDiagnostic.Contains(TEXT("COVERAGE")));

	FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
	const FString PingExecutable = FPaths::Combine(SystemRoot, TEXT("System32"), TEXT("ping.exe"));
	if (FPaths::FileExists(PingExecutable))
	{
		FBlueprintLensExternalLayoutProcessOptions TimeoutOptions;
		TimeoutOptions.ExecutablePath = PingExecutable;
		TimeoutOptions.Arguments = TEXT("127.0.0.1 -n 8 -w 1000");
		TimeoutOptions.TimeoutSeconds = 0.05;
		const FBlueprintLensExternalLayoutProcessResult TimeoutResult =
			FBlueprintLensExternalLayoutProcess::Run(TimeoutOptions);
		TestTrue(TEXT("External layout process timeout is enforced"), TimeoutResult.bTimedOut);
		TestEqual(
			TEXT("External layout process timeout diagnostic"),
			TimeoutResult.DiagnosticCode,
			FString(TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_TIMEOUT")));
	}
	else
	{
		AddWarning(TEXT("System ping.exe unavailable; timeout smoke test skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensElkLayoutBackendTest,
	"BlueprintLens.Layout.ElkBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensElkLayoutBackendTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("ELK fixture loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Projection =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("ELK projection is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		return false;
	}

	FBlueprintLensElkLayoutBackend Backend;
	FString Availability;
	const bool bAvailable = Backend.IsAvailable(Availability);
	TestTrue(TEXT("ELK user-level dependencies are available"), bAvailable);
	if (!bAvailable)
	{
		AddWarning(Availability);
		return true;
	}

	const float Widths[] = {430.0f, 480.0f, 700.0f};
	for (const float Width : Widths)
	{
		const FBlueprintLensLC3ValueConeLayout SourceLayout =
			FBlueprintLensLC3ValueConeLayoutBuilder::Build(Projection, Width);
		const FBlueprintLensLayoutLedger Ledger =
			Backend.Layout(SourceLayout.LayoutRequest);
		TestEqual(
			FString::Printf(TEXT("ELK diagnostic at %.0f"), Width),
			Ledger.DiagnosticCode,
			FString(TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE")));
		TestTrue(
			FString::Printf(TEXT("ELK ledger complete at %.0f"), Width),
			Ledger.IsCompleteFor(SourceLayout.LayoutRequest));
		TestEqual(
			FString::Printf(TEXT("ELK backend at %.0f"), Width),
			Ledger.Backend,
			EBlueprintLensLayoutBackendKind::ElkLayered);
		TestEqual(
			FString::Printf(TEXT("ELK retains seven nodes at %.0f"), Width),
			Ledger.Nodes.Num(),
			7);
		TestEqual(
			FString::Printf(TEXT("ELK retains six relations at %.0f"), Width),
			Ledger.Edges.Num(),
			6);
		TestEqual(
			FString::Printf(TEXT("ELK retains twelve ports at %.0f"), Width),
			Ledger.Ports.Num(),
			12);
		TestTrue(
			FString::Printf(TEXT("ELK has no node overlap at %.0f"), Width),
			Ledger.HasNoNodeOverlaps());
		TestEqual(
			FString::Printf(TEXT("ELK pinned version at %.0f"), Width),
			Ledger.BackendVersion,
			FString(TEXT("ELK.js 0.12.0")));
		TestFalse(
			FString::Printf(TEXT("ELK fingerprint is not blank at %.0f"), Width),
			Ledger.ConfigurationFingerprint.IsEmpty());
	}

	const FBlueprintLensLayoutRequest DeterministicRequest =
		FBlueprintLensLC3ValueConeLayoutBuilder::Build(Projection, 480.0f).LayoutRequest;
	const FBlueprintLensLayoutLedger First = Backend.Layout(DeterministicRequest);
	const FBlueprintLensLayoutLedger Second = Backend.Layout(DeterministicRequest);
	TestTrue(TEXT("Repeated ELK canvas is stable"), First.CanvasSize.Equals(Second.CanvasSize, 0.01f));
	TestEqual(TEXT("Repeated ELK node count is stable"), First.Nodes.Num(), Second.Nodes.Num());
	TestEqual(TEXT("Repeated ELK port count is stable"), First.Ports.Num(), Second.Ports.Num());
	TestEqual(TEXT("Repeated ELK edge count is stable"), First.Edges.Num(), Second.Edges.Num());
	TestEqual(TEXT("Repeated ELK version is stable"), First.BackendVersion, Second.BackendVersion);
	TestEqual(TEXT("Repeated ELK fingerprint is stable"), First.ConfigurationFingerprint, Second.ConfigurationFingerprint);
	for (const FBlueprintLensLayoutNodePlacement& Node : First.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* Other = Second.Nodes.FindByPredicate(
			[&Node](const FBlueprintLensLayoutNodePlacement& Candidate)
			{
				return Candidate.UnitId == Node.UnitId;
			});
		TestNotNull(TEXT("Repeated ELK node identity is retained"), Other);
		if (Other != nullptr)
		{
			TestTrue(TEXT("Repeated ELK node position is stable"), Node.Position.Equals(Other->Position, 0.01f));
		}
	}
	for (const FBlueprintLensLayoutPortPlacement& Port : First.Ports)
	{
		const FBlueprintLensLayoutPortPlacement* Other = Second.Ports.FindByPredicate(
			[&Port](const FBlueprintLensLayoutPortPlacement& Candidate)
			{
				return Candidate.UnitId == Port.UnitId && Candidate.Label == Port.Label &&
					Candidate.bInput == Port.bInput;
			});
		TestNotNull(TEXT("Repeated ELK port identity is retained"), Other);
		if (Other != nullptr)
		{
			TestTrue(TEXT("Repeated ELK port position is stable"), Port.Position.Equals(Other->Position, 0.01f));
		}
	}
	for (const FBlueprintLensLayoutEdgePlacement& Edge : First.Edges)
	{
		const FBlueprintLensLayoutEdgePlacement* Other = Second.Edges.FindByPredicate(
			[&Edge](const FBlueprintLensLayoutEdgePlacement& Candidate)
			{
				return Candidate.RelationId == Edge.RelationId;
			});
		TestNotNull(TEXT("Repeated ELK edge identity is retained"), Other);
		if (Other == nullptr)
		{
			continue;
		}
		TestEqual(TEXT("Repeated ELK bend count is stable"), Edge.BendPoints.Num(), Other->BendPoints.Num());
		for (int32 PointIndex = 0;
			PointIndex < Edge.BendPoints.Num() && PointIndex < Other->BendPoints.Num();
			++PointIndex)
		{
			TestTrue(TEXT("Repeated ELK bend point is stable"), Edge.BendPoints[PointIndex].Equals(Other->BendPoints[PointIndex], 0.01f));
		}
	}

	FBlueprintLensElkLayoutOptions MissingNodeOptions;
	MissingNodeOptions.NodeExecutablePath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("missing-node.exe"));
	FBlueprintLensElkLayoutBackend MissingNodeBackend(MissingNodeOptions);
	FString MissingDiagnostic;
	TestFalse(TEXT("Missing Node.js is unavailable"), MissingNodeBackend.IsAvailable(MissingDiagnostic));
	TestTrue(TEXT("Missing Node.js diagnostic is explicit"), MissingDiagnostic.Contains(TEXT("MISSING")));
	TestTrue(TEXT("Missing Node.js fails closed"), MissingNodeBackend.Layout(DeterministicRequest).DiagnosticCode.Contains(TEXT("MISSING")));

	FBlueprintLensElkLayoutOptions MissingRootOptions;
	MissingRootOptions.ElkJsRoot = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("missing-elk-root"));
	FBlueprintLensElkLayoutBackend MissingRootBackend(MissingRootOptions);
	MissingDiagnostic.Reset();
	TestFalse(TEXT("Missing ELK root is unavailable"), MissingRootBackend.IsAvailable(MissingDiagnostic));
	TestTrue(TEXT("Missing ELK root diagnostic is explicit"), MissingDiagnostic.Contains(TEXT("ROOT_MISSING")));

	FBlueprintLensElkLayoutOptions MissingHelperOptions;
	MissingHelperOptions.HelperPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("missing-elk-helper.mjs"));
	FBlueprintLensElkLayoutBackend MissingHelperBackend(MissingHelperOptions);
	MissingDiagnostic.Reset();
	TestFalse(TEXT("Missing ELK helper is unavailable"), MissingHelperBackend.IsAvailable(MissingDiagnostic));
	TestTrue(TEXT("Missing ELK helper diagnostic is explicit"), MissingDiagnostic.Contains(TEXT("HELPER_MISSING")));

	FBlueprintLensLayoutLedger NormalizedLedger;
	FString NormalizeDiagnostic;
	TestFalse(
		TEXT("Malformed ELK JSON is rejected"),
		FBlueprintLensElkLayoutBackend::NormalizeResponse(
			TEXT("not-json"), DeterministicRequest, TEXT(""), TEXT("test"),
			NormalizedLedger, NormalizeDiagnostic));
	TestTrue(TEXT("Malformed ELK JSON diagnostic is explicit"), NormalizeDiagnostic.Contains(TEXT("MALFORMED")));
	TestFalse(
		TEXT("ELK coverage mismatch is rejected"),
		FBlueprintLensElkLayoutBackend::NormalizeResponse(
			TEXT("{\"schema_version\":\"blueprint-lens-layout-response.v1\",\"backend\":\"ELK.Layered\",\"backend_version\":\"ELK.js 0.12.0\",\"graph\":{\"id\":\"root\",\"children\":[],\"edges\":[]}}"),
			DeterministicRequest, TEXT(""), TEXT("test"), NormalizedLedger, NormalizeDiagnostic));
	TestTrue(TEXT("ELK coverage diagnostic is explicit"), NormalizeDiagnostic.Contains(TEXT("COVERAGE")));

	FBlueprintLensLayoutRequest SmallRequest;
	SmallRequest.GraphKey = TEXT("elk-small");
	SmallRequest.Profile = EBlueprintLensLayoutProfile::LayeredPorts;
	SmallRequest.TargetWidth = 480.0f;
	FBlueprintLensLayoutNodeRequest SourceNode;
	SourceNode.UnitId = TEXT("source");
	SourceNode.DesiredSize = FVector2D(100.0f, 60.0f);
	SourceNode.Ports.Add({TEXT("out"), false, 0});
	SmallRequest.Nodes.Add(SourceNode);
	FBlueprintLensLayoutNodeRequest TargetNode;
	TargetNode.UnitId = TEXT("target");
	TargetNode.DesiredSize = FVector2D(100.0f, 60.0f);
	TargetNode.Ports.Add({TEXT("in"), true, 0});
	SmallRequest.Nodes.Add(TargetNode);
	FBlueprintLensLayoutEdgeRequest SmallEdge;
	SmallEdge.RelationId = TEXT("rel");
	SmallEdge.SourceUnitId = TEXT("source");
	SmallEdge.TargetUnitId = TEXT("target");
	SmallEdge.SourcePortLabel = TEXT("out");
	SmallEdge.TargetPortLabel = TEXT("in");
	SmallRequest.Edges.Add(SmallEdge);
	FBlueprintLensLayoutGroupRequest SmallGroup;
	SmallGroup.GroupId = TEXT("group");
	SmallGroup.MemberUnitIds = {TEXT("source"), TEXT("target")};
	SmallRequest.Groups.Add(SmallGroup);
	const FString CompoundResponse = TEXT(
		"{\"schema_version\":\"blueprint-lens-layout-response.v1\",\"backend\":\"ELK.Layered\",\"backend_version\":\"ELK.js 0.12.0\",\"graph\":{\"id\":\"root\",\"children\":[{\"id\":\"bl_group_0\",\"x\":10,\"y\":20,\"width\":250,\"height\":100,\"children\":[{\"id\":\"bl_node_0\",\"x\":10,\"y\":10,\"width\":100,\"height\":60,\"ports\":[{\"id\":\"bl_port_0_0\",\"x\":99.5,\"y\":29.5,\"width\":1,\"height\":1}]},{\"id\":\"bl_node_1\",\"x\":140,\"y\":10,\"width\":100,\"height\":60,\"ports\":[{\"id\":\"bl_port_1_0\",\"x\":-0.5,\"y\":29.5,\"width\":1,\"height\":1}]}],\"edges\":[{\"id\":\"bl_edge_0\",\"sources\":[\"bl_port_0_0\"],\"targets\":[\"bl_port_1_0\"],\"sections\":[{\"startPoint\":{\"x\":110,\"y\":40},\"endPoint\":{\"x\":140,\"y\":40},\"bendPoints\":[{\"x\":125,\"y\":40}]}]}]}],\"edges\":[]}} ");
	const bool bCompoundNormalized =
		FBlueprintLensElkLayoutBackend::NormalizeResponse(
			CompoundResponse, SmallRequest, TEXT(""), TEXT("test"),
			NormalizedLedger, NormalizeDiagnostic);
	TestTrue(TEXT("ELK compound response normalizes recursively"), bCompoundNormalized);
	TestEqual(TEXT("ELK compound response retains nodes"), NormalizedLedger.Nodes.Num(), 2);
	TestEqual(TEXT("ELK compound response retains ports"), NormalizedLedger.Ports.Num(), 2);
	if (bCompoundNormalized)
	{
		TestEqual(TEXT("ELK compound response retains edge bends"), NormalizedLedger.Edges[0].BendPoints.Num(), 1);
		TestTrue(TEXT("ELK compound response ledger is complete"), NormalizedLedger.IsCompleteFor(SmallRequest));
	}

	const FString TimeoutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintLens"), TEXT("Tests"));
	IFileManager::Get().MakeDirectory(*TimeoutDir, true);
	const FString TimeoutHelper = FPaths::Combine(TimeoutDir, TEXT("elk-timeout.mjs"));
	FFileHelper::SaveStringToFile(
		TEXT("process.stdin.resume(); process.stdin.on('end', () => setTimeout(() => {}, 10000));"),
		*TimeoutHelper);
	FBlueprintLensElkLayoutOptions TimeoutOptions;
	TimeoutOptions.NodeExecutablePath = FPlatformMisc::GetEnvironmentVariable(TEXT("BLUEPRINT_LENS_NODE_EXE"));
	TimeoutOptions.ElkJsRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("BLUEPRINT_LENS_ELKJS_ROOT"));
	TimeoutOptions.HelperPath = TimeoutHelper;
	TimeoutOptions.TimeoutSeconds = 0.05;
	FBlueprintLensElkLayoutBackend TimeoutBackend(TimeoutOptions);
	if (!TimeoutOptions.NodeExecutablePath.IsEmpty() && !TimeoutOptions.ElkJsRoot.IsEmpty())
	{
		const FBlueprintLensLayoutLedger TimeoutLedger = TimeoutBackend.Layout(DeterministicRequest);
		TestEqual(
			TEXT("ELK helper timeout fails closed"),
			TimeoutLedger.DiagnosticCode,
			FString(TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_TIMEOUT")));
	}
	else
	{
		AddWarning(TEXT("ELK user-level roots unavailable; ELK timeout smoke test skipped"));
	}
	IFileManager::Get().Delete(*TimeoutHelper, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceProfileTest,
	"BlueprintLens.Editor.LC4SequenceProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceProfileTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 sequence profile loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProfile& Profile = *LoadResult.Profile;
	TestEqual(TEXT("LC4 profile accounts for four outputs"), Profile.Outputs.Num(), 4);
	TestEqual(TEXT("LC4 profile accounts for eight units"), Profile.AccountedUnitIds.Num(), 8);
	TestEqual(TEXT("LC4 profile accounts for eight relations"), Profile.AccountedRelationIds.Num(), 8);
	TestEqual(TEXT("LC4 connected count"), Profile.Counts.ConnectedOutputs, 3);
	TestEqual(TEXT("LC4 unconnected count"), Profile.Counts.UnconnectedOutputs, 1);
	TestEqual(TEXT("LC4 included count"), Profile.Counts.CriterionIncludedOutputs, 2);
	TestEqual(
		TEXT("LC4 outside-connected count"),
		Profile.Counts.OutsideCriterionConnectedOutputs,
		1);
	TestEqual(TEXT("LC4 indeterminate count"), Profile.Counts.IndeterminateOutputs, 0);
	TestEqual(
		TEXT("LC4 merge incoming ordinals"),
		Profile.Reconvergence.IncomingOutputOrdinals,
		TArray<int32>({0, 1}));
	TestEqual(
		TEXT("LC4 adapter exposes six lanes"),
		LoadResult.ExplanationModel->Lanes.Num(),
		6);
	TestEqual(
		TEXT("LC4 adapter exposes eight units"),
		LoadResult.ExplanationModel->Units.Num(),
		8);
	TestEqual(
		TEXT("LC4 adapter exposes eight relations"),
		LoadResult.ExplanationModel->Relations.Num(),
		8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceProjectionTest,
	"BlueprintLens.Editor.LC4SequenceProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for projection"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	TestTrue(TEXT("LC4 disclosure projection is renderable"), Projection.IsRenderable());
	TestEqual(TEXT("LC4 projection keeps four routes"), Projection.Routes.Num(), 4);
	TestEqual(
		TEXT("then_0 route has two private units"),
		Projection.Routes[0].RouteReaderLabels.Num(),
		2);
	TestEqual(
		TEXT("then_1 route has two private units"),
		Projection.Routes[1].RouteReaderLabels.Num(),
		2);
	TestEqual(
		TEXT("then_2 route has one outside unit"),
		Projection.Routes[2].RouteReaderLabels.Num(),
		1);
	TestTrue(
		TEXT("then_3 remains an empty output"),
		Projection.Routes[3].ConnectionState ==
			EBlueprintLensLC4ConnectionState::Unconnected &&
			Projection.Routes[3].RouteUnitIds.IsEmpty());
	TestEqual(
		TEXT("LC4 shared suffix has one canonical pair"),
		Projection.Merge.SharedSuffixReaderLabels,
		TArray<FString>({TEXT("Set LC4Reconverged"), TEXT("Set LC4Complete")}));

	FBlueprintLensLC4SequenceProfile CountMutation = *LoadResult.Profile;
	CountMutation.Counts.DeclaredOutputs = 5;
	const FBlueprintLensLC4SequenceProjection Rejected =
		FBlueprintLensLC4SequenceProjector::Build(
			CountMutation,
			*LoadResult.ExplanationModel);
	TestFalse(TEXT("count mutation fails closed"), Rejected.IsRenderable());
	TestEqual(
		TEXT("count mutation reports source binding failure"),
		Rejected.DiagnosticCode,
		FString(TEXT("LC4_SEQUENCE_SOURCE_BINDING_INVALID")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceStateTest,
	"BlueprintLens.Editor.LC4SequenceState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for panel state"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Before =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	TestTrue(TEXT("LC4 panel projection starts renderable"), Before.IsRenderable());

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC4SequenceProfile = LoadResult.Profile;
	Panel->LC4SelectedOutputOrdinal = INDEX_NONE;
	Panel->LC4DetailMode = SBlueprintLensPanel::ELC4DetailMode::None;
	Panel->ResolveSources();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	TestTrue(
		TEXT("LC4 scroll box is a hard viewport clipping boundary"),
		Panel->LC4SequenceScrollBox.IsValid() &&
			Panel->LC4SequenceScrollBox->GetClipping() ==
				EWidgetClipping::ClipToBoundsAlways);
	const FString ReaderText = SlateWidgetText(PanelRoot);
	FString ReaderTextWithoutTitle = ReaderText;
	const int32 TitleCount = ReaderTextWithoutTitle.ReplaceInline(
		TEXT("SEQUENCE DISCLOSURE RAIL"),
		TEXT(""),
		ESearchCase::CaseSensitive);
	TestEqual(
		TEXT("LC4 effect title appears exactly once"),
		TitleCount,
		1);
	TestFalse(
		TEXT("LC4 default reader removes the legacy explanatory wrapper"),
		ReaderText.Contains(TEXT(
			"One ordinal spine keeps every declared output visible.")));
	TestFalse(
		TEXT("LC4 default reader removes the legacy Summary control"),
		ReaderText.Contains(TEXT("Summary")));
	TestNotNull(
		TEXT("LC4 exact-width MCP review console variable is registered"),
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("BlueprintLens.LC4ReviewWidth")));
	TestNotNull(
		TEXT("LC4 MCP review scroll-offset console variable is registered"),
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("BlueprintLens.LC4ReviewScrollOffset")));
	for (const TCHAR* Required : {
		TEXT("SEQUENCE DISCLOSURE RAIL"),
		TEXT("SYNCHRONOUS SOURCE ORDER"),
		TEXT("then_0 → then_1 → then_2 → then_3"),
		TEXT("THEN_0 · INCLUDED"),
		TEXT("THEN_1 · INCLUDED"),
		TEXT("THEN_2 · OUTSIDE CRITERION"),
		TEXT("THEN_3 · UNCONNECTED"),
		TEXT("M1"),
		TEXT("merge"),
		TEXT("Set Reconverged"),
		TEXT("Set LC4Complete"),
		TEXT("ORDINARY MERGE · NO WAIT"),
		TEXT("4 declared"),
		TEXT("3 connected"),
		TEXT("1 empty"),
		TEXT("2 included"),
		TEXT("1 outside"),
		TEXT("0 indeterminate")})
	{
		TestTrue(
			FString::Printf(TEXT("LC4 default reader contains %s"), Required),
			ReaderText.Contains(Required));
	}
	for (const TCHAR* Hidden : {
		TEXT("PROFILE SHA-256"),
		TEXT("LC4_SEQUENCE_DISCLOSURE_RAIL_COMPLETE"),
		TEXT("::node::"),
		TEXT("::edge::")})
	{
		TestFalse(
			FString::Printf(TEXT("LC4 default reader withholds %s"), Hidden),
			ReaderText.Contains(Hidden));
	}
	TestTrue(
		TEXT("LC4 projection retains the deferred async frontier"),
		Before.BoundaryNotices.Contains(
			TEXT("LC4-ASYNC remains deferred outside core-v1.")));

	Panel->LC4SequenceScrollBox->SetScrollOffset(225.0f);
	Panel->SelectLC4Output(2);
	TestEqual(
		TEXT("LC4 output selection preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		225.0f);
	const FString SelectedText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("LC4 output selection reveals local detail"),
		SelectedText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")) &&
			SelectedText.Contains(TEXT("Open Sequence in Blueprint")) &&
			SelectedText.Contains(TEXT("Open Set LC4SideEffect in Blueprint")));
	TestFalse(
		TEXT("LC4 output detail excludes complete text and evidence"),
		CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) > 0 ||
			SelectedText.Contains(Before.SourceProfileSha256));

	Panel->LC4SequenceScrollBox->SetScrollOffset(250.0f);
	Panel->SelectLC4Output(2);
	const FString RepeatedSelectedText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 repeated output selection preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		250.0f);
	TestTrue(
		TEXT("LC4 repeated output selection remains deterministic"),
		RepeatedSelectedText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")));

	// Normalize the old toggle implementation before testing cross-action routing.
	Panel->SelectLC4Output(2);
	Panel->LC4SequenceScrollBox->SetScrollOffset(315.0f);
	Panel->ToggleLC4CompleteText();
	const FString CompleteText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 complete text preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		315.0f);
	TestTrue(
		TEXT("LC4 complete text appears in its owning section"),
		CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) == 1);
	TestFalse(
		TEXT("LC4 complete text excludes selected detail and evidence"),
		CompleteText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")) ||
			CompleteText.Contains(Before.SourceProfileSha256));

	Panel->LC4SequenceScrollBox->SetScrollOffset(340.0f);
	Panel->ToggleLC4CompleteText();
	const FString RepeatedCompleteText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 repeated complete text preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		340.0f);
	TestTrue(
		TEXT("LC4 repeated complete text remains deterministic"),
		CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) == 1 &&
			!RepeatedCompleteText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")));

	Panel->LC4SequenceScrollBox->SetScrollOffset(405.0f);
	Panel->ToggleLC4Evidence();
	const FString EvidenceText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 evidence preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		405.0f);
	TestTrue(
		TEXT("LC4 evidence reveals profile digest"),
		EvidenceText.Contains(Before.SourceProfileSha256));
	TestTrue(
		TEXT("LC4 evidence reveals projection diagnostic"),
		EvidenceText.Contains(Before.DiagnosticCode));
	TestFalse(
		TEXT("LC4 evidence excludes selected detail and complete text"),
		EvidenceText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")) ||
			CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) > 0);
	for (const FString& RelationId : Before.AllRelationIds)
	{
		TestTrue(
			FString::Printf(TEXT("LC4 evidence exposes relation %s"), *RelationId),
			EvidenceText.Contains(RelationId));
	}

	Panel->LC4SequenceScrollBox->SetScrollOffset(430.0f);
	Panel->ToggleLC4Evidence();
	const FString RepeatedEvidenceText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 repeated evidence preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		430.0f);
	TestTrue(
		TEXT("LC4 repeated evidence remains deterministic"),
		RepeatedEvidenceText.Contains(Before.SourceProfileSha256));
	TestFalse(
		TEXT("LC4 repeated evidence has no cross-section leakage"),
		RepeatedEvidenceText.Contains(TEXT("OUTPUT 02 DETAIL · then_2")) ||
			CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) > 0);

	Panel->LC4SequenceScrollBox->SetScrollOffset(455.0f);
	Panel->SelectLC4Output(1);
	const FString SwitchedSelectionText = SlateWidgetText(PanelRoot);
	TestEqual(
		TEXT("LC4 action switching preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		455.0f);
	TestTrue(
		TEXT("LC4 action switching routes only the selected output detail"),
		SwitchedSelectionText.Contains(TEXT("OUTPUT 01 DETAIL · then_1")) &&
			!SwitchedSelectionText.Contains(Before.SourceProfileSha256) &&
			CountSlateTextWithPrefix(PanelRoot, TEXT("00 · then_0 ·")) == 0);

	Panel->LC4SequenceScrollBox->SetScrollOffset(475.0f);
	Panel->OpenLC4SelectedSource();
	TestEqual(
		TEXT("LC4 open source preserves scroll offset"),
		Panel->LC4SequenceScrollBox->GetScrollOffset(),
		475.0f);

	const FBlueprintLensLC4SequenceProjection After =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	TestEqual(
		TEXT("LC4 unit ledger is invariant across disclosure"),
		After.AllUnitIds,
		Before.AllUnitIds);
	TestEqual(
		TEXT("LC4 relation ledger is invariant across disclosure"),
		After.AllRelationIds,
		Before.AllRelationIds);
	TestEqual(
		TEXT("LC4 profile digest is invariant across disclosure"),
		After.SourceProfileSha256,
		Before.SourceProfileSha256);
	return true;
}

class FBlueprintLensLC4FixedLayoutBackend final
	: public IBlueprintLensLayoutBackend
{
public:
	FBlueprintLensLC4FixedLayoutBackend(
		const EBlueprintLensLayoutBackendKind InKind,
		const bool bInAvailable,
		const FBlueprintLensLayoutLedger& InLedger)
		: Kind(InKind)
		, bAvailable(bInAvailable)
		, Ledger(InLedger)
	{
		Ledger.Backend = Kind;
		Ledger.BackendVersion = TEXT("BlueprintLens.TestFixed.v1");
	}

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override
	{
		return Kind;
	}

	virtual bool IsAvailable(FString& OutDiagnostic) const override
	{
		OutDiagnostic = bAvailable
			? TEXT("BLUEPRINT_LENS_TEST_BACKEND_AVAILABLE")
			: TEXT("BLUEPRINT_LENS_TEST_BACKEND_UNAVAILABLE");
		return bAvailable;
	}

	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest&) const override
	{
		return Ledger;
	}

private:
	EBlueprintLensLayoutBackendKind Kind;
	bool bAvailable;
	FBlueprintLensLayoutLedger Ledger;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceLayoutTest,
	"BlueprintLens.Editor.LC4SequenceLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceLayoutTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for layout"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);

	for (const TPair<float, FVector2D>& Target : {
		TPair<float, FVector2D>(430.0f, FVector2D(430.0f, 1360.0f)),
		TPair<float, FVector2D>(480.0f, FVector2D(480.0f, 1080.0f)),
		TPair<float, FVector2D>(700.0f, FVector2D(700.0f, 842.0f))})
	{
		const FBlueprintLensLC4SequenceLayout Layout =
			FBlueprintLensLC4SequenceLayoutBuilder::Build(
				Projection,
				Target.Key);
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f layout covers projection"), Target.Key),
			Layout.CoversProjection(Projection));
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f layout has one complete ledger"), Target.Key),
			Layout.HasValidSharedLedger());
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f layout matches visual oracle"), Target.Key),
			Layout.MatchesVisualOracle(1.0f));
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f labels clear routes"), Target.Key),
			Layout.HasNoLabelRouteCollisions());
		TestEqual(
			FString::Printf(TEXT("LC4 %.0f canvas is exact"), Target.Key),
			Layout.CanvasSize,
			Target.Value);
		TestEqual(
			TEXT("LC4 request uses LayeredPorts"),
			Layout.LayoutRequest.Profile,
			EBlueprintLensLayoutProfile::LayeredPorts);
		TestEqual(TEXT("LC4 request owns eight nodes"), Layout.LayoutRequest.Nodes.Num(), 8);
		TestEqual(TEXT("LC4 request owns eight edges"), Layout.LayoutRequest.Edges.Num(), 8);
		TestEqual(TEXT("LC4 layout owns four stations"), Layout.Stations.Num(), 4);
	}

	const FBlueprintLensLC4SequenceLayout Wide =
		FBlueprintLensLC4SequenceLayoutBuilder::Build(Projection, 700.0f);
	const FBlueprintLensLC4SequenceStationLayout* Station0 = Wide.FindStation(0);
	const FBlueprintLensLC4SequenceStationLayout* Station3 = Wide.FindStation(3);
	TestTrue(TEXT("LC4 wide station 0 exists"), Station0 != nullptr);
	TestTrue(TEXT("LC4 wide station 3 exists"), Station3 != nullptr);
	if (Station0 != nullptr && Station3 != nullptr)
	{
		TestEqual(TEXT("LC4 wide station 0 oracle center"), Station0->Center, FVector2D(80.0f, 230.0f));
		TestEqual(TEXT("LC4 wide station 3 oracle center"), Station3->Center, FVector2D(80.0f, 700.0f));
		TestEqual(TEXT("LC4 wide station radius"), Station0->Radius, 22.0f);
	}
	TestEqual(TEXT("LC4 wide merge oracle center"), Wide.MergeCenter, FVector2D(540.0f, 326.0f));
	TestEqual(TEXT("LC4 wide merge oracle radius"), Wide.MergeRadius, 25.0f);
	TestEqual(TEXT("LC4 wide outside terminal x"), Wide.OutsideTerminalX, 374.0f);
	TestEqual(TEXT("LC4 wide outside detail wraps to two lines"), Wide.OutsideDetailLines.Num(), 2);
	TestEqual(TEXT("LC4 wide outside detail owns two positions"), Wide.OutsideDetailLinePositions.Num(), 2);
	TestEqual(TEXT("LC4 wide outside detail owns two sizes"), Wide.OutsideDetailLineSizes.Num(), 2);
	TestTrue(
		TEXT("LC4 wide outside detail is bounded before criterion and warning"),
		Wide.OutsideDetailBounds.bIsValid &&
		Wide.OutsideDetailBounds.Min.Equals(FVector2D(390.0f, 556.0f), 0.1f) &&
		Wide.OutsideDetailBounds.Max.Equals(FVector2D(470.0f, 584.0f), 0.1f) &&
		Wide.OutsideDetailFontSize == 8);
	TestEqual(TEXT("LC4 wide empty stub end x"), Wide.UnconnectedStubEndX, 230.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceLayoutSessionTest,
	"BlueprintLens.Editor.LC4SequenceLayoutSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceLayoutSessionTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for session"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	const FBlueprintLensLC4SequenceLayout Oracle =
		FBlueprintLensLC4SequenceLayoutBuilder::Build(Projection, 700.0f);
	const FBlueprintLensLC4FixedLayoutBackend ElkAccepted(
		EBlueprintLensLayoutBackendKind::ElkLayered,
		true,
		Oracle.LayoutLedger);
	const FBlueprintLensLC4FixedLayoutBackend ElkUnavailable(
		EBlueprintLensLayoutBackendKind::ElkLayered,
		false,
		Oracle.LayoutLedger);
	const FBlueprintLensLC4FixedLayoutBackend GraphvizAccepted(
		EBlueprintLensLayoutBackendKind::GraphvizDot,
		true,
		Oracle.LayoutLedger);

	const FBlueprintLensLC4SequenceLayoutSessionResult ElkResult =
		FBlueprintLensLC4SequenceLayoutSession::BuildWithBackends(
			Projection,
			700.0f,
			ElkAccepted,
			GraphvizAccepted);
	TestTrue(TEXT("LC4 accepts complete fidelity-matched ELK ledger"), ElkResult.IsRenderable(Projection));
	TestEqual(TEXT("LC4 ELK is first"), ElkResult.Layout.LayoutLedger.Backend, EBlueprintLensLayoutBackendKind::ElkLayered);
	TestEqual(TEXT("LC4 stops after accepted ELK"), ElkResult.Attempts.Num(), 1);

	const FBlueprintLensLC4SequenceLayoutSessionResult GraphvizResult =
		FBlueprintLensLC4SequenceLayoutSession::BuildWithBackends(
			Projection,
			700.0f,
			ElkUnavailable,
			GraphvizAccepted);
	TestTrue(TEXT("LC4 accepts Graphviz after ELK unavailable"), GraphvizResult.IsRenderable(Projection));
	TestEqual(TEXT("LC4 Graphviz is explicit fallback"), GraphvizResult.Layout.LayoutLedger.Backend, EBlueprintLensLayoutBackendKind::GraphvizDot);
	TestEqual(TEXT("LC4 records ELK and Graphviz attempts"), GraphvizResult.Attempts.Num(), 2);

	FBlueprintLensLayoutLedger Shifted = Oracle.LayoutLedger;
	Shifted.Nodes[0].Position.X += 8.0f;
	const FBlueprintLensLC4FixedLayoutBackend ElkShifted(
		EBlueprintLensLayoutBackendKind::ElkLayered,
		true,
		Shifted);
	const FBlueprintLensLC4FixedLayoutBackend GraphvizShifted(
		EBlueprintLensLayoutBackendKind::GraphvizDot,
		true,
		Shifted);
	const FBlueprintLensLC4SequenceLayoutSessionResult DeterministicResult =
		FBlueprintLensLC4SequenceLayoutSession::BuildWithBackends(
			Projection,
			700.0f,
			ElkShifted,
			GraphvizShifted);
	TestTrue(TEXT("LC4 deterministic fidelity oracle remains renderable"), DeterministicResult.IsRenderable(Projection));
	TestEqual(TEXT("LC4 rejects shifted external ledgers"), DeterministicResult.Layout.LayoutLedger.Backend, EBlueprintLensLayoutBackendKind::Deterministic);
	TestEqual(TEXT("LC4 records full fallback chain"), DeterministicResult.Attempts.Num(), 3);

	const FBlueprintLensLC4SequenceLayoutSessionResult ProductionResult =
		FBlueprintLensLC4SequenceLayoutSession::Build(
			Projection,
			700.0f);
	TestTrue(
		TEXT("LC4 production backend chain remains renderable"),
		ProductionResult.IsRenderable(Projection));
	TestTrue(
		TEXT("LC4 production backend chain records an ELK primary attempt"),
		ProductionResult.Attempts.Num() >= 1 &&
			ProductionResult.Attempts[0].Backend ==
				EBlueprintLensLayoutBackendKind::ElkLayered);
	if (ProductionResult.Attempts.Num() >= 1 &&
		!ProductionResult.Attempts[0].bAccepted)
	{
		TestTrue(
			TEXT("LC4 production backend chain records Graphviz fallback"),
			ProductionResult.Attempts.Num() >= 2 &&
				ProductionResult.Attempts[1].Backend ==
					EBlueprintLensLayoutBackendKind::GraphvizDot);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceCanvasTest,
	"BlueprintLens.Editor.LC4SequenceCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceCanvasTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for canvas"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	for (const TPair<float, FVector2D>& Target : {
		TPair<float, FVector2D>(430.0f, FVector2D(430.0f, 1360.0f)),
		TPair<float, FVector2D>(480.0f, FVector2D(480.0f, 1080.0f)),
		TPair<float, FVector2D>(700.0f, FVector2D(700.0f, 842.0f))})
	{
		const FBlueprintLensLC4SequenceLayoutSessionResult Session =
			FBlueprintLensLC4SequenceLayoutSession::Build(
				Projection,
				Target.Key);
		TestTrue(TEXT("LC4 canvas session is renderable"), Session.IsRenderable(Projection));
		TSharedRef<SBlueprintLensLC4SequenceRail> Canvas =
			SNew(SBlueprintLensLC4SequenceRail)
				.Projection(Projection)
				.InitialSession(Session)
				.SelectedOrdinal(INDEX_NONE)
				.Evidence(false);
		Canvas->SlatePrepass(1.0f);
		const FVector2D DesiredSize = Canvas->GetDesiredSize();
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f canvas desired size is exact"), Target.Key),
			DesiredSize.Equals(Target.Value, 0.1f));
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f canvas retains oracle geometry"), Target.Key),
			Canvas->GetLayoutForTesting().MatchesVisualOracle(1.0f));
		TArray<TSharedRef<SWidget>> CanvasWidgets;
		CollectSlateWidgets(Canvas, CanvasWidgets);
		int32 TextWidgetCount = 0;
		bool bEveryTextIntersectsParentClip = true;
		for (const TSharedRef<SWidget>& Widget : CanvasWidgets)
		{
			if (Widget->GetTypeAsString() == TEXT("STextBlock"))
			{
				++TextWidgetCount;
				bEveryTextIntersectsParentClip &=
					Widget->GetClipping() == EWidgetClipping::ClipToBounds;
			}
		}
		TestTrue(
			FString::Printf(TEXT("LC4 %.0f canvas owns text children"), Target.Key),
			TextWidgetCount > 0);
		TestTrue(
			FString::Printf(
				TEXT("LC4 %.0f text clipping intersects the scroll viewport"),
				Target.Key),
			bEveryTextIntersectsParentClip);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4SequenceActionStateTest,
	"BlueprintLens.Editor.LC4SequenceActionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4SequenceActionStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC4SequenceLoadResult LoadResult =
		FBlueprintLensLC4SequenceProfileLoader::LoadFile(LC4ProfilePath());
	TestTrue(TEXT("LC4 profile loads for action state"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC4SequenceProjection Projection =
		FBlueprintLensLC4SequenceProjector::Build(
			*LoadResult.Profile,
			*LoadResult.ExplanationModel);
	const FBlueprintLensLC4SequenceLayoutSessionResult Session =
		FBlueprintLensLC4SequenceLayoutSession::Build(Projection, 700.0f);
	TestTrue(
		TEXT("LC4 action-state session is renderable"),
		Session.IsRenderable(Projection));

	const TArray<FString> PersistentActionIds = {
		TEXT("select"),
		TEXT("all-text"),
		TEXT("evidence")};
	for (const FString& ActiveActionId : PersistentActionIds)
	{
		TSharedRef<SBlueprintLensLC4SequenceRail> Canvas =
			SNew(SBlueprintLensLC4SequenceRail)
				.Projection(Projection)
				.InitialSession(Session)
				.ActiveActionId(ActiveActionId);
		for (const FString& ActionId : PersistentActionIds)
		{
			TestEqual(
				*FString::Printf(
					TEXT("LC4 %s activity follows detail mode"),
					*ActionId),
				Canvas->IsActionActive(ActionId),
				ActionId == ActiveActionId);
		}
		TestFalse(
			TEXT("LC4 Open source remains a command, not a persistent mode"),
			Canvas->IsActionActive(TEXT("open-source")));
	}
	TSharedRef<SBlueprintLensLC4SequenceRail> CommandCanvas =
		SNew(SBlueprintLensLC4SequenceRail)
			.Projection(Projection)
			.InitialSession(Session)
			.ActiveActionId(TEXT("open-source"));
	TestFalse(
		TEXT("LC4 Open source cannot become a persistent mode"),
		CommandCanvas->IsActionActive(TEXT("open-source")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC3ValueConeStateTest,
	"BlueprintLens.Editor.LC3ValueConeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC3ValueConeStateTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC3FixturePath());
	TestTrue(TEXT("LC3 fixture loads for panel state"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC3ValueConeProjection Before =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC3 panel projection starts renderable"), Before.IsRenderable());
	const FBlueprintLensLC3ValueConeLayoutSessionResult ResponsiveSession =
		FBlueprintLensLC3ValueConeLayoutSession::Build(Before, 700.0f);
	TestTrue(
		TEXT("LC3 responsive session starts renderable"),
		ResponsiveSession.IsRenderable(Before));
	if (ResponsiveSession.IsRenderable(Before))
	{
		TSharedRef<SBlueprintLensLC3ValueConeCanvas> ResponsiveCanvas =
			SNew(SBlueprintLensLC3ValueConeCanvas)
				.Projection(Before)
				.InitialSession(ResponsiveSession)
				.SelectedUnitId(FString())
				.Density(EBlueprintLensLC3ValueConeDensity::Summary);
		ResponsiveCanvas->SlatePrepass(1.0f);
		TestTrue(
			TEXT("LC3 canvas does not force a wide layout into compact viewports"),
			ResponsiveCanvas->GetDesiredSize().X <= 430.0f);
	}

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.Model;
	Panel->ResolveSources();
	Panel->bLC3TechnicalEvidenceExpanded = false;
	Panel->LC3ValueConeDensity = EBlueprintLensLC3ValueConeDensity::Summary;
	Panel->LC3SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	const FString ReaderText = SlateWidgetText(PanelRoot);

	TestTrue(
		TEXT("LC3 reader identifies the derivation Spine"),
		ReaderText.Contains(TEXT("VALUE PROVENANCE ")) &&
			ReaderText.Contains(TEXT("DERIVATION SPINE")) &&
			ReaderText.Contains(TEXT("local Operator Subtree")));
	TestTrue(
		TEXT("LC3 reader exposes Summary and Evidence"),
		ReaderText.Contains(TEXT("Summary")) &&
			ReaderText.Contains(TEXT("Evidence")));
	for (const TCHAR* ReaderLabel : {
		TEXT("Set LC3Score"),
		TEXT("BeginPlay"),
		TEXT("Subtract"),
		TEXT("Add"),
		TEXT("BaseScore"),
		TEXT("BonusScore"),
		TEXT("Penalty")})
	{
		TestTrue(
			FString::Printf(TEXT("Default LC3 reader contains %s"), ReaderLabel),
			ReaderText.Contains(ReaderLabel));
	}
	TestTrue(
		TEXT("LC3 reader separates the controller"),
		ReaderText.Contains(TEXT("thin blue rail")) &&
			ReaderText.Contains(TEXT("supplies no value")));
	TestTrue(
		TEXT("LC3 default hides all qualified endpoint labels"),
		!ReaderText.Contains(TEXT("Add.A")) &&
			!ReaderText.Contains(TEXT("Add.B")) &&
			!ReaderText.Contains(TEXT("Add.ReturnValue")) &&
			!ReaderText.Contains(TEXT("Subtract.A")) &&
			!ReaderText.Contains(TEXT("Subtract.B")) &&
			!ReaderText.Contains(TEXT("Subtract.ReturnValue")));
	for (const FString& Boundary : Before.BoundaryNotices)
	{
		TestTrue(
			FString::Printf(TEXT("LC3 reader retains boundary: %s"), *Boundary),
			ReaderText.Contains(Boundary));
	}
	for (const TCHAR* Forbidden : {
		TEXT("unit."),
		TEXT("relation."),
		TEXT("group."),
		TEXT("::node::"),
		TEXT("SHA-256"),
		TEXT("LC3_VALUE_CONE_")})
	{
		TestFalse(
			FString::Printf(TEXT("Default LC3 reader withholds %s"), Forbidden),
			ReaderText.Contains(Forbidden));
	}
	const TSharedPtr<SWidget> ModeSwitcherBox = FindDeepestSlateContainer(
		PanelRoot,
		TEXT("SBox"),
		{TEXT("LANES"), TEXT("WEAVE"), TEXT("ROUTE")});
	TestTrue(
		TEXT("LC3 mode-switcher wrapper is present for visibility testing"),
		ModeSwitcherBox.IsValid());
	if (ModeSwitcherBox.IsValid())
	{
		TestTrue(
			TEXT("LC3 collapses LC1-era mode controls"),
			ModeSwitcherBox->GetVisibility() == EVisibility::Collapsed);
	}
	TestEqual(
		TEXT("No LC3 source action appears before selection"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected item in Blueprint"))
			.Num(),
		0);

	const FBlueprintLensLC3ValueConeStep* SubtractStep =
		Before.Steps.FindByPredicate(
			[](const FBlueprintLensLC3ValueConeStep& Step)
			{
				return Step.ProducerReaderLabel == TEXT("Subtract_IntInt");
			});
	TestNotNull(TEXT("LC3 projection exposes Subtract operator"), SubtractStep);
	if (SubtractStep == nullptr)
	{
		return false;
	}
	Panel->SelectLC3Unit(SubtractStep->ProducerUnitId);
	const FString SelectedText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Selecting Subtract opens its local subtree"),
		SelectedText.Contains(TEXT("LOCAL OPERATOR SUBTREE")) &&
			SelectedText.Contains(TEXT("Subtract.A")) &&
			SelectedText.Contains(TEXT("Subtract.B")) &&
			SelectedText.Contains(TEXT("Subtract.ReturnValue")));
	TestFalse(
		TEXT("Selected Subtract does not expose Add endpoints"),
		SelectedText.Contains(TEXT("Add.A")) ||
			SelectedText.Contains(TEXT("Add.B")) ||
			SelectedText.Contains(TEXT("Add.ReturnValue")));
	TestEqual(
		TEXT("One LC3 contextual source action follows selection"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected item in Blueprint"))
			.Num(),
		1);
	Panel->SelectLC3Unit(SubtractStep->ProducerUnitId);
	const FString CollapsedText = SlateWidgetText(PanelRoot);
	TestFalse(
		TEXT("Selecting the same operator collapses local endpoints"),
		CollapsedText.Contains(TEXT("Subtract.A")) ||
			CollapsedText.Contains(TEXT("Subtract.B")) ||
			CollapsedText.Contains(TEXT("Subtract.ReturnValue")));
	Panel->SetLC3ValueConeDensity(
		EBlueprintLensLC3ValueConeDensity::Evidence);
	TestEqual(
		TEXT("LC3 evidence density is selected"),
		Panel->LC3ValueConeDensity,
		EBlueprintLensLC3ValueConeDensity::Evidence);
	const FString TechnicalText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("LC3 technical evidence reveals group ID"),
		TechnicalText.Contains(Before.GroupId));
	for (const FString& RelationId : Before.AllRelationIds)
	{
		TestTrue(
			FString::Printf(
				TEXT("LC3 technical evidence reveals relation %s"),
				*RelationId),
			TechnicalText.Contains(RelationId));
	}
	TestTrue(
		TEXT("LC3 technical evidence reveals source digest"),
		TechnicalText.Contains(Before.SourceIrSha256));
	TestTrue(
		TEXT("LC3 technical evidence reveals projection digest"),
		TechnicalText.Contains(Before.ProjectionIntegrityHash));
	TestTrue(
		TEXT("LC3 technical evidence reveals diagnostic"),
		TechnicalText.Contains(Before.DiagnosticCode));
	for (const FBlueprintLensClaimEvidence& Evidence :
		 Before.GroupClaimEvidence)
	{
		TestTrue(
			FString::Printf(
				TEXT("LC3 technical evidence reveals claim component %s"),
				*Evidence.Component),
			TechnicalText.Contains(Evidence.Component));
	}

	const FBlueprintLensLC3ValueConeProjection After =
		FBlueprintLensLC3ValueConeProjector::Build(*LoadResult.Model);
	TestEqual(
		TEXT("LC3 unit ledger is invariant across selection and disclosure"),
		After.AllUnitIds,
		Before.AllUnitIds);
	TestEqual(
		TEXT("LC3 relation ledger is invariant across selection and disclosure"),
		After.AllRelationIds,
		Before.AllRelationIds);
	TestEqual(
		TEXT("LC3 cone ledger is invariant across selection and disclosure"),
		After.ConeUnitIds,
		Before.ConeUnitIds);

	FBlueprintLensExplanationModel MissingGroups = *LoadResult.Model;
	MissingGroups.bHasGroups = false;
	MissingGroups.Groups.Reset();
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(MissingGroups);
	Panel->ResolveSources();
	Panel->bLC3TechnicalEvidenceExpanded = false;
	Panel->LC3SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const FString FallbackText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("LC3 fallback shows all units"),
		FallbackText.Contains(TEXT("Units (7)")));
	TestTrue(
		TEXT("LC3 fallback shows all relations"),
		FallbackText.Contains(TEXT("Relations (6)")));
	TestFalse(
		TEXT("LC3 fallback keeps diagnostics behind disclosure"),
		FallbackText.Contains(TEXT("LC3_VALUE_CONE_GROUP_SET_INVALID")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensRelationEndpointLedgerTest,
	"BlueprintLens.Explanation.RelationEndpointLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensRelationEndpointLedgerTest::RunTest(const FString&)
{
	const FMutatedFixtureLoad Exact = LoadMutatedFixture(
		TEXT("endpoint-ledger-exact"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
		});
	TestTrue(TEXT("Exact ledger setup succeeds"), Exact.bSetupSucceeded);
	TestTrue(TEXT("Exact ledger loads"), Exact.LoadResult.IsSuccess());
	if (Exact.LoadResult.IsSuccess())
	{
		for (const FBlueprintLensRelation& Relation :
			 Exact.LoadResult.Model->Relations)
		{
			TestTrue(
				TEXT("Endpoint ledger presence is retained"),
				Relation.bHasSourceEdgeEndpoints);
			TestEqual(
				TEXT("Endpoint ledger is bijective"),
				Relation.SourceEdgeEndpoints.Num(),
				Relation.SourceEdgeIds.Num());
		}
	}
	else if (Exact.bSetupSucceeded)
	{
		AddError(Exact.LoadResult.Error);
	}

	const auto VerifyRejected =
		[this](
			const TCHAR* Name,
			const TCHAR* ExpectedDiagnostic,
			TFunctionRef<void(TSharedRef<FJsonObject>)> Mutate)
		{
			const FMutatedFixtureLoad Scenario =
				LoadMutatedFixture(Name, Mutate);
			TestTrue(
				*FString::Printf(TEXT("%s setup succeeds"), Name),
				Scenario.bSetupSucceeded);
			if (!Scenario.bSetupSucceeded)
			{
				AddError(Scenario.SetupError);
				return;
			}
			TestFalse(
				*FString::Printf(TEXT("%s is rejected"), Name),
				Scenario.LoadResult.IsSuccess());
			TestTrue(
				*FString::Printf(TEXT("%s diagnostic is stable"), Name),
				Scenario.LoadResult.Error.Contains(ExpectedDiagnostic));
		};

	VerifyRejected(
		TEXT("endpoint-ledger-missing"),
		TEXT("source_edge_endpoints must bijectively match source_edge_ids"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			TArray<TSharedPtr<FJsonValue>> Endpoints =
				Root->GetArrayField(TEXT("relations"))[3]
					->AsObject()
					->GetArrayField(TEXT("source_edge_endpoints"));
			Endpoints.Pop();
			Root->GetArrayField(TEXT("relations"))[3]
				->AsObject()
				->SetArrayField(TEXT("source_edge_endpoints"), MoveTemp(Endpoints));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-duplicate"),
		TEXT("source_edge_endpoints must bijectively match source_edge_ids"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			TArray<TSharedPtr<FJsonValue>> Endpoints =
				Root->GetArrayField(TEXT("relations"))[3]
					->AsObject()
					->GetArrayField(TEXT("source_edge_endpoints"));
			Endpoints[1] = Endpoints[0];
			Root->GetArrayField(TEXT("relations"))[3]
				->AsObject()
				->SetArrayField(TEXT("source_edge_endpoints"), MoveTemp(Endpoints));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-empty"),
		TEXT("source_edge_endpoints must bijectively match source_edge_ids"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("relations"))[0]
				->AsObject()
				->SetArrayField(
					TEXT("source_edge_endpoints"),
					TArray<TSharedPtr<FJsonValue>>());
		});
	VerifyRejected(
		TEXT("endpoint-ledger-dangling-pin"),
		TEXT("endpoint source pin does not resolve in IR: pin.missing"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			Root->GetArrayField(TEXT("relations"))[0]
				->AsObject()
				->GetArrayField(TEXT("source_edge_endpoints"))[0]
				->AsObject()
				->SetStringField(TEXT("source_pin_id"), TEXT("pin.missing"));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-dangling-edge"),
		TEXT("endpoint source edge does not resolve in IR: edge.missing"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			const TSharedPtr<FJsonObject> Relation =
				Root->GetArrayField(TEXT("relations"))[0]->AsObject();
			Relation->SetArrayField(
				TEXT("source_edge_ids"),
				{MakeShared<FJsonValueString>(TEXT("edge.missing"))});
			Relation->GetArrayField(TEXT("source_edge_endpoints"))[0]
				->AsObject()
				->SetStringField(
					TEXT("source_edge_id"), TEXT("edge.missing"));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-swapped-pin"),
		TEXT("endpoint provenance disagrees with IR edge"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			const TSharedPtr<FJsonObject> Endpoint =
				Root->GetArrayField(TEXT("relations"))[0]
					->AsObject()
					->GetArrayField(TEXT("source_edge_endpoints"))[0]
					->AsObject();
			const FString SourcePinId =
				Endpoint->GetStringField(TEXT("source_pin_id"));
			Endpoint->SetStringField(
				TEXT("source_pin_id"),
				Endpoint->GetStringField(TEXT("target_pin_id")));
			Endpoint->SetStringField(TEXT("target_pin_id"), SourcePinId);
		});
	VerifyRejected(
		TEXT("endpoint-ledger-node-mismatch"),
		TEXT("endpoint provenance disagrees with IR edge"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			const TSharedPtr<FJsonObject> Endpoint =
				Root->GetArrayField(TEXT("relations"))[0]
					->AsObject()
					->GetArrayField(TEXT("source_edge_endpoints"))[0]
					->AsObject();
			Endpoint->SetStringField(
				TEXT("source_node_id"),
				Endpoint->GetStringField(TEXT("target_node_id")));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-cross-relation-swap"),
		TEXT("relation source edge disagrees with endpoints"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			const TArray<TSharedPtr<FJsonValue>>& Relations =
				Root->GetArrayField(TEXT("relations"));
			const TSharedPtr<FJsonObject> First = Relations[0]->AsObject();
			const TSharedPtr<FJsonObject> Second = Relations[1]->AsObject();
			TArray<TSharedPtr<FJsonValue>> FirstEdgeIds =
				First->GetArrayField(TEXT("source_edge_ids"));
			TArray<TSharedPtr<FJsonValue>> FirstEndpoints =
				First->GetArrayField(TEXT("source_edge_endpoints"));
			First->SetArrayField(
				TEXT("source_edge_ids"),
				Second->GetArrayField(TEXT("source_edge_ids")));
			First->SetArrayField(
				TEXT("source_edge_endpoints"),
				Second->GetArrayField(TEXT("source_edge_endpoints")));
			Second->SetArrayField(
				TEXT("source_edge_ids"), MoveTemp(FirstEdgeIds));
			Second->SetArrayField(
				TEXT("source_edge_endpoints"), MoveTemp(FirstEndpoints));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-unit-direction-swap"),
		TEXT("relation source edge disagrees with endpoints"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			const TSharedPtr<FJsonObject> Relation =
				Root->GetArrayField(TEXT("relations"))[0]->AsObject();
			const FString SourceUnitId =
				Relation->GetStringField(TEXT("source_unit_id"));
			Relation->SetStringField(
				TEXT("source_unit_id"),
				Relation->GetStringField(TEXT("target_unit_id")));
			Relation->SetStringField(TEXT("target_unit_id"), SourceUnitId);
		});
	VerifyRejected(
		TEXT("endpoint-ledger-edge-kind"),
		TEXT("relation source edge disagrees with endpoints"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			Root->GetArrayField(TEXT("relations"))[0]
				->AsObject()
				->SetStringField(TEXT("kind"), TEXT("provides_value"));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-edited-label"),
		TEXT("endpoint port label disagrees with IR pin"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			Root->GetArrayField(TEXT("relations"))[0]
				->AsObject()
				->GetArrayField(TEXT("source_edge_endpoints"))[0]
				->AsObject()
				->SetStringField(
					TEXT("source_port_label"), TEXT("edited label"));
		});
	VerifyRejected(
		TEXT("endpoint-ledger-ir-hash"),
		TEXT("endpoint IR SHA-256 mismatch"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			Root->GetObjectField(TEXT("source"))
				->SetStringField(TEXT("ir_sha256"), FString::ChrN(64, TEXT('0')));
		});

	const FMutatedFixtureLoad Shuffled = LoadMutatedFixture(
		TEXT("endpoint-ledger-shuffled"),
		[](TSharedRef<FJsonObject> Root)
		{
			AddExactEndpointLedgers(Root);
			TArray<TSharedPtr<FJsonValue>> Relations =
				Root->GetArrayField(TEXT("relations"));
			Algo::Reverse(Relations);
			for (const TSharedPtr<FJsonValue>& RelationValue : Relations)
			{
				const TSharedPtr<FJsonObject> Relation =
					RelationValue->AsObject();
				TArray<TSharedPtr<FJsonValue>> Endpoints =
					Relation->GetArrayField(TEXT("source_edge_endpoints"));
				Algo::Reverse(Endpoints);
				Relation->SetArrayField(
					TEXT("source_edge_endpoints"), MoveTemp(Endpoints));
			}
			Root->SetArrayField(TEXT("relations"), MoveTemp(Relations));
		});
	TestTrue(TEXT("Shuffled ledger setup succeeds"), Shuffled.bSetupSucceeded);
	TestTrue(TEXT("Shuffled ledger loads"), Shuffled.LoadResult.IsSuccess());
	if (!Shuffled.LoadResult.IsSuccess() && Shuffled.bSetupSucceeded)
	{
		AddError(Shuffled.LoadResult.Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensViewContractTest,
	"BlueprintLens.Editor.ViewContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensViewContractTest::RunTest(const FString&)
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	TestTrue(TEXT("Plugin is discoverable"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	FString PanelSource;
	const FString PanelSourcePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/BlueprintLensEditor/Private/SBlueprintLensPanel.cpp"));
	TestTrue(
		TEXT("Panel source is readable"),
		FFileHelper::LoadFileToString(PanelSource, *PanelSourcePath));
	TestTrue(
		TEXT("Plain outline label"),
		PanelSource.Contains(TEXT("PLAIN ORDERED OUTLINE")));
	TestTrue(
		TEXT("Evidence regions label"),
		PanelSource.Contains(TEXT("EVIDENCE-BACKED REGIONS")));
	TestTrue(
		TEXT("Show all control"),
		PanelSource.Contains(TEXT("Show all 14 steps")));
	TestTrue(
		TEXT("Why grouped control"),
		PanelSource.Contains(TEXT("Why grouped?")));
	TestFalse(
		TEXT("No interval candidate"),
		PanelSource.Contains(TEXT("INTERVAL LENS")));
	TestFalse(
		TEXT("No barcode candidate"),
		PanelSource.Contains(TEXT("BARCODE + DETAIL")));

	FString DisclosureHeader;
	const FString DisclosureHeaderPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/BlueprintLensEditor/Private/BlueprintLensLC1Disclosure.h"));
	TestTrue(
		TEXT("Disclosure header is readable"),
		FFileHelper::LoadFileToString(
			DisclosureHeader,
			*DisclosureHeaderPath));
	TestFalse(
		TEXT("Active disclosure enum has no legacy interval value"),
		DisclosureHeader.Contains(TEXT("SemanticIntervalLens")));
	TestFalse(
		TEXT("Active disclosure enum has no legacy barcode value"),
		DisclosureHeader.Contains(TEXT("OverviewBarcodeAdjacentDetail")));

	const FString FixturePath = CanonicalFixturePath();
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(FixturePath);
	TestTrue(TEXT("Canonical fixture loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	const FBlueprintLensExplanationModel& Model = *Result.Model;
	const TCHAR* ExpectedRoles[] = {
		TEXT("criterion"),
		TEXT("control"),
		TEXT("predicate"),
		TEXT("value"),
		TEXT("consequence"),
		TEXT("boundary")
	};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Lane %d role"), Index),
			FString(LexToString(Model.Lanes[Index].Role)),
			FString(ExpectedRoles[Index]));
	}
	TestEqual(
		TEXT("Consequence message"),
		Model.Lanes[4].EmptyMessage,
		FString(TEXT("Not enabled in this backward-only query")));
	TestEqual(
		TEXT("Boundary message"),
		Model.Lanes[5].EmptyMessage,
		FString(TEXT("All selected constructs supported")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensCausalWeaveProjectionTest,
	"BlueprintLens.Explanation.CausalWeaveProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensCausalWeaveProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(CanonicalFixturePath());
	TestTrue(TEXT("Canonical fixture loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	const FBlueprintLensWeaveProjection Projection =
		FBlueprintLensWeaveProjector::Build(*Result.Model);
	TestTrue(TEXT("Canonical Weave projection succeeds"), Projection.IsValid());
	if (!Projection.IsValid())
	{
		AddError(Projection.Error);
		return false;
	}

	TestEqual(
		TEXT("All six units are accounted"),
		Projection.AccountedUnitIds.Num(),
		6);
	TestEqual(
		TEXT("All five relations are accounted"),
		Projection.AccountedRelationIds.Num(),
		5);
	TestEqual(
		TEXT("Three execution stops precede the criterion"),
		Projection.ExecutionUnits.Num(),
		3);
	TestEqual(
		TEXT("Two execution predecessor relations"),
		Projection.ExecutionRelations.Num(),
		2);
	TestEqual(TEXT("One predicate"), Projection.PredicateUnits.Num(), 1);
	TestEqual(TEXT("One criterion value"), Projection.ValueUnits.Num(), 1);
	TestEqual(
		TEXT("Criterion is Set Health"),
		Projection.Criterion->Title,
		FString(TEXT("Set Health")));
	TestEqual(
		TEXT("Execution starts at Event BeginPlay"),
		Projection.ExecutionUnits[0]->Title,
		FString(TEXT("Event BeginPlay")));
	TestEqual(
		TEXT("Execution gate is Branch"),
		Projection.ExecutionUnits.Last()->Title,
		FString(TEXT("Branch")));
	TestTrue(TEXT("All canonical units supported"), Projection.bAllSupported);

	FBlueprintLensExplanationModel Shuffled = *Result.Model;
	Algo::Reverse(Shuffled.Units);
	Algo::Reverse(Shuffled.Relations);
	const FBlueprintLensWeaveProjection ShuffledProjection =
		FBlueprintLensWeaveProjector::Build(Shuffled);
	TestTrue(
		TEXT("Shuffled Weave projection succeeds"),
		ShuffledProjection.IsValid());
	if (!ShuffledProjection.IsValid())
	{
		AddError(ShuffledProjection.Error);
		return false;
	}
	for (int32 Index = 0; Index < Projection.ExecutionUnits.Num(); ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Stable execution stop %d"), Index),
			ShuffledProjection.ExecutionUnits[Index]->Id,
			Projection.ExecutionUnits[Index]->Id);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1FrameFlowLayoutTest,
	"BlueprintLens.FrameFlow.LC1LinearLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1FrameFlowLayoutTest::RunTest(const FString&)
{
	const FBlueprintLensExplanationModel Explanation =
		MakeLinearExplanation();
	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(Explanation);
	TestTrue(TEXT("LC1 layout succeeds"), Layout.IsReady());
	if (!Layout.IsReady())
	{
		AddError(FString::Join(Layout.Diagnostics, TEXT("; ")));
		return false;
	}

	TestEqual(TEXT("Three semantic segments"), Layout.Segments.Num(), 3);
	TestEqual(
		TEXT("Entry segment kind"),
		Layout.Segments[0].Kind,
		EBlueprintLensFrameFlowSegmentKind::Entry);
	TestEqual(
		TEXT("Straight-run segment kind"),
		Layout.Segments[1].Kind,
		EBlueprintLensFrameFlowSegmentKind::StraightRun);
	TestEqual(
		TEXT("Criterion segment kind"),
		Layout.Segments[2].Kind,
		EBlueprintLensFrameFlowSegmentKind::CriterionFocus);
	TestEqual(
		TEXT("Twelve run units"),
		Layout.Segments[1].MemberUnitIds.Num(),
		12);
	TestEqual(
		TEXT("Eleven run-owned relations"),
		Layout.Segments[1].MemberRelationIds.Num(),
		11);
	TestEqual(TEXT("Two segment edges"), Layout.SegmentEdges.Num(), 2);
	TestEqual(TEXT("All fourteen units counted"), Layout.TruthCounts.UnitCount, 14);
	TestEqual(
		TEXT("All thirteen relations counted"),
		Layout.TruthCounts.RelationCount,
		13);
	TestEqual(
		TEXT("All source nodes remain distinct"),
		Layout.TruthCounts.UniqueSourceNodeCount,
		14);

	FBlueprintLensExplanationModel Shuffled = Explanation;
	Algo::Reverse(Shuffled.Units);
	Algo::Reverse(Shuffled.Relations);
	const FBlueprintLensFrameFlowLayoutModel ShuffledLayout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(Shuffled);
	TestTrue(TEXT("Shuffled LC1 layout succeeds"), ShuffledLayout.IsReady());
	if (ShuffledLayout.IsReady())
	{
		for (int32 Index = 0; Index < Layout.Segments.Num(); ++Index)
		{
			TestEqual(
				FString::Printf(TEXT("Stable segment ID %d"), Index),
				ShuffledLayout.Segments[Index].Id,
				Layout.Segments[Index].Id);
			TestEqual(
				FString::Printf(TEXT("Stable membership %d"), Index),
				ShuffledLayout.Segments[Index].MemberUnitIds,
				Layout.Segments[Index].MemberUnitIds);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1FrameFlowDetailTest,
	"BlueprintLens.FrameFlow.LC1DetailWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1FrameFlowDetailTest::RunTest(const FString&)
{
	const FBlueprintLensExplanationModel Explanation =
		MakeLinearExplanation();
	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(Explanation);
	TestTrue(TEXT("LC1 layout succeeds"), Layout.IsReady());
	if (!Layout.IsReady())
	{
		return false;
	}
	const FBlueprintLensFrameFlowSegment& Run = Layout.Segments[1];
	const FBlueprintLensFrameFlowDetailWindow Detail =
		FBlueprintLensFrameFlowLayoutBuilder::BuildDetailWindow(
			Explanation,
			Layout,
			Run.Id,
			Run.MemberUnitIds.Last(),
			5);
	TestTrue(TEXT("LC1 detail succeeds"), Detail.IsValid());
	if (!Detail.IsValid())
	{
		AddError(Detail.Error);
		return false;
	}
	TestEqual(TEXT("Five visible units"), Detail.VisibleUnitIds.Num(), 5);
	TestEqual(
		TEXT("Seven hidden-prefix units"),
		Detail.HiddenPrefixUnitIds.Num(),
		7);
	TestEqual(
		TEXT("No hidden-suffix units"),
		Detail.HiddenSuffixUnitIds.Num(),
		0);
	TestEqual(
		TEXT("Prefix owns crossing relation"),
		Detail.HiddenPrefixRelationIds.Num(),
		7);
	TestEqual(
		TEXT("Visible sources own four relations"),
		Detail.VisibleRelationIds.Num(),
		4);
	TestEqual(
		TEXT("Relation partition is complete"),
		Detail.HiddenPrefixRelationIds.Num() +
			Detail.VisibleRelationIds.Num() +
			Detail.HiddenSuffixRelationIds.Num(),
		Run.MemberRelationIds.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1FrameFlowRejectsOutOfProfileTest,
	"BlueprintLens.FrameFlow.LC1RejectsOutOfProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1FrameFlowRejectsOutOfProfileTest::RunTest(
	const FString&)
{
	FBlueprintLensExplanationModel FanOut = MakeLinearExplanation();
	FBlueprintLensRelation Extra = FanOut.Relations[0];
	Extra.Id = TEXT("relation.fan-out");
	Extra.TargetUnitId = FanOut.Units[2].Id;
	Extra.SourceEdgeIds = {TEXT("source.edge.fan-out")};
	FanOut.Relations.Add(Extra);
	const FBlueprintLensFrameFlowLayoutModel FanOutLayout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(FanOut);
	TestFalse(TEXT("Fan-out is rejected"), FanOutLayout.IsReady());

	FBlueprintLensExplanationModel NonSupported = MakeLinearExplanation();
	NonSupported.Units[6].SemanticStatus =
		EBlueprintLensSemanticStatus::Opaque;
	const FBlueprintLensFrameFlowLayoutModel NonSupportedLayout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(NonSupported);
	TestFalse(
		TEXT("Non-supported run member is not contracted"),
		NonSupportedLayout.IsReady());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensResolveCanonicalSourcesTest,
	"BlueprintLens.Navigation.ResolveCanonicalSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensResolveCanonicalSourcesTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(CanonicalFixturePath());
	TestTrue(TEXT("Canonical fixture loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	const FBlueprintLensExplanationModel& Model = *Result.Model;
	UBlueprint* Blueprint =
		LoadObject<UBlueprint>(nullptr, *Model.Source.BlueprintAssetPath);
	TestNotNull(TEXT("Canonical Blueprint loads"), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}
	const bool bWasPackageDirty = Blueprint->GetOutermost()->IsDirty();
	const FBlueprintLensSourceNavigator Navigator;
	TSet<FString> ResolvedNodeIds;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		for (const FBlueprintLensSourceReference& Reference
			 : Unit.SourceReferences)
		{
			const FBlueprintLensResolvedSource Resolved =
				Navigator.Resolve(Model.Source, Reference);
			TestTrue(
				*FString::Printf(
					TEXT("Resolved %s"), *Reference.SourceNodeId),
				Resolved.State == EBlueprintLensSourceState::Ready
					|| Resolved.State
						== EBlueprintLensSourceState::Unsaved);
			TestTrue(TEXT("Native node exists"), Resolved.Node.IsValid());
			ResolvedNodeIds.Add(Reference.SourceNodeId);
		}
	}
	TestEqual(TEXT("Eight unique native nodes"), ResolvedNodeIds.Num(), 8);
	TestEqual(
		TEXT("Resolution does not dirty the Blueprint package"),
		Blueprint->GetOutermost()->IsDirty(),
		bWasPackageDirty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensSafetySourceStatesTest,
	"BlueprintLens.Navigation.SafetySourceStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensSafetySourceStatesTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult Result =
		FBlueprintLensExplanationLoader::LoadFile(CanonicalFixturePath());
	TestTrue(TEXT("Canonical fixture loads"), Result.IsSuccess());
	if (!Result.IsSuccess())
	{
		AddError(Result.Error);
		return false;
	}

	const FBlueprintLensExplanationModel& Model = *Result.Model;
	const FBlueprintLensSourceNavigator Navigator;

	UBlueprint* Blueprint =
		LoadObject<UBlueprint>(nullptr, *Model.Source.BlueprintAssetPath);
	TestNotNull(TEXT("Canonical Blueprint loads"), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	UPackage* Package = Blueprint->GetOutermost();
	FPackageDirtyFlagRestorer RestoreDirtyFlag{
		Package,
		Package->IsDirty()
	};
	Package->SetDirtyFlag(true);

	FBlueprintLensSource StaleSource = Model.Source;
	StaleSource.BlueprintPackageSha256 = FString::ChrN(64, TEXT('0'));
	const FBlueprintLensResolvedSource Stale =
		Navigator.Resolve(StaleSource, Model.Units[0].SourceReferences[0]);
	TestEqual(
		TEXT("Wrong package hash is stale"),
		Stale.State,
		EBlueprintLensSourceState::Stale);

	FBlueprintLensSourceReference MissingGuid =
		Model.Units[0].SourceReferences[0];
	MissingGuid.NativeNodeGuid =
		TEXT("00000000-0000-0000-0000-000000000000");
	const FBlueprintLensResolvedSource Unresolved =
		Navigator.Resolve(Model.Source, MissingGuid);
	TestEqual(
		TEXT("Missing native GUID is unresolved"),
		Unresolved.State,
		EBlueprintLensSourceState::Unresolved);

	const FBlueprintLensResolvedSource StaleBeforeMissingGuid =
		Navigator.Resolve(StaleSource, MissingGuid);
	TestEqual(
		TEXT("Stale package takes precedence over missing native GUID"),
		StaleBeforeMissingGuid.State,
		EBlueprintLensSourceState::Stale);

	FBlueprintLensSource MissingPackageSource = Model.Source;
	MissingPackageSource.BlueprintAssetPath =
		TEXT("/Game/Probe/BP_BlueprintLensMissing."
			 "BP_BlueprintLensMissing");
	const FString MissingPackageName =
		FPackageName::ObjectPathToPackageName(
			MissingPackageSource.BlueprintAssetPath);
	FString UnexpectedPackageFilename;
	const bool bMissingPackageExists = FPackageName::DoesPackageExist(
		MissingPackageName,
		&UnexpectedPackageFilename);
	TestFalse(
		TEXT("Missing-package fixture package does not exist"),
		bMissingPackageExists);
	if (bMissingPackageExists)
	{
		AddError(FString::Printf(
			TEXT("Cannot cover the missing-package branch because '%s' "
				 "exists at '%s'"),
			*MissingPackageName,
			*UnexpectedPackageFilename));
	}
	else
	{
		const FBlueprintLensResolvedSource MissingPackage =
			Navigator.Resolve(
				MissingPackageSource,
				Model.Units[0].SourceReferences[0]);
		TestEqual(
			TEXT("Missing package is stale"),
			MissingPackage.State,
			EBlueprintLensSourceState::Stale);
	}

	const FBlueprintLensResolvedSource Unsaved = Navigator.Resolve(
		Model.Source,
		Model.Units[0].SourceReferences[0]);
	TestEqual(
		TEXT("Dirty package is unsaved"),
		Unsaved.State,
		EBlueprintLensSourceState::Unsaved);
	TestTrue(
		TEXT("Unsaved result retains the native node"),
		Unsaved.Node.IsValid());

	Package->SetDirtyFlag(false);
	const FBlueprintLensResolvedSource Ready = Navigator.Resolve(
		Model.Source,
		Model.Units[0].SourceReferences[0]);
	TestEqual(
		TEXT("Clean valid package is ready"),
		Ready.State,
		EBlueprintLensSourceState::Ready);
	TestTrue(
		TEXT("Ready result retains the native node"),
		Ready.Node.IsValid());

	const FMutatedFixtureLoad StaleScenario = LoadMutatedFixture(
		TEXT("stale-package-hash"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetObjectField(TEXT("source"))
				->SetStringField(
					TEXT("blueprint_package_sha256"),
					FString::ChrN(64, TEXT('0')));
		},
		true);
	TestTrue(
		TEXT("Stale scenario is retained"),
		StaleScenario.bSetupSucceeded);
	if (!StaleScenario.bSetupSucceeded)
	{
		AddError(StaleScenario.SetupError);
	}
	else
	{
		TestTrue(
			TEXT("Stale scenario remains structurally valid"),
			StaleScenario.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad UnresolvedScenario = LoadMutatedFixture(
		TEXT("unresolved-node-guid"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("units"))[0]
				->AsObject()
				->GetArrayField(TEXT("source_references"))[0]
				->AsObject()
				->SetStringField(
					TEXT("native_node_guid"),
					TEXT("00000000-0000-0000-0000-000000000000"));
		},
		true);
	TestTrue(
		TEXT("Unresolved scenario is retained"),
		UnresolvedScenario.bSetupSucceeded);
	if (!UnresolvedScenario.bSetupSucceeded)
	{
		AddError(UnresolvedScenario.SetupError);
	}
	else
	{
		TestTrue(
			TEXT("Unresolved scenario remains structurally valid"),
			UnresolvedScenario.LoadResult.IsSuccess());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensMalformedFixtureTest,
	"BlueprintLens.Explanation.RejectsMalformedFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensMalformedFixtureTest::RunTest(const FString&)
{
	const FMutatedFixtureLoad UnknownRole = LoadMutatedFixture(
		TEXT("malformed-unknown-role"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("lanes"))[0]
				->AsObject()
				->SetStringField(TEXT("role"), TEXT("mystery"));
		},
		true);
	TestTrue(
		TEXT("Unknown role mutation setup succeeds"),
		UnknownRole.bSetupSucceeded);
	if (!UnknownRole.bSetupSucceeded)
	{
		AddError(UnknownRole.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Unknown role is rejected"),
			UnknownRole.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad PopulatedLaneState = LoadMutatedFixture(
		TEXT("populated-lane-state"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("lanes"))[0]
				->AsObject()
				->SetStringField(TEXT("state"), TEXT("empty"));
		});
	TestTrue(
		TEXT("Populated lane state mutation setup succeeds"),
		PopulatedLaneState.bSetupSucceeded);
	if (!PopulatedLaneState.bSetupSucceeded)
	{
		AddError(PopulatedLaneState.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Populated lane state change is rejected"),
			PopulatedLaneState.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad ConsequenceLaneState = LoadMutatedFixture(
		TEXT("consequence-lane-state"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("lanes"))[4]
				->AsObject()
				->SetStringField(TEXT("state"), TEXT("empty"));
		});
	TestTrue(
		TEXT("Consequence lane state mutation setup succeeds"),
		ConsequenceLaneState.bSetupSucceeded);
	if (!ConsequenceLaneState.bSetupSucceeded)
	{
		AddError(ConsequenceLaneState.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Consequence lane state change is rejected"),
			ConsequenceLaneState.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad BoundaryLaneOccupancy = LoadMutatedFixture(
		TEXT("boundary-lane-occupancy"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>& Lanes =
				Root->GetArrayField(TEXT("lanes"));
			TArray<TSharedPtr<FJsonValue>> ControlUnitIds =
				Lanes[1]->AsObject()->GetArrayField(TEXT("unit_ids"));
			TArray<TSharedPtr<FJsonValue>> BoundaryUnitIds =
				Lanes[5]->AsObject()->GetArrayField(TEXT("unit_ids"));
			const FString MovedUnitId = ControlUnitIds[0]->AsString();
			ControlUnitIds.RemoveAt(0);
			BoundaryUnitIds.Add(
				MakeShared<FJsonValueString>(MovedUnitId));
			Lanes[1]->AsObject()->SetArrayField(
				TEXT("unit_ids"),
				MoveTemp(ControlUnitIds));
			Lanes[5]->AsObject()->SetArrayField(
				TEXT("unit_ids"),
				MoveTemp(BoundaryUnitIds));

			for (const TSharedPtr<FJsonValue>& UnitValue :
				 Root->GetArrayField(TEXT("units")))
			{
				const TSharedPtr<FJsonObject> Unit = UnitValue->AsObject();
				if (Unit->GetStringField(TEXT("id")) == MovedUnitId)
				{
					Unit->SetStringField(TEXT("role"), TEXT("boundary"));
					break;
				}
			}
		});
	TestTrue(
		TEXT("Boundary lane occupancy mutation setup succeeds"),
		BoundaryLaneOccupancy.bSetupSucceeded);
	if (!BoundaryLaneOccupancy.bSetupSucceeded)
	{
		AddError(BoundaryLaneOccupancy.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Boundary lane occupancy change is rejected"),
			BoundaryLaneOccupancy.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad DanglingRelation = LoadMutatedFixture(
		TEXT("dangling-relation"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->GetArrayField(TEXT("relations"))[0]
				->AsObject()
				->SetStringField(
					TEXT("target_unit_id"),
					TEXT("unit.missing"));
		});
	TestTrue(
		TEXT("Dangling relation mutation setup succeeds"),
		DanglingRelation.bSetupSucceeded);
	if (!DanglingRelation.bSetupSucceeded)
	{
		AddError(DanglingRelation.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Dangling relation endpoint is rejected"),
			DanglingRelation.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad DuplicateSourceNode = LoadMutatedFixture(
		TEXT("duplicate-source-node"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>& Units =
				Root->GetArrayField(TEXT("units"));
			TArray<TSharedPtr<FJsonValue>> References =
				Units[1]
					->AsObject()
					->GetArrayField(TEXT("source_references"));
			References.Add(
				Units[0]
					->AsObject()
					->GetArrayField(TEXT("source_references"))[0]);
			Units[1]
				->AsObject()
				->SetArrayField(
					TEXT("source_references"),
					MoveTemp(References));
		});
	TestTrue(
		TEXT("Duplicate source node mutation setup succeeds"),
		DuplicateSourceNode.bSetupSucceeded);
	if (!DuplicateSourceNode.bSetupSucceeded)
	{
		AddError(DuplicateSourceNode.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Duplicate source node ownership is rejected"),
			DuplicateSourceNode.LoadResult.IsSuccess());
	}

	const FMutatedFixtureLoad DuplicateSourceEdge = LoadMutatedFixture(
		TEXT("duplicate-source-edge"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>& Relations =
				Root->GetArrayField(TEXT("relations"));
			TArray<TSharedPtr<FJsonValue>> EdgeIds =
				Relations[1]
					->AsObject()
					->GetArrayField(TEXT("source_edge_ids"));
			EdgeIds.Add(
				Relations[0]
					->AsObject()
					->GetArrayField(TEXT("source_edge_ids"))[0]);
			Relations[1]
				->AsObject()
				->SetArrayField(
					TEXT("source_edge_ids"),
					MoveTemp(EdgeIds));
		});
	TestTrue(
		TEXT("Duplicate source edge mutation setup succeeds"),
		DuplicateSourceEdge.bSetupSucceeded);
	if (!DuplicateSourceEdge.bSetupSucceeded)
	{
		AddError(DuplicateSourceEdge.SetupError);
	}
	else
	{
		TestFalse(
			TEXT("Duplicate source edge ownership is rejected"),
			DuplicateSourceEdge.LoadResult.IsSuccess());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1TypedIrFactsTest,
	"BlueprintLens.Explanation.LC1TypedIrFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1TypedIrFactsTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("Canonical LC1 Explanation loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC1TypedIrFacts Facts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(
			LoadResult.Model->Source);
	TestTrue(TEXT("Typed IR facts load"), Facts.IsValid());
	if (!Facts.IsValid())
	{
		AddError(Facts.Error);
		return false;
	}
	TestEqual(
		TEXT("Thirteen source-faithful variable-set facts"),
		Facts.OperationsBySourceNodeId.Num(),
		13);
	int32 StepFactCount = 0;
	int32 CriterionFactCount = 0;
	for (const TPair<FString, FBlueprintLensLC1OperationFact>& Pair :
		 Facts.OperationsBySourceNodeId)
	{
		TestEqual(
			TEXT("Source node key"),
			Pair.Key,
			Pair.Value.SourceNodeId);
		TestEqual(
			TEXT("Operation class"),
			Pair.Value.OperationClass,
			TEXT("/Script/BlueprintGraph.K2Node_VariableSet"));
		TestEqual(TEXT("Value type"), Pair.Value.ValueType, TEXT("bool"));
		TestEqual(TEXT("Literal value"), Pair.Value.LiteralValue, TEXT("true"));
		if (Pair.Value.VariableTarget.StartsWith(TEXT("LC1Step"))
			&& Pair.Value.VariableTarget.EndsWith(TEXT("Complete")))
		{
			++StepFactCount;
		}
		else if (Pair.Value.VariableTarget == TEXT("LC1Ready"))
		{
			++CriterionFactCount;
		}
	}
	TestEqual(TEXT("Twelve run-member facts"), StepFactCount, 12);
	TestEqual(TEXT("One criterion fact"), CriterionFactCount, 1);
	TestEqual(
		TEXT("Verified hash"),
		Facts.VerifiedIrSha256,
		LoadResult.Model->Source.IrSha256);

	FString CanonicalIrJson;
	TestTrue(
		TEXT("Canonical LC1 typed IR is readable"),
		FFileHelper::LoadFileToString(
			CanonicalIrJson,
			*LoadResult.Model->Source.IrPath));
	if (CanonicalIrJson.IsEmpty())
	{
		return false;
	}

	const FBlueprintLensLC1TypedIrFacts HashMismatch =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			CanonicalIrJson,
			FString::ChrN(64, TEXT('0')));
	TestEqual(
		TEXT("Hash mismatch"),
		HashMismatch.Error,
		TEXT("LC1_IR_HASH_MISMATCH"));

	FString MissingClassJson;
	FString MissingClassSha256;
	const bool bRemovedClass = MutateLC1IrJson(
		CanonicalIrJson,
		[](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			if (!Node.IsValid() || !Node->HasField(TEXT("class")))
			{
				return false;
			}
			Node->RemoveField(TEXT("class"));
			return true;
		},
		MissingClassJson,
		MissingClassSha256);
	TestTrue(TEXT("Missing-class mutation succeeds"), bRemovedClass);
	if (!bRemovedClass)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts MissingClass =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			MissingClassJson,
			MissingClassSha256);
	TestEqual(
		TEXT("Missing class"),
		MissingClass.Error,
		TEXT("LC1_IR_NODE_CLASS_MISSING"));

	FString MissingValuePinJson;
	FString MissingValuePinSha256;
	const bool bChangedPinRole = MutateLC1IrJson(
		CanonicalIrJson,
		[](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			if (!Node.IsValid())
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!Node->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
			{
				const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
				FString PinRole;
				if (Pin.IsValid()
					&& Pin->TryGetStringField(TEXT("pin_role"), PinRole)
					&& PinRole == TEXT("variable_set_value"))
				{
					Pin->SetStringField(TEXT("pin_role"), TEXT("none"));
					return true;
				}
			}
			return false;
		},
		MissingValuePinJson,
		MissingValuePinSha256);
	TestTrue(TEXT("Missing-value-pin mutation succeeds"), bChangedPinRole);
	if (!bChangedPinRole)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts MissingValuePin =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			MissingValuePinJson,
			MissingValuePinSha256);
	TestEqual(
		TEXT("Missing value pin"),
		MissingValuePin.Error,
		TEXT("LC1_IR_AMBIGUOUS_VALUE_PIN"));

	FString DuplicateValuePinJson;
	FString DuplicateValuePinSha256;
	const bool bDuplicatedPin = MutateLC1IrJson(
		CanonicalIrJson,
		[](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			if (!Node.IsValid())
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* ExistingPins = nullptr;
			if (!Node->TryGetArrayField(TEXT("pins"), ExistingPins)
				|| ExistingPins == nullptr)
			{
				return false;
			}
			TArray<TSharedPtr<FJsonValue>> Pins = *ExistingPins;
			for (const TSharedPtr<FJsonValue>& PinValue : *ExistingPins)
			{
				const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
				FString PinRole;
				if (Pin.IsValid()
					&& Pin->TryGetStringField(TEXT("pin_role"), PinRole)
					&& PinRole == TEXT("variable_set_value"))
				{
					Pins.Add(MakeShared<FJsonValueObject>(Pin));
					Node->SetArrayField(TEXT("pins"), MoveTemp(Pins));
					return true;
				}
			}
			return false;
		},
		DuplicateValuePinJson,
		DuplicateValuePinSha256);
	TestTrue(TEXT("Duplicate-value-pin mutation succeeds"), bDuplicatedPin);
	if (!bDuplicatedPin)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts DuplicateValuePin =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			DuplicateValuePinJson,
			DuplicateValuePinSha256);
	TestEqual(
		TEXT("Duplicate value pin"),
		DuplicateValuePin.Error,
		TEXT("LC1_IR_AMBIGUOUS_VALUE_PIN"));

	const FString CanonicalIrSha256 = Sha256ForJsonText(CanonicalIrJson);
	const FBlueprintLensLC1TypedIrFacts CanonicalJsonFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			CanonicalIrJson,
			CanonicalIrSha256);
	TestTrue(
		TEXT("Canonical JSON facts load for regression comparisons"),
		CanonicalJsonFacts.IsValid());
	if (!CanonicalJsonFacts.IsValid())
	{
		return false;
	}

	FString EmptyTargetNodeId;
	FString EmptyTargetJson;
	FString EmptyTargetSha256;
	const bool bEmptiedTarget = MutateLC1IrJson(
		CanonicalIrJson,
		[&EmptyTargetNodeId](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			const TSharedPtr<FJsonObject> ValuePin =
				FindVariableSetValuePin(Node);
			if (!Node.IsValid() || !ValuePin.IsValid()
				|| !Node->TryGetStringField(
					TEXT("id"),
					EmptyTargetNodeId))
			{
				return false;
			}
			ValuePin->SetStringField(TEXT("name"), FString());
			return true;
		},
		EmptyTargetJson,
		EmptyTargetSha256);
	TestTrue(TEXT("Empty-target mutation succeeds"), bEmptiedTarget);
	if (!bEmptiedTarget)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts EmptyTargetFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			EmptyTargetJson,
			EmptyTargetSha256);
	TestTrue(
		*FString::Printf(
			TEXT("Empty target remains valid (error: %s)"),
			*EmptyTargetFacts.Error),
		EmptyTargetFacts.IsValid());
	if (EmptyTargetFacts.IsValid())
	{
		const FBlueprintLensLC1OperationFact* EmptyTargetFact =
			EmptyTargetFacts.OperationsBySourceNodeId.Find(
				EmptyTargetNodeId);
		TestNotNull(TEXT("Empty-target fact is retained"), EmptyTargetFact);
		if (EmptyTargetFact != nullptr)
		{
			TestTrue(
				TEXT("Empty target is preserved"),
				EmptyTargetFact->VariableTarget.IsEmpty());
		}
	}

	FString EmptyTypeNodeId;
	FString EmptyTypeJson;
	FString EmptyTypeSha256;
	const bool bEmptiedType = MutateLC1IrJson(
		CanonicalIrJson,
		[&EmptyTypeNodeId](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			const TSharedPtr<FJsonObject> ValuePin =
				FindVariableSetValuePin(Node);
			const TSharedPtr<FJsonObject>* Type = nullptr;
			if (!Node.IsValid() || !ValuePin.IsValid()
				|| !Node->TryGetStringField(
					TEXT("id"),
					EmptyTypeNodeId)
				|| !ValuePin->TryGetObjectField(TEXT("type"), Type)
				|| Type == nullptr || !Type->IsValid())
			{
				return false;
			}
			(*Type)->SetStringField(TEXT("category"), FString());
			return true;
		},
		EmptyTypeJson,
		EmptyTypeSha256);
	TestTrue(TEXT("Empty-type mutation succeeds"), bEmptiedType);
	if (!bEmptiedType)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts EmptyTypeFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			EmptyTypeJson,
			EmptyTypeSha256);
	TestTrue(
		*FString::Printf(
			TEXT("Empty type remains valid (error: %s)"),
			*EmptyTypeFacts.Error),
		EmptyTypeFacts.IsValid());
	if (EmptyTypeFacts.IsValid())
	{
		const FBlueprintLensLC1OperationFact* EmptyTypeFact =
			EmptyTypeFacts.OperationsBySourceNodeId.Find(EmptyTypeNodeId);
		TestNotNull(TEXT("Empty-type fact is retained"), EmptyTypeFact);
		if (EmptyTypeFact != nullptr)
		{
			TestTrue(
				TEXT("Empty type is preserved"),
				EmptyTypeFact->ValueType.IsEmpty());
		}
	}

	FString EmptyLiteralNodeId;
	FString EmptyLiteralJson;
	FString EmptyLiteralSha256;
	const bool bEmptiedLiteral = MutateLC1IrJson(
		CanonicalIrJson,
		[&EmptyLiteralNodeId](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			const TSharedPtr<FJsonObject> ValuePin =
				FindVariableSetValuePin(Node);
			const TSharedPtr<FJsonObject>* Default = nullptr;
			if (!Node.IsValid() || !ValuePin.IsValid()
				|| !Node->TryGetStringField(
					TEXT("id"),
					EmptyLiteralNodeId)
				|| !ValuePin->TryGetObjectField(TEXT("default"), Default)
				|| Default == nullptr || !Default->IsValid())
			{
				return false;
			}
			(*Default)->SetStringField(TEXT("value"), FString());
			return true;
		},
		EmptyLiteralJson,
		EmptyLiteralSha256);
	TestTrue(TEXT("Empty-literal mutation succeeds"), bEmptiedLiteral);
	if (!bEmptiedLiteral)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts EmptyLiteralFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			EmptyLiteralJson,
			EmptyLiteralSha256);
	TestTrue(
		*FString::Printf(
			TEXT("Empty literal remains valid (error: %s)"),
			*EmptyLiteralFacts.Error),
		EmptyLiteralFacts.IsValid());
	if (EmptyLiteralFacts.IsValid())
	{
		const FBlueprintLensLC1OperationFact* EmptyLiteralFact =
			EmptyLiteralFacts.OperationsBySourceNodeId.Find(
				EmptyLiteralNodeId);
		TestNotNull(TEXT("Empty-literal fact is retained"), EmptyLiteralFact);
		if (EmptyLiteralFact != nullptr)
		{
			TestTrue(
				TEXT("Empty literal is preserved"),
				EmptyLiteralFact->LiteralValue.IsEmpty());
		}
	}

	FString TitleMutatedJson;
	FString TitleMutatedSha256;
	const bool bMutatedTitle = MutateLC1IrJson(
		CanonicalIrJson,
		[](const TSharedRef<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Node =
				FindLC1StepVariableSetNode(Root);
			if (!Node.IsValid())
			{
				return false;
			}
			Node->SetStringField(
				TEXT("title"),
				TEXT("A title that must not affect extracted facts"));
			return true;
		},
		TitleMutatedJson,
		TitleMutatedSha256);
	TestTrue(TEXT("Title mutation succeeds"), bMutatedTitle);
	if (!bMutatedTitle)
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts TitleMutatedFacts =
		FBlueprintLensLC1TypedIrFactLoader::LoadJson(
			TitleMutatedJson,
			TitleMutatedSha256);
	TestTrue(
		TEXT("Title-mutated facts load"),
		TitleMutatedFacts.IsValid());
	if (!TitleMutatedFacts.IsValid())
	{
		AddError(TitleMutatedFacts.Error);
		return false;
	}
	TestEqual(
		TEXT("Title mutation preserves fact count"),
		TitleMutatedFacts.OperationsBySourceNodeId.Num(),
		CanonicalJsonFacts.OperationsBySourceNodeId.Num());
	for (const TPair<FString, FBlueprintLensLC1OperationFact>& Pair :
		 CanonicalJsonFacts.OperationsBySourceNodeId)
	{
		const FBlueprintLensLC1OperationFact* MutatedFact =
			TitleMutatedFacts.OperationsBySourceNodeId.Find(Pair.Key);
		TestNotNull(TEXT("Title mutation preserves fact identity"), MutatedFact);
		if (MutatedFact == nullptr)
		{
			continue;
		}
		TestEqual(
			TEXT("Title mutation preserves source node"),
			MutatedFact->SourceNodeId,
			Pair.Value.SourceNodeId);
		TestEqual(
			TEXT("Title mutation preserves class"),
			MutatedFact->OperationClass,
			Pair.Value.OperationClass);
		TestEqual(
			TEXT("Title mutation preserves value pin"),
			MutatedFact->ValuePinId,
			Pair.Value.ValuePinId);
		TestEqual(
			TEXT("Title mutation preserves target"),
			MutatedFact->VariableTarget,
			Pair.Value.VariableTarget);
		TestEqual(
			TEXT("Title mutation preserves type"),
			MutatedFact->ValueType,
			Pair.Value.ValueType);
		TestEqual(
			TEXT("Title mutation preserves literal"),
			MutatedFact->LiteralValue,
			Pair.Value.LiteralValue);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1PseudocodeProjectionTest,
	"BlueprintLens.Explanation.LC1Pseudocode.ProjectionAndAbstention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1PseudocodeProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("Canonical LC1 Explanation loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		return false;
	}
	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(*LoadResult.Model);
	const FBlueprintLensLC1TypedIrFacts Facts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(
			LoadResult.Model->Source);
	TestTrue(TEXT("LC1 typed facts load"), Facts.IsValid());
	TestEqual(TEXT("Typed IR node ledger"), Facts.NodesBySourceNodeId.Num(), 17);
	if (!Layout.IsReady() || !Facts.IsValid())
	{
		return false;
	}

	const FBlueprintLensLC1PseudocodeProjection Projection =
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	TestTrue(TEXT("Pseudocode is renderable"), Projection.IsRenderable());
	TestTrue(TEXT("Pseudocode integrity validates"), Projection.HasValidIntegrity());
	TestEqual(TEXT("Pseudocode lines"), Projection.Lines.Num(), 14);
	if (Projection.Lines.Num() != 14)
	{
		return false;
	}
	TestEqual(TEXT("Entry text"), Projection.Lines[0].CodeText, TEXT("event BeginPlay"));
	TestEqual(
		TEXT("First assignment text"),
		Projection.Lines[1].CodeText,
		TEXT("    LC1Step01Complete = true;"));
	TestEqual(
		TEXT("Criterion assignment text"),
		Projection.Lines.Last().CodeText,
		TEXT("    LC1Ready = true;"));
	TestEqual(
		TEXT("Criterion role"),
		Projection.Lines.Last().Role,
		EBlueprintLensRole::Criterion);
	TestTrue(
		TEXT("Final line owns no fabricated following relation"),
		Projection.Lines.Last().FollowingRelationId.IsEmpty());

	TSet<FString> Units;
	TSet<FString> Relations;
	TSet<FString> Sources;
	for (const FBlueprintLensLC1PseudocodeLine& Line : Projection.Lines)
	{
		Units.Add(Line.UnitId);
		Sources.Add(Line.SourceNodeId);
		if (!Line.FollowingRelationId.IsEmpty())
		{
			Relations.Add(Line.FollowingRelationId);
		}
		TestFalse(TEXT("Line fact owner exists"), Line.FactOwner.IsEmpty());
	}
	TestEqual(TEXT("All units mapped exactly once"), Units.Num(), 14);
	TestEqual(TEXT("All relations mapped exactly once"), Relations.Num(), 13);
	TestEqual(TEXT("All sources mapped exactly once"), Sources.Num(), 14);

	const FString EntrySource = Projection.Lines[0].SourceNodeId;
	const FString FirstAssignmentSource = Projection.Lines[1].SourceNodeId;
	FBlueprintLensLC1TypedIrFacts BrokenEntry = Facts;
	BrokenEntry.NodesBySourceNodeId.FindChecked(EntrySource).NativeTitle =
		TEXT("Different event");
	TestFalse(
		TEXT("Unknown entry abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			BrokenEntry)
			.IsRenderable());

	FBlueprintLensLC1TypedIrFacts WrongTarget = Facts;
	WrongTarget.OperationsBySourceNodeId.FindChecked(FirstAssignmentSource)
		.VariableTarget = TEXT("InventedTarget");
	TestFalse(
		TEXT("Unexpected target abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			WrongTarget)
			.IsRenderable());

	FBlueprintLensLC1TypedIrFacts WrongLiteral = Facts;
	WrongLiteral.OperationsBySourceNodeId.FindChecked(FirstAssignmentSource)
		.LiteralValue = TEXT("false");
	TestFalse(
		TEXT("Unexpected literal abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			WrongLiteral)
			.IsRenderable());

	FBlueprintLensLC1TypedIrFacts WrongClass = Facts;
	WrongClass.OperationsBySourceNodeId.FindChecked(FirstAssignmentSource)
		.OperationClass = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
	TestFalse(
		TEXT("Unexpected operation class abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			WrongClass)
			.IsRenderable());

	FBlueprintLensLC1TypedIrFacts UnboundFacts = Facts;
	UnboundFacts.VerifiedIrSha256 = FString::ChrN(64, TEXT('0'));
	TestFalse(
		TEXT("Unbound IR abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			UnboundFacts)
			.IsRenderable());

	FBlueprintLensExplanationModel BrokenRelation = *LoadResult.Model;
	BrokenRelation.Relations[0].TargetUnitId =
		BrokenRelation.Relations[1].TargetUnitId;
	TestFalse(
		TEXT("Broken relation ownership abstains"),
		FBlueprintLensLC1PseudocodeProjector::Build(
			BrokenRelation,
			Layout,
			Facts)
			.IsRenderable());

	FBlueprintLensExplanationModel ShuffledExplanation = *LoadResult.Model;
	Algo::Reverse(ShuffledExplanation.Units);
	Algo::Reverse(ShuffledExplanation.Relations);
	const FBlueprintLensFrameFlowLayoutModel ShuffledLayout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
			ShuffledExplanation);
	const FBlueprintLensLC1PseudocodeProjection Shuffled =
		FBlueprintLensLC1PseudocodeProjector::Build(
			ShuffledExplanation,
			ShuffledLayout,
			Facts);
	TestEqual(
		TEXT("Shuffled serialization is deterministic"),
		CanonicalPseudocodeProjection(Shuffled),
		CanonicalPseudocodeProjection(Projection));

	FBlueprintLensLC1PseudocodeProjection Tampered = Projection;
	Tampered.Lines[1].CodeText = TEXT("    invented = true;");
	TestFalse(TEXT("Tampered line fails integrity"), Tampered.HasValidIntegrity());

	const FBlueprintLensLC1RegionProjection Region =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	const FBlueprintLensLC1DisclosureProjection Paired =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PairedPseudocode,
			FString(),
			3,
			Region,
			Projection);
	TestTrue(TEXT("Paired disclosure is valid"), Paired.IsValid());
	TestEqual(TEXT("Paired unit parity"), Paired.DisplayedUnitIds.Num(), 14);
	TestEqual(TEXT("Paired relation parity"), Paired.DisplayedRelationIds.Num(), 13);
	TestEqual(TEXT("Paired source parity"), Paired.SourceActionUnitIds.Num(), 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RegionProjectionTest,
	"BlueprintLens.Explanation.LC1Region.ProjectionAndFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RegionProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("Canonical LC1 Explanation loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
			*LoadResult.Model);
	const FBlueprintLensLC1TypedIrFacts Facts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(
			LoadResult.Model->Source);
	TestTrue(TEXT("Canonical LC1 layout is ready"), Layout.IsReady());
	TestTrue(TEXT("Canonical LC1 typed facts load"), Facts.IsValid());
	if (!Layout.IsReady() || !Facts.IsValid())
	{
		return false;
	}

	const FBlueprintLensLC1RegionProjection Region =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	TestEqual(
		TEXT("Complete status"),
		Region.Status,
		EBlueprintLensLC1RegionProjectionStatus::CompleteOperationRegion);
	TestTrue(TEXT("Complete region is renderable"), Region.IsRenderable());
	TestEqual(
		TEXT("Region kind"),
		Region.RegionKind,
		TEXT("operation_region"));
	TestEqual(
		TEXT("Template"),
		Region.SummaryTemplateId,
		TEXT("set_completion_flags_true_in_sequence"));
	TestEqual(TEXT("Members"), Region.OrderedMemberUnitIds.Num(), 12);
	TestEqual(TEXT("Internal relations"), Region.InternalRelationIds.Num(), 11);
	TestEqual(TEXT("Incoming relations"), Region.IncomingRelationIds.Num(), 1);
	TestEqual(TEXT("Outgoing relations"), Region.OutgoingRelationIds.Num(), 1);
	TestEqual(TEXT("Ledger entries"), Region.ClaimEvidence.Num(), 5);
	TestEqual(
		TEXT("Region source digest binds to Explanation"),
		Region.SourceIrSha256,
		LoadResult.Model->Source.IrSha256);
	TestFalse(
		TEXT("Projector version marker is present"),
		Region.ProjectorVersion.IsEmpty());
	TestFalse(
		TEXT("Projection integrity hash is present"),
		Region.ProjectionIntegrityHash.IsEmpty());
	TestTrue(
		TEXT("Canonical projection integrity validates"),
		Region.HasValidIntegrity());
	TestEqual(TEXT("Template argument count"), Region.SummaryArguments.Num(), 4);
	if (Region.SummaryArguments.Num() == 4)
	{
		TestEqual(TEXT("Summary count"), Region.SummaryArguments[0], TEXT("12"));
		TestEqual(
			TEXT("Summary first target"),
			Region.SummaryArguments[1],
			TEXT("LC1Step01Complete"));
		TestEqual(
			TEXT("Summary last target"),
			Region.SummaryArguments[2],
			TEXT("LC1Step12Complete"));
		TestEqual(
			TEXT("Summary literal"),
			Region.SummaryArguments[3],
			TEXT("true"));
	}
	const TArray<FString> ExpectedClaimParts = {
		TEXT("operation"),
		TEXT("count"),
		TEXT("target_family"),
		TEXT("literal_value"),
		TEXT("sequence")};
	for (int32 Index = 0;
		 Index < ExpectedClaimParts.Num()
			&& Index < Region.ClaimEvidence.Num();
		 ++Index)
	{
		TestEqual(
			TEXT("Complete ledger order"),
			Region.ClaimEvidence[Index].ClaimPart,
			ExpectedClaimParts[Index]);
		TestFalse(
			TEXT("Complete ledger fact owner is explicit"),
			Region.ClaimEvidence[Index].FactOwner.IsEmpty());
		TestFalse(
			TEXT("Complete ledger source is explicit"),
			Region.ClaimEvidence[Index].SourceId.IsEmpty());
		TestFalse(
			TEXT("Complete ledger value is source-faithful"),
			Region.ClaimEvidence[Index].Value.IsEmpty());
	}

	const FString FirstRunUnitId = Layout.Segments[1].MemberUnitIds[0];
	const FBlueprintLensUnit* FirstRunUnit =
		LoadResult.Model->FindUnit(FirstRunUnitId);
	TestNotNull(TEXT("First run member exists"), FirstRunUnit);
	if (FirstRunUnit == nullptr || FirstRunUnit->SourceReferences.IsEmpty())
	{
		return false;
	}
	const FString FirstRunSourceId =
		FirstRunUnit->SourceReferences[0].SourceNodeId;

	FBlueprintLensExplanationModel UnsupportedExplanation =
		*LoadResult.Model;
	for (FBlueprintLensUnit& Unit : UnsupportedExplanation.Units)
	{
		if (Unit.Id == FirstRunUnitId)
		{
			Unit.SemanticStatus =
				EBlueprintLensSemanticStatus::Unsupported;
		}
	}
	const FBlueprintLensLC1RegionProjection UnsupportedRegion =
		FBlueprintLensLC1RegionProjector::Build(
			UnsupportedExplanation,
			Layout,
			Facts);
	TestEqual(
		TEXT("Unsupported member abstains"),
		UnsupportedRegion.Status,
		EBlueprintLensLC1RegionProjectionStatus::Unavailable);
	TestFalse(
		TEXT("Unsupported member is not renderable"),
		UnsupportedRegion.IsRenderable());

	FBlueprintLensExplanationModel MissingSourceExplanation =
		*LoadResult.Model;
	for (FBlueprintLensUnit& Unit : MissingSourceExplanation.Units)
	{
		if (Unit.Id == FirstRunUnitId)
		{
			for (FBlueprintLensSourceReference& Reference :
				 Unit.SourceReferences)
			{
				Reference.bPrimary = false;
			}
		}
	}
	TestEqual(
		TEXT("Missing primary source abstains"),
		FBlueprintLensLC1RegionProjector::Build(
			MissingSourceExplanation,
			Layout,
			Facts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::Unavailable);

	FBlueprintLensFrameFlowLayoutModel MissingBoundaryLayout = Layout;
	MissingBoundaryLayout.Segments[1].IncomingRelationIds.Reset();
	TestEqual(
		TEXT("Missing incoming ownership abstains"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			MissingBoundaryLayout,
			Facts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::Unavailable);

	FBlueprintLensLC1TypedIrFacts NonVariableSetFacts = Facts;
	NonVariableSetFacts.OperationsBySourceNodeId.FindChecked(
		FirstRunSourceId)
		.OperationClass = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
	TestEqual(
		TEXT("Heterogeneous operation class narrows to structural run"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			NonVariableSetFacts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::StructuralRun);

	FBlueprintLensLC1TypedIrFacts WrongTargetFacts = Facts;
	WrongTargetFacts.OperationsBySourceNodeId.FindChecked(FirstRunSourceId)
		.VariableTarget = TEXT("DifferentTarget");
	TestEqual(
		TEXT("Wrong target family narrows to assignments"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			WrongTargetFacts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::
			OrderedVariableAssignments);

	FBlueprintLensLC1TypedIrFacts WrongTypeFacts = Facts;
	WrongTypeFacts.OperationsBySourceNodeId.FindChecked(FirstRunSourceId)
		.ValueType = TEXT("int");
	TestEqual(
		TEXT("Non-Boolean target narrows to assignments"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			WrongTypeFacts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::
			OrderedVariableAssignments);

	FBlueprintLensLC1TypedIrFacts FalseLiteralFacts = Facts;
	FalseLiteralFacts.OperationsBySourceNodeId.FindChecked(FirstRunSourceId)
		.LiteralValue = TEXT("false");
	TestEqual(
		TEXT("False literal narrows to assignments"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			FalseLiteralFacts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::
			OrderedVariableAssignments);

	FBlueprintLensLC1TypedIrFacts MissingFactFacts = Facts;
	MissingFactFacts.OperationsBySourceNodeId.Remove(FirstRunSourceId);
	TestEqual(
		TEXT("Missing typed-IR fact narrows to structural run"),
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			MissingFactFacts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::StructuralRun);

	FBlueprintLensLC1TypedIrFacts UnboundFacts = Facts;
	UnboundFacts.VerifiedIrSha256 = FString::ChrN(64, TEXT('0'));
	const FBlueprintLensLC1RegionProjection UnboundRegion =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			UnboundFacts);
	TestEqual(
		TEXT("Unbound typed-IR facts narrow to structural run"),
		UnboundRegion.Status,
		EBlueprintLensLC1RegionProjectionStatus::StructuralRun);
	TestEqual(
		TEXT("Unbound typed-IR facts have a stable diagnostic"),
		UnboundRegion.DiagnosticCode,
		TEXT("LC1_REGION_TYPED_IR_UNBOUND"));

	FBlueprintLensExplanationModel TitleMutatedExplanation =
		*LoadResult.Model;
	for (FBlueprintLensUnit& Unit : TitleMutatedExplanation.Units)
	{
		if (Layout.Segments[1].MemberUnitIds.Contains(Unit.Id))
		{
			Unit.Title = TEXT("Misleading title must not drive projection");
		}
	}
	TestEqual(
		TEXT("Titles do not affect the operation claim"),
		FBlueprintLensLC1RegionProjector::Build(
			TitleMutatedExplanation,
			Layout,
			Facts)
			.Status,
		EBlueprintLensLC1RegionProjectionStatus::CompleteOperationRegion);

	FBlueprintLensExplanationModel ShuffledExplanation =
		*LoadResult.Model;
	Algo::Reverse(ShuffledExplanation.Units);
	Algo::Reverse(ShuffledExplanation.Relations);
	const FBlueprintLensFrameFlowLayoutModel ShuffledLayout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
			ShuffledExplanation);
	TestTrue(
		TEXT("Shuffled Explanation rebuilds the layout"),
		ShuffledLayout.IsReady());
	const FBlueprintLensLC1RegionProjection ShuffledRegion =
		FBlueprintLensLC1RegionProjector::Build(
			ShuffledExplanation,
			ShuffledLayout,
			Facts);
	TestEqual(
		TEXT("Shuffled serialization is byte-for-byte deterministic"),
		CanonicalRegionProjection(ShuffledRegion),
		CanonicalRegionProjection(Region));

	const FBlueprintLensLC1DisclosureProjection Plain =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
			FString(),
			3,
			Region);
	const FBlueprintLensLC1DisclosureProjection Evidence =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			Region);
	TestTrue(TEXT("Plain projection is valid"), Plain.IsValid());
	TestTrue(TEXT("Evidence projection is valid"), Evidence.IsValid());
	TestEqual(TEXT("Plain exposes all units"), Plain.DisplayedUnitIds.Num(), 14);
	TestEqual(
		TEXT("Evidence exposes all units"),
		Evidence.DisplayedUnitIds.Num(),
		14);
	TestEqual(
		TEXT("Plain exposes all relations"),
		Plain.DisplayedRelationIds.Num(),
		13);
	TestEqual(
		TEXT("Evidence exposes all relations"),
		Evidence.DisplayedRelationIds.Num(),
		13);
	TestEqual(
		TEXT("Candidate unit coverage matches"),
		Plain.DisplayedUnitIds,
		Evidence.DisplayedUnitIds);
	TestEqual(
		TEXT("Candidate relation coverage matches"),
		Plain.DisplayedRelationIds,
		Evidence.DisplayedRelationIds);
	TestEqual(
		TEXT("Candidate source actions match"),
		Plain.SourceActionUnitIds,
		Evidence.SourceActionUnitIds);
	TestFalse(
		TEXT("Candidate layout hash is non-empty"),
		Plain.LayoutModelHash.IsEmpty());
	TestEqual(
		TEXT("Candidate switch preserves layout hash"),
		Plain.LayoutModelHash,
		Evidence.LayoutModelHash);
	TestFalse(
		TEXT("Plain candidate does not attach a semantic region"),
		Plain.Region.IsRenderable());
	TestEqual(
		TEXT("Evidence candidate attaches the prebuilt region"),
		CanonicalRegionProjection(Evidence.Region),
		CanonicalRegionProjection(Region));

	FBlueprintLensLC1RegionProjection TamperedTemplateRegion = Region;
	TamperedTemplateRegion.SummaryTemplateId = TEXT("structural_run");
	TestFalse(
		TEXT("Evidence disclosure rejects a tampered template"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedTemplateRegion)
			.IsValid());

	FBlueprintLensLC1RegionProjection TamperedStatusRegion = Region;
	TamperedStatusRegion.Status =
		EBlueprintLensLC1RegionProjectionStatus::
			OrderedVariableAssignments;
	TestFalse(
		TEXT("Evidence disclosure rejects a tampered status"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedStatusRegion)
			.IsValid());

	FBlueprintLensLC1RegionProjection TamperedArgumentsRegion = Region;
	TamperedArgumentsRegion.SummaryArguments[0] = TEXT("11");
	TestFalse(
		TEXT("Evidence disclosure rejects tampered summary arguments"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedArgumentsRegion)
			.IsValid());

	FBlueprintLensLC1RegionProjection TamperedLedgerRegion = Region;
	TamperedLedgerRegion.ClaimEvidence[0].Value =
		TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
	TestFalse(
		TEXT("Evidence disclosure rejects a tampered claim ledger"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedLedgerRegion)
			.IsValid());

	FBlueprintLensLC1RegionProjection TamperedIntegrityRegion = Region;
	TamperedIntegrityRegion.ProjectionIntegrityHash =
		FString::ChrN(32, TEXT('0'));
	TestFalse(
		TEXT("Evidence disclosure rejects a tampered integrity hash"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedIntegrityRegion)
			.IsValid());

	FBlueprintLensLC1RegionProjection TamperedSourceRegion = Region;
	TamperedSourceRegion.SourceIrSha256 = FString::ChrN(64, TEXT('0'));
	TestFalse(
		TEXT("Evidence disclosure rejects a tampered source digest"),
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			TamperedSourceRegion)
			.IsValid());

	FBlueprintLensExplanationModel DifferentSourceExplanation =
		*LoadResult.Model;
	DifferentSourceExplanation.Source.IrSha256 =
		FString::ChrN(64, TEXT('0'));
	TestTrue(
		TEXT("Region integrity remains valid before disclosure binding"),
		Region.HasValidIntegrity());
	TestFalse(
		TEXT("Evidence disclosure rejects a different Explanation source"),
		FBlueprintLensLC1DisclosureProjector::Build(
			DifferentSourceExplanation,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			Region)
			.IsValid());

	FBlueprintLensLC1RegionProjection StaleMemberRegion = Region;
	StaleMemberRegion.OrderedMemberUnitIds[0] =
		Layout.Segments[1].MemberUnitIds[1];
	const FBlueprintLensLC1DisclosureProjection StaleMemberEvidence =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			StaleMemberRegion);
	TestFalse(
		TEXT("Evidence disclosure rejects stale region membership"),
		StaleMemberEvidence.IsValid());

	FBlueprintLensLC1RegionProjection StaleRelationRegion = Region;
	StaleRelationRegion.InternalRelationIds[0] =
		Region.IncomingRelationIds[0];
	const FBlueprintLensLC1DisclosureProjection StaleRelationEvidence =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			StaleRelationRegion);
	TestFalse(
		TEXT("Evidence disclosure rejects stale relation ownership"),
		StaleRelationEvidence.IsValid());

	const FBlueprintLensLC1DisclosureProjection CompatibilityEvidence =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString());
	TestFalse(
		TEXT("Evidence compatibility overload requires a region"),
		CompatibilityEvidence.IsValid());
	const FBlueprintLensLC1DisclosureProjection CompatibilityPlain =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
			FString());
	TestTrue(
		TEXT("Plain compatibility overload remains valid"),
		CompatibilityPlain.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLoadRealLC1FixtureTest,
	"BlueprintLens.Explanation.LoadRealLC1Fixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLoadRealLC1FixtureTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("Real LC1 Explanation loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	TestEqual(TEXT("LC1 units"), LoadResult.Model->Counts.Units, 14);
	TestEqual(
		TEXT("LC1 relations"),
		LoadResult.Model->Counts.Relations,
		13);
	TestEqual(
		TEXT("LC1 source nodes"),
		LoadResult.Model->Counts.SourceNodes,
		14);
	TestEqual(
		TEXT("LC1 source edges"),
		LoadResult.Model->Counts.SourceEdges,
		13);

	const FBlueprintLensSourceNavigator Navigator;
	TSet<FString> ResolvedSourceNodeIds;
	for (const FBlueprintLensUnit& Unit : LoadResult.Model->Units)
	{
		for (const FBlueprintLensSourceReference& Reference :
			 Unit.SourceReferences)
		{
			const FBlueprintLensResolvedSource Resolved =
				Navigator.Resolve(LoadResult.Model->Source, Reference);
			TestTrue(
				*FString::Printf(
					TEXT("LC1 source resolves: %s"),
					*Reference.SourceNodeId),
				Resolved.State == EBlueprintLensSourceState::Ready ||
					Resolved.State == EBlueprintLensSourceState::Unsaved);
			TestTrue(
				TEXT("Resolved LC1 source node is valid"),
				Resolved.Node.IsValid());
			if (Resolved.Node.IsValid())
			{
				TestEqual(
					TEXT("Resolved LC1 native GUID matches the explanation"),
					Resolved.Node->NodeGuid.ToString(
						EGuidFormats::DigitsWithHyphensLower),
					Reference.NativeNodeGuid.ToLower());
			}
			ResolvedSourceNodeIds.Add(Reference.SourceNodeId);
		}
	}
	TestEqual(
		TEXT("LC1 has fourteen distinct navigable source nodes"),
		ResolvedSourceNodeIds.Num(),
		14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1DisclosureParityTest,
	"BlueprintLens.FrameFlow.LC1DisclosureParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1DisclosureParityTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
			*LoadResult.Model);
	TestTrue(TEXT("Real LC1 layout is ready"), Layout.IsReady());
	if (!Layout.IsReady())
	{
		return false;
	}
	const FBlueprintLensLC1TypedIrFacts Facts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(
			LoadResult.Model->Source);
	TestTrue(TEXT("Real LC1 typed facts load"), Facts.IsValid());
	if (!Facts.IsValid())
	{
		AddError(Facts.Error);
		return false;
	}
	const FBlueprintLensLC1RegionProjection Region =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	TestTrue(TEXT("Real LC1 region is renderable"), Region.IsRenderable());
	if (!Region.IsRenderable())
	{
		AddError(Region.DiagnosticCode);
		return false;
	}

	const FBlueprintLensLC1DisclosureProjection Plain =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
			FString(),
			3,
			Region);
	const FBlueprintLensLC1DisclosureProjection Evidence =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::
				EvidenceBackedRegions,
			FString(),
			3,
			Region);
	TestTrue(TEXT("Plain projection is valid"), Plain.IsValid());
	TestTrue(TEXT("Evidence projection is valid"), Evidence.IsValid());
	const FBlueprintLensLC1DisclosureProjection UnknownCandidate =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			static_cast<EBlueprintLensLC1DisclosureCandidate>(255),
			FString());
	TestFalse(
		TEXT("Unknown disclosure candidate is rejected"),
		UnknownCandidate.IsValid());
	TestEqual(
		TEXT("Unknown disclosure candidate has the exact diagnostic"),
		UnknownCandidate.Error,
		FString(TEXT("LC1 disclosure candidate is unsupported")));
	if (!Plain.IsValid())
	{
		AddError(Plain.Error);
	}
	if (!Evidence.IsValid())
	{
		AddError(Evidence.Error);
	}
	if (!Plain.IsValid() || !Evidence.IsValid())
	{
		return false;
	}

	TestEqual(
		TEXT("Candidate unit coverage matches"),
		Plain.DisplayedUnitIds,
		Evidence.DisplayedUnitIds);
	TestEqual(
		TEXT("Candidate relation coverage matches"),
		Plain.DisplayedRelationIds,
		Evidence.DisplayedRelationIds);
	TestEqual(
		TEXT("Candidate source actions match"),
		Plain.SourceActionUnitIds,
		Evidence.SourceActionUnitIds);
	TestEqual(
		TEXT("Candidate switch preserves layout hash"),
		Plain.LayoutModelHash,
		Evidence.LayoutModelHash);
	TestEqual(
		TEXT("Initial detail has three visible units"),
		Plain.DetailWindow.VisibleUnitIds.Num(),
		3);
	TestEqual(
		TEXT("Initial detail has nine hidden prefix units"),
		Plain.DetailWindow.HiddenPrefixUnitIds.Num(),
		9);
	TestEqual(
		TEXT("Initial detail has nine hidden prefix relations"),
		Plain.DetailWindow.HiddenPrefixRelationIds.Num(),
		9);
	TestEqual(
		TEXT("Initial detail has two visible internal relations"),
		Plain.DetailWindow.VisibleRelationIds.Num(),
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1DisclosureStateTest,
	"BlueprintLens.Editor.LC1DisclosureState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1DisclosureStateTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("LC1 Explanation loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensFrameFlowLayoutModel Layout =
		FBlueprintLensFrameFlowLayoutBuilder::BuildLinear(
			*LoadResult.Model);
	const FBlueprintLensLC1TypedIrFacts Facts =
		FBlueprintLensLC1TypedIrFactLoader::LoadFile(
			LoadResult.Model->Source);
	const FBlueprintLensLC1RegionProjection Region =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	const FBlueprintLensLC1PseudocodeProjection Pseudocode =
		FBlueprintLensLC1PseudocodeProjector::Build(
			*LoadResult.Model,
			Layout,
			Facts);
	TestTrue(TEXT("LC1 layout is ready"), Layout.IsReady());
	TestTrue(TEXT("LC1 facts are valid"), Facts.IsValid());
	TestTrue(TEXT("LC1 region is sealed"), Region.IsRenderable());
	TestTrue(TEXT("LC1 pseudocode is sealed"), Pseudocode.IsRenderable());
	if (!Layout.IsReady() || !Facts.IsValid() || !Region.IsRenderable()
		|| !Pseudocode.IsRenderable())
	{
		return false;
	}

	const auto BuildProjection =
		[&]() -> FBlueprintLensLC1DisclosureProjection
		{
			return FBlueprintLensLC1DisclosureProjector::Build(
				*LoadResult.Model,
				Layout,
				EBlueprintLensLC1DisclosureCandidate::
					EvidenceBackedRegions,
				FString(),
				3,
				Region);
		};
	const FBlueprintLensLC1DisclosureProjection Baseline =
		BuildProjection();
	TestTrue(TEXT("Baseline evidence projection is valid"), Baseline.IsValid());
	if (!Baseline.IsValid())
	{
		AddError(Baseline.Error);
		return false;
	}

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.Model;
	Panel->ResolveSources();
	Panel->LC1DisclosureCandidate.Reset();
	Panel->bLC1RegionMembersExpanded = false;
	Panel->bLC1ShowAllExpanded = false;
	Panel->bLC1WhyGroupedExpanded = false;
	Panel->bLC1TechnicalEvidenceExpanded = false;
	Panel->LC1SelectedUnitId.Reset();
	Panel->LC1SelectedPseudocodeLineId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;

	const auto VerifyTruthUnchanged =
		[&](const TCHAR* ToggleLabel)
		{
			const FBlueprintLensLC1DisclosureProjection Current =
				BuildProjection();
			TestEqual(
				FString::Printf(
					TEXT("%s preserves displayed unit IDs"),
					ToggleLabel),
				Current.DisplayedUnitIds,
				Baseline.DisplayedUnitIds);
			TestEqual(
				FString::Printf(
					TEXT("%s preserves displayed relation IDs"),
					ToggleLabel),
				Current.DisplayedRelationIds,
				Baseline.DisplayedRelationIds);
			TestEqual(
				FString::Printf(
					TEXT("%s preserves source-action IDs"),
					ToggleLabel),
				Current.SourceActionUnitIds,
				Baseline.SourceActionUnitIds);
			TestEqual(
				FString::Printf(
					TEXT("%s preserves the layout hash"),
					ToggleLabel),
				Current.LayoutModelHash,
				Baseline.LayoutModelHash);
		};

	TestTrue(
		TEXT("Evidence candidate button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("EVIDENCE-BACKED REGIONS")));
	TestTrue(
		TEXT("Evidence candidate content appears"),
		SlateWidgetText(PanelRoot).Contains(
			TEXT("Operation region: set completion flags to true in sequence")));
	TestEqual(
		TEXT("Evidence reader has no contextual source action before selection"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected row in Blueprint"))
			.Num(),
		0);
	VerifyTruthUnchanged(TEXT("Candidate click"));

	const FBlueprintLensUnit* Entry =
		LoadResult.Model->FindUnit(Layout.Segments[0].MemberUnitIds[0]);
	TestNotNull(TEXT("Entry unit exists"), Entry);
	if (Entry == nullptr)
	{
		return false;
	}
	TestTrue(
		TEXT("Entry row invokes its selection delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Event BeginPlay")));
	TestEqual(
		TEXT("Entry row becomes the selected LC1 unit"),
		Panel->LC1SelectedUnitId,
		Entry->Id);
	const TArray<TSharedRef<SButton>> EntryActions =
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected row in Blueprint"));
	TestEqual(
		TEXT("Entry selection exposes one contextual source action"),
		EntryActions.Num(),
		1);
	if (EntryActions.Num() == 1)
	{
		TestEqual(
			TEXT("Entry contextual action uses the source-enabled binding"),
			EntryActions[0]->IsEnabled(),
			Panel->CanNavigateToSource(Panel->PrimarySourceNodeId(*Entry)));
	}

	TestTrue(
		TEXT("Show-all button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Show all 14 steps")));
	TestTrue(
		TEXT("Show-all expanded label appears"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Hide all 14 steps")));
	int32 NumberedRowCount = 0;
	for (int32 Index = 1; Index <= 14; ++Index)
	{
		NumberedRowCount += CountSlateTextWithPrefix(
			PanelRoot,
			FString::Printf(TEXT("%d. "), Index));
	}
	TestEqual(
		TEXT("Show-all renders fourteen numbered rows"),
		NumberedRowCount,
		14);
	TestEqual(
		TEXT("Show-all removes every per-row source action"),
		SlateButtonsWithLabel(PanelRoot, TEXT("Open in Blueprint")).Num(),
		0);
	TestEqual(
		TEXT("Show-all keeps one contextual source action for the selected row"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected row in Blueprint"))
			.Num(),
		1);
	VerifyTruthUnchanged(TEXT("Show-all click"));
	TestTrue(
		TEXT("Hide-all button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Hide all 14 steps")));

	TestTrue(
		TEXT("Show-12 button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Show 12 operations")));
	TestTrue(
		TEXT("Show-12 expanded label appears"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Hide 12 operations")));
	VerifyTruthUnchanged(TEXT("Show-12 click"));

	TestTrue(
		TEXT("Why-grouped button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Why grouped?")));
	const FString ExpandedText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Why-grouped expanded label appears"),
		ExpandedText.Contains(TEXT("Hide grouping evidence")));
	const TCHAR* WhyFields[] = {
		TEXT("Why this is grouped"),
		TEXT("Relation coverage"),
		TEXT("Source coverage"),
		TEXT("Typed-IR binding"),
		TEXT("Claim boundary")
	};
	for (const TCHAR* Field : WhyFields)
	{
		TestTrue(
			FString::Printf(TEXT("Why field appears: %s"), Field),
			ExpandedText.Contains(Field));
	}
	TestTrue(
		TEXT("Source coverage names all primary Blueprint sources"),
		ExpandedText.Contains(
			TEXT("12 of 12 region members have primary Blueprint sources")));
	TestTrue(
		TEXT("Complete region explains its verified typed-IR binding"),
		ExpandedText.Contains(TEXT(
			"Typed evidence verifies the repeated supported assignment and its "
			"true value.")));

	const TSharedPtr<SWidget> ResponsiveWrap =
		FindDeepestSlateContainer(
		PanelRoot,
		TEXT("SWrapBox"),
		{
			TEXT("Operation region: set completion flags to true in sequence"),
			TEXT("Why this is grouped")
			});
	TestTrue(
		TEXT("Reader region and Why panel share one responsive SWrapBox"),
		ResponsiveWrap.IsValid());
	const TSharedPtr<SWidget> WhyPanel =
		FindDeepestSlateContainer(
			PanelRoot,
			TEXT("SBorder"),
			{TEXT("Why this is grouped"), TEXT("Claim boundary")});
	TestTrue(TEXT("Why panel is present"), WhyPanel.IsValid());
	if (WhyPanel.IsValid())
	{
		TestEqual(
			TEXT("Why panel retains no per-member source buttons"),
			SlateButtonsWithLabel(
				WhyPanel.ToSharedRef(),
				TEXT("Open in Blueprint"))
				.Num(),
			0);
	}
	TestTrue(
		TEXT("Evidence technical disclosure invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Technical evidence")));
	const FString EvidenceTechnicalText = SlateWidgetText(PanelRoot);
	VerifyTruthUnchanged(TEXT("Why-grouped click"));

	const auto AssertLayer1HidesTechnicalFacts =
		[&](const TCHAR* ConditionLabel,
			const FString& ReaderText,
			const FBlueprintLensLC1RegionProjection& ConditionRegion,
			const FBlueprintLensLC1PseudocodeProjection* Pseudocode)
		{
			for (const FString& RelationId :
				 ConditionRegion.IncomingRelationIds)
			{
				TestFalse(
					FString::Printf(
						TEXT("%s layer 1 hides incoming relation ID %s"),
						ConditionLabel,
						*RelationId),
					ReaderText.Contains(RelationId));
			}
			for (const FString& RelationId :
				 ConditionRegion.InternalRelationIds)
			{
				TestFalse(
					FString::Printf(
						TEXT("%s layer 1 hides internal relation ID %s"),
						ConditionLabel,
						*RelationId),
					ReaderText.Contains(RelationId));
			}
			for (const FString& RelationId :
				 ConditionRegion.OutgoingRelationIds)
			{
				TestFalse(
					FString::Printf(
						TEXT("%s layer 1 hides outgoing relation ID %s"),
						ConditionLabel,
						*RelationId),
					ReaderText.Contains(RelationId));
			}
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides source IR digest"),
					ConditionLabel),
				ReaderText.Contains(ConditionRegion.SourceIrSha256));
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides region integrity hash"),
					ConditionLabel),
				ReaderText.Contains(ConditionRegion.ProjectionIntegrityHash));
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides region diagnostic code"),
					ConditionLabel),
				ReaderText.Contains(ConditionRegion.DiagnosticCode));
			for (const FBlueprintLensUnit& Unit : LoadResult.Model->Units)
			{
				for (const FBlueprintLensSourceReference& Reference :
					 Unit.SourceReferences)
				{
					if (Reference.NativeNodeGuid.IsEmpty())
					{
						continue;
					}
					TestFalse(
						FString::Printf(
							TEXT("%s layer 1 hides source GUID %s"),
							ConditionLabel,
							*Reference.NativeNodeGuid),
						ReaderText.Contains(Reference.NativeNodeGuid));
				}
			}
			if (Pseudocode == nullptr)
			{
				return;
			}
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides pseudocode source digest"),
					ConditionLabel),
				ReaderText.Contains(Pseudocode->SourceIrSha256));
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides pseudocode integrity hash"),
					ConditionLabel),
				ReaderText.Contains(Pseudocode->ProjectionIntegrityHash));
			TestFalse(
				FString::Printf(
					TEXT("%s layer 1 hides pseudocode diagnostic code"),
					ConditionLabel),
				ReaderText.Contains(Pseudocode->DiagnosticCode));
		};

	const auto AssertTechnicalFactsVisible =
		[&](const TCHAR* ConditionLabel,
			const FString& TechnicalText,
			const FBlueprintLensLC1RegionProjection& ConditionRegion,
			const FBlueprintLensLC1PseudocodeProjection* Pseudocode)
		{
			for (const FString& RelationId :
				 ConditionRegion.IncomingRelationIds)
			{
				TestTrue(
					FString::Printf(
						TEXT("%s technical evidence retains incoming relation ID %s"),
						ConditionLabel,
						*RelationId),
					TechnicalText.Contains(RelationId));
			}
			for (const FString& RelationId :
				 ConditionRegion.InternalRelationIds)
			{
				TestTrue(
					FString::Printf(
						TEXT("%s technical evidence retains internal relation ID %s"),
						ConditionLabel,
						*RelationId),
					TechnicalText.Contains(RelationId));
			}
			for (const FString& RelationId :
				 ConditionRegion.OutgoingRelationIds)
			{
				TestTrue(
					FString::Printf(
						TEXT("%s technical evidence retains outgoing relation ID %s"),
						ConditionLabel,
						*RelationId),
					TechnicalText.Contains(RelationId));
			}
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains source IR digest"),
					ConditionLabel),
				TechnicalText.Contains(ConditionRegion.SourceIrSha256));
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains region integrity hash"),
					ConditionLabel),
				TechnicalText.Contains(ConditionRegion.ProjectionIntegrityHash));
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains region diagnostic code"),
					ConditionLabel),
				TechnicalText.Contains(ConditionRegion.DiagnosticCode));
			for (const FBlueprintLensUnit& Unit : LoadResult.Model->Units)
			{
				for (const FBlueprintLensSourceReference& Reference :
					 Unit.SourceReferences)
				{
					if (Reference.NativeNodeGuid.IsEmpty())
					{
						continue;
					}
					TestTrue(
						FString::Printf(
							TEXT("%s technical evidence retains source GUID %s"),
							ConditionLabel,
							*Reference.NativeNodeGuid),
						TechnicalText.Contains(Reference.NativeNodeGuid));
				}
			}
			if (Pseudocode == nullptr)
			{
				return;
			}
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains pseudocode source digest"),
					ConditionLabel),
				TechnicalText.Contains(Pseudocode->SourceIrSha256));
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains pseudocode integrity hash"),
					ConditionLabel),
				TechnicalText.Contains(Pseudocode->ProjectionIntegrityHash));
			TestTrue(
				FString::Printf(
					TEXT("%s technical evidence retains pseudocode diagnostic code"),
					ConditionLabel),
				TechnicalText.Contains(Pseudocode->DiagnosticCode));
		};

	AssertLayer1HidesTechnicalFacts(
		TEXT("LC1_EVIDENCE_REGIONS"),
		ExpandedText,
		Region,
		nullptr);
	AssertTechnicalFactsVisible(
		TEXT("LC1_EVIDENCE_REGIONS"),
		EvidenceTechnicalText,
		Region,
		nullptr);
	TestTrue(
		TEXT("LC1_EVIDENCE_REGIONS technical evidence retains verified binding"),
		EvidenceTechnicalText.Contains(*FString::Printf(
			TEXT("Verified · SHA-256 %s"),
			*Region.SourceIrSha256)));

	FBlueprintLensLC1TypedIrFacts UnboundFacts = Facts;
	UnboundFacts.VerifiedIrSha256.Reset();
	const FBlueprintLensLC1RegionProjection UnboundRegion =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			UnboundFacts);
	TestEqual(
		TEXT("Unbound facts narrow to structural run"),
		UnboundRegion.Status,
		EBlueprintLensLC1RegionProjectionStatus::StructuralRun);
	TestEqual(
		TEXT("Unbound facts retain the exact diagnostic"),
		UnboundRegion.DiagnosticCode,
		FString(TEXT("LC1_REGION_TYPED_IR_UNBOUND")));
	const FBlueprintLensLC1DisclosureProjection UnboundProjection =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			UnboundRegion);
	TestTrue(
		TEXT("Unbound structural projection remains recoverable"),
		UnboundProjection.IsValid());
	Panel->bLC1RegionMembersExpanded = false;
	Panel->bLC1WhyGroupedExpanded = false;
	Panel->bLC1TechnicalEvidenceExpanded = false;
	Panel->LC1SelectedUnitId.Reset();
	const TSharedRef<SWidget> UnboundWidget =
		Panel->BuildLC1EvidenceRegions(Layout, UnboundProjection);
	const FString UnboundText = SlateWidgetText(UnboundWidget);
	AssertLayer1HidesTechnicalFacts(
		TEXT("LC1_EVIDENCE_REGIONS_ABSTENTION"),
		UnboundText,
		UnboundRegion,
		nullptr);
	TestTrue(
		TEXT("Structural answer uses neutral predecessor wording"),
		UnboundText.Contains(TEXT(
			"It is reached after Event BeginPlay and 12 ordered "
			"predecessor steps.")));
	TestTrue(
		TEXT("Unbound typed-IR binding disclaims an operation claim"),
		UnboundText.Contains(
			TEXT("No operation claim is made because typed evidence is unavailable.")));
	TestTrue(
		TEXT("Structural disclosure uses a neutral member control"),
		InvokeSlateButton(UnboundWidget, TEXT("Show 12 steps")));
	const TSharedRef<SWidget> ExpandedUnboundWidget =
		Panel->BuildLC1EvidenceRegions(Layout, UnboundProjection);
	TestTrue(
		TEXT("Structural disclosure keeps its expanded control neutral"),
		SlateWidgetText(ExpandedUnboundWidget).Contains(
			TEXT("Hide 12 steps")));
	TestFalse(
		TEXT("Structural disclosure never calls members operations"),
		SlateWidgetText(ExpandedUnboundWidget).Contains(
			TEXT("12 operations")));
	Panel->bLC1TechnicalEvidenceExpanded = true;
	const TSharedRef<SWidget> ExpandedUnboundTechnicalWidget =
		Panel->BuildLC1EvidenceRegions(Layout, UnboundProjection);
	AssertTechnicalFactsVisible(
		TEXT("LC1_EVIDENCE_REGIONS_ABSTENTION"),
		SlateWidgetText(ExpandedUnboundTechnicalWidget),
		UnboundRegion,
		nullptr);

	FBlueprintLensLC1TypedIrFacts AssignmentFacts = Facts;
	const FBlueprintLensUnit* FirstRunUnit = LoadResult.Model->FindUnit(
		Layout.Segments[1].MemberUnitIds[0]);
	TestNotNull(TEXT("First run unit exists"), FirstRunUnit);
	if (FirstRunUnit == nullptr)
	{
		return false;
	}
	const FString FirstRunSourceId =
		Panel->PrimarySourceNodeId(*FirstRunUnit);
	AssignmentFacts.OperationsBySourceNodeId.FindChecked(FirstRunSourceId)
		.VariableTarget = TEXT("DifferentTarget");
	const FBlueprintLensLC1RegionProjection AssignmentRegion =
		FBlueprintLensLC1RegionProjector::Build(
			*LoadResult.Model,
			Layout,
			AssignmentFacts);
	TestEqual(
		TEXT("Changed target narrows to variable assignments"),
		AssignmentRegion.Status,
		EBlueprintLensLC1RegionProjectionStatus::
			OrderedVariableAssignments);
	const FBlueprintLensLC1DisclosureProjection AssignmentProjection =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
			FString(),
			3,
			AssignmentRegion);
	TestTrue(
		TEXT("Assignment projection is renderable"),
		AssignmentProjection.IsValid());
	Panel->bLC1RegionMembersExpanded = false;
	const TSharedRef<SWidget> AssignmentWidget =
		Panel->BuildLC1EvidenceRegions(Layout, AssignmentProjection);
	TestTrue(
		TEXT("Assignment answer remains evidence-bounded"),
		SlateWidgetText(AssignmentWidget).Contains(TEXT(
			"It is reached after Event BeginPlay and 12 ordered "
			"predecessor steps.")));
	TestTrue(
		TEXT("Assignment disclosure uses its supported semantic label"),
		InvokeSlateButton(
			AssignmentWidget,
			TEXT("Show 12 variable assignments")));
	const TSharedRef<SWidget> ExpandedAssignmentWidget =
		Panel->BuildLC1EvidenceRegions(Layout, AssignmentProjection);
	TestTrue(
		TEXT("Assignment disclosure keeps its expanded semantic label"),
		SlateWidgetText(ExpandedAssignmentWidget).Contains(
			TEXT("Hide 12 variable assignments")));

	const FBlueprintLensLC1DisclosureProjection PlainFallbackProjection =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
			FString());
	TestTrue(
		TEXT("Plain projection remains valid for the evidence fallback"),
		PlainFallbackProjection.IsValid());

	const FBlueprintLensLC1DisclosureProjection PairedProjection =
		FBlueprintLensLC1DisclosureProjector::Build(
			*LoadResult.Model,
			Layout,
			EBlueprintLensLC1DisclosureCandidate::PairedPseudocode,
			FString(),
			3,
			Region,
			Pseudocode);
	TestTrue(TEXT("Paired projection is valid"), PairedProjection.IsValid());
	Panel->bLC1WhyGroupedExpanded = false;
	Panel->bLC1ShowAllExpanded = false;
	Panel->bLC1PseudocodeExpanded = false;
	Panel->bLC1TechnicalEvidenceExpanded = false;
	Panel->LC1SelectedUnitId.Reset();
	Panel->LC1SelectedPseudocodeLineId.Reset();
	TestTrue(
		TEXT("Paired candidate button invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("PAIRED PSEUDOCODE")));
	FString PairedText = SlateWidgetText(PanelRoot);
	TestFalse(
		TEXT("Paired pseudocode is collapsed by default"),
		Panel->bLC1PseudocodeExpanded);
	TestTrue(
		TEXT("Collapsed surface exposes the mapped-line disclosure"),
		PairedText.Contains(TEXT("Show paired pseudocode (14 mapped lines)")));
	TestFalse(
		TEXT("Collapsed surface withholds the code boundary"),
		PairedText.Contains(
			TEXT("Readable projection, not recovered or compilable C++.")));
	TestFalse(
		TEXT("Collapsed surface withholds code status chips"),
		PairedText.Contains(TEXT("14 / 14 MAPPED")));
	TestEqual(
		TEXT("Collapsed surface withholds contextual code actions"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected line in Blueprint"))
			.Num(),
		0);
	AssertLayer1HidesTechnicalFacts(
		TEXT("LC1_PAIRED_PSEUDOCODE"),
		PairedText,
		Region,
		&Pseudocode);
	TestTrue(
		TEXT("Paired technical disclosure invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("Technical evidence")));
	AssertTechnicalFactsVisible(
		TEXT("LC1_PAIRED_PSEUDOCODE"),
		SlateWidgetText(PanelRoot),
		Region,
		&Pseudocode);
	TestTrue(
		TEXT("Paired technical disclosure hides on demand"),
		InvokeSlateButton(PanelRoot, TEXT("Hide technical evidence")));
	TestTrue(
		TEXT("Code disclosure invokes its click delegate"),
		InvokeSlateButton(
			PanelRoot,
			TEXT("Show paired pseudocode (14 mapped lines)")));
	PairedText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Paired pseudocode expands on demand"),
		Panel->bLC1PseudocodeExpanded);
	TestTrue(
		TEXT("Expanded surface labels its truth boundary"),
		PairedText.Contains(
			TEXT("Readable projection, not recovered or compilable C++.")));
	TestTrue(
		TEXT("Expanded surface shows the deterministic status"),
		PairedText.Contains(TEXT("DETERMINISTIC")));
	TestTrue(
		TEXT("Expanded surface shows all mapped lines"),
		PairedText.Contains(TEXT("14 / 14 MAPPED")));
	TestTrue(
		TEXT("Expanded surface shows the first assignment target"),
		PairedText.Contains(TEXT("LC1Step01Complete")));
	TestTrue(
		TEXT("Expanded surface shows the criterion assignment target"),
		PairedText.Contains(TEXT("LC1Ready")));
	TestTrue(
		TEXT("Criterion is selected by default"),
		PairedText.Contains(TEXT("Line 14 · criterion · supported")));
	TestEqual(
		TEXT("Paired surface exposes one contextual source action"),
		SlateButtonsWithLabel(
			PanelRoot,
			TEXT("Open selected line in Blueprint"))
			.Num(),
		1);
	TestTrue(
		TEXT("Selecting a code line invokes its click delegate"),
		InvokeSlateButton(PanelRoot, TEXT("LC1Step01Complete")));
	PairedText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("Selected operation line is reported in text"),
		PairedText.Contains(TEXT("Line 2 · operation · supported")));
	TestEqual(
		TEXT("Selected code line state is stable"),
		Panel->LC1SelectedPseudocodeLineId,
		Pseudocode.Lines[1].LineId);
	TestTrue(
		TEXT("Paired why-grouped control remains available"),
		InvokeSlateButton(PanelRoot, TEXT("Why grouped?")));
	TestTrue(
		TEXT("Paired why-grouped evidence expands"),
		SlateWidgetText(PanelRoot).Contains(TEXT("Claim boundary")));
	TestTrue(
		TEXT("Paired complete explanation control remains available"),
		InvokeSlateButton(PanelRoot, TEXT("Show explanation steps")));
	int32 PairedNumberedRowCount = 0;
	for (int32 Index = 1; Index <= 14; ++Index)
	{
		PairedNumberedRowCount += CountSlateTextWithPrefix(
			PanelRoot,
			FString::Printf(TEXT("%d. "), Index));
	}
	TestEqual(
		TEXT("Paired complete disclosure renders fourteen rows"),
		PairedNumberedRowCount,
		14);
	TestTrue(
		TEXT("Expanded pseudocode exposes its hide control"),
		InvokeSlateButton(PanelRoot, TEXT("Hide paired pseudocode")));
	TestFalse(
		TEXT("Hide control collapses the pseudocode state"),
		Panel->bLC1PseudocodeExpanded);
	TestFalse(
		TEXT("Collapsed-again surface removes the code boundary"),
		SlateWidgetText(PanelRoot).Contains(
			TEXT("Readable projection, not recovered or compilable C++.")));

	Panel->bLC1TechnicalEvidenceExpanded = false;
	Panel->bLC1WhyGroupedExpanded = false;
	Panel->LC1SelectedUnitId.Reset();
	const TSharedRef<SWidget> PlainFallbackWidget =
		Panel->BuildLC1EvidenceRegions(Layout, PlainFallbackProjection);
	const FString PlainFallbackText =
		SlateWidgetText(PlainFallbackWidget);
	AssertLayer1HidesTechnicalFacts(
		TEXT("LC1_PLAIN_OUTLINE"),
		PlainFallbackText,
		Region,
		nullptr);
	TestTrue(
		TEXT("Plain fallback exposes the exact unavailable diagnostic"),
		PlainFallbackText.Contains(TEXT(
			"Detailed grouping evidence is unavailable. "
			"Showing the complete ordered explanation.")));
	TestTrue(
		TEXT("Plain fallback answer uses neutral predecessor wording"),
		PlainFallbackText.Contains(TEXT(
			"It is reached after Event BeginPlay and 12 ordered "
			"predecessor steps.")));
	TestFalse(
		TEXT("Plain fallback answer does not assert setup operations"),
		PlainFallbackText.Contains(TEXT("setup operations")));
	int32 PlainFallbackNumberedRowCount = 0;
	for (int32 Index = 1; Index <= 14; ++Index)
	{
		PlainFallbackNumberedRowCount += CountSlateTextWithPrefix(
			PlainFallbackWidget,
			FString::Printf(TEXT("%d. "), Index));
	}
	TestEqual(
		TEXT("Plain fallback renders fourteen numbered rows"),
		PlainFallbackNumberedRowCount,
		14);
	TestEqual(
		TEXT("Plain fallback removes every per-row source action"),
		SlateButtonsWithLabel(
			PlainFallbackWidget,
			TEXT("Open in Blueprint"))
			.Num(),
		0);
	TestEqual(
		TEXT("Plain fallback has no contextual action before row selection"),
		SlateButtonsWithLabel(
			PlainFallbackWidget,
			TEXT("Open selected row in Blueprint"))
			.Num(),
		0);
	TestTrue(
		TEXT("Plain fallback row invokes its selection delegate"),
		InvokeSlateButton(PlainFallbackWidget, TEXT("1. Event BeginPlay")));
	TestEqual(
		TEXT("Plain fallback selects the requested entry row"),
		Panel->LC1SelectedUnitId,
		Layout.Segments[0].MemberUnitIds[0]);
	const TSharedRef<SWidget> PlainSelectedWidget =
		Panel->BuildLC1EvidenceRegions(Layout, PlainFallbackProjection);
	TestEqual(
		TEXT("Plain fallback exposes one contextual source action after selection"),
		SlateButtonsWithLabel(
			PlainSelectedWidget,
			TEXT("Open selected row in Blueprint"))
			.Num(),
		1);

	for (const FString& UnitId : PlainFallbackProjection.DisplayedUnitIds)
	{
		const FBlueprintLensUnit* Unit = LoadResult.Model->FindUnit(UnitId);
		TestNotNull(
			FString::Printf(
				TEXT("LC1_PLAIN_OUTLINE primary-source unit exists: %s"),
				*UnitId),
			Unit);
		if (Unit == nullptr)
		{
			continue;
		}
		Panel->SelectLC1Unit(UnitId);
		const TSharedRef<SWidget> UnitWidget =
			Panel->BuildLC1EvidenceRegions(Layout, PlainFallbackProjection);
		const TArray<TSharedRef<SButton>> UnitActions =
			SlateButtonsWithLabel(
				UnitWidget,
				TEXT("Open selected row in Blueprint"));
		TestEqual(
			FString::Printf(
				TEXT("LC1_PLAIN_OUTLINE exposes one action for selected unit %s"),
				*UnitId),
			UnitActions.Num(),
			1);
		if (UnitActions.Num() == 1)
		{
			TestEqual(
				FString::Printf(
					TEXT("LC1_PLAIN_OUTLINE preserves source safety for unit %s"),
					*UnitId),
				UnitActions[0]->IsEnabled(),
				Panel->CanNavigateToSource(
					Panel->PrimarySourceNodeId(*Unit)));
		}
	}
	Panel->bLC1TechnicalEvidenceExpanded = true;
	const TSharedRef<SWidget> PlainTechnicalWidget =
		Panel->BuildLC1EvidenceRegions(Layout, PlainFallbackProjection);
	AssertTechnicalFactsVisible(
		TEXT("LC1_PLAIN_OUTLINE"),
		SlateWidgetText(PlainTechnicalWidget),
		Region,
		nullptr);
	return true;
}

namespace
{
const float LC1RailReviewedWidths[] = {430.0f, 480.0f, 617.0f, 700.0f};

TArray<FString> LC1RailProvenOrder(
	const FBlueprintLensLC1RailProjection& Projection)
{
	TArray<FString> Order;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		 Projection.OrderedCanonicalUnits)
	{
		Order.Add(Unit.UnitId);
	}
	return Order;
}

int32 LC1RailOccurrences(const FString& Haystack, const FString& Needle)
{
	int32 Count = 0;
	for (int32 SearchFrom = 0;;)
	{
		const int32 Found = Haystack.Find(
			Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Found == INDEX_NONE)
		{
			break;
		}
		++Count;
		SearchFrom = Found + 1;
	}
	return Count;
}

int32 LC1StageRelationCount(const FString& StageText)
{
	const FString Lower = StageText.ToLower();
	const int32 RelationWord = Lower.Find(
		TEXT("relation"),
		ESearchCase::CaseSensitive,
		ESearchDir::FromEnd);
	if (RelationWord == INDEX_NONE) return INDEX_NONE;
	FString Prefix = StageText.Left(RelationWord).TrimEnd();
	int32 TokenStart = INDEX_NONE;
	if (!Prefix.FindLastChar(TEXT(' '), TokenStart)) TokenStart = -1;
	FString Token = Prefix.Mid(TokenStart + 1).TrimStartAndEnd();
	if (Token.Equals(TEXT("rail"), ESearchCase::IgnoreCase))
	{
		Prefix = Prefix.Left(TokenStart).TrimEnd();
		if (!Prefix.FindLastChar(TEXT(' '), TokenStart)) TokenStart = -1;
		Token = Prefix.Mid(TokenStart + 1).TrimStartAndEnd();
	}
	return Token.IsNumeric() ? FCString::Atoi(*Token) : INDEX_NONE;
}

class FLC1RailUnavailableBackend final : public IBlueprintLensLayoutBackend
{
public:
	explicit FLC1RailUnavailableBackend(
		const EBlueprintLensLayoutBackendKind InKind)
		: Kind(InKind)
	{
	}

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override
	{
		return Kind;
	}

	virtual bool IsAvailable(FString& OutDiagnostic) const override
	{
		OutDiagnostic = TEXT("TEST_BACKEND_UNAVAILABLE");
		return false;
	}

	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest&) const override
	{
		return FBlueprintLensLayoutLedger();
	}

private:
	EBlueprintLensLayoutBackendKind Kind;
};

// The reviewed fixture never folds, so the fold path needs a chain longer than
// the default radius to be exercised at all. M2 requires the rule to be
// reachable, not merely stated.
FBlueprintLensExplanationModel MakeFoldableChain(const int32 IntermediateUnits)
{
	FBlueprintLensExplanationModel Model =
		MakeLinearExplanation(IntermediateUnits);
	Model.Source.IrSha256 =
		TEXT("00000000000000000000000000000000000000000000000000000000000000FD");
	return Model;
}

const TCHAR* LC1BoundaryDisclosure(
	const EBlueprintLensSemanticStatus SemanticStatus)
{
	switch (SemanticStatus)
	{
	case EBlueprintLensSemanticStatus::Opaque:
		return TEXT(
			"Traversal stops at this opaque call; behavior beyond this cap is not "
			"claimed by the slice.");
	case EBlueprintLensSemanticStatus::Unsupported:
		return TEXT(
			"Traversal stops at this unsupported node; its behavior is not claimed "
			"by the slice.");
	case EBlueprintLensSemanticStatus::Uncertain:
		return TEXT(
			"Traversal stops at this uncertain dependency; the relationship beyond "
			"this cap is not claimed by the slice.");
	default:
		return TEXT(
			"Traversal stops at this boundary; behavior beyond this cap is not "
			"claimed by the slice.");
	}
}

FBlueprintLensExplanationModel MakeBoundaryRailExplanation(
	const FBlueprintLensExplanationModel& Source)
{
	FBlueprintLensExplanationModel Model = Source;
	const EBlueprintLensSemanticStatus Statuses[] = {
		EBlueprintLensSemanticStatus::Opaque,
		EBlueprintLensSemanticStatus::Unsupported,
		EBlueprintLensSemanticStatus::Uncertain};
	const TCHAR* Titles[] = {
		TEXT("Opaque Call"),
		TEXT("Unsupported Node"),
		TEXT("Uncertain Dependency")};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Statuses); ++Index)
	{
		Model.Units[Index].Role = EBlueprintLensRole::Boundary;
		Model.Units[Index].SemanticStatus = Statuses[Index];
		Model.Units[Index].Title = Titles[Index];
	}

	for (FBlueprintLensLane& Lane : Model.Lanes)
	{
		if (Lane.Role == EBlueprintLensRole::Control)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Statuses); ++Index)
			{
				Lane.UnitIds.Remove(Model.Units[Index].Id);
			}
		}
		else if (Lane.Role == EBlueprintLensRole::Boundary)
		{
			Lane.State = EBlueprintLensLaneState::Populated;
			Lane.EmptyMessage.Reset();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Statuses); ++Index)
			{
				Lane.UnitIds.Add(Model.Units[Index].Id);
			}
		}
	}
	return Model;
}

template <typename ProjectionType>
auto AssertLC1BoundaryCapProjection(
	FAutomationTestBase& Test,
	const ProjectionType& Projection,
	const FBlueprintLensExplanationModel& Explanation,
	int)
	-> decltype(
		Projection.BoundaryCaps.Num(),
		Projection.BoundaryCaps[0].UnitId,
		Projection.BoundaryCaps[0].SemanticStatus,
		Projection.BoundaryCaps[0].Title,
		Projection.BoundaryCaps[0].Disclosure,
		bool())
{
	TSet<FString> ExpectedBoundaryIds;
	for (const FBlueprintLensUnit& Unit : Explanation.Units)
	{
		if (Unit.Role == EBlueprintLensRole::Boundary)
		{
			ExpectedBoundaryIds.Add(Unit.Id);
		}
	}
	Test.TestEqual(
		TEXT("LC1 rail projects one cap per boundary unit"),
		Projection.BoundaryCaps.Num(),
		ExpectedBoundaryIds.Num());

	TSet<FString> ObservedCapIds;
	TSet<FString> ObservedDisclosures;
	for (const auto& Cap : Projection.BoundaryCaps)
	{
		ObservedCapIds.Add(Cap.UnitId);
		ObservedDisclosures.Add(Cap.Disclosure);
		const FBlueprintLensUnit* SourceUnit = Explanation.FindUnit(Cap.UnitId);
		Test.TestTrue(
			TEXT("LC1 rail invents no boundary cap"),
			SourceUnit != nullptr &&
				SourceUnit->Role == EBlueprintLensRole::Boundary);
		if (SourceUnit == nullptr)
		{
			continue;
		}
		Test.TestTrue(
			TEXT("LC1 cap semantic status equals its source unit"),
			Cap.SemanticStatus == SourceUnit->SemanticStatus);
		Test.TestEqual(
			TEXT("LC1 cap title equals its source unit"),
			Cap.Title,
			SourceUnit->Title);
		Test.TestEqual(
			TEXT("LC1 cap disclosure is identical to the Python adapter"),
			Cap.Disclosure,
			FString(LC1BoundaryDisclosure(SourceUnit->SemanticStatus)));
	}

	Test.TestEqual(
		TEXT("LC1 rail loses no boundary cap and duplicates none"),
		ObservedCapIds.Num(),
		ExpectedBoundaryIds.Num());
	Test.TestEqual(
		TEXT("LC1 cap disclosures differ across opaque unsupported and uncertain"),
		ObservedDisclosures.Num(),
		3);
	Test.TestFalse(
		TEXT("LC1 criterion is never a boundary cap"),
		ObservedCapIds.Contains(Explanation.CriterionUnitId));
	return true;
}

template <typename ProjectionType>
bool AssertLC1BoundaryCapProjection(
	FAutomationTestBase& Test,
	const ProjectionType&,
	const FBlueprintLensExplanationModel&,
	long)
{
	Test.AddError(TEXT(
		"M10_STAGE1B_BOUNDARY_CAPS_MISSING: the LC1 projection exposes no typed "
		"boundary-cap collection"));
	return false;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RailProjectionTest,
	"BlueprintLens.Editor.LC1RailProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RailProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("LC1 fixture loads for the rail projector"),
		LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FString OriginalIrSha = LoadResult.Model->Source.IrSha256;
	const FString OriginalPackageSha =
		LoadResult.Model->Source.BlueprintPackageSha256;
	const int32 OriginalUnits = LoadResult.Model->Units.Num();
	const int32 OriginalRelations = LoadResult.Model->Relations.Num();

	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(*LoadResult.Model);
	TestTrue(TEXT("LC1 rail projection is renderable"), Projection.IsRenderable());
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	TestEqual(TEXT("LC1 rail accounts exactly 14 units"),
		Projection.AllUnitIds.Num(), 14);
	TestEqual(TEXT("LC1 rail accounts exactly 13 relations"),
		Projection.AllRelationIds.Num(), 13);
	TestEqual(TEXT("LC1 rail orders every unit"),
		Projection.OrderedCanonicalUnits.Num(), 14);
	TestEqual(TEXT("LC1 rail orders every relation"),
		Projection.OrderedExecutionRelations.Num(), 13);

	TSet<FString> Occurrences;
	for (const FBlueprintLensLC1RailCanonicalUnit& Unit :
		 Projection.OrderedCanonicalUnits)
	{
		Occurrences.Add(Unit.UnitId);
	}
	TestEqual(TEXT("LC1 rail draws one canonical occurrence per unit"),
		Occurrences.Num(), 14);

	bool bOrderProven = true;
	for (int32 Index = 0; Index < Projection.OrderedExecutionRelations.Num();
		 ++Index)
	{
		const FBlueprintLensLC1RailExecutionRelation& Relation =
			Projection.OrderedExecutionRelations[Index];
		bOrderProven = bOrderProven &&
			Relation.SourceUnitId ==
				Projection.OrderedCanonicalUnits[Index].UnitId &&
			Relation.TargetUnitId ==
				Projection.OrderedCanonicalUnits[Index + 1].UnitId &&
			LoadResult.Model->FindRelation(Relation.RelationId) != nullptr;
	}
	TestTrue(TEXT("LC1 rail order is the order the ledger proves"), bOrderProven);
	TestEqual(TEXT("LC1 rail terminates at the criterion"),
		Projection.OrderedCanonicalUnits.Last().UnitId,
		LoadResult.Model->CriterionUnitId);
	TestTrue(TEXT("LC1 rail marks the terminal station as the criterion"),
		Projection.OrderedCanonicalUnits.Last().bIsCriterion);
	TestEqual(
		TEXT("LC1 rail maps the Event BeginPlay display label"),
		Projection.OrderedCanonicalUnits[0].DisplayLabel,
		FString(TEXT("Event BeginPlay")));
	TestEqual(
		TEXT("LC1 rail maps the first intermediate display label"),
		Projection.OrderedCanonicalUnits[1].DisplayLabel,
		FString(TEXT("Set LC1Step01Complete")));
	TestEqual(
		TEXT("LC1 rail maps the criterion display label"),
		Projection.CriterionDisplayLabel,
		FString(TEXT("Set LC1Ready")));

	TestTrue(TEXT("LC1 rail integrity hash verifies"),
		Projection.HasValidIntegrity());
	const FBlueprintLensLC1RailProjection Rebuilt =
		FBlueprintLensLC1RailProjector::Build(*LoadResult.Model);
	TestEqual(TEXT("LC1 rail projection is deterministic"),
		Rebuilt.ProjectionIntegrityHash, Projection.ProjectionIntegrityHash);

	TestEqual(TEXT("LC1 rail leaves the Explanation unit count unchanged"),
		LoadResult.Model->Units.Num(), OriginalUnits);
	TestEqual(TEXT("LC1 rail leaves the Explanation relation count unchanged"),
		LoadResult.Model->Relations.Num(), OriginalRelations);
	TestEqual(TEXT("LC1 rail leaves the IR hash unchanged"),
		LoadResult.Model->Source.IrSha256, OriginalIrSha);
	TestEqual(TEXT("LC1 rail leaves the package hash unchanged"),
		LoadResult.Model->Source.BlueprintPackageSha256, OriginalPackageSha);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RailBoundaryCapTest,
	"BlueprintLens.Editor.LC1RailBoundaryCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RailBoundaryCapTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(
		TEXT("LC1 fixture loads for boundary-cap projection"),
		LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensExplanationModel BoundaryExplanation =
		MakeBoundaryRailExplanation(*LoadResult.Model);
	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(BoundaryExplanation);
	TestTrue(
		TEXT("LC1 boundary-bearing explanation remains renderable"),
		Projection.IsRenderable());
	return AssertLC1BoundaryCapProjection(
		*this,
		Projection,
		BoundaryExplanation,
		0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RailSurfaceTest,
	"BlueprintLens.Editor.LC1RailSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RailSurfaceTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("LC1 fixture loads for the rail surface"),
		LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(*LoadResult.Model);
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	const TArray<FString> ProvenOrder = LC1RailProvenOrder(Projection);

	const FLC1RailUnavailableBackend NoElk(
		EBlueprintLensLayoutBackendKind::ElkLayered);
	const FLC1RailUnavailableBackend NoGraphviz(
		EBlueprintLensLayoutBackendKind::GraphvizDot);

	for (const float Width : LC1RailReviewedWidths)
	{
		const FString Where = FString::Printf(TEXT("at %.0f"), Width);
		const FBlueprintLensLC1RailLayoutSessionResult Session =
			FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
				Projection, *LoadResult.Model, Width, NoElk, NoGraphviz);
		TestTrue(*FString::Printf(TEXT("LC1 rail session is renderable %s"), *Where),
			Session.IsRenderable(Projection));
		TestEqual(
			*FString::Printf(TEXT("LC1 rail records every backend attempt %s"), *Where),
			Session.Attempts.Num(), 3);
		TestEqual(
			*FString::Printf(TEXT("LC1 rail geometry has one owning backend %s"), *Where),
			Session.Layout.LayoutLedger.Backend,
			EBlueprintLensLayoutBackendKind::Deterministic);

		const FBlueprintLensLC1RailSurfaceLayout Surface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				Projection, Session, Width);
		TestTrue(*FString::Printf(TEXT("LC1 rail surface is renderable %s"), *Where),
			Surface.IsRenderable(Projection));
		TestEqual(
			*FString::Printf(TEXT("LC1 rail draws the complete spine %s"), *Where),
			Surface.DrawnUnitCount(), 14);
		TestEqual(
			*FString::Printf(TEXT("LC1 rail drawn plus folded equals the total %s"), *Where),
			Surface.Radius.DrawnUnitIds.Num() + Surface.Radius.FoldedUnitIds.Num(),
			Projection.AllUnitIds.Num());
		TestTrue(
			*FString::Printf(TEXT("LC1 rail preserves the proven order %s"), *Where),
			Surface.CanonicalUnitIds == ProvenOrder);
		TestTrue(
			*FString::Printf(TEXT("LC1 rail docks the criterion %s"), *Where),
			Surface.CriterionDockBounds.bIsValid &&
				Surface.Radius.DrawnUnitIds.Contains(Projection.CriterionUnitId));
		TestEqual(
			*FString::Printf(TEXT("LC1 spine has two route extensions %s"), *Where),
			Surface.SpineRoute.Num(),
			Surface.Stations.Num() + 2);
		if (!Surface.Stations.IsEmpty() && Surface.CriterionDockBounds.bIsValid &&
			Surface.SpineRoute.Num() >= 2)
		{
			TestTrue(
				*FString::Printf(TEXT("LC1 spine starts above the first station %s"), *Where),
				Surface.SpineRoute[0].Y < Surface.Stations[0].Position.Y);
			TestTrue(
				*FString::Printf(TEXT("LC1 spine ends at or below the criterion dock top %s"), *Where),
				Surface.SpineRoute.Last().Y >= Surface.CriterionDockBounds.Min.Y);
			TestTrue(
				*FString::Printf(TEXT("LC1 spine endpoint is inside the criterion dock %s"), *Where),
				Surface.CriterionDockBounds.IsInsideOrOn(Surface.SpineRoute.Last()));
			bool bSpineMonotonic = true;
			for (int32 PointIndex = 1; PointIndex < Surface.SpineRoute.Num(); ++PointIndex)
			{
				bSpineMonotonic = bSpineMonotonic &&
					Surface.SpineRoute[PointIndex].Y >=
					Surface.SpineRoute[PointIndex - 1].Y;
			}
			TestTrue(
				*FString::Printf(TEXT("LC1 spine is monotonic non-decreasing %s"), *Where),
				bSpineMonotonic);
		}
		for (const FBlueprintLensLC1RailStation& Station : Surface.Stations)
		{
			if (Station.bIsCriterion)
			{
				continue;
			}
			const FBlueprintLensLC1RailSurfaceLabel* UnitLabel =
				Surface.Labels.FindByPredicate(
					[&Station](const FBlueprintLensLC1RailSurfaceLabel& Label)
					{
						return Label.Key == Station.UnitId;
					});
			TestNotNull(
				*FString::Printf(TEXT("LC1 station has a matching row label %s"), *Where),
				UnitLabel);
			if (UnitLabel != nullptr)
			{
				const float MeasuredTextHeight =
					MeasureLC1SurfaceLabelText(*UnitLabel).Y;
				TestTrue(
					*FString::Printf(TEXT("LC1 station dot centers on rendered row text %s"), *Where),
					FMath::IsNearlyEqual(
						Station.Position.Y,
						UnitLabel->MeasuredBounds.Min.Y + MeasuredTextHeight * 0.5f,
						0.5f));
			}
		}
		const FBlueprintLensLC1RailStation* CriterionStation =
			Surface.Stations.FindByPredicate(
				[&Projection](const FBlueprintLensLC1RailStation& Station)
				{
					return Station.UnitId == Projection.CriterionUnitId &&
						Station.bIsCriterion;
				});
		const FBlueprintLensLC1RailSurfaceLabel* CriterionLabel =
			Surface.Labels.FindByPredicate(
				[&Projection](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("criterion") &&
						Label.UnitId == Projection.CriterionUnitId;
				});
		TestNotNull(
			*FString::Printf(TEXT("LC1 criterion station exists %s"), *Where),
			CriterionStation);
		TestNotNull(
			*FString::Printf(TEXT("LC1 criterion name label exists %s"), *Where),
			CriterionLabel);
		if (CriterionStation != nullptr && CriterionLabel != nullptr)
		{
			const float MeasuredTextHeight =
				MeasureLC1SurfaceLabelText(*CriterionLabel).Y;
			TestTrue(
				*FString::Printf(TEXT("LC1 criterion dot centers on rendered name text %s"), *Where),
				FMath::IsNearlyEqual(
					CriterionStation->Position.Y,
					CriterionLabel->MeasuredBounds.Min.Y + MeasuredTextHeight * 0.5f,
					0.5f));
			TestTrue(
				*FString::Printf(TEXT("LC1 criterion dot remains inside its dock %s"), *Where),
				Surface.CriterionDockBounds.IsInsideOrOn(CriterionStation->Position));
		}
		if (CriterionStation != nullptr)
		{
			TestTrue(
				*FString::Printf(TEXT("LC1 spine terminates at the criterion station %s"), *Where),
				!Surface.SpineRoute.IsEmpty() &&
				FMath::IsNearlyEqual(
					Surface.SpineRoute.Last().Y,
					CriterionStation->Position.Y,
					0.5f));
		}
		const FString ExpectedStageLabel = FString::Printf(
			TEXT("This question's execution route follows proven predecessors backward "
				"toward %s: %d units and %d relations."),
			*Projection.CriterionDisplayLabel,
			Projection.AllUnitIds.Num(),
			Projection.AllRelationIds.Num());
		const TArray<FBlueprintLensLC1RailSurfaceLabel> StageLabels =
			Surface.Labels.FilterByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("stage");
				});
		TestEqual(
			*FString::Printf(TEXT("LC1 rail has one projection-counted stage label %s"), *Where),
			StageLabels.Num(),
			1);
		if (StageLabels.Num() == 1)
		{
			TestEqual(
				*FString::Printf(TEXT("LC1 stage label takes both counts from the projection %s"), *Where),
				StageLabels[0].Text,
				ExpectedStageLabel);
		}
		const TArray<FBlueprintLensLC1RailSurfaceLabel> RelationAnnotations =
			Surface.Labels.FilterByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("relation-annotation");
				});
		int32 EntryAnnotations = 0;
		int32 ThenAnnotations = 0;
		for (const FBlueprintLensLC1RailSurfaceLabel& Label : RelationAnnotations)
		{
			EntryAnnotations += Label.Text == TEXT("entry") ? 1 : 0;
			ThenAnnotations += Label.Text == TEXT("then") ? 1 : 0;
		}
		// The criterion is the dock, not a regular annotation row. Every other
		// drawn unit is relation-annotated in the proven projection order.
		const int32 DrawnAnnotationRows = Surface.DrawnUnitCount() -
			(Surface.Radius.DrawnUnitIds.Contains(Projection.CriterionUnitId) ? 1 : 0);
		TestEqual(
			*FString::Printf(TEXT("LC1 rail has one entry annotation %s"), *Where),
			EntryAnnotations,
			1);
		TestEqual(
			*FString::Printf(TEXT("LC1 rail has then annotations for every subsequent row %s"), *Where),
			ThenAnnotations,
			FMath::Max(DrawnAnnotationRows - 1, 0));
		const TArray<FBlueprintLensLC1RailSurfaceLabel> CriterionCaptions =
			Surface.Labels.FilterByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("criterion-caption");
				});
		TestEqual(
			*FString::Printf(TEXT("LC1 criterion dock has one caption %s"), *Where),
			CriterionCaptions.Num(),
			1);
		if (CriterionCaptions.Num() == 1)
		{
			TestEqual(
				*FString::Printf(TEXT("LC1 criterion dock caption is CRITERION %s"), *Where),
				CriterionCaptions[0].Text,
				FString(TEXT("CRITERION")));
		}
		const TArray<FBlueprintLensLC1RailSurfaceLabel> ScaleRuleLabels =
			Surface.Labels.FilterByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("scale-rule");
				});
		TestEqual(
			*FString::Printf(TEXT("LC1 rail has one measured scale-rule label %s"), *Where),
			ScaleRuleLabels.Num(),
			1);
		if (ScaleRuleLabels.Num() == 1 && StageLabels.Num() == 1)
		{
			TestTrue(
				*FString::Printf(TEXT("LC1 scale rule is bounded above the stage %s"), *Where),
				Surface.ScaleRuleBounds.bIsValid &&
					ScaleRuleLabels[0].MeasuredBounds.bIsValid &&
					Surface.ScaleRuleBounds.Max.Y <=
						StageLabels[0].MeasuredBounds.Min.Y &&
					ScaleRuleLabels[0].Text == Surface.Radius.ReaderText);
		}
		const TArray<FBlueprintLensLC1RailSurfaceLabel> SelectionExplanationLabels =
			Surface.Labels.FilterByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("selection-explanation");
				});
		TestEqual(
			*FString::Printf(TEXT("LC1 rail has one measured selection explanation %s"), *Where),
			SelectionExplanationLabels.Num(),
			1);
		if (SelectionExplanationLabels.Num() == 1)
		{
			TestTrue(
				*FString::Printf(TEXT("LC1 selection explanation is near the criterion dock %s"), *Where),
				Surface.SelectionExplanationBounds.bIsValid &&
					SelectionExplanationLabels[0].MeasuredBounds.bIsValid &&
					Surface.SelectionExplanationBounds.Min.Y >=
						Surface.CriterionDockBounds.Max.Y &&
					SelectionExplanationLabels[0].Text.Contains(
						TEXT("Selecting any unit opens its Blueprint source at 1:1.")) &&
					SelectionExplanationLabels[0].Text.Contains(
						TEXT("Selection is focus, never a runtime claim.")));
		}
		TestTrue(
			*FString::Printf(TEXT("LC1 rail has zero label-label intersections %s"), *Where),
			Surface.HasNoLabelIntersections());
		TestTrue(
			*FString::Printf(TEXT("LC1 rail has zero label-route intersections %s"), *Where),
			Surface.HasNoLabelRouteIntersections());
		const bool bRouteIsActuallyBounded =
			Surface.Radius.CurrentRadius < Projection.OrderedCanonicalUnits.Num() - 1;
		TestEqual(
			*FString::Printf(TEXT("LC1 bounded-radius claim matches the route %s"), *Where),
			Surface.Radius.ReaderText.Contains(TEXT("bounded radius")),
			bRouteIsActuallyBounded);
		const bool bRadiusDiffersFromNormal =
			Surface.Radius.CurrentRadius != Surface.Radius.DefaultRadius;
		TestEqual(
			*FString::Printf(TEXT("LC1 normal-radius comparison is non-redundant %s"), *Where),
			Surface.Radius.ReaderText.Contains(TEXT("normally shows")),
			bRadiusDiffersFromNormal);
		// The counter owns drawn-of-total; the rule must not restate it.
		TestFalse(
			*FString::Printf(TEXT("LC1 radius rule does not restate the counter %s"), *Where),
			Surface.Radius.ReaderText.Contains(TEXT("UNITS DRAWN")));
	}

	// The criterion is docked at every radius, not only the default one.
	const FBlueprintLensLC1RailLayoutSessionResult Session =
		FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
			Projection, *LoadResult.Model, 700.0f, NoElk, NoGraphviz);
	for (int32 Radius = 0; Radius <= 13; ++Radius)
	{
		const FBlueprintLensLC1RailSurfaceLayout Surface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				Projection, Session, 700.0f, Radius);
		TestTrue(
			*FString::Printf(TEXT("LC1 rail draws the criterion at radius %d"), Radius),
			Surface.IsRenderable(Projection) &&
				Surface.CriterionDockBounds.bIsValid &&
				Surface.Radius.DrawnUnitIds.Contains(Projection.CriterionUnitId));
		TestEqual(
			*FString::Printf(TEXT("LC1 rail accounts every unit at radius %d"), Radius),
			Surface.Radius.DrawnUnitIds.Num() + Surface.Radius.FoldedUnitIds.Num(),
			Projection.AllUnitIds.Num());
		TestEqual(
			*FString::Printf(TEXT("LC1 rail draws exactly the radius at %d"), Radius),
			Surface.DrawnUnitCount(), Radius + 1);
	}

	// M2 must be reachable, not merely stated: a longer chain folds and emits
	// the counted boundary.
	const FBlueprintLensExplanationModel LongChain = MakeFoldableChain(20);
	const FBlueprintLensLC1RailProjection LongProjection =
		FBlueprintLensLC1RailProjector::Build(LongChain);
	TestTrue(TEXT("LC1 rail projects a 22-unit chain"),
		LongProjection.IsRenderable());
	if (LongProjection.IsRenderable())
	{
		const FBlueprintLensLC1RailLayoutSessionResult LongSession =
			FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
				LongProjection, LongChain, 700.0f, NoElk, NoGraphviz);
		const FBlueprintLensLC1RailSurfaceLayout Folded =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				LongProjection, LongSession, 700.0f);
		TestTrue(TEXT("LC1 rail folds beyond the default radius"),
			Folded.IsRenderable(LongProjection) &&
				Folded.Radius.FoldedUnitIds.Num() == 8);
		TestEqual(TEXT("LC1 rail folded run stays accounted"),
			Folded.Radius.DrawnUnitIds.Num() + Folded.Radius.FoldedUnitIds.Num(),
			LongProjection.AllUnitIds.Num());
		TestTrue(TEXT("LC1 rail emits a counted boundary naming the omitted count"),
			Folded.Radius.FoldBoundaryBounds.bIsValid &&
				Folded.Radius.FoldReaderText.Contains(TEXT("8 earlier units are not drawn")));
		TestTrue(TEXT("LC1 rail boundary names the recovery route"),
			Folded.Radius.FoldReaderText.Contains(TEXT("raise the radius")));
		TestTrue(TEXT("LC1 rail still docks the criterion when folded"),
			Folded.CriterionDockBounds.bIsValid);
	}

	// A mixed Explanation names counts at one scale. Deferred relations remain in
	// the boundary ledger; the stage disclosure counts only relations on the rail.
	FBlueprintLensExplanationModel Mixed = MakeFoldableChain(4);
	FBlueprintLensRelation DeferredRelation;
	DeferredRelation.Id = TEXT("relation.controls_execution.reader-count");
	DeferredRelation.SourceUnitId = Mixed.Units[0].Id;
	DeferredRelation.TargetUnitId = Mixed.Units.Last().Id;
	DeferredRelation.Kind = EBlueprintLensRelationKind::ControlsExecution;
	DeferredRelation.Label = TEXT("controls execution");
	DeferredRelation.SourceEdgeIds = {TEXT("edge.controls_execution.reader-count")};
	Mixed.Relations.Add(MoveTemp(DeferredRelation));
	const FBlueprintLensLC1RailProjection MixedProjection =
		FBlueprintLensLC1RailProjector::Build(Mixed);
	TestTrue(TEXT("mixed-count probe reaches the shared rail"),
		MixedProjection.IsRenderable());
	if (MixedProjection.IsRenderable())
	{
		const FBlueprintLensLC1RailLayoutSessionResult MixedSession =
			FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
				MixedProjection, Mixed, 700.0f, NoElk, NoGraphviz);
		const FBlueprintLensLC1RailSurfaceLayout MixedSurface =
			FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
				MixedProjection, MixedSession, 700.0f);
		const FBlueprintLensLC1RailSurfaceLabel* StageLabel =
			MixedSurface.Labels.FindByPredicate(
				[](const FBlueprintLensLC1RailSurfaceLabel& Label)
				{
					return Label.Key == TEXT("stage");
				});
		TestNotNull(TEXT("mixed rail retains one route heading"), StageLabel);
		if (StageLabel != nullptr)
		{
			TestEqual(
				TEXT("mixed stage relation count equals the rail relation ledger"),
				LC1StageRelationCount(StageLabel->Text),
				MixedProjection.AllRelationIds.Num());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RailStateTest,
	"BlueprintLens.Editor.LC1RailState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RailStateTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("LC1 fixture loads for the rail panel"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(*LoadResult.Model);
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	const FName RailProbeTag(TEXT("BlueprintLens.Automation.SharedExecutionRail"));

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.Model;
	Panel->ResolveSources();
	Panel->bLC1RailEvidence = false;
	Panel->LC1SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	FString ReaderText = SlateWidgetText(PanelRoot);
	const FString ExpectedHeader = FString::Printf(
		TEXT("CRITERION \u00B7 %s \u00B7 %s \u00B7 %d NODES \u00B7 %d EDGES"),
		*Projection.CriterionDisplayLabel,
		*LoadResult.Model->Query.Direction.Replace(TEXT("_"), TEXT(" ")).ToUpper(),
		LoadResult.Model->Counts.SourceNodes,
		LoadResult.Model->Counts.SourceEdges);
	const FString ExpectedHint = FString::Printf(
		TEXT("Read the proven predecessor chain toward %s. "
				 "The criterion remains docked at the end of the rail."),
		*Projection.CriterionDisplayLabel);
	TestTrue(TEXT("LC1 panel header uses the English criterion display label"),
		ReaderText.Contains(ExpectedHeader) &&
			ExpectedHeader.Contains(TEXT("Set LC1Ready")));
	TestFalse(TEXT("LC1 panel header contains no CJK code point"),
		ContainsCjkCodePoint(ExpectedHeader));
	TestTrue(TEXT("LC1 rail hint uses the English criterion display label"),
		ReaderText.Contains(ExpectedHint) &&
			ExpectedHint.Contains(TEXT("Set LC1Ready")));
	TestFalse(TEXT("LC1 rail hint contains no CJK code point"),
		ContainsCjkCodePoint(ExpectedHint));

	TestTrue(TEXT("LC1 panel dispatches to the shared Execution Rail"),
		SlateHasWidgetTag(PanelRoot, RailProbeTag));
	TestFalse(TEXT("LC1 automation probe is never reader-facing text"),
		ReaderText.Contains(RailProbeTag.ToString()));
	const FBlueprintLensLC1RailLayoutSessionResult M6ProbeSession =
		FBlueprintLensLC1RailLayoutSession::Build(
			Projection, *LoadResult.Model, 700.0f);
	TSharedRef<SBlueprintLensPanel> M6ProbePanel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	const FBlueprintLensCompositeRailSlots M6ProbeSlots =
		FBlueprintLensCompositeRailSlotProjector::Build(
			*LoadResult.Model,
			Projection);
	const TSharedRef<SWidget> M6Rail = M6ProbePanel->BuildM6CausalRailContent(
		LoadResult.Model,
		Projection,
		M6ProbeSession,
		M6ProbeSlots,
		TSharedPtr<SWidget>(),
		FString(),
		700.0f);
	TestTrue(TEXT("M6 shared rail exposes a non-reader automation probe"),
		SlateHasWidgetTag(M6Rail, RailProbeTag));
	TestFalse(TEXT("M6 shared-rail probe differs from its reader heading"),
		SlateWidgetText(M6Rail).Contains(RailProbeTag.ToString()));
	// The reopen was presentation migration only. The rail must not make the
	// three frozen conditions unreachable, nor introduce a selected winner.
	TestTrue(TEXT("LC1 rail keeps the frozen condition switcher reachable"),
		ReaderText.Contains(TEXT("LC1 COMPARISON")) &&
			ReaderText.Contains(TEXT("NO SELECTED WINNER")));
	TestTrue(TEXT("LC1 rail canvas is live"), Panel->LC1RailCanvas.IsValid());
	if (!Panel->LC1RailCanvas.IsValid())
	{
		return false;
	}

	// Drawn comes from the layout that placed the stations; total comes from the
	// ledger. The pair must appear once, from those two sources.
	const FString Counter = FString::Printf(
		TEXT("%d OF %d UNITS DRAWN"),
		Panel->LC1RailCanvas->GetSurface().DrawnUnitCount(),
		Projection.AllUnitIds.Num());
	TestTrue(TEXT("LC1 counter reads drawn from layout against the ledger total"),
		ReaderText.Contains(Counter));
	TestEqual(TEXT("LC1 counter states drawn-of-total exactly once"),
		LC1RailOccurrences(ReaderText, TEXT("UNITS DRAWN")), 1);
	const FBlueprintLensLC1RailSurfaceLayout& PanelSurface =
		Panel->LC1RailCanvas->GetSurface();
	const bool bPanelRadiusIsBounded =
		PanelSurface.Radius.CurrentRadius < Projection.AllUnitIds.Num() - 1;
	TestTrue(TEXT("LC1 panel states the truthful scale rule beside the counter"),
		ReaderText.Contains(
			bPanelRadiusIsBounded
				? TEXT("Scale: bounded radius")
				: TEXT("Scale: complete route")));
	TestTrue(TEXT("LC1 panel paints the scale rule as a measured surface label"),
		Panel->LC1RailCanvas->GetSurface().Labels.ContainsByPredicate(
			[&](const FBlueprintLensLC1RailSurfaceLabel& Label)
			{
				return Label.Key == TEXT("scale-rule") &&
					Label.Text == Panel->LC1RailCanvas->GetSurface().Radius.ReaderText;
			}));
	TestTrue(TEXT("LC1 panel states both selection explanation sentences"),
		ReaderText.Contains(TEXT("Selecting any unit opens its Blueprint source at 1:1.")) &&
			ReaderText.Contains(TEXT("Selection is focus, never a runtime claim.")));

	TestEqual(TEXT("LC1 rail default exposes no source action"),
		SlateButtonsWithLabel(
			PanelRoot, TEXT("Open selected item in Blueprint")).Num(), 0);
	TestFalse(TEXT("LC1 Summary withholds relation IDs"),
		ReaderText.Contains(Projection.AllRelationIds[0]));

	Panel->SelectLC1Unit(Projection.OrderedCanonicalUnits[0].UnitId);
	ReaderText = SlateWidgetText(PanelRoot);
	TestEqual(TEXT("LC1 rail selection exposes exactly one source action"),
		SlateButtonsWithLabel(
			PanelRoot, TEXT("Open selected item in Blueprint")).Num(), 1);
	// Hit-testing resolves to the unit the reader aimed at. Whether the rail
	// then *paints* that selection is visible-review evidence, not automation.
	const FBlueprintLensLC1RailSurfaceLayout& HitSurface =
		Panel->LC1RailCanvas->GetSurface();
	if (HitSurface.Stations.Num() == 14)
	{
		const FBlueprintLensLC1RailStation& Probe = HitSurface.Stations[3];
		TestEqual(TEXT("LC1 rail hit region resolves to its own station"),
			Panel->LC1RailCanvas->ResolveUnitAtLocalPositionForTesting(
				Probe.HitRegion.GetCenter()),
			Probe.UnitId);
	}
	TestTrue(TEXT("LC1 rail survives a selection rebuild"),
		Panel->LC1RailCanvas.IsValid() &&
			SlateHasWidgetTag(PanelRoot, RailProbeTag));

	// Both were LC2 owner-review defects: width and scroll must survive rebuilds.
	Panel->LC1RailScrollOffset = 42.0f;
	Panel->LC1RailLayoutWidth = 617.0f;
	Panel->SelectLC1Unit(Projection.OrderedCanonicalUnits[1].UnitId);
	TestTrue(TEXT("LC1 rail carries the layout width across a rebuild"),
		FMath::IsNearlyEqual(Panel->LC1RailLayoutWidth, 617.0f, 1.0f) ||
			Panel->LC1RailLayoutWidth >= 430.0f);
	TestTrue(TEXT("LC1 rail scroll box survives a rebuild"),
		Panel->LC1RailScrollBox.IsValid());
	IConsoleVariable* LC1ReviewScrollOffset =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("BlueprintLens.LC1ReviewScrollOffset"));
	TestNotNull(
		TEXT("LC1 below-fold review scroll control is registered"),
		LC1ReviewScrollOffset);
	if (LC1ReviewScrollOffset != nullptr && Panel->LC1RailScrollBox.IsValid())
	{
		const float PreviousReviewScrollOffset = LC1ReviewScrollOffset->GetFloat();
		LC1ReviewScrollOffset->Set(180.0f, ECVF_SetByCode);
		Panel->Tick(FGeometry(), 0.0, 0.0f);
		TestEqual(
			TEXT("LC1 below-fold review control drives the real rail scroll box"),
			Panel->LC1RailScrollBox->GetScrollOffset(),
			180.0f);
		LC1ReviewScrollOffset->Set(
			PreviousReviewScrollOffset,
			ECVF_SetByCode);
		Panel->Tick(FGeometry(), 0.0, 0.0f);
	}

	Panel->SetLC1RailDensity(true);
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("LC1 Evidence density reveals the technical ledger"),
		ReaderText.Contains(Projection.ProjectionIntegrityHash) &&
			ReaderText.Contains(Projection.AllRelationIds[0]));
	Panel->SetLC1RailDensity(false);

	// Reachability has two directions. The switcher being visible from the rail
	// does not establish that the rail is recoverable once a condition is
	// chosen, and asserting only the first direction is what let the one-way
	// door ship. Every condition must round-trip.
	const EBlueprintLensLC1DisclosureCandidate RoundTripCandidates[] = {
		EBlueprintLensLC1DisclosureCandidate::PlainOrderedOutline,
		EBlueprintLensLC1DisclosureCandidate::EvidenceBackedRegions,
		EBlueprintLensLC1DisclosureCandidate::PairedPseudocode};
	for (const EBlueprintLensLC1DisclosureCandidate Candidate :
		RoundTripCandidates)
	{
		Panel->SetLC1DisclosureCandidate(Candidate);
		ReaderText = SlateWidgetText(PanelRoot);
		TestFalse(TEXT("LC1 chosen condition replaces the rail"),
			SlateHasWidgetTag(PanelRoot, RailProbeTag));
		TestTrue(TEXT("LC1 chosen condition states the way back"),
			ReaderText.Contains(TEXT("ACTIVE CONDITION AGAIN")));

		Panel->SetLC1DisclosureCandidate(Candidate);
		ReaderText = SlateWidgetText(PanelRoot);
		TestTrue(TEXT("LC1 condition round-trips to the shared Execution Rail"),
			SlateHasWidgetTag(PanelRoot, RailProbeTag));
		TestFalse(TEXT("LC1 rail drops the return hint once it is the surface"),
			ReaderText.Contains(TEXT("ACTIVE CONDITION AGAIN")));
		TestTrue(TEXT("LC1 round-trip restores a live rail canvas"),
			Panel->LC1RailCanvas.IsValid());
	}
	// The rail is the default presentation, not a fourth condition: the return
	// must clear the option rather than record a winner.
	TestFalse(TEXT("LC1 round-trip leaves no selected condition"),
		Panel->LC1DisclosureCandidate.IsSet());

	// Stage 1b is not complete until typed boundary caps reach Slate. Use the
	// frozen 14-unit shape here so this failure cannot be blamed on the old route
	// gate; every title and status-specific disclosure must be visible exactly
	// once.
	const FBlueprintLensExplanationModel BoundaryExplanation =
		MakeBoundaryRailExplanation(*LoadResult.Model);
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(BoundaryExplanation);
	Panel->ResolveSources();
	Panel->LC1DisclosureCandidate.Reset();
	Panel->LC1SelectedUnitId.Reset();
	Panel->bLC1RailEvidence = false;
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("LC1 boundary-bearing fixture still reaches the rail"),
		SlateHasWidgetTag(PanelRoot, RailProbeTag));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FBlueprintLensUnit& BoundaryUnit = BoundaryExplanation.Units[Index];
		TestEqual(
			TEXT("LC1 Slate draws each boundary title exactly once"),
			LC1RailOccurrences(ReaderText, BoundaryUnit.Title),
			1);
		TestEqual(
			TEXT("LC1 Slate draws each boundary disclosure exactly once"),
			LC1RailOccurrences(
				ReaderText,
				LC1BoundaryDisclosure(BoundaryUnit.SemanticStatus)),
			1);
	}

	// The rail belongs to the Explanation shape, not to one asset or one frozen
	// count. This deliberately differs on both dimensions.
	FBlueprintLensExplanationModel OtherShape = MakeFoldableChain(4);
	OtherShape.Source.BlueprintAssetPath =
		TEXT("/Game/LensCorpus/BP_M10_OtherShape.BP_M10_OtherShape");
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(OtherShape);
	Panel->ResolveSources();
	Panel->LC1DisclosureCandidate.Reset();
	Panel->LC1SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	ReaderText = SlateWidgetText(PanelRoot);
	TestTrue(
		TEXT("LC1 shape predicate accepts a non-LC1 non-14-unit explanation"),
		Panel->IsLC1ComparisonModel());
	TestTrue(
		TEXT("LC1 panel routes a non-LC1 non-14-unit explanation to the rail"),
		SlateHasWidgetTag(PanelRoot, RailProbeTag));

	// Aggregation is Stage 4. Stage 1b must still account for every unit above
	// the 24-entity budget and state exactly how many were not drawn.
	FBlueprintLensExplanationModel OverBudget = MakeFoldableChain(23);
	OverBudget.Source.BlueprintAssetPath =
		TEXT("/Game/LensCorpus/BP_M10_OverBudget.BP_M10_OverBudget");
	TestTrue(TEXT("Stage 1b over-budget probe really exceeds 24 units"),
		OverBudget.Units.Num() > 24);
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(OverBudget);
	Panel->ResolveSources();
	Panel->LC1DisclosureCandidate.Reset();
	Panel->LC1SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	ReaderText = SlateWidgetText(PanelRoot);
	const int32 ExpectedNotDrawn = OverBudget.Units.Num() - 14;
	TestTrue(
		TEXT("LC1 shape predicate accepts an explanation above the entity budget"),
		Panel->IsLC1ComparisonModel());
	TestTrue(
		TEXT("LC1 over-budget explanation reaches the rail"),
		SlateHasWidgetTag(PanelRoot, RailProbeTag));
	TestTrue(
		TEXT("LC1 over-budget rail reports the exact not-drawn count"),
		ReaderText.Contains(FString::Printf(
			TEXT("%d earlier units are not drawn"),
			ExpectedNotDrawn)));
	TestTrue(
		TEXT("LC1 over-budget rail reports how to recover omitted rows"),
		ReaderText.Contains(TEXT("raise the radius")));
	TestTrue(
		TEXT("LC1 over-budget counter keeps the full ledger total visible"),
		ReaderText.Contains(FString::Printf(
			TEXT("OF %d UNITS DRAWN"),
			OverBudget.Units.Num())));

	// The fallback keeps a stable condition ID rather than repurposing one.
	FBlueprintLensExplanationModel Broken = *LoadResult.Model;
	Broken.Relations.RemoveAt(0);
	Panel->Model = MakeShared<FBlueprintLensExplanationModel>(Broken);
	Panel->ResolveSources();
	Panel->LC1SelectedUnitId.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	ReaderText = SlateWidgetText(PanelRoot);
	TestFalse(TEXT("LC1 falls back when the rail is not provable"),
		SlateHasWidgetTag(PanelRoot, RailProbeTag));
	TestFalse(TEXT("LC1 fallback draws no partial rail"),
		ReaderText.Contains(TEXT("bounded radius")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC1RailMutationTest,
	"BlueprintLens.Editor.LC1RailMutations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC1RailMutationTest::RunTest(const FString&)
{
	const FBlueprintLensLoadResult LoadResult =
		FBlueprintLensExplanationLoader::LoadFile(LC1FixturePath());
	TestTrue(TEXT("LC1 fixture loads for the rail mutations"),
		LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC1RailProjection Projection =
		FBlueprintLensLC1RailProjector::Build(*LoadResult.Model);
	if (!Projection.IsRenderable())
	{
		AddError(Projection.DiagnosticCode);
		return false;
	}
	const FLC1RailUnavailableBackend NoElk(
		EBlueprintLensLayoutBackendKind::ElkLayered);
	const FLC1RailUnavailableBackend NoGraphviz(
		EBlueprintLensLayoutBackendKind::GraphvizDot);
	const FBlueprintLensLC1RailLayoutSessionResult Session =
		FBlueprintLensLC1RailLayoutSession::BuildWithBackends(
			Projection, *LoadResult.Model, 700.0f, NoElk, NoGraphviz);
	const FBlueprintLensLC1RailSurfaceLayout Baseline =
		FBlueprintLensLC1RailSurfaceLayoutBuilder::Build(
			Projection, Session, 700.0f);
	TestTrue(TEXT("LC1 mutation baseline is renderable"),
		Baseline.IsRenderable(Projection));

	// Mutation 0: a route that folds back at its final point. The route invariant
	// must reject the reversal independently of label clearance.
	FBlueprintLensLC1RailSurfaceLayout ReversingSpine = Baseline;
	if (ReversingSpine.SpineRoute.Num() >= 2)
	{
		const int32 FinalPointIndex = ReversingSpine.SpineRoute.Num() - 1;
		ReversingSpine.SpineRoute[FinalPointIndex].Y =
			ReversingSpine.SpineRoute[FinalPointIndex - 1].Y - 1.0f;
	}
	TestEqual(
		TEXT("Killed: a reversing spine route"),
		ReversingSpine.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_SPINE_ROUTE_NOT_MONOTONIC")));

	// Mutation 1: a folded run with no counted boundary. A radius folds from the
	// front of the run, so fold the entry unit, not the criterion.
	FBlueprintLensLC1RailSurfaceLayout UnaccountedFold = Baseline;
	UnaccountedFold.Radius.FoldedUnitIds.Add(
		UnaccountedFold.Radius.DrawnUnitIds[0]);
	UnaccountedFold.Radius.DrawnUnitIds.RemoveAt(0);
	UnaccountedFold.Stations.RemoveAt(0);
	UnaccountedFold.Radius.FoldBoundaryBounds.Init();
	UnaccountedFold.Radius.FoldReaderText.Reset();
	TestEqual(TEXT("Killed: a folded run with no counted boundary"),
		UnaccountedFold.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_RADIUS_FOLD_UNACCOUNTED")));

	// Mutation 2: a radius that hides the criterion.
	FBlueprintLensLC1RailSurfaceLayout HiddenCriterion = Baseline;
	HiddenCriterion.Radius.DrawnUnitIds.Remove(Projection.CriterionUnitId);
	HiddenCriterion.Radius.FoldedUnitIds.Add(Projection.CriterionUnitId);
	TestEqual(TEXT("Killed: a radius that hides the criterion"),
		HiddenCriterion.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_CRITERION_NOT_DRAWN")));

	// Mutation 3: a reordered pair whose order the ledger proves.
	FBlueprintLensLC1RailSurfaceLayout Reordered = Baseline;
	Reordered.CanonicalUnitIds.Swap(0, 1);
	TestEqual(TEXT("Killed: a reordered proven pair"),
		Reordered.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_ORDER_NOT_PROVEN")));

	// Mutation 4: a duplicated canonical unit presented as a second unit.
	FBlueprintLensLC1RailSurfaceLayout Duplicated = Baseline;
	const FString RepeatedUnitId = Duplicated.CanonicalUnitIds[0];
	Duplicated.CanonicalUnitIds.Add(RepeatedUnitId);
	TestEqual(TEXT("Killed: a duplicated canonical unit"),
		Duplicated.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED")));

	// Mutation 5: a drawn count that disagrees with drawn plus folded. Drop a
	// station that is not the criterion so the count, not the dock, is what fails.
	FBlueprintLensLC1RailSurfaceLayout MiscountedDrawn = Baseline;
	MiscountedDrawn.Stations.RemoveAt(0);
	TestEqual(TEXT("Killed: a drawn count that disagrees with drawn plus folded"),
		MiscountedDrawn.InvariantDiagnostic(Projection),
		FString(TEXT("LC1_RAIL_RADIUS_FOLD_UNACCOUNTED")));

	// The projector refuses the same failures at their own seam.
	FBlueprintLensExplanationModel DuplicateUnit = *LoadResult.Model;
	const FBlueprintLensUnit RepeatedUnit = DuplicateUnit.Units[0];
	DuplicateUnit.Units.Add(RepeatedUnit);
	TestEqual(TEXT("Killed: a duplicated unit reaching the projector"),
		FBlueprintLensLC1RailProjector::Build(DuplicateUnit).DiagnosticCode,
		FString(TEXT("LC1_RAIL_CANONICAL_IDENTITY_FAILED")));

	FBlueprintLensExplanationModel MissingCriterion = *LoadResult.Model;
	MissingCriterion.CriterionUnitId.Reset();
	TestEqual(TEXT("Killed: an explanation with no criterion"),
		FBlueprintLensLC1RailProjector::Build(MissingCriterion).DiagnosticCode,
		FString(TEXT("LC1_RAIL_CRITERION_NOT_DRAWN")));

	FBlueprintLensExplanationModel ForkedChain = *LoadResult.Model;
	ForkedChain.Relations[1].SourceUnitId = ForkedChain.Relations[0].SourceUnitId;
	TestEqual(TEXT("Killed: a chain the ledger does not prove linear"),
		FBlueprintLensLC1RailProjector::Build(ForkedChain).DiagnosticCode,
		FString(TEXT("LC1_RAIL_ORDER_NOT_PROVEN")));

	FBlueprintLensExplanationModel ShortChain = *LoadResult.Model;
	ShortChain.Units.Pop();
	ShortChain.Relations.Pop();
	ShortChain.Relations.Pop();
	TestEqual(TEXT("Killed: a frozen fixture that no longer accounts 14 and 13"),
		FBlueprintLensLC1RailProjector::Build(ShortChain).DiagnosticCode,
		FString(TEXT("LC1_RAIL_TRUTH_COVERAGE_FAILED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4AsyncSlateContractTest,
	"BlueprintLens.Editor.LC4AsyncSlateContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4AsyncSlateContractTest::RunTest(const FString&)
{
	const FBlueprintLensLC4AsyncLoadResult LoadResult =
		FBlueprintLensLC4AsyncProfileLoader::LoadFile(LC4AsyncProfilePath());
	TestTrue(TEXT("LC4 async frozen profile loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	TestEqual(TEXT("LC4 async profile retains four invocations"), LoadResult.Profile->Invocations.Num(), 4);
	TestEqual(TEXT("LC4 async profile retains 44 relations"), LoadResult.Profile->Counts.RelationCount, 44);
	TestEqual(TEXT("LC4 async profile retains four proofs"), LoadResult.Profile->Counts.IncomparabilityCheckCount, 4);

	const FBlueprintLensLC4AsyncProjection AFirst =
		FBlueprintLensLC4AsyncProjector::Build(*LoadResult.Profile, TEXT("A_FIRST"));
	const FBlueprintLensLC4AsyncProjection BFirst =
		FBlueprintLensLC4AsyncProjector::Build(*LoadResult.Profile, TEXT("B_FIRST"));
	TestTrue(TEXT("A_FIRST projection is accountable"), AFirst.IsRenderable());
	TestTrue(TEXT("B_FIRST projection is accountable"), BFirst.IsRenderable());
	TestEqual(TEXT("A_FIRST owns two retained runs"), AFirst.Invocations.Num(), 2);
	TestEqual(TEXT("A_FIRST owns 22 relations"), AFirst.AllRelationIds.Num(), 22);
	TestEqual(TEXT("A_FIRST owns two incomparability proofs"), AFirst.Proofs.Num(), 2);
	TestEqual(TEXT("A/B structural truth signatures match"), AFirst.StructuralSignature, BFirst.StructuralSignature);
	TestNotEqual(TEXT("A/B observation identities remain distinct"), AFirst.EvidenceSignature, BFirst.EvidenceSignature);

	const FBlueprintLensLC4AsyncVisualStyle& VisualStyle =
		FBlueprintLensLC4AsyncVisualStyle::FrozenEffectTarget();
	TestEqual(
		TEXT("LC4 async converts frozen CSS pixels to Slate points at 96 DPI"),
		BlueprintLensLC4AsyncFont(24, EBlueprintLensLC4AsyncFontWeight::Bold).Size,
		18.0f);
	TestEqual(TEXT("LC4 async canvas uses the frozen background"), VisualStyle.CanvasFillHex, FString(TEXT("#11151b")));
	TestEqual(TEXT("LC4 async action dock uses the frozen fill"), VisualStyle.ActionsFillHex, FString(TEXT("#1a2028")));
	TestEqual(TEXT("LC4 async action dock uses the frozen stroke"), VisualStyle.ActionsStrokeHex, FString(TEXT("#43505c")));
	TestEqual(TEXT("LC4 async launch role uses the frozen cyan"), VisualStyle.CyanHex, FString(TEXT("#54d6df")));
	TestEqual(TEXT("LC4 async continuation role uses the frozen orange"), VisualStyle.OrangeHex, FString(TEXT("#e9a568")));
	TestEqual(TEXT("LC4 async proof role uses the frozen purple"), VisualStyle.PurpleHex, FString(TEXT("#bda7ff")));
	TestEqual(TEXT("LC4 async barrier role uses the frozen gold"), VisualStyle.GoldHex, FString(TEXT("#f3ca62")));
	TestEqual(TEXT("LC4 async release role uses the frozen green"), VisualStyle.GreenHex, FString(TEXT("#76d49b")));
	TestEqual(TEXT("LC4 async primary text uses the frozen white"), VisualStyle.PrimaryTextHex, FString(TEXT("#f5f7fa")));
	TestEqual(TEXT("LC4 async muted text uses the frozen grey"), VisualStyle.MutedTextHex, FString(TEXT("#9ba8b4")));
	TestEqual(TEXT("LC4 async launch nodes keep radius 9"), VisualStyle.LaunchRadius, 9.0f);
	TestEqual(TEXT("LC4 async event nodes keep radius 12"), VisualStyle.EventRadius, 12.0f);
	TestEqual(TEXT("LC4 async node outlines keep width 3"), VisualStyle.NodeOutlineWidth, 3.0f);
	TestEqual(TEXT("LC4 async source route keeps width 3"), VisualStyle.SourceRouteWidth, 3.0f);
	TestEqual(TEXT("LC4 async barrier radius matches the target"), VisualStyle.BarrierRadius, 2.0f);
	TestEqual(TEXT("LC4 async criterion radius matches the target"), VisualStyle.CriterionRadius, 8.0f);
	TestEqual(TEXT("LC4 async Frontier radius matches the target"), VisualStyle.FrontierRadius, 6.0f);
	TestEqual(TEXT("LC4 async action dock radius matches the target"), VisualStyle.ActionsRadius, 8.0f);
	TestEqual(TEXT("LC4 async arrows are filled triangles"), VisualStyle.ArrowStyle, EBlueprintLensLC4AsyncArrowStyle::FilledTriangle);
	TestEqual(
		TEXT("LC4 async rounded brushes remain white for element tinting"),
		BlueprintLensLC4AsyncRoundedBrushFill(),
		FLinearColor::White);
	const FLinearColor FrozenActionFill = FLinearColor(FColor::FromHex(*VisualStyle.ActionsFillHex));
	TestEqual(
		TEXT("LC4 async rounded boxes apply the semantic fill as the draw-element tint"),
		BlueprintLensLC4AsyncBoxElementTint(FrozenActionFill),
		FrozenActionFill);

	for (const TPair<float, FVector2D>& Target : {
		TPair<float, FVector2D>(430.0f, FVector2D(430.0f, 1148.0f)),
		TPair<float, FVector2D>(480.0f, FVector2D(480.0f, 1134.0f)),
		TPair<float, FVector2D>(700.0f, FVector2D(700.0f, 1050.0f))})
	{
		const FBlueprintLensLC4AsyncLayout Layout =
			FBlueprintLensLC4AsyncLayoutBuilder::Build(AFirst, Target.Key);
		auto FindLabel = [&Layout](const TCHAR* Id) -> const FBlueprintLensLC4AsyncLabel*
		{
			return Layout.Labels.FindByPredicate(
				[Id](const FBlueprintLensLC4AsyncLabel& Label)
				{
					return Label.Id == Id;
				});
		};
		TestTrue(TEXT("LC4 async layout covers the accountable projection"), Layout.CoversProjection(AFirst));
		TestTrue(TEXT("LC4 async layout owns one complete ledger"), Layout.HasValidSharedLedger());
		TestTrue(TEXT("LC4 async text remains on-canvas and unobstructed"), Layout.HasNoTextOrRouteCollisions());
		if (!Layout.HasNoTextOrRouteCollisions())
		{
			TArray<const FBlueprintLensLC4AsyncLabel*> ProtectedLabels;
			int32 ProtectedIndex = 0;
			for (const FBlueprintLensLC4AsyncLabel& Label : Layout.Labels)
			{
				if (ProtectedIndex < Layout.ProtectedLabelBounds.Num() &&
					Layout.ProtectedLabelBounds[ProtectedIndex].Min.Equals(
						Label.Position,
						0.1f))
				{
					ProtectedLabels.Add(&Label);
					++ProtectedIndex;
				}
			}
			for (int32 Left = 0; Left < ProtectedLabels.Num(); ++Left)
			{
				const FBox2D& LeftBounds = Layout.ProtectedLabelBounds[Left];
				const FBox2D CanvasBounds(FVector2D::ZeroVector, Layout.CanvasSize);
				if (!CanvasBounds.IsInsideOrOn(LeftBounds.Min) ||
					!CanvasBounds.IsInsideOrOn(LeftBounds.Max))
				{
					AddInfo(FString::Printf(
						TEXT("LC4 async %.0f protected label is off-canvas: %s"),
						Target.Key,
						*ProtectedLabels[Left]->Id));
				}
				for (int32 Right = Left + 1; Right < ProtectedLabels.Num(); ++Right)
				{
					const FBox2D& RightBounds = Layout.ProtectedLabelBounds[Right];
					if (LeftBounds.Min.X < RightBounds.Max.X - KINDA_SMALL_NUMBER &&
						LeftBounds.Max.X > RightBounds.Min.X + KINDA_SMALL_NUMBER &&
						LeftBounds.Min.Y < RightBounds.Max.Y - KINDA_SMALL_NUMBER &&
						LeftBounds.Max.Y > RightBounds.Min.Y + KINDA_SMALL_NUMBER)
					{
						AddInfo(FString::Printf(
							TEXT("LC4 async %.0f protected-label collision: %s (%.1f,%.1f %.1fx%.1f) <> %s (%.1f,%.1f %.1fx%.1f)"),
							Target.Key,
							*ProtectedLabels[Left]->Id,
							LeftBounds.Min.X,
							LeftBounds.Min.Y,
							LeftBounds.GetSize().X,
							LeftBounds.GetSize().Y,
							*ProtectedLabels[Right]->Id,
							RightBounds.Min.X,
							RightBounds.Min.Y,
							RightBounds.GetSize().X,
							RightBounds.GetSize().Y));
					}
				}
			}
			auto SegmentIntersectsBounds = [](const FVector2D& A, const FVector2D& B, const FBox2D& Bounds)
			{
				const FVector2D Delta = B - A;
				float Enter = 0.0f;
				float Leave = 1.0f;
				const float P[4] = {-Delta.X, Delta.X, -Delta.Y, Delta.Y};
				const float Q[4] = {
					A.X - Bounds.Min.X, Bounds.Max.X - A.X,
					A.Y - Bounds.Min.Y, Bounds.Max.Y - A.Y};
				for (int32 Index = 0; Index < 4; ++Index)
				{
					if (FMath::IsNearlyZero(P[Index]))
					{
						if (Q[Index] < 0.0f) return false;
						continue;
					}
					const float Ratio = Q[Index] / P[Index];
					if (P[Index] < 0.0f) Enter = FMath::Max(Enter, Ratio);
					else Leave = FMath::Min(Leave, Ratio);
					if (Enter > Leave) return false;
				}
				return true;
			};
			for (int32 RouteIndex = 0; RouteIndex < Layout.PaintedRoutes.Num(); ++RouteIndex)
			{
				const TArray<FVector2D>& Route = Layout.PaintedRoutes[RouteIndex];
				for (int32 SegmentIndex = 0; SegmentIndex + 1 < Route.Num(); ++SegmentIndex)
				{
					for (int32 LabelIndex = 0; LabelIndex < ProtectedLabels.Num(); ++LabelIndex)
					{
						if (SegmentIntersectsBounds(
								Route[SegmentIndex],
								Route[SegmentIndex + 1],
								Layout.ProtectedLabelBounds[LabelIndex]))
						{
							AddInfo(FString::Printf(
								TEXT("LC4 async %.0f route %d segment %d intersects %s"),
								Target.Key,
								RouteIndex,
								SegmentIndex,
								*ProtectedLabels[LabelIndex]->Id));
						}
					}
				}
			}
		}
		const FBlueprintLensLC4AsyncLabel* Eyebrow = FindLabel(TEXT("header.eyebrow"));
		TestNotNull(TEXT("LC4 async canvas retains its frozen identity eyebrow"), Eyebrow);
		if (Eyebrow != nullptr)
		{
			TestEqual(TEXT("LC4 async eyebrow text matches the frozen target"), Eyebrow->Text, FString(TEXT("LC4 · ASYNC · SELECTED")));
			TestEqual(TEXT("LC4 async eyebrow uses bold weight"), Eyebrow->Weight, EBlueprintLensLC4AsyncFontWeight::Bold);
		}
		const FBlueprintLensLC4AsyncLabel* Title = FindLabel(TEXT("title"));
		TestNotNull(TEXT("LC4 async title remains present"), Title);
		if (Title != nullptr)
		{
			TestEqual(TEXT("LC4 async title uses bold weight"), Title->Weight, EBlueprintLensLC4AsyncFontWeight::Bold);
			TestEqual(TEXT("LC4 async title size matches the frozen width"), Title->FontSize, Target.Key == 430.0f ? 22 : 24);
		}
		const FBlueprintLensLC4AsyncLabel* Question0 = FindLabel(TEXT("question.0"));
		TestNotNull(TEXT("LC4 async primary question line remains present"), Question0);
		if (Question0 != nullptr)
		{
			TestEqual(
				TEXT("LC4 async question wording matches the frozen target"),
				Question0->Text,
				Target.Key == 430.0f
					? FString(TEXT("Why only after both complete —"))
					: FString(TEXT("Why only after both complete — and can either finish first?")));
			TestEqual(TEXT("LC4 async question uses regular weight"), Question0->Weight, EBlueprintLensLC4AsyncFontWeight::Regular);
		}
		TestEqual(
			TEXT("Only 430 wraps the frozen question to a second line"),
			FindLabel(TEXT("question.1")) != nullptr,
			Target.Key == 430.0f);
		if (Target.Key == 430.0f)
		{
			TestEqual(
				TEXT("LC4 async second question line matches the frozen target"),
				FindLabel(TEXT("question.1"))->Text,
				FString(TEXT("and can either finish first?")));
		}
		for (const FBlueprintLensLC4AsyncLabel& Label : Layout.Labels)
		{
			const FVector2D Measured = MeasureLC4AsyncLabelText(Label);
			TestTrue(
				*FString::Printf(
					TEXT("LC4 async %.0f label %s owns enough Slate space"),
					Target.Key,
					*Label.Id),
				Measured.X + 10.0f <= Label.ApproximateSize.X + 0.5f &&
					Measured.Y + 2.0f <= Label.ApproximateSize.Y + 0.5f);
		}
		if (Target.Key < 700.0f)
		{
			TestNotNull(TEXT("Narrow proof retains A not-reach B"), FindLabel(TEXT("proof.0")));
			TestNotNull(TEXT("Narrow proof retains B not-reach A"), FindLabel(TEXT("proof.1")));
			TestNotNull(TEXT("Narrow proof retains relation-set completeness"), FindLabel(TEXT("proof.set")));
			TestNotNull(TEXT("Narrow proof retains the explicit conclusion"), FindLabel(TEXT("proof.complete")));
			if (FindLabel(TEXT("proof.set")) != nullptr && FindLabel(TEXT("proof.complete")) != nullptr)
			{
				TestEqual(TEXT("Narrow proof completeness wording is not compressed"), FindLabel(TEXT("proof.set"))->Text, FString(TEXT("relation set complete")));
				TestEqual(TEXT("Narrow proof conclusion wording is explicit"), FindLabel(TEXT("proof.complete"))->Text, FString(TEXT("therefore A ∥ B")));
			}
			for (const FBlueprintLensLC4AsyncLabel& Label : Layout.Labels)
			{
				if (!Label.Id.StartsWith(TEXT("proof")))
				{
					continue;
				}
				TestTrue(
					*FString::Printf(
						TEXT("LC4 async %.0f proof bracket clears %s"),
						Target.Key,
						*Label.Id),
					Label.Position.Y > Layout.NarrowProofBracketY + 12.0f);
			}
		}
		else
		{
			for (const TPair<const TCHAR*, int32>& ProofTarget : {
				TPair<const TCHAR*, int32>(TEXT("proof.0"), 12),
				TPair<const TCHAR*, int32>(TEXT("proof.1"), 12),
				TPair<const TCHAR*, int32>(TEXT("proof.set"), 12),
				TPair<const TCHAR*, int32>(TEXT("proof.complete"), 14)})
			{
				const FBlueprintLensLC4AsyncLabel* ProofLabel = FindLabel(ProofTarget.Key);
				TestNotNull(TEXT("Wide proof label remains present"), ProofLabel);
				if (ProofLabel != nullptr)
				{
					TestEqual(TEXT("Wide proof label keeps its frozen size"), ProofLabel->FontSize, ProofTarget.Value);
					TestTrue(TEXT("Wide proof label starts in the frozen right-side proof column"), ProofLabel->Position.X >= 574.0f);
				}
			}
		}
		TestEqual(TEXT("LC4 async exact-width canvas matches the frozen target"), Layout.CanvasSize, Target.Value);
		TestEqual(TEXT("LC4 async layout requests LayeredPorts"), Layout.LayoutRequest.Profile, EBlueprintLensLayoutProfile::LayeredPorts);
		const FBlueprintLensLC4AsyncLayoutSessionResult Session =
			FBlueprintLensLC4AsyncLayoutSession::Build(AFirst, Target.Key);
		TestTrue(TEXT("LC4 async production session is renderable"), Session.IsRenderable(AFirst));
		TSharedRef<SBlueprintLensLC4AsyncPartialOrder> Canvas =
			SNew(SBlueprintLensLC4AsyncPartialOrder)
				.Projection(AFirst)
				.InitialSession(Session)
				.ActiveActionId(TEXT("proof"));
		Canvas->SlatePrepass(1.0f);
		TestTrue(TEXT("LC4 async native Slate canvas keeps exact desired size"),
			Canvas->GetDesiredSize().Equals(Target.Value, 0.1f));
		TArray<TSharedRef<SWidget>> Widgets;
		CollectSlateWidgets(Canvas, Widgets);
		int32 TextCount = 0;
		bool bTextClipped = true;
		for (const TSharedRef<SWidget>& Widget : Widgets)
		{
			if (Widget->GetTypeAsString() == TEXT("STextBlock"))
			{
				++TextCount;
				bTextClipped &= Widget->GetClipping() == EWidgetClipping::ClipToBoundsAlways;
			}
		}
		TestTrue(TEXT("LC4 async canvas owns native Slate text"), TextCount > 0);
		TestTrue(TEXT("LC4 async every text child is a clipping boundary"), bTextClipped);
		TestEqual(
			TEXT("LC4 async barrier semantics render exactly once"),
			CountSlateTextEqual(Canvas, TEXT("AND · 2/2 ARRIVED")),
			1);
		TestEqual(
			TEXT("LC4 async criterion label renders exactly once"),
			CountSlateTextWithPrefix(
				Canvas,
				TEXT("Set LC4AsyncComplete = true")),
			1);
		const FString CanvasText = SlateWidgetText(Canvas);
		TestTrue(
			TEXT("LC4 async Frontier retains its bounded-profile disclosure"),
			CanvasText.Contains(TEXT("FRONTIER · BOUNDED POSITIVE PROFILE")) &&
				CanvasText.Contains(
					TEXT("observed order only · 0.050 s ticks · 8-tick deadline")) &&
				CanvasText.Contains(
					TEXT("no external service · cancel/incomplete → ABSTAINED")));
		TestTrue(TEXT("Proof is persistent active action"), Canvas->IsActionActive(TEXT("proof")));
		TestFalse(TEXT("Open source is command-only"), Canvas->IsActionActive(TEXT("open-source")));
	}

	const FBlueprintLensLC4AsyncProjection Frontier =
		FBlueprintLensLC4AsyncProjector::Build(*LoadResult.Profile, TEXT("UNKNOWN"));
	TestEqual(TEXT("unsupported variants fail closed to Frontier"), Frontier.Status, EBlueprintLensLC4AsyncProjectionStatus::Frontier);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4AsyncPanelStateTest,
	"BlueprintLens.Editor.LC4AsyncPanelState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4AsyncPanelStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC4AsyncLoadResult LoadResult =
		FBlueprintLensLC4AsyncProfileLoader::LoadFile(LC4AsyncProfilePath());
	TestTrue(TEXT("LC4 async profile loads for panel"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC4AsyncProfile = LoadResult.Profile;
	Panel->LC4AsyncVariant = TEXT("A_FIRST");
	Panel->LC4AsyncDetailMode = SBlueprintLensPanel::ELC4AsyncDetailMode::None;
	Panel->ResolveSources();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TSharedRef<SWidget> PanelRoot = Panel;
	TestTrue(TEXT("LC4 async scroll box clips all descendants"),
		Panel->LC4AsyncScrollBox.IsValid() &&
		Panel->LC4AsyncScrollBox->GetClipping() == EWidgetClipping::ClipToBoundsAlways);
	if (!Panel->LC4AsyncScrollBox.IsValid())
	{
		AddError(TEXT("LC4 async panel did not construct its selected-surface scroll box"));
		return false;
	}
	IConsoleVariable* ReviewScrollOffset =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("BlueprintLens.LC4ReviewScrollOffset"));
	TestNotNull(
		TEXT("LC4 async exact-width review scroll control is registered"),
		ReviewScrollOffset);
	if (ReviewScrollOffset != nullptr)
	{
		const float PreviousReviewScrollOffset = ReviewScrollOffset->GetFloat();
		ReviewScrollOffset->Set(180.0f, ECVF_SetByCode);
		Panel->Tick(FGeometry(), 0.0, 0.0f);
		TestEqual(
			TEXT("LC4 async exact-width review control drives the async scroll box"),
			Panel->LC4AsyncScrollBox->GetScrollOffset(),
			180.0f);
		ReviewScrollOffset->Set(
			PreviousReviewScrollOffset,
			ECVF_SetByCode);
	}
	const FString DefaultText = SlateWidgetText(PanelRoot);
	for (const TCHAR* Required : {
		TEXT("Partial-Order Join"), TEXT("A does not reach B"),
		TEXT("B does not reach A"), TEXT("relation set complete"),
		TEXT("therefore A ∥ B")})
	{
		TestTrue(FString::Printf(TEXT("LC4 async default surface contains %s"), Required), DefaultText.Contains(Required));
	}

	Panel->LC4AsyncScrollBox->SetScrollOffset(220.0f);
	Panel->HandleLC4AsyncAction(TEXT("proof"));
	TestEqual(TEXT("Proof preserves scroll offset"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 220.0f);
	const FString ProofText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("Proof detail exposes pairwise reachability basis"),
		ProofText.Contains(TEXT("pairwise_reachability_plus_completeness")));
	TestFalse(TEXT("Proof excludes raw relation ledger"), ProofText.Contains(LoadResult.Profile->Invocations[0].Relations[0].RelationId));

	Panel->LC4AsyncScrollBox->SetScrollOffset(260.0f);
	Panel->HandleLC4AsyncAction(TEXT("all-text"));
	TestEqual(TEXT("All text preserves scroll offset"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 260.0f);
	const FString AllText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("All text exposes all 22 selected relation ids"),
		AllText.Contains(LoadResult.Profile->Invocations[0].Relations[0].RelationId) &&
		AllText.Contains(LoadResult.Profile->Invocations[2].Relations.Last().RelationId));

	Panel->LC4AsyncScrollBox->SetScrollOffset(300.0f);
	Panel->HandleLC4AsyncAction(TEXT("evidence"));
	TestEqual(TEXT("Evidence preserves scroll offset"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 300.0f);
	const FString EvidenceText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("Evidence exposes profile and projection hashes"),
		EvidenceText.Contains(LoadResult.Profile->ProfileSha256) &&
		EvidenceText.Contains(TEXT("LC4_ASYNC_PARTIAL_ORDER_JOIN_COMPLETE")));

	Panel->LC4AsyncScrollBox->SetScrollOffset(340.0f);
	Panel->HandleLC4AsyncAction(TEXT("select"));
	TestEqual(TEXT("Select preserves scroll offset"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 340.0f);
	const FString SelectText = SlateWidgetText(PanelRoot);
	TestTrue(TEXT("Select exposes source actions"), SelectText.Contains(TEXT("Open Sequence launch")) && SelectText.Contains(TEXT("Open criterion")));

	Panel->LC4AsyncScrollBox->SetScrollOffset(380.0f);
	Panel->HandleLC4AsyncAction(TEXT("toggle-variant"));
	TestEqual(TEXT("Variant switch preserves scroll offset"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 380.0f);
	TestEqual(TEXT("Variant switch selects B_FIRST"), Panel->LC4AsyncVariant, FString(TEXT("B_FIRST")));
	TestTrue(TEXT("B_FIRST observation remains visible"), SlateWidgetText(PanelRoot).Contains(TEXT("B_FIRST")));

	Panel->LC4AsyncScrollBox->SetScrollOffset(420.0f);
	Panel->HandleLC4AsyncAction(TEXT("open-source"));
	TestEqual(TEXT("Open source is command-only and preserves scroll"), Panel->LC4AsyncScrollBox->GetScrollOffset(), 420.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC5ProfileProjectionTest,
	"BlueprintLens.Editor.LC5ProfileProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC5ProfileProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLC5LoadResult LoadResult =
		FBlueprintLensLC5ProfileLoader::LoadFile(LC5ProfilePath());
	TestTrue(TEXT("LC5 frozen contextual profile loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC5Profile& Profile = *LoadResult.Profile;
	TestEqual(TEXT("LC5 profile identity"), Profile.ProfileId,
		FString(TEXT("LC5_INTRA_BP_PURE_CALL_V1")));
	TestEqual(TEXT("LC5 resolved status"), Profile.Status,
		FString(TEXT("resolved_unique")));
	TestEqual(TEXT("LC5 keeps four static occurrences"), Profile.Occurrences.Num(), 4);
	TestEqual(TEXT("LC5 keeps three typed bindings"), Profile.Bindings.Num(), 3);
	TestEqual(TEXT("LC5 keeps four internal relations"), Profile.InternalRelations.Num(), 4);
	TestEqual(TEXT("LC5 keeps two context boundaries"), Profile.ContextBoundaries.Num(), 2);
	TestEqual(TEXT("LC5 max depth remains one"), Profile.MaxCallDepth, 1);
	TestTrue(TEXT("LC5 distinguishes parent and callee contexts"),
		Profile.CallContext.Id != Profile.CallContext.ParentId);
	for (const FBlueprintLensLC5Occurrence& Occurrence : Profile.Occurrences)
	{
		TestTrue(TEXT("source identity differs from occurrence identity"),
			Occurrence.SourceNodeId != Occurrence.OccurrenceId);
		TestTrue(TEXT("occurrence identity carries its call context"),
			Occurrence.OccurrenceId.Contains(Occurrence.CallContextId));
	}
	for (const FBlueprintLensLC5Binding& Binding : Profile.Bindings)
	{
		TestEqual(TEXT("all frozen bindings are int32"), Binding.CppType,
			FString(TEXT("int32")));
		TestEqual(TEXT("all frozen bindings have scalar containers"), Binding.Container,
			FString(TEXT("none")));
	}

	const FBlueprintLensLC5Projection Projection =
		FBlueprintLensLC5Projector::Build(Profile);
	TestTrue(TEXT("LC5 projection is accountable"), Projection.IsRenderable());
	TestEqual(TEXT("projection owns four occurrences"), Projection.Occurrences.Num(), 4);
	TestEqual(TEXT("projection owns three bindings"), Projection.Bindings.Num(), 3);
	TestEqual(TEXT("projection owns four internal relations"), Projection.InternalRelations.Num(), 4);
	TestEqual(TEXT("projection owns all nine relation identities"), Projection.AllRelationIds.Num(), 9);
	TestEqual(TEXT("projection owns four actions"), Projection.ActionIds.Num(), 4);
	TestTrue(TEXT("static-only boundary is explicit"),
		Projection.BoundaryText.ContainsByPredicate([](const FString& Text)
		{
			return Text.Contains(TEXT("static contextual occurrences")) &&
				Text.Contains(TEXT("runtime invocations"));
		}));

	FBlueprintLensLC5Profile Unresolved = Profile;
	Unresolved.Status = TEXT("unresolved");
	const FBlueprintLensLC5Projection Frontier =
		FBlueprintLensLC5Projector::Build(Unresolved);
	TestFalse(TEXT("unresolved profiles never render a body"), Frontier.IsRenderable());
	TestEqual(TEXT("unresolved profile becomes a reason-bearing Frontier"),
		Frontier.Status, EBlueprintLensLC5ProjectionStatus::Frontier);
	TestTrue(TEXT("Frontier retains a diagnostic"), !Frontier.DiagnosticCode.IsEmpty());

	FBlueprintLensLC5Profile Duplicate = Profile;
	const FBlueprintLensLC5Occurrence DuplicateOccurrence = Duplicate.Occurrences[0];
	Duplicate.Occurrences.Add(DuplicateOccurrence);
	TestFalse(TEXT("duplicate occurrence identities fail closed"),
		FBlueprintLensLC5Projector::Build(Duplicate).IsRenderable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC5LayoutContractTest,
	"BlueprintLens.Editor.LC5LayoutContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC5LayoutContractTest::RunTest(const FString&)
{
	const FBlueprintLensLC5LoadResult LoadResult =
		FBlueprintLensLC5ProfileLoader::LoadFile(LC5ProfilePath());
	TestTrue(TEXT("LC5 profile loads for layout"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC5Projection Projection =
		FBlueprintLensLC5Projector::Build(*LoadResult.Profile);
	const TArray<TPair<float, EBlueprintLensLC5LayoutMode>> Targets = {
		{430.0f, EBlueprintLensLC5LayoutMode::SingleColumn430},
		{480.0f, EBlueprintLensLC5LayoutMode::StackedDetail480},
		{700.0f, EBlueprintLensLC5LayoutMode::SideBySide700}};
	const TMap<float, FVector2D> ExpectedCanvasSizes = {
		{430.0f, FVector2D(430.0f, 1280.0f)},
		{480.0f, FVector2D(480.0f, 1200.0f)},
		{700.0f, FVector2D(700.0f, 1000.0f)}};
	for (const TPair<float, EBlueprintLensLC5LayoutMode>& Target : Targets)
	{
		const FBlueprintLensLC5Layout Layout =
			FBlueprintLensLC5LayoutBuilder::Build(Projection, Target.Key);
		TestEqual(TEXT("LC5 exact width selects the frozen responsive mode"),
			Layout.Mode, Target.Value);
		TestTrue(TEXT("LC5 layout covers the accountable projection"),
			Layout.CoversProjection(Projection));
		TestTrue(TEXT("LC5 layout has one complete normalized ledger"),
			Layout.HasValidSharedLedger());
		TestTrue(TEXT("LC5 labels and routes clear each other"),
			Layout.HasNoTextOrRouteCollisions());
		TestEqual(TEXT("LC5 canvas matches the frozen responsive target"),
			Layout.CanvasSize, ExpectedCanvasSizes.FindChecked(Target.Key));
		const auto FindLabel = [&Layout](const FString& Id)
		{
			return Layout.Labels.FindByPredicate([&Id](const FBlueprintLensLC5Label& Label)
			{
				return Label.Id == Id;
			});
		};
		const FBlueprintLensLC5Label* Question = FindLabel(TEXT("question"));
		const FBlueprintLensLC5Label* QuestionLine2 = FindLabel(TEXT("question.1"));
		TestTrue(TEXT("LC5 question discloses cross-boundary correspondence"),
			Question != nullptr && (Question->Text + TEXT(" ") +
				(QuestionLine2 != nullptr ? QuestionLine2->Text : FString()))
				.Contains(TEXT("across the call boundary")));
		TestTrue(TEXT("LC5 wide question uses an explicit safe line break"),
			Target.Key < 700.0f || (Question != nullptr && QuestionLine2 != nullptr &&
				Question->Text == TEXT("How does CalculateRecovery use CurrentHealth and Bonus to produce NewHealth,") &&
				QuestionLine2->Text == TEXT("and how do those values correspond across the call boundary?")));
		for (const TCHAR* RequiredLabelId : {
			TEXT("scope"), TEXT("width"), TEXT("call.current"), TEXT("call.bonus"),
			TEXT("call.result"), TEXT("entry.current"), TEXT("entry.bonus"),
			TEXT("return.type"), TEXT("relation.call_enter"),
			TEXT("relation.argument.current_health"), TEXT("relation.argument.bonus"),
			TEXT("relation.internal.bonus"), TEXT("relation.internal.current_health"),
			TEXT("relation.internal.execution"), TEXT("relation.internal.return_value"),
			TEXT("relation.result.new_health"), TEXT("relation.call_return")})
		{
			TestNotNull(
				FString::Printf(TEXT("LC5 target disclosure owns %s"), RequiredLabelId),
				FindLabel(RequiredLabelId));
		}
		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		for (const FBlueprintLensLC5Label& Label : Layout.Labels)
		{
			const FSlateFontInfo Font = BlueprintLensLC5Font(
				Label.FontSize, Label.bBold);
			float RequiredWidth = 0.0f;
			float RequiredHeight = 0.0f;
			TArray<FString> Lines;
			Label.Text.ParseIntoArrayLines(Lines, false);
			for (const FString& Line : Lines)
			{
				const FVector2D Measured = FontMeasure->Measure(Line, Font);
				RequiredWidth = FMath::Max(RequiredWidth, Measured.X);
				RequiredHeight += Measured.Y;
			}
			TestTrue(
				FString::Printf(TEXT("LC5 %.0f label %s owns enough measured Slate space required=%.1fx%.1f allocated=%.1fx%.1f"),
					Target.Key, *Label.Id, RequiredWidth + 2.0f, RequiredHeight,
					Label.Bounds.GetSize().X, Label.Bounds.GetSize().Y),
				RequiredWidth + 2.0f <= Label.Bounds.GetSize().X + 0.5f &&
					RequiredHeight <= Label.Bounds.GetSize().Y + 0.5f);
		}
		TestEqual(TEXT("LC5 request owns four occurrence nodes"),
			Layout.LayoutRequest.Nodes.Num(), 4);
		TestEqual(TEXT("LC5 request owns all nine relations"),
			Layout.LayoutRequest.Edges.Num(), 9);
		TestEqual(TEXT("LC5 request owns caller and callee regions"),
			Layout.LayoutRequest.Groups.Num(), 2);
		TestEqual(TEXT("LC5 layout owns all four source anchors"),
			Layout.SourceAnchors.Num(), 4);
		TestEqual(TEXT("LC5 layout owns four actions"),
			Layout.Actions.Num(), 4);

		const FBlueprintLensLC5LayoutSessionResult Session =
			FBlueprintLensLC5LayoutSession::Build(Projection, Target.Key);
		TestTrue(TEXT("LC5 layout session is renderable"),
			Session.IsRenderable(Projection));
		TestTrue(TEXT("LC5 session records ELK as the first attempt"),
			Session.Attempts.Num() >= 1 &&
			Session.Attempts[0].Backend == EBlueprintLensLayoutBackendKind::ElkLayered);
		TestEqual(TEXT("LC5 session accepts exactly one ledger"),
			Session.Attempts.FilterByPredicate([](const FBlueprintLensLC5LayoutAttempt& Attempt)
			{
				return Attempt.bAccepted;
			}).Num(), 1);
		for (const FBlueprintLensLayoutPortPlacement& ExpectedPort :
			Session.Layout.VisualOracleLedger.Ports)
		{
			const FBlueprintLensLayoutPortPlacement* ActualPort =
				Session.Layout.LayoutLedger.FindPort(
					ExpectedPort.UnitId, ExpectedPort.Label, ExpectedPort.bInput);
			TestTrue(TEXT("LC5 accepted ledger preserves every frozen port anchor"),
				ActualPort != nullptr && ActualPort->Position.Equals(ExpectedPort.Position, 1.0f));
		}
		for (const FBlueprintLensLayoutEdgePlacement& ExpectedEdge :
			Session.Layout.VisualOracleLedger.Edges)
		{
			const FBlueprintLensLayoutEdgePlacement* ActualEdge =
				Session.Layout.LayoutLedger.Edges.FindByPredicate(
					[&ExpectedEdge](const FBlueprintLensLayoutEdgePlacement& Edge)
					{
						return Edge.RelationId == ExpectedEdge.RelationId;
					});
			TestTrue(TEXT("LC5 accepted ledger preserves every frozen route"),
				ActualEdge != nullptr && ActualEdge->BendPoints == ExpectedEdge.BendPoints);
		}
	}

	FBlueprintLensLC5Projection BrokenEndpoint = Projection;
	BrokenEndpoint.InternalRelations[0].TargetOccurrenceId = TEXT("missing-occurrence");
	const FBlueprintLensLC5Layout BrokenLayout =
		FBlueprintLensLC5LayoutBuilder::Build(BrokenEndpoint, 700.0f);
	TestFalse(TEXT("an unaccounted endpoint never fabricates a route"),
		BrokenLayout.CoversProjection(BrokenEndpoint));
	TestTrue(TEXT("an unaccounted endpoint exposes an explicit diagnostic"),
		!BrokenLayout.DiagnosticCode.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC5SlateContractTest,
	"BlueprintLens.Editor.LC5SlateContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC5SlateContractTest::RunTest(const FString&)
{
	const FBlueprintLensLC5LoadResult LoadResult =
		FBlueprintLensLC5ProfileLoader::LoadFile(LC5ProfilePath());
	TestTrue(TEXT("LC5 profile loads for Slate"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC5Projection Projection =
		FBlueprintLensLC5Projector::Build(*LoadResult.Profile);
	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC5LayoutSessionResult Session =
			FBlueprintLensLC5LayoutSession::Build(Projection, Width);
		TSharedRef<SBlueprintLensLC5TypedPortal> Canvas =
			SNew(SBlueprintLensLC5TypedPortal)
			.Projection(Projection)
			.InitialSession(Session)
			.SelectedOccurrenceId(Projection.Occurrences[0].OccurrenceId)
			.ActiveActionId(TEXT("select"));
		TestTrue(TEXT("LC5 Slate canvas consumes a renderable ledger"),
			Canvas->GetLayoutForTesting().HasValidSharedLedger());
		TestEqual(TEXT("LC5 Slate root hard-clips descendants"),
			Canvas->GetClipping(), EWidgetClipping::ClipToBoundsAlways);
		TArray<TSharedRef<SWidget>> Widgets;
		CollectSlateWidgets(Canvas, Widgets);
		for (const TSharedRef<SWidget>& Widget : Widgets)
		{
			if (Widget->GetTypeAsString() == TEXT("STextBlock"))
			{
				TestEqual(TEXT("every LC5 text block is a hard clipping boundary"),
					Widget->GetClipping(), EWidgetClipping::ClipToBoundsAlways);
			}
		}
		TestTrue(TEXT("Select is the active visual action"),
			Canvas->IsActionActive(TEXT("select")));
		TestFalse(TEXT("Open source is never a persistent visual action"),
			Canvas->IsActionActive(TEXT("open_source")));
		TestEqual(TEXT("call selection highlights all three typed bindings"),
			Canvas->HighlightedRelationIdsForTesting().FilterByPredicate([](const FString& Id)
			{
				return Id.StartsWith(TEXT("binding:"));
			}).Num(), 3);
	}
	TestEqual(TEXT("LC5 rounded brush fill remains white"),
		BlueprintLensLC5RoundedBrushFill(), FLinearColor::White);
	const FLinearColor SemanticFill(0.2f, 0.3f, 0.4f, 1.0f);
	TestEqual(TEXT("LC5 box semantic fill is passed as element tint"),
		BlueprintLensLC5BoxElementTint(SemanticFill), SemanticFill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC5PanelStateTest,
	"BlueprintLens.Editor.LC5PanelState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC5PanelStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC5LoadResult LoadResult =
		FBlueprintLensLC5ProfileLoader::LoadFile(LC5ProfilePath());
	TestTrue(TEXT("LC5 profile loads for panel"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC5Profile = LoadResult.Profile;
	Panel->LC5DetailMode = SBlueprintLensPanel::ELC5DetailMode::None;
	TestEqual(
		TEXT("LC5 navigation binds the frozen Blueprint package hash"),
		Panel->Model->Source.BlueprintPackageSha256,
		FString(TEXT("FFB14E0C9AB22E8FCD71472E063EE6F8F6C74B1FE3F5BFA8F0490E93C0C831B9")));
	Panel->ResolveSources();
	Panel->PopulateExplanationOptions();
	TestEqual(TEXT("LC5 case selector has one unambiguous contextual-slice label"),
		Panel->ExplanationOptions.FilterByPredicate([](const TSharedPtr<FString>& Option)
		{
			return Option.IsValid() && *Option == TEXT("SlicingProbe.contextual-slice");
		}).Num(), 1);
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	TestTrue(TEXT("LC5 panel constructs a hard-clipped ScrollBox"),
		Panel->LC5ScrollBox.IsValid() &&
		Panel->LC5ScrollBox->GetClipping() == EWidgetClipping::ClipToBoundsAlways);
	if (!Panel->LC5ScrollBox.IsValid())
	{
		return false;
	}
	IConsoleVariable* LC5ReviewScrollOffset =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("BlueprintLens.LC5ReviewScrollOffset"));
	float PreviousLC5ReviewScrollOffset = 0.0f;
	TestNotNull(
		TEXT("LC5 exact-width review scroll control is registered"),
		LC5ReviewScrollOffset);
	if (LC5ReviewScrollOffset != nullptr)
	{
		PreviousLC5ReviewScrollOffset = LC5ReviewScrollOffset->GetFloat();
		LC5ReviewScrollOffset->Set(180.0f, ECVF_SetByCode);
		Panel->Tick(FGeometry(), 0.0, 0.0f);
		TestEqual(
			TEXT("LC5 exact-width review control drives the LC5 scroll box"),
			Panel->LC5ScrollBox->GetScrollOffset(),
			180.0f);
		Panel->LC5ScrollBox = SNew(SScrollBox);
		Panel->LC5ScrollOffset = 0.0f;
		Panel->Tick(FGeometry(), 0.0, 0.0f);
		TestEqual(
			TEXT("LC5 review offset is reapplied after the ScrollBox is rebuilt"),
			Panel->LC5ScrollBox->GetScrollOffset(),
			180.0f);
		LC5ReviewScrollOffset->Set(0.0f, ECVF_SetByCode);
		Panel->Tick(FGeometry(), 0.0, 0.0f);
	}

	const FBlueprintLensLC5Projection Projection =
		FBlueprintLensLC5Projector::Build(*LoadResult.Profile);
	const FString CallId = Projection.ContextBoundaries.FindByPredicate([](const auto& Item)
	{
		return Item.Kind == TEXT("call_enter");
	})->SourceOccurrenceId;
	const FString CalleeId = Projection.ContextBoundaries.FindByPredicate([](const auto& Item)
	{
		return Item.Kind == TEXT("call_enter");
	})->TargetOccurrenceId;
	const FBlueprintLensLC5Occurrence* CalleeOccurrence =
		Projection.Occurrences.FindByPredicate([&CalleeId](const auto& Item)
		{
			return Item.OccurrenceId == CalleeId;
		});
	TestNotNull(TEXT("callee occurrence is available for source navigation"), CalleeOccurrence);
	if (CalleeOccurrence != nullptr)
	{
		const FBlueprintLensResolvedSource* CalleeSource =
			Panel->ResolvedSources.Find(CalleeOccurrence->SourceNodeId);
		TestNotNull(TEXT("callee source is present in the navigation cache"), CalleeSource);
		if (CalleeSource != nullptr)
		{
			TestTrue(
				TEXT("callee source is navigable rather than stale"),
				CalleeSource->State == EBlueprintLensSourceState::Ready ||
					CalleeSource->State == EBlueprintLensSourceState::Unsaved);
			TestTrue(
				TEXT("callee source resolves the CalculateRecovery graph"),
				CalleeSource->Graph.IsValid() &&
					CalleeSource->Graph->GetPathName() ==
						TEXT("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery"));
		}
	}

	Panel->LC5ScrollBox->SetScrollOffset(180.0f);
	Panel->SelectLC5Occurrence(CallId);
	TestEqual(TEXT("call selection preserves scroll"),
		Panel->LC5ScrollBox->GetScrollOffset(), 180.0f);
	TestEqual(TEXT("call selection owns Selection mode"),
		Panel->LC5DetailMode, SBlueprintLensPanel::ELC5DetailMode::Selection);
	TestEqual(TEXT("call selection identity persists"), Panel->LC5SelectedOccurrenceId, CallId);

	Panel->LC5ScrollBox->SetScrollOffset(220.0f);
	Panel->HandleLC5Action(TEXT("show-complete-text"));
	TestEqual(TEXT("Complete text preserves scroll"), Panel->LC5ScrollBox->GetScrollOffset(), 220.0f);
	Panel->Tick(FGeometry(), 0.0, 0.0f);
	TestEqual(TEXT("Complete text remains at the preserved scroll on the next frame"),
		Panel->LC5ScrollBox->GetScrollOffset(), 220.0f);
	TestEqual(TEXT("Complete text is mutually exclusive"), Panel->LC5DetailMode,
		SBlueprintLensPanel::ELC5DetailMode::CompleteText);
	Panel->LC5ScrollBox->SetScrollOffset(260.0f);
	Panel->HandleLC5Action(TEXT("show-evidence"));
	TestEqual(TEXT("Evidence preserves scroll"), Panel->LC5ScrollBox->GetScrollOffset(), 260.0f);
	Panel->Tick(FGeometry(), 0.0, 0.0f);
	TestEqual(TEXT("Evidence remains at the preserved scroll on the next frame"),
		Panel->LC5ScrollBox->GetScrollOffset(), 260.0f);
	TestEqual(TEXT("Evidence replaces Complete text"), Panel->LC5DetailMode,
		SBlueprintLensPanel::ELC5DetailMode::Evidence);
	Panel->LC5ScrollBox->SetScrollOffset(300.0f);
	Panel->HandleLC5Action(TEXT("why-portal"));
	TestEqual(TEXT("Why portal preserves scroll"), Panel->LC5ScrollBox->GetScrollOffset(), 300.0f);
	Panel->Tick(FGeometry(), 0.0, 0.0f);
	TestEqual(TEXT("Why portal remains at the preserved scroll on the next frame"),
		Panel->LC5ScrollBox->GetScrollOffset(), 300.0f);
	TestEqual(TEXT("Why portal replaces Evidence"), Panel->LC5DetailMode,
		SBlueprintLensPanel::ELC5DetailMode::WhyPortal);
	const FString WhyText = SlateWidgetText(Panel);
	TestTrue(TEXT("Why portal explains resolution evidence"), WhyText.Contains(TEXT("resolved_unique")));
	TestTrue(TEXT("Why portal explains type and direction correspondence"),
		WhyText.Contains(TEXT("int32")) && WhyText.Contains(TEXT("direction")));
	TestTrue(TEXT("Why portal states the static-only boundary"),
		WhyText.Contains(TEXT("not runtime")));

	Panel->LC5ScrollBox->SetScrollOffset(340.0f);
	const auto ModeBeforeSource = Panel->LC5DetailMode;
	const FString SelectionBeforeSource = Panel->LC5SelectedOccurrenceId;
	Panel->HandleLC5Action(TEXT("open-source"));
	TestEqual(TEXT("Open source preserves scroll"), Panel->LC5ScrollBox->GetScrollOffset(), 340.0f);
	TestEqual(TEXT("Open source is command-only for detail mode"), Panel->LC5DetailMode, ModeBeforeSource);
	TestEqual(TEXT("Open source is command-only for selection"),
		Panel->LC5SelectedOccurrenceId, SelectionBeforeSource);

	Panel->LC5ScrollBox->SetScrollOffset(380.0f);
	Panel->SelectLC5Occurrence(CalleeId);
	TestEqual(TEXT("callee selection preserves scroll"), Panel->LC5ScrollBox->GetScrollOffset(), 380.0f);
	Panel->Tick(FGeometry(), 0.0, 0.0f);
	TestEqual(TEXT("callee selection remains at the preserved scroll on the next frame"),
		Panel->LC5ScrollBox->GetScrollOffset(), 380.0f);
	TestEqual(TEXT("callee selection persists its occurrence identity"),
		Panel->LC5SelectedOccurrenceId, CalleeId);
	if (LC5ReviewScrollOffset != nullptr)
	{
		LC5ReviewScrollOffset->Set(
			PreviousLC5ReviewScrollOffset,
			ECVF_SetByCode);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6ProfileTest,
	"BlueprintLens.Editor.LC6.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6ProfileTest::RunTest(const FString&)
{
	const auto Load = [](const FString& Core = LC6CoreProfilePath(),
		const FString& Query = LC6QueryProfilePath(),
		const FString& Readiness = LC6ReadinessPath(),
		const FString& Raw = LC6RawPath())
	{
		return FBlueprintLensLC6ProfileLoader::LoadFiles(
			Core, Query, Readiness, Raw);
	};

	const FBlueprintLensLC6LoadResult LoadResult = Load();
	TestTrue(TEXT("LC6 frozen four-input profile loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC6Profile& Profile = *LoadResult.Profile;
	TestEqual(TEXT("LC6 core profile identity"), Profile.CoreProfileId,
		FString(TEXT("LC6_CORE_BOUNDARY_MATRIX_V1")));
	TestEqual(TEXT("LC6 query profile identity"), Profile.QueryProfileId,
		FString(TEXT("LC6_MAX_UPSTREAM_HOPS_V1")));
	TestEqual(TEXT("LC6 readiness is frozen"), Profile.ReadinessStatus,
		FString(TEXT("TRUTH_FROZEN")));
	TestEqual(TEXT("LC6 owns four stable scenarios"), Profile.Scenarios.Num(), 4);
	const TArray<FString> ExpectedOrder = {
		TEXT("LC6_OPAQUE"), TEXT("LC6_UNCERTAIN"),
		TEXT("LC6_UNSUPPORTED"), TEXT("LC6_TRUNCATED")};
	for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
	{
		TestEqual(TEXT("LC6 scenario order is stable"),
			Profile.Scenarios[Index].ScenarioId, ExpectedOrder[Index]);
	}

	const auto FindScenario = [&Profile](const TCHAR* Id)
	{
		return Profile.Scenarios.FindByPredicate([Id](const auto& Scenario)
		{
			return Scenario.ScenarioId == Id;
		});
	};
	const FBlueprintLensLC6Scenario* Opaque = FindScenario(TEXT("LC6_OPAQUE"));
	const FBlueprintLensLC6Scenario* Uncertain = FindScenario(TEXT("LC6_UNCERTAIN"));
	const FBlueprintLensLC6Scenario* Unsupported = FindScenario(TEXT("LC6_UNSUPPORTED"));
	const FBlueprintLensLC6Scenario* Truncated = FindScenario(TEXT("LC6_TRUNCATED"));
	TestNotNull(TEXT("LC6 opaque scenario exists"), Opaque);
	TestNotNull(TEXT("LC6 uncertain scenario exists"), Uncertain);
	TestNotNull(TEXT("LC6 unsupported scenario exists"), Unsupported);
	TestNotNull(TEXT("LC6 truncated scenario exists"), Truncated);
	if (Opaque == nullptr || Uncertain == nullptr ||
		Unsupported == nullptr || Truncated == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("opaque status"), Opaque->Status, FString(TEXT("opaque")));
	TestEqual(TEXT("opaque reason"), Opaque->Reason,
		FString(TEXT("function_body_not_expanded")));
	TestEqual(TEXT("uncertain status"), Uncertain->Status,
		FString(TEXT("uncertain")));
	TestEqual(TEXT("uncertain reason"), Uncertain->Reason,
		FString(TEXT("node_family_not_in_supported_matrix_v1")));
	TestEqual(TEXT("unsupported status"), Unsupported->Status,
		FString(TEXT("unsupported")));
	TestEqual(TEXT("unsupported reason"), Unsupported->Reason,
		FString(TEXT("latent_function")));
	TestEqual(TEXT("truncated remains query-owned"), Truncated->TruthOwner,
		FString(TEXT("query_profile")));
	TestEqual(TEXT("truncated status"), Truncated->Status,
		FString(TEXT("truncated")));
	TestEqual(TEXT("query complete node count"), Truncated->CompleteNodeIds.Num(), 7);
	TestEqual(TEXT("query complete edge count"), Truncated->CompleteEdgeIds.Num(), 6);
	TestEqual(TEXT("query selected node count"), Truncated->SliceNodeIds.Num(), 4);
	TestEqual(TEXT("query selected edge count"), Truncated->SliceEdgeIds.Num(), 3);
	TestEqual(TEXT("query omitted node count"), Truncated->OmittedNodeCount, 3);
	TestEqual(TEXT("query omitted edge count"), Truncated->OmittedEdgeCount, 3);
	TestEqual(TEXT("query owns one Frontier"), Truncated->Frontiers.Num(), 1);
	TestEqual(TEXT("query includes seven hop distances"),
		Truncated->HopDistances.Num(), 7);
	TestEqual(TEXT("raw supplies opaque boundary title"),
		Opaque->BoundaryTitle, FString(TEXT("PrintString")));
	TestEqual(TEXT("raw supplies uncertain boundary title"),
		Uncertain->BoundaryTitle, FString(TEXT("Select")));
	TestEqual(TEXT("raw supplies unsupported boundary title"),
		Unsupported->BoundaryTitle, FString(TEXT("Delay")));
	TestEqual(TEXT("raw supplies query criterion title"),
		Truncated->CriterionTitle, FString(TEXT("Set LC6Truncated06")));

	const FString StaleReadiness = WriteLC6Mutation(
		LC6ReadinessPath(), TEXT("stale-readiness"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TSharedPtr<FJsonObject>* Hashes = nullptr;
			if (Root->TryGetObjectField(TEXT("hashes"), Hashes) && Hashes != nullptr)
			{
				(*Hashes)->SetStringField(
					TEXT("BP_LC6_BoundaryMatrix.raw-0.2.json"),
					FString::ChrN(64, TEXT('0')));
			}
		});
	TestTrue(TEXT("stale readiness mutation is written"), !StaleReadiness.IsEmpty());
	TestFalse(TEXT("stale readiness fails closed"),
		Load(LC6CoreProfilePath(), LC6QueryProfilePath(),
			StaleReadiness, LC6RawPath()).IsSuccess());

	const FString MixedIdentity = WriteLC6Mutation(
		LC6CoreProfilePath(), TEXT("mixed-asset-identity"),
		[](TSharedRef<FJsonObject> Root)
		{
			Root->SetStringField(TEXT("graph_id"), TEXT("wrong-graph"));
		});
	TestFalse(TEXT("mixed graph identity fails closed"),
		Load(MixedIdentity).IsSuccess());

	const FString CoreTruncated = WriteLC6Mutation(
		LC6CoreProfilePath(), TEXT("core-truncated"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>* Scenarios = nullptr;
			if (Root->TryGetArrayField(TEXT("scenarios"), Scenarios) &&
				Scenarios != nullptr && Scenarios->Num() > 0)
			{
				(*Scenarios)[0]->AsObject()->SetStringField(
					TEXT("status"), TEXT("truncated"));
			}
		});
	TestFalse(TEXT("query status promoted to core fails closed"),
		Load(CoreTruncated).IsSuccess());

	const FString DuplicateScenario = WriteLC6Mutation(
		LC6CoreProfilePath(), TEXT("duplicate-scenario"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>* Scenarios = nullptr;
			if (Root->TryGetArrayField(TEXT("scenarios"), Scenarios) &&
				Scenarios != nullptr && Scenarios->Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Copy = *Scenarios;
				Copy.Add((*Scenarios)[0]);
				Root->SetArrayField(TEXT("scenarios"), MoveTemp(Copy));
			}
		});
	TestFalse(TEXT("duplicate scenario fails closed"),
		Load(DuplicateScenario).IsSuccess());

	const FString InventedOmission = WriteLC6Mutation(
		LC6QueryProfilePath(), TEXT("invented-omission"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TSharedPtr<FJsonObject>* Counts = nullptr;
			if (Root->TryGetObjectField(TEXT("counts"), Counts) && Counts != nullptr)
			{
				(*Counts)->SetNumberField(TEXT("omitted_nodes"), 4);
			}
		});
	TestFalse(TEXT("invented omission count fails closed"),
		Load(LC6CoreProfilePath(), InventedOmission).IsSuccess());

	const FString WrongFrontier = WriteLC6Mutation(
		LC6QueryProfilePath(), TEXT("wrong-frontier"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TArray<TSharedPtr<FJsonValue>>* Frontiers = nullptr;
			if (Root->TryGetArrayField(TEXT("frontiers"), Frontiers) &&
				Frontiers != nullptr && Frontiers->Num() == 1)
			{
				(*Frontiers)[0]->AsObject()->SetStringField(
					TEXT("source_node_id"), Root->GetStringField(TEXT("criterion_node_id")));
			}
		});
	TestFalse(TEXT("wrong Frontier endpoint fails closed"),
		Load(LC6CoreProfilePath(), WrongFrontier).IsSuccess());

	const FString MissingTitle = WriteLC6Mutation(
		LC6RawPath(), TEXT("missing-source-title"),
		[](TSharedRef<FJsonObject> Root)
		{
			const TSharedPtr<FJsonObject>* Blueprint = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
			if (Root->TryGetObjectField(TEXT("blueprint"), Blueprint) &&
				Blueprint != nullptr &&
				(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs) &&
				Graphs != nullptr && Graphs->Num() > 0)
			{
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if ((*Graphs)[0]->AsObject()->TryGetArrayField(TEXT("nodes"), Nodes) &&
					Nodes != nullptr && Nodes->Num() > 0)
				{
					(*Nodes)[0]->AsObject()->RemoveField(TEXT("title"));
				}
			}
		});
	TestFalse(TEXT("missing raw source title fails closed"),
		Load(LC6CoreProfilePath(), LC6QueryProfilePath(),
			LC6ReadinessPath(), MissingTitle).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7ProfileTest,
	"BlueprintLens.LC7.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7ProfileTest::RunTest(const FString&)
{
	const auto Load = []()
	{
		return FBlueprintLensLC7ProfileLoader::LoadFiles(
			LC7ExplanationPath(), LC7SCCProfilePath(),
			LC7ReviewedPath(), LC7ReadinessPath());
	};
	const auto ExpectRejected = [this](
		const TCHAR* Label,
		const FLC7MutationPacket& Packet)
	{
		TestTrue(TEXT("LC7 mutation packet is written"), Packet.IsValid());
		if (!Packet.IsValid())
		{
			return false;
		}
		const FBlueprintLensLC7LoadResult Result =
			FBlueprintLensLC7ProfileLoader::LoadFiles(
				Packet.ExplanationPath, Packet.SCCProfilePath,
				Packet.ReviewedPath, Packet.ReadinessPath);
		TestFalse(Label, Result.IsSuccess());
		TestFalse(TEXT("LC7 rejection exposes no partial profile"),
			Result.Profile.IsValid());
		TestFalse(TEXT("LC7 rejection exposes no partial Explanation model"),
			Result.ExplanationModel.IsValid());
		return !Result.IsSuccess() && !Result.Profile.IsValid() &&
			!Result.ExplanationModel.IsValid();
	};

	const FBlueprintLensLC7LoadResult LoadResult = Load();
	TestTrue(TEXT("LC7 frozen four-file profile loads"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC7Profile& Profile = *LoadResult.Profile;
	TestEqual(TEXT("LC7 profile identity"), Profile.ProfileId,
		FString(TEXT("LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1")));
	TestEqual(TEXT("LC7 static claim scope"), Profile.ClaimScope,
		FString(TEXT("STATIC_SOURCE_VISIBLE_SCC")));
	TestEqual(TEXT("LC7 runtime remains unclaimed"), Profile.RuntimeIterations,
		FString(TEXT("NOT_CLAIMED")));
	TestEqual(TEXT("LC7 readiness is frozen"), Profile.ReadinessStatus,
		FString(TEXT("TRUTH_FROZEN")));
	TestEqual(TEXT("LC7 source node count"), Profile.SourceNodeCount, 10);
	TestEqual(TEXT("LC7 source edge count"), Profile.SourceEdgeCount, 10);
	TestEqual(TEXT("LC7 Explanation unit count"),
		Profile.ExplanationUnitCount, 8);
	TestEqual(TEXT("LC7 Explanation relation count"),
		Profile.ExplanationRelationCount, 8);
	TestEqual(TEXT("LC7 owns one structural SCC"),
		Profile.StructuralSCCCount, 1);
	TestEqual(TEXT("LC7 SCC member count"), Profile.SCC.MemberNodeIds.Num(), 3);
	TestEqual(TEXT("LC7 SCC internal relation count"),
		Profile.SCC.InternalEdgeIds.Num(), 3);
	TestEqual(TEXT("LC7 SCC incoming relation count"),
		Profile.SCC.IncomingEdgeIds.Num(), 1);
	TestEqual(TEXT("LC7 SCC outgoing relation count"),
		Profile.SCC.OutgoingEdgeIds.Num(), 1);
	TestEqual(TEXT("LC7 SCC returning relation count"),
		Profile.SCC.ReturningEdgeIds.Num(), 1);
	TestEqual(TEXT("LC7 SCC entry and exit source identity agree"),
		Profile.SCC.EntryNodeId, Profile.SCC.ExitNodeId);
	TestEqual(TEXT("LC7 SCC entry and exit unit identity agree"),
		Profile.SCC.EntryUnitId, Profile.SCC.ExitUnitId);
	TestEqual(TEXT("LC7 criterion source identity"), Profile.CriterionNodeId,
		FString(TEXT("/Game/LensCorpus/BP_LC7_StaticSCC."
			"BP_LC7_StaticSCC:EventGraph::node::"
			"c0a8dfab-41b4-77d0-0c2f-19b475bd5dac")));
	TestEqual(TEXT("LC7 criterion unit identity"), Profile.CriterionUnitId,
		FString(TEXT("unit.criterion.c0a8dfab-41b4-77d0-0c2f-19b475bd5dac")));

	const TArray<FString> ExpectedMemberNodeIds = {
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::0805eaa8-47d4-f0af-bd2f-ada703333412"),
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::08b1b7d1-4cfa-e2de-8089-b5832da5a751"),
		TEXT("/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:"
			 "EventGraph::node::cbd7b169-4f74-9746-29b8-539f9f4310f9")};
	const TArray<FString> ExpectedMemberUnitIds = {
		TEXT("unit.control.0805eaa8-47d4-f0af-bd2f-ada703333412"),
		TEXT("unit.control.08b1b7d1-4cfa-e2de-8089-b5832da5a751"),
		TEXT("unit.control.cbd7b169-4f74-9746-29b8-539f9f4310f9")};
	const TArray<FString> ExpectedRelationIds = {
		TEXT("relation.controls_execution.79caf299c113811f"),
		TEXT("relation.execution_predecessor.06a3c388470810d1"),
		TEXT("relation.execution_predecessor.920427473f69bbb9")};
	TestEqual(TEXT("LC7 member source order is stable"),
		Profile.SCC.MemberNodeIds, ExpectedMemberNodeIds);
	TestEqual(TEXT("LC7 member unit order is stable"),
		Profile.SCC.OrderedMemberUnitIds, ExpectedMemberUnitIds);
	TestEqual(TEXT("LC7 relation order is stable"),
		Profile.SCC.OrderedRelationIds, ExpectedRelationIds);

	TestEqual(TEXT("LC7 resolves every Explanation relation"),
		Profile.Relations.Num(), 8);
	int32 ReturningBindings = 0;
	for (const FBlueprintLensLC7RelationBinding& Relation : Profile.Relations)
	{
		TestTrue(TEXT("LC7 relation retains source edge identity"),
			!Relation.SourceEdgeId.IsEmpty());
		TestTrue(TEXT("LC7 relation retains source endpoint identities"),
			!Relation.SourceNodeId.IsEmpty() && !Relation.TargetNodeId.IsEmpty());
		TestTrue(TEXT("LC7 relation retains source pin identities"),
			!Relation.SourcePinId.IsEmpty() && !Relation.TargetPinId.IsEmpty());
		ReturningBindings += Relation.bReturning ? 1 : 0;
	}
	TestEqual(TEXT("LC7 resolves exactly one returning relation"),
		ReturningBindings, 1);
	TestEqual(TEXT("LC7 Explanation hash is exact"), Profile.ExplanationSha256,
		FString(TEXT("5cc0272201757962ea923fa618cc74d33b5b46999df8b7aaad9af0748d7a2d00")));
	TestEqual(TEXT("LC7 SCC profile hash is exact"), Profile.SCCProfileSha256,
		FString(TEXT("bc3a6af7049faf64d18949da7f6ffa5aa8bfb4f8aaadddbfce5cfd870df3a721")));
	TestEqual(TEXT("LC7 reviewed truth hash is exact"), Profile.ReviewedSha256,
		FString(TEXT("0702d166d60187f96815b53c479d310228d0bd04bd90ab86c51393f83c2cc0a6")));

	ExpectRejected(TEXT("LC7 stale readiness fails closed"),
		WriteLC7MutationPacket(
			TEXT("stale-readiness"), {}, {}, {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* Hashes = nullptr;
				if (Root->TryGetObjectField(TEXT("hashes"), Hashes) &&
					Hashes != nullptr)
				{
					(*Hashes)->SetStringField(
						TEXT("BP_LC7_StaticSCC.explanation.v1.json"),
						FString::ChrN(64, TEXT('0')));
				}
			}));

	const auto MutateProfileAndReviewedBinding = [](
		const TCHAR* Field,
		const FString& Value,
		TFunction<void(TSharedRef<FJsonObject>)>& OutProfile,
		TFunction<void(TSharedRef<FJsonObject>)>& OutReviewed)
	{
		OutProfile = [Field, Value](TSharedRef<FJsonObject> Root)
		{
			const TSharedPtr<FJsonObject>* Binding = nullptr;
			if (Root->TryGetObjectField(TEXT("source_binding"), Binding) &&
				Binding != nullptr)
			{
				(*Binding)->SetStringField(Field, Value);
			}
		};
		OutReviewed = [Field, Value](TSharedRef<FJsonObject> Root)
		{
			const TSharedPtr<FJsonObject>* Binding = nullptr;
			if (Root->TryGetObjectField(TEXT("binding"), Binding) &&
				Binding != nullptr)
			{
				(*Binding)->SetStringField(Field, Value);
			}
		};
	};
	TFunction<void(TSharedRef<FJsonObject>)> MutateProfile;
	TFunction<void(TSharedRef<FJsonObject>)> MutateReviewed;
	MutateProfileAndReviewedBinding(
		TEXT("blueprint_asset_path"), TEXT("/Game/Wrong.Wrong"),
		MutateProfile, MutateReviewed);
	ExpectRejected(TEXT("LC7 wrong asset identity fails closed"),
		WriteLC7MutationPacket(
			TEXT("wrong-asset"), {}, MutateProfile, MutateReviewed));
	MutateProfileAndReviewedBinding(
		TEXT("graph_id"), TEXT("/Game/Wrong.Wrong:EventGraph"),
		MutateProfile, MutateReviewed);
	ExpectRejected(TEXT("LC7 wrong graph identity fails closed"),
		WriteLC7MutationPacket(
			TEXT("wrong-graph"), {}, MutateProfile, MutateReviewed));
	MutateProfileAndReviewedBinding(
		TEXT("asset_sha256"), FString::ChrN(64, TEXT('0')),
		MutateProfile, MutateReviewed);
	ExpectRejected(TEXT("LC7 wrong asset hash fails closed"),
		WriteLC7MutationPacket(
			TEXT("wrong-asset-hash"), {}, MutateProfile, MutateReviewed));

	ExpectRejected(TEXT("LC7 runtime claim fails closed"),
		WriteLC7MutationPacket(
			TEXT("runtime-claim"), {},
			[](TSharedRef<FJsonObject> Root)
			{
				Root->SetStringField(TEXT("runtime_iterations"), TEXT("CLAIMED"));
			},
			[](TSharedRef<FJsonObject> Root)
			{
				Root->SetStringField(TEXT("runtime_iterations"), TEXT("CLAIMED"));
			}));
	ExpectRejected(TEXT("LC7 changed source counts fail closed"),
		WriteLC7MutationPacket(
			TEXT("changed-counts"), {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* Counts = nullptr;
				if (Root->TryGetObjectField(TEXT("counts"), Counts) && Counts != nullptr)
				{
					(*Counts)->SetNumberField(TEXT("nodes"), 11);
				}
			}));
	ExpectRejected(TEXT("LC7 missing SCC member fails closed"),
		WriteLC7MutationPacket(
			TEXT("missing-member"), {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* SCC = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
				if (Root->TryGetObjectField(TEXT("scc"), SCC) && SCC != nullptr &&
					(*SCC)->TryGetArrayField(TEXT("member_node_ids"), Members) &&
					Members != nullptr && Members->Num() == 3)
				{
					TArray<TSharedPtr<FJsonValue>> Copy = *Members;
					Copy.RemoveAt(2);
					(*SCC)->SetArrayField(TEXT("member_node_ids"), MoveTemp(Copy));
				}
			}));
	ExpectRejected(TEXT("LC7 non-structural group fails closed"),
		WriteLC7MutationPacket(
			TEXT("non-structural-group"),
			[](TSharedRef<FJsonObject> Root)
			{
				const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
				if (Root->TryGetArrayField(TEXT("groups"), Groups) &&
					Groups != nullptr && Groups->Num() == 1)
				{
					(*Groups)[0]->AsObject()->SetStringField(
						TEXT("projection_status"), TEXT("COMPLETE"));
				}
			}));
	ExpectRejected(TEXT("LC7 profile and Explanation mismatch fails closed"),
		WriteLC7MutationPacket(
			TEXT("criterion-mismatch"), {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* SCC = nullptr;
				if (Root->TryGetObjectField(TEXT("scc"), SCC) && SCC != nullptr)
				{
					Root->SetStringField(
						TEXT("criterion_node_id"),
						(*SCC)->GetStringField(TEXT("entry_node_id")));
				}
			},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* SCC = nullptr;
				if (Root->TryGetObjectField(TEXT("scc"), SCC) && SCC != nullptr)
				{
					Root->SetStringField(
						TEXT("criterion_node_id"),
						(*SCC)->GetStringField(TEXT("entry_node_id")));
				}
			}));
	ExpectRejected(TEXT("LC7 wrong returning edge fails closed"),
		WriteLC7MutationPacket(
			TEXT("wrong-returning-edge"), {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* SCC = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Internal = nullptr;
				if (Root->TryGetObjectField(TEXT("scc"), SCC) && SCC != nullptr &&
					(*SCC)->TryGetArrayField(TEXT("internal_edge_ids"), Internal) &&
					Internal != nullptr && Internal->Num() == 3)
				{
					(*SCC)->SetArrayField(
						TEXT("returning_edge_ids"), {(*Internal)[1]});
				}
			},
			[](TSharedRef<FJsonObject> Root)
			{
				const TSharedPtr<FJsonObject>* SCC = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Internal = nullptr;
				if (Root->TryGetObjectField(TEXT("scc"), SCC) && SCC != nullptr &&
					(*SCC)->TryGetArrayField(TEXT("internal_edge_ids"), Internal) &&
					Internal != nullptr && Internal->Num() == 3)
				{
					(*SCC)->SetArrayField(
						TEXT("returning_edge_ids"), {(*Internal)[1]});
				}
			}));
	ExpectRejected(TEXT("LC7 duplicate relation fails closed"),
		WriteLC7MutationPacket(
			TEXT("duplicate-relation"),
			[](TSharedRef<FJsonObject> Root)
			{
				const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
				const TSharedPtr<FJsonObject>* Counts = nullptr;
				if (Root->TryGetArrayField(TEXT("relations"), Relations) &&
					Relations != nullptr && Relations->Num() == 8 &&
					Root->TryGetObjectField(TEXT("counts"), Counts) && Counts != nullptr)
				{
					TArray<TSharedPtr<FJsonValue>> Copy = *Relations;
					Copy.Add((*Relations)[0]);
					Root->SetArrayField(TEXT("relations"), MoveTemp(Copy));
					(*Counts)->SetNumberField(TEXT("relations"), 9);
				}
			}));
	ExpectRejected(TEXT("LC7 missing source pin fails closed"),
		WriteLC7MutationPacket(
			TEXT("missing-source-pin"), {}, {},
			[](TSharedRef<FJsonObject> Root)
			{
				const TArray<TSharedPtr<FJsonValue>>* Endpoints = nullptr;
				if (Root->TryGetArrayField(TEXT("source_pin_endpoints"), Endpoints) &&
					Endpoints != nullptr && Endpoints->Num() == 8)
				{
					(*Endpoints)[0]->AsObject()->RemoveField(TEXT("source_pin_id"));
				}
			}));
	ExpectRejected(TEXT("LC7 missing criterion fails closed"),
		WriteLC7MutationPacket(
			TEXT("missing-criterion"),
			[](TSharedRef<FJsonObject> Root)
			{
				Root->RemoveField(TEXT("criterion_unit_id"));
			}));
	ExpectRejected(TEXT("LC7 extra SCC fails closed"),
		WriteLC7MutationPacket(
			TEXT("extra-scc"),
			[](TSharedRef<FJsonObject> Root)
			{
				const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
				if (Root->TryGetArrayField(TEXT("groups"), Groups) &&
					Groups != nullptr && Groups->Num() == 1)
				{
					TArray<TSharedPtr<FJsonValue>> Copy = *Groups;
					Copy.Add((*Groups)[0]);
					Root->SetArrayField(TEXT("groups"), MoveTemp(Copy));
				}
			}));
	ExpectRejected(TEXT("LC7 unreadable Complete Text source fails closed"),
		WriteLC7MutationPacket(
			TEXT("unreadable-complete-text"),
			[](TSharedRef<FJsonObject> Root)
			{
				const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
				if (Root->TryGetArrayField(TEXT("units"), Units) &&
					Units != nullptr && Units->Num() == 8)
				{
					(*Units)[0]->AsObject()->SetStringField(TEXT("title"), TEXT(""));
				}
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7ProjectionTest,
	"BlueprintLens.LC7.Projection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7ProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLC7LoadResult LoadResult =
		FBlueprintLensLC7ProfileLoader::LoadFiles(
			LC7ExplanationPath(), LC7SCCProfilePath(),
			LC7ReviewedPath(), LC7ReadinessPath());
	TestTrue(TEXT("LC7 profile loads for projection"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LoadResult.Profile);
	TestTrue(TEXT("LC7 accountable recurrence projection is renderable"),
		Projection.IsRenderable());
	TestEqual(TEXT("LC7 projection status is AdaptiveBackbone"),
		Projection.Status, EBlueprintLensLC7ProjectionStatus::AdaptiveBackbone);
	TestEqual(TEXT("LC7 projection owns one SCC record"),
		Projection.SCCs.Num(), 1);
	TestEqual(TEXT("LC7 projection accounts for all eight units"),
		Projection.AllUnitIds.Num(), 8);
	TestEqual(TEXT("LC7 projection accounts for all eight relations"),
		Projection.AllRelationIds.Num(), 8);
	TestEqual(TEXT("LC7 projection retains eight source anchors"),
		Projection.SourceAnchors.Num(), 8);
	TestEqual(TEXT("LC7 criterion identity is exact"),
		Projection.CriterionUnitId, LoadResult.Profile->CriterionUnitId);
	TestEqual(TEXT("LC7 projection exposes the selected three actions"),
		Projection.ActionIds, TArray<FString>({
			TEXT("inspect_cycle"), TEXT("open_source"),
			TEXT("show_complete_text")}));
	TestTrue(TEXT("LC7 Complete Text is recoverable from the model"),
		Projection.CompleteTextLines.Num() >= 17);
	TestEqual(TEXT("LC7 entry family owns two context relations"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Entry), 2);
	TestEqual(TEXT("LC7 predicate family owns one relation"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Predicate), 1);
	TestEqual(TEXT("LC7 value family owns one relation"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Value), 1);
	TestEqual(TEXT("LC7 forward family owns two relations"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Forward), 2);
	TestEqual(TEXT("LC7 return family owns one relation"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Return), 1);
	TestEqual(TEXT("LC7 exit family owns one relation"),
		Projection.CountRelations(EBlueprintLensLC7RelationFamily::Exit), 1);
	TestTrue(TEXT("LC7 projection owns a deterministic SHA-256 integrity hash"),
		Projection.IntegrityHash.Len() == 64);

	if (Projection.SCCs.Num() == 1)
	{
		const FBlueprintLensLC7SCCRecord& SCC = Projection.SCCs[0];
		TestEqual(TEXT("LC7 SCC identity is exact"),
			SCC.GroupId, LoadResult.Profile->SCC.GroupId);
		TestEqual(TEXT("LC7 SCC spine follows frozen member order"),
			SCC.OrderedSpineUnitIds,
			LoadResult.Profile->SCC.OrderedMemberUnitIds);
		TestEqual(TEXT("LC7 SCC entry identity is exact"),
			SCC.EntryUnitId, LoadResult.Profile->SCC.EntryUnitId);
		TestEqual(TEXT("LC7 SCC exit identity is exact"),
			SCC.ExitUnitId, LoadResult.Profile->SCC.ExitUnitId);
		TestEqual(TEXT("LC7 SCC owns its exact returning relation"),
			SCC.ReturnRelationIds.Num(), 1);
	}

	const FBlueprintLensLC7Relation* Returning =
		Projection.Relations.FindByPredicate([](const auto& Relation)
		{
			return Relation.Family == EBlueprintLensLC7RelationFamily::Return;
		});
	TestNotNull(TEXT("LC7 returning relation exists"), Returning);
	if (Returning != nullptr)
	{
		TestEqual(TEXT("LC7 returning identity derives from source truth"),
			Returning->SourceEdgeId,
			LoadResult.Profile->SCC.ReturningEdgeIds[0]);
	}
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		TestTrue(TEXT("every LC7 relation retains source pins"),
			!Relation.SourcePinId.IsEmpty() && !Relation.TargetPinId.IsEmpty());
		TestTrue(TEXT("every LC7 relation has a recoverable source unit"),
			Projection.SourceAnchors.Contains(Relation.SourceUnitId));
		TestTrue(TEXT("every LC7 relation has a recoverable target unit"),
			Projection.SourceAnchors.Contains(Relation.TargetUnitId));
	}

	FBlueprintLensLC7Profile Shuffled = *LoadResult.Profile;
	Algo::Reverse(Shuffled.Relations);
	TSharedRef<FBlueprintLensExplanationModel> ShuffledModel =
		MakeShared<FBlueprintLensExplanationModel>(*Shuffled.ExplanationModel);
	Algo::Reverse(ShuffledModel->Units);
	Algo::Reverse(ShuffledModel->Relations);
	Shuffled.ExplanationModel = ShuffledModel;
	const FBlueprintLensLC7Projection ShuffledProjection =
		FBlueprintLensLC7Projector::Build(Shuffled);
	TestTrue(TEXT("shuffled LC7 source arrays remain renderable"),
		ShuffledProjection.IsRenderable());
	TestEqual(TEXT("LC7 integrity hash ignores source array order"),
		ShuffledProjection.IntegrityHash, Projection.IntegrityHash);

	const auto ExpectCompleteText = [this](
		const TCHAR* Label, const FBlueprintLensLC7Profile& Mutated)
	{
		const FBlueprintLensLC7Projection Fallback =
			FBlueprintLensLC7Projector::Build(Mutated);
		TestEqual(Label, Fallback.Status,
			EBlueprintLensLC7ProjectionStatus::CompleteText);
		TestTrue(TEXT("LC7 Complete Text fallback remains recoverable"),
			!Fallback.CompleteTextLines.IsEmpty());
	};

	FBlueprintLensLC7Profile Missing = *LoadResult.Profile;
	Missing.Relations.RemoveAt(0);
	ExpectCompleteText(TEXT("missing relation ID falls back to Complete Text"), Missing);
	FBlueprintLensLC7Profile Duplicated = *LoadResult.Profile;
	const FBlueprintLensLC7RelationBinding DuplicateRelation =
		Duplicated.Relations[0];
	Duplicated.Relations.Add(DuplicateRelation);
	ExpectCompleteText(TEXT("duplicate relation ID falls back to Complete Text"),
		Duplicated);
	FBlueprintLensLC7Profile TwoFamilies = *LoadResult.Profile;
	TwoFamilies.SCC.IncomingEdgeIds.Add(TwoFamilies.SCC.InternalEdgeIds[0]);
	ExpectCompleteText(TEXT("relation assigned to two families falls back"),
		TwoFamilies);
	FBlueprintLensLC7Profile Omitted = *LoadResult.Profile;
	Omitted.SCC.OrderedRelationIds.RemoveAt(0);
	ExpectCompleteText(TEXT("silently omitted SCC relation falls back"), Omitted);
	FBlueprintLensLC7Profile OtherSCC = *LoadResult.Profile;
	OtherSCC.SCC.GroupId = TEXT("group.scc.different");
	ExpectCompleteText(TEXT("relation ownership bound to another SCC falls back"),
		OtherSCC);

	FBlueprintLensLC7Profile Unreadable = *LoadResult.Profile;
	TSharedRef<FBlueprintLensExplanationModel> UnreadableModel =
		MakeShared<FBlueprintLensExplanationModel>(*Unreadable.ExplanationModel);
	UnreadableModel->Units[0].Title.Reset();
	Unreadable.ExplanationModel = UnreadableModel;
	const FBlueprintLensLC7Projection Unavailable =
		FBlueprintLensLC7Projector::Build(Unreadable);
	TestEqual(TEXT("invalid textual source becomes Unavailable"),
		Unavailable.Status, EBlueprintLensLC7ProjectionStatus::Unavailable);
	return true;
}

namespace
{
FBlueprintLensLC7LoadResult LoadLC7ForLayout()
{
	return FBlueprintLensLC7ProfileLoader::LoadFiles(
		LC7ExplanationPath(), LC7SCCProfilePath(),
		LC7ReviewedPath(), LC7ReadinessPath());
}

void AddLC7SyntheticUnit(
	FBlueprintLensLC7Projection& Projection,
	const FString& UnitId,
	const FString& Title)
{
	Projection.AllUnitIds.Add(UnitId);
	Projection.UnitTitles.Add(UnitId, Title);
	FBlueprintLensSourceReference Anchor =
		Projection.SourceAnchors.CreateConstIterator().Value();
	Anchor.SourceNodeId = UnitId + TEXT(".source");
	Anchor.NativeNodeGuid = UnitId + TEXT(".guid");
	Projection.SourceAnchors.Add(UnitId, MoveTemp(Anchor));
}

void AddLC7SyntheticRelation(
	FBlueprintLensLC7Projection& Projection,
	const FString& RelationId,
	const FString& OwningSCCId,
	const FString& SourceUnitId,
	const FString& TargetUnitId,
	const EBlueprintLensLC7RelationFamily Family)
{
	FBlueprintLensLC7Relation Relation;
	Relation.RelationId = RelationId;
	Relation.OwningSCCId = OwningSCCId;
	Relation.Family = Family;
	Relation.SourceUnitId = SourceUnitId;
	Relation.TargetUnitId = TargetUnitId;
	Relation.SourceEdgeId = RelationId + TEXT(".edge");
	Relation.SourceNodeId = SourceUnitId + TEXT(".source");
	Relation.TargetNodeId = TargetUnitId + TEXT(".source");
	Relation.SourcePinId = RelationId + TEXT(".out");
	Relation.TargetPinId = RelationId + TEXT(".in");
	Relation.Label = TEXT("synthetic engineering relation");
	Projection.Relations.Add(MoveTemp(Relation));
	Projection.AllRelationIds.Add(RelationId);
}

FBlueprintLensLC7Projection MakeLC7SyntheticEngineeringFocus(
	const FBlueprintLensLC7Projection& Real)
{
	FBlueprintLensLC7Projection Result = Real;
	Result.ProfileId = TEXT("SYNTHETIC_ENGINEERING_FOCUS");
	Result.IntegrityHash = Result.ProfileId;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FString UnitId = FString::Printf(TEXT("synthetic.focus.context.%d"), Index);
		const FString RelationId = FString::Printf(TEXT("synthetic.focus.relation.%d"), Index);
		AddLC7SyntheticUnit(Result, UnitId,
			FString::Printf(TEXT("Engineering context %d"), Index));
		AddLC7SyntheticRelation(Result, RelationId, Result.SCCs[0].GroupId,
			UnitId, Result.SCCs[0].EntryUnitId,
			EBlueprintLensLC7RelationFamily::Entry);
		Result.SCCs[0].EntryRelationIds.Add(RelationId);
	}
	return Result;
}

FBlueprintLensLC7Projection MakeLC7SyntheticEngineeringIndex(
	const FBlueprintLensLC7Projection& Real)
{
	FBlueprintLensLC7Projection Result = Real;
	Result.ProfileId = TEXT("SYNTHETIC_ENGINEERING_INDEX");
	Result.IntegrityHash = Result.ProfileId;
	TArray<FString> FocusMembers = Result.AllUnitIds.Array();
	FocusMembers.Remove(Result.CriterionUnitId);
	FocusMembers.Sort();
	Result.SCCs[0].OrderedSpineUnitIds = MoveTemp(FocusMembers);
	for (int32 SCCIndex = 1; SCCIndex <= 2; ++SCCIndex)
	{
		FBlueprintLensLC7SCCRecord SCC;
		SCC.GroupId = FString::Printf(TEXT("synthetic.index.scc.%d"), SCCIndex);
		for (int32 MemberIndex = 0; MemberIndex < 2; ++MemberIndex)
		{
			const FString UnitId = FString::Printf(
				TEXT("synthetic.index.scc.%d.member.%d"), SCCIndex, MemberIndex);
			AddLC7SyntheticUnit(Result, UnitId,
				FString::Printf(TEXT("Engineering SCC %d member %d"),
					SCCIndex, MemberIndex));
			SCC.OrderedSpineUnitIds.Add(UnitId);
		}
		SCC.EntryUnitId = SCC.OrderedSpineUnitIds[0];
		SCC.ExitUnitId = SCC.OrderedSpineUnitIds[1];
		const FString RelationId = FString::Printf(
			TEXT("synthetic.index.scc.%d.relation"), SCCIndex);
		AddLC7SyntheticRelation(Result, RelationId, SCC.GroupId,
			SCC.EntryUnitId, SCC.ExitUnitId,
			EBlueprintLensLC7RelationFamily::Forward);
		SCC.ForwardRelationIds.Add(RelationId);
		Result.SCCs.Add(MoveTemp(SCC));
	}
	return Result;
}

enum class ELC7TestBackendBehavior : uint8
{
	Accepted,
	Unavailable,
	Timeout,
	Malformed,
	Partial,
	WrongRoute,
	Nondeterministic
};

class FLC7ScriptedLayoutBackend final : public IBlueprintLensLayoutBackend
{
public:
	FLC7ScriptedLayoutBackend(
		const EBlueprintLensLayoutBackendKind InKind,
		const ELC7TestBackendBehavior InBehavior,
		const FBlueprintLensLayoutLedger& InOracle)
		: Kind(InKind), Behavior(InBehavior), Oracle(InOracle)
	{
		Oracle.Backend = Kind;
		Oracle.BackendVersion = TEXT("BlueprintLens.LC7.Test.v1");
	}

	virtual EBlueprintLensLayoutBackendKind GetBackendKind() const override
	{
		return Kind;
	}

	virtual bool IsAvailable(FString& OutDiagnostic) const override
	{
		OutDiagnostic = Behavior == ELC7TestBackendBehavior::Unavailable
			? TEXT("LC7_TEST_BACKEND_UNAVAILABLE")
			: TEXT("LC7_TEST_BACKEND_AVAILABLE");
		return Behavior != ELC7TestBackendBehavior::Unavailable;
	}

	virtual FBlueprintLensLayoutLedger Layout(
		const FBlueprintLensLayoutRequest&) const override
	{
		++CallCount;
		FBlueprintLensLayoutLedger Result = Oracle;
		if (Behavior == ELC7TestBackendBehavior::Timeout)
		{
			Result.Nodes.Reset();
			Result.DiagnosticCode = TEXT("BLUEPRINT_LENS_EXTERNAL_PROCESS_TIMEOUT");
		}
		else if (Behavior == ELC7TestBackendBehavior::Malformed)
		{
			Result.Edges.Reset();
			Result.DiagnosticCode = TEXT("LC7_LAYOUT_MALFORMED_OUTPUT");
		}
		else if (Behavior == ELC7TestBackendBehavior::Partial)
		{
			Result.Nodes.RemoveAt(0);
		}
		else if (Behavior == ELC7TestBackendBehavior::WrongRoute)
		{
			Result.Edges[0].BendPoints.Add(FVector2D(8.0f, 8.0f));
		}
		else if (Behavior == ELC7TestBackendBehavior::Nondeterministic &&
			CallCount > 1)
		{
			Result.Nodes[0].Position.X += 2.0f;
		}
		return Result;
	}

private:
	EBlueprintLensLayoutBackendKind Kind;
	ELC7TestBackendBehavior Behavior;
	FBlueprintLensLayoutLedger Oracle;
	mutable int32 CallCount = 0;
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7ScaleModeTest,
	"BlueprintLens.LC7.ScaleMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7ScaleModeTest::RunTest(const FString&)
{
	const FBlueprintLensLC7LoadResult LoadResult = LoadLC7ForLayout();
	TestTrue(TEXT("LC7 profile loads for scale selection"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC7Projection Real =
		FBlueprintLensLC7Projector::Build(*LoadResult.Profile);
	const FBlueprintLensLC7TextMetrics RealMetrics =
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(Real);
	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC7Layout Layout =
			FBlueprintLensLC7LayoutBuilder::Build(Real, Width, FString(), RealMetrics);
		TestEqual(TEXT("real LC7 evidence remains FULL"),
			Layout.ScaleMode, EBlueprintLensLC7ScaleMode::Full);
		TestEqual(TEXT("real FULL owns no folds"), Layout.Folds.Num(), 0);
		TestEqual(TEXT("real FULL owns no index rows"), Layout.IndexRows.Num(), 0);
		TestTrue(TEXT("real FULL is completely recoverable"),
			Layout.HasValidRecoverability(Real));
	}

	const FBlueprintLensLC7Projection SyntheticFocus =
		MakeLC7SyntheticEngineeringFocus(Real);
	const FBlueprintLensLC7TextMetrics FocusMetrics =
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(SyntheticFocus);
	const FBlueprintLensLC7Layout Focus = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticFocus, 430.0f, FString(), FocusMetrics);
	const FBlueprintLensLC7Layout FocusAgain = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticFocus, 430.0f, FString(), FocusMetrics);
	TestEqual(TEXT("SYNTHETIC_ENGINEERING_FOCUS selects FOCUS (non-evidence)"),
		Focus.ScaleMode, EBlueprintLensLC7ScaleMode::Focus);
	TestEqual(TEXT("FOCUS is deterministic"),
		Focus.RecoverabilityHash, FocusAgain.RecoverabilityHash);
	TestEqual(TEXT("FOCUS derives the criterion-owning SCC"),
		Focus.FocusedSCCId, SyntheticFocus.SCCs[0].GroupId);
	TestTrue(TEXT("FOCUS owns counted expansion folds and complete union"),
		Focus.Folds.Num() == 1 && !Focus.Folds[0].ExpansionActionId.IsEmpty() &&
		Focus.SelectedUnitId.IsEmpty() && Focus.HasValidRecoverability(SyntheticFocus));

	FBlueprintLensLC7Layout MissingCount = Focus;
	MissingCount.Folds[0].UnitCount = -1;
	TestFalse(TEXT("missing fold count is rejected"),
		MissingCount.HasValidRecoverability(SyntheticFocus));
	FBlueprintLensLC7Layout WrongCount = Focus;
	WrongCount.Folds[0].RelationCount += 1;
	TestFalse(TEXT("wrong fold count is rejected"),
		WrongCount.HasValidRecoverability(SyntheticFocus));
	FBlueprintLensLC7Layout Overlap = Focus;
	Overlap.Folds[0].UnitIds.Add(Focus.VisibleUnitIds.Array()[0]);
	Overlap.Folds[0].UnitCount += 1;
	TestFalse(TEXT("overlapping fold ownership is rejected"),
		Overlap.HasValidRecoverability(SyntheticFocus));
	FBlueprintLensLC7Layout LostRelation = Focus;
	LostRelation.Folds[0].RelationIds.Remove(
		LostRelation.Folds[0].RelationIds.CreateConstIterator().operator*());
	LostRelation.Folds[0].RelationCount -= 1;
	TestFalse(TEXT("lost folded relation is rejected"),
		LostRelation.HasValidRecoverability(SyntheticFocus));
	FBlueprintLensLC7Layout MissingExpansion = Focus;
	MissingExpansion.Folds[0].ExpansionActionId.Reset();
	TestFalse(TEXT("absent fold expansion is rejected"),
		MissingExpansion.HasValidRecoverability(SyntheticFocus));

	const FBlueprintLensLC7Projection SyntheticIndex =
		MakeLC7SyntheticEngineeringIndex(Real);
	const FBlueprintLensLC7TextMetrics IndexMetrics =
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(SyntheticIndex);
	const FBlueprintLensLC7Layout Index = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticIndex, 480.0f, FString(), IndexMetrics);
	const FBlueprintLensLC7Layout IndexAgain = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticIndex, 480.0f, FString(), IndexMetrics);
	TestEqual(TEXT("SYNTHETIC_ENGINEERING_INDEX selects INDEX (non-evidence)"),
		Index.ScaleMode, EBlueprintLensLC7ScaleMode::Index);
	TestEqual(TEXT("INDEX is deterministic"),
		Index.RecoverabilityHash, IndexAgain.RecoverabilityHash);
	TestTrue(TEXT("INDEX owns one counted row per off-focus SCC"),
		Index.IndexRows.Num() == 2 && Index.HasValidRecoverability(SyntheticIndex));
	FBlueprintLensLC7Layout MissingAnchor = Index;
	MissingAnchor.IndexRows[0].SourceAnchorUnitId = TEXT("absent.source.anchor");
	TestFalse(TEXT("index row without source anchor is rejected"),
		MissingAnchor.HasValidRecoverability(SyntheticIndex));

	FBlueprintLensLC7Projection MissingCriterion = Real;
	MissingCriterion.CriterionUnitId = TEXT("absent.criterion");
	TestEqual(TEXT("nonexistent criterion falls back to Complete Text"),
		FBlueprintLensLC7LayoutBuilder::Build(
			MissingCriterion, 430.0f, FString(), RealMetrics).ScaleMode,
		EBlueprintLensLC7ScaleMode::CompleteText);
	TestEqual(TEXT("nonexistent requested focus falls back to Complete Text"),
		FBlueprintLensLC7LayoutBuilder::Build(
			Real, 430.0f, TEXT("absent.scc"), RealMetrics).ScaleMode,
		EBlueprintLensLC7ScaleMode::CompleteText);
	FBlueprintLensLC7TextMetrics Overflow = RealMetrics;
	Overflow.UnitLabelSizes.FindOrAdd(Real.CriterionUnitId) = FVector2D(500.0f, 18.0f);
	TestEqual(TEXT("measured text overflow falls back to Complete Text"),
		FBlueprintLensLC7LayoutBuilder::Build(
			Real, 430.0f, FString(), Overflow).ScaleMode,
		EBlueprintLensLC7ScaleMode::CompleteText);
	FBlueprintLensLC7TextMetrics NoClearance = RealMetrics;
	NoClearance.AvailableRouteClearance = 2.0f;
	TestEqual(TEXT("route-clearance failure falls back to Complete Text"),
		FBlueprintLensLC7LayoutBuilder::Build(
			Real, 430.0f, FString(), NoClearance).ScaleMode,
		EBlueprintLensLC7ScaleMode::CompleteText);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7LayoutTest,
	"BlueprintLens.LC7.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7LayoutTest::RunTest(const FString&)
{
	const FBlueprintLensLC7LoadResult LoadResult = LoadLC7ForLayout();
	TestTrue(TEXT("LC7 profile loads for layout"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LoadResult.Profile);
	const FBlueprintLensLC7TextMetrics Metrics =
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(Projection);
	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC7Layout Layout =
			FBlueprintLensLC7LayoutBuilder::Build(Projection, Width, FString(), Metrics);
		const EBlueprintLensLC7ResponsiveMode ExpectedMode = Width <= 430.0f
			? EBlueprintLensLC7ResponsiveMode::SingleColumn430
			: Width < 700.0f
				? EBlueprintLensLC7ResponsiveMode::StackedDetail480
				: EBlueprintLensLC7ResponsiveMode::SideBySide700;
		TestEqual(TEXT("responsive mode is exact"), Layout.ResponsiveMode, ExpectedMode);
		TestEqual(TEXT("canvas height matches selected target"), Layout.CanvasSize.Y,
			Width >= 700.0f ? 760.0 : 1296.0);
		TestTrue(TEXT("content region is exact"),
			Layout.ContentBounds.Min.Equals(FVector2D(24.0f, 142.0f)) &&
			Layout.ContentBounds.Max.Equals(FVector2D(Width - 24.0f,
				Width >= 700.0f ? 736.0f : 1272.0f)));
		TestTrue(TEXT("overview region is exact"),
			Layout.OverviewBounds.Min.Equals(FVector2D(24.0f, 142.0f)) &&
			Layout.OverviewBounds.Max.Equals(FVector2D(
				Width >= 700.0f ? 438.0f : Width - 24.0f, 736.0f)));
		TestTrue(TEXT("detail region is exact"),
			Layout.DetailBounds.Min.Equals(FVector2D(
				Width >= 700.0f ? 454.0f : 24.0f,
				Width >= 700.0f ? 142.0f : 752.0f)) &&
			Layout.DetailBounds.Max.Equals(FVector2D(Width - 24.0f,
				Width >= 700.0f ? 736.0f : 1272.0f)));
		TestEqual(TEXT("layout exposes one SCC"), Layout.VisibleSCCCount, 1);
		TestEqual(TEXT("layout exposes exact criterion"),
			Layout.CriterionUnitId, Projection.CriterionUnitId);
		TestTrue(TEXT("layout covers the real projection"), Layout.CoversProjection(Projection));
		TestTrue(TEXT("layout has one valid shared ledger"), Layout.HasValidSharedLedger());
		TestTrue(TEXT("layout has complete recoverability"),
			Layout.HasValidRecoverability(Projection));
		TestTrue(TEXT("layout labels and routes remain collision-free"),
			Layout.HasNoTextOrRouteCollisions());
		TestTrue(TEXT("layout matches the selected A3 oracle"),
			Layout.MatchesVisualOracle(1.0f));
		TestTrue(TEXT("layout hit targets do not overlap"),
			Layout.HasNonOverlappingHitTargets());
		TestTrue(TEXT("measured labels remain in bounds"),
			Layout.HasInBoundsMeasuredLabels());
		TestTrue(TEXT("relation attachments remain distinct"),
			Layout.HasDistinctRelationAttachments());
		TestTrue(TEXT("relation routes have zero collinear overlap"),
			Layout.HasZeroCollinearRouteOverlap());
		TestTrue(TEXT("return and non-return bend budgets hold"),
			Layout.HasValidBendBudget());
		TestEqual(TEXT("layout owns exactly one semantic return route"),
			Layout.Routes.FilterByPredicate([](const auto& Route)
			{
				return Route.Family == EBlueprintLensLC7RelationFamily::Return;
			}).Num(), 1);
		const FBlueprintLensLC7RouteLayout* ReturnRoute =
			Layout.Routes.FindByPredicate([](const auto& Route)
			{
				return Route.Family == EBlueprintLensLC7RelationFamily::Return;
			});
		TestNotNull(TEXT("layout owns the exact LC7 return route"), ReturnRoute);
		if (ReturnRoute != nullptr)
		{
			const FBlueprintLensLC7NodeLayout* ReturnTarget =
				Layout.Nodes.FindByPredicate([ReturnRoute](const auto& Node)
				{
					return Node.UnitId == ReturnRoute->TargetUnitId;
				});
			TestNotNull(TEXT("return route target owns measured geometry"),
				ReturnTarget);
			TestTrue(TEXT("return route keeps a clear independent left corridor"),
				ReturnTarget != nullptr && ReturnRoute->Points.Num() == 4 &&
				ReturnRoute->Points[1].X <= ReturnTarget->Bounds.Min.X - 32.0f &&
				FMath::IsNearlyEqual(
					ReturnRoute->Points[1].X, ReturnRoute->Points[2].X));
		}
		FBlueprintLensLC7Layout Selected = Layout;
		Selected.SelectedUnitId = Projection.SCCs[0].EntryUnitId;
		TestEqual(TEXT("selection cannot move overview geometry"),
			Selected.OverviewGeometryHash, Layout.OverviewGeometryHash);
		TestTrue(TEXT("selected presentation still matches geometry oracle"),
			Selected.MatchesVisualOracle(1.0f));
	}

	const FBlueprintLensLC7Layout Oracle =
		FBlueprintLensLC7LayoutBuilder::Build(Projection, 700.0f, FString(), Metrics);
	const auto RunFailure = [&](const ELC7TestBackendBehavior Behavior)
	{
		const FLC7ScriptedLayoutBackend Elk(
			EBlueprintLensLayoutBackendKind::ElkLayered, Behavior,
			Oracle.VisualOracleLedger);
		const FLC7ScriptedLayoutBackend Graphviz(
			EBlueprintLensLayoutBackendKind::GraphvizDot,
			ELC7TestBackendBehavior::Unavailable, Oracle.VisualOracleLedger);
		return FBlueprintLensLC7LayoutSession::BuildWithBackends(
			Projection, 700.0f, FString(), Metrics, Elk, Graphviz);
	};
	for (const ELC7TestBackendBehavior Behavior : {
		ELC7TestBackendBehavior::Unavailable,
		ELC7TestBackendBehavior::Timeout,
		ELC7TestBackendBehavior::Malformed,
		ELC7TestBackendBehavior::Partial,
		ELC7TestBackendBehavior::WrongRoute,
		ELC7TestBackendBehavior::Nondeterministic})
	{
		const FBlueprintLensLC7LayoutSessionResult Result = RunFailure(Behavior);
		TestTrue(TEXT("invalid external backend falls through to deterministic A3"),
			Result.IsRenderable(Projection));
		TestEqual(TEXT("deterministic A3 owns rejected-backend fallback"),
			Result.Layout.LayoutLedger.Backend,
			EBlueprintLensLayoutBackendKind::Deterministic);
	}
	const FLC7ScriptedLayoutBackend ElkAccepted(
		EBlueprintLensLayoutBackendKind::ElkLayered,
		ELC7TestBackendBehavior::Accepted, Oracle.VisualOracleLedger);
	const FLC7ScriptedLayoutBackend GraphvizAccepted(
		EBlueprintLensLayoutBackendKind::GraphvizDot,
		ELC7TestBackendBehavior::Accepted, Oracle.VisualOracleLedger);
	const FBlueprintLensLC7LayoutSessionResult Accepted =
		FBlueprintLensLC7LayoutSession::BuildWithBackends(
			Projection, 700.0f, FString(), Metrics,
			ElkAccepted, GraphvizAccepted);
	TestTrue(TEXT("deterministic complete ELK output is accepted"),
		Accepted.IsRenderable(Projection));
	TestEqual(TEXT("ELK owns fidelity-matched geometry"),
		Accepted.Layout.LayoutLedger.Backend,
		EBlueprintLensLayoutBackendKind::ElkLayered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7CanvasTest,
	"BlueprintLens.LC7.Canvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7CanvasTest::RunTest(const FString&)
{
	const FBlueprintLensLC7LoadResult LoadResult = LoadLC7ForLayout();
	TestTrue(TEXT("LC7 profile loads for canvas"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LoadResult.Profile);
	TestEqual(TEXT("LC7 rounded brush fill is white"),
		BlueprintLensLC7RoundedBrushFill(), FLinearColor::White);
	const FLinearColor SemanticFill = FLinearColor::FromSRGBColor(
		FColor::FromHex(TEXT("#1D2430")));
	TestEqual(TEXT("LC7 semantic fill is supplied as element tint"),
		BlueprintLensLC7BoxElementTint(SemanticFill), SemanticFill);
	TestEqual(TEXT("predicate relation is dashed without relying on colour"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Predicate).Pattern,
		EBlueprintLensLC7RoutePattern::Dashed);
	TestEqual(TEXT("value relation is dotted without relying on colour"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Value).Pattern,
		EBlueprintLensLC7RoutePattern::Dotted);
	TestEqual(TEXT("return relation paints as one continuous route"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Return).Pattern,
		EBlueprintLensLC7RoutePattern::Solid);
	TestEqual(TEXT("return relation owns a distinct directional return arrow"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Return).Marker,
		EBlueprintLensLC7RouteMarker::ReturnArrow);
	TestEqual(TEXT("return route uses the proven single-band Slate thickness"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Return).Thickness,
		2.0f);
	TestNotEqual(TEXT("predicate and entry also own distinct marker shapes"),
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Predicate).Marker,
		BlueprintLensLC7RelationEncoding(
			EBlueprintLensLC7RelationFamily::Entry).Marker);

	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC7LayoutSessionResult Session =
			FBlueprintLensLC7LayoutSession::Build(
				Projection, Width, FString());
		TestTrue(TEXT("LC7 layout session is renderable for canvas"),
			Session.IsRenderable(Projection));
		TSharedRef<SBlueprintLensLC7AdaptiveBackbone> Canvas =
			SNew(SBlueprintLensLC7AdaptiveBackbone)
			.Projection(Projection)
			.InitialSession(Session)
			.SelectedUnitId(FString());
		Canvas->SlatePrepass(1.0f);
		TestTrue(TEXT("LC7 canvas desired size matches measured layout"),
			Canvas->GetDesiredSize().Equals(Session.Layout.CanvasSize));
		TestEqual(TEXT("LC7 canvas exposes exactly three SCC-member hit targets"),
			Canvas->GetMemberHitTargetsForTesting().Num(), 3);
		for (const FString& MemberId : Projection.SCCs[0].OrderedSpineUnitIds)
		{
			const FBlueprintLensLC7NodeLayout* Node =
				Session.Layout.Nodes.FindByPredicate(
					[&MemberId](const auto& Candidate)
					{
						return Candidate.UnitId == MemberId;
					});
			TestNotNull(TEXT("LC7 SCC member owns measured geometry"), Node);
			if (Node != nullptr)
			{
				TestEqual(TEXT("LC7 member hit testing resolves exact unit"),
					Canvas->ResolveUnitAtLocalPositionForTesting(
						Node->HitBounds.GetCenter()), MemberId);
				TestEqual(TEXT("neutral member click selects that member"),
					Canvas->ResolveSelectionAtLocalPositionForTesting(
						Node->HitBounds.GetCenter()), MemberId);
			}
		}
		TestTrue(TEXT("LC7 empty-space hit testing stays neutral"),
			Canvas->ResolveUnitAtLocalPositionForTesting(
				FVector2D(8.0f, 8.0f)).IsEmpty());
		TestEqual(TEXT("LC7 canvas exposes all three measured actions"),
			Canvas->GetActionHitTargetsForTesting().Num(), 3);
		for (const FBlueprintLensLC7ActionLayout& Action : Session.Layout.Actions)
		{
			TestEqual(TEXT("LC7 action hit testing resolves exact action"),
				Canvas->ResolveActionAtLocalPositionForTesting(
					Action.HitBounds.GetCenter()), Action.ActionId);
		}
		TestTrue(TEXT("LC7 routes paint before nodes"),
			Canvas->GetRoutePaintLayerForTesting() <
				Canvas->GetNodePaintLayerForTesting());
		TestTrue(TEXT("LC7 nodes paint before text"),
			Canvas->GetNodePaintLayerForTesting() <
				Canvas->GetTextPaintLayerForTesting());
		TestEqual(TEXT("LC7 canvas visibly accounts for one SCC"),
			Canvas->GetVisibleSCCCountForTesting(), 1);
		TestEqual(TEXT("LC7 criterion is visible exactly once"),
			Canvas->GetVisibleCriterionCountForTesting(), 1);
		TestEqual(TEXT("LC7 canvas retains eight visible source anchors"),
			Canvas->GetVisibleSourceAnchorCountForTesting(), 8);
		TestEqual(TEXT("LC7 neutral overview paints no paragraph text"),
			Canvas->GetParagraphTextCountForTesting(), 0);
		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		for (const FBlueprintLensLC7NodeLayout& Node : Session.Layout.Nodes)
		{
			const FString& Title = Projection.UnitTitles.FindChecked(Node.UnitId);
			const FSlateFontInfo Font = FAppStyle::Get().GetFontStyle(
				Node.UnitId == Projection.CriterionUnitId
					? "NormalFontBold" : "SmallFont");
			const FVector2D Actual = FontMeasure->Measure(Title, Font);
			TestTrue(TEXT("LC7 layout retains the actual Slate title width"),
				Node.LabelBounds.GetSize().X + 0.5f >= Actual.X);
			TestTrue(TEXT("LC7 node leaves visible title-side clearance"),
				Node.Bounds.GetSize().X >= Actual.X + 12.0f);
		}
	}

	const FBlueprintLensLC7LayoutSessionResult WideSession =
		FBlueprintLensLC7LayoutSession::Build(Projection, 700.0f, FString());
	const FString SelectedMember = Projection.SCCs[0].OrderedSpineUnitIds[0];
	const FBlueprintLensLC7NodeLayout* SelectedNode =
		WideSession.Layout.Nodes.FindByPredicate(
			[&SelectedMember](const auto& Candidate)
			{
				return Candidate.UnitId == SelectedMember;
			});
	TestNotNull(TEXT("selected LC7 member owns measured geometry"), SelectedNode);
	TSharedRef<SBlueprintLensLC7AdaptiveBackbone> Neutral =
		SNew(SBlueprintLensLC7AdaptiveBackbone)
		.Projection(Projection)
		.InitialSession(WideSession)
		.SelectedUnitId(FString());
	TSharedRef<SBlueprintLensLC7AdaptiveBackbone> Selected =
		SNew(SBlueprintLensLC7AdaptiveBackbone)
		.Projection(Projection)
		.InitialSession(WideSession)
		.SelectedUnitId(SelectedMember);
	TestTrue(TEXT("selected LC7 member owns selected paint state"),
		Selected->IsUnitSelected(SelectedMember));
	TestTrue(TEXT("selected member receives stronger outline emphasis"),
		Selected->GetUnitOutlineWidthForTesting(SelectedMember) >
			Neutral->GetUnitOutlineWidthForTesting(SelectedMember));
	TestEqual(TEXT("selection cannot change overview geometry"),
		Selected->GetLayoutForTesting().OverviewGeometryHash,
		Neutral->GetLayoutForTesting().OverviewGeometryHash);
	if (SelectedNode != nullptr)
	{
		TestTrue(TEXT("clicking the selected member resolves deselection"),
			Selected->ResolveSelectionAtLocalPositionForTesting(
				SelectedNode->HitBounds.GetCenter()).IsEmpty());
	}
	TestEqual(TEXT("selected overview still paints no paragraph text"),
		Selected->GetParagraphTextCountForTesting(), 0);

	const FBlueprintLensLC7Projection SyntheticFocus =
		MakeLC7SyntheticEngineeringFocus(Projection);
	FBlueprintLensLC7LayoutSessionResult FocusSession;
	FocusSession.Layout = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticFocus, 430.0f, FString(),
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(SyntheticFocus));
	TSharedRef<SBlueprintLensLC7AdaptiveBackbone> FocusCanvas =
		SNew(SBlueprintLensLC7AdaptiveBackbone)
		.Projection(SyntheticFocus)
		.InitialSession(FocusSession)
		.SelectedUnitId(FString());
	TestEqual(TEXT("FOCUS exposes every counted fold affordance"),
		FocusCanvas->GetCountedFoldAffordanceCountForTesting(),
		FocusSession.Layout.Folds.Num());
	TestTrue(TEXT("FOCUS fold text exposes exact unit/relation counts"),
		SlateWidgetText(FocusCanvas).Contains(FString::Printf(
			TEXT("%dU / %dR"), FocusSession.Layout.Folds[0].UnitCount,
			FocusSession.Layout.Folds[0].RelationCount)));

	const FBlueprintLensLC7Projection SyntheticIndex =
		MakeLC7SyntheticEngineeringIndex(Projection);
	FBlueprintLensLC7LayoutSessionResult IndexSession;
	IndexSession.Layout = FBlueprintLensLC7LayoutBuilder::Build(
		SyntheticIndex, 480.0f, FString(),
		FBlueprintLensLC7TextMetrics::MeasuredForProjection(SyntheticIndex));
	TSharedRef<SBlueprintLensLC7AdaptiveBackbone> IndexCanvas =
		SNew(SBlueprintLensLC7AdaptiveBackbone)
		.Projection(SyntheticIndex)
		.InitialSession(IndexSession)
		.SelectedUnitId(FString());
	TestEqual(TEXT("INDEX exposes every counted SCC row affordance"),
		IndexCanvas->GetCountedIndexAffordanceCountForTesting(),
		IndexSession.Layout.IndexRows.Num());
	for (const FBlueprintLensLC7IndexRow& Row : IndexSession.Layout.IndexRows)
	{
		TestTrue(TEXT("INDEX row text exposes exact unit/relation counts"),
			SlateWidgetText(IndexCanvas).Contains(FString::Printf(
				TEXT("%dU / %dR"), Row.UnitCount, Row.RelationCount)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC7PanelStateTest,
	"BlueprintLens.LC7.Panel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC7PanelStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC7LoadResult LoadResult = LoadLC7ForLayout();
	TestTrue(TEXT("LC7 profile loads for panel"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC7Projection Projection =
		FBlueprintLensLC7Projector::Build(*LoadResult.Profile);
	TestTrue(TEXT("LC7 panel projection is renderable"), Projection.IsRenderable());
	TestEqual(TEXT("LC7 detail preserves long evidence tokens by wrapping"),
		BlueprintLensLC7DetailWrappingPolicy(),
		ETextWrappingPolicy::AllowPerCharacterWrapping);

	FString DerivedProfilePath;
	FString DerivedReviewedPath;
	FString DerivedReadinessPath;
	TestTrue(TEXT("exact LC7 Explanation name derives frozen sibling truth"),
		SBlueprintLensPanel::TryDeriveLC7TruthPaths(
			LC7ExplanationPath(), DerivedProfilePath,
			DerivedReviewedPath, DerivedReadinessPath));
	TestEqual(TEXT("LC7 panel derives only the frozen SCC profile"),
		FPaths::ConvertRelativePathToFull(DerivedProfilePath),
		FPaths::ConvertRelativePathToFull(LC7SCCProfilePath()));
	TestEqual(TEXT("LC7 panel derives only reviewed truth"),
		FPaths::ConvertRelativePathToFull(DerivedReviewedPath),
		FPaths::ConvertRelativePathToFull(LC7ReviewedPath()));
	TestEqual(TEXT("LC7 panel derives only frozen readiness"),
		FPaths::ConvertRelativePathToFull(DerivedReadinessPath),
		FPaths::ConvertRelativePathToFull(LC7ReadinessPath()));
	TestFalse(TEXT("a similar Explanation name cannot enter the LC7 route"),
		SBlueprintLensPanel::TryDeriveLC7TruthPaths(
			FPaths::Combine(FPaths::GetPath(LC7ExplanationPath()),
				TEXT("BP_LC7_Other.explanation.v1.json")),
			DerivedProfilePath, DerivedReviewedPath, DerivedReadinessPath));

	FString ExplanationText;
	const FString MissingDirectory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(), TEXT("BlueprintLensTests/LC7/PanelMissingSiblings"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString MissingSiblingPath = FPaths::Combine(
		MissingDirectory, TEXT("BP_LC7_StaticSCC.explanation.v1.json"));
	const bool bWroteMissingSiblingCase =
		FFileHelper::LoadFileToString(ExplanationText, *LC7ExplanationPath()) &&
		IFileManager::Get().MakeDirectory(*MissingDirectory, true) &&
		FFileHelper::SaveStringToFile(
			ExplanationText, *MissingSiblingPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	TestTrue(TEXT("LC7 strict-route missing-sibling case is written"),
		bWroteMissingSiblingCase);
	if (bWroteMissingSiblingCase)
	{
		FBlueprintLensEditorModule& Module =
			FModuleManager::GetModuleChecked<FBlueprintLensEditorModule>(
				TEXT("BlueprintLensEditor"));
		const FString PreviousPath = Module.GetExplanationPath();
		Module.SetExplanationPathOverride(MissingSiblingPath);
		TSharedRef<SBlueprintLensPanel> FailedPanel =
			SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
		Module.SetExplanationPathOverride(PreviousPath);
		TestFalse(TEXT("failed LC7 load exposes no generic Explanation model"),
			FailedPanel->Model.IsValid());
		TestFalse(TEXT("failed LC7 load exposes no partial LC7 profile"),
			FailedPanel->LC7Profile.IsValid());
		TestTrue(TEXT("failed LC7 load stays on the explicit error surface"),
			!FailedPanel->LastError.IsEmpty() &&
			!SlateWidgetText(FailedPanel).Contains(TEXT("LANES")) &&
			!SlateWidgetText(FailedPanel).Contains(TEXT("WEAVE")) &&
			!SlateWidgetText(FailedPanel).Contains(TEXT("ROUTE")));
	}

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC7Profile = LoadResult.Profile;
	Panel->LC7SelectedUnitId.Reset();
	Panel->LC7DetailMode = SBlueprintLensPanel::ELC7DetailMode::None;
	Panel->ResolveSources();
	Panel->PopulateExplanationOptions();

	int32 LC7SelectorCount = 0;
	for (int32 Index = 0; Index < Panel->ExplanationOptions.Num(); ++Index)
	{
		if (Panel->ExplanationOptions[Index].IsValid() &&
			*Panel->ExplanationOptions[Index] == TEXT("LC7_StaticSCC"))
		{
			++LC7SelectorCount;
			TestEqual(TEXT("LC7 selector loads the Explanation path"),
				FPaths::ConvertRelativePathToFull(Panel->ExplanationPaths[Index]),
				FPaths::ConvertRelativePathToFull(LC7ExplanationPath()));
		}
	}
	TestEqual(TEXT("LC7 selector owns exactly one frozen case"),
		LC7SelectorCount, 1);
	TestTrue(TEXT("LC7 panel recognizes the strict adaptive-backbone model"),
		Panel->IsLC7AdaptiveBackboneModel());
	for (const FString& MemberId : Projection.SCCs[0].OrderedSpineUnitIds)
	{
		const FBlueprintLensSourceReference* Anchor =
			Projection.SourceAnchors.Find(MemberId);
		TestNotNull(TEXT("every LC7 member owns a source anchor"), Anchor);
		TestTrue(TEXT("every LC7 member owns a native node guid"),
			Anchor != nullptr && !Anchor->NativeNodeGuid.IsEmpty());
		TestTrue(TEXT("every LC7 member resolves in the live fixture"),
			Anchor != nullptr && Panel->CanNavigateToSource(Anchor->SourceNodeId));
	}

	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	TestTrue(TEXT("LC7 panel starts neutral"),
		Panel->LC7SelectedUnitId.IsEmpty() &&
		Panel->LC7DetailMode == SBlueprintLensPanel::ELC7DetailMode::None);
	TestTrue(TEXT("LC7 overview is a hard clipping boundary"),
		Panel->LC7OverviewScrollBox.IsValid() &&
		Panel->LC7OverviewScrollBox->GetClipping() ==
			EWidgetClipping::ClipToBoundsAlways);
	TestTrue(TEXT("LC7 neutral canvas paints no paragraph text"),
		Panel->LC7Canvas.IsValid() &&
		Panel->LC7Canvas->GetParagraphTextCountForTesting() == 0);
	if (!Panel->LC7Canvas.IsValid() || !Panel->LC7OverviewScrollBox.IsValid())
	{
		return false;
	}
	const FString NeutralGeometryHash =
		Panel->LC7Canvas->GetLayoutForTesting().OverviewGeometryHash;
	const TSharedPtr<const FBlueprintLensLC7Profile> StableProfile =
		Panel->LC7Profile;
	for (const FString& MemberId : Projection.SCCs[0].OrderedSpineUnitIds)
	{
		Panel->LC7OverviewScrollBox->SetScrollOffset(180.0f);
		Panel->SelectLC7Unit(MemberId);
		TestEqual(TEXT("LC7 member selection persists"),
			Panel->LC7SelectedUnitId, MemberId);
		TestEqual(TEXT("LC7 first selection opens Summary"),
			Panel->LC7DetailMode, SBlueprintLensPanel::ELC7DetailMode::Summary);
		TestEqual(TEXT("LC7 selection preserves overview scroll"),
			Panel->LC7OverviewScrollBox->GetScrollOffset(), 180.0f);
		TestTrue(TEXT("LC7 detail pane owns its clipping scroll box"),
			Panel->LC7DetailScrollBox.IsValid() &&
			Panel->LC7DetailScrollBox->GetClipping() ==
				EWidgetClipping::ClipToBoundsAlways);
		TestTrue(TEXT("LC7 selection never reloads truth"),
			Panel->LC7Profile == StableProfile);
		TestEqual(TEXT("LC7 selection never moves overview geometry"),
			Panel->LC7Canvas->GetLayoutForTesting().OverviewGeometryHash,
			NeutralGeometryHash);
	}

	const FString FirstMember = Projection.SCCs[0].OrderedSpineUnitIds[0];
	const FString SecondMember = Projection.SCCs[0].OrderedSpineUnitIds[1];
	Panel->LC7DetailScrollBox->SetScrollOffset(90.0f);
	Panel->LC7OverviewScrollBox->SetScrollOffset(220.0f);
	Panel->SelectLC7Unit(FirstMember);
	TestEqual(TEXT("LC7 new member resets detail scroll only"),
		Panel->LC7DetailScrollBox->GetScrollOffset(), 0.0f);
	TestEqual(TEXT("LC7 new member preserves overview scroll"),
		Panel->LC7OverviewScrollBox->GetScrollOffset(), 220.0f);

	Panel->LC7DetailScrollBox->SetScrollOffset(60.0f);
	Panel->HandleLC7Action(TEXT("relations"));
	TestEqual(TEXT("LC7 Relations is mutually exclusive"),
		Panel->LC7DetailMode, SBlueprintLensPanel::ELC7DetailMode::Relations);
	const FString RelationsText = SlateWidgetText(Panel);
	for (const FBlueprintLensLC7Relation& Relation : Projection.Relations)
	{
		TestTrue(TEXT("LC7 Relations exposes every typed relation row"),
			RelationsText.Contains(Relation.RelationId));
	}
	Panel->HandleLC7Action(TEXT("evidence"));
	TestEqual(TEXT("LC7 Evidence replaces Relations"),
		Panel->LC7DetailMode, SBlueprintLensPanel::ELC7DetailMode::Evidence);
	const FBlueprintLensSourceReference* SelectedAnchor =
		Projection.SourceAnchors.Find(FirstMember);
	const FString EvidenceText = SlateWidgetText(Panel);
	TestTrue(TEXT("LC7 Evidence exposes projection and native-source hashes"),
		EvidenceText.Contains(Projection.IntegrityHash) &&
		SelectedAnchor != nullptr &&
		EvidenceText.Contains(SelectedAnchor->NativeNodeGuid));
	Panel->HandleLC7Action(TEXT("show_complete_text"));
	TestEqual(TEXT("LC7 Complete Text replaces Evidence"),
		Panel->LC7DetailMode, SBlueprintLensPanel::ELC7DetailMode::CompleteText);
	TestEqual(TEXT("LC7 Complete Text preserves selection"),
		Panel->LC7SelectedUnitId, FirstMember);
	const FString CompleteText = SlateWidgetText(Panel);
	TestTrue(TEXT("LC7 permanent Complete Text exposes the full source"),
		!Projection.CompleteTextLines.IsEmpty() &&
		CompleteText.Contains(Projection.CompleteTextLines[0]) &&
		CompleteText.Contains(Projection.CompleteTextLines.Last()));

	Panel->LC7DetailScrollBox->SetScrollOffset(110.0f);
	Panel->LC7OverviewScrollBox->SetScrollOffset(260.0f);
	const auto ModeBeforeSource = Panel->LC7DetailMode;
	const FString SelectionBeforeSource = Panel->LC7SelectedUnitId;
	Panel->HandleLC7Action(TEXT("open_source"));
	TestEqual(TEXT("LC7 source action is command-only for mode"),
		Panel->LC7DetailMode, ModeBeforeSource);
	TestEqual(TEXT("LC7 source action is command-only for selection"),
		Panel->LC7SelectedUnitId, SelectionBeforeSource);
	TestEqual(TEXT("LC7 source action preserves overview scroll"),
		Panel->LC7OverviewScrollBox->GetScrollOffset(), 260.0f);
	TestEqual(TEXT("LC7 source action preserves detail scroll"),
		Panel->LC7DetailScrollBox->GetScrollOffset(), 110.0f);

	Panel->SelectLC7Unit(FirstMember);
	TestTrue(TEXT("selecting the active LC7 member returns to neutral"),
		Panel->LC7SelectedUnitId.IsEmpty() &&
		Panel->LC7DetailMode == SBlueprintLensPanel::ELC7DetailMode::None);
	Panel->SelectLC7Unit(SecondMember);
	Panel->ResolvedSources.Reset();
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	const TArray<TSharedRef<SButton>> OpenSourceButtons =
		SlateButtonsWithLabel(Panel, TEXT("Open source"));
	TestTrue(TEXT("LC7 unresolved source action is visibly disabled"),
		OpenSourceButtons.Num() == 1 && !OpenSourceButtons[0]->IsEnabled());

	IConsoleVariable* WidthCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("BlueprintLens.LC7ReviewWidth"));
	IConsoleVariable* ScrollCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("BlueprintLens.LC7ReviewScrollOffset"));
	TestNotNull(TEXT("LC7 review width CVar exists"), WidthCVar);
	TestNotNull(TEXT("LC7 review scroll CVar exists"), ScrollCVar);
	if (WidthCVar != nullptr && ScrollCVar != nullptr)
	{
		TestEqual(TEXT("LC7 review width has zero product default"),
			WidthCVar->GetFloat(), 0.0f);
		TestEqual(TEXT("LC7 review scroll has zero product default"),
			ScrollCVar->GetFloat(), 0.0f);
		for (const float Width : {430.0f, 480.0f, 700.0f})
		{
			WidthCVar->Set(Width, ECVF_SetByCode);
			Panel->RootBox->SetContent(Panel->BuildLoadedContent());
			const FBlueprintLensLC7Layout& Layout =
				Panel->LC7Canvas->GetLayoutForTesting();
			if (Width >= 700.0f)
			{
				TestTrue(TEXT("LC7 700 panel places detail beside overview"),
					Layout.DetailBounds.Min.X >= Layout.OverviewBounds.Max.X);
			}
			else
			{
				TestTrue(TEXT("LC7 430/480 panel places detail below overview"),
					Layout.DetailBounds.Min.Y >= Layout.OverviewBounds.Max.Y);
			}
		}
		WidthCVar->Set(0.0f, ECVF_SetByCode);
	}

	FBlueprintLensLC7LayoutSessionResult InvalidFoldSession =
		FBlueprintLensLC7LayoutSession::Build(Projection, 700.0f, FString());
	FBlueprintLensLC7Fold InvalidFold;
	InvalidFold.FoldId = TEXT("LC7_PANEL_MUST_NOT_RENDER_FOLD");
	InvalidFold.UnitIds.Add(FirstMember);
	InvalidFold.UnitCount = 1;
	InvalidFold.ExpansionActionId = TEXT("expand_invalid_fold");
	InvalidFoldSession.Layout.Folds.Add(InvalidFold);
	TestFalse(TEXT("LC7 panel rejects a fold in the frozen real profile"),
		Panel->IsLC7PanelSessionRenderable(Projection, InvalidFoldSession));

	TSharedRef<FBlueprintLensLC7Profile> Broken =
		MakeShared<FBlueprintLensLC7Profile>(*LoadResult.Profile);
	Broken->Relations.RemoveAt(0);
	Panel->LC7Profile = Broken;
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC7SelectedUnitId.Reset();
	Panel->LC7DetailMode = SBlueprintLensPanel::ELC7DetailMode::None;
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	TestTrue(TEXT("invalid LC7 profile fails closed to Complete Text"),
		SlateWidgetText(Panel).Contains(TEXT("Complete Text fallback")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6ProjectionTest,
	"BlueprintLens.Editor.LC6.Projection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6ProjectionTest::RunTest(const FString&)
{
	const FBlueprintLensLC6LoadResult LoadResult =
		FBlueprintLensLC6ProfileLoader::LoadFiles(
			LC6CoreProfilePath(), LC6QueryProfilePath(),
			LC6ReadinessPath(), LC6RawPath());
	TestTrue(TEXT("LC6 profile loads for projection"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	const FBlueprintLensLC6Projection Projection =
		FBlueprintLensLC6Projector::Build(*LoadResult.Profile);
	TestTrue(TEXT("LC6 Four-Track projection is renderable"), Projection.IsRenderable());
	TestEqual(TEXT("LC6 projection status is FourTrack"), Projection.Status,
		EBlueprintLensLC6ProjectionStatus::FourTrack);
	TestEqual(TEXT("LC6 projection owns four tracks"), Projection.Tracks.Num(), 4);
	TestEqual(TEXT("LC6 projection owns two truth-owner bands"),
		Projection.OwnerBands.Num(), 2);
	TestEqual(TEXT("LC6 projection accounts for all 16 source nodes"),
		Projection.AllMemberIds.Num(), 16);
	TestEqual(TEXT("LC6 projection accounts for all 12 source edges"),
		Projection.AllRelationIds.Num(), 12);
	TestTrue(TEXT("LC6 projection retains evidence identities"),
		Projection.AllEvidenceIds.Num() >= 24);
	TestTrue(TEXT("LC6 projection owns a deterministic SHA-256 integrity hash"),
		Projection.IntegrityHash.Len() == 64);

	const FBlueprintLensLC6OwnerBand* CoreBand =
		Projection.OwnerBands.FindByPredicate([](const auto& Band)
		{
			return Band.TruthOwner == TEXT("core_node_classification");
		});
	const FBlueprintLensLC6OwnerBand* QueryBand =
		Projection.OwnerBands.FindByPredicate([](const auto& Band)
		{
			return Band.TruthOwner == TEXT("query_profile");
		});
	TestNotNull(TEXT("LC6 core owner band exists"), CoreBand);
	TestNotNull(TEXT("LC6 query owner band exists"), QueryBand);
	TestTrue(TEXT("LC6 owner bands preserve the 3+1 partition"),
		CoreBand != nullptr && CoreBand->ScenarioIds.Num() == 3 &&
		QueryBand != nullptr && QueryBand->ScenarioIds == TArray<FString>({TEXT("LC6_TRUNCATED")}));

	TSet<FString> CriterionIds;
	for (int32 Index = 0; Index < Projection.Tracks.Num(); ++Index)
	{
		const FBlueprintLensLC6Track& Track = Projection.Tracks[Index];
		TestEqual(TEXT("LC6 track order follows the stable scenario order"),
			Track.ScenarioId, LoadResult.Profile->Scenarios[Index].ScenarioId);
		TestTrue(TEXT("every LC6 track owns a criterion dock"),
			!Track.CriterionNodeId.IsEmpty() && !Track.CriterionTitle.IsEmpty());
		CriterionIds.Add(Track.CriterionNodeId);
		if (Track.TruthOwner == TEXT("core_node_classification"))
		{
			TestTrue(TEXT("every core track owns one semantic fence"),
				Track.bHasSemanticFence && !Track.BoundaryNodeId.IsEmpty());
			TestEqual(TEXT("core tracks never own a query Frontier"),
				Track.Frontiers.Num(), 0);
			TestFalse(TEXT("core tracks never invent omission aggregates"),
				Track.bHasOmissionAggregate);
		}
	}
	TestEqual(TEXT("LC6 owns four distinct criterion docks"), CriterionIds.Num(), 4);
	const FBlueprintLensLC6Track* QueryTrack = Projection.FindTrack(TEXT("LC6_TRUNCATED"));
	TestNotNull(TEXT("LC6 query track exists"), QueryTrack);
	if (QueryTrack != nullptr)
	{
		TestFalse(TEXT("query track does not own a semantic fence"),
			QueryTrack->bHasSemanticFence);
		TestEqual(TEXT("query track owns one exact Frontier"),
			QueryTrack->Frontiers.Num(), 1);
		TestTrue(TEXT("query track owns query-only 3/3 omissions"),
			QueryTrack->bHasOmissionAggregate &&
			QueryTrack->OmittedNodeCount == 3 && QueryTrack->OmittedEdgeCount == 3);
		TestTrue(TEXT("query track retains selected 4/3 and complete 7/6"),
			QueryTrack->SelectedNodeCount == 4 && QueryTrack->SelectedEdgeCount == 3 &&
			QueryTrack->CompleteNodeCount == 7 && QueryTrack->CompleteEdgeCount == 6);
	}

	for (const FBlueprintLensLC6Relation& Relation : Projection.Relations)
	{
		TestTrue(TEXT("every projected relation has a recoverable source endpoint"),
			Projection.AllMemberIds.Contains(Relation.SourceNodeId));
		TestTrue(TEXT("every projected relation has a recoverable target endpoint"),
			Projection.AllMemberIds.Contains(Relation.TargetNodeId));
		TestTrue(TEXT("every projected relation ID is recoverable"),
			Projection.AllRelationIds.Contains(Relation.RelationId));
	}

	FBlueprintLensLC6Profile Shuffled = *LoadResult.Profile;
	Algo::Reverse(Shuffled.Scenarios);
	for (FBlueprintLensLC6Scenario& Scenario : Shuffled.Scenarios)
	{
		Algo::Reverse(Scenario.SliceNodeIds);
		Algo::Reverse(Scenario.SliceEdgeIds);
		Algo::Reverse(Scenario.IncidentEdgeIds);
		Algo::Reverse(Scenario.SourcePinIds);
		Algo::Reverse(Scenario.CompleteNodeIds);
		Algo::Reverse(Scenario.CompleteEdgeIds);
		Algo::Reverse(Scenario.HopDistances);
	}
	Algo::Reverse(Shuffled.SourceEdges);
	const FBlueprintLensLC6Projection ShuffledProjection =
		FBlueprintLensLC6Projector::Build(Shuffled);
	TestTrue(TEXT("shuffled LC6 inputs remain renderable"),
		ShuffledProjection.IsRenderable());
	TestEqual(TEXT("LC6 projection hash ignores source array order"),
		ShuffledProjection.IntegrityHash, Projection.IntegrityHash);

	FBlueprintLensLC6Profile MissingTitle = *LoadResult.Profile;
	MissingTitle.SourceTitles.Remove(MissingTitle.Scenarios[0].BoundaryNodeId);
	const FBlueprintLensLC6Projection CompleteText =
		FBlueprintLensLC6Projector::Build(MissingTitle);
	TestEqual(TEXT("missing graphical fact fails closed to Complete Text"),
		CompleteText.Status, EBlueprintLensLC6ProjectionStatus::CompleteText);
	TestTrue(TEXT("Complete Text fallback remains recoverable"),
		!CompleteText.CompleteTextLines.IsEmpty());

	FBlueprintLensLC6Profile Empty = *LoadResult.Profile;
	Empty.Scenarios.Reset();
	const FBlueprintLensLC6Projection Unavailable =
		FBlueprintLensLC6Projector::Build(Empty);
	TestEqual(TEXT("missing textual truth becomes Unavailable"),
		Unavailable.Status, EBlueprintLensLC6ProjectionStatus::Unavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6LayoutTest,
	"BlueprintLens.Editor.LC6.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6LayoutTest::RunTest(const FString&)
{
	const FBlueprintLensLC6LoadResult LoadResult =
		FBlueprintLensLC6ProfileLoader::LoadFiles(
			LC6CoreProfilePath(), LC6QueryProfilePath(),
			LC6ReadinessPath(), LC6RawPath());
	TestTrue(TEXT("LC6 profile loads for layout"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC6Projection Projection =
		FBlueprintLensLC6Projector::Build(*LoadResult.Profile);
	const TArray<TPair<float, EBlueprintLensLC6LayoutMode>> Targets = {
		{430.0f, EBlueprintLensLC6LayoutMode::SingleColumn430},
		{480.0f, EBlueprintLensLC6LayoutMode::StackedDetail480},
		{700.0f, EBlueprintLensLC6LayoutMode::SideBySide700}};
	const TMap<float, FVector2D> ExpectedCanvas = {
		{430.0f, FVector2D(430.0f, 1350.0f)},
		{480.0f, FVector2D(480.0f, 1350.0f)},
		{700.0f, FVector2D(700.0f, 760.0f)}};

	for (const TPair<float, EBlueprintLensLC6LayoutMode>& Target : Targets)
	{
		const FBlueprintLensLC6Layout Layout =
			FBlueprintLensLC6LayoutBuilder::Build(Projection, Target.Key);
		TestEqual(TEXT("LC6 width selects exact responsive mode"),
			Layout.Mode, Target.Value);
		TestEqual(TEXT("LC6 canvas matches selected target"),
			Layout.CanvasSize, ExpectedCanvas.FindChecked(Target.Key));
		TestTrue(TEXT("LC6 layout covers accountable projection"),
			Layout.CoversProjection(Projection));
		TestTrue(TEXT("LC6 layout owns a complete normalized ledger"),
			Layout.HasValidSharedLedger());
		TestTrue(TEXT("LC6 layout matches selected SVG oracle within 1px"),
			Layout.MatchesVisualOracle(1.0f));
		TestTrue(TEXT("LC6 layout has no text or route collision"),
			Layout.HasNoTextOrRouteCollisions());
		TestEqual(TEXT("LC6 layout owns four hit targets"),
			Layout.Tracks.Num(), 4);
		TestEqual(TEXT("LC6 request owns all 16 source nodes"),
			Layout.LayoutRequest.Nodes.Num(), 16);
		TestEqual(TEXT("LC6 request owns all 12 source relations"),
			Layout.LayoutRequest.Edges.Num(), 12);
		TestEqual(TEXT("LC6 request owns two truth-owner groups"),
			Layout.LayoutRequest.Groups.Num(), 2);
		TestEqual(TEXT("LC6 layout owns all 16 source anchors"),
			Layout.SourceAnchors.Num(), 16);
		TestEqual(TEXT("LC6 layout owns four criterion markers"),
			Layout.Tracks.FilterByPredicate([](const auto& Track)
			{
				return Track.CriterionBounds.bIsValid &&
					Track.CriterionMarker != FVector2D::ZeroVector;
			}).Num(), 4);
		for (int32 Left = 0; Left < Layout.Tracks.Num(); ++Left)
		{
			for (int32 Right = Left + 1; Right < Layout.Tracks.Num(); ++Right)
			{
				TestFalse(TEXT("LC6 scenario hit targets never overlap"),
					Layout.Tracks[Left].HitBounds.Intersect(
						Layout.Tracks[Right].HitBounds));
			}
		}
		for (const FBlueprintLensLC6TrackLayout& Track : Layout.Tracks)
		{
			if (Track.ScenarioId == TEXT("LC6_TRUNCATED"))
			{
				TestTrue(TEXT("query track owns a non-colour Frontier diamond"),
					Track.FrontierBounds.bIsValid && !Track.bHasSemanticFence);
				TestEqual(TEXT("query selected nodes follow hop distance"),
					Track.QueryHopLabels,
					TArray<FString>({TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6")}));
			}
			else
			{
				TestTrue(TEXT("core track owns a non-colour semantic fence"),
					Track.bHasSemanticFence &&
					Track.SemanticFenceStart != Track.SemanticFenceEnd);
				TestFalse(TEXT("core track never owns Frontier geometry"),
					Track.FrontierBounds.bIsValid);
			}
		}
		const auto LabelById = [&Layout](const TCHAR* Id)
		{
			return Layout.Labels.FindByPredicate([Id](const auto& Label)
			{
				return Label.Id == Id;
			});
		};
		for (const TCHAR* Required : {
			TEXT("header.title"), TEXT("header.question"),
			TEXT("overview.core.owner"), TEXT("overview.query.owner"),
			TEXT("action.complete_text")})
		{
			TestNotNull(TEXT("LC6 selected target retains required label"),
				LabelById(Required));
		}
		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		for (const FBlueprintLensLC6Label& Label : Layout.Labels)
		{
			const FVector2D Measured = FontMeasure->Measure(
				Label.Text,
				BlueprintLensLC6Font(Label.FontSize, Label.bBold));
			TestTrue(
				FString::Printf(
					TEXT("LC6 %.0f label %s owns enough measured Slate space required=%.1fx%.1f allocated=%.1fx%.1f"),
					Target.Key, *Label.Id, Measured.X + 2.0f, Measured.Y,
					Label.Bounds.GetSize().X, Label.Bounds.GetSize().Y),
				Measured.X + 2.0f <= Label.Bounds.GetSize().X + 0.5f &&
					Measured.Y <= Label.Bounds.GetSize().Y + 0.5f);
		}

		const FBlueprintLensLC6LayoutSessionResult Session =
			FBlueprintLensLC6LayoutSession::Build(Projection, Target.Key);
		TestTrue(TEXT("LC6 layout session is renderable"),
			Session.IsRenderable(Projection));
		TestTrue(TEXT("LC6 layout tries ELK first"),
			Session.Attempts.Num() >= 1 &&
			Session.Attempts[0].Backend == EBlueprintLensLayoutBackendKind::ElkLayered);
		TestEqual(TEXT("LC6 session accepts exactly one ledger"),
			Session.Attempts.FilterByPredicate([](const auto& Attempt)
			{
				return Attempt.bAccepted;
			}).Num(), 1);
		TestTrue(TEXT("accepted LC6 ledger matches selected oracle"),
			Session.Layout.MatchesVisualOracle(1.0f));
	}

	FBlueprintLensLC6Projection Broken = Projection;
	Broken.Relations[0].TargetNodeId = TEXT("missing-node");
	TestFalse(TEXT("unrecoverable relation endpoint fails layout closed"),
		FBlueprintLensLC6LayoutBuilder::Build(Broken, 700.0f)
			.CoversProjection(Broken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6CanvasTest,
	"BlueprintLens.Editor.LC6.Canvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6CanvasTest::RunTest(const FString&)
{
	const FBlueprintLensLC6LoadResult LoadResult =
		FBlueprintLensLC6ProfileLoader::LoadFiles(
			LC6CoreProfilePath(), LC6QueryProfilePath(),
			LC6ReadinessPath(), LC6RawPath());
	TestTrue(TEXT("LC6 profile loads for canvas"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}
	const FBlueprintLensLC6Projection Projection =
		FBlueprintLensLC6Projector::Build(*LoadResult.Profile);
	TestEqual(TEXT("LC6 rounded brush fill is white"),
		BlueprintLensLC6RoundedBrushFill(), FLinearColor::White);
	const FLinearColor SemanticFill = FLinearColor::FromSRGBColor(
		FColor::FromHex(TEXT("#1D2430")));
	TestEqual(TEXT("LC6 semantic fill is supplied as element tint"),
		BlueprintLensLC6BoxElementTint(SemanticFill), SemanticFill);

	for (const float Width : {430.0f, 480.0f, 700.0f})
	{
		const FBlueprintLensLC6LayoutSessionResult Session =
			FBlueprintLensLC6LayoutSession::Build(Projection, Width);
		TSharedRef<SBlueprintLensLC6FourTrack> Canvas =
			SNew(SBlueprintLensLC6FourTrack)
			.Projection(Projection)
			.InitialSession(Session)
			.SelectedScenarioId(FString());
		Canvas->SlatePrepass(1.0f);
		const FVector2D DesiredSize = Canvas->GetDesiredSize();
		TestTrue(TEXT("LC6 canvas desired size matches target"),
			DesiredSize.Equals(Session.Layout.CanvasSize));
		TestEqual(TEXT("LC6 canvas exposes four hit targets"),
			Canvas->GetScenarioHitTargetsForTesting().Num(), 4);
		TestEqual(TEXT("LC6 routes paint before nodes"),
			Canvas->GetRoutePaintLayerForTesting() <
				Canvas->GetNodePaintLayerForTesting(), true);
		TestTrue(TEXT("LC6 track backgrounds paint below visible relations"),
			Canvas->GetTrackBackgroundPaintLayerForTesting() <
				Canvas->GetRoutePaintLayerForTesting());
		TestEqual(TEXT("LC6 criterion markers paint for four scenarios"),
			Canvas->GetCriterionMarkerCountForTesting(), 4);
		TestTrue(TEXT("LC6 canvas paints three semantic fences and one Frontier"),
			Canvas->GetSemanticFenceCountForTesting() == 3 &&
			Canvas->GetFrontierCountForTesting() == 1);
		for (const FBlueprintLensLC6TrackLayout& Track : Session.Layout.Tracks)
		{
			TestEqual(TEXT("LC6 hit testing resolves exact scenario"),
				Canvas->ResolveScenarioAtLocalPositionForTesting(
					Track.HitBounds.GetCenter()), Track.ScenarioId);
		}
		TestTrue(TEXT("LC6 empty-space hit testing stays neutral"),
			Canvas->ResolveScenarioAtLocalPositionForTesting(
				FVector2D(8.0f, 8.0f)).IsEmpty());
		TestEqual(TEXT("LC6 Complete Text action owns its visible hit target"),
			Canvas->ResolveActionAtLocalPositionForTesting(
				Session.Layout.CompleteTextActionBounds.GetCenter()),
			FString(TEXT("complete-text")));
	}

	const FBlueprintLensLC6LayoutSessionResult WideSession =
		FBlueprintLensLC6LayoutSession::Build(Projection, 700.0f);
	TSharedRef<SBlueprintLensLC6FourTrack> Selected =
		SNew(SBlueprintLensLC6FourTrack)
		.Projection(Projection)
		.InitialSession(WideSession)
		.SelectedScenarioId(FString(TEXT("LC6_UNCERTAIN")));
	TestTrue(TEXT("selected core track owns selected paint state"),
		Selected->IsScenarioSelected(TEXT("LC6_UNCERTAIN")));
	TestFalse(TEXT("unselected query track remains unselected"),
		Selected->IsScenarioSelected(TEXT("LC6_TRUNCATED")));
	TestFalse(TEXT("selected LC6 canvas does not paint the neutral detail copy"),
		SlateWidgetText(Selected).Contains(TEXT("Select a scenario")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC6PanelStateTest,
	"BlueprintLens.Editor.LC6.Panel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC6PanelStateTest::RunTest(const FString&)
{
	const FBlueprintLensLC6LoadResult LoadResult =
		FBlueprintLensLC6ProfileLoader::LoadFiles(
			LC6CoreProfilePath(), LC6QueryProfilePath(),
			LC6ReadinessPath(), LC6RawPath());
	TestTrue(TEXT("LC6 profile loads for panel"), LoadResult.IsSuccess());
	if (!LoadResult.IsSuccess())
	{
		AddError(LoadResult.Error);
		return false;
	}

	TSharedRef<SBlueprintLensPanel> Panel =
		SNew(SBlueprintLensPanel, TWeakPtr<FBlueprintEditor>());
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC6Profile = LoadResult.Profile;
	Panel->LC6SelectedScenarioId.Reset();
	Panel->LC6DetailMode = SBlueprintLensPanel::ELC6DetailMode::None;
	Panel->ResolveSources();
	Panel->PopulateExplanationOptions();
	for (const FBlueprintLensLC6Scenario& Scenario : Panel->LC6Profile->Scenarios)
	{
		const FBlueprintLensUnit* CriterionUnit =
			Panel->Model->FindUnit(Scenario.CriterionNodeId);
		TestTrue(TEXT("LC6 criterion exposes a native node guid for source navigation"),
			CriterionUnit != nullptr &&
			CriterionUnit->SourceReferences.Num() == 1 &&
			!CriterionUnit->SourceReferences[0].NativeNodeGuid.IsEmpty());
		TestTrue(TEXT("LC6 criterion source resolves in the live fixture"),
			Panel->CanNavigateToSource(Scenario.CriterionNodeId));
	}
	TestEqual(TEXT("LC6 selector owns one core-profile identity"),
		Panel->ExplanationOptions.FilterByPredicate([](const TSharedPtr<FString>& Option)
		{
			return Option.IsValid() &&
				*Option == TEXT("LC6_BoundaryMatrix.core-boundary-matrix");
		}).Num(), 1);

	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	TestTrue(TEXT("LC6 panel starts neutral"),
		Panel->LC6SelectedScenarioId.IsEmpty() &&
		Panel->LC6DetailMode == SBlueprintLensPanel::ELC6DetailMode::None);
	TestTrue(TEXT("LC6 overview scroll is a hard clipping boundary"),
		Panel->LC6OverviewScrollBox.IsValid() &&
		Panel->LC6OverviewScrollBox->GetClipping() ==
			EWidgetClipping::ClipToBoundsAlways);
	if (!Panel->LC6OverviewScrollBox.IsValid())
	{
		return false;
	}

	const TSharedPtr<const FBlueprintLensLC6Profile> StableProfile = Panel->LC6Profile;
	for (const FString& ScenarioId : {
		FString(TEXT("LC6_OPAQUE")), FString(TEXT("LC6_UNCERTAIN")),
		FString(TEXT("LC6_UNSUPPORTED")), FString(TEXT("LC6_TRUNCATED"))})
	{
		Panel->LC6OverviewScrollBox->SetScrollOffset(180.0f);
		Panel->SelectLC6Scenario(ScenarioId);
		TestEqual(TEXT("LC6 scenario selection persists"),
			Panel->LC6SelectedScenarioId, ScenarioId);
		TestEqual(TEXT("LC6 new selection opens Summary"),
			Panel->LC6DetailMode, SBlueprintLensPanel::ELC6DetailMode::Summary);
		TestEqual(TEXT("LC6 selection preserves overview scroll"),
			Panel->LC6OverviewScrollBox->GetScrollOffset(), 180.0f);
		TestTrue(TEXT("LC6 detail pane has its own clipping scroll box"),
			Panel->LC6DetailScrollBox.IsValid() &&
			Panel->LC6DetailScrollBox->GetClipping() ==
				EWidgetClipping::ClipToBoundsAlways);
		TestTrue(TEXT("LC6 selection rebuild does not reload truth"),
			Panel->LC6Profile == StableProfile);
	}

	Panel->LC6DetailScrollBox->SetScrollOffset(90.0f);
	Panel->LC6OverviewScrollBox->SetScrollOffset(220.0f);
	Panel->SelectLC6Scenario(TEXT("LC6_UNCERTAIN"));
	TestEqual(TEXT("LC6 changing scenario resets detail scroll only"),
		Panel->LC6DetailScrollBox->GetScrollOffset(), 0.0f);
	TestEqual(TEXT("LC6 changing scenario preserves overview scroll"),
		Panel->LC6OverviewScrollBox->GetScrollOffset(), 220.0f);

	Panel->LC6DetailScrollBox->SetScrollOffset(60.0f);
	Panel->HandleLC6Action(TEXT("relations"));
	TestEqual(TEXT("LC6 relations is mutually exclusive"),
		Panel->LC6DetailMode, SBlueprintLensPanel::ELC6DetailMode::Relations);
	TestEqual(TEXT("LC6 relations preserves selection"),
		Panel->LC6SelectedScenarioId, FString(TEXT("LC6_UNCERTAIN")));
	Panel->HandleLC6Action(TEXT("evidence"));
	TestEqual(TEXT("LC6 evidence replaces relations"),
		Panel->LC6DetailMode, SBlueprintLensPanel::ELC6DetailMode::Evidence);
	Panel->HandleLC6Action(TEXT("complete-text"));
	TestEqual(TEXT("LC6 Complete Text replaces evidence"),
		Panel->LC6DetailMode, SBlueprintLensPanel::ELC6DetailMode::CompleteText);
	TestEqual(TEXT("LC6 Complete Text preserves selection"),
		Panel->LC6SelectedScenarioId, FString(TEXT("LC6_UNCERTAIN")));
	const FString CompleteText = SlateWidgetText(Panel);
	TestTrue(TEXT("LC6 Complete Text exposes every status and Frontier"),
		CompleteText.Contains(TEXT("opaque")) &&
		CompleteText.Contains(TEXT("uncertain")) &&
		CompleteText.Contains(TEXT("unsupported")) &&
		CompleteText.Contains(TEXT("truncated")) &&
		CompleteText.Contains(TEXT("Frontier")));

	Panel->LC6DetailScrollBox->SetScrollOffset(110.0f);
	Panel->LC6OverviewScrollBox->SetScrollOffset(260.0f);
	const auto ModeBeforeSource = Panel->LC6DetailMode;
	const FString SelectionBeforeSource = Panel->LC6SelectedScenarioId;
	Panel->HandleLC6Action(TEXT("open-source"));
	TestEqual(TEXT("LC6 source action is command-only for mode"),
		Panel->LC6DetailMode, ModeBeforeSource);
	TestEqual(TEXT("LC6 source action is command-only for selection"),
		Panel->LC6SelectedScenarioId, SelectionBeforeSource);
	TestEqual(TEXT("LC6 source action preserves overview scroll"),
		Panel->LC6OverviewScrollBox->GetScrollOffset(), 260.0f);
	TestEqual(TEXT("LC6 source action preserves detail scroll"),
		Panel->LC6DetailScrollBox->GetScrollOffset(), 110.0f);

	Panel->SelectLC6Scenario(TEXT("LC6_UNCERTAIN"));
	TestTrue(TEXT("selecting active LC6 scenario returns to neutral"),
		Panel->LC6SelectedScenarioId.IsEmpty() &&
		Panel->LC6DetailMode == SBlueprintLensPanel::ELC6DetailMode::None);

	Panel->LC6SelectedScenarioId = TEXT("LC6_OPAQUE");
	Panel->LC6DetailMode = SBlueprintLensPanel::ELC6DetailMode::Summary;
	Panel->ResolvedSources.Reset();
	const FBlueprintLensLC6Projection Projection =
		FBlueprintLensLC6Projector::Build(*Panel->LC6Profile);
	const FBlueprintLensLC6Track* Opaque = Projection.FindTrack(TEXT("LC6_OPAQUE"));
	TestNotNull(TEXT("opaque track exists for unresolved source check"), Opaque);
	TestFalse(TEXT("LC6 unresolved source action is disabled"),
		Opaque != nullptr && Panel->CanNavigateToSource(Opaque->CriterionNodeId));

	TSharedRef<FBlueprintLensLC6Profile> Broken =
		MakeShared<FBlueprintLensLC6Profile>(*LoadResult.Profile);
	Broken->SourceTitles.Remove(Broken->Scenarios[0].BoundaryNodeId);
	Panel->LC6Profile = Broken;
	Panel->Model = LoadResult.ExplanationModel;
	Panel->LC6SelectedScenarioId.Reset();
	Panel->LC6DetailMode = SBlueprintLensPanel::ELC6DetailMode::None;
	Panel->RootBox->SetContent(Panel->BuildLoadedContent());
	TestTrue(TEXT("invalid LC6 projection fails closed to Complete Text"),
		SlateWidgetText(Panel).Contains(TEXT("Complete Text fallback")));
	return true;
}

#endif
