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

          InfoLabel { text: "Vendor" }
          InfoField { id: icdl_vendor }

          InfoLabel { text: "Version" }
          InfoField { id: icdl_version }

          InfoLabel { text: "OpenCL Version" }
          InfoField { id: icdl_ocl_version }
        }
      }

      TabBar {
        id: platform_tabs
        Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
        Layout.fillWidth: true

        Component {
          id: platform_tab
          TabButton { }
        }
      }

      StackLayout {
        id: platform_pages
        width: parent.width
        currentIndex: platform_tabs.currentIndex

        Component {
          id: platform_page

          GroupBox {

            GridLayout {
              id: layout
              width: parent.width
              columns: 4
            }

            function addProperty(name, value, span = 1) {
              var l = { text: name };
              var f = { text: value };
              if (span > 1) { f.span = 2*span - 1; }
              Qt.createComponent("InfoLabel.qml").createObject(layout, l);
              Qt.createComponent("InfoField.qml").createObject(layout, f);
            }
          }
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
    const icdl_builtin = [ "CL_ICDL_NAME", "CL_ICDL_VERSION", "CL_ICDL_OCL_VERSION", "CL_ICDL_VENDOR" ];
    var l = object.icd_loader;
    if (l) {
      icdl_name.text = l.CL_ICDL_NAME;
      icdl_version.text = l.CL_ICDL_VERSION;
      icdl_ocl_version.text = l.CL_ICDL_OCL_VERSION;
      icdl_vendor.text = l.CL_ICDL_VENDOR;
    }

    const platform_builtin = [ "CL_PLATFORM_NAME", "CL_PLATFORM_VENDOR", "CL_PLATFORM_VERSION",
      "CL_PLATFORM_PROFILE", "CL_PLATFORM_ICD_SUFFIX_KHR", "CL_PLATFORM_EXTENSIONS" ];

    var plist = object.platforms;
    var dlist = object.devices;
    for (var p = 0; p < plist.length; ++p) {
      var plat = plist[p];
      platform_tabs.addItem(platform_tab.createObject(platform_tabs, {
        active: true,
        text: plat.CL_PLATFORM_NAME
      }));
      var page = platform_page.createObject(platform_pages, { active: true });
      page.addProperty("Name", plat.CL_PLATFORM_NAME);
      page.addProperty("Vendor", plat.CL_PLATFORM_VENDOR);
      page.addProperty("Version", plat.CL_PLATFORM_VERSION, 2);
      page.addProperty("Profile", plat.CL_PLATFORM_PROFILE);
      page.addProperty("ICD suffix", plat.CL_PLATFORM_ICD_SUFFIX_KHR);
      page.addProperty("Extensions", plat.CL_PLATFORM_EXTENSIONS, 2);
      for (var name in plat) {
        if (platform_builtin.includes(name)) { continue; }
        /* Assemble a proper name */
        var present_name = name.replace(/^CL_PLATFORM_/, '').
          replace(/_/g, ' ');
        // TODO Title case, but recognize extension suffixes (how?)
        // TODO even better, use the clinfo own map
        var value = plat[name];
        if (typeof(value) == 'object') {
          value = value.raw
        }
        page.addProperty(present_name, '' + value);
      }
    }
  }

  Component.onCompleted: {
    var path = Qt.resolvedUrl("../output.json");
    request(path, processData);
  }
}

// vim: set ft=qml sw=2 ts=2 et:
