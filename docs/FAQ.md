# How FICSIT Statistics actually works

This is a FAQ about what's going on under the hood, mostly how the production and consumption rates get measured and what ends up in your save. The README has the short version, and this is the long one. If you just want to use the mod you don't need any of this, but if you're curious (or you're poking at the code), here you go :)

## The big picture

### Okay so how does the mod actually figure out rates?

There are two separate things going on, and they work together:

1. **The scan.** Every few seconds we look at every machine and work out what it *should* be doing from its recipe, clock speed, and productivity.
2. **The hooks.** We hook into the game's own functions so that every time a machine actually takes ingredients or finishes a cycle, we count it.

The scan gives you the theoretical side (max rate, efficiency, machine states), and the hooks give you what really happened. The "right now" rate starts out as the scan's estimate, and then switches over to the real counts once we've counted enough for that item.

### How often does all this run?

- The scan runs every 5 seconds by default. You can change that in the settings (anywhere from 1 to 60 seconds), and it picks up the new value without reloading the save.
- The 1m range gets a fresh point every second.
- The 5s range updates every frame, but only while you're actually looking at it, since it costs about as much as the scan does.

## The scan

### What does the scan look at?

It does one pass over every buildable in the world and sorts out the manufacturers, extractors, generators, storage, and anything with power info. For each item it fills in two numbers per source:

- **Max**, which is the rate at 100% productivity
- **Actual**, which is an estimate of the current rate

Here's how each type gets worked out:

- **Manufacturers:** max is `amount per cycle × 60 / cycle time`. Cycle time already includes clock speed, outputs get multiplied by the Somersloop boost, and ingredients include the session's parts-cost multiplier if you changed it. Actual is max × the game's productivity number, and it's 0 if the machine is paused.
- **Extractors:** max is `items per cycle × 60 / cycle time`, where items per cycle already accounts for node purity. Actual works the same way as manufacturers. On the first scan we also compare our number against the game's own `GetExtractionPerMinute()` and log a warning if they don't match.
- **Fuel generators:** fuel burn is `power capacity (MW) × 60 / energy per unit of fuel`, scaled by how loaded the generator is. Water for coal and nuclear plants comes straight from the game's current water rate, and nuclear waste is the fuel rate × waste per rod.

The scan also notes the slowest cycle time among all the machines that make or use each item. That number gets used later to decide how long the rate estimator has to wait before trusting its counts.

### Why do generators only use the scan?

Because there's no hook for fuel burning. Manufacturers and extractors have nice clean "cycle finished" and "took ingredients" functions we can hook, but generators don't, so their rates are always the scan's estimate. The good news is that the estimate matches how the game actually burns fuel (in proportion to the power drawn), so it lines up pretty well.

## The hooks

### Wait, are the hooks something Coffee Stain made?

Not really. The functions we hook are Coffee Stain's (they're just normal game code, like `Factory_ProductionCycleCompleted` and `Factory_ConsumeIngredients`), but they weren't built as hook points for mods. Nothing in the base game calls out to anyone when they run.

The hooking itself comes from SML (Satisfactory Mod Loader), which is the community's mod framework. The `SUBSCRIBE_METHOD...` macros in [FicsitStatisticsHooks.cpp](../Source/FicsitStatistics/Private/FicsitStatisticsHooks.cpp) ask SML to find each function's compiled code in the game and patch in a detour, so that our code runs right before or right after the original.

So we're not adding anything new to the game. We're pretty much splicing our code into calls the game was already making. A few things fall out of that:

- Nothing changes on disk. The patch happens in memory when the mod loads.
- Virtual functions need a sample object (that's what the `GetDefault<...>()` calls are for) so that SML can find the right vtable entry.
- Resource-well extractors override the "cycle finished" function, so their calls never hit the base version. That's why they get their own hook.
- The "after" hooks can see return values, which is how we know whether the Sink actually accepted an item or how much the Space Elevator actually took.
- Hooks only work in the packaged game, not the editor, since the editor build doesn't have the game's compiled functions to patch.
- A game update can break them. If Coffee Stain renames a function, changes its arguments, or inlines it, the hook stops matching and the mod needs a rebuild against the new headers.

### What gets counted?

| Hook | What we count |
|---|---|
| `Factory_ProductionCycleCompleted` | Produced. The recipe's products × Somersloop boost for manufacturers, or the items per cycle for extractors. |
| `Factory_ConsumeIngredients` | Consumed. The recipe's ingredients, with the parts-cost multiplier. |
| `AddPoints_ThreadSafe` (AWESOME Sink) | Consumed. 1 item for every item the Sink accepts. |
| `PayOffOnTargetGamePhase` (Space Elevator) | Consumed. However many items actually got paid in. |
| Craft Bench and Equipment Workshop | Crafted and used in crafting (only the server's call counts, since clients run it too) |
| Build gun | Spent building. We snapshot your inventory and the Dimensional Depot before and after, and count what left. |
| Dismantling | Refunded. Same idea, but we count what showed up. |

The building one is kind of neat, because measuring what actually left your inventory means blueprints, zoops, and upgrades (where the refund shows up in the same call) all just work, and failed builds or No Build Cost count as nothing.

Every count goes into three buffers at once:

- one that gets emptied every scan, for the rate estimator and the saved history
- one that gets emptied every second, for the 1m range
- a list of timestamped events, for the 5s range

### Is consumption counted again when a machine finishes a cycle?

Nope, it's only counted once. Ingredients get counted when the cycle **starts** (that's when the machine takes them), and the "cycle finished" hook only counts output. Nothing gets counted twice.

The thing to know is that this means consumption runs about one cycle ahead of production for any given machine:

- When a machine starts up, its ingredients show up right away, but its first output doesn't show up until one cycle later.
- When a machine stops in the middle of a cycle (paused, lost power, etc.), the ingredients for that cycle are already counted. That's actually correct though, since those items really did leave the input buffer.
- On short ranges (mostly 5s, and 1m with slow recipes), an item's consumption and production can look a little out of step because of this.

The rate estimator mostly hides it, since its windows are always at least 2 cycles long and production and consumption share one window. On the 10m and longer ranges it just disappears into the average.

## The "right now" rate

### How does the "right now" rate work?

This is the rate estimator ([FicsitRateEstimator.cpp](../Source/FicsitStatistics/Private/FicsitRateEstimator.cpp)), and it's what you see in the item rows, the details pane, the HUD, and the ETAs.

Every scan, whatever the hooks counted since the last scan becomes one "bucket", and we keep up to 10 minutes of buckets. Each item gets its own window over those buckets:

- As long as nothing changes, the window keeps growing, up to 10 minutes. A longer window means a steadier number, which matters a ton for slow recipes that only finish a cycle every minute or two.
- If something does change (a machine got built, stopped, ran out of inputs, etc.), the window starts over so that the new rate shows up within seconds instead of slowly creeping there over 10 minutes.

### How does it know something changed?

It compares a short recent window against the long one. Essentially:

1. Pick a **short window** for the item. It's the biggest of 10 seconds, 2× the item's slowest cycle, and however long it takes to see about 12 events at the current rate. That gets capped at 120 seconds (unless the cycle-based minimum is longer) and rounded up to whole buckets.
2. Once the long window is at least twice as long as the short one, work out how much we'd *expect* to see in the short window if the long window's rate was still right.
3. Compare that to what we actually saw.
4. If they're off by more than 3× the normal noise, start the window over at the beginning of the short window.

The noise part is the important bit. We treat the noise as Poisson in *events* (not items), where each event carries the item's average amount. So `noise = sqrt(expected / event size) × event size`. The reason we do it that way is because some machines deliver in big chunks (an oil extractor dropping a bunch of oil at once, for example), and if we treated every unit as its own event, those chunks would look like the rate was jumping around all the time.

There's also a floor of 2 event sizes on the noise, and if an item had zero events before, 2 recent events count as a change (that's the "something just started" case).

### Why don't production and consumption jitter against each other?

Because they share one window per item. If either direction changes, both start over together. Otherwise you'd have production averaged over 10 minutes and consumption averaged over 20 seconds, and the net value would bounce around for no real reason.

