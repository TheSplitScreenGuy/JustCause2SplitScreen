# Puts Player 1 (or Player 2) at a saved spot (docs/positions.md) facing the saved camera direction.
#
#   python -I spot.py p1 <x> <y> <z> <fx> <fy> <fz>   P1 there, his camera turned with the mouse
#   python -I spot.py p2 <x> <y> <z> <fx> <fy> <fz>   P2 there, his camera set directly
#   python -I spot.py face <fx> <fy> <fz>             only turn P1's camera
#
# fx fy fz is the camera forward as "cam" prints it (the camera looks along -forward).
import math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jc2ctl


def cam_once():
    out = jc2ctl.mod('cam').splitlines()
    vals = {l.split()[0]: [float(v) for v in l.split()[1:4]] for l in out if l.split()}
    return vals['pos'], vals['forward']


def cam():
    # In the split the main camera alternates between the views: take Player 1's (nearest Rico 1).
    try:
        from procmem import Game
        g = Game()
        p1 = g.floats(g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48) + 0x18C, 3)
    except Exception:
        return cam_once()
    best = None
    for _ in range(6):
        pos, f = cam_once()
        d = math.dist(pos, p1)
        if best is None or d < best[0]: best = (d, pos, f)
        if d < 20: break
        time.sleep(0.03)
    return best[1], best[2]


def yaw(f):
    return math.atan2(f[0], f[2])


def turn_p1(target):
    want = yaw(target)
    # Pixels per radian, measured on the first move (its sign too).
    _, f0 = cam()
    jc2ctl.mouse(200, 0, 10)
    time.sleep(0.4)
    _, f1 = cam()
    moved = math.remainder(yaw(f1) - yaw(f0), 2 * math.pi)
    if abs(moved) < 1e-3:
        print('the camera did not turn'); return False
    per_rad = 200 / moved
    for _ in range(8):
        _, f = cam()
        err = math.remainder(want - yaw(f), 2 * math.pi)
        if abs(err) < 0.01: break
        jc2ctl.mouse(int(err * per_rad), 0, 10)
        time.sleep(0.4)
    _, f = cam()
    print('P1 camera forward %.3f %.3f %.3f (yaw off by %.1f deg)' % (f[0], f[1], f[2],
          math.degrees(math.remainder(want - yaw(f), 2 * math.pi))))
    return True


def main(argv):
    jc2ctl.focus()
    if argv[0] == 'face':
        turn_p1([float(v) for v in argv[1:4]])
        return
    who = argv[0]
    x, y, z, fx, fy, fz = (float(v) for v in argv[1:7])
    print(jc2ctl.mod('%s at %f %f %f' % (who, x, y, z)))
    time.sleep(1.5)
    if who == 'p1':
        turn_p1([fx, fy, fz])
    else:
        print(jc2ctl.mod('p2 face %f %f %f' % (fx, fy, fz)))


if __name__ == '__main__':
    main(sys.argv[1:])
