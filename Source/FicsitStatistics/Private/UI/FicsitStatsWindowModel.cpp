#include "UI/FicsitStatsWindowModel.h"

#include "FGCategory.h"
#include "FGCentralStorageSubsystem.h"
#include "FicsitBalance.h"
#include "Resources/FGItemDescriptor.h"
#include "UI/FicsitRangeReader.h"
#include "UI/FicsitStatsStyle.h"

// ---------------------------------------------------------------------------------------------------------------------
// The game's item info

FText FFicsitGameItemInfo::GetName(UClass* Item) const
{
	return UFGItemDescriptor::GetItemName(Item);
}

UClass* FFicsitGameItemInfo::GetCategory(UClass* Item) const
{
	return UFGItemDescriptor::GetCategory(Item).Get();
}

float FFicsitGameItemInfo::GetCategoryPriority(UClass* Category) const
{
	const TSubclassOf<UFGCategory> Of = Category;
	return Of ? Of.GetDefaultObject()->GetMenuPriorityFromInstance() : 0.f;
}

FText FFicsitGameItemInfo::GetCategoryName(UClass* Category) const
{
	return UFGCategory::GetCategoryName(Category);
}

int32 FFicsitGameItemInfo::GetDepotLimit(UClass* Item) const
{
	if (!Item || FicsitUnits::IsFluid(Item))
	{
		return 0;
	}
	AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(WorldContext.Get());
	return Depot ? Depot->GetCentralStorageItemLimit(Item) : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Details

bool FFicsitWindowDetails::HasSource(const FFicsitItemFlow& Flow, int32 Source)
{
	return Flow.BySource[Source].Max > 0.f || Flow.BySource[Source].Actual > 0.f;
}

float FFicsitWindowDetails::DemandNow() const
{
	return FicsitBalance::Demand(Consumed.Total.Actual, Consumed.BySource[static_cast<int32>(EFicsitRateSource::ResourceSink)].Actual, bSinkIsDemand);
}

bool FFicsitWindowDetails::HasManual() const
{
	for (const float Value : Manual.Values)
	{
		if (Value > 0.f)
		{
			return true;
		}
	}
	return false;
}

FString FFicsitWindowDetails::LayoutKey() const
{
	FString Key = Item ? Item->GetPathName() : FString(TEXT("none"));
	Key += FString::Printf(TEXT("|%d%d%d%d%d%d|%d%d"), bHasProduced, bHasConsumed, bShowDepot, bShowStorage, Outlook.ElevatorRemaining > 0,
		Outlook.HubRemaining > 0, Producers.Total() > 0, Consumers.Total() > 0);
	for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
	{
		Key += FString::Printf(TEXT("%d%d"), HasSource(Produced, Source), HasSource(Consumed, Source));
	}
	Key += TEXT("|");
	for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
	{
		Key += Manual.Values[Kind] > 0.f ? TEXT("1") : TEXT("0");
	}
	return Key;
}

// ---------------------------------------------------------------------------------------------------------------------
// The model

FFicsitStatsWindowModel::FFicsitStatsWindowModel(TSharedRef<const IFicsitItemInfo> InItemInfo)
	: ItemInfo(MoveTemp(InItemInfo))
{
}

void FFicsitStatsWindowModel::Refresh(const FFicsitRangeReader& Range, const FFicsitProductionSnapshot& LatestScan, FRefreshInputs Inputs)
{
	Context = MoveTemp(Inputs.Context);
	const TMap<UClass*, int32>& MilestoneRemaining = Context.Needs.Combined;
	if (MilestoneRemaining.Num() == 0)
	{
		bMilestoneFilter = false;
	}

	const FFicsitItemOutlooks ItemOutlooks(Range, LatestScan, Context);

	// Copies: milestone parts nothing makes are added below.
	TMap<FItem, FFicsitItemRate> AvgProduced = Range.GetProduced();
	TMap<FItem, FFicsitItemRate> AvgConsumed = Range.GetConsumed();

	// Categories present in the data (sidebar).
	TMap<UClass*, float> CategoryPriority;
	auto NoteCategory = [this, &CategoryPriority](FItem Item)
	{
		if (UClass* Category = ItemInfo->GetCategory(Item.Get()))
		{
			CategoryPriority.Add(Category, ItemInfo->GetCategoryPriority(Category));
		}
	};

	// Milestone parts nobody is making yet still belong in the milestone view (at 0 /min): that's the point of it.
	if (bMilestoneFilter)
	{
		for (const auto& Pair : MilestoneRemaining)
		{
			AvgProduced.FindOrAdd(Pair.Key);
			AvgConsumed.FindOrAdd(Pair.Key);
		}
	}

	const auto Passes = [this](FItem Item) { return PassesFilter(Item); };
	const auto NameOf = [this](FItem Item) { return GetItemName(Item); };

	// Production and Consumption panels, and the lines each graph shows by default.
	for (const auto& Pair : AvgProduced) { NoteCategory(Pair.Key); }
	for (const auto& Pair : AvgConsumed) { NoteCategory(Pair.Key); }
	ProducedRows = FicsitStatsTables::BuildRateRows(AvgProduced, LatestScan.Produced, Passes, NameOf);
	ConsumedRows = FicsitStatsTables::BuildRateRows(AvgConsumed, LatestScan.Consumed, Passes, NameOf);
	DefaultOnGraphProduced = FicsitStatsTables::PickDefaultGraphItems(ProducedRows, Inputs.DefaultGraphItems, Inputs.bGraphFluids);
	DefaultOnGraphConsumed = FicsitStatsTables::PickDefaultGraphItems(ConsumedRows, Inputs.DefaultGraphItems, Inputs.bGraphFluids);

	// Balance: union of both sides, plus items with no flow right now: anything stored in the Dimensional Depot or in
	// storage, and anything the mod has ever recorded. Those show as idle, so stockpiles from before the mod was
	// installed (or that nothing makes any more) are still listed.
	TSet<FItem> AllItems;
	for (const auto& Pair : AvgProduced) { AllItems.Add(Pair.Key); }
	for (const auto& Pair : AvgConsumed) { AllItems.Add(Pair.Key); }
	for (const auto& Pair : MilestoneRemaining) { AllItems.Add(Pair.Key); }
	for (const auto& Pair : Context.Depot) { AllItems.Add(Pair.Key); NoteCategory(Pair.Key); }
	for (const auto& Pair : LatestScan.Stored) { AllItems.Add(Pair.Key); NoteCategory(Pair.Key); }
	for (const FItem& Item : Inputs.RecordedItems) { if (Item) { AllItems.Add(Item); NoteCategory(Item); } }
	Outlooks.Reset();
	for (const FItem& Item : AllItems)
	{
		Outlooks.Add(Item.Get(), ItemOutlooks.Get(Item));
	}
	Balance = FicsitStatsTables::BuildBalanceTable(Outlooks, bOnlyDeficits, BalanceSort, bBalanceAscending, Passes, NameOf);

	// Manual: hand-crafting, building and dismantling over the selected range (totals, not rates).
	TMap<FItem, FFicsitManualTotals> Totals;
	Range.GetManualTotals(Totals);
	ManualByItem.Reset();
	for (const auto& Pair : Totals)
	{
		ManualByItem.Add(Pair.Key.Get(), Pair.Value);
		NoteCategory(Pair.Key);
	}
	Manual = FicsitStatsTables::BuildManualTable(Totals, ManualSort, bManualAscending, Passes, NameOf);

	// Power: averages over the selected range (the latest scan until the range has data). The tab has no sidebar, so
	// only the search filters it.
	{
		float Avg[FicsitPowerTotalCount];
		TMap<FItem, float> AvgProducers, AvgConsumers;
		Range.GetPowerAverages(Avg, AvgProducers, AvgConsumers);
		Power = FicsitStatsTables::BuildPowerTable(LatestScan.Power, Avg, AvgProducers, AvgConsumers, !SearchText.IsEmpty(),
			[this](FItem Building) { return PassesSearch(Building); }, NameOf);
	}

	Categories.Reset();
	for (const auto& Pair : CategoryPriority)
	{
		Categories.Emplace(Pair.Key, ItemInfo->GetCategoryName(Pair.Key));
	}
	Categories.Sort([&CategoryPriority](const TPair<UClass*, FText>& A, const TPair<UClass*, FText>& B)
	{
		const float PA = CategoryPriority[A.Key];
		const float PB = CategoryPriority[B.Key];
		return PA != PB ? PA < PB : A.Value.CompareTo(B.Value) < 0;
	});

	BuildDetails(LatestScan, ItemOutlooks);
}

void FFicsitStatsWindowModel::BuildDetails(const FFicsitProductionSnapshot& LatestScan, const FFicsitItemOutlooks& ItemOutlooks)
{
	Details = FFicsitWindowDetails();
	const FItem Item = DetailItem;
	Details.Item = Item;
	if (!Item)
	{
		return;
	}
	if (const FFicsitItemFlow* P = LatestScan.Produced.Find(Item)) { Details.Produced = *P; Details.bHasProduced = true; }
	if (const FFicsitItemFlow* C = LatestScan.Consumed.Find(Item)) { Details.Consumed = *C; Details.bHasConsumed = true; }
	if (const FFicsitMachineCounts* M = LatestScan.Producers.Find(Item)) { Details.Producers = *M; }
	if (const FFicsitMachineCounts* M = LatestScan.Consumers.Find(Item)) { Details.Consumers = *M; }
	if (const FFicsitManualTotals* Totals = ManualByItem.Find(Item.Get())) { Details.Manual = *Totals; }
	// Items the lists don't have (e.g. one opened from the supply chain) still get their outlook.
	const FFicsitItemOutlook* Listed = Outlooks.Find(Item.Get());
	Details.Outlook = Listed ? *Listed : ItemOutlooks.Get(Item);
	Details.bSinkIsDemand = Context.bSinkIsDemand;
	Details.DepotLimit = ItemInfo->GetDepotLimit(Item.Get());
	Details.bShowDepot = Details.Outlook.Depot > 0 || Details.DepotLimit > 0;
	Details.bShowStorage = Details.Outlook.Stored > 0.f;
}

// --- Commands --------------------------------------------------------------------------------------------------------

void FFicsitStatsWindowModel::SelectTab(EFicsitStatsTab InTab)
{
	Tab = InTab;
	bSupplyChainOpen = false;
}

void FFicsitStatsWindowModel::SelectCategory(UClass* Category)
{
	SelectedCategory = Category;
	bMilestoneFilter = false;
}

void FFicsitStatsWindowModel::SelectMilestoneFilter()
{
	bMilestoneFilter = true;
	SelectedCategory = nullptr;
}

void FFicsitStatsWindowModel::ShowDetails(FItem Item)
{
	DetailItem = Item;
	Tab = EFicsitStatsTab::Balance;
	bSupplyChainOpen = false;
}

void FFicsitStatsWindowModel::ClickBalanceSort(EBalanceSort Column)
{
	if (Column == BalanceSort)
	{
		bBalanceAscending = !bBalanceAscending;
		return;
	}
	BalanceSort = Column;
	bBalanceAscending = Column == EBalanceSort::Item || Column == EBalanceSort::Net;
}

void FFicsitStatsWindowModel::ClickManualSort(EManualSort Column)
{
	if (Column == ManualSort)
	{
		bManualAscending = !bManualAscending;
		return;
	}
	ManualSort = Column;
	bManualAscending = Column == EManualSort::Item;
}

void FFicsitStatsWindowModel::RestoreSorts(int32 BalanceColumn, bool bBalanceAscendingIn, int32 ManualColumn, bool bManualAscendingIn)
{
	if (BalanceColumn >= 0 && BalanceColumn < static_cast<int32>(EBalanceSort::Count))
	{
		BalanceSort = static_cast<EBalanceSort>(BalanceColumn);
		bBalanceAscending = bBalanceAscendingIn;
	}
	else
	{
		BalanceSort = EBalanceSort::Net;
		bBalanceAscending = true;
	}
	if (ManualColumn >= 0 && ManualColumn < static_cast<int32>(EManualSort::Count))
	{
		ManualSort = static_cast<EManualSort>(ManualColumn);
		bManualAscending = bManualAscendingIn;
	}
	else
	{
		ManualSort = EManualSort::Built;
		bManualAscending = false;
	}
}

// --- Graphs ----------------------------------------------------------------------------------------------------------

bool FFicsitStatsWindowModel::IsOnGraph(FItem Item, bool bProduction) const
{
	// Defaults: the top N rows of the (filtered, sorted) list, picked in Refresh.
	return bProduction ? GraphProduced.IsOn(Item.Get(), DefaultOnGraphProduced) : GraphConsumed.IsOn(Item.Get(), DefaultOnGraphConsumed);
}

void FFicsitStatsWindowModel::ToggleOnGraph(FItem Item, bool bProduction)
{
	if (bProduction)
	{
		GraphProduced.Toggle(Item.Get(), DefaultOnGraphProduced);
	}
	else
	{
		GraphConsumed.Toggle(Item.Get(), DefaultOnGraphConsumed);
	}
}

void FFicsitStatsWindowModel::SoloOnGraph(FItem Item, bool bProduction)
{
	(bProduction ? GraphProduced : GraphConsumed).Solo(Item.Get());
}

void FFicsitStatsWindowModel::ResetGraph(bool bProduction)
{
	(bProduction ? GraphProduced : GraphConsumed).Reset();
}

bool FFicsitStatsWindowModel::IsGraphCustomized(bool bProduction) const
{
	return bProduction ? GraphProduced.IsCustomized(DefaultOnGraphProduced) : GraphConsumed.IsCustomized(DefaultOnGraphConsumed);
}

int32 FFicsitStatsWindowModel::GetSeriesColorIndex(FItem Item) const
{
	if (const int32* Index = SeriesColorIndex.Find(Item.Get()))
	{
		return *Index;
	}
	return SeriesColorIndex.Add(Item.Get(), SeriesColorIndex.Num());
}

// --- Lookups ---------------------------------------------------------------------------------------------------------

const FFicsitItemOutlook& FFicsitStatsWindowModel::GetOutlook(UClass* Item) const
{
	static const FFicsitItemOutlook None;
	const FFicsitItemOutlook* Outlook = Outlooks.Find(Item);
	return Outlook ? *Outlook : None;
}

FText FFicsitStatsWindowModel::GetItemName(FItem Item) const
{
	if (const FText* Cached = NameCache.Find(Item.Get()))
	{
		return *Cached;
	}
	return NameCache.Add(Item.Get(), ItemInfo->GetName(Item.Get()));
}

bool FFicsitStatsWindowModel::PassesFilter(FItem Item) const
{
	if (!Item)
	{
		return false;
	}
	if (bMilestoneFilter && !Context.Needs.Combined.Contains(Item.Get()))
	{
		return false;
	}
	if (SelectedCategory && ItemInfo->GetCategory(Item.Get()) != SelectedCategory)
	{
		return false;
	}
	return PassesSearch(Item);
}

bool FFicsitStatsWindowModel::PassesSearch(FItem Item) const
{
	return SearchText.IsEmpty() || GetItemName(Item).ToString().Contains(SearchText, ESearchCase::IgnoreCase);
}

// ---------------------------------------------------------------------------------------------------------------------
// Graph selection

bool FFicsitGraphSelection::IsOnBase(UClass* Item, const TSet<UClass*>& Defaults) const
{
	return SoloItem ? Item == SoloItem : Defaults.Contains(Item);
}

bool FFicsitGraphSelection::IsOn(UClass* Item, const TSet<UClass*>& Defaults) const
{
	if (const bool* Override = Overrides.Find(Item))
	{
		return *Override;
	}
	return IsOnBase(Item, Defaults);
}

void FFicsitGraphSelection::Toggle(UClass* Item, const TSet<UClass*>& Defaults)
{
	const bool bOn = !IsOn(Item, Defaults);
	// Toggling back to what the base shows drops the override, so the item follows the defaults again.
	if (bOn == IsOnBase(Item, Defaults))
	{
		Overrides.Remove(Item);
	}
	else
	{
		Overrides.Add(Item, bOn);
	}
}

void FFicsitGraphSelection::Solo(UClass* Item)
{
	const bool bAlreadyAlone = SoloItem == Item && Overrides.Num() == 0;
	Reset();
	if (!bAlreadyAlone)
	{
		SoloItem = Item;
	}
}

void FFicsitGraphSelection::Reset()
{
	Overrides.Reset();
	SoloItem = nullptr;
}

bool FFicsitGraphSelection::IsCustomized(const TSet<UClass*>& Defaults) const
{
	if (SoloItem)
	{
		return true;
	}
	// The defaults move as rates change, so an item toggled on may since have become a default anyway.
	for (const TPair<UClass*, bool>& Pair : Overrides)
	{
		if (Pair.Value != Defaults.Contains(Pair.Key))
		{
			return true;
		}
	}
	return false;
}
