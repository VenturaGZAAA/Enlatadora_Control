//
// Created by urzu-7 on 10/6/26.
//

#ifndef ENLATADORA_IOT_CANNINGMACHINE_H
#define ENLATADORA_IOT_CANNINGMACHINE_H

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ArduinoJson.h>


class CanningMachine {
    enum class Stage : uint8_t {
        Reposo = 0,
        EsperaLata,
        TopeActivo,
        FrenadoYBajarTope,
        SubirPlataformaTolva,
        Llenado,
        BajarPlataformaTolva,
        TrasladoAPlataformaSalida,
        MantenerCadena2s,
        CadenaSalida10s,
        ExtenderPiston,
        BajarVentosaYTapas,
        RetraerPiston,
        SoltarTapa,
        RetraerVentosaYBajarPlataforma,
        BandaTransportadora3,
        DesactivarBanda3,
        AsegurarPaletYSoltarTope,
        LevantarLata,
        VerificacionEstadoLata,
        Sellar,
        ApagarMotorSellado,
        BajarLata,
        DesasegurarPalet,
        ActivarBanda3,
        ApagarBanda3YRepetir
    };

    enum class Commands : uint8_t {
        Stop = 0,
        Start,
        Pause,
        Resume,
        Unknown
    };

    static const char *stageName(Stage s);

    static constexpr int NUM_INPUTS = 8;
    static constexpr int NUM_OUTPUTS = 19;

    static constexpr int IN_PINS[NUM_INPUTS] = {
        40, 39, 38, 37, 36, 35, 0, 45
    };

    static constexpr int ACTUATOR_PINS[NUM_OUTPUTS] = {
        18, 17, 16, 15, 7, 6, 5, 4, 13, 12,
        11, 10, 9, 46, 3, 8, 41, 42, 2
    };


    static inline bool inputState[NUM_INPUTS] = {};
    static inline bool outputState[NUM_OUTPUTS] = {};
    static inline bool setUp = false;

    static inline auto stage = Stage::Reposo;
    static inline bool authorizedSystem = true;

    static inline bool machineRunning = false;
    static inline bool hardwarePause = false;
    static inline bool softwarePause = false;

    static inline TaskHandle_t logicLoopHandle = nullptr;

    [[noreturn]] static void run();

    static void configurePins();

    static void updateAuthorization();


    static void writeOutput(int index, bool value);


    static bool readInput(int index, bool update = false);
    static inline uint8_t autoUpdatePeriod = 10;
    static inline TaskHandle_t autoUpdateTaskHandle = nullptr;
    [[noreturn]] static void autoUpdateLoop();

    static Commands getCommandByName(const String& name);
    static void processCommand(Commands command);
    
    static inline std::function<void()> onStopCommand = nullptr;
    static inline std::function<void()> onStartCommand = nullptr;
    static inline std::function<void()> onPauseCommand = nullptr;

    static void start();
    static void pausableDelay(uint32_t ms);
    static void stop();
public:
    CanningMachine() = delete;

    static void setup(bool autoUpdate = false);

    static void startAutoUpdate();
    static void stopAutoUpdate();

    static bool isRunning() {return machineRunning;}
    static bool isPaused() {return hardwarePause || softwarePause;}

    static String getSerializedState();

    static void inputsWriteJsonCallback(const JsonDocument &doc);

    static void outputsWriteJsonCallback(const JsonDocument &doc);

    static void processTextCommands(const char *command);

    static void setOnStopCallback(std::function<void()> callback);
    static void setOnStartCallback(std::function<void()> callback);
    static void setOnPauseCallback(std::function<void()> callback);

    static void allOff();

};
#endif //ENLATADORA_IOT_CANNINGMACHINE_H
