QT += core sql
QT -= gui

# 编码设置
msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
    DEFINES += UNICODE _UNICODE
}

gcc {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}

CONFIG += c++17 console
CONFIG -= app_bundle

# 输出目录配置
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc

INCLUDEPATH += $$PWD/Base
INCLUDEPATH += $$PWD/FrameWork
INCLUDEPATH += $$PWD/Functions
INCLUDEPATH += $$PWD/Registry
INCLUDEPATH += $$PWD/Test



# 编译器警告选项
gcc {
    QMAKE_CXXFLAGS += -Wall
}
msvc {
    QMAKE_CXXFLAGS += /W4
}

# Windows 特定库
win32 {
    LIBS += -lkernel32
}

# Debug模式配置
CONFIG(debug, debug|release) {
    DEFINES += DEBUG_MODE
    TARGET = DeviceManager_d
} else {
    TARGET = DeviceManager
}

HEADERS += \
    Base/BaseDatabaseManager.h \
    FrameWork/DatabaseFramework.h \
    Functions/DeviceDatabaseManager/CameraInfoTable.h \
    Functions/DeviceDatabaseManager/DeviceDataBaseStruct.h \
    Functions/DeviceDatabaseManager/DeviceDatabaseManager.h \
    Registry/DatabaseRegistry.h \
    Test/DatabaseTestExample.h

SOURCES += \
    Base/BaseDatabaseManager.cpp \
    FrameWork/DatabaseFramework.cpp \
    Functions/DeviceDatabaseManager/CameraInfoTable.cpp \
    Functions/DeviceDatabaseManager/DeviceDatabaseManager.cpp \
    Registry/DatabaseRegistry.cpp \
    main.cpp
