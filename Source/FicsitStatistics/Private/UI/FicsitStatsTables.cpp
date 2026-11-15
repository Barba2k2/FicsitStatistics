#include "UI/FicsitStatsTables.h"

#include "Resources/FGItemDescriptor.h"
#include "UI/FicsitStatsStyle.h"

#define LOCTEXT_NAMESPACE "FicsitStatistics"

namespace FicsitStatsTables
{
	float DisplayKey(float Value)
	{
		const float Abs = FMath::Abs(Value);
		const float Step = Abs >= 10000.f ? 100.f : Abs >= 100.f ? 1.f : Abs >= 10.f ? 0.1f : 0.01f;
		return FMath::RoundToFloat(Value / Step) * Step;
	}

	namespace
	{
		/** A to Z, or Z to A when sorting by name descending. Numeric columns always break ties A to Z. */
		bool NameOrder(const FText& A, const FText& B, bool bReverse)
		{
			const int32 ByName = A.CompareTo(B);
			return bReverse ? ByName > 0 : ByName < 0;
		}
	}

	TArray<FRateRow> BuildRateRows(const TMap<FItem, FFicsitItemRate>& Averages, const FFicsitItemFlowMap& Live, FPasses Passes, FNameOf NameOf)
	{
		TArray<FRateRow> Rows;
		for (const auto& Pair : Averages)
		{
			if (!Passes(Pair.Key))
			{
				continue;
			}
			FRateRow& Row = Rows.AddDefaulted_GetRef();
			Row.Item = Pair.Key;
			Row.Rate = Pair.Value.Actual;
			if (const FFicsitItemFlow* Flow = Live.Find(Pair.Key))
			{
				Row.CurrentActual = Flow->Total.Actual;
				Row.CurrentMax = Flow->Total.Max;
			}
		}
		Rows.Sort([&NameOf](const FRateRow& A, const FRateRow& B)
		{
			const float KA = DisplayKey(A.Rate);
			const float KB = DisplayKey(B.Rate);
			if (KA != KB)
			{
				return KA > KB;
			}
			return NameOrder(NameOf(A.Item), NameOf(B.Item), false);
		});
		const float Top = Rows.Num() > 0 ? FMath::Max(Rows[0].Rate, KINDA_SMALL_NUMBER) : 1.f;
		for (FRateRow& Row : Rows)
		{
			Row.Share = Row.Rate / Top;
			Row.RateText = FicsitStatsStyle::FormatRate(Row.Rate, Row.Item);
			// How close to max the machines run right now. Shown on the row only below 95%, where it's worth a look.
			const int32 Percent = Row.CurrentMax > 0.f ? FMath::RoundToInt(100.f * Row.CurrentActual / Row.CurrentMax) : 100;
			Row.bShowPercent = Row.CurrentMax > 0.f && Percent < 95;
			Row.PercentText = FText::Format(LOCTEXT("RowPercent", "{0}%"), FText::AsNumber(Percent));
			Row.TooltipText = Row.CurrentMax > 0.f
				? FText::Format(LOCTEXT("RowMaxTooltip", "Right now: {0} of max {1} ({2}%)\nLMB: toggle on graph. Ctrl+LMB: show only this. RMB: details."),
					FicsitStatsStyle::FormatRate(Row.CurrentActual, Row.Item), FicsitStatsStyle::FormatRate(Row.CurrentMax, Row.Item), FText::AsNumber(Percent))
				: LOCTEXT("RowTooltip", "LMB: toggle on graph. Ctrl+LMB: show only this. RMB: details.");
		}
		return Rows;
	}

	TSet<UClass*> PickDefaultGraphItems(const TArray<FRateRow>& Rows, int32 Count, bool bIncludeFluids)
	{
		TSet<UClass*> Picked;
		const bool bSkipFluids = !bIncludeFluids && Rows.ContainsByPredicate([](const FRateRow& Row) { return !FicsitUnits::IsFluid(Row.Item); });
		for (const FRateRow& Row : Rows)
		{
			if (Picked.Num() >= Count)
			{
				break;
			}
			if (!bSkipFluids || !FicsitUnits::IsFluid(Row.Item))
			{
				Picked.Add(Row.Item.Get());
			}
		}
		return Picked;
	}

