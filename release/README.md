# Developer ID signing and notarization

Release 1.0.0 is distributed with an ad hoc signature. This optional workflow adds Developer ID signing and Apple notarization to a future distribution.

## Requirements

- Xcode with the command-line tools.
- Active Apple Developer Program membership.
- A Developer ID Application certificate and its private key in Keychain.
- An app-specific password for the Apple Account.

## Sign an application

1. Build the application with `./build-gui.sh`, or use a matching prebuilt application.
2. In Xcode Settings → Accounts, select your team, then Manage Certificates → **+ → Developer ID Application**.
3. Create an app-specific password at [account.apple.com](https://account.apple.com/) under Sign-In and Security.
4. Start the signing script. Select the signing identity when prompted, enter the Apple Account email, and enter the app-specific password at the Apple tool's password prompt.

From a source checkout:

```sh
./release/Sign-and-notarize.command
```

From a signing package containing the application and scripts together:

```sh
./Sign-and-notarize.command
```

The script stores the notarization profile in Keychain, signs an application copy in a new `ScanMerge-release-…` directory on the Desktop, and submits it to Apple. The original application is preserved.

## Finish notarization

The script waits up to 20 minutes for Apple's response. After acceptance, it staples the ticket, verifies the signature and Gatekeeper assessment, and creates `ScanMerge-1.0.0-macOS-arm64.zip`.

If the submission is still pending, run `Finish-notarization.command` from the generated release directory. It resumes the existing submission. If Apple rejects the application, review `apple-log.json` in that directory.

Publish the final application ZIP with the matching source tag using the repository’s `docs/PUBLISHING.md` instructions. The temporary `notarization-upload.zip`, credentials and local logs are not release assets. Changes to the application after signing require a new signing and notarization run.

## Signing configuration

Local builds use an ad hoc signature. Distribution signing uses Developer ID, a secure timestamp and Hardened Runtime. The `scanmerge-rge` helper has the library-validation exception required to load the separately installed EXScanS library; the engine and GUI use the default validation settings.

The signing package contains both `.command` scripts and `rge-entitlements.plist`. The application bundle includes the GPL-3.0 license and third-party notices.

## References

- [Developer ID certificates](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/)
- [App-specific passwords](https://support.apple.com/en-us/102654)
- [Notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
