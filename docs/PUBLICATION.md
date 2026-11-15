# Public repository launch checklist

## Before creating the repository

- `TOGEH-Products` is the developer's personal developer/publisher identity, not a registered company. Copyright and executable metadata use that identity and do not represent a separate corporate entity.
- The developer confirms ownership and redistribution permission for Lumetra's original project materials, including `Icon.ico`, `Logo.jpg`, all files in `Sounds/`, and other original application artwork/assets. These materials are approved for public distribution under the project MIT license; no separate attribution is required for these owner-created assets.
- This clearance applies only to Lumetra-owned original materials. It does not clear unrelated third-party components, reference applications, or materials with separate terms; keep proprietary/reference directories excluded and verify any third-party material individually before distribution.
- Keep `build/`, root object files/logs, `.devin/`, `.kiro/`, and both `*Reference/` directories out of Git. The reference directories contain proprietary binaries and local browser/runtime data and must not be uploaded.
- Keep the root display/screenshot diagnostic scripts and `docs/PHASE5_REPORT.md` local; the scripts can alter display/registry state or launch an obsolete executable, and the report is internal investigation history.
- The main profile-storage loader and profile importer enforce the same 4 MiB file limit; boundary tests cover empty, at-limit, and over-limit sizes.
- The local Git repository is newly initialized on `main` with no commits, so no prior history exists here to audit. Review any history before importing it.
- Review the staged file list and scan the exact release tree for personal data and secrets. Rotate any credentials if actual credentials are discovered in material that was shared or committed elsewhere.
- Canonical public repository: owner `TOGEH-Products`, repository `Lumetra`, URL <https://github.com/TOGEH-Products/Lumetra>. The owner identity is a personal developer/publisher identity, not a registered company.
- Set up a trusted code-signing identity and decide how its public verification identity will be pinned before enabling any automatic installation.

## Review and publish to GitHub

1. Verify `git remote get-url origin` returns `https://github.com/TOGEH-Products/Lumetra.git`; do not replace an existing remote without reviewing it first.
2. Before staging, inspect `git status --short` and `git add -n .`. Confirm that no local/reference/build files are listed.
3. Stage only reviewed files, inspect `git diff --cached --stat` and `git diff --cached`, and commit locally after the audit passes.
4. Push the reviewed `main` branch only after the staged-file and history reviews are complete and publishing is explicitly authorized.
5. In **Settings → Code security and analysis**, enable secret scanning, push protection, Dependabot alerts, and private vulnerability reporting. Enable Dependabot security updates.
6. In **Settings → Rules**, protect `main`; require pull requests and passing Windows CI/CodeQL checks before merge.
7. Enable CodeQL code scanning and confirm its first workflow completes. Review any alerts before a public release.
8. Configure release builds with `-DLUMETRA_GITHUB_OWNER=TOGEH-Products` and `-DLUMETRA_GITHUB_REPO=Lumetra`; these public identifiers are already selected.

## First release

- Build a clean x64 Release configuration and run CTest.
- Review the generated executable version metadata and the packaged file list.
- Create stable release tag `v<version>` and attach `Lumetra-<version>-win-x64.zip`; verify GitHub exposes its SHA-256 digest.
- Until trusted publisher-signature verification is implemented and tested, keep the updater in download-only mode and state clearly that users must verify and install packages themselves.
- Publish release notes, supported Windows requirements, known limitations, and the exact asset/license notices.
