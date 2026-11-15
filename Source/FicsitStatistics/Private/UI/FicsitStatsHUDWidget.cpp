#include "UI/FicsitStatsHUDWidget.h"

#include "FicsitBalance.h"
#include "FicsitStatisticsConfig.h"
#include "FicsitStatisticsSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FGHUD.h"
#include "FicsitStatistics.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/FGGameUI.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Resources/FGItemDescriptor.h"
#include "UI/FicsitItemOutlook.h"
#include "UI/FicsitPlayerPrefs.h"
#include "UI/FicsitRangeReader.h"
#include "UI/FicsitStatsStyle.h"
#include "UI/SFicsitWidgets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FicsitStatistics"

namespace FontSize = FicsitStatsStyle::FontSize;

TWeakObjectPtr<UFicsitStatsHUDWidget> UFicsitStatsHUDWidget::Instance;

namespace
{
	/** How long a new surplus / deficit / balanced state must hold before the HUD switches to it (no flicker). */
	constexpr double StateHoldSeconds = 60.0;
	/** The sparkline covers this much history (the finest history tier holds 10 minutes). */
	constexpr float SparklineSeconds = 600.f;
	/** "Right now" for the trend arrow. */
	constexpr float TrendSeconds = 60.f;

	/** The game's vertical box that holds the objective and milestone panels. The panel goes in under them. */
	const FName GameProgressionBoxName = TEXT("mGameProgressionVBox");
	/** The milestone panel in that box: the panel copies its spacing. */
	const FName MilestonePanelName = TEXT("mMilestoneInfo");
	/** The milestone panel's background image: the panel copies its brush and tint. */
	const FName MilestoneBackgroundName = TEXT("Background");
	/** How long to keep looking for the game's HUD (twice a second) before showing the panel on its own. */
	constexpr int32 MaxAttachAttempts = 20;

	/**
	 * Fallback, when the game's HUD box isn't found: the panel sits on its own at the same spot. The game draws its
	 * panels through a curving effect; a shear by the slope of their bottom corners (measured at 2560x1440) comes close.
	 */
	constexpr float PanelSlope = -0.052f;
	constexpr float PanelWidth = 376.5f;
	constexpr float PanelRightMargin = 58.f;
	/** The game's "Objective:" / "Milestone:" header size (measured), a step below FontSize::Small. */
	constexpr float HeaderFontSize = 12.f;

	enum class EPinState : uint8 { Unknown, Balanced, Surplus, Deficit };

	/** The HUD has no idle state: an item nothing makes or uses shows as balanced (± 0). */
	EPinState ToPinState(FicsitBalance::EState State)
	{
		switch (State)
		{
		case FicsitBalance::EState::Deficit: return EPinState::Deficit;
		case FicsitBalance::EState::Surplus: return EPinState::Surplus;
		default:                             return EPinState::Balanced;
		}
	}

	FLinearColor ColorFor(EPinState State)
	{
		switch (State)
		{
		case EPinState::Surplus: return FicsitStatsStyle::Surplus();
		case EPinState::Deficit: return FicsitStatsStyle::Deficit();
		default:                 return FicsitStatsStyle::TextSecondary();
		}
	}

