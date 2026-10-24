### Documentación de Arquitectura, Estado y Funcionalidad del Sistema de Control de la Enlatadora

El presente documento detalla extensivamente la arquitectura de software, concurrencia, gestión de hardware y lógica de control secuencial del proyecto **Enlatadora_Control** desarrollado para el microcontrolador ESP32-S3 bajo el framework Arduino y FreeRTOS.

---

### 1. Punto de Entrada y Coordinación Global (`src/main.cpp`)

El archivo `src/main.cpp` actúa como el orquestador principal del firmware. Su propósito es inicializar los subsistemas en un orden determinista, establecer los enlaces de comunicación mediante callbacks reactivos y ejecutar el bucle periódico de telemetría de alta frecuencia.

```
                                  +-------------------+
                                  |   src/main.cpp    |
                                  +---------+---------+
                                            |
         +-----------------+----------------+----------------+-----------------+
         |                 |                                 |                 |
         v                 v                                 v                 v
+-----------------+ +---------------+               +-----------------+ +---------------+
| EvenTheRGBLed   | |  UARTManager  |               |  WifiManager    | | WebSever      |
| HasManagers     | |   (master)    |               |                 | | Manager       |
+-----------------+ +-------+-------+               +-----------------+ +-------+-------+
                            |                                                   |
                            +-------------------+-------------------------------+
                                                |
                                                v
                                      +-------------------+
                                      |  CanningMachine   |
                                      | (FSM & Real-time) |
                                      +-------------------+
```

#### 1.1 Inicialización Global
- **Gestor UART Maestro (`master`)**: Se reserva un puntero inteligente estático `static auto master = UARTManager::reserve();` para la interfaz de comunicación serial con el controlador de interfaz o panel externo.

#### 1.2 Flujo de `setup()`
1. **Inicialización del LED RGB (`EvenTheRGBLedHasManagers::init()`)**: Inicia el hilo en segundo plano que gestiona la iluminación de estado del sistema en el pin GPIO 48 del ESP32-S3.
2. **Creación del Canal UART (`UARTManager::create(Serial, 44, 43)`)**: Asigna el puerto de hardware `Serial` utilizando el pin 44 (RX) y pin 43 (TX) con tasa de baudios de 115200 bps.
3. **Arranque de la Enlatadora (`CanningMachine::setup(true)`)**: Configura el direccionamiento de GPIOs de entrada y salida, establece el estado seguro (todas las salidas apagadas) y levanta la tarea periódica de muestreo (`autoUpdate`).
4. **Acoplamiento de Eventos de Estado al Indicador LED**:
   - `setOnStartCallback`: Emite un pulso verde de 1000 ms.
   - `setOnStopCallback`: Emite un pulso rojo de 1000 ms.
   - `setOnPauseCallback`: Emite un pulso magenta de 1000 ms.
   - `setColorGenerator`: Define el estado lumínico de fondo dinámicamente:
     - Máquina ejecutándose y pausada: Amarillo al 50% de brillo (`RGBColor::Yellow().withAlpha(0.5)`).
     - Máquina ejecutándose normalmente: Blanco al 50% de brillo (`RGBColor::white().withAlpha(0.5)`).
     - Máquina detenida: LED apagado.
5. **Registro de Callbacks Seriales**:
   - `master->registerJsonCallback(CanningMachine::outputsWriteJsonCallback)`: Permite recibir estructuras JSON para comandar directamente actuadores cuando las condiciones de seguridad lo admiten.
   - `master->registerCallback(CanningMachine::processTextCommands)`: Escucha comandos textuales directos (`Start`, `Stop`, `Pause`, `Resume`, `ForceAuthorization`).
   - `master->init()`: Inicia formalmente el puerto y la tarea FreeRTOS de gestión de colas UART.
6. **Conectividad de Red (`WifiManager`)**:
   - Fija el hostname MDNS a `enlatadora-control`.
   - Fuerza el modo estación (`forceSTA`) con credenciales seguras.
   - Si la conexión tiene éxito, notifica mediante un destello verde de 1000 ms.
