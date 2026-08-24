<div align="center">

# EchoEar Flight Radar

A self-contained ESPHome flight radar for the EchoEar, with live aircraft
tracking, interactive flight details, and a local map display.

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.8.1-18BCF2?logo=esphome&logoColor=white)](https://esphome.io/)
[![Validate](https://github.com/moryoav/echoear-flight-radar/actions/workflows/validate.yml/badge.svg)](https://github.com/moryoav/echoear-flight-radar/actions/workflows/validate.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

<p align="center">
  <img src="docs/radar-map.png" width="360" alt="EchoEar radar map with live aircraft">
  <img src="docs/flight-details.png" width="360" alt="EchoEar selected flight details">
</p>

<p align="center"><sub>Illustrative traffic and flight details. Map data (c) OpenStreetMap contributors.</sub></p>

## What it does

- Polls nearby aircraft from the free adsb.fi feed every five seconds.
- Animates aircraft smoothly at a 100 ms display interval between network updates.
- Compensates for ADS-B source and HTTP delivery age, ignores repeated stale
  positions, and confirms low-integrity MLAT relocations before displaying them.
- Draws heading-oriented PNG aircraft icons, range rings, labels, and optional
  runways on the EchoEar's 360 x 360 round display.
- Switches among range-aligned, label-free OpenStreetMap backgrounds for the
  5, 10, 15, and 25 km views.
- Opens a flight details screen when the aircraft icon or its label is tapped.
- Shows callsign, airline, route, elapsed/remaining time, aircraft type,
  altitude, and speed; tapping again or waiting 60 seconds returns to radar.
- Fetches aircraft and route metadata asynchronously so tapping is immediate.
- Optionally requests authoritative departure and arrival times from FlightAware
  AeroAPI only after a flight is selected. It also fills missing route and airline
  metadata from FlightAware; estimated time values are prefixed with `~`.
- Exposes range, units, runway visibility, backlight, touch inputs, battery, and
  diagnostics as native Home Assistant entities.

HTTP runs in a dedicated ESP-IDF worker. The display and ESPHome main loop only
receive bounded handoffs, avoiding long TLS requests on the UI path.

## Target hardware

<p align="center">
  <a href="https://s.click.aliexpress.com/e/_c3kfuJkn">
    <img src="docs/echoear-product.png" width="420" alt="Espressif EchoEar development kit">
  </a>
</p>

<p align="center">
  <strong><a href="https://s.click.aliexpress.com/e/_c3kfuJkn">EchoEar product listing on AliExpress</a></strong>
</p>

This configuration is built and tested for the **Espressif EchoEar v1.1**:

| Component | Configuration |
| --- | --- |
| MCU | ESP32-S3, 240 MHz |
| Flash / PSRAM | 32 MB flash, 16 MB octal PSRAM |
| Display | 360 x 360 round LCD, `JC3636W518V2` ESPHome model |
| Touch | CST816S over I2C |
| Controls | Capacitive top pads and GPIO0 side button |
| Power | Battery reporting, switched 3.3 V rail, PWM backlight |

> [!IMPORTANT]
> The included YAML was written specifically for this EchoEar hardware. Its pin
> assignments, display driver, touch controller, power control, dimensions, and
> memory configuration are not generic ESP32 defaults.

The flight-radar design can be adapted to other ESP32 devices with screens, but
that is a hardware port rather than a drop-in installation. At minimum, another
device will need matching ESPHome definitions for its board, display, touch
controller, backlight, and controls. The layout and bundled map assets may also
need resizing for a different display resolution, and PSRAM is strongly
recommended for the full-frame display buffer and network workloads.

The microphone and speaker are intentionally inactive in this version. See the
[Espressif EchoEar hardware documentation](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-dev-kits-en-master-esp32s3.pdf)
for board and flashing information.

<sub>Product image: Espressif Systems, from the
<a href="https://github.com/espressif/esp-dev-kits">esp-dev-kits documentation</a>,
licensed under
<a href="https://creativecommons.org/licenses/by-sa/4.0/">CC BY-SA 4.0</a>.</sub>

## Requirements

- EchoEar v1.1
- Home Assistant with a correctly positioned `zone.home`
- ESPHome 2026.7.3 or newer; 2026.8.1 is tested
- A 2.4 GHz Wi-Fi network with internet access
- Python and Pillow only if generating a custom map
- FlightAware AeroAPI key, optional but highly recommended for authoritative
  flight times

No Home Assistant helpers or `configuration.yaml` changes are required.

## FlightAware AeroAPI

A [FlightAware AeroAPI](https://www.flightaware.com/commercial/aeroapi/) key is
highly recommended. The radar works without one, but departure and arrival times
will remain estimates and are marked with `~`. With a key, tapping an aircraft
requests FlightAware's current flight record so the details screen can show
authoritative departure and arrival times. FlightAware can also fill missing
airline information when it is available.

For personal use, create a FlightAware account and API key in the
[AeroAPI portal](https://www.flightaware.com/aeroapi/portal/). The Personal plan
currently has no monthly minimum and includes up to $5 of API usage each month.
The primary lookup used by this project currently costs $0.005 per result set and
is limited to one page, so the included usage covers approximately **1,000 normal
aircraft taps per month**. That should be more than enough for typical home use.

Some taps may use a second operator lookup when airline information is missing,
which consumes additional quota. FlightAware pricing can change, so review the
[current AeroAPI fees](https://www.flightaware.com/commercial/aeroapi/#query-fees-breakdown)
and monitor usage in the portal. AeroAPI is called only when an aircraft is
selected, never by the regular five-second radar polling.

## Install

### 1. Get the files

Clone the repository on a computer that has ESPHome installed:

```bash
git clone https://github.com/moryoav/echoear-flight-radar.git
cd echoear-flight-radar
```

For the Home Assistant ESPHome add-on, place these items in `/config/esphome`:

```text
echoear-flight-radar.yaml
assets/
components/
secrets.yaml
```

The YAML file, `assets`, and `components` must remain beside each other because
the configuration uses relative paths.

### 2. Configure credentials

Create `secrets.yaml` from the example:

```bash
cp secrets.example.yaml secrets.yaml
```

Set your Wi-Fi values and paste the key from the FlightAware AeroAPI portal into
`aeroapi_key`. It may remain empty; live tracking and flight details still work,
but departure and arrival durations will be estimates when no authoritative
time is available.

Do not commit `secrets.yaml`. It is excluded by `.gitignore`.

### 3. Configure the time zone

Home Assistant supplies the live radar center from `zone.home`. Update the time
zone substitution near the top of `echoear-flight-radar.yaml`:

```yaml
substitutions:
  timezone: "Europe/London"
```

The bundled example map is centered on London Heathrow. Generate your own map
for another location before flashing.

### 4. Generate the local map

Install the map dependency and run the generator with the same coordinates:

```bash
python -m pip install -r requirements-map.txt
python tools/generate_map.py \
  --lat 51.4700 \
  --lon -0.4543 \
  --all-ranges \
  --output-dir assets
```

The script queries OpenStreetMap through Overpass and creates four label-free
360 x 360 PNGs aligned to the firmware's 5, 10, 15, and 25 km radar
projections. It also writes `assets/flight_radar_map_metadata.h` with the map
center. ESPHome compiles that generated metadata into the firmware and checks
it against `zone.home` before displaying the maps, so no coordinates need to be
copied into the YAML. The firmware switches backgrounds immediately when the
range entity changes. Regenerate the complete set whenever the map center
changes.

The generated files are `flight_radar_map_5km.png`,
`flight_radar_map_10km.png`, `flight_radar_map_15km.png`, and
`flight_radar_map_25km.png`. Large Overpass requests can take several minutes;
rerun the command if all four files are not produced.

The generated metadata contains the exact map center. Do not publish or commit
maps and metadata generated for a private home location.

The public example contains Heathrow's two runway definitions. To draw runways
for another airport, update the `RUNWAYS` array in the display lambda or turn
off the **Flight Radar Show Runways** entity in Home Assistant.

### 5. Validate and flash

Validate before connecting the device:

```bash
esphome config echoear-flight-radar.yaml
```

For the first installation, connect the EchoEar over USB and flash it. Replace
the device name with the serial port used by your system:

```bash
# Windows example
esphome run echoear-flight-radar.yaml --device COM4

# Linux example
esphome run echoear-flight-radar.yaml --device /dev/ttyACM0
```

After the first flash, updates can be installed over Wi-Fi:

```bash
esphome run echoear-flight-radar.yaml --device echoear-flight-radar.local
```

Home Assistant should discover the device after it connects. Add it through
**Settings > Devices & services**, then verify that `zone.home` reaches the
device through the ESPHome API.

## Controls

| Action or entity | Result |
| --- | --- |
| Tap aircraft icon or label | Open the selected flight details |
| Tap while viewing details | Return to the radar |
| 60 seconds without input | Return to the radar automatically |
| Flight Radar Range | Select 5, 10, 15, or 25 km |
| Flight Radar Units | Select kilometers or miles |
| Flight Radar Show Runways | Toggle compiled runway overlays |
| Screen | Adjust display backlight |
| Restart | Reboot the device |

## Data flow

| Service | When used | Data sent |
| --- | --- | --- |
| [adsb.fi](https://opendata.adsb.fi/) | Every five seconds | Home coordinates and radius |
| [ADSBDB](https://www.adsbdb.com/) | As aircraft enter the local set | ICAO hex or callsign |
| [FlightAware AeroAPI](https://www.flightaware.com/aeroapi/) | On tap, when a key is configured | Selected callsign and, only when needed, its operator code |

API responses are handled on-device. No companion server, Home Assistant
automation, or helper entity is needed. Provider availability and data quality
vary. ADSBDB metadata remains preferred, while FlightAware fills missing route
or airline fields and the display falls back to marked time estimates when no
authoritative times are available.

## Project layout

```text
echoear-flight-radar.yaml          Main ESPHome configuration and UI
assets/                            Plane icon, generated maps, and map metadata
components/async_flight_radar/     ESP-IDF asynchronous HTTPS worker
tools/generate_map.py              OpenStreetMap multi-range map generator
tools/render_readme_screenshots.py Reproducible documentation images
docs/                              README screenshots
```

## Troubleshooting

**The radar says `NO HOME`**

Confirm Home Assistant is connected through the ESPHome API and that
`zone.home` has latitude and longitude attributes.

**Aircraft appear but the map does not**

Make sure all four generated map files and
`assets/flight_radar_map_metadata.h` were created by the same `--all-ranges`
command. A mismatch with `zone.home` intentionally disables the static maps at
every range.

**Flight times still start with `~`**

The value is an estimate. Confirm the AeroAPI key is present, then check the
FlightAware status and HTTP diagnostic entities. Not every callsign resolves
to a current AeroAPI flight record.

**The display updates but aircraft jump**

Check the radar fetch and position diagnostics in Home Assistant. Aircraft are
extrapolated continuously from source-timestamped observations. Repeated stale
positions are ignored, while implausible corrections are held until multiple
distinct observations confirm the relocation. The rejected, reacquired, stale,
and maximum-correction sensors show when this protection is active.

## Contributing and support

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before
opening a pull request, and use the structured issue forms for bug reports and
feature requests. General setup guidance is in [SUPPORT.md](SUPPORT.md).

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report vulnerabilities
privately according to [SECURITY.md](SECURITY.md), not through a public issue.

## Support me on Ko-fi

If this project is useful to you, you can support its continued development:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Y5B124NZ2L)

## Credits and license

The project was inspired by
[MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar),
featured by [Hackaday](https://hackaday.com/2026/07/08/esp32-keeps-tabs-on-your-local-airspace/).
See [NOTICE.md](NOTICE.md) for data-provider and OpenStreetMap attribution.

Released under the [MIT License](LICENSE).
