#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
APP=dist/ScanMerge.app
test -d "$APP" || { echo 'Build the application with ./build-gui.sh first.' >&2; exit 1; }
codesign --verify --deep --strict "$APP"
SIGNATURE=$(codesign -dv "$APP" 2>&1)
printf '%s\n' "$SIGNATURE" | grep -q '^Signature=adhoc$' || {
  echo 'This package workflow requires an ad hoc signed application.' >&2
  exit 1
}
VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")
test "$VERSION" = 1.0.0 || { echo 'Update the release notes and package documentation for this version.' >&2; exit 1; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/scanmerge-package-XXXXXX")
trap 'rm -rf "$WORK"' EXIT
PACKAGE="$WORK/ScanMerge-$VERSION"
mkdir -p "$PACKAGE/vendor"
ditto "$APP" "$PACKAGE/ScanMerge.app"
cp LICENSE THIRD_PARTY_NOTICES.md release/README.txt "$PACKAGE/"
cp vendor/JSON-LICENSE.txt "$PACKAGE/vendor/"
ARCHIVE="dist/ScanMerge-$VERSION-macOS-arm64.zip"
ditto -c -k --keepParent "$PACKAGE" "$WORK/application.zip"
mv "$WORK/application.zip" "$ARCHIVE"
printf 'Package: %s/%s\n' "$PWD" "$ARCHIVE"
