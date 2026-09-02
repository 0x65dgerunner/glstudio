#include "glb_loader.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QUrl>
#include <QVector2D>
#include <QtEndian>

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr quint32 kGlbMagic = 0x46546C67;
constexpr quint32 kChunkJson = 0x4E4F534A;
constexpr quint32 kChunkBin = 0x004E4942;

int componentBytes(int componentType) {
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

int typeComponentCount(const QString& type) {
    if (type == QLatin1String("SCALAR")) {
        return 1;
    }
    if (type == QLatin1String("VEC2")) {
        return 2;
    }
    if (type == QLatin1String("VEC3")) {
        return 3;
    }
    if (type == QLatin1String("VEC4")) {
        return 4;
    }
    if (type == QLatin1String("MAT4")) {
        return 16;
    }
    return 0;
}

float readComponent(const char* src, int componentType, bool normalized) {
    switch (componentType) {
    case 5120: {
        const qint8 v = static_cast<qint8>(*src);
        return normalized ? std::max(v / 127.0f, -1.0f) : static_cast<float>(v);
    }
    case 5121: {
        const quint8 v = static_cast<quint8>(*src);
        return normalized ? v / 255.0f : static_cast<float>(v);
    }
    case 5122: {
        qint16 v = 0;
        std::memcpy(&v, src, 2);
        v = qFromLittleEndian(v);
        return normalized ? std::max(v / 32767.0f, -1.0f) : static_cast<float>(v);
    }
    case 5123: {
        quint16 v = 0;
        std::memcpy(&v, src, 2);
        v = qFromLittleEndian(v);
        return normalized ? v / 65535.0f : static_cast<float>(v);
    }
    case 5125: {
        quint32 v = 0;
        std::memcpy(&v, src, 4);
        v = qFromLittleEndian(v);
        return static_cast<float>(v);
    }
    case 5126: {
        quint32 bits = 0;
        std::memcpy(&bits, src, 4);
        bits = qFromLittleEndian(bits);
        float v = 0.0f;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    default:
        return 0.0f;
    }
}

std::vector<QByteArray> gDecodedViews;

bool bufferViewRange(const std::vector<QByteArray>& buffers, const QJsonArray& bufferViews, int viewIndex,
                     int extraOffset, int tight, int count, const char** base, int* strideOut, QString* error) {
    if (viewIndex < 0 || viewIndex >= bufferViews.size()) {
        *error = QStringLiteral("Buffer view out of range.");
        return false;
    }
    const QJsonObject view = bufferViews.at(viewIndex).toObject();
    int stride = view.value(QStringLiteral("byteStride")).toInt(0);
    if (stride <= 0) {
        stride = tight;
    }
    if (viewIndex < static_cast<int>(gDecodedViews.size()) && !gDecodedViews[static_cast<size_t>(viewIndex)].isEmpty()) {
        const QByteArray& bin = gDecodedViews[static_cast<size_t>(viewIndex)];
        const int start = extraOffset;
        if (start < 0 || count <= 0 || start + (count - 1) * stride + tight > bin.size()) {
            *error = QStringLiteral("Accessor data exceeds buffer.");
            return false;
        }
        *base = bin.constData() + start;
        *strideOut = stride;
        return true;
    }
    const int bufferIndex = view.value(QStringLiteral("buffer")).toInt(0);
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) {
        *error = QStringLiteral("Buffer index out of range.");
        return false;
    }
    const QByteArray& bin = buffers[static_cast<size_t>(bufferIndex)];
    const int viewOffset = view.value(QStringLiteral("byteOffset")).toInt(0);
    const int start = viewOffset + extraOffset;
    if (start < 0 || count <= 0 || start + (count - 1) * stride + tight > bin.size()) {
        *error = QStringLiteral("Accessor data exceeds buffer.");
        return false;
    }
    *base = bin.constData() + start;
    *strideOut = stride;
    return true;
}

void applySparseFloats(const QJsonObject& acc, const QJsonArray& bufferViews,
                       const std::vector<QByteArray>& buffers, int comps, std::vector<float>* out) {
    const QJsonObject sparse = acc.value(QStringLiteral("sparse")).toObject();
    if (sparse.isEmpty() || out == nullptr) {
        return;
    }
    const int sparseCount = sparse.value(QStringLiteral("count")).toInt();
    const QJsonObject indices = sparse.value(QStringLiteral("indices")).toObject();
    const QJsonObject values = sparse.value(QStringLiteral("values")).toObject();
    const int indexView = indices.value(QStringLiteral("bufferView")).toInt(-1);
    const int valueView = values.value(QStringLiteral("bufferView")).toInt(-1);
    const int indexType = indices.value(QStringLiteral("componentType")).toInt();
    const int indexBytes = componentBytes(indexType);
    const int valueType = acc.value(QStringLiteral("componentType")).toInt();
    const bool normalized = acc.value(QStringLiteral("normalized")).toBool(false);
    const int valueBytes = componentBytes(valueType);
    if (sparseCount <= 0 || indexBytes <= 0 || valueBytes <= 0) {
        return;
    }
    QString ignored;
    const char* indexBase = nullptr;
    const char* valueBase = nullptr;
    int indexStride = indexBytes;
    int valueStride = comps * valueBytes;
    if (!bufferViewRange(buffers, bufferViews, indexView, indices.value(QStringLiteral("byteOffset")).toInt(0),
                         indexBytes, sparseCount, &indexBase, &indexStride, &ignored) ||
        !bufferViewRange(buffers, bufferViews, valueView, values.value(QStringLiteral("byteOffset")).toInt(0),
                         comps * valueBytes, sparseCount, &valueBase, &valueStride, &ignored)) {
        return;
    }
    const int count = static_cast<int>(out->size() / static_cast<size_t>(std::max(1, comps)));
    for (int i = 0; i < sparseCount; ++i) {
        const int index =
            static_cast<int>(readComponent(indexBase + i * indexStride, indexType, false));
        if (index < 0 || index >= count) {
            continue;
        }
        const char* src = valueBase + i * valueStride;
        for (int c = 0; c < comps; ++c) {
            (*out)[static_cast<size_t>(index * comps + c)] =
                readComponent(src + c * valueBytes, valueType, normalized);
        }
    }
}

bool readAccessorFloats(const QJsonArray& accessors, const QJsonArray& bufferViews,
                        const std::vector<QByteArray>& buffers, int accessorIndex, int expectedComponents,
                        std::vector<float>* out, QString* error) {
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        *error = QStringLiteral("Accessor index out of range.");
        return false;
    }
    const QJsonObject acc = accessors.at(accessorIndex).toObject();
    const int bufferView = acc.value(QStringLiteral("bufferView")).toInt(-1);
    const int count = acc.value(QStringLiteral("count")).toInt();
    const int componentType = acc.value(QStringLiteral("componentType")).toInt();
    const QString type = acc.value(QStringLiteral("type")).toString();
    const bool normalized = acc.value(QStringLiteral("normalized")).toBool(false);
    const int comps = typeComponentCount(type);
    const int elemBytes = componentBytes(componentType);
    if (count <= 0 || comps != expectedComponents || elemBytes <= 0) {
        *error = QStringLiteral("Unsupported or invalid accessor.");
        return false;
    }

    out->assign(static_cast<size_t>(count) * static_cast<size_t>(comps), 0.0f);
    if (bufferView >= 0) {
        const char* base = nullptr;
        int stride = 0;
        if (!bufferViewRange(buffers, bufferViews, bufferView, acc.value(QStringLiteral("byteOffset")).toInt(0),
                             comps * elemBytes, count, &base, &stride, error)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            const char* src = base + i * stride;
            for (int c = 0; c < comps; ++c) {
                (*out)[static_cast<size_t>(i * comps + c)] =
                    readComponent(src + c * elemBytes, componentType, normalized);
            }
        }
    }
    applySparseFloats(acc, bufferViews, buffers, comps, out);
    return true;
}

