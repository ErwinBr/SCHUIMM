#include "Resource/Audio/WavDecoder.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace schuimm {

namespace {

constexpr std::uint16_t formatPcm = 0x0001;
constexpr std::uint16_t formatFloat = 0x0003;
constexpr std::uint16_t formatExtensible = 0xFFFE;

// WAVE_FORMAT_EXTENSIBLE sub-format GUIDs {0000000X-0000-0010-8000-00AA00389B71}, as stored in
// the file (little-endian fields): X is the format tag, followed by these 14 bytes.
constexpr std::uint8_t subFormatTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                            0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
constexpr std::uint16_t minimumExtensionSize = 22; // cbSize of WAVE_FORMAT_EXTENSIBLE

// Reads little-endian values from a byte range with bounds checking.
class Reader {
public:
    explicit Reader(std::span<const std::byte> data)
        : data_(data)
    {
    }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - offset_; }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    std::optional<std::uint32_t> u32() { return read(4); }
    std::optional<std::uint16_t> u16()
    {
        auto value = read(2);
        return value ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(*value)) : std::nullopt;
    }
    bool tag(const char (&expected)[5])
    {
        if (remaining() < 4) {
            return false;
        }
        const bool match = std::memcmp(data_.data() + offset_, expected, 4) == 0;
        offset_ += 4;
        return match;
    }
    std::string fourcc()
    {
        if (remaining() < 4) {
            return {};
        }
        std::string text(reinterpret_cast<const char*>(data_.data() + offset_), 4);
        offset_ += 4;
        return text;
    }
    void skip(std::size_t bytes) { offset_ += std::min(bytes, remaining()); }
    std::span<const std::byte> bytes(std::size_t count)
    {
        const std::size_t available = std::min(count, remaining());
        const auto result = data_.subspan(offset_, available);
        offset_ += available;
        return result;
    }

private:
    std::optional<std::uint32_t> read(std::size_t bytes)
    {
        if (remaining() < bytes) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data_[offset_ + i])) << (8 * i);
        }
        offset_ += bytes;
        return value;
    }

    std::span<const std::byte> data_;
    std::size_t offset_ = 0;
};

struct Format {
    std::uint16_t encoding = 0; // formatPcm or formatFloat after resolving EXTENSIBLE
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0; // container size
    int blockAlign = 0;
};

std::string hexBytes(std::span<const std::byte> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const std::byte b : bytes) {
        const auto value = std::to_integer<unsigned>(b);
        text += digits[value >> 4];
        text += digits[value & 0xF];
    }
    return text;
}

int bitCount(std::uint32_t value)
{
    int count = 0;
    for (; value != 0; value &= value - 1) {
        ++count;
    }
    return count;
}

