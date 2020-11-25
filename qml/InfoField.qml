import QtQuick 2.12
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.12


TextField {
  property int span;
  span: 1;
  Layout.fillWidth: true
  Layout.columnSpan: span
  readOnly: true
  autoScroll: false
  text: "(unknown)"
  onFocusChanged: {
    if (focus) {
      autoScroll = true;
      selectAll()
    }
  }
}

// vim: set ft=qml sw=2 ts=2 et:
