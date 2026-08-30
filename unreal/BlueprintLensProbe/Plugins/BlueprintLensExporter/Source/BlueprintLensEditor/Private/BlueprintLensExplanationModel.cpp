#include "BlueprintLensExplanationModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "IPlatformCrypto.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

namespace
{
constexpr TCHAR GroupPartialOrderSemantics[] =
	TEXT("no execution order is proven between these groups");

static FBlueprintLensLoadResult Failure(const FString& Message)
{
	FBlueprintLensLoadResult Result;
	Result.Error = Message;
	return Result;
}

bool Fail(FString& Error, const FString& Message)
{
	Error = Message;
	return false;
}

bool ValidateFields(
	const FJsonObject& Object,
	const std::initializer_list<const TCHAR*> ExpectedFields,
	const FString& Context,
	FString& Error)
{
	if (Object.Values.Num() != static_cast<int32>(ExpectedFields.size()))
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s must contain exactly %d fields"),
				*Context,
				static_cast<int32>(ExpectedFields.size())));
	}

	for (const TCHAR* Expected : ExpectedFields)
	{
		if (!Object.Values.Contains(Expected))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s is missing required field '%s'"),
					*Context,
					Expected));
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
	{
		bool bKnown = false;
		for (const TCHAR* Expected : ExpectedFields)
		{
			if (Field.Key == Expected)
			{
				bKnown = true;
				break;
			}
		}
		if (!bKnown)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s contains unknown field '%s'"),
					*Context,
					*Field.Key));
		}
	}
	return true;
}

// Same strictness as ValidateFields, but scales to several optional fields
// without needing one exact-set branch per present/absent combination.
bool ValidateFieldsWithOptional(
	const FJsonObject& Object,
	const std::initializer_list<const TCHAR*> RequiredFields,
	const std::initializer_list<const TCHAR*> OptionalFields,
	const FString& Context,
	FString& Error)
{
	for (const TCHAR* Expected : RequiredFields)
	{
		if (!Object.Values.Contains(Expected))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s is missing required field '%s'"),
					*Context,
					Expected));
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
	{
		bool bKnown = false;
		for (const TCHAR* Expected : RequiredFields)
		{
			if (Field.Key == Expected)
			{
				bKnown = true;
				break;
			}
		}
		if (!bKnown)
		{
			for (const TCHAR* Expected : OptionalFields)
			{
				if (Field.Key == Expected)
				{
					bKnown = true;
					break;
				}
			}
		}
		if (!bKnown)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s contains unknown field '%s'"),
					*Context,
					*Field.Key));
		}
	}
	return true;
}

bool ReadString(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	FString& OutValue,
	FString& Error,
	const bool bAllowEmpty = false)
{
	const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
	if (Value == nullptr || !Value->IsValid() ||
		(*Value)->Type != EJson::String)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be a string"), *Context, FieldName));
	}

	OutValue = (*Value)->AsString();
	if (!bAllowEmpty && OutValue.IsEmpty())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must not be empty"), *Context, FieldName));
	}
	return true;
}

bool ReadBool(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	bool& OutValue,
	FString& Error)
{
	const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
	if (Value == nullptr || !Value->IsValid() ||
		(*Value)->Type != EJson::Boolean)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be a boolean"), *Context, FieldName));
	}
	OutValue = (*Value)->AsBool();
	return true;
}

bool ReadCount(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	int32& OutValue,
	FString& Error)
{
	const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
	if (Value == nullptr || !Value->IsValid() ||
		(*Value)->Type != EJson::Number)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be an integer"), *Context, FieldName));
	}

	const double NumericValue = (*Value)->AsNumber();
	if (!FMath::IsFinite(NumericValue) || NumericValue < 0.0 ||
		NumericValue > static_cast<double>(MAX_int32))
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be a non-negative int32"),
				*Context,
				FieldName));
	}

	OutValue = static_cast<int32>(NumericValue);
	if (static_cast<double>(OutValue) != NumericValue)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be an integer"), *Context, FieldName));
	}
	return true;
}

bool ReadObject(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	TSharedPtr<FJsonObject>& OutValue,
	FString& Error)
{
	const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
	if (Value == nullptr || !Value->IsValid() ||
		(*Value)->Type != EJson::Object)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be an object"), *Context, FieldName));
	}
	OutValue = (*Value)->AsObject();
	if (!OutValue.IsValid())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be an object"), *Context, FieldName));
	}
	return true;
}

bool ReadArray(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	const TArray<TSharedPtr<FJsonValue>>*& OutValue,
	FString& Error)
{
	const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
	if (Value == nullptr || !Value->IsValid() ||
		(*Value)->Type != EJson::Array)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must be an array"), *Context, FieldName));
	}
	OutValue = &(*Value)->AsArray();
	return true;
}

bool ReadStringArray(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const FString& Context,
	TArray<FString>& OutValues,
	FString& Error,
	const bool bRequireNonEmpty)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!ReadArray(Object, FieldName, Context, Values, Error))
	{
		return false;
	}
	if (bRequireNonEmpty && Values->IsEmpty())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.%s must contain at least one item"),
				*Context,
				FieldName));
	}

	OutValues.Reserve(Values->Num());
	for (int32 Index = 0; Index < Values->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& Value = (*Values)[Index];
		if (!Value.IsValid() || Value->Type != EJson::String ||
			Value->AsString().IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s.%s[%d] must be a non-empty string"),
					*Context,
					FieldName,
					Index));
		}
		OutValues.Add(Value->AsString());
	}
	return true;
}

bool ReadArrayObject(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Context,
	TSharedPtr<FJsonObject>& OutObject,
	FString& Error)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return Fail(
			Error,
			FString::Printf(TEXT("%s must be an object"), *Context));
	}
	OutObject = Value->AsObject();
	return OutObject.IsValid() ||
		Fail(Error, FString::Printf(TEXT("%s must be an object"), *Context));
}

bool IsUppercaseSha256(const FString& Value)
{
	if (Value.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (!((Character >= TEXT('0') && Character <= TEXT('9')) ||
			  (Character >= TEXT('A') && Character <= TEXT('F'))))
		{
			return false;
		}
	}
	return true;
}

bool ParseRole(
	const FString& Value,
	EBlueprintLensRole& OutValue,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("criterion"))
	{
		OutValue = EBlueprintLensRole::Criterion;
	}
	else if (Value == TEXT("control"))
	{
		OutValue = EBlueprintLensRole::Control;
	}
	else if (Value == TEXT("predicate"))
	{
		OutValue = EBlueprintLensRole::Predicate;
	}
	else if (Value == TEXT("value"))
	{
		OutValue = EBlueprintLensRole::Value;
	}
	else if (Value == TEXT("consequence"))
	{
		OutValue = EBlueprintLensRole::Consequence;
	}
	else if (Value == TEXT("boundary"))
	{
		OutValue = EBlueprintLensRole::Boundary;
	}
	else
	{
		return Fail(
			Error,
			FString::Printf(TEXT("%s has unknown role '%s'"), *Context, *Value));
	}
	return true;
}

bool ParseLaneState(
	const FString& Value,
	EBlueprintLensLaneState& OutValue,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("populated"))
	{
		OutValue = EBlueprintLensLaneState::Populated;
	}
	else if (Value == TEXT("not_enabled"))
	{
		OutValue = EBlueprintLensLaneState::NotEnabled;
	}
	else if (Value == TEXT("empty"))
	{
		OutValue = EBlueprintLensLaneState::Empty;
	}
	else
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s has unknown lane state '%s'"), *Context, *Value));
	}
	return true;
}

bool ParseUnitKind(
	const FString& Value,
	EBlueprintLensUnitKind& OutValue,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("node"))
	{
		OutValue = EBlueprintLensUnitKind::Node;
	}
	else if (Value == TEXT("expression"))
	{
		OutValue = EBlueprintLensUnitKind::Expression;
	}
	else if (Value == TEXT("summary"))
	{
		OutValue = EBlueprintLensUnitKind::Summary;
	}
	else
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s has unknown unit kind '%s'"), *Context, *Value));
	}
	return true;
}

bool ParseSemanticStatus(
	const FString& Value,
	EBlueprintLensSemanticStatus& OutValue,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("supported"))
	{
		OutValue = EBlueprintLensSemanticStatus::Supported;
	}
	else if (Value == TEXT("opaque"))
	{
		OutValue = EBlueprintLensSemanticStatus::Opaque;
	}
	else if (Value == TEXT("uncertain"))
	{
		OutValue = EBlueprintLensSemanticStatus::Uncertain;
	}
	else if (Value == TEXT("unsupported"))
	{
		OutValue = EBlueprintLensSemanticStatus::Unsupported;
	}
	else
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s has unknown semantic status '%s'"),
				*Context,
				*Value));
	}
	return true;
}

bool ParseRelationKind(
	const FString& Value,
	EBlueprintLensRelationKind& OutValue,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("execution_predecessor"))
	{
		OutValue = EBlueprintLensRelationKind::ExecutionPredecessor;
	}
	else if (Value == TEXT("controls_execution"))
	{
		OutValue = EBlueprintLensRelationKind::ControlsExecution;
	}
	else if (Value == TEXT("predicate_for"))
	{
		OutValue = EBlueprintLensRelationKind::PredicateFor;
	}
	else if (Value == TEXT("provides_value"))
	{
		OutValue = EBlueprintLensRelationKind::ProvidesValue;
	}
	else
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s has unknown relation kind '%s'"),
				*Context,
				*Value));
	}
	return true;
}

bool ParseSemanticLabel(
	const FString& Value,
	EBlueprintLensSemanticLabel& OutLabel,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("condition_true"))
	{
		OutLabel = EBlueprintLensSemanticLabel::ConditionTrue;
		return true;
	}
	if (Value == TEXT("condition_false"))
	{
		OutLabel = EBlueprintLensSemanticLabel::ConditionFalse;
		return true;
	}
	if (Value == TEXT("next_execution"))
	{
		OutLabel = EBlueprintLensSemanticLabel::NextExecution;
		return true;
	}
	if (Value == TEXT("branch_condition"))
	{
		OutLabel = EBlueprintLensSemanticLabel::BranchCondition;
		return true;
	}
	if (Value == TEXT("value_input"))
	{
		OutLabel = EBlueprintLensSemanticLabel::ValueInput;
		return true;
	}
	return Fail(
		Error,
		FString::Printf(
			TEXT("%s has unknown semantic label '%s'"), *Context, *Value));
}

