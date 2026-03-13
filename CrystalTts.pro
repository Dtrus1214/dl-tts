QT       += core gui widgets

CONFIG += c++11

TARGET = CrystalTts

win32: LIBS += -luser32
unix:!macx: LIBS += -lxcb -lxcb-xtest -lxcb-keysyms

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    custombutton.cpp

HEADERS += \
    mainwindow.h \
    custombutton.h

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