	/** Tiny line of the net rate over the last 10 minutes, centred on zero. */
	class SFicsitSparkline : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFicsitSparkline) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		void SetValues(const TArray<float>& InValues, const FLinearColor& InColor)
		{
			Values = InValues;
			Color = InColor;
			Invalidate(EInvalidateWidgetReason::Paint);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(56.f, 16.f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FVector2D S = Geometry.GetLocalSize();
			const TArray<FVector2D> Zero = {FVector2D(0.f, S.Y * 0.5f), FVector2D(S.X, S.Y * 0.5f)};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Zero, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.15f), true, 1.f);
			if (Values.Num() < 2)
			{
				return LayerId;
			}
			float MaxAbs = KINDA_SMALL_NUMBER;
			for (const float Value : Values)
			{
				MaxAbs = FMath::Max(MaxAbs, FMath::Abs(Value));
			}
			TArray<FVector2D> Points;
			Points.Reserve(Values.Num());
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				const float X = S.X * Index / (Values.Num() - 1);
				const float Y = S.Y * 0.5f - (Values[Index] / MaxAbs) * (S.Y * 0.5f - 1.f);
				Points.Add(FVector2D(X, Y));
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 1.2f);
			return LayerId + 1;
		}

	private:
		TArray<float> Values;
		FLinearColor Color = FLinearColor::White;
	};

	/**
	 * The orange rule under the header. Drawn as an anti-aliased line, not a filled box: under the panel's shear a box's
	 * edges aren't anti-aliased and come out as visible stair-steps.
	 */
	class SFicsitRule : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFicsitRule) {}
			SLATE_ARGUMENT(FLinearColor, Color)
			SLATE_ARGUMENT(float, Thickness)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Color = InArgs._Color;
			Thickness = InArgs._Thickness;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, Thickness); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FVector2D S = Geometry.GetLocalSize();
			const TArray<FVector2D> Points = {FVector2D(0.f, S.Y * 0.5f), FVector2D(S.X, S.Y * 0.5f)};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
			return LayerId;
		}

	private:
		FLinearColor Color = FLinearColor::White;
		float Thickness = 2.f;
	};

	/**
	 * A filled rectangle with anti-aliased edges, for the sheared panel. A plain box brush has hard edges, which turn
	 * into stair-steps once sheared. This draws the fill plus a one-pixel rim whose alpha fades to zero across the edge.
	 */
	class SFicsitPanelBackground : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFicsitPanelBackground) {}
			SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { Color = InArgs._Color; }

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& WidgetStyle, bool) const override
		{
			const FSlateBrush* Brush = FicsitStatsStyle::White();
			const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
			const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
			const FVector2f Size = FVector2f(Geometry.GetLocalSize());
			// Half a physical pixel in local units: the rim runs from half a pixel inside the edge to half a pixel outside.
			const float Half = 0.5f / FMath::Max(Geometry.Scale, KINDA_SMALL_NUMBER);

			const FLinearColor Tint = Color * WidgetStyle.GetColorAndOpacityTint();
			const FColor Solid = Tint.ToFColor(true);
			const FColor Clear(Solid.R, Solid.G, Solid.B, 0);
			const FVector2f UV(0.5f, 0.5f);
			auto Corners = [&](float Offset, const FColor& InColor, TArray<FSlateVertex>& Out)
			{
				Out.Add(FSlateVertex::Make(Transform, FVector2f(-Offset, -Offset), UV, InColor));
				Out.Add(FSlateVertex::Make(Transform, FVector2f(Size.X + Offset, -Offset), UV, InColor));
				Out.Add(FSlateVertex::Make(Transform, FVector2f(Size.X + Offset, Size.Y + Offset), UV, InColor));
				Out.Add(FSlateVertex::Make(Transform, FVector2f(-Offset, Size.Y + Offset), UV, InColor));
			};
			TArray<FSlateVertex> Verts;
			Verts.Reserve(8);
			Corners(-Half, Solid, Verts); // 0-3: inner (TL, TR, BR, BL)
			Corners(Half, Clear, Verts);  // 4-7: outer
			TArray<SlateIndex> Indices = {
				0, 1, 2, 0, 2, 3,          // fill
				4, 5, 1, 4, 1, 0,          // top rim
				5, 6, 2, 5, 2, 1,          // right rim
				6, 7, 3, 6, 3, 2,          // bottom rim
				7, 4, 0, 7, 0, 3,          // left rim
			};
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Handle, Verts, Indices, nullptr, 0, 0);
			return LayerId;
		}

	private:
		FLinearColor Color = FLinearColor::Black;
	};

	/** Finds a widget by name anywhere under Root, including inside nested user widgets. */
	UWidget* FindHudWidget(UWidget* Root, FName Name)
	{
		if (!Root)
		{
			return nullptr;
		}
		if (Root->GetFName() == Name)
		{
			return Root;
		}
		if (const UUserWidget* User = Cast<UUserWidget>(Root))
		{
			return User->WidgetTree ? FindHudWidget(User->WidgetTree->RootWidget, Name) : nullptr;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Root))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Found = FindHudWidget(Panel->GetChildAt(Index), Name))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	UVerticalBox* FindGameProgressionBox(APlayerController* PlayerController)
	{
		const AFGHUD* Hud = Cast<AFGHUD>(PlayerController->GetHUD());
		return Cast<UVerticalBox>(FindHudWidget(Hud ? Hud->GetGameUI() : nullptr, GameProgressionBoxName));
	}
}

