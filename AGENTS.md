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