	FBalanceTable BuildBalanceTable(const TMap<UClass*, FFicsitItemOutlook>& Outlooks, bool bOnlyDeficits, EBalanceSort Sort, bool bAscending,
		FPasses Passes, FNameOf NameOf)
	{
		using FicsitBalance::EState;
		FBalanceTable Table;
		for (const auto& Pair : Outlooks)
		{
			const FItem Item(Pair.Key);
			const FFicsitItemOutlook& Outlook = Pair.Value;
			if (!Passes(Item) || (bOnlyDeficits && !Outlook.ShowsAsDeficit()))
			{
				continue;
			}
			FBalanceRow& Row = Table.Rows.AddDefaulted_GetRef();
			Row.Item = Item;
			Row.Produced = Outlook.Produced;
			Row.Consumed = Outlook.Demand;
			Row.Sunk = Outlook.Sunk;
			Row.Depot = Outlook.Depot;
			Row.Stored = Outlook.Stored;
			Row.bStorageRunsOutSoon = Outlook.bStorageRunsOutSoon;
			Row.State = Outlook.State;
		}

		auto SortKey = [Sort](const FBalanceRow& Row) -> float
		{
			switch (Sort)
			{
			case EBalanceSort::Produced: return DisplayKey(Row.Produced);
			case EBalanceSort::Used:     return DisplayKey(Row.Consumed);
			case EBalanceSort::Sunk:     return DisplayKey(Row.Sunk);
			case EBalanceSort::Net:      return FicsitBalance::IsBalanced(Row.Produced, Row.Consumed) ? 0.f : DisplayKey(Row.Net());
			case EBalanceSort::Depot:    return static_cast<float>(Row.Depot);
			case EBalanceSort::Storage:  return DisplayKey(Row.Stored);
			default:                     return 0.f;
			}
		};
		Table.Rows.Sort([&](const FBalanceRow& A, const FBalanceRow& B)
		{
			const float KA = SortKey(A);
			const float KB = SortKey(B);
			if (KA != KB)
			{
				return bAscending ? KA < KB : KA > KB;
			}
			return NameOrder(NameOf(A.Item), NameOf(B.Item), Sort == EBalanceSort::Item && !bAscending);
		});

		for (FBalanceRow& Row : Table.Rows)
		{
			// The header says "per minute", so the cells carry only the number (plus "m³" for fluids).
			Row.ProducedText = FicsitStatsStyle::FormatTableRate(Row.Produced, Row.Item);
			Row.ConsumedText = FicsitStatsStyle::FormatTableRate(Row.Consumed, Row.Item);
			Row.SunkText = Row.Sunk > FicsitBalance::IdleRate ? FicsitStatsStyle::FormatTableRate(Row.Sunk, Row.Item) : INVTEXT("—");
			Row.DepotText = Row.Depot > 0 ? FicsitStatsStyle::FormatNumber(Row.Depot) : INVTEXT("—");
			Row.StoredText = Row.Stored > 0.f ? FicsitStatsStyle::FormatAmount(Row.Stored, Row.Item) : INVTEXT("—");
			// Nothing made, used or sunk: idle (a dash), not "balanced".
			Row.NetText = Row.State == EState::Idle ? INVTEXT("—") : Row.State == EState::Balanced ? INVTEXT("± 0") : FicsitStatsStyle::FormatSignedTableRate(Row.Net(), Row.Item);
			Row.NetColor = Row.State == EState::Deficit ? FicsitStatsStyle::Deficit() : Row.State == EState::Surplus ? FicsitStatsStyle::Surplus() : FicsitStatsStyle::TextSecondary();
		}

		Table.bShowSunkColumn = Sort == EBalanceSort::Sunk || Table.Rows.ContainsByPredicate([](const FBalanceRow& Row) { return Row.Sunk > FicsitBalance::IdleRate; });
		Table.bShowDepotColumn = Sort == EBalanceSort::Depot || Table.Rows.ContainsByPredicate([](const FBalanceRow& Row) { return Row.Depot > 0; });
		Table.bShowStorageColumn = Sort == EBalanceSort::Storage || Table.Rows.ContainsByPredicate([](const FBalanceRow& Row) { return Row.Stored > 0.f; });
		return Table;
	}

