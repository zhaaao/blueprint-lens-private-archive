// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintLensM6NativeSlice.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace BlueprintLensM6NativeSlice
{
namespace
{
FLinearColor StatusColor(const FString& Status)
{
	if (Status == TEXT("opaque")) return FLinearColor(0.92f, 0.58f, 0.18f, 1.0f);
	if (Status == TEXT("uncertain")) return FLinearColor(0.82f, 0.68f, 0.22f, 1.0f);
	if (Status == TEXT("unsupported")) return FLinearColor(0.82f, 0.25f, 0.23f, 1.0f);
	if (Status == TEXT("truncated")) return FLinearColor(0.58f, 0.40f, 0.82f, 1.0f);
	return FLinearColor(0.16f, 0.66f, 0.52f, 1.0f);
}
} // namespace
} // namespace BlueprintLensM6NativeSlice

void SBlueprintLensM6NativeSlice::Construct(const FArguments& InArgs)
{
	ViewModel = InArgs._ViewModel;
	OnEntitySelected = InArgs._OnEntitySelected;
	OnSourceRequested = InArgs._OnSourceRequested;
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	if (ViewModel == nullptr)
	{
		Content->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("M6 native slice unavailable")))
		];
	}
	else
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Read-only native slice  |  %d nodes  |  %d links  |  %d boundaries  |  %d truncated"),
				ViewModel->Entities.Num(), ViewModel->Relations.Num(),
				ViewModel->BoundaryCount, ViewModel->TruncatedCount)))
			.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
		];
		for (const FM6BaselineViewEntity& Entity : ViewModel->Entities)
		{
			const FString Reason = !Entity.BoundaryReason.IsEmpty()
				? Entity.BoundaryReason
				: (!Entity.SemanticReason.IsEmpty()
					? Entity.SemanticReason : Entity.PresentationReason);
			Content->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(
					BlueprintLensM6NativeSlice::StatusColor(Entity.PresentationStatus) * 0.28f)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.Text(FText::FromString(Entity.Label))
						.ToolTipText(FText::FromString(Entity.Id))
						.OnClicked(this, &SBlueprintLensM6NativeSlice::SelectEntity, Entity.Id)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s · %s%s"), *Entity.Role,
							*Entity.PresentationStatus,
							Reason.IsEmpty() ? TEXT("") : *FString(TEXT(" · ") + Reason))))
						.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Open source")))
						.OnClicked(this, &SBlueprintLensM6NativeSlice::RequestSource, Entity.Id)
					]
				]
			];
		}
		Content->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
		[
			SNew(SSeparator)
		];
		for (const FM6BaselineViewRelation& Relation : ViewModel->Relations)
		{
			Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("%s  — %s →  %s  [%s]"),
					*Relation.SourceEntityId, *Relation.Label,
					*Relation.TargetEntityId, *Relation.SemanticStatus)))
				.Font(FAppStyle::Get().GetFontStyle("SmallFont"))
				.AutoWrapText(true)
			];
		}
	}

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Content
		]
	];
}
FReply SBlueprintLensM6NativeSlice::SelectEntity(FString EntityId)
{
	if (OnEntitySelected.IsBound()) OnEntitySelected.Execute(MoveTemp(EntityId));
	return FReply::Handled();
}

FReply SBlueprintLensM6NativeSlice::RequestSource(FString EntityId)
{
	if (OnSourceRequested.IsBound()) OnSourceRequested.Execute(MoveTemp(EntityId));
	return FReply::Handled();
}
