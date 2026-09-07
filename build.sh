#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
mkdir -p build dist/ScanMerge.app/Contents/MacOS
for SOURCE in native registration engine; do
  clang++ -std=c++17 -O3 -DNDEBUG -arch arm64 -mmacosx-version-min=13.0 -Wno-deprecated-declarations -Iinclude -Isrc -Ivendor -I"$SDK/usr/include/libxml2" -c "src/$SOURCE.cpp" -o "build/$SOURCE.o"
done
ar rcs build/libscanmerge.a build/native.o build/registration.o build/engine.o
clang++ -std=c++17 -O3 -DNDEBUG -arch arm64 -mmacosx-version-min=13.0 -Wno-deprecated-declarations -Iinclude -Isrc -Ivendor src/main.cpp build/libscanmerge.a -framework Accelerate -lxml2 -o dist/ScanMerge.app/Contents/MacOS/scanmerge
cp dist/ScanMerge.app/Contents/MacOS/scanmerge dist/ScanMerge.app/Contents/MacOS/scanmerge-rge
cat > dist/ScanMerge.app/Contents/Info.plist <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>scanmerge</string><key>CFBundleIdentifier</key><string>local.scanmerge.native</string><key>CFBundleName</key><string>ScanMerge</string><key>CFBundlePackageType</key><string>APPL</string><key>CFBundleShortVersionString</key><string>1.0.0</string><key>CFBundleVersion</key><string>1.0.0</string><key>CFBundleIconFile</key><string>AppIcon</string><key>CFBundleDevelopmentRegion</key><string>en</string><key>CFBundleLocalizations</key><array><string>en</string><string>pl</string></array><key>LSMinimumSystemVersion</key><string>13.0</string></dict></plist>
PLIST
codesign --force --sign - dist/ScanMerge.app/Contents/MacOS/scanmerge-rge
codesign --force --sign - dist/ScanMerge.app
cat > dist/scanmerge <<'LAUNCHER'
#!/bin/sh
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$BASE/ScanMerge.app/Contents/MacOS/scanmerge" "$@"
LAUNCHER
chmod +x dist/scanmerge
mkdir -p dist/ScanMerge.app/Contents/Resources
cp LICENSE THIRD_PARTY_NOTICES.md vendor/JSON-LICENSE.txt dist/ScanMerge.app/Contents/Resources/
cp -R gui/Resources/en.lproj gui/Resources/pl.lproj dist/ScanMerge.app/Contents/Resources/
mkdir -p build/swift-cache
xcrun swiftc -O -module-cache-path build/swift-cache tools/build-icon.swift -o build/build-icon
./build/build-icon assets/AppIconArtwork.png build/AppIcon.iconset
cp build/AppIcon.icns dist/ScanMerge.app/Contents/Resources/AppIcon.icns
cp build/AppIcon.iconset/icon_512x512@2x.png dist/ScanMerge.app/Contents/Resources/AppIcon.png
codesign --force --sign - dist/ScanMerge.app
