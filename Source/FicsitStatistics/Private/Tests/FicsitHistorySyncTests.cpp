#include "FicsitStatisticsNet.h"
#include "FGRecipe.h"
#include "FicsitStatisticsHooks.h"
#include "FicsitStatisticsHistory.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitHistorySyncTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	/** A saved history with Count 5 s points of A being made. */
	FFicsitHistory MakeHistory(int32 Count)
	{
		FFicsitHistory History;
		History.EnsureLayout();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FFicsitProductionSnapshot Sample;
			Sample.Produced.FindOrAdd(ItemA()).Add(EFicsitRateSource::Manufacturing, 10.f + Index, 0.f);
			History.AddSample(Sample, 5.f);
		}
		return History;
	}

	FFicsitLiveSecond Second(int32 Seq, float MadeA)
	{
		FFicsitLiveSecond Live;
		Live.Seq = Seq;
		Live.Produced.Add(ItemA(), MadeA);
		return Live;
	}

	TArray<int32> Seqs(const TArray<FFicsitLiveSecond>& Seconds)
	{
		TArray<int32> Result;
		for (const FFicsitLiveSecond& Live : Seconds) { Result.Add(Live.Seq); }
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitClientHistoryThrottleTest, "FicsitStatistics.HistorySync.ClientAsksAtMostEveryFiveSecondsPerTier", FicsitHistorySyncTests::Flags)
bool FFicsitClientHistoryThrottleTest::RunTest(const FString& Parameters)
{
	FFicsitClientHistory Client;
	const TOptional<double> First = Client.NextRequest(0, 100.0);
	TestTrue(TEXT("the first request goes out"), First.IsSet());
	TestTrue(TEXT("and asks for everything"), First.IsSet() && *First < 0.0);
	TestFalse(TEXT("not again within 5 s"), Client.NextRequest(0, 104.0).IsSet());
	TestTrue(TEXT("again after 5 s"), Client.NextRequest(0, 105.0).IsSet());

	// Even the longest tier, whose points are hours apart, since every answer brings its open point.
	const int32 Longest = FFicsitHistory::NumTiers() - 1;
	TestTrue(TEXT("another tier has its own timing"), Client.NextRequest(Longest, 100.0).IsSet());
	TestFalse(TEXT("not again within 5 s"), Client.NextRequest(Longest, 104.0).IsSet());
	TestTrue(TEXT("again after 5 s"), Client.NextRequest(Longest, 105.0).IsSet());
	TestFalse(TEXT("a tier that doesn't exist is never asked for"), Client.NextRequest(99, 100.0).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitClientHistoryFollowUpTest, "FicsitStatistics.HistorySync.ClientFollowsUpWhenTheServerHasMore", FicsitHistorySyncTests::Flags)
bool FFicsitClientHistoryFollowUpTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistorySyncTests;
	const FFicsitHistory Server = MakeHistory(5);
	// One item, no buildings: 13 values per point, so 26 values is two points per chunk. One chunk per answer.
	const TArray<FFicsitHistoryChunk> FirstAnswer = FFicsitHistoryChunk::Build(Server, 0, -1.0, 26, 1);
	if (!TestEqual(TEXT("one chunk"), FirstAnswer.Num(), 1) || !TestTrue(TEXT("with more to come"), FirstAnswer[0].bMore))
	{
		return false;
	}

	FFicsitClientHistory Client;
	TestTrue(TEXT("asked"), Client.NextRequest(0, 10.0).IsSet());
	const TOptional<double> FollowUp = Client.Apply(FirstAnswer[0], 10.5);
	TestTrue(TEXT("follows up"), FollowUp.IsSet());
	TestEqual(TEXT("from the last point it got"), FollowUp.Get(-1.0), FirstAnswer[0].Times.Last());
	TestFalse(TEXT("no regular request on top of the follow-up"), Client.NextRequest(0, 12.0).IsSet());

	// The rest, as the server would answer the follow-ups.
	double Since = FollowUp.Get(-1.0);
	double Now = 10.5;
	for (int32 Answer = 0; Answer < 5; ++Answer)
	{
		const TArray<FFicsitHistoryChunk> Chunks = FFicsitHistoryChunk::Build(Server, 0, Since, 26, 1);
		if (Chunks.Num() == 0)
		{
			break;
		}
		Now += 0.1;
		const TOptional<double> Next = Client.Apply(Chunks[0], Now);
		if (!Next.IsSet())
		{
			break;
		}
		Since = *Next;
	}
	TestEqual(TEXT("every point arrived"), Client.Get().Tiers[0].Num(), Server.Tiers[0].Num());

	const TOptional<double> Later = Client.NextRequest(0, Now + 5.0);
	TestTrue(TEXT("later, it asks again"), Later.IsSet());
	TestEqual(TEXT("from its newest point"), Later.Get(-1.0), Server.Tiers[0].GetOrdered(Server.Tiers[0].Num() - 1).Time);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistoryRequestsTest, "FicsitStatistics.HistorySync.ServerIgnoresFastRepeatsButNotFollowUps", FicsitHistorySyncTests::Flags)
bool FFicsitHistoryRequestsTest::RunTest(const FString& Parameters)
{
	FFicsitHistoryRequests Requests;
	TestTrue(TEXT("the first request is answered"), Requests.Accept(0, -1.0, 10.0));
	Requests.Answered(0, true);
	TestFalse(TEXT("a follow-up waits for the next frame"), Requests.Accept(0, 5.0, 10.0));
	TestFalse(TEXT("a full request isn't a follow-up"), Requests.Accept(0, -1.0, 10.1));
	TestTrue(TEXT("the follow-up comes right away"), Requests.Accept(0, 5.0, 10.1));
	Requests.Answered(0, false);
	TestFalse(TEXT("nothing more to send: repeats within 2 s are ignored"), Requests.Accept(0, 6.0, 10.2));
	TestTrue(TEXT("other tiers are their own"), Requests.Accept(1, -1.0, 10.2));
	TestTrue(TEXT("after 2 s, answered again"), Requests.Accept(0, 6.0, 12.1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitNetLiveTest, "FicsitStatistics.HistorySync.LiveSecondsArriveOnceEvenWhenUpdatesAreMissed", FicsitHistorySyncTests::Flags)
bool FFicsitNetLiveTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistorySyncTests;
	FFicsitNetLive Net;
	for (int32 Seq = 1; Seq <= 4; ++Seq) { Net.Push(Second(Seq, 10.f * Seq)); }
	TestEqual(TEXT("the server keeps the last 3"), Net.Seconds.Num(), 3);

	int32 LastSeq = 0;
	const TArray<FFicsitLiveSecond> Joined = Net.TakeNew(LastSeq);
	TestEqual(TEXT("a client gets what's there, oldest first"), Seqs(Joined), TArray<int32>{2, 3, 4});
	TestEqual(TEXT("with its amounts"), Joined.Num() == 3 ? Joined[2].Produced.FindRef(ItemA()) : 0.f, 40.f);
	TestEqual(TEXT("the same update again adds nothing"), Net.TakeNew(LastSeq).Num(), 0);

	// Two updates missed: the client catches up from the ones still kept.
	Net.Push(Second(5, 1.f));
	Net.Push(Second(6, 1.f));
	TestEqual(TEXT("missed seconds arrive once"), Seqs(Net.TakeNew(LastSeq)), TArray<int32>{5, 6});

	// A new world: the server starts counting from 1 again.
	FFicsitNetLive Restarted;
	Restarted.Push(Second(1, 1.f));
	TestEqual(TEXT("the client follows the restart"), Seqs(Restarted.TakeNew(LastSeq)), TArray<int32>{1});
	TestEqual(TEXT("and counts from there"), LastSeq, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitNetLiveSecondTest, "FicsitStatistics.HistorySync.LiveSecondsReplicateEveryAmount", FicsitHistorySyncTests::Flags)
bool FFicsitNetLiveSecondTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistorySyncTests;
	FFicsitLiveSecond Live;
	Live.Seq = 7;
	Live.Seconds = 1.5f;
	Live.Produced.Add(ItemA(), 3.f);
	Live.Consumed.Add(ItemB(), 8.f);
	Live.Sunk.Add(ItemB(), 2.f);
	Live.Manual[static_cast<int32>(EFicsitManualKind::Built)].Add(ItemA(), 5.f);

	FFicsitLiveSecond Back;
	FFicsitNetLiveSecond::FromLive(Live).ToLive(Back);
	TestEqual(TEXT("seq"), Back.Seq, 7);
	TestEqual(TEXT("seconds"), Back.Seconds, 1.5f);
	TestEqual(TEXT("made"), Back.Produced.FindRef(ItemA()), 3.f);
	TestEqual(TEXT("used"), Back.Consumed.FindRef(ItemB()), 8.f);
	TestEqual(TEXT("sunk"), Back.Sunk.FindRef(ItemB()), 2.f);
	TestEqual(TEXT("built"), Back.Manual[static_cast<int32>(EFicsitManualKind::Built)].FindRef(ItemA()), 5.f);
	TestFalse(TEXT("nothing it didn't have"), Back.Produced.Contains(ItemB()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitNetSnapshotTest, "FicsitStatistics.HistorySync.SnapshotReplicatesEveryRateAndCount", FicsitHistorySyncTests::Flags)
bool FFicsitNetSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistorySyncTests;
	FFicsitProductionSnapshot Scan;
	Scan.WorldTimeSeconds = 123.0;
	Scan.Produced.FindOrAdd(ItemA()).Add(EFicsitRateSource::Manufacturing, 12.f, 15.f);
	Scan.Produced.FindOrAdd(ItemA()).Add(EFicsitRateSource::Extraction, 3.f, 4.f);
	Scan.Consumed.FindOrAdd(ItemA()).Add(EFicsitRateSource::ResourceSink, 2.f, 2.f);
	Scan.Consumed.FindOrAdd(ItemB()).Add(EFicsitRateSource::PowerGeneration, 5.f, 8.f);
	Scan.Consumed.FindOrAdd(ItemB()).Add(EFicsitRateSource::SpaceElevator, 1.f, 1.f);
	Scan.Producers.FindOrAdd(ItemA()).Add(EFicsitMachineState::Running);
	Scan.Producers.FindOrAdd(ItemA()).Add(EFicsitMachineState::Running);
	Scan.Producers.FindOrAdd(ItemA()).Add(EFicsitMachineState::OutputBlocked);
	Scan.Consumers.FindOrAdd(ItemB()).Add(EFicsitMachineState::Unpowered);
	Scan.RecipeOutput.FindOrAdd(ItemA()).Add(UFGRecipe::StaticClass(), 15.f);
	Scan.MachinesScanned[static_cast<int32>(EFicsitRateSource::Manufacturing)] = 3;
	Scan.Power.Totals[static_cast<int32>(EFicsitPowerTotal::Produced)] = 100.f;
	Scan.Power.Totals[static_cast<int32>(EFicsitPowerTotal::OtherConsumed)] = 6.f;
	Scan.Power.Producers.Add(ItemB(), FFicsitPowerBuilding{40.f, 50.f, 2, 1});
	Scan.Power.Consumers.Add(ItemA(), FFicsitPowerBuilding{7.f, 9.f, 3, 3});
	Scan.Stored.Add(ItemB(), {250.f, -12.5f});
	Scan.StorageTrendSeconds = 300.f;

	FFicsitProductionSnapshot Back;
	FFicsitNetSnapshot::FromSnapshot(Scan).ToSnapshot(Back);
	TestEqual(TEXT("time"), Back.WorldTimeSeconds, 123.0);
	TestEqual(TEXT("stored amount"), Back.Stored.FindRef(ItemB()).Amount, 250.f);
	TestEqual(TEXT("storage trend"), Back.Stored.FindRef(ItemB()).PerMinute, -12.5f);
	TestEqual(TEXT("storage trend coverage"), Back.StorageTrendSeconds, 300.f);
	TestFalse(TEXT("nothing stored it didn't have"), Back.Stored.Contains(ItemA()));
	for (const FFicsitItemFlowMap* Side : {&Scan.Produced, &Scan.Consumed})
	{
		const FFicsitItemFlowMap& BackSide = Side == &Scan.Produced ? Back.Produced : Back.Consumed;
		for (const auto& Pair : *Side)
		{
			const FFicsitItemFlow* Flow = BackSide.Find(Pair.Key);
			if (!TestNotNull(TEXT("every flow arrives"), Flow))
			{
				continue;
			}
			TestEqual(TEXT("total actual"), Flow->Total.Actual, Pair.Value.Total.Actual);
			TestEqual(TEXT("total max"), Flow->Total.Max, Pair.Value.Total.Max);
			for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
			{
				TestEqual(TEXT("source actual"), Flow->BySource[Source].Actual, Pair.Value.BySource[Source].Actual);
				TestEqual(TEXT("source max"), Flow->BySource[Source].Max, Pair.Value.BySource[Source].Max);
			}
		}
	}
	TestFalse(TEXT("no flow it didn't have"), Back.Produced.Contains(ItemB()));
	TestEqual(TEXT("running makers"), Back.Producers.FindRef(ItemA()).Get(EFicsitMachineState::Running), 2);
	TestEqual(TEXT("blocked makers"), Back.Producers.FindRef(ItemA()).Get(EFicsitMachineState::OutputBlocked), 1);
	TestEqual(TEXT("unpowered users"), Back.Consumers.FindRef(ItemB()).Get(EFicsitMachineState::Unpowered), 1);
	TestFalse(TEXT("no machine counts it didn't have"), Back.Consumers.Contains(ItemA()));
	TestEqual(TEXT("recipe output"), Back.RecipeOutput.FindRef(ItemA()).FindRef(UFGRecipe::StaticClass()), 15.f);
	TestEqual(TEXT("machines scanned"), Back.MachinesScanned[static_cast<int32>(EFicsitRateSource::Manufacturing)], 3);
	TestEqual(TEXT("power produced"), Back.Power.Get(EFicsitPowerTotal::Produced), 100.f);
	TestEqual(TEXT("other power"), Back.Power.Get(EFicsitPowerTotal::OtherConsumed), 6.f);
	const FFicsitPowerBuilding Generator = Back.Power.Producers.FindRef(ItemB());
	TestEqual(TEXT("generator MW"), Generator.MW, 40.f);
	TestEqual(TEXT("generator capacity"), Generator.MaxMW, 50.f);
	TestEqual(TEXT("generator count"), Generator.Count, 2);
	TestEqual(TEXT("generators running"), Generator.Active, 1);
	TestEqual(TEXT("consumer MW"), Back.Power.Consumers.FindRef(ItemA()).MW, 7.f);
	TestFalse(TEXT("a consumer stays a consumer"), Back.Power.Producers.Contains(ItemA()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitLiveSecondSampleTest, "FicsitStatistics.HistorySync.CountsBecomeRatesWithTheSinkApartAndScanOnlyFlows", FicsitHistorySyncTests::Flags)
bool FFicsitLiveSecondSampleTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistorySyncTests;
	FFicsitCounts Counts;
	Counts.Produced[static_cast<int32>(EFicsitRateSource::Manufacturing)].Add(ItemA(), 6.0);
	Counts.Produced[static_cast<int32>(EFicsitRateSource::Extraction)].Add(ItemA(), 4.0);
	Counts.Consumed[static_cast<int32>(EFicsitRateSource::Manufacturing)].Add(ItemB(), 3.0);
	Counts.Consumed[static_cast<int32>(EFicsitRateSource::ResourceSink)].Add(ItemB(), 2.0);
	Counts.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Add(ItemA(), 1.0);

	FFicsitLiveSecond Live = FFicsitLiveSecond::FromCounts(Counts);
	TestEqual(TEXT("every source together"), Live.Produced.FindRef(ItemA()), 10.f);
	TestEqual(TEXT("the Sink is part of consumed"), Live.Consumed.FindRef(ItemB()), 5.f);
	TestEqual(TEXT("and kept apart too"), Live.Sunk.FindRef(ItemB()), 2.f);
	TestEqual(TEXT("manual amounts"), Live.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].FindRef(ItemA()), 1.f);

	// Generator fuel has no hook: the scan's rate goes in as it is.
	FFicsitProductionSnapshot Scan;
	Scan.Consumed.FindOrAdd(ItemA()).Add(EFicsitRateSource::PowerGeneration, 12.f, 12.f);
	Scan.Power.Totals[static_cast<int32>(EFicsitPowerTotal::Produced)] = 300.f;

	Live.Seconds = 5.f;
	FFicsitProductionSnapshot Sample;
	Live.ToSample(Scan, 42.0, Sample);
	TestEqual(TEXT("10 in 5 s is 120 /min"), Sample.Produced.FindChecked(ItemA()).Total.Actual, 120.f);
	TestEqual(TEXT("demand without the Sink"), Sample.Consumed.FindChecked(ItemB()).BySource[static_cast<int32>(EFicsitRateSource::Manufacturing)].Actual, 36.f);
	TestEqual(TEXT("the Sink's part"), Sample.Consumed.FindChecked(ItemB()).BySource[static_cast<int32>(EFicsitRateSource::ResourceSink)].Actual, 24.f);
	TestEqual(TEXT("generator fuel from the scan"), Sample.Consumed.FindChecked(ItemA()).Total.Actual, 12.f);
	TestEqual(TEXT("power from the scan"), Sample.Power.Get(EFicsitPowerTotal::Produced), 300.f);
	TestEqual(TEXT("manual amounts stay amounts"), Sample.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].FindRef(ItemA()), 1.f);
	TestEqual(TEXT("the time"), Sample.WorldTimeSeconds, 42.0);
	return true;
}

#endif
