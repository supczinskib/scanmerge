# Testing

## Synthetic tests

Run from the repository root on an Apple Silicon Mac with Xcode:

```sh
./tests/run.sh
```

This checks nearest-neighbour search against brute force, rigid transform inversion, point-to-plane fitting, joint registration for several capture counts, and cancellation. Terminal integration tests use a temporary home directory and check enable/disable, relocation, spaces, file permissions, idempotence, conflicting files and preservation of later edits.

Localization tests check both catalogs, format placeholders and Polish/English selection, including an unsupported primary language with Polish as a secondary preference.

No scan dataset or EXScanS installation is required for these tests. They do not alter the user's shell profile or open application windows.

## Native integration tests

Install the compatible EXScanS build and provide local scan data:

```sh
export SCANMERGE_TEST_RGE="/path/to/project/capture.rge"
export SCANMERGE_TEST_INPUT="/path/to/multi-view-project"
./tests/run.sh --integration
unset SCANMERGE_TEST_RGE SCANMERGE_TEST_INPUT
```

The RGE must be a valid, nonempty capture with normals and enough registration samples. The project directory must contain enough data for processing to still be active when the cancellation test interrupts it after a short delay. A tiny dataset that completes first is unsuitable for that test.

Tests copy the selected capture to a temporary project. They check native roundtrip export, preservation of project/capture filenames, solution metadata and capture attributes, optional PLY, output collision rejection, input/output separation, relocation with a minimal PATH, cancellation cleanup and progress handling through the GUI controller. The EXScanS window, CloudCompare and the ScanMerge window are not opened. The controller test exercises program logic, not interactive UI controls.

Test data is not distributed. These tests do not establish registration quality for every object or compatibility with every scanner and macOS version. A native-library roundtrip does not replace opening an exported project in the target application.
