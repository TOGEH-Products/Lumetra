# Update checks and release packages

## Current behavior

Update checks use the public GitHub Releases API for the single configured repository and the stable `/releases/latest` endpoint. Prereleases and draft releases are rejected. Versions are compared using semantic-version precedence. Automatic checks are enabled by default and are attempted no more than once per 24 hours, but remain disabled until a release owner and repository are configured; Settings also offers a manual check and records the last successful check.

The network requests run on a worker thread with bounded WinHTTP timeouts. Network and metadata errors do not stop normal application use. The update notice can be dismissed for the current session. Release notes are available from the notice and are remembered after **Got It**. If the installed version changes, Lumetra requests notes for that exact stable version and shows them once.

## Configure the official repository

The canonical public repository has not yet been selected. Update checking and downloads are disabled when the release owner or repository is empty. Once decided, configure both at CMake configure time:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DLUMETRA_GITHUB_OWNER=YOUR_OWNER `
  -DLUMETRA_GITHUB_REPO=YOUR_REPOSITORY
```

These are public identifiers, not credentials. Do not add a token or secret to the application. The owner and repository values are restricted to URL-safe name characters.

## Expected release package

The updater accepts only a package named `Lumetra-<semantic-version>-win-x64.zip`, served from the configured repository's GitHub Releases URL. The release API must expose an SHA-256 asset digest and exact package size. The download is limited to 500 MiB, must be a ZIP file, and is written under the current user's `Downloads\Lumetra` folder. Interrupted or invalid downloads are discarded.

SHA-256 detects transfer/file corruption; it does **not** authenticate the publisher when both the package and digest come from an unsigned release. The current build does not verify Authenticode or another trusted publisher signature. Therefore the application never extracts, executes, replaces, installs, elevates, backs up, or rolls back a downloaded package. Users must independently verify and manually handle any package. Do not advertise automatic installation until a publisher identity and tested signature-verification policy exist.

## Test coverage and release prerequisites

`LumetraUpdateTests` covers semantic-version precedence, malformed versions, stable/prerelease metadata, release URL allowlisting, matching release notes, and missing/invalid SHA-256 metadata. It is run by Windows CI with CTest.

| Scenario | Current coverage/status |
|---|---|
| Current version / newer stable / prerelease | Version and stable-metadata unit tests |
| Offline, timeout, API failure | Bounded WinHTTP path; requires integration testing against a configured repository/network |
| Invalid release metadata or unexpected asset | Metadata validation unit tests |
| Interrupted download or invalid checksum | Partial files are discarded; requires integration testing |
| Invalid signature | No signature verifier is configured; package is never executed |
| Successful installation or rollback | Not implemented; blocked on trusted signing and a separately reviewed installer/updater design |
| First launch after updating / repeated notes | Registry state tracks the last run and notes acknowledged; exercise with signed test releases before enabling production update distribution |

Before enabling public update checks, confirm the official repository, publish the exact asset naming/digest convention, and test the full HTTP flow from a clean Windows installation.
