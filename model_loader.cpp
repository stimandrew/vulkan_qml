#include "model_loader.h"

bool ModelLoader::loadOBJ(const QString& filePath,
                          QVector<VertexData>& vertices,
                          QVector<uint32_t>& indices) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open OBJ file:" << filePath;
        return false;
    }

    QTextStream in(&file);
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;

    vertices.clear();
    indices.clear();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) {
            continue;
        }

        QStringList tokens = line.split(" ", Qt::SkipEmptyParts);
        if (tokens.isEmpty()) continue;

        if (tokens[0] == "v") { // Vertex position
            if (tokens.size() >= 4) {
                float x = tokens[1].toFloat();
                float y = tokens[2].toFloat();
                float z = tokens[3].toFloat();
                positions.append(QVector3D(x, y, z));
            }
        }
        else if (tokens[0] == "vn") { // Vertex normal
            if (tokens.size() >= 4) {
                float x = tokens[1].toFloat();
                float y = tokens[2].toFloat();
                float z = tokens[3].toFloat();
                normals.append(QVector3D(x, y, z));
            }
        }
        else if (tokens[0] == "vt") { // Texture coordinate
            if (tokens.size() >= 3) {
                float u = tokens[1].toFloat();
                float v = tokens[2].toFloat();
                texCoords.append(QVector2D(u, 1.0f - v)); // OBJ has v coordinate reversed
            }
        }
        else if (tokens[0] == "f") { // Face
            processFace(tokens, positions, normals, texCoords, vertices, indices);
        }
    }

    file.close();

    qDebug() << "Loaded OBJ model:" << filePath;
    qDebug() << "Vertices:" << vertices.size();
    qDebug() << "Indices:" << indices.size();

    return !vertices.isEmpty();
}

void ModelLoader::processFace(const QStringList& tokens,
                              QVector<QVector3D>& positions,
                              QVector<QVector3D>& normals,
                              QVector<QVector2D>& texCoords,
                              QVector<VertexData>& vertices,
                              QVector<uint32_t>& indices) {
    if (tokens.size() < 4) return; // Need at least 3 vertices for a face

    // For simplicity, assume triangles (will triangulate quads)
    for (int i = 1; i <= tokens.size() - 3; i++) {
        for (int j = 0; j < 3; j++) {
            int vertexIndex = (j == 0) ? 1 : (j == 1 ? i + 1 : i + 2);

            QStringList vertexData = tokens[vertexIndex].split("/");
            VertexData vertex;

            // Position
            if (!vertexData[0].isEmpty()) {
                int posIndex = vertexData[0].toInt() - 1; // OBJ indices start from 1
                if (posIndex >= 0 && posIndex < positions.size()) {
                    vertex.position = positions[posIndex];
                }
            }

            // Texture coordinate
            if (vertexData.size() > 1 && !vertexData[1].isEmpty()) {
                int texIndex = vertexData[1].toInt() - 1;
                if (texIndex >= 0 && texIndex < texCoords.size()) {
                    vertex.texCoord = texCoords[texIndex];
                } else {
                    vertex.texCoord = QVector2D(0, 0);
                }
            } else {
                vertex.texCoord = QVector2D(0, 0);
            }

            // Normal
            if (vertexData.size() > 2 && !vertexData[2].isEmpty()) {
                int normIndex = vertexData[2].toInt() - 1;
                if (normIndex >= 0 && normIndex < normals.size()) {
                    vertex.normal = normals[normIndex];
                } else {
                    vertex.normal = QVector3D(0, 0, 1);
                }
            } else {
                vertex.normal = QVector3D(0, 0, 1);
            }

            // Check if this vertex already exists
            int existingIndex = -1;
            for (int k = 0; k < vertices.size(); k++) {
                if (vertices[k].position == vertex.position &&
                    vertices[k].normal == vertex.normal &&
                    vertices[k].texCoord == vertex.texCoord) {
                    existingIndex = k;
                    break;
                }
            }

            if (existingIndex >= 0) {
                indices.append(existingIndex);
            } else {
                indices.append(vertices.size());
                vertices.append(vertex);
            }
        }
    }
}

bool ModelLoader::loadBuiltInCube(QVector<VertexData>& vertices,
                                  QVector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    // Создаем вершины куба
    for (int i = 0; i < 24; i++) {
        VertexData vertex;
        // Позиции вершин куба
        QVector<QVector3D> positions = {
            {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, // front
            {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, // back
            {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f}, // left
            {1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, // right
            {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}, // top
            {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f}  // bottom
        };

        vertex.position = positions[i];

        // Текстурные координаты
        QVector<QVector2D> texCoords = {
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
        };
        vertex.texCoord = texCoords[i];

        // Нормали
        QVector<QVector3D> normals = {
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f},
            {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}
        };
        vertex.normal = normals[i];

        vertices.append(vertex);
    }

    // Индексы куба
    QVector<uint32_t> cubeIndices = {
        0, 1, 2, 2, 3, 0, // front
        4, 5, 6, 6, 7, 4, // back
        8, 9, 10, 10, 11, 8, // left
        12, 13, 14, 14, 15, 12, // right
        16, 17, 18, 18, 19, 16, // top
        20, 21, 22, 22, 23, 20  // bottom
    };

    indices = cubeIndices;

    qDebug() << "Built-in cube loaded. Vertices:" << vertices.size()
             << "Indices:" << indices.size();

    return true;
}
