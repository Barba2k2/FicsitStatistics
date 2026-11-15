#pragma once

#include "CoreMinimal.h"
#include "FicsitStatisticsTypes.h"

class UFGItemDescriptor;
class UFGRecipe;

/**
 * The supply chain tree: everything that goes into one item, down to raw resources, with how much of each is needed
 * and how much the factory makes now. Each item is expanded with the recipes the factory actually runs for it, or a
 * recipe the player picked, or (if nothing makes it yet) an unlocked standard recipe.
 */
namespace FicsitSupplyChain
{
	using FItem = TSubclassOf<UFGItemDescriptor>;
	using FRecipe = TSubclassOf<UFGRecipe>;

	/** Unlocked recipes that a production building can run, by product. Recipes are only ever added, so the count tells when it's stale. */
	struct FRecipeIndex
	{
		void Rebuild(const UObject* WorldContext);
		bool IsStale(const UObject* WorldContext) const;
		const TArray<FRecipe>* Find(FItem Item) const { return ByProduct.Find(Item); }
		int32 NumRecipes() const { return IndexedRecipes; }

	private:
		TMap<FItem, TArray<FRecipe>> ByProduct;
		int32 SourceCount = -1;
		int32 IndexedRecipes = 0;
	};

	enum class ENodeKind : uint8
	{
		/** Made with Recipes. Its children are their ingredients. */
		Made,
		/** Mined, pumped or extracted. A leaf. */
		Raw,
		/** No unlocked production recipe makes it. A leaf. */
		NoRecipe,
		/** Already further up this branch (a loop, like recycled plastic and rubber). Not expanded again. */
		Repeat,
		/** The branch is too deep to expand further. */
		TooDeep,
	};

	struct FRecipeShare
	{
		FRecipe Recipe;
		/** Part of the node's need that comes from this recipe (0 to 1). */
		float Share = 1.f;
	};

	struct FNode
	{
		FItem Item;
		ENodeKind Kind = ENodeKind::Made;
		int32 Parent = INDEX_NONE;
		int32 Depth = 0;
		/** Which of the parent's Recipes this node is an ingredient of. */
		int32 ViaRecipe = INDEX_NONE;
		/** Items/min (m³/min for fluids) needed for 1 /min of the root. Multiply by a target rate for real numbers. */
		float PerRoot = 0.f;
		/** What the factory makes of this item right now (all uses, not just this chain), and at 100% productivity. */
		float Produced = 0.f;
		float ProducedMax = 0.f;
		/** How this node is made (Made nodes only). */
		TArray<FRecipeShare> Recipes;
		/** The player picked the recipe instead of the factory deciding it. */
		bool bChosen = false;
		/** Recipes the player could pick from for this item (the dropdown). In use first. */
		TArray<FRecipe> Options;
		TArray<int32> Children;
	};

	struct FTree
	{
		/** Depth first. Nodes[0] is the root. */
		TArray<FNode> Nodes;
		/** Each item's need summed over every place it appears, per 1 /min of the root. */
		TMap<FItem, float> TotalPerRoot;
		/**
		 * The most of the root that current production could support if every item in the tree went only to it
		 * (the root's own machines aren't a limit). The item that sets it is the bottleneck.
		 */
		float Ceiling = 0.f;
		/** The same with every machine at 100% productivity. */
		float CeilingMax = 0.f;
		FItem Bottleneck;
	};

	struct FInputs
	{
		const UObject* WorldContext = nullptr;
		const FFicsitProductionSnapshot* Snapshot = nullptr;
		const FRecipeIndex* Recipes = nullptr;
		/** Recipes the player picked, per item. Missing items follow the factory. */
		const TMap<FItem, FRecipe>* Chosen = nullptr;
	};

	FTree Build(const FInputs& Inputs, FItem Root);

	/** Alternate recipes come from hard drives. The game has no flag for it, but every one is named Recipe_Alternate_*. */
	bool IsAlternate(FRecipe Recipe);

	/** The tree as indented text, with needs scaled to TargetPerMinute (for the log). */
	FString Describe(const FTree& Tree, float TargetPerMinute);
}
