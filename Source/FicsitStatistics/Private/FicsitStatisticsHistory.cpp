#include "FicsitStatisticsHistory.h"

#include "FicsitBalance.h"

namespace
{
	struct FTierLayout
	{
		float IntervalSeconds;
		int32 Capacity;
	};

	// 120 points per range so that every graph has the same resolution. The last tier holds 1000 hours for "All".
	constexpr FTierLayout GTierLayout[] = {
		{5.f, 120},     // 10 minutes
		{30.f, 120},    // 1 hour
		{300.f, 120},   // 10 hours
		{1500.f, 120},  // 50 hours
		{7200.f, 500},  // 1000 hours
	};

	/** The 1m range: raw counts per second, not saved. */
	constexpr FTierLayout GMinuteTierLayout[] = {
		{1.f, 60},      // 1 minute
	};

	/** The 5s range: timestamped events in 1/60 s points (about one per frame), not saved. */
	constexpr FTierLayout GRealtimeTierLayout[] = {
		{1.f / 60.f, 300}, // 5 seconds
	};

	TArrayView<const FTierLayout> LayoutsFor(FFicsitHistory::ELayout Layout)
	{
		switch (Layout)
		{
		case FFicsitHistory::ELayout::Minute:   return MakeArrayView(GMinuteTierLayout);
		case FFicsitHistory::ELayout::Realtime: return MakeArrayView(GRealtimeTierLayout);
		default:                                return MakeArrayView(GTierLayout);
		}
	}

	void AddInto(TArray<float>& Sum, const TArray<float>& Values, float Weight)
	{
		if (Sum.Num() < Values.Num())
		{
			Sum.SetNumZeroed(Values.Num());
		}
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			Sum[Index] += Values[Index] * Weight;
		}
	}

	void Scale(TArray<float>& Values, float Factor)
	{
		for (float& Value : Values)
		{
			Value *= Factor;
		}
	}

	/** A point counts as full this close to its interval (float rounding when a sample is split across points). */
	constexpr float BoundaryToleranceSeconds = 0.001f;

	/** Adds a sample's rates (not its manual totals), weighted by the seconds of it that fall into the point. */
	void AddRatesInto(FFicsitHistoryPoint& Sum, const FFicsitHistoryPoint& Sample, float Seconds)
	{
		AddInto(Sum.Produced, Sample.Produced, Seconds);
		AddInto(Sum.Consumed, Sample.Consumed, Seconds);
		AddInto(Sum.Sunk, Sample.Sunk, Seconds);
		AddInto(Sum.PowerTotals, Sample.PowerTotals, Seconds);
		AddInto(Sum.PowerProduced, Sample.PowerProduced, Seconds);
		AddInto(Sum.PowerConsumed, Sample.PowerConsumed, Seconds);
	}

	/** Turns the tier's running sum into an averaged point ending at EndTime and starts a new one. */
	void EmitPoint(FFicsitHistoryTier& Tier, double EndTime)
	{
		FFicsitHistoryPoint Point = MoveTemp(Tier.Accumulator);
		Point.Time = EndTime;
		if (Tier.AccumulatedSeconds > 0.f)
		{
			const float Inv = 1.f / Tier.AccumulatedSeconds;
			Scale(Point.Produced, Inv);
			Scale(Point.Consumed, Inv);
			Scale(Point.Sunk, Inv);
			Scale(Point.PowerTotals, Inv);
			Scale(Point.PowerProduced, Inv);
			Scale(Point.PowerConsumed, Inv);
		}
		Tier.Push(MoveTemp(Point));
		Tier.Accumulator = FFicsitHistoryPoint();
		Tier.AccumulatedSeconds = 0.f;
	}

	/**
	 * Index (oldest first) of the first point within the last Seconds of a tier, measured back from its newest point.
	 * Uses the points' times rather than a point count, so points of any width (e.g. from older saves) are handled.
	 * Returns Num() when Seconds <= 0, otherwise always includes at least the newest point.
	 */
	int32 FirstRecentIndex(const FFicsitHistoryTier& Tier, float Seconds)
	{
		const int32 Num = Tier.Num();
		if (Num == 0 || Seconds <= 0.f)
		{
			return Num;
		}
		const double Cutoff = Tier.GetOrdered(Num - 1).Time - Seconds + BoundaryToleranceSeconds;
		int32 First = Num - 1;
		while (First > 0 && Tier.GetOrdered(First - 1).Time > Cutoff)
		{
			--First;
		}
		return First;
	}
}

