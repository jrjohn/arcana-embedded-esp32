<p align="center">
  <img src="https://img.shields.io/badge/Architecture-Service_+_Observable_+_Timer-gold?style=for-the-badge" alt="Architecture">
  <img src="https://img.shields.io/badge/MCU-ESP32-E7352C?style=for-the-badge&logo=espressif" alt="ESP32">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-00A86B?style=for-the-badge" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Language-C++23-00599C?style=for-the-badge&logo=cplusplus" alt="C++">
  <img src="https://img.shields.io/badge/IDF-v5.5.2-blue?style=for-the-badge" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/BLE-Bluedroid_Dual--Role-0082FC?style=for-the-badge&logo=bluetooth" alt="BLE">
  <img src="https://img.shields.io/badge/Crypto-AES--256--CCM_+_ECDH-8B5CF6?style=for-the-badge" alt="Crypto">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License">
</p>

<h1 align="center">Arcana Embedded ESP32</h1>

<p align="center">
  <strong>Modern C++23 IoT platform: Service Pattern + Observable Event System + Encrypted Command Pipeline</strong>
</p>

<p align="center">
  <a href="#architecture-evaluation">Evaluation</a> &bull;
  <a href="#system-architecture">Architecture</a> &bull;
  <a href="#service-pattern">Service Pattern</a> &bull;
  <a href="#controller-lifecycle">Controller</a> &bull;
  <a href="#data-flow">Data Flow</a> &bull;
  <a href="#arcana-frame-protocol">Frame Protocol</a> &bull;
  <a href="#command-protocol">Command Protocol</a> &bull;
  <a href="#security">Security</a> &bull;
  <a href="#ble-dual-role">BLE</a> &bull;
  <a href="#observable-pattern">Observable</a> &bull;
  <a href="#getting-started">Getting Started</a>
</p>

---

## Architecture Evaluation

### Pros

| # | Strength | Details |
|---|----------|---------|
| 1 | **Service pattern with Input/Output wiring** | Services declare typed dependencies as struct fields; Controller wires Observable pointers at startup, achieving compile-time dependency injection without a DI framework |
| 2 | **Task ownership rule** | ServiceImpl never calls `xTaskCreate`. Observable owns dispatch tasks; `esp_timer` owns periodic behavior. Concurrency management is centralized, not scattered across services |
| 3 | **Dual-rate TimerService** | Single `esp_timer` at 100ms with counter divider for 1000ms. Services choose FastTimer or BaseTimer based on their needs. Adding a new rate is one divider counter away |
| 4 | **Unified command pipeline** | BLE and MQTT share identical wire format (Frame + protobuf + AES-256-CCM). Single CommandCodec handles both transports |
| 5 | **Perfect Forward Secrecy** | ECDH P-256 session keys are independent of PSK; compromised PSK does not expose past sessions. Per-connection isolation (4 slots) |
| 6 | **Two-stage event pipelines** | Sensor/Timer events flow: sync Observable (producer task) -> async Observable (dedicated dispatch task), cleanly decoupling producers from consumers |
| 7 | **4-phase Controller lifecycle** | `wireServices -> initHAL -> initServices -> startServices` with explicit ordering rationale. Late-wiring pattern for bridge inputs ensures dependencies are initialized first |
| 8 | **Protocol layering** | Frame (magic + CRC) / Encryption (AES-CCM) / Serialization (protobuf) -- each layer is independently testable and transport-agnostic |
| 9 | **Extensibility** | Adding a command = 1 header-only class + 1 factory switch case. Adding a service = abstract base + singleton impl + wire in Controller |

### Cons

| # | Issue | Severity | Details | Location |
|---|-------|----------|---------|----------|
| 1 | **7 async Observables = 7 FreeRTOS tasks** | Medium | Each named Observable creates a task + queue. 7 Observables consume ~16 KB stack RAM (2x3072 + 5x2048) + 7 TCBs (~504B) + 7 queues. On ESP32 with ~200 KB free DRAM this is ~9% just for event dispatch | All `new Observable<T>("name")` calls |
| 2 | **LED double queue hop** | Low | Each LED frame traverses two async queues: `esp_timer -> FastTimer queue -> LED callback -> LedObservable queue -> hardware callback`. Adds ~2ms latency per hop. Acceptable for LED cycling but would matter for latency-sensitive subscribers | `LedServiceImpl.cpp:61,70,79` |
| 3 | **`MqttCommandEvent` raw pointer** | **High** | `MqttCommandEvent` stores `const uint8_t* Data` pointing into MQTT event buffer. The Observable is async (queue), but `xQueueSend` does `memcpy` of the struct -- copying the pointer, not the data. If dispatch happens after MQTT recycles the buffer: **use-after-free** | `MqttTypes.hpp:10` |
| 4 | **`SensorError` contains `std::string`** | **High** | `SensorError::Message` is `std::string`. Passing through FreeRTOS queue (`xQueueSend` does raw `memcpy`) would bypass copy constructor, causing double-free. Latent bug -- fires when sensor errors actually occur. `SensorErrorV` (variant version with `char[64]`) exists but is not used | `SensorTypes.hpp:107` |
| 5 | **`CommandService` naming inconsistency** | Low | Uses `Instance()` / `Start()` / `Stop()` (PascalCase) while all other services use `getInstance()` / `start()` / `stop()` (camelCase). Controller calls `mCommand->Start()` vs `mLed->start()` | `CommandService.hpp:29,33-34`, `Controller.cpp:118` |
| 6 | **Controller skips Bridge lifecycle** | Low | `mBridge->init_HAL()` and `mBridge->start()` are never called. Harmless (both are no-ops) but breaks the pattern symmetry of calling all 4 lifecycle methods on every service | `Controller.cpp:67-76,114-122` |
| 7 | **Unnecessary `static_cast`** | Low | Controller casts `*mBle` to `BleTransportServiceImpl&` to call `server()`, but `server()` is already `virtual` on the abstract base `BleTransportService` | `Controller.cpp:100` |
| 8 | **Silent event drops on queue full** | Medium | `xQueueSend(mQueue, &Data, 0)` uses timeout=0: if queue (depth=20) is full, events are silently dropped with no log or metric. At 100ms fast timer rate, a slow subscriber could miss ticks | `Observable.hpp:208` |
| 9 | **`CONFIG_RGB_LED_CYCLE_INTERVAL_MS` orphaned** | Low | Still defined in `RgbLed/Kconfig` and present in sdkconfig, but never referenced in code after timer refactor. Dead config | `RgbLed/Kconfig:17` |
| 10 | **Duplicate Kconfig MQTT topics** | Low | `CommandService/Kconfig` defines `CMD_MQTT_CMD_TOPIC` / `CMD_MQTT_RSP_TOPIC`. `MqttService/Kconfig` defines `MQTT_SVC_CMD_TOPIC` / `MQTT_SVC_RSP_TOPIC`. Only the latter are used in code. The former are dead config | `CommandService/Kconfig:10,16` |

