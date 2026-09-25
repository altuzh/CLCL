"""Regenerate the pinned image editor's native toolbar icons (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

OUT = Path(__file__).parent
S = 4
BLUE = "#2684df"
DEEP = "#155aa9"
GOLD = "#f3b332"
INK = "#34465b"


def xy(*coords):
    return tuple(round(v * S) for v in coords)


def line(draw, points, color, width=2, joint="curve"):
    draw.line([xy(*point) for point in points], fill=color, width=width * S, joint=joint)


def icon(name):
    image = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    if name in ("undo", "redo"):
        # Bent arrows stay recognizable when Windows scales the toolbar to 16 px.
        path = [(26, 24), (26, 20), (23, 15), (19, 12), (9, 12)]
        head = [(3, 12), (13, 5), (13, 19)]
        if name == "redo":
            path = [(32-x, y) for x, y in path]
            head = [(32-x, y) for x, y in head]
        line(d, path, DEEP, 4)
        line(d, path[1:], "#60b5ff", 2)
        d.polygon([xy(x, y) for x, y in head], fill=BLUE)
        d.polygon([xy(x, y) for x, y in head], outline=DEEP)
    elif name == "pen":
        d.polygon([xy(5, 27), xy(8, 18), xy(22, 4), xy(28, 10), xy(14, 24)], fill=BLUE)
        line(d, [(10, 20), (23, 7)], "#bfe4ff", 2)
        d.polygon([xy(5, 27), xy(8, 18), xy(14, 24)], fill=GOLD)
        d.polygon([xy(5, 27), xy(7, 23), xy(9, 25)], fill=INK)
        line(d, [(22, 4), (28, 10)], DEEP, 2)
    elif name == "marker":
        d.rounded_rectangle(xy(9, 3, 23, 22), radius=3*S, fill=GOLD, outline="#bb7627", width=2*S)
        d.rectangle(xy(10, 7, 22, 11), fill="#fff3b6")
        d.polygon([xy(10, 22), xy(22, 22), xy(19, 27), xy(13, 27)], fill=INK)
        line(d, [(7, 29), (25, 29)], "#ffe176", 3)
    elif name == "eraser":
        d.polygon([xy(5, 22), xy(17, 7), xy(27, 15), xy(16, 28), xy(10, 28)], fill="#e789a9")
        d.polygon([xy(5, 22), xy(17, 7), xy(21, 11), xy(10, 28)], fill="#f9bfd0")
        line(d, [(10, 28), (16, 28), (27, 15)], "#a44977", 2)
        line(d, [(18, 28), (28, 28)], "#8795a5", 2)
    elif name == "line":
        line(d, [(6, 25), (26, 7)], BLUE, 3)
        for x, y in [(6, 25), (26, 7)]:
            d.ellipse(xy(x-3, y-3, x+3, y+3), fill="white", outline=DEEP, width=2*S)
    elif name == "arrow":
        line(d, [(5, 26), (25, 6)], BLUE, 3)
        d.polygon([xy(15, 6), xy(27, 4), xy(25, 16)], fill=DEEP)
        d.polygon([xy(19, 8), xy(25, 6), xy(24, 12)], fill="#7ac4ff")
    elif name == "frame-arrow":
        points = [xy(x, y) for x, y in [(3, 12), (16, 12), (16, 5), (29, 16),
                                        (16, 27), (16, 20), (3, 20)]]
        d.line(points + [points[0]], fill="#e52528", width=3*S, joint="curve")
    elif name == "rect":
        d.rounded_rectangle(xy(5, 7, 27, 25), radius=2*S, outline="#7b55c7", width=2*S)
        for x, y in [(5, 7), (27, 7), (5, 25), (27, 25)]:
            d.ellipse(xy(x-2, y-2, x+2, y+2), fill="#7b55c7")
    elif name == "filled-rect":
        d.rounded_rectangle(xy(5, 7, 27, 25), radius=2*S, fill="#a887e4", outline="#57359f", width=2*S)
    elif name == "ellipse":
        d.ellipse(xy(4, 7, 28, 25), outline="#28a270", width=2*S)
        for x, y in [(4, 16), (28, 16)]:
            d.ellipse(xy(x-2, y-2, x+2, y+2), fill="#28a270")
    elif name == "filled-ellipse":
        d.ellipse(xy(4, 7, 28, 25), fill="#63d3a0", outline="#167e55", width=2*S)
    elif name == "crop":
        line(d, [(10, 4), (10, 22), (28, 22)], BLUE, 3)
        line(d, [(4, 10), (22, 10), (22, 28)], DEEP, 3)
        d.rectangle(xy(9, 9, 23, 23), outline="#85c6ff", width=S)
    elif name == "copy":
        d.rounded_rectangle(xy(4, 3, 22, 24), radius=2*S, fill="#bfe4ff", outline=DEEP, width=2*S)
        d.rounded_rectangle(xy(10, 9, 28, 29), radius=2*S, fill="#f5fbff", outline=BLUE, width=2*S)
        for y in (15, 20, 25):
            line(d, [(14, y), (24, y)], BLUE, 2)
    elif name.startswith("size"):
        selected = {"size": 3, "size-3": 3, "size-6": 6, "size-12": 12}[name]
        for y, width, value in [(7, 2, 3), (16, 4, 6), (26, 6, 12)]:
            d.line([xy(5, y), xy(26, y)], fill=DEEP, width=(width + 2) * S)
            d.line([xy(5, y), xy(26, y)], fill=GOLD if value == selected else BLUE, width=width * S)
    elif name == "select":
        d.polygon([xy(6, 4), xy(6, 27), xy(12, 20), xy(17, 29), xy(21, 27), xy(16, 18), xy(26, 17)],
                  fill="#eef7ff", outline=DEEP)
    elif name == "frame-select":
        d.rectangle(xy(2, 3, 29, 28), outline="#242424", width=3*S)
        d.rectangle(xy(2, 3, 29, 28), outline="white", width=2*S)
        arrow = [xy(9, 7), xy(9, 24), xy(13, 20), xy(17, 27), xy(20, 25), xy(16, 19), xy(23, 18)]
        d.polygon(arrow, fill="white")
        d.line(arrow + [arrow[0]], fill="#242424", width=2*S)
    elif name == "zoom":
        d.ellipse(xy(4, 3, 23, 22), outline=BLUE, width=3*S)
        line(d, [(20, 20), (29, 29)], DEEP, 4)
        line(d, [(9, 12), (18, 12)], DEEP, 2)
        line(d, [(13.5, 8), (13.5, 17)], DEEP, 2)
    elif name == "pan":
        line(d, [(16, 3), (16, 29)], DEEP, 3)
        line(d, [(3, 16), (29, 16)], DEEP, 3)
        for points in (((16, 2), (11, 9), (21, 9)), ((16, 30), (11, 23), (21, 23)),
                       ((2, 16), (9, 11), (9, 21)), ((30, 16), (23, 11), (23, 21))):
            d.polygon([xy(x, y) for x, y in points], fill=BLUE)
    elif name == "rotate":
        reference = Image.open(OUT / "pin-rotate-source.png").convert("RGB")
        alpha = Image.new("L", reference.size)
        alpha.putdata([
            max(0, min(255, round((247 - green) * 255 / 241)))
            if red - green > 8 and red - blue > 8 else 0
            for red, green, blue in reference.getdata()
        ])
        glyph = Image.new("RGBA", reference.size, (221, 6, 18, 0))
        glyph.putalpha(alpha)
        glyph = glyph.crop(alpha.getbbox())
        glyph.thumbnail((25*S, 25*S), Image.Resampling.LANCZOS)
        image.alpha_composite(glyph, ((32*S - glyph.width)//2, (32*S - glyph.height)//2))
    elif name == "text":
        d.rounded_rectangle(xy(3, 5, 29, 27), radius=3*S, outline=BLUE, width=2*S)
        line(d, [(8, 11), (24, 11)], DEEP, 2)
        line(d, [(16, 11), (16, 24)], DEEP, 2)
    elif name == "callout":
        d.rounded_rectangle(xy(3, 4, 28, 22), radius=4*S, fill="#f5fbff", outline=BLUE, width=2*S)
        d.polygon([xy(10, 22), xy(8, 29), xy(18, 22)], fill="#f5fbff", outline=BLUE)
        line(d, [(8, 11), (23, 11)], DEEP, 2)
        line(d, [(8, 16), (19, 16)], DEEP, 2)
    elif name == "step":
        d.ellipse(xy(4, 4, 27, 27), fill=BLUE, outline=DEEP, width=2*S)
        line(d, [(15, 9), (17, 9), (17, 22)], "white", 3)
        line(d, [(13, 22), (21, 22)], "white", 2)
    elif name == "redact":
        d.rounded_rectangle(xy(4, 8, 28, 24), radius=2*S, fill=INK, outline=BLUE, width=2*S)
        line(d, [(9, 15), (23, 15)], "#8aa4bb", 2)
    elif name == "spotlight":
        d.rectangle(xy(3, 4, 29, 28), fill=INK)
        d.ellipse(xy(9, 8, 24, 24), fill=GOLD, outline="#ffeb9c", width=2*S)
    image.save(OUT / f"pin-{name}.ico", format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48)])


if __name__ == "__main__":
    for name in ("undo", "redo", "pen", "marker", "eraser", "line", "arrow", "frame-arrow", "rect", "filled-rect", "ellipse", "filled-ellipse", "crop", "rotate", "copy", "size", "size-6", "size-12", "select", "frame-select", "zoom", "pan", "text", "callout", "step", "redact", "spotlight"):
        icon(name)
