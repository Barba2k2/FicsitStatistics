#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "FicsitStatisticsHistory.h"
#include "FicsitStatisticsTypes.h"
#include "UI/FicsitStatsTables.h"
#include "UI/FicsitStatsWindowModel.h"
#include "Widgets/SCompoundWidget.h"

class FFicsitRangeReader;
class SVerticalBox;
class SBox;
class SFicsitGraph;
class SFicsitSupplyChainView;
class SWidgetSwitcher;
class UFGCategory;
class UFGItemDescriptor;

/**
 * The FICSIT Statistics window: native-styled frame, tabs, sidebar, Production and Balance views. What it shows is
 * decided by FFicsitStatsWindowModel. This widget turns the model into widgets and passes clicks back to it.
 */
class SFicsitStatsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitStatsWindow) {}
		/** Used to find the world / statistics subsystem. */
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		/** Text shown in the "Close" key hint, e.g. "K / Esc". */
		SLATE_ATTRIBUTE(FText, CloseKeyText)
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Pull fresh data from the subsystem and rebuild the visible lists. */
	void Refresh();

private:
	using ETab = EFicsitStatsTab;
	// Each tab's rows and sort columns (FicsitStatsTables builds them).
	using EBalanceSort = FicsitStatsTables::EBalanceSort;
	using EManualSort = FicsitStatsTables::EManualSort;
	using FItemRow = FicsitStatsTables::FRateRow;
	using FBalanceRow = FicsitStatsTables::FBalanceRow;
	using FManualRow = FicsitStatsTables::FManualRow;
	using FPowerRow = FicsitStatsTables::FPowerRow;

	// Layout builders
	TSharedRef<SWidget> BuildTitleStrip();
	TSharedRef<SWidget> BuildTab(ETab Tab, const FText& Label);
	TSharedRef<SWidget> BuildSidebar();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildTimeRangeButton(EFicsitTimeRange Range, const FText& Label);
	TSharedRef<SWidget> BuildProductionView();
	TSharedRef<SWidget> BuildRatePanel(const FText& Title, bool bProduction);
	TSharedRef<SWidget> BuildBalanceView();
	TSharedRef<SWidget> BuildBalanceList();
	TSharedRef<SWidget> BuildFooter();
	/** KeyColorAndOpacity tints just the key badge, so it can come in after its label. */
	TSharedRef<SWidget> BuildKeyHint(const TAttribute<FText>& Label, const TAttribute<FText>& Key, const TAttribute<FLinearColor>& KeyColorAndOpacity);
	TSharedRef<SWidget> BuildSortHeader(const FText& Label, float Width, bool bAlignRight, TFunction<bool()> IsActive,
		TFunction<bool()> IsAscending, TFunction<void()> OnClick);
	TSharedRef<SWidget> BuildBalanceSortHeader(EBalanceSort Column, const FText& Label, float Width, bool bAlignRight);
	TSharedRef<SWidget> BuildManualSortHeader(EManualSort Column, const FText& Label, float Width, bool bAlignRight);
	TSharedRef<SWidget> BuildManualView();
	TSharedRef<SWidget> BuildPowerView();
	TSharedRef<SWidget> BuildPowerTile(const FText& Label, int32 Index);
	TSharedRef<SWidget> BuildPowerList(const FText& Title, bool bProducers);
	TSharedRef<SWidget> BuildPowerRow(const TSharedRef<FPowerRow>& Data, bool bProducer);
	TSharedRef<SWidget> BuildManualRow(const TSharedRef<FManualRow>& Data);
	TSharedRef<SWidget> BuildSettingsButton();
	TSharedRef<SWidget> BuildSettingsPanel();

	/** Sort a list by Column (the model decides the direction), and remember it between sessions. */
	void ClickBalanceSort(EBalanceSort Column);
	void ClickManualSort(EManualSort Column);
	void LoadSorts();
	void SaveSorts() const;

	/** Rows are built once per item and then updated in place through their shared data (no per-refresh rebuild). */
	TSharedRef<SWidget> BuildItemRow(const TSharedRef<FItemRow>& Data, bool bProduction);
	TSharedRef<SWidget> BuildBalanceRow(const TSharedRef<FBalanceRow>& Data);
	TSharedRef<SWidget> BuildCategoryButton(UClass* Category, const FText& Label);
	TSharedRef<SWidget> BuildElevatorButton();
	/** Item name plus live notes: what the Space Elevator phase and the HUB milestone still need, and (bShowStorageRunout) when storage runs out soon. */
	TSharedRef<SWidget> BuildItemName(TSubclassOf<UFGItemDescriptor> Item, bool bShowStorageRunout = false);
	TSharedRef<SWidget> BuildDetailsPane();
	void RebuildDetails(const FFicsitRangeReader& Range);
	/** Just the details pane's graph (runs every frame in the 5s range). */
	void UpdateDetailGraph(const FFicsitRangeReader& Range);
	void ShowDetails(TSubclassOf<UFGItemDescriptor> Item);

	/**
	 * An item's supply chain tree, opened from its details pane (an advanced view, so it has no tab of its own). It
	 * takes the window body until Back, a tab click, or a right-click on a row (which shows that item's details).
	 */
	void OpenSupplyChain(TSubclassOf<UFGItemDescriptor> Item);
	void CloseSupplyChain();
	TSharedPtr<SFicsitSupplyChainView> SupplyChainView;

	// Data
	class AFicsitStatisticsSubsystem* GetSubsystem() const;
	/**
	 * The selected range, brought up to date. One per refresh or frame: building one brings 5s up to this frame and
	 * asks the server for new history on clients. Without a subsystem (no world yet), an empty range.
	 */
	FFicsitRangeReader MakeRange() const;
	void RebuildLists();
	void RebuildSidebar();
	void UpdateGraphs(const FFicsitRangeReader& Range);

	/** Color assigned to an item on the graphs (stable per item while the window is open). */
	FLinearColor GetSeriesColor(TSubclassOf<UFGItemDescriptor> Item) const;

	EActiveTimerReturnType OnRefreshTimer(double InCurrentTime, float InDeltaTime);
	/** Every frame: in the 5s range, redraws the graphs from the latest events (and the lists 4 times a second). */
	EActiveTimerReturnType OnRealtimeTick(double InCurrentTime, float InDeltaTime);
	double LastRealtimeListRefresh = 0.0;

	/**
	 * Opening, timed against the game's own inventory: the window slides up into place while the key hints below it
	 * come in from the left one at a time, each key a beat after its label.
	 * Starts on the first frame rather than in Construct, so a slow first build doesn't eat the animation.
	 */
	FCurveSequence OpenAnimation;
	FCurveHandle WindowCurve;
	EActiveTimerReturnType StartOpenAnimation(double InCurrentTime, float InDeltaTime);
	/** How far footer hint Index (or its key) is into its entrance, 0 to 1. Hidden hints don't take a turn. */
	float GetHintLerp(int32 Index, bool bKey) const;
	/** Whether each footer hint shows right now, in footer order. */
	TArray<TFunction<bool()>> FooterHintShown;

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnCloseRequested;
	TAttribute<FText> CloseKeyText;

	/** Tabs, filters, sorts, rows, graphs and details: everything but the widgets. Set in Construct. */
	TUniquePtr<FFicsitStatsWindowModel> Model;

	TSharedPtr<SVerticalBox> ProducedList;
	TSharedPtr<SVerticalBox> ConsumedList;
	TSharedPtr<SVerticalBox> BalanceList;
	TSharedPtr<SVerticalBox> ManualList;
	TSharedPtr<SVerticalBox> PowerProducerList;
	TSharedPtr<SVerticalBox> PowerConsumerList;
	TSharedPtr<SFicsitGraph> PowerGraph;
	TSharedPtr<SVerticalBox> SidebarList;
	TSharedPtr<SFicsitGraph> ProducedGraph;
	TSharedPtr<SFicsitGraph> ConsumedGraph;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;

	/** The in-game settings panel (gear button next to the close button). */
	bool bSettingsOpen = false;

	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<SFicsitGraph> DetailGraph;

	// Widget caches. Rebuilding widgets every refresh makes the window flash, so each list keeps one widget per item
	// (bound to shared data that's updated in place) and only re-slots them when the visible order changes.
	template <typename RowType>
	struct TRowCache
	{
		TMap<UClass*, TSharedRef<RowType>> Data;
		TMap<UClass*, TSharedRef<SWidget>> Widgets;
		TArray<UClass*> Order;
		bool bEmptyShown = false;
		bool bInitialized = false;
	};
	TRowCache<FItemRow> ProducedCache;
	TRowCache<FItemRow> ConsumedCache;
	TRowCache<FBalanceRow> BalanceCache;
	TRowCache<FManualRow> ManualCache;
	TRowCache<FPowerRow> PowerProducerCache;
	TRowCache<FPowerRow> PowerConsumerCache;

	/** Sidebar is rebuilt only when this changes (category list and whether the elevator entry is shown). */
	TArray<UClass*> SidebarSignature;
	bool bSidebarHasElevator = false;
	bool bSidebarBuilt = false;

	/**
	 * The details pane's live values, copied from the model every refresh (the pane's widgets read them through
	 * attributes). The layout is only rebuilt when DetailLayoutKey changes.
	 */
	TSharedRef<FFicsitWindowDetails> DetailState = MakeShared<FFicsitWindowDetails>();
	FString DetailLayoutKey;
};
