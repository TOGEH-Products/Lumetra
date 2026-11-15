# Contributing

Thanks for helping improve Lumetra. Keep changes focused, preserve the native Windows architecture, and describe user-visible behavior and security implications in the pull request.

## Before opening an issue or pull request

- Search existing issues and documentation first.
- Never include credentials, personal paths, private logs, screenshots containing identifying information, or data copied from the local reference-app directories.
- For security issues, follow [SECURITY.md](SECURITY.md) and do not use a public issue.

## Development setup

Use Windows 10/11 x64, Visual Studio 2022 or newer with the C++ workload and Windows SDK, CMake 3.20+, and Ninja (or a supported Visual Studio generator).

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Do not commit build products, local configuration, logs, credentials, signing files, or third-party reference applications. See [`.gitignore`](.gitignore).

## Change expectations

- Follow the existing C++20 and Win32 conventions; avoid adding frameworks or dependencies without a clear need.
- Keep UI and display changes consistent with the existing monochrome interface.
- Add or update tests for behavior changes and verify a Release build when practical.
- Document new settings, dependencies, assets, and data handling.
- Do not add third-party code or assets without verifying redistribution terms and adding required notices. The MIT license applies only to Lumetra-owned material.
- Do not add code that silently executes downloaded content, weakens signature checks, or requests unnecessary elevation.

## Pull requests

Use the pull request template. Explain the problem, implementation, verification performed, and any remaining limitations. Keep unrelated refactoring out of the change.