TArray<float>& FFicsitHistoryPoint::Manual(EFicsitManualKind Kind)
{
	switch (Kind)
	{
	case EFicsitManualKind::Crafted:   return Crafted;
	case EFicsitManualKind::CraftUsed: return CraftUsed;
	case EFicsitManualKind::Built:     return Built;
	default:                           return Refunded;
	}
}

void FFicsitHistoryTier::Push(FFicsitHistoryPoint&& Point)
{
	if (Points.Num() < Capacity)
	{
		Points.Add(MoveTemp(Point));
	}
	else
	{
		Points[Head] = MoveTemp(Point);
		Head = (Head + 1) % Capacity;
	}
}

int32 FFicsitHistory::NumTiers()
{
	return UE_ARRAY_COUNT(GTierLayout);
}

float FFicsitHistory::GetTierIntervalSeconds(int32 TierIndex)
{
	return GTierLayout[TierIndex].IntervalSeconds;
}

int32 FFicsitHistory::GetTierCapacity(int32 TierIndex)
{
	return GTierLayout[TierIndex].Capacity;
}

float FFicsitHistory::GetTierSpanSeconds(int32 TierIndex) const
{
	if (Tiers.IsValidIndex(TierIndex))
	{
		return Tiers[TierIndex].IntervalSeconds * Tiers[TierIndex].Capacity;
	}
	const TArrayView<const FTierLayout> Layouts = LayoutsFor(Layout);
	return Layouts.IsValidIndex(TierIndex) ? Layouts[TierIndex].IntervalSeconds * Layouts[TierIndex].Capacity : 600.f;
}

void FFicsitHistory::EnsureLayout()
{
	const TArrayView<const FTierLayout> Layouts = LayoutsFor(Layout);
	bool bMatches = Tiers.Num() == Layouts.Num();
	for (int32 Index = 0; bMatches && Index < Layouts.Num(); ++Index)
	{
		bMatches = FMath::IsNearlyEqual(Tiers[Index].IntervalSeconds, Layouts[Index].IntervalSeconds) && Tiers[Index].Capacity == Layouts[Index].Capacity;
	}
	if (bMatches)
	{
		return;
	}

	Tiers.Reset();
	for (const FTierLayout& Each : Layouts)
	{
		FFicsitHistoryTier& Tier = Tiers.AddDefaulted_GetRef();
		Tier.IntervalSeconds = Each.IntervalSeconds;
		Tier.Capacity = Each.Capacity;
	}
}

int32 FFicsitHistory::FindOrAddItemIndex(TSubclassOf<UFGItemDescriptor> Item)
{
	const int32 Existing = ItemTable.IndexOfByKey(Item);
	return Existing != INDEX_NONE ? Existing : ItemTable.Add(Item);
}

int32 FFicsitHistory::FindOrAddBuildingIndex(TSubclassOf<UFGItemDescriptor> Building)
{
	const int32 Existing = BuildingTable.IndexOfByKey(Building);
	return Existing != INDEX_NONE ? Existing : BuildingTable.Add(Building);
}

