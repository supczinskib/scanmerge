# Development

## Layout

| Path | Responsibility |
| --- | --- |
| `src/geometry.hpp` | Vectors, transforms, spatial index and sampling |
| `src/registration.cpp` | Orientation search, registration and joint optimization |
| `src/native.cpp` | Project discovery, isolated RGE adapter and export |
| `src/engine.cpp` | Processing pipeline, cancellation and atomic output publication |
| `src/main.cpp` | CLI arguments, signals and progress events |
| `include/scanmerge.hpp` | Public C++ API |
| `gui/` | SwiftUI frontend and optional shell integration |
| `tests/` | Synthetic and opt-in native integration tests |
| `release/` | Developer ID signing and notarization scripts |

`./build-gui.sh` rebuilds the engine before compiling the GUI, so source edits cannot leave a stale engine in the application bundle. Build scripts produce ARM64 binaries with macOS 13 as the deployment target. The application, CLI and report version are 1.0.0.

## CLI integration

Start `scanmerge IN OUT --json-progress`, optionally with `--ply`, using an argument array rather than shell interpolation. Drain standard output and standard error concurrently. Each stdout line is a JSON object with `event: "progress"` or `event: "result"`. Progress includes `stage`, `message`, `completed` and `total`; some stages have no meaningful total.

A result contains `project`, `report`, `points` and `seconds`. Treat the result as complete after the result event and a successful process exit. Use SIGINT to request cooperative cancellation. The CLI returns 130 after cancellation.

## C++ API

Link `build/libscanmerge.a` with Accelerate and libxml2. Place `scanmerge-rge` alongside the host executable.

```cpp
#include "scanmerge.hpp"
#include <atomic>

std::atomic_bool cancelled{false};
scanmerge::Options options{inputPath, outputPath, true};
auto result = scanmerge::run(options, progressCallback, &cancelled);
```

Call the blocking function from a worker thread. Dispatch copied progress events to the main thread before updating UI. The callback must not throw. Set `cancelled` to request cancellation; failure or cancellation removes temporary work rather than publishing a partial project.

## Registration engine

The C++ engine uses CPU parallelism, Apple Accelerate, system libxml2 and bundled nlohmann/json headers. Registration uses working samples, principal-axis orientation hypotheses, robust point-to-plane fitting and joint pose optimization. An FPFH-style fallback supplies additional orientation candidates. Export retains the original observations at full density.

Pairwise capture checks can grow quadratically with capture count. Validation covers synthetic geometry and selected scan datasets; performance of the orientation fallback across diverse objects remains unverified.

## Native adapter

The adapter is isolated in `scanmerge-rge` and depends on the exact ABI of one EXScanS 3.2.0.4 ARM64 library build. The expected SHA-256 in `src/native.cpp` is a compatibility identifier, not a credential. It uses an undocumented interface; EXScanS libraries are loaded from the installed application and are not bundled. Supporting another build requires validating the ABI, memory layout and RGE roundtrip checks before changing the checksum.

Output reports may contain absolute paths. Keep local scan data, reports, signing credentials and build products outside commits; the root `.gitignore` excludes the known output formats.

## Application icon

`assets/AppIconArtwork.png` is the square artwork. `tools/build-icon.swift` renders it inside a white rounded tile with a transparent exterior and consistent padding, at all standard icon sizes. The builder assembles the PNG representations into `AppIcon.icns`, and `build.sh` adds the icon to the application bundle. The same icon is shown in the window header. Building the icon requires only the system frameworks and Xcode tools.

## Localization

The GUI selects Polish only when the primary preferred system language starts with `pl`; all other primary languages use English. Translation catalogs live in `gui/Resources/en.lproj` and `pl.lproj`. `L10n` handles progress messages, dialogs, errors and Terminal integration messages. Restart the application after changing the system language. The CLI and release scripts remain in English.

## Source formatting

C++ formatting is defined in `.clang-format`. Swift formatting is defined in `.swift-format` and can be applied with Xcode's formatter:

```sh
xcrun swift-format format --in-place --recursive gui tools tests
```

Original source files use the `GPL-3.0-only` SPDX identifier. Preserve the licenses and notices in `vendor/` when updating dependencies.