bool ParseGroupKind(
	const FString& Value,
	EBlueprintLensGroupKind& OutKind,
	const FString& Context,
	FString& Error)
{
	static const TPair<const TCHAR*, EBlueprintLensGroupKind> Kinds[] = {
		{TEXT("outcome_path"), EBlueprintLensGroupKind::OutcomePath},
		{TEXT("guard_nest"), EBlueprintLensGroupKind::GuardNest},
		{TEXT("value_cone"), EBlueprintLensGroupKind::ValueCone},
		{TEXT("fanout_branch"), EBlueprintLensGroupKind::FanoutBranch},
		{TEXT("portal_scope"), EBlueprintLensGroupKind::PortalScope},
		{TEXT("frontier_group"), EBlueprintLensGroupKind::FrontierGroup},
		{TEXT("scc"), EBlueprintLensGroupKind::Scc}
	};
	for (const TPair<const TCHAR*, EBlueprintLensGroupKind>& Candidate : Kinds)
	{
		if (Value == Candidate.Key)
		{
			OutKind = Candidate.Value;
			return true;
		}
	}
	return Fail(
		Error,
		FString::Printf(
			TEXT("%s has unknown group kind '%s'"), *Context, *Value));
}

bool ParseProjectionStatus(
	const FString& Value,
	EBlueprintLensProjectionStatus& OutStatus,
	const FString& Context,
	FString& Error)
{
	if (Value == TEXT("COMPLETE"))
	{
		OutStatus = EBlueprintLensProjectionStatus::Complete;
		return true;
	}
	if (Value == TEXT("STRUCTURAL_ONLY"))
	{
		OutStatus = EBlueprintLensProjectionStatus::StructuralOnly;
		return true;
	}
	if (Value == TEXT("ABSTAINED"))
	{
		OutStatus = EBlueprintLensProjectionStatus::Abstained;
		return true;
	}
	return Fail(
		Error,
		FString::Printf(
			TEXT("%s has unknown projection status '%s'"), *Context, *Value));
}

bool ParseSource(
	const FJsonObject& Object,
	FBlueprintLensSource& OutSource,
	FString& Error)
{
	const FString Context(TEXT("root.source"));
	if (!ValidateFields(
			Object,
			{
				TEXT("ir_path"),
				TEXT("ir_sha256"),
				TEXT("slice_path"),
				TEXT("slice_sha256"),
				TEXT("blueprint_asset_path"),
				TEXT("blueprint_package_sha256"),
				TEXT("graph_id")
			},
			Context,
			Error) ||
		!ReadString(Object, TEXT("ir_path"), Context, OutSource.IrPath, Error) ||
		!ReadString(
			Object,
			TEXT("ir_sha256"),
			Context,
			OutSource.IrSha256,
			Error) ||
		!ReadString(
			Object,
			TEXT("slice_path"),
			Context,
			OutSource.SlicePath,
			Error) ||
		!ReadString(
			Object,
			TEXT("slice_sha256"),
			Context,
			OutSource.SliceSha256,
			Error) ||
		!ReadString(
			Object,
			TEXT("blueprint_asset_path"),
			Context,
			OutSource.BlueprintAssetPath,
			Error) ||
		!ReadString(
			Object,
			TEXT("blueprint_package_sha256"),
			Context,
			OutSource.BlueprintPackageSha256,
			Error) ||
		!ReadString(Object, TEXT("graph_id"), Context, OutSource.GraphId, Error))
	{
		return false;
	}

	if (!IsUppercaseSha256(OutSource.IrSha256) ||
		!IsUppercaseSha256(OutSource.SliceSha256) ||
		!IsUppercaseSha256(OutSource.BlueprintPackageSha256))
	{
		return Fail(
			Error,
			TEXT("root.source SHA-256 fields must contain 64 uppercase hexadecimal characters"));
	}
	return true;
}

bool ParseQuery(
	const FJsonObject& Object,
	FBlueprintLensQuery& OutQuery,
	FString& Error)
{
	const FString Context(TEXT("root.query"));
	if (!ValidateFields(
			Object,
			{
				TEXT("question"),
				TEXT("direction"),
				TEXT("criterion_source_node_id")
			},
			Context,
			Error) ||
		!ReadString(
			Object,
			TEXT("question"),
			Context,
			OutQuery.Question,
			Error) ||
		!ReadString(
			Object,
			TEXT("direction"),
			Context,
			OutQuery.Direction,
			Error) ||
		!ReadString(
			Object,
			TEXT("criterion_source_node_id"),
			Context,
			OutQuery.CriterionSourceNodeId,
			Error))
	{
		return false;
	}
	if (OutQuery.Direction != TEXT("backward_only"))
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("root.query.direction has unknown value '%s'"),
				*OutQuery.Direction));
	}
	return true;
}

bool ParseLane(
	const FJsonObject& Object,
	const int32 Index,
	FBlueprintLensLane& OutLane,
	FString& Error)
{
	const FString Context =
		FString::Printf(TEXT("root.lanes[%d]"), Index);
	FString Role;
	FString State;
	return ValidateFields(
			   Object,
			   {
				   TEXT("role"),
				   TEXT("state"),
				   TEXT("unit_ids"),
				   TEXT("empty_message")
			   },
			   Context,
			   Error) &&
		ReadString(Object, TEXT("role"), Context, Role, Error) &&
		ParseRole(Role, OutLane.Role, Context + TEXT(".role"), Error) &&
		ReadString(Object, TEXT("state"), Context, State, Error) &&
		ParseLaneState(State, OutLane.State, Context + TEXT(".state"), Error) &&
		ReadStringArray(
			Object,
			TEXT("unit_ids"),
			Context,
			OutLane.UnitIds,
			Error,
			false) &&
		ReadString(
			Object,
			TEXT("empty_message"),
			Context,
			OutLane.EmptyMessage,
			Error,
			true);
}

bool ParseSourceReference(
	const FJsonObject& Object,
	const FString& Context,
	FBlueprintLensSourceReference& OutReference,
	FString& Error)
{
	return ValidateFields(
			   Object,
			   {
				   TEXT("blueprint_asset_path"),
				   TEXT("graph_id"),
				   TEXT("source_node_id"),
				   TEXT("native_node_guid"),
				   TEXT("source_pin_ids"),
				   TEXT("primary")
			   },
			   Context,
			   Error) &&
		ReadString(
			Object,
			TEXT("blueprint_asset_path"),
			Context,
			OutReference.BlueprintAssetPath,
			Error) &&
		ReadString(
			Object,
			TEXT("graph_id"),
			Context,
			OutReference.GraphId,
			Error) &&
		ReadString(
			Object,
			TEXT("source_node_id"),
			Context,
			OutReference.SourceNodeId,
			Error) &&
		ReadString(
			Object,
			TEXT("native_node_guid"),
			Context,
			OutReference.NativeNodeGuid,
			Error) &&
		ReadStringArray(
			Object,
			TEXT("source_pin_ids"),
			Context,
			OutReference.SourcePinIds,
			Error,
			false) &&
		ReadBool(
			Object,
			TEXT("primary"),
			Context,
			OutReference.bPrimary,
			Error);
}

bool ParseUnit(
	const FJsonObject& Object,
	const int32 Index,
	FBlueprintLensUnit& OutUnit,
	FString& Error)
{
	const FString Context =
		FString::Printf(TEXT("root.units[%d]"), Index);
	if (!ValidateFieldsWithOptional(
			Object,
			{
				TEXT("id"),
				TEXT("role"),
				TEXT("kind"),
				TEXT("title"),
				TEXT("expression"),
				TEXT("semantic_status"),
				TEXT("inclusion_reasons"),
				TEXT("source_references")
			},
			{TEXT("disambiguator")},
			Context,
			Error) ||
		!ReadString(Object, TEXT("id"), Context, OutUnit.Id, Error))
	{
		return false;
	}

	FString Role;
	FString Kind;
	FString Status;
	if (!ReadString(Object, TEXT("role"), Context, Role, Error) ||
		!ParseRole(Role, OutUnit.Role, Context + TEXT(".role"), Error) ||
		!ReadString(Object, TEXT("kind"), Context, Kind, Error) ||
		!ParseUnitKind(Kind, OutUnit.Kind, Context + TEXT(".kind"), Error) ||
		!ReadString(Object, TEXT("title"), Context, OutUnit.Title, Error) ||
		!ReadString(
			Object,
			TEXT("expression"),
			Context,
			OutUnit.Expression,
			Error,
			true) ||
		!ReadString(
			Object,
			TEXT("semantic_status"),
			Context,
			Status,
			Error) ||
		!ParseSemanticStatus(
			Status,
			OutUnit.SemanticStatus,
			Context + TEXT(".semantic_status"),
			Error) ||
		!ReadStringArray(
			Object,
			TEXT("inclusion_reasons"),
			Context,
			OutUnit.InclusionReasons,
			Error,
			true))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* References = nullptr;
	if (!ReadArray(
			Object,
			TEXT("source_references"),
			Context,
			References,
			Error) ||
		References->IsEmpty())
	{
		return References != nullptr
			? Fail(
				  Error,
				  FString::Printf(
					  TEXT("%s.source_references must contain at least one item"),
					  *Context))
			: false;
	}

	OutUnit.SourceReferences.Reserve(References->Num());
	for (int32 ReferenceIndex = 0;
		 ReferenceIndex < References->Num();
		 ++ReferenceIndex)
	{
		const FString ReferenceContext = FString::Printf(
			TEXT("%s.source_references[%d]"),
			*Context,
			ReferenceIndex);
		TSharedPtr<FJsonObject> ReferenceObject;
		FBlueprintLensSourceReference Reference;
		if (!ReadArrayObject(
				(*References)[ReferenceIndex],
				ReferenceContext,
				ReferenceObject,
				Error) ||
			!ParseSourceReference(
				*ReferenceObject,
				ReferenceContext,
				Reference,
				Error))
		{
			return false;
		}
		OutUnit.SourceReferences.Add(MoveTemp(Reference));
	}

	OutUnit.bHasDisambiguator =
		Object.Values.Contains(TEXT("disambiguator"));
	if (OutUnit.bHasDisambiguator)
	{
		const FString DisambiguatorContext = Context + TEXT(".disambiguator");
		TSharedPtr<FJsonObject> DisambiguatorObject;
		if (!ReadObject(
				Object,
				TEXT("disambiguator"),
				Context,
				DisambiguatorObject,
				Error) ||
			!ValidateFields(
				*DisambiguatorObject,
				{
					TEXT("text"),
					TEXT("rule_id"),
					TEXT("evidence_relation_ids")
				},
				DisambiguatorContext,
				Error) ||
			!ReadString(
				*DisambiguatorObject,
				TEXT("text"),
				DisambiguatorContext,
				OutUnit.Disambiguator.Text,
				Error) ||
			!ReadString(
				*DisambiguatorObject,
				TEXT("rule_id"),
				DisambiguatorContext,
				OutUnit.Disambiguator.RuleId,
				Error) ||
			!ReadStringArray(
				*DisambiguatorObject,
				TEXT("evidence_relation_ids"),
				DisambiguatorContext,
				OutUnit.Disambiguator.EvidenceRelationIds,
				Error,
				true))
		{
			return false;
		}
		if (OutUnit.Disambiguator.RuleId !=
			TEXT("unit.branch.from_predicate_for"))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("unknown disambiguator rule: %s"),
					*OutUnit.Disambiguator.RuleId));
		}
	}
	return true;
}

