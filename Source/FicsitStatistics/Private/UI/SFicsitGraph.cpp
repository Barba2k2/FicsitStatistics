#include "UI/SFicsitGraph.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Resources/FGItemDescriptor.h"
#include "Rendering/DrawElements.h"
#include "UI/FicsitStatsStyle.h"

namespace FontSize = FicsitStatsStyle::FontSize;

namespace
{
	constexpr float LeftMargin = 52.f;   // room for Y labels
	constexpr float BottomMargin = 20.f; // room for X labels
	constexpr float TopMargin = 8.f;
	constexpr float RightMargin = 10.f;
	constexpr int32 GridLines = 4;
}

void SFicsitGraph::Construct(const FArguments& InArgs)
{
}

void SFicsitGraph::SetData(TArray<FSeries>&& InSeries, float InSpanSeconds)
{
	Series = MoveTemp(InSeries);
	SpanSeconds = FMath::Max(1.f, InSpanSeconds);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SFicsitGraph::SetHighlightedItem(TSubclassOf<UFGItemDescriptor> Item)
{
	if (HighlightedItem != Item)
	{
		HighlightedItem = Item;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

float SFicsitGraph::NiceCeiling(float Value)
{
	if (Value <= 0.f)
	{
		return 1.f;
	}
	const float Exponent = FMath::Floor(FMath::LogX(10.f, Value));
	const float Base = FMath::Pow(10.f, Exponent);
	const float Fraction = Value / Base;
	// Steps that divide into 4 grid lines cleanly. With only 1 / 2 / 5 / 10, a 5.3 GW peak got a 10 GW axis and left
	// half the graph empty.
	const float Nice = Fraction <= 1.f ? 1.f : Fraction <= 2.f ? 2.f : Fraction <= 3.f ? 3.f : Fraction <= 4.f ? 4.f
		: Fraction <= 5.f ? 5.f : Fraction <= 6.f ? 6.f : Fraction <= 8.f ? 8.f : 10.f;
	return Nice * Base;
}

FText SFicsitGraph::FormatAxisValue(float Value, float AxisMax, bool bPower)
{
	// One format for every label on an axis, picked from the top value: "2,000 / 1,500" or "10k / 7.5k", never a mix.
	FNumberFormattingOptions Options;
	Options.UseGrouping = true;
	Options.MinimumFractionalDigits = 0;
	Options.MaximumFractionalDigits = 2;
	if (bPower)
	{
		// Power values are MW. Use GW for the whole axis once the top reaches 1 GW.
		return AxisMax >= 1000.f
			? FText::Format(INVTEXT("{0} GW"), FText::AsNumber(Value / 1000.f, &Options))
			: FText::Format(INVTEXT("{0} MW"), FText::AsNumber(Value, &Options));
	}
	if (AxisMax >= 10000.f)
	{
		return FText::Format(INVTEXT("{0}k"), FText::AsNumber(Value / 1000.f, &Options));
	}
	return FText::AsNumber(Value, &Options);
}

FText SFicsitGraph::FormatTimeAgo(float SecondsAgo)
{
	const float S = FMath::Abs(SecondsAgo);
	FNumberFormattingOptions Options;
	Options.MaximumFractionalDigits = 1;
	// The 5s range has ticks at 1.25 s steps, so short times keep their fractions.
	if (S < 10.f)
	{
		if (S < 0.05f)
		{
			return INVTEXT("now");
		}
		Options.MaximumFractionalDigits = 2;
		return FText::Format(INVTEXT("{0}s"), FText::AsNumber(S, &Options));
	}
	if (S < 60.f)
	{
		return FText::Format(INVTEXT("{0}s"), FText::AsNumber(FMath::RoundToInt(S)));
	}
	if (S < 3600.f)
	{
		return FText::Format(INVTEXT("{0}m"), FText::AsNumber(S / 60.f, &Options));
	}
	Options.MaximumFractionalDigits = S < 36000.f ? 1 : 0;
	return FText::Format(INVTEXT("{0}h"), FText::AsNumber(S / 3600.f, &Options));
}

FReply SFicsitGraph::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	HoverLocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Unhandled();
}

void SFicsitGraph::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	HoverLocalX.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SFicsitGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* White = FicsitStatsStyle::White();
	const FSlateFontInfo LabelFont = FicsitStatsStyle::Font(FontSize::Caption);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Everything is tinted by the parent's opacity, so the graph fades with the window when it opens.
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();

	// Background
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), White, ESlateDrawEffect::None,
		FicsitStatsStyle::GraphBackground() * Tint);

	const float PlotLeft = LeftMargin;
	const float PlotRight = FMath::Max(PlotLeft + 10.f, Size.X - RightMargin);
	const float PlotTop = TopMargin;
	const float PlotBottom = FMath::Max(PlotTop + 10.f, Size.Y - BottomMargin);
	const float PlotWidth = PlotRight - PlotLeft;
	const float PlotHeight = PlotBottom - PlotTop;

	// Scale: each graph scales to its own highest visible value.
	float MaxValue = 0.f;
	for (const FSeries& Each : Series)
	{
		for (int32 Index = 0; Index < Each.Values.Num(); ++Index)
		{
			if (Each.Times[Index] >= -SpanSeconds)
			{
				MaxValue = FMath::Max(MaxValue, Each.Values[Index]);
			}
		}
	}
	const float YMax = NiceCeiling(MaxValue * 1.05f);

	auto ToLocal = [&](float Time, float Value)
	{
		const float X = PlotLeft + PlotWidth * (1.f + Time / SpanSeconds);
		const float Y = PlotBottom - PlotHeight * FMath::Clamp(Value / YMax, 0.f, 1.f);
		return FVector2D(X, Y);
	};

	// Grid and Y labels
	++LayerId;
	const bool bPowerAxis = Series.Num() > 0 && Series[0].bPower;
	for (int32 Line = 0; Line <= GridLines; ++Line)
	{
		const float Value = YMax * Line / GridLines;
		const float Y = PlotBottom - PlotHeight * Line / GridLines;
		TArray<FVector2D> Points = {FVector2D(PlotLeft, Y), FVector2D(PlotRight, Y)};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			FicsitStatsStyle::GraphGrid() * Tint, true, 1.f);

		const FText Label = FormatAxisValue(Value, YMax, bPowerAxis);
		const FVector2D LabelSize = FontMeasure->Measure(Label, LabelFont);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(FVector2D(PlotLeft - LabelSize.X - 6.f, Y - LabelSize.Y * 0.5f))),
			Label, LabelFont, ESlateDrawEffect::None, FicsitStatsStyle::TextSecondary() * Tint);
	}

	// X labels: 5 ticks from -span to now
	for (int32 Tick = 0; Tick <= 4; ++Tick)
	{
		const float Time = -SpanSeconds + SpanSeconds * Tick / 4.f;
		const FText Label = FormatTimeAgo(Time);
		const FVector2D LabelSize = FontMeasure->Measure(Label, LabelFont);
		// The oldest label starts at the plot's left edge instead of centering on it, so it clears the Y axis "0".
		const float X = Tick == 0 ? PlotLeft : FMath::Min(ToLocal(Time, 0.f).X - LabelSize.X * 0.5f, Size.X - LabelSize.X - 2.f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(FVector2D(X, PlotBottom + 3.f))),
			Label, LabelFont, ESlateDrawEffect::None, FicsitStatsStyle::TextSecondary() * Tint);
	}

	// Series. The highlighted one is drawn last (on top) and thicker, and the others dim while something is highlighted.
	++LayerId;
	const bool bAnyHighlight = HighlightedItem != nullptr && Series.ContainsByPredicate([this](const FSeries& S) { return S.Item == HighlightedItem; });
	auto DrawSeries = [&](const FSeries& Each, bool bHighlighted)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Each.Values.Num());
		for (int32 Index = 0; Index < Each.Values.Num(); ++Index)
		{
			if (Each.Times[Index] >= -SpanSeconds)
			{
				Points.Add(ToLocal(Each.Times[Index], Each.Values[Index]));
			}
		}
		if (Points.Num() == 1)
		{
			Points.Insert(FVector2D(Points[0].X - 2.f, Points[0].Y), 0);
		}
		if (Points.Num() < 2)
		{
			return;
		}
		FLinearColor Color = Each.Color;
		if (bAnyHighlight && !bHighlighted)
		{
			Color.A = 0.25f;
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			Color * Tint, true, bHighlighted ? 2.5f : 1.5f);
	};
	for (const FSeries& Each : Series)
	{
		if (!(bAnyHighlight && Each.Item == HighlightedItem))
		{
			DrawSeries(Each, false);
		}
	}
	if (bAnyHighlight)
	{
		for (const FSeries& Each : Series)
		{
			if (Each.Item == HighlightedItem)
			{
				DrawSeries(Each, true);
			}
		}
	}

	// Hover readout: vertical line plus the value of each series at that time.
	if (HoverLocalX.IsSet() && HoverLocalX.GetValue() >= PlotLeft && HoverLocalX.GetValue() <= PlotRight && Series.Num() > 0)
	{
		++LayerId;
		const float X = HoverLocalX.GetValue();
		const float Time = (X - PlotLeft) / PlotWidth * SpanSeconds - SpanSeconds;
		TArray<FVector2D> Line = {FVector2D(X, PlotTop), FVector2D(X, PlotBottom)};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None,
			FLinearColor(1.f, 1.f, 1.f, 0.35f) * Tint, true, 1.f);

		// Collect the nearest values, sorted high to low, and show up to 6.
		TArray<TPair<float, const FSeries*>> Values;
		for (const FSeries& Each : Series)
		{
			int32 Best = INDEX_NONE;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Each.Times.Num(); ++Index)
			{
				const float Distance = FMath::Abs(Each.Times[Index] - Time);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Index;
				}
			}
			if (Best != INDEX_NONE && (!bAnyHighlight || Each.Item == HighlightedItem))
			{
				Values.Emplace(Each.Values[Best], &Each);
			}
		}
		Values.Sort([](const auto& A, const auto& B) { return A.Key > B.Key; });

		const FSlateFontInfo TipFont = FicsitStatsStyle::Font(FontSize::Caption);
		TArray<FText> Lines;
		Lines.Add(FormatTimeAgo(Time).ToString() == TEXT("now") ? INVTEXT("now") : FText::Format(INVTEXT("{0} ago"), FormatTimeAgo(Time)));
		for (int32 Index = 0; Index < Values.Num() && Index < 6; ++Index)
		{
			const FSeries& Each = *Values[Index].Value;
			Lines.Add(FText::Format(INVTEXT("{0}: {1}"),
				Each.Label.IsEmpty() ? UFGItemDescriptor::GetItemName(Each.Item) : Each.Label,
				Each.bPower ? FicsitStatsStyle::FormatPower(Values[Index].Key) : FicsitStatsStyle::FormatRate(Values[Index].Key, Each.Item)));
		}

		float TipWidth = 0.f;
		float LineHeight = 0.f;
		for (const FText& Text : Lines)
		{
			const FVector2D Measured = FontMeasure->Measure(Text, TipFont);
			TipWidth = FMath::Max(TipWidth, Measured.X);
			LineHeight = FMath::Max(LineHeight, Measured.Y);
		}
		const FVector2D TipSize(TipWidth + 12.f, LineHeight * Lines.Num() + 8.f);
		const float TipX = X + 10.f + TipSize.X > Size.X ? X - 10.f - TipSize.X : X + 10.f;
		const FVector2D TipPos(TipX, PlotTop + 4.f);

		++LayerId;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(TipSize, FSlateLayoutTransform(TipPos)), White,
			ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f, 0.92f) * Tint);
		++LayerId;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FLinearColor Color = Index == 0 ? FicsitStatsStyle::TextSecondary() : Values[Index - 1].Value->Color;
			FSlateDrawElement::MakeText(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(TipWidth, LineHeight), FSlateLayoutTransform(TipPos + FVector2D(6.f, 4.f + LineHeight * Index))),
				Lines[Index], TipFont, ESlateDrawEffect::None, Color * Tint);
		}
	}

	return LayerId;
}
