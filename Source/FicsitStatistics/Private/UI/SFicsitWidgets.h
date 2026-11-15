#pragma once

// Small custom Slate widgets shared by the stats window, the supply chain view and the HUD.

#include "CoreMinimal.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/FicsitStatsStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

/** A solid-colored box with hover color and left/right click callbacks (the game's flat buttons and row tiles). */
class SFicsitClickBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitClickBox)
		: _Color(FLinearColor::Transparent), _HoverColor(FLinearColor::Transparent), _Padding(FMargin(0.f)) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ATTRIBUTE(FLinearColor, HoverColor)
		SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		/** Ctrl+LMB. Unbound: Ctrl+LMB is a plain click. */
		SLATE_EVENT(FSimpleDelegate, OnCtrlClicked)
		SLATE_EVENT(FSimpleDelegate, OnRightClicked)
		SLATE_EVENT(FSimpleDelegate, OnHovered)
		SLATE_EVENT(FSimpleDelegate, OnUnhovered)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Color = InArgs._Color;
		HoverColor = InArgs._HoverColor;
		OnClicked = InArgs._OnClicked;
		OnCtrlClicked = InArgs._OnCtrlClicked;
		OnRightClicked = InArgs._OnRightClicked;
		OnHovered = InArgs._OnHovered;
		OnUnhovered = InArgs._OnUnhovered;

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FicsitStatsStyle::White())
			.Padding(InArgs._Padding)
			.BorderBackgroundColor_Lambda([this]() { return IsHovered() ? HoverColor.Get() : Color.Get(); })
			[
				InArgs._Content.Widget
			]
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			if (MouseEvent.IsControlDown() && OnCtrlClicked.IsBound())
			{
				OnCtrlClicked.Execute();
			}
			else
			{
				OnClicked.ExecuteIfBound();
			}
		}
		else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			OnRightClicked.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
		OnHovered.ExecuteIfBound();
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SCompoundWidget::OnMouseLeave(MouseEvent);
		OnUnhovered.ExecuteIfBound();
	}

private:
	TAttribute<FLinearColor> Color;
	TAttribute<FLinearColor> HoverColor;
	FSimpleDelegate OnClicked;
	FSimpleDelegate OnCtrlClicked;
	FSimpleDelegate OnRightClicked;
	FSimpleDelegate OnHovered;
	FSimpleDelegate OnUnhovered;
};

/**
 * A title that shrinks its font until it fits its width, for item names next to an icon in a fixed-width pane.
 * It starts at MaxSize and steps down 1 pt at a time to MinSize. A name too long even at MinSize wraps.
 *
 * It asks its parent for no width of its own. A plain text block asks for its full unwrapped width, so a long name
 * like "Adaptive Control Unit" pushed the row past the pane and squashed the icon beside it.
 */
class SFicsitFitText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitFitText)
		: _MaxSize(FicsitStatsStyle::FontSize::Display), _MinSize(FicsitStatsStyle::FontSize::Small), _bBold(false),
		  _Color(FSlateColor(FicsitStatsStyle::TextPrimary())) {}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ARGUMENT(float, MaxSize)
		SLATE_ARGUMENT(float, MinSize)
		SLATE_ARGUMENT(bool, bBold)
		SLATE_ATTRIBUTE(FSlateColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Text = InArgs._Text;
		MaxSize = InArgs._MaxSize;
		MinSize = FMath::Min(InArgs._MinSize, MaxSize);
		bBold = InArgs._bBold;
		CurrentSize = MaxSize;

		ChildSlot
		[
			SAssignNew(TextBlock, STextBlock)
			.Text(Text)
			.Font(FicsitStatsStyle::Font(MaxSize, bBold))
			.ColorAndOpacity(InArgs._Color)
			.AutoWrapText(true)
		];
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		// Only re-measure when the width or the text changes (the details pane keeps one header per selected item).
		const float Width = AllottedGeometry.GetLocalSize().X;
		const FText Current = Text.Get();
		if (Width <= 1.f || (FMath::IsNearlyEqual(Width, FittedWidth) && Current.EqualTo(FittedText)))
		{
			return;
		}
		FittedWidth = Width;
		FittedText = Current;

		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		float Size = MaxSize;
		while (Size > MinSize && Measure->Measure(Current, FicsitStatsStyle::Font(Size, bBold)).X > Width)
		{
			Size -= 1.f;
		}
		if (Size != CurrentSize)
		{
			CurrentSize = Size;
			TextBlock->SetFont(FicsitStatsStyle::Font(Size, bBold));
		}
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		// No width of its own (the slot's fill width is what it gets), but the text's real height.
		return FVector2D(1.f, SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier).Y);
	}

