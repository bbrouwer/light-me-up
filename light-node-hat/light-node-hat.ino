#include <FastLED.h>
#include <SPIFFS.h>
#include <stdarg.h>
#include <stdio.h>

const char* ANIMATION = "spiral";

// FastLED settings
const byte HAT_PIN = 32;  // BLACK
const int HAT_LEN = 32;
CRGB hat_leds[HAT_LEN];
File hat_file;

const byte BRIGHTNESS = 255;

// timers
hw_timer_t *frameTimer = NULL;
volatile SemaphoreHandle_t frameSemaphore;
boolean frameTimerEnabled = false;
void IRAM_ATTR onFrameTimer() { xSemaphoreGiveFromISR(frameSemaphore, NULL); }

// debugging
// #define DEBUG_SERIAL 1

void setup() {
    delay(1000);
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
#endif

    if (!SPIFFS.begin()) {
        debugMsg("Cannot open SPIFFS");
        return;
    }

    FastLED.addLeds<WS2812B, HAT_PIN, GRB>(hat_leds, HAT_LEN);
    FastLED.setBrightness(BRIGHTNESS);

    frameSemaphore = xSemaphoreCreateBinary();

    prepare(ANIMATION);
    startPlayback();
}

void loop() {
    if (xSemaphoreTake(frameSemaphore, 0) == pdTRUE) {
        FastLED.show();
        loadNextFrame();
    }
}

void prepare(const char *path) {
    hat_file = openAnimation(path, "hat", hat_file);
    loadNextFrame();
}

File openAnimation(const char *path, const char *suffix, File previousFile) {
    char fullPath[70];
    snprintf(fullPath, sizeof(fullPath), "/%s_%s.gled", path, suffix);
    debugMsg("Opening animation %s", fullPath);

    if (previousFile) previousFile.close();

    if (SPIFFS.exists(fullPath)) {
        File animation = SPIFFS.open(fullPath);
        if (!animation) debugMsg("Cannot open %s", fullPath);
        return animation;
    } else {
        snprintf(fullPath, sizeof(fullPath), "/default_%s.gled", suffix);
        File animation = SPIFFS.open(fullPath);
        if (!animation) debugMsg("Cannot open default animation");
        return animation;
    }
}

void loadNextFrame() {
    unsigned long start = micros();
    loadNextFrame(hat_file, hat_leds, HAT_LEN);
    debugMsg("Loaded frame in %d micros", micros() - start);
}

boolean loadNextFrame(File file, CRGB leds[], int len) {
    if (file) {
        if (!readFrame(file, leds, len)) {
            file.seek(0);
            return readFrame(file, leds, len);
        }
        return true;
    }
    return false;
}

boolean readFrame(File file, CRGB leds[], int len) {
    int bytesToRead = len * 3;
    int read = file.read((uint8_t *)leds, bytesToRead);
    debugMsg("Read %d bytes from %s, wanted %d", read, file.name(),
             bytesToRead);
    return read > 0;
}

void startPlayback() {
    debugMsg("Starting playback");

    frameTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(frameTimer, &onFrameTimer, true);
    timerAlarmWrite(frameTimer, 66733, true);  // half of 29.97 fps
    timerAlarmEnable(frameTimer);
    frameTimerEnabled = true;
}

void debugMsg(const char *msg, ...) {
#define DEBUG_BUFFER_LEN 256
    static char debugBuffer[DEBUG_BUFFER_LEN];

    va_list args;
    va_start(args, msg);
    size_t len = vsnprintf(&debugBuffer[0], DEBUG_BUFFER_LEN, msg, args);

#ifdef DEBUG_SERIAL
    Serial.print(millis());
    Serial.print(": ");
    Serial.println(&debugBuffer[0]);
    delay(10);
#endif

    va_end(args);
}
