#include "Misc/AutomationTest.h"
#include "Resources/FGConsumableDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "UI/FicsitStatsWindowModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitGraphSelectionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// A and B are on the graph by default, C isn't.
	UClass* A() { return UFGConsumableDescriptor::StaticClass(); }
	UClass* B() { return UFGEquipmentDescriptor::StaticClass(); }
	UClass* C() { return UFGItemDescriptor::StaticClass(); }
	TSet<UClass*> Defaults() { return {A(), B()}; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitGraphSelectionToggleTest, "FicsitStatistics.GraphSelection.ToggleFlipsAndToggleBackIsDefault", FicsitGraphSelectionTests::Flags)
bool FFicsitGraphSelectionToggleTest::RunTest(const FString& Parameters)
{
	using namespace FicsitGraphSelectionTests;
	FFicsitGraphSelection Selection;
	TestTrue(TEXT("a default is on"), Selection.IsOn(A(), Defaults()));
	TestFalse(TEXT("a non-default is off"), Selection.IsOn(C(), Defaults()));
	TestFalse(TEXT("untouched is not customized"), Selection.IsCustomized(Defaults()));

	Selection.Toggle(A(), Defaults());
	Selection.Toggle(C(), Defaults());
	TestFalse(TEXT("toggled off"), Selection.IsOn(A(), Defaults()));
	TestTrue(TEXT("toggled on"), Selection.IsOn(C(), Defaults()));
	TestTrue(TEXT("toggled is customized"), Selection.IsCustomized(Defaults()));

	Selection.Toggle(A(), Defaults());
	Selection.Toggle(C(), Defaults());
	TestFalse(TEXT("toggled back is not customized"), Selection.IsCustomized(Defaults()));

	// C toggled on, then the defaults move so that C is a default anyway: the graph is what the defaults show.
	Selection.Toggle(C(), Defaults());
	TestFalse(TEXT("an on-toggle that became a default is not customized"), Selection.IsCustomized({A(), B(), C()}));

	Selection.Reset();
	Selection.Toggle(B(), Defaults());
	Selection.Reset();
	TestTrue(TEXT("reset brings the default back"), Selection.IsOn(B(), Defaults()));
	TestFalse(TEXT("reset is not customized"), Selection.IsCustomized(Defaults()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitGraphSelectionSoloTest, "FicsitStatistics.GraphSelection.SoloShowsOneItemAndSoloAgainUndoes", FicsitGraphSelectionTests::Flags)
bool FFicsitGraphSelectionSoloTest::RunTest(const FString& Parameters)
{
	using namespace FicsitGraphSelectionTests;
	FFicsitGraphSelection Selection;
	Selection.Toggle(C(), Defaults());

	Selection.Solo(B());
	TestEqual(TEXT("the soloed item"), Selection.GetSolo(), B());
	TestTrue(TEXT("the soloed item is on"), Selection.IsOn(B(), Defaults()));
	TestFalse(TEXT("another default is off"), Selection.IsOn(A(), Defaults()));
	TestFalse(TEXT("solo clears earlier toggles"), Selection.IsOn(C(), Defaults()));
	TestTrue(TEXT("solo is customized"), Selection.IsCustomized(Defaults()));

	Selection.Solo(B());
	TestNull(TEXT("solo again: no solo"), Selection.GetSolo());
	TestTrue(TEXT("solo again: defaults back"), Selection.IsOn(A(), Defaults()));
	TestFalse(TEXT("solo again: not customized"), Selection.IsCustomized(Defaults()));

	// Solo, then toggle more items back on: the solo is now a starting point, so Ctrl+LMB solos it afresh.
	Selection.Solo(B());
	Selection.Toggle(A(), Defaults());
	TestTrue(TEXT("toggled on after solo"), Selection.IsOn(A(), Defaults()));
	TestFalse(TEXT("a non-default stays off after solo"), Selection.IsOn(C(), Defaults()));
	Selection.Solo(B());
	TestEqual(TEXT("solo after toggles solos again"), Selection.GetSolo(), B());
	TestFalse(TEXT("and drops the toggles"), Selection.IsOn(A(), Defaults()));

	Selection.Solo(C());
	TestTrue(TEXT("solo another item: it's on"), Selection.IsOn(C(), Defaults()));
	TestFalse(TEXT("solo another item: the old one is off"), Selection.IsOn(B(), Defaults()));
	return true;
}

#endif
