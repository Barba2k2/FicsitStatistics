#include "UI/SFicsitStatsWindow.h"

#include "FGCategory.h"
#include "FicsitBalance.h"
#include "FicsitMilestoneNeeds.h"
#include "FicsitStatistics.h"
#include "FicsitStatisticsConfig.h"
#include "FicsitStatisticsSubsystem.h"
#include "Resources/FGItemDescriptor.h"
#include "Styling/StyleDefaults.h"
#include "UI/FicsitPlayerPrefs.h"
#include "UI/FicsitRangeReader.h"
#include "UI/FicsitStatsStyle.h"
#include "UI/SFicsitWidgets.h"
#include "UI/SFicsitGraph.h"
#include "UI/SFicsitSupplyChainView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FicsitStatistics"

namespace FontSize = FicsitStatsStyle::FontSize;
using FicsitWidgets::MakeText;

namespace
{
	/** The window's close "X", drawn as two lines like the game's close glyph. */
	class SFicsitCloseGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFicsitCloseGlyph) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16.f, 16.f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& Style, bool) const override
		{
			const FVector2D S = Geometry.GetLocalSize();
			const TArray<FVector2D> A = {FVector2D(1.f, 1.f), FVector2D(S.X - 1.f, S.Y - 1.f)};
			const TArray<FVector2D> B = {FVector2D(S.X - 1.f, 1.f), FVector2D(1.f, S.Y - 1.f)};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), A, ESlateDrawEffect::None, Style.GetColorAndOpacityTint(), true, 2.5f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), B, ESlateDrawEffect::None, Style.GetColorAndOpacityTint(), true, 2.5f);
			return LayerId;
		}
	};

	/** Thin share bar under a row: a filled fraction in the item's color over a dark track. Values are live attributes. */
	class SFicsitShareBar : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFicsitShareBar) : _Fraction(0.f), _Color(FLinearColor::White) {}
			SLATE_ATTRIBUTE(float, Fraction)
			SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Fraction = InArgs._Fraction;
			Color = InArgs._Color;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(10.f, 4.f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& Style, bool) const override
		{
			const FVector2D Size = Geometry.GetLocalSize();
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), FicsitStatsStyle::White(), ESlateDrawEffect::None,
				FLinearColor(0.f, 0.f, 0.f, 0.25f) * Style.GetColorAndOpacityTint());
			const float Filled = Size.X * FMath::Clamp(Fraction.Get(), 0.f, 1.f);
			if (Filled > 0.5f)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
					Geometry.ToPaintGeometry(FVector2D(Filled, Size.Y), FSlateLayoutTransform()), FicsitStatsStyle::White(), ESlateDrawEffect::None, Color.Get() * Style.GetColorAndOpacityTint());
			}
			return LayerId + 1;
		}

	private:
		TAttribute<float> Fraction;
		TAttribute<FLinearColor> Color;
	};

	/** The game's checkbox: a white square with a check mark when on. */
	TSharedRef<SWidget> MakeCheckSquare(const TAttribute<bool>& bChecked)
	{
		return SNew(SBox)
			.WidthOverride(18.f)
			.HeightOverride(18.f)
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FLinearColor::White)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.f)
				[
					// The game's own check mark texture, tinted dark.
					SNew(SImage)
					.Image(FicsitStatsStyle::TextureBrush(TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/ficsit_checkmark_256.ficsit_checkmark_256")))
					.ColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.08f))
					.Visibility_Lambda([bChecked]() { return bChecked.Get() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
				]
			];
	}

	/**
	 * Pushpin toggle for the HUD readout (UI adaptation A5). Consumes its own clicks so the row underneath doesn't react.
	 * With a RowHover source, it only shows while that row is hovered or the item is pinned, so a list isn't a column of pins.
	 */
	TSharedRef<SWidget> MakePinButton(TSubclassOf<UFGItemDescriptor> Item, TSharedPtr<TWeakPtr<SWidget>> RowHover = nullptr)
	{
		return SNew(SBox)
			.WidthOverride(26.f)
			.HeightOverride(26.f)
			.ToolTipText(LOCTEXT("PinTooltip", "Pin to HUD"))
			// Hidden, not collapsed, so the numbers next to it stay put.
			.Visibility_Lambda([Item, RowHover]()
			{
				if (!RowHover.IsValid() || FFicsitPins::IsPinned(Item))
				{
					return EVisibility::Visible;
				}
				const TSharedPtr<SWidget> Row = RowHover->Pin();
				return Row.IsValid() && Row->IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
			})
			[
				SNew(SFicsitClickBox)
				.HoverColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
				.OnClicked_Lambda([Item]() { FFicsitPins::Toggle(Item); })
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SFicsitPinGlyph)
						.Pinned_Lambda([Item]() { return FFicsitPins::IsPinned(Item); })
					]
				]
			];
	}

	/** Balance table column widths, shared by the header and the rows. The cells hold bare numbers ("per minute" is in the header). */
	constexpr float BalanceRateColumn = 100.f;
	constexpr float BalanceSunkColumn = 85.f;
	constexpr float BalanceNetColumn = 100.f;
	constexpr float BalanceDepotColumn = 75.f;
	constexpr float BalanceStorageColumn = 85.f;

	using FicsitBalance::IsBalanced;

	/** Opening timings (seconds) and distances (slate units), measured frame by frame from a recording of the game's inventory. */
	constexpr float OpenSlideTime = 0.2f;
	constexpr float OpenSlideDistance = 60.f;
	constexpr float HintStagger = 0.07f;
	constexpr float HintKeyDelay = 0.08f;
	constexpr float HintEntranceTime = 0.1f;
	constexpr float HintSlideDistance = 16.f;
	constexpr int32 MaxFooterHints = 4;
}

void SFicsitStatsWindow::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	// The pin buttons read and save this world's pins, even if the HUD panel hasn't been built yet.
	FFicsitPins::SetWorld(WorldContext.Get());
	OnCloseRequested = InArgs._OnCloseRequested;
	CloseKeyText = InArgs._CloseKeyText;
	Model = MakeUnique<FFicsitStatsWindowModel>(MakeShared<FFicsitGameItemInfo>(WorldContext));
	LoadSorts();

	// The game's window doesn't fade or scale, it shows at full opacity and slides up into place.
	WindowCurve = OpenAnimation.AddCurve(0.f, OpenSlideTime, ECurveEaseFunction::QuadOut);
	// The hints read the sequence time directly (GetHintLerp), so this just keeps the sequence running until the last one is in.
	OpenAnimation.AddCurve(0.f, (MaxFooterHints - 1) * HintStagger + HintKeyDelay + HintEntranceTime);
	OpenAnimation.JumpToStart();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.WidthOverride(1440.f)
			.HeightOverride(840.f)
			.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
			{
				return FSlateRenderTransform(FVector2D(0.f, OpenSlideDistance * (1.f - WindowCurve.GetLerp())));
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildTitleStrip()
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor(FicsitStatsStyle::WindowBody())
						.Padding(0.f)
						[
							// An item's supply chain (opened from its details) takes the whole body until Back or a tab is clicked.
							SNew(SWidgetSwitcher)
							.WidgetIndex_Lambda([this]() { return Model->IsSupplyChainOpen() ? 1 : 0; })
							+ SWidgetSwitcher::Slot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SBox)
									.WidthOverride(220.f)
									.Visibility_Lambda([this]() { return Model->GetTab() == ETab::Power ? EVisibility::Collapsed : EVisibility::Visible; })
									[
										BuildSidebar()
									]
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.Padding(12.f, 10.f, 12.f, 12.f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(0.f, 0.f, 0.f, 10.f)
									[
										BuildToolbar()
									]
									+ SVerticalBox::Slot()
									.FillHeight(1.f)
									[
										SAssignNew(TabSwitcher, SWidgetSwitcher)
										.WidgetIndex_Lambda([this]() { return static_cast<int32>(Model->GetTab()); })
										+ SWidgetSwitcher::Slot()
										[
											BuildProductionView()
										]
										+ SWidgetSwitcher::Slot()
										[
											BuildBalanceView()
										]
										+ SWidgetSwitcher::Slot()
										[
											BuildManualView()
										]
										+ SWidgetSwitcher::Slot()
										[
											BuildPowerView()
										]
									]
								]
							]
							+ SWidgetSwitcher::Slot()
							.Padding(12.f, 10.f, 12.f, 12.f)
							[
								SAssignNew(SupplyChainView, SFicsitSupplyChainView)
								.WorldContext(WorldContext)
								.OnShowDetails_Lambda([this](TSubclassOf<UFGItemDescriptor> Item) { ShowDetails(Item); })
								.OnBack_Lambda([this]() { CloseSupplyChain(); })
							]
						]
					]
					// Settings panel (gear button) drops down over the content.
					+ SOverlay::Slot()
					[
						BuildSettingsPanel()
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.f, 14.f, 0.f, 0.f)
		[
			BuildFooter()
		]
	];

	Refresh();
	RegisterActiveTimer(1.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFicsitStatsWindow::OnRefreshTimer));
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFicsitStatsWindow::OnRealtimeTick));
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFicsitStatsWindow::StartOpenAnimation));
}

EActiveTimerReturnType SFicsitStatsWindow::StartOpenAnimation(double InCurrentTime, float InDeltaTime)
{
	OpenAnimation.Play(AsShared());
	return EActiveTimerReturnType::Stop;
}

float SFicsitStatsWindow::GetHintLerp(int32 Index, bool bKey) const
{
	// Hints take turns in the order they show, so a tab that hides the first few doesn't start with a gap.
	int32 Turn = 0;
	for (int32 Before = 0; Before < Index; ++Before)
	{
		Turn += FooterHintShown[Before]() ? 1 : 0;
	}
	const float Start = Turn * HintStagger + (bKey ? HintKeyDelay : 0.f);
	// Before Play the sequence time is at or below zero, and after it ends it keeps counting up, so the clamp covers both.
	const float Progress = FMath::Clamp((OpenAnimation.GetSequenceTime() - Start) / HintEntranceTime, 0.f, 1.f);
	return FCurveHandle::ApplyEasing(Progress, ECurveEaseFunction::QuadOut);
}

EActiveTimerReturnType SFicsitStatsWindow::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	// The 5s range refreshes itself more often (OnRealtimeTick).
	if (!FFicsitRangeReader::UpdatesEveryFrame(Model->GetTimeRange()))
	{
		Refresh();
	}
	return EActiveTimerReturnType::Continue;
}

