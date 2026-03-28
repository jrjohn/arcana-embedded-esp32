/*
 * Sensor Data Types
 *
 * Type definitions for sensor events and configuration
 * All event types inherit from IModel for polymorphic handling
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

namespace Arcana {
namespace Sensor {

/*******************************************************************************
 * Model Interface (Base class for all events)
 ******************************************************************************/

/**
 * @brief Model type enumeration for runtime type identification
 */
enum class ModelType {
    Unknown = 0,
    SensorData,
    SensorError,
    ThresholdEvent,
    LifecycleEvent
};

/**
 * @brief Base interface for all event models
 *
 * Provides polymorphic handling of different event types.
 * All sensor events inherit from this interface.
 *
 * Usage:
 * @code
 *   sensor.OnAny([](const IModel& Model) {
 *       if (Model.GetType() == ModelType::SensorData) {
 *           auto& Data = static_cast<const SensorData&>(Model);
 *           // Handle SensorData
 *       }
 *   });
 * @endcode
 */
class IModel {
public:
    virtual ~IModel() = default;

    /**
     * @brief Get runtime type identifier
     */
    virtual ModelType GetType() const = 0;

    /**
     * @brief Get type name as string (for debugging/logging)
     */
    virtual const char* GetTypeName() const = 0;

    /**
     * @brief Get sensor ID associated with this event
     */
    virtual uint8_t GetSensorId() const = 0;

    /**
     * @brief Get timestamp of this event
     */
    virtual uint32_t GetTimestampMs() const = 0;
};

/*******************************************************************************
 * Event Types (All inherit from IModel)
 ******************************************************************************/

/**
 * @brief Sensor reading data
 */
struct SensorData : public IModel {
    int32_t Value;              ///< Primary sensor value
    int32_t RawValue;           ///< Raw ADC/sensor value
    float Temperature;          ///< Temperature reading (if applicable)
    float Humidity;             ///< Humidity reading (if applicable)
    uint32_t TimestampMs;       ///< Reading timestamp (ms since boot)
    uint8_t SensorId;           ///< Sensor identifier
    uint8_t Quality;            ///< Data quality (0-100)

    SensorData()
        : Value(0), RawValue(0), Temperature(0.0f), Humidity(0.0f)
        , TimestampMs(0), SensorId(0), Quality(0) {}

    // IModel interface
    ModelType GetType() const override { return ModelType::SensorData; }
    const char* GetTypeName() const override { return "SensorData"; }
    uint8_t GetSensorId() const override { return SensorId; }
    uint32_t GetTimestampMs() const override { return TimestampMs; }
};

/**
 * @brief Sensor error event
 */
struct SensorError : public IModel {
    int32_t ErrorCode;          ///< Error code
    std::string Message;        ///< Error description
    uint32_t TimestampMs;       ///< Error timestamp
    uint8_t SensorId;           ///< Sensor that caused error

    SensorError()
        : ErrorCode(0), TimestampMs(0), SensorId(0) {}

    SensorError(int32_t Code, const std::string& Msg, uint8_t Id = 0)
        : ErrorCode(Code), Message(Msg), TimestampMs(0), SensorId(Id) {}

    // IModel interface
    ModelType GetType() const override { return ModelType::SensorError; }
    const char* GetTypeName() const override { return "SensorError"; }
    uint8_t GetSensorId() const override { return SensorId; }
    uint32_t GetTimestampMs() const override { return TimestampMs; }
};

/**
 * @brief Threshold crossing event
 */
struct ThresholdEvent : public IModel {
    enum class Type { High, Low };

    Type EventType;             ///< Threshold type
    int32_t Value;              ///< Current value
    int32_t Threshold;          ///< Threshold that was crossed
    uint32_t TimestampMs;       ///< Event timestamp
    uint8_t SensorId;           ///< Sensor identifier

    ThresholdEvent()
        : EventType(Type::High), Value(0), Threshold(0), TimestampMs(0), SensorId(0) {}

