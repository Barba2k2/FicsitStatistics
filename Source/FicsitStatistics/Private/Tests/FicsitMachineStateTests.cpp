#include "FicsitStatisticsTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FicsitMachineStateTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	using EState = EFicsitMachineState;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitMachineStatePassThroughTest, "FicsitStatistics.MachineState.OnlyIdleMachinesAreExplained", FicsitMachineStateTests::Flags)
bool FFicsitMachineStatePassThroughTest::RunTest(const FString& Parameters)
{
	using namespace FicsitMachineStateTests;
	for (const EState State : {EState::Running, EState::Paused, EState::Unpowered})
	{
		TestTrue(TEXT("product keeps the state"), FicsitMachineState::ForProduct(State, true) == State);
		TestTrue(TEXT("input keeps the state"), FicsitMachineState::ForInput(State, true, true) == State);
		TestTrue(TEXT("also without an output that can block"), FicsitMachineState::ForInput(State, false, false, false) == State);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitMachineStateProductTest, "FicsitStatistics.MachineState.ProductIsStarvedOrBlocked", FicsitMachineStateTests::Flags)
bool FFicsitMachineStateProductTest::RunTest(const FString& Parameters)
{
	using namespace FicsitMachineStateTests;
	TestTrue(TEXT("short of an input: starved"), FicsitMachineState::ForProduct(EState::Idle, true) == EState::Starved);
	TestTrue(TEXT("has its inputs: output backed up"), FicsitMachineState::ForProduct(EState::Idle, false) == EState::OutputBlocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFicsitMachineStateInputTest, "FicsitStatistics.MachineState.InputIsStarvedOtherInputsOrBlocked", FicsitMachineStateTests::Flags)
bool FFicsitMachineStateInputTest::RunTest(const FString& Parameters)
{
	using namespace FicsitMachineStateTests;
	TestTrue(TEXT("short of this item: starved"), FicsitMachineState::ForInput(EState::Idle, true, true) == EState::Starved);
	TestTrue(TEXT("short of another item: other inputs"), FicsitMachineState::ForInput(EState::Idle, false, true) == EState::OtherInputs);
	TestTrue(TEXT("has every input: output backed up"), FicsitMachineState::ForInput(EState::Idle, false, false) == EState::OutputBlocked);
	TestTrue(TEXT("no output that can fill (a coal generator): idle"), FicsitMachineState::ForInput(EState::Idle, false, false, false) == EState::Idle);
	TestTrue(TEXT("a generator out of fuel is still starved of it"), FicsitMachineState::ForInput(EState::Idle, true, true, false) == EState::Starved);
	return true;
}

#endif