void FFicsitHistory::AddSample(const FFicsitProductionSnapshot& Snapshot, float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}
	EnsureLayout();
	TrackedSeconds += DeltaSeconds;

	FFicsitHistoryPoint Sample;
	Sample.Time = TrackedSeconds;
	for (const auto& Pair : Snapshot.Produced)
	{
		const int32 Index = FindOrAddItemIndex(Pair.Key);
		if (Sample.Produced.Num() <= Index) { Sample.Produced.SetNumZeroed(Index + 1); }
		Sample.Produced[Index] = Pair.Value.Total.Actual;
	}
	for (const auto& Pair : Snapshot.Consumed)
	{
		const int32 Index = FindOrAddItemIndex(Pair.Key);
		if (Sample.Consumed.Num() <= Index) { Sample.Consumed.SetNumZeroed(Index + 1); }
		Sample.Consumed[Index] = Pair.Value.Total.Actual;

		const float Sunk = Pair.Value.BySource[static_cast<int32>(EFicsitRateSource::ResourceSink)].Actual;
		if (Sunk > 0.f)
		{
			if (Sample.Sunk.Num() <= Index) { Sample.Sunk.SetNumZeroed(Index + 1); }
			Sample.Sunk[Index] = Sunk;
		}
	}

	Sample.PowerTotals.Append(Snapshot.Power.Totals, FicsitPowerTotalCount);
	for (const auto& Pair : Snapshot.Power.Producers)
	{
		const int32 Index = FindOrAddBuildingIndex(Pair.Key);
		if (Sample.PowerProduced.Num() <= Index) { Sample.PowerProduced.SetNumZeroed(Index + 1); }
		Sample.PowerProduced[Index] += Pair.Value.MW;
	}
	for (const auto& Pair : Snapshot.Power.Consumers)
	{
		const int32 Index = FindOrAddBuildingIndex(Pair.Key);
		if (Sample.PowerConsumed.Num() <= Index) { Sample.PowerConsumed.SetNumZeroed(Index + 1); }
		Sample.PowerConsumed[Index] += Pair.Value.MW;
	}

	for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
	{
		for (const auto& Pair : Snapshot.Manual[Kind])
		{
			const int32 Index = FindOrAddItemIndex(Pair.Key);
			TArray<float>& Values = Sample.Manual(static_cast<EFicsitManualKind>(Kind));
			if (Values.Num() <= Index) { Values.SetNumZeroed(Index + 1); }
			Values[Index] += Pair.Value;
		}
	}

	// Each point covers exactly its tier's interval of tracked time, whatever the scan interval is (1 to 60 s), so every
	// tier spans capacity x interval and the UI's ranges mean what they say. A sample longer than the interval fills
	// several points with the same rates; one that straddles a boundary is split, and the rest starts the next point.
	const double SampleStart = TrackedSeconds - DeltaSeconds;
	for (FFicsitHistoryTier& Tier : Tiers)
	{
		// Manual amounts are plain totals (summed, never averaged): they go into the point being filled when the sample starts.
		for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
		{
			const EFicsitManualKind ManualKind = static_cast<EFicsitManualKind>(Kind);
			AddInto(Tier.Accumulator.Manual(ManualKind), Sample.Manual(ManualKind), 1.f);
		}

		double Cursor = SampleStart;
		float Remaining = DeltaSeconds;
		while (Remaining > 0.f)
		{
			// Time-weighted running sum of the part of this sample that falls into the current point.
			const float Portion = FMath::Min(Remaining, Tier.IntervalSeconds - Tier.AccumulatedSeconds);
			if (Portion > 0.f)
			{
				AddRatesInto(Tier.Accumulator, Sample, Portion);
				Tier.AccumulatedSeconds += Portion;
				Remaining -= Portion;
				Cursor += Portion;
			}
			if (Tier.AccumulatedSeconds >= Tier.IntervalSeconds - BoundaryToleranceSeconds)
			{
				// The last point of the sample ends exactly at the tracked time (no float drift in the stored times).
				EmitPoint(Tier, Remaining > 0.f ? Cursor : TrackedSeconds);
			}
		}
	}
}

