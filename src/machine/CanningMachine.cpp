//
// Created by urzu-7 on 10/6/26.
//

#include "CanningMachine.h"
#include <ArduinoJson.h>

#include "drivers/universal/Monitor.h"
#include "config/build_flags.h"

#include <utility>

void CanningMachine::setup(const bool autoUpdate) {
    configurePins();
    allOff();
    setUp = true;
    if (autoUpdate) {
        startAutoUpdate();
    }
}

void CanningMachine::startAutoUpdate() {
    if (autoUpdateTaskHandle == nullptr) {
        TaskHandle_t handle = nullptr;
        xTaskCreatePinnedToCore(
            reinterpret_cast<TaskFunction_t>(autoUpdateLoop),
            "CanningMachineAutoUpdate",
            2048,
            nullptr,
            2,
            &handle,
            1
        );
        autoUpdateTaskHandle = handle;
    }
}

void CanningMachine::start() {
    if (!setUp) {
        Monitor::writeLine("Canning machine has not been set up");
        return;
    }

    if (logicLoopHandle == nullptr) {
        stage = Stage::Reposo;
        TaskHandle_t handle = nullptr;
        xTaskCreatePinnedToCore(
            reinterpret_cast<TaskFunction_t>(run),
            "CanningMachine",
            8096,
            nullptr,
            1,
            &handle,
            1
        );
        logicLoopHandle = handle;
    }
}

void CanningMachine::configurePins() {
    for (int i = 0; i < NUM_INPUTS; ++i) {
#ifndef INPUT_LOGIC_TEST
        pinMode(IN_PINS[i], INPUT);
        inputState[i] = (digitalRead(IN_PINS[i]) == HIGH);
#else
        inputState[i] = false;
#endif
    }
#ifdef INPUT_LOGIC_TEST
    inputState[4] = true;
#endif

    for (int i = 0; i < NUM_OUTPUTS; ++i) {
#ifndef DISABLE_REAL_OUTPUT
        pinMode(ACTUATOR_PINS[i], OUTPUT);
#endif
        outputState[i] = false;
    }
}

void CanningMachine::writeOutput(const int index, const bool value) {
    outputState[index] = value;
#ifndef DISABLE_REAL_OUTPUT
    digitalWrite(ACTUATOR_PINS[index], value ? HIGH : LOW);
#endif
}

bool CanningMachine::readInput(const int index, const bool update) {
#ifndef INPUT_LOGIC_TEST
    if (update) {
        inputState[index] = digitalRead(IN_PINS[index]) == HIGH;
    }
#endif
    return inputState[index];
}

[[noreturn]] void CanningMachine::autoUpdateLoop() {
    while (true) {
        for (int i = 0; i < NUM_INPUTS; i++) {
            readInput(i, true);
        }
        updateAuthorization();
        vTaskDelay(pdMS_TO_TICKS(autoUpdatePeriod));
    }
}


const char *CanningMachine::stageName(const Stage s) {
    switch (s) {
        case Stage::Reposo: return "Reposo";
        case Stage::EsperaLata: return "EsperaLata";
        case Stage::TopeActivo: return "TopeActivo";
        case Stage::FrenadoYBajarTope: return "FrenadoYBajarTope";
        case Stage::SubirPlataformaTolva: return "SubirPlataformaTolva";
        case Stage::Llenado: return "Llenado";
        case Stage::BajarPlataformaTolva: return "BajarPlataformaTolva";
        case Stage::TrasladoAPlataformaSalida: return "TrasladoAPlataformaSalida";
        case Stage::MantenerCadena2s: return "MantenerCadena2s";
        case Stage::CadenaSalida10s: return "CadenaSalida10s";
        case Stage::ExtenderPiston: return "ExtenderPiston";
        case Stage::BajarVentosaYTapas: return "BajarVentosaYTapas";
        case Stage::RetraerPiston: return "RetraerPiston";
        case Stage::SoltarTapa: return "SoltarTapa";
        case Stage::RetraerVentosaYBajarPlataforma: return "RetraerVentosaYBajarPlataforma";
        case Stage::BandaTransportadora3: return "BandaTransportadora3";
        case Stage::DesactivarBanda3: return "DesactivarBanda3";
        case Stage::AsegurarPaletYSoltarTope: return "AsegurarPaletYSoltarTope";
        case Stage::LevantarLata: return "LevantarLata";
        case Stage::VerificacionEstadoLata: return "VerificacionEstadoLata";
        case Stage::Sellar: return "Sellar";
        case Stage::ApagarMotorSellado: return "ApagarMotorSellado";
        case Stage::BajarLata: return "BajarLata";
        case Stage::DesasegurarPalet: return "DesasegurarPalet";
        case Stage::ActivarBanda3: return "ActivarBanda3";
        case Stage::Apagar: return "Apagar";
    }
    return "Desconocido";
}


