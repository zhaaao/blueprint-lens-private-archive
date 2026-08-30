// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintLensAsyncBarrier.generated.h"

enum class EBlueprintLensAsyncArrivalResult : uint8
{
	Pending,
	Released,
	Duplicate,
	Unknown,
	Closed
};

UENUM(BlueprintType)
enum class EBlueprintLensAsyncBarrierArrival : uint8
{
	Pending,
	Released,
	Duplicate,
	Unknown,
	Closed
};

struct BLUEPRINTLENSPROBE_API FBlueprintLensLC4AsyncTraceBindings
{
	FString InvocationControllerId;
	TMap<FName, FString> LaunchIds;
	TMap<FName, FString> CompletionIds;
	TMap<FName, FString> ArrivalIds;
	FString BarrierSiteId;
	FString BarrierReleaseId;
	FString CriterionId;

	bool IsComplete(FString& OutError) const;
};

UCLASS()
class BLUEPRINTLENSPROBE_API UBlueprintLensAsyncBarrierLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Blueprint Lens|Async", meta = (WorldContext = "WorldContextObject"))
	static void BeginLC4AsyncInvocation(UObject* WorldContextObject, FName InvocationId);

	UFUNCTION(BlueprintCallable, Category = "Blueprint Lens|Async", meta = (WorldContext = "WorldContextObject"))
	static void RecordLC4AsyncLaunch(UObject* WorldContextObject, FName InvocationId, FName ParticipantId);

	UFUNCTION(BlueprintCallable, Category = "Blueprint Lens|Async", meta = (WorldContext = "WorldContextObject", ExpandEnumAsExecs = "Result"))
	static void ArriveAtLC4Barrier(
		UObject* WorldContextObject,
		FName InvocationId,
		FName ParticipantId,
		EBlueprintLensAsyncBarrierArrival& Result);

	UFUNCTION(BlueprintCallable, Category = "Blueprint Lens|Async", meta = (WorldContext = "WorldContextObject"))
	static void RecordLC4AsyncCriterion(UObject* WorldContextObject, FName InvocationId);

	static bool ConfigureLC4AsyncTrace(
		const FString& ScheduleVariant,
		const FString& RunId,
		const FBlueprintLensLC4AsyncTraceBindings& Bindings,
		FString& OutError);

	static void ResetLC4AsyncTraceConfiguration();

	static bool CancelLC4AsyncInvocation(
		UObject* WorldContextObject,
		const FString& InvocationId,
		const FString& Reason,
		FString& OutError);
	static bool TimeoutLC4AsyncInvocation(
		UObject* WorldContextObject,
		const FString& InvocationId,
		FString& OutError);
	static bool ResetLC4AsyncInvocation(
		UObject* WorldContextObject,
		const FString& PreviousInvocationId,
		const FString& NewInvocationId,
		FString& OutError);

	static bool SaveLC4AsyncTrace(
		UObject* WorldContextObject,
		FString& OutFilePath,
		FString& OutError,
		bool bRequireComplete = true);
};

class BLUEPRINTLENSPROBE_API FBlueprintLensAsyncBarrierState
{
public:
	bool BeginInvocation(
		const FString& InInvocationId,
		const TArray<FName>& InParticipants,
		FString& OutError);

	EBlueprintLensAsyncArrivalResult Arrive(
		const FString& InInvocationId,
		FName ParticipantId);

	bool Cancel(
		const FString& InInvocationId,
		const FString& Reason,
		FString& OutError);

	bool Reset(
		const FString& PreviousInvocationId,
		const FString& NewInvocationId,
		const TArray<FName>& InParticipants,
		FString& OutError);

	int32 GetParticipantCount() const { return Participants.Num(); }
	int32 GetArrivedCount() const { return ArrivedParticipants.Num(); }
	const FString& GetInvocationId() const { return InvocationId; }
	const FString& GetCancellationReason() const { return CancellationReason; }
	bool IsOpen() const { return bOpen; }
	bool IsReleased() const { return bReleased; }
	bool IsCancelled() const { return bCancelled; }

private:
	bool ValidateNewInvocation(
		const FString& InInvocationId,
		const TArray<FName>& InParticipants,
		FString& OutError) const;
	void OpenInvocation(
		const FString& InInvocationId,
		const TArray<FName>& InParticipants);

	FString InvocationId;
	TSet<FName> Participants;
	TSet<FName> ArrivedParticipants;
	FString CancellationReason;
	bool bOpen = false;
	bool bReleased = false;
	bool bCancelled = false;
};
