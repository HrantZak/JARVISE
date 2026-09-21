#include "jarvis/llm/GgufInspector.h"

#include <array>
#include <cstring>
#include <fstream>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace fs = std::filesystem;

namespace jarvis::llm {
namespace {

using namespace jarvis::core;

constexpr std::array<char, 4> kMagic{'G', 'G', 'U', 'F'};
constexpr std::uint32_t kMinVersion = 2;
constexpr std::uint32_t kMaxVersion = 3;

/// A ceiling on the metadata block. A malformed or hostile file must not be
/// able to make the inspector allocate without bound.
constexpr std::uint64_t kMaxKvCount = 4096;
constexpr std::uint64_t kMaxStringLength = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxArrayElements = 4ULL * 1024ULL * 1024ULL;

enum class ValueType : std::uint32_t {
    UInt8 = 0, Int8, UInt16, Int16, UInt32, Int32, Float32,
    Bool, String, Array, UInt64, Int64, Float64,
};

/// Only the value kinds the inspector actually reads are stored; everything
/// else is skipped over.
using Scalar = std::variant<std::monostate, std::uint64_t, std::int64_t,
                            double, bool, std::string>;

/// Bounded little-endian reader over an input stream.
class Reader {
public:
    explicit Reader(std::istream& stream) : m_stream{stream} {}

    [[nodiscard]] bool ok() const { return m_ok; }

    template <typename T>
    [[nodiscard]] bool read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!m_ok) {
            return false;
        }
        m_stream.read(reinterpret_cast<char*>(&out), sizeof(T));
        m_ok = m_stream.good();
        return m_ok;
    }

    [[nodiscard]] bool skip(std::uint64_t bytes) {
        if (!m_ok) {
            return false;
        }
        m_stream.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
        m_ok = m_stream.good();
        return m_ok;
    }

    [[nodiscard]] bool readString(std::string& out) {
        std::uint64_t length = 0;
        if (!read(length)) {
            return false;
        }
        if (length > kMaxStringLength) {
            m_ok = false;
            return false;
        }
        out.resize(static_cast<std::size_t>(length));
        if (length > 0) {
            m_stream.read(out.data(), static_cast<std::streamsize>(length));
            m_ok = m_stream.good();
        }
        return m_ok;
    }

private:
    std::istream& m_stream;
    bool m_ok{true};
};

[[nodiscard]] std::optional<std::size_t> fixedWidth(ValueType type) {
    switch (type) {
    case ValueType::UInt8:
    case ValueType::Int8:
    case ValueType::Bool:    return 1;
    case ValueType::UInt16:
    case ValueType::Int16:   return 2;
    case ValueType::UInt32:
    case ValueType::Int32:
    case ValueType::Float32: return 4;
    case ValueType::UInt64:
    case ValueType::Int64:
    case ValueType::Float64: return 8;
    default:                 return std::nullopt;
    }
}

/// Reads one value, keeping scalars and strings and skipping arrays.
[[nodiscard]] bool readValue(Reader& reader, ValueType type, Scalar& out) {
    switch (type) {
    case ValueType::UInt8:  { std::uint8_t  v{}; if (!reader.read(v)) return false; out = std::uint64_t{v}; return true; }
    case ValueType::UInt16: { std::uint16_t v{}; if (!reader.read(v)) return false; out = std::uint64_t{v}; return true; }
    case ValueType::UInt32: { std::uint32_t v{}; if (!reader.read(v)) return false; out = std::uint64_t{v}; return true; }
    case ValueType::UInt64: { std::uint64_t v{}; if (!reader.read(v)) return false; out = v;                return true; }
    case ValueType::Int8:   { std::int8_t   v{}; if (!reader.read(v)) return false; out = std::int64_t{v};  return true; }
    case ValueType::Int16:  { std::int16_t  v{}; if (!reader.read(v)) return false; out = std::int64_t{v};  return true; }
    case ValueType::Int32:  { std::int32_t  v{}; if (!reader.read(v)) return false; out = std::int64_t{v};  return true; }
    case ValueType::Int64:  { std::int64_t  v{}; if (!reader.read(v)) return false; out = v;                return true; }
    case ValueType::Float32:{ float         v{}; if (!reader.read(v)) return false; out = double{v};        return true; }
    case ValueType::Float64:{ double        v{}; if (!reader.read(v)) return false; out = v;                return true; }
    case ValueType::Bool:   { std::uint8_t  v{}; if (!reader.read(v)) return false; out = (v != 0);         return true; }

    case ValueType::String: {
        std::string value;
        if (!reader.readString(value)) {
            return false;
        }
        out = std::move(value);
        return true;
    }

    case ValueType::Array: {
        std::uint32_t elementTypeRaw = 0;
        std::uint64_t count = 0;
        if (!reader.read(elementTypeRaw) || !reader.read(count)) {
            return false;
        }
        if (count > kMaxArrayElements) {
            return false;
        }

        const auto elementType = static_cast<ValueType>(elementTypeRaw);
        if (const std::optional<std::size_t> width = fixedWidth(elementType)) {
            // Token vocabularies run to hundreds of thousands of entries; the
            // inspector never needs them, so the block is skipped wholesale.
            return reader.skip(count * *width);
        }
        if (elementType == ValueType::String) {
            for (std::uint64_t i = 0; i < count; ++i) {
                std::string ignored;
                if (!reader.readString(ignored)) {
                    return false;
                }
            }
            return true;
        }
        return false;  // nested arrays are not part of the format we accept
    }
    }
    return false;
}

