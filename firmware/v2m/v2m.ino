#include <Arduino.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <esp_heap_caps.h>
#include <driver/i2s_std.h>

#include "board_config.h"
#include "controls.h"
#include "display.h"
#include "sonification.h"

namespace {
constexpr size_t kRgbBytes = 320 * 240 * 3;
constexpr size_t kBlockFrames = 256;
constexpr size_t kDmaBlocks = 4;
uint8_t* rgb = nullptr;
i2s_chan_handle_t audio = nullptr;
bool cameraReady = false;
bool haveSong = false;
bool playing = false;
bool audioRunning = false;
bool drumsEnabled = false;
v2m::Button photoButton, extraButton;
v2m::Song song{};
v2m::Synth synth;
int16_t pcm[kBlockFrames * 2];
uint32_t completed = 0;
portMUX_TYPE clockMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t sentFrames = 0, submittedFrames = 0, underruns = 0;
uint32_t musicalFrames = 0;
uint32_t maxRenderUs = 0, maxDisplayUs = 0;

bool IRAM_ATTR onAudioSent(i2s_chan_handle_t, i2s_event_data_t* event, void*) {
  portENTER_CRITICAL_ISR(&clockMux);
  const uint32_t next = sentFrames + event->size / (2 * sizeof(int16_t));
  if (sentFrames < musicalFrames && next > submittedFrames) ++underruns;
  sentFrames = next;
  portEXIT_CRITICAL_ISR(&clockMux);
  return false;
}

uint32_t playedFrames() {
  portENTER_CRITICAL(&clockMux);
  const uint32_t value = sentFrames;
  portEXIT_CRITICAL(&clockMux);
  return value;
}

void status() {
  Serial.printf("STATUS camera=%d audio=%d song=%d playing=%d completed=%lu heap=%lu psram=%lu display=%d photos=%lu drums=%d key2=%d sent=%lu underruns=%lu render_us=%lu display_us=%lu\n",
                cameraReady, audio != nullptr, haveSong, playing,
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()), display::ready(),
                static_cast<unsigned long>(display::frameCount()), drumsEnabled,
                digitalRead(board::kExtraButton), static_cast<unsigned long>(playedFrames()),
                static_cast<unsigned long>(underruns), static_cast<unsigned long>(maxRenderUs),
                static_cast<unsigned long>(maxDisplayUs));
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
  channel.dma_desc_num = kDmaBlocks;
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
  if (error == ESP_OK) {
    i2s_event_callbacks_t callbacks{};
    callbacks.on_sent = onAudioSent;
    error = i2s_channel_register_event_callback(audio, &callbacks, nullptr);
  }
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
  portENTER_CRITICAL(&clockMux);
  submittedFrames += frames;
  portEXIT_CRITICAL(&clockMux);
  return true;
}

bool haltAudio() {
  if (!audioRunning) return true;
  const esp_err_t error = i2s_channel_disable(audio);
  if (error != ESP_OK) {
    Serial.printf("ERROR audio disable: %s\n", esp_err_to_name(error));
    return false;
  }
  audioRunning = false;
  return true;
}

void startSong(const char* source) {
  if (!haveSong) { captureError("ERROR take a photo first"); return; }
  if (!audio) { captureError("AUDIO unavailable; melody printed only"); return; }
  playing = false;
  if (!haltAudio()) { display::showState(display::State::Error); return; }
  display::showState(display::State::Playing);
  synth.setDrumsEnabled(drumsEnabled);
  synth.start(song);
  musicalFrames = synth.musicalFrames();
  maxRenderUs = maxDisplayUs = 0;
  portENTER_CRITICAL(&clockMux);
  sentFrames = submittedFrames = underruns = 0;
  portEXIT_CRITICAL(&clockMux);
  // Prime the entire DMA ring before starting its clock. This aligns the first
  // image column with the first audio sample, including when restarting mid-song.
  for (size_t i = 0; i < kDmaBlocks; ++i) {
    const size_t frames = synth.render(pcm, kBlockFrames);
    size_t loaded = 0;
    const size_t bytes = frames * 2 * sizeof(int16_t);
    const esp_err_t error = i2s_channel_preload_data(audio, pcm, bytes, &loaded);
    if (error != ESP_OK || loaded != bytes) {
      Serial.printf("ERROR audio preload: %s bytes=%u/%u\n", esp_err_to_name(error),
                    static_cast<unsigned>(loaded), static_cast<unsigned>(bytes));
      display::showState(display::State::Error);
      return;
    }
    submittedFrames += frames;
  }
  const esp_err_t error = i2s_channel_enable(audio);
  if (error != ESP_OK) {
    Serial.printf("ERROR audio enable: %s\n", esp_err_to_name(error));
    display::showState(display::State::Error);
    return;
  }
  audioRunning = true;
  playing = true;
  Serial.printf("PLAY begin source=%s drums=%d musical_frames=%lu\n", source, drumsEnabled,
                static_cast<unsigned long>(musicalFrames));
}

