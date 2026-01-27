import QtQuick
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import Qt.labs.platform 1.1 as Platform
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
        z: -1

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 10000; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 0; duration: 10000; easing.type: Easing.InOutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    // Вращающийся 3D объект (куб или загруженная модель)
    VulkanCube {
        id: cube
        anchors.centerIn: parent
        width: 400
        height: 400
        z: 1

        // Привязки к позиции
        cubePositionX: positionController.xPosition
        cubePositionY: positionController.yPosition
        cubePositionZ: positionController.zPosition

        // ИСПРАВЛЕНО: Убрана попытка доступа к m_renderer
        Component.onCompleted: {
            // Загрузка куба происходит автоматически при создании
            useCustomModel = false
            modelPath = ""
        }

        // Анимация вращения
        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 5000; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 0; duration: 5000; easing.type: Easing.InOutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

    // Панель управления
    Rectangle {
        id: controlPanel
        width: 300
        height: 320
        color: Qt.rgba(1, 1, 1, 0.8)
        radius: 10
        border.width: 2
        border.color: "#6666ff"

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 20

        Column {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 10

            Text {
                text: "Панель управления 3D объекта"
                font.bold: true
                font.pixelSize: 16
                color: "#333"
            }

            Rectangle {
                width: parent.width
                height: 1
                color: "#ccc"
            }

            // Информация о текущей модели
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: cube.useCustomModel ?
                      "Модель: " + (cube.modelPath.toString().split('/').pop() || "OBJ файл") :
                      "Модель: Куб (по умолчанию)"
                font.pixelSize: 12
                color: "#666"
                padding: 5
            }

            // Управление по оси X
            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: "X:"
                    width: 30
                    font.pixelSize: 14
                    color: "#333"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Slider {
                    id: xSlider
                    width: parent.width - 90
                    from: -20
                    to: 20
                    value: positionController.xPosition
                    stepSize: 0.1
                    onValueChanged: positionController.xPosition = value
                }

                Text {
                    text: xSlider.value.toFixed(1)
                    width: 40
                    font.pixelSize: 12
                    color: "#666"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Управление по оси Y
            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: "Y:"
                    width: 30
                    font.pixelSize: 14
                    color: "#333"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Slider {
                    id: ySlider
                    width: parent.width - 90
                    from: -10
                    to: 10
                    value: positionController.yPosition
                    stepSize: 0.1
                    onValueChanged: positionController.yPosition = value
                }

                Text {
                    text: ySlider.value.toFixed(1)
                    width: 40
                    font.pixelSize: 12
                    color: "#666"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Управление по оси Z
            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: "Z:"
                    width: 30
                    font.pixelSize: 14
                    color: "#333"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Slider {
                    id: zSlider
                    width: parent.width - 90
                    from: -5000
                    to: 500
                    value: positionController.zPosition
                    stepSize: 0.1
                    onValueChanged: positionController.zPosition = value
                }

                Text {
                    text: zSlider.value.toFixed(1)
                    width: 40
                    font.pixelSize: 12
                    color: "#666"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Кнопки управления моделью
            Row {
                width: parent.width
                spacing: 10

                Button {
                    text: "Загрузить OBJ"
                    width: (parent.width - 10) / 2
                    onClicked: {
                        fileDialog.open()
                    }
                }

                Button {
                    text: "Использовать куб"
                    width: (parent.width - 10) / 2
                    onClicked: {
                        cube.useCustomModel = false
                        cube.modelPath = ""
                    }
                }
            }

            // Кнопки сброса
            Row {
                width: parent.width
                spacing: 10

                Button {
                    text: "Сбросить позицию"
                    width: (parent.width - 10) / 2
                    onClicked: {
                        positionController.resetPosition()
                        xSlider.value = 0
                        ySlider.value = 0
                        zSlider.value = -5
                    }
                }

                Button {
                    text: "Случайная позиция"
                    width: (parent.width - 10) / 2
                    onClicked: {
                        positionController.randomPosition()
                        xSlider.value = positionController.xPosition
                        ySlider.value = positionController.yPosition
                        zSlider.value = positionController.zPosition
                    }
                }
            }
        }
    }

    // Диалог выбора файла - ИСПРАВЛЕНО: убран StandardPaths
    Platform.FileDialog {
        id: fileDialog
        title: "Выберите OBJ модель"
        folder: Qt.resolvedUrl(".") // ИСПРАВЛЕНО
        nameFilters: ["OBJ files (*.obj)", "All files (*)"]
        onAccepted: {
            cube.loadModel(fileDialog.file)
        }
    }

    // Контроллер позиции куба
    PositionController {
        id: positionController
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
            text: "⛶"
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
        anchors.margins: 20
    }
}
