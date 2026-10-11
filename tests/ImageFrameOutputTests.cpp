/*
 * 模块名: ImageFrameOutputTests
 * 功能概述: 用明确 synthetic CPU 母版检验正式编码器、独立解码、HDR/alpha和出处负控制。
 * 对外接口: Catch2 [image-output-codec]；不冒真实作品/GPU渲染通过。
 * 依赖关系: 实际 OCIO、ImageFrameOutput、Qt PNG、第二个真实 OpenEXR decoder。
 * 输入输出: 固定尺寸样本到关闭文件、全部五 FLOAT 位值/PNG 字节和完整标签。
 * 异常与错误: 身份、尺寸、颜色标签或原值不符直接失败。
 * 维护说明: 仅在 MINI3D_J1_TEST_ROOT 的独占目录写新文件，EXR numThreads=0。
 */
#include "renderer_gl/ImageFrameOutput.h"
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace mini3d;
namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;
using renderer_gl::ImageFileFormat;
QString scratchTemplate() {
    const auto root = qEnvironmentVariable("MINI3D_J1_TEST_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("image-codec-XXXXXX");
}
void createBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    REQUIRE(file.flush());
}
struct Fixture {
    QString error;
    std::shared_ptr<const renderer_gl::OcioDisplayTransform> color;
    core::SourceIdentity origin, output;
    renderer_gl::ImageFrameStamp stamp;
    Fixture() {
        color = renderer_gl::OcioDisplayTransform::createFixedStandard(
            ":/mini3d/color/mini3d-standard.ocio", error);
        INFO(error.toStdString());
        REQUIRE(color);
        origin.documentUuid = core::createDocumentUuid();
        origin.sourceHandle = core::LiveSourceHandle{"codec-test-instance", "codec-test-document"};
        origin.contentRevision = 9007199254740993ULL;
        origin.authorContextIdentity = {origin.documentUuid, origin.contentRevision,
                                        std::get<core::LiveSourceHandle>(origin.sourceHandle)};
        origin.evaluationSessionId = core::createEvaluationSessionId();
        origin.evaluationId = 7;
        origin.frameTime = 12.5;
        origin.evaluationProfile = core::EvaluationProfile::JointPreviewJ1;
        output = origin;
        output.sourceKind = core::SourceKind::FrozenFormal;
        output.sourceHandle = core::FrozenSourceHandle{core::createDocumentUuid().bytes};
        output.evaluationProfile = core::EvaluationProfile::OfflineJ1;
        output.purpose = core::EvaluationPurpose::Output;
        output.evaluationSessionId = core::createEvaluationSessionId();
        output.evaluationId = 1;
        stamp = {core::createEvaluationSessionId(), 9007199254740995ULL, 1, 3, 13, 1};
    }
    renderer_gl::FrozenRenderPixels pixels() const {
        renderer_gl::FrozenRenderPixels value;
        value.source = output;
        value.width = 1280;
        value.height = 720;
        value.color = color->identity();
        value.backendBuild = "synthetic-CPU-codec-sample-not-product-render";
        value.geometryStages.push_back({13, core::PrimitiveKind::Empty, 0, 0, glm::dmat4(1)});
        value.beauty.assign(1280 * 720, glm::vec4(.1F, .2F, .3F, .5F));
        value.coverage.assign(1280 * 720, .75F);
        value.beauty[0] = {4.000123F, -0.250123F, 0.125F, 1};
        value.beauty[1] = {0, 0, 0, 0};
        return value;
    }
    std::shared_ptr<const renderer_gl::ImageFrame> frame(bool zeroAlphaEmission = false) {
        auto value = pixels();
        if (zeroAlphaEmission) value.beauty[1] = {3, -0.5F, .75F, 0};
        auto result = renderer_gl::ImageFrame::create(std::move(value), origin, stamp, error);
        INFO(error.toStdString());
        REQUIRE(result);
        return result;
    }
};
} // namespace

