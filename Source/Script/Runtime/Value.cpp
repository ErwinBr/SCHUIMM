#include "Script/Runtime/Value.h"

#include "Script/Bytecode/Semantics.h"

namespace schuimm::script {

namespace {
std::int64_t liveStrings = 0;
} // namespace

StringObject::StringObject(std::string text)
    : text_(std::move(text))
{
    ++liveStrings;
}

StringObject::~StringObject() { --liveStrings; }

StringObject* StringObject::create(std::string text) { return new StringObject(std::move(text)); }

void StringObject::release() noexcept
{
    if (--references_ == 0) {
        delete this;
    }
}

std::int64_t StringObject::liveCount() noexcept { return liveStrings; }

void Value::copyPayload(const Value& other) noexcept
{
    type_ = other.type_;
    switch (type_) {
    case ScriptType::Bool: bool_ = other.bool_; break;
    case ScriptType::Int: int_ = other.int_; break;
    case ScriptType::Float: float_ = other.float_; break;
    case ScriptType::String: string_ = other.string_; break;
    case ScriptType::Object: object_ = other.object_; break;
    default: int_ = 0; break;
    }
}

Value::Value(const Value& other) noexcept
{
    copyPayload(other);
    if (type_ == ScriptType::String) {
        string_->retain();
    }
}

Value::Value(Value&& other) noexcept
{
    copyPayload(other);
    other.type_ = ScriptType::None;
    other.int_ = 0;
}

Value& Value::operator=(const Value& other) noexcept
{
    if (this != &other) {
        if (other.type_ == ScriptType::String) {
            other.string_->retain(); // Before reset(): other may hold the last reference via this.
        }
        reset();
        copyPayload(other);
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept
{
    if (this != &other) {
        reset();
        copyPayload(other);
        other.type_ = ScriptType::None;
        other.int_ = 0;
    }
    return *this;
}

void Value::reset() noexcept
{
    if (type_ == ScriptType::String) {
        string_->release();
    }
    type_ = ScriptType::None;
    int_ = 0;
}

Value Value::fromBool(bool value) noexcept
{
    Value v;
    v.type_ = ScriptType::Bool;
    v.bool_ = value;
    return v;
}

Value Value::fromInt(std::int64_t value) noexcept
{
    Value v;
    v.type_ = ScriptType::Int;
    v.int_ = value;
    return v;
}

Value Value::fromFloat(double value) noexcept
{
    Value v;
    v.type_ = ScriptType::Float;
    v.float_ = value;
    return v;
}

Value Value::fromObject(ObjectRef object) noexcept
{
    Value v;
    v.type_ = ScriptType::Object;
    v.object_ = object.id;
    return v;
}

Value Value::fromString(std::string text) { return adoptString(StringObject::create(std::move(text))); }

Value Value::adoptString(StringObject* string) noexcept
{
    Value v;
    v.type_ = ScriptType::String;
    v.string_ = string;
    return v;
}

std::string toDisplayString(const Value& value)
{
    switch (value.type()) {
    case ScriptType::Bool: return formatBool(value.asBool());
    case ScriptType::Int: return formatInt(value.asInt());
    case ScriptType::Float: return formatFloat(value.asFloat());
    case ScriptType::String: return std::string(value.asString());
    case ScriptType::Object: return "object#" + std::to_string(value.asObject().id);
    default: return "None";
    }
}

} // namespace schuimm::script
