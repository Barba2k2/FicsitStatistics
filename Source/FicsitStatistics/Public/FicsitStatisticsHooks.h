#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsTypes.h"

class AFGBuildableFactory;
class AFGBuildableManufacturer;

/** Item amounts counted since the last drain, per direction and source (display units: items, or m³ for fluids). */
struct FFicsitCounts
{
	TMap<UClass*, double> Produced[FicsitRateSourceCount];
	TMap<UClass*, double> Consumed[FicsitRateSourceCount];
	/** Hand-crafting, building and dismantling, per EFicsitManualKind. */
	TMap<UClass*, double> Manual[FicsitManualKindCount];
	/**
	 * How many hook events made up the amounts, all sources together: [0] produced, [1] consumed. Amounts arrive in
	 * chunks (an oil extractor delivers several m³ per cycle), so noise follows the number of events, not the amount.
	 */
	TMap<UClass*, int32> Events[2];

	void Reset()
	{
		for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
		{
			Produced[Source].Reset();
			Consumed[Source].Reset();
		}
		Events[0].Reset();
		Events[1].Reset();
		for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
		{
			Manual[Kind].Reset();
		}
	}
};

/** What one hook event was, for the 5s realtime view. */
enum class EFicsitLiveChannel : uint8
{
	Produced,
	Consumed,
	/** Consumed by the AWESOME Sink. */
	Sunk,
	/** Manual amounts: ManualBase + EFicsitManualKind. */
	ManualBase,
};

/** One hook event with the moment it happened (FPlatformTime::Seconds()), for the 5s realtime view. */
struct FFicsitLiveEvent
{
	double Time = 0.0;
	UClass* Item = nullptr;
	float Amount = 0.f;
	uint8 Channel = 0;
};

/**
 * Counts real item flows by hooking the moments the game moves items:
 *  - a manufacturer or extractor finishing a production cycle (output),
 *  - a manufacturer taking a cycle's ingredients (input),
 *  - the AWESOME Sink accepting an item, and the Space Elevator taking a delivery,
 *  - hand-crafting at a Craft Bench / Equipment Workshop, building, and dismantling (the Manual tab).
 * Factory code runs on worker threads, so handlers only do a few map adds under a lock, and the subsystem drains the
 * totals on the game thread once per sample.
 */
class FFicsitEventCounters
{
public:
	static void InstallHooks();

	static void Add(bool bProduced, EFicsitRateSource Source, UClass* Item, double Amount);
	static void AddManual(EFicsitManualKind Kind, UClass* Item, double Amount);

	/** Moves everything counted since the last drain into Out (which is reset first). */
	static void Drain(FFicsitCounts& Out);

	/** Same counts, kept separately for the once-a-second live view (1m range). */
	static void DrainLive(FFicsitCounts& Out);

	/** Every event since the last call, each with its own timestamp, oldest first (roughly: worker threads interleave). */
	static void DrainEvents(TArray<FFicsitLiveEvent>& Out);

	/** Number of hook events since the last call (for the performance log). */
	static int64 ConsumeEventCount();

private:
	static void RecordCycleCompleted(AFGBuildableFactory* Factory);
	static void RecordIngredientsConsumed(AFGBuildableManufacturer* Manufacturer);
	/** Appends to PendingEvents. Lock must be held. */
	static void AddEventLocked(double Time, UClass* Item, double Amount, uint8 Channel);

	static FCriticalSection Lock;
	static FFicsitCounts Pending;
	static FFicsitCounts LivePending;
	static TArray<FFicsitLiveEvent> PendingEvents;
	static int64 EventCount;
	static bool bHooksInstalled;
};

/**
 * What the hooks counted over a stretch of time, as amounts (display units, not rates). One per second feeds the 1m
 * range (and replicates to clients), the 5s range fills its points with them, and each scan turns its counts into one
 * for the saved history.
 */
struct FICSITSTATISTICS_API FFicsitLiveSecond
{
	int32 Seq = 0;
	float Seconds = 1.f;
	/** Every counted source (machines, extractors, Sink, Space Elevator). */
	TMap<UClass*, float> Produced;
	TMap<UClass*, float> Consumed;
	/** The part of Consumed that went into the AWESOME Sink. */
	TMap<UClass*, float> Sunk;
	TMap<UClass*, float> Manual[FicsitManualKindCount];

	/** Hook counts as amounts per item: every source together, the Sink's part separately, and the manual amounts. */
	static FFicsitLiveSecond FromCounts(const FFicsitCounts& Counts);

	/**
	 * A history sample from these amounts over Seconds: rates per minute for the counted sources, plus the Reference
	 * scan's rates for what has no hook (generator fuel, nuclear waste) and its power figures.
	 */
	void ToSample(const FFicsitProductionSnapshot& Reference, double WorldTime, FFicsitProductionSnapshot& OutSample) const;
};
