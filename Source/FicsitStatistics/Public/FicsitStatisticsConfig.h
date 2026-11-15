#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "FicsitStatisticsConfig.generated.h"

/**
 * In-game options (Mods menu > FICSIT Statistics), defined natively so no editor asset is needed.
 * Registered through UFicsitStatisticsGameInstanceModule.
 */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatsModConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UFicsitStatsModConfiguration();
};

/** One option's config key, default and range. The Mods menu, the settings panel, the getters and Reset all read these. */
struct FFicsitStatsOption
{
	const TCHAR* Key;
	float Default;
	float Min;
	float Max;
};

namespace FicsitStatsOptions
{
	inline constexpr FFicsitStatsOption SampleIntervalSeconds{TEXT("SampleIntervalSeconds"), 5.f, 1.f, 60.f};
	inline constexpr FFicsitStatsOption ShowPinnedHud{TEXT("ShowPinnedHud"), 1.f, 0.f, 1.f};
	inline constexpr FFicsitStatsOption DefaultGraphItems{TEXT("DefaultGraphItems"), 20.f, 0.f, 20.f};
	inline constexpr FFicsitStatsOption GraphFluidsByDefault{TEXT("GraphFluidsByDefault"), 1.f, 0.f, 1.f};
	inline constexpr FFicsitStatsOption SinkCountsAsDemand{TEXT("SinkCountsAsDemand"), 1.f, 0.f, 1.f};
	inline constexpr FFicsitStatsOption HudAverageMinutes{TEXT("HudAverageMinutes"), 5.f, 1.f, 10.f};
	inline constexpr FFicsitStatsOption StorageWarningMinutes{TEXT("StorageWarningMinutes"), 30.f, 5.f, 240.f};
}

/** Typed access to the current option values (falls back to defaults if the config isn't available yet). */
struct FICSITSTATISTICS_API FFicsitStatsSettings
{
	/** Seconds between production scans (the subsystem picks up changes on its next scan). */
	static float GetSampleIntervalSeconds(const UObject* WorldContext);
	/** Whether the pinned-items panel is shown on the HUD. */
	static bool GetShowPinnedHud(const UObject* WorldContext);
	/** How many top items each graph shows by default. */
	static int32 GetDefaultGraphItems(const UObject* WorldContext);
	/** Whether fluids count among those top items (default: yes). Off leaves them out while the list also has solids. */
	static bool GetGraphFluidsByDefault(const UObject* WorldContext);
	/** Whether items going into the AWESOME Sink count as demand in balances (default: yes, so sinking an item on purpose shows as using it). */
	static bool GetSinkCountsAsDemand(const UObject* WorldContext);
	/** Minutes the HUD averages its net rate over. */
	static int32 GetHudAverageMinutes(const UObject* WorldContext);
	/** Items warn that their storage is running out only when it runs out within this many minutes. */
	static int32 GetStorageWarningMinutes(const UObject* WorldContext);

	// Setters used by the in-game settings panel. Values are clamped to the same ranges as the getters and saved to disk.
	static void SetSampleIntervalSeconds(const UObject* WorldContext, float Value);
	static void SetShowPinnedHud(const UObject* WorldContext, bool bValue);
	static void SetDefaultGraphItems(const UObject* WorldContext, int32 Value);
	static void SetGraphFluidsByDefault(const UObject* WorldContext, bool bValue);
	static void SetSinkCountsAsDemand(const UObject* WorldContext, bool bValue);
	static void SetHudAverageMinutes(const UObject* WorldContext, int32 Value);
	static void SetStorageWarningMinutes(const UObject* WorldContext, int32 Value);
	static void ResetToDefaults(const UObject* WorldContext);
};