### Trade-offs

| Decision | Trade-off | Rationale |
|----------|-----------|-----------|
| Bluedroid (not NimBLE) | ~400 KB Flash | Dual-role GATT Server+Client with mature API |
| `std::function` callbacks | ~40 bytes per subscriber | Type erasure flexibility; StaticObservable available for zero-heap |
| Manual HKDF | ~50 lines of code | `MBEDTLS_HKDF_C` not enabled in ESP-IDF default sdkconfig |
| nanopb (not full protobuf) | Manual `.options` file | 10x smaller than protobuf-c, fits embedded constraints |
| Singleton pattern | Global state | Natural fit for hardware peripherals (BLE, sensor); single instance enforced |
| Custom Frame (not COBS/SLIP) | 9 bytes overhead | Includes version + flags + stream ID + magic for protocol detection; CRC covers entire frame |
| 1 task per async Observable | 2-3 KB RAM per Observable | Clean decoupling; alternative would be shared thread pool with priority inversion risk |
| TimerTypes in ObservableSensor | Foundation component grows | Avoids circular dependency between `main/` and component layer |

### Transport Compatibility

| Transport | Status | Notes |
|-----------|--------|-------|
| **BLE GATT** | Supported | Write to 0xFF10, Notify on 0xFF11 |
| **MQTT** | Supported | Binary payload on `arcana/cmd` / `arcana/rsp` |
| **UART** | Ready | Frame layer provides packet boundaries + CRC |
| **TCP Raw Socket** | Ready | Frame layer provides length-delimited framing |

---

## System Architecture

```
+-------------------------------------------------------------------------+
|                          APPLICATION LAYER                               |
|                                                                          |
|  +------------------+  +-------------------+  +----------------------+   |
|  | TimerService     |  | SensorService     |  |  LedService          |   |
|  | (Arcana::Timer)  |  | (Arcana::Sensor)  |  |  (Arcana::Led)       |   |
|  |                  |  |                   |  |                      |   |
|  | esp_timer ------>|  | DhtSensor ------->|  | Input: TimerEvents   |   |
|  |  FastTimer 100ms |  |  DataEvents       |  | Output: LedObservable|   |
|  |  BaseTimer 1000ms|  |                   |  |                      |   |
|  +--------+---------+  |  ErrorEvents      |  +----------+-----------+   |
|           |             |  [RTOS Task]      |             |              |
|           |             +---+---------------+             |              |
|           |                 |                             |              |
|           |  +--------------v-----------+                 v              |
|           |  |   BleTransportService    |        RgbLed (WS2812B)       |
|           |  |   (Arcana::Ble)          |        RMT peripheral         |
|           |  |                          |                               |
|           |  |   BleGap (ADV/Scan)      |                               |
|           |  |   BleGattServer (0x181A) |                               |
|           |  |   BleGattClient          |                               |
|           |  +-----+----+--------------+                                |
|           |        |    |                                               |
|  +--------+--------v----v-------------------------------------------+   |
|  |              CommandBridgeService (main/)                        |   |
|  |  Subscribes: BLE cmds + MQTT cmds + Responses + Connections      |   |
|  |  Decodes/Encodes via CommandCodec, routes to/from transports     |   |
|  +---------------------+-------------------------------------------+   |
|                         |                                               |
|  +---------------------v-------------------+  +---------------------+   |
|  |        CommandService                   |  | MqttTransportService|   |
|  |        (Arcana::Command)                |  | (Arcana::Mqtt)      |   |
|  |                                         |  |                     |   |
|  |  CommandDispatcher (EventQueue<10>)     |  | MQTT5 Client        |   |
|  |  CommandFactory (9 ICommand impls)      |  | CommandEvents       |   |
|  |  CommandCodec (Frame+PB+AES-256)        |  | ConnectionStatus    |   |
|  |  KeyExchangeManager (ECDH P-256)        |  +---------------------+   |
|  +------------------------------------------+                           |
|                                                                          |
+--------------------------------------------------------------------------+
|                          PROTOCOL LAYER                                   |
|                                                                          |
|  Application     CommandRequest / CommandResponse                        |
|       |                                                                  |
|  Serialization   nanopb protobuf encode/decode                          |
|       |                                                                  |
|  Encryption      AES-256-CCM [counter:4][cipher][tag:8] (optional)      |
|       |                                                                  |
|  Framing         [magic:2][ver:1][flags:1][sid:1][len:2][payload][crc:2] |
|       |                                                                  |
|  Transport       BLE / MQTT / UART / TCP                                |
|                                                                          |
+--------------------------------------------------------------------------+
|                          SYSTEM LAYER                                     |
|                                                                          |
|  +-----------+  +----------------------------------------------+         |
|  |   WiFi    |  |          Bluedroid BLE Stack                 |         |
|  | (esp_wifi)|  |  +------+  +--------+  +--------+           |         |
|  |           |  |  | GAP  |  | GATTS  |  | GATTC  |           |         |
|  +-----+-----+  |  +------+  +--------+  +--------+           |         |
|        |         +--------------------+------------------------+         |
|        |    WiFi+BLE Coexistence      |                                  |
|        +-------------+---------------+                                   |
|                       |                                                   |
+-----------------------+---------------------------------------------------+
|                     FreeRTOS KERNEL                                        |
+-------------------------+-------------------------------------------------+
|                    ESP32 HARDWARE                                          |
|            (520KB SRAM / 4MB Flash / RMT / GPIO)                          |
+--------------------------------------------------------------------------+
```

### Component Map

| Component | Namespace | Service | Role |
|-----------|-----------|---------|------|
| `ObservableSensor` | `Arcana::Sensor` | `SensorService` | Observable pattern, sensor base, DHT driver, shared types |
| `BleService` | `Arcana::Ble` | `BleTransportService` | BLE GAP, GATT server/client, transport layer |
| `CommandService` | `Arcana::Command` | `CommandService` | Command pattern with protobuf + AES-256-CCM + ECDH |
| `MqttService` | `Arcana::Mqtt` | `MqttTransportService` | MQTT5 client transport layer |
| `RgbLed` | `Arcana::Led` | `LedService` | WS2812B RGB LED strip via RMT peripheral |
| `main/` | `Arcana` / `Arcana::Timer` | `TimerService`, `CommandBridgeService`, `Controller` | App entry, wiring, timer, command bridge |

### Component Dependency Graph

