import QtQuick

Item {
    id: controller
    property real xPosition: 0
    property real yPosition: 0
    property real zPosition: -5

    function resetPosition() {
        xPosition = 0
        yPosition = 0
        zPosition = -5
    }

    function randomPosition() {
        xPosition = (Math.random() * 8 - 4)
        yPosition = (Math.random() * 8 - 4)
        zPosition = -(Math.random() * 15 + 5)
    }
}
