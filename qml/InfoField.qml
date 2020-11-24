import QtQuick 2.12
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.12


TextField {
  Layout.fillWidth: true
  readOnly: true;
  text: "(unknown)"
  onFocusChanged: {
    if (focus) {
      selectAll()
    }
  }
}

// vim: set ft=qml sw=2 ts=2 et:
