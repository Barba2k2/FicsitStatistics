#include "FicsitStatisticsNet.h"

#include "FicsitStatistics.h"
#include "FicsitStatisticsConfig.h"
#include "FicsitStatisticsSubsystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 RatesPerSide = 2 + FicsitRateSourceCount * 2;
	constexpr int32 StatesPerSide = static_cast<int32>(EFicsitMachineState::Count);
	/** Keep each RPC well under the partial-bunch limits. */
	constexpr int32 MaxValuesPerChunk = 6000;
	/**
	 * A chunk is ~24 KB, about 25 reliable packets. The engine drops the connection when more than 256 reliable packets
	 * wait for an ack, so one answer sends only a few chunks and the client asks for the rest once they have arrived.
	 */
	constexpr int32 MaxChunksPerRequest = 4;
	/** Live seconds the server keeps replicating, so a client that misses an update still gets every second. */
	constexpr int32 LiveSecondsKept = 3;

	void PackFlow(const FFicsitItemFlow* Flow, TArray<float>& Out)
	{
		if (!Flow)
		{
			Out.AddZeroed(RatesPerSide);
			return;
		}
		Out.Add(Flow->Total.Actual);
		Out.Add(Flow->Total.Max);
		for (const FFicsitItemRate& Rate : Flow->BySource)
		{
			Out.Add(Rate.Actual);
			Out.Add(Rate.Max);
		}
	}

	void UnpackFlow(const TArray<float>& In, int32 Offset, FFicsitItemFlow& Flow)
	{
		Flow.Total.Actual = In[Offset];
		Flow.Total.Max = In[Offset + 1];
		for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
		{
			Flow.BySource[Source].Actual = In[Offset + 2 + Source * 2];
			Flow.BySource[Source].Max = In[Offset + 3 + Source * 2];
		}
	}

	void PackMachines(const FFicsitMachineCounts* Counts, TArray<int32>& Out)
	{
		for (int32 State = 0; State < StatesPerSide; ++State)
		{
			Out.Add(Counts ? Counts->ByState[State] : 0);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

FFicsitNetSnapshot FFicsitNetSnapshot::FromSnapshot(const FFicsitProductionSnapshot& Snapshot)
{
	FFicsitNetSnapshot Net;
	Net.WorldTimeSeconds = Snapshot.WorldTimeSeconds;
	Net.MachinesScanned.Append(Snapshot.MachinesScanned, FicsitRateSourceCount);
	Net.PowerTotals.Append(Snapshot.Power.Totals, FicsitPowerTotalCount);
	auto AddBuildings = [&Net](const FFicsitPowerBuildingMap& Map, bool bProducer)
	{
		for (const auto& Pair : Map)
		{
			FFicsitNetPowerBuilding& Entry = Net.PowerBuildings.AddDefaulted_GetRef();
			Entry.Building = Pair.Key;
			Entry.bProducer = bProducer;
			Entry.MW = Pair.Value.MW;
			Entry.MaxMW = Pair.Value.MaxMW;
			Entry.Count = Pair.Value.Count;
			Entry.Active = Pair.Value.Active;
		}
	};
	AddBuildings(Snapshot.Power.Producers, true);
	AddBuildings(Snapshot.Power.Consumers, false);

	for (const auto& ItemPair : Snapshot.RecipeOutput)
	{
		for (const auto& RecipePair : ItemPair.Value)
		{
			FFicsitNetRecipeOutput& Entry = Net.RecipeOutput.AddDefaulted_GetRef();
			Entry.Item = ItemPair.Key;
			Entry.Recipe = RecipePair.Key;
			Entry.MaxRate = RecipePair.Value;
		}
	}

	Net.StorageTrendSeconds = Snapshot.StorageTrendSeconds;
	Net.StoredItems.Reserve(Snapshot.Stored.Num());
	Net.StoredAmounts.Reserve(Snapshot.Stored.Num());
	Net.StoredPerMinute.Reserve(Snapshot.Stored.Num());
	for (const auto& Pair : Snapshot.Stored)
	{
		Net.StoredItems.Add(Pair.Key);
		Net.StoredAmounts.Add(Pair.Value.Amount);
		Net.StoredPerMinute.Add(Pair.Value.PerMinute);
	}

	TSet<TSubclassOf<UFGItemDescriptor>> Items;
	for (const auto& Pair : Snapshot.Produced) { Items.Add(Pair.Key); }
	for (const auto& Pair : Snapshot.Consumed) { Items.Add(Pair.Key); }

	Net.Items.Reserve(Items.Num());
	for (const TSubclassOf<UFGItemDescriptor>& Item : Items)
	{
		FFicsitNetItem& Entry = Net.Items.AddDefaulted_GetRef();
		Entry.Item = Item;
		Entry.Rates.Reserve(RatesPerSide * 2);
		PackFlow(Snapshot.Produced.Find(Item), Entry.Rates);
		PackFlow(Snapshot.Consumed.Find(Item), Entry.Rates);
		Entry.Machines.Reserve(StatesPerSide * 2);
		PackMachines(Snapshot.Producers.Find(Item), Entry.Machines);
		PackMachines(Snapshot.Consumers.Find(Item), Entry.Machines);
	}
	return Net;
}

void FFicsitNetSnapshot::ToSnapshot(FFicsitProductionSnapshot& Out) const
{
	Out = FFicsitProductionSnapshot();
	Out.WorldTimeSeconds = WorldTimeSeconds;
	for (int32 Index = 0; Index < FicsitRateSourceCount && Index < MachinesScanned.Num(); ++Index)
	{
		Out.MachinesScanned[Index] = MachinesScanned[Index];
	}
	for (int32 Index = 0; Index < FicsitPowerTotalCount && Index < PowerTotals.Num(); ++Index)
	{
		Out.Power.Totals[Index] = PowerTotals[Index];
	}
	for (const FFicsitNetPowerBuilding& Entry : PowerBuildings)
	{
		if (Entry.Building)
		{
			FFicsitPowerBuilding& Building = (Entry.bProducer ? Out.Power.Producers : Out.Power.Consumers).FindOrAdd(Entry.Building);
			Building.MW = Entry.MW;
			Building.MaxMW = Entry.MaxMW;
			Building.Count = Entry.Count;
			Building.Active = Entry.Active;
		}
	}
	for (const FFicsitNetRecipeOutput& Entry : RecipeOutput)
	{
		if (Entry.Item && Entry.Recipe)
		{
			Out.RecipeOutput.FindOrAdd(Entry.Item).Add(Entry.Recipe, Entry.MaxRate);
		}
	}
	Out.StorageTrendSeconds = StorageTrendSeconds;
	const int32 StoredCount = FMath::Min3(StoredItems.Num(), StoredAmounts.Num(), StoredPerMinute.Num());
	for (int32 Index = 0; Index < StoredCount; ++Index)
	{
		if (StoredItems[Index])
		{
			Out.Stored.Add(StoredItems[Index], {StoredAmounts[Index], StoredPerMinute[Index]});
		}
	}

	for (const FFicsitNetItem& Entry : Items)
	{
		if (!Entry.Item || Entry.Rates.Num() < RatesPerSide * 2)
		{
			continue;
		}
		FFicsitItemFlow Produced, Consumed;
		UnpackFlow(Entry.Rates, 0, Produced);
		UnpackFlow(Entry.Rates, RatesPerSide, Consumed);
		if (Produced.Total.Max > 0.f || Produced.Total.Actual > 0.f) { Out.Produced.Add(Entry.Item, Produced); }
		if (Consumed.Total.Max > 0.f || Consumed.Total.Actual > 0.f) { Out.Consumed.Add(Entry.Item, Consumed); }

		if (Entry.Machines.Num() >= StatesPerSide * 2)
		{
			FFicsitMachineCounts Producers, Consumers;
			for (int32 State = 0; State < StatesPerSide; ++State)
			{
				Producers.ByState[State] = Entry.Machines[State];
				Consumers.ByState[State] = Entry.Machines[StatesPerSide + State];
			}
			if (Producers.Total() > 0) { Out.Producers.Add(Entry.Item, Producers); }
			if (Consumers.Total() > 0) { Out.Consumers.Add(Entry.Item, Consumers); }
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

FFicsitNetLiveSecond FFicsitNetLiveSecond::FromLive(const FFicsitLiveSecond& Live)
{
	FFicsitNetLiveSecond Out;
	Out.Seq = Live.Seq;
	Out.Seconds = Live.Seconds;

	TSet<UClass*> Items;
	for (const auto& Pair : Live.Produced) { Items.Add(Pair.Key); }
	for (const auto& Pair : Live.Consumed) { Items.Add(Pair.Key); }
	for (UClass* Item : Items)
	{
		Out.Items.Add(TSubclassOf<UFGItemDescriptor>(Item));
		Out.Produced.Add(Live.Produced.FindRef(Item));
		Out.Consumed.Add(Live.Consumed.FindRef(Item));
		Out.Sunk.Add(Live.Sunk.FindRef(Item));
	}
	for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
	{
		for (const auto& Pair : Live.Manual[Kind])
		{
			Out.ManualItems.Add(TSubclassOf<UFGItemDescriptor>(Pair.Key));
			Out.ManualKinds.Add(static_cast<uint8>(Kind));
			Out.ManualAmounts.Add(Pair.Value);
		}
	}
	return Out;
}

void FFicsitNetLiveSecond::ToLive(FFicsitLiveSecond& Out) const
{
	Out = FFicsitLiveSecond();
	Out.Seq = Seq;
	Out.Seconds = Seconds;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		UClass* Item = Items[Index].Get();
		if (!Item)
		{
			continue;
		}
		if (Produced.IsValidIndex(Index) && Produced[Index] > 0.f) { Out.Produced.Add(Item, Produced[Index]); }
		if (Consumed.IsValidIndex(Index) && Consumed[Index] > 0.f) { Out.Consumed.Add(Item, Consumed[Index]); }
		if (Sunk.IsValidIndex(Index) && Sunk[Index] > 0.f) { Out.Sunk.Add(Item, Sunk[Index]); }
	}
	for (int32 Index = 0; Index < ManualItems.Num() && Index < ManualKinds.Num() && Index < ManualAmounts.Num(); ++Index)
	{
		if (UClass* Item = ManualItems[Index].Get(); Item && ManualKinds[Index] < FicsitManualKindCount)
		{
			Out.Manual[ManualKinds[Index]].Add(Item, ManualAmounts[Index]);
		}
	}
}

void FFicsitNetLive::Push(const FFicsitLiveSecond& Live)
{
	Seconds.Add(FFicsitNetLiveSecond::FromLive(Live));
	if (Seconds.Num() > LiveSecondsKept)
	{
		Seconds.RemoveAt(0, Seconds.Num() - LiveSecondsKept);
	}
}

TArray<FFicsitLiveSecond> FFicsitNetLive::TakeNew(int32& InOutLastSeq) const
{
	TArray<FFicsitLiveSecond> New;
	if (Seconds.Num() == 0)
	{
		return New;
	}
	if (Seconds.Last().Seq < InOutLastSeq)
	{
		InOutLastSeq = 0;
	}
	for (const FFicsitNetLiveSecond& Second : Seconds)
	{
		if (Second.Seq > InOutLastSeq)
		{
			Second.ToLive(New.AddDefaulted_GetRef());
			InOutLastSeq = Second.Seq;
		}
	}
	return New;
}

// ---------------------------------------------------------------------------------------------------------------------

void UFicsitStatsRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFicsitStatsRCO, bDummy);
}

void UFicsitStatsRCO::Server_RequestHistory_Implementation(int32 Tier, double SinceTime)
{
	AFicsitStatisticsSubsystem* Subsystem = AFicsitStatisticsSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	const FFicsitHistory& History = Subsystem->GetHistory();
	if (!History.Tiers.IsValidIndex(Tier))
	{
		return;
	}

	if (!HistoryRequests.Accept(Tier, SinceTime, Subsystem->GetWorld()->GetTimeSeconds()))
	{
		return;
	}
	const TArray<FFicsitHistoryChunk> Chunks = FFicsitHistoryChunk::Build(History, Tier, SinceTime, MaxValuesPerChunk, MaxChunksPerRequest);
	HistoryRequests.Answered(Tier, Chunks.Num() > 0 && Chunks.Last().bMore);
	for (const FFicsitHistoryChunk& Chunk : Chunks)
	{
		Client_ReceiveHistory(Chunk);
	}
}

TArray<FFicsitHistoryChunk> FFicsitHistoryChunk::Build(const FFicsitHistory& History, int32 Tier, double SinceTime, int32 MaxValues, int32 MaxChunks)
{
	TArray<FFicsitHistoryChunk> Chunks;
	if (!History.Tiers.IsValidIndex(Tier))
	{
		return Chunks;
	}
	const FFicsitHistoryTier& Source = History.Tiers[Tier];
	const int32 ItemCount = FMath::Max(1, History.ItemTable.Num());
	const int32 BuildingCount = History.BuildingTable.Num();
	const int32 PointsPerChunk = FMath::Max(1, MaxValues / (ItemCount * 3 + BuildingCount * 2 + FicsitPowerTotalCount));
	const bool bFullSend = SinceTime < 0.0;

	auto StartChunk = [&]() -> FFicsitHistoryChunk&
	{
		FFicsitHistoryChunk& Chunk = Chunks.AddDefaulted_GetRef();
		Chunk.Tier = Tier;
		// Only the first chunk of a full send clears the client's copy.
		Chunk.bReset = bFullSend && Chunks.Num() == 1;
		Chunk.TrackedSeconds = History.TrackedSeconds;
		Chunk.ItemTable = History.ItemTable;
		Chunk.BuildingTable = History.BuildingTable;
		for (int32 Item = 0; Item < History.ItemTable.Num(); ++Item)
		{
			for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
			{
				const float Value = Source.Accumulator.GetManual(static_cast<EFicsitManualKind>(Kind), Item);
				if (Value > 0.f)
				{
					Chunk.OpenManualKeys.Add(Item * FicsitManualKindCount + Kind);
					Chunk.OpenManualValues.Add(Value);
				}
			}
		}
		return Chunk;
	};

	FFicsitHistoryChunk* Chunk = &StartChunk();
	for (int32 Index = 0; Index < Source.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Source.GetOrdered(Index);
		if (!bFullSend && Point.Time <= SinceTime + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (Chunk->Times.Num() >= PointsPerChunk)
		{
			if (Chunks.Num() >= MaxChunks)
			{
				// Points are oldest first, so everything from here on is newer than SinceTime too: the client asks again.
				Chunk->bMore = true;
				return Chunks;
			}
			Chunk = &StartChunk();
		}
		const int32 PointInChunk = Chunk->Times.Num();
		Chunk->Times.Add(Point.Time);
		for (int32 Total = 0; Total < FicsitPowerTotalCount; ++Total)
		{
			Chunk->PowerTotals.Add(Point.GetPowerTotal(static_cast<EFicsitPowerTotal>(Total)));
		}
		for (int32 Building = 0; Building < BuildingCount; ++Building)
		{
			Chunk->PowerProduced.Add(Point.PowerProduced.IsValidIndex(Building) ? Point.PowerProduced[Building] : 0.f);
			Chunk->PowerConsumed.Add(Point.PowerConsumed.IsValidIndex(Building) ? Point.PowerConsumed[Building] : 0.f);
		}
		for (int32 Item = 0; Item < ItemCount; ++Item)
		{
			Chunk->Produced.Add(Point.GetProduced(Item));
			Chunk->Consumed.Add(Point.GetConsumed(Item));
			Chunk->Sunk.Add(Point.GetSunk(Item));
			// Manual amounts are sparse (most are zero): key = (point in chunk * items + item) * kinds + kind.
			for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
			{
				const float Value = Point.GetManual(static_cast<EFicsitManualKind>(Kind), Item);
				if (Value > 0.f)
				{
					Chunk->ManualKeys.Add((PointInChunk * ItemCount + Item) * FicsitManualKindCount + Kind);
					Chunk->ManualValues.Add(Value);
				}
			}
		}
	}
	return Chunks;
}

bool FFicsitHistoryChunk::ApplyTo(FFicsitHistory& History) const
{
	History.EnsureLayout();
	if (!History.Tiers.IsValidIndex(Tier))
	{
		return false;
	}
	// Two requests in flight can arrive out of order. Only the newer one's tables, time and open point count, otherwise
	// amounts the server has since moved into a closed point would be counted twice.
	const bool bNewest = bReset || TrackedSeconds >= History.TrackedSeconds;
	if (bNewest)
	{
		History.ItemTable = ItemTable;
		History.BuildingTable = BuildingTable;
		History.TrackedSeconds = TrackedSeconds;
	}

	FFicsitHistoryTier& Target = History.Tiers[Tier];
	if (bReset)
	{
		Target.Points.Reset();
		Target.Head = 0;
	}
	if (bNewest)
	{
		// The server's open point is the whole of it, so it replaces what the client had.
		for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
		{
			Target.Accumulator.Manual(static_cast<EFicsitManualKind>(Kind)).Reset();
		}
		for (int32 Entry = 0; Entry < OpenManualKeys.Num() && Entry < OpenManualValues.Num(); ++Entry)
		{
			const int32 Item = OpenManualKeys[Entry] / FicsitManualKindCount;
			TArray<float>& Values = Target.Accumulator.Manual(static_cast<EFicsitManualKind>(OpenManualKeys[Entry] % FicsitManualKindCount));
			if (Values.Num() <= Item)
			{
				Values.SetNumZeroed(Item + 1);
			}
			Values[Item] = OpenManualValues[Entry];
		}
	}

	const int32 ItemCount = FMath::Max(1, ItemTable.Num());
	const int32 BuildingCount = BuildingTable.Num();
	for (int32 Point = 0; Point < Times.Num(); ++Point)
	{
		// Two requests in flight can send the same points; keep each one once.
		if (Target.Num() > 0 && Times[Point] <= Target.GetOrdered(Target.Num() - 1).Time + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		FFicsitHistoryPoint NewPoint;
		NewPoint.Time = Times[Point];
		const int32 Offset = Point * ItemCount;
		if (Produced.Num() >= Offset + ItemCount && Consumed.Num() >= Offset + ItemCount)
		{
			NewPoint.Produced.Append(&Produced[Offset], ItemCount);
			NewPoint.Consumed.Append(&Consumed[Offset], ItemCount);
			if (Sunk.Num() >= Offset + ItemCount)
			{
				NewPoint.Sunk.Append(&Sunk[Offset], ItemCount);
			}
		}
		if (PowerTotals.Num() >= (Point + 1) * FicsitPowerTotalCount)
		{
			NewPoint.PowerTotals.Append(&PowerTotals[Point * FicsitPowerTotalCount], FicsitPowerTotalCount);
		}
		if (BuildingCount > 0 && PowerProduced.Num() >= (Point + 1) * BuildingCount && PowerConsumed.Num() >= (Point + 1) * BuildingCount)
		{
			NewPoint.PowerProduced.Append(&PowerProduced[Point * BuildingCount], BuildingCount);
			NewPoint.PowerConsumed.Append(&PowerConsumed[Point * BuildingCount], BuildingCount);
		}
		for (int32 Entry = 0; Entry < ManualKeys.Num() && Entry < ManualValues.Num(); ++Entry)
		{
			const int32 Key = ManualKeys[Entry];
			if (Key / FicsitManualKindCount / ItemCount != Point)
			{
				continue;
			}
			const int32 Item = (Key / FicsitManualKindCount) % ItemCount;
			TArray<float>& Values = NewPoint.Manual(static_cast<EFicsitManualKind>(Key % FicsitManualKindCount));
			if (Values.Num() <= Item)
			{
				Values.SetNumZeroed(Item + 1);
			}
			Values[Item] = ManualValues[Entry];
		}
		Target.Push(MoveTemp(NewPoint));
	}
	return true;
}

void UFicsitStatsRCO::Client_ReceiveHistory_Implementation(const FFicsitHistoryChunk& Chunk)
{
	AFicsitStatisticsSubsystem* Subsystem = AFicsitStatisticsSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	// The server had more to send: ask for the next few.
	if (const TOptional<double> SinceTime = Subsystem->ApplyHistoryChunk(Chunk))
	{
		Server_RequestHistory(Chunk.Tier, *SinceTime);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

UFicsitStatisticsGameInstanceModule::UFicsitStatisticsGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.Add(UFicsitStatsRCO::StaticClass());
	ModConfigurations.Add(UFicsitStatsModConfiguration::StaticClass());
}

UFicsitStatisticsGameWorldModule::UFicsitStatisticsGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(AFicsitStatisticsSubsystem::StaticClass());
}

// ---------------------------------------------------------------------------------------------------------------------

namespace
{
	/**
	 * Clients ask for a tier this often while a view shows it. That's more often than a long tier gets new points, but
	 * every answer also brings the tier's open point, so a fresh craft shows up in the long ranges within seconds.
	 */
	constexpr double MinSecondsBetweenClientRequests = 5.0;
	/** The server ignores requests closer together than this, unless they follow up an answer that had more to send. */
	constexpr double MinSecondsBetweenServerAnswers = 2.0;
}

TOptional<double> FFicsitClientHistory::NextRequest(int32 Tier, double Now)
{
	History.EnsureLayout();
	if (!History.Tiers.IsValidIndex(Tier))
	{
		return {};
	}
	if (const double* Last = LastRequestTime.Find(Tier); Last && Now - *Last < MinSecondsBetweenClientRequests)
	{
		return {};
	}
	LastRequestTime.Add(Tier, Now);
	const FFicsitHistoryTier& Existing = History.Tiers[Tier];
	return Existing.Num() > 0 ? Existing.GetOrdered(Existing.Num() - 1).Time : -1.0;
}

TOptional<double> FFicsitClientHistory::Apply(const FFicsitHistoryChunk& Chunk, double Now)
{
	if (!Chunk.ApplyTo(History) || !Chunk.bMore || Chunk.Times.Num() == 0)
	{
		return {};
	}
	// The follow-up goes out now. A regular request on top of it would overlap.
	LastRequestTime.Add(Chunk.Tier, Now);
	return Chunk.Times.Last();
}

bool FFicsitHistoryRequests::Accept(int32 Tier, double SinceTime, double Now)
{
	FRequest& Last = Requests.FindOrAdd(Tier);
	const bool bFollowUp = Last.bMore && SinceTime >= 0.0 && Now > Last.Time;
	if (!bFollowUp && Now - Last.Time < MinSecondsBetweenServerAnswers)
	{
		return false;
	}
	Last.Time = Now;
	Last.bMore = false;
	return true;
}

void FFicsitHistoryRequests::Answered(int32 Tier, bool bMore)
{
	Requests.FindOrAdd(Tier).bMore = bMore;
}
