#!/usr/bin/env python3
"""Render reproducible README screenshots from the bundled demo assets."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SIZE = 360
CX = CY = SIZE // 2
BG = (4, 10, 28)
WHITE = (255, 255, 255)
GRID = (18, 128, 46)
YELLOW = (255, 210, 32)
MAGENTA = (255, 0, 255)
CYAN = (90, 200, 255)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf",
        "arialbd.ttf" if bold else "arial.ttf",
    )
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    return ImageFont.load_default()


def centered(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, fill, text_font) -> None:
    draw.text(xy, text, fill=fill, font=text_font, anchor="ma")


def plane(image: Image.Image, x: int, y: int, heading: float, label: tuple[str, str, str]) -> None:
    icon = Image.open(ROOT / "assets" / "flight_radar_plane.png").convert("RGBA")
    icon = icon.resize((34, 34), Image.Resampling.LANCZOS)
    icon = icon.rotate(-heading, resample=Image.Resampling.BICUBIC, expand=True)
    image.alpha_composite(icon, (round(x - icon.width / 2), round(y - icon.height / 2)))
    draw = ImageDraw.Draw(image)
    length = 24
    radians = math.radians(heading)
    draw.line(
        (x, y, x + math.sin(radians) * length, y - math.cos(radians) * length),
        fill=MAGENTA,
        width=2,
    )
    callsign, model, altitude = label
    label_x = min(SIZE - 62, max(4, x + 19 if x < CX else x - 78))
    label_y = min(SIZE - 44, max(4, y - 24))
    draw.rounded_rectangle((label_x, label_y, label_x + 74, label_y + 42), 3, fill=(4, 10, 28, 220))
    draw.text((label_x + 4, label_y + 3), callsign, fill=WHITE, font=font(10, True))
    draw.text((label_x + 4, label_y + 15), model, fill=YELLOW, font=font(10, True))
    draw.text((label_x + 4, label_y + 27), altitude, fill=CYAN, font=font(9))


def render_radar() -> Image.Image:
    image = Image.open(ROOT / "assets" / "flight_radar_map_10km.png").convert("RGBA")
    draw = ImageDraw.Draw(image, "RGBA")
    for radius in (40, 80, 120, 160):
        draw.ellipse((CX - radius, CY - radius, CX + radius, CY + radius), outline=GRID, width=2)
    draw.line((20, CY, SIZE - 20, CY), fill=GRID, width=2)
    draw.line((CX, 20, CX, SIZE - 20), fill=GRID, width=2)
    draw.ellipse((CX - 3, CY - 3, CX + 3, CY + 3), fill=WHITE)
    centered(draw, (CX, 4), "N", WHITE, font(12, True))
    centered(draw, (CX, 341), "S", WHITE, font(12, True))
    draw.text((6, CY), "W", fill=WHITE, font=font(12, True), anchor="lm")
    draw.text((354, CY), "E", fill=WHITE, font=font(12, True), anchor="rm")
    draw.text((302, CY - 10), "10 km", fill=(40, 224, 80), font=font(10, True), anchor="lm")
    plane(image, 112, 114, 62, ("BAW117", "B789", "13,200 ft"))
    plane(image, 246, 86, 194, ("KLM1007", "E190", "9,400 ft"))
    plane(image, 235, 262, 313, ("EZY24KF", "A320", "18,700 ft"))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).ellipse((1, 1, SIZE - 2, SIZE - 2), fill=255)
    output = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    output.paste(image, mask=mask)
    ImageDraw.Draw(output).ellipse((1, 1, SIZE - 2, SIZE - 2), outline=(34, 58, 84, 255), width=3)
    return output


def render_details() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (*BG, 255))
    draw = ImageDraw.Draw(image)
    muted = (90, 150, 165)
    accent = (90, 220, 255)
    route = (80, 220, 130)
    divider = (20, 85, 70)
    centered(draw, (CX, 9), "BAW117", YELLOW, font(24, True))
    centered(draw, (CX, 40), "BRITISH AIRWAYS", accent, font(12, True))
    draw.line((48, 63, SIZE - 48, 63), fill=divider, width=2)
    centered(draw, (CX, 74), "LONDON  LHR", WHITE, font(16, True))
    centered(draw, (CX, 101), "TO", route, font(11, True))
    centered(draw, (CX, 120), "NEW YORK  JFK", WHITE, font(16, True))
    draw.line((35, 151, SIZE - 35, 151), fill=divider, width=2)
    centered(draw, (88, 161), "DEPARTED", muted, font(10, True))
    centered(draw, (SIZE - 88, 161), "ARRIVING IN", muted, font(10, True))
    centered(draw, (88, 181), "01:48 AGO", route, font(15, True))
    centered(draw, (SIZE - 88, 181), "05:22", route, font(15, True))
    draw.line((35, 207, SIZE - 35, 207), fill=divider, width=2)
    centered(draw, (CX, 216), "AIRCRAFT", muted, font(10, True))
    centered(draw, (CX, 234), "B789", (255, 200, 0), font(17, True))
    centered(draw, (CX, 257), "BOEING 787-9 DREAMLINER", WHITE, font(11))
    centered(draw, (112, 280), "ALTITUDE", muted, font(10, True))
    centered(draw, (SIZE - 112, 280), "SPEED", muted, font(10, True))
    centered(draw, (112, 300), "13,200 ft", CYAN, font(16, True))
    centered(draw, (SIZE - 112, 300), "284 kt", CYAN, font(16, True))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).ellipse((1, 1, SIZE - 2, SIZE - 2), fill=255)
    output = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    output.paste(image, mask=mask)
    ImageDraw.Draw(output).ellipse((1, 1, SIZE - 2, SIZE - 2), outline=(34, 58, 84, 255), width=3)
    return output


def main() -> None:
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)
    render_radar().save(docs / "radar-map.png", optimize=True)
    render_details().save(docs / "flight-details.png", optimize=True)
    print("Wrote docs/radar-map.png and docs/flight-details.png")


if __name__ == "__main__":
    main()
