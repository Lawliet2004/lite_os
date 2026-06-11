# Package Format

## Goal

Replace the toy package flow with a safe, deterministic format that can support install, remove, verify, and rollback.

## Minimum Manifest Fields

- `name`
- `version`
- `arch`
- `deps`
- `files`
- `checksums`

## Safety Rules

- Reject path traversal
- Reject absolute paths in payload entries
- Verify checksums before extraction
- Track installed files in a package database
- Support uninstall and rollback

## Current Status

- `lpkg` is still a prototype
- Package layout and metadata rules are not yet frozen
- No signed-package path yet
