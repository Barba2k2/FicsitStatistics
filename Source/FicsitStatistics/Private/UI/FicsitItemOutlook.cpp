#include "UI/FicsitItemOutlook.h"

#include "FicsitStatisticsConfig.h"
#include "UI/FicsitRangeReader.h"

FFicsitOutlookContext FFicsitOutlookContext::Read(const UObject* WorldContext)
{
	FFicsitOutlookContext Context;
	Context.Needs = FFicsitMilestoneNeeds::Read(WorldContext);
	Context.Depot = FicsitDepot::ReadStock(WorldContext);
	Context.bSinkIsDemand = FFicsitStatsSettings::GetSinkCountsAsDemand(WorldContext);
	Context.StorageWarningMinutes = FFicsitStatsSettings::GetStorageWarningMinutes(WorldContext);
	return Context;
}

FFicsitItemOutlooks::FFicsitItemOutlooks(const FFicsitRangeReader& InRange, const FFicsitProductionSnapshot& InLatestScan, const FFicsitOutlookContext& InContext)
	: Range(InRange)
	, LatestScan(InLatestScan)
	, Context(InContext)
{
}

FFicsitItemOutlook FFicsitItemOutlooks::Get(TSubclassOf<UFGItemDescriptor> Item) const
{
	FFicsitItemOutlook Outlook;
	const FFicsitRangeRates Rates = Range.GetRates(Item);
	Outlook.Produced = Rates.Produced;
	Outlook.Consumed = Rates.Consumed;
	Outlook.Sunk = Rates.Sunk;
	Outlook.Demand = FicsitBalance::Demand(Rates.Consumed, Rates.Sunk, Context.bSinkIsDemand);
	Outlook.State = FicsitBalance::GetState(Outlook.Produced, Outlook.Demand, Outlook.Sunk);

	UClass* Key = Item.Get();
	Outlook.Depot = Context.Depot.FindRef(Key);

	// Storage comes from the latest scan whatever the range, since its trend is already measured over the last few
	// minutes, which is what a "runs out" warning is about. Unlike the Depot, machines can draw on it.
	Outlook.bStorageTrendReady = LatestScan.StorageTrendSeconds >= MinStorageTrendSeconds;
	if (const FFicsitStock* Stock = LatestScan.Stored.Find(Item))
	{
		Outlook.Stored = Stock->Amount;
		Outlook.StoredPerMinute = Outlook.bStorageTrendReady ? Stock->PerMinute : 0.f;
	}
	if (Outlook.Stored > 0.f && Outlook.StoredPerMinute < 0.f)
	{
		Outlook.StorageRunoutMinutes = Outlook.Stored / -Outlook.StoredPerMinute;
		Outlook.bStorageRunsOutSoon = Outlook.StorageRunoutMinutes <= Context.StorageWarningMinutes;
	}

	// Milestone ETAs come from the latest scan alone, not the range's averages. What flows into the Space Elevator right
	// now counts as supply for it, so mixing in an average over a range where that flow changed would count it as
	// another use (or leave out a use that's already gone). The HUB is paid by hand, so nothing flows into it.
	FFicsitRangeRates Now;
	float ToElevator = 0.f;
	if (const FFicsitItemFlow* Made = LatestScan.Produced.Find(Item))
	{
		Now.Produced = Made->Total.Actual;
	}
	if (const FFicsitItemFlow* Used = LatestScan.Consumed.Find(Item))
	{
		Now.Consumed = Used->Total.Actual;
		Now.Sunk = Used->BySource[static_cast<int32>(EFicsitRateSource::ResourceSink)].Actual;
		ToElevator = Used->BySource[static_cast<int32>(EFicsitRateSource::SpaceElevator)].Actual;
	}
	auto Eta = [&](int32 Remaining, float Delivered)
	{
		return FicsitBalance::MinutesToDeliver(Remaining,
			FicsitBalance::DeliverySupply(Now.Produced, Now.Consumed, Now.Sunk, Delivered, Context.bSinkIsDemand));
	};
	Outlook.ElevatorRemaining = Context.Needs.SpaceElevator.FindRef(Key);
	if (Outlook.ElevatorRemaining > 0)
	{
		Outlook.ElevatorEtaMinutes = Eta(Outlook.ElevatorRemaining, ToElevator);
	}
	Outlook.HubRemaining = Context.Needs.Hub.FindRef(Key);
	if (Outlook.HubRemaining > 0)
	{
		Outlook.HubEtaMinutes = Eta(Outlook.HubRemaining, 0.f);
	}
	return Outlook;
}
