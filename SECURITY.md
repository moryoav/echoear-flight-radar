# Security Policy

## Supported versions

Security fixes are made on the current `main` branch and included in the latest
published revision. Older snapshots and locally modified firmware are not
maintained separately.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting for this repository when it is
available. If that option is unavailable, open a minimal public issue asking the
maintainer for a private contact channel. Do not include vulnerability details,
proof-of-concept code, credentials, or device location information in a public
issue.

Include the affected commit or version, hardware, impact, reproduction
conditions, and a suggested mitigation when known. Reports will be acknowledged
and assessed as soon as practical. Public disclosure should be coordinated with
the maintainer after a fix or mitigation is available.

## Sensitive information

Never include any of the following in an issue, pull request, screenshot, or log:

- Wi-Fi credentials or ESPHome API and OTA credentials
- FlightAware AeroAPI keys or Home Assistant access tokens
- Exact home coordinates or private network addresses
- Unredacted API responses or logs that expose personal location information

Revoke and replace a credential immediately if it is exposed.

## Security-sensitive areas

Changes in these areas require particular care:

- ESP-IDF HTTPS requests, TLS validation, redirects, and response-size bounds
- External API URLs, authentication headers, and secret handling
- Home Assistant API data, especially `zone.home` coordinates
- OTA updates, Wi-Fi provisioning, and ESPHome encryption
- JSON parsing, fixed-size buffers, task synchronization, and object lifetimes

Only test against devices, accounts, networks, and API subscriptions that you
own or are authorized to use. Vulnerabilities in adsb.fi, ADSBDB, FlightAware,
ESPHome, or another dependency should also be reported to the affected provider
under its disclosure policy.
