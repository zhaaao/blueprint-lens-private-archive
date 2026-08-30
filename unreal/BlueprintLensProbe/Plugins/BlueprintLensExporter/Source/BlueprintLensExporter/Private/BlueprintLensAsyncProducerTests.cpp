// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintLensAsyncCompilerAudit.h"
#include "BlueprintLensAsyncFacts.h"
#include "BlueprintLensAsyncBarrier.h"
#include "BlueprintLensLC4AsyncFixture.h"
#include "BlueprintLensSequenceFacts.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool IsLowerHexSha256(const FString& Value)
	{
		if (Value.Len() != 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!FChar::IsDigit(Character) && (Character < TEXT('a') || Character > TEXT('f')))
			{
				return false;
			}
		}
		return true;
	}

	bool LoadTraceBindings(
		const FString& SourcePath,
		FBlueprintLensLC4AsyncTraceBindings& OutBindings,
		FString& OutError)
	{
		FString JsonText;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(JsonText, *SourcePath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root)
			|| Root == nullptr)
		{
			OutError = TEXT("Could not load async source bindings.");
			return false;
		}
		const TSharedPtr<FJsonObject>* Criterion = nullptr;
		const TSharedPtr<FJsonObject>* Barrier = nullptr;
		if (!Root->TryGetObjectField(TEXT("criterion"), Criterion) || Criterion == nullptr
			|| !Root->TryGetObjectField(TEXT("barrier"), Barrier) || Barrier == nullptr)
		{
			OutError = TEXT("Async source bindings omit criterion or barrier.");
			return false;
		}
		OutBindings.InvocationControllerId = (*Barrier)->GetStringField(TEXT("begin_invocation_node_id"));
		OutBindings.BarrierSiteId = (*Barrier)->GetStringField(TEXT("barrier_site_id"));
		OutBindings.BarrierReleaseId = (*Barrier)->GetStringField(TEXT("release_site_id"));
		OutBindings.CriterionId = (*Criterion)->GetStringField(TEXT("node_id"));
		for (const TSharedPtr<FJsonValue>& LaunchValue : Root->GetArrayField(TEXT("launches")))
		{
			const TSharedPtr<FJsonObject> Launch = LaunchValue->AsObject();
			OutBindings.LaunchIds.Add(
				FName(*Launch->GetStringField(TEXT("participant_id"))),
				Launch->GetStringField(TEXT("launch_node_id")));
		}
		for (const TSharedPtr<FJsonValue>& ContinuationValue : Root->GetArrayField(TEXT("continuations")))
		{
			const TSharedPtr<FJsonObject> Continuation = ContinuationValue->AsObject();
			OutBindings.CompletionIds.Add(
				FName(*Continuation->GetStringField(TEXT("continuation_id"))),
				Continuation->GetStringField(TEXT("node_id")));
		}
		for (const TSharedPtr<FJsonValue>& ParticipantValue : Root->GetArrayField(TEXT("participants")))
		{
			const TSharedPtr<FJsonObject> Participant = ParticipantValue->AsObject();
			OutBindings.ArrivalIds.Add(
				FName(*Participant->GetStringField(TEXT("participant_id"))),
				Participant->GetStringField(TEXT("arrival_node_id")));
		}
		return OutBindings.IsComplete(OutError);
	}

	FString ExpectedSourceOccurrence(
		const FBlueprintLensLC4AsyncTraceBindings& Bindings,
		const FString& EventKind,
		const FString& ParticipantId)
	{
		if (EventKind == TEXT("invocation_started"))
		{
			return Bindings.InvocationControllerId;
		}
		if (EventKind == TEXT("trace_boundary"))
		{
			return ParticipantId == TEXT("close")
				? Bindings.CriterionId
				: Bindings.InvocationControllerId;
		}
		if (EventKind == TEXT("launch"))
		{
			return Bindings.LaunchIds.FindRef(FName(*ParticipantId));
		}
		if (EventKind == TEXT("completion"))
		{
			return Bindings.CompletionIds.FindRef(FName(*ParticipantId));
		}
		if (EventKind == TEXT("barrier_arrival"))
		{
			return Bindings.ArrivalIds.FindRef(FName(*ParticipantId));
		}
		if (EventKind == TEXT("barrier_release"))
		{
			return Bindings.BarrierReleaseId;
		}
		if (EventKind == TEXT("criterion"))
		{
			return Bindings.CriterionId;
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4AsyncFixtureAndProducerTest,
	"BlueprintLens.Exporter.LC4AsyncFixtureAndProducers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4AsyncFixtureAndProducerTest::RunTest(const FString&)
{
	FString AssetObjectPath;
	FString SequenceNodeId;
	FString CriterionNodeId;
	FString Error;
	if (!BlueprintLensLC4AsyncFixture::EnsureFixture(
		AssetObjectPath,
		SequenceNodeId,
		CriterionNodeId,
		Error))
	{
		AddError(FString::Printf(TEXT("LC4_ASYNC_FIXTURE_FAILED: %s"), *Error));
		return false;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
	if (!TestNotNull(TEXT("async fixture loads"), Blueprint))
	{
		return false;
	}
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("async fixture EventGraph"), EventGraph))
	{
		return false;
	}

	int32 SequenceCount = 0;
	int32 LatentDelayCount = 0;
	int32 CriterionSetCount = 0;
	int32 CriterionRecordCount = 0;
	TArray<UK2Node_CallFunction*> FixtureDelays;
	UK2Node_VariableSet* CriterionSet = nullptr;
	UK2Node_CallFunction* CriterionRecord = nullptr;
	TArray<UK2Node_CallFunction*> ArrivalCalls;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		SequenceCount += Node != nullptr && Node->IsA<UK2Node_ExecutionSequence>() ? 1 : 0;
		if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
		{
			if (SetNode->GetVarName() == TEXT("LC4AsyncComplete"))
			{
				++CriterionSetCount;
				CriterionSet = SetNode;
			}
		}
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			if (Function != nullptr
				&& Function->GetFName() == TEXT("Delay")
				&& Function->HasMetaData(TEXT("Latent")))
			{
				++LatentDelayCount;
				FixtureDelays.Add(Call);
			}
			if (Function != nullptr && Function->GetFName() == TEXT("ArriveAtLC4Barrier"))
			{
				ArrivalCalls.Add(Call);
			}
			if (Function != nullptr && Function->GetFName() == TEXT("RecordLC4AsyncCriterion"))
			{
				++CriterionRecordCount;
				CriterionRecord = Call;
			}
		}
	}
	TestEqual(TEXT("exactly one Sequence launch root"), SequenceCount, 1);
	TestEqual(TEXT("exactly two latent Delay sites"), LatentDelayCount, 2);
	TestNotNull(
		TEXT("generated class owns LC4AsyncComplete bool"),
		FindFProperty<FBoolProperty>(Blueprint->GeneratedClass, TEXT("LC4AsyncComplete")));
	TestEqual(TEXT("exactly one canonical LC4AsyncComplete Set"), CriterionSetCount, 1);
	TestEqual(TEXT("exactly one criterion trace Record call"), CriterionRecordCount, 1);
	if (CriterionSet != nullptr && CriterionRecord != nullptr && ArrivalCalls.Num() == 2)
	{
		const FString GraphId = EventGraph->GetPathName();
		TestEqual(
			TEXT("fixture criterion identity is canonical Set"),
			CriterionNodeId,
			BlueprintLensSequenceFacts::MakeNodeId(GraphId, *CriterionSet));
		UEdGraphPin* SetExecute = CriterionSet->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SetThen = CriterionSet->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* RecordExecute = CriterionRecord->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		TestNotNull(TEXT("criterion Set execute pin"), SetExecute);
		TestNotNull(TEXT("criterion Set then pin"), SetThen);
		if (SetExecute != nullptr)
		{
			TestEqual(TEXT("both Released paths enter the canonical Set"), SetExecute->LinkedTo.Num(), 2);
			for (UK2Node_CallFunction* Arrival : ArrivalCalls)
			{
				UEdGraphPin* Released = Arrival->FindPin(TEXT("Released"), EGPD_Output);
				TestTrue(
					TEXT("arrival Released has unique canonical Set target"),
					Released != nullptr && Released->LinkedTo.Num() == 1 && Released->LinkedTo[0] == SetExecute);
			}
		}
		TestTrue(
			TEXT("canonical Set uniquely precedes criterion Record"),
			SetThen != nullptr && RecordExecute != nullptr
				&& SetThen->LinkedTo.Num() == 1 && SetThen->LinkedTo[0] == RecordExecute
				&& RecordExecute->LinkedTo.Num() == 1 && RecordExecute->LinkedTo[0] == SetThen);
	}

	FString SourcePath;
	FString AuditPath;
	BlueprintLensAsyncFacts::FAsyncFactStats SourceStats;
	BlueprintLensAsyncCompilerAudit::FAsyncCompilerAuditStats AuditStats;
	if (!TestTrue(
		TEXT("async source export succeeds"),
		BlueprintLensAsyncFacts::ExportAsyncFacts(
			*Blueprint,
			SequenceNodeId,
			CriterionNodeId,
			SourcePath,
			SourceStats,
			Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(
		TEXT("async compiler audit succeeds"),
		BlueprintLensAsyncCompilerAudit::AuditAsyncCompilerLinkage(
			*Blueprint,
			SourcePath,
			AuditPath,
			AuditStats,
			Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("source launch count"), SourceStats.LaunchCount, 2);
	TestEqual(TEXT("source continuation count"), SourceStats.ContinuationCount, 2);
	TestEqual(TEXT("source participant count"), SourceStats.ParticipantCount, 2);
	TestEqual(TEXT("compiler linkage count"), AuditStats.LinkageCount, 2);
	TestEqual(TEXT("compiler/source linkages agree"), AuditStats.MatchedSourceCount, 2);

	FString JsonText;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("async source JSON readable"), FFileHelper::LoadFileToString(JsonText, *SourcePath)))
	{
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!TestTrue(TEXT("async source JSON parses"), FJsonSerializer::Deserialize(Reader, Root))
		|| !TestNotNull(TEXT("async source root"), Root.Get()))
	{
		return false;
	}
	TestEqual(
		TEXT("async source format"),
		Root->GetStringField(TEXT("format")),
		TEXT("blueprint-lens-async-source"));
	TestEqual(
		TEXT("criterion binding"),
		Root->GetStringField(TEXT("criterion_node_id")),
		CriterionNodeId);
	TestEqual(
		TEXT("async source rules version"),
		Root->GetStringField(TEXT("rules_version")),
		TEXT("1.0.0"));

	const TSharedPtr<FJsonObject>* Provenance = nullptr;
	if (TestTrue(
		TEXT("source provenance present"),
		Root->TryGetObjectField(TEXT("provenance"), Provenance))
		&& TestNotNull(TEXT("source provenance object"), Provenance == nullptr ? nullptr : Provenance->Get()))
	{
		TestEqual(
			TEXT("provenance asset path"),
			(*Provenance)->GetStringField(TEXT("asset_path")),
			Blueprint->GetPathName());
		TestEqual(
			TEXT("provenance graph id"),
			(*Provenance)->GetStringField(TEXT("graph_id")),
			EventGraph->GetPathName());
		TestEqual(
			TEXT("provenance compile state"),
			(*Provenance)->GetStringField(TEXT("compile_state")),
			TEXT("up_to_date"));
		TestEqual(
			TEXT("compile hash uses canonical bytecode expression stream"),
			(*Provenance)->GetStringField(TEXT("compile_hash_basis")),
			TEXT("canonical_bytecode_expression_stream_v1"));
		TestTrue(
			TEXT("asset provenance uses lowercase SHA-256"),
			IsLowerHexSha256((*Provenance)->GetStringField(TEXT("asset_sha256"))));
		TestTrue(
			TEXT("compile provenance uses lowercase SHA-256"),
			IsLowerHexSha256((*Provenance)->GetStringField(TEXT("compile_sha256"))));
		TestEqual(
			TEXT("source declares base plus schedule overlay state"),
			(*Provenance)->GetStringField(TEXT("asset_state")),
			TEXT("base_asset_plus_schedule_overlay"));
		TestTrue(
			TEXT("source overlay provenance uses lowercase SHA-256"),
			IsLowerHexSha256(
				(*Provenance)->GetObjectField(TEXT("schedule_overlay"))->GetStringField(TEXT("overlay_sha256"))));
		TestTrue(
			TEXT("source active state provenance uses lowercase SHA-256"),
			IsLowerHexSha256((*Provenance)->GetStringField(TEXT("active_state_sha256"))));
		FString AuditText;
		if (TestTrue(TEXT("async compiler audit readable"), FFileHelper::LoadFileToString(AuditText, *AuditPath)))
		{
			TestTrue(
				TEXT("compiler audit binds source asset SHA-256"),
				AuditText.Contains(FString::Printf(
					TEXT("SOURCE_ASSET_SHA256\t%s"),
					*(*Provenance)->GetStringField(TEXT("asset_sha256")))));
			TestTrue(
				TEXT("compiler audit binds source compile SHA-256"),
				AuditText.Contains(FString::Printf(
					TEXT("SOURCE_COMPILE_SHA256\t%s"),
					*(*Provenance)->GetStringField(TEXT("compile_sha256")))));
		}
	}

	const TSharedPtr<FJsonObject>* Criterion = nullptr;
	if (TestTrue(
		TEXT("criterion source binding present"),
		Root->TryGetObjectField(TEXT("criterion"), Criterion))
		&& TestNotNull(TEXT("criterion source binding object"), Criterion == nullptr ? nullptr : Criterion->Get()))
	{
		TestEqual(TEXT("criterion node identity"), (*Criterion)->GetStringField(TEXT("node_id")), CriterionNodeId);
		TestFalse(TEXT("criterion execute pin identity"), (*Criterion)->GetStringField(TEXT("execute_pin_id")).IsEmpty());
		TestEqual(
			TEXT("criterion source action"),
			(*Criterion)->GetStringField(TEXT("source_action")),
			TEXT("Set LC4AsyncComplete"));
	}

	const TArray<TSharedPtr<FJsonValue>>& Launches = Root->GetArrayField(TEXT("launches"));
	const TArray<TSharedPtr<FJsonValue>>& Continuations = Root->GetArrayField(TEXT("continuations"));
	const TArray<TSharedPtr<FJsonValue>>& Participants = Root->GetArrayField(TEXT("participants"));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const TSharedPtr<FJsonObject> Launch = Launches[Index]->AsObject();
		const TSharedPtr<FJsonObject> Continuation = Continuations[Index]->AsObject();
		const TSharedPtr<FJsonObject> Participant = Participants[Index]->AsObject();
		TestFalse(TEXT("launch node identity"), Launch->GetStringField(TEXT("launch_node_id")).IsEmpty());
		TestFalse(TEXT("launch connected target identity"), Launch->GetStringField(TEXT("connected_target_node_id")).IsEmpty());
		TestFalse(TEXT("continuation source node GUID"), Continuation->GetStringField(TEXT("source_node_guid")).IsEmpty());
		TestNotEqual(TEXT("continuation latent UUID"), static_cast<int32>(Continuation->GetNumberField(TEXT("latent_uuid"))), 0);
		TestTrue(TEXT("continuation resume code offsets"), Continuation->GetArrayField(TEXT("resume_code_offsets")).Num() > 0);
		TestFalse(TEXT("continuation resume pin identity"), Continuation->GetStringField(TEXT("resume_pin_id")).IsEmpty());
		TestFalse(TEXT("arrival execute pin identity"), Participant->GetStringField(TEXT("arrival_execute_pin_id")).IsEmpty());
		TestFalse(TEXT("arrival release pin identity"), Participant->GetStringField(TEXT("release_pin_id")).IsEmpty());
		TestEqual(
			TEXT("connected target is the source launch occurrence"),
			Launch->GetStringField(TEXT("connected_target_node_id")),
			Launch->GetStringField(TEXT("launch_node_id")));
	}

	FString StaleSourceJson = JsonText.Replace(
		*Root->GetObjectField(TEXT("provenance"))->GetStringField(TEXT("compile_sha256")),
		TEXT("0000000000000000000000000000000000000000000000000000000000000000"));
	const FString StaleSourcePath = SourcePath + TEXT(".stale.json");
	TestTrue(
		TEXT("write stale compile-hash mutation"),
		FFileHelper::SaveStringToFile(
			StaleSourceJson,
			*StaleSourcePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	FString MutatedAuditPath;
	BlueprintLensAsyncCompilerAudit::FAsyncCompilerAuditStats MutatedAuditStats;
	FString MutationError;
	TestFalse(
		TEXT("compiler audit rejects stale compile hash"),
		BlueprintLensAsyncCompilerAudit::AuditAsyncCompilerLinkage(
			*Blueprint,
			StaleSourcePath,
			MutatedAuditPath,
			MutatedAuditStats,
			MutationError));

	const TSharedPtr<FJsonObject>* Barrier = nullptr;
	if (TestTrue(TEXT("barrier declaration present"), Root->TryGetObjectField(TEXT("barrier"), Barrier))
		&& TestNotNull(TEXT("barrier declaration object"), Barrier == nullptr ? nullptr : Barrier->Get()))
	{
		TestFalse(TEXT("barrier source site identity"), (*Barrier)->GetStringField(TEXT("barrier_site_id")).IsEmpty());
		TestTrue(TEXT("barrier single fire guarantee"), (*Barrier)->GetBoolField(TEXT("single_fire_guarantee")));
		TestEqual(TEXT("barrier reset policy"), (*Barrier)->GetStringField(TEXT("reset_policy")), TEXT("explicit_only"));
		TestEqual(TEXT("barrier cancel policy"), (*Barrier)->GetStringField(TEXT("cancel_policy")), TEXT("closes_invocation"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Boundaries = nullptr;
	if (TestTrue(TEXT("source boundaries present"), Root->TryGetArrayField(TEXT("boundaries"), Boundaries))
		&& Boundaries != nullptr)
	{
		TSet<FString> BoundaryKinds;
		for (const TSharedPtr<FJsonValue>& Boundary : *Boundaries)
		{
			BoundaryKinds.Add(Boundary->AsObject()->GetStringField(TEXT("boundary_kind")));
		}
		TestTrue(TEXT("scheduler boundary explicit"), BoundaryKinds.Contains(TEXT("scheduler")));
		TestTrue(TEXT("world tick boundary explicit"), BoundaryKinds.Contains(TEXT("world_tick")));
		TestTrue(TEXT("external service boundary explicit"), BoundaryKinds.Contains(TEXT("external_service")));
		TestTrue(TEXT("cancellation boundary explicit"), BoundaryKinds.Contains(TEXT("cancellation")));
	}

	if (FixtureDelays.Num() == 2)
	{
		const int32 FirstY = FixtureDelays[0]->NodePosY;
		const int32 SecondY = FixtureDelays[1]->NodePosY;
		FixtureDelays[0]->NodePosY = SecondY;
		FixtureDelays[1]->NodePosY = FirstY;
		FString ReorderedPath;
		BlueprintLensAsyncFacts::FAsyncFactStats ReorderedStats;
		TestTrue(
			TEXT("source export survives presentation-position swap"),
			BlueprintLensAsyncFacts::ExportAsyncFacts(
				*Blueprint,
				SequenceNodeId,
				CriterionNodeId,
				ReorderedPath,
				ReorderedStats,
				Error));
		FString ReorderedJson;
		TSharedPtr<FJsonObject> ReorderedRoot;
		if (TestTrue(TEXT("reordered source JSON readable"), FFileHelper::LoadFileToString(ReorderedJson, *ReorderedPath))
			&& TestTrue(
				TEXT("reordered source JSON parses"),
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReorderedJson), ReorderedRoot))
			&& TestNotNull(TEXT("reordered source root"), ReorderedRoot.Get()))
		{
			const TArray<TSharedPtr<FJsonValue>>& ReorderedContinuations =
				ReorderedRoot->GetArrayField(TEXT("continuations"));
			TestTrue(
				TEXT("A binding ignores node Y"),
				FMath::IsNearlyEqual(
					FCString::Atof(*ReorderedContinuations[0]->AsObject()->GetStringField(TEXT("duration"))),
					0.1f));
			TestTrue(
				TEXT("B binding ignores node Y"),
				FMath::IsNearlyEqual(
					FCString::Atof(*ReorderedContinuations[1]->AsObject()->GetStringField(TEXT("duration"))),
					0.2f));
		}
		FixtureDelays[1]->NodePosY = SecondY;
		FixtureDelays[0]->NodePosY = FirstY;
	}
	if (CriterionSet != nullptr && CriterionRecord != nullptr)
	{
		UEdGraphPin* SetThen = CriterionSet->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* RecordExecute = CriterionRecord->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (SetThen != nullptr && RecordExecute != nullptr)
		{
			SetThen->BreakLinkTo(RecordExecute);
			FString BrokenPath;
			BlueprintLensAsyncFacts::FAsyncFactStats BrokenStats;
			FString BrokenError;
			TestFalse(
				TEXT("source producer rejects disconnected criterion Record"),
				BlueprintLensAsyncFacts::ExportAsyncFacts(
					*Blueprint,
					SequenceNodeId,
					CriterionNodeId,
					BrokenPath,
					BrokenStats,
					BrokenError));
			SetThen->MakeLinkTo(RecordExecute);
			FString RestoredPath;
			BlueprintLensAsyncFacts::FAsyncFactStats RestoredStats;
			TestTrue(
				TEXT("source producer succeeds after criterion mutation restore"),
				BlueprintLensAsyncFacts::ExportAsyncFacts(
					*Blueprint,
					SequenceNodeId,
					CriterionNodeId,
					RestoredPath,
					RestoredStats,
					Error));
		}
	}
	UK2Node_ExecutionSequence* FixtureSequence = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (UK2Node_ExecutionSequence* Candidate = Cast<UK2Node_ExecutionSequence>(Node))
		{
			FixtureSequence = Candidate;
			break;
		}
	}
	if (FixtureSequence != nullptr)
	{
		FixtureSequence->AddInputPin();
		FString ExtraOutputPath;
		BlueprintLensAsyncFacts::FAsyncFactStats ExtraOutputStats;
		FString ExtraOutputError;
		TestFalse(
			TEXT("source producer rejects an undeclared third Sequence output"),
			BlueprintLensAsyncFacts::ExportAsyncFacts(
				*Blueprint,
				SequenceNodeId,
				CriterionNodeId,
				ExtraOutputPath,
				ExtraOutputStats,
				ExtraOutputError));
		FixtureSequence->RemovePin(FixtureSequence->GetThenPinGivenIndex(2));
	}
	if (CriterionSet != nullptr)
	{
		UEdGraphPin* CriterionValue = CriterionSet->FindPin(TEXT("LC4AsyncComplete"), EGPD_Input);
		if (TestNotNull(TEXT("canonical criterion value pin"), CriterionValue))
		{
			const FString OriginalCriterionValue = CriterionValue->DefaultValue;
			CriterionValue->DefaultValue = TEXT("false");
			FString FalseCriterionPath;
			BlueprintLensAsyncFacts::FAsyncFactStats FalseCriterionStats;
			FString FalseCriterionError;
			TestFalse(
				TEXT("source producer rejects Set LC4AsyncComplete false"),
				BlueprintLensAsyncFacts::ExportAsyncFacts(
					*Blueprint,
					SequenceNodeId,
					CriterionNodeId,
					FalseCriterionPath,
					FalseCriterionStats,
					FalseCriterionError));
			CriterionValue->DefaultValue = OriginalCriterionValue;
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4AsyncRuntimeTraceTest,
	"BlueprintLens.Exporter.LC4AsyncRuntimeTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4AsyncRuntimeTraceTest::RunTest(const FString&)
{
	FString AssetObjectPath;
	FString SequenceNodeId;
	FString CriterionNodeId;
	FString Error;
	if (!TestTrue(
		TEXT("ensure fixture for A-first trace"),
		BlueprintLensLC4AsyncFixture::EnsureFixture(
			AssetObjectPath,
			SequenceNodeId,
			CriterionNodeId,
			Error)))
	{
		AddError(Error);
		return false;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		*AssetObjectPath);
	if (!TestNotNull(TEXT("async fixture loads for runtime trace"), Blueprint)
		|| !TestNotNull(TEXT("async fixture generated class"), Blueprint->GeneratedClass.Get()))
	{
		return false;
	}
	FString SourcePath;
	BlueprintLensAsyncFacts::FAsyncFactStats SourceStats;
	FBlueprintLensLC4AsyncTraceBindings Bindings;
	if (!TestTrue(
		TEXT("export source facts for A-first trace"),
		BlueprintLensAsyncFacts::ExportAsyncFacts(
			*Blueprint,
			SequenceNodeId,
			CriterionNodeId,
			SourcePath,
			SourceStats,
			Error))
		|| !TestTrue(TEXT("load A-first source bindings"), LoadTraceBindings(SourcePath, Bindings, Error))
		|| !TestTrue(
			TEXT("configure A-first trace bindings"),
			UBlueprintLensAsyncBarrierLibrary::ConfigureLC4AsyncTrace(
				TEXT("A_FIRST"),
				FString::Printf(TEXT("A-first-%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)),
				Bindings,
				Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("A-first launch source binding count"), Bindings.LaunchIds.Num(), 2);
	TestEqual(TEXT("A-first completion source binding count"), Bindings.CompletionIds.Num(), 2);
	TestEqual(TEXT("A-first arrival source binding count"), Bindings.ArrivalIds.Num(), 2);

	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("create transient game world"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Actor = World == nullptr
		? nullptr
		: World->SpawnActor<AActor>(Blueprint->GeneratedClass, FTransform::Identity);
	if (!TestNotNull(TEXT("spawn async fixture actor"), Actor)
		|| !TestTrue(TEXT("begin play in transient world"), WorldWrapper.BeginPlayInTestWorld()))
	{
		WorldWrapper.ForwardErrorMessages(this);
		WorldWrapper.DestroyTestWorld(true);
		return false;
	}
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		if (!TestTrue(TEXT("tick transient world"), WorldWrapper.TickTestWorld(0.05f)))
		{
			break;
		}
	}

	FString TracePath;
	const bool bSaved = UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(Actor, TracePath, Error);
	TestTrue(TEXT("save complete runtime trace"), bSaved);
	if (!bSaved)
	{
		AddError(Error);
	}
	WorldWrapper.EndPlayInTestWorld();
	WorldWrapper.DestroyTestWorld(true);
	if (!bSaved)
	{
		return false;
	}

	FString JsonText;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("runtime trace JSON readable"), FFileHelper::LoadFileToString(JsonText, *TracePath))
		|| !TestTrue(
			TEXT("runtime trace JSON parses"),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root))
		|| !TestNotNull(TEXT("runtime trace JSON root"), Root.Get()))
	{
		return false;
	}
	TestEqual(TEXT("runtime trace close reason"), Root->GetStringField(TEXT("close_reason")), TEXT("complete"));
	TestEqual(TEXT("runtime trace dropped count"), static_cast<int32>(Root->GetNumberField(TEXT("dropped_event_count"))), 0);
	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (!TestTrue(TEXT("runtime trace events present"), Root->TryGetArrayField(TEXT("events"), Events))
		|| Events == nullptr)
	{
		return false;
	}
	TMap<FString, int32> KindCounts;
	for (const TSharedPtr<FJsonValue>& Event : *Events)
	{
		const TSharedPtr<FJsonObject> EventObject = Event->AsObject();
		const FString EventKind = EventObject->GetStringField(TEXT("event_kind"));
		const FString ParticipantId = EventKind == TEXT("trace_boundary")
			? EventObject->GetStringField(TEXT("boundary_phase"))
			: EventObject->GetStringField(TEXT("participant_id")).IsEmpty()
			? EventObject->GetStringField(TEXT("continuation_id"))
			: EventObject->GetStringField(TEXT("participant_id"));
		++KindCounts.FindOrAdd(EventKind);
		TestEqual(
			TEXT("A-first trace source occurrence matches source product"),
			EventObject->GetStringField(TEXT("source_occurrence_id")),
			ExpectedSourceOccurrence(Bindings, EventKind, ParticipantId));
	}
	TestEqual(TEXT("two launches"), KindCounts.FindRef(TEXT("launch")), 2);
	TestEqual(TEXT("two completions"), KindCounts.FindRef(TEXT("completion")), 2);
	TestEqual(TEXT("two arrivals"), KindCounts.FindRef(TEXT("barrier_arrival")), 2);
	TestEqual(TEXT("one release"), KindCounts.FindRef(TEXT("barrier_release")), 1);
	TestEqual(TEXT("one criterion"), KindCounts.FindRef(TEXT("criterion")), 1);
	TestEqual(TEXT("two trace boundaries"), KindCounts.FindRef(TEXT("trace_boundary")), 2);
	UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncTraceConfiguration();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLensLC4AsyncRuntimeTraceBFirstTest,
	"BlueprintLens.Exporter.LC4AsyncRuntimeTraceBFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLensLC4AsyncRuntimeTraceBFirstTest::RunTest(const FString&)
{
	FString AssetObjectPath;
	FString SequenceNodeId;
	FString CriterionNodeId;
	FString Error;
	if (!TestTrue(
		TEXT("ensure fixture for B-first trace"),
		BlueprintLensLC4AsyncFixture::EnsureFixture(
			AssetObjectPath,
			SequenceNodeId,
			CriterionNodeId,
			Error)))
	{
		AddError(Error);
		return false;
	}
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
	if (!TestNotNull(TEXT("async fixture loads for B-first trace"), Blueprint)
		|| !TestNotNull(TEXT("async fixture generated class for B-first trace"), Blueprint->GeneratedClass.Get()))
	{
		return false;
	}

	FString SourcePath;
	BlueprintLensAsyncFacts::FAsyncFactStats SourceStats;
	FBlueprintLensLC4AsyncTraceBindings Bindings;
	if (!TestTrue(
		TEXT("export source facts for B-first trace"),
		BlueprintLensAsyncFacts::ExportAsyncFacts(
			*Blueprint,
			SequenceNodeId,
			CriterionNodeId,
			SourcePath,
			SourceStats,
			Error))
		|| !TestTrue(TEXT("load B-first source bindings"), LoadTraceBindings(SourcePath, Bindings, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("B-first launch source binding count"), Bindings.LaunchIds.Num(), 2);
	TestEqual(TEXT("B-first completion source binding count"), Bindings.CompletionIds.Num(), 2);
	TestEqual(TEXT("B-first arrival source binding count"), Bindings.ArrivalIds.Num(), 2);

	TArray<uint8> AssetBytesBefore;
	TArray<uint8> AssetBytesAfter;
	const FString AssetFilename = FPackageName::LongPackageNameToFilename(
		Blueprint->GetOutermost()->GetName(),
		FPackageName::GetAssetPackageExtension());
	if (!TestTrue(TEXT("read fixture bytes before B-first run"), FFileHelper::LoadFileToArray(AssetBytesBefore, *AssetFilename)))
	{
		return false;
	}

	TMap<FName, UK2Node_CallFunction*> DelayByParticipant;
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
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call == nullptr)
			{
				continue;
			}
			const FString NodeId = BlueprintLensSequenceFacts::MakeNodeId(Graph->GetPathName(), *Call);
			for (const TPair<FName, FString>& Completion : Bindings.CompletionIds)
			{
				if (Completion.Value == NodeId)
				{
					DelayByParticipant.Add(Completion.Key, Call);
				}
			}
		}
	}
	UK2Node_CallFunction* DelayA = DelayByParticipant.FindRef(TEXT("A"));
	UK2Node_CallFunction* DelayB = DelayByParticipant.FindRef(TEXT("B"));
	if (!TestNotNull(TEXT("resolve Delay A for B-first run"), DelayA)
		|| !TestNotNull(TEXT("resolve Delay B for B-first run"), DelayB))
	{
		return false;
	}
	UEdGraphPin* DurationA = DelayA->FindPin(TEXT("Duration"), EGPD_Input);
	UEdGraphPin* DurationB = DelayB->FindPin(TEXT("Duration"), EGPD_Input);
	if (!TestNotNull(TEXT("Delay A duration pin"), DurationA)
		|| !TestNotNull(TEXT("Delay B duration pin"), DurationB))
	{
		return false;
	}
	const FString OriginalDurationA = DurationA->DefaultValue;
	const FString OriginalDurationB = DurationB->DefaultValue;
	TestTrue(
		TEXT("apply formal B-first fixture schedule"),
		BlueprintLensLC4AsyncFixture::ApplyScheduleVariant(*Blueprint, TEXT("B_FIRST"), Error));
	FString BFirstSourcePath;
	BlueprintLensAsyncFacts::FAsyncFactStats BFirstSourceStats;
	FString BFirstAuditPath;
	BlueprintLensAsyncCompilerAudit::FAsyncCompilerAuditStats BFirstAuditStats;
	TestTrue(
		TEXT("B-first source variant export succeeds"),
		BlueprintLensAsyncFacts::ExportAsyncFacts(
			*Blueprint,
			SequenceNodeId,
			CriterionNodeId,
			BFirstSourcePath,
			BFirstSourceStats,
			Error));
	TestTrue(
		TEXT("B-first compiler audit succeeds"),
		BlueprintLensAsyncCompilerAudit::AuditAsyncCompilerLinkage(
			*Blueprint,
			BFirstSourcePath,
			BFirstAuditPath,
			BFirstAuditStats,
			Error));
	FString BFirstSourceText;
	TSharedPtr<FJsonObject> BFirstSourceRoot;
	if (TestTrue(TEXT("B-first source variant readable"), FFileHelper::LoadFileToString(BFirstSourceText, *BFirstSourcePath))
		&& TestTrue(
			TEXT("B-first source variant parses"),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BFirstSourceText), BFirstSourceRoot))
		&& TestNotNull(TEXT("B-first source variant root"), BFirstSourceRoot.Get()))
	{
		const TSharedPtr<FJsonObject> BFirstProvenance = BFirstSourceRoot->GetObjectField(TEXT("provenance"));
		const TSharedPtr<FJsonObject> BFirstOverlay = BFirstProvenance->GetObjectField(TEXT("schedule_overlay"));
		TestEqual(TEXT("B-first overlay names active variant"), BFirstOverlay->GetStringField(TEXT("schedule_variant")), TEXT("B_FIRST"));
		TestTrue(TEXT("B-first overlay hash is SHA-256"), IsLowerHexSha256(BFirstOverlay->GetStringField(TEXT("overlay_sha256"))));
		TestTrue(TEXT("B-first active state hash is SHA-256"), IsLowerHexSha256(BFirstProvenance->GetStringField(TEXT("active_state_sha256"))));
	}
	const FString BFirstSourceVariantPath = FPaths::Combine(
		FPaths::GetPath(BFirstSourcePath),
		TEXT("B_FIRST.async-source.json"));
	const FString BFirstAuditVariantPath = FPaths::Combine(
		FPaths::GetPath(BFirstAuditPath),
		TEXT("B_FIRST.async-compiler-linkage.tsv"));
	TestTrue(
		TEXT("retain B-first source variant product"),
		IFileManager::Get().Copy(*BFirstSourceVariantPath, *BFirstSourcePath, true, true) == COPY_OK);
	TestTrue(
		TEXT("retain B-first audit variant product"),
		IFileManager::Get().Copy(*BFirstAuditVariantPath, *BFirstAuditPath, true, true) == COPY_OK);
	if (!TestTrue(
		TEXT("configure B-first trace bindings"),
		UBlueprintLensAsyncBarrierLibrary::ConfigureLC4AsyncTrace(
			TEXT("B_FIRST"),
			FString::Printf(TEXT("B-first-%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)),
			Bindings,
			Error)))
	{
		AddError(Error);
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create B-first transient game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Actor = World == nullptr
		? nullptr
		: World->SpawnActor<AActor>(Blueprint->GeneratedClass, FTransform::Identity);
	if (TestNotNull(TEXT("spawn B-first async fixture actor"), Actor)
		&& TestTrue(TEXT("begin B-first play"), WorldWrapper.BeginPlayInTestWorld()))
	{
		for (int32 Tick = 0; Tick < 8; ++Tick)
		{
			TestTrue(TEXT("tick B-first transient world"), WorldWrapper.TickTestWorld(0.05f));
		}
	}
	FString TracePath;
	const bool bSaved = UBlueprintLensAsyncBarrierLibrary::SaveLC4AsyncTrace(Actor, TracePath, Error);
	TestTrue(TEXT("save complete B-first runtime trace"), bSaved);
	if (!bSaved)
	{
		AddError(Error);
	}
	WorldWrapper.EndPlayInTestWorld();
	WorldWrapper.DestroyTestWorld(true);
	UBlueprintLensAsyncBarrierLibrary::ResetLC4AsyncTraceConfiguration();

	TestTrue(
		TEXT("restore formal A-first fixture schedule"),
		BlueprintLensLC4AsyncFixture::ApplyScheduleVariant(*Blueprint, TEXT("A_FIRST"), Error));
	FString RestoredSourcePath;
	BlueprintLensAsyncFacts::FAsyncFactStats RestoredSourceStats;
	FString RestoredAuditPath;
	BlueprintLensAsyncCompilerAudit::FAsyncCompilerAuditStats RestoredAuditStats;
	TestTrue(
		TEXT("restored A-first source export succeeds"),
		BlueprintLensAsyncFacts::ExportAsyncFacts(
			*Blueprint,
			SequenceNodeId,
			CriterionNodeId,
			RestoredSourcePath,
			RestoredSourceStats,
			Error));
	TestTrue(
		TEXT("restored A-first compiler audit succeeds"),
		BlueprintLensAsyncCompilerAudit::AuditAsyncCompilerLinkage(
			*Blueprint,
			RestoredSourcePath,
			RestoredAuditPath,
			RestoredAuditStats,
			Error));
	TestTrue(TEXT("read fixture bytes after B-first run"), FFileHelper::LoadFileToArray(AssetBytesAfter, *AssetFilename));
	TestTrue(TEXT("B-first harness does not rewrite fixture asset"), AssetBytesAfter == AssetBytesBefore);
	FString FreshAssetObjectPath;
	FString FreshSequenceNodeId;
	FString FreshCriterionNodeId;
	FString FreshError;
	TestTrue(
		TEXT("post-restore fixture inspection remains canonical"),
		BlueprintLensLC4AsyncFixture::EnsureFixture(
			FreshAssetObjectPath,
			FreshSequenceNodeId,
			FreshCriterionNodeId,
			FreshError));
	TestEqual(TEXT("post-restore fixture sequence identity stable"), FreshSequenceNodeId, SequenceNodeId);
	TestEqual(TEXT("post-restore fixture criterion identity stable"), FreshCriterionNodeId, CriterionNodeId);
	if (!bSaved)
	{
		return false;
	}

	FString TraceText;
	TSharedPtr<FJsonObject> TraceRoot;
	if (!TestTrue(TEXT("B-first runtime trace readable"), FFileHelper::LoadFileToString(TraceText, *TracePath))
		|| !TestTrue(
			TEXT("B-first runtime trace parses"),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TraceText), TraceRoot))
		|| !TestNotNull(TEXT("B-first runtime trace root"), TraceRoot.Get()))
	{
		return false;
	}
	TestEqual(TEXT("B-first schedule header"), TraceRoot->GetStringField(TEXT("schedule_variant")), TEXT("B_FIRST"));
	const TArray<TSharedPtr<FJsonValue>>& Events = TraceRoot->GetArrayField(TEXT("events"));
	TArray<FString> LaunchParticipants;
	TArray<FString> CompletionParticipants;
	TMap<FString, int32> KindCounts;
	for (const TSharedPtr<FJsonValue>& EventValue : Events)
	{
		const TSharedPtr<FJsonObject> Event = EventValue->AsObject();
		const FString EventKind = Event->GetStringField(TEXT("event_kind"));
		const FString ParticipantId = EventKind == TEXT("trace_boundary")
			? Event->GetStringField(TEXT("boundary_phase"))
			: Event->GetStringField(TEXT("participant_id")).IsEmpty()
			? Event->GetStringField(TEXT("continuation_id"))
			: Event->GetStringField(TEXT("participant_id"));
		++KindCounts.FindOrAdd(EventKind);
		TestEqual(
			TEXT("B-first trace source occurrence matches source product"),
			Event->GetStringField(TEXT("source_occurrence_id")),
			ExpectedSourceOccurrence(Bindings, EventKind, ParticipantId));
		if (EventKind == TEXT("launch"))
		{
			LaunchParticipants.Add(Event->GetStringField(TEXT("continuation_id")));
		}
		if (EventKind == TEXT("completion"))
		{
			CompletionParticipants.Add(Event->GetStringField(TEXT("continuation_id")));
		}
	}
	TestEqual(TEXT("B-first launch count"), LaunchParticipants.Num(), 2);
	TestEqual(TEXT("B-first completion count"), CompletionParticipants.Num(), 2);
	TestEqual(TEXT("B-first arrival count"), KindCounts.FindRef(TEXT("barrier_arrival")), 2);
	TestEqual(TEXT("B-first release count"), KindCounts.FindRef(TEXT("barrier_release")), 1);
	TestEqual(TEXT("B-first criterion count"), KindCounts.FindRef(TEXT("criterion")), 1);
	TestEqual(TEXT("B-first trace boundary count"), KindCounts.FindRef(TEXT("trace_boundary")), 2);
	if (LaunchParticipants.Num() == 2)
	{
		TestEqual(TEXT("B-first still launches A first"), LaunchParticipants[0], TEXT("A"));
		TestEqual(TEXT("B-first still launches B second"), LaunchParticipants[1], TEXT("B"));
	}
	if (CompletionParticipants.Num() == 2)
	{
		TestEqual(TEXT("B-first completes B first"), CompletionParticipants[0], TEXT("B"));
		TestEqual(TEXT("B-first completes A second"), CompletionParticipants[1], TEXT("A"));
	}
	return !HasAnyErrors();
}

#endif
