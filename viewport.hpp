#pragma once

#include "glb_loader.hpp"
#include "hdri_loader.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QStringList>
#include <QVector3D>

#include <vector>

class QMouseEvent;
class QTimer;
class QWheelEvent;
class QKeyEvent;
class QDragEnterEvent;
class QDropEvent;

class ModelViewport : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT

public:
    explicit ModelViewport(QWidget* parent = nullptr);
    ~ModelViewport() override;

    enum class Lighting { Light, Dark };
    enum class Quality { Low, Medium, High, Ultra, Extreme };
    enum class DebugView {
        Lit = 0,
        Albedo,
        Normal,
        Roughness,
        Metallic,
        Occlusion,
        DirectLight,
        Specular,
        Shadow,
        Emissive
    };
    enum class Tonemap { Aces = 0, Reinhard, Filmic, Linear };

    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    float distance() const { return distance_; }
    float fov() const { return fov_; }
    float exposure() const { return exposure_; }
    float bloom() const { return bloom_; }
    float bloomThreshold() const { return bloomThreshold_; }
    float vignette() const { return vignette_; }
    float envIntensity() const { return envIntensity_; }
    float envRotation() const { return envRotation_; }
    float keyIntensity() const { return keyIntensity_; }
    float fillIntensity() const { return fillIntensity_; }
    float rimIntensity() const { return rimIntensity_; }
    float keyYaw() const { return keyYaw_; }
    float keyPitch() const { return keyPitch_; }
    float fillYaw() const { return fillYaw_; }
    float fillPitch() const { return fillPitch_; }
    float rimYaw() const { return rimYaw_; }
    float rimPitch() const { return rimPitch_; }
    float renderScale() const { return renderScale_; }
    float contrast() const { return contrast_; }
    float saturation() const { return saturation_; }
    float temperature() const { return temperature_; }
    float tint() const { return tint_; }
    float sharpen() const { return sharpen_; }
    float grain() const { return grain_; }
    float chromatic() const { return chromatic_; }
    float ssaoIntensity() const { return ssaoIntensity_; }
    float shadowStrength() const { return shadowStrength_; }
    float shadowSoftness() const { return shadowSoftness_; }
    float normalScale() const { return normalScale_; }
    float roughnessMul() const { return roughnessMul_; }
    float metallicMul() const { return metallicMul_; }
    float aoMul() const { return aoMul_; }
    float clearcoatMul() const { return clearcoatMul_; }
    float directMul() const { return directMul_; }
    int msaaSamples() const { return msaaSamples_; }
    int msaaActual() const { return msaaActual_; }
    int maxMsaaSamples() const { return maxMsaa_; }
    int shadowSize() const { return shadowSize_; }
    int iblSize() const { return iblSize_; }
    float anisotropy() const { return anisotropy_; }
    int bloomPasses() const { return bloomPasses_; }
    bool autoRotate() const { return autoRotate_; }
    bool gridVisible() const { return gridVisible_; }
    bool axesVisible() const { return axesVisible_; }
    bool texturesEnabled() const { return texturesEnabled_; }
    bool vsync() const { return vsync_; }
    bool skyVisible() const { return skyVisible_; }
    bool keyEnabled() const { return keyEnabled_; }
    bool fillEnabled() const { return fillEnabled_; }
    bool rimEnabled() const { return rimEnabled_; }
    bool envEnabled() const { return envEnabled_; }
    bool shadowsEnabled() const { return shadowsEnabled_; }
    bool ssaoEnabled() const { return ssaoEnabled_; }
    bool bloomEnabled() const { return bloomEnabled_; }
    bool wireframe() const { return wireframe_; }
    bool showLights() const { return showLights_; }
    bool normalMaps() const { return normalMaps_; }
    Lighting lighting() const { return lighting_; }
    Quality quality() const { return quality_; }
    DebugView debugView() const { return debugView_; }
    Tonemap tonemap() const { return tonemap_; }
    QColor backgroundColor() const { return backgroundColor_; }
    QColor keyColor() const { return keyColor_; }
    QColor fillColor() const { return fillColor_; }
    QColor rimColor() const { return rimColor_; }
    QString gpuName() const { return gpuName_; }
    QString gpuDetails() const { return gpuDetails_; }
    QString statusText() const;
    QJsonObject cameraState() const;
    QJsonObject lookState() const;
    QString hdriName() const { return hdri_.name; }
    QString hdriPath() const { return hdri_.path; }
    QString modelName() const { return cpuModel_.title; }
    QString modelPath() const { return requestedModel_; }
    QStringList modelNames() const;
    const LoadedModel& model() const { return cpuModel_; }
    std::vector<OutlinerItem> outlinerItems() const;
    int selectedNode() const { return selectedNode_; }
    int selectedPart() const { return selectedPart_; }
    int selectedMaterial() const { return selectedMaterial_; }
    int isolatedNode() const { return isolatedNode_; }
    bool clayMode() const { return clayMode_; }
    bool floorCatcher() const { return floorCatcher_; }
    bool sceneLights() const { return sceneLights_; }
    bool transparentBackground() const { return transparentBg_; }
    bool dofEnabled() const { return dofEnabled_; }
    float dofAmount() const { return dofAmount_; }
    float focusDistance() const { return focusDistance_; }
    int lookIndex() const { return lookIndex_; }
    int variantIndex() const { return variantIndex_; }
    int animationIndex() const { return animationIndex_; }
    int sceneCameraIndex() const { return sceneCameraIndex_; }
    bool animationPlaying() const { return animationPlaying_; }
    bool animationLoop() const { return animationLoop_; }
    float animationTime() const { return animationTime_; }
    float animationDuration() const;
    QStringList lookNames() const;
    QStringList variantNames() const;
    QStringList animationNames() const;
    QStringList sceneCameraNames() const;
    QStringList morphNames() const;
    std::vector<float> morphWeights() const;
    QStringList ignoredExtensions() const { return cpuModel_.ignoredExtensions; }
    QStringList usedExtensions() const { return cpuModel_.usedExtensions; }
    PbrMaterial selectedMaterialData() const;

