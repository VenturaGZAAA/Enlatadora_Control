#include <Arduino.h>
#include <config/secrets.h>
#include <machine/CanningMachine.h>
#include <Monitor.h>
#include <WifiManager.h>
#include "EvenTheRGBLedHasManagers.h"
#include "SerialManager.h"
#include "WebServerManager.h"
#include "UsbHIDManager.h"

static auto master = SerialManager::reserve();

static std::atomic updateCycle = {true};
static std::atomic<unsigned long> lastHeartbeat = {0};

static void updateHeartbeat(const char *);

static void updateHeartbeatJson(const JsonDocument &);

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

    UsbHIDManager::onDeviceConnected += [](EspUsbHostDeviceInfo) {
        EvenTheRGBLedHasManagers::setColor(RGBColor::blue().withAlpha(0.1), 250);
    };

    UsbHIDManager::onKeyboardEvent += [](const EspUsbHostKeyboardEvent &event) {
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
        const auto modifiers = UsbHIDManager::getModifiers(event.modifiers);
        JsonDocument doc;
        const auto key = "Q" + String(match);
        const bool CTRL = modifiers.indexOf("CTRL") >= 0;
        if (event.pressed && CTRL) {
            doc[key] = true;
        } else {
            doc[key] = false;
        }
        CanningMachine::outputsWriteJsonCallback(doc);
    };

    UsbHIDManager::forceConsumerControlCorrection();
    UsbHIDManager::init();
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