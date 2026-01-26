// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef VULKANCUBE_H
#define VULKANCUBE_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

class CubeRenderer;

class VulkanCube : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)
    Q_PROPERTY(qreal cubePositionX READ cubePositionX WRITE setCubePositionX NOTIFY cubePositionXChanged)
    Q_PROPERTY(qreal cubePositionY READ cubePositionY WRITE setCubePositionY NOTIFY cubePositionYChanged)
    Q_PROPERTY(qreal cubePositionZ READ cubePositionZ WRITE setCubePositionZ NOTIFY cubePositionZChanged)
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
signals:
    void tChanged();
    void cubePositionXChanged();
    void cubePositionYChanged();
    void cubePositionZChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    void releaseResources() override;
    qreal m_t = 0;
    qreal m_cubePositionX = 0;
    qreal m_cubePositionY = 0;
    qreal m_cubePositionZ = -5;
    CubeRenderer *m_renderer = nullptr;
};

#endif
