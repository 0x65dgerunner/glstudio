#include "hdri_loader.hpp"

#include "tinyexr.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHdrMax = 50000.0f;

float sanitizeHdr(float v) {
    if (!std::isfinite(v) || v < 0.0f) {
        return 0.0f;
    }
    return std::min(v, kHdrMax);
}

QVector3D sanitizeHdr(const QVector3D& c) {
    return QVector3D(sanitizeHdr(c.x()), sanitizeHdr(c.y()), sanitizeHdr(c.z()));
}

}  // namespace

void cubemapFaceDirection(int face, float u, float v, QVector3D* dir) {
    switch (face) {
        case 0:
            *dir = QVector3D(1.0f, -v, -u);
            break;
        case 1:
            *dir = QVector3D(-1.0f, -v, u);
            break;
        case 2:
            *dir = QVector3D(u, 1.0f, v);
            break;
        case 3:
            *dir = QVector3D(u, -1.0f, -v);
            break;
        case 4:
            *dir = QVector3D(u, -v, 1.0f);
            break;
        default:
            *dir = QVector3D(-u, -v, -1.0f);
            break;
    }
}

QVector3D HdriImage::sampleDir(const QVector3D& raw) const {
    if (!hasLatLong()) {
        return QVector3D(0, 0, 0);
    }
    QVector3D dir = raw.normalized();
    const float phi = std::atan2(dir.z(), dir.x());
    const float theta = std::acos(std::max(-1.0f, std::min(1.0f, dir.y())));
    float u = phi * (0.5f / kPi) + 0.5f;
    float v = theta / kPi;
    u -= std::floor(u);
    v = std::max(0.0f, std::min(1.0f, v));

    const float x = u * static_cast<float>(width) - 0.5f;
    const float y = v * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    auto at = [&](int ix, int iy) {
        ix = (ix % width + width) % width;
        iy = std::max(0, std::min(height - 1, iy));
        const size_t i = static_cast<size_t>((iy * width + ix) * 3);
        return QVector3D(rgb[i], rgb[i + 1], rgb[i + 2]);
    };

    const QVector3D c00 = at(x0, y0);
    const QVector3D c10 = at(x0 + 1, y0);
    const QVector3D c01 = at(x0, y0 + 1);
    const QVector3D c11 = at(x0 + 1, y0 + 1);
    return c00 * ((1.0f - fx) * (1.0f - fy)) + c10 * (fx * (1.0f - fy)) + c01 * ((1.0f - fx) * fy) +
           c11 * (fx * fy);
}

QString userHdriDir() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hdri"));
}

QString ensureUserHdriDir() {
    const QString path = userHdriDir();
    QDir().mkpath(path);
    return path;
}

void collectHdriDir(const QString& dirPath, const QStringList& filters, QSet<QString>* seen,
                    std::vector<HdriEntry>* out) {
    if (dirPath.isEmpty() || seen == nullptr || out == nullptr || !QDir(dirPath).exists()) {
        return;
    }
    const QFileInfoList infos = QDir(dirPath).entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& info : infos) {
        const QString key = info.fileName().toLower();
        if (seen->contains(key)) {
            continue;
        }
        seen->insert(key);
        HdriEntry entry;
        entry.name = info.completeBaseName().replace(QLatin1Char('_'), QLatin1Char(' '));
        entry.path = info.absoluteFilePath();
        out->push_back(entry);
    }
}

std::vector<HdriEntry> listHdriFiles() {
    std::vector<HdriEntry> out;
    QSet<QString> seen;
    const QStringList hdriFilters = {QStringLiteral("*.exr"), QStringLiteral("*.hdr"),
                                     QStringLiteral("*.EXR"), QStringLiteral("*.HDR")};
    collectHdriDir(ensureUserHdriDir(), hdriFilters, &seen, &out);

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 10; ++i) {
        collectHdriDir(dir.filePath(QStringLiteral("glstudio/resources/HDRI")), hdriFilters, &seen, &out);
        collectHdriDir(dir.filePath(QStringLiteral("glstudio/resources")), {QStringLiteral("*.exr"), QStringLiteral("*.EXR")},
                       &seen, &out);
        collectHdriDir(dir.filePath(QStringLiteral("resources/HDRI")), hdriFilters, &seen, &out);
        collectHdriDir(dir.filePath(QStringLiteral("resources")), {QStringLiteral("*.exr"), QStringLiteral("*.EXR")},
                       &seen, &out);
        if (!dir.cdUp()) {
            break;
        }
    }
    return out;
}

