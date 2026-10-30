#include <Arduino.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <esp_heap_caps.h>
#include <driver/i2s_std.h>

#include "board_config.h"
#include "display.h"
#include "sonification.h"

namespace {
constexpr size_t kRgbBytes = 320 * 240 * 3;
constexpr size_t kBlockFrames = 256;
uint8_t* rgb = nullptr;
i2s_chan_handle_t audio = nullptr;
bool cameraReady = false;
bool haveSong = false;
bool playing = false;
v2m::Song song{};
v2m::Synth synth;
int16_t pcm[kBlockFrames * 2];
uint32_t completed = 0;

void status() {
  Serial.printf("STATUS camera=%d audio=%d song=%d playing=%d completed=%lu heap=%lu psram=%lu display=%d photos=%lu\n",
                cameraReady, audio != nullptr, haveSong, playing,
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()), display::ready(),
                static_cast<unsigned long>(display::frameCount()));
}

void captureError(const char* message) {
  Serial.println(message);
  display::showState(display::State::Error);
}

bool initCamera() {
  if (!board::kCameraPinsVerified) {
    Serial.println("CAMERA disabled: board pinout awaiting verification");
    return false;
  }
  if (!psramFound()) {
    Serial.println("ERROR camera needs PSRAM; check build settings");
    return false;
  }
  camera_config_t c{};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = board::kCameraData[0]; c.pin_d1 = board::kCameraData[1];
  c.pin_d2 = board::kCameraData[2]; c.pin_d3 = board::kCameraData[3];
  c.pin_d4 = board::kCameraData[4]; c.pin_d5 = board::kCameraData[5];
  c.pin_d6 = board::kCameraData[6]; c.pin_d7 = board::kCameraData[7];
  c.pin_xclk = board::kCameraXclk;
  c.pin_pclk = board::kCameraPclk;
  c.pin_vsync = board::kCameraVsync;
  c.pin_href = board::kCameraHref;
  c.pin_sccb_sda = board::kCameraSda;
  c.pin_sccb_scl = board::kCameraScl;
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  esp_err_t error = esp_camera_init(&c);
  if (error != ESP_OK) {
    Serial.printf("ERROR camera init: %s\n", esp_err_to_name(error));
    return false;
  }
  rgb = static_cast<uint8_t*>(heap_caps_malloc(kRgbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb) {
    Serial.println("ERROR RGB allocation failed");
    esp_camera_deinit();
    return false;
  }
  const sensor_t* sensor = esp_camera_sensor_get();
  Serial.printf("CAMERA ready pid=0x%04x format=JPEG size=320x240\n", sensor->id.PID);
  return true;
}

bool initAudio() {
  if (!board::kAudioPinsVerified) {
    Serial.println("AUDIO disabled: board pinout awaiting verification");
    return false;
  }
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 4;
  channel.dma_frame_num = kBlockFrames;
  channel.auto_clear = true;
  esp_err_t error = i2s_new_channel(&channel, &audio, nullptr);
  if (error == ESP_OK) {
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(v2m::Synth::kSampleRate);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk = static_cast<gpio_num_t>(board::kAudioBclk);
    config.gpio_cfg.ws = static_cast<gpio_num_t>(board::kAudioWs);
    config.gpio_cfg.dout = static_cast<gpio_num_t>(board::kAudioData);
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    error = i2s_channel_init_std_mode(audio, &config);
  }
  if (error == ESP_OK) error = i2s_channel_enable(audio);
  if (error != ESP_OK) {
    Serial.printf("ERROR audio init: %s\n", esp_err_to_name(error));
    if (audio) i2s_del_channel(audio);
    audio = nullptr;
    return false;
  }
  Serial.printf("AUDIO ready rate=%lu stereo16 gain<=0.175\n",
                static_cast<unsigned long>(v2m::Synth::kSampleRate));
  return true;
}

bool writeAudio(const int16_t* data, size_t frames) {
  size_t written = 0;
  const size_t bytes = frames * 2 * sizeof(int16_t);
  const esp_err_t error = i2s_channel_write(audio, data, bytes, &written, 1000);
  if (error != ESP_OK || written != bytes) {
    Serial.printf("ERROR audio write: %s bytes=%u/%u\n", esp_err_to_name(error),
                  static_cast<unsigned>(written), static_cast<unsigned>(bytes));
    return false;
  }
  return true;
}

void startSong() {
  if (!haveSong) { captureError("ERROR take a photo first"); return; }
  if (!audio) { captureError("AUDIO unavailable; melody printed only"); return; }
  display::showState(display::State::Playing);
  synth.start(song);
  playing = true;
  Serial.println("PLAY begin");
}

void capture(const char* source) {
  if (playing) { Serial.println("BUSY playing; x stops playback"); return; }
  if (!cameraReady) { captureError("ERROR camera unavailable"); return; }
  const uint32_t started = millis();
  Serial.printf("CAPTURE source=%s\n", source);
  display::showState(display::State::Capturing);
  // A single-buffer camera may hold a frame from the last request. Discard
  // two frames so each press samples the current scene after exposure settles.
  for (int i = 0; i < 2; ++i) {
    camera_fb_t* old = esp_camera_fb_get();
    if (!old) { captureError("ERROR camera warmup timed out"); return; }
    esp_camera_fb_return(old);
  }
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) { captureError("ERROR capture timed out"); return; }
  const uint16_t width = frame->width, height = frame->height;
  const size_t bytes = static_cast<size_t>(width) * height * 3;
  if (!width || !height || bytes > kRgbBytes || frame->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(frame);
    captureError("ERROR unexpected frame dimensions or format");
    return;
  }
  const bool decoded = fmt2rgb888(frame->buf, frame->len, frame->format, rgb);
  esp_camera_fb_return(frame);
  if (!decoded) { captureError("ERROR JPEG decode failed"); return; }
  // In the pinned Arduino ESP32 3.3.10 package, the JPEG path uses esp_jpeg
  // with swap_color_bytes=0 and outputs RGB. Do not apply an extra R/B swap.
  if (!v2m::composeRgb888(rgb, bytes, width, height, song)) {
    captureError("ERROR image analysis failed"); return;
  }
  haveSong = true;
  const uint32_t analysisMs = millis() - started;
  const uint32_t displayStarted = millis();
  if (display::showPhoto(rgb, width, height)) {
    Serial.printf("DISPLAY photo=%lu draw_ms=%lu\n",
                  static_cast<unsigned long>(display::frameCount()),
                  static_cast<unsigned long>(millis() - displayStarted));
  }
  Serial.printf("SONG capture_ms=%lu brightness=%.3f saturation=%.3f edge=%.3f bpm=%u root=%u scale=%s notes=",
                static_cast<unsigned long>(analysisMs), song.brightness,
                song.saturation, song.edge, song.bpm, song.root, song.minor ? "minor" : "major");
  for (size_t i = 0; i < v2m::kNoteCount; ++i) Serial.printf("%s%u", i ? "," : "", song.notes[i].midi);
  Serial.println();
  startSong();
}