7. **Servicios Web y Mantenimiento Remoto (`WebSeverManager`)**:
   - Habilita soporte OTA (*Over-The-Air*) bajo la ruta `/firmware` protegido por credenciales básicas (`admin`/`admin`).
   - Configura callback `setOnUpdateSuccessful`: Al concluir una carga de firmware exitosa, fuerza la detención de la máquina (`Commands::Stop`), apaga todas las salidas y aplica un retardo antes del reinicio automático.
   - Inicializa el servidor HTTP asíncrono en el puerto 80.

#### 1.3 Ciclo Periódico `loop()`
El `loop()` principal de Arduino se ejecuta con un retardo cooperativo no bloqueante de 50 ms (`vTaskDelay(pdMS_TO_TICKS(50))`), transmitiendo continuamente por la línea serie maestra la cadena JSON generada por `CanningMachine::getSerializedState()`. Esto maximiza la observabilidad en tiempo real de la máquina para monitores externos.

---

### 2. Módulo de Señalización Visual (`src/drivers/s3/EvenTheRGBLedHasManagers`)

Controla el diodo NeoPixel WS2812 integrado en el hardware ESP32-S3 (GPIO 48) desacoplando la temporización de la señalización de la lógica de control principal.

```
       +---------------------------------------------+
       |           EvenTheRGBLedHasManagers          |
       |  (Tarea FreeRTOS "RGB-Loop", Período: 50ms) |
       +----------------------+----------------------+
                              |
              +---------------+---------------+
              |                               |
              v                               v
    [ Prioridad Alta ]              [ Prioridad Baja ]
   lastHoldTimestamp + holdDuration   colorGenerator()
     (Pulsos temporizados:             (Estado base:
      Verde, Rojo, Magenta)             Blanco/Amarillo)
              |                               |
              +---------------+---------------+
                              |
                              v
                     neopixelWrite(48)
```

#### 2.1 Componente `RGBColor`
Estructura inmutable con campos `r`, `g`, `b` (0-255).
- Proporciona constructores y fábricas estáticas para colores primarios y secundarios (`Off`, `Red`, `Green`, `Blue`, `Yellow`, `Magenta`, `Cyan`, `White`).
- Moduladores de brillo: `withAlpha(float)` y `withAlphaInt(uint8_t)` para escalado proporcional de intensidad luminosa.

#### 2.2 Motor de Gestión Concurrente
- **Tarea FreeRTOS dedicada**: `RGB-Loop` (Stack: 2048 bytes, Prioridad: 3).
- **Protección de Acceso**: `ledMutex` (`SemaphoreHandle_t`) para prevenir condiciones de carrera entre llamadas concurrentes desde distintas tareas.
- **Estrategia de Prioridad de Color**:
  1. *Color Temporal Prioritario (`holdColor`)*: Si `millis() < lastHoldTimestamp + holdDuration`, se despliega este color. Permite visualizar alertas y transiciones puntuales sin que la tarea que solicita el cambio deba bloquearse esperando que expire el tiempo.
  2. *Generador Dinámico (`colorGenerator`)*: Si no hay un pulso activo, se invoca la función lambda registrada para reflejar el estado actual del proceso en tiempo real.
  3. *Actualización Física*: Ejecuta `neopixelWrite(RGB_PIN, ...)` cada 50 ms.

---

### 3. Módulo de Comunicación y Telemetría Serial (`src/drivers/universal/UARTManager` y `Monitor`)

Proporciona una capa de abstracción sobre `HardwareSerial` que garantiza una transmisión y recepción libre de bloqueos mediante colas en memoria y tareas FreeRTOS.

```
       [ Hilos de Aplicación ]                    [ Puerto Físico UART ]
                 |                                          ^
                 | write() / writeLine()                    |
                 v                                          |
       +-------------------+                                |
       |    serialQueue    | ---> [ Tarea SerialQueue ] ---+
       | (Mutex protegido) |       (Ejecución periódica)
       +-------------------+                                |
                                                            | RX Stream
                                                            v
       [ Callbacks de Aplicación ] <--- [ Deserializador JSON / Texto ]
```

