// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class IConsoleObject;

class FBlueprintLensExporterModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandleExportCommand(const TArray<FString>& Args);
	void HandleExportBatchCommand(const TArray<FString>& Args);
	void HandleAuditCommand(const TArray<FString>& Args);
	void HandleExportSequenceFactsCommand(const TArray<FString>& Args);
	void HandleAuditSequenceCompilerOrderCommand(const TArray<FString>& Args);
	void HandleCreateLC4SequenceFixtureCommand(const TArray<FString>& Args);
	void HandleExportIntraBpPureCallFactsCommand(const TArray<FString>& Args);
	void HandleAuditIntraBpPureCallCommand(const TArray<FString>& Args);
	void HandleCaptureLC6BoundaryTruthCommand(const TArray<FString>& Args);
	void HandleCaptureLC7StaticSCCTruthCommand(const TArray<FString>& Args);

	IConsoleObject* ExportConsoleCommand = nullptr;
	IConsoleObject* ExportBatchConsoleCommand = nullptr;
	IConsoleObject* AuditConsoleCommand = nullptr;
	IConsoleObject* ExportSequenceFactsConsoleCommand = nullptr;
	IConsoleObject* AuditSequenceCompilerOrderConsoleCommand = nullptr;
	IConsoleObject* CreateLC4SequenceFixtureConsoleCommand = nullptr;
	IConsoleObject* ExportIntraBpPureCallFactsConsoleCommand = nullptr;
	IConsoleObject* AuditIntraBpPureCallConsoleCommand = nullptr;
	IConsoleObject* CaptureLC6BoundaryTruthConsoleCommand = nullptr;
	IConsoleObject* CaptureLC7StaticSCCTruthConsoleCommand = nullptr;
};