private:
	TAttribute<FText> Text;
	TSharedPtr<STextBlock> TextBlock;
	float MaxSize = 22.f;
	float MinSize = 13.f;
	bool bBold = false;
	float CurrentSize = 22.f;
	float FittedWidth = 0.f;
	FText FittedText;
};

/** Small up / down triangle or a flat dash: which way the net rate is heading compared to its average. */
class SFicsitTrendGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitTrendGlyph) : _Direction(0), _Color(FLinearColor::White) {}
		SLATE_ATTRIBUTE(int32, Direction)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Direction = InArgs._Direction;
		Color = InArgs._Color;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(10.f, 10.f); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D S = Geometry.GetLocalSize();
		const int32 Dir = Direction.Get();
		const FLinearColor C = Color.Get() * Style.GetColorAndOpacityTint();
		if (Dir == 0)
		{
			const TArray<FVector2D> Dash = {FVector2D(1.f, S.Y * 0.5f), FVector2D(S.X - 1.f, S.Y * 0.5f)};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Dash, ESlateDrawEffect::None, C, true, 1.5f);
			return LayerId;
		}
		// Filled triangle drawn as horizontal strokes.
		const float Top = Dir > 0 ? 1.f : S.Y - 1.f;
		const float Bottom = Dir > 0 ? S.Y - 1.f : 1.f;
		const int32 Steps = FMath::Max(3, FMath::RoundToInt(S.Y));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float T = static_cast<float>(Step) / Steps;
			const float Y = FMath::Lerp(Top, Bottom, T);
			const float HalfWidth = (S.X * 0.5f - 0.5f) * T;
			const TArray<FVector2D> Line = {FVector2D(S.X * 0.5f - HalfWidth, Y), FVector2D(S.X * 0.5f + HalfWidth + 0.01f, Y)};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, C, true, 1.2f);
		}
		return LayerId;
	}

private:
	TAttribute<int32> Direction;
	TAttribute<FLinearColor> Color;
};

/** Reset icon: a clockwise circular arrow with its head at the top. Drawn, since the game font has no arrows. */
class SFicsitResetGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitResetGlyph) : _Size(16.f), _Color(FLinearColor::White) {}
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Size = InArgs._Size;
		Color = InArgs._Color;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D S = Geometry.GetLocalSize();
		const FVector2D C = S * 0.5f + FVector2D(0.f, 0.5f);
		const float R = FMath::Min(S.X, S.Y) * 0.3f;
		const FLinearColor Tint = Color.Get() * Style.GetColorAndOpacityTint();

		// Arc from just above the right, clockwise (y points down), round to the top. The gap between the two ends is
		// where the head goes.
		constexpr float Start = -15.f;
		constexpr float End = 270.f;
		constexpr int32 Segments = 28;
		TArray<FVector2D> Arc;
		for (int32 Step = 0; Step <= Segments; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(FMath::Lerp(Start, End, static_cast<float>(Step) / Segments));
			Arc.Add(C + R * FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Arc, ESlateDrawEffect::None, Tint, true, 1.6f);

		// Filled head: its base straddles the arc's end (so both barbs stick out, one each side of the line) and its tip
		// points along the direction of travel (right, at the top). Filled with strokes parallel to the base.
		const float EndAngle = FMath::DegreesToRadians(End);
		const FVector2D Base = Arc.Last();
		const FVector2D Along(-FMath::Sin(EndAngle), FMath::Cos(EndAngle));
		const FVector2D Across(FMath::Cos(EndAngle), FMath::Sin(EndAngle));
		const float Length = R * 0.95f;
		const float HalfBase = R * 0.6f;
		const int32 Strokes = FMath::Max(4, FMath::CeilToInt(Length * 2.f));
		for (int32 Step = 0; Step <= Strokes; ++Step)
		{
			const float T = static_cast<float>(Step) / Strokes;
			const FVector2D Mid = Base + Along * (Length * T);
			const float Half = FMath::Max(HalfBase * (1.f - T), 0.25f);
			const TArray<FVector2D> Line = {Mid - Across * Half, Mid + Across * Half};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Tint, true, 1.2f);
		}
		return LayerId;
	}