```
main (Controller, TimerService, CommandBridge)
  +-- esp_timer         (TimerServiceImpl)
  +-- CommandService
  |     +-- mbedtls          (AES-256-CCM, ECDH, HMAC, SHA-256)
  |     +-- esp_hw_support   (CRC-16 ROM acceleration)
  |     +-- BleService
  |     |     +-- bt         (Bluedroid)
  |     |     +-- nvs_flash
  |     |     +-- esp_event
  |     |     +-- ObservableSensor   <-- foundation component
  |     |           +-- freertos
  |     |           +-- esp_timer
  |     |           +-- driver
  |     +-- ObservableSensor (reused)
  |     +-- nanopb           (managed component)
  +-- RgbLed
  |     +-- driver           (RMT for WS2812B)
  |     +-- esp_timer
  |     +-- ObservableSensor (reused)
  +-- MqttService
  |     +-- mqtt             (esp_mqtt)
  |     +-- ObservableSensor (reused)
  +-- protocol_examples_common (WiFi helpers)
```

---

## Service Pattern

Every service follows a consistent abstract base / Meyer's singleton implementation pattern:

```cpp
// Abstract base (in component include/)
class XxxService {
public:
    struct Input  { Observable<SomeEvent>* Events = nullptr; };  // dependencies
    struct Output { Observable<MyEvent>*   Data   = nullptr; };  // publications

    Input  input;
    Output output;

    virtual esp_err_t init_HAL() = 0;   // Phase 1: hardware peripherals
    virtual esp_err_t init()     = 0;   // Phase 2: subscriptions + logic
    virtual esp_err_t start()    = 0;   // Phase 3: activate
    virtual void      stop()     = 0;   // Deactivate
};

// Implementation (Meyer's singleton)
class XxxServiceImpl : public XxxService {
public:
    static XxxService& getInstance();   // singleton access
    // ... override all four lifecycle methods
private:
    XxxServiceImpl();                   // allocates output Observables
};
```

### Service Map

| Service | Input | Output | Task Ownership |
|---------|-------|--------|----------------|
| **TimerService** | (none) | `FastTimer` (100ms), `BaseTimer` (1000ms) | `esp_timer` fires at fast rate; counter divider produces base rate |
| **SensorService** | (none) | `DataEvents`, `ErrorEvents`, `Sensor*` | ObservableSensor creates FreeRTOS task |
| **BleTransportService** | `SensorDataEvents` | `ConnectionEvents`, `CommandWriteEvents` | Bluedroid stack tasks |
| **MqttTransportService** | `SensorDataEvents` | `CommandEvents`, `ConnectionStatus` | esp_mqtt_client task |
| **LedService** | `TimerEvents` | `LedObservable` | No own task; timer-driven |
| **CommandService** | `Sensor*` | `ResponseEvents`, `KeyExchangeMgr`, `Factory` | EventQueue creates async task |
| **CommandBridgeService** | 8 fields (BLE+MQTT+Command) | (none) | Purely reactive (subscription callbacks) |

### Task Ownership Rule

**ServiceImpl never calls `xTaskCreate`.** Task lifecycle is owned by:

| Task Source | Mechanism | Example |
|-------------|-----------|---------|
| `Observable<T>("name")` | Named constructor creates FreeRTOS task + queue | `"TimerSvc FastTimer"`, `"TimerSvc BaseTimer"`, `"LedSvc Observable"` |
| `EventQueue<T, N>` | `Start()` creates FreeRTOS task | CommandDispatcher async queue |
| `ObservableSensor` | Base class creates sensor reading task | `"sensor_0"` |
| `esp_timer` | ESP-IDF timer task fires callbacks | TimerServiceImpl periodic tick |
| ESP-IDF stacks | Internal tasks | Bluedroid, WiFi, MQTT client |

---

## Controller Lifecycle

The Controller (`main/Controller.cpp`) is a Meyer's singleton that orchestrates all services through a 4-phase lifecycle:

```
app_main()
  nvs_flash_init()
  esp_netif_init()
  esp_event_loop_create_default()
  Controller::getInstance().run()

Controller::run()
  wireServices()      Phase 0: get singletons, wire Input/Output pointers
  initHAL()           Phase 1: hardware peripherals
  initServices()      Phase 2: subscriptions + logic
  example_connect()   WiFi must be up before MQTT
  startServices()     Phase 3: activate all services
```

### Phase 0: wireServices()

Gets singleton references and connects Observable pointers between services:

```
mTimer   = &TimerServiceImpl::getInstance()     // allocates FastTimer + BaseTimer Observables
mSensor  = &SensorServiceImpl::getInstance()    // allocates DataEvents, ErrorEvents
mLed     = &LedServiceImpl::getInstance()       // allocates LedObservable
mBle     = &BleTransportServiceImpl::getInstance()
mMqtt    = &MqttTransportServiceImpl::getInstance()
mBridge  = &CommandBridgeServiceImpl::getInstance()
mCommand = &CommandService::Instance()

// Wire dependencies via Input structs
mLed->input.TimerEvents      = mTimer->output.FastTimer
mBle->input.SensorDataEvents = mSensor->output.DataEvents
mCommand->input.Sensor        = mSensor->output.Sensor
```

Output Observables are allocated in constructors (`new Observable<T>("name")`), so pointers are valid here. Bridge wiring happens later (Phase 2) because its inputs depend on `init_HAL()` / `init()` populating BLE/MQTT/Command outputs.

### Phase 1: initHAL()

```
mTimer->init_HAL()     // esp_timer_create(periodic callback)
mSensor->init_HAL()    // DhtSensor static instance, output.Sensor = &dht
mBle->init_HAL()       // BleService::Init(), sets output Observable pointers
mLed->init_HAL()       // RgbLed RMT channel setup
mMqtt->init_HAL()      // reads Kconfig topic
```

### Phase 2: initServices()

```
mTimer->init()         // computes base divider (base_ms / fast_ms)
mSensor->init()        // subscribes DhtSensor events -> service Observables
mBle->init()           // subscribes to SensorDataEvents for GATT notifications
mCommand->init()       // creates KeyExchangeManager, Factory, Dispatcher

// Late wiring: Bridge inputs depend on BLE/MQTT/Command init outputs
mBridge->input.BleConnectionEvents    = mBle->output.ConnectionEvents
mBridge->input.BleCommandWriteEvents  = mBle->output.CommandWriteEvents
mBridge->input.MqttCommandEvents      = mMqtt->output.CommandEvents
mBridge->input.MqttConnectionStatus   = mMqtt->output.ConnectionStatus
mBridge->input.CommandResponseEvents  = mCommand->output.ResponseEvents
mBridge->input.KeyExchangeMgr         = mCommand->output.KeyExchangeMgr
mBridge->input.Factory                = mCommand->output.Factory
mBridge->input.MqttTransport          = mMqtt
mBridge->input.BleServer              = &mBle->server()

mBridge->init()        // subscribes to all 5 input event streams
mLed->init()           // subscribes to TimerEvents + own LedObservable
mMqtt->init()          // (no-op)
```

### Phase 3: startServices()

