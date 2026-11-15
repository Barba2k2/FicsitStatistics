#include "FicsitStatisticsTypes.h"

#include "FGInventoryLibrary.h"
#include "Resources/FGItemDescriptor.h"

namespace FicsitUnits
{
	bool IsFluid(TSubclassOf<UFGItemDescriptor> Item)
	{
		const EResourceForm Form = UFGItemDescriptor::GetForm(Item);
		return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
	}

	float ToDisplayAmount(int32 RawAmount, TSubclassOf<UFGItemDescriptor> Item)
	{
		return UFGInventoryLibrary::GetAmountConvertedByForm(RawAmount, UFGItemDescriptor::GetForm(Item));
	}

	float ToDisplayRate(float RawPerMinute, TSubclassOf<UFGItemDescriptor> Item)
	{
		return IsFluid(Item) ? RawPerMinute / 1000.f : RawPerMinute;
	}
}
