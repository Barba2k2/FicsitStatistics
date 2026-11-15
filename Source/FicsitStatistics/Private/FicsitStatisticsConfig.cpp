#include "FicsitStatisticsConfig.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "FicsitStatistics"

namespace
{
	namespace Options = FicsitStatsOptions;

	FConfigId MakeConfigId()
	{
		FConfigId Id;
		Id.ModReference = TEXT("FicsitStatistics");
		Id.ConfigCategory = TEXT("");
		return Id;
	}

	UConfigProperty* FindProperty(const UObject* WorldContext, const TCHAR* Key)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UConfigManager* ConfigManager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
		const UConfigPropertySection* Root = ConfigManager ? ConfigManager->GetConfigurationRootSection(MakeConfigId()) : nullptr;
		return Root ? Root->SectionProperties.FindRef(Key) : nullptr;
	}

	bool GetBool(const UObject* WorldContext, const FFicsitStatsOption& Option)
	{
		const UConfigPropertyBool* Property = Cast<UConfigPropertyBool>(FindProperty(WorldContext, Option.Key));
		return Property ? Property->Value : Option.Default != 0.f;
	}

	int32 GetInteger(const UObject* WorldContext, const FFicsitStatsOption& Option)
	{
		const UConfigPropertyInteger* Property = Cast<UConfigPropertyInteger>(FindProperty(WorldContext, Option.Key));
		const int32 Value = Property ? Property->Value : FMath::RoundToInt(Option.Default);
		return FMath::Clamp(Value, FMath::RoundToInt(Option.Min), FMath::RoundToInt(Option.Max));
	}

	float GetFloat(const UObject* WorldContext, const FFicsitStatsOption& Option)
	{
		const UConfigPropertyFloat* Property = Cast<UConfigPropertyFloat>(FindProperty(WorldContext, Option.Key));
		return FMath::Clamp(Property ? Property->Value : Option.Default, Option.Min, Option.Max);
	}

	/** Writes a value and marks the configuration dirty, which makes SML save it to disk shortly after. */
	template <typename PropertyType, typename ValueType>
	void SetValue(const UObject* WorldContext, const FFicsitStatsOption& Option, ValueType Value)
	{
		if (PropertyType* Property = Cast<PropertyType>(FindProperty(WorldContext, Option.Key)))
		{
			if (Property->Value != Value)
			{
				Property->Value = Value;
				Property->MarkDirty();
			}
		}
	}

	void SetInteger(const UObject* WorldContext, const FFicsitStatsOption& Option, int32 Value)
	{
		SetValue<UConfigPropertyInteger>(WorldContext, Option, FMath::Clamp(Value, FMath::RoundToInt(Option.Min), FMath::RoundToInt(Option.Max)));
	}

	void SetFloat(const UObject* WorldContext, const FFicsitStatsOption& Option, float Value)
	{
		SetValue<UConfigPropertyFloat>(WorldContext, Option, FMath::Clamp(Value, Option.Min, Option.Max));
	}
}

