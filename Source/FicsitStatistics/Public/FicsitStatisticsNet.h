#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsHooks.h"
#include "FicsitStatisticsTypes.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "FicsitStatisticsNet.generated.h"

class UFGItemDescriptor;
class UFGRecipe;

/** One item of the replicated snapshot. Rates and machine counts are packed into flat arrays to keep it small. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetItem
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UFGItemDescriptor> Item;

	/** [produced: total actual, total max, per source (actual, max) ...][consumed: same layout] */
	UPROPERTY()
	TArray<float> Rates;

	/** [producer machine states ...][consumer machine states ...] */
	UPROPERTY()
	TArray<int32> Machines;
};

/** One building type's power in the replicated snapshot. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetPowerBuilding
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UFGItemDescriptor> Building;

	UPROPERTY()
	bool bProducer = false;

	UPROPERTY()
	float MW = 0.f;

	UPROPERTY()
	float MaxMW = 0.f;

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	int32 Active = 0;
};

/** One item made with one recipe in the replicated snapshot. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetRecipeOutput
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UFGItemDescriptor> Item;

	UPROPERTY()
	TSubclassOf<UFGRecipe> Recipe;

	UPROPERTY()
	float MaxRate = 0.f;
};

/** The latest snapshot in a form that replicates to clients. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	double WorldTimeSeconds = 0.0;

	UPROPERTY()
	TArray<FFicsitNetItem> Items;

	UPROPERTY()
	TArray<int32> MachinesScanned;

	/** Per EFicsitPowerTotal. */
	UPROPERTY()
	TArray<float> PowerTotals;

	UPROPERTY()
	TArray<FFicsitNetPowerBuilding> PowerBuildings;

	UPROPERTY()
	TArray<FFicsitNetRecipeOutput> RecipeOutput;

	/** Storage totals and trends: StoredItems, StoredAmounts and StoredPerMinute are parallel. */
	UPROPERTY()
	TArray<TSubclassOf<UFGItemDescriptor>> StoredItems;

	UPROPERTY()
	TArray<float> StoredAmounts;

	UPROPERTY()
	TArray<float> StoredPerMinute;

	UPROPERTY()
	float StorageTrendSeconds = 0.f;

	static FFicsitNetSnapshot FromSnapshot(const FFicsitProductionSnapshot& Snapshot);
	void ToSnapshot(FFicsitProductionSnapshot& OutSnapshot) const;
};

/** One live second in a form that replicates. Items and the three amount arrays are parallel. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetLiveSecond
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Seq = 0;

	UPROPERTY()
	float Seconds = 1.f;

	UPROPERTY()
	TArray<TSubclassOf<UFGItemDescriptor>> Items;

	UPROPERTY()
	TArray<float> Produced;

	UPROPERTY()
	TArray<float> Consumed;

	UPROPERTY()
	TArray<float> Sunk;

	UPROPERTY()
	TArray<TSubclassOf<UFGItemDescriptor>> ManualItems;

	/** EFicsitManualKind per ManualItems entry. */
	UPROPERTY()
	TArray<uint8> ManualKinds;

	UPROPERTY()
	TArray<float> ManualAmounts;

	static FFicsitNetLiveSecond FromLive(const FFicsitLiveSecond& Live);
	void ToLive(FFicsitLiveSecond& OutLive) const;
};

/** The last few live seconds, replicated about once a second. Several are sent so a client that misses an update can catch up. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitNetLive
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FFicsitNetLiveSecond> Seconds;

	/** Server: adds the newest second, keeping the last 3 (replication runs about once a second, so a client that misses an update still gets every second). */
	void Push(const FFicsitLiveSecond& Live);

	/**
	 * Client: the seconds it hasn't applied yet, oldest first, each once. InOutLastSeq is the last one it applied. When
	 * the server's sequence starts over (a new world), it follows the server rather than waiting for it to catch up.
	 */
	TArray<FFicsitLiveSecond> TakeNew(int32& InOutLastSeq) const;
};

