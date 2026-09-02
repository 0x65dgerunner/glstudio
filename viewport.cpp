#include "viewport.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>

namespace {

constexpr int kMaxLights = 8;

constexpr float kCubeVerts[] = {
    -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
    -1, -1, 1,  1, -1, 1,  1, 1, 1,  -1, 1, 1,
};

constexpr unsigned int kCubeIndices[] = {
    0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5,
    4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
};

constexpr const char* kModelVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aUv0;
layout(location = 4) in float aTangentSign;
layout(location = 5) in vec2 aUv1;
layout(location = 6) in vec4 aJoints;
layout(location = 7) in vec4 aWeights;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uLightVP;
uniform int uSkinned;
uniform sampler2D uBoneTex;
uniform sampler2D uMorphTex;
uniform int uMorphCount;
uniform float uMorphW[16];
out vec3 vWorld;
out vec3 vNormal;
out vec3 vTangent;
out float vTangentSign;
out vec2 vUv0;
out vec2 vUv1;
out vec4 vShadow;
mat4 boneAt(int i) {
    return mat4(
        texelFetch(uBoneTex, ivec2(i, 0), 0),
        texelFetch(uBoneTex, ivec2(i, 1), 0),
        texelFetch(uBoneTex, ivec2(i, 2), 0),
        texelFetch(uBoneTex, ivec2(i, 3), 0));
}
void main() {
    vec3 pos = aPos;
    vec3 nrm = aNormal;
    for (int i = 0; i < uMorphCount && i < 16; ++i) {
        float w = uMorphW[i];
        if (abs(w) < 1e-6) continue;
        pos += texelFetch(uMorphTex, ivec2(gl_VertexID, i * 2), 0).xyz * w;
        nrm += texelFetch(uMorphTex, ivec2(gl_VertexID, i * 2 + 1), 0).xyz * w;
    }
    vec3 tan = aTangent;
    if (uSkinned != 0) {
        mat4 skinMat =
            boneAt(int(aJoints.x)) * aWeights.x +
            boneAt(int(aJoints.y)) * aWeights.y +
            boneAt(int(aJoints.z)) * aWeights.z +
            boneAt(int(aJoints.w)) * aWeights.w;
        pos = (skinMat * vec4(pos, 1.0)).xyz;
        mat3 s = mat3(skinMat);
        nrm = s * nrm;
        tan = s * tan;
    }
    vec4 world = uModel * vec4(pos, 1.0);
    vWorld = world.xyz;
    mat3 nmat = mat3(uModel);
    vNormal = nmat * nrm;
    vTangent = nmat * tan;
    vTangentSign = aTangentSign;
    vUv0 = aUv0;
    vUv1 = aUv1;
    vShadow = uLightVP * world;
    gl_Position = uMvp * vec4(pos, 1.0);
}
)";

constexpr const char* kModelFrag = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec3 vTangent;
in float vTangentSign;
in vec2 vUv0;
in vec2 vUv1;
in vec4 vShadow;
out vec4 FragColor;
uniform vec3 uCam;
uniform vec4 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uClearcoat;
uniform float uClearcoatRoughness;
uniform float uAlphaCut;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMaps;
uniform sampler2DShadow uShadowMap;
uniform samplerCube uEnvCube;
uniform samplerCube uIrrCube;
uniform samplerCube uPrefCube;
uniform sampler2D uBrdfLut;
uniform sampler2D uSceneColor;
uniform bool uHasAlbedo;
uniform bool uHasNormal;
uniform bool uHasMaps;
uniform sampler2D uEmissiveMap;
uniform sampler2D uOcclusionMap;
uniform sampler2D uClearcoatMap;
uniform sampler2D uClearcoatRoughMap;
uniform sampler2D uClearcoatNormalMap;
uniform sampler2D uTransmissionMap;
uniform sampler2D uThicknessMap;
uniform sampler2D uSheenColorMap;
uniform sampler2D uSheenRoughMap;
uniform sampler2D uIridescenceMap;
uniform sampler2D uIridThickMap;
uniform bool uGltfMaps;
uniform bool uHasEmissive;
uniform bool uHasOcclusion;
uniform bool uHasClearcoat;
uniform bool uHasClearcoatRough;
uniform bool uHasClearcoatNormal;
uniform bool uHasTransmission;
uniform bool uHasThickness;
uniform bool uHasSheenColor;
uniform bool uHasSheenRough;
uniform bool uHasIridescence;
uniform bool uHasIridThick;
uniform bool uUnlit;
uniform vec3 uKeyDir;
uniform vec3 uKeyColor;
uniform vec3 uFillDir;
uniform vec3 uFillColor;
uniform vec3 uRimDir;
uniform vec3 uRimColor;
uniform float uEnv;
uniform float uEnvYaw;
uniform float uEmissiveGain;
uniform float uNormalScale;
uniform float uRoughnessMul;
uniform float uMetallicMul;
uniform float uAoMul;
uniform float uClearcoatMul;
uniform float uDirectMul;
uniform float uShadowStrength;
uniform float uShadowSoft;
uniform float uTransmission;
uniform float uIor;
uniform float uThickness;
uniform vec3 uAttenuationColor;
uniform float uAttenuationDistance;
uniform float uIridescence;
uniform float uIridIor;
uniform float uIridThickMin;
uniform float uIridThickMax;
uniform vec3 uSheenColor;
uniform float uSheenRough;
uniform int uLightCount;
uniform vec3 uLPos[8];
uniform vec3 uLCol[8];
uniform vec4 uLData[8];
uniform vec3 uLDir[8];
uniform int uDebug;
uniform int uUseNormals;
uniform int uShadowsOn;
uniform int uClay;
uniform int uDimmed;
uniform int uPass;
uniform vec2 uResolution;
uniform vec4 uAlbedoUv;
uniform float uAlbedoUvRot;
uniform int uAlbedoSet;
uniform vec4 uNormalUv;
uniform float uNormalUvRot;
uniform int uNormalSet;
uniform vec4 uMapsUv;
uniform float uMapsUvRot;
uniform int uMapsSet;
uniform vec4 uEmissiveUv;
uniform float uEmissiveUvRot;
uniform int uEmissiveSet;
uniform vec4 uOcclusionUv;
uniform float uOcclusionUvRot;
uniform int uOcclusionSet;
uniform vec4 uClearcoatUv;
uniform float uClearcoatUvRot;
uniform int uClearcoatSet;
uniform vec4 uTransmissionUv;
uniform float uTransmissionUvRot;
uniform int uTransmissionSet;
uniform vec4 uThicknessUv;
uniform float uThicknessUvRot;
uniform int uThicknessSet;
uniform vec4 uSheenColorUv;
uniform float uSheenColorUvRot;
uniform int uSheenColorSet;
uniform vec4 uSheenRoughUv;
uniform float uSheenRoughUvRot;
uniform int uSheenRoughSet;
uniform vec4 uIridUv;
uniform float uIridUvRot;
uniform int uIridSet;
uniform vec4 uIridThickUv;
uniform float uIridThickUvRot;
uniform int uIridThickSet;
const float PI = 3.14159265;
)" R"(
vec2 xformUv(vec2 uv0, vec2 uv1, int set, vec4 st, float rot) {
    vec2 uv = set > 0 ? uv1 : uv0;
    uv *= st.xy;
    float c = cos(rot);
    float s = sin(rot);
    uv = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    return uv + st.zw;
}
vec3 finite3(vec3 c) {
    return vec3(
        (isnan(c.x) || isinf(c.x) || c.x < 0.0) ? 0.0 : min(c.x, 50000.0),
        (isnan(c.y) || isinf(c.y) || c.y < 0.0) ? 0.0 : min(c.y, 50000.0),
        (isnan(c.z) || isinf(c.z) || c.z < 0.0) ? 0.0 : min(c.z, 50000.0));
}
vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 1e-10 ? v * inversesqrt(len2) : vec3(0.0, 1.0, 0.0);
}

mat3 envRot() {
    float c = cos(uEnvYaw);
    float s = sin(uEnvYaw);
    return mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}
float G_Smith(float NdotV, float NdotL, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
float D_Charlie(float roughness, float NoH) {
    float invR = 1.0 / max(roughness, 1e-4);
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * PI);
}
float lambdaSheenNumeric(float x, float alphaG) {
    float oneMinus = (1.0 - alphaG) * (1.0 - alphaG);
    float a = mix(21.5473, 25.3245, oneMinus);
    float b = mix(3.82987, 3.32435, oneMinus);
    float c = mix(0.19823, 0.16801, oneMinus);
    float d = mix(-1.97760, -1.27393, oneMinus);
    float e = mix(-4.32054, -4.85967, oneMinus);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}
float lambdaSheen(float cosTheta, float alphaG) {
    if (abs(cosTheta) < 0.5) return exp(lambdaSheenNumeric(cosTheta, alphaG));
    return exp(2.0 * lambdaSheenNumeric(0.5, alphaG) - lambdaSheenNumeric(1.0 - cosTheta, alphaG));
}
float V_Charlie(float NoL, float NoV, float roughness) {
    float a = max(roughness * roughness, 1e-6);
    return clamp(1.0 / ((1.0 + lambdaSheen(NoV, a) + lambdaSheen(NoL, a)) * max(4.0 * NoV * NoL, 1e-5)), 0.0, 1.0);
}
vec3 fresnel0ToIor(vec3 f0) {
    vec3 sqrtF0 = sqrt(clamp(f0, vec3(0.0), vec3(0.99)));
    return (vec3(1.0) + sqrtF0) / (vec3(1.0) - sqrtF0);
}
vec3 iorToFresnel0(vec3 ior, float outside) {
    return pow((ior - vec3(outside)) / (ior + vec3(outside)), vec3(2.0));
}
vec3 evalIridescence(float outsideIor, float eta2, float cosTheta1, float t, vec3 f0) {
    float iridescenceIor = mix(outsideIor, eta2, smoothstep(0.0, 0.03, t));
    float sinTheta2Sq = pow(outsideIor / iridescenceIor, 2.0) * (1.0 - cosTheta1 * cosTheta1);
    float cosTheta2Sq = 1.0 - sinTheta2Sq;
    if (cosTheta2Sq < 0.0) return vec3(1.0);
    float cosTheta2 = sqrt(cosTheta2Sq);
    float r0 = (iridescenceIor - outsideIor) / (iridescenceIor + outsideIor);
    float r12 = r0 * r0;
    float t121 = 1.0 - r12;
    vec3 baseIor = fresnel0ToIor(f0) / outsideIor;
    vec3 r1 = iorToFresnel0(baseIor, iridescenceIor);
    float phi12 = iridescenceIor < outsideIor ? PI : 0.0;
    float phi21 = PI - phi12;
    vec3 phi23 = mix(vec3(0.0), vec3(PI), vec3(lessThan(baseIor, vec3(1.0))));
    float o = (2.0 * PI) * iridescenceIor * t * cosTheta2;
    vec3 phi = vec3(phi21) + phi23;
    vec3 r123 = sqrt(max(vec3(r12) * r1, vec3(0.0)));
    vec3 c = r123 * 2.0 * t121;
    vec3 r = vec3(r12) + t121 * t121 * r1;
    vec3 s = vec3(0.0);
    for (int m = 1; m <= 2; ++m) {
        float fm = float(m);
        s += c * cos(fm * phi + vec3(fm * o)) / fm;
        c *= r123;
    }
    return clamp(r + s, vec3(0.0), vec3(1.0));
}
vec2 envBRDF(float NdotV, float roughness) {
    return texture(uBrdfLut, vec2(clamp(NdotV, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}
vec3 lightContrib(vec3 N, vec3 V, vec3 L, vec3 light, vec3 albedo, float metallic, float roughness) {
    vec3 H = safeNormalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float a = max(roughness * roughness, 0.0025);
    vec3 F = F_Schlick(HdotV, F0);
    vec3 spec = D_GGX(NdotH, a) * G_Smith(NdotV, NdotL, roughness) * F / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return finite3((kD * albedo / PI + spec) * light * NdotL);
}
float shadowAt() {
    if (uShadowsOn == 0) {
        return 1.0;
    }
    vec3 proj = vShadow.xyz / max(vShadow.w, 1e-4);
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    float bias = 0.0015;
    float s = 0.0;
    vec2 texel = (1.0 / vec2(textureSize(uShadowMap, 0))) * max(uShadowSoft, 0.15);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            s += texture(uShadowMap, vec3(proj.xy + vec2(x, y) * texel, proj.z - bias));
        }
    }
    return mix(1.0 - uShadowStrength, 1.0, s / 25.0);
}
vec3 iridescenceFresnel(float NdotV, float thickness, vec3 f0) {
    return evalIridescence(1.0, uIridIor, NdotV, thickness, f0);
}
vec3 punctualLight(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 world) {
    vec3 acc = vec3(0.0);
    for (int i = 0; i < uLightCount && i < 8; ++i) {
        int type = int(uLData[i].x + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 0) {
            L = safeNormalize(uLDir[i]);
        } else {
            vec3 toL = uLPos[i] - world;
            float dist = length(toL);
            L = toL / max(dist, 1e-4);
            atten = 1.0 / max(dist * dist, 0.01);
            float range = uLData[i].y;
            if (range > 0.0) {
                float x = clamp(1.0 - pow(dist / max(range, 1e-4), 4.0), 0.0, 1.0);
                atten *= x * x;
            }
            if (type == 2) {
                float cd = dot(-L, safeNormalize(uLDir[i]));
                atten *= smoothstep(uLData[i].w, uLData[i].z, cd);
            }
        }
        acc += lightContrib(N, V, L, uLCol[i] * atten, albedo, metallic, roughness);
    }
    return acc;
}
)" R"(
void main() {
    vec3 N = safeNormalize(vNormal);
    vec3 T = safeNormalize(vTangent - N * dot(N, vTangent));
    vec3 B = cross(N, T) * (vTangentSign < 0.0 ? -1.0 : 1.0);
    vec2 albedoUv = xformUv(vUv0, vUv1, uAlbedoSet, uAlbedoUv, uAlbedoUvRot);
    vec2 normalUv = xformUv(vUv0, vUv1, uNormalSet, uNormalUv, uNormalUvRot);
    vec2 mapsUv = xformUv(vUv0, vUv1, uMapsSet, uMapsUv, uMapsUvRot);
    vec2 occUv = xformUv(vUv0, vUv1, uOcclusionSet, uOcclusionUv, uOcclusionUvRot);
    vec2 coatUv = xformUv(vUv0, vUv1, uClearcoatSet, uClearcoatUv, uClearcoatUvRot);
    if (uHasNormal && uUseNormals != 0) {
        vec3 nts = texture(uNormalMap, normalUv).xyz * 2.0 - 1.0;
        nts.xy *= uNormalScale;
        N = safeNormalize(mat3(T, B, N) * nts);
    }
    if (!gl_FrontFacing) N = -N;
    vec3 V = safeNormalize(uCam - vWorld);
    vec4 albedoSample = uHasAlbedo ? texture(uAlbedoMap, albedoUv) : vec4(1.0);
    vec3 albedo = uBaseColor.rgb;
    float alpha = uBaseColor.a;
    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = 1.0;
    if (uHasAlbedo) {
        albedo *= albedoSample.rgb;
        alpha *= albedoSample.a;
    }
    if (uClay != 0) {
        albedo = vec3(0.62);
        metallic = 0.0;
        roughness = 0.55;
        alpha = 1.0;
    }
    if (uAlphaCut > 0.001 && alpha < uAlphaCut) {
        discard;
    }
    if (uHasMaps && uClay == 0) {
        vec3 maps = texture(uMaps, mapsUv).rgb;
        if (uGltfMaps) {
            metallic = clamp(uMetallic * maps.b, 0.0, 1.0);
            roughness = clamp(uRoughness * maps.g, 0.04, 1.0);
            ao = mix(0.35, 1.0, maps.r);
        } else {
            metallic = clamp(maps.r, 0.0, 1.0);
            roughness = clamp(1.0 - maps.g, 0.04, 1.0);
            ao = mix(0.4, 1.0, maps.b);
        }
    }
    if (uHasOcclusion && uClay == 0) {
        ao *= texture(uOcclusionMap, occUv).r;
    }
    metallic = clamp(metallic * uMetallicMul, 0.0, 1.0);
    roughness = clamp(roughness * uRoughnessMul, 0.04, 1.0);
    ao = mix(1.0, ao, clamp(uAoMul, 0.0, 2.0));
    vec3 emissive = uEmissive;
    if (uHasEmissive) {
        vec2 emissiveUv = xformUv(vUv0, vUv1, uEmissiveSet, uEmissiveUv, uEmissiveUvRot);
        emissive *= texture(uEmissiveMap, emissiveUv).rgb;
    }
    if (uClay != 0) {
        emissive = vec3(0.0);
    }
    if (uUnlit && uClay == 0) {
        vec3 color = albedo + emissive * uEmissiveGain;
        if (uDimmed != 0) color *= 0.18;
        FragColor = vec4(color, alpha);
        return;
    }
    float sh = shadowAt();
    vec3 direct = vec3(0.0);
    direct += lightContrib(N, V, normalize(uKeyDir), uKeyColor, albedo, metallic, roughness) * sh;
    direct += lightContrib(N, V, normalize(uFillDir), uFillColor, albedo, metallic, roughness);
    direct += lightContrib(N, V, normalize(uRimDir), uRimColor, albedo, metallic, roughness);
    direct += punctualLight(N, V, albedo, metallic, roughness, vWorld);
    direct *= uDirectMul;

    mat3 er = envRot();
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    if (uIridescence > 0.001 && uClay == 0) {
        float thickN = mix(uIridThickMin, uIridThickMax, NdotV);
        if (uHasIridThick) {
            thickN = mix(uIridThickMin, uIridThickMax, texture(uIridThickMap, xformUv(vUv0, vUv1, uIridThickSet, uIridThickUv, uIridThickUvRot)).g);
        }
        float irid = uIridescence;
        if (uHasIridescence) irid *= texture(uIridescenceMap, xformUv(vUv0, vUv1, uIridSet, uIridUv, uIridUvRot)).r;
        vec3 iridF = iridescenceFresnel(NdotV, thickN, F0);
        F0 = mix(F0, iridF, clamp(irid, 0.0, 1.0));
    }
    vec3 F = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 irradiance = finite3(textureLod(uIrrCube, er * N, 0.0).rgb);
    float lod = clamp(roughness, 0.0, 1.0) * 6.0;
    vec3 prefiltered = finite3(textureLod(uPrefCube, er * R, lod).rgb);
    vec2 brdf = envBRDF(NdotV, roughness);
    vec3 specIbl = finite3(prefiltered * (F * brdf.x + brdf.y));
    vec3 diffIbl = kD * albedo * irradiance;
    vec3 ibl = finite3((diffIbl + specIbl) * uEnv * ao);

    float coat = uClearcoat * uClearcoatMul;
    if (uHasClearcoat && uClay == 0) {
        coat *= texture(uClearcoatMap, coatUv).r;
    }
    float coatRough = uClearcoatRoughness;
    if (uHasClearcoatRough && uClay == 0) {
        coatRough *= texture(uClearcoatRoughMap, coatUv).g;
    }
    vec3 coatN = N;
    if (uHasClearcoatNormal && uUseNormals != 0 && uClay == 0) {
        vec3 nts = texture(uClearcoatNormalMap, coatUv).xyz * 2.0 - 1.0;
        coatN = safeNormalize(mat3(T, B, N) * nts);
    }
    vec3 coatSpec = vec3(0.0);
    if (coat > 0.001) {
        vec3 coatR = reflect(-V, coatN);
        vec3 coatF = F_Schlick(clamp(dot(coatN, V), 0.0, 1.0), vec3(0.04));
        coatSpec = finite3(textureLod(uPrefCube, er * coatR, coatRough * 6.0).rgb) * coatF * coat * uEnv;
        coatSpec += lightContrib(coatN, V, safeNormalize(uKeyDir), uKeyColor * 0.4, vec3(1.0), 0.0, coatRough) * coat * sh;
        coatSpec = finite3(coatSpec);
    }
    vec3 sheenCol = uSheenColor;
    float sheenR = max(uSheenRough, 0.04);
    if (uHasSheenColor && uClay == 0) {
        sheenCol *= texture(uSheenColorMap, xformUv(vUv0, vUv1, uSheenColorSet, uSheenColorUv, uSheenColorUvRot)).rgb;
    }
    if (uHasSheenRough && uClay == 0) {
        sheenR *= texture(uSheenRoughMap, xformUv(vUv0, vUv1, uSheenRoughSet, uSheenRoughUv, uSheenRoughUvRot)).a;
    }
    vec3 sheen = sheenCol * irradiance * 0.35 * (1.0 - metallic) * uEnv;
    vec3 trans = vec3(0.0);
    float transmission = uTransmission;
    if (uHasTransmission && uClay == 0) {
        transmission *= texture(uTransmissionMap, xformUv(vUv0, vUv1, uTransmissionSet, uTransmissionUv, uTransmissionUvRot)).r;
    }
    float thickAmt = uThickness;
    if (uHasThickness && uClay == 0) {
        thickAmt *= texture(uThicknessMap, xformUv(vUv0, vUv1, uThicknessSet, uThicknessUv, uThicknessUvRot)).g;
    }
    if (transmission > 0.001) {
        vec3 rt = refract(-V, N, 1.0 / max(uIor, 1.01));
        if (dot(rt, rt) < 1e-6) rt = -R;
        vec3 cubeT = finite3(textureLod(uPrefCube, er * rt, roughness * 4.0).rgb) * albedo;
        vec3 sceneT = cubeT;
        if (uPass != 0) {
            vec2 screen = gl_FragCoord.xy / max(uResolution, vec2(1.0));
            vec2 offset = rt.xy * 0.08 * clamp(thickAmt, 0.0, 2.0);
            sceneT = texture(uSceneColor, clamp(screen + offset, vec2(0.02), vec2(0.98))).rgb * albedo;
        }
        trans = mix(cubeT, sceneT, 0.72);
        if (uAttenuationDistance > 1e-4 && thickAmt > 0.0) {
            trans *= exp(-log(max(uAttenuationColor, vec3(0.001))) * (thickAmt / max(uAttenuationDistance, 1e-4)));
        } else {
            float thick = max(thickAmt, 0.15) * (0.35 + 0.65 * (1.0 - NdotV));
            trans *= mix(vec3(1.0), uAttenuationColor, clamp(thick * 0.35, 0.0, 1.0));
        }
        trans += specIbl * 0.35;
    }
    vec3 opaque = finite3(direct + ibl + coatSpec + sheen + emissive * uEmissiveGain);
    vec3 color = mix(opaque, trans + coatSpec * 0.45 + emissive * uEmissiveGain, clamp(transmission, 0.0, 1.0));
    if (uDimmed != 0) color *= 0.16;

    if (uDebug == 1) color = albedo;
    if (uDebug == 2) color = N * 0.5 + 0.5;
    if (uDebug == 3) color = vec3(roughness);
    if (uDebug == 4) color = vec3(metallic);
    if (uDebug == 5) color = vec3(ao);
    if (uDebug == 6) color = direct;
    if (uDebug == 7) color = specIbl * uEnv;
    if (uDebug == 8) color = vec3(sh);
    if (uDebug == 9) color = emissive * uEmissiveGain;
    FragColor = vec4(finite3(color), mix(alpha, 1.0, uTransmission * 0.35));
}
)";