    ThresholdEvent(Type T, int32_t Val, int32_t Thresh, uint8_t Id)
        : EventType(T), Value(Val), Threshold(Thresh), TimestampMs(0), SensorId(Id) {}

    // IModel interface
    ModelType GetType() const override { return ModelType::ThresholdEvent; }
    const char* GetTypeName() const override { return "ThresholdEvent"; }
    uint8_t GetSensorId() const override { return SensorId; }
    uint32_t GetTimestampMs() const override { return TimestampMs; }
};

/**
 * @brief Sensor lifecycle event
 */
struct LifecycleEvent : public IModel {
    enum class State { Started, Stopped, Initialized, Deinitialized };

    State CurrentState;
    uint32_t TimestampMs;       ///< Event timestamp
    uint8_t SensorId;

    LifecycleEvent() : CurrentState(State::Initialized), TimestampMs(0), SensorId(0) {}
    LifecycleEvent(State S, uint8_t Id) : CurrentState(S), TimestampMs(0), SensorId(Id) {}

    // IModel interface
    ModelType GetType() const override { return ModelType::LifecycleEvent; }
    const char* GetTypeName() const override { return "LifecycleEvent"; }
    uint8_t GetSensorId() const override { return SensorId; }
    uint32_t GetTimestampMs() const override { return TimestampMs; }

    const char* GetStateName() const {
        switch (CurrentState) {
            case State::Started: return "Started";
            case State::Stopped: return "Stopped";
            case State::Initialized: return "Initialized";
            case State::Deinitialized: return "Deinitialized";
        }
        return "Unknown";
    }
};

/*******************************************************************************
 * Configuration (Not an event, doesn't inherit IModel)
 ******************************************************************************/

/**
 * @brief Sensor configuration
 */
struct SensorConfig {
    uint8_t SensorId;                   ///< Unique sensor identifier
    uint32_t ReadIntervalMs;            ///< Reading interval in ms
    int32_t ThresholdHigh;              ///< High threshold (0 = disabled)
    int32_t ThresholdLow;               ///< Low threshold (0 = disabled)
    bool EnableThresholdEvents;         ///< Enable threshold crossing events
    uint32_t TaskStackSize;             ///< FreeRTOS task stack size
    uint8_t TaskPriority;               ///< FreeRTOS task priority

    SensorConfig()
        : SensorId(0)
        , ReadIntervalMs(1000)
        , ThresholdHigh(0)
        , ThresholdLow(0)
        , EnableThresholdEvents(false)
        , TaskStackSize(4096)
        , TaskPriority(5) {}

    // Builder pattern for fluent configuration
    SensorConfig& WithId(uint8_t Id) { SensorId = Id; return *this; }
    SensorConfig& WithInterval(uint32_t Ms) { ReadIntervalMs = Ms; return *this; }
    SensorConfig& WithThresholds(int32_t Low, int32_t High) {
        ThresholdLow = Low;
        ThresholdHigh = High;
        EnableThresholdEvents = true;
        return *this;
    }
    SensorConfig& WithStackSize(uint32_t Size) { TaskStackSize = Size; return *this; }
    SensorConfig& WithPriority(uint8_t Prio) { TaskPriority = Prio; return *this; }
};

/*******************************************************************************
 * Type-safe casting helpers
 ******************************************************************************/

/**
 * @brief Safe cast from IModel to specific type
 *
 * @tparam T Target type (must inherit from IModel)
 * @param Model Reference to IModel
 * @return Pointer to T if type matches, nullptr otherwise
 */
template<typename T>
const T* ModelCast(const IModel& Model) {
    if constexpr (std::is_same_v<T, SensorData>) {
        return Model.GetType() == ModelType::SensorData ? static_cast<const T*>(&Model) : nullptr;
    } else if constexpr (std::is_same_v<T, SensorError>) {
        return Model.GetType() == ModelType::SensorError ? static_cast<const T*>(&Model) : nullptr;
    } else if constexpr (std::is_same_v<T, ThresholdEvent>) {
        return Model.GetType() == ModelType::ThresholdEvent ? static_cast<const T*>(&Model) : nullptr;
    } else if constexpr (std::is_same_v<T, LifecycleEvent>) {
        return Model.GetType() == ModelType::LifecycleEvent ? static_cast<const T*>(&Model) : nullptr;
    }
    return nullptr;
}