UFicsitStatsModConfiguration::UFicsitStatsModConfiguration()
{
	ConfigId = MakeConfigId();
	DisplayName = LOCTEXT("ConfigName", "FICSIT Statistics");
	Description = LOCTEXT("ConfigDescription", "Production statistics window and HUD options.");

	// The native property classes have no editor widget (SML implements the Mods-menu widgets in Blueprint subclasses).
	// Create our properties from those so the options show up in the in-game Mods menu.
	static ConstructorHelpers::FClassFinder<UConfigPropertySection> SectionClass(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection"));
	static ConstructorHelpers::FClassFinder<UConfigPropertyFloat> FloatClass(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat"));
	static ConstructorHelpers::FClassFinder<UConfigPropertyBool> BoolClass(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool"));
	static ConstructorHelpers::FClassFinder<UConfigPropertyInteger> IntegerClass(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger"));

	// Every option must be a subobject of its section (not of this configuration): SML copies the root section into
	// its live config with the root as template, which only deep-copies objects outered to it. Options outered
	// elsewhere would be shared with this default object and their changes would never reach SML's save handler.
	auto Create = [](UObject* Outer, const TCHAR* Name, UClass* NativeClass, UClass* WidgetClass) -> UObject*
	{
		return FObjectInitializer::Get().CreateDefaultSubobject(Outer, Name, NativeClass, WidgetClass ? WidgetClass : NativeClass, true, false);
	};

	RootSection = Cast<UConfigPropertySection>(Create(this, TEXT("RootSection"), UConfigPropertySection::StaticClass(), SectionClass.Class));
	// SML's section widget defaults to a horizontal list, so we ask for a vertical one that stacks the options like other mods'.
	if (UCP_Section* SectionWidget = Cast<UCP_Section>(RootSection))
	{
		SectionWidget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	}

	auto AddBool = [&](const FFicsitStatsOption& Option, const FText& Name, const FText& Tooltip)
	{
		UConfigPropertyBool* Property = Cast<UConfigPropertyBool>(Create(RootSection, Option.Key, UConfigPropertyBool::StaticClass(), BoolClass.Class));
		Property->DisplayName = Name;
		Property->Tooltip = Tooltip;
		Property->DefaultValue = Option.Default != 0.f;
		Property->Value = Property->DefaultValue;
		RootSection->SectionProperties.Add(Option.Key, Property);
	};
	auto AddInteger = [&](const FFicsitStatsOption& Option, const FText& Name, const FText& Tooltip)
	{
		UConfigPropertyInteger* Property = Cast<UConfigPropertyInteger>(Create(RootSection, Option.Key, UConfigPropertyInteger::StaticClass(), IntegerClass.Class));
		Property->DisplayName = Name;
		Property->Tooltip = Tooltip;
		Property->DefaultValue = FMath::RoundToInt(Option.Default);
		Property->Value = Property->DefaultValue;
		if (UCP_Integer* Slider = Cast<UCP_Integer>(Property))
		{
			Slider->WidgetType = ECP_IntegerWidgetType::CPI_Slider;
			Slider->MinValue = FMath::RoundToInt(Option.Min);
			Slider->MaxValue = FMath::RoundToInt(Option.Max);
		}
		RootSection->SectionProperties.Add(Option.Key, Property);
	};
	auto AddFloat = [&](const FFicsitStatsOption& Option, const FText& Name, const FText& Tooltip)
	{
		UConfigPropertyFloat* Property = Cast<UConfigPropertyFloat>(Create(RootSection, Option.Key, UConfigPropertyFloat::StaticClass(), FloatClass.Class));
		Property->DisplayName = Name;
		Property->Tooltip = Tooltip;
		Property->DefaultValue = Option.Default;
		Property->Value = Option.Default;
		if (UCP_Float* Slider = Cast<UCP_Float>(Property))
		{
			Slider->WidgetType = ECP_FloatWidgetType::CPF_Slider;
			Slider->MinValue = Option.Min;
			Slider->MaxValue = Option.Max;
		}
		RootSection->SectionProperties.Add(Option.Key, Property);
	};

	// Same order as the settings panel in the statistics window.
	AddBool(Options::ShowPinnedHud, LOCTEXT("OptShowHud", "Show pinned items on the HUD"),
		LOCTEXT("OptShowHudTip", "Your pinned items show up under the objective panel while you play."));
	AddInteger(Options::HudAverageMinutes, LOCTEXT("OptHudMinutes", "HUD average (minutes)"),
		LOCTEXT("OptHudMinutesTip", "The HUD averages each pinned item's net rate over this many minutes, so a longer time gives you a steadier number. (1 - 10)"));
	AddBool(Options::SinkCountsAsDemand, LOCTEXT("OptSinkIsDemand", "Count AWESOME Sink as consumption"),
		LOCTEXT("OptSinkIsDemandTip", "On by default, so anything going into the AWESOME Sink counts as using it up. Turn it off if the Sink only takes your overflow, so what goes into it counts as surplus instead."));
	AddInteger(Options::StorageWarningMinutes, LOCTEXT("OptStorageWarning", "Storage warning (minutes)"),
		LOCTEXT("OptStorageWarningTip", "Items only warn that their storage containers and fluid buffers are running out when they'll be empty within this many minutes at the current rate, so a slow drain on a big stockpile doesn't get flagged. (5 - 240)"));
	AddInteger(Options::DefaultGraphItems, LOCTEXT("OptGraphItems", "Items shown on graphs"),
		LOCTEXT("OptGraphItemsTip", "How many of the top items each graph shows until you click on items to pick your own. (0 - 20)"));
	AddBool(Options::GraphFluidsByDefault, LOCTEXT("OptGraphFluids", "Include fluids on graphs"),
		LOCTEXT("OptGraphFluidsTip", "On by default, so fluids (e.g. Water) can show up among the top items on the graphs. Turn it off if their big rates are squashing the other lines. You can still click on a fluid to show it."));
	AddFloat(Options::SampleIntervalSeconds, LOCTEXT("OptSampleInterval", "Scan interval (seconds)"),
		LOCTEXT("OptSampleIntervalTip", "How often we check every machine. Shorter reacts faster, but longer is easier on performance in really big factories. (1 - 60)"));
}

float FFicsitStatsSettings::GetSampleIntervalSeconds(const UObject* WorldContext) { return GetFloat(WorldContext, Options::SampleIntervalSeconds); }
bool FFicsitStatsSettings::GetShowPinnedHud(const UObject* WorldContext) { return GetBool(WorldContext, Options::ShowPinnedHud); }
int32 FFicsitStatsSettings::GetDefaultGraphItems(const UObject* WorldContext) { return GetInteger(WorldContext, Options::DefaultGraphItems); }
bool FFicsitStatsSettings::GetGraphFluidsByDefault(const UObject* WorldContext) { return GetBool(WorldContext, Options::GraphFluidsByDefault); }
bool FFicsitStatsSettings::GetSinkCountsAsDemand(const UObject* WorldContext) { return GetBool(WorldContext, Options::SinkCountsAsDemand); }
int32 FFicsitStatsSettings::GetHudAverageMinutes(const UObject* WorldContext) { return GetInteger(WorldContext, Options::HudAverageMinutes); }
int32 FFicsitStatsSettings::GetStorageWarningMinutes(const UObject* WorldContext) { return GetInteger(WorldContext, Options::StorageWarningMinutes); }

void FFicsitStatsSettings::SetSampleIntervalSeconds(const UObject* WorldContext, float Value) { SetFloat(WorldContext, Options::SampleIntervalSeconds, Value); }
void FFicsitStatsSettings::SetShowPinnedHud(const UObject* WorldContext, bool bValue) { SetValue<UConfigPropertyBool>(WorldContext, Options::ShowPinnedHud, bValue); }
void FFicsitStatsSettings::SetDefaultGraphItems(const UObject* WorldContext, int32 Value) { SetInteger(WorldContext, Options::DefaultGraphItems, Value); }
void FFicsitStatsSettings::SetGraphFluidsByDefault(const UObject* WorldContext, bool bValue) { SetValue<UConfigPropertyBool>(WorldContext, Options::GraphFluidsByDefault, bValue); }
void FFicsitStatsSettings::SetSinkCountsAsDemand(const UObject* WorldContext, bool bValue) { SetValue<UConfigPropertyBool>(WorldContext, Options::SinkCountsAsDemand, bValue); }
void FFicsitStatsSettings::SetHudAverageMinutes(const UObject* WorldContext, int32 Value) { SetInteger(WorldContext, Options::HudAverageMinutes, Value); }
void FFicsitStatsSettings::SetStorageWarningMinutes(const UObject* WorldContext, int32 Value) { SetInteger(WorldContext, Options::StorageWarningMinutes, Value); }

void FFicsitStatsSettings::ResetToDefaults(const UObject* WorldContext)
{
	SetFloat(WorldContext, Options::SampleIntervalSeconds, Options::SampleIntervalSeconds.Default);
	SetValue<UConfigPropertyBool>(WorldContext, Options::ShowPinnedHud, Options::ShowPinnedHud.Default != 0.f);
	SetInteger(WorldContext, Options::DefaultGraphItems, FMath::RoundToInt(Options::DefaultGraphItems.Default));
	SetValue<UConfigPropertyBool>(WorldContext, Options::GraphFluidsByDefault, Options::GraphFluidsByDefault.Default != 0.f);
	SetValue<UConfigPropertyBool>(WorldContext, Options::SinkCountsAsDemand, Options::SinkCountsAsDemand.Default != 0.f);
	SetInteger(WorldContext, Options::HudAverageMinutes, FMath::RoundToInt(Options::HudAverageMinutes.Default));
	SetInteger(WorldContext, Options::StorageWarningMinutes, FMath::RoundToInt(Options::StorageWarningMinutes.Default));
}

#undef LOCTEXT_NAMESPACE