constexpr const char* kGroundVert = R"(#version 330 core
layout(location = 0) in vec3 aP0;
layout(location = 1) in vec3 aP1;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aSide;
layout(location = 4) in float aEnd;
uniform mat4 uMvp;
uniform vec2 uResolution;
uniform float uThickness;
out vec3 vWorld;
out vec3 vColor;
out float vEdge;
void main() {
    vec4 clip0 = uMvp * vec4(aP0, 1.0);
    vec4 clip1 = uMvp * vec4(aP1, 1.0);
    vec2 ndc0 = clip0.xy / max(abs(clip0.w), 1e-5);
    vec2 ndc1 = clip1.xy / max(abs(clip1.w), 1e-5);
    vec2 dir = ndc1 - ndc0;
    float len = length(dir);
    vec2 n = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
    vec2 perp = vec2(-n.y, n.x);
    vec4 clip = mix(clip0, clip1, aEnd);
    vec2 pixel = 2.0 / max(uResolution, vec2(1.0));
    clip.xy += perp * aSide * uThickness * pixel * clip.w;
    vWorld = mix(aP0, aP1, aEnd);
    vColor = aColor;
    vEdge = aSide;
    gl_Position = clip;
}
)";

constexpr const char* kGroundFrag = R"(#version 330 core
in vec3 vWorld;
in vec3 vColor;
in float vEdge;
out vec4 FragColor;
uniform vec3 uCam;
uniform int uFade;
uniform int uDarkGrid;
void main() {
    float coverage = 1.0 - smoothstep(0.35, 1.0, abs(vEdge));
    float fade = 1.0;
    if (uFade == 1) {
        float dist = length(vWorld.xz - uCam.xz);
        fade = 1.0 - smoothstep(14.0, 48.0, dist);
    }
    float alpha = coverage * fade;
    if (alpha < 0.02) discard;
    vec3 color = vColor;
    if (uDarkGrid == 1) {
        float chroma = max(vColor.r, max(vColor.g, vColor.b)) - min(vColor.r, min(vColor.g, vColor.b));
        if (chroma < 0.08) {
            float t = clamp((vColor.r - 0.30) / 0.18, 0.0, 1.0);
            color = vec3(mix(0.12, 0.20, t));
        }
    }
    FragColor = vec4(color, alpha);
}
)";

constexpr const char* kQuadVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kSkyFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform mat4 uInvView;
uniform mat4 uInvProj;
uniform samplerCube uEnvCube;
uniform float uEnv;
uniform float uEnvYaw;
void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 view = uInvProj * clip;
    view /= view.w;
    vec3 dir = normalize(mat3(uInvView) * view.xyz);
    float c = cos(uEnvYaw);
    float s = sin(uEnvYaw);
    dir = mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c) * dir;
    vec3 col = textureLod(uEnvCube, dir, 0.0).rgb * uEnv;
    col = vec3(
        (isnan(col.x) || isinf(col.x) || col.x < 0.0) ? 0.0 : min(col.x, 50000.0),
        (isnan(col.y) || isinf(col.y) || col.y < 0.0) ? 0.0 : min(col.y, 50000.0),
        (isnan(col.z) || isinf(col.z) || col.z < 0.0) ? 0.0 : min(col.z, 50000.0));
    FragColor = vec4(col, 1.0);
}
)";

constexpr const char* kShadowVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 6) in vec4 aJoints;
layout(location = 7) in vec4 aWeights;
uniform mat4 uMvp;
uniform int uSkinned;
uniform sampler2D uBoneTex;
uniform sampler2D uMorphTex;
uniform int uMorphCount;
uniform float uMorphW[16];
mat4 boneAt(int i) {
    return mat4(
        texelFetch(uBoneTex, ivec2(i, 0), 0),
        texelFetch(uBoneTex, ivec2(i, 1), 0),
        texelFetch(uBoneTex, ivec2(i, 2), 0),
        texelFetch(uBoneTex, ivec2(i, 3), 0));
}
void main() {
    vec3 pos = aPos;
    for (int i = 0; i < uMorphCount && i < 16; ++i) {
        float w = uMorphW[i];
        if (abs(w) < 1e-6) continue;
        pos += texelFetch(uMorphTex, ivec2(gl_VertexID, i * 2), 0).xyz * w;
    }
    if (uSkinned != 0) {
        mat4 skinMat =
            boneAt(int(aJoints.x)) * aWeights.x +
            boneAt(int(aJoints.y)) * aWeights.y +
            boneAt(int(aJoints.z)) * aWeights.z +
            boneAt(int(aJoints.w)) * aWeights.w;
        pos = (skinMat * vec4(pos, 1.0)).xyz;
    }
    gl_Position = uMvp * vec4(pos, 1.0);
}
)";

constexpr const char* kShadowFrag = R"(#version 330 core
void main() {}
)";

constexpr const char* kIblVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 vLocal;
uniform mat4 uMvp;
void main() {
    vLocal = aPos;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

constexpr const char* kIblIrrFrag = R"(#version 330 core
in vec3 vLocal;
out vec4 FragColor;
uniform samplerCube uEnv;
const float PI = 3.14159265;
void main() {
    vec3 N = normalize(vLocal);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);
    vec3 irr = vec3(0.0);
    float n = 0.0;
    const float d = 0.035;
    for (float phi = 0.0; phi < 6.283185; phi += d) {
        for (float theta = 0.0; theta < 1.570796; theta += d) {
            vec3 tan = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 w = tan.x * right + tan.y * up + tan.z * N;
            irr += textureLod(uEnv, w, 3.0).rgb * cos(theta) * sin(theta);
            n += 1.0;
        }
    }
    FragColor = vec4(PI * irr / max(n, 1.0), 1.0);
}
)";

constexpr const char* kIblPrefFrag = R"(#version 330 core
in vec3 vLocal;
out vec4 FragColor;
uniform samplerCube uEnv;
uniform float uRoughness;
const float PI = 3.14159265;
float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), radicalInverse(i)); }
vec3 importanceGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3 H = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tan = normalize(cross(up, N));
    vec3 bit = cross(N, tan);
    return normalize(tan * H.x + bit * H.y + N * H.z);
}
void main() {
    vec3 N = normalize(vLocal);
    vec3 R = N;
    vec3 V = R;
    const uint SAMPLE = 64u;
    vec3 pre = vec3(0.0);
    float tw = 0.0;
    for (uint i = 0u; i < SAMPLE; ++i) {
        vec2 xi = hammersley(i, SAMPLE);
        vec3 H = importanceGGX(xi, N, max(uRoughness, 0.04));
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0) {
            pre += textureLod(uEnv, L, 0.0).rgb * NoL;
            tw += NoL;
        }
    }
    FragColor = vec4(pre / max(tw, 1e-4), 1.0);
}
)";

constexpr const char* kIblLutFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
const float PI = 3.14159265;
float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), radicalInverse(i)); }
float G_Smith(float NoV, float NoL, float a) {
    float k = (a * a) / 2.0;
    float gv = NoV / (NoV * (1.0 - k) + k);
    float gl = NoL / (NoL * (1.0 - k) + k);
    return gv * gl;
}
vec3 importanceGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3 H = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tan = normalize(cross(up, N));
    vec3 bit = cross(N, tan);
    return normalize(tan * H.x + bit * H.y + N * H.z);
}
void main() {
    float NoV = max(vUv.x, 0.001);
    float roughness = vUv.y;
    vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    vec3 N = vec3(0.0, 0.0, 1.0);
    float A = 0.0;
    float B = 0.0;
    const uint SAMPLE = 64u;
    for (uint i = 0u; i < SAMPLE; ++i) {
        vec2 xi = hammersley(i, SAMPLE);
        vec3 H = importanceGGX(xi, N, max(roughness, 0.04));
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);
        if (NoL > 0.0) {
            float G = G_Smith(NoV, NoL, roughness);
            float Gv = (G * VoH) / max(NoH * NoV, 1e-5);
            float Fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - Fc) * Gv;
            B += Fc * Gv;
        }
    }
    FragColor = vec4(A / float(SAMPLE), B / float(SAMPLE), 0.0, 1.0);
}
)";

constexpr const char* kSsaoFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform mat4 uInvProj;
uniform vec2 uTexel;
float depthAt(vec2 uv) {
    return texture(uDepth, uv).r;
}
vec3 viewPos(vec2 uv, float d) {
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 view = uInvProj * clip;
    return view.xyz / view.w;
}
void main() {
    float d = depthAt(vUv);
    if (d > 0.999) {
        FragColor = vec4(1.0);
        return;
    }
    vec3 origin = viewPos(vUv, d);
    float occ = 0.0;
    const vec2 offs[8] = vec2[](
        vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1),
        vec2(1,1), vec2(-1,1), vec2(1,-1), vec2(-1,-1));
    for (int i = 0; i < 8; ++i) {
        vec2 uv = vUv + offs[i] * uTexel * 2.4;
        float sd = depthAt(uv);
        vec3 samplePos = viewPos(uv, sd);
        vec3 dir = samplePos - origin;
        float dist = length(dir);
        float nd = max(dot(normalize(dir), vec3(0,0,1)), 0.0);
        occ += (samplePos.z > origin.z + 0.02 ? 1.0 : 0.0) * smoothstep(0.0, 1.0, 0.85 / (dist + 1e-3)) * nd;
    }
    float ao = 1.0 - clamp(occ / 8.0, 0.0, 0.72);
    FragColor = vec4(vec3(ao), 1.0);
}
)";

constexpr const char* kBrightFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThresh;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(c * smoothstep(uThresh, uThresh + 0.95, lum), 1.0);
}
)";

constexpr const char* kBlurFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uDir;
void main() {
    vec2 texel = uDir / vec2(textureSize(uTex, 0));
    vec3 c = texture(uTex, vUv).rgb * 0.227027;
    c += texture(uTex, vUv + texel * 1.3846).rgb * 0.316216;
    c += texture(uTex, vUv - texel * 1.3846).rgb * 0.316216;
    c += texture(uTex, vUv + texel * 3.2308).rgb * 0.070270;
    c += texture(uTex, vUv - texel * 3.2308).rgb * 0.070270;
    FragColor = vec4(c, 1.0);
}
)";

constexpr const char* kCompositeFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uSsao;
uniform sampler2D uDepth;
uniform float uExposure;
uniform float uBloomAmt;
uniform float uVignette;
uniform float uSsaoAmt;
uniform float uContrast;
uniform float uSaturation;
uniform float uTemperature;
uniform float uTint;
uniform float uSharpen;
uniform float uGrain;
uniform float uCA;
uniform float uTime;
uniform float uDof;
uniform float uFocus;
uniform int uKeepAlpha;
uniform int uTonemap;
uniform int uDebug;
uniform vec3 uBg;
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 filmic(vec3 x) {
    vec3 y = max(vec3(0.0), x - 0.004);
    return (y * (6.2 * y + 0.5)) / (y * (6.2 * y + 1.7) + 0.06);
}
vec3 finite3(vec3 c) {
    return vec3(
        (isnan(c.x) || isinf(c.x) || c.x < 0.0) ? 0.0 : min(c.x, 50000.0),
        (isnan(c.y) || isinf(c.y) || c.y < 0.0) ? 0.0 : min(c.y, 50000.0),
        (isnan(c.z) || isinf(c.z) || c.z < 0.0) ? 0.0 : min(c.z, 50000.0));
}
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void main() {
    vec2 ca = vec2(uCA) * vec2(0.002, 0.0);
    vec4 scene;
    scene.r = texture(uScene, vUv + ca).r;
    scene.g = texture(uScene, vUv).g;
    scene.b = texture(uScene, vUv - ca).b;
    scene.a = texture(uScene, vUv).a;
    vec3 bloom = finite3(texture(uBloom, vUv).rgb);
    float ao = mix(1.0, texture(uSsao, vUv).r, uSsaoAmt);
    vec3 hdr = finite3(scene.rgb) * mix(0.72, 1.0, ao) + bloom * uBloomAmt;
    vec3 exposed = finite3(hdr * uExposure);
    vec3 mapped;
    if (uTonemap == 1) {
        mapped = exposed / (exposed + vec3(1.0));
        mapped = pow(mapped, vec3(1.0 / 2.2));
    } else if (uTonemap == 2) {
        mapped = filmic(exposed);
    } else if (uTonemap == 3) {
        mapped = pow(clamp(exposed, 0.0, 8.0) / 8.0 * 8.0, vec3(1.0 / 2.2));
        mapped = pow(max(exposed, vec3(0.0)), vec3(1.0 / 2.2));
    } else {
        mapped = aces(exposed);
        mapped = pow(mapped, vec3(1.0 / 2.2));
    }
    if (uDebug != 0) {
        mapped = pow(max(scene.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    }
    mapped = mix(uKeepAlpha != 0 ? mapped : uBg, mapped, clamp(scene.a, 0.0, 1.0));
    mapped = (mapped - 0.5) * uContrast + 0.5;
    float grey = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(grey), mapped, uSaturation);
    mapped.r += uTemperature * 0.08;
    mapped.b -= uTemperature * 0.08;
    mapped.g += uTint * 0.06;
    if (uSharpen > 0.001) {
        vec2 px = 1.0 / vec2(textureSize(uScene, 0));
        vec3 blur = texture(uScene, vUv + vec2(px.x, 0.0)).rgb + texture(uScene, vUv - vec2(px.x, 0.0)).rgb
                  + texture(uScene, vUv + vec2(0.0, px.y)).rgb + texture(uScene, vUv - vec2(0.0, px.y)).rgb;
        blur *= 0.25;
        mapped += (mapped - pow(max(blur, vec3(0.0)), vec3(1.0 / 2.2))) * uSharpen;
    }
    mapped += (hash(vUv * vec2(1920.0, 1080.0) + uTime) - 0.5) * uGrain * 0.12;
    float vig = smoothstep(1.18, 0.34, length(vUv - 0.5));
    mapped *= mix(1.0, vig, uVignette);
    if (uDof > 0.001) {
        float z = texture(uDepth, vUv).r;
        float lin = (2.0 * 0.05 * 120.0) / (120.0 + 0.05 - (2.0 * z - 1.0) * (120.0 - 0.05));
        float coc = clamp(abs(lin - uFocus) / max(uFocus, 0.2) * uDof, 0.0, 1.0);
        vec2 px = coc * 6.0 / vec2(textureSize(uScene, 0));
        vec3 blur = mapped;
        blur += pow(max(texture(uScene, vUv + vec2(px.x, 0.0)).rgb, vec3(0.0)), vec3(1.0 / 2.2));
        blur += pow(max(texture(uScene, vUv - vec2(px.x, 0.0)).rgb, vec3(0.0)), vec3(1.0 / 2.2));
        blur += pow(max(texture(uScene, vUv + vec2(0.0, px.y)).rgb, vec3(0.0)), vec3(1.0 / 2.2));
        blur += pow(max(texture(uScene, vUv - vec2(0.0, px.y)).rgb, vec3(0.0)), vec3(1.0 / 2.2));
        mapped = mix(mapped, blur / 5.0, coc);
    }
    float outA = uKeepAlpha != 0 ? clamp(scene.a, 0.0, 1.0) : 1.0;
    FragColor = vec4(clamp(mapped, 0.0, 1.0), outA);
}
)";

constexpr const char* kOverlayVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

constexpr const char* kOverlayFrag = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

constexpr const char* kWireVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

constexpr const char* kWireFrag = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

constexpr const char* kFloorVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec4 vShadow;
void main() {
    vWorld = aPos;
    vShadow = uLightVP * vec4(aPos, 1.0);
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

constexpr const char* kFloorFrag = R"(#version 330 core
in vec3 vWorld;
in vec4 vShadow;
out vec4 FragColor;
uniform vec3 uCam;
uniform sampler2DShadow uShadowMap;
uniform samplerCube uEnvCube;
uniform float uEnv;
uniform float uEnvYaw;
uniform float uShadowStrength;
uniform int uShadowsOn;
uniform vec3 uTint;
mat3 envRot() {
    float c = cos(uEnvYaw);
    float s = sin(uEnvYaw);
    return mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);
}
void main() {
    float dist = length(vWorld.xz);
    float fade = 1.0 - smoothstep(6.0, 14.0, dist);
    if (fade < 0.01) discard;
    float sh = 1.0;
    if (uShadowsOn != 0) {
        vec3 proj = vShadow.xyz / max(vShadow.w, 1e-4);
        proj = proj * 0.5 + 0.5;
        if (proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0 && proj.z <= 1.0) {
            sh = texture(uShadowMap, vec3(proj.xy, proj.z - 0.001));
        }
    }
    vec3 V = normalize(uCam - vWorld);
    vec3 R = reflect(-V, vec3(0.0, 1.0, 0.0));
    vec3 spec = textureLod(uEnvCube, envRot() * R, 5.5).rgb * uEnv * 0.22;
    vec3 shadow = vec3(0.02, 0.025, 0.035) * (1.0 - sh) * uShadowStrength;
    float alpha = clamp((1.0 - sh) * uShadowStrength * 0.85 + 0.18, 0.0, 0.72) * fade;
    vec3 color = mix(uTint * 0.12, spec, 0.55) + shadow;
    FragColor = vec4(color, alpha);
}
)";

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

void setPolygonMode(GLenum face, GLenum mode) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return;
    }
    using PolygonModeFn = void(APIENTRY*)(GLenum, GLenum);
    const auto fn = reinterpret_cast<PolygonModeFn>(ctx->getProcAddress("glPolygonMode"));
    if (fn != nullptr) {
        fn(face, mode);
    }
}

GLuint makeBuffer(QOpenGLExtraFunctions* gl, GLenum target, const void* data, int bytes) {
    GLuint id = 0;
    gl->glGenBuffers(1, &id);
    gl->glBindBuffer(target, id);
    gl->glBufferData(target, bytes, data, GL_STATIC_DRAW);
    return id;
}

void appendAaLine(std::vector<float>* verts, const QVector3D& p0, const QVector3D& p1, float r, float g,
                  float b) {
    auto pushVert = [&](float end, float side) {
        verts->insert(verts->end(), {p0.x(), p0.y(), p0.z(), p1.x(), p1.y(), p1.z(), r, g, b, side, end});
    };
    pushVert(0.0f, -1.0f);
    pushVert(0.0f, 1.0f);
    pushVert(1.0f, -1.0f);
    pushVert(0.0f, 1.0f);
    pushVert(1.0f, 1.0f);
    pushVert(1.0f, -1.0f);
}

void setupLineAttribs(QOpenGLExtraFunctions* gl) {
    const GLsizei stride = static_cast<GLsizei>(11 * sizeof(float));
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(10 * sizeof(float)));
}

QVector3D sampleHdriCube(const HdriImage& hdri, QVector3D dir) {
    if (!hdri.hasCube()) {
        return {};
    }
    dir.normalize();
    const float ax = std::fabs(dir.x());
    const float ay = std::fabs(dir.y());
    const float az = std::fabs(dir.z());
    int face = 0;
    float sc = 0, tc = 0, ma = 1;
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (dir.x() > 0) {
            face = 0;
            sc = -dir.z();
            tc = -dir.y();
        } else {
            face = 1;
            sc = dir.z();
            tc = -dir.y();
        }
    } else if (ay >= ax && ay >= az) {
        ma = ay;
        if (dir.y() > 0) {
            face = 2;
            sc = dir.x();
            tc = dir.z();
        } else {
            face = 3;
            sc = dir.x();
            tc = -dir.z();
        }
    } else {
        ma = az;
        if (dir.z() > 0) {
            face = 4;
            sc = dir.x();
            tc = -dir.y();
        } else {
            face = 5;
            sc = -dir.x();
            tc = -dir.y();
        }
    }
    const float u = 0.5f * (sc / ma + 1.0f);
    const float v = 0.5f * (tc / ma + 1.0f);
    const int size = hdri.cubeSize;
    const float x = std::clamp(u * size - 0.5f, 0.0f, size - 1.001f);
    const float y = std::clamp(v * size - 0.5f, 0.0f, size - 1.001f);
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, size - 1);
    const int y1 = std::min(y0 + 1, size - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    auto at = [&](int ix, int iy) {
        const size_t i = static_cast<size_t>((iy * size + ix) * 4);
        return QVector3D(hdri.cube[face][i], hdri.cube[face][i + 1], hdri.cube[face][i + 2]);
    };
    const QVector3D c = at(x0, y0) * ((1 - fx) * (1 - fy)) + at(x1, y0) * (fx * (1 - fy)) +
                        at(x0, y1) * ((1 - fx) * fy) + at(x1, y1) * (fx * fy);
    return QVector3D(std::min(std::max(c.x(), 0.0f), 50000.0f), std::min(std::max(c.y(), 0.0f), 50000.0f),
                     std::min(std::max(c.z(), 0.0f), 50000.0f));
}

}  // namespace