bool ParseRelation(
	const FJsonObject& Object,
	const int32 Index,
	FBlueprintLensRelation& OutRelation,
	FString& Error)
{
	const FString Context =
		FString::Printf(TEXT("root.relations[%d]"), Index);
	const bool bHasSourceEdgeEndpoints =
		Object.Values.Contains(TEXT("source_edge_endpoints"));
	FString Kind;
	const bool bFieldsValid = ValidateFieldsWithOptional(
		Object,
		{
			TEXT("id"),
			TEXT("source_unit_id"),
			TEXT("target_unit_id"),
			TEXT("kind"),
			TEXT("label"),
			TEXT("source_edge_ids")
		},
		{
			TEXT("source_edge_endpoints"),
			TEXT("port_label"),
			TEXT("semantic_label")
		},
		Context,
		Error);
	if (!bFieldsValid
		|| !ReadString(Object, TEXT("id"), Context, OutRelation.Id, Error)
		|| !ReadString(
			Object,
			TEXT("source_unit_id"),
			Context,
			OutRelation.SourceUnitId,
			Error)
		|| !ReadString(
			Object,
			TEXT("target_unit_id"),
			Context,
			OutRelation.TargetUnitId,
			Error)
		|| !ReadString(Object, TEXT("kind"), Context, Kind, Error)
		|| !ParseRelationKind(
			Kind,
			OutRelation.Kind,
			Context + TEXT(".kind"),
			Error)
		|| !ReadString(
			Object, TEXT("label"), Context, OutRelation.Label, Error)
		|| !ReadStringArray(
			Object,
			TEXT("source_edge_ids"),
			Context,
			OutRelation.SourceEdgeIds,
			Error,
			true))
	{
		return false;
	}

	// Parsed before the endpoint block below so the optional labels are read
	// even when a relation carries no endpoint ledger; their cross-field rules
	// are enforced later in ValidateSemanticExtension.
	OutRelation.bHasPortLabel = Object.Values.Contains(TEXT("port_label"));
	if (OutRelation.bHasPortLabel &&
		!ReadString(
			Object,
			TEXT("port_label"),
			Context,
			OutRelation.PortLabel,
			Error,
			true))
	{
		return false;
	}
	OutRelation.bHasSemanticLabel =
		Object.Values.Contains(TEXT("semantic_label"));
	if (OutRelation.bHasSemanticLabel)
	{
		FString SemanticLabel;
		if (!ReadString(
				Object,
				TEXT("semantic_label"),
				Context,
				SemanticLabel,
				Error) ||
			!ParseSemanticLabel(
				SemanticLabel,
				OutRelation.SemanticLabel,
				Context + TEXT(".semantic_label"),
				Error))
		{
			return false;
		}
	}

	OutRelation.bHasSourceEdgeEndpoints = bHasSourceEdgeEndpoints;
	if (!bHasSourceEdgeEndpoints)
	{
		return true;
	}
	const TArray<TSharedPtr<FJsonValue>>* Endpoints = nullptr;
	if (!ReadArray(
			Object,
			TEXT("source_edge_endpoints"),
			Context,
			Endpoints,
			Error))
	{
		return false;
	}
	OutRelation.SourceEdgeEndpoints.Reserve(Endpoints->Num());
	for (int32 EndpointIndex = 0;
		 EndpointIndex < Endpoints->Num();
		 ++EndpointIndex)
	{
		const FString EndpointContext = FString::Printf(
			TEXT("%s.source_edge_endpoints[%d]"),
			*Context,
			EndpointIndex);
		TSharedPtr<FJsonObject> EndpointObject;
		FBlueprintLensSourceEdgeEndpoint Endpoint;
		if (!ReadArrayObject(
				(*Endpoints)[EndpointIndex],
				EndpointContext,
				EndpointObject,
				Error)
			|| !ValidateFields(
				*EndpointObject,
				{
					TEXT("source_edge_id"),
					TEXT("source_node_id"),
					TEXT("source_pin_id"),
					TEXT("source_port_label"),
					TEXT("target_node_id"),
					TEXT("target_pin_id"),
					TEXT("target_port_label")
				},
				EndpointContext,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("source_edge_id"),
				EndpointContext,
				Endpoint.SourceEdgeId,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("source_node_id"),
				EndpointContext,
				Endpoint.SourceNodeId,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("source_pin_id"),
				EndpointContext,
				Endpoint.SourcePinId,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("source_port_label"),
				EndpointContext,
				Endpoint.SourcePortLabel,
				Error,
				true)
			|| !ReadString(
				*EndpointObject,
				TEXT("target_node_id"),
				EndpointContext,
				Endpoint.TargetNodeId,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("target_pin_id"),
				EndpointContext,
				Endpoint.TargetPinId,
				Error)
			|| !ReadString(
				*EndpointObject,
				TEXT("target_port_label"),
				EndpointContext,
				Endpoint.TargetPortLabel,
				Error,
				true))
		{
			return false;
		}
		OutRelation.SourceEdgeEndpoints.Add(MoveTemp(Endpoint));
	}
	return true;
}

bool ParseGroup(
	const FJsonObject& Object,
	const int32 Index,
	FBlueprintLensGroup& OutGroup,
	FString& Error)
{
	const FString Context = FString::Printf(TEXT("root.groups[%d]"), Index);
	FString Kind;
	FString Status;
	if (!ValidateFieldsWithOptional(
			Object,
			{
				TEXT("id"),
				TEXT("kind"),
				TEXT("title"),
				TEXT("ordered_unit_ids"),
				TEXT("ordered_relation_ids"),
				TEXT("entry_unit_id"),
				TEXT("parent_group_id"),
				TEXT("entered_by"),
				TEXT("member_count"),
				TEXT("projection_status"),
				TEXT("diagnostic_code"),
				TEXT("claim_evidence")
			},
			{TEXT("exit_unit_id")},
			Context,
			Error) ||
		!ReadString(Object, TEXT("id"), Context, OutGroup.Id, Error) ||
		!ReadString(Object, TEXT("kind"), Context, Kind, Error) ||
		!ParseGroupKind(Kind, OutGroup.Kind, Context + TEXT(".kind"), Error) ||
		!ReadString(
			Object, TEXT("title"), Context, OutGroup.Title, Error, true) ||
		!ReadStringArray(
			Object,
			TEXT("ordered_unit_ids"),
			Context,
			OutGroup.OrderedUnitIds,
			Error,
			true) ||
		!ReadStringArray(
			Object,
			TEXT("ordered_relation_ids"),
			Context,
			OutGroup.OrderedRelationIds,
			Error,
			false) ||
		!ReadString(
			Object, TEXT("entry_unit_id"), Context, OutGroup.EntryUnitId, Error) ||
		!ReadCount(
			Object, TEXT("member_count"), Context, OutGroup.MemberCount, Error) ||
		!ReadString(
			Object, TEXT("projection_status"), Context, Status, Error) ||
		!ParseProjectionStatus(
			Status,
			OutGroup.ProjectionStatus,
			Context + TEXT(".projection_status"),
			Error) ||
		!ReadString(
			Object,
			TEXT("diagnostic_code"),
			Context,
			OutGroup.DiagnosticCode,
			Error,
			true))
	{
		return false;
	}

	OutGroup.bHasExitUnitId = Object.Values.Contains(TEXT("exit_unit_id"));
	if (OutGroup.Kind == EBlueprintLensGroupKind::GuardNest &&
		OutGroup.bHasExitUnitId)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("guard_nest must not declare exit_unit_id: %s"),
				*Context));
	}
	if ((OutGroup.Kind == EBlueprintLensGroupKind::OutcomePath ||
		 OutGroup.Kind == EBlueprintLensGroupKind::Scc) &&
		!OutGroup.bHasExitUnitId)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s requires exit_unit_id: %s"),
				*Kind,
				*Context));
	}
	if (OutGroup.bHasExitUnitId &&
		!ReadString(
			Object, TEXT("exit_unit_id"), Context, OutGroup.ExitUnitId, Error))
	{
		return false;
	}

	const TSharedPtr<FJsonValue>* ParentValue =
		Object.Values.Find(TEXT("parent_group_id"));
	if (ParentValue == nullptr || !ParentValue->IsValid())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.parent_group_id must be a string or null"), *Context));
	}
	if ((*ParentValue)->Type == EJson::String)
	{
		OutGroup.bHasParent = true;
		OutGroup.ParentGroupId = (*ParentValue)->AsString();
		if (OutGroup.ParentGroupId.IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s.parent_group_id must not be empty"), *Context));
		}
	}
	else if ((*ParentValue)->Type != EJson::Null)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.parent_group_id must be a string or null"), *Context));
	}

	const TSharedPtr<FJsonValue>* EnteredValue =
		Object.Values.Find(TEXT("entered_by"));
	if (EnteredValue == nullptr || !EnteredValue->IsValid())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.entered_by must be a semantic label or null"), *Context));
	}
	if ((*EnteredValue)->Type == EJson::String)
	{
		OutGroup.bHasEnteredBy = true;
		if (!ParseSemanticLabel(
				(*EnteredValue)->AsString(),
				OutGroup.EnteredBy,
				Context + TEXT(".entered_by"),
				Error))
		{
			return false;
		}
	}
	else if ((*EnteredValue)->Type != EJson::Null)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("%s.entered_by must be a semantic label or null"), *Context));
	}

	const TArray<TSharedPtr<FJsonValue>>* Evidence = nullptr;
	if (!ReadArray(Object, TEXT("claim_evidence"), Context, Evidence, Error))
	{
		return false;
	}
	OutGroup.ClaimEvidence.Reserve(Evidence->Num());
	for (int32 EvidenceIndex = 0; EvidenceIndex < Evidence->Num(); ++EvidenceIndex)
	{
		const FString EvidenceContext = FString::Printf(
			TEXT("%s.claim_evidence[%d]"), *Context, EvidenceIndex);
		TSharedPtr<FJsonObject> EvidenceObject;
		FBlueprintLensClaimEvidence Entry;
		if (!ReadArrayObject(
				(*Evidence)[EvidenceIndex], EvidenceContext, EvidenceObject, Error) ||
			!ValidateFields(
				*EvidenceObject,
				{TEXT("component"), TEXT("fact_owner"), TEXT("source")},
				EvidenceContext,
				Error) ||
			!ReadString(
				*EvidenceObject,
				TEXT("component"),
				EvidenceContext,
				Entry.Component,
				Error) ||
			!ReadString(
				*EvidenceObject,
				TEXT("fact_owner"),
				EvidenceContext,
				Entry.FactOwner,
				Error) ||
			!ReadString(
				*EvidenceObject, TEXT("source"), EvidenceContext, Entry.Source, Error))
		{
			return false;
		}
		OutGroup.ClaimEvidence.Add(MoveTemp(Entry));
	}
	return true;
}

