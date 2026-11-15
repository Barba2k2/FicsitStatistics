#include "FicsitStatisticsHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "UI/FicsitRangeReader.h"
#include "UI/FicsitStatsWindowModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitStatsWindowModelTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	using FModel = FFicsitStatsWindowModel;
	using FItem = FModel::FItem;

	// Named so that A to Z is Consumable, Equipment, Item, Resource.
	UClass* Consumable() { return UFGConsumableDescriptor::StaticClass(); }
	UClass* Equipment() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* Item() { return UFGItemDescriptor::StaticClass(); }
	UClass* Resource() { return UFGResourceDescriptor::StaticClass(); }

	// Stand-in categories (any class will do): "Class", "Field", "Struct".
	UClass* CategoryClass() { return UClass::StaticClass(); }
	UClass* CategoryField() { return UField::StaticClass(); }
	UClass* CategoryStruct() { return UStruct::StaticClass(); }

	/** Item data the editor's stub descriptors don't have: names are class names, categories and Depot limits are set by the test. */
	class FTestItemInfo : public IFicsitItemInfo
	{
	public:
		TMap<UClass*, UClass*> CategoryOf;
		TMap<UClass*, float> Priority;
		TMap<UClass*, int32> DepotLimits;

		virtual FText GetName(UClass* Of) const override { return FText::FromString(GetNameSafe(Of)); }
		virtual UClass* GetCategory(UClass* Of) const override { return CategoryOf.FindRef(Of); }
		virtual float GetCategoryPriority(UClass* Category) const override { return Priority.FindRef(Category); }
		virtual FText GetCategoryName(UClass* Category) const override { return FText::FromString(GetNameSafe(Category)); }
		virtual int32 GetDepotLimit(UClass* Of) const override { return DepotLimits.FindRef(Of); }
	};

	/** A latest scan where machines make and use items at the given rates (per minute). */
	struct FScan
	{
		FFicsitProductionSnapshot Snapshot;
		FScan& Made(UClass* Of, float Rate, EFicsitRateSource Source = EFicsitRateSource::Manufacturing)
		{
			Snapshot.Produced.FindOrAdd(Of).Add(Source, Rate, Rate);
			return *this;
		}
		FScan& Used(UClass* Of, float Rate)
		{
			Snapshot.Consumed.FindOrAdd(Of).Add(EFicsitRateSource::Manufacturing, Rate, Rate);
			return *this;
		}
		FScan& Generator(UClass* Building, float MW)
		{
			FFicsitPowerBuilding& Entry = Snapshot.Power.Producers.FindOrAdd(Building);
			Entry.MW = MW;
			Entry.MaxMW = MW;
			Entry.Count = 1;
			Entry.Active = 1;
			return *this;
		}
	};

	/** The window over an empty saved history, so every range shows the latest scan. */
	struct FWindow
	{
		TSharedRef<FTestItemInfo> Info = MakeShared<FTestItemInfo>();
		FModel Model{Info};
		FFicsitHistory History;

		FWindow() { History.EnsureLayout(); }

		void Refresh(const FScan& Scan, FModel::FRefreshInputs Inputs = FModel::FRefreshInputs())
		{
			const FFicsitRangeReader Range(History, Scan.Snapshot, Model.GetTimeRange());
			Model.Refresh(Range, Scan.Snapshot, MoveTemp(Inputs));
		}
	};

	/** Milestone needs as the game would report them: the Space Elevator's, combined. */
	FFicsitOutlookContext Needs(UClass* Of, int32 Amount)
	{
		FFicsitOutlookContext Context;
		Context.Needs.SpaceElevator.Add(Of, Amount);
		Context.Needs.Combined.Add(Of, Amount);
		return Context;
	}

	template <typename RowType>
	TArray<UClass*> Order(const TArray<RowType>& Rows)
	{
		TArray<UClass*> Result;
		for (const RowType& Row : Rows) { Result.Add(Row.Item.Get()); }
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelMilestoneTest, "FicsitStatistics.WindowModel.MilestoneFilterListsUnmadePartsAndTurnsOffWhenDone", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelMilestoneTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	const FScan Scan = FScan().Made(Equipment(), 30.f);

	FModel::FRefreshInputs Inputs;
	Inputs.Context = Needs(Resource(), 100);
	Window.Model.SelectMilestoneFilter();
	Window.Refresh(Scan, Inputs);
	TestTrue(TEXT("the sidebar offers the filter"), Window.Model.HasMilestoneNeeds());
	TestEqual(TEXT("only the part the milestone needs, though nothing makes it"), Order(Window.Model.GetRateRows(true)), TArray<UClass*>{Resource()});
	TestEqual(TEXT("listed at 0 /min"), Window.Model.GetRateRows(true)[0].Rate, 0.f);
	TestEqual(TEXT("Balance lists it too"), Order(Window.Model.GetBalance().Rows), TArray<UClass*>{Resource()});

	// The milestone is done: nothing is needed, so the filter goes and everything shows again.
	Window.Refresh(Scan);
	TestFalse(TEXT("the filter turned itself off"), Window.Model.IsMilestoneFilterOn());
	TestFalse(TEXT("the sidebar drops it"), Window.Model.HasMilestoneNeeds());
	TestEqual(TEXT("everything made shows"), Order(Window.Model.GetRateRows(true)), TArray<UClass*>{Equipment()});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelIdleTest, "FicsitStatistics.WindowModel.BalanceListsStoredAndRecordedItemsAsIdle", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelIdleTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	FModel::FRefreshInputs Inputs;
	Inputs.Context.Depot.Add(Consumable(), 50);
	// The saved history's item table can hold classes that no longer load (null).
	Inputs.RecordedItems.Add(Item());
	Inputs.RecordedItems.Add(FItem());
	Window.Model.ClickBalanceSort(FModel::EBalanceSort::Item);
	Window.Refresh(FScan().Made(Equipment(), 30.f), Inputs);

	TestEqual(TEXT("made, stored and recorded items, A to Z"), Order(Window.Model.GetBalance().Rows), TArray<UClass*>{Consumable(), Equipment(), Item()});
	TestTrue(TEXT("the stored item is idle"), Window.Model.GetOutlook(Consumable()).State == FicsitBalance::EState::Idle);
	TestEqual(TEXT("with its stock"), Window.Model.GetOutlook(Consumable()).Depot, 50);
	TestTrue(TEXT("Production only lists what's made"), Order(Window.Model.GetRateRows(true)) == TArray<UClass*>{Equipment()});
	TestEqual(TEXT("an item it never saw has an empty outlook"), Window.Model.GetOutlook(Resource()).Produced, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelCategoriesTest, "FicsitStatistics.WindowModel.CategoriesFollowMenuOrderAndFilterTheLists", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelCategoriesTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	Window.Info->CategoryOf.Add(Equipment(), CategoryStruct());
	Window.Info->CategoryOf.Add(Consumable(), CategoryField());
	Window.Info->CategoryOf.Add(Item(), CategoryClass());
	Window.Info->Priority.Add(CategoryStruct(), 1.f);
	Window.Info->Priority.Add(CategoryClass(), 1.f);
	Window.Info->Priority.Add(CategoryField(), 0.f);
	const FScan Scan = FScan().Made(Equipment(), 30.f).Made(Consumable(), 20.f).Used(Item(), 5.f);

	Window.Refresh(Scan);
	TArray<UClass*> Sidebar;
	for (const auto& Pair : Window.Model.GetCategories()) { Sidebar.Add(Pair.Key); }
	TestEqual(TEXT("menu priority first, then A to Z"), Sidebar, TArray<UClass*>{CategoryField(), CategoryClass(), CategoryStruct()});

	Window.Model.SelectMilestoneFilter();
	Window.Model.SelectCategory(CategoryStruct());
	TestFalse(TEXT("picking a category leaves the milestone filter"), Window.Model.IsMilestoneFilterOn());
	Window.Refresh(Scan);
	TestEqual(TEXT("only that category's items"), Order(Window.Model.GetRateRows(true)), TArray<UClass*>{Equipment()});
	TestEqual(TEXT("in every list"), Window.Model.GetRateRows(false).Num(), 0);

	Window.Model.SelectCategory(nullptr);
	Window.Refresh(Scan);
	TestEqual(TEXT("All shows everything again"), Window.Model.GetRateRows(true).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelSearchTest, "FicsitStatistics.WindowModel.SearchFiltersEveryTabByName", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelSearchTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	const FScan Scan = FScan().Made(Equipment(), 30.f).Made(Consumable(), 20.f).Generator(Equipment(), 30.f).Generator(Consumable(), 20.f);

	Window.Model.SetSearch(TEXT("  EQUIP "));
	Window.Refresh(Scan);
	TestEqual(TEXT("trimmed, any case"), Order(Window.Model.GetRateRows(true)), TArray<UClass*>{Equipment()});
	TestEqual(TEXT("Balance"), Order(Window.Model.GetBalance().Rows), TArray<UClass*>{Equipment()});
	TestEqual(TEXT("Power goes by building name"), Order(Window.Model.GetPower().Producers), TArray<UClass*>{Equipment()});

	Window.Model.SetSearch(TEXT(""));
	Window.Refresh(Scan);
	TestEqual(TEXT("cleared"), Window.Model.GetPower().Producers.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelNavigationTest, "FicsitStatistics.WindowModel.DetailsJumpToBalanceAndLeaveTheSupplyChain", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelNavigationTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	Window.Info->DepotLimits.Add(Equipment(), 500);
	const FScan Scan = FScan().Made(Equipment(), 30.f).Used(Equipment(), 40.f);

	Window.Model.SelectTab(EFicsitStatsTab::Power);
	Window.Model.OpenSupplyChain();
	Window.Model.ShowDetails(Equipment());
	TestTrue(TEXT("details live on the Balance tab"), Window.Model.GetTab() == EFicsitStatsTab::Balance);
	TestFalse(TEXT("the supply chain closes"), Window.Model.IsSupplyChainOpen());

	Window.Refresh(Scan);
	const FFicsitWindowDetails& Details = Window.Model.GetDetails();
	TestTrue(TEXT("the item"), Details.Item == Equipment());
	TestEqual(TEXT("made right now"), Details.Produced.Total.Actual, 30.f);
	TestEqual(TEXT("net right now"), Details.NetNow(), -10.f);
	TestEqual(TEXT("the pane's outlook is the row's"), Details.Outlook.Net(), Window.Model.GetOutlook(Equipment()).Net());
	TestTrue(TEXT("the Depot could hold it: a Depot section"), Details.bShowDepot);

	Window.Model.OpenSupplyChain();
	Window.Model.SelectTab(EFicsitStatsTab::Production);
	TestFalse(TEXT("a tab click leaves the supply chain too"), Window.Model.IsSupplyChainOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelLayoutTest, "FicsitStatistics.WindowModel.DetailsLayoutChangesOnlyWhenASectionDoes", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelLayoutTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	Window.Model.ShowDetails(Equipment());

	Window.Refresh(FScan().Made(Equipment(), 30.f));
	const FString Before = Window.Model.GetDetails().LayoutKey();
	Window.Refresh(FScan().Made(Equipment(), 45.f));
	TestEqual(TEXT("a new rate keeps the layout"), Window.Model.GetDetails().LayoutKey(), Before);
	Window.Refresh(FScan().Made(Equipment(), 45.f).Made(Equipment(), 5.f, EFicsitRateSource::Extraction));
	TestNotEqual(TEXT("a second source adds a breakdown"), Window.Model.GetDetails().LayoutKey(), Before);

	Window.Model.ShowDetails(nullptr);
	Window.Refresh(FScan().Made(Equipment(), 30.f));
	TestFalse(TEXT("nothing selected"), static_cast<bool>(Window.Model.GetDetails().Item));
	TestNotEqual(TEXT("which is its own layout"), Window.Model.GetDetails().LayoutKey(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelSortTest, "FicsitStatistics.WindowModel.SortClicksPickADirectionThenFlip", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelSortTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	using EBalanceSort = FModel::EBalanceSort;
	using EManualSort = FModel::EManualSort;
	FWindow Window;
	FModel& Model = Window.Model;
	TestTrue(TEXT("Balance starts on Net, worst deficit first"), Model.GetBalanceSort() == EBalanceSort::Net && Model.IsBalanceAscending());

	Model.ClickBalanceSort(EBalanceSort::Produced);
	TestTrue(TEXT("amounts start largest first"), Model.GetBalanceSort() == EBalanceSort::Produced && !Model.IsBalanceAscending());
	Model.ClickBalanceSort(EBalanceSort::Produced);
	TestTrue(TEXT("the same column again flips"), Model.IsBalanceAscending());
	Model.ClickBalanceSort(EBalanceSort::Item);
	TestTrue(TEXT("names start A to Z"), Model.GetBalanceSort() == EBalanceSort::Item && Model.IsBalanceAscending());

	Model.ClickManualSort(EManualSort::Crafted);
	TestTrue(TEXT("Manual amounts start largest first"), Model.GetManualSort() == EManualSort::Crafted && !Model.IsManualAscending());
	Model.ClickManualSort(EManualSort::Item);
	TestTrue(TEXT("Manual names start A to Z"), Model.IsManualAscending());

	Model.RestoreSorts(static_cast<int32>(EBalanceSort::Depot), false, static_cast<int32>(EManualSort::Refunded), true);
	TestTrue(TEXT("saved sorts come back"), Model.GetBalanceSort() == EBalanceSort::Depot && Model.GetManualSort() == EManualSort::Refunded && Model.IsManualAscending());
	Model.RestoreSorts(99, false, -1, true);
	TestTrue(TEXT("unknown columns fall back to Net"), Model.GetBalanceSort() == EBalanceSort::Net && Model.IsBalanceAscending());
	TestTrue(TEXT("and to Spent building"), Model.GetManualSort() == EManualSort::Built && !Model.IsManualAscending());

	// The sort reaches the rows.
	Model.ClickBalanceSort(EBalanceSort::Produced);
	Window.Refresh(FScan().Made(Equipment(), 10.f).Made(Consumable(), 20.f));
	TestEqual(TEXT("most made first"), Order(Model.GetBalance().Rows), TArray<UClass*>{Consumable(), Equipment()});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitWindowModelGraphTest, "FicsitStatistics.WindowModel.GraphsShowTheTopRowsUntilChanged", FicsitStatsWindowModelTests::Flags)
bool FFicsitWindowModelGraphTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStatsWindowModelTests;
	FWindow Window;
	FModel& Model = Window.Model;
	FModel::FRefreshInputs Inputs;
	Inputs.DefaultGraphItems = 1;
	const FScan Scan = FScan().Made(Equipment(), 30.f).Made(Consumable(), 10.f);
	Window.Refresh(Scan, Inputs);

	TestTrue(TEXT("the top row is on"), Model.IsOnGraph(Equipment(), true));
	TestFalse(TEXT("the next one isn't"), Model.IsOnGraph(Consumable(), true));
	TestFalse(TEXT("untouched"), Model.IsGraphCustomized(true));

	Model.ToggleOnGraph(Consumable(), true);
	TestTrue(TEXT("toggled on"), Model.IsOnGraph(Consumable(), true));
	TestTrue(TEXT("customized"), Model.IsGraphCustomized(true));
	TestFalse(TEXT("the other graph is its own"), Model.IsGraphCustomized(false));

	Window.Refresh(Scan, Inputs);
	TestTrue(TEXT("a refresh keeps the toggle"), Model.IsOnGraph(Consumable(), true));
	Model.ResetGraph(true);
	TestFalse(TEXT("reset"), Model.IsGraphCustomized(true));

	const int32 First = Model.GetSeriesColorIndex(Consumable());
	const int32 Second = Model.GetSeriesColorIndex(Equipment());
	TestNotEqual(TEXT("items get their own colors"), First, Second);
	TestEqual(TEXT("and keep them"), Model.GetSeriesColorIndex(Consumable()), First);
	return true;
}

#endif