#### 3.1 Estructura y Funcionamiento de `UARTManager`
- **Patrón de Instanciación**: Constructor privado con fábrica estática `create()` que devuelve `std::unique_ptr<UARTManager>`.
- **Tarea de Transmisión Periódica**: Tarea `SerialQueue` (Stack: 8192 bytes, Prioridad: 1).
  - Utiliza `vTaskDelayUntil` para mantener una frecuencia constante (por defecto 20 ms / 50 Hz).
  - Vacía la cola `serialQueue` protegida por `queueMutex`.
  - Limita el tamaño máximo de la cola a 100 mensajes para evitar saturación de memoria RAM.
- **Procesamiento de Recepción (RX)**:
  - Buffer acumulador `rxBuffer` con longitud configurable.
  - Al detectar salto de línea (`\r` o `\n`), analiza el contenido con `ArduinoJson`:
    - Si es un JSON válido, dispara los callbacks en `serialJsonCallbacks`.
    - Si es texto plano, invoca `serialCallbacks`.

#### 3.2 Capa `Monitor`
Envoltorio estático global singleton sobre `UARTManager` que estandariza las salidas de depuración y registro del sistema mediante el puerto `Serial` estándar.

---

### 4. Módulo de Control de la Máquina Enlatadora (`src/machine/CanningMachine`)

Constituye el núcleo del sistema. Implementa la lógica de control secuencial determinista (FSM - Máquina de Estados Finita), la gestión de seguridad de entradas y salidas, la protección ante fallas y la concurrencia en tiempo real.

```
+-----------------------------------------------------------------------------+
|                          CanningMachine (ESP32-S3)                          |
|                                                                             |
|   +------------------------------------+  +-----------------------------+   |
|   | Tarea: CanningMachineAutoUpdate    |  | Tarea: CanningMachine (run) |   |
|   | Core: 1 | Prioridad: 2 | Stack: 2K |  | Core: 1 | Prioridad: 1 | 8K |   |
|   +-----------------+------------------+  +--------------+--------------+   |
|                     |                                    |                  |
|                     v                                    v                  |
|       +----------------------------+        +--------------------------+    |
|       | Actualización I0..I7 (10ms)|        | Máquina de Estados (FSM) |    |
|       | updateAuthorization()      |        | 25 Etapas Secuenciales   |    |
|       +--------------+-------------+        +------------+-------------+    |
|                      |                                   |                  |
|                      +-----------------+-----------------+                  |
|                                        |                                    |
|                                        v                                    |
|                    +---------------------------------------+                |
|                    |     pausableDelay() & Variables       |                |
|                    |              Atómicas                 |                |
|                    +---------------------------------------+                |
+-----------------------------------------------------------------------------+
```

#### 4.1 Mapeo de Hardware y Conexión Física de E/S

##### Entradas Digitales (`IN_PINS`, Total: 8)
Los pines están configurados como `INPUT` y son monitoreados continuamente:

| Índice | Pin GPIO | Función Asignada / Sensor | Lógica de Activación |
| :--- | :--- | :--- | :--- |
| **I0** | GPIO 40 | Sensor de Verificación de Estado de Lata | `HIGH`: Falla detectada (omite sellado) / `LOW`: Lata correcta |
| **I1** | GPIO 39 | Sensor de Salida de Banda 3 | Bloquea hasta que la lata/pallet es expulsado (`LOW`) |
| **I2** | GPIO 38 | Sensor de Plataforma de Salida | Bloquea hasta que la lata llega a posición (`LOW`) |
| **I3** | GPIO 37 | Sensor de Entrada / Detección de Lata | `LOW`: Lata detectada en espera de inicio de ciclo |
| **I4** | GPIO 36 | Pulsador de Autorización de Sistema | `LOW`: Habilita `authorizedSystem = true` |
| **I5** | GPIO 35 | Pulsador de Parada / Desautorización | `HIGH`: Deshabilita `authorizedSystem = false` |
| **I6** | GPIO 0  | Entrada Reservada / No utilizada en ciclo actual | - |
| **I7** | GPIO 45 | Sensor de Fin de Carrera de Ventosa / Tapas | `LOW`: Detección anómala / aborto de colocación de tapa |

##### Salidas Digitales (`ACTUATOR_PINS`, Total: 19)
Controlan los relés y actuadores neumáticos y motores de la línea:

| Índice | Pin GPIO | Actuador / Elemento Controlado |
| :--- | :--- | :--- |
| **Q0** | GPIO 18 | Motor de Cadena de Transporte Intermedio |
| **Q1** | GPIO 17 | Válvula de Llenado de Líquido en Tolva |
| **Q2** | GPIO 16 | Retenedor / Soporte de Plataforma de Tapas |
| **Q3** | GPIO 15 | Descenso de Mecanismo de Tapas |
| **Q4** | GPIO 7  | Pistón de Extensión del Dispensador de Tapas |
| **Q5** | GPIO 6  | Actuador de Descenso de Ventosa |
| **Q6** | GPIO 5  | Válvula de Vacío (Succión de Tapa) |
| **Q7** | GPIO 4  | Tope Mecánico de Pallet en Banda 3 |
| **Q8** | GPIO 13 | Mordaza / Cilindro de Aseguramiento de Pallet |
| **Q9** | GPIO 12 | Cilindro Elevador de Lata a Cabezal de Sellado |
| **Q10**| GPIO 11 | Salida Auxiliar / Reservada |
| **Q11**| GPIO 10 | Cadena de Salida (Accionamiento 1) |
| **Q12**| GPIO 9  | Cadena de Salida (Accionamiento 2) |
| **Q13**| GPIO 46 | Motor Principal del Cabezal de Sellado (Seamer) |
| **Q14**| GPIO 3  | Motor de Banda Transportadora 3 |
| **Q15**| GPIO 8  | Cadena de Salida (Accionamiento 3) |
| **Q16**| GPIO 41 | Motor de Cinta Transportadora de Entrada |
| **Q17**| GPIO 42 | Pistón Elevador de Plataforma de Tolva (Llenado) |
| **Q18**| GPIO 2  | Tope Mecánico Retráctil de Entrada |

---

#### 4.2 Tareas FreeRTOS y Concurrencia

1. **Tarea `CanningMachineAutoUpdate`**:
   - Asignada a Core 1, prioridad 2, tamaño de pila de 2048 bytes.
   - Bucle `autoUpdateLoop()`: Ejecuta cada 10 ms (`autoUpdatePeriod = 10`):
     - Muestrea todas las entradas digitales `I0` a `I7`.
     - Ejecuta `updateAuthorization()`:
       ```cpp
       if (!readInput(4, true) || forcedAuthorization) authorizedSystem = true;
       if (readInput(5, true)) {
           authorizedSystem = false;
           forcedAuthorization = false;
       }
       ```
2. **Tarea `CanningMachine` (`run`)**:
   - Asignada a Core 1, prioridad 1, tamaño de pila de 8096 bytes.
   - Aloja la ejecución de la máquina de estados.

#### 4.3 Variables Atómicas de Estado y Sincronización
Para prevenir fallos por acceso concurrente entre núcleos y tareas sin requerir el sobrecosto de bloqueos pesados, se emplean primitivas `std::atomic`:
- `std::atomic<bool> inputState[NUM_INPUTS]`: Reflejo en memoria de los sensores.
- `std::atomic<bool> outputState[NUM_OUTPUTS]`: Estado lógico de los actuadores.
- `std::atomic<Stage> stage`: Etapa actual de la FSM.
- `std::atomic<bool> authorizedSystem`: Permiso de operación del sistema.
- `std::atomic<bool> forcedAuthorization`: Flag de bypass manual de autorización.
- `std::atomic<bool> machineRunning`: Bandera de ciclo activo.
- `std::atomic<bool> hardwarePause` / `softwarePause`: Banderas de pausa.
- `std::atomic<TaskHandle_t> logicLoopHandle` / `autoUpdateTaskHandle`: Manejadores de tareas.

---

#### 4.4 Mecanismo de Retardo Pausable Seguro (`pausableDelay`)

El método `pausableDelay(uint32_t ms)` es fundamental para la seguridad del sistema:

```cpp
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
```