String CanningMachine::getSerializedState() {
    JsonDocument jsonDoc;
    jsonDoc["running"] = machineRunning.load();
    jsonDoc["emergencyStop"] = hardwarePause.load();
    jsonDoc["paused"] = softwarePause.load();
    jsonDoc["authorizedSystem"] = authorizedSystem.load();
    jsonDoc["forcedAuthorization"] = forcedAuthorization.load();
    jsonDoc["stage_name"] = stageName(stage.load());
    jsonDoc["stage_num"] = static_cast<int>(stage.load());
    const auto inputs = jsonDoc["inputs"].to<JsonObject>();
    for (int i = 0; i < NUM_INPUTS; ++i) {
        inputs["I" + String(i)] = inputState[i].load();
    }
    const auto outputs = jsonDoc["outputs"].to<JsonObject>();
    for (int i = 0; i < NUM_OUTPUTS; ++i) {
        outputs["Q" + String(i)] = outputState[i].load();
    }
    String jsonOutput;
    serializeJson(jsonDoc, jsonOutput);
    return jsonOutput;
}


void CanningMachine::outputsWriteJsonCallback(const JsonDocument &doc) {
    bool authorized = false;
    for (int i = 0; i < NUM_OUTPUTS; ++i) {
        if (doc["Q" + String(i)].is<bool>()) {
            if (!authorized) {
                if (hardwarePause) {
                    Monitor::writeLine("Output override while the machine is paused is not allowed");
                    return;
                }
                if ((machineRunning && !softwarePause)) {
                    Monitor::writeLine(
                        "Output override while the machine is running and not paused is not allowed");
                    return;
                }
                authorized = true;
            }
            writeOutput(i, doc["Q" + String(i)]);
        }
    }
}

void CanningMachine::inputsWriteJsonCallback(const JsonDocument &doc) {
#ifdef INPUT_LOGIC_TEST
    for (int i = 0; i < NUM_INPUTS; ++i) {
        if (doc["I" + String(i)].is<bool>()) {
            inputState[i] = doc["I" + String(i)];
        }
    }
#else
    Monitor::writeLine("Input override not authorized");
#endif
}

void CanningMachine::allOff() {
    for (int i = 0; i < NUM_OUTPUTS; ++i) {
        writeOutput(i, false);
    }
}

CanningMachine::Commands CanningMachine::getCommandByName(const String &name) {
    if (name == "Start") {
        return Commands::Start;
    }
    if (name == "Stop") {
        return Commands::Stop;
    }
    if (name == "Pause") {
        return Commands::Pause;
    }
    if (name == "Resume") {
        return Commands::Resume;
    }

    return Commands::Unknown;
}

void CanningMachine::processTextCommands(const char *command) {
    const auto cmd = String(command);

    if (cmd == "ForceAuthorization") {
        forcedAuthorization = true;
        return;
    }

    processCommand(getCommandByName(cmd));
}