bool readAccessorIndices(const QJsonArray& accessors, const QJsonArray& bufferViews,
                         const std::vector<QByteArray>& buffers, int accessorIndex,
                         std::vector<std::uint32_t>* out, QString* error) {
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        *error = QStringLiteral("Index accessor out of range.");
        return false;
    }
    const QJsonObject acc = accessors.at(accessorIndex).toObject();
    const int bufferView = acc.value(QStringLiteral("bufferView")).toInt(-1);
    const int count = acc.value(QStringLiteral("count")).toInt();
    const int componentType = acc.value(QStringLiteral("componentType")).toInt();
    const int elemBytes = componentBytes(componentType);
    if (count <= 0 || (componentType != 5121 && componentType != 5123 && componentType != 5125) ||
        elemBytes <= 0) {
        *error = QStringLiteral("Unsupported index accessor.");
        return false;
    }

    out->assign(static_cast<size_t>(count), 0);
    if (bufferView >= 0) {
        const char* base = nullptr;
        int stride = 0;
        if (!bufferViewRange(buffers, bufferViews, bufferView, acc.value(QStringLiteral("byteOffset")).toInt(0),
                             elemBytes, count, &base, &stride, error)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            (*out)[static_cast<size_t>(i)] =
                static_cast<std::uint32_t>(readComponent(base + i * stride, componentType, false));
        }
    }
    return true;
}

QMatrix4x4 fromGltfColumnMajor(const float v[16]) {
    return QMatrix4x4(v[0], v[4], v[8], v[12], v[1], v[5], v[9], v[13], v[2], v[6], v[10], v[14],
                      v[3], v[7], v[11], v[15]);
}

QMatrix4x4 nodeMatrix(const QJsonObject& node) {
    if (node.contains(QStringLiteral("matrix"))) {
        const QJsonArray m = node.value(QStringLiteral("matrix")).toArray();
        if (m.size() == 16) {
            float v[16];
            for (int i = 0; i < 16; ++i) {
                v[i] = static_cast<float>(m.at(i).toDouble());
            }
            return fromGltfColumnMajor(v);
        }
    }

    QVector3D translation(0, 0, 0);
    if (node.contains(QStringLiteral("translation"))) {
        const QJsonArray t = node.value(QStringLiteral("translation")).toArray();
        if (t.size() >= 3) {
            translation = QVector3D(static_cast<float>(t.at(0).toDouble()),
                                    static_cast<float>(t.at(1).toDouble()),
                                    static_cast<float>(t.at(2).toDouble()));
        }
    }
    QQuaternion rotation;
    if (node.contains(QStringLiteral("rotation"))) {
        const QJsonArray r = node.value(QStringLiteral("rotation")).toArray();
        if (r.size() >= 4) {
            rotation = QQuaternion(static_cast<float>(r.at(3).toDouble()),
                                   static_cast<float>(r.at(0).toDouble()),
                                   static_cast<float>(r.at(1).toDouble()),
                                   static_cast<float>(r.at(2).toDouble()));
        }
    }
    QVector3D scale(1, 1, 1);
    if (node.contains(QStringLiteral("scale"))) {
        const QJsonArray s = node.value(QStringLiteral("scale")).toArray();
        if (s.size() >= 3) {
            scale = QVector3D(static_cast<float>(s.at(0).toDouble()),
                              static_cast<float>(s.at(1).toDouble()),
                              static_cast<float>(s.at(2).toDouble()));
        }
    }
    QMatrix4x4 local;
    local.translate(translation);
    local.rotate(rotation);
    local.scale(scale);
    return local;
}

void expandBounds(LoadedModel* model, const QVector3D& p) {
    if (!model->hasBounds) {
        model->min = p;
        model->max = p;
        model->hasBounds = true;
        return;
    }
    model->min.setX(std::min(model->min.x(), p.x()));
    model->min.setY(std::min(model->min.y(), p.y()));
    model->min.setZ(std::min(model->min.z(), p.z()));
    model->max.setX(std::max(model->max.x(), p.x()));
    model->max.setY(std::max(model->max.y(), p.y()));
    model->max.setZ(std::max(model->max.z(), p.z()));
}

QVector4D readVec4(const QJsonArray& a, const QVector4D& fallback) {
    if (a.size() < 4) {
        return fallback;
    }
    return QVector4D(static_cast<float>(a.at(0).toDouble()), static_cast<float>(a.at(1).toDouble()),
                     static_cast<float>(a.at(2).toDouble()), static_cast<float>(a.at(3).toDouble()));
}

QVector3D readVec3(const QJsonArray& a, const QVector3D& fallback) {
    if (a.size() < 3) {
        return fallback;
    }
    return QVector3D(static_cast<float>(a.at(0).toDouble()), static_cast<float>(a.at(1).toDouble()),
                     static_cast<float>(a.at(2).toDouble()));
}

TextureTransform parseTextureTransform(const QJsonObject& textureInfo) {
    TextureTransform transform;
    transform.texCoord = textureInfo.value(QStringLiteral("texCoord")).toInt(0);
    const QJsonObject ext = textureInfo.value(QStringLiteral("extensions"))
                                .toObject()
                                .value(QStringLiteral("KHR_texture_transform"))
                                .toObject();
    if (ext.isEmpty()) {
        return transform;
    }
    if (ext.contains(QStringLiteral("texCoord"))) {
        transform.texCoord = ext.value(QStringLiteral("texCoord")).toInt(transform.texCoord);
    }
    const QJsonArray offset = ext.value(QStringLiteral("offset")).toArray();
    if (offset.size() >= 2) {
        transform.offset = QVector2D(static_cast<float>(offset.at(0).toDouble()),
                                     static_cast<float>(offset.at(1).toDouble()));
    }
    const QJsonArray scale = ext.value(QStringLiteral("scale")).toArray();
    if (scale.size() >= 2) {
        transform.scale = QVector2D(static_cast<float>(scale.at(0).toDouble()),
                                    static_cast<float>(scale.at(1).toDouble()));
    }
    transform.rotation = static_cast<float>(ext.value(QStringLiteral("rotation")).toDouble(0.0));
    return transform;
}

int parseTextureIndex(const QJsonObject& textureInfo, TextureTransform* transform) {
    if (!textureInfo.contains(QStringLiteral("index"))) {
        return -1;
    }
    if (transform != nullptr) {
        *transform = parseTextureTransform(textureInfo);
    }
    return textureInfo.value(QStringLiteral("index")).toInt(-1);
}

