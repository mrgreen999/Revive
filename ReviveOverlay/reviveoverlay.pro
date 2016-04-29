#-------------------------------------------------
#
# Project created by QtCreator 2015-06-10T16:57:45
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ReviveOverlay
TEMPLATE = app


SOURCES += main.cpp\
        overlaywidget.cpp \
    openvroverlaycontroller.cpp

HEADERS  += overlaywidget.h \
    openvroverlaycontroller.h

FORMS    += overlaywidget.ui

INCLUDEPATH += ../openvr/headers

LIBS += -L../openvr/lib/win64 -lopenvr_api

Debug:DESTDIR = ../Debug
Release:DESTDIR = ../Release
