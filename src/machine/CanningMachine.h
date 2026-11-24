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
#include <atomic>


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
        Apagar
    };

public:
    enum class Inputs : uint8_t {
        VerificarEstadoLata = 40,
        SensorFinal = 39,
        SensorEsquina1 = 38,
        SensorEntrada = 37,
        BotonAutorizacion = 36,
        BotonDesautorizacion = 35,
        NoSeUsa = 0,
        EtapaPonerTapaPiston = 45
    };

    enum class Outputs : uint8_t {
        PlataformaElevador1 = 18,
        TapaTanque = 17,
        PlataformaElevador2 = 16,
        ElevadorTapas = 15,
        ExtenderPiston = 7,
        BajarVentosaPiston = 6,
        VentosaPiston = 5,
        MiniTopeFinal = 4,
        AsegurarPallet = 13,
        ElevadorLata = 12,
        DisconectedOrUnknown = 11,
        BandaElevador1 = 10,
        BandaElevador2 = 9,
        MotorSellador = 46,
        BandaGrande3 = 3,
        BandaGrande2 = 8,
        BandaGrande1 = 41,
        Plataforma1 = 42,
        Tope1 = 2
    };

    enum class Commands : uint8_t {
        Stop = 0,
        Start,
        Pause,
        Resume,
        Unknown
    };

    static constexpr int getInputIndex(Inputs in) {
        switch (in) {
            case Inputs::VerificarEstadoLata: return 0;
            case Inputs::SensorFinal: return 1;
            case Inputs::SensorEsquina1: return 2;
            case Inputs::SensorEntrada: return 3;
            case Inputs::BotonAutorizacion: return 4;
            case Inputs::BotonDesautorizacion: return 5;
            case Inputs::NoSeUsa: return 6;
            case Inputs::EtapaPonerTapaPiston: return 7;
        }
        return -1;
    }

    static constexpr int getOutputIndex(Outputs out) {
        switch (out) {
            case Outputs::PlataformaElevador1: return 0;
            case Outputs::TapaTanque: return 1;
            case Outputs::PlataformaElevador2: return 2;
            case Outputs::ElevadorTapas: return 3;
            case Outputs::ExtenderPiston: return 4;
            case Outputs::BajarVentosaPiston: return 5;
            case Outputs::VentosaPiston: return 6;
            case Outputs::MiniTopeFinal: return 7;
            case Outputs::AsegurarPallet: return 8;
            case Outputs::ElevadorLata: return 9;
            case Outputs::DisconectedOrUnknown: return 10;
            case Outputs::BandaElevador1: return 11;
            case Outputs::BandaElevador2: return 12;
            case Outputs::MotorSellador: return 13;
            case Outputs::BandaGrande3: return 14;
            case Outputs::BandaGrande2: return 15;
            case Outputs::BandaGrande1: return 16;
            case Outputs::Plataforma1: return 17;
            case Outputs::Tope1: return 18;
        }
        return -1;
    }

private:
    static const char *stageName(Stage s);

    static constexpr int NUM_INPUTS = 8;
    static constexpr int NUM_OUTPUTS = 19;

    static constexpr uint8_t IN_PINS[NUM_INPUTS] = {
        static_cast<uint8_t>(Inputs::VerificarEstadoLata),
        static_cast<uint8_t>(Inputs::SensorFinal),
        static_cast<uint8_t>(Inputs::SensorEsquina1),
        static_cast<uint8_t>(Inputs::SensorEntrada),
        static_cast<uint8_t>(Inputs::BotonAutorizacion),
        static_cast<uint8_t>(Inputs::BotonDesautorizacion),
        static_cast<uint8_t>(Inputs::NoSeUsa),
        static_cast<uint8_t>(Inputs::EtapaPonerTapaPiston)
    };

    static constexpr uint8_t ACTUATOR_PINS[NUM_OUTPUTS] = {
        static_cast<uint8_t>(Outputs::PlataformaElevador1),
        static_cast<uint8_t>(Outputs::TapaTanque),
        static_cast<uint8_t>(Outputs::PlataformaElevador2),
        static_cast<uint8_t>(Outputs::ElevadorTapas),
        static_cast<uint8_t>(Outputs::ExtenderPiston),
        static_cast<uint8_t>(Outputs::BajarVentosaPiston),
        static_cast<uint8_t>(Outputs::VentosaPiston),
        static_cast<uint8_t>(Outputs::MiniTopeFinal),
        static_cast<uint8_t>(Outputs::AsegurarPallet),
        static_cast<uint8_t>(Outputs::ElevadorLata),
        static_cast<uint8_t>(Outputs::DisconectedOrUnknown),
        static_cast<uint8_t>(Outputs::BandaElevador1),
        static_cast<uint8_t>(Outputs::BandaElevador2),
        static_cast<uint8_t>(Outputs::MotorSellador),
        static_cast<uint8_t>(Outputs::BandaGrande3),
        static_cast<uint8_t>(Outputs::BandaGrande2),
        static_cast<uint8_t>(Outputs::BandaGrande1),
        static_cast<uint8_t>(Outputs::Plataforma1),
        static_cast<uint8_t>(Outputs::Tope1)
    };


    static inline std::atomic<bool> inputState[NUM_INPUTS] = {};
    static inline std::atomic<bool> outputState[NUM_OUTPUTS] = {};
    static inline std::atomic<bool> setUp{false};

    static inline std::atomic<Stage> stage{Stage::Reposo};
    static inline std::atomic<bool> authorizedSystem{false};
    static inline std::atomic<bool> forcedAuthorization{false};

    static inline std::atomic<bool> machineRunning{false};
    static inline std::atomic<bool> hardwarePause{false};
    static inline std::atomic<bool> softwarePause{false};

    static inline std::atomic<TaskHandle_t> logicLoopHandle{nullptr};

    static void run();

    static void configurePins();

    static void updateAuthorization();


    static void writeOutput(Outputs output, bool value);
    static void writeOutput(int index, bool value);

    static bool readInput(Inputs input, bool update = false);
    static bool readInput(int index, bool update = false);

    static inline uint8_t autoUpdatePeriod = 10;
    static inline std::atomic<TaskHandle_t> autoUpdateTaskHandle{nullptr};

    [[noreturn]] static void autoUpdateLoop();

    static Commands getCommandByName(const String &name);

    static inline std::function<void()> onStopCommand = nullptr;
    static inline std::function<void()> onStartCommand = nullptr;
    static inline std::function<void()> onPauseCommand = nullptr;

    static void start();

    static void pausableDelay(uint32_t ms);

    static void stop();

public:
    CanningMachine() = delete;

    static void setup(bool autoUpdate = true);

    static void startAutoUpdate();

    static void stopAutoUpdate();

    static bool isRunning() { return machineRunning; }
    static bool isPaused() { return hardwarePause || softwarePause; }

    static String getSerializedState();

    static void inputsWriteJsonCallback(const JsonDocument &doc);

    static void outputsWriteJsonCallback(const JsonDocument &doc);

    static void processTextCommands(const char *command);
    static void processCommand(Commands command);

    static void setOnStopCallback(std::function<void()> callback);

    static void setOnStartCallback(std::function<void()> callback);

    static void setOnPauseCallback(std::function<void()> callback);

    static void allOff();
};
#endif //ENLATADORA_IOT_CANNINGMACHINE_H
