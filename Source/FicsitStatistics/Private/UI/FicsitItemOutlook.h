#pragma once

#include "CoreMinimal.h"
#include "FicsitBalance.h"
#include "FicsitMilestoneNeeds.h"
#include "FicsitStatisticsTypes.h"

class FFicsitRangeReader;
class UFGItemDescriptor;

/** One item's outlook over a time range. Rates are averages over the range, in items/min (m³/min for fluids). */
struct FFicsitItemOutlook
{
	float Produced = 0.f;
	/** Everything consumed, AWESOME Sink included. */
	float Consumed = 0.f;
	/** The part of Consumed that went into the AWESOME Sink. */
	float Sunk = 0.f;
	/** Consumed without the Sink, unless the player counts the Sink as demand. */
	float Demand = 0.f;
	FicsitBalance::EState State = FicsitBalance::EState::Idle;

	/** Amount in the Dimensional Depot. */
	int32 Depot = 0;

	/** Amount in storage containers and fluid buffers. */
	float Stored = 0.f;
	/** How fast that's changing per minute, or 0 until the trend has had a couple of minutes of scans. */
	float StoredPerMinute = 0.f;
	/** Whether the trend has had those couple of minutes. */
	bool bStorageTrendReady = false;
	/** Minutes until storage runs out at its current trend, or < 0 if it isn't falling. */
	float StorageRunoutMinutes = -1.f;
	/** Whether storage runs out within the context's StorageWarningMinutes, which is when we warn about it. */
	bool bStorageRunsOutSoon = false;

	/**
	 * What the current Space Elevator phase still needs of this item (0: none), and minutes until it has it (< 0: not
	 * being made). The ETAs use the latest scan's rates, not the range's, since they're about the current supply.
	 */
	int32 ElevatorRemaining = 0;
	float ElevatorEtaMinutes = -1.f;
	/** The same for the milestone selected in the HUB. */
	int32 HubRemaining = 0;
	float HubEtaMinutes = -1.f;

	float Net() const { return Produced - Demand; }
	bool IsMilestonePart() const { return ElevatorRemaining > 0 || HubRemaining > 0; }
	/** A deficit, or a milestone part nothing makes (it has to be made to finish the milestone). The "Only deficits" filter. */
	bool ShowsAsDeficit() const { return State == FicsitBalance::EState::Deficit || (IsMilestonePart() && Produced <= 0.f); }
};

/** What the item outlook reads from the world besides rates. Read it once per refresh and share it between readers. */
struct FFicsitOutlookContext
{
	FFicsitMilestoneNeeds Needs;
	/** Item -> amount in the Dimensional Depot. */
	TMap<UClass*, int32> Depot;
	bool bSinkIsDemand = false;
	/** Storage that runs out further away than this many minutes doesn't get a warning, since that's not worth one. */
	float StorageWarningMinutes = 30.f;

	/** Milestone needs, Depot stock and the Sink and storage warning settings, from the game. */
	static FFicsitOutlookContext Read(const UObject* WorldContext);
};

/**
 * Item outlooks over one time range: what an item's rates mean (demand, net, balance state), its Depot stock, its
 * storage and how long that lasts, and how long until each milestone that needs it has it. The stats window, its
 * details pane and the HUD all read items through this, so they always agree.
 *
 * Holds references to the reader, scan and context it was given: build one per refresh, like the time range reader.
 */
class FFicsitItemOutlooks
{
public:
	/** LatestScan gives the rates right now, which the milestone ETAs are worked out from. */
	FFicsitItemOutlooks(const FFicsitRangeReader& InRange, const FFicsitProductionSnapshot& InLatestScan, const FFicsitOutlookContext& InContext);

	FFicsitItemOutlook Get(TSubclassOf<UFGItemDescriptor> Item) const;

	/** Storage trends covering less than this are too jumpy to warn from (e.g. right after a save loads). */
	static constexpr float MinStorageTrendSeconds = 120.f;

	const FFicsitOutlookContext& GetContext() const { return Context; }

private:
	const FFicsitRangeReader& Range;
	const FFicsitProductionSnapshot& LatestScan;
	const FFicsitOutlookContext& Context;
};
