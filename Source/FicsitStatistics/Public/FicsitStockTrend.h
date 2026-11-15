#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsTypes.h"

class UFGItemDescriptor;

/**
 * How fast each item's total in storage is changing, from the scans over the last few minutes. The scan passes in its
 * totals, so this has no game objects and can be tested on its own.
 *
 * The trend is a least-squares slope over the whole window rather than the first scan against the last, so a train
 * unloading or a stack grabbed by hand moves it less. It keeps running sums that scans are added to and taken out of
 * as they enter and leave the window, so each scan costs the same however many scans the window holds.
 */
class FICSITSTATISTICS_API FFicsitStockTrend
{
public:
	/** How far back the trend looks: long enough to smooth over a train round trip, short enough to notice a change. */
	static constexpr double WindowSeconds = 600.0;

	/** Records one scan's totals (item -> amount) and drops scans that have left the window. */
	void Add(double WorldTime, const TMap<TSubclassOf<UFGItemDescriptor>, float>& Totals);
	/** Each item's amount in the latest scan and its change per minute over the window. */
	void Fill(FFicsitStockMap& OutStock) const;
	/** Seconds between the oldest and the latest scan kept (0 with fewer than two). */
	double GetCoveredSeconds() const;

private:
	struct FSample
	{
		/** Seconds since BaseTime, which keeps the squared times small. */
		double Time = 0.0;
		TMap<TSubclassOf<UFGItemDescriptor>, float> Totals;
	};
	/** Per item: the sum of its amounts, and of time * amount, over the scans in the window. */
	struct FItemSums
	{
		double Amount = 0.0;
		double TimeAmount = 0.0;
	};

	/** Adds a scan to the running sums (Sign 1) or takes it out (Sign -1). */
	void Accumulate(const FSample& Sample, double Sign);

	TArray<FSample> Samples;
	double BaseTime = 0.0;
	double SumTime = 0.0;
	double SumTimeSquared = 0.0;
	TMap<TSubclassOf<UFGItemDescriptor>, FItemSums> Sums;
};
