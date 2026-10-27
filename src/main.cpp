#include <Arduino.h>
#include <machine/CanningMachine.h>
#include <drivers/universal/Monitor.h>
#include <drivers/universal/WifiManager.h>
#include <drivers/universal/wifi_secrets.h>
#include "drivers/s3/EvenTheRGBLedHasManagers.h"
#include "drivers/universal/SerialManager.h"
#include "network/WebSeverManager.h"

static auto master = SerialManager::reserve();
static std::atomic<bool> updateCycle = {true};
static std::atomic<unsigned long> lastHeartbeat = {0};
static void updateHeartbeat(const char *);
static void updateHeartbeatJson(const JsonDocument &);

void setup() {
    Monitor::useSerialManager(SerialManager::createHWCDC(Serial, 115200, 20));
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
    WifiManager::setHostname("enlatadora-control");
    WifiManager::forceSTA(SECRET_SSID,SECRET_PASSWORD);
    WifiManager::setup();
    if (WifiManager::isConnected()) {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Green(), 1000);
    }
#ifdef ENABLE_USE_ASYNC_WEBSOCKET_SERVER
    WebSeverManager::addHandler(&Monitor::enableAsyncWebSocketHandler());
#endif
    WebSeverManager::enableOTASupport("admin", "admin");
    WebSeverManager::setOnUpdateSuccessful([] {
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
    WebSeverManager::setup();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (updateCycle.load() && millis() - lastHeartbeat < 5000) {
        master->writeLine(CanningMachine::getSerializedState());
    }
}

static void updateHeartbeat(const char *)  {
    lastHeartbeat = millis();
}
static void updateHeartbeatJson(const JsonDocument &doc)  {
    lastHeartbeat = millis();
}