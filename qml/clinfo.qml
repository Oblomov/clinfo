#!/usr/bin/env -S QML_XHR_ALLOW_FILE_READ=1 qml

import QtQuick 2.12
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.12

ApplicationWindow
{
  visible: true

  id: clinfo
  title: "QML OpenCL info"

  GroupBox {

    anchors.fill: parent
    anchors.margins: font.pixelSize

    label: Rectangle {

      id: titleBox

      width: titleBoxText.width + font.pixelSize*2
      height: titleBoxText.font.pixelSize + font.pixelSize

      anchors.horizontalCenter: parent.horizontalCenter
      anchors.bottom: parent.top
      anchors.bottomMargin: -height/2
      color: clinfo.color

      Text {
        id: titleBoxText
        text: clinfo.title
        anchors.centerIn: parent
      }

    }

    ColumnLayout {
      anchors {
        fill: parent
        margins: font.pixelSize/2
        topMargin: font.pixelSize
      }

      GroupBox {
        id: loader_info
        Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
        Layout.fillWidth: true

        label: Rectangle {

          id: icdLoaderTitleBox

          width: icdLoaderTitleBoxText.width + font.pixelSize*2
          height: icdLoaderTitleBoxText.font.pixelSize + font.pixelSize

          anchors.horizontalCenter: parent.horizontalCenter
          anchors.bottom: parent.top
          anchors.bottomMargin: -height/2
          color: clinfo.color

          Text {
            id: icdLoaderTitleBoxText
            text: "libOpenCL (ICD loader)"
            anchors.centerIn: parent
          }

        }

        GridLayout {
          width: parent.width
          columns: 4

          InfoLabel { text: "Name" }
          InfoField { id: icdl_name }

          InfoLabel { text: "Version" }
          InfoField { id: icdl_version }

          InfoLabel { text: "Vendor" }
          InfoField { id: icdl_vendor }

          InfoLabel { text: "OpenCL Version" }
          InfoField { id: icdl_ocl_version }
        }
      }
    }

  }

  function request(url, callback)
  {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
      if (xhr.readyState == 4) {
        var o = eval('new Object(' + xhr.responseText + ')');
        callback(o);
      }
    }
    xhr.open('GET', url, true);
    xhr.send('');
  }

  function processData(object)
  {
    var l = object.icd_loader;
    if (l) {
      icdl_name.text = l.CL_ICDL_NAME;
      icdl_version.text = l.CL_ICDL_VERSION;
      icdl_ocl_version.text = l.CL_ICDL_OCL_VERSION;
      icdl_vendor.text = l.CL_ICDL_VENDOR;
    }
  }

  Component.onCompleted: {
    var path = Qt.resolvedUrl("../output.json");
    request(path, processData);
  }
}

// vim: set ft=qml sw=2 ts=2 et:
