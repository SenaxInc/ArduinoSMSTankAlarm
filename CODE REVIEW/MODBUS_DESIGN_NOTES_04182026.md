# Modbus Design Notes

Date: 2026-04-18

Scope: Decision guidance for the client-side Modbus solar charger communication architecture.

Non-Modbus Compatibility Note:
- This document applies only when Modbus solar monitoring is enabled (`powerSource = solar_modbus_mppt`, `solarCharger.enabled = true`).
- The non-Modbus solar path remains supported (`powerSource = solar_mppt`), including SunKeeper-6/basic charger deployments.
- Non-Modbus mode does not require RS-485 hardware and intentionally provides reduced telemetry compared with SunSaver Modbus register data.

## Operating Constraints

- The solar charger will always be the only device on the Modbus bus.
- The design should favor low energy usage.
- Fast polling or low-latency response is not a priority.

## Architectural Impact

These constraints reduce the value of a multi-loop state machine.

### 1. Single Modbus Device

Because the solar charger is the only Modbus device on the bus:

- There is no need for bus arbitration or fairness scheduling.
- There is no need to interleave transactions among multiple slaves.
- There is no scalability benefit from building a more elaborate polling scheduler today.

This removes one of the main reasons to adopt a state-machine-driven Modbus architecture.

### 2. Low Energy Usage Matters More Than Responsiveness

For this system, energy usage is driven more by wakeups, transaction count, and time spent active than by raw polling speed.

That pushes the design toward:

- Fewer total Modbus transactions.
- Longer intervals between polls.
- Keeping active periods consolidated so the device can return to lower-power states sooner.

A multi-loop state machine often spreads work across more loop iterations. That can be useful for responsiveness, but it can also work against sleep consolidation if the device stays active just to advance one small polling state at a time.

### 3. Speed Is Not Important

If fast response is not required:

- Blocking batched reads are more acceptable.
- Slightly longer poll completion times are acceptable.
- Simpler code is preferable to a more flexible but more complex architecture.

This strongly favors a straightforward synchronous polling model, provided worst-case blocking remains within acceptable system limits.

## Recommendation

Use the current batched synchronous Modbus polling approach, not a multi-loop state machine.

Reasoning:

- It is simpler and easier to maintain.
- It matches the current one-device topology.
- It supports low-power operation better when paired with aggressive poll scheduling and sleep-aware timing.
- The recent batching work already reduced blocking time substantially without introducing state-machine complexity.

## Practical Design Priorities

Given these requirements, optimization effort should focus on power-aware scheduling rather than state-machine restructuring.

Recommended priorities:

1. Increase the solar poll interval as much as the application can tolerate.
2. Add or preserve communication-failure backoff so repeated failed polls do not waste energy.
3. Align solar polling with existing wake cycles when possible, instead of waking specifically for Modbus.
4. Keep register reads batched to minimize transaction count and active time.
5. Avoid immediate retries except for genuinely critical conditions.
6. Consider reading non-essential groups less often than core battery-health groups if additional savings are needed.

## When a Multi-Loop State Machine Would Make Sense Later

A multi-loop state machine becomes more attractive only if the assumptions change, for example:

- More Modbus devices are added.
- Tight responsiveness becomes important.
- Additional concurrent tasks make loop jitter unacceptable.
- Polling work becomes large enough that even batched synchronous reads are still too disruptive.

Under the current assumptions, those benefits do not justify the added complexity.

## Bottom Line

For a single Modbus solar charger, with low energy usage as a priority and speed as a non-priority, the better choice is:

- Keep the Modbus implementation simple.
- Use synchronous batched reads.
- Optimize poll frequency and wake behavior.
- Defer a multi-loop state machine unless future system requirements materially change.