// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include <QGuiApplication>
#include <QtQuick/QQuickView>
#include <QQmlApplicationEngine>
#include <QVulkanInstance>
#include <QDebug>
#include "vulkanquickwindow.h"
#include "vulkancube.h"
#include "vulkansquircle.h"

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // Устанавливаем API рендеринга на Vulkan
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

    // Создаём глобальный экземпляр Vulkan
    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 3, 275));

#ifdef QT_DEBUG
    inst.setLayers(QByteArrayList() << "VK_LAYER_KHRONOS_validation");
#endif

    // Пытаемся создать с указанной версией
    if (!inst.create()) {
        qWarning() << "Failed to create Vulkan instance with version 1.3.275, falling back...";
        inst.setApiVersion(QVersionNumber());  // Без версии
        if (!inst.create()) {
            qFatal("Cannot create Vulkan instance");
        }
    }

    qDebug() << "Vulkan instance created with version:" << inst.apiVersion();

    // Регистрируем QML типы
    QQmlApplicationEngine engine;
    qmlRegisterType<VulkanQuickWindow>("VulkanUnderQML", 1, 0, "VulkanQuickWindow");
    qmlRegisterType<VulkanCube>("VulkanUnderQML", 1, 0, "VulkanCube");
    qmlRegisterType<VulkanSquircle>("VulkanUnderQML", 1, 0, "VulkanSquircle");

    // Загружаем QML
    engine.load(QUrl("qrc:///main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    // Получаем корневой объект (окно) и устанавливаем экземпляр Vulkan
    QObject *rootObject = engine.rootObjects().first();
    if (QQuickWindow *window = qobject_cast<QQuickWindow *>(rootObject)) {
        window->setVulkanInstance(&inst);
    } else {
        qWarning() << "Root object is not a QQuickWindow";
    }

    return app.exec();
}
