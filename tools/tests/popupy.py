# Mission popup position test: restarts with Player 2 joined from the menu, and as soon as the save
# is in, sets the 138.0 floats of the Anark objects of the given class to a new value, then takes
# screenshots while the "Agency Mission Started" popup shows (tools\shots\popupy_<n>.png).
#   python -I popupy.py <class substring> <value>
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import os, subprocess, struct, sys, time, json
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl
from procmem import Game

want, value = sys.argv[1], float(sys.argv[2])
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
S = os.path.join(_ROOT, r'tools\shots\popupy_%d.png')
subprocess.run([sys.executable, '-I', os.path.join(_ROOT, r'tools\jc2ctl.py'), 'close'])
os.startfile('steam://rungameid/8190')
end = time.time() + 120
while True:
    try:
        jc2ctl.mod('ping', timeout=5); break
    except SystemExit:
        if time.time() > end: raise SystemExit('game never started rendering')
time.sleep(10)
print(jc2ctl.mod('p2 join'))
for attempt in range(8):
    jc2ctl.focus(); jc2ctl.tap('enter'); time.sleep(4)
    if not jc2ctl.mod('cam').startswith('pos 0.000 0.000 0.000'): break
g = Game(write=True)
patched = []
for tries in range(20):
    for h in g.find_dwords([0x430A0000], limit=200000)[0x430A0000]:
        for back in range(0, 0x200, 4):
            n = names.get('%08X' % g.u32(h - back))
            if n:
                if want in n:
                    g.write(h, struct.pack('<f', value)); patched.append((h, n, back))
                break
    if patched: break
    time.sleep(0.5)
print('patched', [('%08X' % h, n, '+0x%X' % b) for h, n, b in patched])
for i in range(8):
    jc2ctl.mod('screenshot ' + S % i); time.sleep(1.0)
