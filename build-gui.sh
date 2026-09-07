#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")"
APP=dist/ScanMerge.app
./build.sh
mkdir -p build/swift-cache
xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx13.0 \
  -module-cache-path build/swift-cache gui/Localization.swift gui/TerminalIntegration.swift gui/ScanMergeGUI.swift \
  -o "$APP/Contents/MacOS/ScanMergeGUI"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable ScanMergeGUI' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 1.0.0' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName ScanMerge' "$APP/Contents/Info.plist"
codesign --force --sign - "$APP/Contents/MacOS/scanmerge"
codesign --force --sign - "$APP/Contents/MacOS/ScanMergeGUI"
codesign --force --sign - "$APP"
