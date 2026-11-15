#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsTypes.h"
#include "UI/FicsitItemOutlook.h"
#include "UI/FicsitStatsTables.h"

class FFicsitRangeReader;
class UFGItemDescriptor;

/**
 * Which items one Production tab graph shows. By default that's the top rows of its list (the window picks them each
 * refresh and passes them in as Defaults). On top of that the player can:
 * - toggle single items (LMB on a row),
 * - solo one item (Ctrl+LMB): only that item, until they toggle more back on,
 * - reset back to the defaults.
 *
 * No widgets, so the rules can be tested on their own.
 */
class FFicsitGraphSelection
{
public:
	/** Whether Item is on the graph. */
	bool IsOn(UClass* Item, const TSet<UClass*>& Defaults) const;

	/** Turn Item on if it's off, off if it's on. */
	void Toggle(UClass* Item, const TSet<UClass*>& Defaults);

	/**
	 * Show only Item. Doing it again on an item that's already shown alone (nothing toggled since) goes back to the
	 * defaults, so Ctrl+LMB twice is a round trip.
	 */
	void Solo(UClass* Item);

	/** Back to the defaults: no solo, no toggles. */
	void Reset();

	/** Whether the graph differs from the defaults (the Reset link only shows then). */
	bool IsCustomized(const TSet<UClass*>& Defaults) const;

	/** The soloed item, or null. */
	UClass* GetSolo() const { return SoloItem; }

private:
	/** What the item would show without toggles: the solo item alone, or the defaults. */
	bool IsOnBase(UClass* Item, const TSet<UClass*>& Defaults) const;

	/** Items the player toggled away from the base. */
	TMap<UClass*, bool> Overrides;
	UClass* SoloItem = nullptr;
};

/** The stats window's tabs, in the order the title strip shows them. */
enum class EFicsitStatsTab : uint8 { Production, Balance, Manual, Power };

/**
 * What the window model reads about items from the game: names, categories and Depot limits. The game's version reads
 * the item descriptors. Tests give their own, since the editor's item classes are stubs.
 */
class IFicsitItemInfo
{
public:
	virtual ~IFicsitItemInfo() = default;
	virtual FText GetName(UClass* Item) const = 0;
	/** The item's category (a UFGCategory class), or null. */
	virtual UClass* GetCategory(UClass* Item) const = 0;
	/** Where a category sits in the game's menus. Lower comes first. */
	virtual float GetCategoryPriority(UClass* Category) const = 0;
	virtual FText GetCategoryName(UClass* Category) const = 0;
	/** How much of Item the Dimensional Depot can hold (0 when it can't hold any, e.g. fluids). */
	virtual int32 GetDepotLimit(UClass* Item) const = 0;
};

/** IFicsitItemInfo from the game's item descriptors, and the Dimensional Depot of WorldContext's world. */
class FFicsitGameItemInfo : public IFicsitItemInfo
{
public:
	explicit FFicsitGameItemInfo(TWeakObjectPtr<UObject> InWorldContext) : WorldContext(InWorldContext) {}

	virtual FText GetName(UClass* Item) const override;
	virtual UClass* GetCategory(UClass* Item) const override;
	virtual float GetCategoryPriority(UClass* Category) const override;
	virtual FText GetCategoryName(UClass* Category) const override;
	virtual int32 GetDepotLimit(UClass* Item) const override;

private:
	TWeakObjectPtr<UObject> WorldContext;
};

/** The item in the details pane, as of the last refresh. */
struct FFicsitWindowDetails
{
	/** Null when no item is selected. */
	TSubclassOf<UFGItemDescriptor> Item;
	/** Right now, from the latest scan. */
	FFicsitItemFlow Produced;
	FFicsitItemFlow Consumed;
	bool bHasProduced = false;
	bool bHasConsumed = false;
	FFicsitMachineCounts Producers;
	FFicsitMachineCounts Consumers;
	/** Crafted, used crafting, spent building and refunded over the selected range. */
	FFicsitManualTotals Manual;
	/** Over the selected range: the same outlook the lists show, so the pane and the rows agree. */
	FFicsitItemOutlook Outlook;
	bool bSinkIsDemand = false;
	int32 DepotLimit = 0;
	/** Whether the pane has a Dimensional Depot section: something is stored, or the Depot could hold it. */
	bool bShowDepot = false;
	/** Whether the pane has a storage section: some of it is in storage containers or fluid buffers. */
	bool bShowStorage = false;

