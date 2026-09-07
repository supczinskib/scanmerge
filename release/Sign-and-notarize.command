#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE="$BASE/../dist/ScanMerge.app"
if [[ ! -d "$SOURCE" ]]; then SOURCE="$BASE/ScanMerge.app"; fi
if [[ ! -d "$SOURCE" ]]; then echo 'ScanMerge.app was not found. Place it beside this script.'; exit 1; fi
IDENTITIES=()
while IFS= read -r LINE; do IDENTITIES+=("$LINE"); done < <(/usr/bin/security find-identity -v -p codesigning | /usr/bin/awk '/Developer ID Application:/')
if [[ ${#IDENTITIES[@]} -eq 0 ]]; then
  echo 'No valid Developer ID Application identity with a private key was found.'
  echo 'Create or import a Developer ID Application certificate in Xcode > Settings > Accounts > Manage Certificates.'
  exit 1
fi
CHOICE=1
if [[ ${#IDENTITIES[@]} -gt 1 ]]; then
  echo 'Select a signing identity:'
  for ((I=0; I<${#IDENTITIES[@]}; I++)); do printf '%d: %s\n' "$((I+1))" "${IDENTITIES[$I]}"; done
  read -r -p 'Number: ' CHOICE
  [[ "$CHOICE" =~ ^[0-9]+$ ]] || exit 1
  ((CHOICE>=1 && CHOICE<=${#IDENTITIES[@]})) || exit 1
fi
LINE=${IDENTITIES[$((CHOICE-1))]}
IDENTITY=$(printf '%s\n' "$LINE" | /usr/bin/awk '{print $2}')
TEAM=$(printf '%s\n' "$LINE" | /usr/bin/sed -nE 's/.*\(([A-Z0-9]{10})\)".*/\1/p')
[[ "$IDENTITY" =~ ^[A-Fa-f0-9]{40}$ && "$TEAM" =~ ^[A-Z0-9]{10}$ ]] || { echo 'Could not read the signing identity.'; exit 1; }
PROFILE="ScanMerge-$TEAM"
echo "Signing identity: $LINE"
echo 'Set up notarization access. Enter the password directly in the Apple tool.'
echo 'Use an app-specific password from account.apple.com, not your account password.'
read -r -p 'Apple Account email: ' APPLE_ACCOUNT
[[ -n "$APPLE_ACCOUNT" ]] || exit 1
/usr/bin/xcrun notarytool store-credentials "$PROFILE" --apple-id "$APPLE_ACCOUNT" --team-id "$TEAM"
WORK=$(/usr/bin/mktemp -d "$HOME/Desktop/ScanMerge-release-XXXXXX")
echo "Preparing an application copy: $WORK"
/usr/bin/ditto "$SOURCE" "$WORK/ScanMerge.app"
printf '%s\n' "$PROFILE" > "$WORK/profile.txt"
cp "$BASE/Finish-notarization.command" "$WORK/Finish-notarization.command"
APP="$WORK/ScanMerge.app"
# Only the isolated RGE adapter loads Shining3D's external library.
/usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" --entitlements "$BASE/rge-entitlements.plist" "$APP/Contents/MacOS/scanmerge-rge"
/usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP/Contents/MacOS/scanmerge"
/usr/bin/codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"
/usr/bin/ditto -c -k --keepParent "$APP" "$WORK/notarization-upload.zip"
echo 'Submitting only the application bundle to Apple. Scan data is not included.'
/usr/bin/xcrun notarytool submit "$WORK/notarization-upload.zip" --keychain-profile "$PROFILE" --output-format json > "$WORK/submission.json"
"$WORK/Finish-notarization.command"