ModelViewport::ModelViewport(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setAlphaBufferSize(8);
    fmt.setSamples(0);
    fmt.setSwapInterval(1);
    setFormat(fmt);
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAutoFillBackground(false);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &ModelViewport::tick);
    timer_->start();
    clock_.start();
    applyQualityPreset();
    applyLightingPreset();
    loadModelAsync();
    loadHdriAsync();
}

ModelViewport::~ModelViewport() {
    makeCurrent();
    destroyGpu();
    doneCurrent();
}

QString ModelViewport::statusText() const {
    QString extra;
    if (!gpuName_.isEmpty()) {
        extra = QStringLiteral(" · %1 · %2x AA · %3%")
                    .arg(gpuName_.left(28))
                    .arg(msaaActual_)
                    .arg(qRound(renderScale_ * 100.0f));
    }
    if (!hdri_.name.isEmpty()) {
        extra += QStringLiteral(" · %1").arg(hdri_.name);
    }
    if (!cpuModel_.title.isEmpty()) {
        extra += QStringLiteral(" · %1").arg(cpuModel_.title);
    }
    return status_ + extra;
}

QStringList ModelViewport::modelNames() const {
    QStringList names;
    for (const ModelEntry& entry : listPreviewModels()) {
        names << entry.name;
    }
    return names;
}

void ModelViewport::setFov(float degrees) {
    fov_ = qBound(18.0f, degrees, 75.0f);
    update();
}
void ModelViewport::setExposure(float value) {
    exposure_ = qBound(0.2f, value, 3.0f);
    update();
}
void ModelViewport::setBloom(float value) {
    bloom_ = qBound(0.0f, value, 1.5f);
    update();
}
void ModelViewport::setVignette(float value) {
    vignette_ = qBound(0.0f, value, 1.0f);
    update();
}
void ModelViewport::setEnvIntensity(float value) {
    envIntensity_ = qBound(0.0f, value, 3.0f);
    update();
}
void ModelViewport::setKeyIntensity(float value) {
    keyIntensity_ = qBound(0.0f, value, 4.0f);
    update();
}
void ModelViewport::setAutoRotate(bool enabled) {
    autoRotate_ = enabled;
    update();
}
void ModelViewport::setGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}
void ModelViewport::setAxesVisible(bool visible) {
    axesVisible_ = visible;
    update();
}
void ModelViewport::setTexturesEnabled(bool enabled) {
    texturesEnabled_ = enabled;
    update();
}
void ModelViewport::setShadowSize(int size) {
    int clamped = 1024;
    if (size >= 8192) {
        clamped = 8192;
    } else if (size >= 4096) {
        clamped = 4096;
    } else if (size >= 2048) {
        clamped = 2048;
    }
    if (maxTexSize_ > 0) {
        clamped = std::min(clamped, maxTexSize_);
    }
    if (shadowSize_ == clamped) {
        return;
    }
    shadowSize_ = clamped;
    recreateTargets();
    update();
    emit graphicsChanged();
}
void ModelViewport::setIblSize(int size) {
    int clamped = 128;
    if (size >= 1024) {
        clamped = 1024;
    } else if (size >= 512) {
        clamped = 512;
    } else if (size >= 256) {
        clamped = 256;
    }
    if (iblSize_ == clamped) {
        return;
    }
    iblSize_ = clamped;
    if (!hdri_.path.isEmpty()) {
        loadHdriAsync(hdri_.path);
    }
    emit graphicsChanged();
}
void ModelViewport::setAnisotropy(float value) {
    anisotropy_ = qBound(1.0f, value, std::max(1.0f, maxAniso_));
    applyAnisotropy();
    update();
    emit graphicsChanged();
}
void ModelViewport::setBloomPasses(int passes) {
    bloomPasses_ = qBound(0, passes, 6);
    update();
    emit graphicsChanged();
}
void ModelViewport::setBackgroundColor(const QColor& color) {
    backgroundColor_ = color;
    update();
    emit graphicsChanged();
}

void ModelViewport::setLighting(Lighting lighting) {
    lighting_ = lighting;
    applyLightingPreset();
    update();
    emit graphicsChanged();
}

void ModelViewport::setQuality(Quality quality) {
    const int prevIbl = iblSize_;
    quality_ = quality;
    applyQualityPreset();
    applyAnisotropy();
    recreateTargets();
    if (iblSize_ != prevIbl && !hdri_.path.isEmpty()) {
        loadHdriAsync(hdri_.path);
    }
    update();
    emit graphicsChanged();
}

void ModelViewport::setMsaaSamples(int samples) {
    msaaSamples_ = samples;
    recreateTargets();
    update();
    emit graphicsChanged();
}

void ModelViewport::setRenderScale(float scale) {
    renderScale_ = qBound(0.5f, scale, 3.0f);
    recreateTargets();
    update();
    emit graphicsChanged();
}

void ModelViewport::setVSync(bool enabled) {
    vsync_ = enabled;
    applyVSync();
    emit graphicsChanged();
}

void ModelViewport::setHdriPath(const QString& path) {
    requestedHdri_ = path;
    loadHdriAsync(path);
}

void ModelViewport::setModelPath(const QString& path) {
    if (path.isEmpty() || path.compare(requestedModel_, Qt::CaseInsensitive) == 0) {
        return;
    }
    requestedModel_ = path;
    status_ = QStringLiteral("Loading model…");
    emit statusChanged();
    loadModelAsync();
}

void ModelViewport::setSkyVisible(bool visible) {
    skyVisible_ = visible;
    update();
    emit graphicsChanged();
}

void ModelViewport::setKeyEnabled(bool enabled) {
    keyEnabled_ = enabled;
    update();
}
void ModelViewport::setFillEnabled(bool enabled) {
    fillEnabled_ = enabled;
    update();
}
void ModelViewport::setRimEnabled(bool enabled) {
    rimEnabled_ = enabled;
    update();
}
void ModelViewport::setEnvEnabled(bool enabled) {
    envEnabled_ = enabled;
    update();
}
void ModelViewport::setShadowsEnabled(bool enabled) {
    shadowsEnabled_ = enabled;
    update();
}
void ModelViewport::setSsaoEnabled(bool enabled) {
    ssaoEnabled_ = enabled;
    update();
}
void ModelViewport::setBloomEnabled(bool enabled) {
    bloomEnabled_ = enabled;
    update();
}
void ModelViewport::setWireframe(bool enabled) {
    wireframe_ = enabled;
    update();
}
void ModelViewport::setShowLights(bool enabled) {
    showLights_ = enabled;
    update();
}
void ModelViewport::setNormalMaps(bool enabled) {
    normalMaps_ = enabled;
    update();
}
void ModelViewport::setDebugView(DebugView view) {
    debugView_ = view;
    update();
}
void ModelViewport::setTonemap(Tonemap tonemap) {
    tonemap_ = tonemap;
    update();
}
void ModelViewport::setBloomThreshold(float value) {
    bloomThreshold_ = qBound(0.2f, value, 4.0f);
    update();
}
void ModelViewport::setEnvRotation(float degrees) {
    envRotation_ = degrees;
    update();
}
void ModelViewport::setFillIntensity(float value) {
    fillIntensity_ = qBound(0.0f, value, 4.0f);
    update();
}
void ModelViewport::setRimIntensity(float value) {
    rimIntensity_ = qBound(0.0f, value, 4.0f);
    update();
}
void ModelViewport::setKeyYaw(float degrees) {
    keyYaw_ = degrees;
    update();
}
void ModelViewport::setKeyPitch(float degrees) {
    keyPitch_ = qBound(-89.0f, degrees, 89.0f);
    update();
}
void ModelViewport::setFillYaw(float degrees) {
    fillYaw_ = degrees;
    update();
}
void ModelViewport::setFillPitch(float degrees) {
    fillPitch_ = qBound(-89.0f, degrees, 89.0f);
    update();
}
void ModelViewport::setRimYaw(float degrees) {
    rimYaw_ = degrees;
    update();
}
void ModelViewport::setRimPitch(float degrees) {
    rimPitch_ = qBound(-89.0f, degrees, 89.0f);
    update();
}
void ModelViewport::setKeyColor(const QColor& color) {
    keyColor_ = color;
    update();
}
void ModelViewport::setFillColor(const QColor& color) {
    fillColor_ = color;
    update();
}
void ModelViewport::setRimColor(const QColor& color) {
    rimColor_ = color;
    update();
}
void ModelViewport::setContrast(float value) {
    contrast_ = qBound(0.2f, value, 2.5f);
    update();
}
void ModelViewport::setSaturation(float value) {
    saturation_ = qBound(0.0f, value, 2.5f);
    update();
}
void ModelViewport::setTemperature(float value) {
    temperature_ = qBound(-1.0f, value, 1.0f);
    update();
}
void ModelViewport::setTint(float value) {
    tint_ = qBound(-1.0f, value, 1.0f);
    update();
}
void ModelViewport::setSharpen(float value) {
    sharpen_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setGrain(float value) {
    grain_ = qBound(0.0f, value, 1.0f);
    update();
}
void ModelViewport::setChromatic(float value) {
    chromatic_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setSsaoIntensity(float value) {
    ssaoIntensity_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setShadowStrength(float value) {
    shadowStrength_ = qBound(0.0f, value, 1.0f);
    update();
}
void ModelViewport::setShadowSoftness(float value) {
    shadowSoftness_ = qBound(0.15f, value, 4.0f);
    update();
}
void ModelViewport::setNormalScale(float value) {
    normalScale_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setRoughnessMul(float value) {
    roughnessMul_ = qBound(0.05f, value, 3.0f);
    update();
}
void ModelViewport::setMetallicMul(float value) {
    metallicMul_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setAoMul(float value) {
    aoMul_ = qBound(0.0f, value, 2.0f);
    update();
}
void ModelViewport::setClearcoatMul(float value) {
    clearcoatMul_ = qBound(0.0f, value, 3.0f);
    update();
}
void ModelViewport::setDirectMul(float value) {
    directMul_ = qBound(0.0f, value, 3.0f);
    update();
}

void ModelViewport::applyLightingPreset() {
    if (lighting_ == Lighting::Dark) {
        backgroundColor_ = QColor(8, 9, 12);
        exposure_ = 0.92f;
        envIntensity_ = 0.85f;
        keyIntensity_ = 0.40f;
        fillIntensity_ = 0.08f;
        rimIntensity_ = 0.55f;
        bloom_ = 0.22f;
        vignette_ = 0.16f;
        keyYaw_ = 39.0f;
        keyPitch_ = 44.0f;
        fillYaw_ = -97.0f;
        fillPitch_ = 6.0f;
        rimYaw_ = -168.0f;
        rimPitch_ = 26.0f;
        keyColor_ = QColor(255, 220, 190);
        fillColor_ = QColor(70, 90, 130);
        rimColor_ = QColor(140, 180, 255);
    } else {
        backgroundColor_ = QColor(196, 199, 204);
        exposure_ = 1.05f;
        envIntensity_ = 1.15f;
        keyIntensity_ = 0.55f;
        fillIntensity_ = 0.16f;
        rimIntensity_ = 0.22f;
        bloom_ = 0.16f;
        vignette_ = 0.08f;
        keyYaw_ = 38.0f;
        keyPitch_ = 55.0f;
        fillYaw_ = -112.0f;
        fillPitch_ = 14.0f;
        rimYaw_ = -173.0f;
        rimPitch_ = 19.0f;
        keyColor_ = QColor(255, 248, 240);
        fillColor_ = QColor(180, 200, 220);
        rimColor_ = QColor(200, 210, 255);
    }
}

void ModelViewport::applyQualityPreset() {
    switch (quality_) {
        case Quality::Low:
            renderScale_ = 0.70f;
            msaaSamples_ = 0;
            bloomPasses_ = 0;
            shadowSize_ = 1024;
            iblSize_ = 128;
            anisotropy_ = 4.0f;
            break;
        case Quality::Medium:
            renderScale_ = 1.00f;
            msaaSamples_ = 2;
            bloomPasses_ = 1;
            shadowSize_ = 2048;
            iblSize_ = 256;
            anisotropy_ = 8.0f;
            break;
        case Quality::High:
            renderScale_ = 1.15f;
            msaaSamples_ = 4;
            bloomPasses_ = 2;
            shadowSize_ = 2048;
            iblSize_ = 256;
            anisotropy_ = 8.0f;
            break;
        case Quality::Ultra:
            renderScale_ = 1.35f;
            msaaSamples_ = 8;
            bloomPasses_ = 2;
            shadowSize_ = 4096;
            iblSize_ = 512;
            anisotropy_ = 16.0f;
            break;
        case Quality::Extreme:
            renderScale_ = 1.75f;
            msaaSamples_ = 16;
            bloomPasses_ = 4;
            shadowSize_ = 8192;
            iblSize_ = 1024;
            anisotropy_ = 16.0f;
            break;
    }
}

void ModelViewport::applyVSync() {
    QOpenGLContext* ctx = context();
    if (ctx == nullptr) {
        return;
    }
    const bool wasCurrent = QOpenGLContext::currentContext() == ctx;
    if (!wasCurrent) {
        makeCurrent();
    }
    QFunctionPointer proc = ctx->getProcAddress(QByteArrayLiteral("wglSwapIntervalEXT"));
    if (proc == nullptr) {
        proc = ctx->getProcAddress(QByteArrayLiteral("glXSwapIntervalSGI"));
    }
    if (proc != nullptr) {
        using SwapIntervalFn = int (*)(int);
        reinterpret_cast<SwapIntervalFn>(proc)(vsync_ ? 1 : 0);
    }
    if (!wasCurrent) {
        doneCurrent();
    }
}

void ModelViewport::resetCamera() {
    yaw_ = 138.0f;
    pitch_ = 16.0f;
    distance_ = std::max(5.5f, modelSize_.length() * 1.15f);
    fov_ = 40.0f;
    target_ = QVector3D(0.0f, modelCenter_.y() * 0.55f, 0.0f);
    update();
    emit cameraChanged();
}

int ModelViewport::framebufferWidth() const {
    if (captureW_ > 0) {
        return captureW_;
    }
    const int screenW = std::max(1, static_cast<int>(width() * devicePixelRatioF()));
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(screenW) * renderScale_)));
}

int ModelViewport::framebufferHeight() const {
    if (captureH_ > 0) {
        return captureH_;
    }
    const int screenH = std::max(1, static_cast<int>(height() * devicePixelRatioF()));
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(screenH) * renderScale_)));
}

QJsonObject ModelViewport::cameraState() const {
    QJsonObject o;
    o.insert(QStringLiteral("yaw"), yaw_);
    o.insert(QStringLiteral("pitch"), pitch_);
    o.insert(QStringLiteral("distance"), distance_);
    o.insert(QStringLiteral("fov"), fov_);
    o.insert(QStringLiteral("tx"), target_.x());
    o.insert(QStringLiteral("ty"), target_.y());
    o.insert(QStringLiteral("tz"), target_.z());
    return o;
}

void ModelViewport::applyCameraState(const QJsonObject& state) {
    yaw_ = static_cast<float>(state.value(QStringLiteral("yaw")).toDouble(yaw_));
    pitch_ = qBound(-89.0f, static_cast<float>(state.value(QStringLiteral("pitch")).toDouble(pitch_)), 89.0f);
    distance_ = qBound(1.2f, static_cast<float>(state.value(QStringLiteral("distance")).toDouble(distance_)), 40.0f);
    fov_ = qBound(18.0f, static_cast<float>(state.value(QStringLiteral("fov")).toDouble(fov_)), 75.0f);
    target_ = QVector3D(static_cast<float>(state.value(QStringLiteral("tx")).toDouble(target_.x())),
                        static_cast<float>(state.value(QStringLiteral("ty")).toDouble(target_.y())),
                        static_cast<float>(state.value(QStringLiteral("tz")).toDouble(target_.z())));
    update();
    emit cameraChanged();
}

