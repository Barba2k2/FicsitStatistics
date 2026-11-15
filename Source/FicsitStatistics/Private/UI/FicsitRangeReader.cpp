#include "UI/FicsitRangeReader.h"

#include "FicsitStatisticsSubsystem.h"
#include "UI/FicsitStatsStyle.h"

bool FFicsitRangeReader::IsAvailable(const AFicsitStatisticsSubsystem* Subsystem, EFicsitTimeRange Range)
{
	return !Subsystem || Subsystem->HasRange(Range);
}

bool FFicsitRangeReader::UpdatesEveryFrame(EFicsitTimeRange Range)
{
	return Range == EFicsitTimeRange::FiveSeconds;
}

FFicsitRangeReader::FFicsitRangeReader(AFicsitStatisticsSubsystem& InSubsystem, EFicsitTimeRange InRange)
	: FFicsitRangeReader(InSubsystem, InRange, 0.f)
{
}

FFicsitRangeReader FFicsitRangeReader::Recent(AFicsitStatisticsSubsystem& Subsystem, float Seconds)
{
	// The finest saved tier backs the 10m range, so "recent" is the end of that.
	return FFicsitRangeReader(Subsystem, EFicsitTimeRange::TenMinutes, FMath::Max(Seconds, KINDA_SMALL_NUMBER));
}

FFicsitRangeReader::FFicsitRangeReader(const FFicsitHistory& InHistory, const FFicsitProductionSnapshot& InLatestScan, EFicsitTimeRange InRange, float InMaxSeconds)
	: History(InHistory)
	, LatestScan(InLatestScan)
	, Range(InRange)
	, Tier(InHistory.GetTierForRange(InRange))
	, MaxSeconds(InMaxSeconds)
{
}

FFicsitRangeReader::FFicsitRangeReader(AFicsitStatisticsSubsystem& InSubsystem, EFicsitTimeRange InRange, float InMaxSeconds)
	// The references stay valid: they're the subsystem's own members.
	: FFicsitRangeReader(InSubsystem.GetFreshHistory(InRange), InSubsystem.GetLatestSnapshot(), InRange, InMaxSeconds)
{
}

bool FFicsitRangeReader::HasPoints() const
{
	return History.Tiers.IsValidIndex(Tier) && History.Tiers[Tier].Num() > 0;
}

bool FFicsitRangeReader::SmoothsForDisplay() const
{
	// The saved history's 5 s points are exact counts, so they're jagged. The 5s and 1m ranges are never smoothed.
	return History.Layout == FFicsitHistory::ELayout::Saved && History.Tiers.IsValidIndex(Tier) && History.Tiers[Tier].IntervalSeconds < 30.f;
}

void FFicsitRangeReader::ReadAverages() const
{
	if (bAveragesRead)
	{
		return;
	}
	bAveragesRead = true;
	History.GetAverages(Tier, Produced, Consumed, &Sunk, MaxSeconds);

	// Right after loading, a range may have no points yet: show the latest scan instead. A live range with points but
	// nothing moving really is zero, so only a saved range with nothing in it falls back when it has points.
	const bool bLiveRange = History.Layout != FFicsitHistory::ELayout::Saved;
	if (HasPoints() && (bLiveRange || Produced.Num() > 0 || Consumed.Num() > 0))
	{
		return;
	}
	const FFicsitProductionSnapshot& Snapshot = LatestScan;
	Produced.Reset();
	Consumed.Reset();
	Sunk.Reset();
	for (const auto& Pair : Snapshot.Produced)
	{
		if (Pair.Value.Total.Actual > 0.f) { Produced.Add(Pair.Key, Pair.Value.Total); }
	}
	for (const auto& Pair : Snapshot.Consumed)
	{
		if (Pair.Value.Total.Actual > 0.f) { Consumed.Add(Pair.Key, Pair.Value.Total); }
		const float SinkRate = Pair.Value.BySource[static_cast<int32>(EFicsitRateSource::ResourceSink)].Actual;
		if (SinkRate > 0.f) { Sunk.Add(Pair.Key, SinkRate); }
	}
}

