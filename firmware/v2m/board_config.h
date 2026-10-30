#pragma once

// Seller V2.0 schematic and bundled bread-compact-wifi-s3cam/config.h.
// Archived evidence: vendor/goouuu-s3-cam (see its README).
namespace board {
constexpr bool kCameraPinsVerified = true;
constexpr bool kAudioPinsVerified = true;
constexpr int kButton = 0;  // ESP32-S3 BOOT; use only after normal startup.
// Bottom board BUTTON1 shares GPIO0 with BOOT. BUTTON2 is the only extra
// independent button; active low, with the internal pull-up enabled at runtime.
constexpr int kExtraButton = 46;
constexpr int kCameraXclk = 15;
constexpr int kCameraSda = 4;
constexpr int kCameraScl = 5;
constexpr int kCameraData[8] = {11, 9, 8, 10, 12, 18, 17, 16};
constexpr int kCameraVsync = 6;
constexpr int kCameraHref = 7;
constexpr int kCameraPclk = 13;
constexpr int kAudioBclk = 40;
constexpr int kAudioWs = 41;
constexpr int kAudioData = 39;
// Seller's ST7789 240x320 configuration; SPI3, RGB, inversion on.
// GPIO19/20 overlap native USB: use the CH340 / TTL connector.
constexpr int kDisplayMosi = 20;
constexpr int kDisplayClk = 19;
constexpr int kDisplayDc = 47;
constexpr int kDisplayRst = 21;
constexpr int kDisplayCs = 45;
constexpr int kDisplayBacklight = 38;
}  // namespace board
