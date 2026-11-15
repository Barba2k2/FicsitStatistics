#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "UI/FGInteractWidget.h"
#include "FicsitStatsWindowWidget.generated.h"

class SFicsitStatsWindow;
class UInputAction;

/** Hosts the Slate window inside UMG. */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatsSlateHost : public UWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<class UFicsitStatsWindowWidget> Owner;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SFicsitStatsWindow> Window;
};

/**
 * The statistics window as a game "interact widget", so the game's own UI stack handles input mode, mouse cursor,
 * blocking movement, and Esc-to-close exactly like the base game's menus.
 */
UCLASS()
class FICSITSTATISTICS_API UFicsitStatsWindowWidget : public UFGInteractWidget
{
	GENERATED_BODY()

public:
	UFicsitStatsWindowWidget(const FObjectInitializer& ObjectInitializer);

	/** Opens the window for this player, or closes it if it's already open. */
	static void Toggle(APlayerController* PlayerController);
	static bool IsOpen();

	/** The keybind's input action asset. */
	static UInputAction* GetToggleAction();

	void CloseWindow();

	/** Keys that close the window again (the current binding of the toggle action). */
	TArray<FKey> ToggleKeys;
	FText CloseKeyText;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UFicsitStatsSlateHost> Host;

	static TWeakObjectPtr<UFicsitStatsWindowWidget> OpenWindow;
};
