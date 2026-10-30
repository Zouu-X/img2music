#pragma once

#include <stdint.h>

namespace v2m {

enum class ButtonEvent { None, Pressed, ShortPress, LongPress };

// Active-low wiring is converted to a pressed bool by the caller. A held key
// at startup is ignored until released. All intervals tolerate millis rollover.
class Button {
 public:
  void begin(bool pressed, uint32_t now) {
    raw_ = stable_ = pressed;
    changedAt_ = now;
    tracking_ = longFired_ = false;
  }

  ButtonEvent update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
    if (raw_ != stable_ && uint32_t(now - changedAt_) >= 35) {
      stable_ = raw_;
      if (stable_) {
        pressedAt_ = now;
        tracking_ = true;
        longFired_ = false;
        return ButtonEvent::Pressed;
      }
      const bool pending = tracking_ && !longFired_;
      const bool wasLong = uint32_t(changedAt_ - pressedAt_) >= 650;
      tracking_ = false;
      if (!pending) return ButtonEvent::None;
      return wasLong ? ButtonEvent::LongPress : ButtonEvent::ShortPress;
    }
    if (stable_ && raw_ && tracking_ && !longFired_ && uint32_t(now - pressedAt_) >= 650) {
      longFired_ = true;
      return ButtonEvent::LongPress;
    }
    return ButtonEvent::None;
  }

 private:
  bool raw_ = false, stable_ = false, tracking_ = false, longFired_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0;
};

}  // namespace v2m