	FManualTable BuildManualTable(const TMap<FItem, FFicsitManualTotals>& Totals, EManualSort Sort, bool bAscending, FPasses Passes, FNameOf NameOf)
	{
		FManualTable Table;
		float Sums[FicsitManualKindCount] = {};
		for (const auto& Pair : Totals)
		{
			if (!Passes(Pair.Key))
			{
				continue;
			}
			FManualRow& Row = Table.Rows.AddDefaulted_GetRef();
			Row.Item = Pair.Key;
			for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
			{
				Row.Values[Kind] = Pair.Value.Values[Kind];
				Sums[Kind] += Row.Values[Kind];
			}
		}
		Table.Rows.Sort([&](const FManualRow& A, const FManualRow& B)
		{
			if (Sort != EManualSort::Item)
			{
				const int32 Kind = static_cast<int32>(Sort) - 1;
				const float KA = DisplayKey(A.Values[Kind]);
				const float KB = DisplayKey(B.Values[Kind]);
				if (KA != KB)
				{
					return bAscending ? KA < KB : KA > KB;
				}
			}
			return NameOrder(NameOf(A.Item), NameOf(B.Item), Sort == EManualSort::Item && !bAscending);
		});
		for (FManualRow& Row : Table.Rows)
		{
			for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
			{
				Row.Texts[Kind] = Row.Values[Kind] > 0.f ? FicsitStatsStyle::FormatNumber(Row.Values[Kind]) : INVTEXT("—");
			}
		}
		Table.Summary = Table.Rows.Num() > 0
			? FText::Format(LOCTEXT("ManualSummary", "{0} crafted · {1} spent building · {2} refunded"),
				FicsitStatsStyle::FormatNumber(Sums[static_cast<int32>(EFicsitManualKind::Crafted)]),
				FicsitStatsStyle::FormatNumber(Sums[static_cast<int32>(EFicsitManualKind::Built)]),
				FicsitStatsStyle::FormatNumber(Sums[static_cast<int32>(EFicsitManualKind::Refunded)]))
			: FText::GetEmpty();
		return Table;
	}

