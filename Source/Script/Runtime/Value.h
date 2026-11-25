#pragma once

#include "Script/Bytecode/ScriptType.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace schuimm::script {

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

// Opaque handle of an engine object (e.g. an object of the current room), the value of "self" in
// object handlers and of the script type "object". The engine chooses the ids and owns the
// objects and their state; scripts only compare handles and pass them to natives (bytecode has no
// object constants, so scripts cannot make handles up). The engine's contract:
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

// A register value: a 16-byte tagged union. Copying a string Value adds a reference.
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
    [[nodiscard]] static Value fromObject(ObjectRef object) noexcept;
    // Takes over one reference of `string`.
    [[nodiscard]] static Value adoptString(StringObject* string) noexcept;

    [[nodiscard]] ScriptType type() const noexcept { return type_; }
    [[nodiscard]] bool is(ScriptType type) const noexcept { return type_ == type; }
    [[nodiscard]] bool asBool() const noexcept { return bool_; }
    [[nodiscard]] std::int64_t asInt() const noexcept { return int_; }
    [[nodiscard]] double asFloat() const noexcept { return float_; }
    [[nodiscard]] std::string_view asString() const noexcept { return string_->view(); }
    [[nodiscard]] ObjectRef asObject() const noexcept { return {object_}; }
    [[nodiscard]] const StringObject* stringObject() const noexcept { return type_ == ScriptType::String ? string_ : nullptr; }

    // Whether the value fits a declared parameter or return type (Any: anything but None).
    [[nodiscard]] bool matches(ScriptType declared) const noexcept
    {
        return declared == ScriptType::Any ? type_ != ScriptType::None : type_ == declared;
    }

    void reset() noexcept;

private:
    void copyPayload(const Value& other) noexcept;

    ScriptType type_ = ScriptType::None;
    union {
        bool bool_;
        std::int64_t int_ = 0;
        double float_;
        StringObject* string_;
        std::uint64_t object_;
    };
};

// The text print() produces: 42, 1.5, True, the string itself, object#7, None.
[[nodiscard]] std::string toDisplayString(const Value& value);

} // namespace schuimm::script
