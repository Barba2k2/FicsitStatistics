#include "FicsitStatistics.h"

#include "EnhancedInputComponent.h"
#include "FGCharacterPlayer.h"
#include "FicsitStatisticsHooks.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "UI/FicsitStatsHUDWidget.h"
#include "UI/FicsitStatsWindowWidget.h"

DEFINE_LOG_CATEGORY(LogFicsitStatistics);

namespace
{
	/** Binds the "Toggle Production Statistics" keybind every time a local player's input is (re)initialized. */
	void BindToggleAction(AFGCharacterPlayer* Character, UInputComponent* InputComponent)
	{
		if (!Character || !Character->IsLocallyControlled())
		{
			return;
		}
		UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
		UInputAction* Action = UFicsitStatsWindowWidget::GetToggleAction();
		if (!EnhancedInput || !Action)
		{
			UE_LOG(LogFicsitStatistics, Warning, TEXT("Could not bind the statistics keybind (input component %s, action %s)"),
				EnhancedInput ? TEXT("ok") : TEXT("missing"), Action ? TEXT("ok") : TEXT("missing"));
			return;
		}

		TWeakObjectPtr<AFGCharacterPlayer> WeakCharacter = Character;
		EnhancedInput->BindActionValueLambda(Action, ETriggerEvent::Triggered, [WeakCharacter](const FInputActionValue& Value)
		{
			// The action fires on both press and release, so only react to the press.
			if (!Value.Get<bool>())
			{
				return;
			}
			if (AFGCharacterPlayer* Player = WeakCharacter.Get())
			{
				UFicsitStatsWindowWidget::Toggle(Cast<APlayerController>(Player->GetController()));
			}
		});
		UE_LOG(LogFicsitStatistics, Log, TEXT("Bound statistics window keybind"));

		// The pinned-items HUD readout lives as long as the local player's HUD.
		UFicsitStatsHUDWidget::EnsureFor(Cast<APlayerController>(Character->GetController()));
	}
}

void FFicsitStatisticsModule::StartupModule()
{
	UE_LOG(LogFicsitStatistics, Log, TEXT("FICSIT Statistics module loaded"));
	FFicsitEventCounters::InstallHooks();
	InputInitializedHandle = AFGCharacterPlayer::OnPlayerInputInitialized.AddStatic(&BindToggleAction);
}

void FFicsitStatisticsModule::ShutdownModule()
{
	AFGCharacterPlayer::OnPlayerInputInitialized.Remove(InputInitializedHandle);
}

IMPLEMENT_MODULE(FFicsitStatisticsModule, FicsitStatistics)
