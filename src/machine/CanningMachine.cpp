//
// Created by urzu-7 on 10/6/26.
//

#include "CanningMachine.h"
#include <ArduinoJson.h>

#include "Monitor.h"
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
    static_assert(sizeof(IN_PINS) / sizeof(IN_PINS[0]) == 8, "NUM_INPUTS must be 8");
    static_assert(sizeof(ACTUATOR_PINS) / sizeof(ACTUATOR_PINS[0]) == 19, "NUM_OUTPUTS must be 19");

    static_assert(IN_PINS[0] == 40 && IN_PINS[1] == 39 &&
                  IN_PINS[2] == 38 && IN_PINS[3] == 37 &&
                  IN_PINS[4] == 36 && IN_PINS[5] == 35 &&
                  IN_PINS[6] == 0 && IN_PINS[7] == 45,
                  "Input pin assignments mismatch");

    static_assert(ACTUATOR_PINS[0] == 18 && ACTUATOR_PINS[1] == 17 &&
                  ACTUATOR_PINS[2] == 16 && ACTUATOR_PINS[3] == 15 &&
                  ACTUATOR_PINS[4] == 7 && ACTUATOR_PINS[5] == 6 &&
                  ACTUATOR_PINS[6] == 5 && ACTUATOR_PINS[7] == 4 &&
                  ACTUATOR_PINS[8] == 13 && ACTUATOR_PINS[9] == 12 &&
                  ACTUATOR_PINS[10] == 11 && ACTUATOR_PINS[11] == 10 &&
                  ACTUATOR_PINS[12] == 9 && ACTUATOR_PINS[13] == 46 &&
                  ACTUATOR_PINS[14] == 3 && ACTUATOR_PINS[15] == 8 &&
                  ACTUATOR_PINS[16] == 41 && ACTUATOR_PINS[17] == 42 &&
                  ACTUATOR_PINS[18] == 2,
                  "Actuator pin assignments mismatch");

    static_assert(getInputIndex(Inputs::VerificarEstadoLata) == 0 &&
                  getInputIndex(Inputs::SensorFinal) == 1 &&
                  getInputIndex(Inputs::SensorEsquina1) == 2 &&
                  getInputIndex(Inputs::SensorEntrada) == 3 &&
                  getInputIndex(Inputs::BotonAutorizacion) == 4 &&
                  getInputIndex(Inputs::BotonDesautorizacion) == 5 &&
                  getInputIndex(Inputs::NoSeUsa) == 6 &&
                  getInputIndex(Inputs::EtapaPonerTapaPiston) == 7,
                  "Input index mapping mismatch");

    static_assert(getOutputIndex(Outputs::PlataformaElevador1) == 0 &&
                  getOutputIndex(Outputs::TapaTanque) == 1 &&
                  getOutputIndex(Outputs::PlataformaElevador2) == 2 &&
                  getOutputIndex(Outputs::ElevadorTapas) == 3 &&
                  getOutputIndex(Outputs::ExtenderPiston) == 4 &&
                  getOutputIndex(Outputs::BajarVentosaPiston) == 5 &&
                  getOutputIndex(Outputs::VentosaPiston) == 6 &&
                  getOutputIndex(Outputs::MiniTopeFinal) == 7 &&
                  getOutputIndex(Outputs::AsegurarPallet) == 8 &&
                  getOutputIndex(Outputs::ElevadorLata) == 9 &&
                  getOutputIndex(Outputs::DisconectedOrUnknown) == 10 &&
                  getOutputIndex(Outputs::BandaElevador1) == 11 &&
                  getOutputIndex(Outputs::BandaElevador2) == 12 &&
                  getOutputIndex(Outputs::MotorSellador) == 13 &&
                  getOutputIndex(Outputs::BandaGrande3) == 14 &&
                  getOutputIndex(Outputs::BandaGrande2) == 15 &&
                  getOutputIndex(Outputs::BandaGrande1) == 16 &&
                  getOutputIndex(Outputs::Plataforma1) == 17 &&
                  getOutputIndex(Outputs::Tope1) == 18,
                  "Output index mapping mismatch");

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