```
mTimer->start()        // esp_timer_start_periodic (CONFIG_TIMER_FAST_INTERVAL_MS = 100ms)
mSensor->start()       // ObservableSensor FreeRTOS task starts reading
mBle->start()          // BLE advertising begins
mCommand->Start()      // CommandDispatcher async queue task starts
mLed->start()          // sets mRunning=true (timer ticks now produce LED frames)
mMqtt->start()         // MQTT client connects to broker
```

---

## Data Flow

### Timer -> LED Cycling

```
esp_timer periodic callback (every 100ms, timer task context)
  -> TimerServiceImpl::periodic_timer_callback()
    -> output.FastTimer->Notify(TimerTick)           [async: "TimerSvc FastTimer" 3072B]
      -> LedServiceImpl (subscribed in init)
        -> if mRunning: build LedFrame, cycle color index
        -> output.LedObservable->Notify(frame)       [async: "LedSvc Observable" 3072B]
          -> LedServiceImpl self-subscription
            -> RgbLed::SetColor() per LED + Show()    [RMT peripheral]
    -> every 10th tick: output.BaseTimer->Notify()   [sync: "TimerSvc BaseTimer"]
      -> (future subscribers at 1000ms rate)
```

### Sensor -> BLE Notifications

```
ObservableSensor task (periodic ReadHardware)
  -> DhtSensor::ReadHardware() [GPIO bit-banging, critical section]
  -> mDataObservable.Notify(SensorData)              [sync, sensor task]
    -> SensorServiceImpl (subscribed in init)
      -> output.DataEvents->Notify(data)             [async: "SensorSvc DataEvents"]
        -> BleTransportServiceImpl (subscribed in init)
          -> BleGattServer::UpdateTemperature/UpdateHumidity
          -> esp_ble_gatts_send_indicate() per client
```

### BLE Command -> Response

```
BLE GATT write to 0xFF10
  -> BleGattServer::HandleGattsEvent()
    -> output.CommandWriteEvents->Notify(event)      [sync]
      -> CommandBridgeServiceImpl (subscribed in init)
        -> CommandCodec::DecodeRequest()
          -> FrameCodec::Deframe() -> CryptoEngine::Decrypt() -> pb_decode
        -> CommandService::HandleRequest()
          -> CommandDispatcher -> CommandFactory::Create() -> ICommand::Execute()
          -> output.ResponseEvents->Notify(response) [sync]
            -> CommandBridgeServiceImpl (subscribed in init)
              -> CommandCodec::EncodeResponse()
                -> pb_encode -> CryptoEngine::Encrypt() -> FrameCodec::Frame()
              -> BleGattServer::SendCommandResponse()
```

### MQTT Command -> Response

```
MQTT broker -> esp_mqtt event handler
  -> output.CommandEvents->Notify(MqttCommandEvent)  [async: "MqttSvc CommandEvents"]
    -> CommandBridgeServiceImpl (subscribed in init)
      -> Same decode/dispatch path as BLE
      -> MqttTransportService::publish("arcana/rsp", framed_response)
```

### BLE Disconnect -> Session Cleanup

```
BLE disconnect event
  -> output.ConnectionEvents->Notify(event)
    -> CommandBridgeServiceImpl (subscribed in init)
      -> KeyExchangeManager::RemoveSession(BLE, connId)
```

---

## Arcana Frame Protocol

Every Arcana packet -- whether plaintext or encrypted -- is wrapped in a Frame for transport integrity.

### Wire Layout

```
Offset:  0     1     2     3     4     5     6     7        7+N   7+N+1
       +-----+-----+-----+-----+-----+-----+-----+--------+-----+-----+
       | 0xAC| 0xDA| 0x01| Flg | SID |  Length LE | Payload|  CRC-16 LE|
       +-----+-----+-----+-----+-----+-----+-----+--------+-----+-----+
       |<- Magic ->| Ver |     |     |<-- 2B  -->|<- N B ->|<-- 2B  -->|
       |                                                    |
       |<----------------- CRC-16 covers ------------------>|
```

| Field | Offset | Size | Value | Description |
|-------|--------|------|-------|-------------|
| **Magic** | 0 | 2 | `0xAC 0xDA` | "Arcana Data" identifier |
| **Version** | 2 | 1 | `0x01` | Protocol version (v1) |
| **Flags** | 3 | 1 | bitfield | Bit 0: FIN (last frame in stream); bits 1-7 reserved (must be 0) |
| **Stream ID** | 4 | 1 | `0x00-0xFF` | Stream identifier for request-response correlation |
| **Length** | 5 | 2 | LE uint16 | Payload length (excludes header and CRC) |
| **Payload** | 7 | N | -- | Encrypted: `[counter:4][cipher][tag:8]`; Plaintext: raw protobuf |
| **CRC-16** | 7+N | 2 | LE uint16 | `esp_crc16_le(0, magic..payload)` (hardware-accelerated on ESP32) |

- **Header**: 7 bytes (Magic + Version + Flags + Stream ID + Length)
- **Trailer**: 2 bytes (CRC-16)
- **Total overhead**: 9 bytes
- **CRC scope**: Magic through end of Payload (excludes the CRC itself)

### Stream ID Ranges

| Range | Usage |
|-------|-------|
| `0x00` | One-shot (no stream, default) |
| `0x01-0x7F` | Client-initiated (client assigns, server echoes) |
| `0x80-0xFE` | Server-initiated push |
| `0xFF` | Reserved |

### Stream Lifecycle

**Sync (Ping)**:
```
Client -> [FIN, SID=0x01] Request
Server -> [FIN, SID=0x01] Response    <- stream complete
```

**Async multi-response (BleScan, future)**:
```
Client -> [FIN, SID=0x02] Scan Request
Server -> [    SID=0x02] ACK           <- Fin=0, more to come
Server -> [    SID=0x02] Scan Result 1
Server -> [    SID=0x02] Scan Result 2
Server -> [FIN, SID=0x02] Scan Done    <- stream complete
```

### Max Wire Sizes

| Direction | Protobuf Max | + Crypto (12B) | + Frame (9B) | Total |
|-----------|-------------|----------------|--------------|-------|
| Request   | 143 B       | 155 B          | **164 B**    | 164 B |
| Response  | 277 B       | 289 B          | **298 B**    | 298 B |

### FrameCodec API

```cpp
namespace Arcana::Command {

class FrameCodec {
public:
    static constexpr uint8_t  kMagic[2] = {0xAC, 0xDA};
    static constexpr uint8_t  kVersion  = 0x01;
    static constexpr size_t   kOverhead = 9;   // 7 header + 2 CRC
    static constexpr uint8_t  kFlagFin  = 0x01;
    static constexpr uint8_t  kSidNone  = 0x00;

    // Wrap payload into frame (flags defaults to FIN, streamId defaults to 0)
    static bool Frame(const uint8_t* payload, size_t payloadLen,
                      uint8_t* out, size_t outBufSize, size_t& outLen,
                      uint8_t flags = kFlagFin, uint8_t streamId = kSidNone);

    // Unwrap frame, verify magic + version + CRC, return payload + flags + streamId
    static bool Deframe(const uint8_t* frame, size_t frameLen,
                        const uint8_t*& payload, size_t& payloadLen,
                        uint8_t& flags, uint8_t& streamId);
};

}
```

