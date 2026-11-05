# User report (2026-10-28): P2's melee works but the grapple launcher he swings isn't drawn.
# Polls the launcher's model visibility (weapon +0xC50 / +0xC54 objects, flag 0x10 at +0xC, set by
# weapon vtable slot 24 FUN_006B1D00) for both players while P2 swings (pad B) or grapples (LB).
#   python -I tools\tests\p2launcherviz.py [melee|grapple]
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

what = sys.argv[1] if len(sys.argv) > 1 else 'melee'
g = Game()
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'player2.cpp')).read()
launcher_off = int(re.search(r'kInventoryLauncher = (0x[0-9A-Fa-f]+)', src).group(1), 16)


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', mod('p2')).group(1), 16)
w = [g.u32(g.u32(c + 0x484) + launcher_off) for c in (c1, c2)]


def viz(weapon):
    out = []
    for off in (0x98, 0xC50):
        o = g.u32(weapon + off)
        out.append('-' if not o else ('on ' if g.u32(o + 0xC) & 0x10 else 'off'))
    return '/'.join(out)


mod('p2 at 960 228 -2060')
time.sleep(5)
if what == 'grapple':
    mod('p2 face -0.7 -0.6 -0.45')
    time.sleep(1)
t0 = time.time()
seen = []
pressed = 0
while time.time() - t0 < 4:
    t = time.time() - t0
    if pressed < 3 and t > 0.3 + pressed * 0.7:
        mod('p2 padpress %s 120' % ('2000' if what == 'melee' else '100'))
        pressed += 1
        if what == 'grapple':
            pressed = 3
    s = 'P1 %s  P2 %s' % (viz(w[0]), viz(w[1]))
    if not seen or seen[-1][1] != s:
        seen.append((round(t, 2), s))
    time.sleep(0.01)
for t, s in seen:
    print('%5.2f %s' % (t, s))