QJsonObject ModelViewport::lookState() const {
    QJsonObject o;
    o.insert(QStringLiteral("lighting"), lighting_ == Lighting::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    o.insert(QStringLiteral("quality"), static_cast<int>(quality_));
    o.insert(QStringLiteral("hdri"), hdri_.path);
    o.insert(QStringLiteral("skyVisible"), skyVisible_);
    o.insert(QStringLiteral("gridVisible"), gridVisible_);
    o.insert(QStringLiteral("axesVisible"), axesVisible_);
    o.insert(QStringLiteral("texturesEnabled"), texturesEnabled_);
    o.insert(QStringLiteral("autoRotate"), autoRotate_);
    o.insert(QStringLiteral("wireframe"), wireframe_);
    o.insert(QStringLiteral("showLights"), showLights_);
    o.insert(QStringLiteral("vsync"), vsync_);
    o.insert(QStringLiteral("keyEnabled"), keyEnabled_);
    o.insert(QStringLiteral("fillEnabled"), fillEnabled_);
    o.insert(QStringLiteral("rimEnabled"), rimEnabled_);
    o.insert(QStringLiteral("envEnabled"), envEnabled_);
    o.insert(QStringLiteral("shadowsEnabled"), shadowsEnabled_);
    o.insert(QStringLiteral("ssaoEnabled"), ssaoEnabled_);
    o.insert(QStringLiteral("bloomEnabled"), bloomEnabled_);
    o.insert(QStringLiteral("clayMode"), clayMode_);
    o.insert(QStringLiteral("floorCatcher"), floorCatcher_);
    o.insert(QStringLiteral("dofEnabled"), dofEnabled_);
    o.insert(QStringLiteral("dofAmount"), dofAmount_);
    o.insert(QStringLiteral("focusDistance"), focusDistance_);
    o.insert(QStringLiteral("lookIndex"), lookIndex_);
    o.insert(QStringLiteral("tonemap"), static_cast<int>(tonemap_));
    o.insert(QStringLiteral("msaa"), msaaSamples_);
    o.insert(QStringLiteral("renderScale"), renderScale_);
    o.insert(QStringLiteral("shadowSize"), shadowSize_);
    o.insert(QStringLiteral("iblSize"), iblSize_);
    o.insert(QStringLiteral("anisotropy"), anisotropy_);
    o.insert(QStringLiteral("bloomPasses"), bloomPasses_);
    o.insert(QStringLiteral("exposure"), exposure_);
    o.insert(QStringLiteral("bloom"), bloom_);
    o.insert(QStringLiteral("bloomThreshold"), bloomThreshold_);
    o.insert(QStringLiteral("vignette"), vignette_);
    o.insert(QStringLiteral("envIntensity"), envIntensity_);
    o.insert(QStringLiteral("envRotation"), envRotation_);
    o.insert(QStringLiteral("keyIntensity"), keyIntensity_);
    o.insert(QStringLiteral("fillIntensity"), fillIntensity_);
    o.insert(QStringLiteral("rimIntensity"), rimIntensity_);
    o.insert(QStringLiteral("keyYaw"), keyYaw_);
    o.insert(QStringLiteral("keyPitch"), keyPitch_);
    o.insert(QStringLiteral("fillYaw"), fillYaw_);
    o.insert(QStringLiteral("fillPitch"), fillPitch_);
    o.insert(QStringLiteral("rimYaw"), rimYaw_);
    o.insert(QStringLiteral("rimPitch"), rimPitch_);
    o.insert(QStringLiteral("contrast"), contrast_);
    o.insert(QStringLiteral("saturation"), saturation_);
    o.insert(QStringLiteral("temperature"), temperature_);
    o.insert(QStringLiteral("tint"), tint_);
    o.insert(QStringLiteral("sharpen"), sharpen_);
    o.insert(QStringLiteral("grain"), grain_);
    o.insert(QStringLiteral("chromatic"), chromatic_);
    o.insert(QStringLiteral("ssaoIntensity"), ssaoIntensity_);
    o.insert(QStringLiteral("shadowStrength"), shadowStrength_);
    o.insert(QStringLiteral("shadowSoftness"), shadowSoftness_);
    o.insert(QStringLiteral("normalScale"), normalScale_);
    o.insert(QStringLiteral("roughnessMul"), roughnessMul_);
    o.insert(QStringLiteral("metallicMul"), metallicMul_);
    o.insert(QStringLiteral("aoMul"), aoMul_);
    o.insert(QStringLiteral("clearcoatMul"), clearcoatMul_);
    o.insert(QStringLiteral("directMul"), directMul_);
    o.insert(QStringLiteral("background"), backgroundColor_.name(QColor::HexRgb));
    o.insert(QStringLiteral("keyColor"), keyColor_.name(QColor::HexRgb));
    o.insert(QStringLiteral("fillColor"), fillColor_.name(QColor::HexRgb));
    o.insert(QStringLiteral("rimColor"), rimColor_.name(QColor::HexRgb));
    return o;
}

void ModelViewport::applyLookState(const QJsonObject& state) {
    const QString lighting = state.value(QStringLiteral("lighting")).toString();
    lighting_ = lighting == QLatin1String("light") ? Lighting::Light : Lighting::Dark;
    quality_ = static_cast<Quality>(qBound(0, state.value(QStringLiteral("quality")).toInt(static_cast<int>(quality_)), 4));
    skyVisible_ = state.value(QStringLiteral("skyVisible")).toBool(skyVisible_);
    gridVisible_ = state.value(QStringLiteral("gridVisible")).toBool(gridVisible_);
    axesVisible_ = state.value(QStringLiteral("axesVisible")).toBool(axesVisible_);
    texturesEnabled_ = state.value(QStringLiteral("texturesEnabled")).toBool(texturesEnabled_);
    autoRotate_ = state.value(QStringLiteral("autoRotate")).toBool(autoRotate_);
    wireframe_ = state.value(QStringLiteral("wireframe")).toBool(wireframe_);
    showLights_ = state.value(QStringLiteral("showLights")).toBool(showLights_);
    vsync_ = state.value(QStringLiteral("vsync")).toBool(vsync_);
    keyEnabled_ = state.value(QStringLiteral("keyEnabled")).toBool(keyEnabled_);
    fillEnabled_ = state.value(QStringLiteral("fillEnabled")).toBool(fillEnabled_);
    rimEnabled_ = state.value(QStringLiteral("rimEnabled")).toBool(rimEnabled_);
    envEnabled_ = state.value(QStringLiteral("envEnabled")).toBool(envEnabled_);
    shadowsEnabled_ = state.value(QStringLiteral("shadowsEnabled")).toBool(shadowsEnabled_);
    ssaoEnabled_ = state.value(QStringLiteral("ssaoEnabled")).toBool(ssaoEnabled_);
    bloomEnabled_ = state.value(QStringLiteral("bloomEnabled")).toBool(bloomEnabled_);
    normalMaps_ = state.value(QStringLiteral("normalMaps")).toBool(normalMaps_);
    clayMode_ = state.value(QStringLiteral("clayMode")).toBool(clayMode_);
    floorCatcher_ = state.value(QStringLiteral("floorCatcher")).toBool(floorCatcher_);
    dofEnabled_ = state.value(QStringLiteral("dofEnabled")).toBool(dofEnabled_);
    dofAmount_ = static_cast<float>(state.value(QStringLiteral("dofAmount")).toDouble(dofAmount_));
    focusDistance_ = static_cast<float>(state.value(QStringLiteral("focusDistance")).toDouble(focusDistance_));
    lookIndex_ = state.value(QStringLiteral("lookIndex")).toInt(lookIndex_);
    tonemap_ = static_cast<Tonemap>(qBound(0, state.value(QStringLiteral("tonemap")).toInt(static_cast<int>(tonemap_)), 3));
    msaaSamples_ = state.value(QStringLiteral("msaa")).toInt(msaaSamples_);
    renderScale_ = qBound(0.5f, static_cast<float>(state.value(QStringLiteral("renderScale")).toDouble(renderScale_)), 3.0f);
    shadowSize_ = state.value(QStringLiteral("shadowSize")).toInt(shadowSize_);
    const int nextIbl = state.value(QStringLiteral("iblSize")).toInt(iblSize_);
    iblSize_ = nextIbl;
    anisotropy_ = static_cast<float>(state.value(QStringLiteral("anisotropy")).toDouble(anisotropy_));
    bloomPasses_ = state.value(QStringLiteral("bloomPasses")).toInt(bloomPasses_);
    exposure_ = static_cast<float>(state.value(QStringLiteral("exposure")).toDouble(exposure_));
    bloom_ = static_cast<float>(state.value(QStringLiteral("bloom")).toDouble(bloom_));
    bloomThreshold_ = static_cast<float>(state.value(QStringLiteral("bloomThreshold")).toDouble(bloomThreshold_));
    vignette_ = static_cast<float>(state.value(QStringLiteral("vignette")).toDouble(vignette_));
    envIntensity_ = static_cast<float>(state.value(QStringLiteral("envIntensity")).toDouble(envIntensity_));
    envRotation_ = static_cast<float>(state.value(QStringLiteral("envRotation")).toDouble(envRotation_));
    keyIntensity_ = static_cast<float>(state.value(QStringLiteral("keyIntensity")).toDouble(keyIntensity_));
    fillIntensity_ = static_cast<float>(state.value(QStringLiteral("fillIntensity")).toDouble(fillIntensity_));
    rimIntensity_ = static_cast<float>(state.value(QStringLiteral("rimIntensity")).toDouble(rimIntensity_));
    keyYaw_ = static_cast<float>(state.value(QStringLiteral("keyYaw")).toDouble(keyYaw_));
    keyPitch_ = static_cast<float>(state.value(QStringLiteral("keyPitch")).toDouble(keyPitch_));
    fillYaw_ = static_cast<float>(state.value(QStringLiteral("fillYaw")).toDouble(fillYaw_));
    fillPitch_ = static_cast<float>(state.value(QStringLiteral("fillPitch")).toDouble(fillPitch_));
    rimYaw_ = static_cast<float>(state.value(QStringLiteral("rimYaw")).toDouble(rimYaw_));
    rimPitch_ = static_cast<float>(state.value(QStringLiteral("rimPitch")).toDouble(rimPitch_));
    contrast_ = static_cast<float>(state.value(QStringLiteral("contrast")).toDouble(contrast_));
    saturation_ = static_cast<float>(state.value(QStringLiteral("saturation")).toDouble(saturation_));
    temperature_ = static_cast<float>(state.value(QStringLiteral("temperature")).toDouble(temperature_));
    tint_ = static_cast<float>(state.value(QStringLiteral("tint")).toDouble(tint_));
    sharpen_ = static_cast<float>(state.value(QStringLiteral("sharpen")).toDouble(sharpen_));
    grain_ = static_cast<float>(state.value(QStringLiteral("grain")).toDouble(grain_));
    chromatic_ = static_cast<float>(state.value(QStringLiteral("chromatic")).toDouble(chromatic_));
    ssaoIntensity_ = static_cast<float>(state.value(QStringLiteral("ssaoIntensity")).toDouble(ssaoIntensity_));
    shadowStrength_ = static_cast<float>(state.value(QStringLiteral("shadowStrength")).toDouble(shadowStrength_));
    shadowSoftness_ = static_cast<float>(state.value(QStringLiteral("shadowSoftness")).toDouble(shadowSoftness_));
    normalScale_ = static_cast<float>(state.value(QStringLiteral("normalScale")).toDouble(normalScale_));
    roughnessMul_ = static_cast<float>(state.value(QStringLiteral("roughnessMul")).toDouble(roughnessMul_));
    metallicMul_ = static_cast<float>(state.value(QStringLiteral("metallicMul")).toDouble(metallicMul_));
    aoMul_ = static_cast<float>(state.value(QStringLiteral("aoMul")).toDouble(aoMul_));
    clearcoatMul_ = static_cast<float>(state.value(QStringLiteral("clearcoatMul")).toDouble(clearcoatMul_));
    directMul_ = static_cast<float>(state.value(QStringLiteral("directMul")).toDouble(directMul_));
    if (state.contains(QStringLiteral("background"))) {
        backgroundColor_ = QColor(state.value(QStringLiteral("background")).toString());
    }
    if (state.contains(QStringLiteral("keyColor"))) {
        keyColor_ = QColor(state.value(QStringLiteral("keyColor")).toString());
    }
    if (state.contains(QStringLiteral("fillColor"))) {
        fillColor_ = QColor(state.value(QStringLiteral("fillColor")).toString());
    }
    if (state.contains(QStringLiteral("rimColor"))) {
        rimColor_ = QColor(state.value(QStringLiteral("rimColor")).toString());
    }
    applyVSync();
    applyAnisotropy();
    if (glReady_) {
        makeCurrent();
        recreateTargets();
    }
    const QString hdriPath = state.value(QStringLiteral("hdri")).toString();
    if (!hdriPath.isEmpty() && hdriPath.compare(hdri_.path, Qt::CaseInsensitive) != 0) {
        setHdriPath(hdriPath);
    } else if (nextIbl != hdri_.cubeSize && !hdri_.path.isEmpty()) {
        loadHdriAsync(hdri_.path);
    }
    update();
    emit graphicsChanged();
}

bool ModelViewport::exportRender(const QString& path, int width, int height) {
    if (path.isEmpty() || width < 64 || height < 64) {
        return false;
    }
    makeCurrent();
    captureW_ = width;
    captureH_ = height;
    recreateTargets();
    updateWorldMatrices();
    renderFrame(width, height, true);
    QImage image(width, height, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, presentFbo_ != 0 ? presentFbo_ : sceneFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    captureW_ = 0;
    captureH_ = 0;
    recreateTargets();
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    image = image.mirrored(false, true);
    const bool ok = image.save(path);
    status_ = ok ? QStringLiteral("Exported %1×%2").arg(width).arg(height)
                 : QStringLiteral("Failed to write %1").arg(QFileInfo(path).fileName());
    emit statusChanged();
    update();
    return ok;
}

void ModelViewport::applyAnisotropy() {
    if (!glReady_) {
        return;
    }
    const float value = std::min(std::max(1.0f, anisotropy_), std::max(1.0f, maxAniso_));
    auto setTex = [&](GLuint tex) {
        if (tex == 0) {
            return;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, value);
    };
    for (const GpuMaterial& mat : materials_) {
        setTex(mat.albedo);
        setTex(mat.normal);
        setTex(mat.maps);
    }
}

void ModelViewport::rebuildEnvironment() {
    if (!glReady_) {
        return;
    }
    const int kSize = hdri_.hasCube() ? hdri_.cubeSize : 256;
    if (envCube_ == 0) {
        glGenTextures(1, &envCube_);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube_);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    std::vector<float> fallback;
    for (int face = 0; face < 6; ++face) {
        const float* data = nullptr;
        if (hdri_.hasCube()) {
            data = hdri_.cube[face].data();
        } else {
            fallback.assign(static_cast<size_t>(kSize * kSize * 4), 0.08f);
            data = fallback.data();
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, kSize, kSize, 0, GL_RGBA, GL_FLOAT, data);
    }
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    auto delTex = [&](GLuint& id) {
        if (id != 0 && id != envCube_) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    };
    delTex(irrCube_);
    delTex(prefCube_);
    if (brdfLut_ != 0) {
        glDeleteTextures(1, &brdfLut_);
        brdfLut_ = 0;
    }

    if (iblIrrProg_ == 0 || iblPrefProg_ == 0 || lutProgram_ == 0 || cubeVao_ == 0) {
        prefCube_ = envCube_;
        return;
    }

    GLint prevFbo = 0;
    GLint prevVp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);

    QMatrix4x4 proj;
    proj.perspective(90.0f, 1.0f, 0.1f, 10.0f);
    const QVector3D centers[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const QVector3D ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

    auto allocCube = [&](GLuint* cube, int size, int mips, bool mipFilter) {
        glGenTextures(1, cube);
        glBindTexture(GL_TEXTURE_CUBE_MAP, *cube);
        for (int mip = 0; mip < mips; ++mip) {
            const int dim = std::max(1, size >> mip);
            for (int f = 0; f < 6; ++f) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, mip, GL_RGBA16F, dim, dim, 0, GL_RGBA, GL_FLOAT,
                             nullptr);
            }
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mipFilter ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        if (mips > 1) {
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mips - 1);
        }
    };

    auto renderCubeFaces = [&](GLuint prog, GLuint cube, int faceSize, int mips, auto setUniforms) {
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glUseProgram(prog);
        glBindVertexArray(cubeVao_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube_);
        glUniform1i(glGetUniformLocation(prog, "uEnv"), 0);
        for (int mip = 0; mip < mips; ++mip) {
            const int dim = std::max(1, faceSize >> mip);
            glViewport(0, 0, dim, dim);
            setUniforms(mip, mips);
            for (int f = 0; f < 6; ++f) {
                QMatrix4x4 view;
                view.lookAt(QVector3D(0, 0, 0), centers[f], ups[f]);
                const QMatrix4x4 mvp = proj * view;
                glUniformMatrix4fv(glGetUniformLocation(prog, "uMvp"), 1, GL_FALSE, mvp.constData());
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cube,
                                       mip);
                glClear(GL_COLOR_BUFFER_BIT);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            }
        }
        glDeleteFramebuffers(1, &fbo);
    };

    constexpr int kIrr = 32;
    allocCube(&irrCube_, kIrr, 1, false);
    renderCubeFaces(iblIrrProg_, irrCube_, kIrr, 1, [](int, int) {});

    constexpr int kPref = 128;
    constexpr int kPrefMips = 7;
    allocCube(&prefCube_, kPref, kPrefMips, true);
    renderCubeFaces(iblPrefProg_, prefCube_, kPref, kPrefMips, [&](int mip, int mips) {
        const float rough = static_cast<float>(mip) / static_cast<float>(std::max(1, mips - 1));
        glUniform1f(glGetUniformLocation(iblPrefProg_, "uRoughness"), rough);
    });

    glGenTextures(1, &brdfLut_);
    glBindTexture(GL_TEXTURE_2D, brdfLut_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 256, 256, 0, GL_RG, GL_FLOAT, nullptr);
    GLuint lutFbo = 0;
    glGenFramebuffers(1, &lutFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, lutFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLut_, 0);
    glViewport(0, 0, 256, 256);
    glUseProgram(lutProgram_);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDeleteFramebuffers(1, &lutFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    glBindVertexArray(0);
    glUseProgram(0);
}

void ModelViewport::initializeGL() {
    initializeOpenGLFunctions();
    glReady_ = true;
    if (!compileProgram(&modelProgram_, kModelVert, kModelFrag) ||
        !compileProgram(&groundProgram_, kGroundVert, kGroundFrag) ||
        !compileProgram(&brightProgram_, kQuadVert, kBrightFrag) ||
        !compileProgram(&blurProgram_, kQuadVert, kBlurFrag) ||
        !compileProgram(&compositeProgram_, kQuadVert, kCompositeFrag) ||
        !compileProgram(&skyProgram_, kQuadVert, kSkyFrag) ||
        !compileProgram(&shadowProgram_, kShadowVert, kShadowFrag) ||
        !compileProgram(&ssaoProgram_, kQuadVert, kSsaoFrag) ||
        !compileProgram(&overlayProgram_, kOverlayVert, kOverlayFrag) ||
        !compileProgram(&wireProgram_, kWireVert, kWireFrag) ||
        !compileProgram(&floorProgram_, kFloorVert, kFloorFrag) ||
        !compileProgram(&iblIrrProg_, kIblVert, kIblIrrFrag) ||
        !compileProgram(&iblPrefProg_, kIblVert, kIblPrefFrag) ||
        !compileProgram(&lutProgram_, kQuadVert, kIblLutFrag)) {
        status_ = QStringLiteral("Failed to compile shaders.");
        emit statusChanged();
        return;
    }

    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    glGenVertexArrays(1, &quadVao_);
    glBindVertexArray(quadVao_);
    quadVbo_ = makeBuffer(this, GL_ARRAY_BUFFER, quad, sizeof(quad));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    std::vector<float> ground;
    auto pushGridLine = [&](float x0, float z0, float x1, float z1, float r, float g, float b) {
        appendAaLine(&ground, QVector3D(x0, 0.0f, z0), QVector3D(x1, 0.0f, z1), r, g, b);
    };
    constexpr int kExtent = 40;
    for (int i = -kExtent; i <= kExtent; ++i) {
        if (i == 0) {
            continue;
        }
        const bool major = (i % 10) == 0;
        const float c = major ? 0.48f : 0.30f;
        pushGridLine(static_cast<float>(i), static_cast<float>(-kExtent), static_cast<float>(i),
                     static_cast<float>(kExtent), c, c, c);
        pushGridLine(static_cast<float>(-kExtent), static_cast<float>(i), static_cast<float>(kExtent),
                     static_cast<float>(i), c, c, c);
    }
    pushGridLine(static_cast<float>(-kExtent), 0.0f, static_cast<float>(kExtent), 0.0f, 0.90f, 0.22f, 0.22f);
    pushGridLine(0.0f, static_cast<float>(-kExtent), 0.0f, static_cast<float>(kExtent), 0.28f, 0.78f, 0.32f);
    glGenVertexArrays(1, &groundVao_);
    glBindVertexArray(groundVao_);
    groundVbo_ = makeBuffer(this, GL_ARRAY_BUFFER, ground.data(), static_cast<int>(ground.size() * sizeof(float)));
    groundCount_ = static_cast<int>(ground.size() / 11);
    setupLineAttribs(this);
    glBindVertexArray(0);

    glGenVertexArrays(1, &gizmoVao_);
    glBindVertexArray(gizmoVao_);
    glGenBuffers(1, &gizmoVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
    glBufferData(GL_ARRAY_BUFFER, 4096, nullptr, GL_DYNAMIC_DRAW);
    setupLineAttribs(this);
    glBindVertexArray(0);

    const float floor[] = {
        -16.f, 0.f, -16.f, 16.f, 0.f, -16.f, -16.f, 0.f, 16.f,
        16.f, 0.f, -16.f, 16.f, 0.f, 16.f, -16.f, 0.f, 16.f,
    };
    glGenVertexArrays(1, &floorVao_);
    glBindVertexArray(floorVao_);
    floorVbo_ = makeBuffer(this, GL_ARRAY_BUFFER, floor, sizeof(floor));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    whiteTex_ = makeSolidTexture(255, 255, 255, 255);
    flatNormalTex_ = makeSolidTexture(128, 128, 255, 255);

    glGenVertexArrays(1, &cubeVao_);
    glGenBuffers(1, &cubeVbo_);
    glGenBuffers(1, &cubeEbo_);
    glBindVertexArray(cubeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &boneTex_);
    glBindTexture(GL_TEXTURE_2D, boneTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kMaxBones, 4, 0, GL_RGBA, GL_FLOAT, nullptr);

    rebuildEnvironment();
    uploadPendingHdri();
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    gpuName_ = renderer != nullptr ? QString::fromUtf8(renderer) : QStringLiteral("Unknown GPU");
    GLint maxSamples = 8;
    GLint maxTex = 8192;
    GLfloat maxAniso = 1.0f;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    maxMsaa_ = std::max(0, static_cast<int>(maxSamples));
    maxTexSize_ = std::max(1024, static_cast<int>(maxTex));
    maxAniso_ = std::max(1.0f, maxAniso);
    anisotropy_ = std::min(anisotropy_, maxAniso_);
    gpuDetails_ = QStringLiteral("%1\nMax MSAA %2x · Max texture %3 · AF %.0fx")
                      .arg(gpuName_)
                      .arg(maxMsaa_)
                      .arg(maxTexSize_)
                      .arg(static_cast<double>(maxAniso_));
    applyVSync();
    recreateTargets();
    uploadPendingModel();
    emit graphicsChanged();
    emit statusChanged();
}

void ModelViewport::resizeGL(int, int) {
    recreateTargets();
}

void ModelViewport::updateWorldMatrices() {
    applyAnimation(animationTime_);
    const std::vector<QMatrix4x4> local = composeWorldMatrices(cpuModel_);
    world_.resize(local.size());
    for (size_t i = 0; i < local.size(); ++i) {
        world_[i] = cpuModel_.fit * local[i];
    }
    deformParts();
}

QMatrix4x4 ModelViewport::viewMatrix() const {
    float unused = fov_;
    const QMatrix4x4 scene = sceneCameraView(&unused);
    if (!scene.isIdentity()) {
        return scene;
    }
    QMatrix4x4 view;
    view.lookAt(cameraPosition(), target_, QVector3D(0, 1, 0));
    return view;
}

QVector3D ModelViewport::cameraPosition() const {
    const float yaw = qDegreesToRadians(yaw_);
    const float pitch = qDegreesToRadians(pitch_);
    return QVector3D(target_.x() + distance_ * std::cos(pitch) * std::sin(yaw),
                     target_.y() + distance_ * std::sin(pitch),
                     target_.z() + distance_ * std::cos(pitch) * std::cos(yaw));
}

QVector3D ModelViewport::lightDirection(float yawDeg, float pitchDeg) const {
    const float yaw = qDegreesToRadians(yawDeg);
    const float pitch = qDegreesToRadians(qBound(-89.0f, pitchDeg, 89.0f));
    return QVector3D(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)).normalized();
}

QVector3D ModelViewport::keyDirection() const {
    return lightDirection(keyYaw_, keyPitch_);
}

QMatrix4x4 ModelViewport::lightViewProjection() const {
    const QVector3D center = modelCenter_;
    const float radius = std::max(3.5f, modelSize_.length() * 0.62f);
    const QVector3D light = center + keyDirection() * (radius * 2.4f);
    QMatrix4x4 view;
    view.lookAt(light, center, QVector3D(0, 1, 0));
    QMatrix4x4 proj;
    proj.ortho(-radius, radius, -radius, radius, 0.2f, radius * 6.0f);
    return proj * view;
}