---

## Command Protocol

### Overview

The CommandService provides a **unified binary command pipeline** shared by BLE and MQTT. Both channels use identical framed protobuf wire format with optional AES-256-CCM encryption.

```
              BLE Write (0xFF10)                    MQTT (arcana/cmd)
                     |                                     |
                     v                                     v
            +------------------------------------------------+
            |          FrameCodec::Deframe                   |
            |  verify magic + version + CRC-16               |
            +------------------------+-----------------------+
                                     |
                                     v
            +------------------------------------------------+
            |            CommandCodec.DecodeRequest           |
            |  [counter:4][ciphertext:N][tag:8] -> protobuf  |
            |       (session key -> PSK fallback)             |
            +------------------------+-----------------------+
                                     |
                                     v
            +------------------------------------------------+
            |         CommandDispatcher (EventQueue)          |
            |              CommandFactory.Create()            |
            +------------------------+-----------------------+
                                     |
                                     v
            +------------------------------------------------+
            |              ICommand.Execute()                |
            |         -> CommandResponse                      |
            +------------------------+-----------------------+
                                     |
                                     v
            +------------------------------------------------+
            |           CommandCodec.EncodeResponse           |
            |  protobuf -> [counter:4][ciphertext:N][tag:8]  |
            +------------------------+-----------------------+
                                     |
                                     v
            +------------------------------------------------+
            |           FrameCodec::Frame                    |
            |  wrap with magic + version + length + CRC-16   |
            +------------------------+-----------------------+
                                     |
                     +---------------+---------------+
                     |                               |
                     v                               v
           BLE Notify (0xFF11)              MQTT (arcana/rsp)
```

### Cluster + Command Dispatch

Commands follow a **Matter/ZCL-style two-layer dispatch**: a `Cluster` identifies the domain, and a `Command` ID selects the operation within that cluster.

| Cluster | ID | Command | ID | Type | Description |
|---------|----|---------|----|------|-------------|
| **System** | `0x00` | Ping | `0x01` | Sync | Returns timestamp (microseconds since boot) |
| | | GetDeviceInfo | `0x02` | Sync | Chip model, IDF version, free heap, MAC |
| **Sensor** | `0x01` | GetData | `0x01` | Sync | Current temperature + humidity |
| | | SetNotifyInterval | `0x02` | Sync | Change sensor polling interval (ms) |
| **Ble** | `0x02` | GetStatus | `0x01` | Sync | Connected clients, advertising state |
| | | SetDeviceName | `0x02` | Sync | Update BLE device name (persisted to NVS) |
| | | Scan | `0x03` | **Async** | Trigger BLE scan, results via response stream |
| **Mqtt** | `0x03` | GetStatus | `0x01` | Sync | MQTT connection state |
| **Security** | `0x04` | KeyExchange | `0x01` | Sync | ECDH P-256 key exchange (requires encryption) |

### Status Codes

| Code | Name | Description |
|------|------|-------------|
| `0x00` | OK | Success |
| `0x01` | UnknownCommand | Cluster or command not recognized |
| `0x02` | InvalidParam | Payload validation failed |
| `0x03` | Busy | Resource busy (e.g., scan in progress) |
| `0xFF` | Error | Generic error |

### Wire Format (Protobuf)

```protobuf
// arcana_cmd.proto
message CmdRequest {
  uint32 cluster = 1;    // Cluster domain (System=0, Sensor=1, Ble=2, Mqtt=3, Security=4)
  uint32 command = 2;    // Command ID within cluster
  bytes  payload = 3;    // max 128 bytes
}

message CmdResponse {
  uint32 cluster = 1;    // Cluster domain (echo)
  uint32 command = 2;    // Command ID (echo)
  uint32 status  = 3;    // 0 = OK
  bytes  payload = 4;    // max 256 bytes
}
```

### Complete Wire Encoding

```
Plaintext:   Frame( protobuf_bytes )
Encrypted:   Frame( [counter:4 LE][AES-CCM(protobuf_bytes)][tag:8] )
```

---

## Protocol Samples

### Sample 1: Plaintext Ping Request

System::Ping -- cluster=0x00, command=0x01, no payload.

**Protobuf encoding** (4 bytes):
```
08 00        <- field 1 (cluster) varint = 0x00
10 01        <- field 2 (command) varint = 0x01
             <- field 3 (payload) omitted (empty)
```

**Framed wire** (13 bytes):
```
Offset  Hex                          Description
------  ---------------------------  -----------
 0-1    AC DA                        Magic "Arcana Data"
 2      01                           Version 1
 3      01                           Flags (FIN=1)
 4      00                           Stream ID (0 = one-shot)
 5-6    04 00                        Length = 4 (LE)
 7-10   08 00 10 01                  Payload (protobuf)
 11-12  xx xx                        CRC-16 (LE, computed over bytes 0..10)
```

### Sample 2: Encrypted Ping Request

Same Ping request, but with AES-256-CCM encryption enabled.

**Inner protobuf** (4 bytes): `08 00 10 01`

**Encrypted payload** (16 bytes = 4 counter + 4 ciphertext + 8 tag):
```
01 00 00 00        <- TX counter = 1 (LE uint32)
xx xx xx xx        <- AES-256-CCM ciphertext (4 bytes, same length as plaintext)
xx xx xx xx        <- Authentication tag (8 bytes)
xx xx xx xx
```

**Framed wire** (25 bytes):
```
Offset  Hex                                            Description
------  ---------------------------------------------  -----------
 0-1    AC DA                                          Magic
 2      01                                             Version 1
 3      01                                             Flags (FIN=1)
 4      00                                             Stream ID (0 = one-shot)
 5-6    10 00                                          Length = 16 (LE)
 7-10   01 00 00 00                                    Counter (LE)
 11-14  xx xx xx xx                                    Ciphertext
 15-22  xx xx xx xx xx xx xx xx                        Auth tag (8B)
 23-24  xx xx                                          CRC-16 (LE)
```

### Sample 3: Security::KeyExchange Request (Encrypted with PSK)

The KeyExchange request carries a 64-byte P-256 public key, encrypted with PSK.

**Inner protobuf** (~69 bytes):
```
08 04              <- cluster = 0x04 (Security)
10 01              <- command = 0x01 (KeyExchange)
1A 40 ...          <- payload = 64 bytes (client public key: X||Y)
```

