#include "BlueprintLensEditorModule.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModes.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorTabs.h"
#include "Editor.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "SBlueprintLensPanel.h"
#include "Styling/AppStyle.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#define LOCTEXT_NAMESPACE "BlueprintLensEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintLensEditor, Log, All);

const FName FBlueprintLensEditorModule::SemanticLaneTabId(
	TEXT("BlueprintLens.SemanticLane"));

namespace
{
class FBlueprintLensTabFactory final : public FWorkflowTabFactory
{
public:
	explicit FBlueprintLensTabFactory(
		const TSharedPtr<FBlueprintEditor>& InBlueprintEditor)
		: FWorkflowTabFactory(
			  FBlueprintLensEditorModule::SemanticLaneTabId,
			  InBlueprintEditor)
		, BlueprintEditor(InBlueprintEditor)
	{
		TabLabel = LOCTEXT("BlueprintLensTabLabel", "Blueprint Lens");
		TabIcon = FSlateIcon(
			FAppStyle::GetAppStyleSetName(),
			"Kismet.Tabs.FindResults");
		bIsSingleton = true;
		ViewMenuDescription =
			LOCTEXT("BlueprintLensViewMenu", "Blueprint Lens");
		ViewMenuTooltip = LOCTEXT(
			"BlueprintLensTooltip",
			"Compare information-matched Blueprint explanation representations");
	}

	virtual TSharedRef<SWidget> CreateTabBody(
		const FWorkflowTabSpawnInfo&) const override
	{
		return SNew(SBlueprintLensPanel, BlueprintEditor);
	}

	virtual FText GetTabToolTipText(
		const FWorkflowTabSpawnInfo&) const override
	{
		return ViewMenuTooltip;
	}

private:
	TWeakPtr<FBlueprintEditor> BlueprintEditor;
};
} // namespace

void FBlueprintLensEditorModule::StartupModule()
{
	if (ExplanationPathConsoleCommand == nullptr)
	{
		ExplanationPathConsoleCommand =
			IConsoleManager::Get().RegisterConsoleCommand(
				TEXT("BlueprintLens.ExplanationPath"),
				TEXT("Set a session-only absolute reader-model JSON path; "
					 "pass no argument to restore the bundled fixture."),
				FConsoleCommandWithArgsDelegate::CreateRaw(
					this,
					&FBlueprintLensEditorModule::
						HandleExplanationPathCommand),
				ECVF_Default);
	}

	if (GEditor)
	{
		RegisterTabExtensions();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this,
			&FBlueprintLensEditorModule::RegisterTabExtensions);
	}
}

void FBlueprintLensEditorModule::RegisterTabExtensions()
{
	if (bTabExtensionRegistered)
	{
		return;
	}

	FBlueprintEditorModule& BlueprintEditorModule =
		FModuleManager::LoadModuleChecked<FBlueprintEditorModule>(
			TEXT("Kismet"));
	BlueprintTabsHandle =
		BlueprintEditorModule.OnRegisterTabsForEditor().AddRaw(
			this,
			&FBlueprintLensEditorModule::RegisterBlueprintEditorTabs);
	BlueprintLayoutHandle =
		BlueprintEditorModule.OnRegisterLayoutExtensions().AddRaw(
			this,
			&FBlueprintLensEditorModule::ExtendBlueprintEditorLayout);
	bTabExtensionRegistered = true;
}

void FBlueprintLensEditorModule::ShutdownModule()
{
	if (ExplanationPathConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(
			ExplanationPathConsoleCommand);
		ExplanationPathConsoleCommand = nullptr;
	}
	ExplanationPathOverride.Reset();

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("Kismet")))
	{
		FBlueprintEditorModule& BlueprintEditorModule =
			FModuleManager::GetModuleChecked<FBlueprintEditorModule>(
				TEXT("Kismet"));
		if (BlueprintTabsHandle.IsValid())
		{
			BlueprintEditorModule.OnRegisterTabsForEditor().Remove(
				BlueprintTabsHandle);
		}
		if (BlueprintLayoutHandle.IsValid())
		{
			BlueprintEditorModule.OnRegisterLayoutExtensions().Remove(
				BlueprintLayoutHandle);
		}
	}

	BlueprintTabsHandle.Reset();
	BlueprintLayoutHandle.Reset();
	PostEngineInitHandle.Reset();
	bTabExtensionRegistered = false;
}

FString FBlueprintLensEditorModule::GetExplanationPath() const
{
	if (!ExplanationPathOverride.IsEmpty())
	{
		return ExplanationPathOverride;
	}

	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("BlueprintLensExporter"));
	const FString FixturePath = Plugin.IsValid()
		? FPaths::Combine(
			  Plugin->GetBaseDir(),
			  TEXT("Resources/Explanation/"
				   "BP_SlicingProbe.set-health.explanation.v1.json"))
		: FPaths::Combine(
			  FPaths::ProjectPluginsDir(),
			  TEXT("BlueprintLensExporter/Resources/Explanation/"
				   "BP_SlicingProbe.set-health.explanation.v1.json"));
	return FPaths::ConvertRelativePathToFull(FixturePath);
}

void FBlueprintLensEditorModule::SetExplanationPathOverride(
	const FString& Path)
{
	ExplanationPathOverride = Path;
}

void FBlueprintLensEditorModule::HandleExplanationPathCommand(
	const TArray<FString>& Args)
{
	if (Args.IsEmpty())
	{
		SetExplanationPathOverride(FString());
		UE_LOG(
			LogBlueprintLensEditor,
			Display,
			TEXT("BlueprintLens reader-model path reset to '%s'"),
			*GetExplanationPath());
		return;
	}

	if (Args.Num() != 1)
	{
		UE_LOG(
			LogBlueprintLensEditor,
			Warning,
			TEXT("BlueprintLens.ExplanationPath expects zero arguments or "
				 "one absolute JSON path; received %d arguments"),
			Args.Num());
		return;
	}

	if (FPaths::IsRelative(Args[0]))
	{
		UE_LOG(
			LogBlueprintLensEditor,
			Warning,
			TEXT("BlueprintLens.ExplanationPath rejected relative path '%s'"),
			*Args[0]);
		return;
	}

	SetExplanationPathOverride(Args[0]);
	UE_LOG(
		LogBlueprintLensEditor,
		Display,
		TEXT("BlueprintLens reader-model path set to '%s'"),
		*GetExplanationPath());
}

void FBlueprintLensEditorModule::RegisterBlueprintEditorTabs(
	FWorkflowAllowedTabSet& TabFactories,
	FName ModeName,
	TSharedPtr<FBlueprintEditor> BlueprintEditor)
{
	if (ModeName !=
			FBlueprintEditorApplicationModes::StandardBlueprintEditorMode ||
		!BlueprintEditor.IsValid() ||
		TabFactories.GetFactory(SemanticLaneTabId).IsValid())
	{
		return;
	}

	TabFactories.RegisterFactory(
		MakeShared<FBlueprintLensTabFactory>(BlueprintEditor));
}

void FBlueprintLensEditorModule::ExtendBlueprintEditorLayout(
	FLayoutExtender& Extender)
{
	Extender.ExtendLayout(
		FBlueprintEditorTabs::DetailsID,
		ELayoutExtensionPosition::After,
		FTabManager::FTab(
			FTabId(SemanticLaneTabId, ETabIdFlags::SaveLayout),
			ETabState::ClosedTab));
}

IMPLEMENT_MODULE(FBlueprintLensEditorModule, BlueprintLensEditor)

#undef LOCTEXT_NAMESPACE
