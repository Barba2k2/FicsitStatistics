# Architecture

These diagrams show how the FicsitStatistics module fits together, starting from the module entry point in `FicsitStatistics.h`. Each box is a `.h`/`.cpp` pair (or a header-only file), and each arrow is an internal `#include`. Engine, FactoryGame, and SML includes are left out so that the mod's own structure is easier to see.

## Layer overview

```mermaid
flowchart TD
  E["Entry point<br/>FicsitStatistics.h / .cpp"]
  H["UMG hosts (Public/UI)<br/>FicsitStatsWindowWidget, FicsitStatsHUDWidget"]
  S["Slate views (Private/UI)<br/>SFicsitStatsWindow, SFicsitSupplyChainView,<br/>SFicsitGraph, SFicsitWidgets"]
  V["View model and UI helpers (Private/UI)<br/>FicsitStatsWindowModel, FicsitStatsTables, FicsitRangeReader,<br/>FicsitItemOutlook, FicsitPlayerPrefs, FicsitStatsStyle"]
  C["Runtime core<br/>FicsitStatisticsSubsystem ⇄ FicsitStatisticsNet"]
  D["Domain logic<br/>Hooks, History, RateEstimator, RealtimeHistory, StockTrend,<br/>SupplyChain, MilestoneNeeds, Balance, Config"]
  F["Foundation<br/>FicsitStatisticsTypes, LogFicsitStatistics"]

  E --> H
  E -->|installs hooks| D
  H --> S
  H --> V
  H --> D
  S --> V
  V --> C
  V --> D
  S --> D
  C --> D
  D --> F
  H -.->|HUD builds RangeReaders, reads snapshot| C
  S -.->|window builds RangeReaders, reads snapshot<br/>supply chain view reads snapshot only| C

  linkStyle 11,12 stroke:#e8590c,stroke-width:2px
```

## Full include graph

```mermaid
flowchart TD
  subgraph ENTRY["Entry point"]
    MOD["FicsitStatistics.h / .cpp<br/>FFicsitStatisticsModule"]
  end

  subgraph HOSTS["UMG hosts (Public/UI)"]
    WW["FicsitStatsWindowWidget<br/>toggle + Slate host"]
    HUD["FicsitStatsHUDWidget<br/>pinned items HUD, 727 lines"]
  end

  subgraph SLATE["Slate views (Private/UI)"]
    SWIN["SFicsitStatsWindow<br/>main window, 2,736 lines"]
    SSC["SFicsitSupplyChainView<br/>1,173 lines"]
    SG["SFicsitGraph"]
    SW["SFicsitWidgets<br/>header-only"]
  end

  subgraph VM["View model and UI helpers (Private/UI)"]
    WM["FicsitStatsWindowModel"]
    TBL["FicsitStatsTables"]
    RR["FicsitRangeReader"]
    IO["FicsitItemOutlook"]
    PP["FicsitPlayerPrefs"]
    STY["FicsitStatsStyle"]
  end

  subgraph CORE["Runtime core"]
    SUB["FicsitStatisticsSubsystem<br/>AModSubsystem, 983 lines"]
    NET["FicsitStatisticsNet<br/>RCO, net structs, SML game modules"]
  end

  subgraph DOMAIN["Domain logic"]
    HOOKS["FicsitStatisticsHooks<br/>native hooks, event counters"]
    RATE["FicsitRateEstimator"]
    RT["FicsitRealtimeHistory"]
    HIST["FicsitStatisticsHistory"]
    TREND["FicsitStockTrend"]
    SC["FicsitSupplyChain"]
    MN["FicsitMilestoneNeeds<br/>+ FicsitDepot"]
    BAL["FicsitBalance<br/>header-only"]
    CFG["FicsitStatisticsConfig"]
  end

  subgraph FOUND["Foundation"]
    TYPES["FicsitStatisticsTypes"]
    LOG["LogFicsitStatistics<br/>(declared in FicsitStatistics.h)"]
  end

  %% 0-5: log category only
  HOOKS & SUB & PP & HUD & WW & SWIN -.-> LOG

  %% 6-7: Subsystem and Net depend on each other
  SUB --> NET
  NET -.-> SUB

  %% 8-10: UI reaching straight into the subsystem
  HUD -.-> SUB
  SWIN -.-> SUB
  SSC -.-> SUB

  %% The view model's intended read path into the subsystem
  RR -.-> SUB

  %% Entry point
  MOD -.-> HOOKS & WW & HUD

  %% UMG hosts
  WW -.-> SWIN
  HUD -.-> CFG & BAL & IO & PP & RR & STY & SW

  %% Slate views
  SWIN --> WM & TBL & HIST & TYPES
  SWIN -.-> SSC & SG & SW & CFG & MN & BAL & PP & RR & STY
  SSC --> SC
  SSC -.-> PP & STY & SW
  SG -.-> STY
  SW --> STY

  %% View model and UI helpers
  WM --> IO & TBL & HIST & TYPES
  WM -.-> RR & BAL & STY
  TBL --> IO & BAL & HIST & TYPES
  TBL -.-> STY
  RR --> HIST & TYPES
  RR -.-> STY
  IO --> BAL & MN & TYPES
  IO -.-> RR & CFG
  STY -.-> TYPES

  %% Runtime core
  SUB --> HOOKS & RATE & RT & HIST & TREND & TYPES
  SUB -.-> SC & MN & CFG
  NET --> HIST & HOOKS & TYPES
  NET -.-> CFG

  %% Domain logic
  RATE --> HOOKS & TYPES
  RT --> HIST & HOOKS & TYPES
  HIST --> TYPES
  HIST -.-> BAL
  HOOKS --> TYPES
  HOOKS -.-> MN
  TREND --> TYPES
  SC --> TYPES

  linkStyle 0,1,2,3,4,5 stroke:#adb5bd,stroke-width:1px,stroke-dasharray:2 4
  linkStyle 6,7 stroke:#e03131,stroke-width:2.5px
  linkStyle 8,9,10 stroke:#e8590c,stroke-width:2px
```

### Legend

- Solid arrow: the header includes the other header, so the dependency leaks to everything that includes it.
- Dashed arrow: only the `.cpp` includes the other header.
- Red: `FicsitStatisticsSubsystem` and `FicsitStatisticsNet` depend on each other.
- Orange: UI code that includes `FicsitStatisticsSubsystem.h` directly. The `FFicsitRangeReader` constructor takes an `AFicsitStatisticsSubsystem&`, so the HUD and the main window have to get the subsystem just to build a reader, and they also call getters like `GetLatestSnapshot()` on it themselves. `SFicsitSupplyChainView` skips the view model completely and only reads `GetLatestSnapshot()`.
- Grey dotted: the file only includes `FicsitStatistics.h` for the `LogFicsitStatistics` log category.

Tests aren't in the graph. The 14 test files under `Private/Tests` include the domain logic and view model headers directly (History, Net, RangeReader, WindowModel, Tables, SupplyChain, etc.) and never touch the subsystem or the Slate views.