float sampleAt(const std::byte* p, const Format& format)
{
    const auto byte = [&](int i) { return std::to_integer<std::uint32_t>(p[i]); };
    if (format.encoding == formatFloat) {
        const std::uint32_t bits = byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    switch (format.bitsPerSample) {
    case 8: return (static_cast<float>(byte(0)) - 128.0f) / 128.0f; // unsigned
    case 16: return static_cast<float>(static_cast<std::int16_t>(byte(0) | (byte(1) << 8))) / 32768.0f;
    case 24: {
        const std::uint32_t raw = byte(0) | (byte(1) << 8) | (byte(2) << 16);
        const auto value = static_cast<std::int32_t>(raw << 8) >> 8; // sign-extend
        return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
        const auto value = static_cast<std::int32_t>(byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    default: return 0.0f;
    }
}

} // namespace

Result<SoundData> decodeWav(std::span<const std::byte> data, std::string_view sourceName)
{
    const auto fail = [&](std::string_view reason) {
        return makeError(ErrorCode::InvalidData, concat("cannot decode WAV '", sourceName, "': ", reason));
    };

    Reader reader(data);
    if (!reader.tag("RIFF")) {
        return fail("not a RIFF file");
    }
    (void)reader.u32(); // RIFF size: not trusted, chunks are bounds-checked individually
    if (!reader.tag("WAVE")) {
        return fail("not a WAVE file");
    }

    std::optional<Format> format;
    std::span<const std::byte> sampleBytes;
    bool haveData = false;
    while (reader.remaining() >= 8 && !(haveData && format)) {
        const std::string id = reader.fourcc();
        const std::uint32_t size = *reader.u32();
        const std::size_t start = reader.offset();
        if (size > reader.remaining()) {
            if (id == "data") {
                return fail("truncated data chunk");
            }
            return fail(concat("truncated '", id, "' chunk"));
        }
        if (id == "fmt ") {
            if (format) {
                return fail("duplicate format chunk");
            }
            if (size < 16) {
                return fail("format chunk too small");
            }
            Format f;
            f.encoding = *reader.u16();
            f.channels = *reader.u16();
            f.sampleRate = static_cast<int>(*reader.u32());
            (void)reader.u32(); // byte rate
            f.blockAlign = *reader.u16();
            f.bitsPerSample = *reader.u16();
            if (f.encoding == formatExtensible) {
                if (size < 40) {
                    return fail("extensible format chunk too small");
                }
                const std::uint16_t extensionSize = *reader.u16();
                const int validBits = *reader.u16();
                const std::uint32_t channelMask = *reader.u32();
                const std::span<const std::byte> guid = reader.bytes(16);
                if (extensionSize < minimumExtensionSize) {
                    return fail(concat("extensible format extension too small (", extensionSize, " bytes)"));
                }
                // The sub-format must be one of the two known GUIDs, compared in full.
                const auto tag = static_cast<std::uint16_t>(std::to_integer<unsigned>(guid[0]) |
                                                            (std::to_integer<unsigned>(guid[1]) << 8));
                const bool knownGuid = (tag == formatPcm || tag == formatFloat) &&
                                       std::memcmp(guid.data() + 2, subFormatTail, sizeof(subFormatTail)) == 0;
                if (!knownGuid) {
                    return fail(concat("unsupported sub-format {", hexBytes(guid), "} (only PCM and IEEE float)"));
                }
                f.encoding = tag;
                // Valid bits are stored left-aligned in the container (the padding bits are
                // the low ones), so decoding at the container size gives the right values.
                const bool validBitsOk = tag == formatFloat ? validBits == 32
                                                            : (validBits >= 1 && validBits <= f.bitsPerSample);
                if (!validBitsOk) {
                    return fail(concat("inconsistent valid bits per sample (", validBits, " in a ", f.bitsPerSample,
                                       "-bit container)"));
                }
                // Fewer assigned speakers than channels is allowed; more is not.
                if (bitCount(channelMask) > f.channels) {
                    return fail(concat("channel mask 0x", std::hex, channelMask, std::dec, " assigns more speakers than the ",
                                       f.channels, " channels"));
                }
            }
            format = f;
        } else if (id == "data") {
            sampleBytes = data.subspan(start, size);
            haveData = true;
        }
        // Chunks are padded to an even size.
        reader.skip(start + size + (size & 1u) - reader.offset());
    }

    if (!format) {
        return fail("missing format chunk");
    }
    if (!haveData) {
        return fail("missing data chunk");
    }
    const Format& f = *format;
    if (f.encoding != formatPcm && f.encoding != formatFloat) {
        return fail(concat("unsupported encoding 0x", std::hex, f.encoding, std::dec, " (only PCM and IEEE float)"));
    }
    if (f.channels < 1 || f.channels > SoundData::maxChannels) {
        return fail(concat("unsupported channel count ", f.channels, " (mono or stereo only)"));
    }
    const bool validBits = f.encoding == formatFloat ? f.bitsPerSample == 32
                                                     : (f.bitsPerSample == 8 || f.bitsPerSample == 16 ||
                                                        f.bitsPerSample == 24 || f.bitsPerSample == 32);
    if (!validBits) {
        return fail(concat("unsupported sample size of ", f.bitsPerSample, " bits"));
    }
    const int bytesPerSample = f.bitsPerSample / 8;
    if (f.blockAlign != bytesPerSample * f.channels) {
        return fail(concat("inconsistent block alignment ", f.blockAlign));
    }
    if (f.sampleRate < SoundData::minSampleRate || f.sampleRate > SoundData::maxSampleRate) {
        return fail(concat("unsupported sample rate ", f.sampleRate, " Hz"));
    }

    const auto blockAlign = static_cast<std::size_t>(f.blockAlign);
    if (sampleBytes.size() % blockAlign != 0) {
        return fail(concat("data chunk is not a whole number of frames (", sampleBytes.size(), " bytes, ", blockAlign,
                           " per frame)"));
    }
    const std::size_t frames = sampleBytes.size() / blockAlign;
    if (frames == 0) {
        return fail("no audio frames");
    }
    std::vector<float> samples(frames * static_cast<std::size_t>(f.channels));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = sampleAt(sampleBytes.data() + i * static_cast<std::size_t>(bytesPerSample), f);
    }
    auto sound = SoundData::create(f.channels, f.sampleRate, std::move(samples));
    if (!sound) {
        return fail(sound.error().message);
    }
    return sound;
}

Result<SoundData> loadSound(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decodeWav(*bytes, asset.view());
}

} // namespace schuimm
