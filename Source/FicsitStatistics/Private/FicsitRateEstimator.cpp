#include "FicsitRateEstimator.h"

FFicsitRateEstimator::FFicsitRateEstimator()
	: FFicsitRateEstimator(FSettings())
{
}

FFicsitRateEstimator::FFicsitRateEstimator(FSettings InSettings)
	: Settings(MoveTemp(InSettings))
{
}

void FFicsitRateEstimator::Reset(double InStartTime)
{
	StartTime = InStartTime;
	Buckets.Reset();
	WindowStart.Reset();
	WindowSeconds.Reset();
	PendingCycleSeconds.Reset();
	CycleSeconds.Reset();
	Restarts = 0;
}

void FFicsitRateEstimator::SetSettings(FSettings InSettings)
{
	Settings = MoveTemp(InSettings);
}

void FFicsitRateEstimator::NoteCycle(UClass* Item, float InCycleSeconds)
{
	float& Slowest = PendingCycleSeconds.FindOrAdd(Item);
	Slowest = FMath::Max(Slowest, InCycleSeconds);
}

const FFicsitCounts& FFicsitRateEstimator::GetLatestCounts() const
{
	static const FFicsitCounts Empty;
	return Buckets.Num() > 0 ? Buckets.Last().Counts : Empty;
}

double FFicsitRateEstimator::GetWindowSeconds(UClass* Item) const
{
	return WindowSeconds.FindRef(Item);
}

float FFicsitRateEstimator::GetCycleSeconds(UClass* Item) const
{
	return CycleSeconds.FindRef(Item);
}

FFicsitRateEstimator::FStats FFicsitRateEstimator::ConsumeStats()
{
	FStats Stats;
	Stats.Restarts = Restarts;
	Stats.Items = WindowSeconds.Num();
	TArray<double> Spans;
	WindowSeconds.GenerateValueArray(Spans);
	Spans.Sort();
	Stats.MedianWindowSeconds = Spans.Num() > 0 ? Spans[Spans.Num() / 2] : 0.0;
	Restarts = 0;
	return Stats;
}