void ModelViewport::uploadBoneTexture(int skinIndex, int meshNode) {
    if (boneTex_ == 0) {
        return;
    }
    std::vector<float> pixels(static_cast<size_t>(kMaxBones) * 4u * 4u, 0.0f);
    QMatrix4x4 identity;
    for (int i = 0; i < kMaxBones; ++i) {
        const float* m = identity.constData();
        for (int r = 0; r < 4; ++r) {
            std::memcpy(pixels.data() + static_cast<size_t>(r * kMaxBones + i) * 4u, m + r * 4, 4 * sizeof(float));
        }
    }
    if (skinIndex >= 0 && skinIndex < static_cast<int>(cpuModel_.gltfSkins.size())) {
        const GltfSkin& skin = cpuModel_.gltfSkins[static_cast<size_t>(skinIndex)];
        QMatrix4x4 meshWorld;
        if (meshNode >= 0 && meshNode < static_cast<int>(world_.size())) {
            meshWorld = world_[static_cast<size_t>(meshNode)];
        }
        const QMatrix4x4 invMesh = meshWorld.inverted();
        const int n = std::min(static_cast<int>(skin.joints.size()), kMaxBones);
        for (int j = 0; j < n; ++j) {
            const int joint = skin.joints[static_cast<size_t>(j)];
            QMatrix4x4 jointWorld = (joint >= 0 && joint < static_cast<int>(world_.size()))
                                        ? world_[static_cast<size_t>(joint)]
                                        : QMatrix4x4();
            const QMatrix4x4 ibm = j < static_cast<int>(skin.inverseBind.size()) ? skin.inverseBind[static_cast<size_t>(j)]
                                                                                : QMatrix4x4();
            const QMatrix4x4 bone = invMesh * jointWorld * ibm;
            const float* m = bone.constData();
            for (int r = 0; r < 4; ++r) {
                std::memcpy(pixels.data() + static_cast<size_t>(r * kMaxBones + j) * 4u, m + r * 4, 4 * sizeof(float));
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, boneTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kMaxBones, 4, GL_RGBA, GL_FLOAT, pixels.data());
}

void ModelViewport::bindPartSkin(GLuint program, const GpuPart& part) {
    uploadBoneTexture(part.skin, part.node);
    glUniform1i(glGetUniformLocation(program, "uBoneTex"), 14);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_2D, boneTex_);
    glUniform1i(glGetUniformLocation(program, "uSkinned"), part.gpuSkin ? 1 : 0);
    const int morphCount = part.gpuMorph ? part.morphCount : 0;
    glUniform1i(glGetUniformLocation(program, "uMorphCount"), morphCount);
    float mw[kMaxMorphs] = {};
    const std::vector<float>* weights = &morphWeights_;
    if (part.node >= 0 && part.node < static_cast<int>(cpuModel_.nodes.size()) &&
        !cpuModel_.nodes[static_cast<size_t>(part.node)].morphWeights.empty()) {
        weights = &cpuModel_.nodes[static_cast<size_t>(part.node)].morphWeights;
    }
    for (int m = 0; m < morphCount && m < kMaxMorphs && m < static_cast<int>(weights->size()); ++m) {
        mw[m] = (*weights)[static_cast<size_t>(m)];
    }
    glUniform1fv(glGetUniformLocation(program, "uMorphW"), kMaxMorphs, mw);
    glUniform1i(glGetUniformLocation(program, "uMorphTex"), 21);
    glActiveTexture(GL_TEXTURE21);
    glBindTexture(GL_TEXTURE_2D, part.morphTex != 0 ? part.morphTex : whiteTex_);
}

void ModelViewport::drawShadow() {
    if (!shadowsEnabled_ || !keyEnabled_ || shadowFbo_ == 0 || shadowProgram_ == 0) {
        return;
    }
    const QMatrix4x4 vp = lightViewProjection();

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(shadowProgram_);
    for (const GpuPart& part : parts_) {
        if (!partVisible(part)) {
            continue;
        }
        QMatrix4x4 model;
        if (part.node >= 0 && part.node < static_cast<int>(world_.size())) {
            model = world_[static_cast<size_t>(part.node)];
        }
        const QMatrix4x4 mvp = vp * model;
        glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uMvp"), 1, GL_FALSE, mvp.constData());
        bindPartSkin(shadowProgram_, part);
        glBindVertexArray(part.vao);
        glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(0);
}

void ModelViewport::drawSsao(const QMatrix4x4& proj) {
    if (ssaoFbo_ == 0 || ssaoProgram_ == 0 || sceneDepth_ == 0) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
    glViewport(0, 0, std::max(1, targetW_ / 2), std::max(1, targetH_ / 2));
    glDisable(GL_DEPTH_TEST);
    glUseProgram(ssaoProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneDepth_);
    glUniform1i(glGetUniformLocation(ssaoProgram_, "uDepth"), 0);
    const QMatrix4x4 inv = proj.inverted();
    glUniformMatrix4fv(glGetUniformLocation(ssaoProgram_, "uInvProj"), 1, GL_FALSE, inv.constData());
    glUniform2f(glGetUniformLocation(ssaoProgram_, "uTexel"), 1.0f / std::max(1, targetW_),
                1.0f / std::max(1, targetH_));
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void ModelViewport::drawFloor(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& cam) {
    if (floorProgram_ == 0 || floorVao_ == 0) {
        return;
    }
    const QMatrix4x4 vp = proj * view;
    const QMatrix4x4 lightVP = lightViewProjection();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(floorProgram_);
    glUniformMatrix4fv(glGetUniformLocation(floorProgram_, "uMvp"), 1, GL_FALSE, vp.constData());
    glUniformMatrix4fv(glGetUniformLocation(floorProgram_, "uLightVP"), 1, GL_FALSE, lightVP.constData());
    glUniform3f(glGetUniformLocation(floorProgram_, "uCam"), cam.x(), cam.y(), cam.z());
    glUniform1f(glGetUniformLocation(floorProgram_, "uEnv"), envEnabled_ ? envIntensity_ : 0.35f);
    glUniform1f(glGetUniformLocation(floorProgram_, "uEnvYaw"), qDegreesToRadians(envRotation_));
    glUniform1f(glGetUniformLocation(floorProgram_, "uShadowStrength"), shadowStrength_);
    glUniform1i(glGetUniformLocation(floorProgram_, "uShadowsOn"), (shadowsEnabled_ && keyEnabled_) ? 1 : 0);
    glUniform3f(glGetUniformLocation(floorProgram_, "uTint"), static_cast<float>(backgroundColor_.redF()),
                static_cast<float>(backgroundColor_.greenF()), static_cast<float>(backgroundColor_.blueF()));
    glUniform1i(glGetUniformLocation(floorProgram_, "uShadowMap"), 0);
    glUniform1i(glGetUniformLocation(floorProgram_, "uEnvCube"), 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube_);
    glBindVertexArray(floorVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
}

void ModelViewport::drawScene(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& cam, int transPass) {
    const QMatrix4x4 vp = proj * view;
    const QMatrix4x4 lightVP = lightViewProjection();
    const bool dark = lighting_ == Lighting::Dark;
    const QVector3D keyDir = keyDirection();
    const QVector3D fillDir = lightDirection(fillYaw_, fillPitch_);
    const QVector3D rimDir = lightDirection(rimYaw_, rimPitch_);
    const float envYaw = qDegreesToRadians(envRotation_);
    const float envAmt = envEnabled_ ? envIntensity_ : 0.0f;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    if (transPass == 0 && skyVisible_ && !transparentBg_ && envCube_ != 0 && skyProgram_ != 0) {
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glUseProgram(skyProgram_);
        const QMatrix4x4 invView = view.inverted();
        const QMatrix4x4 invProj = proj.inverted();
        glUniformMatrix4fv(glGetUniformLocation(skyProgram_, "uInvView"), 1, GL_FALSE, invView.constData());
        glUniformMatrix4fv(glGetUniformLocation(skyProgram_, "uInvProj"), 1, GL_FALSE, invProj.constData());
        glUniform1f(glGetUniformLocation(skyProgram_, "uEnv"), envAmt);
        glUniform1f(glGetUniformLocation(skyProgram_, "uEnvYaw"), envYaw);
        glUniform1i(glGetUniformLocation(skyProgram_, "uEnvCube"), 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube_);
        glBindVertexArray(quadVao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }

    if (transPass == 0 && gridVisible_ && groundProgram_ != 0) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawLineMesh(groundVao_, groundCount_, vp, cam, 1.35f, true);
        glDepthMask(GL_TRUE);
    }

    if (transPass == 0 && floorCatcher_ && !gridVisible_) {
        drawFloor(view, proj, cam);
    }

    if (!uploaded_ || modelProgram_ == 0) {
        return;
    }

    auto lightRgb = [](const QColor& c, float intensity, bool on) {
        const float s = on ? intensity : 0.0f;
        return QVector3D(static_cast<float>(c.redF()) * s, static_cast<float>(c.greenF()) * s,
                         static_cast<float>(c.blueF()) * s);
    };
    const QVector3D keyRgb = lightRgb(keyColor_, keyIntensity_, keyEnabled_);
    const QVector3D fillRgb = lightRgb(fillColor_, fillIntensity_, fillEnabled_);
    const QVector3D rimRgb = lightRgb(rimColor_, rimIntensity_, rimEnabled_);

    glUseProgram(modelProgram_);
    glUniform3f(glGetUniformLocation(modelProgram_, "uCam"), cam.x(), cam.y(), cam.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uKeyDir"), keyDir.x(), keyDir.y(), keyDir.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uKeyColor"), keyRgb.x(), keyRgb.y(), keyRgb.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uFillDir"), fillDir.x(), fillDir.y(), fillDir.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uFillColor"), fillRgb.x(), fillRgb.y(), fillRgb.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uRimDir"), rimDir.x(), rimDir.y(), rimDir.z());
    glUniform3f(glGetUniformLocation(modelProgram_, "uRimColor"), rimRgb.x(), rimRgb.y(), rimRgb.z());
    int lightCount = 0;
    if (sceneLights_) {
        for (const GltfLight& light : cpuModel_.lights) {
            if (lightCount >= kMaxLights) {
                break;
            }
            QMatrix4x4 world;
            if (light.node >= 0 && light.node < static_cast<int>(world_.size())) {
                world = world_[static_cast<size_t>(light.node)];
            }
            const QVector3D pos = world.map(QVector3D(0, 0, 0));
            const QVector3D dir = world.mapVector(QVector3D(0, 0, -1)).normalized();
            const float scale = light.type == 0 ? 0.08f : 0.0015f;
            glUniform3f(glGetUniformLocation(modelProgram_, qPrintable(QStringLiteral("uLPos[%1]").arg(lightCount))),
                        pos.x(), pos.y(), pos.z());
            glUniform3f(glGetUniformLocation(modelProgram_, qPrintable(QStringLiteral("uLDir[%1]").arg(lightCount))),
                        dir.x(), dir.y(), dir.z());
            glUniform3f(glGetUniformLocation(modelProgram_, qPrintable(QStringLiteral("uLCol[%1]").arg(lightCount))),
                        light.color.x() * light.intensity * scale, light.color.y() * light.intensity * scale,
                        light.color.z() * light.intensity * scale);
            glUniform4f(glGetUniformLocation(modelProgram_, qPrintable(QStringLiteral("uLData[%1]").arg(lightCount))),
                        static_cast<float>(light.type), light.range, std::cos(light.innerCone),
                        std::cos(light.outerCone));
            ++lightCount;
        }
    }
    glUniform1i(glGetUniformLocation(modelProgram_, "uLightCount"), lightCount);
    glUniform1i(glGetUniformLocation(modelProgram_, "uBrdfLut"), 8);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, brdfLut_ != 0 ? brdfLut_ : whiteTex_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uEnv"), envAmt);
    glUniform1f(glGetUniformLocation(modelProgram_, "uEnvYaw"), envYaw);
    glUniform1f(glGetUniformLocation(modelProgram_, "uEmissiveGain"), 2.6f);
    glUniform1f(glGetUniformLocation(modelProgram_, "uNormalScale"), normalScale_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uRoughnessMul"), roughnessMul_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uMetallicMul"), metallicMul_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uAoMul"), aoMul_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uClearcoatMul"), clearcoatMul_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uDirectMul"), directMul_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uShadowStrength"), shadowStrength_);
    glUniform1f(glGetUniformLocation(modelProgram_, "uShadowSoft"), shadowSoftness_);
    glUniform1i(glGetUniformLocation(modelProgram_, "uDebug"), static_cast<int>(debugView_));
    glUniform1i(glGetUniformLocation(modelProgram_, "uUseNormals"), normalMaps_ ? 1 : 0);
    glUniform1i(glGetUniformLocation(modelProgram_, "uShadowsOn"),
                (shadowsEnabled_ && keyEnabled_) ? 1 : 0);
    glUniform1i(glGetUniformLocation(modelProgram_, "uClay"), clayMode_ ? 1 : 0);
    glUniform1i(glGetUniformLocation(modelProgram_, "uPass"), transPass);
    glUniform2f(glGetUniformLocation(modelProgram_, "uResolution"), static_cast<float>(std::max(1, targetW_)),
                static_cast<float>(std::max(1, targetH_)));
    glUniform1i(glGetUniformLocation(modelProgram_, "uAlbedoMap"), 0);
    glUniform1i(glGetUniformLocation(modelProgram_, "uEnvCube"), 1);
    glUniform1i(glGetUniformLocation(modelProgram_, "uIrrCube"), 2);
    glUniform1i(glGetUniformLocation(modelProgram_, "uShadowMap"), 3);
    glUniform1i(glGetUniformLocation(modelProgram_, "uNormalMap"), 4);
    glUniform1i(glGetUniformLocation(modelProgram_, "uMaps"), 5);
    glUniform1i(glGetUniformLocation(modelProgram_, "uEmissiveMap"), 6);
    glUniform1i(glGetUniformLocation(modelProgram_, "uOcclusionMap"), 7);
    glUniform1i(glGetUniformLocation(modelProgram_, "uTransmissionMap"), 12);
    glUniform1i(glGetUniformLocation(modelProgram_, "uThicknessMap"), 13);
    glUniform1i(glGetUniformLocation(modelProgram_, "uSheenColorMap"), 15);
    glUniform1i(glGetUniformLocation(modelProgram_, "uSheenRoughMap"), 16);
    glUniform1i(glGetUniformLocation(modelProgram_, "uIridescenceMap"), 17);
    glUniform1i(glGetUniformLocation(modelProgram_, "uIridThickMap"), 18);
    glUniform1i(glGetUniformLocation(modelProgram_, "uPrefCube"), 19);
    glUniform1i(glGetUniformLocation(modelProgram_, "uSceneColor"), 20);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irrCube_ != 0 ? irrCube_ : envCube_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE19);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefCube_ != 0 ? prefCube_ : envCube_);
    glActiveTexture(GL_TEXTURE20);
    glBindTexture(GL_TEXTURE_2D, opaqueColor_ != 0 ? opaqueColor_ : whiteTex_);

    auto drawPart = [&](const GpuPart& part, bool transparent) {
        if (!partVisible(part)) {
            return;
        }
        const int matIndex = qBound(0, part.material, static_cast<int>(materials_.size()) - 1);
        const GpuMaterial& mat = materials_.at(matIndex);
        const bool transMat = mat.cpu.blend || mat.cpu.transmission > 0.001f;
        if (transMat != transparent) {
            return;
        }
        const bool dimmed = isolatedNode_ >= 0 && part.node != isolatedNode_;
        QMatrix4x4 model;
        if (part.node >= 0 && part.node < static_cast<int>(world_.size())) {
            model = world_[static_cast<size_t>(part.node)];
        }
        const QMatrix4x4 mvp = vp * model;
        glUniformMatrix4fv(glGetUniformLocation(modelProgram_, "uMvp"), 1, GL_FALSE, mvp.constData());
        glUniformMatrix4fv(glGetUniformLocation(modelProgram_, "uModel"), 1, GL_FALSE, model.constData());
        glUniformMatrix4fv(glGetUniformLocation(modelProgram_, "uLightVP"), 1, GL_FALSE, lightVP.constData());
        glUniform4f(glGetUniformLocation(modelProgram_, "uBaseColor"), mat.cpu.baseColor.x(), mat.cpu.baseColor.y(),
                    mat.cpu.baseColor.z(), mat.cpu.baseColor.w());
        glUniform1f(glGetUniformLocation(modelProgram_, "uMetallic"), mat.cpu.metallic);
        glUniform1f(glGetUniformLocation(modelProgram_, "uRoughness"), mat.cpu.roughness);
        glUniform3f(glGetUniformLocation(modelProgram_, "uEmissive"), mat.cpu.emissive.x(), mat.cpu.emissive.y(),
                    mat.cpu.emissive.z());
        glUniform1f(glGetUniformLocation(modelProgram_, "uEmissiveGain"),
                    2.6f * std::max(0.0f, mat.cpu.emissiveStrength));
        glUniform1f(glGetUniformLocation(modelProgram_, "uClearcoat"), mat.cpu.clearcoat);
        glUniform1f(glGetUniformLocation(modelProgram_, "uClearcoatRoughness"), mat.cpu.clearcoatRoughness);
        glUniform1f(glGetUniformLocation(modelProgram_, "uAlphaCut"), mat.cpu.alphaCutoff);
        glUniform1f(glGetUniformLocation(modelProgram_, "uTransmission"), clayMode_ ? 0.0f : mat.cpu.transmission);
        glUniform1f(glGetUniformLocation(modelProgram_, "uIor"), mat.cpu.ior);
        glUniform1f(glGetUniformLocation(modelProgram_, "uThickness"), mat.cpu.thickness);
        glUniform3f(glGetUniformLocation(modelProgram_, "uAttenuationColor"), mat.cpu.attenuationColor.x(),
                    mat.cpu.attenuationColor.y(), mat.cpu.attenuationColor.z());
        glUniform1f(glGetUniformLocation(modelProgram_, "uAttenuationDistance"), mat.cpu.attenuationDistance);
        glUniform1f(glGetUniformLocation(modelProgram_, "uIridescence"), clayMode_ ? 0.0f : mat.cpu.iridescence);
        glUniform1f(glGetUniformLocation(modelProgram_, "uIridIor"), mat.cpu.iridescenceIor);
        glUniform1f(glGetUniformLocation(modelProgram_, "uIridThickMin"), mat.cpu.iridescenceThicknessMin);
        glUniform1f(glGetUniformLocation(modelProgram_, "uIridThickMax"), mat.cpu.iridescenceThicknessMax);
        glUniform3f(glGetUniformLocation(modelProgram_, "uSheenColor"), mat.cpu.sheenColor.x(), mat.cpu.sheenColor.y(),
                    mat.cpu.sheenColor.z());
        glUniform1f(glGetUniformLocation(modelProgram_, "uSheenRough"), mat.cpu.sheenRoughness);
        glUniform1i(glGetUniformLocation(modelProgram_, "uUnlit"), mat.cpu.unlit ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uDimmed"), dimmed ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasAlbedo"),
                    (texturesEnabled_ && mat.hasAlbedo) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasNormal"),
                    (texturesEnabled_ && mat.hasNormal) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasMaps"), (texturesEnabled_ && mat.hasMaps) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasEmissive"),
                    (texturesEnabled_ && mat.hasEmissive) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasOcclusion"),
                    (texturesEnabled_ && mat.hasOcclusion) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasClearcoat"),
                    (texturesEnabled_ && mat.hasClearcoat) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasClearcoatRough"),
                    (texturesEnabled_ && mat.hasClearcoatRough) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasClearcoatNormal"),
                    (texturesEnabled_ && mat.hasClearcoatNormal) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasTransmission"),
                    (texturesEnabled_ && mat.hasTransmission) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasThickness"),
                    (texturesEnabled_ && mat.hasThickness) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasSheenColor"),
                    (texturesEnabled_ && mat.hasSheenColor) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasSheenRough"),
                    (texturesEnabled_ && mat.hasSheenRough) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasIridescence"),
                    (texturesEnabled_ && mat.hasIridescence) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uHasIridThick"),
                    (texturesEnabled_ && mat.hasIridescenceThickness) ? 1 : 0);
        glUniform1i(glGetUniformLocation(modelProgram_, "uGltfMaps"), mat.cpu.gltfMaps ? 1 : 0);
        auto setUv = [&](const char* stName, const char* rotName, const char* setName, const TextureTransform& uv) {
            glUniform4f(glGetUniformLocation(modelProgram_, stName), uv.scale.x(), uv.scale.y(), uv.offset.x(),
                        uv.offset.y());
            glUniform1f(glGetUniformLocation(modelProgram_, rotName), uv.rotation);
            glUniform1i(glGetUniformLocation(modelProgram_, setName), uv.texCoord);
        };
        glUniform1i(glGetUniformLocation(modelProgram_, "uClearcoatMap"), 9);
        glUniform1i(glGetUniformLocation(modelProgram_, "uClearcoatRoughMap"), 10);
        glUniform1i(glGetUniformLocation(modelProgram_, "uClearcoatNormalMap"), 11);
        setUv("uAlbedoUv", "uAlbedoUvRot", "uAlbedoSet", mat.cpu.albedoUv);
        setUv("uNormalUv", "uNormalUvRot", "uNormalSet", mat.cpu.normalUv);
        setUv("uMapsUv", "uMapsUvRot", "uMapsSet", mat.cpu.mapsUv);
        setUv("uEmissiveUv", "uEmissiveUvRot", "uEmissiveSet", mat.cpu.emissiveUv);
        setUv("uOcclusionUv", "uOcclusionUvRot", "uOcclusionSet", mat.cpu.occlusionUv);
        setUv("uClearcoatUv", "uClearcoatUvRot", "uClearcoatSet", mat.cpu.clearcoatUv);
        setUv("uTransmissionUv", "uTransmissionUvRot", "uTransmissionSet", mat.cpu.transmissionUv);
        setUv("uThicknessUv", "uThicknessUvRot", "uThicknessSet", mat.cpu.thicknessUv);
        setUv("uSheenColorUv", "uSheenColorUvRot", "uSheenColorSet", mat.cpu.sheenColorUv);
        setUv("uSheenRoughUv", "uSheenRoughUvRot", "uSheenRoughSet", mat.cpu.sheenRoughUv);
        setUv("uIridUv", "uIridUvRot", "uIridSet", mat.cpu.iridescenceUv);
        setUv("uIridThickUv", "uIridThickUvRot", "uIridThickSet", mat.cpu.iridescenceThicknessUv);
        auto bind2d = [&](int unit, GLuint tex, GLuint fallback) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex != 0 ? tex : fallback);
        };
        bind2d(0, mat.hasAlbedo ? mat.albedo : 0, whiteTex_);
        bind2d(4, mat.hasNormal ? mat.normal : 0, flatNormalTex_);
        bind2d(5, mat.hasMaps ? mat.maps : 0, whiteTex_);
        bind2d(6, mat.hasEmissive ? mat.emissive : 0, whiteTex_);
        bind2d(7, mat.hasOcclusion ? mat.occlusion : 0, whiteTex_);
        bind2d(9, mat.hasClearcoat ? mat.clearcoat : 0, whiteTex_);
        bind2d(10, mat.hasClearcoatRough ? mat.clearcoatRough : 0, whiteTex_);
        bind2d(11, mat.hasClearcoatNormal ? mat.clearcoatNormal : 0, flatNormalTex_);
        bind2d(12, mat.hasTransmission ? mat.transmission : 0, whiteTex_);
        bind2d(13, mat.hasThickness ? mat.thickness : 0, whiteTex_);
        bind2d(15, mat.hasSheenColor ? mat.sheenColor : 0, whiteTex_);
        bind2d(16, mat.hasSheenRough ? mat.sheenRough : 0, whiteTex_);
        bind2d(17, mat.hasIridescence ? mat.iridescence : 0, whiteTex_);
        bind2d(18, mat.hasIridescenceThickness ? mat.iridescenceThickness : 0, whiteTex_);
        bindPartSkin(modelProgram_, part);
        if (mat.cpu.doubleSided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
        }
        glBindVertexArray(part.vao);
        glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
    };

    if (transPass == 0) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        for (const GpuPart& part : parts_) {
            drawPart(part, false);
        }
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const GpuPart& part : parts_) {
            drawPart(part, true);
        }
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_CULL_FACE);

    if (transPass == 0 && wireframe_ && wireProgram_ != 0) {
        setPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        glUseProgram(wireProgram_);
        const QColor wc = dark ? QColor(220, 230, 240) : QColor(20, 22, 28);
        glUniform3f(glGetUniformLocation(wireProgram_, "uColor"), static_cast<float>(wc.redF()),
                    static_cast<float>(wc.greenF()), static_cast<float>(wc.blueF()));
        for (const GpuPart& part : parts_) {
            if (!partVisible(part)) {
                continue;
            }
            QMatrix4x4 model;
            if (part.node >= 0 && part.node < static_cast<int>(world_.size())) {
                model = world_[static_cast<size_t>(part.node)];
            }
            const QMatrix4x4 mvp = vp * model;
            glUniformMatrix4fv(glGetUniformLocation(wireProgram_, "uMvp"), 1, GL_FALSE, mvp.constData());
            glBindVertexArray(part.vao);
            glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
        }
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_LINE);
        setPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    if (transPass == 0 && axesVisible_) {
        drawAxes(view, proj);
    }
    glBindVertexArray(0);
}

