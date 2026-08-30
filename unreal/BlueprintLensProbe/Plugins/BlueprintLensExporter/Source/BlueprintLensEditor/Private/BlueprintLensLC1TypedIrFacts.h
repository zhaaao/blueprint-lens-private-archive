#pragma once

#include "BlueprintLensExplanationModel.h"
#include "CoreMinimal.h"

struct FBlueprintLensLC1OperationFact
{
	FString SourceNodeId;
	FString OperationClass;
	FString ValuePinId;
	FString VariableTarget;
	FString ValueType;
	FString LiteralValue;
};

struct FBlueprintLensLC1NodeFact
{
	FString SourceNodeId;
	FString GraphId;
	FString NodeClass;
	FString NativeTitle;
	bool bHasSymbol = false;
	FString SymbolName;
	bool bIsSelfContext = false;
	bool bIsPure = false;
	bool bIsLatent = false;
	struct FPin
	{
		FString PinId;
		FString Name;
		FString Direction;
		FString Kind;
	};
	TArray<FPin> Pins;
};

struct FBlueprintLensLC1EdgeFact
{
	FString EdgeId;
	FString GraphId;
	FString Kind;
	FString SourceNodeId;
	FString SourcePinId;
	FString TargetNodeId;
	FString TargetPinId;
	bool bDirectionIsValid = false;
};

struct FBlueprintLensLC1GraphFact
{
	FString GraphId;
	FString GraphName;
	FString GraphKind;
	TArray<FString> NodeIds;
	TArray<FString> EdgeIds;
};

struct FBlueprintLensLC1TypedIrFacts
{
	TArray<FBlueprintLensLC1GraphFact> Graphs;
	TMap<FString, FBlueprintLensLC1GraphFact> GraphsById;
	TMap<FString, FBlueprintLensLC1NodeFact> NodesBySourceNodeId;
	TMap<FString, FBlueprintLensLC1OperationFact> OperationsBySourceNodeId;
	TArray<FBlueprintLensLC1EdgeFact> Edges;
	FString VerifiedIrSha256;
	FString Error;

	bool IsValid() const
	{
		return Error.IsEmpty();
	}
};

class FBlueprintLensLC1TypedIrFactLoader
{
public:
	static FBlueprintLensLC1TypedIrFacts LoadFile(
		const FBlueprintLensSource& Source,
		bool bRequireVariableSetFacts = true);
	static FBlueprintLensLC1TypedIrFacts LoadJson(
		const FString& JsonText,
		const FString& ExpectedIrSha256,
		bool bRequireVariableSetFacts = true);
};