	/** Demand right now: consumption minus Sink overflow, unless the Sink counts as demand. */
	float DemandNow() const;
	float NetNow() const { return Produced.Total.Actual - DemandNow(); }
	bool HasManual() const;
	/** Changes only when the pane's layout has to: another item, or a section or source showing up or going away. */
	FString LayoutKey() const;

	/** Whether a flow has any of Source, right now or at max. */
	static bool HasSource(const FFicsitItemFlow& Flow, int32 Source);
};

/**
 * Everything the stats window shows, without the widgets: which tab, range, category, search and details item the
 * player picked, each tab's rows, the sidebar's categories, the graphs' items and colors, and the details pane's values.
 *
 * The window builds one time range reader per refresh and passes it to Refresh. After that it only turns the model's
 * state into widgets. Commands (SelectTab, ShowDetails, ...) change what the next Refresh builds, so call Refresh after them.
 */
class FFicsitStatsWindowModel
{
public:
	using FItem = TSubclassOf<UFGItemDescriptor>;
	using EBalanceSort = FicsitStatsTables::EBalanceSort;
	using EManualSort = FicsitStatsTables::EManualSort;

	explicit FFicsitStatsWindowModel(TSharedRef<const IFicsitItemInfo> InItemInfo);

	/** Settings and game data a refresh reads besides the range. */
	struct FRefreshInputs
	{
		/** Milestone needs, Depot stock and the Sink-as-demand setting. */
		FFicsitOutlookContext Context;
		/** Every item the saved history has recorded. Idle ones still get a Balance row. */
		TArray<FItem> RecordedItems;
		/** How many top rows each graph shows by default, and whether fluids count among them. */
		int32 DefaultGraphItems = 0;
		bool bGraphFluids = true;
	};

	/** Rebuild every tab's rows, the sidebar and the details from one range and the latest scan. */
	void Refresh(const FFicsitRangeReader& Range, const FFicsitProductionSnapshot& LatestScan, FRefreshInputs Inputs);

	// --- Commands ----------------------------------------------------------------------------------------------------

	/** A tab click also leaves an open supply chain. */
	void SelectTab(EFicsitStatsTab Tab);
	void SetTimeRange(EFicsitTimeRange Range) { TimeRange = Range; }
	/** A sidebar category (null for All). Turns the "Current milestone" filter off. */
	void SelectCategory(UClass* Category);
	/** The "Current milestone" pseudo-category. It turns itself off once nothing is needed. */
	void SelectMilestoneFilter();
	void SetSearch(const FString& Text) { SearchText = Text.TrimStartAndEnd(); }
	void ToggleOnlyDeficits() { bOnlyDeficits = !bOnlyDeficits; }
	/** Show Item in the details pane, which lives on the Balance tab. Leaves an open supply chain. */
	void ShowDetails(FItem Item);
	void OpenSupplyChain() { bSupplyChainOpen = true; }
	void CloseSupplyChain() { bSupplyChainOpen = false; }

	/**
	 * Click on a Balance column header: the same column again flips the direction. A new column starts names A to Z,
	 * Net worst deficit first, and amounts largest first.
	 */
	void ClickBalanceSort(EBalanceSort Column);
	/** Click on a Manual column header: names start A to Z, amounts largest first. */
	void ClickManualSort(EManualSort Column);
	/** Restore sorts saved in an earlier session. Columns out of range fall back to the defaults. */
	void RestoreSorts(int32 BalanceColumn, bool bBalanceAscending, int32 ManualColumn, bool bManualAscending);

	// --- Graphs ------------------------------------------------------------------------------------------------------

	bool IsOnGraph(FItem Item, bool bProduction) const;
	void ToggleOnGraph(FItem Item, bool bProduction);
	/** Ctrl+LMB: only Item on that graph (again: back to the defaults). */
	void SoloOnGraph(FItem Item, bool bProduction);
	void ResetGraph(bool bProduction);
	bool IsGraphCustomized(bool bProduction) const;
	const FFicsitGraphSelection& GetGraph(bool bProduction) const { return bProduction ? GraphProduced : GraphConsumed; }
	/** Index into the series colors, given out in the order items first ask, so an item keeps its color while the window is open. */
	int32 GetSeriesColorIndex(FItem Item) const;

