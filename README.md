# ScanMerge

<img src="assets/AppIcon.png" width="96" height="96" alt="ScanMerge icon">

[Polski](README_PL.md)

ScanMerge aligns EXScanS captures on Apple Silicon Macs and saves the result as an EXScanS project. An optional PLY export provides a single point cloud for CloudCompare and other 3D applications.

- Automatic alignment across multiple scan positions and turntable captures.
- Preservation of all measured points and the original scale.
- Desktop application with progress reporting and cancellation.
- Command-line interface for batch processing and integration.
- Polish interface for Polish system language settings; English otherwise.

## Requirements

- Apple Silicon Mac running macOS 13 or later.
- EXScanS **3.2.0.4 ARM64**, installed in `/Applications/EXScanS.app`.

ScanMerge supports a specific build of the EXScanS native library and checks compatibility before processing. Other library builds are rejected. See [Native adapter](docs/DEVELOPMENT.md#native-adapter) for technical details.

## Getting started

Download `ScanMerge-1.0.0-macOS-arm64.zip` from [Releases](https://github.com/supczinskib/scanmerge/releases).

1. Extract the application archive and copy `ScanMerge.app` to Applications.
2. Open ScanMerge and select the directory containing your scans.
3. Choose where to save the result and enter a new directory name.
4. Enable **Also export a merged PLY point cloud** if required.
5. Click **Align scans**.

Open `EXScanS/merged.sln_fix` from the output directory in EXScanS. Keep the entire `EXScanS` directory together: the solution file references the accompanying project and capture files.

### First launch on macOS

Version 1.0.0 uses an ad hoc signature and is not Developer ID signed or notarized by Apple. If macOS blocks the application, first attempt to open it, then go to **System Settings → Privacy & Security → Open Anyway** and confirm. See [Apple’s instructions](https://support.apple.com/guide/mac-help/mh40616/mac).

## Command line

In the application's Terminal section, click **Enable Terminal command**, then open a new Terminal window:

```sh
scanmerge "/path/to/scans" "/path/to/result" --ply
```

Omit `--ply` to save only the EXScanS project and diagnostic reports. The output directory must be new or empty and separate from the input. Source files are preserved. Press Ctrl-C to cancel.

The command is also available directly inside the application:

```sh
/Applications/ScanMerge.app/Contents/MacOS/scanmerge "/path/to/scans" "/path/to/result"
```

Terminal integration creates `~/.local/bin/scanmerge` and adds it to the shell search path. After moving the application, use **Refresh Terminal command**. Use **Disable Terminal command** to remove the integration.

Run `scanmerge --help` for all options. For automation, see [CLI integration](docs/DEVELOPMENT.md#cli-integration).

## Supported files

The input directory must contain a `.sln_fix` solution and its referenced `.fix_prj` and `.rge` files, or a set of `.fix_prj` projects with their captures. Capture counts and initial poses are read from the project data, including the RGE files. There is no fixed number of turntable steps.

| Output | Description |
| --- | --- |
| `EXScanS/merged.sln_fix` | Solution to open in EXScanS |
| `EXScanS/*.fix_prj` and `EXScanS/*.rge` | Required project and capture data |
| `merged.ply` | Optional merged point cloud with normals, in millimetres |
| JSON reports and `progress.jsonl` | Alignment diagnostics, transforms and processing times |

The EXScanS project preserves individual captures with their updated positions. The PLY contains the combined point cloud. Reports are optional for viewing the result.

## Alignment

ScanMerge adjusts capture rotation and translation while retaining the original observations. It checks exported RGE data and project references before completing the output.

Captures must show overlapping parts of the same object. Limited overlap, symmetry and repeated features can make alignment ambiguous. Alignment preserves scanner measurement noise; surface reconstruction and mesh editing are separate processing steps. Review the result before further processing. Running Global Optimization in EXScanS may change the alignment.

## Development

Build with Xcode and the macOS SDK:

```sh
./build-gui.sh
```

The application is written to `dist/ScanMerge.app`. Use `./build.sh` to build only the engine and command-line tool.

- [Architecture and integration](docs/DEVELOPMENT.md)
- [Tests](docs/TESTING.md)
- [Optional Developer ID signing and notarization](release/README.md)
- [Publishing a GitHub release](docs/PUBLISHING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

ScanMerge is an independent project and is not affiliated with SHINING 3D.

## License

ScanMerge source code is licensed under the [GNU General Public License v3.0](LICENSE) (`GPL-3.0-only`). Third-party components are covered by their respective licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
