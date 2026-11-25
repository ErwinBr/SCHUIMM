#include "Resource/Json/JsonDecoder.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace schuimm::json {

namespace {

// Records the first duplicate key of any JSON object while nlohmann parses (its objects are maps
// and would silently keep the last value), with the property path where it occurs.
class DuplicateKeyFinder {
public:
    bool onEvent(Json::parse_event_t event, const Json& parsed)
    {
        switch (event) {
        case Json::parse_event_t::object_start: stack_.push_back(Frame{false, 0, {}, {}}); break;
        case Json::parse_event_t::array_start: stack_.push_back(Frame{true, 0, {}, {}}); break;
        case Json::parse_event_t::key: {
            Frame& frame = stack_.back();
            frame.key = parsed.get<std::string>();
            if (!frame.keys.insert(frame.key).second && !duplicate_) {
                duplicate_ = std::make_pair(path(), frame.key);
            }
            break;
        }
        case Json::parse_event_t::object_end:
        case Json::parse_event_t::array_end:
            stack_.pop_back();
            elementDone();
            break;
        case Json::parse_event_t::value: elementDone(); break;
        }
        return true;
    }

    // {path of the duplicate member, key}.
    [[nodiscard]] const std::optional<std::pair<std::string, std::string>>& duplicate() const noexcept { return duplicate_; }

private:
    struct Frame {
        bool array = false;
        std::size_t index = 0;
        std::string key;
        std::set<std::string> keys;
    };

    void elementDone()
    {
        if (!stack_.empty() && stack_.back().array) ++stack_.back().index;
    }

    [[nodiscard]] std::string path() const
    {
        std::string text;
        for (const Frame& frame : stack_) {
            text = frame.array ? element(text, frame.index) : member(text, frame.key);
        }
        return text;
    }

    std::vector<Frame> stack_;
    std::optional<std::pair<std::string, std::string>> duplicate_;
};

// "parse error at line 4, column 17: syntax error ..." -> "syntax error ...".
std::string parseErrorReason(const char* what)
{
    const std::string_view text(what);
    const std::size_t column = text.find("column ");
    if (column != std::string_view::npos) {
        const std::size_t colon = text.find(": ", column);
        if (colon != std::string_view::npos) return std::string(text.substr(colon + 2));
    }
    return std::string(text);
}

} // namespace

std::string member(std::string_view parent, std::string_view key)
{
    return parent.empty() ? std::string(key) : concat(parent, ".", key);
}

std::string element(std::string_view parent, std::size_t index)
{
    return concat(parent, "[", index, "]");
}

std::string describeValue(const Json& value)
{
    switch (value.type()) {
    case Json::value_t::object: return "an object";
    case Json::value_t::array: return "an array";
    case Json::value_t::string: return "a string";
    case Json::value_t::boolean: return "a boolean";
    case Json::value_t::null: return "null";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float: return concat("the number ", value.dump());
    default: return "an unsupported value";
    }
}

