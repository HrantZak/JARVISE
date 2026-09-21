#include <QTest>

#include <filesystem>
#include <fstream>

#include "jarvis/llm/GgufInspector.h"
#include "jarvis/llm/LlamaCppBackend.h"
#include "jarvis/llm/ModelRegistry.h"

using namespace jarvis::llm;
namespace fs = std::filesystem;

/// Unit tests for the model layer. Nothing here loads weights: these cover the
/// parts that must work before a model is ever opened - inspecting files,
/// finding them, and refusing the ones that are not usable.
class TestLlm : public QObject {
    Q_OBJECT

private slots:
    // GGUF inspection
    void magicIsRejectedForNonGgufFiles();
    void inspectRejectsMissingFile();
    void inspectRejectsTruncatedHeader();
    void inspectReadsARealModel();

    // Model description
    void displayNameFallsBackToFileName();
    void kvCacheEstimateScalesWithContext();
    void vramEstimateExceedsFileSize();

    // Registry
    void registryIgnoresSmallFiles();
    void registryFindsOllamaBlobs();
    void registryDeduplicatesPaths();

    // Backend, without loading a model
    void backendReportsDevices();
    void backendRefusesToGenerateWithoutAModel();
    void backendRefusesAnInvalidModel();

private:
    /// Path of a real GGUF language model on this machine, or an empty path.
    static fs::path findLocalModel();
};

fs::path TestLlm::findLocalModel() {
    ModelRegistry::Options options;
    options.includeOllamaBlobs = true;
    ModelRegistry registry{options};

    const std::vector<ModelInfo> models = registry.scan();
    return models.empty() ? fs::path{} : models.front().path;
}

// --- GGUF inspection ------------------------------------------------------

void TestLlm::magicIsRejectedForNonGgufFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const fs::path file = fs::path{dir.path().toStdWString()} / "not-a-model.bin";
    {
        std::ofstream out{file, std::ios::binary};
        out << "This is plainly not a GGUF file.";
    }

    QVERIFY(!GgufInspector::hasGgufMagic(file));
    QVERIFY(!GgufInspector::inspect(file).has_value());
}

void TestLlm::inspectRejectsMissingFile() {
    const auto result = GgufInspector::inspect("Z:/no/such/model.gguf");
    QVERIFY(!result.has_value());
    QVERIFY(result.error().code() == jarvis::core::ErrorCode::NotFound);
}

void TestLlm::inspectRejectsTruncatedHeader() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Correct magic, then nothing. A file that lies about being a model must be
    // reported, not crash the inspector.
    const fs::path file = fs::path{dir.path().toStdWString()} / "truncated.gguf";
    {
        std::ofstream out{file, std::ios::binary};
        out.write("GGUF", 4);
    }

    QVERIFY(GgufInspector::hasGgufMagic(file));

    const auto result = GgufInspector::inspect(file);
    QVERIFY(!result.has_value());
    QVERIFY(result.error().code() == jarvis::core::ErrorCode::ParseFailure);
}

void TestLlm::inspectReadsARealModel() {
    const fs::path model = findLocalModel();
    if (model.empty()) {
        QSKIP("no GGUF language model on this machine");
    }

    const auto info = GgufInspector::inspect(model);
    QVERIFY2(info.has_value(), info ? "" : info.error().toUserString().c_str());

    QVERIFY(info->isValid());
    QVERIFY(!info->architecture.empty());
    QVERIFY(info->ggufVersion >= 2);
    QVERIFY(info->fileSizeBytes > 0);
    QVERIFY2(info->blockCount > 0, "a language model must report its layer count");
    QVERIFY(info->contextLength > 0);
}

// --- Model description ----------------------------------------------------

void TestLlm::displayNameFallsBackToFileName() {
    ModelInfo info;
    info.path = "C:/models/sha256-abcdef";
    QCOMPARE(QString::fromStdString(info.displayName()), QStringLiteral("sha256-abcdef"));

    info.name = "Qwen3 8B";
    QCOMPARE(QString::fromStdString(info.displayName()), QStringLiteral("Qwen3 8B"));
}

void TestLlm::kvCacheEstimateScalesWithContext() {
    ModelInfo info;
    info.blockCount = 36;
    info.embeddingLength = 4096;

    const std::uint64_t small = estimateKvCacheBytes(info, 4096);
    const std::uint64_t large = estimateKvCacheBytes(info, 8192);

    QVERIFY(small > 0);
    QCOMPARE(large, small * 2);

    // A model whose shape is unknown cannot be estimated, and must say zero
    // rather than invent a number.
    QCOMPARE(estimateKvCacheBytes(ModelInfo{}, 4096), std::uint64_t{0});
}

