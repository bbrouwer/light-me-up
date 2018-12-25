#include <FastLED.h>
#include <SPIFFS.h>
#include <stdarg.h>
#include <stdio.h>

// debugging
// #define DEBUG_SERIAL 1

// const char* SYNC[] = {"years1", "lightmeup", "bulb", "arrowup", "heart"}; // 1
// const char* SYNC[] = {"years4", "lightmeup", "bulb", "arrowup", "heart"}; // 2
// const char* SYNC[] = {"years2", "lightmeup", "star", "arrowup", "gift"}; // 3
// const char* SYNC[] = {"years3", "lightmeup", "star", "arrowup", "gift"}; // 4
// const char* SYNC[] = {"star", "star", "star", "star", "star"};
const char* SYNC[] = {"snowman", "lightmeup", "bulb", "arrowup", "heart"};

const int SYNC_LEN = 5;

const char* RAND[] = {"bell", "bulb", "clocktree", "gift", "heart", "lightmeup", "snowflakes", "snowman", "star", "arrowup"};
const int RAND_LEN = 9;

// FastLED settings
const byte MTX_PIN = 13;  // RED
const int MTX_WIDTH = 12;
const int MTX_HEIGHT = 18;
const int MTX_LEN = MTX_WIDTH * MTX_HEIGHT;
CRGB mtx_leds[MTX_LEN];
File mtx_file;

const byte HAT_PIN = 22;  // BLACK
const int HAT_LEN = 32;
CRGB hat_leds[HAT_LEN];
File hat_file;

const byte ARM_PIN = 15;  // GREEN
const int ARM_LEN = 22;
CRGB arm_leds[ARM_LEN];
File arm_file;

const byte LEG_PIN = 32;  // BLUE
const int LEG_LEN = 24;
CRGB leg_leds[LEG_LEN];
File leg_file;

const byte BRIGHTNESS = 255;

// timers
hw_timer_t *frameTimer = NULL;
volatile SemaphoreHandle_t frameSemaphore;
void IRAM_ATTR onFrameTimer() { xSemaphoreGiveFromISR(frameSemaphore, NULL); }

hw_timer_t *startTimer = NULL;
volatile SemaphoreHandle_t startSemaphore;
void IRAM_ATTR onStartTimer() { xSemaphoreGiveFromISR(startSemaphore, NULL); }

void setup() {
    // delay(1000);
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
#endif

    if (!SPIFFS.begin()) {
        debugMsg("Cannot open SPIFFS");
        return;
    }

    FastLED.addLeds<WS2812B, MTX_PIN, GRB>(mtx_leds, MTX_LEN);
    FastLED.addLeds<WS2812B, HAT_PIN, GRB>(hat_leds, HAT_LEN);
    FastLED.addLeds<WS2812B, ARM_PIN, GRB>(arm_leds, ARM_LEN);
    FastLED.addLeds<WS2812B, LEG_PIN, GRB>(leg_leds, LEG_LEN);
    FastLED.setBrightness(BRIGHTNESS);

    startSemaphore = xSemaphoreCreateBinary();
    startTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(startTimer, &onStartTimer, true);
    timerAlarmWrite(startTimer, 8000000, true); // every 8 seconds
    timerAlarmEnable(startTimer);

    frameSemaphore = xSemaphoreCreateBinary();
    frameTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(frameTimer, &onFrameTimer, true);
    timerAlarmWrite(frameTimer, 66733, true);  // half of 29.97 fps
    timerAlarmEnable(frameTimer);

    randomSeed(analogRead(0));

    FastLED.clear();
    arm_leds[0] = CRGB::White;
    FastLED.show();
}

void loop() {
    if (xSemaphoreTake(startSemaphore, 0) == pdTRUE) {
        nextAnimation();
    }
    if (xSemaphoreTake(frameSemaphore, 0) == pdTRUE) {
        FastLED.show();
        loadNextFrame();
    }
}

void nextAnimation() {
    static int syncIndex;

    if (syncIndex < SYNC_LEN) {
        debugMsg("Next animation synced %d %s", syncIndex, SYNC[syncIndex]);
        prepare(SYNC[syncIndex]);
        syncIndex++;
    } else {
        int randIndex = random(RAND_LEN);
        debugMsg("Next animation random %d %s", randIndex, RAND[randIndex]);
        prepare(RAND[randIndex]);
    }
}

void prepare(const char *path) {
    mtx_file = openAnimation(path, "mtx", mtx_file);
    hat_file = openAnimation(path, "hat", hat_file);
    arm_file = openAnimation(path, "arm", arm_file);
    leg_file = openAnimation(path, "leg", leg_file);
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
    if (loadNextFrame(mtx_file, mtx_leds, MTX_LEN)) sortMatrix();
    loadNextFrame(hat_file, hat_leds, HAT_LEN);
    loadNextFrame(arm_file, arm_leds, ARM_LEN);
    loadNextFrame(leg_file, leg_leds, LEG_LEN);
    // debugMsg("Loaded frame in %d micros", micros() - start);
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
    // debugMsg("Read %d bytes from %s, wanted %d", read, file.name(),
    //          bytesToRead);
    return read > 0;
}

void sortMatrix() {
    CRGB tmp[MTX_WIDTH];
    int rowSize = sizeof(tmp);

    // flip upside down
    for (int y = 0; y < (MTX_HEIGHT / 2); y++) {
        memmove(&tmp[0], &mtx_leds[y * MTX_WIDTH], rowSize);
        memmove(&mtx_leds[y * MTX_WIDTH],
                &mtx_leds[(MTX_HEIGHT - y - 1) * MTX_WIDTH], rowSize);
        memmove(&mtx_leds[(MTX_HEIGHT - y - 1) * MTX_WIDTH], &tmp[0], rowSize);
    }

    // change direction of every other row because of serpentine pattern
    for (int y = 1; y < MTX_HEIGHT; y += 2) {
        memmove(&tmp[0], &mtx_leds[y * MTX_WIDTH], rowSize);
        for (int x = 0; x < MTX_WIDTH; x++) {
            mtx_leds[y * MTX_WIDTH + x] = tmp[MTX_WIDTH - x - 1];
        }
    }
}

void debugMsg(const char *msg, ...) {
#define DEBUG_BUFFER_LEN 256
#ifdef DEBUG_SERIAL
    static char debugBuffer[DEBUG_BUFFER_LEN];

    va_list args;
    va_start(args, msg);
    size_t len = vsnprintf(&debugBuffer[0], DEBUG_BUFFER_LEN, msg, args);

    Serial.print(millis());
    Serial.print(": ");
    Serial.println(&debugBuffer[0]);
    delay(10);

    va_end(args);
#endif
}
