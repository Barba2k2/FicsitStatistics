#pragma once

#include "CoreMinimal.h"

/**
 * What an item's rates mean: its demand, whether it is balanced, and how long stock lasts or a delivery takes. The
 * stats window, its details pane, the HUD and the history's net series all use these, so they always agree.
 * Pure: rates in (items/min, m³/min for fluids), answers out.
 */
namespace FicsitBalance
{
	enum class EState : uint8
	{
		/** Nothing made, used or sunk. */
		Idle,
		/** Net within measurement noise. */
		Balanced,
		Deficit,
		Surplus,
	};

	/** Rates below this show as nothing (a dash). */
	constexpr float IdleRate = 0.005f;

	/**
	 * Demand: everything consumed except what went into the AWESOME Sink. The Sink only takes overflow, so it is surplus,
	 * not demand, unless the player set the option to count it as demand.
	 */
	inline float Demand(float Consumed, float Sunk, bool bSinkIsDemand)
	{
		return FMath::Max(0.f, Consumed - (bSinkIsDemand ? 0.f : Sunk));
	}

	/** Net rates within 2% of the flow (at least 0.1 /min) are measurement noise, not a real deficit or surplus. */
	inline float NoiseThreshold(float Produced, float Demand)
	{
		return FMath::Max(0.1f, 0.02f * FMath::Max(Produced, Demand));
	}

	inline bool IsBalanced(float Produced, float Demand)
	{
		return FMath::Abs(Produced - Demand) <= NoiseThreshold(Produced, Demand);
	}

	inline EState GetState(float Produced, float Demand, float Sunk = 0.f)
	{
		if (Produced <= IdleRate && Demand <= IdleRate && Sunk <= IdleRate)
		{
			return EState::Idle;
		}
		if (IsBalanced(Produced, Demand))
		{
			return EState::Balanced;
		}
		return Produced < Demand ? EState::Deficit : EState::Surplus;
	}

	/**
	 * Items per minute free for a delivery (the Space Elevator, or the HUB by hand): production minus every other use.
	 * What already goes into the Space Elevator (ToElevator) doesn't count against it, and the Sink only takes overflow
	 * unless it counts as demand. Consumed includes both.
	 */
	inline float DeliverySupply(float Produced, float Consumed, float Sunk, float ToElevator, bool bSinkIsDemand)
	{
		const float OtherUses = FMath::Max(0.f, Demand(Consumed, Sunk, bSinkIsDemand) - ToElevator);
		return FMath::Max(0.f, Produced - OtherUses);
	}

	/** Minutes to deliver Remaining at Supply per minute, or a negative value if nothing is being made. */
	inline float MinutesToDeliver(float Remaining, float Supply)
	{
		return Supply > 0.01f ? Remaining / Supply : -1.f;
	}
}