PbrMaterial parseMaterial(const QJsonObject& json) {
    PbrMaterial mat;
    mat.name = json.value(QStringLiteral("name")).toString();
    mat.doubleSided = json.value(QStringLiteral("doubleSided")).toBool(false);
    const QString alphaMode = json.value(QStringLiteral("alphaMode")).toString();
    mat.blend = alphaMode == QLatin1String("BLEND");
    if (alphaMode == QLatin1String("MASK")) {
        mat.alphaCutoff = static_cast<float>(json.value(QStringLiteral("alphaCutoff")).toDouble(0.5));
    }
    mat.emissive = readVec3(json.value(QStringLiteral("emissiveFactor")).toArray(), mat.emissive);
    mat.gltfMaps = true;

    const QJsonObject pbr = json.value(QStringLiteral("pbrMetallicRoughness")).toObject();
    mat.baseColor = readVec4(pbr.value(QStringLiteral("baseColorFactor")).toArray(), mat.baseColor);
    if (pbr.contains(QStringLiteral("metallicFactor"))) {
        mat.metallic = static_cast<float>(pbr.value(QStringLiteral("metallicFactor")).toDouble());
    }
    if (pbr.contains(QStringLiteral("roughnessFactor"))) {
        mat.roughness = static_cast<float>(pbr.value(QStringLiteral("roughnessFactor")).toDouble());
    }
    mat.baseColorTexture =
        parseTextureIndex(pbr.value(QStringLiteral("baseColorTexture")).toObject(), &mat.albedoUv);
    mat.mapsTexture =
        parseTextureIndex(pbr.value(QStringLiteral("metallicRoughnessTexture")).toObject(), &mat.mapsUv);
    mat.normalTexture = parseTextureIndex(json.value(QStringLiteral("normalTexture")).toObject(), &mat.normalUv);
    mat.emissiveTexture =
        parseTextureIndex(json.value(QStringLiteral("emissiveTexture")).toObject(), &mat.emissiveUv);
    mat.occlusionTexture =
        parseTextureIndex(json.value(QStringLiteral("occlusionTexture")).toObject(), &mat.occlusionUv);
    if (mat.mapsTexture < 0) {
        mat.mapsTexture = mat.occlusionTexture;
        mat.mapsUv = mat.occlusionUv;
    }

    const QJsonObject ext = json.value(QStringLiteral("extensions")).toObject();
    if (ext.contains(QStringLiteral("KHR_materials_unlit"))) {
        mat.unlit = true;
    }
    const QJsonObject coat = ext.value(QStringLiteral("KHR_materials_clearcoat")).toObject();
    if (!coat.isEmpty()) {
        mat.clearcoat = static_cast<float>(coat.value(QStringLiteral("clearcoatFactor")).toDouble());
        mat.clearcoatRoughness =
            static_cast<float>(coat.value(QStringLiteral("clearcoatRoughnessFactor")).toDouble(0.0));
        mat.clearcoatTexture =
            parseTextureIndex(coat.value(QStringLiteral("clearcoatTexture")).toObject(), &mat.clearcoatUv);
        mat.clearcoatRoughnessTexture = parseTextureIndex(
            coat.value(QStringLiteral("clearcoatRoughnessTexture")).toObject(), &mat.clearcoatRoughUv);
        mat.clearcoatNormalTexture = parseTextureIndex(
            coat.value(QStringLiteral("clearcoatNormalTexture")).toObject(), &mat.clearcoatNormalUv);
    }
    const QJsonObject trans = ext.value(QStringLiteral("KHR_materials_transmission")).toObject();
    if (!trans.isEmpty()) {
        mat.transmission = static_cast<float>(trans.value(QStringLiteral("transmissionFactor")).toDouble());
        mat.transmissionTexture =
            parseTextureIndex(trans.value(QStringLiteral("transmissionTexture")).toObject(), &mat.transmissionUv);
        mat.blend = true;
    }
    const QJsonObject ior = ext.value(QStringLiteral("KHR_materials_ior")).toObject();
    if (!ior.isEmpty()) {
        mat.ior = static_cast<float>(ior.value(QStringLiteral("ior")).toDouble(1.5));
    }
    const QJsonObject volume = ext.value(QStringLiteral("KHR_materials_volume")).toObject();
    if (!volume.isEmpty()) {
        mat.thickness = static_cast<float>(volume.value(QStringLiteral("thicknessFactor")).toDouble());
        mat.thicknessTexture =
            parseTextureIndex(volume.value(QStringLiteral("thicknessTexture")).toObject(), &mat.thicknessUv);
        mat.attenuationColor =
            readVec3(volume.value(QStringLiteral("attenuationColor")).toArray(), mat.attenuationColor);
        if (volume.contains(QStringLiteral("attenuationDistance"))) {
            mat.attenuationDistance =
                static_cast<float>(volume.value(QStringLiteral("attenuationDistance")).toDouble());
        }
    }
    const QJsonObject irid = ext.value(QStringLiteral("KHR_materials_iridescence")).toObject();
    if (!irid.isEmpty()) {
        mat.iridescence = static_cast<float>(irid.value(QStringLiteral("iridescenceFactor")).toDouble());
        mat.iridescenceIor = static_cast<float>(irid.value(QStringLiteral("iridescenceIor")).toDouble(1.3));
        mat.iridescenceThicknessMin =
            static_cast<float>(irid.value(QStringLiteral("iridescenceThicknessMinimum")).toDouble(100.0));
        mat.iridescenceThicknessMax =
            static_cast<float>(irid.value(QStringLiteral("iridescenceThicknessMaximum")).toDouble(400.0));
        mat.iridescenceTexture =
            parseTextureIndex(irid.value(QStringLiteral("iridescenceTexture")).toObject(), &mat.iridescenceUv);
        mat.iridescenceThicknessTexture = parseTextureIndex(
            irid.value(QStringLiteral("iridescenceThicknessTexture")).toObject(), &mat.iridescenceThicknessUv);
    }
    const QJsonObject sheen = ext.value(QStringLiteral("KHR_materials_sheen")).toObject();
    if (!sheen.isEmpty()) {
        mat.sheenColor = readVec3(sheen.value(QStringLiteral("sheenColorFactor")).toArray(), mat.sheenColor);
        mat.sheenRoughness = static_cast<float>(sheen.value(QStringLiteral("sheenRoughnessFactor")).toDouble(0.0));
        mat.sheenColorTexture =
            parseTextureIndex(sheen.value(QStringLiteral("sheenColorTexture")).toObject(), &mat.sheenColorUv);
        mat.sheenRoughnessTexture =
            parseTextureIndex(sheen.value(QStringLiteral("sheenRoughnessTexture")).toObject(), &mat.sheenRoughUv);
    }
    const QJsonObject emitExt = ext.value(QStringLiteral("KHR_materials_emissive_strength")).toObject();
    if (!emitExt.isEmpty()) {
        mat.emissiveStrength = static_cast<float>(emitExt.value(QStringLiteral("emissiveStrength")).toDouble(1.0));
    }
    return mat;
}

void fillNodeTransform(SceneNode* node, const QJsonObject& json) {
    node->rest = nodeMatrix(json);
    node->hasTrs = !json.contains(QStringLiteral("matrix"));
    if (json.contains(QStringLiteral("translation"))) {
        node->translation = readVec3(json.value(QStringLiteral("translation")).toArray(), node->translation);
        node->hasTrs = true;
    }
    if (json.contains(QStringLiteral("rotation"))) {
        const QJsonArray r = json.value(QStringLiteral("rotation")).toArray();
        if (r.size() >= 4) {
            node->rotation = QQuaternion(static_cast<float>(r.at(3).toDouble()),
                                         static_cast<float>(r.at(0).toDouble()),
                                         static_cast<float>(r.at(1).toDouble()),
                                         static_cast<float>(r.at(2).toDouble()));
            node->hasTrs = true;
        }
    }
    if (json.contains(QStringLiteral("scale"))) {
        node->scale = readVec3(json.value(QStringLiteral("scale")).toArray(), node->scale);
        node->hasTrs = true;
    }
    if (!node->hasTrs) {
        node->rest = nodeMatrix(json);
    }
}

void computeTangents(MeshPart* part) {
    const size_t vcount = part->vertices.size() / static_cast<size_t>(kVertexFloats);
    if (vcount == 0) {
        return;
    }
    std::vector<QVector3D> acc(vcount);
    auto vtx = [&](size_t i) -> float* { return part->vertices.data() + i * static_cast<size_t>(kVertexFloats); };
    for (size_t i = 0; i + 2 < part->indices.size(); i += 3) {
        const std::uint32_t i0 = part->indices[i];
        const std::uint32_t i1 = part->indices[i + 1];
        const std::uint32_t i2 = part->indices[i + 2];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) {
            continue;
        }
        const QVector3D p0(vtx(i0)[0], vtx(i0)[1], vtx(i0)[2]);
        const QVector3D p1(vtx(i1)[0], vtx(i1)[1], vtx(i1)[2]);
        const QVector3D p2(vtx(i2)[0], vtx(i2)[1], vtx(i2)[2]);
        const QVector2D uv0(vtx(i0)[9], vtx(i0)[10]);
        const QVector2D uv1(vtx(i1)[9], vtx(i1)[10]);
        const QVector2D uv2(vtx(i2)[9], vtx(i2)[10]);
        const QVector3D e1 = p1 - p0;
        const QVector3D e2 = p2 - p0;
        const float du1 = uv1.x() - uv0.x();
        const float dv1 = uv1.y() - uv0.y();
        const float du2 = uv2.x() - uv0.x();
        const float dv2 = uv2.y() - uv0.y();
        const float det = du1 * dv2 - du2 * dv1;
        QVector3D t = std::fabs(det) < 1e-8f ? e1 : (e1 * dv2 - e2 * dv1) / det;
        acc[i0] += t;
        acc[i1] += t;
        acc[i2] += t;
    }
    for (size_t i = 0; i < vcount; ++i) {
        float* v = vtx(i);
        const QVector3D n(v[3], v[4], v[5]);
        QVector3D t = acc[i];
        if (t.lengthSquared() < 1e-10f) {
            t = std::fabs(n.y()) < 0.99f ? QVector3D::crossProduct(n, QVector3D(0, 1, 0))
                                         : QVector3D::crossProduct(n, QVector3D(1, 0, 0));
        } else {
            t = t - n * QVector3D::dotProduct(n, t);
        }
        if (t.isNull()) {
            t = QVector3D(1, 0, 0);
        }
        t.normalize();
        v[6] = t.x();
        v[7] = t.y();
        v[8] = t.z();
        v[11] = 1.0f;
    }
}

