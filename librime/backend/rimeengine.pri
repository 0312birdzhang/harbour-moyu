nativeRoot = $$absolute_path(../../.rime-build, $$PWD)
INCLUDEPATH += $$PWD $$nativeRoot/source/librime/src $$nativeRoot/source/librime/include \
    $$nativeRoot/build/librime/src $$nativeRoot/prefix/include
LIBS += -Wl,--whole-archive $$nativeRoot/prefix/lib/librime.a -Wl,--no-whole-archive \
    $$nativeRoot/prefix/lib/libopencc.a $$nativeRoot/prefix/lib/libleveldb.a \
    $$nativeRoot/prefix/lib/libmarisa.a -lyaml-cpp -lboost_regex -lpthread
QMAKE_LFLAGS += -Wl,--exclude-libs,ALL
HEADERS += $$PWD/rimebackend.h $$PWD/legacydictionary.h
SOURCES += $$PWD/rimebackend.cpp $$PWD/legacydictionary.cpp $$PWD/dictionaryfiles.cpp
