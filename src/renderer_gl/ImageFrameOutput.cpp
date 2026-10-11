/*
 * 模块名: ImageFrameOutput
 * 功能概述: 实际编码并独立解析 PNG chunk、EXR FLOAT 通道与完整来源标签。
 * 对外接口: ImageFrameOutput.h。
 * 依赖关系: Qt6 Gui/Core、zlib、OpenEXR、固定 OCIO CPU processor。
 * 输入输出: 母版到字节，关闭后的文件到精确往返断言。
 * 异常与错误: 任一格式、来源、颜色或像素矛盾拒绝；库异常转换为 error。
 * 维护说明: 单一 mini3d.ImageFrame 标签；编码器不做第二次颜色转换或 alpha 变换。
 */
#include "ImageFrameOutput.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace mini3d::renderer_gl {
namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;
constexpr char kKeyword[] = "mini3d.ImageFrame";
constexpr qsizetype kFileByteLimit = 64 * 1024 * 1024;
constexpr std::array<const char*, 5> kChannels{"R", "G", "B", "A", "coverage"};
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

class ByteOutput final : public Exr::OStream {
  public:
    ByteOutput() : Exr::OStream("ImageFrame-memory") {}
    void write(const char* bytes, int count) override {
        if (count < 0 || position_ > std::uint64_t(kFileByteLimit) ||
            std::uint64_t(count) > std::uint64_t(kFileByteLimit) - position_)
            throw std::runtime_error("EXR exceeds the fixed file budget");
        const auto end = qsizetype(position_ + std::uint64_t(count));
        if (end > data.size()) data.resize(end);
        std::memcpy(data.data() + position_, bytes, std::size_t(count));
        position_ += std::uint64_t(count);
    }
    std::uint64_t tellp() override { return position_; }
    void seekp(std::uint64_t position) override {
        if (position > std::uint64_t(kFileByteLimit))
            throw std::runtime_error("EXR seek exceeds the fixed file budget");
        position_ = position;
    }
    QByteArray data;
  private:
    std::uint64_t position_ = 0;
};
class ByteInput final : public Exr::IStream {
  public:
    explicit ByteInput(const QByteArray& bytes) : Exr::IStream("ImageFrame-closed-file"), data_(bytes) {}
    bool read(char* bytes, int count) override {
        if (count < 0 || position_ > std::uint64_t(data_.size()) ||
            std::uint64_t(count) > std::uint64_t(data_.size()) - position_)
            throw std::runtime_error("Truncated EXR bytes");
        std::memcpy(bytes, data_.constData() + position_, std::size_t(count));
        position_ += std::uint64_t(count);
        return position_ < std::uint64_t(data_.size());
    }
    std::uint64_t tellg() override { return position_; }
    void seekg(std::uint64_t position) override {
        if (position > std::uint64_t(data_.size())) throw std::runtime_error("Invalid EXR seek");
        position_ = position;
    }
  private:
    const QByteArray& data_;
    std::uint64_t position_ = 0;
};
using Planes = std::array<std::vector<float>, 5>;
Exr::FrameBuffer frameBuffer(Planes& planes, int width) {
    Exr::FrameBuffer result;
    for (std::size_t channel = 0; channel < kChannels.size(); ++channel)
        result.insert(kChannels[channel], Exr::Slice(Exr::FLOAT,
            reinterpret_cast<char*>(planes[channel].data()), sizeof(float),
            sizeof(float) * std::size_t(width), 1, 1));
    return result;
}
bool reject(QString& error, const QString& message) {
    error = message;
    return false;
}
unsigned char quantize(float value) {
    return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}
std::optional<EncodedImage> encodePng(const ImageFrame& frame,
                                     const OcioDisplayTransform& color, QString& error) {
    const auto& pixels = frame.pixels();
    if (pixels.color != color.identity()) {
        reject(error, QStringLiteral("PNG 的实际 OCIO processor 与冻结母版不一致。"));
        return std::nullopt;
    }
    std::vector<glm::vec4> straight(pixels.beauty.size()), display(pixels.beauty.size());
    for (std::size_t index = 0; index < pixels.beauty.size(); ++index) {
        const auto& rgba = pixels.beauty[index];
        if (rgba.a == 0 && (rgba.r != 0 || rgba.g != 0 || rgba.b != 0)) {
            reject(error, QStringLiteral("PNG 无法保真保存零 alpha 非零 RGB 发光，请使用 FLOAT32 EXR。"));
            return std::nullopt;
        }
        straight[index] = rgba.a > 0 ? glm::vec4(glm::vec3(rgba) / rgba.a, rgba.a) : rgba;
    }
    if (!color.applyCpuRgba(straight, display, error)) return std::nullopt;
    EncodedImage result;
    result.format = ImageFileFormat::Png;
    result.rgba8.resize(qsizetype(display.size() * 4));
    QImage image(pixels.width, pixels.height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        reject(error, QStringLiteral("PNG 像素分配失败。"));
        return std::nullopt;
    }
    image.setColorSpace(QColorSpace{});
    if (!image.colorSpace().iccProfile().isEmpty()) {
        reject(error, QStringLiteral("PNG 必须没有隐式 ICC profile。"));
        return std::nullopt;
    }
    for (std::size_t index = 0; index < display.size(); ++index) {
        for (int channel = 0; channel < 4; ++channel)
            result.rgba8[qsizetype(index * 4 + std::size_t(channel))] = char(quantize(display[index][channel]));
    }
    for (int row = 0; row < pixels.height; ++row)
        std::memcpy(image.scanLine(row), result.rgba8.constData() + qsizetype(row) * pixels.width * 4,
                    std::size_t(pixels.width) * 4);
    const auto label = frame.provenanceJson(ImageFileFormat::Png).toBase64();
    image.setText(QString::fromLatin1(kKeyword), QString::fromLatin1(label));
    QBuffer buffer(&result.bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return std::nullopt;
    QImageWriter writer(&buffer, "png");
    // Qt6 PNG handler 的 gamma 默认 0；不设置额外颜色选项，独立门拒绝 gAMA/ICC/sRGB。
    if (!writer.write(image) || result.bytes.size() > kFileByteLimit) {
        reject(error, QStringLiteral("PNG 编码失败：%1").arg(writer.errorString()));
        return std::nullopt;
    }
    error.clear();
    return result;
}
std::optional<EncodedImage> encodeExr(const ImageFrame& frame, QString& error) {
    const auto& pixels = frame.pixels();
    Planes planes;
    for (auto& plane : planes) plane.resize(pixels.beauty.size());
    for (std::size_t index = 0; index < pixels.beauty.size(); ++index) {
        for (std::size_t channel = 0; channel < 4; ++channel)
            planes[channel][index] = pixels.beauty[index][int(channel)];
        planes[4][index] = pixels.coverage[index];
    }
    Exr::Header header(pixels.width, pixels.height);
    header.lineOrder() = Exr::INCREASING_Y;
    header.compression() = Exr::ZIP_COMPRESSION;
    for (const auto* name : kChannels) header.channels().insert(name, Exr::Channel(Exr::FLOAT, 1, 1));
    const auto metadata = frame.provenanceJson(ImageFileFormat::Exr);
    header.insert(kKeyword, Exr::StringAttribute(std::string(metadata.constData(), std::size_t(metadata.size()))));
    ByteOutput stream;
    {
        Exr::OutputFile output(stream, header, 0);
        output.setFrameBuffer(frameBuffer(planes, pixels.width));
        output.writePixels(pixels.height);
    }
    error.clear();
    return EncodedImage{ImageFileFormat::Exr, std::move(stream.data), {}};
}

bool validatePng(const QByteArray& bytes, const ImageFrame& frame,
                 const EncodedImage& encoded, QString& error) {
    const auto& pixels = frame.pixels();
    const auto metadata = frame.provenanceJson(ImageFileFormat::Png);
    const auto expectedLabel = metadata.toBase64();
    if (!bytes.startsWith(QByteArray::fromHex("89504E470D0A1A0A")))
        return reject(error, QStringLiteral("PNG signature 无效。"));
    qsizetype position = 8;
    int headers = 0, labels = 0, endings = 0;
    QByteArray actualLabel;
    while (position < bytes.size()) {
        if (bytes.size() - position < 12) return reject(error, QStringLiteral("PNG chunk 不完整。"));
        const auto length = qFromBigEndian<quint32>(bytes.constData() + position);
        if (std::uint64_t(length) + 12 > std::uint64_t(bytes.size() - position))
            return reject(error, QStringLiteral("PNG chunk 长度无效。"));
        const auto type = bytes.mid(position + 4, 4);
        const auto data = bytes.mid(position + 8, length);
        const auto checksum = qFromBigEndian<quint32>(bytes.constData() + position + 8 + length);
        const auto actualCrc = crc32(0, reinterpret_cast<const Bytef*>(bytes.constData() + position + 4),
                                      uInt(length + 4));
        if (actualCrc != checksum) return reject(error, QStringLiteral("PNG CRC 不符。"));
        if (type == "IHDR") {
            if (++headers != 1 || position != 8 || length != 13 ||
                qFromBigEndian<quint32>(data.constData()) != quint32(pixels.width) ||
                qFromBigEndian<quint32>(data.constData() + 4) != quint32(pixels.height) ||
                static_cast<unsigned char>(data[8]) != 8 || data[9] != 6 || data[10] != 0 ||
                data[11] != 0 || data[12] != 0)
                return reject(error, QStringLiteral("PNG 实际 IHDR 不是本次尺寸 RGBA8 Progressive。"));
        } else if (type == "iCCP" || type == "sRGB" || type == "gAMA" ||
                   type == "tEXt" || type == "iTXt") {
            return reject(error, QStringLiteral("PNG 含冲突色彩或第二文字标签。"));
        } else if (type == "zTXt") {
            const auto separator = data.indexOf('\0');
            if (++labels != 1 || separator != qsizetype(sizeof(kKeyword) - 1) ||
                data.left(separator) != kKeyword || separator + 2 > data.size() || data[separator + 1] != 0)
                return reject(error, QStringLiteral("PNG 必须只有 mini3d.ImageFrame method0 zTXt。"));
            QByteArray decoded(expectedLabel.size(), Qt::Uninitialized);
            uLongf size = uLongf(decoded.size());
            const auto compressed = data.mid(separator + 2);
            uLong compressedSize = uLong(compressed.size());
            if (uncompress2(reinterpret_cast<Bytef*>(decoded.data()), &size,
                            reinterpret_cast<const Bytef*>(compressed.constData()), &compressedSize) != Z_OK ||
                compressedSize != uLong(compressed.size()) ||
                size != uLongf(expectedLabel.size()))
                return reject(error, QStringLiteral("PNG 原始压缩出处无法完整解压。"));
            actualLabel = std::move(decoded);
        } else if (type == "IEND") {
            if (++endings != 1 || length != 0 || position + 12 != bytes.size())
                return reject(error, QStringLiteral("PNG IEND/尾部无效。"));
        }
        position += qsizetype(length) + 12;
    }
    if (headers != 1 || labels != 1 || endings != 1 || actualLabel.size() < 40 ||
        actualLabel.contains('\n') || actualLabel.contains('\r'))
        return reject(error, QStringLiteral("PNG 完整出处缺失。"));
    const auto decoded = QByteArray::fromBase64Encoding(actualLabel, QByteArray::AbortOnBase64DecodingErrors);
    QJsonParseError jsonError;
    const auto json = QJsonDocument::fromJson(decoded.decoded, &jsonError);
    if (!decoded || decoded.decoded.toBase64() != actualLabel || actualLabel != expectedLabel ||
        jsonError.error != QJsonParseError::NoError || !json.isObject() ||
        json.toJson(QJsonDocument::Compact) != metadata)
        return reject(error, QStringLiteral("PNG Base64/完整 JSON 出处与母版矛盾。"));
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return reject(error, QStringLiteral("PNG 实际关闭文件 bytes 不能独立解码。"));
    QImageReader reader(&buffer, "png");
    const auto image = reader.read();
    if (image.isNull() || image.size() != QSize(pixels.width, pixels.height) ||
        !image.colorSpace().iccProfile().isEmpty() ||
        image.textKeys() != QStringList{QString::fromLatin1(kKeyword)} ||
        image.text(QString::fromLatin1(kKeyword)).toLatin1() != actualLabel)
        return reject(error, QStringLiteral("Qt 独立 PNG 解码尺寸/标签/颜色不符。"));
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (encoded.rgba8.size() != qsizetype(pixels.beauty.size() * 4))
        return reject(error, QStringLiteral("PNG 期望像素不完整。"));
    for (int row = 0; row < pixels.height; ++row)
        if (std::memcmp(rgba.constScanLine(row), encoded.rgba8.constData() + qsizetype(row) * pixels.width * 4,
                        std::size_t(pixels.width) * 4) != 0)
            return reject(error, QStringLiteral("PNG 全部 RGBA 字节与本次编码原值不符。"));
    return true;
}
bool validateExr(const QByteArray& bytes, const ImageFrame& frame, QString& error) {
    const auto& pixels = frame.pixels();
    ByteInput stream(bytes);
    Exr::InputFile input(stream, 0);
    const auto& header = input.header();
    const IMATH_NAMESPACE::Box2i window({0, 0}, {pixels.width - 1, pixels.height - 1});
    if (!input.isComplete() || header.dataWindow() != window || header.displayWindow() != window ||
        header.pixelAspectRatio() != 1 || header.lineOrder() != Exr::INCREASING_Y ||
        header.compression() != Exr::ZIP_COMPRESSION)
        return reject(error, QStringLiteral("EXR 实际尺寸/窗口/采样顺序/无损压缩不符。"));
    std::size_t channelCount = 0;
    for (auto channel = header.channels().begin(); channel != header.channels().end(); ++channel) ++channelCount;
    if (channelCount != kChannels.size()) return reject(error, QStringLiteral("EXR 必须恰有五个 FLOAT 通道。"));
    for (const auto* name : kChannels) {
        const auto* channel = header.channels().findChannel(name);
        if (!channel || channel->type != Exr::FLOAT || channel->xSampling != 1 || channel->ySampling != 1)
            return reject(error, QStringLiteral("EXR 通道名/type/sampling 不符。"));
    }
    const auto* tag = header.findTypedAttribute<Exr::StringAttribute>(kKeyword);
    const auto metadata = frame.provenanceJson(ImageFileFormat::Exr);
    if (!tag || QByteArray::fromStdString(tag->value()) != metadata)
        return reject(error, QStringLiteral("EXR 完整出处与本次母版不符。"));
    QJsonParseError jsonError;
    const auto json = QJsonDocument::fromJson(QByteArray::fromStdString(tag->value()), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !json.isObject() ||
        json.toJson(QJsonDocument::Compact) != metadata)
        return reject(error, QStringLiteral("EXR JSON 出处无效。"));
    Planes decoded;
    for (auto& plane : decoded) plane.resize(pixels.beauty.size());
    input.setFrameBuffer(frameBuffer(decoded, pixels.width));
    input.readPixels(0, pixels.height - 1);
    for (std::size_t index = 0; index < pixels.beauty.size(); ++index) {
        for (std::size_t channel = 0; channel < 5; ++channel) {
            const auto expected = channel == 4 ? pixels.coverage[index] : pixels.beauty[index][int(channel)];
            if (std::bit_cast<std::uint32_t>(decoded[channel][index]) != std::bit_cast<std::uint32_t>(expected))
                return reject(error, QStringLiteral("EXR 全部 FLOAT32 原位值不符。"));
        }
    }
    return true;
}
} // namespace

