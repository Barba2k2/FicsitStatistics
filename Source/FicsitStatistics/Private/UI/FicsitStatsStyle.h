#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

class UFGItemDescriptor;

/**
 * Colors, fonts and formatting that match the base game's menus.
 * Colors were sampled from screenshots of the game's menus.
 * Fonts are the game's own Noto Sans font faces, resolved at runtime from /Game/FactoryGame/Interface/Font.
 */
namespace FicsitStatsStyle
{
	// Window chrome
	const FLinearColor& TitleStrip();      // #3F3F3F
	const FLinearColor& TabActive();       // #575757
	const FLinearColor& TabInactive();     // #787878
	const FLinearColor& CloseBlock();      // #575757
	const FLinearColor& WindowBody();      // translucent dark
	const FLinearColor& Sidebar();         // #171614
	const FLinearColor& SectionStrip();    // #3F3F3F
	const FLinearColor& RowTile();         // #393939
	const FLinearColor& RowTileHover();
	const FLinearColor& SearchField();     // #666667
	const FLinearColor& Orange();          // #E59344 (selection)
	const FLinearColor& GraphBackground();
	const FLinearColor& GraphGrid();

	// Text
	const FLinearColor& TextPrimary();
	const FLinearColor& TextSecondary();
	const FLinearColor& TextOnOrange();
	const FLinearColor& Deficit();
	const FLinearColor& Surplus();
	/** Amber for "needs a look" states (output full, waiting for inputs). Softer than Deficit red. */
	const FLinearColor& Warning();

	/** Produced and consumed lines and bars. Consumption is blue, not red, since using power or items is normal. */
	const FLinearColor& Produced();
	const FLinearColor& Consumed();

	/** Distinct graph colors that read well on dark gray. Orange is deliberately absent (it means "selected"). */
	FLinearColor SeriesColor(int32 Index);
	int32 NumSeriesColors();

	/**
	 * The only text sizes the window uses. Few sizes read calmer than many slightly different ones.
	 * Caption: sub-lines, axis labels, key badges. Small: labels, column headers, details text.
	 * Body: names, rates, tabs, buttons. Display: big numbers and the details title.
	 */
	namespace FontSize
	{
		constexpr float Caption = 11.f;
		constexpr float Small = 13.f;
		constexpr float Body = 16.f;
		constexpr float Display = 22.f;
	}

	FSlateFontInfo Font(float Size, bool bBold = false);

	/** A plain white brush to tint via BorderBackgroundColor / ColorAndOpacity. */
	const FSlateBrush* White();

	/** Item icon brush (cached per item). */
	const FSlateBrush* ItemIcon(TSubclassOf<UFGItemDescriptor> Item);
	/** Game texture brush by object path (cached), e.g. the search icon. */
	const FSlateBrush* TextureBrush(const TCHAR* ObjectPath);

	/** Game-style number: commas below 10,000 ("5,900"), short form above ("12.4k", "1.2M"). */
	FText FormatNumber(float Value);
	/** "5,900 /min" or "12.4 m³/min". */
	FText FormatRate(float Value, TSubclassOf<UFGItemDescriptor> Item);
	FText FormatSignedRate(float Value, TSubclassOf<UFGItemDescriptor> Item);
	/** A rate in a table whose header already says "per minute": "5,900", or "12.4 m³" for fluids. */
	FText FormatTableRate(float Value, TSubclassOf<UFGItemDescriptor> Item);
	FText FormatSignedTableRate(float Value, TSubclassOf<UFGItemDescriptor> Item);
	/** An amount of an item: "5,900", or "12.4 m³" for fluids. */
	FText FormatAmount(float Value, TSubclassOf<UFGItemDescriptor> Item);
	/** "45m", "3h 20m", "2d 4h". */
	FText FormatDuration(double Minutes);
	/** "850 MW", "12.5 MW", "1.24 GW". */
	FText FormatPower(float MW);
	/** Battery charge: "100 MWh", "1.2 GWh". */
	FText FormatEnergy(float MWh);

	/**
	 * Display-only smoothing for graphs drawn from exact 5 s history points, which are jagged (a recipe that finishes
	 * every 8 s lands in some points and not others). Moving average over up to Radius points either side, one-sided at
	 * the ends (the newest point averages the Radius before it). Averages and totals always use the exact points.
	 */
	void SmoothForDisplay(TArray<float>& Values, int32 Radius = 3);
}
