# QGroundControl — Non-GPS Edition

**Ground control station for autonomous drone flight without GNSS.**
An unofficial research fork of [QGroundControl](https://github.com/mavlink/qgroundcontrol),
built around a local grid in metres instead of a world map.

> [!WARNING]
> **Research preview — not flight validated.** This build runs and has been exercised against
> ArduPilot SITL, but it has not yet been tested in the field on real hardware. Do not use it to
> fly an aircraft you cannot afford to lose. See the [roadmap](docs/nongps/roadmap.md) for where
> the project actually is.

<!-- TODO: tambahkan screenshot Local Grid View di sini — README dengan gambar jauh lebih menarik. -->

## Why this fork exists

Conventional drone navigation depends entirely on GNSS. Indoors, under canopy, in urban canyons,
or under jamming, GNSS is unavailable or untrustworthy. This project flies missions using modern
dead reckoning instead — optical flow, rangefinder, IMU and compass fused by ArduPilot's EKF3 —
with missions expressed as **points in metres relative to home**, not latitude/longitude.

A ground station built around a world map cannot express that. So the fly view was rebuilt around
a **local grid**: a cartesian plane in metres, annotated in compass degrees, North = 0°.

The research goal is to characterise position drift: return-to-home error after a known pattern,
normalised per metre travelled. GNSS stays on the aircraft purely as ground truth, never as a
navigation source.

## What is different from upstream QGroundControl

| Area | What was added |
|---|---|
| **Local Grid View** | A fly view in metres rather than on a map — plan, fly and monitor without any map tiles |
| **Planning on the grid** | Place waypoints by bearing and distance, set per-leg speed, start with takeoff and land where the pattern ends; send, save and clear plans |
| **Non-GNSS status panel** | Optical flow health judged against the EKF's own limit, rangefinder height, estimator aiding status and why it stopped |
| **Estimator origin** | Set and correct the EKF origin without a map; tell the vehicle where it actually stands, and surface a missing origin before it costs a flight |
| **Optical flow calibration** | Run flow scale calibration from the fly view, with the message rate raised while calibrating |
| **Arming diagnostics** | Read ArduPilot's arming refusal under the name it actually sends, and show why the vehicle will not arm wherever the operator is looking |
| **Airspeed** | Read the airspeed sensor's own numbers rather than the estimator's |

Roughly 80 commits and 16,000 lines across `src/FlyView`, `src/PlanView` and `src/Vehicle`,
with unit and integration tests alongside.

## Documentation

- [Project brief](docs/nongps/project-brief.md) — vision, scientific background, scope *(Bahasa Indonesia)*
- [Roadmap](docs/nongps/roadmap.md) — 6 phases, deliverables, risks, current status *(Bahasa Indonesia)*
- [Upstream QGC README](README.upstream.md) — the original project's documentation

## Building

Verified on macOS (Apple Silicon) with Qt 6.11.1, CMake 3.25+ and Ninja.
For other platforms, follow the [upstream build instructions](https://docs.qgroundcontrol.com/master/en/qgc-dev-guide/getting_started/).

```bash
git clone --recursive https://github.com/and0789/qgroundcontrol.git
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```bash
cmake --build build
```

**macOS note.** Upstream defaults to a universal binary (`x86_64h;arm64`). If your Qt is
arm64-only — Homebrew's `qt` is — the x86_64h slice has no Qt to link against and the build
fails with `symbol(s) not found for architecture x86_64h`. Configure with
`-DQGC_MACOS_UNIVERSAL_BUILD=OFF` to build for your host architecture only.

## Branch model

This repository tracks upstream continuously rather than diverging from it.

| Branch | Role |
|---|---|
| `master` | A clean mirror of `mavlink/qgroundcontrol`. Never committed to directly, only fast-forwarded |
| `main` | This project: upstream plus the non-GNSS work. **Default branch** |
| `feat/*` | Work in progress, merged into `main` |

Keeping `master` pristine means upstream can always be merged without conflict archaeology, and
the exact difference from stock QGroundControl stays one `git diff` away.

## Relationship to upstream

This is a derivative work, not a competitor. Changes here that are not specific to GNSS-denied
flight — arming refusal reporting, airspeed sourcing — belong upstream, and are intended to be
submitted there as individual pull requests.

QGroundControl is developed by the [Dronecode Foundation](https://www.dronecode.org/) and its
contributors. This fork is **not affiliated with, endorsed by, or supported by** the
QGroundControl project. Please report issues with this fork here, not to upstream.

## License

Same as upstream QGroundControl: dual-licensed under
[Apache 2.0](LICENSE-APACHE) and [GPL v3](LICENSE-GPL).
Upstream copyright notices are retained in full.

## Acknowledgements

Built on the work of the QGroundControl and ArduPilot communities. This fork exists because they
made their work open — the same reason its changes are open in turn.
