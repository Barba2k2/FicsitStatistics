#include "FicsitSupplyChain.h"

#include "Buildables/FGBuildableManufacturer.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"

namespace FicsitSupplyChain
{
	namespace
	{
		// Late-game chains are about 10 levels deep. These only stop a runaway from a modded recipe loop.
		constexpr int32 MaxDepth = 24;
		constexpr int32 MaxNodes = 4000;

		// Recipe amounts are in liters for fluids, the snapshot is in m³ (the same conversion the scan uses).
		using FicsitUnits::ToDisplayAmount;

		float AmountOf(const TArray<FItemAmount>& Amounts, FItem Item)
		{
			float Sum = 0.f;
			for (const FItemAmount& Amount : Amounts)
			{
				if (Amount.ItemClass == Item)
				{
					Sum += ToDisplayAmount(Amount.Amount, Item);
				}
			}
			return Sum;
		}

		bool IsRaw(FItem Item)
		{
			return Item && Item->IsChildOf(UFGResourceDescriptor::StaticClass());
		}

		/** Unpackaging turns a packaged item back into itself. It's never how the item is made in the first place. */
		bool IsUnpackage(FRecipe Recipe)
		{
			return Recipe->GetName().Contains(TEXT("Unpackage"));
		}

		/** The recipe to assume for an item nobody makes yet: standard over alternate, then the one the game lists first. */
		void SortForDefault(TArray<FRecipe>& Recipes, FItem Item)
		{
			auto IsMainProduct = [Item](FRecipe Recipe)
			{
				const TArray<FItemAmount>& Products = Recipe.GetDefaultObject()->GetProducts();
				return Products.Num() > 0 && Products[0].ItemClass == Item;
			};
			Recipes.StableSort([&](const FRecipe& A, const FRecipe& B)
			{
				if (IsAlternate(A) != IsAlternate(B)) { return !IsAlternate(A); }
				if (IsUnpackage(A) != IsUnpackage(B)) { return !IsUnpackage(A); }
				if (IsMainProduct(A) != IsMainProduct(B)) { return IsMainProduct(A); }
				return UFGRecipe::GetManufacturingMenuPriority(A) < UFGRecipe::GetManufacturingMenuPriority(B);
			});
		}

		FString ItemName(FItem Item)
		{
			return Item ? UFGItemDescriptor::GetItemName(Item).ToString() : TEXT("(none)");
		}

		struct FBuilder
		{
			const FInputs& In;
			FTree Tree;
			TArray<FItem> Path;

			/** In use first (most output first), then the other unlocked recipes. */
			TArray<FRecipe> GatherOptions(FItem Item) const
			{
				TArray<FRecipe> Options;
				if (const FFicsitRecipeRateMap* InUse = In.Snapshot ? In.Snapshot->RecipeOutput.Find(Item) : nullptr)
				{
					TArray<TPair<FRecipe, float>> Sorted = InUse->Array();
					Sorted.Sort([](const auto& A, const auto& B) { return A.Value > B.Value; });
					for (const auto& Pair : Sorted) { Options.Add(Pair.Key); }
				}
				if (const TArray<FRecipe>* Unlocked = In.Recipes ? In.Recipes->Find(Item) : nullptr)
				{
					TArray<FRecipe> Rest = *Unlocked;
					SortForDefault(Rest, Item);
					for (const FRecipe& Recipe : Rest) { Options.AddUnique(Recipe); }
				}
				return Options;
			}

			TArray<FRecipeShare> ChooseRecipes(FItem Item, const TArray<FRecipe>& Options, bool& bOutChosen) const
			{
				TArray<FRecipeShare> Shares;
				bOutChosen = false;
				if (const FRecipe* Picked = In.Chosen ? In.Chosen->Find(Item) : nullptr; Picked && *Picked)
				{
					bOutChosen = true;
					Shares.Add({*Picked, 1.f});
					return Shares;
				}
				// What the factory runs, split by how much each recipe makes.
				if (const FFicsitRecipeRateMap* InUse = In.Snapshot ? In.Snapshot->RecipeOutput.Find(Item) : nullptr)
				{
					float Total = 0.f;
					for (const auto& Pair : *InUse) { Total += Pair.Value; }
					if (Total > 0.f)
					{
						for (const auto& Pair : *InUse) { Shares.Add({Pair.Key, Pair.Value / Total}); }
						Shares.Sort([](const FRecipeShare& A, const FRecipeShare& B) { return A.Share > B.Share; });
						return Shares;
					}
				}
				// Nothing makes it yet: the best unlocked recipe (Options lists the unlocked ones in default order).
				if (Options.Num() > 0)
				{
					Shares.Add({Options[0], 1.f});
				}
				return Shares;
			}