/** A slice of one history tier sent from the server to a client. Values are flattened as [point][item]. */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitHistoryChunk
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Tier = 0;

	/** Clear the client's copy of this tier before applying (first chunk of a full send). */
	UPROPERTY()
	bool bReset = false;

	/** The server stopped at its per-request limit: the client asks again from this chunk's last point. */
	UPROPERTY()
	bool bMore = false;

	UPROPERTY()
	double TrackedSeconds = 0.0;

	UPROPERTY()
	TArray<TSubclassOf<UFGItemDescriptor>> ItemTable;

	UPROPERTY()
	TArray<double> Times;

	UPROPERTY()
	TArray<float> Produced;

	UPROPERTY()
	TArray<float> Consumed;

	UPROPERTY()
	TArray<float> Sunk;

	UPROPERTY()
	TArray<TSubclassOf<UFGItemDescriptor>> BuildingTable;

	/** [point][EFicsitPowerTotal] */
	UPROPERTY()
	TArray<float> PowerTotals;

	/** [point][building] */
	UPROPERTY()
	TArray<float> PowerProduced;

	UPROPERTY()
	TArray<float> PowerConsumed;

	/** Manual amounts, sparse (most are zero): key = (point in chunk * items + item) * kinds + kind. */
	UPROPERTY()
	TArray<int32> ManualKeys;

	UPROPERTY()
	TArray<float> ManualValues;

	/**
	 * Manual amounts of the tier's point that's still being filled, sparse: key = item * kinds + kind. The Manual tab
	 * counts that point too, and in the longest tier it covers up to two hours, so every chunk brings it along.
	 */
	UPROPERTY()
	TArray<int32> OpenManualKeys;

	UPROPERTY()
	TArray<float> OpenManualValues;

	/**
	 * Server: the points of one tier newer than SinceTime, or all of them when SinceTime < 0 (a full send, whose first
	 * chunk resets the client's copy). Each chunk holds at most about MaxValues values. At most MaxChunks are made: if
	 * points are left over, the last chunk has bMore. There is always at least one chunk, even with no new points, so
	 * the client learns the item table, the tracked time and the open point's manual amounts.
	 */
	static TArray<FFicsitHistoryChunk> Build(const struct FFicsitHistory& History, int32 Tier, double SinceTime, int32 MaxValues, int32 MaxChunks);

	/** Client: merges this chunk into its copy of the history. Points it already has are skipped. False if the tier doesn't exist. */
	bool ApplyTo(struct FFicsitHistory& History) const;
};

/**
 * How a multiplayer client keeps its copy of the saved history up to date. The server only sends history when asked,
 * a few chunks per answer. When an answer has more to send, its last chunk says so and the client asks again from there
 * right away. Otherwise the client asks again a few seconds later, for new points and the tier's open point.
 *
 * The subsystem sends the requests and passes the chunks in. Everything about when to ask and from where lives here.
 */
USTRUCT()
struct FICSITSTATISTICS_API FFicsitClientHistory
{
	GENERATED_BODY()

	/** The points received so far. */
	const FFicsitHistory& Get() const { return History; }

	/**
	 * Where to ask the server for new points of Tier from (the newest point's tracked time, or < 0 for everything), or
	 * nothing when it's too soon (every 5 s at most, per tier). A request it returns counts as sent.
	 */
	TOptional<double> NextRequest(int32 Tier, double Now);

	/**
	 * Merges a chunk from the server. When the server had more to send, returns where to ask from for the rest. Ask right
	 * away (the chunks before it have arrived by now). NextRequest holds off meanwhile, so the two don't overlap.
	 */
	TOptional<double> Apply(const FFicsitHistoryChunk& Chunk, double Now);

private:
	/** A UPROPERTY so that the GC sees the item classes in its tables. */
	UPROPERTY(Transient)
	FFicsitHistory History;

	TMap<int32, double> LastRequestTime;
};

/** The server's side: which of one player's history requests to answer. */
class FICSITSTATISTICS_API FFicsitHistoryRequests
{
public:
	/**
	 * Whether to answer a request for Tier from SinceTime at Now. Requests less than 2 s apart are ignored, except a
	 * follow-up to an answer that had more to send, which may come right away (but only once per frame). True notes it.
	 */
	bool Accept(int32 Tier, double SinceTime, double Now);

	/** How the answer to the accepted request ended: with bMore, the client's follow-up may come right away. */
	void Answered(int32 Tier, bool bMore);

private:
	struct FRequest
	{
		double Time = -1.0e9;
		bool bMore = false;
	};
	TMap<int32, FRequest> Requests;
};

/** Per-player remote call object: clients ask the server for history, the server answers in chunks. */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatsRCO : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	/**
	 * Ask for the points of a tier newer than SinceTime (in tracked seconds, where negative means everything). One answer
	 * holds a few chunks at most; if more are left, its last chunk has bMore and the client asks again from there.
	 */
	UFUNCTION(Server, Reliable)
	void Server_RequestHistory(int32 Tier, double SinceTime);

	UFUNCTION(Client, Reliable)
	void Client_ReceiveHistory(const FFicsitHistoryChunk& Chunk);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Satisfactory RCOs need at least one replicated property to be set up for networking. */
	UPROPERTY(Replicated)
	bool bDummy = true;

	/** Server: which of this player's history requests to answer. */
	FFicsitHistoryRequests HistoryRequests;
};

/*
 * The mod's two root modules. SML discovers native root modules directly (FPluginModuleLoader::FindRootModulesOfType),
 * so no Blueprint child is needed. Adding one would trigger "Multiple root modules of the same type" errors.
 */

/** Native root game instance module: registers the RCO and the mod configuration. */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatisticsGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UFicsitStatisticsGameInstanceModule();
};

/** Native root game world module: registers the mod's subsystem. */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatisticsGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UFicsitStatisticsGameWorldModule();
};
