#include "FicsitMilestoneNeeds.h"

#include "FGCentralStorageSubsystem.h"
#include "FGGamePhaseManager.h"
#include "FGSchematicManager.h"

FFicsitMilestoneNeeds FFicsitMilestoneNeeds::Read(const UObject* WorldContext)
{
	FFicsitMilestoneNeeds Needs;
	if (!WorldContext)
	{
		return Needs;
	}
	UObject* Context = const_cast<UObject*>(WorldContext);
	if (AFGGamePhaseManager* PhaseManager = AFGGamePhaseManager::Get(Context))
	{
		TArray<FRemainingPhaseCost> Remaining;
		PhaseManager->GetRemainingPhaseCosts(Remaining);
		for (const FRemainingPhaseCost& Cost : Remaining)
		{
			if (Cost.ItemClass && Cost.RemainingCost > 0)
			{
				Needs.SpaceElevator.Add(Cost.ItemClass.Get(), Cost.RemainingCost);
			}
		}
	}
	// The milestone selected in the HUB is optional: the player may not have one selected.
	if (AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(Context))
	{
		if (const TSubclassOf<UFGSchematic> Active = SchematicManager->GetActiveSchematic())
		{
			for (const FItemAmount& Cost : SchematicManager->GetRemainingCostFor(Active))
			{
				if (Cost.ItemClass && Cost.Amount > 0)
				{
					Needs.Hub.Add(Cost.ItemClass.Get(), Cost.Amount);
				}
			}
		}
	}
	Needs.Combined = Needs.SpaceElevator;
	for (const auto& Pair : Needs.Hub)
	{
		Needs.Combined.FindOrAdd(Pair.Key) += Pair.Value;
	}
	return Needs;
}

TMap<UClass*, int32> FicsitDepot::ReadStock(const UObject* WorldContext)
{
	TMap<UClass*, int32> Stock;
	if (AFGCentralStorageSubsystem* Depot = WorldContext ? AFGCentralStorageSubsystem::Get(const_cast<UObject*>(WorldContext)) : nullptr)
	{
		TArray<FItemAmount> Stored;
		Depot->GetAllItemsFromCentralStorage(Stored);
		for (const FItemAmount& Amount : Stored)
		{
			if (Amount.ItemClass && Amount.Amount > 0)
			{
				Stock.Add(Amount.ItemClass.Get(), Amount.Amount);
			}
		}
	}
	return Stock;
}
