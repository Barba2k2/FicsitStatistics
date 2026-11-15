#include "FicsitStatisticsHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "UI/FicsitItemOutlook.h"
#include "UI/FicsitRangeReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitItemOutlookTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	/** A scan where Item is made at Made /min and used at Used /min by machines, plus Sunk into the Sink and ToElevator into the Space Elevator. */
	FFicsitProductionSnapshot Scan(UClass* Item, float Made, float Used, float Sunk = 0.f, float ToElevator = 0.f)
	{
		FFicsitProductionSnapshot Snapshot;
		if (Made > 0.f) { Snapshot.Produced.FindOrAdd(Item).Add(EFicsitRateSource::Manufacturing, Made, Made); }
		if (Used > 0.f) { Snapshot.Consumed.FindOrAdd(Item).Add(EFicsitRateSource::Manufacturing, Used, Used); }
		if (Sunk > 0.f) { Snapshot.Consumed.FindOrAdd(Item).Add(EFicsitRateSource::ResourceSink, Sunk, Sunk); }
		if (ToElevator > 0.f) { Snapshot.Consumed.FindOrAdd(Item).Add(EFicsitRateSource::SpaceElevator, ToElevator, ToElevator); }
		return Snapshot;
	}

	/** An empty saved history, so the range shows the latest scan. */
	FFicsitHistory EmptyHistory()
	{
		FFicsitHistory History;
		History.EnsureLayout();
		return History;
	}

	FFicsitItemOutlook Read(const FFicsitProductionSnapshot& Latest, const FFicsitOutlookContext& Context, UClass* Item)
	{
		const FFicsitHistory History = EmptyHistory();
		const FFicsitRangeReader Range(History, Latest, EFicsitTimeRange::TenMinutes);
		return FFicsitItemOutlooks(Range, Latest, Context).Get(Item);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitItemOutlookDemandTest, "FicsitStatistics.ItemOutlook.SinkIsOverflowUnlessCountedAsDemand", FicsitItemOutlookTests::Flags)
bool FFicsitItemOutlookDemandTest::RunTest(const FString& Parameters)
{
	using namespace FicsitItemOutlookTests;
	const FFicsitProductionSnapshot Latest = Scan(ItemA(), 30.f, 20.f, 10.f);

	FFicsitOutlookContext Context;
	const FFicsitItemOutlook Overflow = Read(Latest, Context, ItemA());
	TestEqual(TEXT("consumed includes the Sink"), Overflow.Consumed, 30.f);
	TestEqual(TEXT("demand leaves the Sink out"), Overflow.Demand, 20.f);
	TestEqual(TEXT("net"), Overflow.Net(), 10.f);
	TestTrue(TEXT("surplus"), Overflow.State == FicsitBalance::EState::Surplus);

	Context.bSinkIsDemand = true;
	const FFicsitItemOutlook Counted = Read(Latest, Context, ItemA());
	TestEqual(TEXT("demand counts the Sink"), Counted.Demand, 30.f);
	TestTrue(TEXT("balanced"), Counted.State == FicsitBalance::EState::Balanced);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitItemOutlookStorageTest, "FicsitStatistics.ItemOutlook.StorageWarnsOnlyWhenItRunsOutSoon", FicsitItemOutlookTests::Flags)
