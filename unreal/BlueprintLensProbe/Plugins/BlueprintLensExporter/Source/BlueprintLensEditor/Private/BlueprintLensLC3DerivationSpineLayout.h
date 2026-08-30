#pragma once

#include "BlueprintLensLC3ValueConeLayoutSession.h"
#include "Fonts/SlateFontInfo.h"

enum class EBlueprintLensLC3DerivationSpineElementKind : uint8
{
	Value,
	Operator,
	Criterion,
	Control
};

enum class EBlueprintLensLC3DerivationSpineLabelKind : uint8
{
	Value,
	OperatorGlyph,
	OperatorName,
	CriterionRole,
	CriterionValue,
	Control,
	QualifiedPort,
	Region
};

struct FBlueprintLensLC3DerivationSpineElement
{
	FString UnitId;
	FString ReaderLabel;
	FString DisplayLabel;
	FString Glyph;
	EBlueprintLensLC3DerivationSpineElementKind Kind =
		EBlueprintLensLC3DerivationSpineElementKind::Value;
	FBox2D Bounds;
	FVector2D InputAnchor = FVector2D::ZeroVector;
	FVector2D OutputAnchor = FVector2D::ZeroVector;
	bool bSelected = false;
};

struct FBlueprintLensLC3DerivationSpineLabel
{
	FString Key;
	FString UnitId;
	FString Text;
	EBlueprintLensLC3DerivationSpineLabelKind Kind =
		EBlueprintLensLC3DerivationSpineLabelKind::Value;
	FSlateFontInfo Font;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	FBox2D ExclusionBounds;
	bool bSelectedEndpoint = false;
	// Labels the surface paints on an opaque plate stay readable where a route
	// runs under them, so they are excluded from route clearance only. Every
	// label still owns its bounds for label-label clearance.
	bool bHasBackingPlate = false;
};

struct FBlueprintLensLC3DerivationSpineRoute
{
	FString RelationId;
	FString SourceUnitId;
	FString TargetUnitId;
	EBlueprintLensLayoutRelationFamily Family =
		EBlueprintLensLayoutRelationFamily::Value;
	TArray<FVector2D> Points;
};

struct FBlueprintLensLC3DerivationSpineLayout
{
	FVector2D CanvasSize = FVector2D::ZeroVector;
	TArray<FBlueprintLensLC3DerivationSpineElement> Elements;
	TArray<FBlueprintLensLC3DerivationSpineLabel> Labels;
	TArray<FBlueprintLensLC3DerivationSpineRoute> Routes;
	FString SelectedOperatorUnitId;
	FString BaseLedgerFingerprint;
	FBox2D LocalSubtreeBounds;
	bool bCompact = false;
	bool bHasLocalSubtree = false;
	FString DiagnosticCode;

	const FBlueprintLensLC3DerivationSpineElement* FindElement(
		const FString& UnitId) const;
	bool CoversProjection(
		const FBlueprintLensLC3ValueConeProjection& Projection) const;
	bool HasNoRouteLabelIntersections() const;
	bool HasNoLabelIntersections() const;
	bool IsRenderable(
		const FBlueprintLensLC3ValueConeProjection& Projection) const;
};

class FBlueprintLensLC3DerivationSpineLayoutBuilder
{
public:
	static FBlueprintLensLC3DerivationSpineLayout Build(
		const FBlueprintLensLC3ValueConeProjection& Projection,
		const FBlueprintLensLC3ValueConeLayout& BaseLayout,
		float TargetWidth,
		const FString& SelectedUnitId);
};
