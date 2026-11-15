#include "UI/SFicsitSupplyChainView.h"

#include "FGRecipe.h"
#include "FicsitStatisticsSubsystem.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "UI/FicsitPlayerPrefs.h"
#include "UI/FicsitStatsStyle.h"
#include "UI/SFicsitWidgets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FicsitStatistics"

namespace FontSize = FicsitStatsStyle::FontSize;
namespace FSC = FicsitSupplyChain;

// Named (not anonymous) because unity builds merge this file with the window's, which has its own helpers.
namespace FicsitChainViewDetail
{
	constexpr float NeededColumn = 110.f;
	constexpr float MakingColumn = 110.f;
	constexpr float RecipeColumn = 330.f;
	constexpr float IndentPerLevel = 22.f;

	using FicsitWidgets::MakeText;

	/** One header figure: a small label, the value, and a caption under it. Stats sit side by side with a thin divider. */
	TSharedRef<SWidget> MakeStat(const FText& Label, const TSharedRef<SWidget>& Value, const TAttribute<FText>& Caption,
		const TAttribute<FText>& Tooltip, const TAttribute<EVisibility>& Visibility)
	{
		return SNew(SHorizontalBox)
			.Visibility(Visibility)
			.ToolTipText(Tooltip)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 2.f, 16.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(1.f)
				[
					SNew(SBorder)
					.BorderImage(FicsitStatsStyle::White())
					.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.1f))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 20.f, 0.f)
			[
				SNew(SBox)
				.MinDesiredWidth(120.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeText(Label, FontSize::Caption, FSlateColor(FicsitStatsStyle::TextSecondary()))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
					[
						Value
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeText(Caption, FontSize::Caption, FSlateColor(FicsitStatsStyle::TextSecondary()))
					]
				]
			];
	}

	// Recipe picks, saved per player next to the pins: "ItemClassPath|RecipeClassPath" per entry.
	const TCHAR* PicksKey = TEXT("RecipePicks");

	void KeepPicksAlive(const TMap<FSC::FItem, FSC::FRecipe>& Picks)
	{
		TArray<UClass*> Classes;
		for (const auto& Pair : Picks)
		{
			Classes.Add(Pair.Key.Get());
			Classes.Add(Pair.Value.Get());
		}
		FicsitPlayerPrefs::KeepAlive(PicksKey, Classes);
	}

	TMap<FSC::FItem, FSC::FRecipe>& GetPicks()
	{
		static TMap<FSC::FItem, FSC::FRecipe> Picks;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			for (const FString& Entry : FicsitPlayerPrefs::GetArray(PicksKey))
			{
				FString ItemPath, RecipePath;
				if (!Entry.Split(TEXT("|"), &ItemPath, &RecipePath))
				{
					continue;
				}
				UClass* Item = FSoftClassPath(ItemPath).TryLoadClass<UFGItemDescriptor>();
				UClass* Recipe = FSoftClassPath(RecipePath).TryLoadClass<UFGRecipe>();
				if (Item && Recipe)
				{
					Picks.Add(Item, Recipe);
				}
			}
			KeepPicksAlive(Picks);
		}
		return Picks;
	}

	/** Call after every change to the picks: saves them and keeps the picked classes loaded. */
	void SavePicks()
	{
		KeepPicksAlive(GetPicks());
		TArray<FString> Entries;
		for (const auto& Pair : GetPicks())
		{
			if (Pair.Key && Pair.Value)
			{
				Entries.Add(FSoftClassPath(Pair.Key.Get()).ToString() + TEXT("|") + FSoftClassPath(Pair.Value.Get()).ToString());
			}
		}
		FicsitPlayerPrefs::SetArray(PicksKey, Entries);
	}

	/** A default target that reads as a plain number: two significant digits. */
	float RoundTarget(float Value)
	{
		if (Value <= 0.f)
		{
			return 1.f;
		}
		const float Magnitude = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, Value)) - 1.f);
		return FMath::Max(0.01f, FMath::CeilToFloat(Value / Magnitude) * Magnitude);
	}

	FText FormatTarget(float Value)
	{
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 2;
		Options.UseGrouping = false;
		return FText::AsNumber(Value, &Options);
	}

	/** "15 Iron Ingot + 7.5 Coal  >  30 /min" at 100% speed, for the recipe picker. */
	FText DescribeRecipe(FSC::FRecipe Recipe, FSC::FItem Product, const UObject* WorldContext)
	{
		const UFGRecipe* CDO = Recipe.GetDefaultObject();
		const float Duration = UFGRecipe::GetManufacturingDuration(Recipe);
		if (!CDO || Duration <= KINDA_SMALL_NUMBER)
		{
			return FText::GetEmpty();
		}
		const float PerMinute = 60.f / Duration;
		auto Amount = [PerMinute](const FItemAmount& Each)
		{
			return FicsitUnits::ToDisplayAmount(Each.Amount, Each.ItemClass) * PerMinute;
		};

		FString Inputs;
		for (const FItemAmount& Ingredient : CDO->GetIngredientsWithPartsCostMultiplier(WorldContext))
		{
			if (Ingredient.ItemClass)
			{
				Inputs += FString::Printf(TEXT("%s%s %s"), Inputs.IsEmpty() ? TEXT("") : TEXT(" + "),
					*FicsitStatsStyle::FormatNumber(Amount(Ingredient)).ToString(), *UFGItemDescriptor::GetItemName(Ingredient.ItemClass).ToString());
			}
		}
		float Out = 0.f;
		for (const FItemAmount& Each : CDO->GetProducts())
		{
			if (Each.ItemClass == Product)
			{
				Out += Amount(Each);
			}
		}
		return FText::Format(LOCTEXT("ChainRecipeRates", "{0}  makes {1}"), FText::FromString(Inputs), FicsitStatsStyle::FormatRate(Out, Product));
	}
}
namespace ChainDetail = FicsitChainViewDetail;