const FFicsitRangeReader::FRateMap& FFicsitRangeReader::GetProduced() const
{
	ReadAverages();
	return Produced;
}

const FFicsitRangeReader::FRateMap& FFicsitRangeReader::GetConsumed() const
{
	ReadAverages();
	return Consumed;
}

const FFicsitRangeReader::FAmountMap& FFicsitRangeReader::GetSunk() const
{
	ReadAverages();
	return Sunk;
}

FFicsitRangeRates FFicsitRangeReader::GetRates(FItem Item) const
{
	ReadAverages();
	FFicsitRangeRates Rates;
	if (const FFicsitItemRate* P = Produced.Find(Item)) { Rates.Produced = P->Actual; }
	if (const FFicsitItemRate* C = Consumed.Find(Item)) { Rates.Consumed = C->Actual; }
	Rates.Sunk = Sunk.FindRef(Item);
	return Rates;
}

void FFicsitRangeReader::GetManualTotals(TMap<FItem, FFicsitManualTotals>& OutTotals) const
{
	History.GetManualTotals(Tier, OutTotals);
}

void FFicsitRangeReader::GetPowerAverages(float (&OutTotals)[FicsitPowerTotalCount], FAmountMap& OutProducers, FAmountMap& OutConsumers) const
{
	if (History.GetPowerAverages(Tier, OutTotals, OutProducers, OutConsumers))
	{
		return;
	}
	const FFicsitPowerSnapshot& Now = LatestScan.Power;
	for (int32 Index = 0; Index < FicsitPowerTotalCount; ++Index) { OutTotals[Index] = Now.Totals[Index]; }
	OutProducers.Reset();
	OutConsumers.Reset();
	for (const auto& Pair : Now.Producers) { OutProducers.Add(Pair.Key, Pair.Value.MW); }
	for (const auto& Pair : Now.Consumers) { OutConsumers.Add(Pair.Key, Pair.Value.MW); }
}

void FFicsitRangeReader::GetSeries(FItem Item, bool bProduced, TArray<float>& OutTimes, TArray<float>& OutValues) const
{
	History.GetSeries(Tier, Item, bProduced, OutTimes, OutValues);
	if (MaxSeconds > 0.f)
	{
		int32 First = 0;
		while (First < OutTimes.Num() && OutTimes[First] < -MaxSeconds) { ++First; }
		OutTimes.RemoveAt(0, First);
		OutValues.RemoveAt(0, First);
	}
	if (SmoothsForDisplay())
	{
		FicsitStatsStyle::SmoothForDisplay(OutValues);
	}
}

void FFicsitRangeReader::GetNetSeries(FItem Item, bool bSinkIsDemand, TArray<float>& OutValues) const
{
	History.GetNetSeries(Tier, Item, MaxSeconds, bSinkIsDemand, OutValues);
	if (SmoothsForDisplay())
	{
		FicsitStatsStyle::SmoothForDisplay(OutValues);
	}
}

void FFicsitRangeReader::GetPowerSeries(EFicsitPowerTotal Total, TArray<float>& OutTimes, TArray<float>& OutValues) const
{
	History.GetPowerSeries(Tier, Total, OutTimes, OutValues);
}

float FFicsitRangeReader::GetGraphSpanSeconds() const
{
	if (MaxSeconds > 0.f)
	{
		return MaxSeconds;
	}
	const float Full = History.GetTierSpanSeconds(Tier);
	if (Range != EFicsitTimeRange::All)
	{
		// Fixed ranges keep a fixed axis (like Factorio), and data fills in from the right.
		return Full;
	}
	// "All" spans exactly what has been tracked.
	return FMath::Max(60.f, FMath::Min<float>(Full, History.TrackedSeconds));
}