**Encrypted payload** (~81 bytes = 4 counter + ~69 ciphertext + 8 tag)

**Framed wire** (~90 bytes):
```
AC DA 01 01 00 51 00  Magic + Version + Flags(FIN) + SID(0) + Length=81 (LE)
[encrypted_payload: 81 bytes]
xx xx                 CRC-16 (LE)
```

### Decode/Encode Flow Summary

```
=== RECEIVE (Decode) ===
Raw bytes from BLE/MQTT/UART
  |
  v
FrameCodec::Deframe()
  - Check magic: 0xAC 0xDA
  - Check version: 0x01
  - Read flags + stream ID
  - Read length (LE uint16)
  - Verify CRC-16 over [magic..payload]
  - Return payload pointer + length + flags + streamId
  |
  v
CommandCodec::DecodeRequest()
  - If encrypted: try session key, then PSK fallback
    - Strip [counter:4], decrypt ciphertext, verify [tag:8]
  - Decode protobuf (cluster, command, payload)
  - Return CommandRequest struct

=== SEND (Encode) ===
CommandResponse struct
  |
  v
CommandCodec::EncodeResponse()
  - Encode protobuf (cluster, command, status, payload)
  - If encrypted: AES-256-CCM -> [counter:4][ciphertext:N][tag:8]
  |
  v
FrameCodec::Frame()
  - Write header: [0xAC 0xDA][0x01][flags][SID][length LE]
  - Copy payload
  - Compute & append CRC-16 (LE)
  |
  v
Send framed bytes via BLE/MQTT/UART
```

---

## Security

### Encryption (AES-256-CCM)

Optional, enabled via `CMD_ENCRYPTION_ENABLED=y` in Kconfig.

| Parameter | Value |
|-----------|-------|
| Algorithm | AES-256-CCM (via mbedtls) |
| Key size | 256-bit (32 bytes) |
| Auth tag | 8 bytes |
| Nonce | 13 bytes (9-byte SHA-256 derived prefix + 4-byte LE counter) |
| Wire overhead | 12 bytes (4B counter + 8B tag) |
| PSK config | `CMD_ENCRYPTION_PSK` (64 hex chars) |

### ECDH P-256 Key Exchange

Provides **Perfect Forward Secrecy** -- per-connection session keys are derived independently from the PSK. If the PSK is compromised, past session traffic remains protected.

```
Client                                   Server (ESP32)
  |                                         |
  |  PSK-encrypted KeyExchange request      |
  |  payload = [client_pub_x:32][y:32]      |
  | --------------------------------------> |
  |                                         |  Generate server keypair (P-256)
  |                                         |  ECDH -> shared_secret (32 bytes)
  |                                         |  session_key = HKDF-SHA256(
  |                                         |    ikm=shared_secret,
  |                                         |    salt=PSK,
  |                                         |    info="ARCANA-SESSION"
  |                                         |  )[0:32]
  |                                         |  auth_tag = HMAC-SHA256(
  |                                         |    PSK, server_pub || client_pub)
  |                                         |
  |  PSK-encrypted KeyExchange response     |
  |  payload = [server_pub:64][auth_tag:32] |
  | <-------------------------------------- |
  |                                         |  <- Install session key
  |                                         |
  |  Session-key encrypted commands         |
  | <=====================================> |
```

### Session Management

| Property | Value |
|----------|-------|
| Max concurrent sessions | 4 (3 BLE + 1 MQTT) |
| Session key derivation | HKDF-SHA256 (manual impl, MBEDTLS_HKDF_C not available) |
| Auth tag | HMAC-SHA256(PSK, server_pub \|\| client_pub) -- 32 bytes |
| Decrypt fallback | Session key first, then PSK (allows pre-KeyExchange commands) |
| KeyExchange response | Always PSK-encrypted (session installed after send) |
| BLE disconnect | Session automatically removed via ConnectionEvents subscription |
| Thread safety | FreeRTOS mutex protects session table across BLE/MQTT tasks |

### Integrity Protection by Mode

| Mode | Encryption Auth | Frame CRC-16 | Protection Level |
|------|----------------|--------------|------------------|
| Plaintext | None | Yes | Corruption detection |
| PSK Encrypted | AES-CCM tag (8B) | Yes | Tampering + corruption |
| Session Encrypted | AES-CCM tag (8B) | Yes | Tampering + corruption + PFS |

---

## BLE Dual-Role

### GATT Server -- Environmental Sensing (0x181A)

| Characteristic | UUID | Properties | Format |
|---------------|------|------------|--------|
| Temperature | 0x2A6E | Read + Notify | `int16_t` (Celsius * 100) |
| Humidity | 0x2A6F | Read + Notify | `uint16_t` (% * 100) |
| Sensor Status | 0xFF01 | Read | `uint8_t` |
| Command | 0xFF10 | Write | Binary (framed protobuf or encrypted) |
| Response | 0xFF11 | Notify | Binary (framed protobuf or encrypted) |

**Features:**
- Attribute table approach (`esp_ble_gatts_create_attr_tab`), 14 attributes total
- Up to 3 simultaneous client connections with per-client CCCD tracking
- Automatic re-advertising after client disconnect
- Observable for connection events and command writes

### GATT Client -- Remote Sensor Discovery

```
Scan -> Connect -> MTU Negotiation -> Service Discovery
    -> Characteristic Discovery -> CCCD Discovery
    -> Register for Notify -> Write CCCD -> Receive Notifications
```

### GAP -- Advertising & Scanning

| Parameter | Value |
|-----------|-------|
| ADV Type | `ADV_TYPE_IND` (connectable undirected) |
| ADV Interval | 20-40 ms |
| Scan Type | Active |
| Scan Interval / Window | 50 ms / 30 ms |
| Device Name | `ARCANA-ESP32` (configurable via Kconfig) |
| Appearance | Generic Sensor (0x0540) |

### BLE + WiFi Coexistence

Both WiFi and BLE run simultaneously via ESP-IDF's software coexistence manager (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`). The MQTT5 client operates over WiFi while BLE handles local sensor communication.

---

## Observable Pattern

### Construction Modes

| Mode | Constructor | Dispatch | Task Created? |
|------|-------------|----------|---------------|
| **Synchronous** | `Observable<T>()` | In caller's thread | No |
| **Asynchronous** | `Observable<T>("name")` | Dedicated FreeRTOS task + queue | Yes |

Asynchronous mode is used for all service-level Observables (decouples producers from consumers). Synchronous mode is used for hardware-level callbacks within components.

### Variants

| Variant | Heap | Callback Type | Max Subscribers | Use Case |
|---------|------|---------------|-----------------|----------|
| `Observable<T>` | Yes | `std::function` | Unlimited | General use |
| `Observable<T, N>` | Yes | `std::function` | N (compile-time) | Bounded resources |
| `StaticObservable<T, N>` | No | Function pointer | N (compile-time) | Memory-constrained |

