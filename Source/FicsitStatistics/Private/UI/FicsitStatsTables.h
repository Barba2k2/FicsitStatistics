#pragma once

#include "CoreMinimal.h"
#include "FicsitBalance.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsTypes.h"
#include "UI/FicsitItemOutlook.h"

class UFGItemDescriptor;

/**
 * The stats window's tables: which rows each tab lists, in what order, and the text in their cells. No widgets: the
 * window turns rows into widgets, and these rules can be tested on their own.
 *
 * Rows that look equal never swap places: numbers sort by their displayed value (DisplayKey), and ties go A to Z by
 * name.
 */
namespace FicsitStatsTables
{
	using FItem = TSubclassOf<UFGItemDescriptor>;
	/** Whether the sidebar, the "Current milestone" filter and the search let an item through. */
	using FPasses = TFunctionRef<bool(FItem)>;
	/** An item's display name (the window caches them). */
	using FNameOf = TFunctionRef<FText(FItem)>;

	/** Value rounded the way FormatNumber displays it, so rows that look equal sort as equal (no jittering order). */
	float DisplayKey(float Value);

	// --- Production tab ----------------------------------------------------------------------------------------------

	/** One row of the Production or Consumption panel. */
	struct FRateRow
	{
		FItem Item;
		/** Average over the selected range. */
		float Rate = 0.f;
		float CurrentActual = 0.f;
		/** Max at 100% productivity, right now (A2). */
		float CurrentMax = 0.f;
		/** Rate relative to the largest row in the list. */
		float Share = 0.f;
		// Formatted once per refresh (the row's text attributes just return these, so there's no per-frame formatting).
		FText RateText;
		/** "87%" of max right now, shown only when bShowPercent (below 95%). */
		FText PercentText;
		bool bShowPercent = false;
		/** Actual vs max right now, and what the clicks do. */
		FText TooltipText;
	};

	/** The items that passed, highest average first. Live is the latest scan (for actual vs max right now). */
	TArray<FRateRow> BuildRateRows(const TMap<FItem, FFicsitItemRate>& Averages, const FFicsitItemFlowMap& Live, FPasses Passes, FNameOf NameOf);

	/**
	 * The items a graph shows unless the player toggled them: the top Count rows. Without bIncludeFluids (an option),
	 * fluids are left out while the list also has solids, since one big fluid (Water at 1,000+ m³/min) sets the scale
	 * and squashes every other line.
	 */
	TSet<UClass*> PickDefaultGraphItems(const TArray<FRateRow>& Rows, int32 Count, bool bIncludeFluids);

	// --- Balance tab -------------------------------------------------------------------------------------------------

	/** Columns the Balance list can be sorted by (click a column header, and click it again to reverse). */
	enum class EBalanceSort : uint8 { Item, Produced, Used, Sunk, Net, Depot, Storage, Count };

	struct FBalanceRow
	{
		FItem Item;
		float Produced = 0.f;
		/** Demand: everything consumed except what went into the AWESOME Sink (unless the Sink-as-demand option is on). */
		float Consumed = 0.f;
		/** Overflow into the AWESOME Sink (shown separately, but part of the surplus). */
		float Sunk = 0.f;
		float Net() const { return Produced - Consumed; }
		FicsitBalance::EState State = FicsitBalance::EState::Idle;
		/** Amount in the Dimensional Depot (0 if none). */
		int32 Depot = 0;
		FText DepotText;
		/** Amount in storage containers and fluid buffers (0 if none), and whether it runs out within the warning time. */
		float Stored = 0.f;
		bool bStorageRunsOutSoon = false;
		FText StoredText;
		FText ProducedText;
		FText ConsumedText;
		FText SunkText;
		FText NetText;
		FLinearColor NetColor = FLinearColor::White;
	};

	struct FBalanceTable
	{
		TArray<FBalanceRow> Rows;
		/** The Sunk column only shows when something listed was sunk (or it's the sort column). */
		bool bShowSunkColumn = false;
		/** The Depot column only shows when something listed is in the Dimensional Depot (or it's the sort column). */
		bool bShowDepotColumn = false;
		/** The Storage column only shows when something listed is in storage (or it's the sort column). */
		bool bShowStorageColumn = false;
	};

	/**
	 * The Balance list from every item's outlook. bOnlyDeficits keeps deficits and milestone parts nothing makes. Net
	 * sorts balanced items as 0, so ascending (the default) puts the worst deficit first.
	 */
	FBalanceTable BuildBalanceTable(const TMap<UClass*, FFicsitItemOutlook>& Outlooks, bool bOnlyDeficits, EBalanceSort Sort, bool bAscending,
		FPasses Passes, FNameOf NameOf);

	// --- Manual tab --------------------------------------------------------------------------------------------------

	/** Manual tab columns (the numeric ones follow EFicsitManualKind, offset by one). */
	enum class EManualSort : uint8 { Item, Crafted, CraftUsed, Built, Refunded, Count };

	/** One item in the Manual tab: amounts over the selected range, per EFicsitManualKind. */
	struct FManualRow
	{
		FItem Item;
		float Values[FicsitManualKindCount] = {};
		FText Texts[FicsitManualKindCount];
	};

	struct FManualTable
	{
		TArray<FManualRow> Rows;
		/** "1,234 crafted · 5,600 spent building · ..." for the listed rows (empty when there are none). */
		FText Summary;
	};

	FManualTable BuildManualTable(const TMap<FItem, FFicsitManualTotals>& Totals, EManualSort Sort, bool bAscending, FPasses Passes, FNameOf NameOf);

	// --- Power tab ---------------------------------------------------------------------------------------------------

	/** One building type in the Power tab (producers or consumers). No Item: power no scanned building accounts for. */
	struct FPowerRow
	{
		/** The building's descriptor (name and icon). */
		FItem Item;
		FText Name;
		float AvgMW = 0.f;
		float NowMW = 0.f;
		float MaxMW = 0.f;
		int32 Count = 0;
		int32 Active = 0;
		float Share = 0.f;
		FText MWText;
		FText CountText;
		FText SecondaryText;
	};

	/** One summary tile at the top of the Power tab. */
	struct FPowerTile
	{
		FText Value;
		FText Sub;
		FLinearColor SubColor = FLinearColor::White;
		/** Collapsed tiles give their width to the others (e.g. Batteries when none are built). */
		bool bHidden = false;
	};

	struct FPowerTable
	{
		static constexpr int32 NumTiles = 5;
		/** Production, consumption, capacity, max consumption, batteries. */
		FPowerTile Tiles[NumTiles];
		TArray<FPowerRow> Producers;
		TArray<FPowerRow> Consumers;
		FText ProducerSummary;
		FText ConsumerSummary;
	};

	/**
	 * The Power tab from the latest scan (Now) and the selected range's averages (Avg, per EFicsitPowerTotal, and MW per
	 * building type). Variants of one building (the HUB's built-in Biomass Burner) share a row. PassesSearch filters
	 * buildings by name, and while searching there is no "Other" row.
	 */
	FPowerTable BuildPowerTable(const FFicsitPowerSnapshot& Now, const float (&Avg)[FicsitPowerTotalCount], const TMap<FItem, float>& AvgProducers,
		const TMap<FItem, float>& AvgConsumers, bool bSearching, FPasses PassesSearch, FNameOf NameOf);
}
