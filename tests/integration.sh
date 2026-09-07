#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
: "${SCANMERGE_TEST_RGE:?Set SCANMERGE_TEST_RGE to a compatible .rge file}"
: "${SCANMERGE_TEST_INPUT:?Set SCANMERGE_TEST_INPUT to a multi-view project directory}"
test -f "$SCANMERGE_TEST_RGE"
test -d "$SCANMERGE_TEST_INPUT"
mkdir -p build
APP="$ROOT/dist/ScanMerge.app/Contents/MacOS/scanmerge"
TEMP=$(mktemp -d /tmp/scanmerge-integration-XXXXXX)
trap 'rm -rf "$TEMP"' EXIT
# Use temporary copies for native export tests.
mkdir "$TEMP/input" "$TEMP/nonempty"
cp "$SCANMERGE_TEST_RGE" "$TEMP/input/view.rge"
cat > "$TEMP/input/capture.fix_prj" <<'XML'
<PROJECT><SYSTEM><NAME>capture.fix_prj</NAME><SCANNER>SE2</SCANNER><TEX>false</TEX><FRAMES>1</FRAMES><POINTS>0</POINTS><TRIANGLES>0</TRIANGLES><SCAN_ALIGN_TYPE>turntable</SCAN_ALIGN_TYPE><GENERATE_CLOUD>0</GENERATE_CLOUD></SYSTEM><SCENE><PATH>./</PATH><DATA><MESH><NAME>view</NAME><ERR>0.125</ERR><GROUP_NAME>Group1</GROUP_NAME></MESH></DATA></SCENE></PROJECT>
XML
cat > "$TEMP/input/capture.sln_fix" <<'XML'
<solution><version>0.1</version><point_dis>0.25</point_dis><scan_mode>4</scan_mode><haveTexture>0</haveTexture><projects><project><path>capture.fix_prj</path><groupId>42</groupId><dirty>1</dirty></project></projects></solution>
XML
printf preserve > "$TEMP/nonempty/sentinel"
if "$APP" "$TEMP/input" "$TEMP/nonempty" > "$TEMP/reject.log" 2>&1; then exit 1; fi
test "$(cat "$TEMP/nonempty/sentinel")" = preserve
if "$APP" "$TEMP/input" "$TEMP/input/out" > "$TEMP/inside.log" 2>&1; then exit 1; fi
test ! -e "$TEMP/input/out"
# Verify a relocated bundle with system tools only.
cp -R "$ROOT/dist/ScanMerge.app" "$TEMP/ScanMerge.app"
PATH=/usr/bin:/bin "$TEMP/ScanMerge.app/Contents/MacOS/scanmerge" "$TEMP/input" "$TEMP/one-output" --json-progress > "$TEMP/one.log"
test -f "$TEMP/one-output/EXScanS/merged.sln_fix"
test ! -e "$TEMP/one-output/merged.ply"
# Native names must survive export: EXScanS derives filenames during later stages.
test -f "$TEMP/one-output/EXScanS/capture.fix_prj"
test -f "$TEMP/one-output/EXScanS/view.rge"
test ! -e "$TEMP/one-output/EXScanS/view_0.rge"
test "$(/usr/bin/xmllint --xpath 'string(/solution/projects/project/path)' "$TEMP/one-output/EXScanS/merged.sln_fix")" = capture.fix_prj
test "$(/usr/bin/xmllint --xpath 'string(/solution/point_dis)' "$TEMP/one-output/EXScanS/merged.sln_fix")" = 0.25
test "$(/usr/bin/xmllint --xpath 'string(/solution/projects/project/groupId)' "$TEMP/one-output/EXScanS/merged.sln_fix")" = 42
test "$(/usr/bin/xmllint --xpath 'string(/PROJECT/SCENE/DATA/MESH/NAME)' "$TEMP/one-output/EXScanS/capture.fix_prj")" = view
test "$(/usr/bin/xmllint --xpath 'string(/PROJECT/SCENE/DATA/MESH/ERR)' "$TEMP/one-output/EXScanS/capture.fix_prj")" = 0.125
printf 'PASS: native project/capture names, solution metadata and capture attributes preserved\n'

/usr/bin/plutil -lint "$TEMP/ScanMerge.app/Contents/Info.plist"
cp "$TEMP/one-output/report.json" "$ROOT/build/single-frame-report.json"
cp "$TEMP/one.log" "$ROOT/build/single-frame-progress.jsonl"
printf 'PASS: no overwrite; output/input separation; one frame; native RGE roundtrip; relocated bundle; no optional PLY\n'
# Interrupt a real run; the private staging directory must be removed.
"$APP" "$SCANMERGE_TEST_INPUT" "$TEMP/cancelled-output" > "$TEMP/cancel.log" 2>&1 &
PID=$!
sleep 0.3
kill -INT "$PID"
set +e
wait "$PID"
STATUS=$?
set -e
test "$STATUS" -eq 130
test ! -e "$TEMP/cancelled-output"
test -z "$(find "$TEMP" -maxdepth 1 -name '.scanmerge-work-*' -print)"
printf 'PASS: cooperative cancellation leaves no published or temporary output\n'
