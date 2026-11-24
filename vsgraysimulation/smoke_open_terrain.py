#!/usr/bin/env python3
"""Open the terrain editor from the launcher and screenshot it."""
import sys, time, subprocess
import numpy as np
from PIL import Image
from Xlib import X, display
from Xlib.ext import xtest

disp = display.Display()
root = disp.screen().root

def find_window(title_substr):
    out = subprocess.run(["xwininfo", "-root", "-tree"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if title_substr in line and '"' in line:
            wid = line.strip().split()[0]
            return int(wid, 16)
    return None

def grab(w):
    geom = w.get_geometry()
    pos = w.translate_coords(root, 0, 0)
    ax, ay = -pos.x, -pos.y
    img = root.get_image(ax, ay, geom.width, geom.height, X.ZPixmap, 0xffffffff)
    arr = np.frombuffer(img.data, dtype=np.uint8).reshape(geom.height, geom.width, 4)
    return arr[:, :, [2, 1, 0]]  # BGRX -> RGB

def click(x, y):
    root.warp_pointer(x, y)
    disp.sync(); time.sleep(0.3)
    xtest.fake_input(disp, X.ButtonPress, 1, x, y)
    xtest.fake_input(disp, X.ButtonRelease, 1, x, y)
    disp.sync()

# ---- launcher: locate green badge cards ----
lw = disp.create_resource_object("window", find_window("多光谱仿真工作台"))
lw.configure(stack_mode=X.Above)
lw.set_input_focus(X.RevertToParent, X.CurrentTime)
disp.sync(); time.sleep(0.8)

origin = lw.translate_coords(root, 0, 0)
ox, oy = -origin.x, -origin.y
shot = grab(lw)
Image.fromarray(shot).save("smoke_launcher.png")
r, g, b = shot[:, :, 0].astype(int), shot[:, :, 1].astype(int), shot[:, :, 2].astype(int)
mask = (g > 120) & (g - r > 60) & (g - b > 20)
ys, xs = np.nonzero(mask)
if len(xs) == 0:
    print("NO_GREEN_BADGE"); sys.exit(1)
# cluster by x into 3 cards
order = np.argsort(xs)
xs_s, ys_s = xs[order], ys[order]
clusters, cur = [], [0]
for i in range(1, len(xs_s)):
    if xs_s[i] - xs_s[i - 1] > 30:
        clusters.append(cur); cur = [i]
    else:
        cur.append(i)
clusters.append(cur)
centers = []
for c in clusters:
    if len(c) > 30:
        centers.append((int(xs_s[c].mean()), int(ys_s[c].mean())))
print("badge centers (client):", centers)
if not centers:
    print("NO_CLUSTER"); sys.exit(1)

# first card = terrain editor; click its badge then its area
bx, by = centers[0]
click(ox + bx, oy + by)
time.sleep(0.4)
click(ox + bx, oy + min(by + 150, shot.shape[0] - 10))
time.sleep(6)

# ---- terrain editor screenshot ----
wid = find_window("地形编辑器")
if wid is None:
    print("TERRAIN_WINDOW_NOT_FOUND"); sys.exit(2)
tw = disp.create_resource_object("window", wid)
tw.configure(stack_mode=X.Above)
tw.set_input_focus(X.RevertToParent, X.CurrentTime)
disp.sync(); time.sleep(1.5)
tshot = grab(tw)
Image.fromarray(tshot).save("smoke_terrain.png")
print("TERRAIN_SHOT_OK", tshot.shape)
