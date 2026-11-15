#pragma once

#include "CoreMinimal.h"

class UFGItemDescriptor;

/**
 * Settings that belong to the local player rather than the world (recipe picks, sort columns, etc.). They're stored in
 * the [FicsitStatistics] section of the local GameUserSettings config, so they follow the player across saves and
 * sessions. Pins live there too, but are filed per world (see FFicsitPins). Setters write the file right away.
 */
namespace FicsitPlayerPrefs
{
	TArray<FString> GetArray(const TCHAR* Key);
	void SetArray(const TCHAR* Key, const TArray<FString>& Values);
	int32 GetInt(const TCHAR* Key, int32 Default);
	void SetInt(const TCHAR* Key, int32 Value);
	bool GetBool(const TCHAR* Key, bool Default);
	void SetBool(const TCHAR* Key, bool bValue);

	/**
	 * Keeps Classes loaded, replacing what was kept under Key before. Static lists of classes are invisible to the GC, so
	 * a class nothing else references (e.g. after loading a save) would be unloaded and leave the list dangling.
	 */
	void KeepAlive(const TCHAR* Key, TConstArrayView<UClass*> Classes);
}

/**
 * Items pinned to the HUD (UI adaptation A5), saved per player and per world. Each world's pins are filed under its
 * session name in the local config, so a new game starts with no pins and clients keep their own without any network
 * code. Until a world is set (or while a client is still waiting on the session name), there are no pins.
 */
class FFicsitPins
{
public:
	/** Switches to the pins of the world WorldContext is in, if that's a different world. Cheap to call often. */
	static void SetWorld(const UObject* WorldContext);

	static const TArray<TSubclassOf<UFGItemDescriptor>>& Get();
	static bool IsPinned(TSubclassOf<UFGItemDescriptor> Item);
	static void Toggle(TSubclassOf<UFGItemDescriptor> Item);
	/** Unpins everything in this world, including items that no longer show up in any list (so can't be unpinned there). */
	static void Clear();

	/** Bumped whenever pins change, so widgets can rebuild cheaply. */
	static int32 GetRevision();

private:
	static void Load();
	/** Saves the pins and keeps the pinned classes loaded. */
	static void Save();

	static TArray<TSubclassOf<UFGItemDescriptor>> Items;
	/** The current world's session name, or empty when there's no world yet. */
	static FString SessionName;
	static bool bLoaded;
	static int32 Revision;
};
