#include "controls.h"

#include <cassert>
#include <cstdio>

using v2m::ButtonEvent;

int main() {
  v2m::Button button;
  button.begin(false, 0);
  assert(button.update(true, 10) == ButtonEvent::None);
  assert(button.update(false, 20) == ButtonEvent::None);
  assert(button.update(true, 25) == ButtonEvent::None);
  assert(button.update(true, 59) == ButtonEvent::None);
  assert(button.update(true, 60) == ButtonEvent::Pressed);
  assert(button.update(false, 100) == ButtonEvent::None);
  assert(button.update(false, 135) == ButtonEvent::ShortPress);
  assert(button.update(false, 200) == ButtonEvent::None);

  assert(button.update(true, 300) == ButtonEvent::None);
  assert(button.update(true, 335) == ButtonEvent::Pressed);
  assert(button.update(true, 984) == ButtonEvent::None);
  assert(button.update(true, 985) == ButtonEvent::LongPress);
  assert(button.update(true, 2000) == ButtonEvent::None);
  assert(button.update(false, 2100) == ButtonEvent::None);
  assert(button.update(false, 2135) == ButtonEvent::None);

  // A blocking capture can skip the hold threshold; releasing must still emit
  // only LongPress instead of unexpectedly replaying the song.
  button.begin(false, 0);
  assert(button.update(true, 10) == ButtonEvent::None);
  assert(button.update(true, 45) == ButtonEvent::Pressed);
  assert(button.update(false, 800) == ButtonEvent::None);
  assert(button.update(false, 835) == ButtonEvent::LongPress);
  assert(button.update(false, 900) == ButtonEvent::None);

  // A key held at power-up must not silently toggle drums or replay on release.
  button.begin(true, 0);
  assert(button.update(true, 1000) == ButtonEvent::None);
  assert(button.update(false, 1100) == ButtonEvent::None);
  assert(button.update(false, 1135) == ButtonEvent::None);

  // Both debounce and long-press intervals work across uint32_t rollover.
  button.begin(false, 0xffffffd0U);
  assert(button.update(true, 0xffffffe0U) == ButtonEvent::None);
  assert(button.update(true, 3) == ButtonEvent::Pressed);
  assert(button.update(true, 653) == ButtonEvent::LongPress);
  std::puts("PASS: button bounce, short/long separation, startup-held key, timer rollover");
}
