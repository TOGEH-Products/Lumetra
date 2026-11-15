# Security Policy

## Supported versions

Until the first public release, security fixes are made on the default development branch. After a release, the latest stable release is the supported version unless a release note says otherwise.

## Reporting a vulnerability

Please use GitHub's **Private vulnerability reporting** feature for this repository when it is enabled. Do not publish exploit details, credentials, or personal data in a public issue or discussion.

Include the affected version, Windows version, reproduction conditions, and impact. Redact personal information and secrets from logs and screenshots. Maintainers will acknowledge reports and coordinate a fix and disclosure.

If private reporting is not enabled yet, contact the maintainers through a published private security contact instead of opening a public report. Repository owners should enable private vulnerability reporting before the public launch.

## Release security

Lumetra's update feature is intentionally download-only until a canonical release repository and trusted publisher-signature identity are configured. A checksum delivered with an unsigned release is an integrity check, not proof of publisher identity. Do not treat unsigned downloaded content as trusted software.