/** The panel itself: a compact list of pinned items with their net rate, styled like the game's objective panel. */
class SFicsitPinnedHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitPinnedHUD) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		/** True when placed in the game's own HUD, which already curves and fades it. */
		SLATE_ARGUMENT(bool, InGameHud)
		/** The game's own panel background (from its milestone panel), or null to draw a plain one. */
		SLATE_ARGUMENT(const FSlateBrush*, GameBackground)
		SLATE_ARGUMENT(FLinearColor, GameBackgroundColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		WorldContext = InArgs._WorldContext;
		// Before anything reads the pins, so a newly loaded world never shows the last world's.
		FFicsitPins::SetWorld(WorldContext.Get());
		const bool bInGameHud = InArgs._InGameHud;
		// The same brush and tint as the game's panels, so the shade matches exactly. Otherwise plain translucent black.
		const TSharedRef<SWidget> Background = InArgs._GameBackground
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(InArgs._GameBackground).ColorAndOpacity(InArgs._GameBackgroundColor))
			: StaticCastSharedRef<SWidget>(SNew(SFicsitPanelBackground).Color(FLinearColor(0.f, 0.f, 0.f, 0.6f)));
		ChildSlot
		.HAlign(bInGameHud ? HAlign_Fill : HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(bInGameHud ? FMargin(0.f) : FMargin(0.f, 284.f, PanelRightMargin, 0.f)) // just below the game's panels
		[
			SNew(SBox)
			.WidthOverride(bInGameHud ? FOptionalSize() : FOptionalSize(PanelWidth))
			.RenderTransform(bInGameHud ? TOptional<FSlateRenderTransform>() : FSlateRenderTransform(FShear2D(0.f, PanelSlope)))
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.Visibility_Lambda([this]()
			{
				// Stays up while the statistics window is open, like the game's own HUD panels.
				const bool bShow = FFicsitPins::Get().Num() > 0 && bShowHud;
				return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					Background
				]
				+ SOverlay::Slot()
				.Padding(FMargin(8.f, 6.f, 8.f, 8.f))
				[
					SNew(SVerticalBox)
					// Header in the game's "Objective: Complete Phase 3" pattern: orange label, white value, centred.
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(6.f, 0.f, 6.f, 3.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HudTitle", "Production:"))
							.Font(FicsitStatsStyle::Font(HeaderFontSize, true))
							.ColorAndOpacity(FicsitStatsStyle::Orange())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						.Padding(5.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return HeaderNote; })
							.Font(FicsitStatsStyle::Font(HeaderFontSize, true))
							.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SFicsitRule)
						.Color(FicsitStatsStyle::Orange())
						.Thickness(2.f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6.f, 4.f, 6.f, 0.f)
					[
						SAssignNew(Rows, SVerticalBox)
					]
				]
			]
		];
		Rebuild();
		UpdateValues();
		RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SFicsitPinnedHUD::OnTimer));
	}

