//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#include "Customizations/NTTVerticalAlignmentCustomization.h"

#include "DetailWidgetRow.h"
#include "Layout/Margin.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SSegmentedControl.h"

class IDetailChildrenBuilder;

#define LOCTEXT_NAMESPACE "FNiagaraTextToolkitEditorModule"

void FNTTVerticalAlignmentCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(0)
	[
		SNew(SSegmentedControl<ENTTTextVerticalAlignment>)
		.Value(this, &FNTTVerticalAlignmentCustomization::GetCurrentAlignment, PropertyHandle)
		.OnValueChanged(this, &FNTTVerticalAlignmentCustomization::OnCurrentAlignmentChanged, PropertyHandle)
		+ SSegmentedControl<ENTTTextVerticalAlignment>::Slot(ENTTTextVerticalAlignment::NTT_TVA_Top)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Top"))
			.ToolTip(LOCTEXT("NTTVAlignTop", "Top Align Vertically"))
		+ SSegmentedControl<ENTTTextVerticalAlignment>::Slot(ENTTTextVerticalAlignment::NTT_TVA_Center)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Center"))
			.ToolTip(LOCTEXT("NTTVAlignCenter", "Center Align Vertically"))
		+ SSegmentedControl<ENTTTextVerticalAlignment>::Slot(ENTTTextVerticalAlignment::NTT_TVA_Bottom)
			.Icon(FAppStyle::GetBrush("VerticalAlignment_Bottom"))
			.ToolTip(LOCTEXT("NTTVAlignBottom", "Bottom Align Vertically"))
	];
}

void FNTTVerticalAlignmentCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

ENTTTextVerticalAlignment FNTTVerticalAlignmentCustomization::GetCurrentAlignment(TSharedRef<IPropertyHandle> PropertyHandle) const
{
	uint8 Value;
	if (PropertyHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		return (ENTTTextVerticalAlignment)Value;
	}

	return ENTTTextVerticalAlignment::NTT_TVA_Center;
}

void FNTTVerticalAlignmentCustomization::OnCurrentAlignmentChanged(ENTTTextVerticalAlignment NewAlignment, TSharedRef<IPropertyHandle> PropertyHandle)
{
	PropertyHandle->SetValue((uint8)NewAlignment);
}

#undef LOCTEXT_NAMESPACE