void ModelViewport::drawLineMesh(GLuint vao, int vertices, const QMatrix4x4& mvp, const QVector3D& cam,
                                float thickness, bool fade) {
    if (vao == 0 || vertices <= 0 || groundProgram_ == 0) {
        return;
    }
    GLint vp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, vp);
    glUseProgram(groundProgram_);
    glUniformMatrix4fv(glGetUniformLocation(groundProgram_, "uMvp"), 1, GL_FALSE, mvp.constData());
    glUniform3f(glGetUniformLocation(groundProgram_, "uCam"), cam.x(), cam.y(), cam.z());
    glUniform2f(glGetUniformLocation(groundProgram_, "uResolution"),
                static_cast<float>(std::max(1, vp[2])), static_cast<float>(std::max(1, vp[3])));
    glUniform1f(glGetUniformLocation(groundProgram_, "uThickness"), thickness);
    glUniform1i(glGetUniformLocation(groundProgram_, "uFade"), fade ? 1 : 0);
    glUniform1i(glGetUniformLocation(groundProgram_, "uDarkGrid"),
                (fade && lighting_ == Lighting::Dark) ? 1 : 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertices);
}

void ModelViewport::drawLightGizmos(const QMatrix4x4& view, const QMatrix4x4& proj) {
    if (!showLights_ || gizmoVao_ == 0) {
        return;
    }
    const QVector3D center = QVector3D(0.0f, modelCenter_.y() * 0.45f, 0.0f);
    const float len = std::max(2.4f, modelSize_.length() * 0.85f);
    struct Ray {
        QVector3D dir;
        QColor color;
        bool on;
    };
    const Ray rays[] = {
        {keyDirection(), keyColor_, keyEnabled_},
        {lightDirection(fillYaw_, fillPitch_), fillColor_, fillEnabled_},
        {lightDirection(rimYaw_, rimPitch_), rimColor_, rimEnabled_},
    };
    std::vector<float> verts;
    for (const Ray& ray : rays) {
        if (!ray.on) {
            continue;
        }
        const QVector3D tip = center + ray.dir * len;
        appendAaLine(&verts, center, tip, static_cast<float>(ray.color.redF()),
                     static_cast<float>(ray.color.greenF()), static_cast<float>(ray.color.blueF()));
        QVector3D side = QVector3D::crossProduct(ray.dir, QVector3D(0, 1, 0));
        if (side.lengthSquared() < 0.01f) {
            side = QVector3D::crossProduct(ray.dir, QVector3D(1, 0, 0));
        }
        side.normalize();
        const QVector3D bit = QVector3D::crossProduct(ray.dir, side).normalized();
        const float head = len * 0.08f;
        appendAaLine(&verts, tip, tip - ray.dir * head + side * head * 0.45f, static_cast<float>(ray.color.redF()),
                     static_cast<float>(ray.color.greenF()), static_cast<float>(ray.color.blueF()));
        appendAaLine(&verts, tip, tip - ray.dir * head - side * head * 0.45f, static_cast<float>(ray.color.redF()),
                     static_cast<float>(ray.color.greenF()), static_cast<float>(ray.color.blueF()));
        appendAaLine(&verts, tip, tip - ray.dir * head + bit * head * 0.45f, static_cast<float>(ray.color.redF()),
                     static_cast<float>(ray.color.greenF()), static_cast<float>(ray.color.blueF()));
        appendAaLine(&verts, tip, tip - ray.dir * head - bit * head * 0.45f, static_cast<float>(ray.color.redF()),
                     static_cast<float>(ray.color.greenF()), static_cast<float>(ray.color.blueF()));
    }
    if (verts.empty()) {
        return;
    }
    const QMatrix4x4 mvp = proj * view;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(gizmoVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
    drawLineMesh(gizmoVao_, static_cast<int>(verts.size() / 11), mvp, cameraPosition(), 2.0f, false);
    glBindVertexArray(0);
}

void ModelViewport::drawAxes(const QMatrix4x4& view, const QMatrix4x4& proj) {
    if (!axesVisible_ || gizmoVao_ == 0) {
        return;
    }
    const float len = std::max(1.6f, modelSize_.length() * 0.35f);
    const struct Axis {
        QVector3D dir;
        QColor color;
    } axes[] = {
        {QVector3D(1, 0, 0), QColor(220, 56, 56)},
        {QVector3D(0, 1, 0), QColor(72, 180, 72)},
        {QVector3D(0, 0, 1), QColor(56, 96, 220)},
    };
    std::vector<float> verts;
    for (const Axis& axis : axes) {
        appendAaLine(&verts, QVector3D(0, 0, 0), axis.dir * len, static_cast<float>(axis.color.redF()),
                     static_cast<float>(axis.color.greenF()), static_cast<float>(axis.color.blueF()));
    }
    const QMatrix4x4 mvp = proj * view;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(gizmoVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
    drawLineMesh(gizmoVao_, static_cast<int>(verts.size() / 11), mvp, cameraPosition(), 2.15f, false);
    glBindVertexArray(0);
}

void ModelViewport::paintGL() {
    uploadPendingModel();
    uploadPendingHdri();
    updateWorldMatrices();
    const int screenW = std::max(1, static_cast<int>(width() * devicePixelRatioF()));
    const int screenH = std::max(1, static_cast<int>(height() * devicePixelRatioF()));
    if (framebufferWidth() != targetW_ || framebufferHeight() != targetH_) {
        recreateTargets();
    }
    renderFrame(targetW_, targetH_, true);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, presentFbo_ != 0 ? presentFbo_ : sceneFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    glBlitFramebuffer(0, 0, targetW_, targetH_, 0, 0, screenW, screenH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, screenW, screenH);
    const QMatrix4x4 view = viewMatrix();
    QMatrix4x4 proj;
    const float aspect = screenH > 0 ? static_cast<float>(screenW) / static_cast<float>(screenH) : 1.0f;
    proj.perspective(fov_, aspect, 0.05f, 120.0f);
    drawLightGizmos(view, proj);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

void ModelViewport::renderFrame(int pixelW, int pixelH, bool) {
    if (sceneFbo_ == 0 || pixelW <= 0 || pixelH <= 0) {
        return;
    }
    float useFov = fov_;
    const QMatrix4x4 sceneView = sceneCameraView(&useFov);
    const QMatrix4x4 view = sceneView.isIdentity() ? viewMatrix() : sceneView;
    QMatrix4x4 proj;
    const float aspect = static_cast<float>(pixelW) / static_cast<float>(pixelH);
    proj.perspective(useFov, aspect, 0.05f, 120.0f);
    const QVector3D cam = view.inverted().map(QVector3D(0, 0, 0));

    drawShadow();

    const GLuint drawFbo = msaaFbo_ != 0 ? msaaFbo_ : sceneFbo_;
    glBindFramebuffer(GL_FRAMEBUFFER, drawFbo);
    glViewport(0, 0, pixelW, pixelH);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(view, proj, cam, 0);

    auto resolveScene = [&]() {
        if (msaaFbo_ != 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_);
            glBlitFramebuffer(0, 0, pixelW, pixelH, 0, 0, pixelW, pixelH, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                              GL_NEAREST);
        }
    };
    resolveScene();
    if (opaqueColor_ != 0 && sceneFbo_ != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
        glBindTexture(GL_TEXTURE_2D, opaqueColor_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, pixelW, pixelH);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, drawFbo);
    glViewport(0, 0, pixelW, pixelH);
    drawScene(view, proj, cam, 1);
    resolveScene();

    if (ssaoEnabled_) {
        drawSsao(proj);
    }

    const bool debugLit = debugView_ == DebugView::Lit;
    const bool doBloom =
        debugLit && bloomEnabled_ && bloomPasses_ > 0 && bloom_ > 0.01f && bloomFbo_[0] != 0;
    if (doBloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
        glViewport(0, 0, std::max(1, pixelW / 2), std::max(1, pixelH / 2));
        glDisable(GL_DEPTH_TEST);
        glUseProgram(brightProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneColor_);
        glUniform1i(glGetUniformLocation(brightProgram_, "uScene"), 0);
        glUniform1f(glGetUniformLocation(brightProgram_, "uThresh"), bloomThreshold_);
        glBindVertexArray(quadVao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glUseProgram(blurProgram_);
        glUniform1i(glGetUniformLocation(blurProgram_, "uTex"), 0);
        const int passes = std::max(1, bloomPasses_);
        for (int i = 0; i < passes; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[1]);
            glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
            glUniform2f(glGetUniformLocation(blurProgram_, "uDir"), 1.0f, 0.0f);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
            glBindTexture(GL_TEXTURE_2D, bloomTex_[1]);
            glUniform2f(glGetUniformLocation(blurProgram_, "uDir"), 0.0f, 1.0f);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    const GLuint outFbo = presentFbo_ != 0 ? presentFbo_ : defaultFramebufferObject();
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glViewport(0, 0, pixelW, pixelH);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(compositeProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColor_);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, doBloom ? bloomTex_[0] : sceneColor_);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uBloom"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoTex_ != 0 ? ssaoTex_ : whiteTex_);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uSsao"), 2);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uExposure"), exposure_);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uBloomAmt"), doBloom ? bloom_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uVignette"), debugLit ? vignette_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uSsaoAmt"),
                (ssaoEnabled_ && debugLit) ? ssaoIntensity_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uContrast"), debugLit ? contrast_ : 1.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uSaturation"), debugLit ? saturation_ : 1.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uTemperature"), debugLit ? temperature_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uTint"), debugLit ? tint_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uSharpen"), debugLit ? sharpen_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uGrain"), debugLit ? grain_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uCA"), debugLit ? chromatic_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uTime"), clock_.elapsed() / 1000.0f);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uTonemap"), static_cast<int>(tonemap_));
    glUniform1i(glGetUniformLocation(compositeProgram_, "uDebug"), static_cast<int>(debugView_));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sceneDepth_ != 0 ? sceneDepth_ : whiteTex_);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uDepth"), 3);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uDof"),
                (debugLit && dofEnabled_) ? dofAmount_ : 0.0f);
    glUniform1f(glGetUniformLocation(compositeProgram_, "uFocus"), focusDistance_);
    glUniform1i(glGetUniformLocation(compositeProgram_, "uKeepAlpha"), transparentBg_ ? 1 : 0);
    glUniform3f(glGetUniformLocation(compositeProgram_, "uBg"), static_cast<float>(backgroundColor_.redF()),
                static_cast<float>(backgroundColor_.greenF()), static_cast<float>(backgroundColor_.blueF()));
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void ModelViewport::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
    dragButtons_ = event->buttons();
    dragMods_ = event->modifiers();
    dragged_ = false;
    setFocus(Qt::MouseFocusReason);
}
void ModelViewport::mouseMoveEvent(QMouseEvent* event) {
    if (dragButtons_ == Qt::NoButton) {
        return;
    }
    const QPoint delta = event->pos() - lastMouse_;
    if (delta.manhattanLength() > 3) {
        dragged_ = true;
    }
    lastMouse_ = event->pos();
    const bool shift = dragMods_.testFlag(Qt::ShiftModifier);
    const bool ctrl = dragMods_.testFlag(Qt::ControlModifier);
    if ((dragButtons_ & Qt::MiddleButton) && shift) {
        pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    } else if ((dragButtons_ & Qt::MiddleButton) || ((dragButtons_ & Qt::LeftButton) && !shift && !ctrl)) {
        orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    } else if ((dragButtons_ & Qt::RightButton) || ((dragButtons_ & Qt::LeftButton) && shift)) {
        pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    } else if ((dragButtons_ & Qt::LeftButton) && ctrl) {
        zoom(static_cast<float>(-delta.y()) * 0.08f);
    }
}
void ModelViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragged_ && (event->button() == Qt::LeftButton)) {
        if (event->modifiers().testFlag(Qt::AltModifier)) {
            const float depth = pickDepth(event->pos());
            if (depth > 0.0f) {
                setFocusDistance(depth);
            }
        } else {
            const int part = pickPart(event->pos());
            setSelectedPart(part);
        }
    }
    dragButtons_ = Qt::NoButton;
}