/**
 * @brief Check if model is of specific type
 */
template<typename T>
bool IsModelType(const IModel& Model) {
    return ModelCast<T>(Model) != nullptr;
}

/*******************************************************************************
 * std::variant Alternative (Compile-time type safety, no virtual overhead)
 ******************************************************************************/

/**
 * @brief Lightweight event structs for variant (no IModel inheritance)
 *
 * These are simpler versions without virtual functions,
 * designed for use with std::variant for better performance.
 */
namespace Variant {

struct SensorDataV {
    int32_t Value;
    int32_t RawValue;
    float Temperature;
    float Humidity;
    uint32_t TimestampMs;
    uint8_t SensorId;
    uint8_t Quality;

    SensorDataV()
        : Value(0), RawValue(0), Temperature(0.0f), Humidity(0.0f)
        , TimestampMs(0), SensorId(0), Quality(0) {}

    // Convert from IModel-based SensorData
    explicit SensorDataV(const SensorData& D)
        : Value(D.Value), RawValue(D.RawValue)
        , Temperature(D.Temperature), Humidity(D.Humidity)
        , TimestampMs(D.TimestampMs), SensorId(D.SensorId), Quality(D.Quality) {}
};

struct SensorErrorV {
    int32_t ErrorCode;
    char Message[64];  // Fixed size for variant (no heap allocation)
    uint32_t TimestampMs;
    uint8_t SensorId;

    SensorErrorV() : ErrorCode(0), TimestampMs(0), SensorId(0) {
        Message[0] = '\0';
    }

    SensorErrorV(int32_t Code, const char* Msg, uint8_t Id = 0)
        : ErrorCode(Code), TimestampMs(0), SensorId(Id) {
        strncpy(Message, Msg, sizeof(Message) - 1);
        Message[sizeof(Message) - 1] = '\0';
    }

    // Convert from IModel-based SensorError
    explicit SensorErrorV(const SensorError& E)
        : ErrorCode(E.ErrorCode), TimestampMs(E.TimestampMs), SensorId(E.SensorId) {
        strncpy(Message, E.Message.c_str(), sizeof(Message) - 1);
        Message[sizeof(Message) - 1] = '\0';
    }
};

struct ThresholdEventV {
    enum class Type : uint8_t { High, Low };

    Type EventType;
    int32_t Value;
    int32_t Threshold;
    uint32_t TimestampMs;
    uint8_t SensorId;

    ThresholdEventV()
        : EventType(Type::High), Value(0), Threshold(0), TimestampMs(0), SensorId(0) {}

    ThresholdEventV(Type T, int32_t Val, int32_t Thresh, uint8_t Id)
        : EventType(T), Value(Val), Threshold(Thresh), TimestampMs(0), SensorId(Id) {}

    // Convert from IModel-based ThresholdEvent
    explicit ThresholdEventV(const ThresholdEvent& E)
        : EventType(E.EventType == ThresholdEvent::Type::High ? Type::High : Type::Low)
        , Value(E.Value), Threshold(E.Threshold)
        , TimestampMs(E.TimestampMs), SensorId(E.SensorId) {}
};

struct LifecycleEventV {
    enum class State : uint8_t { Started, Stopped, Initialized, Deinitialized };

    State CurrentState;
    uint32_t TimestampMs;
    uint8_t SensorId;

    LifecycleEventV() : CurrentState(State::Initialized), TimestampMs(0), SensorId(0) {}
    LifecycleEventV(State S, uint8_t Id) : CurrentState(S), TimestampMs(0), SensorId(Id) {}

