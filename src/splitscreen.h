#pragma once

// Decides when the split view is on.
// Player 2 joins by pressing Start on their controller and leaves with Back + Start.
// The split is only shown while Player 2 is joined AND the game is in normal
// gameplay (never during cutscenes or menus).

enum class Player2Status {
    NoController, // configured XInput slot has no controller
    Waiting,      // controller connected, Start not pressed yet
    Joined,
};

// Call once per frame from the render thread.
void SplitscreenUpdate();

Player2Status SplitscreenPlayer2Status();
int SplitscreenPlayer2Slot(); // 0-based XInput slot, set in jc2ss.ini

// True only when the two views should be drawn.
bool SplitscreenActive();

// Normal gameplay: no menu, map, cutscene or script hold (the split may be shown).
bool SplitscreenInGameplay();

// Whether a cutscene is loaded or playing (user: Player 2's view off then; the split is off).
bool SplitscreenCutscene();

// Whether a script has taken Player 1's control away (the race challenge screen before a race;
// user: Player 2's view off so Player 1 sees that screen whole, back on when he gets control).
bool SplitscreenPlayer1Held();

// Player 2 called the black market dealer: the menu that opens next takes pad 2 as pad 1.
void SplitscreenPlayer2CalledDealer();

// Testing ("p2rumble <low> <high>"): pad 2's motors per unit of Player 2's free fall strength.
void SplitscreenSetRumbleScale(float low, float high);

// Testing: acts as if Player 2 had joined.
void SplitscreenForce(bool on);

// Testing: same as Player 2 pressing Start (join) or Back + Start (leave) on their controller.
void SplitscreenSimulateJoin(bool joined);

// Player 2's sticks, -1..1 with dead zone applied, triggers 0..1 and XInput button bits
// (all 0 if no controller).
struct Player2Sticks {
    float leftX, leftY, rightX, rightY;
    float leftTrigger, rightTrigger;
    unsigned short buttons;
};
Player2Sticks SplitscreenPlayer2Sticks();
