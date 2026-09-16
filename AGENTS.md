# Project development rules

## Canonical development environment

- The published GHCR Docker image and Docker Compose stack are the canonical
  development environment for this project.
- `make up` must pull the configured image and open a shell with ESP-IDF
  activated. Source code is mounted into `/workspace`.
- Users must not need to run `make bootstrap` to build, test, format, lint,
  flash, or monitor the project. All required tools and Python dependencies
  must be installed in the published image.
- GitHub Actions must run comprehensive image and Compose workflow tests before
  publishing an image to GHCR.
- Published images must provide a branch tag and a short-commit tag so users
  can choose a moving development version or pin an exact image version.

## Python client and documentation

- Keep client package metadata, implementation, and command-line helpers under
  `client/`; do not place client packaging metadata in the repository root.
- The Python REST/WebSocket client in `client/remote_hid_client/` is a supported
  user-facing interface and must remain aligned with the firmware API contract.
- Changes to REST or WebSocket behavior must include client updates and
  comprehensive automated client/API tests.
- User-facing client and API behavior must be documented comprehensively in
  `docs/`, with `docs/PYTHON_CLIENT.md` kept current and linked from the main
  README. Do not leave new client methods, protocol commands, authentication
  behavior, limits, or error handling undocumented.
