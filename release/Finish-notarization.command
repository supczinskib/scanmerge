#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
WORK=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$WORK"
if [[ ! -f submission.json || ! -f profile.txt || ! -d ScanMerge.app ]]; then
  echo 'Run this script from the release directory created on the Desktop.'; exit 1
fi
PROFILE=$(cat profile.txt)
SUBMISSION_ID=$(/usr/bin/plutil -extract id raw -o - submission.json)
echo "Apple submission: $SUBMISSION_ID"
echo 'Waiting for Apple (up to 20 minutes; you can resume afterward).'
set +e
/usr/bin/xcrun notarytool wait "$SUBMISSION_ID" --keychain-profile "$PROFILE" --timeout 20m --output-format json > status.json
WAIT_STATUS=$?
set -e
STATUS=$(/usr/bin/plutil -extract status raw -o - status.json 2>/dev/null || true)
if [[ "$STATUS" != Accepted ]]; then
  if [[ "$STATUS" == Invalid || "$STATUS" == Rejected ]]; then
    /usr/bin/xcrun notarytool log "$SUBMISSION_ID" --keychain-profile "$PROFILE" apple-log.json || true
    echo 'Apple did not accept the package. See apple-log.json.'
  else
    echo "No final acceptance yet (tool exit code: $WAIT_STATUS). The submission is saved."
    echo 'Run Finish-notarization.command again later. Do not resubmit the application.'
  fi
  exit 1
fi
/usr/bin/xcrun stapler staple ScanMerge.app
/usr/bin/xcrun stapler validate ScanMerge.app
/usr/bin/codesign --verify --deep --strict ScanMerge.app
/usr/sbin/spctl --assess --type execute --verbose=2 ScanMerge.app
VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' ScanMerge.app/Contents/Info.plist)
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid application version.'; exit 1; }
TEMPZIP="ScanMerge-$VERSION-macOS-arm64.zip"
/usr/bin/ditto -c -k --keepParent ScanMerge.app "$TEMPZIP"
echo
echo "READY. File to distribute: $WORK/$TEMPZIP"
echo 'Do not modify the application after signing and notarization.'
