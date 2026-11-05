# Starts the game with Player 2 joined from the menu (so the split is on as the save loads), loads
# the latest autosave and takes screenshots while it comes in (tools\shots\loadjoin_<n>.png).
#   python -I loadjoin.py [shots] [seconds between]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

shots = int(sys.argv[1]) if len(sys.argv) > 1 else 12
every = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
S = os.path.join(_ROOT, r'tools\shots\loadjoin_%d.png')

running = subprocess.run(['powershell', '-NoProfile', '-Command', '[bool](Get-Process JustCause2 -ErrorAction SilentlyContinue)'],
                         capture_output=True, text=True).stdout.strip() == 'True'
if not running:
    os.startfile('steam://rungameid/8190')
end = time.time() + 120
while True:
    try:
        jc2ctl.mod('ping', timeout=5)
        break
    except SystemExit:
        if time.time() > end:
            raise SystemExit('game never started rendering')
time.sleep(10)
print(jc2ctl.mod('p2 join'))
for attempt in range(8):
    jc2ctl.focus()
    jc2ctl.tap('enter')
    time.sleep(4)
    if not jc2ctl.mod('cam').startswith('pos 0.000 0.000 0.000'):
        break
else:
    raise SystemExit('could not start loading the save')
for i in range(shots):
    time.sleep(every)
    jc2ctl.mod('screenshot ' + S % i)
print(jc2ctl.mod('p2'))