void CanningMachine::processCommand(const Commands command) {
    switch (command) {
        case Commands::Start:
            start();
            if (onStartCommand)
                onStartCommand();
            break;
        case Commands::Stop:
            stop();
            softwarePause = false;
            forcedAuthorization = false;
            if (onStopCommand)
                onStopCommand();
            break;
        case Commands::Pause:
            if (machineRunning) {
                softwarePause = true;
                if (onPauseCommand)
                    onPauseCommand();
            }
            break;

        case Commands::Resume:
            softwarePause = false;
            // if (onResumeCommand)
            //     onResumeCommand();
            break;

        case Commands::Unknown:
            Monitor::writeLine("Unknown command");
            break;
    }
}

void CanningMachine::setOnStopCallback(std::function<void()> callback) {
    onStopCommand = std::move(callback);
}

void CanningMachine::setOnStartCallback(std::function<void()> callback) {
    onStartCommand = std::move(callback);
}

void CanningMachine::setOnPauseCallback(std::function<void()> callback) {
    onPauseCommand = std::move(callback);
}

void CanningMachine::updateAuthorization() {
    if (!readInput(4, true) || forcedAuthorization) authorizedSystem = true;
    if (readInput(5, true)) {
        authorizedSystem = false;
        forcedAuthorization = false;
    }
}

void CanningMachine::pausableDelay(const uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (isPaused()) {
            bool snapshot[NUM_OUTPUTS];
            for (int o = 0; o < NUM_OUTPUTS; ++o) {
                snapshot[o] = outputState[o].load();
            }
            allOff();
            while (isPaused()) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            updateAuthorization();

            for (int o = 0; o < NUM_OUTPUTS; o++) {
                writeOutput(o, snapshot[o]);
            }
        }
    }
}

