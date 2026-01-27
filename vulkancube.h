// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef VULKANCUBE_H
#define VULKANCUBE_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QVector>
#include <QUrl>

class CubeRenderer;

class VulkanCube : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)
    Q_PROPERTY(qreal cubePositionX READ cubePositionX WRITE setCubePositionX NOTIFY cubePositionXChanged)
    Q_PROPERTY(qreal cubePositionY READ cubePositionY WRITE setCubePositionY NOTIFY cubePositionYChanged)
    Q_PROPERTY(qreal cubePositionZ READ cubePositionZ WRITE setCubePositionZ NOTIFY cubePositionZChanged)
    Q_PROPERTY(QUrl modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(bool useCustomModel READ useCustomModel WRITE setUseCustomModel NOTIFY useCustomModelChanged)
    QML_ELEMENT

public:
    VulkanCube();
    qreal t() const { return m_t; }
    void setT(qreal t);

    qreal cubePositionX() const { return m_cubePositionX; }
    void setCubePositionX(qreal x);

    qreal cubePositionY() const { return m_cubePositionY; }
    void setCubePositionY(qreal y);

    qreal cubePositionZ() const { return m_cubePositionZ; }
    void setCubePositionZ(qreal z);

    QUrl modelPath() const { return m_modelPath; }
    void setModelPath(const QUrl& path);

    bool useCustomModel() const { return m_useCustomModel; }
    void setUseCustomModel(bool use);

signals:
    void tChanged();
    void cubePositionXChanged();
    void cubePositionYChanged();
    void cubePositionZChanged();
    void modelPathChanged();
    void useCustomModelChanged();
    void modelLoaded();

public slots:
    void sync();
    void cleanup();
    void loadModel(const QUrl& fileUrl);

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    void releaseResources() override;
    qreal m_t = 0;
    qreal m_cubePositionX = 0;
    qreal m_cubePositionY = 0;
    qreal m_cubePositionZ = -5;
    QUrl m_modelPath;
    bool m_useCustomModel = false;
    CubeRenderer *m_renderer = nullptr;
};

#endif
