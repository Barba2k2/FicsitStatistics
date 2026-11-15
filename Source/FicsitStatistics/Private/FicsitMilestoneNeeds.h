#pragma once

#include "CoreMinimal.h"

/** What the current milestones still need, read from the game. The window, the HUD and the log all read it here. */
struct FFicsitMilestoneNeeds
{
	/** Item -> amount the current Space Elevator phase still needs. */
	TMap<UClass*, int32> SpaceElevator;
	/** Item -> amount the milestone selected in the HUB still needs (empty when none is selected). */
	TMap<UClass*, int32> Hub;
	/** Both added together: what the "Current milestone" filter and the supply chain use. */
	TMap<UClass*, int32> Combined;

	static FFicsitMilestoneNeeds Read(const UObject* WorldContext);
};

namespace FicsitDepot
{
	/** Item -> amount stored in the Dimensional Depot (its contents replicate, so this works on clients too). */
	TMap<UClass*, int32> ReadStock(const UObject* WorldContext);
}
