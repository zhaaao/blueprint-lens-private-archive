#include "BlueprintLensDisplayLabel.h"

namespace
{
const TMap<FString, FString>& DisplayLabelTranslations()
{
	static const TMap<FString, FString> Map = []()
	{
		TMap<FString, FString> Result;
		Result.Add(TEXT("\u4E8B\u4EF6BeginPlay"), TEXT("Event BeginPlay"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step01Complete"), TEXT("Set LC1Step01Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step02Complete"), TEXT("Set LC1Step02Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step03Complete"), TEXT("Set LC1Step03Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step04Complete"), TEXT("Set LC1Step04Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step05Complete"), TEXT("Set LC1Step05Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step06Complete"), TEXT("Set LC1Step06Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step07Complete"), TEXT("Set LC1Step07Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step08Complete"), TEXT("Set LC1Step08Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step09Complete"), TEXT("Set LC1Step09Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step10Complete"), TEXT("Set LC1Step10Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step11Complete"), TEXT("Set LC1Step11Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Step12Complete"), TEXT("Set LC1Step12Complete"));
		Result.Add(TEXT("\u8BBE\u7F6ELC1Ready"), TEXT("Set LC1Ready"));
		return Result;
	}();
	return Map;
}
} // namespace

FString BlueprintLensDisplayLabel(const FString& Title)
{
	const FString* const MappedLabel = DisplayLabelTranslations().Find(Title);
	return MappedLabel != nullptr ? *MappedLabel : Title;
}

FString BlueprintLensDisplayLabel(const FBlueprintLensUnit& Unit)
{
	const FString DisplayTitle = BlueprintLensDisplayLabel(Unit.Title);
	if (Unit.bHasDisambiguator && !Unit.Disambiguator.Text.IsEmpty())
	{
		return FString::Printf(
			TEXT("%s (%s)"), *DisplayTitle, *Unit.Disambiguator.Text);
	}
	return DisplayTitle;
}
