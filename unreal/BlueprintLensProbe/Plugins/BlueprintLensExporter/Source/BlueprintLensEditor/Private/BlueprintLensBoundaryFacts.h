#pragma once

#include "BlueprintLensExplanationModel.h"

inline bool BlueprintLensIsBoundarySemanticStatus(
	const EBlueprintLensSemanticStatus SemanticStatus)
{
	return SemanticStatus == EBlueprintLensSemanticStatus::Opaque ||
		SemanticStatus == EBlueprintLensSemanticStatus::Unsupported ||
		SemanticStatus == EBlueprintLensSemanticStatus::Uncertain;
}

inline const TCHAR* BlueprintLensBoundaryStatusName(
	const EBlueprintLensSemanticStatus SemanticStatus)
{
	switch (SemanticStatus)
	{
	case EBlueprintLensSemanticStatus::Opaque:
		return TEXT("opaque");
	case EBlueprintLensSemanticStatus::Unsupported:
		return TEXT("unsupported");
	case EBlueprintLensSemanticStatus::Uncertain:
		return TEXT("uncertain");
	default:
		return TEXT("supported");
	}
}

inline const TCHAR* BlueprintLensBoundaryDisclosure(
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