	namespace
	{
		TArray<FPowerRow> BuildPowerRows(const FFicsitPowerBuildingMap& NowMap, const TMap<FItem, float>& AvgMap, bool bProducer, float OtherNow, float OtherAvg,
			bool bSearching, FPasses PassesSearch, FNameOf NameOf)
		{
			TArray<FPowerRow> Rows;
			TSet<FItem> Buildings;
			for (const auto& Pair : NowMap) { Buildings.Add(Pair.Key); }
			for (const auto& Pair : AvgMap) { Buildings.Add(Pair.Key); }
			for (const FItem& Building : Buildings)
			{
				if (!Building || !PassesSearch(Building))
				{
					continue;
				}
				// Variants of one building (e.g. the HUB's built-in Biomass Burner) share a row, under the one with an icon.
				const FText Name = NameOf(Building);
				FPowerRow* Row = Rows.FindByPredicate([&Name](const FPowerRow& Existing) { return Existing.Item && Existing.Name.EqualTo(Name); });
				if (!Row)
				{
					Row = &Rows.AddDefaulted_GetRef();
					Row->Item = Building;
					Row->Name = Name;
				}
				else if (!UFGItemDescriptor::GetSmallIcon(Row->Item) && UFGItemDescriptor::GetSmallIcon(Building))
				{
					Row->Item = Building;
				}
				Row->AvgMW += AvgMap.FindRef(Building);
				if (const FFicsitPowerBuilding* Entry = NowMap.Find(Building))
				{
					Row->NowMW += Entry->MW;
					Row->MaxMW += Entry->MaxMW;
					Row->Count += Entry->Count;
					Row->Active += Entry->Active;
				}
			}
			// Power the circuits report but no scanned building accounts for.
			if ((OtherNow > 0.5f || OtherAvg > 0.5f) && !bSearching)
			{
				FPowerRow& Other = Rows.AddDefaulted_GetRef();
				Other.Name = bProducer ? LOCTEXT("PowerOtherProducers", "Other generators") : LOCTEXT("PowerOtherConsumers", "Other (trains, lights, ...)");
				Other.AvgMW = OtherAvg;
				Other.NowMW = OtherNow;
				Other.MaxMW = OtherNow;
			}
			Rows.Sort([](const FPowerRow& A, const FPowerRow& B)
			{
				const float KA = DisplayKey(A.AvgMW);
				const float KB = DisplayKey(B.AvgMW);
				if (KA != KB)
				{
					return KA > KB;
				}
				const float MA = DisplayKey(A.MaxMW);
				const float MB = DisplayKey(B.MaxMW);
				if (MA != MB)
				{
					return MA > MB;
				}
				return A.Name.CompareTo(B.Name) < 0;
			});
			const float Top = Rows.Num() > 0 ? FMath::Max(Rows[0].AvgMW, KINDA_SMALL_NUMBER) : 1.f;
			for (FPowerRow& Row : Rows)
			{
				Row.Share = Row.AvgMW / Top;
				Row.MWText = FicsitStatsStyle::FormatPower(Row.AvgMW);
				Row.CountText = !Row.Item
					? LOCTEXT("PowerOtherCount", "on your circuits, not tied to a building type")
					: bProducer
					? FText::Format(LOCTEXT("PowerCountProducer", "{0} built · {1} generating"), FText::AsNumber(Row.Count), FText::AsNumber(Row.Active))
					: FText::Format(LOCTEXT("PowerCountConsumer", "{0} built · {1} drawing power"), FText::AsNumber(Row.Count), FText::AsNumber(Row.Active));
				// The big number is the range average and the tiles show "now", so the sub-line only adds the ceiling.
				Row.SecondaryText = !Row.Item
					? FText::GetEmpty()
					: FText::Format(bProducer ? LOCTEXT("PowerRowProducer", "of {0} capacity") : LOCTEXT("PowerRowConsumer", "of {0} max"),
					FicsitStatsStyle::FormatPower(Row.MaxMW));
			}
			return Rows;
		}

		FText BatteryDuration(float Hours)
		{
			const int32 Minutes = FMath::Max(1, FMath::RoundToInt(Hours * 60.f));
			return Minutes < 60
				? FText::Format(LOCTEXT("PowerMinutes", "{0}m"), FText::AsNumber(Minutes))
				: FText::Format(LOCTEXT("PowerHours", "{0}h {1}m"), FText::AsNumber(Minutes / 60), FText::AsNumber(Minutes % 60));
		}

		int32 CountBuildings(const TArray<FPowerRow>& Rows)
		{
			int32 Count = 0;
			for (const FPowerRow& Row : Rows) { Count += Row.Count; }
			return Count;
		}
	}

