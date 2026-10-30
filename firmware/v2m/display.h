#pragma once

#include <stdint.h>

// Calls belong to the main application task; this module is not reentrant.
// Successful SPI transfers do not establish that a physical screen is attached.
namespace display {

enum class State { Starting, Ready, Capturing, Playing, Done, Stopped, Error };

bool begin();
bool ready();
void showState(State state);
// Packed RGB888 (R, G, B); the caller owns width * height * 3 valid bytes.
// Image is fitted without cropping into the 240 x 180 photo area.
bool showPhoto(const uint8_t* rgb, uint16_t width, uint16_t height);
void setProgress(uint32_t frames, uint32_t totalFrames);
// Number of photos whose complete SPI transfer finished successfully.
uint32_t frameCount();

}  // namespace display