### Utilities

| Utility | Purpose |
|---------|---------|
| `Subscription<T>` | RAII guard, auto-unsubscribes on destruction |
| `EventQueue<T, N>` | Standalone FreeRTOS queue + task (used by CommandDispatcher) |
| `WeakObserver<T, Owner>` | Wraps `std::weak_ptr`, skips expired observers |
| `Subject<Events...>` | Variadic base for components emitting multiple event types |

### Event Hierarchy (SensorTypes)

```
IModel (interface, runtime type ID without RTTI)
  |-- SensorData      (Value, Temperature, Humidity, Quality, Timestamp)
  |-- SensorError     (ErrorCode, Message)
  |-- ThresholdEvent  (High/Low, Value, Threshold)
  +-- LifecycleEvent  (Started/Stopped/Initialized/Deinitialized)
```

---

## Task Architecture

| Task | Stack | Priority | Owner | Created By |
|------|-------|----------|-------|------------|
| `"TimerSvc FastTimer"` | 3072 | 5 | Observable | TimerServiceImpl constructor |
| `"TimerSvc BaseTimer"` | 2048 | 5 | Observable | TimerServiceImpl constructor |
| `"sensor_0"` | 4096 (Kconfig) | 5 | ObservableSensor | ObservableSensor::Start() |
| `"SensorSvc DataEvents"` | 2048 | 5 | Observable | SensorServiceImpl constructor |
| `"SensorSvc ErrorEvents"` | 2048 | 5 | Observable | SensorServiceImpl constructor |
| `"LedSvc Observable"` | 3072 | 5 | Observable | LedServiceImpl constructor |
| `"MqttSvc CommandEvents"` | 2048 | 5 | Observable | MqttTransportServiceImpl constructor |
| `"MqttSvc ConnStatus"` | 2048 | 5 | Observable | MqttTransportServiceImpl constructor |
| CommandDispatcher | 4096 (Kconfig) | 5 | EventQueue | CommandService::Start() |
| BT Controller | (internal) | High | Bluedroid | BleService::Init() |
| BT Host | (internal) | High | Bluedroid | BleService::Init() |
| WiFi | (internal) | -- | esp_wifi | example_connect() |
| MQTT Client | (internal) | -- | esp_mqtt | MqttTransportService::start() |
| esp_timer | (internal) | 22 | esp_timer | System startup |

---

## Getting Started

### Prerequisites