void CanningMachine::writeOutput(const Outputs output, const bool value) {
    const int index = getOutputIndex(output);
    if (index >= 0) {
        writeOutput(index, value);
    }
}

void CanningMachine::writeOutput(const int index, const bool value) {
    if (index < 0 || index >= NUM_OUTPUTS) return;
    outputState[index] = value;
#ifndef DISABLE_REAL_OUTPUT
    digitalWrite(ACTUATOR_PINS[index], value ? HIGH : LOW);
#endif
}

bool CanningMachine::readInput(const Inputs input, const bool update) {
    const int index = getInputIndex(input);
    if (index >= 0) {
        return readInput(index, update);
    }
    return false;
}

bool CanningMachine::readInput(const int index, const bool update) {
    if (index < 0 || index >= NUM_INPUTS) return false;
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

    if (cmd == "HEARTBEAT") {
        return;
    }
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
            if (forcedAuthorization) {
                authorizedSystem = false;
            }
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
    if (!readInput(Inputs::BotonAutorizacion, true) || forcedAuthorization) authorizedSystem = true;
    if (readInput(Inputs::BotonDesautorizacion, true)) {
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
                writeOutput(Outputs::BandaGrande1, true);

                while (readInput(Inputs::SensorEntrada) && authorizedSystem) {
                    pausableDelay(1);
                    updateAuthorization();
                }

                // Original priority: sensor first, then authorization.
                if (!readInput(Inputs::SensorEntrada)) {
                    stage = Stage::TopeActivo;
                } else if (!authorizedSystem) {
                    stage = Stage::Reposo;
                }
                break;

            case Stage::TopeActivo:
                writeOutput(Outputs::Tope1, true);
                pausableDelay(2000);
                stage = Stage::FrenadoYBajarTope;
                break;

            case Stage::FrenadoYBajarTope:
                writeOutput(Outputs::BandaGrande1, false);
                writeOutput(Outputs::Tope1, false);
                stage = Stage::SubirPlataformaTolva;
                break;

            case Stage::SubirPlataformaTolva:
                writeOutput(Outputs::Plataforma1, true);
                pausableDelay(1000);
                stage = Stage::Llenado;
                break;

            case Stage::Llenado:
                writeOutput(Outputs::TapaTanque, true);
                pausableDelay(1000);
                writeOutput(Outputs::TapaTanque, false);
                stage = Stage::BajarPlataformaTolva;
                break;

            case Stage::BajarPlataformaTolva:
                writeOutput(Outputs::Plataforma1, false);
                stage = Stage::TrasladoAPlataformaSalida;
                break;

            case Stage::TrasladoAPlataformaSalida:
                writeOutput(Outputs::BandaGrande1, true);

                while (readInput(Inputs::SensorEsquina1)) {
                    pausableDelay(1);
                }

                stage = Stage::MantenerCadena2s;
                break;

            case Stage::MantenerCadena2s:
                pausableDelay(2000);
                writeOutput(Outputs::BandaGrande1, false);
                writeOutput(Outputs::PlataformaElevador1, true);
                stage = Stage::CadenaSalida10s;
                break;

            case Stage::CadenaSalida10s:
                writeOutput(Outputs::BandaElevador1, true);
                writeOutput(Outputs::BandaGrande2, true);
                writeOutput(Outputs::BandaElevador2, true);
                writeOutput(Outputs::PlataformaElevador2, true);

                pausableDelay(10000);

                writeOutput(Outputs::BandaElevador1, false);
                writeOutput(Outputs::BandaGrande2, false);
                writeOutput(Outputs::BandaElevador2, false);
                writeOutput(Outputs::PlataformaElevador1, false);

                stage = Stage::ExtenderPiston;
                break;

            case Stage::ExtenderPiston:
                writeOutput(Outputs::ExtenderPiston, true);
                pausableDelay(2000);
                stage = Stage::BajarVentosaYTapas;
                break;

            case Stage::BajarVentosaYTapas:
                writeOutput(Outputs::BajarVentosaPiston, true);
                writeOutput(Outputs::VentosaPiston, true);
                writeOutput(Outputs::ElevadorTapas, true);

                // Original: waits 1000 ms OR until IN_PINS[7] goes LOW.
                // Poll at 1 ms to preserve the sensor override.
                for (int i = 0; i < 1000; ++i) {
                    if (!readInput(Inputs::EtapaPonerTapaPiston)) {
                        writeOutput(Outputs::ExtenderPiston, false);
                        writeOutput(Outputs::ElevadorTapas, false);
                        writeOutput(Outputs::VentosaPiston, false);
                        writeOutput(Outputs::BajarVentosaPiston, false);
                        writeOutput(Outputs::PlataformaElevador2, false);

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
                writeOutput(Outputs::ExtenderPiston, false);
                writeOutput(Outputs::ElevadorTapas, false);
                pausableDelay(2000);
                stage = Stage::SoltarTapa;
                break;

            case Stage::SoltarTapa:
                writeOutput(Outputs::VentosaPiston, false);
                pausableDelay(500);
                stage = Stage::RetraerVentosaYBajarPlataforma;
                break;

            case Stage::RetraerVentosaYBajarPlataforma:
                writeOutput(Outputs::BajarVentosaPiston, false);
                writeOutput(Outputs::PlataformaElevador2, false);
                pausableDelay(500);
                stage = Stage::BandaTransportadora3;
                break;

            case Stage::BandaTransportadora3:
                writeOutput(Outputs::BandaGrande3, true);
                writeOutput(Outputs::MiniTopeFinal, true);
                pausableDelay(5000);
                stage = Stage::DesactivarBanda3;
                break;

            case Stage::DesactivarBanda3:
                writeOutput(Outputs::BandaGrande3, false);
                pausableDelay(1000);
                stage = Stage::AsegurarPaletYSoltarTope;
                break;

            case Stage::AsegurarPaletYSoltarTope:
                writeOutput(Outputs::AsegurarPallet, true);
                writeOutput(Outputs::MiniTopeFinal, false);
                pausableDelay(1000);
                stage = Stage::LevantarLata;
                break;

            case Stage::LevantarLata:
                writeOutput(Outputs::ElevadorLata, true);
                pausableDelay(500);
                stage = Stage::VerificacionEstadoLata;
                break;

            case Stage::VerificacionEstadoLata:
                if (readInput(Inputs::VerificarEstadoLata)) {
                    stage = Stage::BajarLata;
                } else {
                    stage = Stage::Sellar;
                }
                break;

            case Stage::Sellar:
                writeOutput(Outputs::MotorSellador, true);
                pausableDelay(1000);
                stage = Stage::ApagarMotorSellado;
                break;

            case Stage::ApagarMotorSellado:
                writeOutput(Outputs::MotorSellador, false);
                pausableDelay(500);
                stage = Stage::BajarLata;
                break;

            case Stage::BajarLata:
                writeOutput(Outputs::ElevadorLata, false);
                pausableDelay(500);
                stage = Stage::DesasegurarPalet;
                break;

            case Stage::DesasegurarPalet:
                writeOutput(Outputs::AsegurarPallet, false);
                pausableDelay(500);
                stage = Stage::ActivarBanda3;
                break;

            case Stage::ActivarBanda3:
                writeOutput(Outputs::BandaGrande3, true);

                while (readInput(Inputs::SensorFinal)) {
                    pausableDelay(1);
                }

                stage = Stage::Apagar;
                break;

            case Stage::Apagar:
                writeOutput(Outputs::BandaGrande3, false);

                runLoop = false;
                break;
        }
    }
    allOff();
    if (forcedAuthorization) {
        authorizedSystem = false;
    }
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
        if (forcedAuthorization) {
            authorizedSystem = false;
        }
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