private:
	/** Values for one pinned item, refreshed twice a second. The row's widgets just read these (no per-frame work). */
	struct FPinRow
	{
		TSubclassOf<UFGItemDescriptor> Item;
		FText NetText;
		FText DetailText;
		/** Optional third line: storage running out, or time left for a Space Elevator or HUB milestone part. */
		FText AlertText;
		FLinearColor AlertColor = FLinearColor::White;
		FLinearColor NetColor = FLinearColor::White;
		int32 Trend = 0;
		FLinearColor TrendColor = FLinearColor::White;
		TSharedPtr<SFicsitSparkline> Sparkline;

		// Hysteresis: the shown state only changes after the new one has held for StateHoldSeconds.
		EPinState State = EPinState::Unknown;
		EPinState PendingState = EPinState::Unknown;
		double PendingSince = 0.0;
	};

	EActiveTimerReturnType OnTimer(double, float)
	{
		// Picks up a client's session name once it replicates, which swaps in that world's pins.
		FFicsitPins::SetWorld(WorldContext.Get());
		// Only rebuild the rows when the set of pinned items changes (no flashing). Otherwise, just refresh the values.
		if (FFicsitPins::GetRevision() != BuiltRevision)
		{
			Rebuild();
		}
		UpdateValues();
		return EActiveTimerReturnType::Continue;
	}

	void UpdateValues()
	{
		const UObject* Context = WorldContext.Get();
		bShowHud = FFicsitStatsSettings::GetShowPinnedHud(Context);
		if (!bShowHud || PinRows.Num() == 0)
		{
			return;
		}

		AFicsitStatisticsSubsystem* Subsystem = AFicsitStatisticsSubsystem::Get(Context);
		// What the Space Elevator phase and the HUB milestone still need, and the Sink and storage warning settings.
		const FFicsitOutlookContext OutlookContext = FFicsitOutlookContext::Read(Context);
		const bool bSinkIsDemand = OutlookContext.bSinkIsDemand;
		const int32 AverageMinutes = FFicsitStatsSettings::GetHudAverageMinutes(Context);
		const float AverageSeconds = AverageMinutes * 60.f;
		HeaderNote = FText::Format(LOCTEXT("HudHeaderNote", "net, {0} min avg"), FText::AsNumber(AverageMinutes));
		const double Now = FPlatformTime::Seconds();

		// Averages over the HUD's window and the last minute (for the trend), and the sparkline. Each falls back to the
		// latest scan until the history has points.
		TOptional<FFicsitRangeReader> Average, Trend, SparklineRange;
		TOptional<FFicsitItemOutlooks> AverageOutlooks, TrendOutlooks;
		if (Subsystem)
		{
			Average.Emplace(FFicsitRangeReader::Recent(*Subsystem, AverageSeconds));
			Trend.Emplace(FFicsitRangeReader::Recent(*Subsystem, TrendSeconds));
			SparklineRange.Emplace(FFicsitRangeReader::Recent(*Subsystem, SparklineSeconds));
			AverageOutlooks.Emplace(*Average, Subsystem->GetLatestSnapshot(), OutlookContext);
			TrendOutlooks.Emplace(*Trend, Subsystem->GetLatestSnapshot(), OutlookContext);
		}

		for (const TSharedRef<FPinRow>& Row : PinRows)
		{
			const FFicsitItemOutlook Outlook = AverageOutlooks ? AverageOutlooks->Get(Row->Item) : FFicsitItemOutlook();
			const FFicsitItemOutlook Recent = TrendOutlooks ? TrendOutlooks->Get(Row->Item) : FFicsitItemOutlook();
			const float Produced = Outlook.Produced, Demand = Outlook.Demand, Sunk = Outlook.Sunk;
			const float Net = Outlook.Net();
			const float RecentNet = Recent.Net();
			const float Threshold = FicsitBalance::NoiseThreshold(Produced, Demand);

			// State with hysteresis.
			const EPinState Candidate = ToPinState(Outlook.State);
			if (Row->State == EPinState::Unknown)
			{
				Row->State = Candidate;
				Row->PendingState = Candidate;
			}
			else if (Candidate == Row->State)
			{
				Row->PendingState = Candidate;
			}
			else
			{
				if (Candidate != Row->PendingState)
				{
					Row->PendingState = Candidate;
					Row->PendingSince = Now;
				}
				else if (Now - Row->PendingSince >= StateHoldSeconds)
				{
					Row->State = Candidate;
				}
			}

			Row->NetColor = ColorFor(Row->State);
			Row->NetText = Row->State == EPinState::Balanced ? INVTEXT("± 0") : FicsitStatsStyle::FormatSignedRate(Net, Row->Item);

			// Trend: last minute compared to the average.
			const float Delta = RecentNet - Net;
			Row->Trend = FMath::Abs(Delta) <= Threshold ? 0 : (Delta > 0.f ? 1 : -1);
			Row->TrendColor = Row->Trend > 0 ? FicsitStatsStyle::Surplus() : (Row->Trend < 0 ? FicsitStatsStyle::Deficit() : FicsitStatsStyle::TextSecondary());

			Row->DetailText = Sunk > 0.005f && !bSinkIsDemand
				? FText::Format(LOCTEXT("HudDetailSink", "{0} made · {1} used · {2} sunk"),
					FicsitStatsStyle::FormatNumber(Produced), FicsitStatsStyle::FormatNumber(Demand), FicsitStatsStyle::FormatNumber(Sunk))
				: FText::Format(LOCTEXT("HudDetail", "{0} made · {1} used"), FicsitStatsStyle::FormatNumber(Produced), FicsitStatsStyle::FormatNumber(Demand));

			// Time estimates: when storage runs out within the warning time, or else how long until each milestone that
			// still needs this part has it (one line each, like the window's notes).
			Row->AlertText = FText::GetEmpty();
			if (Outlook.bStorageRunsOutSoon)
			{
				Row->AlertText = FText::Format(LOCTEXT("HudStorageRunout", "Storage empty in {0} ({1} left)"),
					FicsitStatsStyle::FormatDuration(Outlook.StorageRunoutMinutes), FicsitStatsStyle::FormatAmount(Outlook.Stored, Row->Item));
				Row->AlertColor = FicsitStatsStyle::Deficit();
			}
			else
			{
				TArray<FText> Lines;
				auto AddNeed = [&](int32 Needed, float Minutes, const FText& Milestone)
				{
					if (Needed <= 0)
					{
						return;
					}
					Lines.Add(Minutes > 0.f
						? FText::Format(LOCTEXT("HudMilestoneEta", "{0} {1} left · ~{2}"), Milestone, FicsitStatsStyle::FormatNumber(Needed), FicsitStatsStyle::FormatDuration(Minutes))
						: FText::Format(LOCTEXT("HudMilestoneNotMade", "{0} {1} left · not made"), Milestone, FicsitStatsStyle::FormatNumber(Needed)));
				};
				AddNeed(Outlook.ElevatorRemaining, Outlook.ElevatorEtaMinutes, LOCTEXT("HudElevator", "Elevator"));
				AddNeed(Outlook.HubRemaining, Outlook.HubEtaMinutes, LOCTEXT("HudHub", "HUB"));
				if (Lines.Num() > 0)
				{
					Row->AlertText = FText::Join(INVTEXT("\n"), Lines);
					Row->AlertColor = FicsitStatsStyle::Orange();
				}
			}

			if (Row->Sparkline.IsValid() && SparklineRange)
			{
				TArray<float> Series;
				SparklineRange->GetNetSeries(Row->Item, bSinkIsDemand, Series);
				Row->Sparkline->SetValues(Series, Row->NetColor);
			}
		}
	}

	void Rebuild()
	{
		BuiltRevision = FFicsitPins::GetRevision();
		Rows->ClearChildren();
		PinRows.Reset();
		for (const TSubclassOf<UFGItemDescriptor>& Item : FFicsitPins::Get())
		{
			const TSharedRef<FPinRow> Row = MakeShared<FPinRow>();
			Row->Item = Item;
			PinRows.Add(Row);

			Rows->AddSlot()
			.AutoHeight()
			.Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(26.f)
					.HeightOverride(26.f)
					[
						SNew(SImage)
						.Image(FicsitStatsStyle::ItemIcon(Item))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SVerticalBox)
					// Top line: name, sparkline, averaged net, trend arrow.
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(STextBlock)
							.Text(UFGItemDescriptor::GetItemName(Item))
							.Font(FicsitStatsStyle::Font(FontSize::Small))
							.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 6.f, 0.f)
						[
							SAssignNew(Row->Sparkline, SFicsitSparkline)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.MinDesiredWidth(66.f)
							.HAlign(HAlign_Right)
							[
								SNew(STextBlock)
								.Text_Lambda([Row]() { return Row->NetText; })
								.Font(FicsitStatsStyle::Font(FontSize::Small, true))
								.ColorAndOpacity_Lambda([Row]() { return FSlateColor(Row->NetColor); })
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(5.f, 0.f, 0.f, 0.f)
						[
							SNew(SBox)
							.WidthOverride(9.f)
							.HeightOverride(9.f)
							[
								SNew(SFicsitTrendGlyph)
								.Direction_Lambda([Row]() { return Row->Trend; })
								.Color_Lambda([Row]() { return Row->TrendColor; })
							]
						]
					]
					// Second line, full width: made / used / sunk.
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([Row]() { return Row->DetailText; })
						.Font(FicsitStatsStyle::Font(FontSize::Caption))
						.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					// Third line, only when there's something to warn about.
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([Row]() { return Row->AlertText; })
						.Visibility_Lambda([Row]() { return Row->AlertText.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.Font(FicsitStatsStyle::Font(FontSize::Caption))
						.ColorAndOpacity_Lambda([Row]() { return FSlateColor(Row->AlertColor); })
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
			];
		}
	}

	TWeakObjectPtr<UObject> WorldContext;
	TSharedPtr<SVerticalBox> Rows;
	int32 BuiltRevision = -1;
	TArray<TSharedRef<FPinRow>> PinRows;
	FText HeaderNote;
	bool bShowHud = true;
};

