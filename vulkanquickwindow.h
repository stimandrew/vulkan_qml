// vulkanquickwindow.h
#ifndef VULKANQUICKWINDOW_H
#define VULKANQUICKWINDOW_H

#include <QtQuick/QQuickView>

class VulkanQuickWindow : public QQuickView
{
    Q_OBJECT

public:
    VulkanQuickWindow();
    ~VulkanQuickWindow();
};

#endif
