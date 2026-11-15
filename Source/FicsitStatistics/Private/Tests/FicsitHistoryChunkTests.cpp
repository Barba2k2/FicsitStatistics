#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsNet.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitHistoryChunkTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UClass* ItemA() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* ItemB() { return UFGConsumableDescriptor::StaticClass(); }

	/** A saved history with Count 5 s points, each one different: A made and sunk, B used, B crafted by hand, and power. */
	FFicsitHistory MakeHistory(int32 Count)
	{
		FFicsitHistory History;
		History.EnsureLayout();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FFicsitProductionSnapshot Sample;
			Sample.Produced.FindOrAdd(ItemA()).Add(EFicsitRateSource::Manufacturing, 10.f + Index, 0.f);
			Sample.Consumed.FindOrAdd(ItemA()).Add(EFicsitRateSource::ResourceSink, 2.f + Index, 0.f);
			Sample.Consumed.FindOrAdd(ItemB()).Add(EFicsitRateSource::Manufacturing, 5.f * Index, 0.f);
			if (Index % 3 == 0)
			{
				Sample.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Add(ItemB(), 1.f + Index);
			}
			Sample.Power.Totals[static_cast<int32>(EFicsitPowerTotal::Produced)] = 100.f + Index;
			Sample.Power.Producers.FindOrAdd(ItemB()).MW = 40.f + Index;
			History.AddSample(Sample, 5.f);
		}
		return History;
	}

	/** Applies every chunk to Client, as the client would on receiving them. */
	void ApplyAll(const TArray<FFicsitHistoryChunk>& Chunks, FFicsitHistory& Client)
	{
		for (const FFicsitHistoryChunk& Chunk : Chunks) { Chunk.ApplyTo(Client); }
	}

	/** True if tier 0 of both histories has the same points, value for value. */
	bool SamePoints(FAutomationTestBase& Test, const FFicsitHistory& Server, const FFicsitHistory& Client)
	{
		const FFicsitHistoryTier& A = Server.Tiers[0];
		const FFicsitHistoryTier& B = Client.Tiers[0];
		if (!Test.TestEqual(TEXT("point count"), B.Num(), A.Num()))
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const FFicsitHistoryPoint& P = A.GetOrdered(Index);
			const FFicsitHistoryPoint& Q = B.GetOrdered(Index);
			Test.TestEqual(TEXT("time"), Q.Time, P.Time);
			for (int32 Item = 0; Item < Server.ItemTable.Num(); ++Item)
			{
				Test.TestEqual(TEXT("produced"), Q.GetProduced(Item), P.GetProduced(Item));
				Test.TestEqual(TEXT("consumed"), Q.GetConsumed(Item), P.GetConsumed(Item));
				Test.TestEqual(TEXT("sunk"), Q.GetSunk(Item), P.GetSunk(Item));
				Test.TestEqual(TEXT("crafted"), Q.GetManual(EFicsitManualKind::Crafted, Item), P.GetManual(EFicsitManualKind::Crafted, Item));
			}
			Test.TestEqual(TEXT("power"), Q.GetPowerTotal(EFicsitPowerTotal::Produced), P.GetPowerTotal(EFicsitPowerTotal::Produced));
			Test.TestEqual(TEXT("power per building"), Q.PowerProduced.Num() > 0 ? Q.PowerProduced[0] : -1.f, P.PowerProduced.Num() > 0 ? P.PowerProduced[0] : -2.f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistoryChunkRoundTripTest, "FicsitStatistics.HistoryChunk.FullSendRoundTripsAcrossChunks", FicsitHistoryChunkTests::Flags)
bool FFicsitHistoryChunkRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistoryChunkTests;
	const FFicsitHistory Server = MakeHistory(10);

	// Small chunks: (2 items * 3 + 1 building * 2 + power totals) values per point, so about 3 points per chunk.
	const TArray<FFicsitHistoryChunk> Chunks = FFicsitHistoryChunk::Build(Server, 0, -1.0, 3 * (2 * 3 + 2 + FicsitPowerTotalCount), 100);
	TestEqual(TEXT("10 points in chunks of 3"), Chunks.Num(), 4);
	TestTrue(TEXT("the first chunk resets the client's copy"), Chunks.Num() > 0 && Chunks[0].bReset);
	TestTrue(TEXT("only the first"), Chunks.Num() > 1 && !Chunks[1].bReset);
	TestFalse(TEXT("nothing left over"), Chunks.Num() > 0 && Chunks.Last().bMore);

	FFicsitHistory Client;
	ApplyAll(Chunks, Client);
	TestEqual(TEXT("item table"), Client.ItemTable.Num(), Server.ItemTable.Num());
	TestEqual(TEXT("tracked time"), Client.TrackedSeconds, Server.TrackedSeconds);
	SamePoints(*this, Server, Client);

	// The same chunks again (two requests in flight): points are kept once.
	TArray<FFicsitHistoryChunk> Again = Chunks;
	Again[0].bReset = false;
	ApplyAll(Again, Client);
	SamePoints(*this, Server, Client);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistoryChunkIncrementalTest, "FicsitStatistics.HistoryChunk.IncrementalSendHasOnlyNewerPoints", FicsitHistoryChunkTests::Flags)
bool FFicsitHistoryChunkIncrementalTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistoryChunkTests;
	const FFicsitHistory Server = MakeHistory(6);
	const double Since = Server.Tiers[0].GetOrdered(3).Time;

	const TArray<FFicsitHistoryChunk> Chunks = FFicsitHistoryChunk::Build(Server, 0, Since, 6000, 4);
	TestEqual(TEXT("one chunk"), Chunks.Num(), 1);
	TestFalse(TEXT("doesn't reset"), Chunks[0].bReset);
	TestEqual(TEXT("the two points after Since"), Chunks[0].Times.Num(), 2);

	const double Latest = Server.Tiers[0].GetOrdered(5).Time;
	const TArray<FFicsitHistoryChunk> NothingNew = FFicsitHistoryChunk::Build(Server, 0, Latest, 6000, 4);
	TestEqual(TEXT("nothing newer than the latest point: still one chunk, for the open point"), NothingNew.Num(), 1);
	TestEqual(TEXT("with no points"), NothingNew.Num() > 0 ? NothingNew[0].Times.Num() : -1, 0);
	TestEqual(TEXT("a missing tier: no chunk"), FFicsitHistoryChunk::Build(Server, 99, -1.0, 6000, 4).Num(), 0);

	FFicsitHistory Empty;
	Empty.EnsureLayout();
	TestEqual(TEXT("a full send of an empty tier still tells the client the tables"), FFicsitHistoryChunk::Build(Empty, 0, -1.0, 6000, 4).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistoryChunkMoreTest, "FicsitStatistics.HistoryChunk.ChunkLimitLeavesTheRestForTheNextRequest", FicsitHistoryChunkTests::Flags)
bool FFicsitHistoryChunkMoreTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistoryChunkTests;
	const FFicsitHistory Server = MakeHistory(10);
	const int32 ValuesPerPoint = 2 * 3 + 2 + FicsitPowerTotalCount;

	// Two chunks of 2 points per request, so the client asks again from the last point it got, like the RCO does.
	FFicsitHistory Client;
	TArray<FFicsitHistoryChunk> Chunks = FFicsitHistoryChunk::Build(Server, 0, -1.0, 2 * ValuesPerPoint, 2);
	int32 Requests = 1;
	while (Chunks.Num() > 0)
	{
		ApplyAll(Chunks, Client);
		if (!Chunks.Last().bMore || Requests > 10)
		{
			break;
		}
		TestEqual(TEXT("a full answer"), Chunks.Num(), 2);
		Chunks = FFicsitHistoryChunk::Build(Server, 0, Chunks.Last().Times.Last(), 2 * ValuesPerPoint, 2);
		++Requests;
	}
	TestEqual(TEXT("10 points, 4 per request"), Requests, 3);
	SamePoints(*this, Server, Client);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitHistoryChunkOpenManualTest, "FicsitStatistics.HistoryChunk.OpenPointManualAmountsReachTheClient", FicsitHistoryChunkTests::Flags)
bool FFicsitHistoryChunkOpenManualTest::RunTest(const FString& Parameters)
{
	using namespace FicsitHistoryChunkTests;
	FFicsitHistory Server = MakeHistory(3);
	// 7 of A crafted 2 s into a new 5 s point. The longest tier's first point is still hours from closing.
	FFicsitProductionSnapshot Craft;
	Craft.Manual[static_cast<int32>(EFicsitManualKind::Crafted)].Add(ItemA(), 7.f);
	Server.AddSample(Craft, 2.f);

	const int32 Longest = FFicsitHistory::NumTiers() - 1;
	auto Crafted = [Longest](const FFicsitHistory& History, UClass* Item)
	{
		TMap<TSubclassOf<UFGItemDescriptor>, FFicsitManualTotals> Totals;
		History.GetManualTotals(Longest, Totals);
		return Totals.FindRef(Item).Get(EFicsitManualKind::Crafted);
	};
	FFicsitHistory Client;
	ApplyAll(FFicsitHistoryChunk::Build(Server, Longest, -1.0, 6000, 4), Client);
	TestEqual(TEXT("the host counts the craft right away"), Crafted(Server, ItemA()), 7.f);
	TestEqual(TEXT("so does the client"), Crafted(Client, ItemA()), 7.f);
	TestEqual(TEXT("and the earlier one"), Crafted(Client, ItemB()), Crafted(Server, ItemB()));

	// An answer with no new points still brings the open point up to date, replacing what the client had.
	Server.AddSample(Craft, 1.f);
	ApplyAll(FFicsitHistoryChunk::Build(Server, Longest, 0.0, 6000, 4), Client);
	TestEqual(TEXT("both crafts, counted once"), Crafted(Client, ItemA()), 14.f);

	// An older answer arriving late doesn't take it back.
	FFicsitHistory Older = MakeHistory(3);
	ApplyAll(FFicsitHistoryChunk::Build(Older, Longest, 0.0, 6000, 4), Client);
	TestEqual(TEXT("still both crafts"), Crafted(Client, ItemA()), 14.f);
	return true;
}

#endif