	// --- State -------------------------------------------------------------------------------------------------------

	EFicsitStatsTab GetTab() const { return Tab; }
	EFicsitTimeRange GetTimeRange() const { return TimeRange; }
	UClass* GetSelectedCategory() const { return SelectedCategory; }
	bool IsMilestoneFilterOn() const { return bMilestoneFilter; }
	bool IsOnlyDeficits() const { return bOnlyDeficits; }
	bool IsSupplyChainOpen() const { return bSupplyChainOpen; }
	EBalanceSort GetBalanceSort() const { return BalanceSort; }
	bool IsBalanceAscending() const { return bBalanceAscending; }
	EManualSort GetManualSort() const { return ManualSort; }
	bool IsManualAscending() const { return bManualAscending; }

	// --- What the last refresh built ---------------------------------------------------------------------------------

	const TArray<FicsitStatsTables::FRateRow>& GetRateRows(bool bProduction) const { return bProduction ? ProducedRows : ConsumedRows; }
	const FicsitStatsTables::FBalanceTable& GetBalance() const { return Balance; }
	const FicsitStatsTables::FManualTable& GetManual() const { return Manual; }
	const FicsitStatsTables::FPowerTable& GetPower() const { return Power; }
	/** Sidebar categories present in the data, in the game's menu order (then by name). */
	const TArray<TPair<UClass*, FText>>& GetCategories() const { return Categories; }
	/** Whether the sidebar offers "Current milestone": something is needed right now. */
	bool HasMilestoneNeeds() const { return Context.Needs.Combined.Num() > 0; }
	const FFicsitOutlookContext& GetContext() const { return Context; }
	/** The outlook of an item over the selected range, or an empty one if it has none. */
	const FFicsitItemOutlook& GetOutlook(UClass* Item) const;
	const FFicsitWindowDetails& GetDetails() const { return Details; }

	/** Item names, looked up once (sorting and the search read them every refresh). */
	FText GetItemName(FItem Item) const;

private:
	/** Whether the sidebar, the "Current milestone" filter and the search let Item through. */
	bool PassesFilter(FItem Item) const;
	bool PassesSearch(FItem Item) const;
	void BuildDetails(const FFicsitProductionSnapshot& LatestScan, const class FFicsitItemOutlooks& ItemOutlooks);

	TSharedRef<const IFicsitItemInfo> ItemInfo;

	EFicsitStatsTab Tab = EFicsitStatsTab::Production;
	EFicsitTimeRange TimeRange = EFicsitTimeRange::TenMinutes;
	/** Null: all categories. */
	UClass* SelectedCategory = nullptr;
	bool bMilestoneFilter = false;
	FString SearchText;
	bool bOnlyDeficits = false;
	bool bSupplyChainOpen = false;
	FItem DetailItem;

	EBalanceSort BalanceSort = EBalanceSort::Net;
	bool bBalanceAscending = true;
	EManualSort ManualSort = EManualSort::Built;
	bool bManualAscending = false;

	/** What the player toggled, soloed or reset on each graph, on top of the default top rows. */
	FFicsitGraphSelection GraphProduced;
	FFicsitGraphSelection GraphConsumed;
	TSet<UClass*> DefaultOnGraphProduced;
	TSet<UClass*> DefaultOnGraphConsumed;
	mutable TMap<UClass*, int32> SeriesColorIndex;

	FFicsitOutlookContext Context;
	TArray<FicsitStatsTables::FRateRow> ProducedRows;
	TArray<FicsitStatsTables::FRateRow> ConsumedRows;
	FicsitStatsTables::FBalanceTable Balance;
	FicsitStatsTables::FManualTable Manual;
	FicsitStatsTables::FPowerTable Power;
	TArray<TPair<UClass*, FText>> Categories;
	/** Item -> outlook over the selected range, for every item the Balance tab could list. */
	TMap<UClass*, FFicsitItemOutlook> Outlooks;
	/** Manual amounts over the selected range, for every item. */
	TMap<UClass*, FFicsitManualTotals> ManualByItem;
	FFicsitWindowDetails Details;

	mutable TMap<UClass*, FText> NameCache;
};
