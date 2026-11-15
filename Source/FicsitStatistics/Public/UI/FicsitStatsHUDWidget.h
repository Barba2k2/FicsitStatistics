#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FicsitStatsHUDWidget.generated.h"

/** Always-on HUD readout of pinned items' net rates (UI adaptation A5). Not interactive. */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatsHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Creates the HUD readout for a local player if it doesn't exist yet. */
	static void EnsureFor(APlayerController* PlayerController);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	/** Creates the widget and places it in the game's HUD, retrying while that HUD is still being built. */
	static void Attach(TWeakObjectPtr<APlayerController> WeakController, int32 Attempt);

	TSharedPtr<class SFicsitPinnedHUD> Panel;
	/** Placed inside the game's objective / milestone panel box rather than on its own in the viewport. */
	bool bInGameHud = false;

	/** The game's panel background, copied from its milestone panel so this one has exactly the same shade. */
	UPROPERTY()
	FSlateBrush GameBackground;
	FLinearColor GameBackgroundColor = FLinearColor::White;
	bool bHasGameBackground = false;

	static TWeakObjectPtr<UFicsitStatsHUDWidget> Instance;
};