### What do I see right after loading a save?

The scan's estimate (`max × productivity`). That's the warm-up value, and it stays there until the item's window covers its short window. After that the real counted rate (`amount in window × 60 / window length`) takes over.

The max rate always comes from the scan though, even after warm-up, since the hooks only tell us what happened, not what a machine could do at 100%.

### What if I dismantle a machine?

The scan won't see it anymore, but its counts are still in the window, so its flow keeps showing up until it ages out. That's on purpose, because those items really did get made.

### What about the AWESOME Sink and Space Elevator?

Those only exist as counts (there's nothing to scan), so their rate is just `amount in window × 60 / time covered`, using the same window as the item's production so that the Sink's share lines up with what you're making. Fluids get converted from liters to m³ here too, since the Sink counts raw amounts. Max is just set to the same number as actual.

## Time ranges

### What do the 5s, 1m, 10m, etc. ranges show?

None of them use the "right now" rate. They're all averages of raw counts:

- **5s** puts the timestamped events into tiny 1/60 second points over a 5 second span, so every finished cycle pops up as a spike the moment it happens. It's host and single player only, because clients don't see the hook events.
- **1m** has one point per second, made straight from that second's counts. These get sent to clients as they happen. Generator rates come from the latest scan.
- **10m and longer** come from the saved history. Every scan, we take what the hooks counted in that scan and turn it into a rate over the real time it covered (plus the scan's numbers for generators and power). So each range's average is the actual average over exactly that range.

All rates are per minute, and fluids show up in m³ because the game stores them in liters internally and we convert them.

## The save

### What actually gets saved?

Only the history behind the 10m and longer ranges. The 1m and 5s ranges and the rate estimator's buckets aren't saved, which is why the "right now" rate goes through warm-up again after a load.

The saved history has five tiers, and each one is a ring buffer (once it's full, the oldest point gets overwritten):

| Tier | Each point covers | Points | Total span |
|---|---|---|---|
| 0 | 5 s | 120 | 10 min |
| 1 | 30 s | 120 | 1 h |
| 2 | 5 min | 120 | 10 h |
| 3 | 25 min | 120 | 50 h |
| 4 | 2 h | 500 | 1000 h |

Every tier gets fed directly from every scan, so they're not rolled up from each other. They're the same data, just in wider buckets. Each tier also saves the point it's in the middle of building, so a half-finished point survives a save and reload.

### What's in one point?

For each item:

- **Produced and consumed:** the average per-minute rate over that point's time. Only the totals are kept, not the breakdown by source.
- **Sunk:** how much of consumed went into the AWESOME Sink.
- **Crafted, used in crafting, built, and refunded:** the actual amounts in that time. These get added up, not averaged.
- **Power:** average MW for the whole factory and for each building type.

Max isn't saved at all, so the history ranges show the average for both.

### So after an hour of playing, what's in there?

- **Tier 0:** the last 10 minutes, as 120 five-second points. Everything older got overwritten.
- **Tier 1:** the whole hour, as 120 thirty-second points.
- **Tier 2:** 12 five-minute points.
- **Tier 3:** 2 full points, plus 10 minutes in the one it's building.
- **Tier 4:** no finished points yet, just an hour in the one it's building.

The 1h range is just the average of tier 1's 120 points. Since every point covers exactly 30 seconds, that's the same thing as the total amount over the hour divided by 60 minutes.

### So is it storing counts or averages?

Technically averages, but they're built from the exact counts over exact time spans, so you can turn them right back into counts with `rate × interval / 60`. For example, a 30 second point that says 120/min means 60 items got made in those 30 seconds (give or take float rounding).

Long story short, it's pretty much counts stored as rates, with two exceptions:

1. **Generator stuff** (fuel, water, and nuclear waste) comes from the scan's estimate, so those points are averages of estimates, not counts.
2. **Long hitches.** The rate uses the real time that passed, but a single scan only gets credited with up to 3 scan intervals of history time. So after a big hitch or pause the rate is still right, but the history gives it less time than actually passed, and some of the amount effectively gets dropped. That's on purpose so that one giant pause doesn't wreck your averages, but it does mean the history isn't a perfect running total.

## Performance

### Is this going to tank my frame rate?

It really shouldn't. On my test save (338 machines and 5,616 buildables), the scan takes about 0.7 ms, and it only runs once every 5 seconds. The hooks handle around 75 counting events per second on that same save, and each one is just a quick lock and a couple of map updates.

### What's actually running all the time?

- **The scan**, once every scan interval (5 s by default). It's one pass over the buildable list with cheap class checks, so it doesn't walk the list again for each machine type. If you've got a giant factory and want to cut the cost even more, you can raise the scan interval in the settings.
- **The hooks**, whenever a machine takes ingredients or finishes a cycle, the Sink accepts an item, etc. These are tiny.
- **The rate estimator**, once per scan. It just adds up counts that are already in memory.
- **The 1m range**, once per second. Same deal, it just turns that second's counts into a point.

### What only runs while I'm looking at it?

The 5s range. It redraws every frame and costs about as much as the scan does, so it only runs while a view is actually showing it. Once nothing has shown it for 2 seconds it stops, and its events just get thrown away so that the list stays short. The next time you open it, it fills back in over 5 seconds.

There's also a safety cap of 1,000,000 pending events, so if nothing ever empties that list for some reason, it can't just grow forever.

### What about multiplayer?

The server (or host) does all the math, and clients just get the results:

- The latest scan's numbers get replicated every scan.
- The 1m range's points get sent as they happen. The server keeps the last 3 seconds in there, so a client that misses an update still gets every second.
- The saved history only gets sent when a client opens a range that needs it, and it comes over in chunks instead of all at once.

The 5s range is host and single player only, since clients don't see the hook events.

### Will it bloat my save?

Nope, not really. Only the history behind the 10m and longer ranges gets saved, and every tier is a fixed-size ring buffer, so it can't grow forever no matter how long you play. Once a tier is full, each new point just overwrites the oldest one.

The most it'll ever hold is 980 points (120 each for the first four tiers, and 500 for the last one). The first four tiers are full after about 50 hours of play, and after that the only thing still growing is the 1000-hour tier, which adds one point every 2 hours.

Each point is pretty much just a couple of float arrays (one value per item for produced and consumed, plus a handful of power numbers), so the size mostly depends on how many different items your factory has touched. Rough math: with around 100 items, a point is about a kilobyte or two, so even a completely full history is somewhere around a megabyte or two before the game compresses the save. I haven't measured that on a real save though, so take it as a ballpark.

None of the other stuff gets saved (the 1m and 5s ranges, the rate estimator's windows, the latest scan, etc.). That's all rebuilt after you load.

### How do I check the numbers on my own save?

There's a once-a-minute summary in the log that includes the scan time (average and max), how many buildables and machines it looked at, and how many counting events the hooks handled in the last minute. It logs at Verbose so that it doesn't clog up everyone's logs, so you'll need to turn it on first. Either add this to `%LOCALAPPDATA%\FactoryGame\Saved\Config\Windows\Engine.ini`:

```ini
[Core.Log]
LogFicsitStatistics=Verbose
```

or launch the game with `-LogCmds="LogFicsitStatistics Verbose"`. Then look for the `LogFicsitStatistics: Performance` line.

## Where to look in the code

- [FicsitStatisticsHooks.cpp](../Source/FicsitStatistics/Private/FicsitStatisticsHooks.cpp) has the hooks and the counting buffers.
- [FicsitStatisticsSubsystem.cpp](../Source/FicsitStatistics/Private/FicsitStatisticsSubsystem.cpp) has the scan (`TakeSample` and the `Scan...` functions) and the 1m updates (`TakeLiveSample`).
- [FicsitRateEstimator.cpp](../Source/FicsitStatistics/Private/FicsitRateEstimator.cpp) has the "right now" rate.
- [FicsitRealtimeHistory.cpp](../Source/FicsitStatistics/Private/FicsitRealtimeHistory.cpp) has the 5s range.
- [FicsitStatisticsHistory.cpp](../Source/FicsitStatistics/Private/FicsitStatisticsHistory.cpp) has the saved history tiers and the range averages.
