#include "FicsitStatisticsHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitHistoryTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }

	FFicsitProductionSnapshot Made(float PerMinute)
	{
		FFicsitProductionSnapshot Sample;
		Sample.Produced.FindOrAdd(ItemA()).Add(EFicsitRateSource::Manufacturing, PerMinute, 0.f);
		return Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistorySplitTest, "FicsitStatistics.History.SamplesSplitAcrossPointBoundaries", FicsitHistoryTests::Flags)
bool FFicsitHistorySplitTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistoryTests;
	FFicsitHistory History;
	History.EnsureLayout();

	// Scans that don't line up with the 5 s points: 7 s at 60 /min, 7 s at 120 /min, then a 30 s one at 0 /min that
	// also has 9 crafted by hand.
	History.AddSample(Made(60.f), 7.f);
	History.AddSample(Made(120.f), 7.f);
	FFicsitProductionSnapshot Long;
	Long.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Add(ItemA(), 9.f);
	History.AddSample(Long, 30.f);

	const int32 Item = History.FindItemIndex(ItemA());
	const FFicsitHistoryTier& Fine = History.Tiers[0];
	TestEqual(TEXT("tracked time"), History.TrackedSeconds, 44.0);
	if (!TestEqual(TEXT("44 s fill eight 5 s points"), Fine.Num(), 8))
	{
		return false;
	}
	for (int32 Index = 0; Index < Fine.Num(); ++Index)
	{
		TestEqual(TEXT("each point ends on its boundary"), Fine.GetOrdered(Index).Time, 5.0 * (Index + 1));
	}
	TestEqual(TEXT("the first point is all 60 /min"), Fine.GetOrdered(0).GetProduced(Item), 60.f);
	TestEqual(TEXT("2 s at 60 and 3 s at 120"), Fine.GetOrdered(1).GetProduced(Item), 96.f);
	TestEqual(TEXT("4 s at 120 and 1 s at 0"), Fine.GetOrdered(2).GetProduced(Item), 96.f);
	TestEqual(TEXT("the long sample fills whole points"), Fine.GetOrdered(5).GetProduced(Item), 0.f);
	TestEqual(TEXT("4 s left over in the open point"), Fine.AccumulatedSeconds, 4.f);

	// Manual amounts are totals: all of them go into the point the sample started in.
	TestEqual(TEXT("the craft lands in the point the long sample started in"), Fine.GetOrdered(2).GetManual(EFicsitManualKind::Crafted, Item), 9.f);
	float CraftedElsewhere = 0.f;
	for (int32 Index = 0; Index < Fine.Num(); ++Index)
	{
		CraftedElsewhere += Index == 2 ? 0.f : Fine.GetOrdered(Index).GetManual(EFicsitManualKind::Crafted, Item);
	}
	TestEqual(TEXT("and nowhere else"), CraftedElsewhere, 0.f);

	// The 30 s tier gets one point: 7 s at 60, 7 s at 120 and 16 s at 0.
	const FFicsitHistoryTier& Coarse = History.Tiers[1];
	if (TestEqual(TEXT("one 30 s point"), Coarse.Num(), 1))
	{
		TestEqual(TEXT("time-weighted average"), Coarse.GetOrdered(0).GetProduced(Item), 42.f);
		TestEqual(TEXT("ending at 30 s"), Coarse.GetOrdered(0).Time, 30.0);
	}
	return true;
}

#endif
