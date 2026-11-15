#include "FicsitStatisticsHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "UI/FicsitRangeReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitRangeReaderTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	/** A scan (or one history sample) where Item is made at Made /min, used at Used /min, of which Sunk went into the Sink. */
	FFicsitProductionSnapshot Scan(UClass* Item, float Made, float Used = 0.f, float Sunk = 0.f)
	{
		FFicsitProductionSnapshot Snapshot;
		if (Made > 0.f) { Snapshot.Produced.FindOrAdd(Item).Add(EFicsitRateSource::Manufacturing, Made, Made); }
		if (Used - Sunk > 0.f) { Snapshot.Consumed.FindOrAdd(Item).Add(EFicsitRateSource::Manufacturing, Used - Sunk, Used - Sunk); }
		if (Sunk > 0.f) { Snapshot.Consumed.FindOrAdd(Item).Add(EFicsitRateSource::ResourceSink, Sunk, Sunk); }
		return Snapshot;
	}

	FFicsitHistory MakeHistory(FFicsitHistory::ELayout Layout = FFicsitHistory::ELayout::Saved)
	{
		FFicsitHistory History;
		History.Layout = Layout;
		History.EnsureLayout();
		return History;
	}

	void AddSamples(FFicsitHistory& History, const FFicsitProductionSnapshot& Sample, int32 Count, float Seconds = 5.f)
	{
		for (int32 Index = 0; Index < Count; ++Index) { History.AddSample(Sample, Seconds); }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderFallbackTest, "FicsitStatistics.RangeReader.ShowsLatestScanUntilRangeHasPoints", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderFallbackTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	const FFicsitHistory History = MakeHistory();
	FFicsitProductionSnapshot Latest = Scan(ItemA(), 30.f, 10.f, 4.f);
	Latest.Power.Totals[static_cast<int32>(EFicsitPowerTotal::Produced)] = 100.f;

	const FFicsitRangeReader Range(History, Latest, EFicsitTimeRange::TenMinutes);
	const FFicsitRangeRates Rates = Range.GetRates(ItemA());
	TestEqual(TEXT("produced from the scan"), Rates.Produced, 30.f);
	TestEqual(TEXT("consumed from the scan, Sink included"), Rates.Consumed, 10.f);
	TestEqual(TEXT("sunk from the scan"), Rates.Sunk, 4.f);

	float Totals[FicsitPowerTotalCount];
	FFicsitRangeReader::FAmountMap Producers, Consumers;
	Range.GetPowerAverages(Totals, Producers, Consumers);
	TestEqual(TEXT("power from the scan"), Totals[static_cast<int32>(EFicsitPowerTotal::Produced)], 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderAveragesTest, "FicsitStatistics.RangeReader.AveragesTheRangeOnceItHasPoints", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderAveragesTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	FFicsitHistory History = MakeHistory();
	AddSamples(History, Scan(ItemA(), 60.f), 2);
	AddSamples(History, Scan(ItemA(), 120.f, 30.f, 10.f), 2);

	const FFicsitRangeReader Range(History, Scan(ItemA(), 999.f), EFicsitTimeRange::TenMinutes);
	const FFicsitRangeRates Rates = Range.GetRates(ItemA());
	TestEqual(TEXT("average over the range, not the scan"), Rates.Produced, 90.f, 0.01f);
	TestEqual(TEXT("consumed averaged"), Rates.Consumed, 15.f, 0.01f);
	TestEqual(TEXT("sunk averaged"), Rates.Sunk, 5.f, 0.01f);
	TestEqual(TEXT("an item that didn't move reads 0"), Range.GetRates(ItemB()).Produced, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderEmptyPointsTest, "FicsitStatistics.RangeReader.EmptySavedRangeFallsBackButQuietLiveRangeIsZero", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderEmptyPointsTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	const FFicsitProductionSnapshot Latest = Scan(ItemA(), 30.f);

	// Saved points with nothing in them (e.g. just after loading): show the scan.
	FFicsitHistory Saved = MakeHistory();
	AddSamples(Saved, FFicsitProductionSnapshot(), 2);
	TestEqual(TEXT("saved range falls back"), FFicsitRangeReader(Saved, Latest, EFicsitTimeRange::TenMinutes).GetRates(ItemA()).Produced, 30.f);

	// A live minute with points but nothing moving really is zero.
	FFicsitHistory Live = MakeHistory(FFicsitHistory::ELayout::Minute);
	AddSamples(Live, FFicsitProductionSnapshot(), 3, 1.f);
	TestEqual(TEXT("live range reads 0"), FFicsitRangeReader(Live, Latest, EFicsitTimeRange::OneMinute).GetRates(ItemA()).Produced, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderRecentTest, "FicsitStatistics.RangeReader.RecentReadsOnlyTheLastSeconds", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderRecentTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	FFicsitHistory History = MakeHistory();
	AddSamples(History, Scan(ItemA(), 60.f), 10);
	AddSamples(History, Scan(ItemA(), 120.f), 2);
	const FFicsitProductionSnapshot Latest;

	TestEqual(TEXT("last 10 s"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes, 10.f).GetRates(ItemA()).Produced, 120.f, 0.01f);
	TestEqual(TEXT("whole range"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes).GetRates(ItemA()).Produced, 70.f, 0.01f);
	TestEqual(TEXT("shorter than a point still reads the newest one"),
		FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes, 1.f).GetRates(ItemA()).Produced, 120.f, 0.01f);

	TArray<float> Net;
	FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes, 10.f).GetNetSeries(ItemA(), false, Net);
	TestEqual(TEXT("net series covers the last 10 s"), Net.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderSpanTest, "FicsitStatistics.RangeReader.GraphSpanIsFixedExceptForAll", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderSpanTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	FFicsitHistory History = MakeHistory();
	AddSamples(History, Scan(ItemA(), 60.f), 60); // 300 s tracked
	const FFicsitProductionSnapshot Latest;

	TestEqual(TEXT("10m axis is the full 10 minutes"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes).GetGraphSpanSeconds(), 600.f);
	TestEqual(TEXT("1h axis is the full hour"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::OneHour).GetGraphSpanSeconds(), 3600.f);
	TestEqual(TEXT("All spans what was tracked"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::All).GetGraphSpanSeconds(), 300.f);
	TestEqual(TEXT("recent spans its seconds"), FFicsitRangeReader(History, Latest, EFicsitTimeRange::TenMinutes, 120.f).GetGraphSpanSeconds(), 120.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRangeReaderSmoothingTest, "FicsitStatistics.RangeReader.SmoothsSavedFiveSecondPointsOnly", FicsitRangeReaderTests::Flags)
bool FFicsitRangeReaderSmoothingTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRangeReaderTests;
	// Exact counts alternate between 0 and 120 per 5 s point: jagged.
	FFicsitHistory Saved = MakeHistory();
	FFicsitHistory Live = MakeHistory(FFicsitHistory::ELayout::Minute);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const FFicsitProductionSnapshot Sample = Scan(ItemA(), Index % 2 == 0 ? 120.f : 0.f);
		Saved.AddSample(Sample, 5.f);
		Live.AddSample(Sample, 1.f);
	}
	const FFicsitProductionSnapshot Latest;
	auto Swing = [](const TArray<float>& Values)
	{
		// Max minus min away from the ends (the ends are one-sided).
		float Min = TNumericLimits<float>::Max(), Max = TNumericLimits<float>::Lowest();
		for (int32 Index = 4; Index < Values.Num() - 4; ++Index) { Min = FMath::Min(Min, Values[Index]); Max = FMath::Max(Max, Values[Index]); }
		return Max - Min;
	};

	TArray<float> Times, Values;
	FFicsitRangeReader(Saved, Latest, EFicsitTimeRange::TenMinutes).GetSeries(ItemA(), true, Times, Values);
	TestEqual(TEXT("one value per point"), Values.Num(), 20);
	TestTrue(TEXT("10m line is smoothed"), Swing(Values) < 40.f);

	FFicsitRangeReader(Live, Latest, EFicsitTimeRange::OneMinute).GetSeries(ItemA(), true, Times, Values);
	TestEqual(TEXT("1m line keeps every point as counted"), Swing(Values), 120.f, 0.01f);
	return true;
}

#endif
