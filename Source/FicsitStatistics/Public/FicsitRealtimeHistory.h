#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsHooks.h"
#include "FicsitStatisticsTypes.h"

/**
 * The history behind the 5s live range: timestamped hook events sorted into points of 1/60 s, 300 of them.
 *
 * Every point that has ended is closed, events or not, so the graph keeps moving while nothing happens. An event that
 * arrives after its point closed (factory work runs on worker threads) goes into the point being filled, a frame or so
 * late, rather than being lost. Nothing advancing it for longer than the range (a long hitch, the game paused) starts
 * the history over, so the graph fills in from the right instead of replaying seconds of empty points.
 *
 * Times are FPlatformTime seconds: the same clock the events are stamped with.
 */
class FICSITSTATISTICS_API FFicsitRealtimeHistory
{
public:
	static constexpr double PointSeconds = 1.0 / 60.0;
	static constexpr double SpanSeconds = 5.0;

	FFicsitRealtimeHistory();

	/**
	 * Adds Events (oldest first, roughly) and closes every point that has ended by Now. Reference is the latest scan:
	 * what has no hook (generator fuel, nuclear waste) and the power figures come from it.
	 */
	void Advance(TConstArrayView<FFicsitLiveEvent> Events, double Now, const FFicsitProductionSnapshot& Reference);

	const FFicsitHistory& GetHistory() const { return History; }

	struct FStats
	{
		/** Events placed in a point, and how many of those arrived after their point had closed. */
		int64 Placed = 0;
		int64 Late = 0;
		/** Events from before the history last started over, left out. */
		int64 Dropped = 0;
	};
	/** What happened since the last call (for the once-a-minute log). */
	FStats ConsumeStats();

	/** Benchmark only: when on, what the closed points produced is added up, per item, until taken. */
	bool bTallyProduced = false;
	TMap<UClass*, float> TakeProducedTally();

private:
	void StartOver(double Now);
	void ClosePoint(const FFicsitProductionSnapshot& Reference);

	FFicsitHistory History;
	/** The point being filled: its start (< 0 before the first Advance) and what's in it so far. */
	double PointStart = -1.0;
	FFicsitLiveSecond Open;
	/** When the history last started over: events from before it are left out. */
	double StartTime = 0.0;
	FStats Stats;
	TMap<UClass*, float> ProducedTally;
};