void appendPrimitive(LoadedModel* model, const QJsonObject& primitive, const QJsonArray& accessors,
                     const QJsonArray& bufferViews, const std::vector<QByteArray>& buffers, int nodeIndex, int meshIndex,
                     int primitiveIndex, const QString& meshName, int variantCount, QString* error) {
    if (primitive.value(QStringLiteral("extensions")).toObject().contains(QStringLiteral("KHR_draco_mesh_compression"))) {
        return;
    }
    const QJsonObject attributes = primitive.value(QStringLiteral("attributes")).toObject();
    if (!attributes.contains(QStringLiteral("POSITION"))) {
        return;
    }

    std::vector<float> positions;
    if (!readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("POSITION")).toInt(),
                            3, &positions, error)) {
        return;
    }

    std::vector<float> normals;
    if (attributes.contains(QStringLiteral("NORMAL"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("NORMAL")).toInt(), 3,
                           &normals, &ignored);
    }

    std::vector<float> uvs;
    if (attributes.contains(QStringLiteral("TEXCOORD_0"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("TEXCOORD_0")).toInt(),
                           2, &uvs, &ignored);
    }
    std::vector<float> uv1;
    if (attributes.contains(QStringLiteral("TEXCOORD_1"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("TEXCOORD_1")).toInt(),
                           2, &uv1, &ignored);
    }

    std::vector<float> tangents;
    if (attributes.contains(QStringLiteral("TANGENT"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("TANGENT")).toInt(), 4,
                           &tangents, &ignored);
    }

    std::vector<float> joints;
    if (attributes.contains(QStringLiteral("JOINTS_0"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("JOINTS_0")).toInt(), 4,
                           &joints, &ignored);
    }
    std::vector<float> weights;
    if (attributes.contains(QStringLiteral("WEIGHTS_0"))) {
        QString ignored;
        readAccessorFloats(accessors, bufferViews, buffers, attributes.value(QStringLiteral("WEIGHTS_0")).toInt(), 4,
                           &weights, &ignored);
    }

    std::vector<std::uint32_t> indices;
    if (primitive.contains(QStringLiteral("indices"))) {
        if (!readAccessorIndices(accessors, bufferViews, buffers, primitive.value(QStringLiteral("indices")).toInt(),
                                 &indices, error)) {
            return;
        }
    } else {
        const std::uint32_t count = static_cast<std::uint32_t>(positions.size() / 3);
        indices.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            indices[i] = i;
        }
    }

    const int mode = primitive.value(QStringLiteral("mode")).toInt(4);
    std::vector<std::uint32_t> triangles;
    if (mode == 4) {
        triangles = std::move(indices);
    } else if (mode == 5) {
        for (size_t i = 0; i + 2 < indices.size(); ++i) {
            if (i % 2 == 0) {
                triangles.push_back(indices[i]);
                triangles.push_back(indices[i + 1]);
                triangles.push_back(indices[i + 2]);
            } else {
                triangles.push_back(indices[i + 1]);
                triangles.push_back(indices[i]);
                triangles.push_back(indices[i + 2]);
            }
        }
    } else {
        return;
    }

    MeshPart part;
    part.name = meshName;
    part.material = primitive.value(QStringLiteral("material")).toInt(0);
    part.defaultMaterial = part.material;
    part.node = nodeIndex;
    part.meshIndex = meshIndex;
    part.primitiveIndex = primitiveIndex;
    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(model->nodes.size())) {
        part.skin = model->nodes[static_cast<size_t>(nodeIndex)].skin;
    }
    const size_t vertexCount = positions.size() / 3;
    const bool hasNormals = normals.size() == positions.size();
    const bool hasUvs = uvs.size() == vertexCount * 2;
    const bool hasUv1 = uv1.size() == vertexCount * 2;
    const bool hasTangents = tangents.size() == vertexCount * 4;
    part.vertices.reserve(vertexCount * static_cast<size_t>(kVertexFloats));
    for (size_t i = 0; i < vertexCount; ++i) {
        const QVector3D p(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        QVector3D n(0, 1, 0);
        if (hasNormals) {
            n = QVector3D(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
            if (n.isNull()) {
                n = QVector3D(0, 1, 0);
            } else {
                n.normalize();
            }
        }
        QVector3D t(1, 0, 0);
        if (hasTangents) {
            t = QVector3D(tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2]);
            if (t.isNull()) {
                t = QVector3D(1, 0, 0);
            } else {
                t.normalize();
            }
        }
        part.vertices.push_back(p.x());
        part.vertices.push_back(p.y());
        part.vertices.push_back(p.z());
        part.vertices.push_back(n.x());
        part.vertices.push_back(n.y());
        part.vertices.push_back(n.z());
        part.vertices.push_back(t.x());
        part.vertices.push_back(t.y());
        part.vertices.push_back(t.z());
        part.vertices.push_back(hasUvs ? uvs[i * 2] : 0.0f);
        part.vertices.push_back(hasUvs ? uvs[i * 2 + 1] : 0.0f);
        part.vertices.push_back(hasTangents ? tangents[i * 4 + 3] : 1.0f);
        part.vertices.push_back(hasUv1 ? uv1[i * 2] : (hasUvs ? uvs[i * 2] : 0.0f));
        part.vertices.push_back(hasUv1 ? uv1[i * 2 + 1] : (hasUvs ? uvs[i * 2 + 1] : 0.0f));
    }
    if (joints.size() == vertexCount * 4 && weights.size() == vertexCount * 4) {
        part.joints = std::move(joints);
        part.weights = std::move(weights);
    }
    const QJsonArray targets = primitive.value(QStringLiteral("targets")).toArray();
    for (const QJsonValue& targetValue : targets) {
        const QJsonObject target = targetValue.toObject();
        MorphTarget morph;
        QString ignored;
        if (target.contains(QStringLiteral("POSITION"))) {
            readAccessorFloats(accessors, bufferViews, buffers, target.value(QStringLiteral("POSITION")).toInt(), 3,
                               &morph.positions, &ignored);
        }
        if (target.contains(QStringLiteral("NORMAL"))) {
            readAccessorFloats(accessors, bufferViews, buffers, target.value(QStringLiteral("NORMAL")).toInt(), 3,
                               &morph.normals, &ignored);
        }
        if (!morph.positions.empty()) {
            part.morphs.push_back(std::move(morph));
        }
    }
    if (variantCount > 0) {
        part.variantMaterials.assign(static_cast<size_t>(variantCount), -1);
        const QJsonArray mappings = primitive.value(QStringLiteral("extensions"))
                                        .toObject()
                                        .value(QStringLiteral("KHR_materials_variants"))
                                        .toObject()
                                        .value(QStringLiteral("mappings"))
                                        .toArray();
        for (const QJsonValue& mappingValue : mappings) {
            const QJsonObject mapping = mappingValue.toObject();
            const int material = mapping.value(QStringLiteral("material")).toInt(-1);
            const QJsonArray variants = mapping.value(QStringLiteral("variants")).toArray();
            for (const QJsonValue& variant : variants) {
                const int index = variant.toInt(-1);
                if (index >= 0 && index < variantCount) {
                    part.variantMaterials[static_cast<size_t>(index)] = material;
                }
            }
        }
    }
    part.indices = std::move(triangles);
    if (!hasTangents) {
        computeTangents(&part);
    }
    if (!part.indices.empty()) {
        model->parts.push_back(std::move(part));
    }
}

