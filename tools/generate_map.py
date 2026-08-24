#!/usr/bin/env python3
"""Generate the label-free 360x360 radar background from OpenStreetMap data."""

from __future__ import annotations

import argparse
import json
import math
import time
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw


OVERPASS_URLS = (
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
)

SUPPORTED_RANGES_KM = (5.0, 10.0, 15.0, 25.0)

BACKGROUND = (4, 10, 28, 255)
STYLES = {
    "green": ((16, 76, 47, 125), 0),
    "water": ((10, 62, 91, 190), 0),
    "waterway": ((49, 133, 165, 150), 1.1),
    "local": ((117, 144, 168, 75), 0.8),
    "collector": ((127, 155, 178, 105), 1.2),
    "arterial": ((150, 175, 194, 145), 1.8),
    "motorway": ((182, 199, 212, 180), 2.6),
    "rail": ((173, 140, 174, 145), 1.3),
    "aeroway": ((140, 155, 168, 145), 2.2),
    "coast": ((80, 167, 196, 220), 2.0),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build an EchoEar flight-radar map from OpenStreetMap."
    )
    parser.add_argument("--lat", type=float, required=True, help="Map center latitude")
    parser.add_argument("--lon", type=float, required=True, help="Map center longitude")
    parser.add_argument(
        "--ring-km",
        type=float,
        default=10.0,
        help="Distance represented by the third radar ring (default: 10)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/flight_radar_map_10km.png"),
    )
    parser.add_argument(
        "--all-ranges",
        action="store_true",
        help="Generate the 5, 10, 15, and 25 km firmware map set",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("assets"),
        help="Output directory used with --all-ranges (default: assets)",
    )
    parser.add_argument(
        "--cache",
        type=Path,
        help="Optional path for caching the raw Overpass response",
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        help="Optional Overpass cache directory used with --all-ranges",
    )
    return parser.parse_args()


def build_query(south: float, west: float, north: float, east: float) -> str:
    bbox = f"{south:.6f},{west:.6f},{north:.6f},{east:.6f}"
    return f"""[out:json][timeout:120];
(
  way["highway"~"^(motorway|motorway_link|trunk|trunk_link|primary|primary_link|secondary|secondary_link|tertiary|tertiary_link|unclassified|residential|living_street)$"]({bbox});
  way["railway"="rail"]({bbox});
  way["waterway"~"^(river|canal|stream)$"]({bbox});
  way["natural"="coastline"]({bbox});
  way["aeroway"~"^(runway|taxiway)$"]({bbox});
  way["natural"="water"]({bbox});
  way["water"]({bbox});
  way["leisure"~"^(park|garden|nature_reserve)$"]({bbox});
  way["landuse"~"^(forest|grass|meadow|recreation_ground)$"]({bbox});
);
out tags geom;
"""


def fetch_overpass(query: str) -> dict:
    payload = urllib.parse.urlencode({"data": query}).encode("utf-8")
    last_error: Exception | None = None
    for attempt in range(3):
        for url in OVERPASS_URLS:
            request = urllib.request.Request(
                url,
                data=payload,
                headers={"User-Agent": "echoear-flight-radar-map-generator/1.0"},
            )
            try:
                with urllib.request.urlopen(request, timeout=150) as response:
                    return json.load(response)
            except Exception as error:  # pragma: no cover - remote service
                last_error = error
        if attempt < 2:
            time.sleep(3 * (attempt + 1))
    raise RuntimeError(f"Overpass request failed: {last_error}")


def category(tags: dict[str, str]) -> str | None:
    highway = tags.get("highway")
    if highway in {"motorway", "motorway_link", "trunk", "trunk_link"}:
        return "motorway"
    if highway in {"primary", "primary_link", "secondary", "secondary_link"}:
        return "arterial"
    if highway in {"tertiary", "tertiary_link"}:
        return "collector"
    if highway:
        return "local"
    if tags.get("railway") == "rail":
        return "rail"
    if tags.get("natural") == "coastline":
        return "coast"
    if tags.get("waterway"):
        return "waterway"
    if tags.get("aeroway"):
        return "aeroway"
    if tags.get("natural") == "water" or tags.get("water"):
        return "water"
    if tags.get("leisure") or tags.get("landuse"):
        return "green"
    return None


