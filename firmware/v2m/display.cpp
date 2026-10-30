#include "display.h"

#include <Arduino.h>
#include <stddef.h>

#include "board_config.h"
#include "ui_assets.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace display {
namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 320;
constexpr int kPhotoY = 48;
constexpr int kPhotoHeight = 180;
constexpr int kChunkRows = 16;
constexpr int kProgressX = 12;
constexpr int kProgressY = 310;
constexpr int kProgressWidth = 216;
constexpr int kProgressHeight = 4;
constexpr int kStateY = 234;
constexpr int kStateHeight = 29;
constexpr int kDrumsX = 132;
constexpr int kDrumsY = 8;
constexpr int kDrumsWidth = 96;
constexpr int kDrumsHeight = 30;
constexpr int kScanWidth = 2;
// Merge nearby old/new lines into one transfer, but do not redraw a large gap
// after a delayed update or an active-song restart.
constexpr int kMaxMergedScanWidth = 8;
constexpr int64_t kProgressIntervalUs = 50000;
constexpr size_t kDmaBytes = kWidth * kChunkRows * sizeof(uint16_t);

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xf8U) << 8) |
                               ((green & 0xfcU) << 3) | (blue >> 3));
}

constexpr uint16_t kBackground = rgb565(12, 20, 34);
constexpr uint16_t kPhotoBackground = rgb565(20, 31, 47);
constexpr uint16_t kWhite = rgb565(235, 244, 250);
constexpr uint16_t kMuted = rgb565(125, 150, 174);
constexpr uint16_t kCyan = rgb565(104, 212, 220);
constexpr uint16_t kGreen = rgb565(123, 227, 166);
constexpr uint16_t kRed = rgb565(255, 126, 126);
constexpr uint16_t kTrack = rgb565(36, 53, 73);

esp_lcd_panel_io_handle_t panelIo = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t transferDone = nullptr;
uint16_t* framebuffer = nullptr;  // Host-endian RGB565, exclusively in PSRAM.
uint8_t* dmaBytes = nullptr;      // Internal DMA RAM, wire-order RGB565 bytes.
bool attempted = false;
bool isReady = false;
bool disabled = false;
bool backlightConfigured = false;
bool drumsEnabled = false;
State currentState = State::Starting;
uint32_t photoFrames = 0;
int progressPixels = 0;
int scanX = -1;
int photoX = 0;
int photoY = kPhotoY;
int photoWidth = kWidth;
int photoHeight = kPhotoHeight;
int64_t lastProgressUs = 0;

int scanWidth() { return photoWidth < kScanWidth ? photoWidth : kScanWidth; }

bool IRAM_ATTR onTransferDone(esp_lcd_panel_io_handle_t,
                             esp_lcd_panel_io_event_data_t*, void* context) {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context),
                        &higherPriorityTaskWoken);
  // The bundled SPI LCD transport ignores the callback return value. Wake the
  // waiting application immediately instead of waiting for the next OS tick.
  if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
  return higherPriorityTaskWoken == pdTRUE;
}

bool fail(const char* operation, esp_err_t error) {
  disabled = true;
  isReady = false;
  Serial.printf("DISPLAY error operation=%s code=%s (0x%x); disabled\n",
                operation, esp_err_to_name(error), static_cast<unsigned int>(error));
  if (backlightConfigured) {
    const esp_err_t off = gpio_set_level(static_cast<gpio_num_t>(board::kDisplayBacklight), 0);
    if (off != ESP_OK) {
      Serial.printf("DISPLAY error operation=backlight-off code=%s (0x%x)\n",
                    esp_err_to_name(off), static_cast<unsigned int>(off));
    }
  }
  // Deliberately keep panel IO, semaphore and buffers alive until reboot.
  // A failed/timed-out transfer may still own dmaBytes and call the ISR later.
  // Do not send further SPI commands, retry initialization or free that memory.
  return false;
}

bool check(const char* operation, esp_err_t error) {
  return error == ESP_OK || fail(operation, error);
}

void fillRect(int x, int y, int width, int height, uint16_t color) {
  for (int row = y; row < y + height; ++row) {
    uint16_t* pixels = framebuffer + row * kWidth + x;
    for (int column = 0; column < width; ++column) pixels[column] = color;
  }
}

