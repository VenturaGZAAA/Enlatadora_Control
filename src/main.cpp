#include <Arduino.h>
#include <machine/CanningMachine.h>
#include <drivers/universal/Monitor.h>
#include <drivers/universal/WifiManager.h>
#include <drivers/universal/wifi_secrets.h>
#include "drivers/s3/EvenTheRGBLedHasManagers.h"
#include "drivers/universal/UARTManager.h"
#include "network/WebSeverManager.h"

static auto master = UARTManager::reserve();

void setup() {
    // Monitor::init();
    EvenTheRGBLedHasManagers::init();

    master = UARTManager::create(Serial1, 44, 43);


    CanningMachine::setup(true);
    Monitor::registerJsonCallback(CanningMachine::outputsWriteJsonCallback);
    Monitor::registerCallback(CanningMachine::processTextCommands);


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

    master->registerJsonCallback(CanningMachine::outputsWriteJsonCallback);
    master->registerCallback(CanningMachine::processTextCommands);
    master->registerCallback([](const char *S) {
        Monitor::writeLine("Command received: " + String(S));
    });
    master->init();
    WifiManager::setHostname("enlatadora-control");
    WifiManager::forceSTA(SECRET_SSID,SECRET_PASSWORD);
    WifiManager::setup();
    if (WifiManager::isConnected()) {
        EvenTheRGBLedHasManagers::setColor(RGBColor::Green(), 1000);
    }
    WebSeverManager::enableOTASupport("admin", "admin");
    WebSeverManager::setOnUpdateSuccessful([] {
        CanningMachine::processTextCommands("Stop");
        CanningMachine::allOff();
        Monitor::init();
        vTaskDelay(pdMS_TO_TICKS(1000));
    });
    WebSeverManager::setup();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(50));
    master->writeLine(CanningMachine::getSerializedState());
}
