#pragma once

struct IDXGISwapChain;

// Test/automation channel. tools\jc2ctl.py writes jc2ss_cmd.txt in the game folder:
//   id <n>
//   <command> [args]
// and the mod answers in jc2ss_result.txt ("id <n>" then output lines).
// Commands: ping, cam, screenshot <path>, watch <w|rw|x> <ms> <addr> [addr...],
//           camoffset <dx> <dy> <dz> [yaw_deg] | camoffset off, split on|off
void ControlOnPresent(IDXGISwapChain* swapChain);