std::optional<EncodedImage> ImageFrameOutput::encode(
    const ImageFrame& frame, ImageFileFormat format, const OcioDisplayTransform& color, QString& error) {
    try {
        if (frame.pixels().width != 1280 || frame.pixels().height != 720 ||
            frame.pixels().source.purpose != core::EvaluationPurpose::Output) {
            reject(error, QStringLiteral("正式编码只接受固定 1280×720 的本次 Frozen Output 母版。"));
            return std::nullopt;
        }
        if (format == ImageFileFormat::Png) return encodePng(frame, color, error);
        if (format == ImageFileFormat::Exr) return encodeExr(frame, error);
        reject(error, QStringLiteral("不支持的图像文件格式。"));
    } catch (const std::exception& failure) {
        error = QStringLiteral("图像编码失败：%1").arg(QString::fromUtf8(failure.what()));
    }
    return std::nullopt;
}
bool ImageFrameOutput::validateFile(const QString& path, const ImageFrame& frame,
                                   const EncodedImage& encoded, QString& error) {
    try {
        QByteArray bytes;
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > kFileByteLimit)
                return reject(error, QStringLiteral("关闭后的图像暂存不可读取或超出预算。"));
            bytes = file.readAll();
            if (bytes.size() != file.size()) return reject(error, QStringLiteral("图像暂存读取不完整。"));
        }
        const bool valid = encoded.format == ImageFileFormat::Png
            ? validatePng(bytes, frame, encoded, error)
            : encoded.format == ImageFileFormat::Exr ? validateExr(bytes, frame, error)
            : reject(error, QStringLiteral("未知编码格式。"));
        if (valid) error.clear();
        return valid;
    } catch (const std::exception& failure) {
        return reject(error, QStringLiteral("独立图像解码失败：%1").arg(QString::fromUtf8(failure.what())));
    }
}
} // namespace mini3d::renderer_gl
