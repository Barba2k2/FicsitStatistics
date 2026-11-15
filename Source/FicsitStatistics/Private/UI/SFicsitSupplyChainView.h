#pragma once

#include "CoreMinimal.h"
#include "FicsitSupplyChain.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UFGItemDescriptor;

DECLARE_DELEGATE_OneParam(FOnFicsitItem, TSubclassOf<UFGItemDescriptor>);

/**
 * An item's supply chain (opened from its details): everything that goes into it as an indented tree, with what a target rate needs of each
 * part, what the factory makes of it now, and a recipe picker where there's a choice. Picks are saved per player.
 */
class SFicsitSupplyChainView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFicsitSupplyChainView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		/** Right-click on a row: show that item's details. */
		SLATE_EVENT(FOnFicsitItem, OnShowDetails)
		/** The Back button. */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show the tree for Item on the next Refresh. The target rate resets to a default for it. */
	void SetRoot(TSubclassOf<UFGItemDescriptor> Item);
	TSubclassOf<UFGItemDescriptor> GetRoot() const { return Root; }

	/** Rebuild the tree from the latest snapshot. Milestone: item -> amount the current milestone still needs. */
	void Refresh(const TMap<UClass*, int32>& Milestone);

private:
	/** One visible line of the tree. Widgets read it through attributes, so values update without rebuilding rows. */
	struct FRow
	{
		int32 Node = INDEX_NONE;
		FString Key;
		TSubclassOf<UFGItemDescriptor> Item;
		FicsitSupplyChain::ENodeKind Kind = FicsitSupplyChain::ENodeKind::Made;
		int32 Depth = 0;
		bool bHasChildren = false;
		bool bExpanded = false;
		bool bChosen = false;
		TArray<FicsitSupplyChain::FRecipe> Options;
		/** Recipes running for this item right now (marked in the picker). */
		TSet<UClass*> InUse;
		float Needed = 0.f;
		float Making = 0.f;
		FText NeededText;
		FText MakingText;
		FText RecipeText;
		FText RecipeTooltip;
		/** Making / Needed, for the bar under the row. */
		float Fraction = 0.f;
		FLinearColor StatusColor = FLinearColor::White;
	};

	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildColumnHeader();
	TSharedRef<SWidget> BuildRow(const TSharedRef<FRow>& Row);
	TSharedRef<SWidget> BuildRecipeCell(const TSharedRef<FRow>& Row);
	TSharedRef<SWidget> BuildRecipeMenu(const TSharedRef<FRow>& Row, TWeakPtr<class SMenuAnchor> Anchor);

	void Rebuild();
	void ToggleExpanded(const TSharedRef<FRow>& Row);
	void Pick(TSubclassOf<UFGItemDescriptor> Item, FicsitSupplyChain::FRecipe Recipe);
	void SetTargetFromText(const FText& Text);

	TWeakObjectPtr<UObject> WorldContext;
	FOnFicsitItem OnShowDetails;
	FSimpleDelegate OnBack;

	TSubclassOf<UFGItemDescriptor> Root;
	/** Items per minute the tree is scaled to. Reset to a default when the root changes. */
	float Target = 1.f;
	bool bTargetNeedsDefault = true;
	int32 MilestoneRemaining = 0;

	FicsitSupplyChain::FRecipeIndex Recipes;
	FicsitSupplyChain::FTree Tree;
	TMap<UClass*, int32> LastMilestone;

	/** Rows the player opened or closed by hand. Everything else follows the default (leaf-only branches start closed). */
	TMap<FString, bool> ExpandedOverrides;

	TArray<TSharedRef<FRow>> Rows;
	FString RowSignature;
	TSharedPtr<SVerticalBox> RowList;

	// Header figures, refreshed with the rows. An empty value hides its figure.
	FText CeilingText;
	FText CeilingCaption;
	FText CeilingTooltip;
	FLinearColor CeilingColor = FLinearColor::White;
	FText MilestoneText;
	FText MilestoneCaption;
	TSharedPtr<class SEditableText> TargetField;
};