void drawText(const ui_assets::TextBitmap& bitmap, int x, int y, uint16_t color) {
  const size_t stride = (bitmap.width + 7U) / 8U;
  for (uint16_t row = 0; row < bitmap.height; ++row) {
    const int targetY = y + row;
    if (targetY < 0 || targetY >= kHeight) continue;
    for (uint16_t column = 0; column < bitmap.width; ++column) {
      const int targetX = x + column;
      if (targetX < 0 || targetX >= kWidth) continue;
      if (bitmap.data[row * stride + column / 8U] & (0x80U >> (column % 8U))) {
        framebuffer[targetY * kWidth + targetX] = color;
      }
    }
  }
}

void centeredText(const ui_assets::TextBitmap& bitmap, int y, uint16_t color) {
  drawText(bitmap, (kWidth - static_cast<int>(bitmap.width)) / 2, y, color);
}

bool flushRect(int x, int y, int width, int height, int overlayScanX = -1) {
  if (disabled || !panel || !framebuffer || !dmaBytes || !transferDone) return false;
  if (width <= 0 || height <= 0 || x < 0 || y < 0 ||
      width > kWidth - x || height > kHeight - y) return false;
  // Wide rectangles still use 16 rows; a narrow scan strip fits all 180 rows
  // in the same staging allocation and therefore needs only one color transfer.
  const int rowsPerTransfer = kDmaBytes / (static_cast<size_t>(width) * sizeof(uint16_t));
  for (int row = 0; row < height; row += rowsPerTransfer) {
    const int rows = height - row < rowsPerTransfer ? height - row : rowsPerTransfer;
    size_t output = 0;
    for (int line = 0; line < rows; ++line) {
      const uint16_t* pixels = framebuffer + (y + row + line) * kWidth + x;
      for (int column = 0; column < width; ++column) {
        const int targetX = x + column;
        const int targetY = y + row + line;
        uint16_t pixel = pixels[column];
        if (overlayScanX >= 0 && targetX >= overlayScanX &&
            targetX < overlayScanX + scanWidth() &&
            targetY >= photoY && targetY < photoY + photoHeight) {
          // A dark edge and a bright cyan edge stay visible on light and dark
          // photos. Only staging bytes change; the retained photo stays exact.
          pixel = scanWidth() > 1 && targetX == overlayScanX ? kBackground : kCyan;
        }
        dmaBytes[output++] = static_cast<uint8_t>(pixel >> 8);
        dmaBytes[output++] = static_cast<uint8_t>(pixel & 0xffU);
      }
    }
    if (!check("draw-bitmap", esp_lcd_panel_draw_bitmap(
        panel, x, y + row, x + width, y + row + rows, dmaBytes))) return false;
    // There is exactly one outstanding transfer; never overwrite staging RAM
    // until the driver's completion callback releases this semaphore.
    if (xSemaphoreTake(transferDone, pdMS_TO_TICKS(100)) != pdTRUE) {
      return fail("color-transfer-timeout", ESP_ERR_TIMEOUT);
    }
  }
  return true;
}

bool moveScanLine(int nextX) {
  if (nextX == scanX) return true;
  if (scanX >= 0 && nextX >= 0) {
    const int left = scanX < nextX ? scanX : nextX;
    const int right = scanX > nextX ? scanX : nextX;
    const int width = right - left + scanWidth();
    if (width <= kMaxMergedScanWidth) {
      // Old line pixels come from the unchanged photo; new line is an overlay.
      if (!flushRect(left, photoY, width, photoHeight, nextX)) return false;
      scanX = nextX;
      return true;
    }
  }
  if (scanX >= 0 && !flushRect(scanX, photoY, scanWidth(), photoHeight)) return false;
  if (nextX >= 0 && !flushRect(nextX, photoY, scanWidth(), photoHeight, nextX)) return false;
  scanX = nextX;
  return true;
}

void drawDrums() {
  fillRect(kDrumsX, kDrumsY, kDrumsWidth, kDrumsHeight, kBackground);
  const ui_assets::TextBitmap& label = drumsEnabled ? ui_assets::kDrumsOn : ui_assets::kDrumsOff;
  drawText(label, kWidth - 12 - label.width, 14, drumsEnabled ? kGreen : kMuted);
}

uint16_t stateColor(State state) {
  switch (state) {
    case State::Ready:
    case State::Done: return kCyan;
    case State::Playing: return kGreen;
    case State::Error: return kRed;
    default: return kMuted;
  }
}

