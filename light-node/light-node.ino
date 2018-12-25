#include <FastLED.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>
#include <stdio.h>

const byte CLIENT_ID = 1;

// debugging
// #define DEBUG_SERIAL 1

// communication
const char *WIFI_SSID = "WIFI_SSID";
const char *WIFI_PASSWORD = "WIFI_PASSWORD";
boolean wifiConnected = false;
uint32_t lastCommanderMsg = 0;
uint32_t lastFrameRequest = 0;
WiFiUDP udp;
IPAddress commanderIP;

const int BUFFER_LEN = 1024;
uint8_t buffer[BUFFER_LEN];

const byte ID_PROG = 12;
const byte ID_NODE = 24;
const byte ID_CMDR = 25;

const int PORT_CMDR = 1224;
const int PORT_NODE = 1225;

const int MSG_HEADER_SIZE = 3;
const byte MSG_ANNOUNCE = 0;
const byte MSG_PREPARE = 1;
const byte MSG_START = 2;
const byte MSG_STOP = 3;
const byte MSG_DEBUG = 4;

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
boolean frameTimerEnabled = false;
void IRAM_ATTR onFrameTimer() { xSemaphoreGiveFromISR(frameSemaphore, NULL); }

hw_timer_t *startTimer = NULL;
volatile SemaphoreHandle_t startSemaphore;
boolean startTimerEnabled = false;
void IRAM_ATTR onStartTimer() { xSemaphoreGiveFromISR(startSemaphore, NULL); }

void setup() {
    delay(1000);
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
#endif

    if (!SPIFFS.begin()) {
        debugMsg("Cannot open SPIFFS");
        return;
    }

    connectToWiFi();

    FastLED.addLeds<WS2812B, MTX_PIN, GRB>(mtx_leds, MTX_LEN);
    FastLED.addLeds<WS2812B, HAT_PIN, GRB>(hat_leds, HAT_LEN);
    FastLED.addLeds<WS2812B, ARM_PIN, GRB>(arm_leds, ARM_LEN);
    FastLED.addLeds<WS2812B, LEG_PIN, GRB>(leg_leds, LEG_LEN);
    FastLED.setBrightness(BRIGHTNESS);

    frameSemaphore = xSemaphoreCreateBinary();
    startSemaphore = xSemaphoreCreateBinary();

    stop();
}

void loop() {
    if (wifiConnected) {
        int packetSize = udp.parsePacket();
        if (packetSize) {
            lastCommanderMsg = millis();
            int len = udp.read(buffer, BUFFER_LEN);
            if (handleMsg(len)) {
                commanderIP = udp.remoteIP();
            }
        }
        if ((millis() - lastCommanderMsg) > 10000) {
            lastCommanderMsg = millis();
            announcePresence();
        }
    }
    if (xSemaphoreTake(startSemaphore, 0) == pdTRUE) {
        startPlayback();
    }
    if (xSemaphoreTake(frameSemaphore, 0) == pdTRUE) {
        FastLED.show();
        loadNextFrame();
    }
}

boolean handleMsg(int len) {
    if (len >= MSG_HEADER_SIZE && buffer[0] == ID_PROG &&
        buffer[1] == ID_CMDR) {
        lastCommanderMsg = millis();
        switch (buffer[2]) {
            case MSG_PREPARE:
                prepare(len);
                break;
            case MSG_START:
                start(len);
                break;
            case MSG_STOP:
                stop();
                break;
        }
        return true;
    }
    return false;
}

void announcePresence() {
    uint8_t msg[4] = {ID_PROG, ID_NODE, CLIENT_ID, MSG_ANNOUNCE};
    sendToCommander(msg, 4, ~WiFi.subnetMask() | WiFi.gatewayIP());
}

void prepare(int len) {
    char path[60];
    int pathLen = len - MSG_HEADER_SIZE;
    if (pathLen < sizeof(path)) {
        memset(path, 0, sizeof(path));
        memcpy(path, &buffer[MSG_HEADER_SIZE], pathLen);

        mtx_file = openAnimation(path, "mtx", mtx_file);
        hat_file = openAnimation(path, "hat", hat_file);
        arm_file = openAnimation(path, "arm", arm_file);
        leg_file = openAnimation(path, "leg", leg_file);
    }
    loadNextFrame();
    uint8_t msg[4] = {ID_PROG, ID_NODE, CLIENT_ID, MSG_PREPARE};
    sendToCommander(msg, 4);
}

File openAnimation(char path[], const char *suffix, File previousFile) {
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

void start(int len) {
    int delay = 5;
    if (len > MSG_HEADER_SIZE) {
        delay = buffer[MSG_HEADER_SIZE];
    }
    debugMsg("Starting playback in %d seconds", delay);

    startTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(startTimer, &onStartTimer, true);
    timerAlarmWrite(startTimer, delay * 1000000, false);
    timerAlarmEnable(startTimer);
    startTimerEnabled = true;

    uint8_t msg[4] = {ID_PROG, ID_NODE, CLIENT_ID, MSG_START};
    sendToCommander(msg, 4);
    FastLED.show();
}

void startPlayback() {
    debugMsg("Starting playback");
    
    frameTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(frameTimer, &onFrameTimer, true);
    timerAlarmWrite(frameTimer, 66733, true);  // half of 29.97 fps
    timerAlarmEnable(frameTimer);
    frameTimerEnabled = true;
    
    if (startTimerEnabled) timerEnd(startTimer);
    startTimerEnabled = false;
}

void stop() {
    debugMsg("Stopping");

    if (startTimerEnabled) timerEnd(startTimer);
    startTimerEnabled = false;

    if (frameTimerEnabled) timerEnd(frameTimer);
    frameTimerEnabled = false;

    FastLED.clear();
    FastLED.show();

    uint8_t msg[4] = {ID_PROG, ID_NODE, CLIENT_ID, MSG_STOP};
    sendToCommander(msg, 4);
}

void debugMsg(const char *msg, ...) {
#define DEBUG_BUFFER_LEN 256
    static char debugBuffer[DEBUG_BUFFER_LEN] = {ID_PROG, ID_NODE, CLIENT_ID,
                                                 MSG_DEBUG};

    va_list args;
    va_start(args, msg);
    size_t len = vsnprintf(&debugBuffer[4], DEBUG_BUFFER_LEN - 4, msg, args);

#ifdef DEBUG_SERIAL
    Serial.print(millis());
    Serial.print(": ");
    Serial.println(&debugBuffer[4]);
    delay(10);
#endif
#ifdef DEBUG_CMDR
    if (wifiConnected) {
        sendToCommander(debugBuffer, len);
    }
#endif

    va_end(args);
}

void sendToCommander(uint8_t msg[], int len) {
    sendToCommander(msg, len, commanderIP);
}

void sendToCommander(uint8_t msg[], int len, IPAddress ip) {
    if (wifiConnected) {
        udp.beginPacket(ip, PORT_CMDR);
        udp.write(msg, len);
        udp.endPacket();
    }
}

void connectToWiFi() {
    debugMsg("Connecting to WiFi network: %s", WIFI_SSID);
    WiFi.disconnect(true);  // delete old config
    WiFi.onEvent(WiFiEvent);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    debugMsg("Waiting for WIFI connection...");
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case SYSTEM_EVENT_STA_GOT_IP:
            debugMsg("WiFi connected! IP address: %s",
                     WiFi.localIP().toString().c_str());
            wifiConnected = true;
            commanderIP = ~WiFi.subnetMask() | WiFi.gatewayIP();
            udp.begin(PORT_NODE);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            debugMsg("WiFi lost connection");
            wifiConnected = false;
            break;
    }
}