TEST_CASE("PNG stores straight RGB through one actual OCIO transform and alpha through none", "[image-output-codec]") {
    Fixture fixture;
    const auto frame = fixture.frame();
    const auto encoded = renderer_gl::ImageFrameOutput::encode(*frame, ImageFileFormat::Png,
                                                               *fixture.color, fixture.error);
    INFO(fixture.error.toStdString());
    REQUIRE(encoded);
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("frame.png");
    createBytes(path, encoded->bytes);
    REQUIRE(renderer_gl::ImageFrameOutput::validateFile(path, *frame, *encoded, fixture.error));
    const auto decoded = QImage::fromData(encoded->bytes, "PNG").convertToFormat(QImage::Format_RGBA8888);
    REQUIRE(decoded.size() == QSize(1280, 720));
    REQUIRE(decoded.textKeys() == QStringList{"mini3d.ImageFrame"});
    CHECK(decoded.colorSpace().iccProfile().isEmpty());
    std::array<glm::vec4, 1> linear{{{.2F, .4F, .6F, .5F}}}, display;
    REQUIRE(fixture.color->applyCpuRgba(linear, display, fixture.error));
    const auto* pixel = decoded.constScanLine(0) + 8;
    for (int channel = 0; channel < 3; ++channel)
        CHECK(pixel[channel] == static_cast<unsigned char>(std::lround(std::clamp(display[0][channel], 0.0F, 1.0F) * 255.0F)));
    CHECK(pixel[3] == 128);
    for (int row = 0; row < 720; ++row)
        REQUIRE(std::memcmp(decoded.constScanLine(row), encoded->rgba8.constData() + row * 1280 * 4, 1280 * 4) == 0);
    const auto tag = QByteArray::fromBase64(decoded.text("mini3d.ImageFrame").toLatin1());
    const auto metadata = QJsonDocument::fromJson(tag).object();
    CHECK(tag == frame->provenanceJson(ImageFileFormat::Png));
    CHECK(metadata["source"].toObject()["contentRevision"].toString() == "9007199254740993");
    CHECK(metadata["frameId"].toString() == "9007199254740995");
    CHECK(metadata["file"].toObject()["alpha"].toString() == "straight");
    CHECK_FALSE(metadata["file"].toObject()["independentCoverageSaved"].toBool());
}

TEST_CASE("PNG rejects zero-alpha emission and conflicting color or provenance files", "[image-output-codec]") {
    Fixture fixture;
    const auto emission = fixture.frame(true);
    CHECK_FALSE(renderer_gl::ImageFrameOutput::encode(*emission, ImageFileFormat::Png,
                                                      *fixture.color, fixture.error));
    const auto frame = fixture.frame();
    const auto encoded = renderer_gl::ImageFrameOutput::encode(*frame, ImageFileFormat::Png,
                                                               *fixture.color, fixture.error);
    REQUIRE(encoded);
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    auto image = QImage::fromData(encoded->bytes, "PNG");
    SECTION("ICC sRGB must not coexist with owned Standard output") {
        image.setColorSpace(QColorSpace::SRgb);
    }
    SECTION("Premultiplied declaration contradicts actual straight PNG") {
        auto metadata = QJsonDocument::fromJson(frame->provenanceJson(ImageFileFormat::Png)).object();
        auto file = metadata["file"].toObject();
        file.insert("alpha", "associated");
        metadata.insert("file", file);
        image.setText("mini3d.ImageFrame", QString::fromLatin1(QJsonDocument(metadata).toJson(QJsonDocument::Compact).toBase64()));
    }
    const auto path = directory.filePath("contradiction.png");
    REQUIRE(image.save(path, "PNG"));
    CHECK_FALSE(renderer_gl::ImageFrameOutput::validateFile(path, *frame, *encoded, fixture.error));
    CHECK_FALSE(fixture.error.isEmpty());
}