			void Add(FItem Item, float PerRoot, int32 Parent, int32 ViaRecipe, int32 Depth)
			{
				const int32 Index = Tree.Nodes.AddDefaulted();
				{
					FNode& Node = Tree.Nodes[Index];
					Node.Item = Item;
					Node.Parent = Parent;
					Node.ViaRecipe = ViaRecipe;
					Node.Depth = Depth;
					Node.PerRoot = PerRoot;
					if (const FFicsitItemFlow* Flow = In.Snapshot ? In.Snapshot->Produced.Find(Item) : nullptr)
					{
						Node.Produced = Flow->Total.Actual;
						Node.ProducedMax = Flow->Total.Max;
					}
					Node.Options = GatherOptions(Item);
				}
				Tree.TotalPerRoot.FindOrAdd(Item) += PerRoot;
				if (Parent != INDEX_NONE)
				{
					Tree.Nodes[Parent].Children.Add(Index);
				}

				if (Path.Contains(Item))
				{
					Tree.Nodes[Index].Kind = ENodeKind::Repeat;
					return;
				}
				if (Depth >= MaxDepth || Tree.Nodes.Num() >= MaxNodes)
				{
					Tree.Nodes[Index].Kind = ENodeKind::TooDeep;
					return;
				}

				bool bChosen = false;
				TArray<FRecipeShare> Shares = ChooseRecipes(Item, Tree.Nodes[Index].Options, bChosen);
				// Ores, water, oil and gases are leaves, even though converters can make some of them, unless the player picks a recipe.
				if (IsRaw(Item) && !bChosen)
				{
					Tree.Nodes[Index].Kind = ENodeKind::Raw;
					return;
				}
				if (Shares.Num() == 0)
				{
					Tree.Nodes[Index].Kind = ENodeKind::NoRecipe;
					return;
				}
				Tree.Nodes[Index].Recipes = Shares;
				Tree.Nodes[Index].bChosen = bChosen;

				Path.Push(Item);
				for (int32 RecipeIndex = 0; RecipeIndex < Shares.Num(); ++RecipeIndex)
				{
					const UFGRecipe* Recipe = Shares[RecipeIndex].Recipe.GetDefaultObject();
					const float OutPerCycle = AmountOf(Recipe->GetProducts(), Item);
					if (OutPerCycle <= KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const float CyclesPerRoot = PerRoot * Shares[RecipeIndex].Share / OutPerCycle;
					// Includes the session's parts-cost multiplier, like the scan.
					for (const FItemAmount& Ingredient : Recipe->GetIngredientsWithPartsCostMultiplier(In.WorldContext))
					{
						if (Ingredient.ItemClass)
						{
							Add(Ingredient.ItemClass, CyclesPerRoot * ToDisplayAmount(Ingredient.Amount, Ingredient.ItemClass), Index, RecipeIndex, Depth + 1);
						}
					}
				}
				Path.Pop();
			}

			void ComputeCeiling(FItem Root)
			{
				Tree.Ceiling = TNumericLimits<float>::Max();
				Tree.CeilingMax = TNumericLimits<float>::Max();
				for (const auto& Pair : Tree.TotalPerRoot)
				{
					if (Pair.Key == Root || Pair.Value <= KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const FFicsitItemFlow* Flow = In.Snapshot ? In.Snapshot->Produced.Find(Pair.Key) : nullptr;
					const float Supported = (Flow ? Flow->Total.Actual : 0.f) / Pair.Value;
					if (Supported < Tree.Ceiling)
					{
						Tree.Ceiling = Supported;
						Tree.Bottleneck = Pair.Key;
					}
					Tree.CeilingMax = FMath::Min(Tree.CeilingMax, (Flow ? Flow->Total.Max : 0.f) / Pair.Value);
				}
				// A root with no ingredients (raw, or no recipe) is limited only by what is made of it.
				if (!Tree.Bottleneck)
				{
					Tree.Ceiling = Tree.Nodes.Num() > 0 ? Tree.Nodes[0].Produced : 0.f;
					Tree.CeilingMax = Tree.Nodes.Num() > 0 ? Tree.Nodes[0].ProducedMax : 0.f;
				}
			}
		};
	}

	void FRecipeIndex::Rebuild(const UObject* WorldContext)
	{
		ByProduct.Reset();
		IndexedRecipes = 0;
		SourceCount = -1;
		AFGRecipeManager* Manager = WorldContext ? AFGRecipeManager::Get(const_cast<UObject*>(WorldContext)) : nullptr;
		if (!Manager)
		{
			return;
		}
		const TArray<FRecipe>& Available = Manager->GetAllAvailableRecipes();
		SourceCount = Available.Num();

		TArray<TSubclassOf<UObject>> ProducedIn;
		for (const FRecipe& Recipe : Available)
		{
			if (!Recipe)
			{
				continue;
			}
			// Only recipes a production building runs: not the build gun, the Craft Bench or the Equipment Workshop.
			ProducedIn.Reset();
			Recipe.GetDefaultObject()->GetProducedIn(ProducedIn);
			const bool bInBuilding = ProducedIn.ContainsByPredicate([](const TSubclassOf<UObject>& Producer)
			{
				return Producer && Producer->IsChildOf(AFGBuildableManufacturer::StaticClass());
			});
			if (!bInBuilding)
			{
				continue;
			}
			++IndexedRecipes;
			for (const FItemAmount& Product : Recipe.GetDefaultObject()->GetProducts())
			{
				if (Product.ItemClass)
				{
					ByProduct.FindOrAdd(Product.ItemClass).AddUnique(Recipe);
				}
			}
		}
	}

