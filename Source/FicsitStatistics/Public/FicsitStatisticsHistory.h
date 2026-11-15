#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsTypes.h"
#include "FicsitStatisticsHistory.generated.h"

/**
 * The time ranges the UI offers (UI_DESIGN.md: 5s / 1m / 10m / 1h / 10h / 50h / All). 5s and 1m come from histories
 * that aren't saved: 5s from timestamped hook events (redrawn every frame), 1m from per-second counts.
 */
UENUM(BlueprintType)
enum class EFicsitTimeRange : uint8
{
	FiveSeconds,
	OneMinute,
	TenMinutes,
	OneHour,
	TenHours,
	FiftyHours,
	All,
	Count UMETA(Hidden)
};

/** One averaged point in time: actual produced / consumed rate for every item in the item table (by index). */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitHistoryPoint
{
	GENERATED_BODY()

	/** Tracked-time (seconds) at the end of the period this point averages. */
	UPROPERTY(SaveGame)
	double Time = 0.0;

	/** Indexed like FFicsitHistory::ItemTable. May be shorter than the table (missing entries are 0). */
	UPROPERTY(SaveGame)
	TArray<float> Produced;

	UPROPERTY(SaveGame)
	TArray<float> Consumed;

	/** The part of Consumed that went into the AWESOME Sink (overflow, not demand). Empty in saves from older versions. */
	UPROPERTY(SaveGame)
	TArray<float> Sunk;

	/** Manual amounts in this point's period (totals, not rates), per EFicsitManualKind. Empty in older saves. */
	UPROPERTY(SaveGame)
	TArray<float> Crafted;

	UPROPERTY(SaveGame)
	TArray<float> CraftUsed;

	UPROPERTY(SaveGame)
	TArray<float> Built;

	UPROPERTY(SaveGame)
	TArray<float> Refunded;

	/** Average power over the point's period, per EFicsitPowerTotal. Empty in older saves. */
	UPROPERTY(SaveGame)
	TArray<float> PowerTotals;

	/** Average MW per building type, indexed like FFicsitHistory::BuildingTable. */
	UPROPERTY(SaveGame)
	TArray<float> PowerProduced;

	UPROPERTY(SaveGame)
	TArray<float> PowerConsumed;

	float GetPowerTotal(EFicsitPowerTotal Total) const { const int32 Index = static_cast<int32>(Total); return PowerTotals.IsValidIndex(Index) ? PowerTotals[Index] : 0.f; }

	TArray<float>& Manual(EFicsitManualKind Kind);
	const TArray<float>& Manual(EFicsitManualKind Kind) const { return const_cast<FFicsitHistoryPoint*>(this)->Manual(Kind); }
	float GetManual(EFicsitManualKind Kind, int32 Index) const { const TArray<float>& Values = Manual(Kind); return Values.IsValidIndex(Index) ? Values[Index] : 0.f; }

	float GetProduced(int32 Index) const { return Produced.IsValidIndex(Index) ? Produced[Index] : 0.f; }
	float GetConsumed(int32 Index) const { return Consumed.IsValidIndex(Index) ? Consumed[Index] : 0.f; }
	float GetSunk(int32 Index) const { return Sunk.IsValidIndex(Index) ? Sunk[Index] : 0.f; }
};

/** One item's manual amounts over a time range, per EFicsitManualKind. */
struct FFicsitManualTotals
{
	float Values[FicsitManualKindCount] = {};

	float Get(EFicsitManualKind Kind) const { return Values[static_cast<int32>(Kind)]; }
};

/** A fixed-capacity ring of points at one resolution. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitHistoryTier
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	float IntervalSeconds = 5.f;

	UPROPERTY(SaveGame)
	int32 Capacity = 120;

	/** Ring buffer. Once full, Head is the index of the oldest point (next to be overwritten). */
	UPROPERTY(SaveGame)
	TArray<FFicsitHistoryPoint> Points;

	UPROPERTY(SaveGame)
	int32 Head = 0;

	/** Running sum of base samples since the last emitted point. */
	UPROPERTY(SaveGame)
	FFicsitHistoryPoint Accumulator;

	UPROPERTY(SaveGame)
	float AccumulatedSeconds = 0.f;

	int32 Num() const { return Points.Num(); }

	/** Point by age order: 0 = oldest. */
	const FFicsitHistoryPoint& GetOrdered(int32 Index) const
	{
		return Points.Num() < Capacity ? Points[Index] : Points[(Head + Index) % Capacity];
	}

	void Push(FFicsitHistoryPoint&& Point);
};