public slots:
    void setFov(float degrees);
    void setExposure(float value);
    void setBloom(float value);
    void setBloomThreshold(float value);
    void setVignette(float value);
    void setEnvIntensity(float value);
    void setEnvRotation(float degrees);
    void setKeyIntensity(float value);
    void setFillIntensity(float value);
    void setRimIntensity(float value);
    void setKeyYaw(float degrees);
    void setKeyPitch(float degrees);
    void setFillYaw(float degrees);
    void setFillPitch(float degrees);
    void setRimYaw(float degrees);
    void setRimPitch(float degrees);
    void setKeyColor(const QColor& color);
    void setFillColor(const QColor& color);
    void setRimColor(const QColor& color);
    void setAutoRotate(bool enabled);
    void setGridVisible(bool visible);
    void setAxesVisible(bool visible);
    void setTexturesEnabled(bool enabled);
    void setShadowSize(int size);
    void setIblSize(int size);
    void setAnisotropy(float value);
    void setBloomPasses(int passes);
    void applyCameraState(const QJsonObject& state);
    void applyLookState(const QJsonObject& state);
    bool exportRender(const QString& path, int width, int height);
    bool exportTurntable(const QString& path, int width, int height, int frames = 120);
    void setBackgroundColor(const QColor& color);
    void setLighting(Lighting lighting);
    void setQuality(Quality quality);
    void setMsaaSamples(int samples);
    void setRenderScale(float scale);
    void setVSync(bool enabled);
    void setHdriPath(const QString& path);
    void setModelPath(const QString& path);
    void setSkyVisible(bool visible);
    void setKeyEnabled(bool enabled);
    void setFillEnabled(bool enabled);
    void setRimEnabled(bool enabled);
    void setEnvEnabled(bool enabled);
    void setShadowsEnabled(bool enabled);
    void setSsaoEnabled(bool enabled);
    void setBloomEnabled(bool enabled);
    void setWireframe(bool enabled);
    void setShowLights(bool enabled);
    void setNormalMaps(bool enabled);
    void setDebugView(DebugView view);
    void setTonemap(Tonemap tonemap);
    void setContrast(float value);
    void setSaturation(float value);
    void setTemperature(float value);
    void setTint(float value);
    void setSharpen(float value);
    void setGrain(float value);
    void setChromatic(float value);
    void setSsaoIntensity(float value);
    void setShadowStrength(float value);
    void setShadowSoftness(float value);
    void setNormalScale(float value);
    void setRoughnessMul(float value);
    void setMetallicMul(float value);
    void setAoMul(float value);
    void setClearcoatMul(float value);
    void setDirectMul(float value);
    void resetCamera();
    void setClayMode(bool enabled);
    void setFloorCatcher(bool enabled);
    void setSceneLights(bool enabled);
    void setTransparentBackground(bool enabled);
    void setDofEnabled(bool enabled);
    void setDofAmount(float value);
    void setFocusDistance(float value);
    void applyLook(int index);
    void setVariantIndex(int index);
    void setSelectedNode(int node);
    void setSelectedPart(int part);
    void setIsolatedNode(int node);
    void clearIsolation();
    void setPartHidden(int part, bool hidden);
    void setAnimationIndex(int index);
    void setAnimationPlaying(bool playing);
    void setAnimationLoop(bool loop);
    void setAnimationTime(float seconds);
    void setSceneCameraIndex(int index);
    void setMorphWeight(int index, float value);
    void setSelectedBaseColor(const QColor& color);
    void setSelectedMetallic(float value);
    void setSelectedRoughness(float value);
    void setSelectedTransmission(float value);
    void setSelectedSheen(float value);
    void setSelectedEmissiveGain(float value);
    void setSelectedUnlit(bool enabled);