void ModelViewport::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() == nullptr) {
        return;
    }
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".glb"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".gltf"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".exr"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".hdr"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void ModelViewport::dropEvent(QDropEvent* event) {
    if (event->mimeData() == nullptr) {
        return;
    }
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".glb"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".gltf"), Qt::CaseInsensitive)) {
            setModelPath(path);
            event->acceptProposedAction();
            return;
        }
        if (path.endsWith(QLatin1String(".exr"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".hdr"), Qt::CaseInsensitive)) {
            setHdriPath(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void ModelViewport::wheelEvent(QWheelEvent* event) {
    zoom(event->angleDelta().y() / 120.0f);
}
void ModelViewport::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_Period || event->key() == Qt::Key_F) {
        resetCamera();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}
void ModelViewport::orbit(float dx, float dy) {
    yaw_ -= dx * 0.35f;
    pitch_ = qBound(-89.0f, pitch_ + dy * 0.35f, 89.0f);
    update();
    emit cameraChanged();
}
void ModelViewport::pan(float dx, float dy) {
    const QMatrix4x4 view = viewMatrix();
    const QVector4D right4 = view.inverted().column(0);
    const QVector4D up4 = view.inverted().column(1);
    const float scale = distance_ * 0.0018f;
    target_ += -QVector3D(right4.x(), right4.y(), right4.z()) * dx * scale +
               QVector3D(up4.x(), up4.y(), up4.z()) * dy * scale;
    update();
    emit cameraChanged();
}
void ModelViewport::zoom(float steps) {
    distance_ = qBound(1.2f, distance_ * std::exp(-steps * 0.12f), 40.0f);
    update();
    emit cameraChanged();
}

void ModelViewport::loadModelAsync() {
    QPointer<ModelViewport> self(this);
    QString path = requestedModel_;
    if (path.isEmpty()) {
        path = findModelGlbPath();
        requestedModel_ = path;
    }
    std::thread([self, path]() {
        LoadedModel loaded;
        if (path.isEmpty()) {
            loaded.error = QStringLiteral("No model found. Drop a .glb or .gltf onto the viewport, or into the models folder.");
        } else {
            loaded = loadGlbModel(path);
            if (loaded.error.isEmpty()) {
                normalizeModel(&loaded);
            }
        }
        ModelViewport* raw = self.data();
        if (raw == nullptr) {
            return;
        }
        auto heap = std::make_shared<LoadedModel>(std::move(loaded));
        QMetaObject::invokeMethod(
            raw,
            [self, heap]() {
                if (self.isNull()) {
                    return;
                }
                self->pending_ = std::move(*heap);
                self->hasPending_ = true;
                self->update();
            },
            Qt::QueuedConnection);
    }).detach();
}

void ModelViewport::loadHdriAsync(const QString& path) {
    QPointer<ModelViewport> self(this);
    const QString usePath = path.isEmpty() ? findHdriPath() : path;
    const int cube = iblSize_;
    std::thread([self, usePath, cube]() {
        HdriImage loaded;
        if (usePath.isEmpty()) {
            loaded.error = QStringLiteral("No HDRI found. Import an .exr/.hdr or drop one on the viewport.");
        } else {
            loaded = loadHdri(usePath, cube);
        }
        auto heap = std::make_shared<HdriImage>(std::move(loaded));
        ModelViewport* raw = self.data();
        if (raw == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(
            raw,
            [self, heap]() {
                if (self.isNull()) {
                    return;
                }
                self->pendingHdri_ = std::move(*heap);
                self->hasPendingHdri_ = true;
                self->update();
            },
            Qt::QueuedConnection);
    }).detach();
}

void ModelViewport::uploadPendingHdri() {
    if (!glReady_ || !hasPendingHdri_) {
        return;
    }
    hasPendingHdri_ = false;
    hdri_ = std::move(pendingHdri_);
    if (!hdri_.error.isEmpty() || !hdri_.hasCube()) {
        emit statusChanged();
        return;
    }
    rebuildEnvironment();
    emit statusChanged();
    emit graphicsChanged();
}

void ModelViewport::uploadPendingModel() {
    if (!glReady_ || !hasPending_) {
        return;
    }
    hasPending_ = false;
    LoadedModel model = std::move(pending_);
    if (!model.error.isEmpty() || model.isEmpty()) {
        status_ = model.error.isEmpty() ? QStringLiteral("Empty model.") : model.error;
        emit statusChanged();
        return;
    }
    for (GpuPart& part : parts_) {
        if (part.vao) {
            glDeleteVertexArrays(1, &part.vao);
        }
        if (part.vbo) {
            glDeleteBuffers(1, &part.vbo);
        }
        if (part.ebo) {
            glDeleteBuffers(1, &part.ebo);
        }
        if (part.jointVbo) {
            glDeleteBuffers(1, &part.jointVbo);
        }
        if (part.weightVbo) {
            glDeleteBuffers(1, &part.weightVbo);
        }
        if (part.morphTex) {
            glDeleteTextures(1, &part.morphTex);
        }
    }
    parts_.clear();
    for (GpuMaterial& mat : materials_) {
        if (mat.albedo && mat.albedo != whiteTex_) {
            glDeleteTextures(1, &mat.albedo);
        }
        if (mat.normal && mat.normal != flatNormalTex_ && mat.normal != mat.albedo) {
            glDeleteTextures(1, &mat.normal);
        }
        if (mat.maps && mat.maps != whiteTex_ && mat.maps != mat.albedo && mat.maps != mat.normal) {
            glDeleteTextures(1, &mat.maps);
        }
        if (mat.emissive && mat.emissive != whiteTex_ && mat.emissive != mat.albedo) {
            glDeleteTextures(1, &mat.emissive);
        }
        if (mat.occlusion && mat.occlusion != whiteTex_ && mat.occlusion != mat.maps) {
            glDeleteTextures(1, &mat.occlusion);
        }
        if (mat.clearcoat && mat.clearcoat != whiteTex_ && mat.clearcoat != mat.albedo) {
            glDeleteTextures(1, &mat.clearcoat);
        }
        if (mat.clearcoatRough && mat.clearcoatRough != whiteTex_ && mat.clearcoatRough != mat.maps) {
            glDeleteTextures(1, &mat.clearcoatRough);
        }
        if (mat.clearcoatNormal && mat.clearcoatNormal != flatNormalTex_ && mat.clearcoatNormal != mat.normal) {
            glDeleteTextures(1, &mat.clearcoatNormal);
        }
        auto dropExtra = [&](GLuint tex) {
            if (tex && tex != whiteTex_ && tex != flatNormalTex_ && tex != mat.albedo && tex != mat.maps) {
                glDeleteTextures(1, &tex);
            }
        };
        dropExtra(mat.transmission);
        dropExtra(mat.thickness);
        dropExtra(mat.sheenColor);
        dropExtra(mat.sheenRough);
        dropExtra(mat.iridescence);
        dropExtra(mat.iridescenceThickness);
    }
    materials_.clear();

    std::vector<GLuint> imageTex(model.images.size(), 0);
    std::vector<char> srgb(model.images.size(), 1);
    for (int i = 0; i < static_cast<int>(model.materials.size()); ++i) {
        const PbrMaterial& mat = model.materials[static_cast<size_t>(i)];
        auto markLinear = [&](int idx) {
            if (idx >= 0 && idx < static_cast<int>(srgb.size())) {
                srgb[static_cast<size_t>(idx)] = 0;
            }
        };
        markLinear(mat.normalTexture);
        markLinear(mat.mapsTexture);
        markLinear(mat.occlusionTexture);
        markLinear(mat.clearcoatRoughnessTexture);
        markLinear(mat.clearcoatNormalTexture);
        markLinear(mat.transmissionTexture);
        markLinear(mat.thicknessTexture);
        markLinear(mat.sheenRoughnessTexture);
        markLinear(mat.iridescenceTexture);
        markLinear(mat.iridescenceThicknessTexture);
    }
    for (int i = 0; i < static_cast<int>(model.images.size()); ++i) {
        if (!model.images.at(i).isNull()) {
            imageTex[static_cast<size_t>(i)] = makeTexture(model.images.at(i), srgb[static_cast<size_t>(i)] != 0, false);
        }
    }
    auto texOf = [&](int idx) -> GLuint {
        if (idx < 0 || idx >= static_cast<int>(imageTex.size())) {
            return 0;
        }
        return imageTex[static_cast<size_t>(idx)];
    };
    materials_.reserve(model.materials.size());
    for (const PbrMaterial& src : model.materials) {
        GpuMaterial mat;
        mat.cpu = src;
        mat.albedo = texOf(src.baseColorTexture);
        mat.normal = texOf(src.normalTexture);
        mat.maps = texOf(src.mapsTexture);
        mat.emissive = texOf(src.emissiveTexture);
        mat.occlusion = texOf(src.occlusionTexture);
        mat.clearcoat = texOf(src.clearcoatTexture);
        mat.clearcoatRough = texOf(src.clearcoatRoughnessTexture);
        mat.clearcoatNormal = texOf(src.clearcoatNormalTexture);
        mat.transmission = texOf(src.transmissionTexture);
        mat.thickness = texOf(src.thicknessTexture);
        mat.sheenColor = texOf(src.sheenColorTexture);
        mat.sheenRough = texOf(src.sheenRoughnessTexture);
        mat.iridescence = texOf(src.iridescenceTexture);
        mat.iridescenceThickness = texOf(src.iridescenceThicknessTexture);
        mat.hasAlbedo = mat.albedo != 0;
        mat.hasNormal = mat.normal != 0;
        mat.hasMaps = mat.maps != 0;
        mat.hasEmissive = mat.emissive != 0;
        mat.hasOcclusion = mat.occlusion != 0 && mat.occlusion != mat.maps;
        mat.hasClearcoat = mat.clearcoat != 0;
        mat.hasClearcoatRough = mat.clearcoatRough != 0;
        mat.hasClearcoatNormal = mat.clearcoatNormal != 0;
        mat.hasTransmission = mat.transmission != 0;
        mat.hasThickness = mat.thickness != 0;
        mat.hasSheenColor = mat.sheenColor != 0;
        mat.hasSheenRough = mat.sheenRough != 0;
        mat.hasIridescence = mat.iridescence != 0;
        mat.hasIridescenceThickness = mat.iridescenceThickness != 0;
        materials_.push_back(mat);
    }

    int tris = 0;
    for (const MeshPart& src : model.parts) {
        GpuPart part;
        part.material = src.material;
        part.defaultMaterial = src.defaultMaterial != 0 ? src.defaultMaterial : src.material;
        part.node = src.node;
        part.skin = src.skin;
        part.bindVertices = src.vertices;
        part.joints = src.joints;
        part.weights = src.weights;
        part.morphs = src.morphs;
        part.variantMaterials = src.variantMaterials;
        part.indexCount = static_cast<int>(src.indices.size());
        part.vertexCount = static_cast<int>(src.vertices.size() / static_cast<size_t>(kVertexFloats));
        tris += part.indexCount / 3;
        const bool canGpuMorph = !src.morphs.empty() && static_cast<int>(src.morphs.size()) <= kMaxMorphs;
        const bool canGpuSkin = src.skin >= 0 && !src.joints.empty() && !src.weights.empty() &&
                                src.skin < static_cast<int>(model.gltfSkins.size()) &&
                                static_cast<int>(model.gltfSkins[static_cast<size_t>(src.skin)].joints.size()) <=
                                    kMaxBones &&
                                (src.morphs.empty() || canGpuMorph);
        part.gpuSkin = canGpuSkin;
        part.gpuMorph = canGpuMorph;
        part.morphCount = canGpuMorph ? static_cast<int>(src.morphs.size()) : 0;
        glGenVertexArrays(1, &part.vao);
        glBindVertexArray(part.vao);
        part.vbo = makeBuffer(this, GL_ARRAY_BUFFER, src.vertices.data(),
                              static_cast<int>(src.vertices.size() * sizeof(float)));
        if (!src.morphs.empty() && !canGpuMorph) {
            glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(src.vertices.size() * sizeof(float)),
                         src.vertices.data(), GL_DYNAMIC_DRAW);
        } else if (src.skin >= 0 && !canGpuSkin) {
            glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(src.vertices.size() * sizeof(float)),
                         src.vertices.data(), GL_DYNAMIC_DRAW);
        }
        const GLsizei stride = kVertexFloats * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(11 * sizeof(float)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12 * sizeof(float)));
        if (!src.joints.empty() && !src.weights.empty()) {
            part.jointVbo = makeBuffer(this, GL_ARRAY_BUFFER, src.joints.data(),
                                       static_cast<int>(src.joints.size() * sizeof(float)));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
            part.weightVbo = makeBuffer(this, GL_ARRAY_BUFFER, src.weights.data(),
                                        static_cast<int>(src.weights.size() * sizeof(float)));
            glEnableVertexAttribArray(7);
            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        }
        if (canGpuMorph) {
            const int w = std::max(1, part.vertexCount);
            const int h = std::max(2, part.morphCount * 2);
            std::vector<float> tex(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u, 0.0f);
            for (int m = 0; m < part.morphCount; ++m) {
                const MorphTarget& mt = src.morphs[static_cast<size_t>(m)];
                for (int v = 0; v < part.vertexCount; ++v) {
                    if (mt.positions.size() >= static_cast<size_t>(v + 1) * 3u) {
                        const size_t dst =
                            (static_cast<size_t>(m * 2) * static_cast<size_t>(w) + static_cast<size_t>(v)) * 3u;
                        tex[dst] = mt.positions[static_cast<size_t>(v) * 3u];
                        tex[dst + 1] = mt.positions[static_cast<size_t>(v) * 3u + 1];
                        tex[dst + 2] = mt.positions[static_cast<size_t>(v) * 3u + 2];
                    }
                    if (mt.normals.size() >= static_cast<size_t>(v + 1) * 3u) {
                        const size_t dst =
                            (static_cast<size_t>(m * 2 + 1) * static_cast<size_t>(w) + static_cast<size_t>(v)) * 3u;
                        tex[dst] = mt.normals[static_cast<size_t>(v) * 3u];
                        tex[dst + 1] = mt.normals[static_cast<size_t>(v) * 3u + 1];
                        tex[dst + 2] = mt.normals[static_cast<size_t>(v) * 3u + 2];
                    }
                }
            }
            glGenTextures(1, &part.morphTex);
            glBindTexture(GL_TEXTURE_2D, part.morphTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, tex.data());
        }
        part.ebo = makeBuffer(this, GL_ELEMENT_ARRAY_BUFFER, src.indices.data(),
                              static_cast<int>(src.indices.size() * sizeof(std::uint32_t)));
        parts_.push_back(part);
    }
    glBindVertexArray(0);

    cpuModel_ = std::move(model);
    if (!cpuModel_.path.isEmpty()) {
        requestedModel_ = cpuModel_.path;
    }
    bindNodes_ = cpuModel_.nodes;
    morphWeights_.clear();
    for (const SceneNode& node : cpuModel_.nodes) {
        for (float w : node.morphWeights) {
            if (static_cast<int>(morphWeights_.size()) < kMaxMorphs) {
                morphWeights_.push_back(w);
            }
        }
    }
    selectedNode_ = -1;
    selectedPart_ = -1;
    selectedMaterial_ = -1;
    isolatedNode_ = -1;
    variantIndex_ = cpuModel_.variants.empty() ? -1 : 0;
    animationIndex_ = 0;
    animationTime_ = 0.0f;
    animationPlaying_ = false;
    sceneCameraIndex_ = -1;
    modelCenter_ = cpuModel_.center();
    modelSize_ = cpuModel_.size();
    target_ = QVector3D(0.0f, modelCenter_.y() * 0.55f, 0.0f);
    distance_ = std::max(5.8f, modelSize_.length() * 1.2f);
    focusDistance_ = distance_;
    uploaded_ = true;
    status_ = QStringLiteral("%1 tris · %2 materials · %3")
                  .arg(tris)
                  .arg(static_cast<int>(materials_.size()))
                  .arg(cpuModel_.title);
    if (!cpuModel_.usedExtensions.isEmpty()) {
        status_ += QStringLiteral(" · %1").arg(cpuModel_.usedExtensions.join(QStringLiteral(", ")));
    }
    if (!cpuModel_.ignoredExtensions.isEmpty()) {
        status_ += QStringLiteral(" · ignored %1").arg(cpuModel_.ignoredExtensions.join(QStringLiteral(", ")));
    }
    emit statusChanged();
    emit cameraChanged();
    emit graphicsChanged();
    emit modelChanged();
    emit selectionChanged();
    emit animationChanged();
}

GLuint ModelViewport::makeTexture(const QImage& image, bool srgb, bool isNormal) {
    QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    if (isNormal) {
        img = img.mirrored(false, true);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, img.width(), img.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img.constBits());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat aniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
    const float use = std::min(std::max(1.0f, anisotropy_), std::max(1.0f, aniso));
    if (use > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, use);
    }
    return tex;
}

GLuint ModelViewport::makeSolidTexture(quint8 r, quint8 g, quint8 b, quint8 a) {
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.setPixelColor(0, 0, QColor(r, g, b, a));
    return makeTexture(img, false, false);
}

void ModelViewport::recreateTargets() {
    if (!glReady_) {
        return;
    }
    destroyTargets();
    targetW_ = framebufferWidth();
    targetH_ = framebufferHeight();
    if (maxTexSize_ > 0) {
        shadowSize_ = std::min(shadowSize_, maxTexSize_);
    }
    const int bw = std::max(1, targetW_ / 2);
    const int bh = std::max(1, targetH_ / 2);

    auto makeColorTex = [&](int w, int h, GLint format) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format == GL_DEPTH_COMPONENT24 ? GL_DEPTH_COMPONENT : GL_RGBA,
                     format == GL_DEPTH_COMPONENT24 ? GL_UNSIGNED_INT : GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    };

    sceneColor_ = makeColorTex(targetW_, targetH_, GL_RGBA16F);
    opaqueColor_ = makeColorTex(targetW_, targetH_, GL_RGBA16F);
    sceneDepth_ = 0;
    glGenTextures(1, &sceneDepth_);
    glBindTexture(GL_TEXTURE_2D, sceneDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, targetW_, targetH_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth_, 0);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    int samples = msaaSamples_;
    while (samples > maxSamples && samples > 1) {
        samples /= 2;
    }
    if (samples <= 1) {
        samples = 0;
    }
    msaaActual_ = samples;
    if (samples >= 2) {
        glGenRenderbuffers(1, &msaaColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, targetW_, targetH_);
        glGenRenderbuffers(1, &msaaDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, targetW_, targetH_);
        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &msaaFbo_);
            glDeleteRenderbuffers(1, &msaaColor_);
            glDeleteRenderbuffers(1, &msaaDepth_);
            msaaFbo_ = 0;
            msaaColor_ = 0;
            msaaDepth_ = 0;
            msaaActual_ = 0;
        }
    }

    for (int i = 0; i < 2; ++i) {
        bloomTex_[i] = makeColorTex(bw, bh, GL_RGBA16F);
        glGenFramebuffers(1, &bloomFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
    }

    ssaoTex_ = makeColorTex(bw, bh, GL_RGBA16F);
    glGenFramebuffers(1, &ssaoFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoTex_, 0);

    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);

    presentTex_ = 0;
    glGenTextures(1, &presentTex_);
    glBindTexture(GL_TEXTURE_2D, presentTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, targetW_, targetH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &presentFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, presentFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, presentTex_, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}

void ModelViewport::destroyTargets() {
    auto delFbo = [&](GLuint& id) {
        if (id) {
            glDeleteFramebuffers(1, &id);
            id = 0;
        }
    };
    auto delTex = [&](GLuint& id) {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    };
    auto delRb = [&](GLuint& id) {
        if (id) {
            glDeleteRenderbuffers(1, &id);
            id = 0;
        }
    };
    delFbo(sceneFbo_);
    delTex(sceneColor_);
    delTex(opaqueColor_);
    delTex(sceneDepth_);
    delFbo(msaaFbo_);
    delRb(msaaColor_);
    delRb(msaaDepth_);
    msaaActual_ = 0;
    for (int i = 0; i < 2; ++i) {
        delFbo(bloomFbo_[i]);
        delTex(bloomTex_[i]);
    }
    delFbo(ssaoFbo_);
    delTex(ssaoTex_);
    delFbo(shadowFbo_);
    delTex(shadowTex_);
    delFbo(presentFbo_);
    delTex(presentTex_);
}

void ModelViewport::destroyGpu() {
    destroyTargets();
    for (GpuPart& part : parts_) {
        if (part.vao) {
            glDeleteVertexArrays(1, &part.vao);
        }
        if (part.vbo) {
            glDeleteBuffers(1, &part.vbo);
        }
        if (part.ebo) {
            glDeleteBuffers(1, &part.ebo);
        }
        if (part.jointVbo) {
            glDeleteBuffers(1, &part.jointVbo);
        }
        if (part.weightVbo) {
            glDeleteBuffers(1, &part.weightVbo);
        }
        if (part.morphTex) {
            glDeleteTextures(1, &part.morphTex);
        }
    }
    parts_.clear();
    for (GpuMaterial& mat : materials_) {
        if (mat.albedo && mat.albedo != whiteTex_) {
            glDeleteTextures(1, &mat.albedo);
        }
        if (mat.normal && mat.normal != flatNormalTex_ && mat.normal != mat.albedo) {
            glDeleteTextures(1, &mat.normal);
        }
        if (mat.maps && mat.maps != whiteTex_ && mat.maps != mat.albedo && mat.maps != mat.normal) {
            glDeleteTextures(1, &mat.maps);
        }
        if (mat.emissive && mat.emissive != whiteTex_ && mat.emissive != mat.albedo) {
            glDeleteTextures(1, &mat.emissive);
        }
        if (mat.occlusion && mat.occlusion != whiteTex_ && mat.occlusion != mat.maps) {
            glDeleteTextures(1, &mat.occlusion);
        }
        if (mat.clearcoat && mat.clearcoat != whiteTex_ && mat.clearcoat != mat.albedo) {
            glDeleteTextures(1, &mat.clearcoat);
        }
        if (mat.clearcoatRough && mat.clearcoatRough != whiteTex_ && mat.clearcoatRough != mat.maps) {
            glDeleteTextures(1, &mat.clearcoatRough);
        }
        if (mat.clearcoatNormal && mat.clearcoatNormal != flatNormalTex_ && mat.clearcoatNormal != mat.normal) {
            glDeleteTextures(1, &mat.clearcoatNormal);
        }
        auto dropExtra = [&](GLuint tex) {
            if (tex && tex != whiteTex_ && tex != flatNormalTex_ && tex != mat.albedo && tex != mat.maps) {
                glDeleteTextures(1, &tex);
            }
        };
        dropExtra(mat.transmission);
        dropExtra(mat.thickness);
        dropExtra(mat.sheenColor);
        dropExtra(mat.sheenRough);
        dropExtra(mat.iridescence);
        dropExtra(mat.iridescenceThickness);
    }
    materials_.clear();
    auto delProg = [&](GLuint& p) {
        if (p) {
            glDeleteProgram(p);
            p = 0;
        }
    };
    delProg(modelProgram_);
    delProg(groundProgram_);
    delProg(brightProgram_);
    delProg(blurProgram_);
    delProg(compositeProgram_);
    delProg(skyProgram_);
    delProg(shadowProgram_);
    delProg(ssaoProgram_);
    delProg(overlayProgram_);
    delProg(wireProgram_);
    delProg(floorProgram_);
    delProg(iblIrrProg_);
    delProg(iblPrefProg_);
    delProg(lutProgram_);
    auto delVao = [&](GLuint& id) {
        if (id) {
            glDeleteVertexArrays(1, &id);
            id = 0;
        }
    };
    auto delBuf = [&](GLuint& id) {
        if (id) {
            glDeleteBuffers(1, &id);
            id = 0;
        }
    };
    auto delTex = [&](GLuint& id) {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    };
    delVao(groundVao_);
    delBuf(groundVbo_);
    delVao(quadVao_);
    delBuf(quadVbo_);
    delVao(cubeVao_);
    delBuf(cubeVbo_);
    delBuf(cubeEbo_);
    delVao(gizmoVao_);
    delBuf(gizmoVbo_);
    delTex(whiteTex_);
    delTex(flatNormalTex_);
    delTex(envCube_);
    delTex(boneTex_);
    if (irrCube_ && irrCube_ != envCube_) {
        glDeleteTextures(1, &irrCube_);
        irrCube_ = 0;
    }
    if (prefCube_ && prefCube_ != envCube_) {
        glDeleteTextures(1, &prefCube_);
        prefCube_ = 0;
    } else {
        prefCube_ = 0;
    }
    delTex(brdfLut_);
    glReady_ = false;
}

