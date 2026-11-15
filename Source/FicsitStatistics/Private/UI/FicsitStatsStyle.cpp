#include "UI/FicsitStatsStyle.h"

#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "FicsitStatisticsTypes.h"
#include "Resources/FGItemDescriptor.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	FLinearColor Hex(const TCHAR* InHex, float Alpha = 1.f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor::FromHex(InHex));
		Color.A = Alpha;
		return Color;
	}

	struct FFontCache
	{
		TStrongObjectPtr<UFontFace> RegularFace;
		TStrongObjectPtr<UFontFace> BoldFace;
		TSharedPtr<FStandaloneCompositeFont> Regular;
		TSharedPtr<FStandaloneCompositeFont> Bold;
		bool bTried = false;
	};

	FFontCache& Fonts()
	{
		static FFontCache Cache;
		return Cache;
	}

	TSharedPtr<FStandaloneCompositeFont> MakeComposite(UFontFace* Face)
	{
		if (!Face)
		{
			return nullptr;
		}
		TSharedPtr<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
		FTypefaceEntry& Entry = Composite->DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(Face);
		return Composite;
	}

	/**
	 * A cached brush and the object it draws from. The caches outlive every world, and the GC sees neither a static map
	 * nor a brush's resource object, so the entry holds a reference that keeps both loaded (a class keeps its icon).
	 */
	struct FCachedBrush
	{
		TStrongObjectPtr<UObject> Source;
		TSharedPtr<FSlateBrush> Brush;
	};

	TMap<UClass*, FCachedBrush>& IconCache()
	{
		static TMap<UClass*, FCachedBrush> Cache;
		return Cache;
	}

	TMap<FString, FCachedBrush>& TextureCache()
	{
		static TMap<FString, FCachedBrush> Cache;
		return Cache;
	}
}

namespace FicsitStatsStyle
{
	const FLinearColor& TitleStrip()     { static const FLinearColor C = Hex(TEXT("3F3F3F")); return C; }
	const FLinearColor& TabActive()      { static const FLinearColor C = Hex(TEXT("575757")); return C; }
	const FLinearColor& TabInactive()    { static const FLinearColor C = Hex(TEXT("787878")); return C; }
	const FLinearColor& CloseBlock()     { static const FLinearColor C = Hex(TEXT("575757")); return C; }
	const FLinearColor& WindowBody()     { static const FLinearColor C = Hex(TEXT("161514"), 0.93f); return C; }
	const FLinearColor& Sidebar()        { static const FLinearColor C = Hex(TEXT("121111"), 0.97f); return C; }
	const FLinearColor& SectionStrip()   { static const FLinearColor C = Hex(TEXT("3F3F3F")); return C; }
	const FLinearColor& RowTile()        { static const FLinearColor C = Hex(TEXT("393939")); return C; }
	const FLinearColor& RowTileHover()   { static const FLinearColor C = Hex(TEXT("474747")); return C; }
	const FLinearColor& SearchField()    { static const FLinearColor C = Hex(TEXT("666667")); return C; }
	const FLinearColor& Orange()         { static const FLinearColor C = Hex(TEXT("E59344")); return C; }
	const FLinearColor& GraphBackground(){ static const FLinearColor C = Hex(TEXT("1E1D1C")); return C; }
	const FLinearColor& GraphGrid()      { static const FLinearColor C = Hex(TEXT("FFFFFF"), 0.07f); return C; }

	const FLinearColor& TextPrimary()    { static const FLinearColor C = Hex(TEXT("E8E8E8")); return C; }
	const FLinearColor& TextSecondary()  { static const FLinearColor C = Hex(TEXT("9A9A9A")); return C; }
	const FLinearColor& TextOnOrange()   { static const FLinearColor C = Hex(TEXT("FFFFFF")); return C; }
	const FLinearColor& Deficit()        { static const FLinearColor C = Hex(TEXT("E5534B")); return C; }
	const FLinearColor& Surplus()        { static const FLinearColor C = Hex(TEXT("B8C7B0")); return C; }
	const FLinearColor& Warning()        { static const FLinearColor C = Hex(TEXT("E5C04B")); return C; }
	const FLinearColor& Produced()       { static const FLinearColor C = Hex(TEXT("5CC98A")); return C; }
	const FLinearColor& Consumed()       { static const FLinearColor C = Hex(TEXT("4E9BE6")); return C; }

