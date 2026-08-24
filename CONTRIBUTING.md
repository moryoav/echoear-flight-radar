# Contributing to EchoEar Flight Radar

Thank you for helping improve EchoEar Flight Radar. Contributions to the ESPHome
configuration, custom component, generated assets, documentation, and hardware
portability are welcome.

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md) in all project spaces.

## Before you start

Open an issue before making a large behavioral or architectural change so the
approach can be discussed first. Small fixes and documentation improvements can
go directly to a pull request.

Use the repository's issue forms for bug reports and feature requests. Search
existing issues first to avoid duplicates.

## Reporting a bug

Include enough information to reproduce the problem:

- ESPHome version and Home Assistant version
- EchoEar hardware revision, or details of another ESP32 device
- Installation method and any relevant configuration changes
- Selected radar range and whether a custom map is in use
- Exact reproduction steps, expected behavior, and actual behavior
- Relevant ESPHome logs and, for display issues, a clear photo or screenshot

Remove Wi-Fi credentials, API keys, Home Assistant tokens, exact home
coordinates, private URLs, and other personal information before posting logs or
configuration.

## Development setup

Install a current ESPHome release and clone the repository:

```bash
git clone https://github.com/moryoav/echoear-flight-radar.git
cd echoear-flight-radar
cp secrets.example.yaml secrets.yaml
esphome config echoear-flight-radar.yaml
esphome compile echoear-flight-radar.yaml
```

Fill `secrets.yaml` with local values before compiling. Never commit that file.
See the README for map generation and first-flash instructions.

The main project areas are:

```text
echoear-flight-radar.yaml          ESPHome configuration and display UI
assets/                            Aircraft icon and generated maps
components/async_flight_radar/     ESP-IDF asynchronous HTTPS worker
tools/                             Map and documentation rendering tools
docs/                              README images
```

## Testing changes

At minimum, run:

```bash
esphome config echoear-flight-radar.yaml
esphome compile echoear-flight-radar.yaml
```

For custom component changes, verify bounded HTTP responses, JSON parsing,
thread synchronization, object lifetimes, error handling, and that network work
does not block the ESPHome main loop.

When hardware is available, test boot, Wi-Fi and Home Assistant reconnection,
radar polling, smooth aircraft movement, touch selection, flight details, the
60-second detail timeout, and all supported map ranges. Describe any hardware
testing that was not possible in the pull request.

## Pull requests

- Keep each pull request focused on one problem.
- Explain what changed, why it changed, and how it was tested.
- Link the related issue when one exists.
- Update documentation and screenshots when behavior or appearance changes.
- Preserve provider attribution for data and map assets.
- Do not include generated build output, secrets, private coordinates, or
  unrelated formatting changes.
- Make sure the repository validation workflow passes.

For setup questions, see [SUPPORT.md](SUPPORT.md). Report vulnerabilities using
the process in [SECURITY.md](.github/SECURITY.md), not through a public bug
report.
