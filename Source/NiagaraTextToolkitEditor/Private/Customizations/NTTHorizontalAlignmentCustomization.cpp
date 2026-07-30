//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#include "Customizations/NTTHorizontalAlignmentCustomization.h"

#include "DetailWidgetRow.h"
#include "Layout/Margin.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SSegmentedControl.h"

class IDetailChildrenBuilder;

#define LOCTEXT_NAMESPACE "FNiagaraTextToolkitEditorModule"

void FNTTHorizontalAlignmentCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(0)
	[
		SNew(SSegmentedControl<ENTTTextHorizontalAlignment>)
		.Value(this, &FNTTHorizontalAlignmentCustomization::GetCurrentAlignment, PropertyHandle)
		.OnValueChanged(this, &FNTTHorizontalAlignmentCustomization::OnCurrentAlignmentChanged, PropertyHandle)
		+ SSegmentedControl<ENTTTextHorizontalAlignment>::Slot(ENTTTextHorizontalAlignment::NTT_THA_Left)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Left"))
			.ToolTip(LOCTEXT("NTTHAlignLeft", "Left Align Horizontally"))
		+ SSegmentedControl<ENTTTextHorizontalAlignment>::Slot(ENTTTextHorizontalAlignment::NTT_THA_Center)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Center"))
			.ToolTip(LOCTEXT("NTTHAlignCenter", "Center Align Horizontally"))
		+ SSegmentedControl<ENTTTextHorizontalAlignment>::Slot(ENTTTextHorizontalAlignment::NTT_THA_Right)
			.Icon(FAppStyle::GetBrush("HorizontalAlignment_Right"))
			.ToolTip(LOCTEXT("NTTHAlignRight", "Right Align Horizontally"))
	];
}

void FNTTHorizontalAlignmentCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

ENTTTextHorizontalAlignment FNTTHorizontalAlignmentCustomization::GetCurrentAlignment(TSharedRef<IPropertyHandle> PropertyHandle) const
{
	uint8 Value;
	if (PropertyHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		return (ENTTTextHorizontalAlignment)Value;
	}

	return ENTTTextHorizontalAlignment::NTT_THA_Center;
}

void FNTTHorizontalAlignmentCustomization::OnCurrentAlignmentChanged(ENTTTextHorizontalAlignment NewAlignment, TSharedRef<IPropertyHandle> PropertyHandle)
{
	PropertyHandle->SetValue((uint8)NewAlignment);
}

#undef LOCTEXT_NAMESPACE
