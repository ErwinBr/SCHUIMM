#pragma once

#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace schuimm::script {

struct Module;

// Immutable runtime string with an intrusive, non-atomic reference count. Strings are created
// by the VM (concatenation, str()) and by Program for constants; a string is freed when its last
// Value is destroyed. Strings reference nothing, so cycles (and a garbage collector) are
// impossible. All script execution happens on one thread (the game thread); string Values must
// not be shared across threads.
class StringObject {
public:
    [[nodiscard]] static StringObject* create(std::string text);

    [[nodiscard]] std::string_view view() const noexcept { return text_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::uint32_t referenceCount() const noexcept { return references_; }

    void retain() noexcept { ++references_; }
    void release() noexcept;

    // Live string objects in the process (single-threaded use): for leak checks in tests.
    [[nodiscard]] static std::int64_t liveCount() noexcept;

private:
    explicit StringObject(std::string text);
    ~StringObject();

    std::string text_;
    std::uint32_t references_ = 1;
};

// Opaque handle of an engine entity (an actor or an object of the current room): the value of the
// script types "actor" and "object" (and "entity", which holds either), and of "self" in object
// handlers. The engine chooses the ids and owns the entities and their state; scripts only compare
// handles and pass them to natives (bytecode has no reference constants, so scripts cannot make
// handles up). The engine's contract:
//   * an id is never reused during the process lifetime: a handle stays bound to the object it
//     was issued for, and becomes stale (never refers to another object) when it is destroyed;
//     a monotonic counter, or {index, generation} packed into the 64 bits, both satisfy this;
//   * id 0 is the null handle and never refers to an object;
//   * natives given a stale or unknown handle fail with an error (a runtime error of the script).
// Under this contract equality of ids is equality of object identity.
struct ObjectRef {
    std::uint64_t id = 0;
    [[nodiscard]] constexpr bool isNull() const noexcept { return id == 0; }
    friend bool operator==(const ObjectRef&, const ObjectRef&) = default;
};

// A register value: a 24-byte tagged union. Copying a string Value adds a reference.
//
//   vec2         two doubles (the script float), a value: copies are independent;
//   enum         the enum's index in the module's enum table, and the ordinal of the member;
//   actor/object an ObjectRef (id 0 is the null reference of that type).
class Value {
public:
    Value() noexcept = default;
    ~Value() { reset(); }
    Value(const Value& other) noexcept;
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other) noexcept;
    Value& operator=(Value&& other) noexcept;

    [[nodiscard]] static Value none() noexcept { return {}; }
    [[nodiscard]] static Value fromBool(bool value) noexcept;
    [[nodiscard]] static Value fromInt(std::int64_t value) noexcept;
    [[nodiscard]] static Value fromFloat(double value) noexcept;
    [[nodiscard]] static Value fromString(std::string text);
    [[nodiscard]] static Value fromObject(ObjectRef object) noexcept; // A room-object reference.
    [[nodiscard]] static Value fromActor(ObjectRef actor) noexcept;
    [[nodiscard]] static Value fromVec2(double x, double y) noexcept;
    [[nodiscard]] static Value fromEnum(std::uint16_t enumIndex, std::uint32_t ordinal) noexcept;
    // Takes over one reference of `string`.
    [[nodiscard]] static Value adoptString(StringObject* string) noexcept;

    [[nodiscard]] ScriptType type() const noexcept { return type_; }
    [[nodiscard]] bool is(ScriptType type) const noexcept { return type_ == type; }
    [[nodiscard]] bool asBool() const noexcept { return bool_; }
    [[nodiscard]] std::int64_t asInt() const noexcept { return int_; }
    [[nodiscard]] double asFloat() const noexcept { return float_; }
    [[nodiscard]] std::string_view asString() const noexcept { return string_->view(); }
    // The reference of an actor or object value.
    [[nodiscard]] ObjectRef asObject() const noexcept { return {object_}; }
    [[nodiscard]] ObjectRef asReference() const noexcept { return {object_}; }
    [[nodiscard]] double vec2X() const noexcept { return vec2_.x; }
    [[nodiscard]] double vec2Y() const noexcept { return vec2_.y; }
    [[nodiscard]] std::uint16_t enumIndex() const noexcept { return enum_.index; }
    [[nodiscard]] std::uint32_t enumOrdinal() const noexcept { return enum_.ordinal; }
    void setVec2X(double x) noexcept { vec2_.x = x; }
    void setVec2Y(double y) noexcept { vec2_.y = y; }
    [[nodiscard]] const StringObject* stringObject() const noexcept { return type_ == ScriptType::String ? string_ : nullptr; }

    // Whether the value fits a declared parameter, register or return type: Any is anything but
    // None, entity is an actor or object reference, and an enum value must belong to the enum.
    [[nodiscard]] bool matches(TypeRef declared) const noexcept
    {
        switch (declared.kind) {
        case ScriptType::Any: return type_ != ScriptType::None;
        case ScriptType::Entity: return type_ == ScriptType::Actor || type_ == ScriptType::Object;
        case ScriptType::Enum: return type_ == ScriptType::Enum && enum_.index == declared.enumIndex;
        default: return type_ == declared.kind;
        }
    }

    void reset() noexcept;

private:
    void copyPayload(const Value& other) noexcept;

    struct Vec2Payload {
        double x;
        double y;
    };
    struct EnumPayload {
        std::uint16_t index;
        std::uint32_t ordinal;
    };

    ScriptType type_ = ScriptType::None;
    union {
        Vec2Payload vec2_ = {0.0, 0.0};
        bool bool_;
        std::int64_t int_;
        double float_;
        StringObject* string_;
        std::uint64_t object_;
        EnumPayload enum_;
    };
};

// The text print() produces: 42, 1.5, True, the string itself, vec2(1, 2.5), actor#7, object#7,
// None. Without a module, enum values read "enum#<index>.<ordinal>"; with the module that owns
// the value they read "Direction.NORTH".
[[nodiscard]] std::string toDisplayString(const Value& value);
[[nodiscard]] std::string toDisplayString(const Value& value, const Module& module);

} // namespace schuimm::script