bool FFicsitHistory::GetPowerAverages(int32 TierIndex, float (&OutTotals)[FicsitPowerTotalCount], TMap<TSubclassOf<UFGItemDescriptor>, float>& OutProduced,
	TMap<TSubclassOf<UFGItemDescriptor>, float>& OutConsumed) const
{
	for (float& Value : OutTotals) { Value = 0.f; }
	OutProduced.Reset();
	OutConsumed.Reset();
	if (!Tiers.IsValidIndex(TierIndex))
	{
		return false;
	}
	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	// Only points that carry power data (older saves have none) count toward the average.
	TArray<float> SumTotals, SumProduced, SumConsumed;
	int32 Count = 0;
	for (int32 Index = 0; Index < Tier.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Tier.GetOrdered(Index);
		if (Point.PowerTotals.Num() == 0)
		{
			continue;
		}
		++Count;
		AddInto(SumTotals, Point.PowerTotals, 1.f);
		AddInto(SumProduced, Point.PowerProduced, 1.f);
		AddInto(SumConsumed, Point.PowerConsumed, 1.f);
	}
	if (Count == 0)
	{
		return false;
	}
	const float Inv = 1.f / Count;
	for (int32 Index = 0; Index < FicsitPowerTotalCount && Index < SumTotals.Num(); ++Index)
	{
		OutTotals[Index] = SumTotals[Index] * Inv;
	}
	for (int32 Index = 0; Index < SumProduced.Num(); ++Index)
	{
		if (SumProduced[Index] > 0.f && BuildingTable.IsValidIndex(Index) && BuildingTable[Index])
		{
			OutProduced.Add(BuildingTable[Index], SumProduced[Index] * Inv);
		}
	}
	for (int32 Index = 0; Index < SumConsumed.Num(); ++Index)
	{
		if (SumConsumed[Index] > 0.f && BuildingTable.IsValidIndex(Index) && BuildingTable[Index])
		{
			OutConsumed.Add(BuildingTable[Index], SumConsumed[Index] * Inv);
		}
	}
	return true;
}

void FFicsitHistory::GetPowerSeries(int32 TierIndex, EFicsitPowerTotal Total, TArray<float>& OutTimes, TArray<float>& OutValues) const
{
	OutTimes.Reset();
	OutValues.Reset();
	if (!Tiers.IsValidIndex(TierIndex))
	{
		return;
	}
	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	for (int32 Index = 0; Index < Tier.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Tier.GetOrdered(Index);
		if (Point.PowerTotals.Num() == 0)
		{
			continue;
		}
		OutTimes.Add(static_cast<float>(Point.Time - TrackedSeconds));
		OutValues.Add(Point.GetPowerTotal(Total));
	}
}

void FFicsitHistory::GetManualTotals(int32 TierIndex, TMap<TSubclassOf<UFGItemDescriptor>, FFicsitManualTotals>& OutTotals) const
{
	OutTotals.Reset();
	if (!Tiers.IsValidIndex(TierIndex))
	{
		return;
	}
	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	auto AddPoint = [this, &OutTotals](const FFicsitHistoryPoint& Point)
	{
		for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
		{
			const TArray<float>& Values = Point.Manual(static_cast<EFicsitManualKind>(Kind));
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				if (Values[Index] > 0.f && ItemTable.IsValidIndex(Index) && ItemTable[Index])
				{
					OutTotals.FindOrAdd(ItemTable[Index]).Values[Kind] += Values[Index];
				}
			}
		}
	};
	for (int32 Index = 0; Index < Tier.Num(); ++Index)
	{
		AddPoint(Tier.GetOrdered(Index));
	}
	// The point being filled right now, so a fresh craft shows up immediately even in the long ranges.
	AddPoint(Tier.Accumulator);
}

int32 FFicsitHistory::GetTierForRange(EFicsitTimeRange Range) const
{
	if (Layout != ELayout::Saved)
	{
		return 0;
	}
	switch (Range)
	{
	case EFicsitTimeRange::FiveSeconds: // shown from their own histories; the finest saved tier is the closest
	case EFicsitTimeRange::OneMinute:  return 0;
	case EFicsitTimeRange::TenMinutes: return 0;
	case EFicsitTimeRange::OneHour:    return 1;
	case EFicsitTimeRange::TenHours:   return 2;
	case EFicsitTimeRange::FiftyHours: return 3;
	default: break;
	}

	// "All": the finest tier whose full span covers everything tracked (and has at least a couple of points).
	for (int32 Index = 0; Index < NumTiers(); ++Index)
	{
		const double Span = static_cast<double>(GTierLayout[Index].IntervalSeconds) * GTierLayout[Index].Capacity;
		if (Span >= TrackedSeconds)
		{
			return Index;
		}
	}
	return NumTiers() - 1;
}