bool ParseGroupPartialOrder(
	const FJsonObject& Object,
	FBlueprintLensGroupPartialOrder& OutOrder,
	FString& Error)
{
	const FString Context = TEXT("root.group_partial_order");
	const TArray<TSharedPtr<FJsonValue>>* Pairs = nullptr;
	if (!ValidateFields(
			Object,
			{TEXT("incomparable_group_ids"), TEXT("semantics")},
			Context,
			Error) ||
		!ReadString(
			Object, TEXT("semantics"), Context, OutOrder.Semantics, Error) ||
		!ReadArray(
			Object, TEXT("incomparable_group_ids"), Context, Pairs, Error))
	{
		return false;
	}
	OutOrder.IncomparableGroupIds.Reserve(Pairs->Num());
	TArray<TPair<FString, FString>> SeenPairs;
	for (int32 PairIndex = 0; PairIndex < Pairs->Num(); ++PairIndex)
	{
		const FString PairContext = FString::Printf(
			TEXT("%s.incomparable_group_ids[%d]"), *Context, PairIndex);
		const TSharedPtr<FJsonValue>& PairValue = (*Pairs)[PairIndex];
		if (!PairValue.IsValid() || PairValue->Type != EJson::Array)
		{
			return Fail(
				Error,
				FString::Printf(TEXT("%s must be an array"), *PairContext));
		}
		const TArray<TSharedPtr<FJsonValue>>& Pair = PairValue->AsArray();
		if (Pair.Num() != 2)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s must name exactly two groups"), *PairContext));
		}
		FString First;
		FString Second;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (!Pair[Side].IsValid() || Pair[Side]->Type != EJson::String ||
				Pair[Side]->AsString().IsEmpty())
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("%s[%d] must be a nonempty string"),
						*PairContext,
						Side));
			}
		}
		First = Pair[0]->AsString();
		Second = Pair[1]->AsString();
		const bool bFirstComesFirst = First.Compare(Second) <= 0;
		const FString CanonicalFirst = bFirstComesFirst ? First : Second;
		const FString CanonicalSecond = bFirstComesFirst ? Second : First;
		for (const TPair<FString, FString>& ExistingPair : SeenPairs)
		{
			if (ExistingPair.Key == CanonicalFirst &&
				ExistingPair.Value == CanonicalSecond)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("incomparable pair is duplicated: %s/%s"),
						*CanonicalFirst,
						*CanonicalSecond));
			}
		}
		SeenPairs.Add(TPair<FString, FString>(CanonicalFirst, CanonicalSecond));
		OutOrder.IncomparableGroupIds.Add(TPair<FString, FString>(First, Second));
	}
	return true;
}

bool ParseCounts(
	const FJsonObject& Object,
	FBlueprintLensCounts& OutCounts,
	FString& Error)
{
	const FString Context(TEXT("root.counts"));
	return ValidateFields(
			   Object,
			   {
				   TEXT("lanes"),
				   TEXT("units"),
				   TEXT("relations"),
				   TEXT("source_nodes"),
				   TEXT("source_edges")
			   },
			   Context,
			   Error) &&
		ReadCount(Object, TEXT("lanes"), Context, OutCounts.Lanes, Error) &&
		ReadCount(Object, TEXT("units"), Context, OutCounts.Units, Error) &&
		ReadCount(
			Object,
			TEXT("relations"),
			Context,
			OutCounts.Relations,
			Error) &&
		ReadCount(
			Object,
			TEXT("source_nodes"),
			Context,
			OutCounts.SourceNodes,
			Error) &&
		ReadCount(
			Object,
			TEXT("source_edges"),
			Context,
			OutCounts.SourceEdges,
			Error);
}

struct FRelationEndpointPinFact
{
	FString NodeId;
	FString Name;
};

struct FRelationEndpointEdgeFact
{
	FString Kind;
	FString SourceNodeId;
	FString SourcePinId;
	FString TargetNodeId;
	FString TargetPinId;
};

struct FRelationEndpointIrFacts
{
	TSet<FString> NodeIds;
	TMap<FString, FRelationEndpointPinFact> PinsById;
	TMap<FString, FRelationEndpointEdgeFact> EdgesById;
};

bool CalculateSha256(const TArray<uint8>& Bytes, FString& OutSha256)
{
	TUniquePtr<FEncryptionContext> CryptoContext =
		IPlatformCrypto::Get().CreateContext();
	TArray<uint8> Digest;
	if (!CryptoContext.IsValid()
		|| !CryptoContext->CalcSHA256(Bytes, Digest)
		|| Digest.Num() != 32)
	{
		return false;
	}
	OutSha256 = BytesToHex(Digest.GetData(), Digest.Num());
	return true;
}

bool ReadIrString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	FString& OutValue,
	const bool bAllowEmpty = false)
{
	return Object.IsValid()
		&& Object->TryGetStringField(FieldName, OutValue)
		&& (bAllowEmpty || !OutValue.IsEmpty());
}

bool LoadRelationEndpointIrFacts(
	const FBlueprintLensSource& Source,
	FRelationEndpointIrFacts& OutFacts,
	FString& Error)
{
	TArray<uint8> IrBytes;
	if (Source.IrPath.IsEmpty()
		|| !FFileHelper::LoadFileToArray(IrBytes, *Source.IrPath))
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("relation endpoint IR file is unreadable: %s"),
				*Source.IrPath));
	}
	FString ActualSha256;
	if (!CalculateSha256(IrBytes, ActualSha256))
	{
		return Fail(Error, TEXT("relation endpoint IR SHA-256 is unavailable"));
	}
	if (ActualSha256 != Source.IrSha256)
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("relation endpoint IR SHA-256 mismatch: declared %s, actual %s"),
				*Source.IrSha256,
				*ActualSha256));
	}

	FString IrJson;
	FFileHelper::BufferToString(IrJson, IrBytes.GetData(), IrBytes.Num());
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(IrJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Fail(Error, TEXT("relation endpoint IR root is malformed"));
	}
	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (!Root->TryGetObjectField(TEXT("blueprint"), Blueprint)
		|| Blueprint == nullptr || !Blueprint->IsValid()
		|| !(*Blueprint)->TryGetArrayField(TEXT("graphs"), Graphs)
		|| Graphs == nullptr)
	{
		return Fail(Error, TEXT("relation endpoint IR root is malformed"));
	}

	TSharedPtr<FJsonObject> SelectedGraph;
	int32 GraphMatchCount = 0;
	for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
	{
		const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
		FString GraphId;
		if (!ReadIrString(Graph, TEXT("id"), GraphId))
		{
			return Fail(Error, TEXT("relation endpoint IR graph is malformed"));
		}
		if (GraphId == Source.GraphId)
		{
			SelectedGraph = Graph;
			++GraphMatchCount;
		}
	}
	if (GraphMatchCount != 1 || !SelectedGraph.IsValid())
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("relation endpoint IR graph does not resolve exactly once: %s"),
				*Source.GraphId));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (!SelectedGraph->TryGetArrayField(TEXT("nodes"), Nodes)
		|| Nodes == nullptr
		|| !SelectedGraph->TryGetArrayField(TEXT("edges"), Edges)
		|| Edges == nullptr)
	{
		return Fail(Error, TEXT("relation endpoint IR graph is malformed"));
	}
	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
		FString NodeId;
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!ReadIrString(Node, TEXT("id"), NodeId)
			|| Node->TryGetArrayField(TEXT("pins"), Pins) == false
			|| Pins == nullptr
			|| OutFacts.NodeIds.Contains(NodeId))
		{
			return Fail(Error, TEXT("relation endpoint IR node is malformed"));
		}
		OutFacts.NodeIds.Add(NodeId);
		for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
		{
			const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
			FString PinId;
			FRelationEndpointPinFact PinFact;
			if (!ReadIrString(Pin, TEXT("id"), PinId)
				|| !ReadIrString(Pin, TEXT("node_id"), PinFact.NodeId)
				|| !ReadIrString(Pin, TEXT("name"), PinFact.Name, true)
				|| PinFact.NodeId != NodeId
				|| OutFacts.PinsById.Contains(PinId))
			{
				return Fail(Error, TEXT("relation endpoint IR pin is malformed"));
			}
			OutFacts.PinsById.Add(PinId, MoveTemp(PinFact));
		}
	}
	for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
	{
		const TSharedPtr<FJsonObject> Edge = EdgeValue->AsObject();
		FString EdgeId;
		FRelationEndpointEdgeFact EdgeFact;
		if (!ReadIrString(Edge, TEXT("id"), EdgeId)
			|| !ReadIrString(Edge, TEXT("kind"), EdgeFact.Kind)
			|| !ReadIrString(
				Edge, TEXT("source_node_id"), EdgeFact.SourceNodeId)
			|| !ReadIrString(
				Edge, TEXT("source_pin_id"), EdgeFact.SourcePinId)
			|| !ReadIrString(
				Edge, TEXT("target_node_id"), EdgeFact.TargetNodeId)
			|| !ReadIrString(
				Edge, TEXT("target_pin_id"), EdgeFact.TargetPinId)
			|| OutFacts.EdgesById.Contains(EdgeId))
		{
			return Fail(Error, TEXT("relation endpoint IR edge is malformed"));
		}
		const FRelationEndpointPinFact* SourcePin =
			OutFacts.PinsById.Find(EdgeFact.SourcePinId);
		const FRelationEndpointPinFact* TargetPin =
			OutFacts.PinsById.Find(EdgeFact.TargetPinId);
		if (!OutFacts.NodeIds.Contains(EdgeFact.SourceNodeId)
			|| !OutFacts.NodeIds.Contains(EdgeFact.TargetNodeId)
			|| SourcePin == nullptr || TargetPin == nullptr
			|| SourcePin->NodeId != EdgeFact.SourceNodeId
			|| TargetPin->NodeId != EdgeFact.TargetNodeId)
		{
			return Fail(Error, TEXT("relation endpoint IR edge is malformed"));
		}
		OutFacts.EdgesById.Add(EdgeId, MoveTemp(EdgeFact));
	}
	return true;
}

