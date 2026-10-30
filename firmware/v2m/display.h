#pragma once

#include <stdint.h>

// Calls belong to the main application task; this module is not reentrant.
// Successful SPI transfers do not establish that a physical screen is attached.
namespace display {

enum class State { Starting, Ready, Capturing, Playing, Done, Stopped, Error };

bool begin();
bool ready();
// Playing always resets progress and the scan line, including an active replay.
void showState(State state);
// Packed RGB888 (R, G, B); the caller owns width * height * 3 valid bytes.
// Image is fitted without cropping into the 240 x 180 photo area.
bool showPhoto(const uint8_t* rgb, uint16_t width, uint16_t height);
// Pass frames actually played by I2S and the musical duration (no silent tail).
// Updates are limited to 20 Hz except an explicit end-of-song update.
void setProgress(uint32_t frames, uint32_t totalFrames);
// Can be called before begin(); the selected setting is retained for the header.
void setDrumsEnabled(bool enabled);
// Number of photos whose complete SPI transfer finished successfully.
uint32_t frameCount();

}  // namespace display
