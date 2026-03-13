# Offline TTS uses sherpa-onnx C API (in-process) + QMediaPlayer
# Set SHERPA_ONNX_DIR to sherpa-onnx install dir (e.g. C:/sherpa-onnx-install) to auto-add include/lib.
QT       += core gui widgets multimedia concurrent svg

SHERPA_ONNX_DIR = C:/sherpa-onnx

!isEmpty(SHERPA_ONNX_DIR) {
    INCLUDEPATH += $$SHERPA_ONNX_DIR/include
    win32: LIBS += -L$$SHERPA_ONNX_DIR/lib -lsherpa-onnx-c-api
    unix: LIBS += -L$$SHERPA_ONNX_DIR/lib -lsherpa-onnx-c-api
}

CONFIG += c++11

TARGET = CrystalTts

win32: LIBS += -luser32
unix:!macx: LIBS += -lxcb -lxcb-xtest -lxcb-keysyms

DEFINES += QT_DEPRECATED_WARNINGS

# Optional: set POPPLER_DIR to Poppler install dir for PDF viewer (e.g. C:/poppler-build)
POPPLER_DIR = c:/poppler-build

!isEmpty(POPPLER_DIR) {
    DEFINES += HAVE_POPPLER
    INCLUDEPATH += $$POPPLER_DIR/include $$POPPLER_DIR/include/poppler
    win32: LIBS += -L$$POPPLER_DIR/lib -lpoppler-qt5
    unix: LIBS += -L$$POPPLER_DIR/lib -lpoppler-qt5
}

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    custombutton.cpp \
    tts/ttsengine.cpp \
    pdfviewerform.cpp

HEADERS += \
    mainwindow.h \
    custombutton.h \
    tts/ttsengine.h \
    pdfviewerform.h

RESOURCES += icons.qrc

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