bool ModelViewport::compileProgram(GLuint* program, const char* vertSrc, const char* fragSrc) {
    auto compile = [&](GLenum type, const char* src) -> GLuint {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_FALSE) {
            char log[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, log);
            status_ = QString::fromUtf8(log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };
    const GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
    const GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (vert == 0 || frag == 0) {
        if (vert) {
            glDeleteShader(vert);
        }
        if (frag) {
            glDeleteShader(frag);
        }
        return false;
    }
    *program = glCreateProgram();
    glAttachShader(*program, vert);
    glAttachShader(*program, frag);
    glLinkProgram(*program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(*program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        char log[1024];
        glGetProgramInfoLog(*program, 1024, nullptr, log);
        status_ = QString::fromUtf8(log);
        glDeleteProgram(*program);
        *program = 0;
        return false;
    }
    return true;
}

void ModelViewport::tick() {
    if (!isVisible()) {
        return;
    }
    const qint64 now = clock_.elapsed();
    static qint64 last = 0;
    float dt = last == 0 ? 0.016f : static_cast<float>(now - last) / 1000.0f;
    last = now;
    dt = qBound(0.0f, dt, 0.08f);
    bool dirty = false;
    if (autoRotate_) {
        yaw_ += 0.28f;
        if (yaw_ >= 360.0f) {
            yaw_ -= 360.0f;
        }
        emit cameraChanged();
        dirty = true;
    }
    if (animationPlaying_) {
        const float duration = animationDuration();
        animationTime_ += dt;
        if (duration > 0.0f && animationTime_ > duration) {
            animationTime_ = animationLoop_ ? std::fmod(animationTime_, duration) : duration;
            if (!animationLoop_) {
                animationPlaying_ = false;
            }
        }
        emit animationChanged();
        dirty = true;
    }
    if (dirty) {
        update();
    }
}

bool ModelViewport::partVisible(const GpuPart& part) const {
    return !part.hidden;
}

QStringList ModelViewport::lookNames() const {
    return {QStringLiteral("Showroom"), QStringLiteral("Soft daylight"), QStringLiteral("Overcast"),
            QStringLiteral("Night"), QStringLiteral("Clay studio")};
}

std::vector<OutlinerItem> ModelViewport::outlinerItems() const {
    return buildOutliner(cpuModel_);
}

QStringList ModelViewport::variantNames() const {
    QStringList names;
    for (const MaterialVariant& variant : cpuModel_.variants) {
        names << variant.name;
    }
    return names;
}

QStringList ModelViewport::animationNames() const {
    QStringList names;
    for (const AnimClip& clip : cpuModel_.animations) {
        names << (clip.label.isEmpty() ? clip.name : clip.label);
    }
    return names;
}

QStringList ModelViewport::sceneCameraNames() const {
    QStringList names;
    names << QStringLiteral("Orbit");
    for (const GltfCamera& camera : cpuModel_.cameras) {
        names << camera.name;
    }
    return names;
}

QStringList ModelViewport::morphNames() const {
    QStringList names;
    for (int i = 0; i < static_cast<int>(morphWeights_.size()); ++i) {
        names << QStringLiteral("Morph %1").arg(i + 1);
    }
    return names;
}

std::vector<float> ModelViewport::morphWeights() const {
    return morphWeights_;
}

float ModelViewport::animationDuration() const {
    if (animationIndex_ < 0 || animationIndex_ >= static_cast<int>(cpuModel_.animations.size())) {
        return 0.0f;
    }
    const AnimClip& clip = cpuModel_.animations[static_cast<size_t>(animationIndex_)];
    if (clip.duration > 0.0f) {
        return clip.duration;
    }
    return 1.0f;
}

PbrMaterial ModelViewport::selectedMaterialData() const {
    if (selectedMaterial_ >= 0 && selectedMaterial_ < static_cast<int>(materials_.size())) {
        return materials_[static_cast<size_t>(selectedMaterial_)].cpu;
    }
    return {};
}

void ModelViewport::setClayMode(bool enabled) {
    clayMode_ = enabled;
    update();
    emit graphicsChanged();
}

void ModelViewport::setFloorCatcher(bool enabled) {
    floorCatcher_ = enabled;
    update();
    emit graphicsChanged();
}

void ModelViewport::setSceneLights(bool enabled) {
    sceneLights_ = enabled;
    update();
    emit graphicsChanged();
}

void ModelViewport::setTransparentBackground(bool enabled) {
    transparentBg_ = enabled;
    update();
    emit graphicsChanged();
}

void ModelViewport::setDofEnabled(bool enabled) {
    dofEnabled_ = enabled;
    update();
    emit graphicsChanged();
}

void ModelViewport::setDofAmount(float value) {
    dofAmount_ = qBound(0.0f, value, 2.0f);
    update();
}

void ModelViewport::setFocusDistance(float value) {
    focusDistance_ = qBound(0.2f, value, 80.0f);
    update();
}

void ModelViewport::applyLook(int index) {
    lookIndex_ = qBound(0, index, 4);
    clayMode_ = lookIndex_ == 4;
    floorCatcher_ = lookIndex_ != 2;
    skyVisible_ = lookIndex_ == 1 || lookIndex_ == 2;
    switch (lookIndex_) {
    case 0:
        keyEnabled_ = true;
        fillEnabled_ = true;
        rimEnabled_ = true;
        envEnabled_ = true;
        keyIntensity_ = 0.62f;
        fillIntensity_ = 0.18f;
        rimIntensity_ = 0.28f;
        envIntensity_ = 1.05f;
        exposure_ = 1.05f;
        backgroundColor_ = QColor(8, 9, 12);
        break;
    case 1:
        keyEnabled_ = true;
        fillEnabled_ = true;
        rimEnabled_ = false;
        envEnabled_ = true;
        keyIntensity_ = 0.32f;
        fillIntensity_ = 0.12f;
        envIntensity_ = 1.45f;
        exposure_ = 1.12f;
        backgroundColor_ = QColor(18, 20, 24);
        break;
    case 2:
        keyEnabled_ = false;
        fillEnabled_ = true;
        rimEnabled_ = false;
        envEnabled_ = true;
        fillIntensity_ = 0.08f;
        envIntensity_ = 1.7f;
        exposure_ = 1.2f;
        backgroundColor_ = QColor(36, 38, 42);
        break;
    case 3:
        keyEnabled_ = true;
        fillEnabled_ = false;
        rimEnabled_ = true;
        envEnabled_ = true;
        keyIntensity_ = 0.22f;
        rimIntensity_ = 0.55f;
        envIntensity_ = 0.38f;
        exposure_ = 0.92f;
        backgroundColor_ = QColor(4, 5, 8);
        break;
    default:
        keyEnabled_ = true;
        fillEnabled_ = true;
        rimEnabled_ = true;
        envEnabled_ = true;
        keyIntensity_ = 0.7f;
        fillIntensity_ = 0.22f;
        rimIntensity_ = 0.18f;
        envIntensity_ = 0.85f;
        exposure_ = 1.0f;
        backgroundColor_ = QColor(24, 24, 26);
        break;
    }
    update();
    emit graphicsChanged();
}

void ModelViewport::setVariantIndex(int index) {
    variantIndex_ = index;
    for (GpuPart& part : parts_) {
        int material = part.defaultMaterial;
        if (index >= 0 && index < static_cast<int>(part.variantMaterials.size()) &&
            part.variantMaterials[static_cast<size_t>(index)] >= 0) {
            material = part.variantMaterials[static_cast<size_t>(index)];
        }
        part.material = material;
    }
    update();
    emit modelChanged();
}

void ModelViewport::setSelectedNode(int node) {
    selectedNode_ = node;
    selectedPart_ = -1;
    if (node >= 0) {
        for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
            if (parts_[static_cast<size_t>(i)].node == node) {
                selectedPart_ = i;
                selectedMaterial_ = parts_[static_cast<size_t>(i)].material;
                break;
            }
        }
    }
    update();
    emit selectionChanged();
}

void ModelViewport::setSelectedPart(int part) {
    selectedPart_ = part;
    if (part >= 0 && part < static_cast<int>(parts_.size())) {
        selectedNode_ = parts_[static_cast<size_t>(part)].node;
        selectedMaterial_ = parts_[static_cast<size_t>(part)].material;
    } else {
        selectedNode_ = -1;
        selectedMaterial_ = -1;
    }
    update();
    emit selectionChanged();
}

void ModelViewport::setIsolatedNode(int node) {
    isolatedNode_ = node;
    update();
    emit selectionChanged();
}

void ModelViewport::clearIsolation() {
    isolatedNode_ = -1;
    update();
    emit selectionChanged();
}

void ModelViewport::setPartHidden(int part, bool hidden) {
    if (part >= 0 && part < static_cast<int>(parts_.size())) {
        parts_[static_cast<size_t>(part)].hidden = hidden;
        update();
    }
}

void ModelViewport::setAnimationIndex(int index) {
    animationIndex_ = index;
    animationTime_ = 0.0f;
    resetNodePose();
    update();
    emit animationChanged();
}

void ModelViewport::setAnimationPlaying(bool playing) {
    animationPlaying_ = playing;
    emit animationChanged();
    update();
}

void ModelViewport::setAnimationLoop(bool loop) {
    animationLoop_ = loop;
}

void ModelViewport::setAnimationTime(float seconds) {
    animationTime_ = std::max(0.0f, seconds);
    update();
    emit animationChanged();
}

void ModelViewport::setSceneCameraIndex(int index) {
    sceneCameraIndex_ = index - 1;
    update();
    emit cameraChanged();
}

void ModelViewport::setMorphWeight(int index, float value) {
    if (index < 0 || index >= static_cast<int>(morphWeights_.size())) {
        return;
    }
    morphWeights_[static_cast<size_t>(index)] = qBound(0.0f, value, 1.0f);
    update();
}

void ModelViewport::setSelectedBaseColor(const QColor& color) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    PbrMaterial& mat = materials_[static_cast<size_t>(selectedMaterial_)].cpu;
    mat.baseColor = QVector4D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                              static_cast<float>(color.blueF()), mat.baseColor.w());
    update();
}

void ModelViewport::setSelectedMetallic(float value) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.metallic = qBound(0.0f, value, 1.0f);
    update();
}

void ModelViewport::setSelectedRoughness(float value) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.roughness = qBound(0.04f, value, 1.0f);
    update();
}

void ModelViewport::setSelectedTransmission(float value) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.transmission = qBound(0.0f, value, 1.0f);
    update();
}

void ModelViewport::setSelectedSheen(float value) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    const float v = qBound(0.0f, value, 1.0f);
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.sheenColor = QVector3D(v, v, v);
    update();
}

void ModelViewport::setSelectedEmissiveGain(float value) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.emissiveStrength = qBound(0.0f, value, 8.0f);
    update();
}

void ModelViewport::setSelectedUnlit(bool enabled) {
    if (selectedMaterial_ < 0 || selectedMaterial_ >= static_cast<int>(materials_.size())) {
        return;
    }
    materials_[static_cast<size_t>(selectedMaterial_)].cpu.unlit = enabled;
    update();
}

void ModelViewport::resetNodePose() {
    if (bindNodes_.size() == cpuModel_.nodes.size()) {
        cpuModel_.nodes = bindNodes_;
    }
}

static void sampleChannel(const AnimSampler& sampler, float time, float* out, int comps) {
    if (sampler.times.empty() || sampler.values.empty()) {
        return;
    }
    const int stride = sampler.interpolation == AnimInterp::Cubic ? comps * 3 : comps;
    const int keys = static_cast<int>(sampler.times.size());
    if (time <= sampler.times.front()) {
        const int src = sampler.interpolation == AnimInterp::Cubic ? comps : 0;
        for (int c = 0; c < comps; ++c) {
            out[c] = sampler.values[static_cast<size_t>(src + c)];
        }
        return;
    }
    if (time >= sampler.times.back()) {
        const int last = (keys - 1) * stride + (sampler.interpolation == AnimInterp::Cubic ? comps : 0);
        for (int c = 0; c < comps; ++c) {
            out[c] = sampler.values[static_cast<size_t>(last + c)];
        }
        return;
    }
    int i = 0;
    while (i + 1 < keys && sampler.times[static_cast<size_t>(i + 1)] < time) {
        ++i;
    }
    const float t0 = sampler.times[static_cast<size_t>(i)];
    const float t1 = sampler.times[static_cast<size_t>(i + 1)];
    float u = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0f;
    if (sampler.interpolation == AnimInterp::Step) {
        u = 0.0f;
    }
    const bool cubic = sampler.interpolation == AnimInterp::Cubic;
    const int a = i * stride + (cubic ? comps : 0);
    const int b = (i + 1) * stride + (cubic ? comps : 0);
    if (cubic) {
        const float dt = std::max(0.0f, t1 - t0);
        const int out0 = a + comps;
        const int in1 = b - comps;
        const float t = u;
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        for (int c = 0; c < comps; ++c) {
            const float p0 = sampler.values[static_cast<size_t>(a + c)];
            const float p1 = sampler.values[static_cast<size_t>(b + c)];
            const float m0 = dt * sampler.values[static_cast<size_t>(out0 + c)];
            const float m1 = dt * sampler.values[static_cast<size_t>(in1 + c)];
            out[c] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
        }
        return;
    }
    for (int c = 0; c < comps; ++c) {
        out[c] = sampler.values[static_cast<size_t>(a + c)] * (1.0f - u) +
                 sampler.values[static_cast<size_t>(b + c)] * u;
    }
}

void ModelViewport::applyAnimation(float time) {
    resetNodePose();
    if (animationIndex_ < 0 || animationIndex_ >= static_cast<int>(cpuModel_.animations.size())) {
        return;
    }
    const AnimClip& clip = cpuModel_.animations[static_cast<size_t>(animationIndex_)];
    for (const AnimChannel& channel : clip.channels) {
        if (channel.node < 0 || channel.node >= static_cast<int>(cpuModel_.nodes.size())) {
            continue;
        }
        SceneNode& node = cpuModel_.nodes[static_cast<size_t>(channel.node)];
        node.hasTrs = true;
        float v[4] = {0, 0, 0, 1};
        sampleChannel(channel.sampler, time, v, channel.sampler.components);
        if (channel.path == AnimPath::Translation) {
            node.translation = QVector3D(v[0], v[1], v[2]);
        } else if (channel.path == AnimPath::Scale) {
            node.scale = QVector3D(v[0], v[1], v[2]);
        } else if (channel.path == AnimPath::Rotation) {
            node.rotation = QQuaternion(v[3], v[0], v[1], v[2]).normalized();
        } else if (channel.path == AnimPath::Weights) {
            morphWeights_.assign(channel.sampler.components, 0.0f);
            for (int i = 0; i < channel.sampler.components && i < static_cast<int>(morphWeights_.size()); ++i) {
                morphWeights_[static_cast<size_t>(i)] = v[i];
            }
        }
    }
}

void ModelViewport::deformParts() {
    if (!glReady_) {
        return;
    }
    std::vector<QMatrix4x4> jointPalette;
    auto skinPalette = [&](int skinIndex, int meshNode) {
        jointPalette.clear();
        if (skinIndex < 0 || skinIndex >= static_cast<int>(cpuModel_.gltfSkins.size())) {
            return;
        }
        const GltfSkin& skin = cpuModel_.gltfSkins[static_cast<size_t>(skinIndex)];
        QMatrix4x4 meshWorld;
        if (meshNode >= 0 && meshNode < static_cast<int>(world_.size())) {
            meshWorld = world_[static_cast<size_t>(meshNode)];
        }
        const QMatrix4x4 invMesh = meshWorld.inverted();
        jointPalette.resize(skin.joints.size());
        for (size_t j = 0; j < skin.joints.size(); ++j) {
            const int joint = skin.joints[j];
            QMatrix4x4 jointWorld = (joint >= 0 && joint < static_cast<int>(world_.size()))
                                        ? world_[static_cast<size_t>(joint)]
                                        : QMatrix4x4();
            const QMatrix4x4 ibm = j < skin.inverseBind.size() ? skin.inverseBind[j] : QMatrix4x4();
            jointPalette[j] = invMesh * jointWorld * ibm;
        }
    };
    for (GpuPart& part : parts_) {
        const bool morph = !part.morphs.empty() && !morphWeights_.empty();
        const bool skin = part.skin >= 0 && !part.joints.empty() && !part.weights.empty();
        if (part.gpuSkin || part.gpuMorph) {
            continue;
        }
        if (!morph && !skin) {
            continue;
        }
        std::vector<float> verts = part.bindVertices;
        const size_t count = verts.size() / static_cast<size_t>(kVertexFloats);
        if (morph) {
            for (size_t t = 0; t < part.morphs.size() && t < morphWeights_.size(); ++t) {
                const float w = morphWeights_[t];
                if (std::fabs(w) < 1e-5f) {
                    continue;
                }
                const MorphTarget& target = part.morphs[t];
                for (size_t i = 0; i < count && i * 3 + 2 < target.positions.size(); ++i) {
                    verts[i * kVertexFloats] += target.positions[i * 3] * w;
                    verts[i * kVertexFloats + 1] += target.positions[i * 3 + 1] * w;
                    verts[i * kVertexFloats + 2] += target.positions[i * 3 + 2] * w;
                }
            }
        }
        if (skin) {
            skinPalette(part.skin, part.node);
            if (jointPalette.empty()) {
                continue;
            }
            for (size_t i = 0; i < count && (i + 1) * 4 <= part.joints.size(); ++i) {
                QMatrix4x4 skinMat;
                skinMat.fill(0);
                float wsum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    const int joint = static_cast<int>(part.joints[i * 4 + static_cast<size_t>(k)]);
                    const float w = part.weights[i * 4 + static_cast<size_t>(k)];
                    if (w <= 0.0f || joint < 0 || joint >= static_cast<int>(jointPalette.size())) {
                        continue;
                    }
                    for (int r = 0; r < 4; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            skinMat(r, c) += jointPalette[static_cast<size_t>(joint)](r, c) * w;
                        }
                    }
                    wsum += w;
                }
                if (wsum < 1e-5f) {
                    continue;
                }
                const QVector3D p(verts[i * kVertexFloats], verts[i * kVertexFloats + 1], verts[i * kVertexFloats + 2]);
                const QVector3D n(verts[i * kVertexFloats + 3], verts[i * kVertexFloats + 4],
                                  verts[i * kVertexFloats + 5]);
                const QVector3D skinned = skinMat.map(p);
                const QVector3D nrm = skinMat.mapVector(n).normalized();
                verts[i * kVertexFloats] = skinned.x();
                verts[i * kVertexFloats + 1] = skinned.y();
                verts[i * kVertexFloats + 2] = skinned.z();
                verts[i * kVertexFloats + 3] = nrm.x();
                verts[i * kVertexFloats + 4] = nrm.y();
                verts[i * kVertexFloats + 5] = nrm.z();
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());
    }
}

QMatrix4x4 ModelViewport::sceneCameraView(float* fovOut) const {
    if (sceneCameraIndex_ < 0 || sceneCameraIndex_ >= static_cast<int>(cpuModel_.cameras.size())) {
        return QMatrix4x4();
    }
    const GltfCamera& camera = cpuModel_.cameras[static_cast<size_t>(sceneCameraIndex_)];
    if (fovOut != nullptr && camera.perspective) {
        *fovOut = qRadiansToDegrees(camera.yfov);
    }
    QMatrix4x4 world;
    if (camera.node >= 0 && camera.node < static_cast<int>(world_.size())) {
        world = world_[static_cast<size_t>(camera.node)];
    } else {
        const std::vector<QMatrix4x4> local = composeWorldMatrices(cpuModel_);
        if (camera.node >= 0 && camera.node < static_cast<int>(local.size())) {
            world = cpuModel_.fit * local[static_cast<size_t>(camera.node)];
        }
    }
    return world.inverted();
}

int ModelViewport::pickPart(const QPoint& pos) {
    if (parts_.empty() || width() <= 0 || height() <= 0) {
        return -1;
    }
    float useFov = fov_;
    const QMatrix4x4 view = viewMatrix();
    sceneCameraView(&useFov);
    QMatrix4x4 proj;
    proj.perspective(useFov, static_cast<float>(width()) / static_cast<float>(height()), 0.05f, 120.0f);
    const QMatrix4x4 inv = (proj * view).inverted();
    auto unproject = [&](float x, float y, float z) {
        QVector4D ndc(2.0f * x / static_cast<float>(width()) - 1.0f,
                      1.0f - 2.0f * y / static_cast<float>(height()), z * 2.0f - 1.0f, 1.0f);
        QVector4D world = inv * ndc;
        if (std::fabs(world.w()) > 1e-8f) {
            world /= world.w();
        }
        return QVector3D(world.x(), world.y(), world.z());
    };
    const QVector3D origin = unproject(static_cast<float>(pos.x()), static_cast<float>(pos.y()), 0.0f);
    const QVector3D dest = unproject(static_cast<float>(pos.x()), static_cast<float>(pos.y()), 1.0f);
    const QVector3D dir = (dest - origin).normalized();
    float best = 1e9f;
    int hit = -1;
    for (int p = 0; p < static_cast<int>(parts_.size()); ++p) {
        const GpuPart& part = parts_[static_cast<size_t>(p)];
        if (!partVisible(part) || part.bindVertices.empty()) {
            continue;
        }
        QMatrix4x4 model;
        if (part.node >= 0 && part.node < static_cast<int>(world_.size())) {
            model = world_[static_cast<size_t>(part.node)];
        }
        const std::vector<float>& verts = part.bindVertices;
        const MeshPart* cpu = p < static_cast<int>(cpuModel_.parts.size()) ? &cpuModel_.parts[static_cast<size_t>(p)]
                                                                          : nullptr;
        if (cpu == nullptr) {
            continue;
        }
        for (size_t i = 0; i + 2 < cpu->indices.size(); i += 3) {
            auto vtx = [&](std::uint32_t idx) {
                const size_t o = static_cast<size_t>(idx) * static_cast<size_t>(kVertexFloats);
                return model.map(QVector3D(verts[o], verts[o + 1], verts[o + 2]));
            };
            const QVector3D a = vtx(cpu->indices[i]);
            const QVector3D b = vtx(cpu->indices[i + 1]);
            const QVector3D c = vtx(cpu->indices[i + 2]);
            const QVector3D e1 = b - a;
            const QVector3D e2 = c - a;
            const QVector3D pvec = QVector3D::crossProduct(dir, e2);
            const float det = QVector3D::dotProduct(e1, pvec);
            if (std::fabs(det) < 1e-8f) {
                continue;
            }
            const float invDet = 1.0f / det;
            const QVector3D tvec = origin - a;
            const float u = QVector3D::dotProduct(tvec, pvec) * invDet;
            if (u < 0.0f || u > 1.0f) {
                continue;
            }
            const QVector3D qvec = QVector3D::crossProduct(tvec, e1);
            const float v = QVector3D::dotProduct(dir, qvec) * invDet;
            if (v < 0.0f || u + v > 1.0f) {
                continue;
            }
            const float t = QVector3D::dotProduct(e2, qvec) * invDet;
            if (t > 0.05f && t < best) {
                best = t;
                hit = p;
            }
        }
    }
    return hit;
}

float ModelViewport::pickDepth(const QPoint& pos) {
    const int part = pickPart(pos);
    if (part < 0) {
        return distance_;
    }
    return qBound(0.4f, distance_ * 0.85f, 40.0f);
}

bool ModelViewport::exportTurntable(const QString& path, int width, int height, int frames) {
    if (path.isEmpty() || width < 64 || height < 64 || frames < 8) {
        return false;
    }
    const float savedYaw = yaw_;
    const bool savedRotate = autoRotate_;
    autoRotate_ = false;
    QTemporaryDir temp;
    if (!temp.isValid()) {
        return false;
    }
    frames = qBound(8, frames, 240);
    for (int i = 0; i < frames; ++i) {
        yaw_ = savedYaw + 360.0f * static_cast<float>(i) / static_cast<float>(frames);
        const QString framePath = temp.filePath(QStringLiteral("frame_%1.png").arg(i, 4, 10, QLatin1Char('0')));
        if (!exportRender(framePath, width, height)) {
            yaw_ = savedYaw;
            autoRotate_ = savedRotate;
            return false;
        }
    }
    yaw_ = savedYaw;
    autoRotate_ = savedRotate;
    QStringList args;
    args << QStringLiteral("-y") << QStringLiteral("-framerate") << QStringLiteral("30") << QStringLiteral("-i")
         << temp.filePath(QStringLiteral("frame_%04d.png")) << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << path;
    const int code = QProcess::execute(QStringLiteral("ffmpeg"), args);
    if (code != 0) {
        const QString dir = QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName();
        QDir().mkpath(dir);
        for (int i = 0; i < frames; ++i) {
            const QString src = temp.filePath(QStringLiteral("frame_%1.png").arg(i, 4, 10, QLatin1Char('0')));
            QFile::copy(src, dir + QLatin1Char('/') + QFileInfo(src).fileName());
        }
        status_ = QStringLiteral("ffmpeg missing — wrote %1 PNG frames").arg(frames);
        emit statusChanged();
        update();
        return true;
    }
    status_ = QStringLiteral("Exported turntable %1").arg(QFileInfo(path).fileName());
    emit statusChanged();
    update();
    return true;
}

void ModelViewport::buildIbl() {
    rebuildEnvironment();
}