QString findHdriPath() {
    const std::vector<HdriEntry> all = listHdriFiles();
    const QStringList preferred = {QStringLiteral("Default")};
    for (const QString& key : preferred) {
        for (const HdriEntry& e : all) {
            if (e.name.compare(key, Qt::CaseInsensitive) == 0 ||
                e.path.contains(key, Qt::CaseInsensitive)) {
                return e.path;
            }
        }
    }
    return all.empty() ? QString() : all.front().path;
}

void bakeCubemap(HdriImage* image, int cubeSize);

HdriImage loadExrHdri(const QString& path, int cubeSize) {
    HdriImage image;
    image.name = QFileInfo(path).completeBaseName().replace(QLatin1Char('_'), QLatin1Char(' '));
    image.path = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        image.error = QStringLiteral("Could not open %1").arg(path);
        return image;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        image.error = QStringLiteral("HDRI file is empty.");
        return image;
    }

    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;
    const int ret = LoadEXRFromMemory(&rgba, &width, &height,
                                      reinterpret_cast<const unsigned char*>(bytes.constData()),
                                      static_cast<size_t>(bytes.size()), &err);
    if (ret != TINYEXR_SUCCESS || rgba == nullptr || width <= 0 || height <= 0) {
        image.error = err != nullptr ? QString::fromUtf8(err) : QStringLiteral("Failed to decode EXR.");
        if (err != nullptr) {
            FreeEXRErrorMessage(err);
        }
        if (rgba != nullptr) {
            free(rgba);
        }
        return image;
    }

    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    for (int i = 0; i < width * height; ++i) {
        image.rgb[static_cast<size_t>(i) * 3u] = sanitizeHdr(rgba[i * 4]);
        image.rgb[static_cast<size_t>(i) * 3u + 1u] = sanitizeHdr(rgba[i * 4 + 1]);
        image.rgb[static_cast<size_t>(i) * 3u + 2u] = sanitizeHdr(rgba[i * 4 + 2]);
    }
    free(rgba);

    bakeCubemap(&image, cubeSize);
    image.path = path;
    return image;
}

void rgbeToRgb(const unsigned char* rgbe, float* rgb) {
    if (rgbe[3] == 0) {
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        return;
    }
    const float f = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - 128 - 8);
    rgb[0] = sanitizeHdr(static_cast<float>(rgbe[0]) * f);
    rgb[1] = sanitizeHdr(static_cast<float>(rgbe[1]) * f);
    rgb[2] = sanitizeHdr(static_cast<float>(rgbe[2]) * f);
}

bool readRadianceScanline(const unsigned char* data, int length, int* cursor, int width,
                          unsigned char* scan) {
    if (*cursor + 4 > length) {
        return false;
    }
    const unsigned char* p = data + *cursor;
    if (width >= 8 && width <= 32767 && p[0] == 2 && p[1] == 2 && ((p[2] << 8) | p[3]) == width) {
        *cursor += 4;
        for (int ch = 0; ch < 4; ++ch) {
            int x = 0;
            while (x < width) {
                if (*cursor >= length) {
                    return false;
                }
                const int code = data[(*cursor)++];
                if (code > 128) {
                    const int count = code - 128;
                    if (*cursor >= length || x + count > width) {
                        return false;
                    }
                    const unsigned char val = data[(*cursor)++];
                    for (int i = 0; i < count; ++i) {
                        scan[(x + i) * 4 + ch] = val;
                    }
                    x += count;
                } else {
                    if (*cursor + code > length || x + code > width) {
                        return false;
                    }
                    for (int i = 0; i < code; ++i) {
                        scan[(x + i) * 4 + ch] = data[(*cursor)++];
                    }
                    x += code;
                }
            }
        }
        return true;
    }
    if (*cursor + width * 4 > length) {
        return false;
    }
    std::memcpy(scan, data + *cursor, static_cast<size_t>(width) * 4u);
    *cursor += width * 4;
    return true;
}

