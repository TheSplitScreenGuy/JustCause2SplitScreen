# FMOD's playing channels with the players apart: quiet, then each player's grapple, then gunfire.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

print(jc2ctl.mod('p2 at 892 225 -2500'))
time.sleep(6)
print('quiet', jc2ctl.mod('p2audio dump'))
print(jc2ctl.mod('p2 padpress 100 300'))      # P2 LB: grapple
time.sleep(0.25)
print('P2 grapple', jc2ctl.mod('p2audio dump'))
time.sleep(2)
print(jc2ctl.mod('p2 padpress 200 200'))      # P2 RB: roll
time.sleep(0.2)
print('P2 roll', jc2ctl.mod('p2audio dump'))
time.sleep(1.5)
print(jc2ctl.mod('p2 press D+B 300'))         # P2 fires
time.sleep(0.2)
print('P2 fire', jc2ctl.mod('p2audio dump'))
time.sleep(1.5)
jc2ctl.focus()
jc2ctl.click('left', 200)
print('P1 fire', jc2ctl.mod('p2audio dump'))