- **Operación Normal**: Divide el retardo en intervalos de 1 ms mediante `vTaskDelay`, cediendo tiempo de CPU a otras tareas.
- **Detección de Pausa**: Si `isPaused()` es verdadero:
  1. Captura una instantánea (`snapshot`) del estado exacto de todas las salidas `Q0..Q18`.
  2. Apaga de inmediato todos los actuadores (`allOff()`) para evitar daños mecánicos o vertidos.
  3. Permanece en espera hasta que la condición de pausa se desactive.
  4. Restaura con precisión el estado de cada actuador previo a la pausa y continúa la temporización exactamente en el milisegundo donde se interrumpió.

---

#### 4.5 Descripción Detallada de las 25 Etapas de la FSM

```
 [0: Reposo] ---> (Autorizado) ---> [1: EsperaLata] ---> (!I3) ---> [2: TopeActivo]
                                                                          |
 [5: Llenado] <--- [4: SubirPlataformaTolva] <--- [3: FrenadoYBajarTope] <-+
       |
       v
 [6: BajarPlataformaTolva] ---> [7: TrasladoAPlataformaSalida] ---> (!I2)
                                                                     |
 [10: ExtenderPiston] <--- [9: CadenaSalida10s] <--- [8: MantenerCadena2s]
       |
       v
 [11: BajarVentosaYTapas] --(!I7 / Aborto)--------------------------+
       |                                                            |
       v (Normal)                                                   |
 [12: RetraerPiston] ---> [13: SoltarTapa] ---> [14: RetraerVentosa] |
                                                        |           |
 [17: AsegurarPalet] <--- [16: DesactivarBanda3] <--- [15: Banda3] <+
       |
       v
 [18: LevantarLata] ---> [19: VerificacionEstadoLata]
                                 |             |
                               (I0)          (!I0)
                                 |             v
                                 |      [20: Sellar]
                                 |             |
                                 v             v
 [22: DesasegurarPalet] <--- [21: BajarLata] <-+
       |
       v
 [23: ActivarBanda3] ---> (!I1) ---> [24: Apagar] ---> [0: Reposo]
```

A continuación se describe exhaustivamente cada transición y acción física:

1. **`Stage::Reposo` (0)**:
   - Apaga todas las salidas (`allOff()`).
   - Verifica `authorizedSystem`. Si es verdadero, pasa a `EsperaLata`. De lo contrario, espera 1 ms.
2. **`Stage::EsperaLata` (1)**:
   - Enciende la cinta de entrada `Q16 = true` para transportar una lata hacia la máquina.
   - Monitorea `I3` mientras el sistema esté autorizado.
   - Si `I3` pasa a `LOW` (lata detectada por sensor de entrada), avanza a `TopeActivo`.
   - Si se retira la autorización antes de detectar una lata, retorna a `Reposo`.
3. **`Stage::TopeActivo` (2)**:
   - Activa el tope mecánico retráctil de entrada `Q18 = true` para frenar la lata.
   - Mantiene la retención por 2000 ms (`pausableDelay(2000)`).
   - Avanza a `FrenadoYBajarTope`.
4. **`Stage::FrenadoYBajarTope` (3)**:
   - Detiene la cinta de entrada `Q16 = false`.
   - Retrae el tope mecánico `Q18 = false`.
   - Avanza de inmediato a `SubirPlataformaTolva`.
5. **`Stage::SubirPlataformaTolva` (4)**:
   - Activa el cilindro elevador de la plataforma de llenado `Q17 = true`.
   - Espera 1000 ms para completar la elevación física.
   - Avanza a `Llenado`.
6. **`Stage::Llenado` (5)**:
   - Abre la válvula de dosificación de líquido `Q1 = true`.
   - Mantiene la dosificación activa durante 1000 ms.
   - Cierra la válvula `Q1 = false`.
   - Avanza a `BajarPlataformaTolva`.
7. **`Stage::BajarPlataformaTolva` (6)**:
   - Desactiva el cilindro elevador de la tolva `Q17 = false` para retornar la lata llena a la altura de la vía.
   - Avanza a `TrasladoAPlataformaSalida`.
8. **`Stage::TrasladoAPlataformaSalida` (7)**:
   - Enciende nuevamente la cinta `Q16 = true` para impulsar la lata llena hacia la plataforma de salida.
   - Bloquea en espera activa mientras el sensor `I2` permanezca en `HIGH`.
   - Al detectar la lata (`I2` pasa a `LOW`), avanza a `MantenerCadena2s`.
