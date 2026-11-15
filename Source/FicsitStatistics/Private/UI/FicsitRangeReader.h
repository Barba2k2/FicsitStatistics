#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsTypes.h"

class AFicsitStatisticsSubsystem;

/** One item's average rates over a range. Consumed includes what went into the AWESOME Sink, and Sunk is that part. */
struct FFicsitRangeRates
{
	float Produced = 0.f;
	float Consumed = 0.f;
	float Sunk = 0.f;
};

/**
 * What one time range shows: average rates, manual totals and power over it, and the series to draw.
 *
 * Hides which history backs a range (the 5s realtime one, the 1m live one, a saved tier, or a multiplayer client's
 * copy), keeping it fresh (5s up to this frame, clients asking the server for new history), what to show while a range
 * has no data yet (the latest scan), and which series are smoothed for display.
 *
 * Build one per refresh and drop it after: it holds references into the subsystem (or the history and scan it was
 * given). Averages are read on first use.
 */
class FFicsitRangeReader
{
public:
	using FItem = TSubclassOf<UFGItemDescriptor>;
	using FRateMap = TMap<FItem, FFicsitItemRate>;
	using FAmountMap = TMap<FItem, float>;

	/** Whether Range can be shown here. 5s is built from the hook events, which only the server sees. */
	static bool IsAvailable(const AFicsitStatisticsSubsystem* Subsystem, EFicsitTimeRange Range);

	/** Ranges whose data changes every frame, so views redraw their graphs per frame. */
	static bool UpdatesEveryFrame(EFicsitTimeRange Range);

	/** A UI time range, brought up to date first (5s up to this frame, and on clients a request for new history). */
	FFicsitRangeReader(AFicsitStatisticsSubsystem& Subsystem, EFicsitTimeRange Range);

	/** The last Seconds of the finest saved history (the HUD's averages and sparkline). */
	static FFicsitRangeReader Recent(AFicsitStatisticsSubsystem& Subsystem, float Seconds);

	/**
	 * A range over a given history and latest scan, without the subsystem: nothing is brought up to date or requested.
	 * History must be the one that backs Range. MaxSeconds > 0 reads only the most recent MaxSeconds (like Recent).
	 */
	FFicsitRangeReader(const FFicsitHistory& InHistory, const FFicsitProductionSnapshot& InLatestScan, EFicsitTimeRange InRange, float InMaxSeconds = 0.f);

	/** Average actual rates, every item that moved. Until the range has data, the latest scan's rates. */
	const FRateMap& GetProduced() const;
	const FRateMap& GetConsumed() const;
	/** The part of GetConsumed that went into the AWESOME Sink. */
	const FAmountMap& GetSunk() const;
	/** One item's averages (zero if it didn't move). */
	FFicsitRangeRates GetRates(FItem Item) const;

	/** Manual amounts (crafting, building, dismantling) summed over the range, including the point being filled now. */
	void GetManualTotals(TMap<FItem, FFicsitManualTotals>& OutTotals) const;

	/** Average power: totals per EFicsitPowerTotal and MW per building type. Until the range has data, the latest scan. */
	void GetPowerAverages(float (&OutTotals)[FicsitPowerTotalCount], FAmountMap& OutProducers, FAmountMap& OutConsumers) const;

	/** One item's produced or consumed rate over time, oldest first. Times are seconds relative to now (<= 0). Smoothed where the points are jagged. */
	void GetSeries(FItem Item, bool bProduced, TArray<float>& OutTimes, TArray<float>& OutValues) const;

	/** One item's net rate (produced minus demand) per point, oldest first. Smoothed like GetSeries. */
	void GetNetSeries(FItem Item, bool bSinkIsDemand, TArray<float>& OutValues) const;

	/** One power total over time, oldest first, like GetSeries. */
	void GetPowerSeries(EFicsitPowerTotal Total, TArray<float>& OutTimes, TArray<float>& OutValues) const;

	/** Seconds a graph's time axis spans: fixed per range (data fills in from the right), and what has been tracked for All. */
	float GetGraphSpanSeconds() const;

private:
	FFicsitRangeReader(AFicsitStatisticsSubsystem& InSubsystem, EFicsitTimeRange InRange, float InMaxSeconds);

	bool HasPoints() const;
	bool SmoothsForDisplay() const;
	void ReadAverages() const;

	const FFicsitHistory& History;
	const FFicsitProductionSnapshot& LatestScan;
	EFicsitTimeRange Range;
	int32 Tier = 0;
	/** > 0: only the most recent MaxSeconds of the tier. */
	float MaxSeconds = 0.f;

	mutable bool bAveragesRead = false;
	mutable FRateMap Produced;
	mutable FRateMap Consumed;
	mutable FAmountMap Sunk;
};