bool isValidIdentifier(std::string_view text) noexcept
{
    if (text.empty()) return false;
    const auto isLetter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!isLetter(text[0])) return false;
    for (char c : text) {
        if (!isLetter(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

std::unexpected<Error> Decoder::fail(std::string_view path, std::string_view reason) const
{
    return makeError(ErrorCode::InvalidData,
                     path.empty() ? concat(source_, ": ", reason) : concat(source_, ": ", path, ": ", reason));
}

Result<Json> Decoder::parse(std::span<const std::byte> data) const
{
    const char* const begin = reinterpret_cast<const char*>(data.data());
    const char* const end = begin + data.size();
    DuplicateKeyFinder duplicates;
    Json root;
    try {
        root = Json::parse(begin, end, [&duplicates](int, Json::parse_event_t event, Json& parsed) {
            return duplicates.onEvent(event, parsed);
        });
    } catch (const Json::parse_error& error) {
        // error.byte: 1-based offset of the character where parsing failed.
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t stop = error.byte == 0 ? 0 : std::min<std::size_t>(error.byte - 1, data.size());
        for (std::size_t i = 0; i < stop; ++i) {
            if (begin[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return fail("", concat("invalid JSON at line ", line, ", column ", column, ": ", parseErrorReason(error.what())));
    } catch (const Json::exception& error) {
        return fail("", concat("invalid JSON: ", error.what()));
    }
    if (const auto& duplicate = duplicates.duplicate()) {
        return fail(duplicate->first, concat("duplicate property '", duplicate->second, "'"));
    }
    return root;
}

Status Decoder::readVersion(const Json& root, std::string_view kind, int minimum, int maximum, int& out) const
{
    if (!root.is_object()) {
        const bool vowel = !kind.empty() && std::string_view("aeiou").find(kind.front()) != std::string_view::npos;
        return fail("", concat(vowel ? "an " : "a ", kind, " must be a JSON object, found ", describeValue(root)));
    }
    const Json* version = nullptr;
    SCHUIMM_JSON_TRY(required(root, "", "version", version));
    if (!version->is_number_integer()) {
        return fail("version", concat("expected an integer, found ", describeValue(*version)));
    }
    const bool supported = version->is_number_unsigned()
                               ? version->get<std::uint64_t>() >= static_cast<std::uint64_t>(minimum) &&
                                     version->get<std::uint64_t>() <= static_cast<std::uint64_t>(maximum)
                               : version->get<std::int64_t>() >= minimum && version->get<std::int64_t>() <= maximum;
    if (!supported) {
        const std::string reads = minimum == maximum ? concat("version ", minimum)
                                                     : concat("versions ", minimum, " to ", maximum);
        return fail("version", concat("unsupported ", kind, " version ", version->dump(), " (this engine reads ",
                                      reads, ")"));
    }
    out = static_cast<int>(version->get<std::int64_t>());
    return {};
}

Status Decoder::readJsonObject(const Json& value, std::string_view path) const
{
    if (!value.is_object()) return fail(path, concat("expected an object, found ", describeValue(value)));
    return {};
}

Status Decoder::readArray(const Json& value, std::string_view path) const
{
    if (!value.is_array()) return fail(path, concat("expected an array, found ", describeValue(value)));
    return {};
}

Status Decoder::required(const Json& object, std::string_view path, const char* key, const Json*& out) const
{
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return fail(member(path, key), "missing required property");
    out = &*found;
    return {};
}

const Json* Decoder::optional(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found == object.end() || found->is_null() ? nullptr : &*found;
}

Status Decoder::onlyKnown(const Json& object, std::string_view path, std::initializer_list<std::string_view> known) const
{
    for (const auto& item : object.items()) {
        bool isKnown = false;
        for (std::string_view name : known) isKnown = isKnown || item.key() == name;
        if (!isKnown) return fail(member(path, item.key()), "unknown property");
    }
    return {};
}

Status Decoder::readString(const Json& value, std::string_view path, std::string& out) const
{
    if (!value.is_string()) return fail(path, concat("expected a string, found ", describeValue(value)));
    out = value.get<std::string>();
    return {};
}

Status Decoder::readBool(const Json& value, std::string_view path, bool& out) const
{
    if (!value.is_boolean()) return fail(path, concat("expected true or false, found ", describeValue(value)));
    out = value.get<bool>();
    return {};
}

Status Decoder::readIdentifier(const Json& value, std::string_view path, std::string& out) const
{
    SCHUIMM_JSON_TRY(readString(value, path, out));
    if (!isValidIdentifier(out)) {
        return fail(path, concat("invalid identifier '", out, "' (letters, digits and '_' only, not starting with a digit)"));
    }
    return {};
}

Status Decoder::readUniqueIdentifier(const Json& object, std::string_view path, IdMap& ids, std::string& out) const
{
    const Json* value = nullptr;
    SCHUIMM_JSON_TRY(required(object, path, "id", value));
    const std::string idPath = member(path, "id");
    SCHUIMM_JSON_TRY(readIdentifier(*value, idPath, out));
    const auto [existing, inserted] = ids.try_emplace(out, std::string(path));
    if (!inserted) {
        return fail(idPath, concat("duplicate identifier '", out, "' (first used by ", existing->second, ")"));
    }
    return {};
}

Status Decoder::readInteger(const Json& value, std::string_view path, int minimum, int maximum, int& out) const
{
    const auto outOfRange = [&] {
        return fail(path, concat("must be an integer from ", minimum, " to ", maximum, ", found ", value.dump()));
    };
    if (!value.is_number_integer()) return fail(path, concat("expected an integer, found ", describeValue(value)));
    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(maximum)) return outOfRange();
        out = static_cast<int>(number);
    } else {
        const std::int64_t number = value.get<std::int64_t>();
        if (number < minimum || number > maximum) return outOfRange();
        out = static_cast<int>(number);
    }
    if (out < minimum) return outOfRange();
    return {};
}

Status Decoder::readNumber(const Json& value, std::string_view path, double& out) const
{
    if (!value.is_number()) return fail(path, concat("expected a number, found ", describeValue(value)));
    out = value.get<double>();
    if (!std::isfinite(out)) return fail(path, "must be a finite number");
    return {};
}

Status Decoder::readPositiveFloat(const Json& value, std::string_view path, double maximum, float& out) const
{
    double number = 0.0;
    SCHUIMM_JSON_TRY(readNumber(value, path, number));
    if (!(number > 0.0) || number > maximum) {
        return fail(path, concat("must be a positive number of at most ", maximum, ", found ", value.dump()));
    }
    // A tiny value such as 1e-50 becomes 0 as a float.
    const float stored = static_cast<float>(number);
    if (!(stored > 0.0f)) return fail(path, concat("must remain positive when stored as a float, found ", value.dump()));
    out = stored;
    return {};
}

Status Decoder::readCoordinate(const Json& value, std::string_view path, float& out) const
{
    double number = 0.0;
    SCHUIMM_JSON_TRY(readNumber(value, path, number));
    if (std::fabs(number) > maxCoordinate) {
        return fail(path, concat("coordinate ", value.dump(), " is out of range (at most ", maxCoordinate,
                                 " pixels from the origin)"));
    }
    out = static_cast<float>(number);
    return {};
}

Status Decoder::readPoint(const Json& value, std::string_view path, Vec2& out) const
{
    if (!value.is_array() || value.size() != 2) {
        return fail(path, concat("expected a point [x, y], found ", describeValue(value)));
    }
    SCHUIMM_JSON_TRY(readCoordinate(value[0], element(path, 0), out.x));
    SCHUIMM_JSON_TRY(readCoordinate(value[1], element(path, 1), out.y));
    return {};
}

Status Decoder::readAssetPath(const Json& value, std::string_view path, std::optional<AssetPath>& out) const
{
    std::string text;
    SCHUIMM_JSON_TRY(readString(value, path, text));
    auto parsed = AssetPath::parse(text);
    if (!parsed) return fail(path, parsed.error().message);
    out = std::move(*parsed);
    return {};
}

Status Decoder::readAssetPath(const Json& value, std::string_view path, AssetPath& out) const
{
    std::optional<AssetPath> parsed;
    SCHUIMM_JSON_TRY(readAssetPath(value, path, parsed));
    out = std::move(*parsed);
    return {};
}

} // namespace schuimm::json
