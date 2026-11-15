#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "FicsitRateEstimator.h"
#include "FicsitRealtimeHistory.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsHooks.h"
#include "FicsitStatisticsNet.h"
#include "FicsitStatisticsTypes.h"
#include "FicsitStockTrend.h"
#include "Subsystem/ModSubsystem.h"
#include "FicsitStatisticsSubsystem.generated.h"

class AFGBuildableFactory;

/**
 * Server-side subsystem that periodically scans every producer and consumer in the world and builds a snapshot of
 * per-item rates, broken down by source, plus machine-state counts per item. Keeps a multi-resolution history that is
 * saved with the game.
 */
UCLASS()
class FICSITSTATISTICS_API AFicsitStatisticsSubsystem : public AModSubsystem, public IFGSaveInterface
{
	GENERATED_BODY()

public:
	AFicsitStatisticsSubsystem();

	/** Finds the subsystem in a world (server, or locally where it is replicated). */
	static AFicsitStatisticsSubsystem* Get(const UObject* WorldContext);

	// Begin IFGSaveInterface
	virtual bool ShouldSave_Implementation() const override { return true; }
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	// End IFGSaveInterface

	/** On the server: the authoritative saved history. On clients: the copy received from the server so far. */
	const FFicsitHistory& GetHistory() const { return HasAuthority() ? History : ClientHistory.Get(); }

	/**
	 * The history behind a UI range, brought up to date: the realtime one for 5s (up to this frame), the one-second one
	 * for 1m, else GetHistory(). On clients, also asks the server for new points of the saved tier behind the range
	 * (throttled). The time range reader calls this, so views don't need to know which side they're on.
	 */
	const FFicsitHistory& GetFreshHistory(EFicsitTimeRange Range);

	/** Whether Range can be shown here. 5s is built from the hook events, which only the server sees. */
	bool HasRange(EFicsitTimeRange Range) const { return Range != EFicsitTimeRange::FiveSeconds || HasAuthority(); }

	/** Whether the scan runs here (the host). Its settings, like the scan interval, only mean something there. */
	bool RunsScan() const { return HasAuthority(); }

	/** Every item the saved history has recorded (on clients, as far as the copy goes). */
	const TArray<TSubclassOf<UFGItemDescriptor>>& GetRecordedItems() const { return GetHistory().ItemTable; }

