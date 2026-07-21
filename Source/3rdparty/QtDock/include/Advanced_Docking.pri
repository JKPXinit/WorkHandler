

include(../../../../Pro_Config.pri)

CONFIG(debug, debug|release){
    win32-g++ {
        versionAtLeast(QT_VERSION, 5.15.0) {
                LIBS +=-L$$software_build_path/bin/ -lqtadvanceddocking
        }
        else {
                LIBS +=-L$$software_build_path/bin/ -lqtadvanceddockingd
        }
    }
    else:msvc {
        LIBS +=-L$$software_build_path/bin/ -lqtadvanceddockingd
    }
    else:mac {
        LIBS +=-L$$software_build_path/bin/ -lqtadvanceddocking_debug
    }
    else {
        LIBS +=-L$$software_build_path/bin/ -lqtadvanceddocking
    }
}
else{
    LIBS +=-L$$software_build_path/bin/ -lqtadvanceddocking
}

#引入目录
INCLUDEPATH += $$PWD


unix:!macx {
    LIBS += -lxcb
}
