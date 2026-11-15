#include "FicsitStockTrend.h"

void FFicsitStockTrend::Accumulate(const FSample& Sample, double Sign)
{
	SumTime += Sign * Sample.Time;
	SumTimeSquared += Sign * Sample.Time * Sample.Time;
	for (const auto& Pair : Sample.Totals)
	{
		FItemSums& Item = Sums.FindOrAdd(Pair.Key);
		Item.Amount += Sign * Pair.Value;
		Item.TimeAmount += Sign * Sample.Time * Pair.Value;
	}
}

void FFicsitStockTrend::Add(double WorldTime, const TMap<TSubclassOf<UFGItemDescriptor>, float>& Totals)
{
	// World time starts over when a save loads, and older scans would make a nonsense slope.
	if (Samples.Num() > 0 && WorldTime < BaseTime + Samples.Last().Time)
	{
		Samples.Reset();
	}
	if (Samples.Num() == 0)
	{
		BaseTime = WorldTime;
		SumTime = 0.0;
		SumTimeSquared = 0.0;
		Sums.Reset();
	}

	FSample& Added = Samples.AddDefaulted_GetRef();
	Added.Time = WorldTime - BaseTime;
	Added.Totals = Totals;
	Accumulate(Added, 1.0);

	int32 Expired = 0;
	while (Expired < Samples.Num() - 1 && Samples[Expired].Time < Added.Time - WindowSeconds)
	{
		Accumulate(Samples[Expired], -1.0);
		++Expired;
	}
	Samples.RemoveAt(0, Expired);
}

void FFicsitStockTrend::Fill(FFicsitStockMap& OutStock) const
{
	OutStock.Reset();
	if (Samples.Num() == 0)
	{
		return;
	}
	for (const auto& Pair : Samples.Last().Totals)
	{
		if (Pair.Value > 0.f)
		{
			OutStock.Add(Pair.Key).Amount = Pair.Value;
		}
	}

	// Least-squares slope: (n * sum(t * y) - sum(t) * sum(y)) / (n * sum(t^2) - sum(t)^2). An item missing from a scan
	// had none stored then, so it adds nothing to its sums, which is the same as counting it as 0.
	const double Count = Samples.Num();
	const double Spread = Count * SumTimeSquared - SumTime * SumTime;
	if (Samples.Num() < 2 || Spread <= 0.0)
	{
		return;
	}
	for (const auto& Pair : Sums)
	{
		const float PerMinute = static_cast<float>((Count * Pair.Value.TimeAmount - SumTime * Pair.Value.Amount) / Spread * 60.0);
		if (!FMath::IsNearlyZero(PerMinute, 1e-3f))
		{
			OutStock.FindOrAdd(Pair.Key).PerMinute = PerMinute;
		}
	}
}

double FFicsitStockTrend::GetCoveredSeconds() const
{
	return Samples.Num() >= 2 ? Samples.Last().Time - Samples[0].Time : 0.0;
}
