#include "FicsitRealtimeHistory.h"

FFicsitRealtimeHistory::FFicsitRealtimeHistory()
{
	History.Layout = FFicsitHistory::ELayout::Realtime;
}

void FFicsitRealtimeHistory::StartOver(double Now)
{
	History = FFicsitHistory();
	History.Layout = FFicsitHistory::ELayout::Realtime;
	Open = FFicsitLiveSecond();
	PointStart = Now;
	StartTime = Now;
}

void FFicsitRealtimeHistory::Advance(TConstArrayView<FFicsitLiveEvent> Events, double Now, const FFicsitProductionSnapshot& Reference)
{
	if (PointStart < 0.0 || Now - PointStart > SpanSeconds)
	{
		StartOver(Now);
	}

	for (const FFicsitLiveEvent& Event : Events)
	{
		if (Event.Time < StartTime || !Event.Item)
		{
			++Stats.Dropped;
			continue;
		}
		if (Event.Time < PointStart)
		{
			++Stats.Late;
		}
		++Stats.Placed;
		while (Event.Time >= PointStart + PointSeconds)
		{
			ClosePoint(Reference);
		}
		switch (static_cast<EFicsitLiveChannel>(FMath::Min<uint8>(Event.Channel, static_cast<uint8>(EFicsitLiveChannel::ManualBase))))
		{
		case EFicsitLiveChannel::Produced:
			Open.Produced.FindOrAdd(Event.Item) += Event.Amount;
			break;
		case EFicsitLiveChannel::Consumed:
			Open.Consumed.FindOrAdd(Event.Item) += Event.Amount;
			break;
		case EFicsitLiveChannel::Sunk:
			Open.Consumed.FindOrAdd(Event.Item) += Event.Amount;
			Open.Sunk.FindOrAdd(Event.Item) += Event.Amount;
			break;
		default:
		{
			const int32 Kind = Event.Channel - static_cast<uint8>(EFicsitLiveChannel::ManualBase);
			if (Kind < FicsitManualKindCount)
			{
				Open.Manual[Kind].FindOrAdd(Event.Item) += Event.Amount;
			}
			break;
		}
		}
	}

	while (Now >= PointStart + PointSeconds)
	{
		ClosePoint(Reference);
	}
}

void FFicsitRealtimeHistory::ClosePoint(const FFicsitProductionSnapshot& Reference)
{
	Open.Seconds = static_cast<float>(PointSeconds);
	FFicsitProductionSnapshot Sample;
	Open.ToSample(Reference, Reference.WorldTimeSeconds, Sample);
	History.AddSample(Sample, static_cast<float>(PointSeconds));
	if (bTallyProduced)
	{
		for (const auto& Pair : Open.Produced) { ProducedTally.FindOrAdd(Pair.Key) += Pair.Value; }
	}

	Open.Produced.Reset();
	Open.Consumed.Reset();
	Open.Sunk.Reset();
	for (TMap<UClass*, float>& Manual : Open.Manual)
	{
		Manual.Reset();
	}
	PointStart += PointSeconds;
}

FFicsitRealtimeHistory::FStats FFicsitRealtimeHistory::ConsumeStats()
{
	const FStats Result = Stats;
	Stats = FStats();
	return Result;
}

TMap<UClass*, float> FFicsitRealtimeHistory::TakeProducedTally()
{
	TMap<UClass*, float> Result = MoveTemp(ProducedTally);
	ProducedTally.Reset();
	return Result;
}