private:
	float Size = 16.f;
	TAttribute<FLinearColor> Color;
};

/** Tree disclosure arrow: points right when collapsed, down when expanded. Drawn, since the game font has no arrows. */
class SFicsitDisclosureGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitDisclosureGlyph) : _Expanded(false), _Color(FLinearColor::White) {}
		SLATE_ATTRIBUTE(bool, Expanded)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Expanded = InArgs._Expanded;
		Color = InArgs._Color;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(10.f, 10.f); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D S = Geometry.GetLocalSize();
		const FVector2D C = S * 0.5f;
		const float R = FMath::Min(S.X, S.Y) * 0.32f;
		// A chevron (two strokes), like the game's dropdown arrows.
		const TArray<FVector2D> Points = Expanded.Get()
			? TArray<FVector2D>{C + FVector2D(-R, -R * 0.5f), C + FVector2D(0.f, R * 0.5f), C + FVector2D(R, -R * 0.5f)}
			: TArray<FVector2D>{C + FVector2D(-R * 0.5f, -R), C + FVector2D(R * 0.5f, 0.f), C + FVector2D(-R * 0.5f, R)};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color.Get() * Style.GetColorAndOpacityTint(), true, 1.8f);
		return LayerId;
	}

private:
	TAttribute<bool> Expanded;
	TAttribute<FLinearColor> Color;
};

/** Outline pushpin like the Dimensional Depot's pin button, filled in when pinned. */
class SFicsitPinGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitPinGlyph) : _Pinned(false) {}
		SLATE_ATTRIBUTE(bool, Pinned)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Pinned = InArgs._Pinned; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(18.f, 18.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float S = FMath::Min(Size.X, Size.Y) / 16.f;
		const FVector2D O((Size.X - 16.f * S) * 0.5f, (Size.Y - 16.f * S) * 0.5f);
		auto P = [&](float X, float Y) { return O + FVector2D(X * S, Y * S); };

		const bool bPinned = Pinned.Get();
		const FLinearColor Color = (bPinned ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f)) * InWidgetStyle.GetColorAndOpacityTint();

		// Pin pointing to the bottom-left: head (rotated square), collar, needle.
		const TArray<FVector2D> Head = {P(9.f, 1.5f), P(14.5f, 7.f), P(11.f, 10.5f), P(5.5f, 5.f), P(9.f, 1.5f)};
		const TArray<FVector2D> Collar = {P(3.5f, 6.5f), P(9.5f, 12.5f)};
		const TArray<FVector2D> Needle = {P(6.5f, 9.5f), P(1.5f, 14.5f)};

		if (bPinned)
		{
			// Filled head: a few parallel strokes inside the outline.
			for (float T = 0.15f; T < 0.9f; T += 0.12f)
			{
				const TArray<FVector2D> Fill = {FMath::Lerp(P(9.f, 1.5f), P(5.5f, 5.f), T), FMath::Lerp(P(14.5f, 7.f), P(11.f, 10.5f), T)};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Fill, ESlateDrawEffect::None, Color, true, 1.5f * S);
			}
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Head, ESlateDrawEffect::None, Color, true, 1.4f * S);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Collar, ESlateDrawEffect::None, Color, true, 1.6f * S);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Needle, ESlateDrawEffect::None, Color, true, 1.4f * S);
		return LayerId;
	}

private:
	TAttribute<bool> Pinned;
};

namespace FicsitWidgets
{
	/** A text block in the game's font. Text that doesn't fit ends in "...". */
	inline TSharedRef<SWidget> MakeText(const TAttribute<FText>& Text, float Size,
		const TAttribute<FSlateColor>& Color = FSlateColor(FicsitStatsStyle::TextPrimary()), bool bBold = false)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FicsitStatsStyle::Font(Size, bBold))
			.ColorAndOpacity(Color)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis);
	}
}
