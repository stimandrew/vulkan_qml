#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QFile>
#include <QTextStream>
#include <QDebug>

struct VertexData {
    QVector3D position;
    QVector3D normal;
    QVector2D texCoord;
};

class ModelLoader {
public:
    bool loadOBJ(const QString& filePath,
                 QVector<VertexData>& vertices,
                 QVector<uint32_t>& indices);

    bool loadBuiltInCube(QVector<VertexData>& vertices,
                         QVector<uint32_t>& indices);

private:
    void processFace(const QStringList& tokens,
                     QVector<QVector3D>& positions,
                     QVector<QVector3D>& normals,
                     QVector<QVector2D>& texCoords,
                     QVector<VertexData>& vertices,
                     QVector<uint32_t>& indices);
};

#endif // MODEL_LOADER_H