void CanningMachine::run() {
    machineRunning = true;
    bool runLoop = true;
    allOff();
    while (runLoop) {
        updateAuthorization();

        switch (stage.load()) {
            case Stage::Reposo:
                allOff();
                if (authorizedSystem) {
                    stage = Stage::EsperaLata;
                } else {
                    pausableDelay(1);
                }
                break;

            case Stage::EsperaLata:
                writeOutput(16, true);

                while (readInput(3) && authorizedSystem) {
                    pausableDelay(1);
                    updateAuthorization();
                }

                // Original priority: sensor first, then authorization.
                if (!readInput(3)) {
                    stage = Stage::TopeActivo;
                } else if (!authorizedSystem) {
                    stage = Stage::Reposo;
                }
                break;

            case Stage::TopeActivo:
                writeOutput(18, true);
                pausableDelay(2000);
                stage = Stage::FrenadoYBajarTope;
                break;

            case Stage::FrenadoYBajarTope:
                writeOutput(16, false);
                writeOutput(18, false);
                stage = Stage::SubirPlataformaTolva;
                break;

            case Stage::SubirPlataformaTolva:
                writeOutput(17, true);
                pausableDelay(1000);
                stage = Stage::Llenado;
                break;

            case Stage::Llenado:
                writeOutput(1, true);
                pausableDelay(1000);
                writeOutput(1, false);
                stage = Stage::BajarPlataformaTolva;
                break;

            case Stage::BajarPlataformaTolva:
                writeOutput(17, false);
                stage = Stage::TrasladoAPlataformaSalida;
                break;

            case Stage::TrasladoAPlataformaSalida:
                writeOutput(16, true);

                while (readInput(2)) {
                    pausableDelay(1);
                }

                stage = Stage::MantenerCadena2s;
                break;

            case Stage::MantenerCadena2s:
                pausableDelay(2000);
                writeOutput(16, false);
                writeOutput(0, true);
                stage = Stage::CadenaSalida10s;
                break;

            case Stage::CadenaSalida10s:
                writeOutput(11, true);
                writeOutput(15, true);
                writeOutput(12, true);
                writeOutput(2, true);

                pausableDelay(10000);

                writeOutput(11, false);
                writeOutput(15, false);
                writeOutput(12, false);
                writeOutput(0, false);

                stage = Stage::ExtenderPiston;
                break;

            case Stage::ExtenderPiston:
                writeOutput(4, true);
                pausableDelay(2000);
                stage = Stage::BajarVentosaYTapas;
                break;

            case Stage::BajarVentosaYTapas:
                writeOutput(5, true);
                writeOutput(6, true);
                writeOutput(3, true);

                // Original: waits 1000 ms OR until IN_PINS[7] goes LOW.
                // Poll at 1 ms to preserve the sensor override.
                for (int i = 0; i < 1000; ++i) {
                    if (!readInput(7)) {
                        writeOutput(4, false);
                        writeOutput(3, false);
                        writeOutput(6, false);
                        writeOutput(5, false);
                        writeOutput(2, false);

                        stage = Stage::ActivarBanda3;
                        break;
                    }
                    pausableDelay(1);
                }

                if (stage == Stage::BajarVentosaYTapas) {
                    stage = Stage::RetraerPiston;
                }
                break;

            case Stage::RetraerPiston:
                writeOutput(4, false);
                writeOutput(3, false);
                pausableDelay(2000);
                stage = Stage::SoltarTapa;
                break;

            case Stage::SoltarTapa:
                writeOutput(6, false);
                pausableDelay(500);
                stage = Stage::RetraerVentosaYBajarPlataforma;
                break;

            case Stage::RetraerVentosaYBajarPlataforma:
                writeOutput(5, false);
                writeOutput(2, false);
                pausableDelay(500);
                stage = Stage::BandaTransportadora3;
                break;

            case Stage::BandaTransportadora3:
                writeOutput(14, true);
                writeOutput(7, true);
                pausableDelay(5000);
                stage = Stage::DesactivarBanda3;
                break;

            case Stage::DesactivarBanda3:
                writeOutput(14, false);
                pausableDelay(1000);
                stage = Stage::AsegurarPaletYSoltarTope;
                break;

            case Stage::AsegurarPaletYSoltarTope:
                writeOutput(8, true);
                writeOutput(7, false);
                pausableDelay(1000);
                stage = Stage::LevantarLata;
                break;

            case Stage::LevantarLata:
                writeOutput(9, true);
                pausableDelay(500);
                stage = Stage::VerificacionEstadoLata;
                break;

            case Stage::VerificacionEstadoLata:
                if (readInput(0)) {
                    stage = Stage::BajarLata;
                } else {
                    stage = Stage::Sellar;
                }
                break;

            case Stage::Sellar:
                writeOutput(13, true);
                pausableDelay(1000);
                stage = Stage::ApagarMotorSellado;
                break;

            case Stage::ApagarMotorSellado:
                writeOutput(13, false);
                pausableDelay(500);
                stage = Stage::BajarLata;
                break;

            case Stage::BajarLata:
                writeOutput(9, false);
                pausableDelay(500);
                stage = Stage::DesasegurarPalet;
                break;

            case Stage::DesasegurarPalet:
                writeOutput(8, false);
                pausableDelay(500);
                stage = Stage::ActivarBanda3;
                break;

            case Stage::ActivarBanda3:
                writeOutput(14, true);

                while (readInput(1)) {
                    pausableDelay(1);
                }

                stage = Stage::Apagar;
                break;

            case Stage::Apagar:
                writeOutput(14, false);

                runLoop = false;
                break;
        }
    }

    allOff();
    machineRunning = false;
    forcedAuthorization = false;
    stage = Stage::Reposo;
    logicLoopHandle = nullptr;
    vTaskDelete(nullptr);
}

void CanningMachine::stop() {
    if (TaskHandle_t handle = logicLoopHandle.exchange(nullptr); handle != nullptr) {
        Monitor::writeLine("Stopping canning machine");
        vTaskDelete(handle);
        allOff();
        machineRunning = false;
        forcedAuthorization = false;
        stage = Stage::Reposo;
    }
    // stopAutoUpdate();
}

void CanningMachine::stopAutoUpdate() {
    if (TaskHandle_t handle = autoUpdateTaskHandle.exchange(nullptr); handle != nullptr) {
        vTaskDelete(handle);
    }
}