void exportJpeg() {
  if (playing || !cameraReady) { Serial.println("ERROR JPEG export needs idle camera"); return; }
  for (int i = 0; i < 2; ++i) {
    camera_fb_t* old = esp_camera_fb_get();
    if (!old) { Serial.println("ERROR camera warmup timed out"); return; }
    esp_camera_fb_return(old);
  }
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) { Serial.println("ERROR capture timed out"); return; }
  if (frame->format != PIXFORMAT_JPEG || frame->len > 256 * 1024) {
    esp_camera_fb_return(frame);
    Serial.println("ERROR invalid JPEG frame");
    return;
  }
  Serial.printf("JPEG %u\n", static_cast<unsigned>(frame->len));
  Serial.write(frame->buf, frame->len);
  Serial.flush();
  esp_camera_fb_return(frame);
  Serial.println("\nJPEG END");
}

void pumpAudio() {
  if (!playing) return;
  const size_t frames = synth.render(pcm, kBlockFrames);
  if (frames && !writeAudio(pcm, frames)) {
    playing = false;
    display::showState(display::State::Error);
    return;
  }
  display::setProgress(synth.renderedFrames(), synth.totalFrames());
  if (synth.done()) {
    // The synth already supplies 100 ms of silence, longer than the DMA queue.
    playing = false;
    ++completed;
    display::showState(display::State::Done);
    Serial.println("PLAY complete");
    status();
  }
}

void pollButton() {
  static bool previous = HIGH, stable = HIGH;
  static uint32_t changedAt = 0;
  const bool current = digitalRead(board::kButton);
  if (current != previous) { previous = current; changedAt = millis(); }
  if (current != stable && millis() - changedAt >= 35) {
    stable = current;
    if (stable == LOW) capture("button");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(700);
  Serial.println("\nV2M 0.2.0");
  Serial.printf("DEVICE chip=%s flash=%lu psram=%lu\n", ESP.getChipModel(),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  pinMode(board::kButton, INPUT_PULLUP);
  display::begin();
  cameraReady = initCamera();
  const bool audioReady = initAudio();
  display::showState(cameraReady && audioReady ? display::State::Ready : display::State::Error);
  Serial.println("READY BOOT or c=photo+music p=replay x=stop s=status j=JPEG export");
  status();
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'c' || c == ' ') capture("serial-test");
    else if (c == 'j') exportJpeg();
    else if (c == 'p' && !playing) startSong();
    else if (c == 'x') {
      playing = false;
      // Replace queued sound with silence; auto-clear silences idle DMA buffers.
      if (audio) { memset(pcm, 0, sizeof(pcm)); for (int i = 0; i < 4; ++i) if (!writeAudio(pcm, kBlockFrames)) break; }
      display::showState(display::State::Stopped);
      Serial.println("PLAY stopped");
    } else if (c == 's') status();
  }
  pollButton();
  pumpAudio();
  delay(1);
}