	FPowerTable BuildPowerTable(const FFicsitPowerSnapshot& Now, const float (&Avg)[FicsitPowerTotalCount], const TMap<FItem, float>& AvgProducers,
		const TMap<FItem, float>& AvgConsumers, bool bSearching, FPasses PassesSearch, FNameOf NameOf)
	{
		FPowerTable Table;
		auto AvgOf = [&Avg](EFicsitPowerTotal Total) { return Avg[static_cast<int32>(Total)]; };

		// Every generator is a scanned building, so producers get no "Other" row (any difference is just timing).
		Table.Producers = BuildPowerRows(Now.Producers, AvgProducers, true, 0.f, 0.f, bSearching, PassesSearch, NameOf);
		Table.Consumers = BuildPowerRows(Now.Consumers, AvgConsumers, false, Now.Get(EFicsitPowerTotal::OtherConsumed), AvgOf(EFicsitPowerTotal::OtherConsumed),
			bSearching, PassesSearch, NameOf);

		const FLinearColor Secondary = FicsitStatsStyle::TextSecondary();

		// Production / consumption: right now, with the range average underneath.
		Table.Tiles[0] = {FicsitStatsStyle::FormatPower(Now.Get(EFicsitPowerTotal::Produced)),
			FText::Format(LOCTEXT("PowerTileAvg", "avg {0}"), FicsitStatsStyle::FormatPower(AvgOf(EFicsitPowerTotal::Produced))), Secondary};
		Table.Tiles[1] = {FicsitStatsStyle::FormatPower(Now.Get(EFicsitPowerTotal::Consumed)),
			FText::Format(LOCTEXT("PowerTileAvg", "avg {0}"), FicsitStatsStyle::FormatPower(AvgOf(EFicsitPowerTotal::Consumed))), Secondary};

		// Capacity and how much of it is in use.
		const float Capacity = Now.Get(EFicsitPowerTotal::Capacity);
		Table.Tiles[2] = {FicsitStatsStyle::FormatPower(Capacity),
			Capacity > 0.f
				? FText::Format(LOCTEXT("PowerTileUse", "{0}% in use"), FText::AsNumber(FMath::RoundToInt(100.f * Now.Get(EFicsitPowerTotal::Consumed) / Capacity)))
				: LOCTEXT("PowerTileNoGenerators", "no generators"), Secondary};

		// Max consumption against capacity: the fuse risk.
		const float Headroom = Capacity - Now.Get(EFicsitPowerTotal::MaxConsumption);
		Table.Tiles[3] = {FicsitStatsStyle::FormatPower(Now.Get(EFicsitPowerTotal::MaxConsumption)),
			Headroom >= 0.f
				? FText::Format(LOCTEXT("PowerTileHeadroom", "{0} headroom"), FicsitStatsStyle::FormatPower(Headroom))
				: FText::Format(LOCTEXT("PowerTileOver", "{0} over capacity"), FicsitStatsStyle::FormatPower(-Headroom)),
			Headroom >= 0.f ? Secondary : FicsitStatsStyle::Deficit()};

		// Batteries: charge level, and whether they're filling or draining.
		const float Stored = Now.Get(EFicsitPowerTotal::BatteryStored);
		const float StoreCapacity = Now.Get(EFicsitPowerTotal::BatteryCapacity);
		const float In = Now.Get(EFicsitPowerTotal::BatteryInput);
		const float Out = Now.Get(EFicsitPowerTotal::BatteryOutput);
		if (StoreCapacity <= 0.f)
		{
			// No Power Storage built: drop the tile and let the others use its width.
			Table.Tiles[4] = {INVTEXT("—"), FText::GetEmpty(), Secondary, true};
		}
		else
		{
			const FText Level = FText::Format(INVTEXT("{0} / {1}"), FicsitStatsStyle::FormatEnergy(Stored), FicsitStatsStyle::FormatEnergy(StoreCapacity));
			if (Out > In + 0.01f)
			{
				Table.Tiles[4] = {Level, FText::Format(LOCTEXT("PowerTileDraining", "draining {0} · empty in {1}"),
					FicsitStatsStyle::FormatPower(Out - In), BatteryDuration(Stored / FMath::Max(Out - In, 0.01f))), FicsitStatsStyle::Deficit()};
			}
			else if (In > Out + 0.01f)
			{
				Table.Tiles[4] = {Level, FText::Format(LOCTEXT("PowerTileCharging", "charging {0} · full in {1}"),
					FicsitStatsStyle::FormatPower(In - Out), BatteryDuration((StoreCapacity - Stored) / FMath::Max(In - Out, 0.01f))), FicsitStatsStyle::Surplus()};
			}
			else
			{
				Table.Tiles[4] = {Level, FText::Format(LOCTEXT("PowerTileIdle", "{0}% charged"), FText::AsNumber(FMath::RoundToInt(100.f * Stored / StoreCapacity))), Secondary};
			}
		}

		Table.ProducerSummary = FText::Format(LOCTEXT("PowerProducerSummary", "{0} generators · avg {1}"),
			FText::AsNumber(CountBuildings(Table.Producers)), FicsitStatsStyle::FormatPower(AvgOf(EFicsitPowerTotal::Produced)));
		Table.ConsumerSummary = FText::Format(LOCTEXT("PowerConsumerSummary", "{0} buildings · avg {1}"),
			FText::AsNumber(CountBuildings(Table.Consumers)), FicsitStatsStyle::FormatPower(AvgOf(EFicsitPowerTotal::Consumed)));
		return Table;
	}
}

#undef LOCTEXT_NAMESPACE
