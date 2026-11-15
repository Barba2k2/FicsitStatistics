#include "FicsitRealtimeHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitRealtimeHistoryTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	FFicsitLiveEvent Event(double Time, UClass* Item, float Amount, EFicsitLiveChannel Channel = EFicsitLiveChannel::Produced)
	{
		FFicsitLiveEvent Result;
		Result.Time = Time;
		Result.Item = Item;
		Result.Amount = Amount;
		Result.Channel = static_cast<uint8>(Channel);
		return Result;
	}

	int32 NumPoints(const FFicsitRealtimeHistory& Realtime)
	{
		const FFicsitHistory& History = Realtime.GetHistory();
		return History.Tiers.Num() > 0 ? History.Tiers[0].Num() : 0;
	}

	/** The amount the points hold, summed back from their per-minute rates. Consumed includes the Sink. */
	float Total(const FFicsitRealtimeHistory& Realtime, UClass* Item, bool bProduced, bool bSunk = false)
	{
		const FFicsitHistory& History = Realtime.GetHistory();
		const int32 Index = History.FindItemIndex(Item);
		if (Index == INDEX_NONE || History.Tiers.Num() == 0)
		{
			return 0.f;
		}
		float Sum = 0.f;
		const FFicsitHistoryTier& Tier = History.Tiers[0];
		for (int32 Point = 0; Point < Tier.Num(); ++Point)
		{
			const FFicsitHistoryPoint& Each = Tier.GetOrdered(Point);
			Sum += (bSunk ? Each.GetSunk(Index) : bProduced ? Each.GetProduced(Index) : Each.GetConsumed(Index)) * FFicsitRealtimeHistory::PointSeconds / 60.0;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRealtimeQuietTest, "FicsitStatistics.Realtime.PointsCloseWhileNothingHappens", FicsitRealtimeHistoryTests::Flags)
bool FFicsitRealtimeQuietTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRealtimeHistoryTests;
	FFicsitRealtimeHistory Realtime;
	const FFicsitProductionSnapshot Scan;
	Realtime.Advance({}, 100.0, Scan);
	TestEqual(TEXT("the first advance only starts"), NumPoints(Realtime), 0);
	Realtime.Advance({}, 100.5, Scan);
	TestTrue(TEXT("half a second is about 30 points"), FMath::Abs(NumPoints(Realtime) - 30) <= 1);
	// Advancing a second at a time never stalls, so it never starts over: the oldest points roll off instead.
	for (double Now = 101.0; Now <= 110.0; Now += 1.0)
	{
		Realtime.Advance({}, Now, Scan);
	}
	TestEqual(TEXT("the range holds 300 points"), NumPoints(Realtime), 300);
	TestEqual(TEXT("nothing dropped"), Realtime.ConsumeStats().Dropped, static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRealtimeEventsTest, "FicsitStatistics.Realtime.EventsLandInPointsAndKeepTheirAmounts", FicsitRealtimeHistoryTests::Flags)
bool FFicsitRealtimeEventsTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRealtimeHistoryTests;
	FFicsitRealtimeHistory Realtime;
	const FFicsitProductionSnapshot Scan;
	Realtime.Advance({}, 100.0, Scan);

	const TArray<FFicsitLiveEvent> Events = {
		Event(100.001, ItemA(), 1.f),
		Event(100.1, ItemA(), 2.f),
		Event(100.15, ItemB(), 4.f, EFicsitLiveChannel::Consumed),
		Event(100.16, ItemB(), 3.f, EFicsitLiveChannel::Sunk),
	};
	Realtime.Advance(Events, 100.2, Scan);
	TestEqual(TEXT("made"), Total(Realtime, ItemA(), true), 3.f, 0.001f);
	TestEqual(TEXT("used, Sink included"), Total(Realtime, ItemB(), false), 7.f, 0.001f);
	TestEqual(TEXT("sunk"), Total(Realtime, ItemB(), false, true), 3.f, 0.001f);

	const FFicsitRealtimeHistory::FStats Stats = Realtime.ConsumeStats();
	TestEqual(TEXT("all placed"), Stats.Placed, static_cast<int64>(4));
	TestEqual(TEXT("none late"), Stats.Late, static_cast<int64>(0));
	TestEqual(TEXT("stats start over once read"), Realtime.ConsumeStats().Placed, static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRealtimeLateTest, "FicsitStatistics.Realtime.LateEventGoesIntoTheOpenPoint", FicsitRealtimeHistoryTests::Flags)
bool FFicsitRealtimeLateTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRealtimeHistoryTests;
	FFicsitRealtimeHistory Realtime;
	const FFicsitProductionSnapshot Scan;
	Realtime.Advance({}, 100.0, Scan);
	Realtime.Advance({}, 100.5, Scan);
	const int32 Before = NumPoints(Realtime);

	// Stamped 0.4 s ago (its point closed long since), arriving now from a worker thread.
	const TArray<FFicsitLiveEvent> Late = {Event(100.1, ItemA(), 5.f)};
	Realtime.Advance(Late, 100.5, Scan);
	TestEqual(TEXT("still counted"), Realtime.ConsumeStats().Late, static_cast<int64>(1));
	TestEqual(TEXT("no point closed for it"), NumPoints(Realtime), Before);
	Realtime.Advance({}, 100.6, Scan);
	TestEqual(TEXT("lands in the next point to close"), Total(Realtime, ItemA(), true), 5.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitRealtimeStallTest, "FicsitStatistics.Realtime.StallStartsOverAndDropsOlderEvents", FicsitRealtimeHistoryTests::Flags)
bool FFicsitRealtimeStallTest::RunTest(const FString& Parameters)
{
	using namespace FicsitRealtimeHistoryTests;
	FFicsitRealtimeHistory Realtime;
	const FFicsitProductionSnapshot Scan;
	Realtime.Advance({}, 100.0, Scan);
	Realtime.Advance({Event(100.1, ItemA(), 1.f)}, 101.0, Scan);
	TestTrue(TEXT("a second of points"), NumPoints(Realtime) > 50);

	// Nothing advanced it for 6 s (the game paused): it starts over instead of filling 6 s of empty points.
	const TArray<FFicsitLiveEvent> Stale = {Event(103.0, ItemA(), 9.f), Event(107.01, ItemA(), 2.f)};
	Realtime.Advance(Stale, 107.0, Scan);
	TestEqual(TEXT("started over"), NumPoints(Realtime), 0);
	const FFicsitRealtimeHistory::FStats Stats = Realtime.ConsumeStats();
	TestEqual(TEXT("the event from before the restart is left out"), Stats.Dropped, static_cast<int64>(1));
	Realtime.Advance({}, 107.1, Scan);
	TestEqual(TEXT("only the new event"), Total(Realtime, ItemA(), true), 2.f, 0.001f);
	return true;
}

#endif