	FLinearColor SeriesColor(int32 Index)
	{
		static const FLinearColor Palette[] = {
			Hex(TEXT("4E9BE6")), // blue
			Hex(TEXT("5CC98A")), // green
			Hex(TEXT("E5D04B")), // yellow
			Hex(TEXT("E56BB0")), // pink
			Hex(TEXT("4BD0E5")), // cyan
			Hex(TEXT("B57BE6")), // purple
			Hex(TEXT("E5534B")), // red
			Hex(TEXT("A3E54B")), // lime
			Hex(TEXT("8A9BE5")), // periwinkle
			Hex(TEXT("D9D9D9")), // light gray
		};
		return Palette[FMath::Abs(Index) % UE_ARRAY_COUNT(Palette)];
	}

	int32 NumSeriesColors() { return 10; }

	FSlateFontInfo Font(float Size, bool bBold)
	{
		FFontCache& Cache = Fonts();
		if (!Cache.bTried)
		{
			Cache.bTried = true;
			Cache.RegularFace.Reset(LoadObject<UFontFace>(nullptr, TEXT("/Game/FactoryGame/Interface/Font/NotoSans-Regular.NotoSans-Regular")));
			Cache.BoldFace.Reset(LoadObject<UFontFace>(nullptr, TEXT("/Game/FactoryGame/Interface/Font/NotoSans-SemiBold.NotoSans-SemiBold")));
			Cache.Regular = MakeComposite(Cache.RegularFace.Get());
			Cache.Bold = MakeComposite(Cache.BoldFace.Get());
		}

		const TSharedPtr<FStandaloneCompositeFont>& Composite = bBold ? Cache.Bold : Cache.Regular;
		if (Composite.IsValid())
		{
			return FSlateFontInfo(Composite, Size, TEXT("Regular"));
		}
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(Size));
	}

	const FSlateBrush* White()
	{
		return FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	const FSlateBrush* ItemIcon(TSubclassOf<UFGItemDescriptor> Item)
	{
		if (!Item)
		{
			return White();
		}
		FCachedBrush& Entry = IconCache().FindOrAdd(Item.Get());
		if (!Entry.Brush.IsValid())
		{
			Entry.Source.Reset(Item.Get());
			Entry.Brush = MakeShared<FSlateBrush>();
			if (UTexture2D* Texture = UFGItemDescriptor::GetSmallIcon(Item))
			{
				Entry.Brush->SetResourceObject(Texture);
				Entry.Brush->ImageSize = FVector2D(64.f, 64.f);
			}
		}
		return Entry.Brush.Get();
	}

	const FSlateBrush* TextureBrush(const TCHAR* ObjectPath)
	{
		FCachedBrush& Entry = TextureCache().FindOrAdd(ObjectPath);
		if (!Entry.Brush.IsValid())
		{
			Entry.Brush = MakeShared<FSlateBrush>();
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, ObjectPath))
			{
				Entry.Source.Reset(Texture);
				Entry.Brush->SetResourceObject(Texture);
				Entry.Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			}
		}
		return Entry.Brush.Get();
	}

	FText FormatNumber(float Value)
	{
		const float Abs = FMath::Abs(Value);
		FNumberFormattingOptions Options;
		Options.UseGrouping = true;
		if (Abs >= 1000000.f)
		{
			Options.MinimumFractionalDigits = 1;
			Options.MaximumFractionalDigits = 1;
			return FText::Format(INVTEXT("{0}M"), FText::AsNumber(Value / 1000000.f, &Options));
		}
		if (Abs >= 10000.f)
		{
			Options.MinimumFractionalDigits = 1;
			Options.MaximumFractionalDigits = 1;
			return FText::Format(INVTEXT("{0}k"), FText::AsNumber(Value / 1000.f, &Options));
		}
		Options.MinimumFractionalDigits = 0;
		Options.MaximumFractionalDigits = Abs >= 100.f ? 0 : (Abs >= 10.f ? 1 : 2);
		return FText::AsNumber(Value, &Options);
	}

	FText FormatRate(float Value, TSubclassOf<UFGItemDescriptor> Item)
	{
		return FText::Format(FicsitUnits::IsFluid(Item) ? INVTEXT("{0} m³/min") : INVTEXT("{0} /min"), FormatNumber(Value));
	}

	FText FormatDuration(double Minutes)
	{
		if (Minutes < 60.0)
		{
			return FText::Format(INVTEXT("{0}m"), FText::AsNumber(FMath::Max(1, FMath::RoundToInt(Minutes))));
		}
		const int32 TotalMinutes = FMath::RoundToInt(Minutes);
		if (TotalMinutes < 60 * 48)
		{
			return FText::Format(INVTEXT("{0}h {1}m"), FText::AsNumber(TotalMinutes / 60), FText::AsNumber(TotalMinutes % 60));
		}
		return FText::Format(INVTEXT("{0}d {1}h"), FText::AsNumber(TotalMinutes / (60 * 24)), FText::AsNumber((TotalMinutes / 60) % 24));
	}

	FText FormatPower(float MW)
	{
		FNumberFormattingOptions Options;
		Options.UseGrouping = true;
		const float Abs = FMath::Abs(MW);
		if (Abs >= 1000.f)
		{
			Options.MinimumFractionalDigits = 2;
			Options.MaximumFractionalDigits = 2;
			return FText::Format(INVTEXT("{0} GW"), FText::AsNumber(MW / 1000.f, &Options));
		}
		Options.MinimumFractionalDigits = 0;
		Options.MaximumFractionalDigits = Abs >= 100.f ? 0 : 1;
		return FText::Format(INVTEXT("{0} MW"), FText::AsNumber(MW, &Options));
	}

	FText FormatEnergy(float MWh)
	{
		FNumberFormattingOptions Options;
		Options.UseGrouping = true;
		if (FMath::Abs(MWh) >= 1000.f)
		{
			Options.MinimumFractionalDigits = 1;
			Options.MaximumFractionalDigits = 1;
			return FText::Format(INVTEXT("{0} GWh"), FText::AsNumber(MWh / 1000.f, &Options));
		}
		Options.MaximumFractionalDigits = 0;
		return FText::Format(INVTEXT("{0} MWh"), FText::AsNumber(MWh, &Options));
	}

	void SmoothForDisplay(TArray<float>& Values, int32 Radius)
	{
		const int32 Num = Values.Num();
		if (Num < 3 || Radius <= 0)
		{
			return;
		}
		// Prefix sums, then each point averages up to Radius neighbours on each side. Near the ends the window is cut off
		// on the missing side only, so the newest point averages itself and the Radius before it (a symmetric cut
		// would leave it raw, and the line would hook at "now").
		TArray<double> Prefix;
		Prefix.SetNumUninitialized(Num + 1);
		Prefix[0] = 0.0;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Prefix[Index + 1] = Prefix[Index] + Values[Index];
		}
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const int32 Lo = FMath::Max(0, Index - Radius);
			const int32 Hi = FMath::Min(Num - 1, Index + Radius);
			Values[Index] = static_cast<float>((Prefix[Hi + 1] - Prefix[Lo]) / (Hi - Lo + 1));
		}
	}

	FText FormatSignedRate(float Value, TSubclassOf<UFGItemDescriptor> Item)
	{
		const FText Number = FormatNumber(FMath::Abs(Value));
		const TCHAR* Sign = Value > 0.005f ? TEXT("+") : (Value < -0.005f ? TEXT("−") : TEXT(""));
		return FText::Format(FicsitUnits::IsFluid(Item) ? INVTEXT("{0}{1} m³/min") : INVTEXT("{0}{1} /min"), FText::FromString(Sign), Number);
	}

	FText FormatTableRate(float Value, TSubclassOf<UFGItemDescriptor> Item)
	{
		return FicsitUnits::IsFluid(Item) ? FText::Format(INVTEXT("{0} m³"), FormatNumber(Value)) : FormatNumber(Value);
	}

	FText FormatAmount(float Value, TSubclassOf<UFGItemDescriptor> Item)
	{
		return FicsitUnits::IsFluid(Item) ? FText::Format(INVTEXT("{0} m³"), FormatNumber(Value)) : FormatNumber(Value);
	}

	FText FormatSignedTableRate(float Value, TSubclassOf<UFGItemDescriptor> Item)
	{
		const FText Number = FormatNumber(FMath::Abs(Value));
		const TCHAR* Sign = Value > 0.005f ? TEXT("+") : (Value < -0.005f ? TEXT("−") : TEXT(""));
		return FText::Format(FicsitUnits::IsFluid(Item) ? INVTEXT("{0}{1} m³") : INVTEXT("{0}{1}"), FText::FromString(Sign), Number);
	}
}