	/** Clients: merge a chunk of history sent by the server. When the server had more to send, where to ask from next. */
	TOptional<double> ApplyHistoryChunk(const struct FFicsitHistoryChunk& Chunk);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const FFicsitProductionSnapshot& GetLatestSnapshot() const { return LatestSnapshot; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Seconds between scans, from the options (the scan picks up a change on its next run). */
	float SampleIntervalSeconds = 5.f;
	/** Seconds between live updates (raw hook counts for the 1m range and the row pulses). Matches the live history's points. */
	static constexpr float LiveIntervalSeconds = 1.f;
	/** How many items to print per list in the log summary. */
	static constexpr int32 LogTopN = 12;

	void TakeSample();

	/**
	 * Server: brings the 5s realtime history up to this moment from the timestamped hook events. Cheap (a frame's worth
	 * of events). Runs every frame while a view shows 5s, and once a second for a little while after.
	 */
	void UpdateRealtime();
	/** When a view last read the 5s range (FPlatformTime). Past the timeout, the 5s history stops advancing. */
	double LastRealtimeViewTime = -1.0e9;
	static constexpr double RealtimeViewTimeoutSeconds = 2.0;

	/** Clients: ask the server for new points of a saved history tier (throttled by ClientHistory). No-op on the server. */
	void RequestHistory(int32 Tier);

	/** Server: drain the live hook counts into a live second, apply it and queue it for clients. */
	void TakeLiveSample();
	/** Server and clients: add a live second to the 1m history. */
	void ApplyLiveSecond(const FFicsitLiveSecond& Live);
	/** The world time now (0 without a world), for history samples. */
	double GetWorldTime() const;
	/** The live history's per-minute rates and the smoothed rates, side by side, for the once-a-minute log. */
	void LogLive();

	/** Max rates, machine states and warm-up estimates (actual rates are replaced by real counts once available). */
	void ScanManufacturers(const TArray<class AFGBuildableManufacturer*>& Manufacturers, FFicsitProductionSnapshot& Snapshot);
	void ScanExtractors(const TArray<class AFGBuildableResourceExtractor*>& Extractors, FFicsitProductionSnapshot& Snapshot);
	void ScanFuelGenerators(const TArray<class AFGBuildableGeneratorFuel*>& Generators, FFicsitProductionSnapshot& Snapshot);
	/** Power produced / consumed per building type, plus batteries (from each building's power info). */
	void ScanPower(const TArray<class AFGBuildableFactory*>& Factories, FFicsitProductionSnapshot& Snapshot);
	/** What's in storage containers and fluid buffers, and how fast each item's total is changing. */
	void ScanStorage(FFicsitProductionSnapshot& Snapshot);
	/** The last few minutes of storage totals the trends come from. */
	FFicsitStockTrend StockTrend;
	/** The building descriptor (name and icon) for a buildable class, from the recipe it was built with. Cached. */
	TSubclassOf<UFGItemDescriptor> GetBuildingDescriptor(const class AFGBuildable* Buildable);
	/** A UPROPERTY so that the GC sees (and keeps loaded) the classes it holds. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UClass>> BuildingDescriptorCache;
	/** Last scan's building sums vs circuit sums (produced, consumed), for the once-a-minute log. */
	float LastBuildingPower[2] = {};
	float LastCircuitPower[2] = {};
	int32 LastCircuitCount = 0;

	/** Turns the hook counts into "right now" rates, replacing the scan's estimates. The scans note cycle times into it. */
	FFicsitRateEstimator RateEstimator;
	FFicsitRateEstimator::FSettings GetRateEstimatorSettings() const;

	static EFicsitMachineState GetMachineState(const AFGBuildableFactory* Factory, bool bNeedsPower);

	void LogSnapshot(const FFicsitProductionSnapshot& Snapshot) const;
	/** The supply chain tree of every part the current milestone still needs, when that set of parts changes. */
	void LogSupplyChains();
	uint32 LastLoggedMilestoneParts = 0;

	FTimerHandle SampleTimer;
	FFicsitProductionSnapshot LatestSnapshot;
	double LastSampleWorldTime = -1.0;
	int32 SamplesSinceFullLog = 0;

	UPROPERTY(SaveGame)
	FFicsitHistory History;

	/** Latest snapshot, replicated to clients (they rebuild LatestSnapshot from it). */
	UPROPERTY(ReplicatedUsing = OnRep_NetSnapshot)
	FFicsitNetSnapshot NetSnapshot;

	UFUNCTION()
	void OnRep_NetSnapshot();

	/**
	 * One-second points of raw counts for the 1m range. Not saved: it only ever covers the last minute. Transient
	 * UPROPERTY (like ClientHistory) so that the GC sees the item classes in its tables.
	 */
	UPROPERTY(Transient)
	FFicsitHistory LiveHistory;
	FTimerHandle LiveTimer;
	double LastLiveWorldTime = -1.0;
	int32 LiveSeq = 0;

	/** The 5s range's history, from timestamped hook events (server only, not saved). */
	FFicsitRealtimeHistory Realtime;
	TArray<FFicsitLiveEvent> RealtimeScratch;
	/** UpdateRealtime cost, logged and reset once a minute. */
	int32 RealtimeUpdateCount = 0;
	double RealtimeUpdateMsTotal = 0.0;
	double RealtimeUpdateMsMax = 0.0;

	/** Dev-only benchmark recording (Saved/FicsitStatsBenchmark.txt present at start): one CSV row per item per second. */
	void WriteBenchmarkSecond(const FFicsitLiveSecond& Live, const FFicsitCounts& Counts, double WorldTime);
	bool bBenchmark = false;
	FString BenchmarkPath;
	/** The produced rates the latest scan wrote to the saved history. */
	TMap<UClass*, float> BenchHistoryMade;

	/** The last few live seconds, replicated (clients apply the ones they haven't seen yet). */
	UPROPERTY(ReplicatedUsing = OnRep_NetLive)
	FFicsitNetLive NetLive;
	int32 LastAppliedLiveSeq = 0;

	UFUNCTION()
	void OnRep_NetLive();

	/** Clients: the copy of the saved history, filled on demand through UFicsitStatsRCO, and when to ask for more. */
	UPROPERTY(Transient)
	FFicsitClientHistory ClientHistory;

	// Reused between samples to avoid reallocating.
	TArray<class AFGBuildableManufacturer*> ManufacturerScratch;
	TArray<class AFGBuildableResourceExtractor*> ExtractorScratch;
	TArray<class AFGBuildableGeneratorFuel*> GeneratorScratch;
	TArray<class AFGBuildableFactory*> FactoryScratch;
	TArray<class AFGBuildableStorage*> StorageScratch;
	TArray<class AFGBuildablePipeReservoir*> ReservoirScratch;
	TMap<TSubclassOf<UFGItemDescriptor>, float> StockScratch;
	/** Ingredients the machine being scanned is short of. */
	TSet<UClass*> ShortScratch;

	// Performance counters, logged once a minute.
	double PerfScanMsTotal = 0.0;
	double PerfScanMsMax = 0.0;
	int32 PerfSamples = 0;
	int32 PerfBuildablesVisited = 0;
	int32 PerfStoragesScanned = 0;
	double PerfStorageMsTotal = 0.0;

	/** Extractor rate cross-check against the game's own GetExtractionPerMinute (logged once). */
	mutable bool bLoggedExtractorCrossCheck = false;
};