[[nodiscard]] std::string asString(const Scalar& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    return {};
}

[[nodiscard]] std::uint32_t asUInt32(const Scalar& value) {
    if (const auto* unsignedValue = std::get_if<std::uint64_t>(&value)) {
        return static_cast<std::uint32_t>(*unsignedValue);
    }
    if (const auto* signedValue = std::get_if<std::int64_t>(&value)) {
        return *signedValue > 0 ? static_cast<std::uint32_t>(*signedValue) : 0;
    }
    return 0;
}

/// llama.cpp's file-type enum, as written to general.file_type. Only the
/// quantisations that actually turn up are named; anything else is reported by
/// number rather than guessed at.
[[nodiscard]] std::string quantizationName(std::uint32_t fileType) {
    switch (fileType) {
    case 0:  return "F32";
    case 1:  return "F16";
    case 2:  return "Q4_0";
    case 3:  return "Q4_1";
    case 7:  return "Q8_0";
    case 8:  return "Q5_0";
    case 9:  return "Q5_1";
    case 10: return "Q2_K";
    case 11: return "Q3_K_S";
    case 12: return "Q3_K_M";
    case 13: return "Q3_K_L";
    case 14: return "Q4_K_S";
    case 15: return "Q4_K_M";
    case 16: return "Q5_K_S";
    case 17: return "Q5_K_M";
    case 18: return "Q6_K";
    case 30: return "BF16";
    default: return std::format("type {}", fileType);
    }
}

} // namespace

bool GgufInspector::hasGgufMagic(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return false;
    }

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }

    std::array<char, 4> magic{};
    file.read(magic.data(), magic.size());
    return file.good() && magic == kMagic;
}

core::Result<ModelInfo> GgufInspector::inspect(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return fail(ErrorCode::NotFound,
                    std::format("'{}' is not a file", path.string()));
    }

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return fail(ErrorCode::IoFailure,
                    std::format("cannot open '{}'", path.string()));
    }

    Reader reader{file};

    std::array<char, 4> magic{};
    file.read(magic.data(), magic.size());
    if (!file.good() || magic != kMagic) {
        return fail(ErrorCode::ParseFailure,
                    std::format("'{}' is not a GGUF file", path.filename().string()));
    }

    ModelInfo info;
    info.path = path;
    info.fileSizeBytes = static_cast<std::uint64_t>(fs::file_size(path, ec));

    std::uint64_t kvCount = 0;
    if (!reader.read(info.ggufVersion) || !reader.read(info.tensorCount) ||
        !reader.read(kvCount)) {
        return fail(ErrorCode::ParseFailure,
                    std::format("'{}' has a truncated GGUF header",
                                path.filename().string()));
    }

    if (info.ggufVersion < kMinVersion || info.ggufVersion > kMaxVersion) {
        return fail(ErrorCode::ParseFailure,
                    std::format("GGUF version {} is not supported (need {}-{})",
                                info.ggufVersion, kMinVersion, kMaxVersion));
    }
    if (kvCount > kMaxKvCount) {
        return fail(ErrorCode::ParseFailure,
                    std::format("GGUF metadata block is implausibly large ({} entries)",
                                kvCount));
    }

    std::unordered_map<std::string, Scalar> metadata;
    metadata.reserve(static_cast<std::size_t>(kvCount));

    for (std::uint64_t i = 0; i < kvCount; ++i) {
        std::string key;
        std::uint32_t typeRaw = 0;
        if (!reader.readString(key) || !reader.read(typeRaw)) {
            return fail(ErrorCode::ParseFailure,
                        std::format("GGUF metadata ends unexpectedly at entry {}", i));
        }

        Scalar value;
        if (!readValue(reader, static_cast<ValueType>(typeRaw), value)) {
            return fail(ErrorCode::ParseFailure,
                        std::format("cannot read GGUF metadata key '{}'", key));
        }
        metadata.emplace(std::move(key), std::move(value));
    }

    const auto lookup = [&metadata](std::string_view key) -> Scalar {
        const auto it = metadata.find(std::string{key});
        return it != metadata.end() ? it->second : Scalar{};
    };

    info.architecture = asString(lookup("general.architecture"));
    info.name = asString(lookup("general.name"));
    info.sizeLabel = asString(lookup("general.size_label"));

    const Scalar fileType = lookup("general.file_type");
    if (!std::holds_alternative<std::monostate>(fileType)) {
        info.quantization = quantizationName(asUInt32(fileType));
    }

    // Shape keys are namespaced by architecture: "qwen3.context_length".
    if (!info.architecture.empty()) {
        info.contextLength = asUInt32(lookup(info.architecture + ".context_length"));
        info.embeddingLength = asUInt32(lookup(info.architecture + ".embedding_length"));
        info.blockCount = asUInt32(lookup(info.architecture + ".block_count"));
    }

    if (info.architecture.empty()) {
        return fail(ErrorCode::ParseFailure,
                    std::format("'{}' has no general.architecture and cannot be used",
                                path.filename().string()));
    }

    return info;
}

} // namespace jarvis::llm