def render(data: dict, lat: float, lon: float, ring_km: float) -> Image.Image:
    size = 360
    oversample = 3
    canvas_size = size * oversample
    center = canvas_size / 2
    radar_radius = 160 * oversample
    edge_km = ring_km * 4.0 / 3.0
    cos_lat = math.cos(math.radians(lat))

    image = Image.new("RGBA", (canvas_size, canvas_size), BACKGROUND)
    polygon_layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    line_layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    polygons = ImageDraw.Draw(polygon_layer)
    lines = ImageDraw.Draw(line_layer)

    def project(point: dict[str, float]) -> tuple[float, float]:
        dx_km = (point["lon"] - lon) * 111.320 * cos_lat
        dy_km = (point["lat"] - lat) * 110.574
        return (
            center + dx_km * radar_radius / edge_km,
            center - dy_km * radar_radius / edge_km,
        )

    grouped: dict[str, list[list[tuple[float, float]]]] = {
        key: [] for key in STYLES
    }
    for element in data.get("elements", []):
        kind = category(element.get("tags", {}))
        geometry = element.get("geometry")
        if not kind or not geometry or len(geometry) < 2:
            continue
        grouped[kind].append([project(point) for point in geometry])

    for kind in ("green", "water"):
        color, _ = STYLES[kind]
        for points in grouped[kind]:
            if len(points) >= 3:
                polygons.polygon(points, fill=color)

    draw_order = (
        "waterway",
        "local",
        "collector",
        "arterial",
        "motorway",
        "rail",
        "aeroway",
        "coast",
    )
    for kind in draw_order:
        color, width = STYLES[kind]
        scaled_width = max(1, round(width * oversample))
        for points in grouped[kind]:
            lines.line(points, fill=color, width=scaled_width, joint="curve")

    image = Image.alpha_composite(image, polygon_layer)
    image = Image.alpha_composite(image, line_layer)
    image = image.resize((size, size), Image.Resampling.LANCZOS)
    return image.convert("RGB")


def generate_map(
    lat: float,
    lon: float,
    ring_km: float,
    output: Path,
    cache: Path | None,
) -> None:
    edge_km = ring_km * 4.0 / 3.0
    margin_km = edge_km * 1.08
    lat_delta = margin_km / 110.574
    lon_delta = margin_km / (111.320 * math.cos(math.radians(lat)))
    query = build_query(
        lat - lat_delta,
        lon - lon_delta,
        lat + lat_delta,
        lon + lon_delta,
    )

    if cache and cache.exists():
        data = json.loads(cache.read_text(encoding="utf-8"))
    else:
        data = fetch_overpass(query)
        if cache:
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(json.dumps(data), encoding="utf-8")

    output.parent.mkdir(parents=True, exist_ok=True)
    render(data, lat, lon, ring_km).save(output, optimize=True)
    print(f"Wrote {output} ({len(data.get('elements', []))} OSM ways)")


def main() -> None:
    args = parse_args()
    if not -90 <= args.lat <= 90 or not -180 <= args.lon <= 180:
        raise SystemExit("Latitude or longitude is outside its valid range")
    if args.ring_km <= 0:
        raise SystemExit("--ring-km must be positive")

    if args.all_ranges:
        for ring_km in SUPPORTED_RANGES_KM:
            suffix = f"{int(ring_km)}km"
            cache = (
                args.cache_dir / f"overpass_{suffix}.json"
                if args.cache_dir
                else None
            )
            generate_map(
                args.lat,
                args.lon,
                ring_km,
                args.output_dir / f"flight_radar_map_{suffix}.png",
                cache,
            )
        return

    generate_map(args.lat, args.lon, args.ring_km, args.output, args.cache)


if __name__ == "__main__":
    main()
