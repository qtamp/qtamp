// Import scan target for the WebAssembly build: statically linked Qt has
// no runtime QML plugin loading, so this file tells qt_import_qml_plugins
// which module plugins (QtQuick, QtQuick.Window) the inline window QML in
// main.cpp needs baked into the binary. Never instantiated.
import QtQuick
import QtQuick.Window

Item {}