void appendMeshForNode(LoadedModel* model, const QJsonArray& meshes, const QJsonArray& accessors,
                       const QJsonArray& bufferViews, const std::vector<QByteArray>& buffers, int nodeIndex, int variantCount,
                       QString* error) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model->nodes.size())) {
        return;
    }
    const int meshIndex = model->nodes[static_cast<size_t>(nodeIndex)].mesh;
    if (meshIndex < 0 || meshIndex >= meshes.size()) {
        return;
    }
    const QJsonObject mesh = meshes.at(meshIndex).toObject();
    QString meshName = mesh.value(QStringLiteral("name")).toString();
    if (meshName.isEmpty()) {
        meshName = model->nodes[static_cast<size_t>(nodeIndex)].name;
    }
    const QJsonArray primitives = mesh.value(QStringLiteral("primitives")).toArray();
    int primitiveIndex = 0;
    for (const QJsonValue& value : primitives) {
        appendPrimitive(model, value.toObject(), accessors, bufferViews, buffers, nodeIndex, meshIndex, primitiveIndex++,
                        meshName, variantCount, error);
        if (!error->isEmpty()) {
            return;
        }
    }
}

AnimInterp parseInterp(const QString& name) {
    if (name == QLatin1String("STEP")) {
        return AnimInterp::Step;
    }
    if (name == QLatin1String("CUBICSPLINE")) {
        return AnimInterp::Cubic;
    }
    return AnimInterp::Linear;
}

void parseAnimations(LoadedModel* model, const QJsonArray& animations, const QJsonArray& accessors,
                     const QJsonArray& bufferViews, const std::vector<QByteArray>& buffers) {
    for (const QJsonValue& value : animations) {
        const QJsonObject json = value.toObject();
        AnimClip clip;
        clip.name = json.value(QStringLiteral("name")).toString();
        if (clip.name.isEmpty()) {
            clip.name = QStringLiteral("Clip %1").arg(model->animations.size() + 1);
        }
        clip.label = clip.name;
        const QJsonArray samplersJson = json.value(QStringLiteral("samplers")).toArray();
        const QJsonArray channelsJson = json.value(QStringLiteral("channels")).toArray();
        std::vector<AnimSampler> samplers;
        samplers.reserve(static_cast<size_t>(samplersJson.size()));
        for (const QJsonValue& samplerValue : samplersJson) {
            const QJsonObject samplerJson = samplerValue.toObject();
            AnimSampler sampler;
            sampler.interpolation = parseInterp(samplerJson.value(QStringLiteral("interpolation")).toString());
            QString ignored;
            readAccessorFloats(accessors, bufferViews, buffers, samplerJson.value(QStringLiteral("input")).toInt(-1), 1,
                               &sampler.times, &ignored);
            const int output = samplerJson.value(QStringLiteral("output")).toInt(-1);
            if (output >= 0 && output < accessors.size()) {
                const QString type = accessors.at(output).toObject().value(QStringLiteral("type")).toString();
                sampler.components = type == QLatin1String("VEC4") ? 4 : type == QLatin1String("SCALAR") ? 1 : 3;
                readAccessorFloats(accessors, bufferViews, buffers, output, sampler.components, &sampler.values, &ignored);
            }
            if (!sampler.times.empty()) {
                clip.duration = std::max(clip.duration, sampler.times.back());
            }
            samplers.push_back(std::move(sampler));
        }
        for (const QJsonValue& channelValue : channelsJson) {
            const QJsonObject channelJson = channelValue.toObject();
            const int samplerIndex = channelJson.value(QStringLiteral("sampler")).toInt(-1);
            const QJsonObject target = channelJson.value(QStringLiteral("target")).toObject();
            const int node = target.value(QStringLiteral("node")).toInt(-1);
            if (samplerIndex < 0 || samplerIndex >= static_cast<int>(samplers.size()) || node < 0) {
                continue;
            }
            AnimChannel channel;
            channel.node = node;
            channel.sampler = samplers[static_cast<size_t>(samplerIndex)];
            const QString path = target.value(QStringLiteral("path")).toString();
            if (path == QLatin1String("rotation")) {
                channel.path = AnimPath::Rotation;
            } else if (path == QLatin1String("scale")) {
                channel.path = AnimPath::Scale;
            } else if (path == QLatin1String("weights")) {
                channel.path = AnimPath::Weights;
            } else {
                channel.path = AnimPath::Translation;
            }
            clip.channels.push_back(std::move(channel));
        }
        if (!clip.channels.empty()) {
            model->animations.push_back(std::move(clip));
        }
    }
}

void parseSkins(LoadedModel* model, const QJsonArray& skins, const QJsonArray& accessors,
                const QJsonArray& bufferViews, const std::vector<QByteArray>& buffers) {
    for (const QJsonValue& value : skins) {
        const QJsonObject json = value.toObject();
        GltfSkin skin;
        skin.name = json.value(QStringLiteral("name")).toString();
        skin.skeleton = json.value(QStringLiteral("skeleton")).toInt(-1);
        const QJsonArray joints = json.value(QStringLiteral("joints")).toArray();
        for (const QJsonValue& joint : joints) {
            skin.joints.push_back(joint.toInt(-1));
        }
        const int ibm = json.value(QStringLiteral("inverseBindMatrices")).toInt(-1);
        if (ibm >= 0) {
            std::vector<float> values;
            QString ignored;
            if (readAccessorFloats(accessors, bufferViews, buffers, ibm, 16, &values, &ignored)) {
                for (size_t i = 0; i + 15 < values.size(); i += 16) {
                    float m[16];
                    for (int c = 0; c < 16; ++c) {
                        m[c] = values[i + static_cast<size_t>(c)];
                    }
                    skin.inverseBind.push_back(fromGltfColumnMajor(m));
                }
            }
        }
        model->gltfSkins.push_back(std::move(skin));
    }
}

void parseCameras(LoadedModel* model, const QJsonArray& cameras) {
    for (int i = 0; i < cameras.size(); ++i) {
        const QJsonObject json = cameras.at(i).toObject();
        GltfCamera camera;
        camera.name = json.value(QStringLiteral("name")).toString();
        if (camera.name.isEmpty()) {
            camera.name = QStringLiteral("Camera %1").arg(i + 1);
        }
        const QJsonObject perspective = json.value(QStringLiteral("perspective")).toObject();
        if (!perspective.isEmpty()) {
            camera.perspective = true;
            camera.yfov = static_cast<float>(perspective.value(QStringLiteral("yfov")).toDouble(0.7));
            camera.znear = static_cast<float>(perspective.value(QStringLiteral("znear")).toDouble(0.05));
            camera.zfar = static_cast<float>(perspective.value(QStringLiteral("zfar")).toDouble(120.0));
        } else {
            camera.perspective = false;
        }
        model->cameras.push_back(camera);
    }
}

void parseLights(LoadedModel* model, const QJsonObject& root) {
    const QJsonArray lights = root.value(QStringLiteral("extensions"))
                                  .toObject()
                                  .value(QStringLiteral("KHR_lights_punctual"))
                                  .toObject()
                                  .value(QStringLiteral("lights"))
                                  .toArray();
    for (const QJsonValue& value : lights) {
        const QJsonObject json = value.toObject();
        GltfLight light;
        light.name = json.value(QStringLiteral("name")).toString();
        const QString type = json.value(QStringLiteral("type")).toString();
        light.type = type == QLatin1String("point") ? 1 : type == QLatin1String("spot") ? 2 : 0;
        light.color = readVec3(json.value(QStringLiteral("color")).toArray(), light.color);
        light.intensity = static_cast<float>(json.value(QStringLiteral("intensity")).toDouble(1.0));
        light.range = static_cast<float>(json.value(QStringLiteral("range")).toDouble(0.0));
        const QJsonObject spot = json.value(QStringLiteral("spot")).toObject();
        if (!spot.isEmpty()) {
            light.innerCone = static_cast<float>(spot.value(QStringLiteral("innerConeAngle")).toDouble(0.0));
            light.outerCone = static_cast<float>(spot.value(QStringLiteral("outerConeAngle")).toDouble(0.78539816339));
        }
        model->lights.push_back(light);
    }
}

QByteArray decodeDataUri(const QString& uri) {
    const int comma = uri.indexOf(QLatin1Char(','));
    if (comma < 0) {
        return {};
    }
    const QByteArray payload = uri.mid(comma + 1).toLatin1();
    if (uri.left(comma).contains(QLatin1String("base64"), Qt::CaseInsensitive)) {
        return QByteArray::fromBase64(payload);
    }
    return QByteArray::fromPercentEncoding(payload);
}