signals:
    void cameraChanged();
    void statusChanged();
    void graphicsChanged();
    void modelChanged();
    void selectionChanged();
    void animationChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct GpuPart {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLuint jointVbo = 0;
        GLuint weightVbo = 0;
        GLuint morphTex = 0;
        int indexCount = 0;
        int vertexCount = 0;
        int morphCount = 0;
        int material = 0;
        int defaultMaterial = 0;
        int node = 0;
        int skin = -1;
        bool hidden = false;
        bool gpuSkin = false;
        bool gpuMorph = false;
        std::vector<float> bindVertices;
        std::vector<float> joints;
        std::vector<float> weights;
        std::vector<MorphTarget> morphs;
        std::vector<int> variantMaterials;
    };

    struct GpuMaterial {
        PbrMaterial cpu;
        GLuint albedo = 0;
        GLuint normal = 0;
        GLuint maps = 0;
        GLuint emissive = 0;
        GLuint occlusion = 0;
        GLuint clearcoat = 0;
        GLuint clearcoatRough = 0;
        GLuint clearcoatNormal = 0;
        GLuint transmission = 0;
        GLuint thickness = 0;
        GLuint sheenColor = 0;
        GLuint sheenRough = 0;
        GLuint iridescence = 0;
        GLuint iridescenceThickness = 0;
        bool hasAlbedo = false;
        bool hasNormal = false;
        bool hasMaps = false;
        bool hasEmissive = false;
        bool hasOcclusion = false;
        bool hasClearcoat = false;
        bool hasClearcoatRough = false;
        bool hasClearcoatNormal = false;
        bool hasTransmission = false;
        bool hasThickness = false;
        bool hasSheenColor = false;
        bool hasSheenRough = false;
        bool hasIridescence = false;
        bool hasIridescenceThickness = false;
    };

    void loadModelAsync();
    void loadHdriAsync(const QString& path = QString());
    void uploadPendingModel();
    void uploadPendingHdri();
    void recreateTargets();
    void destroyTargets();
    void destroyGpu();
    bool compileProgram(GLuint* program, const char* vert, const char* frag);
    void drawScene(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& cam, int transPass);
    void uploadBoneTexture(int skinIndex, int meshNode);
    void bindPartSkin(GLuint program, const GpuPart& part);
    void drawShadow();
    void drawSsao(const QMatrix4x4& proj);
    void drawFloor(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& cam);
    void drawLightGizmos(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawAxes(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawLineMesh(GLuint vao, int vertices, const QMatrix4x4& mvp, const QVector3D& cam,
                      float thickness, bool fade);
    void renderFrame(int pixelW, int pixelH, bool overlays);
    void applyAnimation(float time);
    void deformParts();
    void resetNodePose();
    bool partVisible(const GpuPart& part) const;
    int pickPart(const QPoint& pos);
    float pickDepth(const QPoint& pos);
    QMatrix4x4 sceneCameraView(float* fovOut) const;
    void applyAnisotropy();
    int framebufferWidth() const;
    int framebufferHeight() const;
    void buildIbl();
    void updateWorldMatrices();
    QMatrix4x4 viewMatrix() const;
    QVector3D cameraPosition() const;
    QVector3D lightDirection(float yawDeg, float pitchDeg) const;
    QVector3D keyDirection() const;
    QMatrix4x4 lightViewProjection() const;
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float steps);
    void tick();
    void applyLightingPreset();
    void applyQualityPreset();
    void applyVSync();
    void rebuildEnvironment();
    GLuint makeTexture(const QImage& image, bool srgb, bool isNormal);
    GLuint makeSolidTexture(quint8 r, quint8 g, quint8 b, quint8 a);

    LoadedModel cpuModel_;
    LoadedModel pending_;
    bool hasPending_ = false;
    HdriImage pendingHdri_;
    HdriImage hdri_;
    bool hasPendingHdri_ = false;
    bool uploaded_ = false;
    bool glReady_ = false;
    QString status_ = QStringLiteral("Loading model…");
    QString requestedModel_;
    QString requestedHdri_;
    std::vector<QMatrix4x4> world_;

    std::vector<GpuPart> parts_;
    std::vector<GpuMaterial> materials_;
    GLuint whiteTex_ = 0;
    GLuint flatNormalTex_ = 0;
    GLuint envCube_ = 0;
    GLuint irrCube_ = 0;
    GLuint prefCube_ = 0;
    GLuint brdfLut_ = 0;
    GLuint modelProgram_ = 0;
    GLuint groundProgram_ = 0;
    GLuint skyProgram_ = 0;
    GLuint shadowProgram_ = 0;
    GLuint ssaoProgram_ = 0;
    GLuint brightProgram_ = 0;
    GLuint blurProgram_ = 0;
    GLuint compositeProgram_ = 0;
    GLuint overlayProgram_ = 0;
    GLuint wireProgram_ = 0;
    GLuint iblProgram_ = 0;
    GLuint lutProgram_ = 0;
    GLuint iblIrrProg_ = 0;
    GLuint iblPrefProg_ = 0;
    GLuint boneTex_ = 0;
    GLuint cubeVao_ = 0;
    GLuint cubeVbo_ = 0;
    GLuint cubeEbo_ = 0;
    GLuint opaqueColor_ = 0;

    GLuint floorProgram_ = 0;
    GLuint floorVao_ = 0;
    GLuint floorVbo_ = 0;
    GLuint groundVao_ = 0;
    GLuint groundVbo_ = 0;
    int groundCount_ = 0;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint gizmoVao_ = 0;
    GLuint gizmoVbo_ = 0;

    GLuint sceneFbo_ = 0;
    GLuint sceneColor_ = 0;
    GLuint sceneDepth_ = 0;
    GLuint msaaFbo_ = 0;
    GLuint msaaColor_ = 0;
    GLuint msaaDepth_ = 0;
    GLuint bloomFbo_[2] = {0, 0};
    GLuint bloomTex_[2] = {0, 0};
    GLuint shadowFbo_ = 0;
    GLuint shadowTex_ = 0;
    GLuint ssaoFbo_ = 0;
    GLuint ssaoTex_ = 0;
    GLuint presentFbo_ = 0;
    GLuint presentTex_ = 0;
    int targetW_ = 0;
    int targetH_ = 0;
    int shadowSize_ = 2048;
    int iblSize_ = 512;
    int captureW_ = 0;
    int captureH_ = 0;
    int maxMsaa_ = 8;
    int maxTexSize_ = 8192;
    float maxAniso_ = 16.0f;
    float anisotropy_ = 16.0f;

    QVector3D modelCenter_{0, 0.8f, 0};
    QVector3D modelSize_{4, 1.4f, 4};

    float yaw_ = 138.0f;
    float pitch_ = 16.0f;
    float distance_ = 7.2f;
    float fov_ = 40.0f;
    QVector3D target_{0, 0.7f, 0};
    float exposure_ = 1.05f;
    float bloom_ = 0.16f;
    float bloomThreshold_ = 1.25f;
    float vignette_ = 0.08f;
    float envIntensity_ = 1.15f;
    float envRotation_ = 0.0f;
    float keyIntensity_ = 0.55f;
    float fillIntensity_ = 0.16f;
    float rimIntensity_ = 0.22f;
    float keyYaw_ = 38.0f;
    float keyPitch_ = 55.0f;
    float fillYaw_ = -112.0f;
    float fillPitch_ = 14.0f;
    float rimYaw_ = -173.0f;
    float rimPitch_ = 19.0f;
    float renderScale_ = 1.35f;
    float contrast_ = 1.0f;
    float saturation_ = 1.0f;
    float temperature_ = 0.0f;
    float tint_ = 0.0f;
    float sharpen_ = 0.0f;
    float grain_ = 0.0f;
    float chromatic_ = 0.0f;
    float ssaoIntensity_ = 1.0f;
    float shadowStrength_ = 0.72f;
    float shadowSoftness_ = 1.0f;
    float normalScale_ = 1.0f;
    float roughnessMul_ = 1.0f;
    float metallicMul_ = 1.0f;
    float aoMul_ = 1.0f;
    float clearcoatMul_ = 1.0f;
    float directMul_ = 1.0f;
    int msaaSamples_ = 8;
    int msaaActual_ = 0;
    int bloomPasses_ = 2;
    bool autoRotate_ = false;
    bool gridVisible_ = false;
    bool axesVisible_ = false;
    bool texturesEnabled_ = true;
    bool skyVisible_ = false;
    bool vsync_ = true;
    bool keyEnabled_ = true;
    bool fillEnabled_ = true;
    bool rimEnabled_ = true;
    bool envEnabled_ = true;
    bool shadowsEnabled_ = true;
    bool ssaoEnabled_ = true;
    bool bloomEnabled_ = true;
    bool wireframe_ = false;
    bool showLights_ = false;
    bool normalMaps_ = true;
    bool clayMode_ = false;
    bool floorCatcher_ = false;
    bool sceneLights_ = true;
    bool transparentBg_ = false;
    bool dofEnabled_ = false;
    bool animationPlaying_ = false;
    bool animationLoop_ = true;
    float dofAmount_ = 0.35f;
    float focusDistance_ = 7.0f;
    int lookIndex_ = 0;
    int variantIndex_ = -1;
    int animationIndex_ = 0;
    int sceneCameraIndex_ = -1;
    int selectedNode_ = -1;
    int selectedPart_ = -1;
    int selectedMaterial_ = -1;
    int isolatedNode_ = -1;
    float animationTime_ = 0.0f;
    std::vector<float> morphWeights_;
    std::vector<SceneNode> bindNodes_;
    bool dragged_ = false;
    Lighting lighting_ = Lighting::Dark;
    Quality quality_ = Quality::Ultra;
    DebugView debugView_ = DebugView::Lit;
    Tonemap tonemap_ = Tonemap::Aces;
    QColor backgroundColor_{8, 9, 12};
    QColor keyColor_{255, 220, 190};
    QColor fillColor_{70, 90, 130};
    QColor rimColor_{140, 180, 255};
    QString gpuName_;
    QString gpuDetails_;

    QPoint lastMouse_;
    Qt::MouseButtons dragButtons_ = Qt::NoButton;
    Qt::KeyboardModifiers dragMods_ = Qt::NoModifier;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
};
