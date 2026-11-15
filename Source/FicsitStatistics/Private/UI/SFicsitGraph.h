#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UFGItemDescriptor;

/** Line chart of item rates over time, drawn directly in OnPaint. */
class SFicsitGraph : public SLeafWidget
{
public:
	struct FSeries
	{
		TSubclassOf<UFGItemDescriptor> Item;
		FLinearColor Color;
		/** Seconds relative to now (<= 0), oldest first. */
		TArray<float> Times;
		TArray<float> Values;
		/** Shown instead of the item name when set (e.g. "Production" on the power graph). */
		FText Label;
		/** Values are MW (format as power) rather than item rates. */
		bool bPower = false;
	};

	SLATE_BEGIN_ARGS(SFicsitGraph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces all series. SpanSeconds is the width of the X axis (e.g. 600 for 10 minutes). */
	void SetData(TArray<FSeries>&& InSeries, float InSpanSeconds);
	/** Brightens one series and dims the others (null = none). */
	void SetHighlightedItem(TSubclassOf<UFGItemDescriptor> Item);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(400.f, 200.f); }
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	static float NiceCeiling(float Value);
	static FText FormatTimeAgo(float SecondsAgo);
	/** A Y axis label. The format depends on the axis' top value so all labels match. */
	static FText FormatAxisValue(float Value, float AxisMax, bool bPower);

	TArray<FSeries> Series;
	float SpanSeconds = 600.f;
	TSubclassOf<UFGItemDescriptor> HighlightedItem;
	TOptional<float> HoverLocalX;
};
