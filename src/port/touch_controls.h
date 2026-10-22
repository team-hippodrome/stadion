#pragma once
// On-screen controller for touch screens (touch_controls.cpp).

// Window thread, once per frame with controller 1's pad (an OSContPad) as the ControlDeck read it: adds the buttons and
// the stick of the fingers on the on-screen controller when it shows. gui: libultraship's Ship::Gui (the overlay is
// added to it on the first call).
void Port_TouchControlsApply(void* pad0, void* gui);
