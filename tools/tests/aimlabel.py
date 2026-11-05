# The destructible's name/health label ("Statue Torso") per player: it should show in a half only
# while that half's player aims at the statue. Screenshots: tools\shots\aimlabel_<case>.png
#   python -I aimlabel.py
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

SHOTS = os.path.join(_ROOT, r'tools\shots')
SPOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'spot.py')
AT_STATUE = ['633.3', '222.2', '-2566.7']
FACE = ['0.946', '-0.191', '-0.263']       # camera forward toward the statue (from "cam")
AWAY = ['0.263', '-0.191', '0.946']


def spot(*args):
    print(subprocess.run([sys.executable, '-I', SPOT] + list(args), capture_output=True, text=True).stdout.strip())


def shot(name):
    time.sleep(1.5)
    jc2ctl.mod('screenshot %s\\aimlabel_%s.png' % (SHOTS, name))
    time.sleep(0.5)


jc2ctl.focus()
spot('p1', *AT_STATUE, *FACE)
print(jc2ctl.mod('p2 at 988 203 -1499'))           # far away, looking at a beach
time.sleep(6)
shot('p1only')
print(jc2ctl.mod('p2 at 632.3 222.5 -2563.7'))      # 3 m beside P1
time.sleep(5)
print(jc2ctl.mod('p2 face %s %s %s' % tuple(FACE)))
shot('both')
spot('face', *AWAY)
shot('p2only')
spot('face', *FACE)
