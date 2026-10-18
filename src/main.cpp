#include <Arduino.h>
#include <machine/CanningMachine.h>
#include <drivers/universal/SerialMonitor.h>

#include "drivers/s3/EvenTheRGBLedHasManagers.h"

void setup() {
    SerialMonitor::init();
    EvenTheRGBLedHasManagers::init();

    CanningMachine::setup(true);
    SerialMonitor::registerJsonCallback(CanningMachine::outputsWriteJsonCallback);
    SerialMonitor::registerCallback(CanningMachine::processTextCommands);


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

}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