9. **`Stage::MantenerCadena2s` (8)**:
   - Espera 2000 ms para asegurar que la lata penetre completamente en la zona de transferencia.
   - Detiene la cinta de entrada `Q16 = false`.
   - Enciende el motor de la cadena intermedia `Q0 = true`.
   - Avanza a `CadenaSalida10s`.
10. **`Stage::CadenaSalida10s` (9)**:
    - Activa el conjunto de tracción y transferencia sincronizada: `Q11 = true`, `Q15 = true`, `Q12 = true` y retención de tapas `Q2 = true`.
    - Temporiza el arrastre durante 10000 ms (10 segundos).
    - Al expirar el tiempo, apaga las cadenas de arrastre `Q11 = false`, `Q15 = false`, `Q12 = false` y `Q0 = false` (manteniendo `Q2 = true`).
    - Avanza a `ExtenderPiston`.
11. **`Stage::ExtenderPiston` (10)**:
    - Activa el pistón horizontal del alimentador de tapas `Q4 = true`.
    - Espera 2000 ms para posicionar una tapa bajo la ventosa.
    - Avanza a `BajarVentosaYTapas`.
12. **`Stage::BajarVentosaYTapas` (11)**:
    - Activa el cilindro de descenso de ventosa `Q5 = true`, activa la válvula de vacío `Q6 = true` e inicia el descenso del cabezal de tapas `Q3 = true`.
    - **Monitoreo dinámico de seguridad (hasta 1000 ms)**: Muestrea cada 1 ms el sensor de seguridad `I7`.
      - Si `I7` pasa a `LOW` (falla en agarre o atasco de tapa): desactiva inmediatamente `Q4, Q3, Q6, Q5, Q2 = false` y salta la colocación de tapa derivando el proceso directo a `ActivarBanda3`.
      - Si transcurren los 1000 ms sin activación de `I7`, avanza regularmente a `RetraerPiston`.
13. **`Stage::RetraerPiston` (12)**:
    - Desactiva el pistón del alimentador `Q4 = false` y el actuador de descenso `Q3 = false` (manteniendo la succión `Q6` y la ventosa extendida `Q5`).
    - Espera 2000 ms para que el alimentador retorne a su origen.
    - Avanza a `SoltarTapa`.
14. **`Stage::SoltarTapa` (13)**:
    - Apaga la válvula de vacío `Q6 = false`, liberando la tapa sobre la boca de la lata.
    - Espera 500 ms para el asentamiento mecánico de la tapa.
    - Avanza a `RetraerVentosaYBajarPlataforma`.
15. **`Stage::RetraerVentosaYBajarPlataforma` (14)**:
    - Retrae el cilindro de la ventosa `Q5 = false` y libera la plataforma `Q2 = false`.
    - Espera 500 ms de estabilización mecánica.
    - Avanza a `BandaTransportadora3`.
16. **`Stage::BandaTransportadora3` (15)**:
    - Enciende el motor de la banda transportadora 3 `Q14 = true`.
    - Activa el tope neumático de pallet `Q7 = true` para atrapar el pallet con la lata en la estación de sellado.
    - Opera durante 5000 ms (5 segundos).
    - Avanza a `DesactivarBanda3`.
17. **`Stage::DesactivarBanda3` (16)**:
    - Apaga el motor de la banda 3 `Q14 = false`.
    - Espera 1000 ms para amortiguar cualquier inercia de la lata.
    - Avanza a `AsegurarPaletYSoltarTope`.
18. **`Stage::AsegurarPaletYSoltarTope` (17)**:
    - Activa la mordaza de sujeción del pallet `Q8 = true`.
    - Retrae el tope mecánico `Q7 = false`.
    - Espera 1000 ms de confirmación de fijación.
    - Avanza a `LevantarLata`.
19. **`Stage::LevantarLata` (18)**:
    - Activa el cilindro elevador inferior `Q9 = true` para presionar la lata y su tapa contra el mandril giratorio del cabezal de sellado.
    - Espera 500 ms para el acople vertical completo.
    - Avanza a `VerificacionEstadoLata`.