QByteArray loadUriBytes(const QString& uri, const QString& baseDir) {
    if (uri.startsWith(QLatin1String("data:"))) {
        return decodeDataUri(uri);
    }
    const QString decoded = QUrl::fromPercentEncoding(uri.toUtf8());
    const QString path = QDir::isAbsolutePath(decoded) ? decoded : QDir(baseDir).filePath(decoded);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray bufferViewBytes(const std::vector<QByteArray>& buffers, const QJsonArray& bufferViews, int viewIndex) {
    if (viewIndex < 0 || viewIndex >= bufferViews.size()) {
        return {};
    }
    if (viewIndex < static_cast<int>(gDecodedViews.size()) && !gDecodedViews[static_cast<size_t>(viewIndex)].isEmpty()) {
        return gDecodedViews[static_cast<size_t>(viewIndex)];
    }
    const QJsonObject view = bufferViews.at(viewIndex).toObject();
    const int bufferIndex = view.value(QStringLiteral("buffer")).toInt(0);
    const int offset = view.value(QStringLiteral("byteOffset")).toInt(0);
    const int length = view.value(QStringLiteral("byteLength")).toInt(0);
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size()) || offset < 0 || length <= 0) {
        return {};
    }
    const QByteArray& bin = buffers[static_cast<size_t>(bufferIndex)];
    if (offset + length > bin.size()) {
        return {};
    }
    return bin.mid(offset, length);
}

void loadImages(LoadedModel* model, const QJsonArray& images, const QJsonArray& bufferViews,
                const std::vector<QByteArray>& buffers, const QString& baseDir) {
    for (const QJsonValue& value : images) {
        const QJsonObject image = value.toObject();
        QByteArray bytes;
        if (image.contains(QStringLiteral("bufferView"))) {
            bytes = bufferViewBytes(buffers, bufferViews, image.value(QStringLiteral("bufferView")).toInt(-1));
        } else if (image.contains(QStringLiteral("uri"))) {
            bytes = loadUriBytes(image.value(QStringLiteral("uri")).toString(), baseDir);
        }
        QImage decoded;
        decoded.loadFromData(bytes);
        if (!decoded.isNull()) {
            decoded = decoded.convertToFormat(QImage::Format_RGBA8888);
        }
        model->imageNames.push_back(image.value(QStringLiteral("name")).toString());
        model->images.push_back(decoded);
    }
}

void decodeMeshoptViews(const QJsonArray& bufferViews, const std::vector<QByteArray>& buffers) {
    gDecodedViews.clear();
    gDecodedViews.resize(static_cast<size_t>(std::max(0, static_cast<int>(bufferViews.size()))));
    for (int i = 0; i < bufferViews.size(); ++i) {
        const QJsonObject ext = bufferViews.at(i)
                                    .toObject()
                                    .value(QStringLiteral("extensions"))
                                    .toObject()
                                    .value(QStringLiteral("EXT_meshopt_compression"))
                                    .toObject();
        if (ext.isEmpty()) {
            continue;
        }
        const int bufferIndex = ext.value(QStringLiteral("buffer")).toInt(0);
        const int byteOffset = ext.value(QStringLiteral("byteOffset")).toInt(0);
        const int byteLength = ext.value(QStringLiteral("byteLength")).toInt(0);
        const int byteStride = ext.value(QStringLiteral("byteStride")).toInt(0);
        const int count = ext.value(QStringLiteral("count")).toInt(0);
        const QString mode = ext.value(QStringLiteral("mode")).toString();
        const QString filter = ext.value(QStringLiteral("filter")).toString();
        if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size()) || count <= 0 || byteStride <= 0 ||
            byteLength <= 0) {
            continue;
        }
        const QByteArray& srcBuf = buffers[static_cast<size_t>(bufferIndex)];
        if (byteOffset < 0 || byteOffset + byteLength > srcBuf.size()) {
            continue;
        }
        QByteArray dst(count * byteStride, 0);
        const unsigned char* src = reinterpret_cast<const unsigned char*>(srcBuf.constData() + byteOffset);
        int ok = -1;
        if (mode == QLatin1String("ATTRIBUTES")) {
            ok = meshopt_decodeVertexBuffer(reinterpret_cast<unsigned char*>(dst.data()), static_cast<size_t>(count),
                                            static_cast<size_t>(byteStride), src, static_cast<size_t>(byteLength));
        } else if (mode == QLatin1String("TRIANGLES")) {
            ok = meshopt_decodeIndexBuffer(reinterpret_cast<unsigned char*>(dst.data()), static_cast<size_t>(count),
                                           static_cast<size_t>(byteStride), src, static_cast<size_t>(byteLength));
        } else if (mode == QLatin1String("INDICES")) {
            ok = meshopt_decodeIndexSequence(reinterpret_cast<unsigned char*>(dst.data()), static_cast<size_t>(count),
                                             static_cast<size_t>(byteStride), src, static_cast<size_t>(byteLength));
        }
        if (ok != 0) {
            continue;
        }
        if (filter == QLatin1String("OCTAHEDRAL")) {
            meshopt_decodeFilterOct(dst.data(), static_cast<size_t>(count), static_cast<size_t>(byteStride));
        } else if (filter == QLatin1String("QUATERNION")) {
            meshopt_decodeFilterQuat(dst.data(), static_cast<size_t>(count), static_cast<size_t>(byteStride));
        } else if (filter == QLatin1String("EXPONENTIAL")) {
            meshopt_decodeFilterExp(dst.data(), static_cast<size_t>(count), static_cast<size_t>(byteStride));
        }
        gDecodedViews[static_cast<size_t>(i)] = std::move(dst);
    }
}

}  // namespace

QString prettyModelName(const QString& fileName) {
    QString pretty = QFileInfo(fileName).completeBaseName();
    pretty.replace(QLatin1Char('_'), QLatin1Char(' '));
    return pretty;
}

QString userModelsDir() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

QString ensureUserModelsDir() {
    const QString path = userModelsDir();
    QDir().mkpath(path);
    return path;
}

