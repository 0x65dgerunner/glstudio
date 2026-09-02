#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include <cstdint>
#include <vector>

constexpr int kVertexFloats = 14;
constexpr int kMaxBones = 256;
constexpr int kMaxMorphs = 16;

struct TextureTransform {
    QVector2D offset{0.0f, 0.0f};
    QVector2D scale{1.0f, 1.0f};
    float rotation = 0.0f;
    int texCoord = 0;
};

struct PbrMaterial {
    QString name;
    QString shader;
    QVector4D baseColor{1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.45f;
    QVector3D emissive{0, 0, 0};
    float emissiveStrength = 1.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.08f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float thickness = 0.0f;
    QVector3D attenuationColor{1, 1, 1};
    float attenuationDistance = 0.0f;
    float iridescence = 0.0f;
    float iridescenceIor = 1.3f;
    float iridescenceThicknessMin = 100.0f;
    float iridescenceThicknessMax = 400.0f;
    QVector3D sheenColor{0, 0, 0};
    float sheenRoughness = 0.5f;
    float alphaCutoff = 0.0f;
    bool blend = false;
    bool doubleSided = true;
    bool unlit = false;
    bool gltfMaps = false;
    int baseColorTexture = -1;
    int normalTexture = -1;
    int mapsTexture = -1;
    int emissiveTexture = -1;
    int occlusionTexture = -1;
    int clearcoatTexture = -1;
    int clearcoatRoughnessTexture = -1;
    int clearcoatNormalTexture = -1;
    int transmissionTexture = -1;
    int thicknessTexture = -1;
    int sheenColorTexture = -1;
    int sheenRoughnessTexture = -1;
    int iridescenceTexture = -1;
    int iridescenceThicknessTexture = -1;
    TextureTransform albedoUv;
    TextureTransform normalUv;
    TextureTransform mapsUv;
    TextureTransform emissiveUv;
    TextureTransform occlusionUv;
    TextureTransform clearcoatUv;
    TextureTransform clearcoatRoughUv;
    TextureTransform clearcoatNormalUv;
    TextureTransform transmissionUv;
    TextureTransform thicknessUv;
    TextureTransform sheenColorUv;
    TextureTransform sheenRoughUv;
    TextureTransform iridescenceUv;
    TextureTransform iridescenceThicknessUv;
};

struct MorphTarget {
    std::vector<float> positions;
    std::vector<float> normals;
};

struct MeshPart {
    QString name;
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<float> joints;
    std::vector<float> weights;
    std::vector<MorphTarget> morphs;
    std::vector<int> variantMaterials;
    int material = 0;
    int defaultMaterial = 0;
    int node = 0;
    int skin = -1;
    int meshIndex = -1;
    int primitiveIndex = 0;
};

struct SceneNode {
    QString name;
    int parent = -1;
    int mesh = -1;
    int skin = -1;
    int camera = -1;
    int light = -1;
    QVector3D translation{0, 0, 0};
    QQuaternion rotation;
    QVector3D scale{1, 1, 1};
    QMatrix4x4 rest;
    std::vector<float> morphWeights;
    bool hasTrs = false;
};

inline QMatrix4x4 nodeLocalMatrix(const SceneNode& node) {
    if (!node.hasTrs) {
        return node.rest;
    }
    QMatrix4x4 local;
    local.translate(node.translation);
    local.rotate(node.rotation);
    local.scale(node.scale);
    return local;
}

enum class AnimPath { Translation, Rotation, Scale, Weights };
enum class AnimInterp { Linear, Step, Cubic };

struct AnimSampler {
    std::vector<float> times;
    std::vector<float> values;
    int components = 3;
    AnimInterp interpolation = AnimInterp::Linear;
};

struct AnimChannel {
    int node = -1;
    AnimPath path = AnimPath::Translation;
    AnimSampler sampler;
};

struct AnimClip {
    QString name;
    QString label;
    float duration = 0.0f;
    std::vector<AnimChannel> channels;
};

struct GltfSkin {
    QString name;
    std::vector<int> joints;
    std::vector<QMatrix4x4> inverseBind;
    int skeleton = -1;
};

struct GltfCamera {
    QString name;
    float yfov = 0.7f;
    float znear = 0.05f;
    float zfar = 120.0f;
    bool perspective = true;
    int node = -1;
};

struct GltfLight {
    QString name;
    int type = 0;
    QVector3D color{1, 1, 1};
    float intensity = 1.0f;
    float range = 0.0f;
    float innerCone = 0.0f;
    float outerCone = 0.785398f;
    int node = -1;
};

struct MaterialVariant {
    QString name;
};

struct LoadedModel {
    std::vector<MeshPart> parts;
    std::vector<PbrMaterial> materials;
    std::vector<QImage> images;
    std::vector<QString> imageNames;
    std::vector<SceneNode> nodes;
    std::vector<AnimClip> animations;
    std::vector<GltfSkin> gltfSkins;
    std::vector<GltfCamera> cameras;
    std::vector<GltfLight> lights;
    std::vector<MaterialVariant> variants;
    QStringList usedExtensions;
    QStringList ignoredExtensions;
    QMatrix4x4 fit;
    QVector3D min{0, 0, 0};
    QVector3D max{0, 0, 0};
    bool hasBounds = false;
    QString error;
    QString title = QStringLiteral("Model");
    QString path;

    bool isEmpty() const { return parts.empty(); }
    QVector3D center() const { return (min + max) * 0.5f; }
    QVector3D size() const { return max - min; }
};

struct OutlinerItem {
    QString name;
    QString kind;
    int node = -1;
    int part = -1;
    int material = -1;
    int depth = 0;
};

struct ModelEntry {
    QString name;
    QString path;
};

QString userModelsDir();
QString ensureUserModelsDir();
QString findBundledModelsDir();
QString findModelsDir();
std::vector<ModelEntry> listPreviewModels();
QString findModelGlbPath();
LoadedModel loadGlbModel(const QString& path);
void normalizeModel(LoadedModel* model);
void computeModelBounds(LoadedModel* model);
std::vector<OutlinerItem> buildOutliner(const LoadedModel& model);
std::vector<QMatrix4x4> composeWorldMatrices(const LoadedModel& model);