const ui_assets::TextBitmap& stateText(State state) {
  switch (state) {
    case State::Starting: return ui_assets::kStarting;
    case State::Ready: return ui_assets::kReady;
    case State::Capturing: return ui_assets::kCapturing;
    case State::Playing: return ui_assets::kPlaying;
    case State::Done: return ui_assets::kDone;
    case State::Stopped: return ui_assets::kStopped;
    case State::Error: return ui_assets::kError;
  }
  return ui_assets::kError;
}

void drawProgress() {
  fillRect(kProgressX, kProgressY, kProgressWidth, kProgressHeight, kTrack);
  if (progressPixels > 0) {
    fillRect(kProgressX, kProgressY, progressPixels, kProgressHeight,
             currentState == State::Error ? kRed : kGreen);
  }
}

void drawState(State state) {
  currentState = state;
  switch (state) {
    case State::Starting:
    case State::Ready:
    case State::Capturing:
    case State::Playing: progressPixels = 0; break;
    case State::Done: progressPixels = kProgressWidth; break;
    case State::Stopped:
    case State::Error: break;
  }
  fillRect(0, kStateY, kWidth, kStateHeight, kBackground);
  centeredText(stateText(state), 236, stateColor(state));
  drawProgress();
  lastProgressUs = esp_timer_get_time();
}

}  // namespace

bool begin() {
  if (attempted) return isReady;
  attempted = true;

  gpio_config_t backlight = {};
  backlight.pin_bit_mask = 1ULL << board::kDisplayBacklight;
  backlight.mode = GPIO_MODE_OUTPUT;
  if (!check("backlight-config", gpio_config(&backlight))) return false;
  backlightConfigured = true;
  if (!check("backlight-off", gpio_set_level(
      static_cast<gpio_num_t>(board::kDisplayBacklight), 0))) return false;

  framebuffer = static_cast<uint16_t*>(heap_caps_malloc(
      kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!framebuffer) return fail("allocate-psram-framebuffer", ESP_ERR_NO_MEM);
  dmaBytes = static_cast<uint8_t*>(heap_caps_malloc(
      kDmaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!dmaBytes) return fail("allocate-dma-staging", ESP_ERR_NO_MEM);
  transferDone = xSemaphoreCreateBinary();
  if (!transferDone) return fail("allocate-transfer-semaphore", ESP_ERR_NO_MEM);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = board::kDisplayMosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = board::kDisplayClk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = kDmaBytes;
  if (!check("spi-bus-initialize", spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO))) return false;

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = board::kDisplayCs;
  io.dc_gpio_num = board::kDisplayDc;
  io.spi_mode = 0;
  io.pclk_hz = 20000000;
  io.trans_queue_depth = 1;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  io.on_color_trans_done = onTransferDone;
  io.user_ctx = transferDone;
  if (!check("new-panel-io", esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &panelIo))) return false;

  esp_lcd_panel_dev_config_t config = {};
  config.reset_gpio_num = board::kDisplayRst;
  config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
  config.bits_per_pixel = 16;
  if (!check("new-st7789", esp_lcd_new_panel_st7789(panelIo, &config, &panel))) return false;
  if (!check("panel-reset", esp_lcd_panel_reset(panel))) return false;
  if (!check("panel-init", esp_lcd_panel_init(panel))) return false;
  if (!check("panel-invert", esp_lcd_panel_invert_color(panel, true))) return false;
  if (!check("panel-swap-xy", esp_lcd_panel_swap_xy(panel, false))) return false;
  if (!check("panel-mirror", esp_lcd_panel_mirror(panel, false, false))) return false;
  if (!check("panel-gap", esp_lcd_panel_set_gap(panel, 0, 0))) return false;
  if (!check("panel-on", esp_lcd_panel_disp_on_off(panel, true))) return false;

  fillRect(0, 0, kWidth, kHeight, kBackground);
  drawText(ui_assets::kBrand, 12, 12, kWhite);
  drawDrums();
  fillRect(0, kPhotoY, kWidth, kPhotoHeight, kPhotoBackground);
  centeredText(ui_assets::kNoPhoto, 130, kMuted);
  centeredText(ui_assets::kHintCapture, 267, kMuted);
  centeredText(ui_assets::kHintControls, 287, kMuted);
  drawState(State::Starting);
  if (!flushRect(0, 0, kWidth, kHeight)) return false;
  if (!check("backlight-on", gpio_set_level(
      static_cast<gpio_num_t>(board::kDisplayBacklight), 1))) return false;
  isReady = true;
  Serial.println("DISPLAY ready controller=ST7789 size=240x320 spi_hz=20000000; physical image not verified");
  return true;
}