/**
 * Multi-resolution history of every item's actual production and consumption.
 * Saved with the game through the subsystem's SaveGame properties.
 */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitHistory
{
	GENERATED_BODY()

	/** Index -> item. Points store values by these indices so items never need to be repeated per point. */
	UPROPERTY(SaveGame)
	TArray<TSubclassOf<UFGItemDescriptor>> ItemTable;

	/** Index -> building descriptor, for the per-building power arrays. */
	UPROPERTY(SaveGame)
	TArray<TSubclassOf<UFGItemDescriptor>> BuildingTable;

	UPROPERTY(SaveGame)
	TArray<FFicsitHistoryTier> Tiers;

	/** Seconds of game time tracked so far (advances only while the save is being played). */
	UPROPERTY(SaveGame)
	double TrackedSeconds = 0.0;

	/** Which set of tiers this history has. */
	enum class ELayout : uint8
	{
		/** The saved multi-resolution history (10m to All). */
		Saved,
		/** 60 one-second points of raw hook counts, behind the 1m range. Not saved. */
		Minute,
		/** 300 points of 1/60 s each from timestamped hook events, behind the 5s range. Not saved. */
		Realtime,
	};

	/**
	 * Set before the first EnsureLayout. The short ranges are separate histories with their own layouts because a
	 * layout change resets the saved history.
	 */
	ELayout Layout = ELayout::Saved;

	/** Creates the tiers if missing, or resets them if a saved layout doesn't match this version's. */
	void EnsureLayout();

	/** Adds one base sample covering DeltaSeconds of game time. */
	void AddSample(const FFicsitProductionSnapshot& Snapshot, float DeltaSeconds);

	/** The tier that backs a UI time range. "All" picks the finest tier that covers everything tracked. */
	int32 GetTierForRange(EFicsitTimeRange Range) const;

	/**
	 * Average actual rates over the points of a tier, or (MaxSeconds > 0) over its most recent MaxSeconds. Only items
	 * with non-zero values are returned.
	 */
	void GetAverages(int32 TierIndex, TMap<TSubclassOf<UFGItemDescriptor>, FFicsitItemRate>& OutProduced,
		TMap<TSubclassOf<UFGItemDescriptor>, FFicsitItemRate>& OutConsumed,
		TMap<TSubclassOf<UFGItemDescriptor>, float>* OutSunk = nullptr, float MaxSeconds = 0.f) const;

	/** Manual amounts summed over a tier (including its not-yet-complete point). Only items with any amount. */
	void GetManualTotals(int32 TierIndex, TMap<TSubclassOf<UFGItemDescriptor>, FFicsitManualTotals>& OutTotals) const;

	/** Average power over a tier: totals (per EFicsitPowerTotal) and MW per building type. False if the tier is empty. */
	bool GetPowerAverages(int32 TierIndex, float (&OutTotals)[FicsitPowerTotalCount], TMap<TSubclassOf<UFGItemDescriptor>, float>& OutProduced,
		TMap<TSubclassOf<UFGItemDescriptor>, float>& OutConsumed) const;

	/** One power total's series in a tier, oldest first. Times are seconds relative to now (<= 0). */
	void GetPowerSeries(int32 TierIndex, EFicsitPowerTotal Total, TArray<float>& OutTimes, TArray<float>& OutValues) const;

	/** One item's net (produced - consumed, optionally excluding the Sink) per point of a tier, oldest first. MaxSeconds > 0: only the most recent MaxSeconds. */
	void GetNetSeries(int32 TierIndex, TSubclassOf<UFGItemDescriptor> Item, float MaxSeconds, bool bSinkIsDemand, TArray<float>& OutValues) const;

	/** One item's series in a tier, oldest first. Times are seconds relative to now (<= 0). */
	void GetSeries(int32 TierIndex, TSubclassOf<UFGItemDescriptor> Item, bool bProduced, TArray<float>& OutTimes, TArray<float>& OutValues) const;

	int32 FindItemIndex(TSubclassOf<UFGItemDescriptor> Item) const { return ItemTable.IndexOfByKey(Item); }

	/** Seconds a full tier of this history spans (interval x capacity). */
	float GetTierSpanSeconds(int32 TierIndex) const;

	/** The saved history's layout (not the live one). */
	static int32 NumTiers();
	static float GetTierIntervalSeconds(int32 TierIndex);
	static int32 GetTierCapacity(int32 TierIndex);

private:
	int32 FindOrAddItemIndex(TSubclassOf<UFGItemDescriptor> Item);
	int32 FindOrAddBuildingIndex(TSubclassOf<UFGItemDescriptor> Building);
};
