#include "FicsitBalance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitBalanceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitBalanceDemandTest, "FicsitStatistics.Balance.SinkIsOverflowUnlessCountedAsDemand", FicsitBalanceTests::Flags)
bool FFicsitBalanceDemandTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Sink overflow isn't demand"), FicsitBalance::Demand(100.f, 30.f, false), 70.f);
	TestEqual(TEXT("unless the option says so"), FicsitBalance::Demand(100.f, 30.f, true), 100.f);
	TestEqual(TEXT("never negative"), FicsitBalance::Demand(10.f, 10.5f, false), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitBalanceStateTest, "FicsitStatistics.Balance.StateUsesTwoPercentNoiseWithAFloor", FicsitBalanceTests::Flags)
bool FFicsitBalanceStateTest::RunTest(const FString& Parameters)
{
	using FicsitBalance::EState;
	TestTrue(TEXT("nothing moving is idle"), FicsitBalance::GetState(0.f, 0.f) == EState::Idle);
	TestTrue(TEXT("only sinking is not idle"), FicsitBalance::GetState(0.f, 0.f, 5.f) == EState::Balanced);
	TestTrue(TEXT("within 2% is balanced"), FicsitBalance::GetState(100.f, 98.5f) == EState::Balanced);
	TestTrue(TEXT("past 2% is a deficit"), FicsitBalance::GetState(100.f, 103.f) == EState::Deficit);
	TestTrue(TEXT("past 2% is a surplus"), FicsitBalance::GetState(103.f, 100.f) == EState::Surplus);
	// Small flows: the 0.1 /min floor keeps 1.0 vs 1.05 balanced although it's 5%.
	TestTrue(TEXT("0.1 /min floor"), FicsitBalance::GetState(1.f, 1.05f) == EState::Balanced);
	TestTrue(TEXT("past the floor"), FicsitBalance::GetState(1.f, 1.2f) == EState::Deficit);
	TestEqual(TEXT("threshold is 2% of the larger side"), FicsitBalance::NoiseThreshold(50.f, 200.f), 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitBalanceDeliveryTest, "FicsitStatistics.Balance.DeliverySupplyIsWhatOtherUsesLeave", FicsitBalanceTests::Flags)
bool FFicsitBalanceDeliveryTest::RunTest(const FString& Parameters)
{
	// 100 made, 60 used: 20 by machines, 30 into the Space Elevator, 10 sunk. Free for the elevator: 100 - 20 = 80.
	TestEqual(TEXT("elevator flow and Sink overflow count as supply"), FicsitBalance::DeliverySupply(100.f, 60.f, 10.f, 30.f, false), 80.f);
	TestEqual(TEXT("Sink as demand takes its share"), FicsitBalance::DeliverySupply(100.f, 60.f, 10.f, 30.f, true), 70.f);
	// The HUB is paid by hand, so nothing flows to it: everything used elsewhere is taken.
	TestEqual(TEXT("HUB supply"), FicsitBalance::DeliverySupply(100.f, 60.f, 10.f, 0.f, false), 50.f);
	TestEqual(TEXT("never negative"), FicsitBalance::DeliverySupply(10.f, 60.f, 0.f, 0.f, false), 0.f);

	TestEqual(TEXT("1,000 left at 50 /min"), FicsitBalance::MinutesToDeliver(1000.f, 50.f), 20.f);
	TestTrue(TEXT("not being made"), FicsitBalance::MinutesToDeliver(1000.f, 0.f) < 0.f);
	return true;
}

#endif
