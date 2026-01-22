// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import VulkanUnderQML

Window {
    id: mainWindow
    width: 800
    height: 600
    visible: true
    title: "Vulkan QML"
    color: "white"

    VulkanBackground {
        id: background
        anchors.fill: parent
        z: -1 // Помещаем фон за кубом

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 10000; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 0; duration: 10000; easing.type: Easing.InOutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    // Вращающийся куб
    VulkanCube {
        id: cube
        anchors.centerIn: parent
        width: 400
        height: 400
        z: 1

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 5000; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 0; duration: 5000; easing.type: Easing.InOutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    // Кнопка переключения полноэкранного режима
        Rectangle {
            id: fullscreenButton
            width: 40
            height: 40
            radius: 2
            color: fullscreenMouseArea.containsMouse ? "#4444ff" : "#6666ff"
            border.color: "white"
            border.width: 2

            anchors.top: parent.top
            anchors.right: closeButton.left
            anchors.margins: 10

            Text {
                text: "⛶"  // Символ полноэкранного режима
                color: "white"
                font.pixelSize: 16
                font.bold: true
                anchors.centerIn: parent
            }

            MouseArea {
                id: fullscreenMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (mainWindow.visibility === Window.FullScreen) {
                        mainWindow.visibility = Window.Windowed
                    } else {
                        mainWindow.visibility = Window.FullScreen
                    }
                }
            }
        }

        // Кнопка закрытия окна
        Rectangle {
            id: closeButton
            width: 40
            height: 40
            radius: 2
            color: closeMouseArea.containsMouse ? "#ff4444" : "#ff6666"
            border.color: "white"
            border.width: 2

            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10

            Text {
                text: "✕"
                color: "white"
                font.pixelSize: 18
                font.bold: true
                anchors.centerIn: parent
            }

            MouseArea {
                id: closeMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: Qt.quit()
            }
        }

    // Оверлей с текстом
    Rectangle {
        color: Qt.rgba(1, 1, 1, 0.7)
        radius: 10
        border.width: 1
        border.color: "white"
        anchors.fill: label
        anchors.margins: -10
    }

    Text {
        id: label
        color: "black"
        wrapMode: Text.WordWrap
        text: qsTr("The background here is a squircle rendered with raw Vulkan using the beforeRendering() and beforeRenderPassRecording() signals in QQuickWindow. This text label and its border is rendered using QML")
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 20  // Упрощенная установка отступов
    }
}