bool ValidateRelationEndpointProvenance(
	const FBlueprintLensExplanationModel& Model,
	FString& Error)
{
	const bool bHasAnyEndpointLedger = Model.Relations.ContainsByPredicate(
		[](const FBlueprintLensRelation& Relation)
		{
			return Relation.bHasSourceEdgeEndpoints;
		});
	if (!bHasAnyEndpointLedger)
	{
		return true;
	}

	FRelationEndpointIrFacts IrFacts;
	if (!LoadRelationEndpointIrFacts(Model.Source, IrFacts, Error))
	{
		return false;
	}
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (!Relation.bHasSourceEdgeEndpoints)
		{
			continue;
		}
		const FBlueprintLensUnit* SourceUnit =
			Model.FindUnit(Relation.SourceUnitId);
		const FBlueprintLensUnit* TargetUnit =
			Model.FindUnit(Relation.TargetUnitId);
		if (SourceUnit == nullptr || TargetUnit == nullptr)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation '%s' contains a dangling endpoint"),
					*Relation.Id));
		}
		TSet<FString> SourceUnitNodeIds;
		for (const FBlueprintLensSourceReference& Reference :
			 SourceUnit->SourceReferences)
		{
			SourceUnitNodeIds.Add(Reference.SourceNodeId);
		}
		TSet<FString> TargetUnitNodeIds;
		for (const FBlueprintLensSourceReference& Reference :
			 TargetUnit->SourceReferences)
		{
			TargetUnitNodeIds.Add(Reference.SourceNodeId);
		}
		const FString ExpectedEdgeKind =
			Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor
				|| Relation.Kind == EBlueprintLensRelationKind::ControlsExecution
			? TEXT("execution")
			: TEXT("data");
		TSet<FString> ParticipatingNodeIds = SourceUnitNodeIds;
		ParticipatingNodeIds.Append(TargetUnitNodeIds);
		// A one-edge relation (mandatory for LC2) is exact-direction by this
		// bridge check. Legacy grouped expressions may retain internal evidence
		// edges, but must still bridge source unit -> target unit.
		bool bHasDirectedBridge = false;
		for (const FString& SourceEdgeId : Relation.SourceEdgeIds)
		{
			const FRelationEndpointEdgeFact* Edge =
				IrFacts.EdgesById.Find(SourceEdgeId);
			if (Edge == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint source edge does not resolve in IR: %s"),
						*SourceEdgeId));
			}
			if (Edge->Kind != ExpectedEdgeKind
				|| !ParticipatingNodeIds.Contains(Edge->SourceNodeId)
				|| !ParticipatingNodeIds.Contains(Edge->TargetNodeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation source edge disagrees with endpoints: %s"),
						*SourceEdgeId));
			}
			bHasDirectedBridge |=
				SourceUnitNodeIds.Contains(Edge->SourceNodeId)
				&& TargetUnitNodeIds.Contains(Edge->TargetNodeId);
		}
		if (!bHasDirectedBridge)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation source edge disagrees with endpoints: %s"),
					*Relation.SourceEdgeIds[0]));
		}
		TSet<FString> EndpointEdgeIds;
		for (const FBlueprintLensSourceEdgeEndpoint& Endpoint :
			 Relation.SourceEdgeEndpoints)
		{
			if (EndpointEdgeIds.Contains(Endpoint.SourceEdgeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation source_edge_endpoints must bijectively match source_edge_ids: %s"),
						*Relation.Id));
			}
			EndpointEdgeIds.Add(Endpoint.SourceEdgeId);
		}
		bool bBijection =
			EndpointEdgeIds.Num() == Relation.SourceEdgeIds.Num();
		for (const FString& SourceEdgeId : Relation.SourceEdgeIds)
		{
			bBijection &= EndpointEdgeIds.Contains(SourceEdgeId);
		}
		if (!bBijection)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation source_edge_endpoints must bijectively match source_edge_ids: %s"),
					*Relation.Id));
		}

		for (const FBlueprintLensSourceEdgeEndpoint& Endpoint :
			 Relation.SourceEdgeEndpoints)
		{
			const FRelationEndpointEdgeFact* Edge =
				IrFacts.EdgesById.Find(Endpoint.SourceEdgeId);
			if (Edge == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint source edge does not resolve in IR: %s"),
						*Endpoint.SourceEdgeId));
			}
			if (!IrFacts.NodeIds.Contains(Endpoint.SourceNodeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint source node does not resolve in IR: %s"),
						*Endpoint.SourceNodeId));
			}
			if (!IrFacts.NodeIds.Contains(Endpoint.TargetNodeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint target node does not resolve in IR: %s"),
						*Endpoint.TargetNodeId));
			}
			const FRelationEndpointPinFact* SourcePin =
				IrFacts.PinsById.Find(Endpoint.SourcePinId);
			const FRelationEndpointPinFact* TargetPin =
				IrFacts.PinsById.Find(Endpoint.TargetPinId);
			if (SourcePin == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint source pin does not resolve in IR: %s"),
						*Endpoint.SourcePinId));
			}
			if (TargetPin == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint target pin does not resolve in IR: %s"),
						*Endpoint.TargetPinId));
			}
			if (Endpoint.SourceNodeId != Edge->SourceNodeId
				|| Endpoint.SourcePinId != Edge->SourcePinId
				|| Endpoint.TargetNodeId != Edge->TargetNodeId
				|| Endpoint.TargetPinId != Edge->TargetPinId)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint provenance disagrees with IR edge: %s"),
						*Endpoint.SourceEdgeId));
			}
			if (Endpoint.SourcePortLabel != SourcePin->Name
				|| Endpoint.TargetPortLabel != TargetPin->Name)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation endpoint port label disagrees with IR pin: %s"),
						*Endpoint.SourceEdgeId));
			}
		}
	}
	return true;
}

// Mirrors analysis/blueprint_lens/explanation_model.py so Python and UE reject
// exactly the same models. Every check is a no-op when its optional field is
// absent, so frozen v1.0 artifacts stay valid.
bool AdmissibleSemanticLabel(
	const FBlueprintLensRelation& Relation,
	EBlueprintLensSemanticLabel& OutExpected)
{
	switch (Relation.Kind)
	{
	case EBlueprintLensRelationKind::ExecutionPredecessor:
		OutExpected = EBlueprintLensSemanticLabel::NextExecution;
		return true;
	case EBlueprintLensRelationKind::PredicateFor:
		OutExpected = EBlueprintLensSemanticLabel::BranchCondition;
		return true;
	case EBlueprintLensRelationKind::ProvidesValue:
		OutExpected = EBlueprintLensSemanticLabel::ValueInput;
		return true;
	case EBlueprintLensRelationKind::ControlsExecution:
		if (Relation.PortLabel == TEXT("then"))
		{
			OutExpected = EBlueprintLensSemanticLabel::ConditionTrue;
			return true;
		}
		if (Relation.PortLabel == TEXT("else"))
		{
			OutExpected = EBlueprintLensSemanticLabel::ConditionFalse;
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool ReachesUnit(
	const TMap<FString, TArray<FString>>& Adjacency,
	const FString& Source,
	const FString& Target)
{
	TArray<FString> Frontier;
	if (const TArray<FString>* Initial = Adjacency.Find(Source))
	{
		Frontier = *Initial;
	}
	TSet<FString> Visited;
	while (!Frontier.IsEmpty())
	{
		const FString Current = Frontier.Pop();
		if (Current == Target)
		{
			return true;
		}
		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);
		if (const TArray<FString>* Next = Adjacency.Find(Current))
		{
			Frontier.Append(*Next);
		}
	}
	return false;
}

bool ValidateRelationLabels(
	const FBlueprintLensExplanationModel& Model,
	FString& Error)
{
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (!Relation.bHasPortLabel && !Relation.bHasSemanticLabel)
		{
			continue;
		}
		if (Relation.bHasSemanticLabel && !Relation.bHasPortLabel)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation semantic_label requires port_label: %s"),
					*Relation.Id));
		}
		if (!Relation.bHasSourceEdgeEndpoints ||
			Relation.SourceEdgeEndpoints.IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation port_label requires source_edge_endpoints: %s"),
					*Relation.Id));
		}
		for (const FBlueprintLensSourceEdgeEndpoint& Endpoint :
			 Relation.SourceEdgeEndpoints)
		{
			if (Endpoint.SourcePortLabel != Relation.PortLabel)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("relation port_label disagrees with endpoint provenance: %s"),
						*Relation.Id));
			}
		}
		if (!Relation.bHasSemanticLabel)
		{
			continue;
		}
		if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor)
		{
			for (const FBlueprintLensSourceEdgeEndpoint& Endpoint :
				 Relation.SourceEdgeEndpoints)
			{
				if (Endpoint.TargetPortLabel != TEXT("Condition"))
				{
					return Fail(
						Error,
						FString::Printf(
							TEXT("relation predicate_for must consume the 'Condition' port: %s"),
							*Relation.Id));
				}
			}
		}
		EBlueprintLensSemanticLabel Expected =
			EBlueprintLensSemanticLabel::NextExecution;
		if (!AdmissibleSemanticLabel(Relation, Expected))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation (%s, '%s') has no admissible semantic label: %s"),
					LexToString(Relation.Kind),
					*Relation.PortLabel,
					*Relation.Id));
		}
		if (Relation.SemanticLabel != Expected)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation semantic_label must be '%s': %s"),
					LexToString(Expected),
					*Relation.Id));
		}
	}
	return true;
}

bool ValidateDisambiguators(
	const FBlueprintLensExplanationModel& Model,
	FString& Error)
{
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		if (!Unit.bHasDisambiguator)
		{
			continue;
		}
		if (Unit.Role != EBlueprintLensRole::Control)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("disambiguator rule requires control role: %s"),
					*Unit.Id));
		}
		for (const FString& EvidenceId : Unit.Disambiguator.EvidenceRelationIds)
		{
			if (Model.FindRelation(EvidenceId) == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("disambiguator evidence does not resolve: %s"),
						*EvidenceId));
			}
		}
		const FBlueprintLensRelation* Deriving = nullptr;
		int32 Candidates = 0;
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
				Relation.TargetUnitId == Unit.Id)
			{
				++Candidates;
				Deriving = &Relation;
			}
		}
		if (Candidates != 1)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("disambiguator rule requires exactly one predicate_for relation, got %d: %s"),
					Candidates,
					*Unit.Id));
		}
		if (Unit.Disambiguator.EvidenceRelationIds.Num() != 1 ||
			Unit.Disambiguator.EvidenceRelationIds[0] != Deriving->Id)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("disambiguator evidence must name its deriving relation: %s"),
					*Unit.Id));
		}
		if (!Deriving->bHasSourceEdgeEndpoints ||
			Deriving->SourceEdgeEndpoints.Num() != 1)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("disambiguator requires exactly one endpoint ledger: %s"),
					*Unit.Id));
		}
		if (Unit.Disambiguator.Text !=
			Deriving->SourceEdgeEndpoints[0].SourcePortLabel)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("disambiguator text must equal the exported source port label '%s': %s"),
					*Deriving->SourceEdgeEndpoints[0].SourcePortLabel,
					*Unit.Id));
		}
	}
	return true;
}

bool ValidateGroupStatus(const FBlueprintLensGroup& Group, FString& Error)
{
	switch (Group.ProjectionStatus)
	{
	case EBlueprintLensProjectionStatus::Complete:
		if (Group.Title.IsEmpty() || Group.ClaimEvidence.IsEmpty() ||
			!Group.DiagnosticCode.IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("COMPLETE group requires a title and claim evidence and no diagnostic: %s"),
					*Group.Id));
		}
		return true;
	case EBlueprintLensProjectionStatus::StructuralOnly:
		if (!Group.Title.IsEmpty() || !Group.ClaimEvidence.IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("STRUCTURAL_ONLY group requires an empty title and empty claim_evidence: %s"),
					*Group.Id));
		}
		return true;
	default:
		if (!Group.Title.IsEmpty() || !Group.ClaimEvidence.IsEmpty() ||
			Group.DiagnosticCode.IsEmpty())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("ABSTAINED group requires a diagnostic and no title or claim: %s"),
					*Group.Id));
		}
		return true;
	}
}

bool ValidateCompleteGroupClaimCoverage(
	const FBlueprintLensExplanationModel& Model,
	const FBlueprintLensGroup& Group,
	FString& Error)
{
	TSet<FString> Components;
	for (const FBlueprintLensClaimEvidence& Evidence : Group.ClaimEvidence)
	{
		Components.Add(Evidence.Component);
	}

	TSet<FString> BranchUnitIds;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::ControlsExecution &&
			Group.OrderedUnitIds.Contains(Relation.SourceUnitId))
		{
			BranchUnitIds.Add(Relation.SourceUnitId);
		}
	}

	if (Group.Kind == EBlueprintLensGroupKind::GuardNest)
	{
		int32 PredicateRelationCount = 0;
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			if (Relation.Kind == EBlueprintLensRelationKind::PredicateFor &&
				Relation.TargetUnitId == Group.EntryUnitId)
			{
				++PredicateRelationCount;
			}
		}
		bool bHasPredicateLabel = false;
		for (const FString& Component : Components)
		{
			if (Component.StartsWith(TEXT("predicate_label.")))
			{
				bHasPredicateLabel = true;
				break;
			}
		}
		if (PredicateRelationCount > 0 && !bHasPredicateLabel)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("COMPLETE group claim evidence does not cover stated claim component 'predicate_label': %s"),
					*Group.Id));
		}
		return true;
	}

	if (Group.Kind == EBlueprintLensGroupKind::OutcomePath &&
		!BranchUnitIds.IsEmpty())
	{
		TSet<FString> PredicateComponents;
		TSet<FString> OutcomeComponents;
		for (const FString& Component : Components)
		{
			if (Component.StartsWith(TEXT("predicate_label.")))
			{
				PredicateComponents.Add(Component);
			}
			if (Component.StartsWith(TEXT("branch_outcome.")))
			{
				OutcomeComponents.Add(Component);
			}
		}
		if (PredicateComponents.Num() < BranchUnitIds.Num())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("COMPLETE group claim evidence does not cover predicate_label prefix: %s"),
					*Group.Id));
		}
		if (OutcomeComponents.Num() < BranchUnitIds.Num())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("COMPLETE group claim evidence does not cover stated claim component 'branch_outcome': %s"),
					*Group.Id));
		}
		for (const FString& Component : PredicateComponents)
		{
			const FString Suffix = Component.RightChop(
				FString(TEXT("predicate_label.")).Len());
			if (!OutcomeComponents.Contains(
				FString::Printf(TEXT("branch_outcome.%s"), *Suffix)))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("COMPLETE group claim evidence does not cover stated claim component '%s': %s"),
						*FString::Printf(TEXT("branch_outcome.%s"), *Suffix),
						*Group.Id));
			}
		}
		for (const FString& Component : OutcomeComponents)
		{
			const FString Suffix = Component.RightChop(
				FString(TEXT("branch_outcome.")).Len());
			if (!PredicateComponents.Contains(
				FString::Printf(TEXT("predicate_label.%s"), *Suffix)))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("COMPLETE group claim evidence does not cover stated claim component '%s': %s"),
						*FString::Printf(TEXT("predicate_label.%s"), *Suffix),
						*Group.Id));
			}
		}
		return true;
	}

	if (!Components.Contains(TEXT("title")))
	{
		return Fail(
			Error,
			FString::Printf(
				TEXT("COMPLETE group claim evidence does not cover stated claim component 'title': %s"),
				*Group.Id));
	}
	return true;
}

bool ValidateGroups(
	const FBlueprintLensExplanationModel& Model,
	FString& Error)
{
	if (!Model.bHasGroups)
	{
		if (Model.bHasGroupPartialOrder)
		{
			return Fail(Error, TEXT("group_partial_order requires groups"));
		}
		return true;
	}

	TSet<FString> SeenGroupIds;
	for (const FBlueprintLensGroup& Group : Model.Groups)
	{
		if (SeenGroupIds.Contains(Group.Id))
		{
			return Fail(
				Error,
				FString::Printf(TEXT("group IDs must be unique: %s"), *Group.Id));
		}
		SeenGroupIds.Add(Group.Id);
	}

	for (const FBlueprintLensGroup& Group : Model.Groups)
	{
		TSet<FString> Members;
		for (const FString& UnitId : Group.OrderedUnitIds)
		{
			if (Members.Contains(UnitId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("group members must be unique: %s"), *Group.Id));
			}
			Members.Add(UnitId);
			if (Model.FindUnit(UnitId) == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("group unit does not resolve: %s: %s"),
						*Group.Id,
						*UnitId));
			}
		}
		for (const FString& RelationId : Group.OrderedRelationIds)
		{
			if (Model.FindRelation(RelationId) == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("group relation does not resolve: %s: %s"),
						*Group.Id,
						*RelationId));
			}
		}
		if (Group.MemberCount != Group.OrderedUnitIds.Num())
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("group member_count does not match members: %s"),
					*Group.Id));
		}
		if (!Members.Contains(Group.EntryUnitId))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("group entry_unit_id must be a member: %s"), *Group.Id));
		}
		if ((Group.Kind == EBlueprintLensGroupKind::OutcomePath ||
			 Group.Kind == EBlueprintLensGroupKind::Scc) &&
			!Group.bHasExitUnitId)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("%s requires exit_unit_id: %s"),
					LexToString(Group.Kind),
					*Group.Id));
		}
		if (Group.bHasExitUnitId && !Members.Contains(Group.ExitUnitId))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("group exit_unit_id must be a member: %s"), *Group.Id));
		}
		if (Group.Kind == EBlueprintLensGroupKind::OutcomePath &&
			Group.ExitUnitId == Model.CriterionUnitId)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("outcome_path exit_unit_id must not equal criterion_unit_id: %s"),
					*Group.Id));
		}

		if (Group.Kind == EBlueprintLensGroupKind::OutcomePath)
		{
			if (Group.OrderedRelationIds.Num() != Group.OrderedUnitIds.Num() - 1)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("outcome_path must chain every member exactly once: %s"),
						*Group.Id));
			}
			for (int32 Index = 0; Index < Group.OrderedRelationIds.Num(); ++Index)
			{
				const FBlueprintLensRelation* Relation =
					Model.FindRelation(Group.OrderedRelationIds[Index]);
				if (Relation->SourceUnitId != Group.OrderedUnitIds[Index] ||
					Relation->TargetUnitId != Group.OrderedUnitIds[Index + 1])
				{
					return Fail(
						Error,
						FString::Printf(
							TEXT("outcome_path relation order is inconsistent: %s"),
							*Group.Id));
				}
			}
			if (Group.EntryUnitId != Group.OrderedUnitIds[0] ||
				Group.ExitUnitId !=
					Group.OrderedUnitIds[Group.OrderedUnitIds.Num() - 1])
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("outcome_path entry/exit must be its chain endpoints: %s"),
						*Group.Id));
			}
		}
		else
		{
			for (const FString& RelationId : Group.OrderedRelationIds)
			{
				const FBlueprintLensRelation* Relation =
					Model.FindRelation(RelationId);
				if (!Members.Contains(Relation->SourceUnitId) ||
					!Members.Contains(Relation->TargetUnitId))
				{
					return Fail(
						Error,
						FString::Printf(
							TEXT("group relation leaves its members: %s"),
							*Group.Id));
				}
			}
		}

		if (!ValidateGroupStatus(Group, Error))
		{
			return false;
		}
		if (Group.ProjectionStatus == EBlueprintLensProjectionStatus::Complete &&
			!ValidateCompleteGroupClaimCoverage(Model, Group, Error))
		{
			return false;
		}
	}

	// Structure before semantics, matching the Python ordering.
	for (const FBlueprintLensGroup& Group : Model.Groups)
	{
		if (Group.bHasParent && Model.FindGroup(Group.ParentGroupId) == nullptr)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("group parent does not resolve: %s"), *Group.Id));
		}
	}

	for (const FBlueprintLensGroup& Group : Model.Groups)
	{
		TSet<FString> Seen;
		const FBlueprintLensGroup* Current = &Group;
		while (Current != nullptr)
		{
			if (Seen.Contains(Current->Id))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("group parent chain contains a cycle: %s"), *Group.Id));
			}
			Seen.Add(Current->Id);
			Current = Current->bHasParent ? Model.FindGroup(Current->ParentGroupId)
										  : nullptr;
		}
	}

	for (const FBlueprintLensGroup& Group : Model.Groups)
	{
		if (!Group.bHasParent)
		{
			if (Group.bHasEnteredBy)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("group without a parent must not declare entered_by: %s"),
						*Group.Id));
			}
			continue;
		}
		if (!Group.bHasEnteredBy)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("nested group must declare entered_by: %s"), *Group.Id));
		}
		const FBlueprintLensGroup* Parent = Model.FindGroup(Group.ParentGroupId);
		const TSet<FString> Members(Group.OrderedUnitIds);
		bool bFound = false;
		for (const FBlueprintLensRelation& Relation : Model.Relations)
		{
			if (!Relation.bHasSemanticLabel ||
				Relation.SemanticLabel != Group.EnteredBy ||
				!Members.Contains(Relation.TargetUnitId))
			{
				continue;
			}
			if (Parent->OrderedUnitIds.Contains(Relation.SourceUnitId) &&
				!Members.Contains(Relation.SourceUnitId))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("group entered_by has no entering relation from its parent: %s"),
					*Group.Id));
		}
	}

	if (!Model.bHasGroupPartialOrder)
	{
		return true;
	}
	if (Model.GroupPartialOrder.Semantics != GroupPartialOrderSemantics)
	{
		return Fail(
			Error,
			TEXT("group_partial_order semantics does not match frozen constant"));
	}

	TMap<FString, TArray<FString>> Adjacency;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (Relation.Kind == EBlueprintLensRelationKind::ExecutionPredecessor ||
			Relation.Kind == EBlueprintLensRelationKind::ControlsExecution)
		{
			Adjacency.FindOrAdd(Relation.SourceUnitId).Add(Relation.TargetUnitId);
		}
	}
	for (const TPair<FString, FString>& Pair :
		 Model.GroupPartialOrder.IncomparableGroupIds)
	{
		if (Pair.Key == Pair.Value)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("incomparable pair must name two distinct groups: %s"),
					*Pair.Key));
		}
		const FBlueprintLensGroup* First = Model.FindGroup(Pair.Key);
		const FBlueprintLensGroup* Second = Model.FindGroup(Pair.Value);
		if (First == nullptr || Second == nullptr)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("incomparable group does not resolve: %s"),
					First == nullptr ? *Pair.Key : *Pair.Value));
		}
		if (!First->bHasExitUnitId || !Second->bHasExitUnitId ||
			First->ExitUnitId == Second->ExitUnitId)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("incomparable groups must have distinct exit_unit_id values: %s/%s"),
					*Pair.Key,
					*Pair.Value));
		}
		if (ReachesUnit(Adjacency, First->ExitUnitId, Second->ExitUnitId) ||
			ReachesUnit(Adjacency, Second->ExitUnitId, First->ExitUnitId))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("declared incomparable groups are ordered: %s/%s"),
					*Pair.Key,
					*Pair.Value));
		}
	}
	return true;
}

bool ValidateModel(
	const FBlueprintLensExplanationModel& Model,
	FString& Error)
{
	static const EBlueprintLensRole ExpectedLaneOrder[] = {
		EBlueprintLensRole::Criterion,
		EBlueprintLensRole::Control,
		EBlueprintLensRole::Predicate,
		EBlueprintLensRole::Value,
		EBlueprintLensRole::Consequence,
		EBlueprintLensRole::Boundary
	};
	if (Model.Lanes.Num() != UE_ARRAY_COUNT(ExpectedLaneOrder))
	{
		return Fail(Error, TEXT("root.lanes must contain the six v1 lanes"));
	}
	for (int32 Index = 0; Index < Model.Lanes.Num(); ++Index)
	{
		const FBlueprintLensLane& Lane = Model.Lanes[Index];
		if (Lane.Role != ExpectedLaneOrder[Index])
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("root.lanes[%d] violates the fixed v1 lane order"),
					Index));
		}

		switch (Lane.Role)
		{
		case EBlueprintLensRole::Criterion:
			if (Lane.State != EBlueprintLensLaneState::Populated ||
				Lane.UnitIds.IsEmpty() ||
				!Lane.EmptyMessage.IsEmpty())
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("root.lanes[%d] must be populated with units and no empty message"),
						Index));
			}
			break;
		case EBlueprintLensRole::Control:
		{
			const bool bPopulated =
				Lane.State == EBlueprintLensLaneState::Populated &&
				!Lane.UnitIds.IsEmpty() && Lane.EmptyMessage.IsEmpty();
			const bool bExplicitlyEmpty =
				Lane.State == EBlueprintLensLaneState::Empty &&
				Lane.UnitIds.IsEmpty() && !Lane.EmptyMessage.IsEmpty();
			if (!bPopulated && !bExplicitlyEmpty)
			{
				return Fail(
					Error,
					TEXT("control lane must be populated with units and no empty message, or explicitly empty with a message"));
			}
			break;
		}
		case EBlueprintLensRole::Predicate:
		case EBlueprintLensRole::Value:
		{
			const TCHAR* ExpectedEmptyMessage =
				Lane.Role == EBlueprintLensRole::Predicate
				? TEXT("No predicate facts in this explanation")
				: TEXT("No value facts in this explanation");
			const bool bPopulated =
				Lane.State == EBlueprintLensLaneState::Populated &&
				!Lane.UnitIds.IsEmpty() &&
				Lane.EmptyMessage.IsEmpty();
			const bool bExplicitlyEmpty =
				Lane.State == EBlueprintLensLaneState::Empty &&
				Lane.UnitIds.IsEmpty() &&
				Lane.EmptyMessage == ExpectedEmptyMessage;
			if (!bPopulated && !bExplicitlyEmpty)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("root.lanes[%d] must be populated or explicitly empty with its no-facts message"),
						Index));
			}
			break;
		}
		case EBlueprintLensRole::Consequence:
			if (Lane.State != EBlueprintLensLaneState::NotEnabled ||
				!Lane.UnitIds.IsEmpty() ||
				Lane.EmptyMessage !=
					TEXT("Not enabled in this backward-only query"))
			{
				return Fail(
					Error,
					TEXT("consequence lane must be not_enabled with no units and the backward-only message"));
			}
			break;
		case EBlueprintLensRole::Boundary:
		{
			const bool bPopulated =
				Lane.State == EBlueprintLensLaneState::Populated &&
				!Lane.UnitIds.IsEmpty() && Lane.EmptyMessage.IsEmpty();
			const bool bExplicitlyEmpty =
				Lane.State == EBlueprintLensLaneState::Empty &&
				Lane.UnitIds.IsEmpty() &&
				Lane.EmptyMessage == TEXT("All selected constructs supported");
			if (!bPopulated && !bExplicitlyEmpty)
			{
				return Fail(
					Error,
					TEXT("boundary lane must be empty with no units and the supported-constructs message, or populated with boundary units"));
			}
			break;
		}
		default:
			return Fail(
				Error,
				FString::Printf(
					TEXT("root.lanes[%d] contains an unknown role"),
					Index));
		}
	}

	TSet<FString> UnitIds;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		if (UnitIds.Contains(Unit.Id))
		{
			return Fail(
				Error,
				FString::Printf(TEXT("duplicate unit ID '%s'"), *Unit.Id));
		}
		UnitIds.Add(Unit.Id);

		int32 PrimaryCount = 0;
		for (const FBlueprintLensSourceReference& Reference :
			 Unit.SourceReferences)
		{
			PrimaryCount += Reference.bPrimary ? 1 : 0;
			if (Reference.BlueprintAssetPath !=
					Model.Source.BlueprintAssetPath ||
				Reference.GraphId != Model.Source.GraphId)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("source reference disagrees with source metadata in unit '%s'"),
						*Unit.Id));
			}
		}
		if (PrimaryCount != 1)
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("unit '%s' must have exactly one primary source"),
					*Unit.Id));
		}
	}

	TSet<FString> LaneOwnedUnitIds;
	for (const FBlueprintLensLane& Lane : Model.Lanes)
	{
		for (const FString& UnitId : Lane.UnitIds)
		{
			if (LaneOwnedUnitIds.Contains(UnitId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("unit '%s' is owned by multiple lanes"),
						*UnitId));
			}
			const FBlueprintLensUnit* Unit = Model.FindUnit(UnitId);
			if (Unit == nullptr)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("lane unit reference '%s' does not resolve"),
						*UnitId));
			}
			if (Unit->Role != Lane.Role)
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("lane role disagrees with unit role for '%s'"),
						*UnitId));
			}
			LaneOwnedUnitIds.Add(UnitId);
		}
	}
	if (LaneOwnedUnitIds.Num() != UnitIds.Num())
	{
		return Fail(Error, TEXT("lane unit IDs must partition all units"));
	}

	const FBlueprintLensUnit* CriterionUnit =
		Model.FindUnit(Model.CriterionUnitId);
	if (CriterionUnit == nullptr)
	{
		return Fail(Error, TEXT("criterion_unit_id does not resolve"));
	}
	if (CriterionUnit->Role != EBlueprintLensRole::Criterion)
	{
		return Fail(Error, TEXT("criterion unit must have criterion role"));
	}
	const FBlueprintLensSourceReference* CriterionReference =
		Model.FindSourceReference(Model.Query.CriterionSourceNodeId);
	if (CriterionReference == nullptr)
	{
		return Fail(
			Error,
			TEXT("query criterion_source_node_id does not resolve"));
	}
	bool bCriterionReferenceOwnedByCriterion = false;
	for (const FBlueprintLensSourceReference& Reference :
		 CriterionUnit->SourceReferences)
	{
		bCriterionReferenceOwnedByCriterion |=
			Reference.SourceNodeId == Model.Query.CriterionSourceNodeId;
	}
	if (!bCriterionReferenceOwnedByCriterion)
	{
		return Fail(
			Error,
			TEXT("criterion unit does not own the query criterion source node"));
	}

	TSet<FString> RelationIds;
	TSet<FString> SourceEdgeIds;
	for (const FBlueprintLensRelation& Relation : Model.Relations)
	{
		if (RelationIds.Contains(Relation.Id))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("duplicate relation ID '%s'"), *Relation.Id));
		}
		RelationIds.Add(Relation.Id);
		if (!UnitIds.Contains(Relation.SourceUnitId) ||
			!UnitIds.Contains(Relation.TargetUnitId))
		{
			return Fail(
				Error,
				FString::Printf(
					TEXT("relation '%s' contains a dangling endpoint"),
					*Relation.Id));
		}
		for (const FString& SourceEdgeId : Relation.SourceEdgeIds)
		{
			if (SourceEdgeIds.Contains(SourceEdgeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("source edge '%s' is owned by multiple relations"),
						*SourceEdgeId));
			}
			SourceEdgeIds.Add(SourceEdgeId);
		}
	}

	TSet<FString> SourceNodeIds;
	for (const FBlueprintLensUnit& Unit : Model.Units)
	{
		for (const FBlueprintLensSourceReference& Reference :
			 Unit.SourceReferences)
		{
			if (SourceNodeIds.Contains(Reference.SourceNodeId))
			{
				return Fail(
					Error,
					FString::Printf(
						TEXT("source node '%s' is owned by multiple units"),
						*Reference.SourceNodeId));
			}
			SourceNodeIds.Add(Reference.SourceNodeId);
		}
	}

	// v1.1 optional semantic extension; no-ops when the fields are absent.
	if (!ValidateRelationLabels(Model, Error) ||
		!ValidateDisambiguators(Model, Error) || !ValidateGroups(Model, Error))
	{
		return false;
	}

	const FBlueprintLensCounts DerivedCounts = {
		Model.Lanes.Num(),
		Model.Units.Num(),
		Model.Relations.Num(),
		SourceNodeIds.Num(),
		SourceEdgeIds.Num()
	};
	if (Model.Counts.Lanes != DerivedCounts.Lanes ||
		Model.Counts.Units != DerivedCounts.Units ||
		Model.Counts.Relations != DerivedCounts.Relations ||
		Model.Counts.SourceNodes != DerivedCounts.SourceNodes ||
		Model.Counts.SourceEdges != DerivedCounts.SourceEdges)
	{
		return Fail(
			Error,
			TEXT("declared counts do not match values derived from the model arrays"));
	}
	return true;
}
} // namespace