	bool FRecipeIndex::IsStale(const UObject* WorldContext) const
	{
		const AFGRecipeManager* Manager = WorldContext ? AFGRecipeManager::Get(const_cast<UObject*>(WorldContext)) : nullptr;
		return !Manager || Manager->GetAllAvailableRecipes().Num() != SourceCount;
	}

	bool IsAlternate(FRecipe Recipe)
	{
		return Recipe && Recipe->GetName().Contains(TEXT("Alternate"));
	}

	FTree Build(const FInputs& Inputs, FItem Root)
	{
		FBuilder Builder{Inputs};
		if (Root)
		{
			Builder.Add(Root, 1.f, INDEX_NONE, INDEX_NONE, 0);
			Builder.ComputeCeiling(Root);
		}
		return MoveTemp(Builder.Tree);
	}

	TArray<FRawNeed> RawNeeds(const FTree& Tree)
	{
		TArray<FRawNeed> Needs;
		for (const FNode& Node : Tree.Nodes)
		{
			if (Node.Kind != ENodeKind::Raw)
			{
				continue;
			}
			FRawNeed* Need = Needs.FindByPredicate([&Node](const FRawNeed& Each) { return Each.Item == Node.Item; });
			if (!Need)
			{
				Need = &Needs.AddDefaulted_GetRef();
				Need->Item = Node.Item;
				Need->Produced = Node.Produced;
			}
			Need->PerRoot += Node.PerRoot;
		}
		Needs.StableSort([](const FRawNeed& A, const FRawNeed& B) { return A.PerRoot > B.PerRoot; });
		return Needs;
	}

	FString Describe(const FTree& Tree, float TargetPerMinute)
	{
		if (Tree.Nodes.Num() == 0)
		{
			return TEXT("(empty)");
		}
		FString Text = FString::Printf(TEXT("%s at %.2f /min: %d nodes, %d items, ceiling %.2f /min (max %.2f), bottleneck %s\n"),
			*ItemName(Tree.Nodes[0].Item), TargetPerMinute, Tree.Nodes.Num(), Tree.TotalPerRoot.Num(),
			Tree.Ceiling, Tree.CeilingMax, *ItemName(Tree.Bottleneck));

		FString Raw;
		for (const FRawNeed& Need : RawNeeds(Tree))
		{
			Raw += FString::Printf(TEXT("%s%s %.2f (extracting %.2f)"), Raw.IsEmpty() ? TEXT("") : TEXT(", "),
				*ItemName(Need.Item), Need.PerRoot * TargetPerMinute, Need.Produced);
		}
		Text += FString::Printf(TEXT("  raw resources: %s\n"), Raw.IsEmpty() ? TEXT("none") : *Raw);

		for (const FNode& Node : Tree.Nodes)
		{
			FString How;
			switch (Node.Kind)
			{
			case ENodeKind::Raw:      How = TEXT("raw"); break;
			case ENodeKind::NoRecipe: How = TEXT("no unlocked recipe"); break;
			case ENodeKind::Repeat:   How = TEXT("loop, see above"); break;
			case ENodeKind::TooDeep:  How = TEXT("too deep"); break;
			case ENodeKind::Made:
				for (const FRecipeShare& Share : Node.Recipes)
				{
					How += FString::Printf(TEXT("%s%s %.0f%%"), How.IsEmpty() ? TEXT("via ") : TEXT(" + "),
						*UFGRecipe::GetRecipeName(Share.Recipe).ToString(), Share.Share * 100.f);
				}
				if (Node.bChosen) { How += TEXT(" (picked)"); }
				break;
			}
			const FString Parent = Node.Parent != INDEX_NONE && Tree.Nodes[Node.Parent].Recipes.Num() > 1
				? FString::Printf(TEXT(" [for %s]"), *UFGRecipe::GetRecipeName(Tree.Nodes[Node.Parent].Recipes[Node.ViaRecipe].Recipe).ToString())
				: FString();
			Text += FString::Printf(TEXT("%s%s: needs %.2f, makes %.2f (max %.2f), %s, %d recipes to pick from%s\n"),
				*FString::ChrN(2 + Node.Depth * 2, TEXT(' ')), *ItemName(Node.Item), Node.PerRoot * TargetPerMinute,
				Node.Produced, Node.ProducedMax, *How, Node.Options.Num(), *Parent);
		}
		return Text;
	}
}
