# Publishing a release

Release 1.0.0 consists of the `v1.0.0` source tag and `ScanMerge-1.0.0-macOS-arm64.zip`. The application uses an ad hoc signature, without Developer ID signing or Apple notarization.

## Build and package

From the repository root:

```sh
./build-gui.sh
./release/package.sh
```

The package contains the application, installation instructions, license and third-party notices. Scan data and signing credentials are excluded.

## GitHub setup

Authenticate with GitHub CLI and select the repository:

```sh
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git --hostname github.com
REPO="supczinskib/scanmerge"
```

For a new repository, create it from the local source checkout:

```sh
gh repo create "$REPO" --public --source . --remote origin --push \
  --description "EXScanS capture alignment for Apple Silicon macOS, with native project and merged PLY export."
```

For an existing repository with `origin` configured, push the source branch:

```sh
git push -u origin main
```

Set the repository description and topics:

```sh
gh repo edit "$REPO" \
  --description "EXScanS capture alignment for Apple Silicon macOS, with native project and merged PLY export." \
  --add-topic "3d-scanning,einscan,exscans,point-cloud,point-cloud-registration,scan-alignment,macos,apple-silicon,swiftui,cpp"
```

## Publish 1.0.0

The local `v1.0.0` tag must identify the release source commit. Publish it, then attach the application package and release notes:

```sh
git push origin v1.0.0
gh release create v1.0.0 dist/ScanMerge-1.0.0-macOS-arm64.zip \
  --repo "$REPO" \
  --verify-tag \
  --title "1.0.0" \
  --notes-file release/1.0.0.md \
  --latest
gh release view v1.0.0 --repo "$REPO" --web
```

GitHub provides ZIP and TAR.GZ source archives for the same tag. Application binaries are release assets and stay outside Git history. Future releases should use a new version and tag.
