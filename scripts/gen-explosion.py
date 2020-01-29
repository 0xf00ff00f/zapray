import math

size = 64
margin = 2
radius = 0.5 * (size - margin)

total_frames = 16

color0 = (1, 1, 1)
color1 = (0.25, 0.25, 1)

def distance(x0, y0, x1, y1):
    dx = x0 - x1
    dy = y0 - y1
    return math.sqrt(dx * dx + dy * dy)

for frame in range(total_frames):
    name = 'explosion-%d.ppm' % frame
    with open(name, 'w') as f:
        small_radius = (1.05 * radius) * frame / total_frames
        t = float(total_frames - 1 - frame) / (total_frames - 1)
        cx = 0.5 * size - 10 * t
        cy = 0.5 * size - 5 * t
        f.write('P6\n')
        f.write('%d %d\n' % (size, size))
        f.write('255\n')
        for x in range(size):
            for y in range(size):
                d0 = distance(x, y, 0.5 * size, 0.5 * size)
                d1 = distance(x, y, cx, cy)
                if d0 > radius:
                    r0 = r1 = r2 = 0
                elif d1 < small_radius:
                    r0 = r1 = r2 = 0
                else:
                    v = (d1 - small_radius) / radius
                    if v > 1:
                        v = 1
                    band_interval = 0.25
                    v = int(v / band_interval) * band_interval
                    r0 = int((color0[0] + v * (color1[0] - color0[0])) * 255)
                    r1 = int((color0[1] + v * (color1[1] - color0[1])) * 255)
                    r2 = int((color0[2] + v * (color1[2] - color0[2])) * 255)
                f.write('%c%c%c' % (r0, r1, r2))