QString findBundledModelsDir() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 10; ++i) {
        const QString nested = dir.filePath(QStringLiteral("glstudio/resources/models"));
        if (QDir(nested).exists()) {
            return nested;
        }
        const QString legacy = dir.filePath(QStringLiteral("resources/models"));
        if (QDir(legacy).exists()) {
            return legacy;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString findModelsDir() {
    return ensureUserModelsDir();
}

void collectGlbModels(const QString& dirPath, bool recursive, QSet<QString>* seen,
                      std::vector<ModelEntry>* out) {
    if (dirPath.isEmpty() || seen == nullptr || out == nullptr || !QDir(dirPath).exists()) {
        return;
    }
    QDirIterator it(dirPath, {QStringLiteral("*.glb"), QStringLiteral("*.GLB"), QStringLiteral("*.gltf"),
                              QStringLiteral("*.GLTF")},
                    QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        const QString filePath = QFileInfo(it.filePath()).absoluteFilePath();
        if (filePath.contains(QLatin1String("MustangDarkHorse"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString fileName = it.fileName();
        const QString key = fileName.toLower();
        if (seen->contains(key)) {
            continue;
        }
        seen->insert(key);
        ModelEntry entry;
        entry.name = prettyModelName(fileName);
        entry.path = filePath;
        out->push_back(entry);
    }
}

std::vector<ModelEntry> listPreviewModels() {
    std::vector<ModelEntry> out;
    QSet<QString> seen;
    collectGlbModels(ensureUserModelsDir(), true, &seen, &out);
    collectGlbModels(findBundledModelsDir(), true, &seen, &out);
    return out;
}

QString findModelGlbPath() {
    const std::vector<ModelEntry> models = listPreviewModels();
    const QStringList preferred = {QStringLiteral("F1 2026 - Aston Martin"), QStringLiteral("Aston Martin")};
    for (const QString& key : preferred) {
        for (const ModelEntry& entry : models) {
            if (entry.name.contains(key, Qt::CaseInsensitive) ||
                QFileInfo(entry.path).completeBaseName().contains(key, Qt::CaseInsensitive)) {
                return entry.path;
            }
        }
    }
    return models.empty() ? QString() : models.front().path;
}

LoadedModel loadGlbModel(const QString& path) {
    LoadedModel model;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        model.error = QStringLiteral("Could not open %1").arg(path);
        return model;
    }
    const QByteArray bytes = file.readAll();
    const QString baseDir = QFileInfo(path).absolutePath();
    quint32 magic = 0;
    if (bytes.size() >= 4) {
        std::memcpy(&magic, bytes.constData(), 4);
        magic = qFromLittleEndian(magic);
    }
    const bool looksGlb = magic == kGlbMagic;
    const bool isGltfJson = path.endsWith(QLatin1String(".gltf"), Qt::CaseInsensitive);

    QByteArray json;
    QByteArray glbBin;
    if (looksGlb && !isGltfJson) {
        if (bytes.size() < 32) {
            model.error = QStringLiteral("File is too small to be a GLB.");
            return model;
        }
        auto readU32 = [&](int offset) -> quint32 {
            quint32 value = 0;
            std::memcpy(&value, bytes.constData() + offset, 4);
            return qFromLittleEndian(value);
        };
        if (readU32(0) != kGlbMagic || readU32(4) != 2) {
            model.error = QStringLiteral("Not a glTF 2.0 binary (.glb) file.");
            return model;
        }
        int offset = 12;
        while (offset + 8 <= bytes.size()) {
            const quint32 chunkLength = readU32(offset);
            const quint32 chunkType = readU32(offset + 4);
            offset += 8;
            if (offset + static_cast<int>(chunkLength) > bytes.size()) {
                model.error = QStringLiteral("GLB chunk is truncated.");
                return model;
            }
            const QByteArray chunk = bytes.mid(offset, static_cast<int>(chunkLength));
            offset += static_cast<int>((chunkLength + 3u) & ~3u);
            if (chunkType == kChunkJson) {
                json = chunk;
            } else if (chunkType == kChunkBin) {
                glbBin = chunk;
            }
        }
    } else {
        json = bytes;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        model.error = QStringLiteral("Invalid glTF JSON: %1").arg(parseError.errorString());
        return model;
    }

    const QJsonObject root = doc.object();
    const QJsonArray bufferDefs = root.value(QStringLiteral("buffers")).toArray();
    std::vector<QByteArray> buffers(static_cast<size_t>(std::max(1, static_cast<int>(bufferDefs.size()))));
    if (!glbBin.isEmpty()) {
        buffers[0] = glbBin;
    }
    for (int i = 0; i < bufferDefs.size(); ++i) {
        const QJsonObject buffer = bufferDefs.at(i).toObject();
        if (i == 0 && !buffers[0].isEmpty() && !buffer.contains(QStringLiteral("uri"))) {
            continue;
        }
        const QString uri = buffer.value(QStringLiteral("uri")).toString();
        if (uri.isEmpty()) {
            continue;
        }
        QByteArray data = loadUriBytes(uri, baseDir);
        if (data.isEmpty()) {
            model.error = QStringLiteral("Could not load buffer %1").arg(uri);
            return model;
        }
        if (i >= static_cast<int>(buffers.size())) {
            buffers.resize(static_cast<size_t>(i + 1));
        }
        buffers[static_cast<size_t>(i)] = std::move(data);
    }

    const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    const QJsonArray meshes = root.value(QStringLiteral("meshes")).toArray();
    const QJsonArray accessors = root.value(QStringLiteral("accessors")).toArray();
    const QJsonArray bufferViews = root.value(QStringLiteral("bufferViews")).toArray();
    const QJsonArray scenes = root.value(QStringLiteral("scenes")).toArray();
    const QJsonArray materials = root.value(QStringLiteral("materials")).toArray();
    const QJsonArray images = root.value(QStringLiteral("images")).toArray();
    const QJsonArray textures = root.value(QStringLiteral("textures")).toArray();
    const QJsonArray animations = root.value(QStringLiteral("animations")).toArray();
    const QJsonArray skins = root.value(QStringLiteral("skins")).toArray();
    const QJsonArray cameras = root.value(QStringLiteral("cameras")).toArray();
    const int sceneIndex = root.value(QStringLiteral("scene")).toInt(0);
    decodeMeshoptViews(bufferViews, buffers);

    const QJsonArray extUsed = root.value(QStringLiteral("extensionsUsed")).toArray();
    const QStringList known = {QStringLiteral("KHR_texture_transform"),
                               QStringLiteral("KHR_materials_clearcoat"),
                               QStringLiteral("KHR_materials_unlit"),
                               QStringLiteral("KHR_materials_transmission"),
                               QStringLiteral("KHR_materials_volume"),
                               QStringLiteral("KHR_materials_ior"),
                               QStringLiteral("KHR_materials_sheen"),
                               QStringLiteral("KHR_materials_emissive_strength"),
                               QStringLiteral("KHR_materials_iridescence"),
                               QStringLiteral("KHR_materials_variants"),
                               QStringLiteral("KHR_lights_punctual"),
                               QStringLiteral("EXT_meshopt_compression"),
                               QStringLiteral("KHR_mesh_quantization")};
    for (const QJsonValue& value : extUsed) {
        const QString name = value.toString();
        if (known.contains(name)) {
            model.usedExtensions.push_back(name);
        } else {
            model.ignoredExtensions.push_back(name);
        }
    }

    const QJsonArray variantDefs = root.value(QStringLiteral("extensions"))
                                       .toObject()
                                       .value(QStringLiteral("KHR_materials_variants"))
                                       .toObject()
                                       .value(QStringLiteral("variants"))
                                       .toArray();
    for (const QJsonValue& value : variantDefs) {
        MaterialVariant variant;
        variant.name = value.toObject().value(QStringLiteral("name")).toString();
        if (variant.name.isEmpty()) {
            variant.name = QStringLiteral("Variant %1").arg(model.variants.size() + 1);
        }
        model.variants.push_back(variant);
    }

    for (const QJsonValue& value : materials) {
        model.materials.push_back(parseMaterial(value.toObject()));
    }
    if (model.materials.empty()) {
        model.materials.push_back(PbrMaterial{});
    }
    loadImages(&model, images, bufferViews, buffers, baseDir);

    auto resolveTex = [&](int textureIndex) -> int {
        if (textureIndex < 0 || textureIndex >= textures.size()) {
            return -1;
        }
        return textures.at(textureIndex).toObject().value(QStringLiteral("source")).toInt(-1);
    };
    for (PbrMaterial& mat : model.materials) {
        mat.baseColorTexture = resolveTex(mat.baseColorTexture);
        mat.normalTexture = resolveTex(mat.normalTexture);
        mat.mapsTexture = resolveTex(mat.mapsTexture);
        mat.emissiveTexture = resolveTex(mat.emissiveTexture);
        mat.occlusionTexture = resolveTex(mat.occlusionTexture);
        mat.clearcoatTexture = resolveTex(mat.clearcoatTexture);
        mat.clearcoatRoughnessTexture = resolveTex(mat.clearcoatRoughnessTexture);
        mat.clearcoatNormalTexture = resolveTex(mat.clearcoatNormalTexture);
        mat.transmissionTexture = resolveTex(mat.transmissionTexture);
        mat.thicknessTexture = resolveTex(mat.thicknessTexture);
        mat.sheenColorTexture = resolveTex(mat.sheenColorTexture);
        mat.sheenRoughnessTexture = resolveTex(mat.sheenRoughnessTexture);
        mat.iridescenceTexture = resolveTex(mat.iridescenceTexture);
        mat.iridescenceThicknessTexture = resolveTex(mat.iridescenceThicknessTexture);
        mat.gltfMaps = true;
    }

    parseSkins(&model, skins, accessors, bufferViews, buffers);
    parseCameras(&model, cameras);
    parseLights(&model, root);

    model.nodes.resize(static_cast<size_t>(nodes.size()));
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject json = nodes.at(i).toObject();
        SceneNode& node = model.nodes[static_cast<size_t>(i)];
        node.name = json.value(QStringLiteral("name")).toString();
        if (node.name.isEmpty()) {
            node.name = QStringLiteral("Node %1").arg(i);
        }
        node.mesh = json.value(QStringLiteral("mesh")).toInt(-1);
        node.skin = json.value(QStringLiteral("skin")).toInt(-1);
        node.camera = json.value(QStringLiteral("camera")).toInt(-1);
        fillNodeTransform(&node, json);
        const QJsonArray weights = json.value(QStringLiteral("weights")).toArray();
        for (const QJsonValue& weight : weights) {
            node.morphWeights.push_back(static_cast<float>(weight.toDouble()));
        }
        if (node.mesh >= 0 && node.mesh < meshes.size() && node.morphWeights.empty()) {
            const QJsonArray meshWeights = meshes.at(node.mesh).toObject().value(QStringLiteral("weights")).toArray();
            for (const QJsonValue& weight : meshWeights) {
                node.morphWeights.push_back(static_cast<float>(weight.toDouble()));
            }
        }
        const int light = json.value(QStringLiteral("extensions"))
                              .toObject()
                              .value(QStringLiteral("KHR_lights_punctual"))
                              .toObject()
                              .value(QStringLiteral("light"))
                              .toInt(-1);
        node.light = light;
        if (light >= 0 && light < static_cast<int>(model.lights.size())) {
            model.lights[static_cast<size_t>(light)].node = i;
        }
        if (node.camera >= 0 && node.camera < static_cast<int>(model.cameras.size())) {
            model.cameras[static_cast<size_t>(node.camera)].node = i;
        }
    }
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonArray children = nodes.at(i).toObject().value(QStringLiteral("children")).toArray();
        for (const QJsonValue& child : children) {
            const int childIndex = child.toInt(-1);
            if (childIndex >= 0 && childIndex < static_cast<int>(model.nodes.size())) {
                model.nodes[static_cast<size_t>(childIndex)].parent = i;
            }
        }
    }

    model.path = path;
    model.title = prettyModelName(QFileInfo(path).fileName());

    QString error;
    std::vector<int> roots;
    if (sceneIndex >= 0 && sceneIndex < scenes.size()) {
        const QJsonArray sceneNodes = scenes.at(sceneIndex).toObject().value(QStringLiteral("nodes")).toArray();
        for (const QJsonValue& node : sceneNodes) {
            const int index = node.toInt(-1);
            if (index >= 0) {
                roots.push_back(index);
            }
        }
    }
    if (roots.empty()) {
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
            if (model.nodes[static_cast<size_t>(i)].parent < 0) {
                roots.push_back(i);
            }
        }
    }
    const int variantCount = static_cast<int>(model.variants.size());
    auto visit = [&](auto&& self, int index) -> void {
        if (index < 0 || index >= static_cast<int>(model.nodes.size()) || !error.isEmpty()) {
            return;
        }
        appendMeshForNode(&model, meshes, accessors, bufferViews, buffers, index, variantCount, &error);
        const QJsonArray children = nodes.at(index).toObject().value(QStringLiteral("children")).toArray();
        for (const QJsonValue& child : children) {
            self(self, child.toInt(-1));
        }
    };
    for (int rootIndex : roots) {
        visit(visit, rootIndex);
    }

    parseAnimations(&model, animations, accessors, bufferViews, buffers);

    if (!error.isEmpty()) {
        model.error = error;
        return model;
    }
    if (model.parts.empty()) {
        model.error = QStringLiteral("No triangle primitives found in the model.");
        return model;
    }
    return model;
}

std::vector<QMatrix4x4> composeWorldMatrices(const LoadedModel& model) {
    const int n = static_cast<int>(model.nodes.size());
    std::vector<QMatrix4x4> world(static_cast<size_t>(std::max(n, 0)));
    std::vector<char> done(static_cast<size_t>(std::max(n, 0)), 0);
    auto compute = [&](auto&& self, int index) -> QMatrix4x4 {
        if (index < 0 || index >= n) {
            return QMatrix4x4();
        }
        if (done[static_cast<size_t>(index)]) {
            return world[static_cast<size_t>(index)];
        }
        const SceneNode& node = model.nodes[static_cast<size_t>(index)];
        const QMatrix4x4 local = nodeLocalMatrix(node);
        const QMatrix4x4 parent = node.parent >= 0 ? self(self, node.parent) : QMatrix4x4();
        world[static_cast<size_t>(index)] = parent * local;
        done[static_cast<size_t>(index)] = 1;
        return world[static_cast<size_t>(index)];
    };
    for (int i = 0; i < n; ++i) {
        compute(compute, i);
    }
    return world;
}

void computeModelBounds(LoadedModel* model) {
    if (model == nullptr) {
        return;
    }
    const std::vector<QMatrix4x4> localWorld = composeWorldMatrices(*model);
    QVector3D mn(1e9f, 1e9f, 1e9f);
    QVector3D mx(-1e9f, -1e9f, -1e9f);
    bool any = false;
    for (const MeshPart& part : model->parts) {
        QMatrix4x4 xf = model->fit;
        if (part.node >= 0 && part.node < static_cast<int>(localWorld.size())) {
            xf = model->fit * localWorld[static_cast<size_t>(part.node)];
        }
        for (size_t i = 0; i + 2 < part.vertices.size(); i += static_cast<size_t>(kVertexFloats)) {
            const QVector3D p = xf.map(QVector3D(part.vertices[i], part.vertices[i + 1], part.vertices[i + 2]));
            mn.setX(std::min(mn.x(), p.x()));
            mn.setY(std::min(mn.y(), p.y()));
            mn.setZ(std::min(mn.z(), p.z()));
            mx.setX(std::max(mx.x(), p.x()));
            mx.setY(std::max(mx.y(), p.y()));
            mx.setZ(std::max(mx.z(), p.z()));
            any = true;
        }
    }
    if (!any) {
        return;
    }
    model->min = mn;
    model->max = mx;
    model->hasBounds = true;
}

void normalizeModel(LoadedModel* model) {
    if (model == nullptr) {
        return;
    }
    model->fit.setToIdentity();
    computeModelBounds(model);
    const QVector3D size = model->size();
    const float span = std::max(size.x(), std::max(size.y(), size.z()));
    const float scale = span > 1e-5f ? 4.4f / span : 1.0f;
    const QVector3D origin(model->center().x(), model->min.y(), model->center().z());
    QMatrix4x4 fit;
    fit.scale(scale);
    fit.translate(-origin);
    model->fit = fit;
    computeModelBounds(model);
}

std::vector<OutlinerItem> buildOutliner(const LoadedModel& model) {
    std::vector<OutlinerItem> items;
    std::vector<int> depth(model.nodes.size(), 0);
    for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
        int d = 0;
        int parent = model.nodes[static_cast<size_t>(i)].parent;
        while (parent >= 0 && parent < static_cast<int>(model.nodes.size()) && d < 32) {
            ++d;
            parent = model.nodes[static_cast<size_t>(parent)].parent;
        }
        depth[static_cast<size_t>(i)] = d;
    }
    std::vector<int> order;
    order.reserve(model.nodes.size());
    for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
        if (model.nodes[static_cast<size_t>(i)].parent < 0) {
            order.push_back(i);
        }
    }
    auto walk = [&](auto&& self, int index) -> void {
        if (index < 0 || index >= static_cast<int>(model.nodes.size())) {
            return;
        }
        const SceneNode& node = model.nodes[static_cast<size_t>(index)];
        OutlinerItem item;
        item.name = node.name;
        item.node = index;
        item.depth = depth[static_cast<size_t>(index)];
        item.kind = node.mesh >= 0 ? QStringLiteral("Mesh") : QStringLiteral("Node");
        if (node.camera >= 0) {
            item.kind = QStringLiteral("Camera");
        } else if (node.light >= 0) {
            item.kind = QStringLiteral("Light");
        }
        items.push_back(item);
        for (int i = 0; i < static_cast<int>(model.parts.size()); ++i) {
            const MeshPart& part = model.parts[static_cast<size_t>(i)];
            if (part.node != index) {
                continue;
            }
            OutlinerItem child;
            child.name = part.name.isEmpty() ? QStringLiteral("Primitive %1").arg(i) : part.name;
            if (part.primitiveIndex > 0) {
                child.name += QStringLiteral(" · %1").arg(part.primitiveIndex);
            }
            child.kind = QStringLiteral("Primitive");
            child.node = index;
            child.part = i;
            child.material = part.material;
            child.depth = item.depth + 1;
            items.push_back(child);
        }
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i) {
            if (model.nodes[static_cast<size_t>(i)].parent == index) {
                self(self, i);
            }
        }
    };
    for (int root : order) {
        walk(walk, root);
    }
    return items;
}