- [ESP-IDF v5.5.2+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
- ESP32 development board

### Build & Flash

```bash
git clone https://github.com/jrjohn/arcana-embedded-esp32.git
cd arcana-embedded-esp32

# Set up ESP-IDF environment
source ~/.espressif/v5.5.2/esp-idf/export.sh

# Configure credentials (required on first clone)
cp sdkconfig.credentials.example sdkconfig.credentials
# Edit sdkconfig.credentials with your Wi-Fi and MQTT settings

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### Credentials

Wi-Fi and MQTT broker settings are stored in `sdkconfig.credentials` which is **gitignored**.

| File | Purpose | Git |
|------|---------|-----|
| `sdkconfig.defaults` | Base config (BT, partition, etc.) | Committed |
| `sdkconfig.credentials.example` | Template for credentials | Committed |
| `sdkconfig.credentials` | **Your Wi-Fi SSID/password, MQTT broker IP** | **Gitignored** |
| `sdkconfig` | Full generated config (build output) | Gitignored |

`CMakeLists.txt` loads `sdkconfig.defaults` then overlays `sdkconfig.credentials` automatically. Values in credentials override the placeholder defaults in Kconfig.

### Configuration

```bash
idf.py menuconfig
```

| Menu | Option | Default |
|------|--------|---------|
| **Arcana Timer** | Fast Interval | 100 ms |
| | Base Interval | 1000 ms |
| **RGB LED** | GPIO Pin | 26 |
| | Number of LEDs | 3 |
| | Cycle Interval | 1000 ms |
| **Observable Sensor** | DHT Type | DHT11 |
| | GPIO Pin | (Kconfig) |
| | Read Interval | (Kconfig) |
| **BLE Service** | Device Name | `ARCANA-ESP32` |
| | Max Connections | 3 |
| **Command Service** | Encryption (AES-256-CCM) | OFF |
| | PSK (64 hex chars) | `0011...EEFF` |
| **MQTT Service** | Cmd Topic | `arcana/cmd` |
| | Rsp Topic | `arcana/rsp` |
| **MQTT Configuration** | Broker URL | (via `sdkconfig.credentials`) |
| **WiFi** | SSID / Password | (via `sdkconfig.credentials`) |

---

## Project Structure

```
arcana-embedded-esp32/
+-- components/
|   +-- ObservableSensor/            # Foundation: Observable, sensor, shared types
|   |   +-- include/
|   |   |   +-- Observable.hpp           # Observable<T,N>, EventQueue, StaticObservable
|   |   |   +-- ObservableSensor.hpp     # Sensor base with RTOS task
|   |   |   +-- SensorTypes.hpp          # IModel, SensorData, std::variant events
|   |   |   +-- SensorService.hpp        # Abstract service base
|   |   |   +-- SensorServiceImpl.hpp    # Meyer's singleton impl
|   |   |   +-- DhtSensor.hpp            # DHT11/DHT22 GPIO bit-bang driver
|   |   |   +-- TimerTypes.hpp           # TimerTick struct (shared)
|   |   +-- ObservableSensor.cpp
|   |   +-- SensorServiceImpl.cpp
|   |   +-- DhtSensor.cpp
|   |   +-- CMakeLists.txt / Kconfig
|   |
|   +-- BleService/                  # BLE dual-role transport
|   |   +-- include/
|   |   |   +-- BleService.hpp           # BLE facade
|   |   |   +-- BleTransportService.hpp  # Abstract service base
|   |   |   +-- BleTransportServiceImpl.hpp
|   |   |   +-- BleGattServer.hpp        # GATT Server (Env Sensing + Command)
|   |   |   +-- BleGattClient.hpp        # GATT Client (scan + connect)
|   |   |   +-- BleGap.hpp              # GAP advertising + scanning
|   |   |   +-- BleTypes.hpp / BleUuids.hpp
|   |   +-- src/
|   |   |   +-- BleService.cpp / BleTransportServiceImpl.cpp
|   |   |   +-- BleGattServer.cpp / BleGattClient.cpp / BleGap.cpp
|   |   +-- CMakeLists.txt / Kconfig
|   |
|   +-- CommandService/              # Command pipeline + crypto
|   |   +-- include/
|   |   |   +-- CommandService.hpp       # Singleton facade
|   |   |   +-- CommandDispatcher.hpp    # EventQueue async dispatch
|   |   |   +-- CommandFactory.hpp       # Cluster+Command -> ICommand
|   |   |   +-- CommandTypes.hpp         # Enums, Request/Response structs
|   |   |   +-- CommandCodec.hpp         # Protobuf + AES-256-CCM codec
|   |   |   +-- FrameCodec.hpp          # Frame/Deframe: magic + ver + CRC-16
|   |   |   +-- CryptoEngine.hpp        # AES-256-CCM encrypt/decrypt
|   |   |   +-- KeyExchangeManager.hpp  # ECDH P-256, session management
|   |   |   +-- ICommand.hpp            # Command interface
|   |   |   +-- commands/               # 9 ICommand implementations
|   |   +-- src/
|   |   |   +-- CommandService.cpp / CommandFactory.cpp / CommandDispatcher.cpp
|   |   |   +-- CommandCodec.cpp / CryptoEngine.cpp / KeyExchangeManager.cpp
|   |   |   +-- arcana_cmd.pb.c         # nanopb generated
|   |   +-- proto/
|   |   |   +-- arcana_cmd.proto / arcana_cmd.options
|   |   +-- CMakeLists.txt / Kconfig / idf_component.yml
|   |
|   +-- MqttService/                 # MQTT5 transport
|   |   +-- include/
|   |   |   +-- MqttTransportService.hpp
|   |   |   +-- MqttTransportServiceImpl.hpp
|   |   +-- MqttTransportServiceImpl.cpp
|   |   +-- CMakeLists.txt / Kconfig
|   |
|   +-- RgbLed/                      # WS2812B LED strip via RMT
|       +-- include/
|       |   +-- RgbLed.hpp               # RMT driver
|       |   +-- LedService.hpp           # Abstract service base
|       |   +-- LedServiceImpl.hpp       # Meyer's singleton impl
|       +-- RgbLed.cpp / LedServiceImpl.cpp
|       +-- CMakeLists.txt / Kconfig
|
+-- main/
|   +-- app_main.cpp                     # Entry: NVS + netif + event loop + Controller::run()
|   +-- Controller.hpp / Controller.cpp  # Service wiring + lifecycle orchestration
|   +-- TimerService.hpp                 # Abstract base (esp_timer periodic ticks)
|   +-- TimerServiceImpl.hpp / .cpp      # Meyer's singleton impl
|   +-- CommandBridgeService.hpp         # Abstract base (glue service)
|   +-- CommandBridgeServiceImpl.hpp / .cpp
|   +-- CMakeLists.txt / Kconfig.projbuild / idf_component.yml
|
+-- partitions.csv                       # Custom partition table (~4MB app)
+-- sdkconfig.defaults                   # Base config (committed)
+-- sdkconfig.credentials.example        # Credentials template (committed)
+-- sdkconfig.credentials                # Your Wi-Fi/MQTT secrets (gitignored)
+-- sdkconfig                            # Generated full config (gitignored)
+-- CMakeLists.txt                       # Project config (MINIMAL_BUILD)
+-- README.md
```

---

## Adding a New Command

```cpp
// 1. Add command ID to the appropriate cluster namespace in CommandTypes.hpp
namespace SensorCmd {
    static constexpr uint8_t GetData           = 0x01;
    static constexpr uint8_t SetNotifyInterval = 0x02;
    static constexpr uint8_t Calibrate         = 0x03;  // NEW
}

// 2. Create header-only command
class CalibrateCommand : public ICommand {
public:
    CommandResponse Execute(const CommandRequest& req) override {
        CommandResponse rsp;
        rsp.Source = req.Source;
        rsp.ConnectionId = req.ConnectionId;
        rsp.ClusterId = Cluster::Sensor;
        rsp.Command = SensorCmd::Calibrate;
        rsp.Status = kStatusOk;
        // Fill rsp.Payload...
        return rsp;
    }
};

// 3. Add to CommandFactory::Create() switch
case Cluster::Sensor:
    switch (cmd) {
    case SensorCmd::Calibrate:
        return std::make_unique<CalibrateCommand>();
    // ...
    }
```

---

## Verification

1. **Build**: `idf.py build`
2. **LED Cycling**: 3 WS2812B LEDs cycle colors at 100ms intervals via FastTimer (timer-driven)
3. **BLE Sensor**: Use nRF Connect to scan for `ARCANA-ESP32`, subscribe to Temperature/Humidity notifications
4. **BLE Command**: Write framed binary to 0xFF10, receive framed response on 0xFF11
5. **MQTT Command**: Publish framed binary to `arcana/cmd`, subscribe to `arcana/rsp`
6. **Frame Validation**: Send garbage bytes (wrong magic / bad CRC), verify `FrameCodec::Deframe` rejects
7. **Encryption**: Enable `CMD_ENCRYPTION_ENABLED`, verify PSK-encrypted round-trip
8. **Key Exchange**: Send KeyExchange (Security/0x01) with client P-256 public key, verify session-encrypted subsequent commands
9. **Session Cleanup**: Disconnect BLE, verify session removed, next command falls back to PSK
10. **No task leaks**: `grep -r xTaskCreate components/RgbLed/` returns nothing

---

## Roadmap

- [x] Type-safe Observable template (dynamic + static + async)
- [x] RAII Subscription guard + WeakObserver
- [x] IModel polymorphic events + std::variant alternative
- [x] EventQueue async dispatch
- [x] **Service pattern (abstract base + Input/Output + 4-phase lifecycle)**
- [x] **Controller orchestration (wire -> initHAL -> init -> start)**
- [x] **TimerService (esp_timer periodic ticks via Observable)**
- [x] BLE GATT Server (Environmental Sensing 0x181A)
- [x] BLE GATT Client (scan + connect + notify)
- [x] WiFi + BLE coexistence
- [x] ObservableSensor -> BLE bridge
- [x] **Unified Command Pipeline (BLE + MQTT)**
- [x] **nanopb Protobuf wire format**
- [x] **AES-256-CCM encryption**
- [x] **ECDH P-256 key exchange (Perfect Forward Secrecy)**
- [x] **Per-connection session keys (4 slots)**
- [x] **Frame Protocol (magic + version + flags + stream ID + CRC-16)**
- [x] **RGB LED service (timer-driven, no own task)**
- [ ] UART transport (frame layer ready)
- [ ] BLE bonding & SMP pairing
- [ ] OTA firmware update
- [ ] Real hardware sensor driver (I2C/SPI)
- [ ] Runtime statistics dashboard

---

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

- Inspired by [Arcana Embedded STM32](https://github.com/jrjohn/arcana-embedded-stm32) architecture
- FreeRTOS by Amazon Web Services
- ESP-IDF by Espressif Systems
- Bluetooth SIG Environmental Sensing Service specification
- nanopb by Petteri Aimonen
- mbedtls by Arm (via ESP-IDF)

---

<p align="center">
  Made with care for embedded systems developers
</p>
