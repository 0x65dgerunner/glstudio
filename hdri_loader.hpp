#pragma once

#include <QString>
#include <QVector3D>

#include <vector>

struct HdriImage {
    int width = 0;
    int height = 0;
    int cubeSize = 0;
    std::vector<float> rgb;
    std::vector<float> cube[6];
    QString error;
    QString name;
    QString path;

    bool hasLatLong() const { return width > 0 && height > 0 && static_cast<int>(rgb.size()) >= width * height * 3; }
    bool hasCube() const { return cubeSize > 0 && cube[0].size() == static_cast<size_t>(cubeSize * cubeSize * 4); }
    QVector3D sampleDir(const QVector3D& dir) const;
};

struct HdriEntry {
    QString name;
    QString path;
};

QString ensureUserHdriDir();
std::vector<HdriEntry> listHdriFiles();
QString findHdriPath();
HdriImage loadHdri(const QString& path, int cubeSize = 512);
HdriImage loadExrHdri(const QString& path, int cubeSize = 512);
void cubemapFaceDirection(int face, float u, float v, QVector3D* dir);
