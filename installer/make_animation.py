"""Draws the board animation the Windows installer shows while it installs.

A pen draws a right triangle, a compass draws a circle and a graph is plotted, then the board
is wiped clean and it starts again. Writes 24-bit BMP frames (what Inno Setup can show):
board-NN.bmp at 1x and board@2x-NN.bmp for high-DPI screens such as 4K boards.

    python installer/make_animation.py <output folder>

Needs Pillow. Uses no fonts, so the frames are the same on every computer.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

WIDTH, HEIGHT = 440, 200  # 1x size; ClassBoard.iss keeps the same aspect ratio
FRAMES = 40  # shown every 100 ms: a 4 second loop
SUPERSAMPLE = 3  # drawn larger, then scaled down for smooth lines

BOARD = (35, 45, 42)
GRID = (42, 54, 50)
RIBBON = (24, 32, 30)
RIBBON_LINE = (52, 64, 60)
BUTTON = (62, 76, 72)
ACCENT = (90, 209, 166)
CHALK = (242, 245, 244)
YELLOW = (255, 209, 102)
AXIS = (143, 163, 157)
RIBBON_H = 26


def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def progress(frame, start, end):
    return smooth((frame - start) / (end - start))


def partial(points, t):
    """The first fraction t (by length) of a polyline, and where the pen is."""
    lengths = [math.dist(a, b) for a, b in zip(points, points[1:])]
    left = sum(lengths) * t
    out = [points[0]]
    for (a, b), length in zip(zip(points, points[1:]), lengths):
        if left >= length:
            out.append(b)
            left -= length
            continue
        f = left / length if length else 0
        out.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
        break
    return out, out[-1]


TRIANGLE = [(40, 150), (150, 150), (40, 50), (40, 150)]
CIRCLE = [(225 + 42 * math.cos(a / 90 * 2 * math.pi), 100 - 42 * math.sin(a / 90 * 2 * math.pi)) for a in range(91)]
SINE = [(300 + x, 100 - 38 * math.sin(x / 120 * 2 * math.pi)) for x in range(0, 121, 2)]


def frame_image(frame, scale):
    s = scale * SUPERSAMPLE
    img = Image.new("RGB", (WIDTH * s, HEIGHT * s), BOARD)
    d = ImageDraw.Draw(img)
    board_h = HEIGHT - RIBBON_H

    def P(pt):
        return (pt[0] * s, pt[1] * s)

    def line(points, color, width):
        if len(points) > 1:
            d.line([P(p) for p in points], fill=color, width=round(width * s), joint="curve")
            for p in (points[0], points[-1]):  # round caps
                r = width * s / 2
                d.ellipse([P(p)[0] - r, P(p)[1] - r, P(p)[0] + r, P(p)[1] + r], fill=color)

    for x in range(20, WIDTH, 20):
        d.line([P((x, 0)), P((x, board_h))], fill=GRID, width=s)
    for y in range(20, board_h, 20):
        d.line([P((0, y)), P((WIDTH, y))], fill=GRID, width=s)

    # Fading out at the end of the loop: everything drawn blends back into the board.
    fade = 1 - progress(frame, 34, 39)

    def mix(color):
        return tuple(round(b + (c - b) * fade) for b, c in zip(BOARD, color))

    pen = None
    t = progress(frame, 0, 10)
    if t > 0:
        drawn, tip = partial(TRIANGLE, t)
        line(drawn, mix(CHALK), 3)
        if t < 1:
            pen = tip
    t = progress(frame, 10, 20)
    if t > 0:
        drawn, tip = partial(CIRCLE, t)
        line(drawn, mix(YELLOW), 3)
        if t < 1:
            pen = tip
            line([(225, 100), tip], mix(AXIS), 1)  # the compass arm
    t = progress(frame, 19, 22)
    if t > 0:
        axes = tuple(round(b + (c - b) * t) for b, c in zip(BOARD, mix(AXIS)))
        line([(290, 100), (426, 100)], axes, 1.5)
        line([(300, 44), (300, 156)], axes, 1.5)
    t = progress(frame, 22, 31)
    if t > 0:
        drawn, tip = partial(SINE, t)
        line(drawn, mix(ACCENT), 3)
        if t < 1:
            pen = tip

    if pen:  # the pen tip, with a short stylus body
        x, y = pen
        line([(x + 5, y - 5), (x + 16, y - 16)], (200, 206, 204), 4)
        r = 5 * s
        d.ellipse([x * s - r, y * s - r, x * s + r, y * s + r], fill=ACCENT, outline=CHALK, width=s)

    # The ribbon along the bottom, with the pen selected.
    d.rectangle([P((0, board_h)), P((WIDTH, HEIGHT))], fill=RIBBON)
    d.line([P((0, board_h)), P((WIDTH, board_h))], fill=RIBBON_LINE, width=s)
    for i, x in enumerate([14, 40, 62, 84, 120, 142, 250, 340, 362, 384, 412]):
        color = ACCENT if i == 1 else BUTTON
        w = 26 if i == 1 else 14
        d.rounded_rectangle([P((x - w / 2 + 7, board_h + 7)), P((x + w / 2 + 7, HEIGHT - 7))], radius=3 * s, fill=color)

    return img.resize((WIDTH * scale, HEIGHT * scale), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "animation"
    os.makedirs(out, exist_ok=True)
    for frame in range(FRAMES):
        frame_image(frame, 1).save(os.path.join(out, f"board-{frame:02d}.bmp"))
        frame_image(frame, 2).save(os.path.join(out, f"board@2x-{frame:02d}.bmp"))
    print(f"{FRAMES} frames ({WIDTH}x{HEIGHT} and {2 * WIDTH}x{2 * HEIGHT}) written to {out}")


if __name__ == "__main__":
    main()