bool FFicsitItemOutlookStorageTest::RunTest(const FString& Parameters)
{
	using namespace FicsitItemOutlookTests;
	FFicsitOutlookContext Context;
	FFicsitProductionSnapshot Latest = Scan(ItemA(), 10.f, 10.f);
	Latest.Stored.Add(ItemA(), {600.f, -30.f});
	Latest.Stored.Add(ItemB(), {600.f, 5.f});

	Latest.StorageTrendSeconds = 30.f;
	const FFicsitItemOutlook Early = Read(Latest, Context, ItemA());
	TestEqual(TEXT("the amount shows right away"), Early.Stored, 600.f);
	TestFalse(TEXT("but not the trend"), Early.bStorageTrendReady);
	TestTrue(TEXT("so no runout yet"), Early.StorageRunoutMinutes < 0.f && !Early.bStorageRunsOutSoon);

	Latest.StorageTrendSeconds = 300.f;
	const FFicsitItemOutlook Falling = Read(Latest, Context, ItemA());
	TestEqual(TEXT("trend"), Falling.StoredPerMinute, -30.f);
	TestEqual(TEXT("600 at -30 /min lasts 20 min"), Falling.StorageRunoutMinutes, 20.f);
	TestTrue(TEXT("20 min is within the 30 min warning time"), Falling.bStorageRunsOutSoon);

	Context.StorageWarningMinutes = 15.f;
	const FFicsitItemOutlook Later = Read(Latest, Context, ItemA());
	TestEqual(TEXT("still runs out"), Later.StorageRunoutMinutes, 20.f);
	TestFalse(TEXT("but past a 15 min warning time"), Later.bStorageRunsOutSoon);

	const FFicsitItemOutlook Rising = Read(Latest, Context, ItemB());
	TestTrue(TEXT("rising storage doesn't run out"), Rising.StorageRunoutMinutes < 0.f && !Rising.bStorageRunsOutSoon);

	FFicsitProductionSnapshot NothingStored = Scan(ItemA(), 10.f, 40.f);
	NothingStored.StorageTrendSeconds = 300.f;
	const FFicsitItemOutlook Empty = Read(NothingStored, Context, ItemA());
	TestTrue(TEXT("a deficit alone doesn't warn about storage"), Empty.Stored == 0.f && !Empty.bStorageRunsOutSoon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitItemOutlookEtaTest, "FicsitStatistics.ItemOutlook.ElevatorFlowIsSupplyForTheElevatorOnly", FicsitItemOutlookTests::Flags)
bool FFicsitItemOutlookEtaTest::RunTest(const FString& Parameters)
{
	using namespace FicsitItemOutlookTests;
	FFicsitOutlookContext Context;
	Context.Needs.SpaceElevator.Add(ItemA(), 100);
	Context.Needs.Hub.Add(ItemA(), 50);

	// 30 made, 10 used by machines, 15 going into the Space Elevator.
	const FFicsitItemOutlook Outlook = Read(Scan(ItemA(), 30.f, 10.f, 0.f, 15.f), Context, ItemA());
	TestEqual(TEXT("elevator needs"), Outlook.ElevatorRemaining, 100);
	TestEqual(TEXT("elevator gets everything machines leave: 100 at 20 /min"), Outlook.ElevatorEtaMinutes, 5.f);
	TestEqual(TEXT("HUB needs"), Outlook.HubRemaining, 50);
	TestEqual(TEXT("HUB gets what the machines and the elevator leave: 50 at 5 /min"), Outlook.HubEtaMinutes, 10.f);
	TestTrue(TEXT("a milestone part"), Outlook.IsMilestonePart());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitItemOutlookEtaNowTest, "FicsitStatistics.ItemOutlook.MilestoneEtaUsesTheLatestScan", FicsitItemOutlookTests::Flags)
bool FFicsitItemOutlookEtaNowTest::RunTest(const FString& Parameters)
{
	using namespace FicsitItemOutlookTests;
	FFicsitOutlookContext Context;
	Context.Needs.SpaceElevator.Add(ItemA(), 100);

	// Over the range, 15 /min went into the Space Elevator. Its belt was just cut, so nothing does now.
	FFicsitHistory History = EmptyHistory();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		History.AddSample(Scan(ItemA(), 30.f, 10.f, 0.f, 15.f), 5.f);
	}
	const FFicsitProductionSnapshot Latest = Scan(ItemA(), 30.f, 10.f);
	const FFicsitRangeReader Range(History, Latest, EFicsitTimeRange::TenMinutes);
	const FFicsitItemOutlook Outlook = FFicsitItemOutlooks(Range, Latest, Context).Get(ItemA());
	TestEqual(TEXT("the range still averages the elevator's use"), Outlook.Consumed, 25.f);
	TestEqual(TEXT("100 at the 20 /min the machines leave now"), Outlook.ElevatorEtaMinutes, 5.f);

	// The other way around: the elevator was only just connected, so the range barely saw it.
	FFicsitHistory Before = EmptyHistory();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Before.AddSample(Scan(ItemA(), 30.f, 10.f), 5.f);
	}
	const FFicsitProductionSnapshot Connected = Scan(ItemA(), 30.f, 10.f, 0.f, 15.f);
	const FFicsitRangeReader NewRange(Before, Connected, EFicsitTimeRange::TenMinutes);
	TestEqual(TEXT("the machines still leave 20 /min"), FFicsitItemOutlooks(NewRange, Connected, Context).Get(ItemA()).ElevatorEtaMinutes, 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitItemOutlookUnmadeTest, "FicsitStatistics.ItemOutlook.UnmadeMilestonePartShowsAsDeficit", FicsitItemOutlookTests::Flags)
bool FFicsitItemOutlookUnmadeTest::RunTest(const FString& Parameters)
{
	using namespace FicsitItemOutlookTests;
	FFicsitOutlookContext Context;
	Context.Needs.Hub.Add(ItemA(), 50);
	const FFicsitProductionSnapshot Latest;

	const FFicsitItemOutlook Unmade = Read(Latest, Context, ItemA());
	TestTrue(TEXT("idle"), Unmade.State == FicsitBalance::EState::Idle);
	TestTrue(TEXT("not being made"), Unmade.HubEtaMinutes < 0.f);
	TestTrue(TEXT("shows as a deficit"), Unmade.ShowsAsDeficit());

	const FFicsitItemOutlook Unneeded = Read(Latest, Context, ItemB());
	TestFalse(TEXT("an idle item nobody needs isn't a deficit"), Unneeded.ShowsAsDeficit());
	TestTrue(TEXT("no elevator ETA"), Unneeded.ElevatorEtaMinutes < 0.f);
	return true;
}

#endif
