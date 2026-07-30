//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"

#include "NTTDataInterface.h"

class IPropertyHandle;

class FNTTVerticalAlignmentCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<class IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FNTTVerticalAlignmentCustomization());
	}

	FNTTVerticalAlignmentCustomization()
	{
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	ENTTTextVerticalAlignment GetCurrentAlignment(TSharedRef<IPropertyHandle> PropertyHandle) const;

	void OnCurrentAlignmentChanged(ENTTTextVerticalAlignment NewAlignment, TSharedRef<IPropertyHandle> PropertyHandle);
};