void FFicsitHistory::GetAverages(int32 TierIndex, TMap<TSubclassOf<UFGItemDescriptor>, FFicsitItemRate>& OutProduced,
	TMap<TSubclassOf<UFGItemDescriptor>, FFicsitItemRate>& OutConsumed, TMap<TSubclassOf<UFGItemDescriptor>, float>* OutSunk, float MaxSeconds) const
{
	OutProduced.Reset();
	OutConsumed.Reset();
	if (OutSunk)
	{
		OutSunk->Reset();
	}
	if (!Tiers.IsValidIndex(TierIndex) || Tiers[TierIndex].Num() == 0)
	{
		return;
	}

	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	// At least the latest point, however short MaxSeconds is.
	const int32 First = MaxSeconds > 0.f ? FMath::Min(FirstRecentIndex(Tier, MaxSeconds), Tier.Num() - 1) : 0;
	const int32 Count = Tier.Num() - First;
	TArray<float> SumProduced, SumConsumed, SumSunk;
	for (int32 Index = First; Index < Tier.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Tier.GetOrdered(Index);
		AddInto(SumProduced, Point.Produced, 1.f);
		AddInto(SumConsumed, Point.Consumed, 1.f);
		AddInto(SumSunk, Point.Sunk, 1.f);
	}
	const float InvCount = 1.f / Count;
	if (OutSunk)
	{
		for (int32 Index = 0; Index < SumSunk.Num(); ++Index)
		{
			if (SumSunk[Index] > 0.f && ItemTable.IsValidIndex(Index))
			{
				OutSunk->Add(ItemTable[Index], SumSunk[Index] * InvCount);
			}
		}
	}

	for (int32 Index = 0; Index < SumProduced.Num(); ++Index)
	{
		if (SumProduced[Index] > 0.f && ItemTable.IsValidIndex(Index))
		{
			const float Avg = SumProduced[Index] * InvCount;
			OutProduced.Add(ItemTable[Index], FFicsitItemRate{Avg, Avg});
		}
	}
	for (int32 Index = 0; Index < SumConsumed.Num(); ++Index)
	{
		if (SumConsumed[Index] > 0.f && ItemTable.IsValidIndex(Index))
		{
			const float Avg = SumConsumed[Index] * InvCount;
			OutConsumed.Add(ItemTable[Index], FFicsitItemRate{Avg, Avg});
		}
	}
}

void FFicsitHistory::GetNetSeries(int32 TierIndex, TSubclassOf<UFGItemDescriptor> Item, float MaxSeconds, bool bSinkIsDemand, TArray<float>& OutValues) const
{
	OutValues.Reset();
	const int32 ItemIndex = FindItemIndex(Item);
	if (ItemIndex == INDEX_NONE || !Tiers.IsValidIndex(TierIndex))
	{
		return;
	}
	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	const int32 First = MaxSeconds > 0.f ? FirstRecentIndex(Tier, MaxSeconds) : 0;
	OutValues.Reserve(Tier.Num() - First);
	for (int32 Index = First; Index < Tier.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Tier.GetOrdered(Index);
		const float Demand = FicsitBalance::Demand(Point.GetConsumed(ItemIndex), Point.GetSunk(ItemIndex), bSinkIsDemand);
		OutValues.Add(Point.GetProduced(ItemIndex) - Demand);
	}
}

void FFicsitHistory::GetSeries(int32 TierIndex, TSubclassOf<UFGItemDescriptor> Item, bool bProduced, TArray<float>& OutTimes, TArray<float>& OutValues) const
{
	OutTimes.Reset();
	OutValues.Reset();
	const int32 ItemIndex = FindItemIndex(Item);
	if (!Tiers.IsValidIndex(TierIndex) || ItemIndex == INDEX_NONE)
	{
		return;
	}

	const FFicsitHistoryTier& Tier = Tiers[TierIndex];
	OutTimes.Reserve(Tier.Num());
	OutValues.Reserve(Tier.Num());
	for (int32 Index = 0; Index < Tier.Num(); ++Index)
	{
		const FFicsitHistoryPoint& Point = Tier.GetOrdered(Index);
		OutTimes.Add(static_cast<float>(Point.Time - TrackedSeconds));
		OutValues.Add(bProduced ? Point.GetProduced(ItemIndex) : Point.GetConsumed(ItemIndex));
	}
}
