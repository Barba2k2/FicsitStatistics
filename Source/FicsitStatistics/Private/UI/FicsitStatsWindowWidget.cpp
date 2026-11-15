#include "UI/FicsitStatsWindowWidget.h"

#include "AkAudioEvent.h"
#include "AkGameplayStatics.h"
#include "Blueprint/WidgetTree.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "FGHUD.h"
#include "FicsitStatistics.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "UI/FGGameUI.h"
#include "UI/SFicsitStatsWindow.h"

TWeakObjectPtr<UFicsitStatsWindowWidget> UFicsitStatsWindowWidget::OpenWindow;

namespace
{
	// The sounds the game's own machine windows make when they open and close.
	const TCHAR* OpenSoundPath = TEXT("/Game/WwiseAudio/Events/Interface/Factory_Interact/Play_UI_FactoryInteract_Open.Play_UI_FactoryInteract_Open");
	const TCHAR* CloseSoundPath = TEXT("/Game/WwiseAudio/Events/Interface/Factory_Interact/Play_UI_FactoryInteract_Close.Play_UI_FactoryInteract_Close");

	/** Plays a UI sound on the widget's player, like the game's menus do. Skipped while the world is going away. */
	void PlayWindowSound(const UUserWidget& Widget, const TCHAR* Path)
	{
		const UWorld* World = Widget.GetWorld();
		APlayerController* PlayerController = Widget.GetOwningPlayer();
		if (!World || World->bIsTearingDown || !PlayerController)
		{
			return;
		}
		AActor* Target = PlayerController->GetPawn() ? static_cast<AActor*>(PlayerController->GetPawn()) : PlayerController;

		UAkAudioEvent* Event = LoadObject<UAkAudioEvent>(nullptr, Path);
		int32 PlayingID = 0;
		if (Event)
		{
			UAkGameplayStatics::PostAkEvent(Event, Target, 0, FOnAkPostEventCallback(), {}, PlayingID);
		}
		// Once per session, so the log shows the sounds work without a line per open.
		static TSet<FString> Logged;
		if (!Logged.Contains(Path))
		{
			Logged.Add(Path);
			UE_LOG(LogFicsitStatistics, Log, TEXT("Window sound %s: %s (playing id %d)"), Path,
				!Event ? TEXT("event not found") : PlayingID != 0 ? TEXT("played") : TEXT("not played"), PlayingID);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> UFicsitStatsSlateHost::RebuildWidget()
{
	UFicsitStatsWindowWidget* OwnerWidget = Owner.Get();
	const FText KeyText = OwnerWidget ? OwnerWidget->CloseKeyText : FText::FromString(TEXT("Esc"));

	SAssignNew(Window, SFicsitStatsWindow)
		.WorldContext(OwnerWidget)
		.CloseKeyText(KeyText)
		.OnCloseRequested_Lambda([WeakOwner = Owner]()
		{
			if (UFicsitStatsWindowWidget* Widget = WeakOwner.Get())
			{
				Widget->CloseWindow();
			}
		});
	return Window.ToSharedRef();
}

void UFicsitStatsSlateHost::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Window.Reset();
}

// ---------------------------------------------------------------------------------------------------------------------

UFicsitStatsWindowWidget::UFicsitStatsWindowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Behave like the game's own menus: block movement and mouse look, show the cursor.
	mUseKeyboard = true;
	mUseMouse = true;
	mDisablePlayerActions = true;
	mDisableBuildGunActions = true;
	mDisablePlayerEquipmentManagement = true;
	SetDesiredHorizontalAlignment(HAlign_Center);
	SetDesiredVerticalAlignment(VAlign_Center);
	SetIsFocusable(true);
}

UInputAction* UFicsitStatsWindowWidget::GetToggleAction()
{
	static TWeakObjectPtr<UInputAction> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UInputAction>(nullptr, TEXT("/FicsitStatistics/Inputs/IA_FicsitStats_ToggleWindow.IA_FicsitStats_ToggleWindow"));
	}
	return Cached.Get();
}

bool UFicsitStatsWindowWidget::IsOpen()
{
	return OpenWindow.IsValid();
}

void UFicsitStatsWindowWidget::Toggle(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (UFicsitStatsWindowWidget* Existing = OpenWindow.Get())
	{
		Existing->CloseWindow();
		return;
	}

	UFicsitStatsWindowWidget* Widget = CreateWidget<UFicsitStatsWindowWidget>(PlayerController, UFicsitStatsWindowWidget::StaticClass());
	if (!Widget)
	{
		return;
	}

	// Remember what the toggle key is right now (its context is disabled while the window is open).
	FText KeyText;
	if (UInputAction* Action = GetToggleAction())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Widget->ToggleKeys = Input->QueryKeysMappedToAction(Action);
		}
	}
	if (Widget->ToggleKeys.Num() > 0)
	{
		KeyText = FText::Format(INVTEXT("{0} / Esc"), Widget->ToggleKeys[0].GetDisplayName(false));
	}
	else
	{
		KeyText = INVTEXT("Esc");
	}
	Widget->CloseKeyText = KeyText;

	AFGHUD* HUD = Cast<AFGHUD>(PlayerController->GetHUD());
	UFGGameUI* GameUI = HUD ? HUD->GetGameUI() : nullptr;
	if (GameUI)
	{
		GameUI->PushWidget(Widget);
	}
	else
	{
		UE_LOG(LogFicsitStatistics, Warning, TEXT("No game UI found; adding the statistics window directly to the viewport"));
		Widget->AddToViewport(100);
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Widget->TakeWidget());
		PlayerController->SetInputMode(Mode);
		PlayerController->SetShowMouseCursor(true);
	}
}

void UFicsitStatsWindowWidget::CloseWindow()
{
	APlayerController* PlayerController = GetOwningPlayer();
	AFGHUD* HUD = PlayerController ? Cast<AFGHUD>(PlayerController->GetHUD()) : nullptr;
	UFGGameUI* GameUI = HUD ? HUD->GetGameUI() : nullptr;
	if (GameUI)
	{
		GameUI->PopWidget(this);
	}
	else
	{
		RemoveFromParent();
		if (PlayerController)
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->SetShowMouseCursor(false);
		}
	}
}

TSharedRef<SWidget> UFicsitStatsWindowWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		Host = WidgetTree->ConstructWidget<UFicsitStatsSlateHost>(UFicsitStatsSlateHost::StaticClass());
		Host->Owner = this;
		WidgetTree->RootWidget = Host;
	}
	return Super::RebuildWidget();
}

void UFicsitStatsWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	OpenWindow = this;
	PlayWindowSound(*this, OpenSoundPath);
}

void UFicsitStatsWindowWidget::NativeDestruct()
{
	if (OpenWindow.Get() == this)
	{
		OpenWindow.Reset();
	}
	PlayWindowSound(*this, CloseSoundPath);
	Super::NativeDestruct();
}

FReply UFicsitStatsWindowWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ToggleKeys.Contains(InKeyEvent.GetKey()))
	{
		CloseWindow();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