const TCHAR* LexToString(const EBlueprintLensRole Value)
{
	switch (Value)
	{
	case EBlueprintLensRole::Criterion:
		return TEXT("criterion");
	case EBlueprintLensRole::Control:
		return TEXT("control");
	case EBlueprintLensRole::Predicate:
		return TEXT("predicate");
	case EBlueprintLensRole::Value:
		return TEXT("value");
	case EBlueprintLensRole::Consequence:
		return TEXT("consequence");
	case EBlueprintLensRole::Boundary:
		return TEXT("boundary");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensLaneState Value)
{
	switch (Value)
	{
	case EBlueprintLensLaneState::Populated:
		return TEXT("populated");
	case EBlueprintLensLaneState::NotEnabled:
		return TEXT("not_enabled");
	case EBlueprintLensLaneState::Empty:
		return TEXT("empty");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensUnitKind Value)
{
	switch (Value)
	{
	case EBlueprintLensUnitKind::Node:
		return TEXT("node");
	case EBlueprintLensUnitKind::Expression:
		return TEXT("expression");
	case EBlueprintLensUnitKind::Summary:
		return TEXT("summary");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensSemanticStatus Value)
{
	switch (Value)
	{
	case EBlueprintLensSemanticStatus::Supported:
		return TEXT("supported");
	case EBlueprintLensSemanticStatus::Opaque:
		return TEXT("opaque");
	case EBlueprintLensSemanticStatus::Uncertain:
		return TEXT("uncertain");
	case EBlueprintLensSemanticStatus::Unsupported:
		return TEXT("unsupported");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensRelationKind Value)
{
	switch (Value)
	{
	case EBlueprintLensRelationKind::ExecutionPredecessor:
		return TEXT("execution_predecessor");
	case EBlueprintLensRelationKind::ControlsExecution:
		return TEXT("controls_execution");
	case EBlueprintLensRelationKind::PredicateFor:
		return TEXT("predicate_for");
	case EBlueprintLensRelationKind::ProvidesValue:
		return TEXT("provides_value");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensSemanticLabel Value)
{
	switch (Value)
	{
	case EBlueprintLensSemanticLabel::ConditionTrue:
		return TEXT("condition_true");
	case EBlueprintLensSemanticLabel::ConditionFalse:
		return TEXT("condition_false");
	case EBlueprintLensSemanticLabel::NextExecution:
		return TEXT("next_execution");
	case EBlueprintLensSemanticLabel::BranchCondition:
		return TEXT("branch_condition");
	case EBlueprintLensSemanticLabel::ValueInput:
		return TEXT("value_input");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensGroupKind Value)
{
	switch (Value)
	{
	case EBlueprintLensGroupKind::OutcomePath:
		return TEXT("outcome_path");
	case EBlueprintLensGroupKind::GuardNest:
		return TEXT("guard_nest");
	case EBlueprintLensGroupKind::ValueCone:
		return TEXT("value_cone");
	case EBlueprintLensGroupKind::FanoutBranch:
		return TEXT("fanout_branch");
	case EBlueprintLensGroupKind::PortalScope:
		return TEXT("portal_scope");
	case EBlueprintLensGroupKind::FrontierGroup:
		return TEXT("frontier_group");
	case EBlueprintLensGroupKind::Scc:
		return TEXT("scc");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* LexToString(const EBlueprintLensProjectionStatus Value)
{
	switch (Value)
	{
	case EBlueprintLensProjectionStatus::Complete:
		return TEXT("COMPLETE");
	case EBlueprintLensProjectionStatus::StructuralOnly:
		return TEXT("STRUCTURAL_ONLY");
	case EBlueprintLensProjectionStatus::Abstained:
		return TEXT("ABSTAINED");
	default:
		return TEXT("unknown");
	}
}

const FBlueprintLensUnit* FBlueprintLensExplanationModel::FindUnit(
	const FString& UnitId) const
{
	return Units.FindByPredicate(
		[&UnitId](const FBlueprintLensUnit& Unit)
		{
			return Unit.Id == UnitId;
		});
}

const FBlueprintLensRelation* FBlueprintLensExplanationModel::FindRelation(
	const FString& RelationId) const
{
	return Relations.FindByPredicate(
		[&RelationId](const FBlueprintLensRelation& Relation)
		{
			return Relation.Id == RelationId;
		});
}

const FBlueprintLensGroup* FBlueprintLensExplanationModel::FindGroup(
	const FString& GroupId) const
{
	return Groups.FindByPredicate(
		[&GroupId](const FBlueprintLensGroup& Group)
		{
			return Group.Id == GroupId;
		});
}

const FBlueprintLensSourceReference*
FBlueprintLensExplanationModel::FindSourceReference(
	const FString& SourceNodeId) const
{
	for (const FBlueprintLensUnit& Unit : Units)
	{
		const FBlueprintLensSourceReference* Reference =
			Unit.SourceReferences.FindByPredicate(
				[&SourceNodeId](
					const FBlueprintLensSourceReference& Candidate)
				{
					return Candidate.SourceNodeId == SourceNodeId;
				});
		if (Reference != nullptr)
		{
			return Reference;
		}
	}
	return nullptr;
}

FBlueprintLensLoadResult FBlueprintLensExplanationLoader::LoadFile(
	const FString& FilePath)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		return Failure(
			FString::Printf(TEXT("failed to read UTF-8 JSON file '%s'"), *FilePath));
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Failure(
			FString::Printf(TEXT("failed to parse JSON file '%s'"), *FilePath));
	}

	FString Error;
	if (!ValidateFieldsWithOptional(
			*Root,
			{
				TEXT("format"),
				TEXT("schema_version"),
				TEXT("rules_version"),
				TEXT("source"),
				TEXT("query"),
				TEXT("criterion_unit_id"),
				TEXT("lanes"),
				TEXT("units"),
				TEXT("relations"),
				TEXT("counts")
			},
			{TEXT("groups"), TEXT("group_partial_order")},
			TEXT("root"),
			Error))
	{
		return Failure(Error);
	}

	TSharedRef<FBlueprintLensExplanationModel> Model =
		MakeShared<FBlueprintLensExplanationModel>();
	if (!ReadString(
			*Root,
			TEXT("format"),
			TEXT("root"),
			Model->Format,
			Error) ||
		Model->Format != TEXT("blueprint-lens-explanation"))
	{
		return Failure(
			Error.IsEmpty()
				? FString::Printf(
					  TEXT("unsupported explanation format '%s'"),
					  *Model->Format)
				: Error);
	}
	if (!ReadString(
			*Root,
			TEXT("schema_version"),
			TEXT("root"),
			Model->SchemaVersion,
			Error) ||
		Model->SchemaVersion != TEXT("1.0.0"))
	{
		return Failure(
			Error.IsEmpty()
				? FString::Printf(
					  TEXT("unsupported schema_version '%s'; expected '1.0.0'"),
					  *Model->SchemaVersion)
				: Error);
	}
	if (!ReadString(
			*Root,
			TEXT("rules_version"),
			TEXT("root"),
			Model->RulesVersion,
			Error) ||
		!ReadString(
			*Root,
			TEXT("criterion_unit_id"),
			TEXT("root"),
			Model->CriterionUnitId,
			Error))
	{
		return Failure(Error);
	}

	TSharedPtr<FJsonObject> SourceObject;
	TSharedPtr<FJsonObject> QueryObject;
	TSharedPtr<FJsonObject> CountsObject;
	if (!ReadObject(
			*Root,
			TEXT("source"),
			TEXT("root"),
			SourceObject,
			Error) ||
		!ParseSource(*SourceObject, Model->Source, Error) ||
		!ReadObject(
			*Root,
			TEXT("query"),
			TEXT("root"),
			QueryObject,
			Error) ||
		!ParseQuery(*QueryObject, Model->Query, Error) ||
		!ReadObject(
			*Root,
			TEXT("counts"),
			TEXT("root"),
			CountsObject,
			Error) ||
		!ParseCounts(*CountsObject, Model->Counts, Error))
	{
		return Failure(Error);
	}

	const TArray<TSharedPtr<FJsonValue>>* Lanes = nullptr;
	if (!ReadArray(*Root, TEXT("lanes"), TEXT("root"), Lanes, Error))
	{
		return Failure(Error);
	}
	Model->Lanes.Reserve(Lanes->Num());
	for (int32 Index = 0; Index < Lanes->Num(); ++Index)
	{
		const FString Context =
			FString::Printf(TEXT("root.lanes[%d]"), Index);
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensLane Lane;
		if (!ReadArrayObject((*Lanes)[Index], Context, Object, Error) ||
			!ParseLane(*Object, Index, Lane, Error))
		{
			return Failure(Error);
		}
		Model->Lanes.Add(MoveTemp(Lane));
	}

	const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
	if (!ReadArray(*Root, TEXT("units"), TEXT("root"), Units, Error) ||
		Units->IsEmpty())
	{
		return Failure(
			Error.IsEmpty() ? TEXT("root.units must contain at least one item")
							: Error);
	}
	Model->Units.Reserve(Units->Num());
	for (int32 Index = 0; Index < Units->Num(); ++Index)
	{
		const FString Context =
			FString::Printf(TEXT("root.units[%d]"), Index);
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensUnit Unit;
		if (!ReadArrayObject((*Units)[Index], Context, Object, Error) ||
			!ParseUnit(*Object, Index, Unit, Error))
		{
			return Failure(Error);
		}
		Model->Units.Add(MoveTemp(Unit));
	}

	const TArray<TSharedPtr<FJsonValue>>* Relations = nullptr;
	if (!ReadArray(
			*Root,
			TEXT("relations"),
			TEXT("root"),
			Relations,
			Error) ||
		Relations->IsEmpty())
	{
		return Failure(
			Error.IsEmpty()
				? TEXT("root.relations must contain at least one item")
				: Error);
	}
	Model->Relations.Reserve(Relations->Num());
	for (int32 Index = 0; Index < Relations->Num(); ++Index)
	{
		const FString Context =
			FString::Printf(TEXT("root.relations[%d]"), Index);
		TSharedPtr<FJsonObject> Object;
		FBlueprintLensRelation Relation;
		if (!ReadArrayObject((*Relations)[Index], Context, Object, Error) ||
			!ParseRelation(*Object, Index, Relation, Error))
		{
			return Failure(Error);
		}
		Model->Relations.Add(MoveTemp(Relation));
	}

	Model->bHasGroups = Root->Values.Contains(TEXT("groups"));
	if (Model->bHasGroups)
	{
		const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
		if (!ReadArray(*Root, TEXT("groups"), TEXT("root"), Groups, Error))
		{
			return Failure(Error);
		}
		Model->Groups.Reserve(Groups->Num());
		for (int32 Index = 0; Index < Groups->Num(); ++Index)
		{
			const FString Context =
				FString::Printf(TEXT("root.groups[%d]"), Index);
			TSharedPtr<FJsonObject> Object;
			FBlueprintLensGroup Group;
			if (!ReadArrayObject((*Groups)[Index], Context, Object, Error) ||
				!ParseGroup(*Object, Index, Group, Error))
			{
				return Failure(Error);
			}
			Model->Groups.Add(MoveTemp(Group));
		}
	}

	Model->bHasGroupPartialOrder =
		Root->Values.Contains(TEXT("group_partial_order"));
	if (Model->bHasGroupPartialOrder)
	{
		TSharedPtr<FJsonObject> OrderObject;
		if (!ReadObject(
				*Root,
				TEXT("group_partial_order"),
				TEXT("root"),
				OrderObject,
				Error) ||
			!ParseGroupPartialOrder(
				*OrderObject, Model->GroupPartialOrder, Error))
		{
			return Failure(Error);
		}
	}

	if (!ValidateModel(*Model, Error))
	{
		return Failure(Error);
	}
	const FString SourceBaseDirectory = FPaths::GetPath(
		FPaths::ConvertRelativePathToFull(FilePath));
	if (FPaths::IsRelative(Model->Source.IrPath))
	{
		Model->Source.IrPath = FPaths::Combine(
			SourceBaseDirectory, Model->Source.IrPath);
	}
	if (FPaths::IsRelative(Model->Source.SlicePath))
	{
		Model->Source.SlicePath = FPaths::Combine(
			SourceBaseDirectory, Model->Source.SlicePath);
	}
	if (!ValidateRelationEndpointProvenance(*Model, Error))
	{
		return Failure(Error);
	}

	FBlueprintLensLoadResult Result;
	Result.Model = Model;
	return Result;
}