void TestLlm::vramEstimateExceedsFileSize() {
    ModelInfo info;
    info.fileSizeBytes = 5ULL * 1024 * 1024 * 1024;
    info.blockCount = 36;
    info.embeddingLength = 4096;

    // Weights alone are never the whole story: the estimate must leave room for
    // the KV cache and compute buffers, or JARVIS would happily load a model
    // that runs the card out of memory mid-answer.
    QVERIFY(info.estimateVramBytes(8192) > info.fileSizeBytes);
}

// --- Registry -------------------------------------------------------------

void TestLlm::registryIgnoresSmallFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const fs::path root = fs::path{dir.path().toStdWString()};
    {
        // Valid magic but tiny: not a plausible model, must not be opened.
        std::ofstream out{root / "tiny.gguf", std::ios::binary};
        out.write("GGUF", 4);
    }

    ModelRegistry::Options options;
    options.directories = {root};
    options.includeOllamaBlobs = false;
    ModelRegistry registry{options};

    QVERIFY(registry.scan().empty());
    QVERIFY2(registry.warnings().empty(),
             "a file skipped by size must not produce a warning");
}

void TestLlm::registryFindsOllamaBlobs() {
    const fs::path blobs = ModelRegistry::ollamaBlobDirectory();
    if (blobs.empty() || !fs::is_directory(blobs)) {
        QSKIP("no Ollama blob store on this machine");
    }

    ModelRegistry::Options options;
    options.includeOllamaBlobs = true;
    ModelRegistry registry{options};

    const std::vector<ModelInfo> models = registry.scan();
    if (models.empty()) {
        QSKIP("Ollama blob store holds no language models");
    }

    for (const ModelInfo& model : models) {
        QVERIFY(model.isValid());
        QVERIFY(model.fileSizeBytes > 0);
        // Diffusion models must have been filtered out.
        QVERIFY(model.architecture != "hyvid");
    }
}

void TestLlm::registryDeduplicatesPaths() {
    const fs::path blobs = ModelRegistry::ollamaBlobDirectory();
    if (blobs.empty() || !fs::is_directory(blobs)) {
        QSKIP("no Ollama blob store on this machine");
    }

    // The same directory listed twice, plus the implicit Ollama scan.
    ModelRegistry::Options options;
    options.directories = {blobs, blobs};
    options.includeOllamaBlobs = true;
    ModelRegistry registry{options};

    const std::vector<ModelInfo> models = registry.scan();

    std::set<fs::path> unique;
    for (const ModelInfo& model : models) {
        QVERIFY2(unique.insert(model.path).second, "a model was listed twice");
    }
}

// --- Backend --------------------------------------------------------------

void TestLlm::backendReportsDevices() {
    LlamaCppBackend backend;

    const std::vector<DeviceInfo> devices = backend.devices();
    QVERIFY2(!devices.empty(), "llama.cpp must register at least the CPU device");

    for (const DeviceInfo& device : devices) {
        QVERIFY(!device.name.empty());
        QVERIFY(!device.backend.empty());
    }

    // This build is configured with the Vulkan backend, so a GPU device must be
    // present. If this fails, the build lost its GPU support.
    const bool hasGpu = std::any_of(devices.begin(), devices.end(),
                                    [](const DeviceInfo& d) { return d.isGpu; });
    QVERIFY2(hasGpu, "no GPU device: the Vulkan backend is missing from this build");
    QVERIFY2(backend.supportsGpuOffload(), "llama.cpp reports no GPU offload support");
}

void TestLlm::backendRefusesToGenerateWithoutAModel() {
    LlamaCppBackend backend;
    QVERIFY(!backend.isLoaded());

    GenerationRequest request;
    request.messages.push_back({ChatMessage::Role::User, "Привет"});

    const auto result = backend.generate(request, [](std::string_view) { return true; });
    QVERIFY2(!result.has_value(), "generating without a model must fail, not return text");
    QVERIFY(result.error().code() == jarvis::core::ErrorCode::Unavailable);
}

void TestLlm::backendRefusesAnInvalidModel() {
    LlamaCppBackend backend;

    ModelInfo bogus;
    bogus.path = "Z:/no/such/model.gguf";

    const auto status = backend.load(bogus, LoadParams{});
    QVERIFY(!status.has_value());
    QVERIFY(!backend.isLoaded());
}

QTEST_GUILESS_MAIN(TestLlm)

#include "tst_llm.moc"
