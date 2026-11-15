#include "FicsitRateEstimator.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGAnyUndefinedDescriptor.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

// Run in the editor: Session Frontend > Automation, filter "FicsitStatistics".
// Items are native descriptor classes: the estimator only needs distinct item classes, not real game items.

namespace FicsitRateEstimatorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr double Bucket = 5.0;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }
	UClass* ItemC() { return UFGAnyUndefinedDescriptor::StaticClass(); }

	/** Drives an estimator one 5 s bucket at a time, the way the subsystem does. */
	struct FDriver
	{
		FFicsitRateEstimator Estimator;
		double Now = 0.0;

		explicit FDriver(FFicsitRateEstimator::FSettings Settings = FFicsitRateEstimator::FSettings())
			: Estimator(MoveTemp(Settings))
		{
			Estimator.Reset(0.0);
		}

		/**
		 * One scan and one bucket: Made items of Item made by manufacturing in this bucket, one item per event. When
		 * bScanSeesMachine, the scan reports a machine for Item with this warm-up estimate and max.
		 */
		FFicsitProductionSnapshot Step(UClass* Item, double Made, bool bScanSeesMachine = true, float Estimate = 0.f, float Max = 120.f)
		{
			Now += Bucket;
			FFicsitProductionSnapshot Snapshot;
			Snapshot.WorldTimeSeconds = Now;
			if (bScanSeesMachine)
			{
				Snapshot.Produced.FindOrAdd(Item).Add(EFicsitRateSource::Manufacturing, Estimate, Max);
			}
			FFicsitCounts Counts;
			if (Made > 0.0)
			{
				Counts.Produced[static_cast<int32>(EFicsitRateSource::Manufacturing)].Add(Item, Made);
				Counts.Events[0].Add(Item, static_cast<int32>(Made));
			}
			Estimator.Apply(MoveTemp(Counts), Snapshot);
			return Snapshot;
		}

		/** Steps until Now reaches Until, making MadePerBucket each time. Returns the last snapshot. */
		FFicsitProductionSnapshot RunUntil(double Until, UClass* Item, double MadePerBucket)
		{
			FFicsitProductionSnapshot Last;
			while (Now + Bucket <= Until + 0.001)
			{
				Last = Step(Item, MadePerBucket, true, 0.f);
			}
			return Last;
		}
	};

	float Made(const FFicsitProductionSnapshot& Snapshot, UClass* Item, EFicsitRateSource Source = EFicsitRateSource::Manufacturing)
	{
		const FFicsitItemFlow* Flow = Snapshot.Produced.Find(Item);
		return Flow ? Flow->BySource[static_cast<int32>(Source)].Actual : -1.f;
	}

	float Used(const FFicsitProductionSnapshot& Snapshot, UClass* Item, EFicsitRateSource Source)
	{
		const FFicsitItemFlow* Flow = Snapshot.Consumed.Find(Item);
		return Flow ? Flow->BySource[static_cast<int32>(Source)].Actual : -1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorWarmUpTest, "FicsitStatistics.RateEstimator.KeepsScanEstimateUntilWindowIsReady", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorWarmUpTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FDriver Driver;
	// 5 s in, the window (5 s) is shorter than the 10 s short window: the scan's estimate stays.
	FFicsitProductionSnapshot First = Driver.Step(ItemA(), 10.0, true, 30.f, 60.f);
	TestEqual(TEXT("estimate kept during warm-up"), Made(First, ItemA()), 30.f);
	TestEqual(TEXT("total follows the source"), First.Produced.FindChecked(ItemA()).Total.Actual, 30.f);

	// 10 s in, 20 counted over 10 s: 120 /min replaces the estimate. Max stays the scan's.
	FFicsitProductionSnapshot Second = Driver.Step(ItemA(), 10.0, true, 30.f, 60.f);
	TestEqual(TEXT("counted rate once ready"), Made(Second, ItemA()), 120.f, 0.01f);
	TestEqual(TEXT("max from the scan"), Second.Produced.FindChecked(ItemA()).Total.Max, 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorSteadyTest, "FicsitStatistics.RateEstimator.SteadyRateIsExactAndWindowGrowsToMax", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorSteadyTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FDriver Driver;
	FFicsitProductionSnapshot At300 = Driver.RunUntil(300.0, ItemA(), 10.0);
	TestEqual(TEXT("10 per 5 s is 120 /min"), Made(At300, ItemA()), 120.f, 0.01f);
	TestEqual(TEXT("window covers everything so far"), Driver.Estimator.GetWindowSeconds(ItemA()), 300.0, 0.001);

	FFicsitProductionSnapshot At900 = Driver.RunUntil(900.0, ItemA(), 10.0);
	TestEqual(TEXT("still exact with a full window"), Made(At900, ItemA()), 120.f, 0.01f);
	TestEqual(TEXT("window stops at MaxWindowSeconds"), Driver.Estimator.GetWindowSeconds(ItemA()), 600.0, 0.001);
	TestEqual(TEXT("no restarts on a steady factory"), Driver.Estimator.ConsumeStats().Restarts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorNoiseTest, "FicsitStatistics.RateEstimator.NormalNoiseDoesNotRestart", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorNoiseTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FDriver Driver;
	const double Pattern[] = {8.0, 12.0, 10.0, 13.0, 7.0}; // averages 10 per bucket
	int32 Index = 0;
	FFicsitProductionSnapshot Last;
	while (Driver.Now < 600.0)
	{
		Last = Driver.Step(ItemA(), Pattern[Index++ % UE_ARRAY_COUNT(Pattern)]);
	}
	TestEqual(TEXT("no restarts"), Driver.Estimator.ConsumeStats().Restarts, 0);
	TestEqual(TEXT("average of the pattern"), Made(Last, ItemA()), 120.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorStopTest, "FicsitStatistics.RateEstimator.StopShowsWithinTwoBuckets", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorStopTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FDriver Driver;
	Driver.RunUntil(300.0, ItemA(), 10.0);
	Driver.Estimator.ConsumeStats();

	FFicsitProductionSnapshot OneBucket = Driver.Step(ItemA(), 0.0);
	TestTrue(TEXT("one quiet bucket is still within noise"), Made(OneBucket, ItemA()) > 100.f);

	FFicsitProductionSnapshot TwoBuckets = Driver.Step(ItemA(), 0.0);
	TestEqual(TEXT("two quiet buckets: stopped"), Made(TwoBuckets, ItemA()), 0.f, 0.01f);
	TestEqual(TEXT("window started over at the change"), Driver.Estimator.GetWindowSeconds(ItemA()), 10.0, 0.001);
	TestEqual(TEXT("one restart"), Driver.Estimator.ConsumeStats().Restarts, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorStartTest, "FicsitStatistics.RateEstimator.StartFromNothingRestartsAndConverges", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorStartTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FDriver Driver;
	FFicsitProductionSnapshot Idle = Driver.RunUntil(300.0, ItemA(), 0.0);
	TestEqual(TEXT("idle machine reads 0"), Made(Idle, ItemA()), 0.f);

	// The first bucket restarts the window, but its short window was sized while nothing was made (the 120 s cap), so
	// the rate shows at once yet low. Within seconds, restarts sized from real events bring it to the true rate.
	FFicsitProductionSnapshot First = Driver.Step(ItemA(), 10.0);
	TestTrue(TEXT("production shows in the first bucket"), Made(First, ItemA()) > 0.f);
	TestEqual(TEXT("restart on the start"), Driver.Estimator.ConsumeStats().Restarts, 1);

	FFicsitProductionSnapshot Minute = Driver.RunUntil(360.0, ItemA(), 10.0);
	TestTrue(TEXT("within 10% of 120 /min after a minute"), FMath::Abs(Made(Minute, ItemA()) - 120.f) < 12.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorCycleTest, "FicsitStatistics.RateEstimator.SlowCycleDelaysWarmUp", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorCycleTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	// A 60 s recipe: the short window is 2 cycles (120 s), so the estimate stays until 120 s of counts.
	FDriver Driver;
	FFicsitProductionSnapshot Last;
	while (Driver.Now < 115.0)
	{
		Driver.Estimator.NoteCycle(ItemA(), 60.f);
		Last = Driver.Step(ItemA(), 10.0, true, 50.f);
	}
	TestEqual(TEXT("estimate kept short of 2 cycles (115 s)"), Made(Last, ItemA()), 50.f);
	TestEqual(TEXT("the cycle the last Apply used"), Driver.Estimator.GetCycleSeconds(ItemA()), 60.f);

	Driver.Estimator.NoteCycle(ItemA(), 60.f);
	Last = Driver.Step(ItemA(), 10.0, true, 50.f);
	TestEqual(TEXT("counted at 2 cycles (120 s)"), Made(Last, ItemA()), 120.f, 0.01f);

	// Cycles are per scan: one that isn't noted again is dropped.
	Driver.Step(ItemA(), 10.0);
	TestEqual(TEXT("cycle dropped when not noted"), Driver.Estimator.GetCycleSeconds(ItemA()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorDismantledTest, "FicsitStatistics.RateEstimator.CountsOutliveTheScan", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorDismantledTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	// The machine is dismantled (the scan no longer sees it) but its last output still arrives.
	FDriver Driver;
	Driver.RunUntil(300.0, ItemA(), 10.0);
	FFicsitProductionSnapshot Gone = Driver.Step(ItemA(), 10.0, false);
	TestEqual(TEXT("counted flow still reported"), Made(Gone, ItemA()), 120.f, 0.01f);
	TestEqual(TEXT("no max without a machine"), Gone.Produced.FindChecked(ItemA()).Total.Max, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorEventSourcesTest, "FicsitStatistics.RateEstimator.SinkAndElevatorFromCountsWithFluidConversion", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorEventSourcesTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FFicsitRateEstimator::FSettings Settings;
	Settings.IsFluid = [](UClass* Item) { return Item == ItemB(); };
	FFicsitRateEstimator Estimator(MoveTemp(Settings));
	Estimator.Reset(0.0);

	FFicsitCounts Counts;
	const int32 Sink = static_cast<int32>(EFicsitRateSource::ResourceSink);
	const int32 Elevator = static_cast<int32>(EFicsitRateSource::SpaceElevator);
	Counts.Consumed[Sink].Add(ItemB(), 5000.0); // liters of a packaged fluid's raw amount
	Counts.Consumed[Sink].Add(ItemC(), 10.0);
	Counts.Consumed[Elevator].Add(ItemA(), 2.0);
	Counts.Events[1].Add(ItemB(), 1);
	Counts.Events[1].Add(ItemC(), 10);
	Counts.Events[1].Add(ItemA(), 2);

	FFicsitProductionSnapshot Snapshot;
	Snapshot.WorldTimeSeconds = 5.0;
	Estimator.Apply(MoveTemp(Counts), Snapshot);

	// No warm-up for event sources: they have no scan estimate to fall back on.
	TestEqual(TEXT("fluid Sink rate in m³/min"), Used(Snapshot, ItemB(), EFicsitRateSource::ResourceSink), 60.f, 0.01f);
	TestEqual(TEXT("solid Sink rate in items/min"), Used(Snapshot, ItemC(), EFicsitRateSource::ResourceSink), 120.f, 0.01f);
	TestEqual(TEXT("Space Elevator rate"), Used(Snapshot, ItemA(), EFicsitRateSource::SpaceElevator), 24.f, 0.01f);
	TestEqual(TEXT("event sources count as their own max"), Snapshot.Consumed.FindChecked(ItemC()).Total.Max, 120.f, 0.01f);
	TestEqual(TEXT("totals include event sources"), Snapshot.Consumed.FindChecked(ItemB()).Total.Actual, 60.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRateEstimatorManualTest, "FicsitStatistics.RateEstimator.ManualAmountsGoToTheSnapshot", FicsitRateEstimatorTests::Flags)
bool FFicsitRateEstimatorManualTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRateEstimatorTests;
	FFicsitRateEstimator Estimator;
	Estimator.Reset(0.0);
	TestEqual(TEXT("no counts before the first Apply"), Estimator.GetLatestCounts().Produced[0].Num(), 0);

	FFicsitCounts Counts;
	Counts.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Add(ItemA(), 3.0);
	Counts.Produced[static_cast<int32>(EFicsitRateSource::Manufacturing)].Add(ItemB(), 4.0);
	FFicsitProductionSnapshot Snapshot;
	Snapshot.WorldTimeSeconds = 5.0;
	Estimator.Apply(MoveTemp(Counts), Snapshot);

	TestEqual(TEXT("crafted amount in the snapshot"), Snapshot.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].FindRef(ItemA()), 3.f);
	TestEqual(TEXT("not kept with the rate counts"), Estimator.GetLatestCounts().Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Num(), 0);
	TestEqual(TEXT("rate counts kept for the history"),
		Estimator.GetLatestCounts().Produced[static_cast<int32>(EFicsitRateSource::Manufacturing)].FindRef(ItemB()), 4.0);
	return true;
}

#endif
