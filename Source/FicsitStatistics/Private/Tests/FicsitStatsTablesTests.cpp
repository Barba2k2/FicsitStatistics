#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "UI/FicsitStatsStyle.h"
#include "UI/FicsitStatsTables.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitStatsTablesTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	using FItem = FicsitStatsTables::FItem;

	// Named so that A to Z is Consumable, Equipment, Item, Resource.
	UClass* Consumable() { return UFGConsumableDescriptor::StaticClass(); }
	UClass* Equipment() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* Item() { return UFGItemDescriptor::StaticClass(); }
	UClass* Resource() { return UFGResourceDescriptor::StaticClass(); }

	FText NameOf(FItem Of) { return FText::FromString(GetNameSafe(Of.Get())); }
	bool PassAll(FItem) { return true; }

	/** An outlook made at Produced, used at Demand (and Sunk on top), as the item outlook module would read it. */
	FFicsitItemOutlook Outlook(float Produced, float Demand, float Sunk = 0.f)
	{
		FFicsitItemOutlook Result;
		Result.Produced = Produced;
		Result.Demand = Demand;
		Result.Sunk = Sunk;
		Result.Consumed = Demand + Sunk;
		Result.State = FicsitBalance::GetState(Produced, Demand, Sunk);
		return Result;
	}

	template <typename RowType>
	TArray<UClass*> Order(const TArray<RowType>& Rows)
	{
		TArray<UClass*> Result;
		for (const RowType& Row : Rows) { Result.Add(Row.Item.Get()); }
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitTablesRateRowsTest, "FicsitStatistics.Tables.RateRowsSortByShownValueThenName", FicsitStatsTablesTests::Flags)
bool FFicsitTablesRateRowsTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsTablesTests;
	TMap<FItem, FFicsitItemRate> Averages;
	Averages.Add(Equipment()).Actual = 30.f;
	Averages.Add(Consumable()).Actual = 30.004f;
	Averages.Add(Item()).Actual = 10.f;
	Averages.Add(Resource()).Actual = 99.f;
	FFicsitItemFlowMap Live;
	Live.Add(Equipment()).Add(EFicsitRateSource::Manufacturing, 30.f, 40.f);

	const TArray<FicsitStatsTables::FRateRow> Rows = FicsitStatsTables::BuildRateRows(Averages, Live, [](FItem Of) { return Of != Resource(); }, NameOf);
	TestEqual(TEXT("filtered, then 30.00 and 30.00 by name, then 10"), Order(Rows), TArray<UClass*>{Consumable(), Equipment(), Item()});
	TestEqual(TEXT("the top row is the full share"), Rows[0].Share, 1.f);
	TestEqual(TEXT("shares follow the top row"), Rows[2].Share, 10.f / 30.004f, 0.0001f);
	TestTrue(TEXT("running at 75% of max shows the percent"), Rows[1].bShowPercent);
	TestEqual(TEXT("the percent"), Rows[1].PercentText.ToString(), FString(TEXT("75%")));
	TestFalse(TEXT("no max, no percent"), Rows[0].bShowPercent);

	const TSet<UClass*> Defaults = FicsitStatsTables::PickDefaultGraphItems(Rows, 2, true);
	TestEqual(TEXT("the top 2 go on the graph"), Defaults.Num(), 2);
	TestTrue(TEXT("top row"), Defaults.Contains(Consumable()));
	TestFalse(TEXT("third row"), Defaults.Contains(Item()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitTablesBalanceTest, "FicsitStatistics.Tables.BalancePutsTheWorstDeficitFirst", FicsitStatsTablesTests::Flags)
bool FFicsitTablesBalanceTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsTablesTests;
	using FicsitStatsTables::EBalanceSort;
	TMap<UClass*, FFicsitItemOutlook> Outlooks;
	Outlooks.Add(Equipment(), Outlook(10.f, 40.f));
	Outlooks.Add(Consumable(), Outlook(50.f, 20.f, 5.f));
	Outlooks.Add(Item(), Outlook(20.f, 20.f));
	FFicsitItemOutlook Unmade;
	Unmade.ElevatorRemaining = 100;
	Outlooks.Add(Resource(), Unmade);

	const FicsitStatsTables::FBalanceTable Net = FicsitStatsTables::BuildBalanceTable(Outlooks, false, EBalanceSort::Net, true, PassAll, NameOf);
	TestEqual(TEXT("deficit, then balanced and idle (both 0) by name, then surplus"), Order(Net.Rows),
		TArray<UClass*>{Equipment(), Item(), Resource(), Consumable()});
	TestEqual(TEXT("idle shows a dash"), Net.Rows[2].NetText.ToString(), FString(TEXT("—")));
	TestEqual(TEXT("balanced shows ± 0"), Net.Rows[1].NetText.ToString(), FString(TEXT("± 0")));
	TestTrue(TEXT("something was sunk: Sunk column"), Net.bShowSunkColumn);
	TestFalse(TEXT("nothing in the Depot: no Depot column"), Net.bShowDepotColumn);

	const FicsitStatsTables::FBalanceTable Deficits = FicsitStatsTables::BuildBalanceTable(Outlooks, true, EBalanceSort::Net, true, PassAll, NameOf);
	TestEqual(TEXT("only deficits keeps the unmade milestone part"), Order(Deficits.Rows), TArray<UClass*>{Equipment(), Resource()});
	TestFalse(TEXT("nothing listed was sunk"), Deficits.bShowSunkColumn);

	const FicsitStatsTables::FBalanceTable ByName = FicsitStatsTables::BuildBalanceTable(Outlooks, false, EBalanceSort::Item, false, PassAll, NameOf);
	TestEqual(TEXT("by name, Z to A"), Order(ByName.Rows), TArray<UClass*>{Resource(), Item(), Equipment(), Consumable()});

	const FicsitStatsTables::FBalanceTable ByDepot = FicsitStatsTables::BuildBalanceTable(Outlooks, false, EBalanceSort::Depot, false, PassAll, NameOf);
	TestTrue(TEXT("the sort column always shows"), ByDepot.bShowDepotColumn);
	TestEqual(TEXT("numeric ties still read A to Z"), Order(ByDepot.Rows), TArray<UClass*>{Consumable(), Equipment(), Item(), Resource()});
	TestFalse(TEXT("nothing in storage: no Storage column"), ByDepot.bShowStorageColumn);

	Outlooks[Item()].Stored = 500.f;
	Outlooks[Item()].bStorageRunsOutSoon = true;
	Outlooks[Equipment()].Stored = 20.f;
	const FicsitStatsTables::FBalanceTable ByStorage = FicsitStatsTables::BuildBalanceTable(Outlooks, false, EBalanceSort::Storage, false, PassAll, NameOf);
	TestTrue(TEXT("something in storage: Storage column"), ByStorage.bShowStorageColumn);
	TestEqual(TEXT("most stored first"), Order(ByStorage.Rows), TArray<UClass*>{Item(), Equipment(), Consumable(), Resource()});
	TestEqual(TEXT("stored amount"), ByStorage.Rows[0].StoredText.ToString(), FString(TEXT("500")));
	TestTrue(TEXT("the runout warning reaches the row"), ByStorage.Rows[0].bStorageRunsOutSoon);
	TestEqual(TEXT("nothing stored shows a dash"), ByStorage.Rows[2].StoredText.ToString(), FString(TEXT("—")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitTablesManualTest, "FicsitStatistics.Tables.ManualSortsByTheChosenAmount", FicsitStatsTablesTests::Flags)
bool FFicsitTablesManualTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsTablesTests;
	using FicsitStatsTables::EManualSort;
	TMap<FItem, FFicsitManualTotals> Totals;
	Totals.Add(Equipment()).Values[static_cast<int32>(EFicsitManualKind::Built)] = 5.f;
	Totals.Add(Consumable()).Values[static_cast<int32>(EFicsitManualKind::Built)] = 50.f;
	Totals.Add(Item()).Values[static_cast<int32>(EFicsitManualKind::Crafted)] = 7.f;

	const FicsitStatsTables::FManualTable Table = FicsitStatsTables::BuildManualTable(Totals, EManualSort::Built, false, PassAll, NameOf);
	TestEqual(TEXT("most spent building first"), Order(Table.Rows), TArray<UClass*>{Consumable(), Equipment(), Item()});
	TestEqual(TEXT("zero shows a dash"), Table.Rows[2].Texts[static_cast<int32>(EFicsitManualKind::Built)].ToString(), FString(TEXT("—")));
	TestFalse(TEXT("a summary"), Table.Summary.IsEmpty());

	const FicsitStatsTables::FManualTable None = FicsitStatsTables::BuildManualTable(Totals, EManualSort::Built, false, [](FItem) { return false; }, NameOf);
	TestEqual(TEXT("nothing passes"), None.Rows.Num(), 0);
	TestTrue(TEXT("no rows, no summary"), None.Summary.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitTablesPowerTest, "FicsitStatistics.Tables.PowerMergesVariantsAndShowsOtherAndBatteries", FicsitStatsTablesTests::Flags)
bool FFicsitTablesPowerTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsTablesTests;
	FFicsitPowerSnapshot Now;
	auto Building = [](float MW, int32 Count) { FFicsitPowerBuilding Entry; Entry.MW = MW; Entry.MaxMW = MW * 2.f; Entry.Count = Count; Entry.Active = Count; return Entry; };
	// Two variants of one building (same name), and one other.
	Now.Producers.Add(Equipment(), Building(20.f, 2));
	Now.Producers.Add(Consumable(), Building(10.f, 1));
	Now.Consumers.Add(Item(), Building(25.f, 5));
	Now.Totals[static_cast<int32>(EFicsitPowerTotal::OtherConsumed)] = 4.f;
	Now.Totals[static_cast<int32>(EFicsitPowerTotal::BatteryCapacity)] = 100.f;
	Now.Totals[static_cast<int32>(EFicsitPowerTotal::BatteryStored)] = 50.f;
	Now.Totals[static_cast<int32>(EFicsitPowerTotal::BatteryOutput)] = 10.f;
	float Avg[FicsitPowerTotalCount] = {};
	TMap<FItem, float> AvgProducers, AvgConsumers;
	AvgProducers.Add(Equipment(), 20.f);
	AvgProducers.Add(Consumable(), 10.f);
	AvgConsumers.Add(Item(), 25.f);
	auto BurnerName = [](FItem Of) { return Of == Item() ? INVTEXT("Constructor") : INVTEXT("Biomass Burner"); };

	const FicsitStatsTables::FPowerTable Table = FicsitStatsTables::BuildPowerTable(Now, Avg, AvgProducers, AvgConsumers, false, PassAll, BurnerName);
	TestEqual(TEXT("variants share a row"), Table.Producers.Num(), 1);
	TestEqual(TEXT("their average adds up"), Table.Producers[0].AvgMW, 30.f);
	TestEqual(TEXT("and their count"), Table.Producers[0].Count, 3);
	TestEqual(TEXT("consumers plus Other"), Table.Consumers.Num(), 2);
	TestTrue(TEXT("Other has no building"), Table.Consumers.Num() == 2 && !Table.Consumers[1].Item);
	TestFalse(TEXT("batteries are shown"), Table.Tiles[4].bHidden);
	TestEqual(TEXT("draining is a deficit"), Table.Tiles[4].SubColor, FicsitStatsStyle::Deficit());

	const FicsitStatsTables::FPowerTable Searching = FicsitStatsTables::BuildPowerTable(Now, Avg, AvgProducers, AvgConsumers, true, PassAll, BurnerName);
	TestEqual(TEXT("no Other row while searching"), Searching.Consumers.Num(), 1);

	FFicsitPowerSnapshot NoBatteries;
	const FicsitStatsTables::FPowerTable Empty = FicsitStatsTables::BuildPowerTable(NoBatteries, Avg, {}, {}, false, PassAll, BurnerName);
	TestTrue(TEXT("no Power Storage: the tile hides"), Empty.Tiles[4].bHidden);
	return true;
}

#endif