20. **`Stage::VerificacionEstadoLata` (19)**:
    - Evalúa el sensor de inspección `I0`:
      - Si `I0 == HIGH` (anomalía de altura o deformación de la lata): salta directamente a `BajarLata` sin accionar el motor de sellado (protege la mecánica del cabezal).
      - Si `I0 == LOW` (condición óptima): avanza a `Sellar`.
21. **`Stage::Sellar` (20)**:
    - Enciende el motor de alta potencia del cabezal de sellado `Q13 = true` (pestañado y doble cierre de la tapa).
    - Mantiene el ciclo de sellado durante 1000 ms.
    - Avanza a `ApagarMotorSellado`.
22. **`Stage::ApagarMotorSellado` (21)**:
    - Apaga el motor de sellado `Q13 = false`.
    - Espera 500 ms para detención por inercia del cabezal.
    - Avanza a `BajarLata`.
23. **`Stage::BajarLata` (22)**:
    - Desactiva el cilindro elevador `Q9 = false`, descendiendo la lata sellada nuevamente hacia el pallet.
    - Espera 500 ms de estabilización.
    - Avanza a `DesasegurarPalet`.
24. **`Stage::DesasegurarPalet` (23)**:
    - Libera la mordaza de fijación del pallet `Q8 = false`.
    - Espera 500 ms.
    - Avanza a `ActivarBanda3`.
25. **`Stage::ActivarBanda3` (24)**:
    - Enciende la banda de salida `Q14 = true` para evacuar el pallet con la lata terminada.
    - Espera mientras el sensor de salida `I1` permanezca en `HIGH`.
    - Al activarse el sensor de salida (`I1` pasa a `LOW`), avanza a `Apagar`.
26. **`Stage::Apagar` (25)**:
    - Detiene la banda de salida `Q14 = false`.
    - Finaliza el bucle de ejecución (`runLoop = false`).
    - Al salir del bucle: apaga todas las salidas (`allOff()`), restablece banderas (`machineRunning = false`, `stage = Stage::Reposo`), elimina el manejador `logicLoopHandle` y autoelimina la tarea FreeRTOS mediante `vTaskDelete(nullptr)`.

---

#### 4.6 Gestión de Comandos y API Externa

##### Comandos de Texto Soportados (`processTextCommands`)
- `Start`: Llama a `start()`. Si el sistema está configurado y no está en ejecución, instancia la tarea `run()` con 8 KB de pila en Core 1 e inicia la FSM desde `Stage::Reposo`.
- `Stop`: Llama a `stop()`. Elimina inmediatamente la tarea FreeRTOS `logicLoopHandle`, invoca `allOff()`, restablece `machineRunning = false`, cancela pausas y retorna a `Reposo`.
- `Pause`: Fija `softwarePause = true`. La FSM se congela de forma segura en `pausableDelay`.
- `Resume`: Fija `softwarePause = false`. La FSM reanuda la operación restaurando los actuadores.
- `ForceAuthorization`: Fija `forcedAuthorization = true` para permitir pruebas o bypass de los sensores físicos de autorización `I4`/`I5`.

##### Interfaz JSON para Control de Salidas (`outputsWriteJsonCallback`)
Permite forzado manual de actuadores desde la interfaz web o puerto serie bajo reglas estrictas:
```json
{
  "Q0": true,
  "Q1": false
}
```
**Reglas de Seguridad**:
- Si la máquina está en `hardwarePause`, el forzado se rechaza.
- Si la máquina está en ejecución (`machineRunning == true`) y **no** está en pausa de software, el forzado se rechaza para evitar colisiones con la FSM.

##### Serialización de Estado (`getSerializedState`)
Genera la cadena JSON que se emite cada 50 ms por UART:
```json
{
  "running": true,
  "emergencyStop": false,
  "paused": false,
  "authorizedSystem": true,
  "forcedAuthorization": false,
  "stage_name": "Llenado",
  "stage_num": 5,
  "inputs": {
    "I0": false, "I1": true, "I2": true, "I3": false,
    "I4": false, "I5": false, "I6": false, "I7": true
  },
  "outputs": {
    "Q0": false, "Q1": true, "Q2": false, "Q3": false,
    "Q4": false, "Q5": false, "Q6": false, "Q7": false,
    "Q8": false, "Q9": false, "Q10": false, "Q11": false,
    "Q12": false, "Q13": false, "Q14": false, "Q15": false,
    "Q16": false, "Q17": true, "Q18": false
  }
}
```