TEST_CASE("EXR independently decodes all five FLOAT planes with exact HDR negative and zero-alpha values", "[image-output-codec]") {
    Fixture fixture;
    const auto frame = fixture.frame(true);
    const auto encoded = renderer_gl::ImageFrameOutput::encode(*frame, ImageFileFormat::Exr,
                                                               *fixture.color, fixture.error);
    INFO(fixture.error.toStdString());
    REQUIRE(encoded);
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("frame.exr");
    createBytes(path, encoded->bytes);
    REQUIRE(renderer_gl::ImageFrameOutput::validateFile(path, *frame, *encoded, fixture.error));
    std::array<std::vector<float>, 5> decoded;
    constexpr std::array<const char*, 5> names{"R", "G", "B", "A", "coverage"};
    for (auto& plane : decoded) plane.resize(1280 * 720);
    {
        const auto nativePath = QFile::encodeName(path);
        Exr::InputFile input(nativePath.constData(), 0);
        const auto& header = input.header();
        const IMATH_NAMESPACE::Box2i window({0, 0}, {1279, 719});
        REQUIRE(header.dataWindow() == window);
        REQUIRE(header.displayWindow() == window);
        REQUIRE(header.pixelAspectRatio() == 1);
        REQUIRE(header.compression() == Exr::ZIP_COMPRESSION);
        REQUIRE(header.lineOrder() == Exr::INCREASING_Y);
        int channels = 0;
        for (auto item = header.channels().begin(); item != header.channels().end(); ++item) ++channels;
        REQUIRE(channels == 5);
        Exr::FrameBuffer buffer;
        for (std::size_t channel = 0; channel < names.size(); ++channel) {
            const auto* actual = header.channels().findChannel(names[channel]);
            REQUIRE(actual);
            REQUIRE(actual->type == Exr::FLOAT);
            REQUIRE(actual->xSampling == 1);
            REQUIRE(actual->ySampling == 1);
            buffer.insert(names[channel], Exr::Slice(Exr::FLOAT, reinterpret_cast<char*>(decoded[channel].data()),
                                                     sizeof(float), sizeof(float) * 1280, 1, 1));
        }
        const auto* tag = header.findTypedAttribute<Exr::StringAttribute>("mini3d.ImageFrame");
        REQUIRE(tag);
        REQUIRE(QByteArray::fromStdString(tag->value()) == frame->provenanceJson(ImageFileFormat::Exr));
        input.setFrameBuffer(buffer);
        input.readPixels(0, 719);
    }
    for (std::size_t channel = 0; channel < 5; ++channel) {
        std::size_t mismatches = 0;
        for (std::size_t index = 0; index < decoded[channel].size(); ++index) {
            const auto expected = channel == 4 ? frame->pixels().coverage[index] : frame->pixels().beauty[index][int(channel)];
            if (std::bit_cast<std::uint32_t>(decoded[channel][index]) !=
                std::bit_cast<std::uint32_t>(expected))
                ++mismatches;
        }
        INFO(names[channel]);
        CHECK(mismatches == 0);
    }
    CHECK(decoded[0][0] == 4.000123F);
    CHECK(decoded[1][0] == -0.250123F);
    CHECK(decoded[0][1] == 3);
    CHECK(decoded[3][1] == 0);
    CHECK(decoded[4][1] == .75F);
    const auto other = fixture.frame();
    CHECK_FALSE(renderer_gl::ImageFrameOutput::validateFile(path, *other, *encoded, fixture.error));
}

TEST_CASE("ImageFrame rejects illegal mother values and forbidden origin to output derivations", "[image-output-codec]") {
    Fixture fixture;
    SECTION("nonfinite pixel") {
        auto pixels = fixture.pixels();
        pixels.beauty[0].r = std::numeric_limits<float>::infinity();
        CHECK_FALSE(renderer_gl::ImageFrame::create(std::move(pixels), fixture.origin, fixture.stamp, fixture.error));
    }
    SECTION("coverage is independent but must be legal") {
        auto pixels = fixture.pixels();
        pixels.coverage[0] = -1;
        CHECK_FALSE(renderer_gl::ImageFrame::create(std::move(pixels), fixture.origin, fixture.stamp, fixture.error));
    }
    SECTION("fixed dimensions are not a label replacement") {
        auto pixels = fixture.pixels();
        pixels.width = 1279;
        CHECK_FALSE(renderer_gl::ImageFrame::create(std::move(pixels), fixture.origin, fixture.stamp, fixture.error));
    }
    SECTION("revision domains do not collapse into UUID and revision") {
        fixture.output.authorContextIdentity.revisionDomain.documentId = "other-domain";
        CHECK_FALSE(renderer_gl::isAllowedImageOutputDerivation(fixture.origin, fixture.output));
    }
    SECTION("request frame remains exact") {
        fixture.output.frameTime = 12.500000000000002;
        CHECK_FALSE(renderer_gl::isAllowedImageOutputDerivation(fixture.origin, fixture.output));
    }
    SECTION("Base algorithm is not full animation") {
        fixture.origin.algorithmBuild += "/base-only-1";
        fixture.output.algorithmBuild = fixture.origin.algorithmBuild;
        CHECK_FALSE(renderer_gl::isAllowedImageOutputDerivation(fixture.origin, fixture.output));
    }
}

TEST_CASE("An empty Joint preview keeps its genuinely empty stage inventory without weakening output camera checks", "[image-output-codec]") {
    Fixture fixture;
    auto pixels = fixture.pixels();
    pixels.geometryStages.clear();
    SECTION("Empty preview inventory is valid") {
        pixels.source = fixture.origin;
        fixture.stamp.cameraEntity = 0;
        CHECK(renderer_gl::ImageFrame::create(std::move(pixels), fixture.origin, fixture.stamp, fixture.error));
    }
    SECTION("Output still requires the consumed camera stage") {
        CHECK_FALSE(renderer_gl::ImageFrame::create(std::move(pixels), fixture.origin, fixture.stamp, fixture.error));
    }
}
