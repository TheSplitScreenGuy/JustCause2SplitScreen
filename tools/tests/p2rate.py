# Player 2's update rate (should match the frame rate, like Player 1's CPlayer::Update).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
jc2ctl.focus()
time.sleep(0.5)
count = lambda: int(re.search(r'updates (\d+)', jc2ctl.mod('p2')).group(1))
a, t = count(), time.time()
time.sleep(3)
b = count()
print('P2 updates/s %.1f' % ((b - a) / (time.time() - t)))