---

### 5. Módulo de Conectividad de Red (`src/drivers/universal/WifiManager`)

Gestiona la conexión a la infraestructura de red local y la resolución de nombres DNS multidifusión (mDNS).

- **Modos de Operación**:
  - `forceSTA`: Configura conexión directa en modo estación a la red Wi-Fi configurada.
  - `forceAp`: Modo punto de acceso autónomo de rescate (IP: `192.168.4.1`).
  - Persistencia NVS vía `Preferences`: Permite almacenar y recuperar SSID y contraseña en memoria no volátil mediante `wifiConfigJsonCallback`.
- **Servicio mDNS**: Inicia el resolvedor local `enlatadora-control.local` y publica el servicio HTTP en el puerto 80 y MQTT en el puerto 1883.

---

### 6. Módulo de Servicios Web y Actualizaciones Remotas (`src/network/WebSeverManager`)

Implementa un servidor web asíncrono basado en `ESPAsyncWebServer` sobre el puerto TCP 80, centrado exclusivamente en la telemetría y el mantenimiento del firmware.

- **Actualización de Firmware OTA (`enableOTASupport`)**:
  - Ruta configurada: `/firmware` (método `HTTP_POST`).
  - Seguridad: Autenticación básica HTTP (`admin` / `admin`).
  - Manejo de Transmisión:
    - Admite cargas multiparte (`multipart/form-data`) o flujos binarios directos (`application/octet-stream`).
    - Utiliza la librería nativa de ESP32 `Update` escribiendo bloques directamente en la partición flash OTA.
  - Finalización Segura y Reinicio:
    - Una vez completada la escritura y verificación de suma de comprobación, ejecuta el callback `onUpdateSuccessful` registrado en `main.cpp`.
    - Dicho callback detiene el proceso industrial (`CanningMachine::processCommand(Stop)`), asegura todos los actuadores en estado bajo (`allOff()`) y programa una tarea independiente que tras 1000 ms invoca `ESP.restart()`.
- **Endpoints de Configuración JSON**:
  - `registerJsonPostEndpoint`: Permite registrar callbacks que deserializan cargas útiles JSON directamente desde peticiones POST HTTP.

---

### 7. Banderas de Compilación y Modos de Prueba (`src/config/build_flags_example.h`)

El firmware está preparado para pruebas y depuración tanto en laboratorio como en el equipo físico mediante directivas del preprocesador:

- `INPUT_LOGIC_TEST`: Desactiva la lectura física con `digitalRead` en los pines de entrada, permitiendo simular estados lógicos de sensores inyectando valores vía `inputsWriteJsonCallback`. Por defecto simula la entrada `I4` en `true` para autorizar el sistema en pruebas.
- `DISABLE_REAL_OUTPUT`: Inhibe la ejecución de `digitalWrite` hacia los pines de actuadores reales, protegiendo los relés y salidas de potencia durante pruebas lógicas de la FSM.

---

### 8. Conclusiones y Resumen Operativo

El código de la Enlatadora presenta una arquitectura concurrente y robusta diseñada para garantizar máxima confiabilidad y seguridad operativa:
1. **Desacoplamiento Total**: Las tareas de tiempo real (FSM y muestreo de sensores), señalización visual (LED NeoPixel), comunicación serial (UART) y servicios de red (HTTP/OTA) corren en tareas independientes de FreeRTOS con prioridades balanceadas.
2. **Determinismo y Seguridad Industrial**: La FSM en `CanningMachine.cpp` implementa validaciones estrictas en cada transición, abortos automáticos ante anomalías de sensores (como `I7` e `I0`) y retenciones seguras con apagado preventivo de actuadores ante pausas o paradas de emergencia.
3. **Observabilidad Continua**: El sistema publica periódicamente el estado atómico de todas sus variables, entradas y salidas, permitiendo la integración transparente con dashboards y consolas externas.