bool ready() { return isReady; }

void showState(State state) {
  if (!isReady || (currentState == state && state != State::Playing)) return;
  // Replaying an active song must reset even if its label remains Playing.
  // Non-playing states restore the clean photo, including completion and stop.
  if (!moveScanLine(state == State::Playing && photoFrames ? photoX : -1)) return;
  drawState(state);
  if (!flushRect(0, kStateY, kWidth, kStateHeight)) return;
  flushRect(kProgressX, kProgressY, kProgressWidth, kProgressHeight);
}

bool showPhoto(const uint8_t* rgb, uint16_t width, uint16_t height) {
  if (!isReady) return false;
  if (!rgb || !width || !height ||
      static_cast<uint64_t>(width) * height * 3U > SIZE_MAX) {
    Serial.println("DISPLAY error operation=show-photo-invalid-input");
    return false;
  }
  // Preserve every part of the image. The normal 4:3 camera frame fills the
  // entire 240 x 180 area; other aspect ratios receive centered letterboxing.
  photoWidth = kWidth;
  photoHeight = kPhotoHeight;
  if (static_cast<uint32_t>(width) * kPhotoHeight > static_cast<uint32_t>(height) * kWidth) {
    photoHeight = static_cast<uint32_t>(height) * kWidth / width;
    if (!photoHeight) photoHeight = 1;
  } else {
    photoWidth = static_cast<uint32_t>(width) * kPhotoHeight / height;
    if (!photoWidth) photoWidth = 1;
  }
  photoX = (kWidth - photoWidth) / 2;
  photoY = kPhotoY + (kPhotoHeight - photoHeight) / 2;
  scanX = -1;  // The complete photo area below replaces any previous overlay.
  fillRect(0, kPhotoY, kWidth, kPhotoHeight, kPhotoBackground);
  for (int y = 0; y < photoHeight; ++y) {
    const size_t sourceY = static_cast<uint32_t>(y) * height / photoHeight;
    for (int x = 0; x < photoWidth; ++x) {
      const size_t sourceX = static_cast<uint32_t>(x) * width / photoWidth;
      const uint8_t* source = rgb + (sourceY * width + sourceX) * 3;
      framebuffer[(photoY + y) * kWidth + photoX + x] = rgb565(source[0], source[1], source[2]);
    }
  }
  if (!flushRect(0, kPhotoY, kWidth, kPhotoHeight)) return false;
  ++photoFrames;
  return true;
}

void setProgress(uint32_t frames, uint32_t totalFrames) {
  if (!isReady || currentState != State::Playing) return;
  if (frames > totalFrames) frames = totalFrames;
  const int pixels = totalFrames ? static_cast<uint64_t>(frames) * kProgressWidth / totalFrames : 0;
  const int scanRange = photoWidth - scanWidth();
  int nextScanX = -1;
  if (photoFrames) {
    nextScanX = photoX + static_cast<int>(totalFrames ?
        static_cast<uint64_t>(frames) * scanRange / totalFrames : 0);
  }
  if (pixels == progressPixels && nextScanX == scanX) return;
  const int64_t now = esp_timer_get_time();
  const bool final = totalFrames && frames == totalFrames;
  if (!final && now - lastProgressUs < kProgressIntervalUs) return;
  if (!moveScanLine(nextScanX)) return;
  const int previousPixels = progressPixels;
  progressPixels = pixels;
  lastProgressUs = now;
  const int changedX = pixels < previousPixels ? pixels : previousPixels;
  const int changedWidth = pixels < previousPixels ? previousPixels - pixels : pixels - previousPixels;
  if (changedWidth > 0) {
    fillRect(kProgressX + changedX, kProgressY, changedWidth, kProgressHeight,
             pixels < previousPixels ? kTrack : kGreen);
    flushRect(kProgressX + changedX, kProgressY, changedWidth, kProgressHeight);
  }
}

void setDrumsEnabled(bool enabled) {
  if (drumsEnabled == enabled) return;
  drumsEnabled = enabled;
  if (!isReady) return;
  drawDrums();
  flushRect(kDrumsX, kDrumsY, kDrumsWidth, kDrumsHeight);
}

uint32_t frameCount() { return photoFrames; }

}  // namespace display