void UFicsitStatsHUDWidget::EnsureFor(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	if (UFicsitStatsHUDWidget* Existing = Instance.Get())
	{
		if (Existing->GetOwningPlayer() == PlayerController && (Existing->IsInViewport() || Existing->GetParent()))
		{
			return;
		}
		Existing->RemoveFromParent();
		Instance.Reset();
	}
	Attach(PlayerController, 0);
}

void UFicsitStatsHUDWidget::Attach(TWeakObjectPtr<APlayerController> WeakController, int32 Attempt)
{
	APlayerController* PlayerController = WeakController.Get();
	if (!PlayerController || Instance.IsValid())
	{
		return;
	}

	// Under the game's objective / milestone panels, inside their HUD: the game curves and fades the panel with its own.
	// Its HUD may not be built yet when the player's input is, so look again for a few seconds.
	UVerticalBox* Box = FindGameProgressionBox(PlayerController);
	if (!Box && Attempt < MaxAttachAttempts)
	{
		FTimerHandle Handle;
		PlayerController->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakController, Attempt]()
		{
			Attach(WeakController, Attempt + 1);
		}), 0.5f, false);
		return;
	}

	UFicsitStatsHUDWidget* Widget = CreateWidget<UFicsitStatsHUDWidget>(PlayerController, UFicsitStatsHUDWidget::StaticClass());
	if (!Widget)
	{
		return;
	}
	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Box)
	{
		// Look like the milestone panel: its background brush and tint (set before the widget is built, below).
		UWidget* Milestone = FindHudWidget(Box, MilestonePanelName);
		if (const UImage* Background = Cast<UImage>(FindHudWidget(Milestone, MilestoneBackgroundName)))
		{
			Widget->GameBackground = Background->GetBrush();
			Widget->GameBackgroundColor = Background->GetColorAndOpacity();
			Widget->bHasGameBackground = true;
		}
		Widget->bInGameHud = true;
		UVerticalBoxSlot* NewSlot = Box->AddChildToVerticalBox(Widget);

		// The same gap above this panel as the game leaves between its objective and milestone panels.
		const int32 MilestoneIndex = Milestone ? Box->GetChildIndex(Milestone) : INDEX_NONE;
		const UVerticalBoxSlot* MilestoneSlot = Milestone ? Cast<UVerticalBoxSlot>(Milestone->Slot) : nullptr;
		const UWidget* Above = MilestoneIndex > 0 ? Box->GetChildAt(MilestoneIndex - 1) : nullptr;
		const UVerticalBoxSlot* AboveSlot = Above ? Cast<UVerticalBoxSlot>(Above->Slot) : nullptr;
		if (MilestoneSlot && AboveSlot)
		{
			FMargin Padding = MilestoneSlot->GetPadding();
			const float Gap = AboveSlot->GetPadding().Bottom + MilestoneSlot->GetPadding().Top;
			Padding.Top = FMath::Max(0.f, Gap - MilestoneSlot->GetPadding().Bottom);
			NewSlot->SetPadding(Padding);
		}
		UE_LOG(LogFicsitStatistics, Log, TEXT("Pinned-items HUD placed under the milestone panel: background %s (tint %s), top gap %.1f"),
			Widget->bHasGameBackground ? *Widget->GameBackground.GetResourceName().ToString() : TEXT("not found"),
			*Widget->GameBackgroundColor.ToString(), NewSlot->GetPadding().Top);
	}
	else
	{
		UE_LOG(LogFicsitStatistics, Warning, TEXT("The game's objective panel wasn't found; the pinned-items HUD is shown on its own"));
		Widget->AddToViewport(-10);
	}
	Instance = Widget;
}

TSharedRef<SWidget> UFicsitStatsHUDWidget::RebuildWidget()
{
	SAssignNew(Panel, SFicsitPinnedHUD)
		.WorldContext(this)
		.InGameHud(bInGameHud)
		.GameBackground(bHasGameBackground ? &GameBackground : nullptr)
		.GameBackgroundColor(GameBackgroundColor);
	return Panel.ToSharedRef();
}

void UFicsitStatsHUDWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Panel.Reset();
}

#undef LOCTEXT_NAMESPACE
