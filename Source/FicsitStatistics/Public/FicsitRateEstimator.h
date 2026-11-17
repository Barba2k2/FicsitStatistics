#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsHooks.h"
#include "FicsitStatisticsTypes.h"

/**
 * Turns what the hooks counted into "right now" rates: a change-detecting average with one window per item.
 *
 * Each item's window grows to MaxWindowSeconds while nothing changes, and starts over when a short recent window
 * disagrees with it by more than normal noise (Poisson in events, so an oil extractor's few large deliveries aren't
 * mistaken for a change). Production and consumption of an item share its window, so their difference doesn't jitter.
 * Until an item's window covers its short window, the scan's estimate stays in place as a warm-up value.
 *
 * Pure: no world, no hooks, no timers. The subsystem drains the hooks and passes the counts in once per scan.
 */
class FFicsitRateEstimator
{
public:
	struct FSettings
	{
		/** Seconds between Apply calls. Each call adds one bucket of counts. */
		double BucketSeconds = 5.0;
		/** The longest an item's window grows while nothing changes. */
		double MaxWindowSeconds = 600.0;
		/** The short window is max(this, 2 cycles of the item's slowest machine, time for ~12 expected events), up to 120 s. */
		double MinShortWindowSeconds = 10.0;
		/** How far (in standard deviations) the short window must stray before the window starts over. */
		double ChangeThreshold = 3.0;
		/** AWESOME Sink and Space Elevator counts are raw amounts, so fluids among them are converted from liters to m³. Unset: no fluids. */
		TFunction<bool(UClass* Item)> IsFluid;
	};

	/** For the once-a-minute log. */
	struct FStats
	{
		/** Windows that started over since the last ConsumeStats. */
		int32 Restarts = 0;
		int32 Items = 0;
		double MedianWindowSeconds = 0.0;
	};

	// Two constructors instead of a "= FSettings()" default argument, since clang (the Linux server build) won't use
	// FSettings' member initializers before this class is complete.
	FFicsitRateEstimator();
	explicit FFicsitRateEstimator(FSettings InSettings);

	/** Forgets everything and counts from StartTime (world seconds), e.g. when a save loads. */
	void Reset(double StartTime);

	void SetSettings(FSettings InSettings);

	/** The scan notes each item's slowest machine cycle before the next Apply. Notes from earlier scans are dropped. */
	void NoteCycle(UClass* Item, float CycleSeconds);

	/**
	 * Adds Counts (what the hooks counted since the previous call) as the bucket ending at Snapshot.WorldTimeSeconds.
	 * Then, in Snapshot:
	 *  - manufacturing and extraction rates become counted rates once the item's window is ready (Max stays the scan's),
	 *  - counted flows the scan no longer sees (a machine just dismantled) are added,
	 *  - AWESOME Sink and Space Elevator rates are filled in (they only exist as counts),
	 *  - manual amounts (crafting, building, dismantling) are added to Snapshot.Manual,
	 *  - totals are recomputed from their sources.
	 */
	void Apply(FFicsitCounts&& Counts, FFicsitProductionSnapshot& Snapshot);

	/** What the latest Apply counted, without the manual amounts (those moved to the snapshot). Empty before the first. */
	const FFicsitCounts& GetLatestCounts() const;

	/** The span Item's counted rates are averaged over right now (0 if it has none). */
	double GetWindowSeconds(UClass* Item) const;

	/** The slowest cycle the latest Apply used for Item (0 if none was noted). */
	float GetCycleSeconds(UClass* Item) const;

	/** Restarts since the last call, plus the current windows. */
	FStats ConsumeStats();

private:
	struct FBucket
	{
		double Time = 0.0;
		FFicsitCounts Counts;
	};

	FSettings Settings;
	double StartTime = 0.0;
	/** One per Apply, kept for the longest window. Each holds the counts of the interval that ended at its time. */
	TArray<FBucket> Buckets;
	/** Where each item's window starts (world seconds), moved forward when a change is detected. */
	TMap<UClass*, double> WindowStart;
	TMap<UClass*, double> WindowSeconds;
	/** Cycles noted since the last Apply, and the ones the last Apply used. */
	TMap<UClass*, float> PendingCycleSeconds;
	TMap<UClass*, float> CycleSeconds;
	int32 Restarts = 0;
};
