#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsTypes.generated.h"

class UFGItemDescriptor;
class UFGRecipe;

/** Where an item's production or consumption comes from. Drives the item-details breakdown (UI adaptation A4). */
UENUM()
enum class EFicsitRateSource : uint8
{
	Manufacturing,
	Extraction,
	PowerGeneration,
	ResourceSink,
	SpaceElevator,
	Count UMETA(Hidden)
};

constexpr int32 FicsitRateSourceCount = static_cast<int32>(EFicsitRateSource::Count);

inline const TCHAR* LexToString(EFicsitRateSource Source)
{
	switch (Source)
	{
	case EFicsitRateSource::Manufacturing:   return TEXT("Manufacturing");
	case EFicsitRateSource::Extraction:      return TEXT("Extraction");
	case EFicsitRateSource::PowerGeneration: return TEXT("Power");
	case EFicsitRateSource::ResourceSink:    return TEXT("Sink");
	case EFicsitRateSource::SpaceElevator:   return TEXT("Elevator");
	default:                                 return TEXT("?");
	}
}

/** What a machine is doing right now, as seen from one item it makes or uses (UI adaptation A4). */
enum class EFicsitMachineState : uint8
{
	Running,
	/** Not producing for lack of inputs. Seen from an item the machine uses: it is short of that item. */
	Starved,
	/** Seen from an item the machine uses: it has enough of that item but is short of another input. */
	OtherInputs,
	/** Has its inputs but can't put out its products (belt or pipe backed up, or nuclear waste full). */
	OutputBlocked,
	/** Powered and not paused, not producing for another reason (no power demand, starting up). */
	Idle,
	Paused,
	Unpowered,
	Count
};

/** Items moved by hand rather than by machines (the Manual tab). Stored as amounts, not rates. */
enum class EFicsitManualKind : uint8
{
	/** Made at a Craft Bench or Equipment Workshop. */
	Crafted,
	/** Ingredients used up by hand-crafting. */
	CraftUsed,
	/** Spent on building (from the inventory or the Dimensional Depot). */
	Built,
	/** Returned by dismantling. */
	Refunded,
	Count
};

constexpr int32 FicsitManualKindCount = static_cast<int32>(EFicsitManualKind::Count);

/** Whole-factory power figures (MW, or MWh for battery storage). */
enum class EFicsitPowerTotal : uint8
{
	Produced,
	Consumed,
	/** What every generator could produce right now. */
	Capacity,
	/** What every consumer could draw at once (the fuse-risk figure). */
	MaxConsumption,
	BatteryInput,
	BatteryOutput,
	/** MWh */
	BatteryStored,
	/** MWh */
	BatteryCapacity,
	/** Part of Produced / Consumed that the per-building scan can't attribute (electric trains, lights, ...). */
	OtherProduced,
	OtherConsumed,
	Count
};

constexpr int32 FicsitPowerTotalCount = static_cast<int32>(EFicsitPowerTotal::Count);

/** One building type's power (keyed by its building descriptor, so it has a name and an icon). */
struct FFicsitPowerBuilding
{
	/** Produced (generators) or consumed (everything else) right now, MW. */
	float MW = 0.f;
	/** Capacity (generators) or maximum draw (consumers), MW. */
	float MaxMW = 0.f;
	int32 Count = 0;
	/** How many are generating / drawing power right now. */
	int32 Active = 0;
};

using FFicsitPowerBuildingMap = TMap<TSubclassOf<UFGItemDescriptor>, FFicsitPowerBuilding>;

/** The whole factory's power at one moment. */
struct FFicsitPowerSnapshot
{
	float Totals[FicsitPowerTotalCount] = {};
	FFicsitPowerBuildingMap Producers;
	FFicsitPowerBuildingMap Consumers;

	float Get(EFicsitPowerTotal Total) const { return Totals[static_cast<int32>(Total)]; }
};

/** Rate of one item, in items/min (or m³/min for fluids). */
struct FFicsitItemRate
{
	/** What is actually flowing right now. */
	float Actual = 0.f;
	/** What the machines would do at 100% productivity (UI adaptation A2). */
	float Max = 0.f;

	void Add(float InActual, float InMax)
	{
		Actual += InActual;
		Max += InMax;
	}
};

/** One item's total rate in one direction, plus the breakdown by source. */
struct FFicsitItemFlow
{
	FFicsitItemRate Total;
	FFicsitItemRate BySource[FicsitRateSourceCount];

	void Add(EFicsitRateSource Source, float Actual, float Max)
	{
		Total.Add(Actual, Max);
		BySource[static_cast<int32>(Source)].Add(Actual, Max);
	}
};

/** How many machines making (or using) an item are in each state. */
struct FFicsitMachineCounts
{
	int32 ByState[static_cast<int32>(EFicsitMachineState::Count)] = {};

