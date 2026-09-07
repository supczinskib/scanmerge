#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in
  ""|--integration) ;;
  *) echo 'Usage: ./tests/run.sh [--integration]' >&2; exit 2 ;;
esac
mkdir -p build/swift-cache
clang++ -std=c++17 -O3 -arch arm64 -mmacosx-version-min=13.0 \
  -Wno-deprecated-declarations -Iinclude -Isrc -Ivendor \
  tests/core.cpp -framework Accelerate -o build/test-core
./build/test-core
xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx13.0 \
  -module-cache-path build/swift-cache gui/Localization.swift gui/TerminalIntegration.swift \
  tests/terminal-integration.swift -o build/test-terminal
./build/test-terminal
xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx13.0 \
  -module-cache-path build/swift-cache gui/Localization.swift tests/localization-tests.swift -o build/test-localization
./build/test-localization -AppleLanguages '(pl)'
./build/test-localization -AppleLanguages '(en)'
./build/test-localization -AppleLanguages '(de, pl)'
if [ "${1:-}" = --integration ]; then
  : "${SCANMERGE_TEST_RGE:?Set SCANMERGE_TEST_RGE to a compatible .rge file}"
  : "${SCANMERGE_TEST_INPUT:?Set SCANMERGE_TEST_INPUT to a multi-view project directory}"
  ./build-gui.sh
  ./tests/integration.sh
  xcrun swiftc -swift-version 5 -O -D GUI_TESTS -target arm64-apple-macosx13.0 \
    -module-cache-path build/swift-cache gui/Localization.swift gui/TerminalIntegration.swift \
    gui/ScanMergeGUI.swift tests/gui-controller.swift -o build/test-controller
  ./build/test-controller -AppleLanguages '(en)'
  ./build/test-controller -AppleLanguages '(pl)'
fi