void SFicsitSupplyChainView::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnShowDetails = InArgs._OnShowDetails;
	OnBack = InArgs._OnBack;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildHeader()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 10.f, 0.f, 0.f)
		[
			BuildColumnHeader()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(RowList, SVerticalBox)
			]
		]
	];
	Rebuild();
}

TSharedRef<SWidget> SFicsitSupplyChainView::BuildHeader()
{
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::RowTile())
		.Padding(FMargin(12.f, 10.f))
		.Visibility_Lambda([this]() { return Root ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 16.f, 0.f)
			[
				SNew(SFicsitClickBox)
				.Color(FicsitStatsStyle::TabActive())
				.HoverColor(FicsitStatsStyle::Orange())
				.Padding(FMargin(16.f, 8.f))
				.OnClicked_Lambda([this]() { OnBack.ExecuteIfBound(); })
				[
					ChainDetail::MakeText(LOCTEXT("ChainBack", "Back"), FontSize::Body)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.MinWidth(52.f)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(52.f)
				.HeightOverride(52.f)
				[
					SNew(SImage)
					.Image_Lambda([this]() { return FicsitStatsStyle::ItemIcon(Root); })
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(14.f, 0.f, 16.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					ChainDetail::MakeText(LOCTEXT("ChainTitle", "Supply chain"), FontSize::Small, FSlateColor(FicsitStatsStyle::TextSecondary()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					// Long names get a smaller font instead of squashing the icon.
					SNew(SFicsitFitText)
					.Text_Lambda([this]() { return Root ? UFGItemDescriptor::GetItemName(Root) : FText::GetEmpty(); })
					.MaxSize(FontSize::Display)
					.bBold(true)
				]
			]
			// Numbers, not sentences: what supply allows, what the milestone needs, and the target everything below is for.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				ChainDetail::MakeStat(LOCTEXT("ChainSupplyLabel", "Supply allows"),
					ChainDetail::MakeText(TAttribute<FText>::CreateLambda([this]() { return CeilingText; }), FontSize::Body,
						TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(CeilingColor); })),
					TAttribute<FText>::CreateLambda([this]() { return CeilingCaption; }),
					TAttribute<FText>::CreateLambda([this]() { return CeilingTooltip; }),
					TAttribute<EVisibility>::CreateLambda([this]() { return CeilingText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				ChainDetail::MakeStat(LOCTEXT("ChainMilestoneLabel", "Milestone"),
					ChainDetail::MakeText(TAttribute<FText>::CreateLambda([this]() { return MilestoneText; }), FontSize::Body, FSlateColor(FicsitStatsStyle::Orange())),
					TAttribute<FText>::CreateLambda([this]() { return MilestoneCaption; }),
					LOCTEXT("ChainMilestoneTooltip", "How many the current milestone (Space Elevator and HUB) still needs, and how long that takes at the target rate."),
					TAttribute<EVisibility>::CreateLambda([this]() { return MilestoneText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				ChainDetail::MakeStat(LOCTEXT("ChainTarget", "Target"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(84.f)
						[
							SNew(SBorder)
							.BorderImage(FicsitStatsStyle::White())
							.BorderBackgroundColor(FicsitStatsStyle::SearchField())
							.Padding(FMargin(8.f, 2.f))
							[
								SAssignNew(TargetField, SEditableText)
								.Text(ChainDetail::FormatTarget(Target))
								.Font(FicsitStatsStyle::Font(FontSize::Body))
								.ColorAndOpacity(FicsitStatsStyle::TextPrimary())
								.Justification(ETextJustify::Right)
								.SelectAllTextWhenFocused(true)
								.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { SetTargetFromText(Text); })
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.f, 0.f, 0.f, 0.f)
					[
						ChainDetail::MakeText(TAttribute<FText>::CreateLambda([this]()
						{
							return Root && FicsitUnits::IsFluid(Root) ? LOCTEXT("ChainPerMinuteFluid", "m³/min") : LOCTEXT("ChainPerMinute", "/min");
						}), FontSize::Body, FSlateColor(FicsitStatsStyle::TextSecondary()))
					],
					LOCTEXT("ChainTargetCaption", "Enter to apply"),
					LOCTEXT("ChainTargetTooltip", "How many of this item per minute to plan for. Every Needed number is for this rate."),
					EVisibility::Visible)
			]
		];
}

TSharedRef<SWidget> SFicsitSupplyChainView::BuildColumnHeader()
{
	auto Column = [](const FText& Label, float Width, const FText& Tooltip)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HAlign(HAlign_Right)
			.ToolTipText(Tooltip)
			[
				ChainDetail::MakeText(Label, FontSize::Small)
			];
	};
	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FicsitStatsStyle::SectionStrip())
		.Padding(FMargin(12.f, 6.f, 28.f, 6.f))
		.Visibility_Lambda([this]() { return Root ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ChainDetail::MakeText(LOCTEXT("ChainItem", "Item"), FontSize::Small)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					ChainDetail::MakeText(LOCTEXT("ChainPerMinuteHeader", "rates per minute"), FontSize::Small, FSlateColor(FicsitStatsStyle::TextSecondary()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Column(LOCTEXT("ChainNeeded", "Needed"), ChainDetail::NeededColumn, LOCTEXT("ChainNeededTooltip", "How much of this the target rate needs, through this branch."))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Column(LOCTEXT("ChainMaking", "Making now"), ChainDetail::MakingColumn,
					LOCTEXT("ChainMakingTooltip", "Everything your factory makes of this right now, for all of its uses, not just this one."))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(ChainDetail::RecipeColumn)
				[
					ChainDetail::MakeText(LOCTEXT("ChainRecipe", "Recipe"), FontSize::Small)
				]
			]
		];
}

void SFicsitSupplyChainView::SetRoot(TSubclassOf<UFGItemDescriptor> Item)
{
	if (Item == Root)
	{
		return;
	}
	Root = Item;
	bTargetNeedsDefault = true;
	ExpandedOverrides.Reset();
	// No refresh here: the window refreshes right after, with the milestone amounts the default target depends on.
}

void SFicsitSupplyChainView::SetTargetFromText(const FText& Text)
{
	FString Clean = Text.ToString().Replace(TEXT(","), TEXT("")).TrimStartAndEnd();
	const float Value = FCString::Atof(*Clean);
	if (Value > 0.f && FMath::IsFinite(Value))
	{
		Target = Value;
	}
	if (TargetField.IsValid())
	{
		TargetField->SetText(ChainDetail::FormatTarget(Target));
	}
	Refresh(LastMilestone);
}

void SFicsitSupplyChainView::Pick(TSubclassOf<UFGItemDescriptor> Item, FSC::FRecipe Recipe)
{
	if (Recipe)
	{
		ChainDetail::GetPicks().Add(Item, Recipe);
	}
	else
	{
		ChainDetail::GetPicks().Remove(Item);
	}
	ChainDetail::SavePicks();
	Refresh(LastMilestone);
}

void SFicsitSupplyChainView::ToggleExpanded(const TSharedRef<FRow>& Row)
{
	ExpandedOverrides.Add(Row->Key, !Row->bExpanded);
	Rebuild();
}

void SFicsitSupplyChainView::Refresh(const TMap<UClass*, int32>& Milestone)
{
	if (&Milestone != &LastMilestone)
	{
		LastMilestone = Milestone;
	}
	AFicsitStatisticsSubsystem* Subsystem = AFicsitStatisticsSubsystem::Get(WorldContext.Get());
	if (!Subsystem || !Root)
	{
		Tree = FSC::FTree();
		Rebuild();
		return;
	}

	const UObject* Context = WorldContext.Get();
	if (Recipes.IsStale(Context))
	{
		Recipes.Rebuild(Context);
	}
	const FFicsitProductionSnapshot& Snapshot = Subsystem->GetLatestSnapshot();
	MilestoneRemaining = LastMilestone.FindRef(Root.Get());

	if (bTargetNeedsDefault)
	{
		// A milestone part: enough to finish it in an hour. Anything else: what's made of it now (or 1 /min).
		const FFicsitItemFlow* Flow = Snapshot.Produced.Find(Root);
		const float Made = Flow ? Flow->Total.Actual : 0.f;
		Target = ChainDetail::RoundTarget(MilestoneRemaining > 0 ? MilestoneRemaining / 60.f : (Made > 0.f ? Made : 1.f));
		bTargetNeedsDefault = false;
		if (TargetField.IsValid())
		{
			TargetField->SetText(ChainDetail::FormatTarget(Target));
		}
	}

	FSC::FInputs Inputs;
	Inputs.WorldContext = Context;
	Inputs.Snapshot = &Snapshot;
	Inputs.Recipes = &Recipes;
	Inputs.Chosen = &ChainDetail::GetPicks();
	Tree = FSC::Build(Inputs, Root);

	// Header figures.
	MilestoneText = MilestoneRemaining > 0
		? FText::Format(LOCTEXT("ChainMilestoneLeft", "{0} left"), FicsitStatsStyle::FormatNumber(MilestoneRemaining))
		: FText::GetEmpty();
	MilestoneCaption = FText::Format(LOCTEXT("ChainMilestoneEta", "~{0} at target"),
		FicsitStatsStyle::FormatDuration(MilestoneRemaining / FMath::Max(Target, 0.001f)));

	CeilingText = FText::GetEmpty();
	if (Tree.Bottleneck)
	{
		const FText Limit = UFGItemDescriptor::GetItemName(Tree.Bottleneck);
		CeilingText = FicsitStatsStyle::FormatRate(Tree.Ceiling, Root);
		CeilingColor = Tree.Ceiling <= 0.f ? FicsitStatsStyle::Deficit()
			: Tree.Ceiling >= Target * 0.995f ? FicsitStatsStyle::Surplus()
			: FicsitStatsStyle::Warning();
		CeilingCaption = Tree.Ceiling <= 0.f
			? FText::Format(LOCTEXT("ChainCeilingNoneCaption", "{0} not made"), Limit)
			: FText::Format(LOCTEXT("ChainCeilingCaption", "limited by {0}"), Limit);
		CeilingTooltip = FText::Format(LOCTEXT("ChainCeilingTooltip", "The most of this your factory could make from what it produces today, if every part went to it. {0} runs out first."), Limit);
		if (Tree.CeilingMax > Tree.Ceiling * 1.05f && Tree.CeilingMax > 0.f)
		{
			CeilingTooltip = FText::Format(LOCTEXT("ChainCeilingMaxTooltip", "{0}\nWith every machine at 100%: {1}."), CeilingTooltip, FicsitStatsStyle::FormatRate(Tree.CeilingMax, Root));
		}
	}
	Rebuild();
}

void SFicsitSupplyChainView::Rebuild()
{
	if (!RowList.IsValid())
	{
		return;
	}

	// 1) The visible rows, from the tree and which branches are open.
	TArray<FString> Keys;
	Keys.SetNum(Tree.Nodes.Num());
	TArray<TSharedRef<FRow>> NewRows;
	FString Signature = Root ? Root->GetPathName() : FString();
	int32 HiddenBelowDepth = MAX_int32;

	for (int32 Index = 0; Index < Tree.Nodes.Num(); ++Index)
	{
		const FSC::FNode& Node = Tree.Nodes[Index];
		Keys[Index] = (Node.Parent != INDEX_NONE ? Keys[Node.Parent] : FString())
			+ FString::Printf(TEXT("/%d:"), Node.ViaRecipe) + (Node.Item ? Node.Item->GetName() : FString());
		if (Node.Depth > HiddenBelowDepth)
		{
			continue;
		}
		HiddenBelowDepth = MAX_int32;

		TSharedRef<FRow> Row = MakeShared<FRow>();
		Row->Node = Index;
		Row->Key = Keys[Index];
		Row->Item = Node.Item;
		Row->Kind = Node.Kind;
		Row->Depth = Node.Depth;
		Row->bHasChildren = Node.Children.Num() > 0;
		Row->bChosen = Node.bChosen;
		Row->Options = Node.Options;

		// Branches that only lead to raw resources (Iron Ingot from Iron Ore) start closed: they'd repeat on every level.
		const bool bOnlyLeaves = !Node.Children.ContainsByPredicate([this](int32 Child) { return Tree.Nodes[Child].Children.Num() > 0; });
		const bool* Override = ExpandedOverrides.Find(Row->Key);
		Row->bExpanded = Override ? *Override : (Node.Depth == 0 || !bOnlyLeaves);
		if (Row->bHasChildren && !Row->bExpanded)
		{
			HiddenBelowDepth = Node.Depth;
		}

		// Values (also refreshed in place when the layout doesn't change).
		Row->Needed = Node.PerRoot * Target;
		Row->Making = Node.Produced;
		Row->NeededText = FicsitStatsStyle::FormatTableRate(Row->Needed, Node.Item);
		Row->MakingText = FicsitStatsStyle::FormatTableRate(Row->Making, Node.Item);
		Row->Fraction = Row->Needed > 0.f ? Row->Making / Row->Needed : 1.f;
		Row->StatusColor = Node.Kind == FSC::ENodeKind::Repeat ? FicsitStatsStyle::TextSecondary()
			: Row->Fraction >= 0.995f ? FicsitStatsStyle::Surplus()
			: Row->Making > 0.f ? FicsitStatsStyle::Warning()
			: FicsitStatsStyle::Deficit();

		switch (Node.Kind)
		{
		case FSC::ENodeKind::Made:
			if (Node.Recipes.Num() == 1)
			{
				Row->RecipeText = UFGRecipe::GetRecipeName(Node.Recipes[0].Recipe);
				Row->RecipeTooltip = Node.bChosen ? LOCTEXT("ChainPickedTooltip", "You picked this recipe.")
					: LOCTEXT("ChainSingleTooltip", "The recipe your machines run (or the standard one, if nothing makes it yet).");
			}
			else
			{
				// Mixed: every recipe the factory runs, with its share of what's made.
				TArray<FText> Parts;
				for (const FSC::FRecipeShare& Share : Node.Recipes)
				{
					Parts.Add(FText::Format(LOCTEXT("ChainShare", "{0} {1}%"), UFGRecipe::GetRecipeName(Share.Recipe), FText::AsNumber(FMath::RoundToInt(Share.Share * 100.f))));
				}
				Row->RecipeText = FText::Join(INVTEXT(" · "), Parts);
				Row->RecipeTooltip = FText::Format(LOCTEXT("ChainMixedTooltip", "Your machines make it {0} ways. The tree splits the need the same way:\n{1}"),
					FText::AsNumber(Node.Recipes.Num()), FText::Join(INVTEXT("\n"), Parts));
			}
			break;
		case FSC::ENodeKind::Raw:
			Row->RecipeText = LOCTEXT("ChainRaw", "Raw resource");
			Row->RecipeTooltip = LOCTEXT("ChainRawTooltip", "Mined, pumped or extracted.");
			break;
		case FSC::ENodeKind::NoRecipe:
			Row->RecipeText = LOCTEXT("ChainNoRecipe", "No unlocked recipe");
			Row->RecipeTooltip = LOCTEXT("ChainNoRecipeTooltip", "Nothing you've unlocked can make this in a building yet.");
			break;
		case FSC::ENodeKind::Repeat:
			Row->RecipeText = LOCTEXT("ChainRepeat", "Loop: made further up");
			Row->RecipeTooltip = LOCTEXT("ChainRepeatTooltip", "This branch feeds back into itself, so it isn't expanded again.");
			break;
		case FSC::ENodeKind::TooDeep:
			Row->RecipeText = LOCTEXT("ChainTooDeep", "Too deep to show");
			Row->RecipeTooltip = Row->RecipeText;
			break;
		}

		Signature += FString::Printf(TEXT("|%s,%d,%d,%d,%d,%d"), *Row->Key, static_cast<int32>(Node.Kind), Row->bExpanded, Row->bHasChildren, Node.bChosen, Node.Options.Num());
		for (const FSC::FRecipeShare& Share : Node.Recipes)
		{
			Signature += Share.Recipe ? Share.Recipe->GetName() : FString();
		}
		NewRows.Add(Row);
	}

	// Recipes in use (for the picker's "in use" marks), straight from the snapshot.
	if (const AFicsitStatisticsSubsystem* Subsystem = AFicsitStatisticsSubsystem::Get(WorldContext.Get()))
	{
		const FFicsitProductionSnapshot& Snapshot = Subsystem->GetLatestSnapshot();
		for (const TSharedRef<FRow>& Row : NewRows)
		{
			if (const FFicsitRecipeRateMap* InUse = Snapshot.RecipeOutput.Find(Row->Item))
			{
				for (const auto& Pair : *InUse) { Row->InUse.Add(Pair.Key.Get()); }
			}
		}
	}

	// 2) Same layout: copy the values into the existing rows (no flashing). Otherwise rebuild the widgets.
	if (Signature == RowSignature && NewRows.Num() == Rows.Num())
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			*Rows[Index] = *NewRows[Index];
		}
		return;
	}
	RowSignature = Signature;
	Rows = MoveTemp(NewRows);
	RowList->ClearChildren();

	if (!Root)
	{
		RowList->AddSlot().AutoHeight().Padding(16.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChainEmpty", "Open any item's details and choose Show supply chain."))
			.Font(FicsitStatsStyle::Font(FontSize::Small))
			.ColorAndOpacity(FicsitStatsStyle::TextSecondary())
			.AutoWrapText(true)
		];
		return;
	}
	for (const TSharedRef<FRow>& Row : Rows)
	{
		RowList->AddSlot().AutoHeight()[BuildRow(Row)];
	}
}

TSharedRef<SWidget> SFicsitSupplyChainView::BuildRow(const TSharedRef<FRow>& Row)
{
	const TSubclassOf<UFGItemDescriptor> Item = Row->Item;
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
	const bool bLeafLike = Row->Kind == FSC::ENodeKind::Repeat || Row->Kind == FSC::ENodeKind::TooDeep;

	return SNew(SBox)
		.Padding(0.f, 0.f, 4.f, 3.f)
		[
			SNew(SFicsitClickBox)
			.Color(Row->Depth == 0 ? FicsitStatsStyle::TabActive() : FicsitStatsStyle::RowTile())
			.HoverColor(FicsitStatsStyle::RowTileHover())
			.OnClicked_Lambda([this, Row]() { if (Row->bHasChildren) { ToggleExpanded(Row); } })
			.OnRightClicked_Lambda([this, Item]() { OnShowDetails.ExecuteIfBound(Item); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(6.f, 3.f, 12.f, 3.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox).WidthOverride(Row->Depth * ChainDetail::IndentPerLevel)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(18.f)
						.HeightOverride(18.f)
						.Visibility(Row->bHasChildren ? EVisibility::HitTestInvisible : EVisibility::Hidden)
						[
							SNew(SFicsitDisclosureGlyph)
							.Expanded_Lambda([Row]() { return Row->bExpanded; })
							.Color(FicsitStatsStyle::TextSecondary())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(30.f)
						.HeightOverride(30.f)
						[
							SNew(SImage)
							.Image(FicsitStatsStyle::ItemIcon(Item))
							.ColorAndOpacity(bLeafLike ? FLinearColor(1.f, 1.f, 1.f, 0.5f) : FLinearColor::White)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(10.f, 0.f, 8.f, 0.f)
					[
						ChainDetail::MakeText(UFGItemDescriptor::GetItemName(Item), FontSize::Body,
							FSlateColor(bLeafLike ? FicsitStatsStyle::TextSecondary() : FicsitStatsStyle::TextPrimary()), Row->Depth == 0)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						Number(TAttribute<FText>::CreateLambda([Row]() { return Row->NeededText; }), ChainDetail::NeededColumn, FSlateColor(FicsitStatsStyle::TextPrimary()))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						Number(TAttribute<FText>::CreateLambda([Row]() { return Row->MakingText; }), ChainDetail::MakingColumn,
							TAttribute<FSlateColor>::CreateLambda([Row]() { return FSlateColor(Row->StatusColor); }))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(16.f, 0.f, 0.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(ChainDetail::RecipeColumn)
						[
							BuildRecipeCell(Row)
						]
					]
				]
				// How much of the need today's production covers.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(3.f)
					.Visibility(bLeafLike ? EVisibility::Hidden : EVisibility::HitTestInvisible)
					[
						SNew(SBorder)
						.BorderImage(FicsitStatsStyle::White())
						.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.25f))
						.Padding(0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(TAttribute<float>::CreateLambda([Row]() { return FMath::Clamp(Row->Fraction, 0.f, 1.f); }))
							[
								SNew(SBorder)
								.BorderImage(FicsitStatsStyle::White())
								.BorderBackgroundColor_Lambda([Row]() { return Row->StatusColor; })
							]
							+ SHorizontalBox::Slot()
							.FillWidth(TAttribute<float>::CreateLambda([Row]() { return 1.f - FMath::Clamp(Row->Fraction, 0.f, 1.f); }))
							[
								SNew(SSpacer)
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SFicsitSupplyChainView::BuildRecipeCell(const TSharedRef<FRow>& Row)
{
	const bool bPickable = Row->bChosen || Row->Options.Num() >= 2 || (Row->Kind == FSC::ENodeKind::Raw && Row->Options.Num() > 0);
	const TAttribute<FText> Text = TAttribute<FText>::CreateLambda([Row]() { return Row->RecipeText; });
	const TAttribute<FText> Tooltip = TAttribute<FText>::CreateLambda([Row]() { return Row->RecipeTooltip; });
	const FLinearColor TextColor = Row->Kind == FSC::ENodeKind::NoRecipe ? FicsitStatsStyle::Warning() : FicsitStatsStyle::TextSecondary();

	if (!bPickable)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f)
			.ToolTipText(Tooltip)
			[
				ChainDetail::MakeText(Text, FontSize::Small, FSlateColor(TextColor))
			];
	}

	// A dropdown like the game's: flat button with an arrow, list of recipes below it.
	const TSharedRef<TWeakPtr<SMenuAnchor>> AnchorRef = MakeShared<TWeakPtr<SMenuAnchor>>();
	TSharedPtr<SMenuAnchor> Anchor;
	SAssignNew(Anchor, SMenuAnchor)
	.Placement(MenuPlacement_ComboBox)
	.OnGetMenuContent(FOnGetContent::CreateLambda([this, Row, AnchorRef]() { return BuildRecipeMenu(Row, *AnchorRef); }))
	[
		SNew(SFicsitClickBox)
		.Color(Row->bChosen ? FLinearColor(FicsitStatsStyle::Orange().R, FicsitStatsStyle::Orange().G, FicsitStatsStyle::Orange().B, 0.35f) : FLinearColor(1.f, 1.f, 1.f, 0.06f))
		.HoverColor(FicsitStatsStyle::TabInactive())
		.Padding(FMargin(8.f, 4.f))
		.ToolTipText(Tooltip)
		.OnClicked_Lambda([AnchorRef]()
		{
			if (const TSharedPtr<SMenuAnchor> Pinned = AnchorRef->Pin())
			{
				Pinned->SetIsOpen(!Pinned->IsOpen());
			}
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				ChainDetail::MakeText(Text, FontSize::Small, FSlateColor(Row->bChosen ? FicsitStatsStyle::TextPrimary() : TextColor))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(9.f)
				.HeightOverride(6.f)
				[
					SNew(SFicsitTrendGlyph)
					.Direction(-1)
					.Color(FicsitStatsStyle::TextSecondary())
				]
			]
		]
	];
	*AnchorRef = Anchor;
	return Anchor.ToSharedRef();
}

TSharedRef<SWidget> SFicsitSupplyChainView::BuildRecipeMenu(const TSharedRef<FRow>& Row, TWeakPtr<SMenuAnchor> Anchor)
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TSubclassOf<UFGItemDescriptor> Item = Row->Item;
	const FSC::FRecipe Current = Row->bChosen && Tree.Nodes.IsValidIndex(Row->Node) && Tree.Nodes[Row->Node].Recipes.Num() > 0
		? Tree.Nodes[Row->Node].Recipes[0].Recipe : FSC::FRecipe();

	auto AddOption = [this, &List, Anchor, Item](const FText& Title, const FText& Caption, bool bSelected, FSC::FRecipe Recipe)
	{
		List->AddSlot().AutoHeight()
		[
			SNew(SFicsitClickBox)
			.Color(bSelected ? FicsitStatsStyle::Orange() : FLinearColor::Transparent)
			.HoverColor(bSelected ? FicsitStatsStyle::Orange() : FicsitStatsStyle::RowTileHover())
			.Padding(FMargin(10.f, 5.f))
			.OnClicked_Lambda([this, Anchor, Item, Recipe]()
			{
				if (const TSharedPtr<SMenuAnchor> Pinned = Anchor.Pin())
				{
					Pinned->SetIsOpen(false);
				}
				Pick(Item, Recipe);
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ChainDetail::MakeText(Title, FontSize::Small, FSlateColor(FicsitStatsStyle::TextPrimary()))]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ChainDetail::MakeText(Caption, FontSize::Caption, FSlateColor(bSelected ? FLinearColor::White : FicsitStatsStyle::TextSecondary()))
				]
			]
		];
	};

	// First choice: don't override, follow what the machines run.
	const bool bResource = Item && Item->IsChildOf(UFGResourceDescriptor::StaticClass());
	AddOption(bResource ? LOCTEXT("ChainFollowRaw", "Mine it (raw resource)") : LOCTEXT("ChainFollow", "Follow the factory"),
		Row->InUse.Num() > 0 ? LOCTEXT("ChainFollowInUse", "Use the recipes your machines run now") : LOCTEXT("ChainFollowDefault", "Use the standard recipe until something makes it"),
		!Row->bChosen, FSC::FRecipe());

	for (const FSC::FRecipe& Recipe : Row->Options)
	{
		FText Title = UFGRecipe::GetRecipeName(Recipe);
		if (Row->InUse.Contains(Recipe.Get()))
		{
			Title = FText::Format(LOCTEXT("ChainInUse", "{0}  (in use)"), Title);
		}
		AddOption(Title, ChainDetail::DescribeRecipe(Recipe, Item, WorldContext.Get()), Row->bChosen && Recipe == Current, Recipe);
	}

	return SNew(SBorder)
		.BorderImage(FicsitStatsStyle::White())
		.BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.07f, 0.98f))
		.Padding(2.f)
		[
			SNew(SBox)
			.MinDesiredWidth(ChainDetail::RecipeColumn + 120.f)
			.MaxDesiredHeight(420.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					List
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
