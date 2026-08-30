#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleManager.h"

class FBlueprintEditor;
class FLayoutExtender;
class FWorkflowAllowedTabSet;
class IConsoleObject;

class BLUEPRINTLENSEDITOR_API FBlueprintLensEditorModule final
	: public IModuleInterface
{
public:
	static const FName SemanticLaneTabId;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FString GetExplanationPath() const;
	void SetExplanationPathOverride(const FString& Path);
	void HandleExplanationPathCommand(const TArray<FString>& Args);

	bool IsTabExtensionRegistered() const
	{
		return bTabExtensionRegistered;
	}

private:
	void RegisterTabExtensions();
	void RegisterBlueprintEditorTabs(
		FWorkflowAllowedTabSet& TabFactories,
		FName ModeName,
		TSharedPtr<FBlueprintEditor> BlueprintEditor);
	void ExtendBlueprintEditorLayout(FLayoutExtender& Extender);

	FDelegateHandle BlueprintTabsHandle;
	FDelegateHandle BlueprintLayoutHandle;
	FDelegateHandle PostEngineInitHandle;
	FString ExplanationPathOverride;
	IConsoleObject* ExplanationPathConsoleCommand = nullptr;
	bool bTabExtensionRegistered = false;
};