void toggleDrums(const char* source) {
  drumsEnabled = !drumsEnabled;
  synth.setDrumsEnabled(drumsEnabled);
  display::setDrumsEnabled(drumsEnabled);
  Serial.printf("DRUMS enabled=%d source=%s frame=%lu\n", drumsEnabled, source,
                static_cast<unsigned long>(playedFrames()));
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
                song.saturation, song.edge, song.bpm, song.root,
                song.minor ? "minor-pentatonic" : "major-pentatonic");
  for (size_t i = 0; i < v2m::kNoteCount; ++i) Serial.printf("%s%u", i ? "," : "", song.notes[i].midi);
  Serial.print(" timbres=");
  for (size_t i = 0; i < v2m::kNoteCount; ++i) Serial.printf("%s%s", i ? "," : "", v2m::timbreName(song.notes[i].timbre));
  Serial.println();
  startSong(source);
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
  // DMA also advances while sending auto-cleared silence. Do not keep drawing
  // a misleading playhead (or report success) if the producer ever falls behind.
  if (underruns) {
    playing = false;
    haltAudio();
    Serial.println("ERROR audio underrun; replay to restart the shared timeline");
    display::showState(display::State::Error);
    status();
    return;
  }
  const uint32_t renderStarted = micros();
  const size_t frames = synth.render(pcm, kBlockFrames);
  const uint32_t renderUs = micros() - renderStarted;
  if (renderUs > maxRenderUs) maxRenderUs = renderUs;
  if (frames && !writeAudio(pcm, frames)) {
    playing = false;
    haltAudio();
    display::showState(display::State::Error);
    return;
  }
  const uint32_t sent = playedFrames();
  const uint32_t displayStarted = micros();
  display::setProgress(sent, musicalFrames);
  const uint32_t displayUs = micros() - displayStarted;
  if (displayUs > maxDisplayUs) maxDisplayUs = displayUs;
  if (synth.done() && sent >= synth.totalFrames() && !underruns) {
    playing = false;
    haltAudio();
    ++completed;
    display::showState(display::State::Done);
    Serial.println("PLAY complete");
    status();
  }
}

void pollButtons() {
  const uint32_t now = millis();
  const auto photo = photoButton.update(digitalRead(board::kButton) == LOW, now);
  const auto extra = extraButton.update(digitalRead(board::kExtraButton) == LOW, now);
  if (photo == v2m::ButtonEvent::Pressed) capture("button");
  if (extra == v2m::ButtonEvent::ShortPress) startSong("key2-short");
  if (extra == v2m::ButtonEvent::LongPress) toggleDrums("key2-long");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(700);
  Serial.println("\nV2M 0.3.0");
  Serial.printf("DEVICE chip=%s flash=%lu psram=%lu\n", ESP.getChipModel(),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  pinMode(board::kButton, INPUT_PULLUP);
  pinMode(board::kExtraButton, INPUT_PULLUP);
  photoButton.begin(digitalRead(board::kButton) == LOW, millis());
  extraButton.begin(digitalRead(board::kExtraButton) == LOW, millis());
  display::begin();
  cameraReady = initCamera();
  const bool audioReady = initAudio();
  display::showState(cameraReady && audioReady ? display::State::Ready : display::State::Error);
  Serial.println("READY BOOT/BUTTON1=capture BUTTON2=short replay/hold650ms drums; c=photo p=replay d=drums x=stop s=status j=JPEG");
  status();
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'c' || c == ' ') capture("serial-test");
    else if (c == 'j') exportJpeg();
    else if (c == 'p') startSong("serial-replay");
    else if (c == 'd') toggleDrums("serial");
    else if (c == 'x') {
      playing = false;
      haltAudio();
      display::showState(display::State::Stopped);
      Serial.println("PLAY stopped");
    } else if (c == 's') status();
  }
  pollButtons();
  pumpAudio();
  delay(1);
}
