#include "FicsitSupplyChain.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitSupplyChainTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// Stand-ins for real items, since the editor only has header stubs for FactoryGame.
	UClass* Ore() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* Coal() { return UFGConsumableDescriptor::StaticClass(); }
	UClass* Part() { return UFGItemDescriptor::StaticClass(); }

	void AddNode(FicsitSupplyChain::FTree& Tree, UClass* Item, FicsitSupplyChain::ENodeKind Kind, float PerRoot, float Produced = 0.f)
	{
		FicsitSupplyChain::FNode& Node = Tree.Nodes.AddDefaulted_GetRef();
		Node.Item = Item;
		Node.Kind = Kind;
		Node.PerRoot = PerRoot;
		Node.Produced = Produced;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitSupplyChainRawNeedsTest, "FicsitStatistics.SupplyChain.RawNeedsSumEveryRawLeaf", FicsitSupplyChainTests::Flags)
bool FFicsitSupplyChainRawNeedsTest::RunTest(const FString& Parameters)
{
	using namespace FicsitSupplyChainTests;
	using FicsitSupplyChain::ENodeKind;

	// A part made from ore in two branches, coal in one, plus a loop and a branch too deep to follow.
	FicsitSupplyChain::FTree Tree;
	AddNode(Tree, Part(), ENodeKind::Made, 1.f);
	AddNode(Tree, Ore(), ENodeKind::Raw, 3.f, 120.f);
	AddNode(Tree, Coal(), ENodeKind::Raw, 2.f, 45.f);
	AddNode(Tree, Ore(), ENodeKind::Raw, 4.5f, 120.f);
	AddNode(Tree, Part(), ENodeKind::Repeat, 0.5f);
	AddNode(Tree, Coal(), ENodeKind::TooDeep, 10.f);

	const TArray<FicsitSupplyChain::FRawNeed> Needs = FicsitSupplyChain::RawNeeds(Tree);
	if (!TestEqual(TEXT("one entry per raw item"), Needs.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("most needed first"), Needs[0].Item == Ore());
	TestEqual(TEXT("ore is summed over both branches"), Needs[0].PerRoot, 7.5f);
	TestEqual(TEXT("extraction is the factory's, not summed"), Needs[0].Produced, 120.f);
	TestTrue(TEXT("coal second"), Needs[1].Item == Coal());
	TestEqual(TEXT("too-deep coal isn't counted as raw"), Needs[1].PerRoot, 2.f);

	TestEqual(TEXT("an empty tree needs nothing"), FicsitSupplyChain::RawNeeds(FicsitSupplyChain::FTree()).Num(), 0);
	return true;
}

#endif