void FFicsitRateEstimator::Apply(FFicsitCounts&& Counts, FFicsitProductionSnapshot& Snapshot)
{
	const double Now = Snapshot.WorldTimeSeconds;
	const double Elapsed = Now - StartTime;
	const double Interval = Settings.BucketSeconds;
	CycleSeconds = MoveTemp(PendingCycleSeconds);
	PendingCycleSeconds.Reset();

	FBucket& Bucket = Buckets.AddDefaulted_GetRef();
	Bucket.Time = Now;
	Bucket.Counts = MoveTemp(Counts);

	// Manual amounts aren't rates: they go straight into this sample (and the history), not into the rate windows.
	for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
	{
		for (const auto& Pair : Bucket.Counts.Manual[Kind])
		{
			Snapshot.Manual[Kind].FindOrAdd(Pair.Key) += static_cast<float>(Pair.Value);
		}
		Bucket.Counts.Manual[Kind].Reset();
	}

	// Keep enough buckets for the longest window (half an interval of slack for timer jitter).
	const double Slack = Interval * 0.5;
	const double LongestWindow = Settings.MaxWindowSeconds;
	Buckets.RemoveAll([Now, LongestWindow, Slack](const FBucket& Old) { return Now - Old.Time > LongestWindow - Slack; });

	auto SumOver = [this, Now, Slack](bool bProduced, int32 Source, UClass* Item, double Window)
	{
		double Sum = 0.0;
		for (const FBucket& Each : Buckets)
		{
			if (Now - Each.Time <= Window - Slack)
			{
				const TMap<UClass*, double>& Map = bProduced ? Each.Counts.Produced[Source] : Each.Counts.Consumed[Source];
				if (const double* Value = Map.Find(Item))
				{
					Sum += *Value;
				}
			}
		}
		return Sum;
	};

	// Every item that has counts anywhere in the kept buckets, per direction and source.
	TSet<UClass*> CountedItems[2][FicsitRateSourceCount];
	for (const FBucket& Each : Buckets)
	{
		for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
		{
			for (const auto& Pair : Each.Counts.Produced[Source]) { CountedItems[0][Source].Add(Pair.Key); }
			for (const auto& Pair : Each.Counts.Consumed[Source]) { CountedItems[1][Source].Add(Pair.Key); }
		}
	}

	const int32 CountedSources[] = {static_cast<int32>(EFicsitRateSource::Manufacturing), static_cast<int32>(EFicsitRateSource::Extraction)};
	const int32 EventSources[] = {static_cast<int32>(EFicsitRateSource::ResourceSink), static_cast<int32>(EFicsitRateSource::SpaceElevator)};
	auto TotalOver = [&](bool bProduced, UClass* Item, double Window)
	{
		double Sum = 0.0;
		for (const int32 Source : CountedSources) { Sum += SumOver(bProduced, Source, Item, Window); }
		if (!bProduced)
		{
			for (const int32 Source : EventSources) { Sum += SumOver(false, Source, Item, Window); }
		}
		return Sum;
	};
	auto EventsOver = [this, Now, Slack](bool bProduced, UClass* Item, double Window)
	{
		int64 Sum = 0;
		for (const FBucket& Each : Buckets)
		{
			if (Now - Each.Time <= Window - Slack)
			{
				Sum += Each.Counts.Events[bProduced ? 0 : 1].FindRef(Item);
			}
		}
		return Sum;
	};
	// Windows are whole buckets: a window that ends mid-bucket would sum fewer seconds than it divides by.
	auto ToBuckets = [Interval](double Seconds) { return FMath::CeilToDouble(Seconds / Interval - 0.01) * Interval; };

	TSet<UClass*> WindowItems;
	for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
	{
		WindowItems.Append(CountedItems[0][Source]);
		WindowItems.Append(CountedItems[1][Source]);
	}
	for (const FFicsitItemFlowMap* Flows : {&Snapshot.Produced, &Snapshot.Consumed})
	{
		for (const auto& Pair : *Flows)
		{
			for (const int32 Source : CountedSources)
			{
				if (Pair.Value.BySource[Source].Max > 0.f) { WindowItems.Add(Pair.Key.Get()); }
			}
		}
	}

	TSet<UClass*> ReadyItems; // items whose window holds enough counts to replace the scan's warm-up estimate
	TMap<UClass*, double> NewWindowSeconds;
	for (UClass* Item : WindowItems)
	{
		double& Start = WindowStart.FindOrAdd(Item, StartTime);
		Start = FMath::Max(Start, Now - Settings.MaxWindowSeconds);
		double Span = FMath::Max(Now - Start, 0.0);
		const float* Cycle = CycleSeconds.Find(Item);
		const double ShortFloor = FMath::Max<double>(Settings.MinShortWindowSeconds, Cycle ? 2.0 * *Cycle : 0.0);

		// The short window: long enough for ~12 expected events, so slow items get a fair test.
		double ShortWindow = ShortFloor;
		for (const bool bProduced : {true, false})
		{
			const double EventsPerSecond = Span > 0.0 ? static_cast<double>(EventsOver(bProduced, Item, Span)) / Span : 0.0;
			if (EventsPerSecond > 0.0)
			{
				ShortWindow = FMath::Max(ShortWindow, 12.0 / EventsPerSecond);
			}
		}
		ShortWindow = ToBuckets(FMath::Clamp(ShortWindow, FMath::Max(ShortFloor, 2.0 * Interval), FMath::Max(ShortFloor, 120.0)));

		// A change in either direction restarts the item's window at the start of the short window. Noise is Poisson in
		// events, each event carrying the item's average amount: sd = sqrt(expected events) x event size.
		if (Span >= 2.0 * ShortWindow)
		{
			for (const bool bProduced : {true, false})
			{
				const double LongAmount = TotalOver(bProduced, Item, Span);
				const int64 LongEvents = EventsOver(bProduced, Item, Span);
				const double Recent = TotalOver(bProduced, Item, ShortWindow);
				const int64 RecentEvents = EventsOver(bProduced, Item, ShortWindow);
				bool bChanged;
				if (LongEvents == 0)
				{
					bChanged = RecentEvents >= 2; // started from nothing
				}
				else
				{
					const double EventSize = LongAmount / LongEvents;
					const double Expected = LongAmount / Span * ShortWindow;
					const double Sd = FMath::Sqrt(Expected / EventSize) * EventSize;
					bChanged = FMath::Abs(Recent - Expected) > Settings.ChangeThreshold * FMath::Max(Sd, 2.0 * EventSize);
				}
				if (bChanged)
				{
					Start = Now - ShortWindow;
					Span = ShortWindow;
					++Restarts;
					break;
				}
			}
		}
		NewWindowSeconds.Add(Item, Span);
		if (Span + Slack >= ShortWindow)
		{
			ReadyItems.Add(Item);
		}
	}
	WindowSeconds = MoveTemp(NewWindowSeconds);
	// Items nothing makes or uses any more drop out of the window bookkeeping.
	for (auto It = WindowStart.CreateIterator(); It; ++It)
	{
		if (!WindowItems.Contains(It.Key())) { It.RemoveCurrent(); }
	}

	// Manufacturing and extraction: the scan's estimate stays as a warm-up value until the item's window is ready, then
	// the counted rate replaces it. Max (100% productivity) always comes from the scan.
	for (int32 Direction = 0; Direction < 2; ++Direction)
	{
		const bool bProduced = Direction == 0;
		FFicsitItemFlowMap& Flows = bProduced ? Snapshot.Produced : Snapshot.Consumed;
		for (const int32 Source : CountedSources)
		{
			for (auto& Pair : Flows)
			{
				FFicsitItemRate& Rate = Pair.Value.BySource[Source];
				UClass* Item = Pair.Key.Get();
				if (Rate.Max <= 0.f && Rate.Actual <= 0.f && !CountedItems[Direction][Source].Contains(Item))
				{
					continue;
				}
				if (!ReadyItems.Contains(Item))
				{
					continue; // warm-up: keep the estimate
				}
				const double Window = GetWindowSeconds(Item);
				Rate.Actual = static_cast<float>(SumOver(bProduced, Source, Item, Window) * 60.0 / Window);
			}
			// Counted flows the scan no longer sees (e.g. the machine was just dismantled) still happened.
			for (UClass* Item : CountedItems[Direction][Source])
			{
				if (!Flows.Contains(Item) && ReadyItems.Contains(Item))
				{
					const double Window = GetWindowSeconds(Item);
					const float Value = static_cast<float>(SumOver(bProduced, Source, Item, Window) * 60.0 / Window);
					if (Value > 0.f)
					{
						Flows.FindOrAdd(Item).BySource[Source].Actual = Value;
					}
				}
			}
		}
	}

	// AWESOME Sink and Space Elevator only exist as counts (raw amounts, converted for display). Same per-item window as
	// the item's production, so the Sink's share lines up with what is being made.
	for (const EFicsitRateSource Source : {EFicsitRateSource::ResourceSink, EFicsitRateSource::SpaceElevator})
	{
		const int32 SourceIndex = static_cast<int32>(Source);
		for (UClass* Item : CountedItems[1][SourceIndex])
		{
			const double Window = GetWindowSeconds(Item);
			const double Covered = FMath::Max(1.0, FMath::Min(Window, Elapsed));
			float Rate = static_cast<float>(SumOver(false, SourceIndex, Item, Window) * 60.0 / Covered);
			if (Settings.IsFluid && Settings.IsFluid(Item))
			{
				Rate /= 1000.f; // liters to m³
			}
			if (Rate > 0.f)
			{
				FFicsitItemRate& Entry = Snapshot.Consumed.FindOrAdd(Item).BySource[SourceIndex];
				Entry.Actual = Rate;
				Entry.Max = Rate;
			}
		}
	}

	// Totals are the sum of their sources.
	for (FFicsitItemFlowMap* Flows : {&Snapshot.Produced, &Snapshot.Consumed})
	{
		for (auto& Pair : *Flows)
		{
			FFicsitItemFlow& Flow = Pair.Value;
			Flow.Total = FFicsitItemRate();
			for (const FFicsitItemRate& Rate : Flow.BySource)
			{
				Flow.Total.Add(Rate.Actual, Rate.Max);
			}
		}
	}
}
