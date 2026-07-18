QT += core gui network widgets

CONFIG += c++17
DESTDIR = ../../binary
INCLUDEPATH += ../../dev/core/src/include
INCLUDEPATH += ../../dev/client/src/include

SOURCES += \
    ../../dev/client/src/chat_window.cpp \
    ../../dev/client/src/main.cpp \
    ../../dev/core/src/packet_codec.cpp \
    ../../dev/core/src/validation.cpp

HEADERS += \
    ../../dev/client/src/include/chat_window.h \
    ../../dev/core/src/include/chat_types.h \
    ../../dev/core/src/include/packet_codec.h \
    ../../dev/core/src/include/validation.h

TARGET = QtClient
TEMPLATE = app

FORMS += ../../dev/client/form/chat_window.ui