EActiveTimerReturnType SFicsitStatsWindow::OnRealtimeTick(double InCurrentTime, float InDeltaTime)
{
	if (!FFicsitRangeReader::UpdatesEveryFrame(Model->GetTimeRange()))
	{
		return EActiveTimerReturnType::Continue;
	}
	// Graphs every frame, so each finished cycle appears the frame it happens. The lists and texts (sorting, filtering,
	// the Depot and milestones) only 4 times a second: re-sorting every frame would make rows jump around.
	if (InCurrentTime - LastRealtimeListRefresh >= 0.25)
	{
		LastRealtimeListRefresh = InCurrentTime;
		Refresh();
		return EActiveTimerReturnType::Continue;
	}
	// One range for every graph: building it brings 5s up to this frame.
	const FFicsitRangeReader Range = MakeRange();
	UpdateGraphs(Range);
	UpdateDetailGraph(Range);
	return EActiveTimerReturnType::Continue;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

TSharedRef<SWidget> SFicsitStatsWindow::BuildTitleStrip()
{
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::TitleStrip())
		.Padding(0.f)
		[
			SNew(SBox)
			.HeightOverride(46.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					BuildTab(ETab::Production, LOCTEXT("TabProduction", "Production"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f, 0.f, 0.f)
				[
					BuildTab(ETab::Balance, LOCTEXT("TabBalance", "Balance"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f, 0.f, 0.f)
				[
					BuildTab(ETab::Manual, LOCTEXT("TabManual", "Manual"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f, 0.f, 0.f)
				[
					BuildTab(ETab::Power, LOCTEXT("TabPower", "Power"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 2.f, 0.f)
				[
					BuildSettingsButton()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(64.f)
					[
						SNew(SFicsitClickBox)
						.Color(FicsitStatsStyle::CloseBlock())
						.HoverColor(FicsitStatsStyle::Orange())
						.OnClicked_Lambda([this]() { OnCloseRequested.ExecuteIfBound(); })
						[
							SNew(SBox)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(SFicsitCloseGlyph)
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildTab(ETab Tab, const FText& Label)
{
	return SNew(SBox)
		.WidthOverride(280.f)
		[
			SNew(SFicsitClickBox)
			.Color_Lambda([this, Tab]() { return Model->GetTab() == Tab ? FicsitStatsStyle::TabActive() : FicsitStatsStyle::TabInactive(); })
			.HoverColor_Lambda([this, Tab]() { return Model->GetTab() == Tab ? FicsitStatsStyle::TabActive() : FicsitStatsStyle::RowTileHover(); })
			.OnClicked_Lambda([this, Tab]()
			{
				Model->SelectTab(Tab);
				Refresh();
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				.Padding(18.f, 0.f)
				[
					MakeText(Label, FontSize::Body)
				]
				// Orange bar under the active tab. The two tab grays alone were too close to tell apart in game.
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.HeightOverride(3.f)
					.Visibility_Lambda([this, Tab]() { return Model->GetTab() == Tab ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
					[
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor(FicsitStatsStyle::Orange())
					]
				]
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildSidebar()
{
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::Sidebar())
		.Padding(0.f, 10.f, 0.f, 10.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(SidebarList, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildCategoryButton(UClass* Category, const FText& Label)
{
	auto IsSelected = [this, Category]() { return !Model->IsMilestoneFilterOn() && Model->GetSelectedCategory() == Category; };
	return SNew(SFicsitClickBox)
		.Color_Lambda([IsSelected]() { return IsSelected() ? FicsitStatsStyle::Orange() : FLinearColor::Transparent; })
		.HoverColor_Lambda([IsSelected]() { return IsSelected() ? FicsitStatsStyle::Orange() : FLinearColor(1.f, 1.f, 1.f, 0.06f); })
		.Padding(FMargin(14.f, 8.f))
		.OnClicked_Lambda([this, Category]()
		{
			Model->SelectCategory(Category);
			Refresh();
		})
		[
			MakeText(Label, FontSize::Body)
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildElevatorButton()
{
	return SNew(SFicsitClickBox)
		.Color_Lambda([this]() { return Model->IsMilestoneFilterOn() ? FicsitStatsStyle::Orange() : FLinearColor::Transparent; })
		.HoverColor_Lambda([this]() { return Model->IsMilestoneFilterOn() ? FicsitStatsStyle::Orange() : FLinearColor(1.f, 1.f, 1.f, 0.06f); })
		.Padding(FMargin(14.f, 8.f))
		.OnClicked_Lambda([this]()
		{
			Model->SelectMilestoneFilter();
			Refresh();
		})
		[
			MakeText(LOCTEXT("CategoryElevator", "Current milestone"), FontSize::Body)
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildItemName(TSubclassOf<UFGItemDescriptor> Item, bool bShowStorageRunout)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(UFGItemDescriptor::GetItemName(Item))
			.Font(FicsitStatsStyle::Font(FontSize::Body))
			.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		];

	// Items the Space Elevator or the HUB milestone still needs get a small orange note under the name (UI adaptation A3).
	// Rows are long-lived, so the notes follow the live milestone data instead of being decided once.
	UClass* Key = Item.Get();
	auto AddMilestoneNote = [this, &Box, Key](int32 FFicsitItemOutlook::* Remaining, float FFicsitItemOutlook::* EtaMinutes, FText Source)
	{
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FicsitStatsStyle::Font(FontSize::Caption))
				.ColorAndOpacity(FicsitStatsStyle::Orange())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Visibility_Lambda([this, Remaining, Key]() { return Model->GetOutlook(Key).*Remaining > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.Text_Lambda([this, Remaining, EtaMinutes, Key, Source]()
				{
					const FFicsitItemOutlook& Outlook = Model->GetOutlook(Key);
					const int32 Left = Outlook.*Remaining;
					if (Left <= 0)
					{
						return FText::GetEmpty();
					}
					const float Minutes = Outlook.*EtaMinutes;
					return Minutes > 0.f
						? FText::Format(LOCTEXT("MilestoneNeedsEta", "{0} · {1} left · ~{2}"), Source, FicsitStatsStyle::FormatNumber(Left), FicsitStatsStyle::FormatDuration(Minutes))
						: FText::Format(LOCTEXT("MilestoneNeedsNotMade", "{0} · {1} left · not made"), Source, FicsitStatsStyle::FormatNumber(Left));
				})
			];
	};
	AddMilestoneNote(&FFicsitItemOutlook::ElevatorRemaining, &FFicsitItemOutlook::ElevatorEtaMinutes, LOCTEXT("NoteElevator", "Elevator"));
	AddMilestoneNote(&FFicsitItemOutlook::HubRemaining, &FFicsitItemOutlook::HubEtaMinutes, LOCTEXT("NoteHub", "HUB"));

	// The amount stored has its own column. Under the name, only the warning: storage that runs out within the
	// warning time.
	if (bShowStorageRunout)
	{
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FicsitStatsStyle::Font(FontSize::Caption))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FicsitStatsStyle::Deficit())
				.Visibility_Lambda([this, Key]() { return Model->GetOutlook(Key).bStorageRunsOutSoon ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.Text_Lambda([this, Key]()
				{
					const FFicsitItemOutlook& Outlook = Model->GetOutlook(Key);
					return Outlook.bStorageRunsOutSoon
						? FText::Format(LOCTEXT("StorageRunsOut", "Storage empty in {0}"), FicsitStatsStyle::FormatDuration(Outlook.StorageRunoutMinutes))
						: FText::GetEmpty();
				})
			];
	}
	return Box;
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildToolbar()
{
	TSharedRef<SHorizontalBox> Ranges = SNew(SHorizontalBox);
	const TPair<EFicsitTimeRange, FText> RangeLabels[] = {
		{EFicsitTimeRange::FiveSeconds, LOCTEXT("Range5s", "5s")},
		{EFicsitTimeRange::OneMinute, LOCTEXT("Range1m", "1m")},
		{EFicsitTimeRange::TenMinutes, LOCTEXT("Range10m", "10m")},
		{EFicsitTimeRange::OneHour, LOCTEXT("Range1h", "1h")},
		{EFicsitTimeRange::TenHours, LOCTEXT("Range10h", "10h")},
		{EFicsitTimeRange::FiftyHours, LOCTEXT("Range50h", "50h")},
		{EFicsitTimeRange::All, LOCTEXT("RangeAll", "All")},
	};
	for (const auto& Pair : RangeLabels)
	{
		Ranges->AddSlot()
			.AutoWidth()
			.Padding(2.f, 0.f, 0.f, 0.f)
			[
				BuildTimeRangeButton(Pair.Key, Pair.Value)
			];
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SearchField())
			.Padding(FMargin(10.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableText)
					.HintText(LOCTEXT("SearchHint", "Search..."))
					.Font(FicsitStatsStyle::Font(FontSize::Body))
					.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
					.OnTextChanged_Lambda([this](const FText& NewText)
					{
						Model->SetSearch(NewText.ToString());
						Refresh();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(22.f)
					.HeightOverride(22.f)
					[
						SNew(SImage)
						.Image(FicsitStatsStyle::TextureBrush(TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/SearchIcon.SearchIcon")))
					]
				]
			]
		]
		// "Only deficits" filters the Balance list, so it sits with the other filters instead of in the column headers.
		// It's the game's checkbox: a white square with a check mark when on.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(12.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Model->GetTab() == ETab::Balance ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SFicsitClickBox)
				.HoverColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
				.Padding(FMargin(6.f, 4.f))
				.OnClicked_Lambda([this]()
				{
					Model->ToggleOnlyDeficits();
					Refresh();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						MakeCheckSquare(TAttribute<bool>::CreateLambda([this]() { return Model->IsOnlyDeficits(); }))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.f, 0.f, 0.f, 0.f)
					[
						MakeText(LOCTEXT("OnlyDeficits", "Only deficits"), FontSize::Body)
					]
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(12.f, 0.f, 0.f, 0.f)
		[
			Ranges
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildTimeRangeButton(EFicsitTimeRange Range, const FText& Label)
{
	return SNew(SBox)
		.WidthOverride(64.f)
		// 5s is built from the hook events, which only the server sees: multiplayer clients don't get it.
		.Visibility_Lambda([this, Range]()
		{
			return FFicsitRangeReader::IsAvailable(GetSubsystem(), Range) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SFicsitClickBox)
			.Color_Lambda([this, Range]() { return Model->GetTimeRange() == Range ? FicsitStatsStyle::Orange() : FicsitStatsStyle::TabActive(); })
			.HoverColor_Lambda([this, Range]() { return Model->GetTimeRange() == Range ? FicsitStatsStyle::Orange() : FicsitStatsStyle::TabInactive(); })
			.OnClicked_Lambda([this, Range]()
			{
				Model->SetTimeRange(Range);
				Refresh();
			})
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(0.f, 5.f)
				[
					MakeText(Label, FontSize::Body)
				]
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildProductionView()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			BuildRatePanel(LOCTEXT("Production", "Production"), true)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(6.f, 0.f, 0.f, 0.f)
		[
			BuildRatePanel(LOCTEXT("Consumption", "Consumption"), false)
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildRatePanel(const FText& Title, bool bProduction)
{
	TSharedPtr<SFicsitGraph>& Graph = bProduction ? ProducedGraph : ConsumedGraph;
	TSharedPtr<SVerticalBox>& List = bProduction ? ProducedList : ConsumedList;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
			.Padding(FMargin(12.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					MakeText(Title, FontSize::Body)
				]
				// Reset icon, only once the graph differs from the default top rows, so the untouched header stays just a title.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SFicsitClickBox)
					.Visibility_Lambda([this, bProduction]() { return Model->IsGraphCustomized(bProduction) ? EVisibility::Visible : EVisibility::Collapsed; })
					.HoverColor(FLinearColor(1.f, 1.f, 1.f, 0.12f))
					.Padding(FMargin(4.f, 0.f))
					.ToolTipText(LOCTEXT("GraphReset", "Reset graph"))
					.OnClicked_Lambda([this, bProduction]()
					{
						Model->ResetGraph(bProduction);
						UE_LOG(LogFicsitStatistics, Log, TEXT("Graph: %s reset to defaults"), bProduction ? TEXT("Production") : TEXT("Consumption"));
						Refresh();
					})
					[
						SNew(SFicsitResetGlyph)
						.Size(18.f)
						.Color(FicsitStatsStyle::TextPrimary())
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(230.f)
			[
				SAssignNew(Graph, SFicsitGraph)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(List, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildBalanceView()
{
	// Codex pattern: list on the left, details of the selected item on the right.
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			BuildBalanceList()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(12.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(350.f)
			[
				BuildDetailsPane()
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildBalanceList()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
			.Padding(FMargin(12.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildBalanceSortHeader(EBalanceSort::Item, LOCTEXT("BalanceItem", "Item"), 0.f, false)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.f, 0.f, 0.f, 0.f)
					[
						MakeText(LOCTEXT("BalancePerMinute", "rates per minute"), FontSize::Small, FicsitStatsStyle::TextSecondary())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildBalanceSortHeader(EBalanceSort::Produced, LOCTEXT("BalanceProduced", "Produced"), BalanceRateColumn, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildBalanceSortHeader(EBalanceSort::Used, LOCTEXT("BalanceConsumed", "Used"), BalanceRateColumn, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return Model->GetBalance().bShowSunkColumn ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BuildBalanceSortHeader(EBalanceSort::Sunk, LOCTEXT("BalanceSunk", "Sunk"), BalanceSunkColumn, true)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildBalanceSortHeader(EBalanceSort::Net, LOCTEXT("BalanceNet", "Net"), BalanceNetColumn, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return Model->GetBalance().bShowDepotColumn ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BuildBalanceSortHeader(EBalanceSort::Depot, LOCTEXT("BalanceDepot", "Depot"), BalanceDepotColumn, true)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return Model->GetBalance().bShowStorageColumn ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BuildBalanceSortHeader(EBalanceSort::Storage, LOCTEXT("BalanceStorage", "Storage"), BalanceStorageColumn, true)
					]
				]
				// Space for the pin column in the rows below.
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(36.f)]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(BalanceList, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildKeyHint(const TAttribute<FText>& Label, const TAttribute<FText>& Key, const TAttribute<FLinearColor>& KeyColorAndOpacity)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FicsitStatsStyle::Font(FontSize::Body))
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			// A brushless border so the tint reaches the badge's own background, not just its text.
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(0.f)
			.ColorAndOpacity(KeyColorAndOpacity)
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FicsitStatsStyle::Orange())
				.Padding(FMargin(6.f, 1.f))
				[
					SNew(STextBlock)
					.Text(Key)
					.Font(FicsitStatsStyle::Font(FontSize::Caption, true))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildFooter()
{
	// Only the clicks the current tab answers to. Power rows aren't clickable, so that tab just shows Close.
	FooterHintShown = {
		[this]() { return Model->GetTab() != ETab::Power || Model->IsSupplyChainOpen(); },
		[this]() { return Model->GetTab() == ETab::Production || Model->IsSupplyChainOpen(); },
		[this]() { return Model->GetTab() == ETab::Production && !Model->IsSupplyChainOpen(); },
		[]() { return true; },
	};
	const TAttribute<FText> Labels[] = {
		TAttribute<FText>::CreateLambda([this]()
		{
			return Model->IsSupplyChainOpen() ? LOCTEXT("HintOpenClose", "Open / Close")
				: Model->GetTab() == ETab::Production ? LOCTEXT("HintToggleGraph", "Toggle in Graph")
				: LOCTEXT("HintSelect", "Select");
		}),
		LOCTEXT("HintDetails", "Details"),
		LOCTEXT("HintShowOnly", "Show Only"),
		LOCTEXT("HintClose", "Close"),
	};
	const TAttribute<FText> Keys[] = {
		LOCTEXT("KeyLMB", "LMB"),
		LOCTEXT("KeyRMB", "RMB"),
		LOCTEXT("KeyCtrlLMB", "CTRL + LMB"),
		CloseKeyText,
	};
	static_assert(UE_ARRAY_COUNT(Labels) == MaxFooterHints && UE_ARRAY_COUNT(Keys) == MaxFooterHints, "The open animation's length counts on this many hints");

	const TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < MaxFooterHints; ++Index)
	{
		Footer->AddSlot().AutoWidth().Padding(4.f, 0.f)
		[
			// Each hint fades in while sliding over from the left, and its key badge follows a beat later.
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(0.f)
			.Visibility_Lambda([this, Index]() { return FooterHintShown[Index]() ? EVisibility::Visible : EVisibility::Collapsed; })
			.ColorAndOpacity_Lambda([this, Index]() { return FLinearColor(1.f, 1.f, 1.f, GetHintLerp(Index, false)); })
			.RenderTransform_Lambda([this, Index]() -> TOptional<FSlateRenderTransform>
			{
				return FSlateRenderTransform(FVector2D(-HintSlideDistance * (1.f - GetHintLerp(Index, false)), 0.f));
			})
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.35f))
				.Padding(FMargin(14.f, 4.f))
				[
					BuildKeyHint(Labels[Index], Keys[Index],
						TAttribute<FLinearColor>::CreateLambda([this, Index]() { return FLinearColor(1.f, 1.f, 1.f, GetHintLerp(Index, true)); }))
				]
			]
		];
	}
	return Footer;
}

// ---------------------------------------------------------------------------------------------------------------------
// Sorting (Balance and Manual), remembered between sessions

void SFicsitStatsWindow::LoadSorts()
{
	Model->RestoreSorts(
		FicsitPlayerPrefs::GetInt(TEXT("BalanceSortColumn"), static_cast<int32>(EBalanceSort::Net)),
		FicsitPlayerPrefs::GetBool(TEXT("BalanceSortAscending"), true),
		FicsitPlayerPrefs::GetInt(TEXT("ManualSortColumn"), static_cast<int32>(EManualSort::Built)),
		FicsitPlayerPrefs::GetBool(TEXT("ManualSortAscending"), false));
}

void SFicsitStatsWindow::SaveSorts() const
{
	FicsitPlayerPrefs::SetInt(TEXT("BalanceSortColumn"), static_cast<int32>(Model->GetBalanceSort()));
	FicsitPlayerPrefs::SetBool(TEXT("BalanceSortAscending"), Model->IsBalanceAscending());
	FicsitPlayerPrefs::SetInt(TEXT("ManualSortColumn"), static_cast<int32>(Model->GetManualSort()));
	FicsitPlayerPrefs::SetBool(TEXT("ManualSortAscending"), Model->IsManualAscending());
}

void SFicsitStatsWindow::ClickBalanceSort(EBalanceSort Column)
{
	Model->ClickBalanceSort(Column);
	SaveSorts();
	Refresh();
}

void SFicsitStatsWindow::ClickManualSort(EManualSort Column)
{
	Model->ClickManualSort(Column);
	SaveSorts();
	Refresh();
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildBalanceSortHeader(EBalanceSort Column, const FText& Label, float Width, bool bAlignRight)
{
	return BuildSortHeader(Label, Width, bAlignRight,
		[this, Column]() { return Model->GetBalanceSort() == Column; },
		[this]() { return Model->IsBalanceAscending(); },
		[this, Column]() { ClickBalanceSort(Column); });
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildSortHeader(const FText& Label, float Width, bool bAlignRight, TFunction<bool()> IsActive,
	TFunction<bool()> IsAscending, TFunction<void()> OnClick)
{

	// Orange triangle on the sorted column: up = ascending, down = descending. Hidden (not collapsed) elsewhere so the
	// labels don't shift when the sort changes.
	TSharedRef<SWidget> Arrow = SNew(SBox)
		.WidthOverride(9.f)
		.HeightOverride(9.f)
		.Visibility_Lambda([IsActive]() { return IsActive() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
		[
			SNew(SFicsitTrendGlyph)
			.Direction_Lambda([IsAscending]() { return IsAscending() ? 1 : -1; })
			.Color(FicsitStatsStyle::Orange())
		];
	TSharedRef<SWidget> Text = MakeText(Label, FontSize::Small);

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	if (bAlignRight)
	{
		// Arrow on the left so the label stays flush with the numbers below it.
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)[Arrow];
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text];
	}
	else
	{
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text];
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)[Arrow];
	}

	TSharedRef<SWidget> Button = SNew(SFicsitClickBox)
		.HoverColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
		.Padding(bAlignRight ? FMargin(6.f, 2.f, 0.f, 2.f) : FMargin(0.f, 2.f, 6.f, 2.f))
		.ToolTipText(FText::Format(LOCTEXT("SortTooltip", "Sort by {0} (click again to reverse)"), Label))
		.OnClicked_Lambda([OnClick]() { OnClick(); })
		[
			Content
		];

	if (Width <= 0.f)
	{
		return Button;
	}
	return SNew(SBox)
		.WidthOverride(Width)
		.HAlign(bAlignRight ? HAlign_Right : HAlign_Left)
		[
			Button
		];
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings panel (gear button)

TSharedRef<SWidget> SFicsitStatsWindow::BuildSettingsButton()
{
	return SNew(SBox)
		.WidthOverride(64.f)
		.ToolTipText(LOCTEXT("SettingsTooltip", "Settings"))
		[
			SNew(SFicsitClickBox)
			.Color_Lambda([this]() { return bSettingsOpen ? FicsitStatsStyle::Orange() : FicsitStatsStyle::CloseBlock(); })
			.HoverColor(FicsitStatsStyle::Orange())
			.OnClicked_Lambda([this]() { bSettingsOpen = !bSettingsOpen; })
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(26.f)
					.HeightOverride(26.f)
					[
						SNew(SImage)
						.Image(FicsitStatsStyle::TextureBrush(TEXT("/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/TXUI_MIcon_Cogwheel.TXUI_MIcon_Cogwheel")))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildSettingsPanel()
{
	const TWeakObjectPtr<UObject> Context = WorldContext;

	// One option: title and explanation on the left, its control on the right (the game's settings-row layout).
	auto OptionRow = [](const FText& Title, const FText& Description, const TSharedRef<SWidget>& Control) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.Padding(0.f, 0.f, 0.f, 3.f)
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FicsitStatsStyle::RowTile())
				.Padding(FMargin(14.f, 10.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							MakeText(Title, FontSize::Body)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 2.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(Description)
							.Font(FicsitStatsStyle::Font(FontSize::Caption))
							.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
							.AutoWrapText(true)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Control
					]
				]
			];
	};

	auto Toggle = [this](TFunction<bool()> Get, TFunction<void(bool)> Set) -> TSharedRef<SWidget>
	{
		return SNew(SFicsitClickBox)
			.HoverColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
			.Padding(FMargin(6.f))
			.OnClicked_Lambda([this, Get, Set]()
			{
				Set(!Get());
				Refresh();
			})
			[
				MakeCheckSquare(TAttribute<bool>::CreateLambda([Get]() { return Get(); }))
			];
	};

	// [ - ]  value  [ + ] over a fixed list of values.
	auto Stepper = [this](TArray<float> Values, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Format) -> TSharedRef<SWidget>
	{
		auto IndexOf = [Values, Get]()
		{
			const float Current = Get();
			int32 Best = 0;
			for (int32 Index = 1; Index < Values.Num(); ++Index)
			{
				if (FMath::Abs(Values[Index] - Current) < FMath::Abs(Values[Best] - Current))
				{
					Best = Index;
				}
			}
			return Best;
		};
		const int32 Last = Values.Num() - 1;
		auto Step = [this, Values, IndexOf, Set, Last](int32 Delta)
		{
			Set(Values[FMath::Clamp(IndexOf() + Delta, 0, Last)]);
			Refresh();
		};
		auto StepButton = [](const FText& Glyph, TFunction<bool()> Enabled, TFunction<void()> OnClick) -> TSharedRef<SWidget>
		{
			const FLinearColor Disabled(0.16f, 0.16f, 0.16f);
			return SNew(SBox)
				.WidthOverride(30.f)
				.HeightOverride(30.f)
				[
					SNew(SFicsitClickBox)
					.Color_Lambda([Enabled, Disabled]() { return Enabled() ? FicsitStatsStyle::TabActive() : Disabled; })
					.HoverColor_Lambda([Enabled, Disabled]() { return Enabled() ? FicsitStatsStyle::Orange() : Disabled; })
					.OnClicked_Lambda([Enabled, OnClick]() { if (Enabled()) { OnClick(); } })
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							MakeText(Glyph, FontSize::Body, FicsitStatsStyle::TextPrimary(), true)
						]
					]
				];
		};

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StepButton(INVTEXT("–"), [IndexOf]() { return IndexOf() > 0; }, [Step]() { Step(-1); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(76.f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([Get, Format]() { return Format(Get()); })
					.Font(FicsitStatsStyle::Font(FontSize::Body))
					.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StepButton(INVTEXT("+"), [IndexOf, Last]() { return IndexOf() < Last; }, [Step]() { Step(1); })
			];
	};

	auto Minutes = [](float Value) { return FText::Format(LOCTEXT("SettingMinutes", "{0} min"), FText::AsNumber(FMath::RoundToInt(Value))); };
	auto Seconds = [](float Value) { return FText::Format(LOCTEXT("SettingSeconds", "{0} s"), FText::AsNumber(FMath::RoundToInt(Value))); };
	auto Count = [](float Value) { return FText::AsNumber(FMath::RoundToInt(Value)); };
	// Every whole number in an option's range.
	auto Range = [](const FFicsitStatsOption& Option)
	{
		TArray<float> Values;
		for (int32 Value = FMath::RoundToInt(Option.Min); Value <= FMath::RoundToInt(Option.Max); ++Value)
		{
			Values.Add(static_cast<float>(Value));
		}
		return Values;
	};

	TSharedRef<SVerticalBox> Options = SNew(SVerticalBox);
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetShowHud", "Show pinned items on the HUD"),
			LOCTEXT("SetShowHudDesc", "Your pinned items show up under the objective panel while you play."),
			Toggle([Context]() { return FFicsitStatsSettings::GetShowPinnedHud(Context.Get()); },
				[Context](bool bValue) { FFicsitStatsSettings::SetShowPinnedHud(Context.Get(), bValue); }))
	];
	// The way out for a pinned item that no longer shows up in any list, since that's the only place to unpin it.
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetClearPins", "Clear pinned items"),
			LOCTEXT("SetClearPinsDesc", "Unpins everything on the HUD in this world. Use this if an item is stuck on the HUD because it isn't in any list anymore."),
			SNew(SFicsitClickBox)
			.Color_Lambda([]() { return FFicsitPins::Get().Num() > 0 ? FicsitStatsStyle::TabActive() : FLinearColor(0.16f, 0.16f, 0.16f); })
			.HoverColor_Lambda([]() { return FFicsitPins::Get().Num() > 0 ? FicsitStatsStyle::Orange() : FLinearColor(0.16f, 0.16f, 0.16f); })
			.Padding(FMargin(16.f, 6.f))
			.OnClicked_Lambda([this]()
			{
				FFicsitPins::Clear();
				Refresh();
			})
			[
				MakeText(LOCTEXT("SetClearPinsButton", "Clear"), FontSize::Body)
			])
	];
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetHudMinutes", "HUD average"),
			LOCTEXT("SetHudMinutesDesc", "The HUD averages each pinned item's net rate over this long, so a longer time gives you a steadier number."),
			Stepper(Range(FicsitStatsOptions::HudAverageMinutes),
				[Context]() { return static_cast<float>(FFicsitStatsSettings::GetHudAverageMinutes(Context.Get())); },
				[Context](float Value) { FFicsitStatsSettings::SetHudAverageMinutes(Context.Get(), FMath::RoundToInt(Value)); },
				Minutes))
	];
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetSinkDemand", "Count AWESOME Sink as consumption"),
			LOCTEXT("SetSinkDemandDesc", "When this is off, anything going into the AWESOME Sink counts as overflow (i.e. surplus). Turn it on if you're making items on purpose just to sink them."),
			Toggle([Context]() { return FFicsitStatsSettings::GetSinkCountsAsDemand(Context.Get()); },
				[Context](bool bValue) { FFicsitStatsSettings::SetSinkCountsAsDemand(Context.Get(), bValue); }))
	];
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetStorageWarning", "Storage warning"),
			LOCTEXT("SetStorageWarningDesc", "Items only warn that their storage containers and fluid buffers are running out when they'll be empty within this long at the current rate, so a slow drain on a big stockpile doesn't get flagged."),
			Stepper({5.f, 10.f, 15.f, 20.f, 30.f, 45.f, 60.f, 90.f, 120.f, 180.f, 240.f},
				[Context]() { return static_cast<float>(FFicsitStatsSettings::GetStorageWarningMinutes(Context.Get())); },
				[Context](float Value) { FFicsitStatsSettings::SetStorageWarningMinutes(Context.Get(), FMath::RoundToInt(Value)); },
				Minutes))
	];
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetGraphItems", "Items shown on graphs"),
			LOCTEXT("SetGraphItemsDesc", "How many of the top items each graph shows until you click on items to pick your own."),
			Stepper(Range(FicsitStatsOptions::DefaultGraphItems),
				[Context]() { return static_cast<float>(FFicsitStatsSettings::GetDefaultGraphItems(Context.Get())); },
				[Context](float Value) { FFicsitStatsSettings::SetDefaultGraphItems(Context.Get(), FMath::RoundToInt(Value)); },
				Count))
	];
	Options->AddSlot().AutoHeight()
	[
		OptionRow(LOCTEXT("SetGraphFluids", "Include fluids on graphs"),
			LOCTEXT("SetGraphFluidsDesc", "Lets fluids (e.g. Water) show up among the top items on the graphs. Turn it off if their big rates are squashing the other lines. You can still click on a fluid to show it."),
			Toggle([Context]() { return FFicsitStatsSettings::GetGraphFluidsByDefault(Context.Get()); },
				[Context](bool bValue) { FFicsitStatsSettings::SetGraphFluidsByDefault(Context.Get(), bValue); }))
	];
	// The scan runs on the host, so clients don't get this option.
	Options->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const AFicsitStatisticsSubsystem* Subsystem = GetSubsystem();
			return Subsystem && Subsystem->RunsScan() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			OptionRow(LOCTEXT("SetSampleInterval", "Scan interval"),
				LOCTEXT("SetSampleIntervalDesc", "How often we check every machine. Shorter reacts faster, but longer is easier on performance in really big factories."),
				Stepper({1.f, 2.f, 3.f, 5.f, 10.f, 15.f, 30.f, 60.f},
					[Context]() { return FFicsitStatsSettings::GetSampleIntervalSeconds(Context.Get()); },
					[Context](float Value) { FFicsitStatsSettings::SetSampleIntervalSeconds(Context.Get(), Value); },
					Seconds))
		]
	];

	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return bSettingsOpen ? EVisibility::Visible : EVisibility::Collapsed; })
		// Dim the window behind the panel. Clicking outside the panel closes it.
		+ SOverlay::Slot()
		[
			SNew(SFicsitClickBox)
			.Color(FLinearColor(0.f, 0.f, 0.f, 0.45f))
			.HoverColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
			.OnClicked_Lambda([this]() { bSettingsOpen = false; })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(580.f)
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FicsitStatsStyle::Sidebar())
				.Padding(0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
						.Padding(FMargin(14.f, 8.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							[
								MakeText(LOCTEXT("SettingsTitle", "Settings"), FontSize::Body)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								MakeText(LOCTEXT("SettingsSaved", "Changes save automatically"), FontSize::Small, FicsitStatsStyle::TextSecondary())
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(10.f, 10.f, 10.f, 0.f)
					[
						Options
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					.Padding(10.f, 7.f, 10.f, 10.f)
					[
						SNew(SFicsitClickBox)
						.Color(FicsitStatsStyle::TabActive())
						.HoverColor(FicsitStatsStyle::Orange())
						.Padding(FMargin(16.f, 6.f))
						.OnClicked_Lambda([this, Context]()
						{
							FFicsitStatsSettings::ResetToDefaults(Context.Get());
							Refresh();
						})
						[
							MakeText(LOCTEXT("SettingsReset", "Reset to defaults"), FontSize::Body)
						]
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------------------------------
// Rows

TSharedRef<SWidget> SFicsitStatsWindow::BuildItemRow(const TSharedRef<FItemRow>& Data, bool bProduction)
{
	const TSubclassOf<UFGItemDescriptor> Item = Data->Item;

	auto RateText = [Data]() { return Data->RateText; };
	auto BarColor = [this, Item, bProduction]()
	{
		return Model->IsOnGraph(Item, bProduction) ? GetSeriesColor(Item) : FLinearColor(0.37f, 0.37f, 0.37f);
	};
	// Rows that aren't on the graph fade back, so what the graph shows reads at a glance (not only from the bar).
	auto Fade = [this, Item, bProduction]()
	{
		return Model->IsOnGraph(Item, bProduction) ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f);
	};

	TSharedPtr<SFicsitGraph> Graph = bProduction ? ProducedGraph : ConsumedGraph;
	const TSharedRef<TWeakPtr<SWidget>> RowHover = MakeShared<TWeakPtr<SWidget>>();
	TSharedPtr<SFicsitClickBox> RowBox;

	TSharedRef<SWidget> Result = SNew(SBox)
		.Padding(0.f, 0.f, 4.f, 3.f)
		[
			SAssignNew(RowBox, SFicsitClickBox)
			.Color(FicsitStatsStyle::RowTile())
			.HoverColor(FicsitStatsStyle::RowTileHover())
			.OnClicked_Lambda([this, Item, bProduction]()
			{
				Model->ToggleOnGraph(Item, bProduction);
				Refresh();
			})
			.OnCtrlClicked_Lambda([this, Item, bProduction]()
			{
				Model->SoloOnGraph(Item, bProduction);
				const UClass* Solo = Model->GetGraph(bProduction).GetSolo();
				UE_LOG(LogFicsitStatistics, Log, TEXT("Graph: %s solo %s"), bProduction ? TEXT("Production") : TEXT("Consumption"),
					Solo ? *GetNameSafe(Solo) : TEXT("off (defaults)"));
				Refresh();
			})
			.OnRightClicked_Lambda([this, Item]() { ShowDetails(Item); })
			.ToolTipText_Lambda([Data]() { return Data->TooltipText; })
			.OnHovered_Lambda([Graph, Item]() { if (Graph.IsValid()) { Graph->SetHighlightedItem(Item); } })
			.OnUnhovered_Lambda([Graph]() { if (Graph.IsValid()) { Graph->SetHighlightedItem(nullptr); } })
			[
				SNew(SVerticalBox)
				// One line: icon, name, rate, and the % of max only when it's below 95% (the max itself is in the tooltip).
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(6.f, 3.f, 12.f, 3.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						// Fades everything but the pin (which has its own on / off look).
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor(FLinearColor::Transparent)
						.Padding(0.f)
						.ColorAndOpacity_Lambda(Fade)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(32.f)
								.HeightOverride(32.f)
								[
									SNew(SImage)
									.Image(FicsitStatsStyle::ItemIcon(Item))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							.Padding(10.f, 0.f, 8.f, 0.f)
							[
								BuildItemName(Item)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								MakeText(TAttribute<FText>::CreateLambda(RateText), FontSize::Body)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								// Hidden, not collapsed, so the rates stay lined up.
								SNew(SBox)
								.WidthOverride(48.f)
								.HAlign(HAlign_Right)
								.Visibility_Lambda([Data]() { return Data->bShowPercent ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
								[
									MakeText(TAttribute<FText>::CreateLambda([Data]() { return Data->PercentText; }), FontSize::Caption, FicsitStatsStyle::TextSecondary())
								]
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.f, 0.f, 0.f, 0.f)
					[
						MakePinButton(Item, RowHover)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(4.f)
					[
						SNew(SFicsitShareBar)
						.Fraction_Lambda([Data]() { return Data->Share; })
						.Color_Lambda(BarColor)
					]
				]
			]
		];
	*RowHover = RowBox;
	return Result;
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildBalanceRow(const TSharedRef<FBalanceRow>& Data)
{
	const TSubclassOf<UFGItemDescriptor> Item = Data->Item;

	auto NetText = [Data]() { return Data->NetText; };
	auto NetColor = [Data]() { return FSlateColor(Data->NetColor); };

	auto Number = [](const TAttribute<FText>& Text, float Width, const TAttribute<FSlateColor>& Color)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(FicsitStatsStyle::Font(FontSize::Body))
				.ColorAndOpacity(Color)
			];
	};
	const FSlateColor Primary(FicsitStatsStyle::TextPrimary());

	auto IsSelected = [this, Item]() { return Model->GetDetails().Item == Item; };
	auto SunkVisibility = [this]() { return Model->GetBalance().bShowSunkColumn ? EVisibility::Visible : EVisibility::Collapsed; };
	const TSharedRef<TWeakPtr<SWidget>> RowHover = MakeShared<TWeakPtr<SWidget>>();
	TSharedPtr<SFicsitClickBox> RowBox;
	TSharedRef<SWidget> Result = SNew(SBox)
		.Padding(0.f, 0.f, 4.f, 3.f)
		[
			SAssignNew(RowBox, SFicsitClickBox)
			.Color_Lambda([IsSelected]() { return IsSelected() ? FicsitStatsStyle::TabActive() : FicsitStatsStyle::RowTile(); })
			.HoverColor_Lambda([IsSelected]() { return IsSelected() ? FicsitStatsStyle::TabActive() : FicsitStatsStyle::RowTileHover(); })
			.Padding(FMargin(0.f, 0.f, 12.f, 0.f))
			.OnClicked_Lambda([this, Item]() { ShowDetails(Item); })
			[
				SNew(SHorizontalBox)
				// Orange selection marker on the left edge of the selected row.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(4.f)
					[
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor_Lambda([IsSelected]() { return IsSelected() ? FicsitStatsStyle::Orange() : FLinearColor::Transparent; })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.f, 4.f, 0.f, 4.f)
				[
					SNew(SBox)
					.WidthOverride(38.f)
					.HeightOverride(38.f)
					[
						SNew(SImage)
						.Image(FicsitStatsStyle::ItemIcon(Item))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(10.f, 0.f, 8.f, 0.f)
				[
					BuildItemName(Item, true)
				]
				+ SHorizontalBox::Slot().AutoWidth()[Number(TAttribute<FText>::CreateLambda([Data]() { return Data->ProducedText; }), BalanceRateColumn, Primary)]
				+ SHorizontalBox::Slot().AutoWidth()[Number(TAttribute<FText>::CreateLambda([Data]() { return Data->ConsumedText; }), BalanceRateColumn, Primary)]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda(SunkVisibility)
					[
						Number(TAttribute<FText>::CreateLambda([Data]() { return Data->SunkText; }), BalanceSunkColumn, FSlateColor(FicsitStatsStyle::TextSecondary()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()[Number(TAttribute<FText>::CreateLambda(NetText), BalanceNetColumn, TAttribute<FSlateColor>::CreateLambda(NetColor))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return Model->GetBalance().bShowDepotColumn ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						Number(TAttribute<FText>::CreateLambda([Data]() { return Data->DepotText; }), BalanceDepotColumn, FSlateColor(FicsitStatsStyle::TextSecondary()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return Model->GetBalance().bShowStorageColumn ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						// Red while storage runs out within the warning time (how long is under the item name).
						Number(TAttribute<FText>::CreateLambda([Data]() { return Data->StoredText; }), BalanceStorageColumn,
							TAttribute<FSlateColor>::CreateLambda([Data]()
							{
								return FSlateColor(Data->bStorageRunsOutSoon ? FicsitStatsStyle::Deficit() : FicsitStatsStyle::TextSecondary());
							}))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)[MakePinButton(Item, RowHover)]
			]
		];
	*RowHover = RowBox;
	return Result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Data

AFicsitStatisticsSubsystem* SFicsitStatsWindow::GetSubsystem() const
{
	return AFicsitStatisticsSubsystem::Get(WorldContext.Get());
}

namespace
{
	/** What the window shows before there is a world: nothing. */
	const FFicsitHistory& EmptyWindowHistory()
	{
		static const FFicsitHistory History = []()
		{
			FFicsitHistory Result;
			Result.EnsureLayout();
			return Result;
		}();
		return History;
	}

	const FFicsitProductionSnapshot& EmptyWindowScan()
	{
		static const FFicsitProductionSnapshot Scan;
		return Scan;
	}
}

FFicsitRangeReader SFicsitStatsWindow::MakeRange() const
{
	if (AFicsitStatisticsSubsystem* Subsystem = GetSubsystem())
	{
		return FFicsitRangeReader(*Subsystem, Model->GetTimeRange());
	}
	return FFicsitRangeReader(EmptyWindowHistory(), EmptyWindowScan(), Model->GetTimeRange());
}

FLinearColor SFicsitStatsWindow::GetSeriesColor(TSubclassOf<UFGItemDescriptor> Item) const
{
	return FicsitStatsStyle::SeriesColor(Model->GetSeriesColorIndex(Item));
}

void SFicsitStatsWindow::Refresh()
{
	const AFicsitStatisticsSubsystem* Subsystem = GetSubsystem();
	const FFicsitRangeReader Range = MakeRange();

	FFicsitStatsWindowModel::FRefreshInputs Inputs;
	// What the Space Elevator phase and the HUB milestone still need, and everything in the Dimensional Depot. Cheap
	// (small arrays), so it's read live rather than once.
	Inputs.Context = FFicsitOutlookContext::Read(WorldContext.Get());
	if (Subsystem)
	{
		Inputs.RecordedItems = Subsystem->GetRecordedItems();
	}
	Inputs.DefaultGraphItems = FFicsitStatsSettings::GetDefaultGraphItems(WorldContext.Get());
	Inputs.bGraphFluids = FFicsitStatsSettings::GetGraphFluidsByDefault(WorldContext.Get());
	Model->Refresh(Range, Subsystem ? Subsystem->GetLatestSnapshot() : EmptyWindowScan(), MoveTemp(Inputs));

	RebuildSidebar();
	RebuildLists();
	UpdateGraphs(Range);
	RebuildDetails(Range);

	if (Model->IsSupplyChainOpen() && SupplyChainView.IsValid())
	{
		SupplyChainView->Refresh(Model->GetContext().Needs.Combined);
	}
}

void SFicsitStatsWindow::OpenSupplyChain(TSubclassOf<UFGItemDescriptor> Item)
{
	if (!SupplyChainView.IsValid() || !Item)
	{
		return;
	}
	SupplyChainView->SetRoot(Item);
	Model->OpenSupplyChain();
	Refresh();
}

void SFicsitStatsWindow::CloseSupplyChain()
{
	Model->CloseSupplyChain();
	Refresh();
}

void SFicsitStatsWindow::RebuildSidebar()
{
	if (!SidebarList.IsValid())
	{
		return;
	}

	// Only rebuild when the set of entries changes, since selection and counts are live attributes.
	TArray<UClass*> Signature;
	for (const auto& Pair : Model->GetCategories())
	{
		Signature.Add(Pair.Key);
	}
	const bool bHasElevator = Model->HasMilestoneNeeds();
	if (bSidebarBuilt && Signature == SidebarSignature && bHasElevator == bSidebarHasElevator)
	{
		return;
	}
	bSidebarBuilt = true;
	SidebarSignature = MoveTemp(Signature);
	bSidebarHasElevator = bHasElevator;

	SidebarList->ClearChildren();
	SidebarList->AddSlot().AutoHeight()[BuildCategoryButton(nullptr, LOCTEXT("CategoryAll", "All"))];
	if (bHasElevator)
	{
		SidebarList->AddSlot().AutoHeight()[BuildElevatorButton()];
		// Thin divider between the special filters and the game's categories.
		SidebarList->AddSlot().AutoHeight().Padding(14.f, 6.f)
		[
			SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SBorder)
				.BorderImage(FicsitStatsStyle::White())
				.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.12f))
			]
		];
	}
	for (const auto& Pair : Model->GetCategories())
	{
		SidebarList->AddSlot().AutoHeight()[BuildCategoryButton(Pair.Key, Pair.Value)];
	}
}

namespace
{
	/**
	 * Brings a list in line with Rows without recreating widgets: values are copied into each item's shared data (which
	 * the row's attributes read), new items get a widget once, and the list is only re-slotted when the order changes.
	 */
	template <typename RowType, typename CacheType, typename BuildFn>
	void SyncList(SVerticalBox& List, const TArray<RowType>& Rows, CacheType& Cache, const FText& EmptyText, BuildFn&& Build)
	{
		TArray<UClass*> NewOrder;
		NewOrder.Reserve(Rows.Num());
		for (const RowType& Row : Rows)
		{
			UClass* Key = Row.Item.Get();
			NewOrder.Add(Key);
			if (TSharedRef<RowType>* Existing = Cache.Data.Find(Key))
			{
				**Existing = Row;
			}
			else
			{
				TSharedRef<RowType> Data = MakeShared<RowType>(Row);
				Cache.Data.Add(Key, Data);
				Cache.Widgets.Add(Key, Build(Data));
			}
		}

		const bool bEmpty = Rows.Num() == 0;
		if (Cache.bInitialized && NewOrder == Cache.Order && bEmpty == Cache.bEmptyShown)
		{
			return;
		}
		Cache.bInitialized = true;
		Cache.Order = MoveTemp(NewOrder);
		Cache.bEmptyShown = bEmpty;

		List.ClearChildren();
		for (UClass* Key : Cache.Order)
		{
			List.AddSlot().AutoHeight()[Cache.Widgets.FindChecked(Key)];
		}
		if (bEmpty)
		{
			List.AddSlot().AutoHeight().Padding(12.f)
			[
				SNew(STextBlock)
				.Text(EmptyText)
				.Font(FicsitStatsStyle::Font(FontSize::Small))
				.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
				.AutoWrapText(true)
			];
		}
	}
}

void SFicsitStatsWindow::RebuildLists()
{
	if (ProducedList.IsValid())
	{
		SyncList(*ProducedList, Model->GetRateRows(true), ProducedCache, LOCTEXT("NoItems", "Nothing here yet."),
			[this](const TSharedRef<FItemRow>& Data) { return BuildItemRow(Data, true); });
	}
	if (ConsumedList.IsValid())
	{
		SyncList(*ConsumedList, Model->GetRateRows(false), ConsumedCache, LOCTEXT("NoItems", "Nothing here yet."),
			[this](const TSharedRef<FItemRow>& Data) { return BuildItemRow(Data, false); });
	}
	if (BalanceList.IsValid())
	{
		SyncList(*BalanceList, Model->GetBalance().Rows, BalanceCache, LOCTEXT("NoBalance", "No items match."),
			[this](const TSharedRef<FBalanceRow>& Data) { return BuildBalanceRow(Data); });
	}
	if (PowerProducerList.IsValid())
	{
		SyncList(*PowerProducerList, Model->GetPower().Producers, PowerProducerCache, LOCTEXT("NoPowerProducers", "No generators yet."),
			[this](const TSharedRef<FPowerRow>& Data) { return BuildPowerRow(Data, true); });
	}
	if (PowerConsumerList.IsValid())
	{
		SyncList(*PowerConsumerList, Model->GetPower().Consumers, PowerConsumerCache, LOCTEXT("NoPowerConsumers", "Nothing uses power yet."),
			[this](const TSharedRef<FPowerRow>& Data) { return BuildPowerRow(Data, false); });
	}
	if (ManualList.IsValid())
	{
		SyncList(*ManualList, Model->GetManual().Rows, ManualCache,
			LOCTEXT("NoManual", "Nothing crafted, built or dismantled in this time range."),
			[this](const TSharedRef<FManualRow>& Data) { return BuildManualRow(Data); });
	}
}

void SFicsitStatsWindow::UpdateGraphs(const FFicsitRangeReader& Range)
{
	const float Span = Range.GetGraphSpanSeconds();

	auto Fill = [&](const TSharedPtr<SFicsitGraph>& Graph, bool bProduction)
	{
		if (!Graph.IsValid())
		{
			return;
		}
		TArray<SFicsitGraph::FSeries> Series;
		for (const FItemRow& Row : Model->GetRateRows(bProduction))
		{
			if (!Model->IsOnGraph(Row.Item, bProduction))
			{
				continue;
			}
			SFicsitGraph::FSeries& Each = Series.AddDefaulted_GetRef();
			Each.Item = Row.Item;
			Each.Color = GetSeriesColor(Row.Item);
			Range.GetSeries(Row.Item, bProduction, Each.Times, Each.Values);
		}
		Graph->SetData(MoveTemp(Series), Span);
	};
	Fill(ProducedGraph, true);
	Fill(ConsumedGraph, false);

	if (PowerGraph.IsValid())
	{
		TArray<SFicsitGraph::FSeries> Series;
		auto AddSeries = [&](EFicsitPowerTotal Total, const FText& Label, const FLinearColor& Color)
		{
			SFicsitGraph::FSeries& Each = Series.AddDefaulted_GetRef();
			Each.Label = Label;
			Each.bPower = true;
			Each.Color = Color;
			Range.GetPowerSeries(Total, Each.Times, Each.Values);
		};
		AddSeries(EFicsitPowerTotal::Capacity, LOCTEXT("PowerCapacity", "Capacity"), FicsitStatsStyle::SeriesColor(9));
		AddSeries(EFicsitPowerTotal::MaxConsumption, LOCTEXT("PowerMaxConsumption", "Max consumption"), FicsitStatsStyle::SeriesColor(2));
		AddSeries(EFicsitPowerTotal::Produced, LOCTEXT("PowerProduced", "Production"), FicsitStatsStyle::Produced());
		AddSeries(EFicsitPowerTotal::Consumed, LOCTEXT("PowerConsumed", "Consumption"), FicsitStatsStyle::Consumed());
		PowerGraph->SetData(MoveTemp(Series), Span);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Details pane (UI adaptation A4)


namespace
{
	TSharedRef<SWidget> MakeSectionStrip(const FText& Title, const TAttribute<FText>& Right)
	{
		return SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
			.Padding(FMargin(10.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[MakeText(Title, FontSize::Body)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MakeText(Right, FontSize::Small)]
			];
	}

	TSharedRef<SWidget> MakeKeyValue(const FText& Key, const TAttribute<FText>& Value,
		const TAttribute<FSlateColor>& ValueColor = FSlateColor(FicsitStatsStyle::TextPrimary()),
		const TAttribute<FSlateColor>& KeyColor = FSlateColor(FicsitStatsStyle::TextSecondary()))
	{
		// The value is sized first and the label gets what's left, so a long label ends in "..." instead of being cut off.
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Key)
				.ToolTipText(Key)
				.Font(FicsitStatsStyle::Font(FontSize::Small))
				.ColorAndOpacity(KeyColor)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Value)
				.Font(FicsitStatsStyle::Font(FontSize::Small))
				.ColorAndOpacity(ValueColor)
			];
	}

	FText SourceName(int32 Source)
	{
		switch (static_cast<EFicsitRateSource>(Source))
		{
		case EFicsitRateSource::Manufacturing:   return LOCTEXT("SourceManufacturing", "Manufacturing");
		case EFicsitRateSource::Extraction:      return LOCTEXT("SourceExtraction", "Extraction");
		case EFicsitRateSource::PowerGeneration: return LOCTEXT("SourcePower", "Power generation");
		case EFicsitRateSource::ResourceSink:    return LOCTEXT("SourceSink", "AWESOME Sink (overflow)");
		case EFicsitRateSource::SpaceElevator:   return LOCTEXT("SourceElevator", "Space Elevator");
		default:                                 return FText::GetEmpty();
		}
	}
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildDetailsPane()
{
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.25f))
		.Padding(0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(DetailsBox, SVerticalBox)
			]
		];
}

void SFicsitStatsWindow::ShowDetails(TSubclassOf<UFGItemDescriptor> Item)
{
	Model->ShowDetails(Item);
	Refresh();
}

void SFicsitStatsWindow::UpdateDetailGraph(const FFicsitRangeReader& Range)
{
	const TSubclassOf<UFGItemDescriptor> Item = Model->GetDetails().Item;
	if (!DetailGraph.IsValid() || !Item)
	{
		return;
	}
	TArray<SFicsitGraph::FSeries> Series;
	SFicsitGraph::FSeries& P = Series.AddDefaulted_GetRef();
	P.Item = Item;
	P.Color = FicsitStatsStyle::Produced();
	Range.GetSeries(Item, true, P.Times, P.Values);
	SFicsitGraph::FSeries& C = Series.AddDefaulted_GetRef();
	C.Item = Item;
	C.Color = FicsitStatsStyle::Consumed();
	Range.GetSeries(Item, false, C.Times, C.Values);
	DetailGraph->SetData(MoveTemp(Series), Range.GetGraphSpanSeconds());
}

void SFicsitStatsWindow::RebuildDetails(const FFicsitRangeReader& Range)
{
	if (!DetailsBox.IsValid())
	{
		return;
	}

	// 1) Copy the live values every time. Widgets read them through attributes.
	FFicsitWindowDetails& State = *DetailState;
	State = Model->GetDetails();
	const TSubclassOf<UFGItemDescriptor> Item = State.Item;

	// 2) Rebuild the layout only when its shape changes (different item, a source or section appearing, ...).
	FString LayoutKey = State.LayoutKey();
	if (LayoutKey == DetailLayoutKey)
	{
		UpdateDetailGraph(Range);
		return;
	}
	DetailLayoutKey = MoveTemp(LayoutKey);
	DetailsBox->ClearChildren();
	DetailGraph.Reset();

	if (!Item)
	{
		DetailsBox->AddSlot().AutoHeight().Padding(16.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DetailsHint", "Select an item to see where it comes from, where it goes, and how its machines are doing."))
			.Font(FicsitStatsStyle::Font(FontSize::Small))
			.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
			.AutoWrapText(true)
		];
		return;
	}

	const TSharedRef<FFicsitWindowDetails> Live = DetailState;

	// Header: big icon, name, category, pin.
	const TSubclassOf<UFGCategory> Category = UFGItemDescriptor::GetCategory(Item);
	DetailsBox->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::RowTile())
		.Padding(FMargin(10.f, 8.f))
		[
			SNew(SHorizontalBox)
			// Never narrower than the icon: an auto slot can be shrunk when the row overflows, which squashed the icon.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.MinWidth(56.f)
			[
				SNew(SBox)
				.WidthOverride(56.f)
				.HeightOverride(56.f)
				[
					SNew(SImage)
					.Image(FicsitStatsStyle::ItemIcon(Item))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					// Long names get a smaller font instead of squashing the icon.
					SNew(SFicsitFitText)
					.Text(UFGItemDescriptor::GetItemName(Item))
					.MaxSize(FontSize::Display)
					.bBold(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeText(Category ? UFGCategory::GetCategoryName(Category) : FText::GetEmpty(), FontSize::Small, FicsitStatsStyle::TextSecondary())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			[
				MakePinButton(Item)
			]
		]
	];

	// Everything that goes into this item, in the Supply chain tab.
	DetailsBox->AddSlot().AutoHeight().Padding(10.f, 8.f, 10.f, 0.f)
	[
		SNew(SFicsitClickBox)
		.Color(FicsitStatsStyle::TabActive())
		.HoverColor(FicsitStatsStyle::Orange())
		.Padding(FMargin(10.f, 5.f))
		.OnClicked_Lambda([this, Item]() { OpenSupplyChain(Item); })
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			[
				MakeText(LOCTEXT("DetailSupplyChain", "Show supply chain"), FontSize::Small)
			]
		]
	];

	// Net on one line: over the selected range (the steady picture, colored), then right now (the immediate feedback,
	// dim). Both treat the Sink as overflow unless configured otherwise.
	auto AvgNet = [Live]() { return Live->Outlook.Net(); };
	auto IsAvgBalanced = [Live]() { return IsBalanced(Live->Outlook.Produced, Live->Outlook.Demand); };
	auto NetNow = [Live]() { return Live->Produced.Total.Actual - Live->DemandNow(); };
	auto IsNetBalanced = [Live]() { return IsBalanced(Live->Produced.Total.Actual, Live->DemandNow()); };
	DetailsBox->AddSlot().AutoHeight().Padding(10.f, 8.f, 10.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			MakeText(LOCTEXT("DetailNet", "Net"), FontSize::Small, FicsitStatsStyle::TextSecondary())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FicsitStatsStyle::Font(FontSize::Small))
			.Text_Lambda([Item, AvgNet, IsAvgBalanced]()
			{
				return IsAvgBalanced() ? INVTEXT("± 0") : FicsitStatsStyle::FormatSignedRate(AvgNet(), Item);
			})
			.ColorAndOpacity_Lambda([AvgNet, IsAvgBalanced]()
			{
				return FSlateColor(IsAvgBalanced() ? FicsitStatsStyle::TextSecondary() : (AvgNet() < 0.f ? FicsitStatsStyle::Deficit() : FicsitStatsStyle::Surplus()));
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(FicsitStatsStyle::Font(FontSize::Caption))
			.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
			.ToolTipText(LOCTEXT("DetailNetNowTooltip", "Net right now. The number before it is the average over the selected range."))
			.Text_Lambda([Item, NetNow, IsNetBalanced]()
			{
				return FText::Format(LOCTEXT("DetailNetNow", "now {0}"),
					IsNetBalanced() ? INVTEXT("± 0") : FicsitStatsStyle::FormatSignedRate(NetNow(), Item));
			})
		]
	];

	// Space Elevator and HUB milestone needs (A3).
	auto AddMilestoneNeed = [this, &State, Live](FText Label, int32 FFicsitItemOutlook::* Remaining, float FFicsitItemOutlook::* EtaMinutes)
	{
		if (State.Outlook.*Remaining <= 0)
		{
			return;
		}
		DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
		[
			MakeKeyValue(Label,
				TAttribute<FText>::CreateLambda([Live, Remaining, EtaMinutes]()
				{
					const int32 Left = Live->Outlook.*Remaining;
					const float Minutes = Live->Outlook.*EtaMinutes;
					return Minutes > 0.f
						? FText::Format(LOCTEXT("DetailElevatorEta", "{0} · ~{1}"), FicsitStatsStyle::FormatNumber(Left), FicsitStatsStyle::FormatDuration(Minutes))
						: FText::Format(LOCTEXT("DetailElevatorNotMade", "{0} · not being made"), FicsitStatsStyle::FormatNumber(Left));
				}),
				FSlateColor(FicsitStatsStyle::Orange()))
		];
	};
	AddMilestoneNeed(LOCTEXT("DetailElevator", "Elevator needs"), &FFicsitItemOutlook::ElevatorRemaining, &FFicsitItemOutlook::ElevatorEtaMinutes);
	AddMilestoneNeed(LOCTEXT("DetailHub", "HUB needs"), &FFicsitItemOutlook::HubRemaining, &FFicsitItemOutlook::HubEtaMinutes);

	// Dimensional Depot stock (A6).
	if (State.bShowDepot)
	{
		DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
		[
			MakeKeyValue(LOCTEXT("DetailDepot", "Dimensional Depot"),
				TAttribute<FText>::CreateLambda([Live]()
				{
					return Live->DepotLimit > 0
						? FText::Format(INVTEXT("{0} / {1}"), FText::AsNumber(Live->Outlook.Depot), FText::AsNumber(Live->DepotLimit))
						: FText::AsNumber(Live->Outlook.Depot);
				}))
		];
	}

	// What's in storage containers and fluid buffers, which way it's going, and how long it lasts if it's falling.
	if (State.bShowStorage)
	{
		// Smaller trends are machine buffers topping up and emptying out, not the stock really moving.
		constexpr float SteadyPerMinute = 0.5f;
		DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
		[
			MakeKeyValue(LOCTEXT("DetailStorage", "In storage"),
				TAttribute<FText>::CreateLambda([Live]() { return FicsitStatsStyle::FormatAmount(Live->Outlook.Stored, Live->Item); }))
		];
		DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
		[
			MakeKeyValue(LOCTEXT("DetailStorageTrend", "Storage trend"),
				TAttribute<FText>::CreateLambda([Live]()
				{
					const FFicsitItemOutlook& Outlook = Live->Outlook;
					if (!Outlook.bStorageTrendReady)
					{
						return LOCTEXT("DetailStorageMeasuring", "measuring...");
					}
					return FMath::Abs(Outlook.StoredPerMinute) < SteadyPerMinute ? LOCTEXT("DetailStorageSteady", "steady") : FicsitStatsStyle::FormatSignedRate(Outlook.StoredPerMinute, Live->Item);
				}),
				TAttribute<FSlateColor>::CreateLambda([Live]()
				{
					const float PerMinute = Live->Outlook.StoredPerMinute;
					return FSlateColor(PerMinute <= -SteadyPerMinute ? FicsitStatsStyle::Deficit() : PerMinute >= SteadyPerMinute ? FicsitStatsStyle::Surplus() : FicsitStatsStyle::TextPrimary());
				}))
		];
		DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
		[
			SNew(SBox)
			.Visibility_Lambda([Live]() { return Live->Outlook.StorageRunoutMinutes >= 0.f ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeKeyValue(LOCTEXT("DetailStorageEmpty", "Storage empty in"),
					TAttribute<FText>::CreateLambda([Live]()
					{
						const float Minutes = Live->Outlook.StorageRunoutMinutes;
						return Minutes >= 0.f ? FicsitStatsStyle::FormatDuration(Minutes) : FText::GetEmpty();
					}),
					TAttribute<FSlateColor>::CreateLambda([Live]()
					{
						return FSlateColor(Live->Outlook.bStorageRunsOutSoon ? FicsitStatsStyle::Deficit() : FicsitStatsStyle::TextPrimary());
					}))
			]
		];
	}

	// Mini graph: production vs consumption of this item over the selected range.
	DetailsBox->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SBox)
		.HeightOverride(150.f)
		[
			SAssignNew(DetailGraph, SFicsitGraph)
		]
	];
	DetailsBox->AddSlot().AutoHeight().Padding(10.f, 4.f, 10.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)[MakeText(LOCTEXT("LegendProduced", "— Produced"), FontSize::Caption, FicsitStatsStyle::Produced())]
		+ SHorizontalBox::Slot().AutoWidth()[MakeText(LOCTEXT("LegendConsumed", "— Consumed"), FontSize::Caption, FicsitStatsStyle::Consumed())]
	];
	UpdateDetailGraph(Range);

	// Breakdown by source plus machine states, for each direction.
	auto AddFlowSection = [&](const FText& Title, bool bProduced, bool bHasFlow)
	{
		auto Flow = [Live, bProduced]() -> const FFicsitItemFlow& { return bProduced ? Live->Produced : Live->Consumed; };
		DetailsBox->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
		[
			MakeSectionStrip(Title, TAttribute<FText>::CreateLambda([Item, Flow, bHasFlow]()
			{
				const FFicsitItemFlow& F = Flow();
				return bHasFlow
					? FText::Format(LOCTEXT("DetailTotal", "{0} · max {1}"), FicsitStatsStyle::FormatRate(F.Total.Actual, Item), FicsitStatsStyle::FormatNumber(F.Total.Max))
					: FicsitStatsStyle::FormatRate(0.f, Item);
			}))
		];

		if (!bHasFlow)
		{
			DetailsBox->AddSlot().AutoHeight().Padding(10.f, 2.f)[MakeText(LOCTEXT("DetailNone", "None"), FontSize::Small, FicsitStatsStyle::TextSecondary())];
			return;
		}
		// The breakdown by source, only when there's more than one. With a single source it repeats the strip above.
		const FFicsitItemFlow& Current = Flow();
		int32 SourceCount = 0;
		for (int32 Source = 0; Source < FicsitRateSourceCount; ++Source)
		{
			SourceCount += FFicsitWindowDetails::HasSource(Current, Source) ? 1 : 0;
		}
		for (int32 Source = 0; Source < FicsitRateSourceCount && SourceCount > 1; ++Source)
		{
			if (!FFicsitWindowDetails::HasSource(Current, Source))
			{
				continue;
			}
			DetailsBox->AddSlot().AutoHeight().Padding(10.f, 2.f)
			[
				MakeKeyValue(SourceName(Source), TAttribute<FText>::CreateLambda([Item, Flow, Source]()
				{
					// Same "· max" form as the strip above.
					const FFicsitItemRate& Rate = Flow().BySource[Source];
					return Rate.Max > Rate.Actual + 0.005f
						? FText::Format(LOCTEXT("DetailTotal", "{0} · max {1}"), FicsitStatsStyle::FormatRate(Rate.Actual, Item), FicsitStatsStyle::FormatNumber(Rate.Max))
						: FicsitStatsStyle::FormatRate(Rate.Actual, Item);
				}))
			];
		}

		const FFicsitMachineCounts& Machines = bProduced ? Live->Producers : Live->Consumers;
		if (Machines.Total() > 0)
		{
			auto Counts = [Live, bProduced]() -> const FFicsitMachineCounts& { return bProduced ? Live->Producers : Live->Consumers; };
			DetailsBox->AddSlot().AutoHeight().Padding(10.f, 6.f, 10.f, 2.f)
			[
				SNew(STextBlock)
				.Text_Lambda([Counts]() { return FText::Format(LOCTEXT("DetailMachines", "{0} machines"), FText::AsNumber(Counts().Total())); })
				.Font(FicsitStatsStyle::Font(FontSize::Small))
				.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
			];
			// One line per state that occurs (no wrapping mid-phrase), with the reason a machine is stopped spelled out from
			// this item's side: makers wait for inputs or have a full output, and users are short of this item or of another input.
			const TPair<EFicsitMachineState, FText> States[] = {
				{EFicsitMachineState::Running, LOCTEXT("StateRunning", "Running")},
				{EFicsitMachineState::Starved, bProduced ? LOCTEXT("StateStarvedMaker", "Waiting for inputs") : LOCTEXT("StateStarvedUser", "Short of this item")},
				{EFicsitMachineState::OtherInputs, LOCTEXT("StateOtherInputs", "Short of another input")},
				{EFicsitMachineState::OutputBlocked, LOCTEXT("StateOutputBlocked", "Output full")},
				{EFicsitMachineState::Idle, LOCTEXT("StateIdle", "Idle")},
				{EFicsitMachineState::Paused, LOCTEXT("StatePaused", "Paused")},
				{EFicsitMachineState::Unpowered, LOCTEXT("StateUnpowered", "Unpowered")},
			};
			for (const auto& State : States)
			{
				const EFicsitMachineState Key = State.Key;
				// Stopped machines stand out: amber when they wait on something, red when they have no power. Idle and paused
				// stay dim, since those are usually on purpose.
				const FSlateColor Color = Key == EFicsitMachineState::Running ? FSlateColor(FicsitStatsStyle::TextPrimary())
					: Key == EFicsitMachineState::Unpowered ? FSlateColor(FicsitStatsStyle::Deficit())
					: (Key == EFicsitMachineState::Idle || Key == EFicsitMachineState::Paused) ? FSlateColor(FicsitStatsStyle::TextSecondary())
					: FSlateColor(FicsitStatsStyle::Warning());
				DetailsBox->AddSlot().AutoHeight().Padding(22.f, 1.f, 10.f, 1.f)
				[
					SNew(SBox)
					.Visibility_Lambda([Counts, Key]() { return Counts().Get(Key) > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeKeyValue(State.Value, TAttribute<FText>::CreateLambda([Counts, Key]() { return FText::AsNumber(Counts().Get(Key)); }), Color, Color)
					]
				];
			}
		}
	};
	AddFlowSection(LOCTEXT("DetailProduction", "Production"), true, State.bHasProduced);
	AddFlowSection(LOCTEXT("DetailConsumption", "Consumption"), false, State.bHasConsumed);

	// Hand-crafting, building and dismantling over the selected range (only when there was any).
	if (State.HasManual())
	{
		DetailsBox->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)
		[
			MakeSectionStrip(LOCTEXT("DetailManual", "Manual (selected range)"), FText::GetEmpty())
		];
		const FText Labels[FicsitManualKindCount] = {
			LOCTEXT("DetailCrafted", "Crafted"),
			LOCTEXT("DetailCraftUsed", "Used in crafting"),
			LOCTEXT("DetailBuilt", "Spent building"),
			LOCTEXT("DetailRefunded", "Refunded by dismantling"),
		};
		for (int32 Kind = 0; Kind < FicsitManualKindCount; ++Kind)
		{
			if (State.Manual.Values[Kind] <= 0.f)
			{
				continue;
			}
			DetailsBox->AddSlot().AutoHeight().Padding(10.f, 2.f)
			[
				MakeKeyValue(Labels[Kind], TAttribute<FText>::CreateLambda([Live, Kind]() { return FicsitStatsStyle::FormatNumber(Live->Manual.Values[Kind]); }))
			];
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Manual tab

TSharedRef<SWidget> SFicsitStatsWindow::BuildManualSortHeader(EManualSort Column, const FText& Label, float Width, bool bAlignRight)
{
	return BuildSortHeader(Label, Width, bAlignRight,
		[this, Column]() { return Model->GetManualSort() == Column; },
		[this]() { return Model->IsManualAscending(); },
		[this, Column]() { ClickManualSort(Column); });
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildManualView()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 3.f)
		[
			MakeSectionStrip(LOCTEXT("ManualTitle", "Built and Dismantled by hand"),
				TAttribute<FText>::CreateLambda([this]() { return Model->GetManual().Summary; }))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
			.Padding(FMargin(12.f, 6.f))
			// No column headers over an empty list.
			.Visibility_Lambda([this]() { return Model->GetManual().Rows.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					BuildManualSortHeader(EManualSort::Item, LOCTEXT("ManualItem", "Item"), 0.f, false)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildManualSortHeader(EManualSort::Crafted, LOCTEXT("ManualCrafted", "Crafted"), 150.f, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildManualSortHeader(EManualSort::CraftUsed, LOCTEXT("ManualCraftUsed", "Used in crafting"), 170.f, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildManualSortHeader(EManualSort::Built, LOCTEXT("ManualBuilt", "Spent building"), 170.f, true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[BuildManualSortHeader(EManualSort::Refunded, LOCTEXT("ManualRefunded", "Refunded"), 150.f, true)]
				// Space for the pin column in the rows below.
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(36.f)]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ManualList, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildManualRow(const TSharedRef<FManualRow>& Data)
{
	const TSubclassOf<UFGItemDescriptor> Item = Data->Item;
	auto Number = [Data](int32 Kind, float Width)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Data, Kind]() { return Data->Texts[Kind]; })
				.Font(FicsitStatsStyle::Font(FontSize::Body))
				.ColorAndOpacity_Lambda([Data, Kind]() { return FSlateColor(Data->Values[Kind] > 0.f ? FicsitStatsStyle::TextPrimary() : FicsitStatsStyle::TextSecondary()); })
			];
	};

	const TSharedRef<TWeakPtr<SWidget>> RowHover = MakeShared<TWeakPtr<SWidget>>();
	TSharedPtr<SFicsitClickBox> RowBox;
	TSharedRef<SWidget> Result = SNew(SBox)
		.Padding(0.f, 0.f, 4.f, 3.f)
		[
			SAssignNew(RowBox, SFicsitClickBox)
			.Color(FicsitStatsStyle::RowTile())
			.HoverColor(FicsitStatsStyle::RowTileHover())
			.Padding(FMargin(6.f, 0.f, 12.f, 0.f))
			.OnClicked_Lambda([this, Item]() { ShowDetails(Item); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 4.f)
				[
					SNew(SBox)
					.WidthOverride(38.f)
					.HeightOverride(38.f)
					[
						SNew(SImage)
						.Image(FicsitStatsStyle::ItemIcon(Item))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(10.f, 0.f, 8.f, 0.f)
				[
					BuildItemName(Item)
				]
				+ SHorizontalBox::Slot().AutoWidth()[Number(static_cast<int32>(EFicsitManualKind::Crafted), 150.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Number(static_cast<int32>(EFicsitManualKind::CraftUsed), 170.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Number(static_cast<int32>(EFicsitManualKind::Built), 170.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Number(static_cast<int32>(EFicsitManualKind::Refunded), 150.f)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)[MakePinButton(Item, RowHover)]
			]
		];
	*RowHover = RowBox;
	return Result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Power tab

TSharedRef<SWidget> SFicsitStatsWindow::BuildPowerTile(const FText& Label, int32 Index)
{
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::RowTile())
		.Padding(FMargin(12.f, 8.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeText(Label, FontSize::Small, FicsitStatsStyle::TextSecondary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([this, Index]() { return Model->GetPower().Tiles[Index].Value; })
				.Font(FicsitStatsStyle::Font(FontSize::Display, true))
				.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([this, Index]() { return Model->GetPower().Tiles[Index].Sub; })
				.Font(FicsitStatsStyle::Font(FontSize::Caption))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(Model->GetPower().Tiles[Index].SubColor); })
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildPowerList(const FText& Title, bool bProducers)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionStrip(Title, TAttribute<FText>::CreateLambda([this, bProducers]() { return bProducers ? Model->GetPower().ProducerSummary : Model->GetPower().ConsumerSummary; }))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(bProducers ? PowerProducerList : PowerConsumerList, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildPowerView()
{
	TSharedRef<SHorizontalBox> Tiles = SNew(SHorizontalBox);
	constexpr int32 NumPowerTiles = FicsitStatsTables::FPowerTable::NumTiles;
	const FText Labels[NumPowerTiles] = {
		LOCTEXT("PowerTileProduction", "Production"),
		LOCTEXT("PowerTileConsumption", "Consumption"),
		LOCTEXT("PowerTileCapacity", "Capacity"),
		LOCTEXT("PowerTileMax", "Max consumption"),
		LOCTEXT("PowerTileBatteries", "Batteries"),
	};
	for (int32 Index = 0; Index < NumPowerTiles; ++Index)
	{
		Tiles->AddSlot()
			.FillWidth(1.f)
			.Padding(Index == 0 ? 0.f : 3.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.Visibility_Lambda([this, Index]() { return Model->GetPower().Tiles[Index].bHidden ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					BuildPowerTile(Labels[Index], Index)
				]
			];
	}

	auto Legend = [](const FText& Text, const FLinearColor& Color)
	{
		return MakeText(FText::Format(INVTEXT("— {0}"), Text), FontSize::Small, Color);
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Tiles
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
			.Padding(FMargin(12.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[MakeText(LOCTEXT("PowerOverTime", "Power over time"), FontSize::Body)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)[Legend(LOCTEXT("PowerProduced", "Production"), FicsitStatsStyle::Produced())]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)[Legend(LOCTEXT("PowerConsumed", "Consumption"), FicsitStatsStyle::Consumed())]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)[Legend(LOCTEXT("PowerCapacity", "Capacity"), FicsitStatsStyle::SeriesColor(9))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)[Legend(LOCTEXT("PowerMaxConsumption", "Max consumption"), FicsitStatsStyle::SeriesColor(2))]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(220.f)
			[
				SAssignNew(PowerGraph, SFicsitGraph)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				BuildPowerList(LOCTEXT("PowerProducers", "Generators"), true)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				BuildPowerList(LOCTEXT("PowerConsumers", "Consumers"), false)
			]
		];
}

TSharedRef<SWidget> SFicsitStatsWindow::BuildPowerRow(const TSharedRef<FPowerRow>& Data, bool bProducer)
{
	const TSubclassOf<UFGItemDescriptor> Building = Data->Item;
	// Consumers aren't a problem, so their bars are blue, not red. Red stays for deficits and overloads.
	const FLinearColor BarColor = bProducer ? FicsitStatsStyle::Produced() : FicsitStatsStyle::Consumed();

	return SNew(SBox)
		.Padding(0.f, 0.f, 4.f, 3.f)
		[
			SNew(SFicsitClickBox)
			.Color(FicsitStatsStyle::RowTile())
			.HoverColor(FicsitStatsStyle::RowTileHover())
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(6.f, 4.f, 12.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(38.f)
						.HeightOverride(38.f)
						[
							Building
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(FicsitStatsStyle::ItemIcon(Building)))
								: SNullWidget::NullWidget
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(10.f, 0.f, 8.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text_Lambda([Data]() { return Data->Name; })
							.Font(FicsitStatsStyle::Font(FontSize::Body))
							.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							MakeText(TAttribute<FText>::CreateLambda([Data]() { return Data->CountText; }), FontSize::Caption, FicsitStatsStyle::TextSecondary())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							MakeText(TAttribute<FText>::CreateLambda([Data]() { return Data->MWText; }), FontSize::Body)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							MakeText(TAttribute<FText>::CreateLambda([Data]() { return Data->SecondaryText; }), FontSize::Caption, FicsitStatsStyle::TextSecondary())
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(4.f)
					[
						SNew(SFicsitShareBar)
						.Fraction_Lambda([Data]() { return Data->Share; })
						.Color(BarColor)
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