    // Convert from IModel-based LifecycleEvent
    explicit LifecycleEventV(const LifecycleEvent& E)
        : CurrentState(static_cast<State>(E.CurrentState))
        , TimestampMs(E.TimestampMs), SensorId(E.SensorId) {}

    const char* GetStateName() const {
        switch (CurrentState) {
            case State::Started: return "Started";
            case State::Stopped: return "Stopped";
            case State::Initialized: return "Initialized";
            case State::Deinitialized: return "Deinitialized";
        }
        return "Unknown";
    }
};

} // namespace Variant

/**
 * @brief Type-safe sensor event using std::variant
 *
 * Benefits over IModel inheritance:
 * - No virtual function overhead
 * - Compile-time type safety
 * - Value semantics (copyable, no heap for variant itself)
 * - Pattern matching via std::visit
 *
 * Usage:
 * @code
 *   SensorEvent event = Variant::SensorDataV{...};
 *
 *   // Pattern matching with visitor
 *   std::visit(EventVisitor{
 *       [](const Variant::SensorDataV& d) { printf("Data: %d\n", d.Value); },
 *       [](const Variant::SensorErrorV& e) { printf("Error: %s\n", e.Message); },
 *       [](const Variant::ThresholdEventV& t) { printf("Threshold!\n"); },
 *       [](const Variant::LifecycleEventV& l) { printf("Lifecycle: %s\n", l.GetStateName()); }
 *   }, event);
 *
 *   // Or check specific type
 *   if (auto* data = std::get_if<Variant::SensorDataV>(&event)) {
 *       printf("Got data: %d\n", data->Value);
 *   }
 * @endcode
 */
using SensorEvent = std::variant<
    Variant::SensorDataV,
    Variant::SensorErrorV,
    Variant::ThresholdEventV,
    Variant::LifecycleEventV
>;

/**
 * @brief Helper for creating visitors (C++17 overload pattern)
 *
 * Usage:
 * @code
 *   std::visit(EventVisitor{
 *       [](const Variant::SensorDataV& d) { ... },
 *       [](const Variant::SensorErrorV& e) { ... },
 *       [](auto&) { ... }  // Default case
 *   }, event);
 * @endcode
 */
template<class... Ts>
struct EventVisitor : Ts... {
    using Ts::operator()...;
};
// Deduction guide
template<class... Ts>
EventVisitor(Ts...) -> EventVisitor<Ts...>;

/**
 * @brief Get sensor ID from any event type in variant
 */
inline uint8_t GetSensorId(const SensorEvent& Event) {
    return std::visit([](const auto& E) { return E.SensorId; }, Event);
}

/**
 * @brief Get timestamp from any event type in variant
 */
inline uint32_t GetTimestampMs(const SensorEvent& Event) {
    return std::visit([](const auto& E) { return E.TimestampMs; }, Event);
}

/**
 * @brief Get type name string from variant
 */
inline const char* GetTypeName(const SensorEvent& Event) {
    return std::visit(EventVisitor{
        [](const Variant::SensorDataV&) { return "SensorData"; },
        [](const Variant::SensorErrorV&) { return "SensorError"; },
        [](const Variant::ThresholdEventV&) { return "ThresholdEvent"; },
        [](const Variant::LifecycleEventV&) { return "LifecycleEvent"; }
    }, Event);
}

/**
 * @brief Convert IModel to SensorEvent variant
 */
inline SensorEvent ToVariant(const IModel& Model) {
    switch (Model.GetType()) {
        case ModelType::SensorData:
            return Variant::SensorDataV(static_cast<const SensorData&>(Model));
        case ModelType::SensorError:
            return Variant::SensorErrorV(static_cast<const SensorError&>(Model));
        case ModelType::ThresholdEvent:
            return Variant::ThresholdEventV(static_cast<const ThresholdEvent&>(Model));
        case ModelType::LifecycleEvent:
            return Variant::LifecycleEventV(static_cast<const LifecycleEvent&>(Model));
        default:
            return Variant::LifecycleEventV{};  // Fallback
    }
}

} // namespace Sensor
} // namespace Arcana