bool loadRadianceHdr(const QByteArray& bytes, HdriImage* image, QString* error) {
    const QByteArray headerEnd = QByteArray(1, '\n') + QByteArray(1, '\n');
    int headerAt = bytes.indexOf(headerEnd);
    if (headerAt < 0) {
        headerAt = bytes.indexOf("\r\n\r\n");
        if (headerAt < 0) {
            *error = QStringLiteral("Invalid Radiance HDR header.");
            return false;
        }
        headerAt += 4;
    } else {
        headerAt += 2;
    }
    const QByteArray header = bytes.left(headerAt);
    if (!header.contains("#?RADIANCE") && !header.contains("#?RGBE")) {
        *error = QStringLiteral("Not a Radiance HDR file.");
        return false;
    }
    int width = 0;
    int height = 0;
    const int resAt = bytes.indexOf("-Y ", headerAt - 2);
    const int resAt2 = bytes.indexOf("+Y ", headerAt - 2);
    const int useRes = resAt >= 0 ? resAt : resAt2;
    if (useRes < 0) {
        *error = QStringLiteral("HDR resolution line missing.");
        return false;
    }
    const int lineEnd = bytes.indexOf('\n', useRes);
    const QByteArray resLine = bytes.mid(useRes, lineEnd > useRes ? lineEnd - useRes : 64);
    const QList<QByteArray> parts = resLine.simplified().split(' ');
    if (parts.size() >= 4) {
        height = parts[1].toInt();
        width = parts[3].toInt();
    }
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) {
        *error = QStringLiteral("Invalid HDR dimensions.");
        return false;
    }
    int cursor = (lineEnd >= 0 ? lineEnd + 1 : useRes);
    image->width = width;
    image->height = height;
    image->rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    std::vector<unsigned char> scan(static_cast<size_t>(width) * 4u);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    for (int y = 0; y < height; ++y) {
        if (!readRadianceScanline(data, bytes.size(), &cursor, width, scan.data())) {
            *error = QStringLiteral("HDR scanline decode failed.");
            return false;
        }
        for (int x = 0; x < width; ++x) {
            float rgb[3];
            rgbeToRgb(scan.data() + x * 4, rgb);
            const size_t i = static_cast<size_t>((y * width + x) * 3);
            image->rgb[i] = rgb[0];
            image->rgb[i + 1] = rgb[1];
            image->rgb[i + 2] = rgb[2];
        }
    }
    return true;
}

void bakeCubemap(HdriImage* image, int cubeSize) {
    cubeSize = std::max(64, cubeSize);
    image->cubeSize = cubeSize;
    for (int face = 0; face < 6; ++face) {
        image->cube[face].resize(static_cast<size_t>(cubeSize) * static_cast<size_t>(cubeSize) * 4u);
        for (int y = 0; y < cubeSize; ++y) {
            const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(cubeSize) - 1.0f;
            for (int x = 0; x < cubeSize; ++x) {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(cubeSize) - 1.0f;
                QVector3D dir;
                cubemapFaceDirection(face, u, v, &dir);
                const QVector3D c = sanitizeHdr(image->sampleDir(dir));
                const size_t i = static_cast<size_t>((y * cubeSize + x) * 4);
                image->cube[face][i] = c.x();
                image->cube[face][i + 1] = c.y();
                image->cube[face][i + 2] = c.z();
                image->cube[face][i + 3] = 1.0f;
            }
        }
    }
    image->rgb.clear();
    image->rgb.shrink_to_fit();
    image->width = 0;
    image->height = 0;
}

HdriImage loadHdri(const QString& path, int cubeSize) {
    HdriImage image;
    image.name = QFileInfo(path).completeBaseName().replace(QLatin1Char('_'), QLatin1Char(' '));
    image.path = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        image.error = QStringLiteral("Could not open %1").arg(path);
        return image;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        image.error = QStringLiteral("HDRI file is empty.");
        return image;
    }
    const bool wantHdr = path.endsWith(QLatin1String(".hdr"), Qt::CaseInsensitive) ||
                         bytes.startsWith("#?RADIANCE") || bytes.startsWith("#?RGBE");
    if (wantHdr) {
        QString error;
        if (!loadRadianceHdr(bytes, &image, &error)) {
            image.error = error;
            return image;
        }
        bakeCubemap(&image, cubeSize);
        image.path = path;
        return image;
    }
    return loadExrHdri(path, cubeSize);
}