	void Add(EFicsitMachineState State) { ++ByState[static_cast<int32>(State)]; }
	int32 Get(EFicsitMachineState State) const { return ByState[static_cast<int32>(State)]; }
	int32 Total() const
	{
		int32 Sum = 0;
		for (const int32 Count : ByState) { Sum += Count; }
		return Sum;
	}
};

using FFicsitItemFlowMap = TMap<TSubclassOf<UFGItemDescriptor>, FFicsitItemFlow>;
/** Recipe -> items/min of one product at 100% productivity (what the machines running that recipe are set up for). */
using FFicsitRecipeRateMap = TMap<TSubclassOf<UFGRecipe>, float>;
using FFicsitMachineCountMap = TMap<TSubclassOf<UFGItemDescriptor>, FFicsitMachineCounts>;
using FFicsitItemCountMap = TMap<TSubclassOf<UFGItemDescriptor>, int64>;

/** One item's total in storage and how fast that total is changing, in display units (m³ for fluids). */
struct FFicsitStock
{
	float Amount = 0.f;
	/** Change per minute over the last few minutes of scans. Negative while the stock is being drawn down. */
	float PerMinute = 0.f;
};
using FFicsitStockMap = TMap<TSubclassOf<UFGItemDescriptor>, FFicsitStock>;

/** All items' production and consumption at one moment. */
struct FFicsitProductionSnapshot
{
	double WorldTimeSeconds = 0.0;

	FFicsitItemFlowMap Produced;
	FFicsitItemFlowMap Consumed;

	/** Machine states, per item they produce / consume. A machine is counted once for every item it touches. */
	FFicsitMachineCountMap Producers;
	FFicsitMachineCountMap Consumers;

	/** Which recipes the factory makes each item with, from manufacturers only. Drives the supply chain tree. */
	TMap<TSubclassOf<UFGItemDescriptor>, FFicsitRecipeRateMap> RecipeOutput;

	/** Machines scanned, per source type. */
	int32 MachinesScanned[FicsitRateSourceCount] = {};

	/** Power production, consumption and storage. */
	FFicsitPowerSnapshot Power;

	/** Amounts moved by hand since the previous sample (totals, not rates), per EFicsitManualKind. */
	TMap<TSubclassOf<UFGItemDescriptor>, float> Manual[FicsitManualKindCount];

	/**
	 * What's in storage containers and fluid buffers. Dimensional Depot uploaders, belts, pipes, vehicles and machine
	 * buffers aren't counted.
	 */
	FFicsitStockMap Stored;
	/** Seconds of scans the storage trends cover. They start over when a save loads. */
	float StorageTrendSeconds = 0.f;
};

/** Display units: the game keeps fluids in liters, and the UI shows them in m³ (items stay as they are). */
namespace FicsitUnits
{
	/** Liquids and gases. */
	FICSITSTATISTICS_API bool IsFluid(TSubclassOf<UFGItemDescriptor> Item);
	/** A recipe or inventory amount in display units. */
	FICSITSTATISTICS_API float ToDisplayAmount(int32 RawAmount, TSubclassOf<UFGItemDescriptor> Item);
	/** A raw per-minute rate in display units. */
	FICSITSTATISTICS_API float ToDisplayRate(float RawPerMinute, TSubclassOf<UFGItemDescriptor> Item);
}

/**
 * Why a machine that isn't producing stopped, as seen from each item it makes or uses. The scan reads the machine's
 * state and what it is short of; these rules turn that into one state per item. Manufacturers, extractors and
 * generators all use them, so a stopped machine reads the same way everywhere. Pure: no game objects.
 *
 * Only Idle machines are explained. Running, paused and unpowered machines keep their state for every item.
 */
namespace FicsitMachineState
{
	/** An item the machine makes: starved if it is short of any input, otherwise its output is backed up. */
	inline EFicsitMachineState ForProduct(EFicsitMachineState State, bool bShortOfAnyInput)
	{
		if (State != EFicsitMachineState::Idle)
		{
			return State;
		}
		return bShortOfAnyInput ? EFicsitMachineState::Starved : EFicsitMachineState::OutputBlocked;
	}

	/**
	 * An item the machine uses: starved if it is short of this item, waiting on other inputs if it is short of another
	 * one, and otherwise its output is backed up. bOutputCanBlock is false for machines with no output that can fill
	 * (generators other than nuclear plants), which then just stay Idle.
	 */
	inline EFicsitMachineState ForInput(EFicsitMachineState State, bool bShortOfThis, bool bShortOfAnyInput, bool bOutputCanBlock = true)
	{
		if (State != EFicsitMachineState::Idle)
		{
			return State;
		}
		if (bShortOfThis)
		{
			return EFicsitMachineState::Starved;
		}
		if (bShortOfAnyInput)
		{
			return EFicsitMachineState::OtherInputs;
		}
		return bOutputCanBlock ? EFicsitMachineState::OutputBlocked : EFicsitMachineState::Idle;
	}
}
