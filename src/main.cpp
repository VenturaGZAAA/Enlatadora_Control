#include <Arduino.h>
#include <config/secrets.h>
#include <machine/CanningMachine.h>
#include <Monitor.h>
#include <WifiManager.h>
#include "EvenTheRGBLedHasManagers.h"
#include "SerialManager.h"
#include "WebServerManager.h"
#include "EspUsbHost.h"


static auto master = SerialManager::reserve();
static EspUsbHost usb;

static std::atomic updateCycle = {true};
static std::atomic<unsigned long> lastHeartbeat = {0};

static void updateHeartbeat(const char *);

static void updateHeartbeatJson(const JsonDocument &);

static bool usbStarted;

static String getModifiers(const uint8_t modifiers) {
    String msg = "";
    static const char *modifierNames[] = {
        "LCTRL", "LSHIFT", "LALT", "LGUI",
        "RCTRL", "RSHIFT", "RALT", "RGUI"
    };

    if (modifiers == 0) {
        msg = "none";
        return msg;
    }

    bool first = true;
    for (int i = 0; i < 8; i++) {
        if (modifiers & (1 << i)) {
            if (!first) {
                msg += "+";
            }
            msg += modifierNames[i];
            first = false;
        }
    }
    return msg;
}

[[noreturn]] static void handleUSB();

void setup() {
    // Monitor::useSerialManager(SerialManager::createHWCDC(Serial, 115200, 20));
    Monitor::init();

    EvenTheRGBLedHasManagers::init();

    master = SerialManager::create(Serial1, 44, 43, 115200, 20);

    CanningMachine::setup(true);


    CanningMachine::setOnStartCallback([] {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Green(), 1000);
    });

    CanningMachine::setOnStopCallback([] {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Red(), 1000);
    });

    CanningMachine::setOnPauseCallback([] {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Magenta(), 1000);
    });

    EvenTheRGBLedHasManagers::setColorGenerator([] {
        if (CanningMachine::isRunning()) {
            if (CanningMachine::isPaused()) {
                return RGBColor::Yellow().withAlpha(0.5);
            }
            return RGBColor::white().withAlpha(0.5);
        }
        return RGBColor();
    });

    Monitor::registerJsonCallback(CanningMachine::outputsWriteJsonCallback);
    Monitor::registerCallback(CanningMachine::processTextCommands);
    master->registerJsonCallback(CanningMachine::outputsWriteJsonCallback);
    master->registerCallback(CanningMachine::processTextCommands);
    master->registerJsonCallback(updateHeartbeatJson);
    master->registerCallback(updateHeartbeat);

    master->init();


    usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES);
    usb.setHubTrackingEnabled(false);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &) {
        EvenTheRGBLedHasManagers::setColor(RGBColor::blue().withAlpha(0.1), 250);
    });

    usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
        constexpr const char *QWERTY_CHARS = "qwertyuiopasdfghjklzxcvbnm";
        int match = -1;
        for (uint8_t i = 0; i < 18; i++) {
            if (QWERTY_CHARS[i] == event.ascii) {
                match = i;
                break;
            }
        }
        if (match < 0 || match >= 18) {
            return;
        }
        const auto modifiers = getModifiers(event.modifiers);
        JsonDocument doc;
        const auto key = "Q" + String(match);
        const bool CTRL = modifiers.indexOf("CTRL") >= 0;
        if (event.pressed && CTRL) {
            doc[key] = true;
        } else {
            doc[key] = false;
        }
        CanningMachine::outputsWriteJsonCallback(doc);
    });

    xTaskCreatePinnedToCore(reinterpret_cast<TaskFunction_t>(handleUSB), "usbTask", 4096, nullptr, 2, nullptr, 1);
    WifiManager::setHostname("enlatadora-control");
    WifiManager::forceSTA(STA_SSID,STA_PASS);
    WifiManager::setup("Enlatadora-control", "12345678");
    if (WifiManager::isConnected()) {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Green(), 1000);
    }
#ifdef ENABLE_USE_ASYNC_WEBSOCKET_SERVER
    WebServerManager::addHandler(&Monitor::enableAsyncWebSocketHandler());
#endif
    WebServerManager::enableOTASupport("admin", "admin");
    WebServerManager::setOnUpdateSuccessful([] {
        updateCycle = false;
        CanningMachine::processCommand(CanningMachine::Commands::Stop);
        CanningMachine::allOff();
        master->writeLine(CanningMachine::getSerializedState());
        vTaskDelay(pdMS_TO_TICKS(50));
        master->writeLine("UPDATE");
        vTaskDelay(pdMS_TO_TICKS(100));
        master->stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
    });
    WebServerManager::setup();
    lastHeartbeat = millis();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (updateCycle.load() && millis() - lastHeartbeat.load() < 5000) {
        master->writeLine(CanningMachine::getSerializedState());
    }
}

static void updateHeartbeat(const char *) {
    lastHeartbeat = millis();
}

static void updateHeartbeatJson(const JsonDocument &) {
    lastHeartbeat = millis();
}

[[noreturn]] static void handleUSB() {
    while (true) {
        if (!usbStarted) {
            usbStarted = usb.begin();
            if (!usbStarted) {
                JsonDocument doc;
                doc["usbFailure"] = usb.lastErrorName();
                String output;
                serializeJson(doc, output);
                Monitor::writeLine(output);
            }
        }
        vTaskDelay(2500);
    }
}
