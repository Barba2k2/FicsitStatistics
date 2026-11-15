#include "FicsitStockTrend.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitStockTrendTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	TSubclassOf<UFGItemDescriptor> ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	TSubclassOf<UFGItemDescriptor> ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	TMap<TSubclassOf<UFGItemDescriptor>, float> Totals(float A, float B = 0.f)
	{
		TMap<TSubclassOf<UFGItemDescriptor>, float> Out;
		if (A > 0.f) { Out.Add(ItemA(), A); }
		if (B > 0.f) { Out.Add(ItemB(), B); }
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitStockTrendSteadyTest, "FicsitStatistics.StockTrend.SteadyDrainIsExact", FicsitStockTrendTests::Flags)
bool FFicsitStockTrendSteadyTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStockTrendTests;
	FFicsitStockTrend Trend;
	FFicsitStockMap Stock;
	Trend.Add(0.0, Totals(1000.f, 50.f));
	Trend.Fill(Stock);
	TestEqual(TEXT("one scan: the amount"), Stock.FindRef(ItemA()).Amount, 1000.f);
	TestEqual(TEXT("one scan: no trend yet"), Stock.FindRef(ItemA()).PerMinute, 0.f);
	TestEqual(TEXT("one scan covers nothing"), Trend.GetCoveredSeconds(), 0.0);

	// A drains 20 /min (5 every 15 s), B stays put.
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		Trend.Add(Step * 15.0, Totals(1000.f - Step * 5.f, 50.f));
	}
	Trend.Fill(Stock);
	TestEqual(TEXT("latest amount"), Stock.FindRef(ItemA()).Amount, 900.f);
	TestEqual(TEXT("steady drain"), Stock.FindRef(ItemA()).PerMinute, -20.f, 0.001f);
	TestEqual(TEXT("steady stock"), Stock.FindRef(ItemB()).PerMinute, 0.f);
	TestEqual(TEXT("covered"), Trend.GetCoveredSeconds(), 300.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitStockTrendWindowTest, "FicsitStatistics.StockTrend.OldScansLeaveAndALoadStartsOver", FicsitStockTrendTests::Flags)
bool FFicsitStockTrendWindowTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStockTrendTests;
	FFicsitStockTrend Trend;
	FFicsitStockMap Stock;

	// Rising for the first 10 minutes, then flat for 10: only the flat part is left in the window.
	for (int32 Minute = 0; Minute <= 20; ++Minute)
	{
		Trend.Add(Minute * 60.0, Totals(Minute <= 10 ? 100.f + Minute * 10.f : 200.f));
	}
	Trend.Fill(Stock);
	TestEqual(TEXT("the window is 10 minutes"), Trend.GetCoveredSeconds(), FFicsitStockTrend::WindowSeconds);
	TestEqual(TEXT("only the flat part counts"), Stock.FindRef(ItemA()).PerMinute, 0.f);

	// Emptied out: the item goes from the latest amounts, but its fall still shows in the trend.
	Trend.Add(21 * 60.0, Totals(0.f));
	Trend.Fill(Stock);
	TestEqual(TEXT("nothing left"), Stock.FindRef(ItemA()).Amount, 0.f);
	TestTrue(TEXT("falling"), Stock.FindRef(ItemA()).PerMinute < 0.f);

	// World time starts over when a save loads.
	Trend.Add(5.0, Totals(300.f));
	Trend.Fill(Stock);
	TestEqual(TEXT("a load starts over"), Trend.GetCoveredSeconds(), 0.0);
	TestEqual(TEXT("with the new amount"), Stock.FindRef(ItemA()).Amount, 300.f);
	TestEqual(TEXT("and no trend"), Stock.FindRef(ItemA()).PerMinute, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitStockTrendBurstTest, "FicsitStatistics.StockTrend.OneDeliveryMovesTheTrendLessThanTheEnds", FicsitStockTrendTests::Flags)
bool FFicsitStockTrendBurstTest::RunTest(const FString& Parameters)
{
	using namespace FicsitStockTrendTests;
	FFicsitStockTrend Trend;
	FFicsitStockMap Stock;
	// Flat at 500 for 9 minutes, then a train drops off 300 in the last minute. First-vs-last would say +30 /min.
	for (int32 Minute = 0; Minute <= 10; ++Minute)
	{
		Trend.Add(Minute * 60.0, Totals(Minute < 10 ? 500.f : 800.f));
	}
	Trend.Fill(Stock);
	const float PerMinute = Stock.FindRef(ItemA()).PerMinute;
	TestTrue(TEXT("rising"), PerMinute > 0.f);
	TestTrue(TEXT("but less than first-vs-last"), PerMinute < 30.f);
	return true;
}

#endif
