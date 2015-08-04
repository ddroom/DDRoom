#!/bin/sh

MINGW_PATH=""
MSYS_PATH=""
SYS_PATH=""
QT_PATH="/c/Qt/Qt5.5.0/5.5/mingw492_32"

TARGET="./win32"

# create target
rm -Rf ${TARGET}
mkdir -p ${TARGET}
mkdir -p ${TARGET}/plugins
mkdir -p ${TARGET}/plugins/imageformats
mkdir -p ${TARGET}/platforms
cp -f ./release/ddroom.exe ${TARGET}/

list_sys="libgcc_s_dw2-1 libwinpthread-1 libstdc++-6"
for target in ${list_sys} ; do
	cp ${SYS_PATH}/bin/${target}.dll ${TARGET}/
done

list_glib="libglib-2.0-0 libintl-8 libiconv-2 pthreadGC2 "
for target in ${list_glib} ; do
	cp ${SYS_PATH}/bin/${target}.dll ${TARGET}/
done

list_qt="Qt5Core Qt5Gui Qt5Widgets Qt5Xml Qt5Svg icuin54 icuuc54 icudt54 "
for target in ${list_qt} ; do
	cp ${QT_PATH}/bin/${target}.dll ${TARGET}/
done

list_sys_local="libexiv2-13 libexpat-1 zlib1 libjpeg-9 liblensfun libopenjpeg-1 libpng16-16 libtiff-5 "
for target in ${list_sys_local} ; do
	cp ${SYS_PATH}/local/bin/${target}.dll ${TARGET}/
done

list_plugins_img="qjpeg qsvg qtiff"
for target in ${list_plugins_img} ; do
	cp ${QT_PATH}/plugins/imageformats/${target}.dll ${TARGET}/plugins/imageformats/
done

cp ${QT_PATH}/plugins/platforms/qwindows.dll ${TARGET}/platforms/

# lensfun DB
mkdir -p ${TARGET}/lensfun/db
cp ${MSYS_PATH}/local/share/lensfun/* ${TARGET}/lensfun/db/

