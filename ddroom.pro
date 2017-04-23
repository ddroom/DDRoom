TEMPLATE = app

HEADERS	= \
	src/version.h \
	src/window.h \
\
	src/config.h \
	src/system.h \
	src/mt.h \
	src/memory.h \
	src/area.h \
	src/area_helper.h \
	src/dataset.h \
	src/filter.h \
	src/filter_cp.h \
	src/filter_gp.h \
\
	src/browser.h \
	src/thumbnail_view.h \
	src/thumbnail_loader.h \
	src/view.h \
	src/view_header.h \
	src/view_clock.h \
	src/view_zoom.h \
	src/views_layout.h \
\
	src/demosaic_pattern.h \
	src/dcraw.h \
	src/import.h \
	src/import_exiv2.h \
	src/import_raw.h \
	src/import_jpeg.h \
	src/import_j2k.h \
	src/import_png.h \
	src/import_tiff.h \
\
	src/f_demosaic.h \
	src/f_demosaic_int.h \
	src/f_vignetting.h \
	src/f_chromatic_aberration.h \
	src/f_shift.h \
	src/f_projection.h \
	src/f_rotation.h \
	src/f_crop.h \
\
	src/f_wb.h \
	src/f_crgb_to_cm.h \
	src/f_cm_lightness.h \
	src/f_cm_rainbow.h \
	src/f_cm_sepia.h \
	src/f_cm_colors.h \
	src/f_unsharp.h \
	src/f_cm_to_cs.h \
\
	src/f_soften.h \
\
	src/metadata.h \
	src/photo.h \
	src/photo_storage.h \
	src/tiles.h \
	src/process_h.h \
	src/edit.h \
	src/edit_history.h \
	src/widgets.h \
	src/gui_curve.h \
	src/gui_curve_histogram.h \
	src/gui_slider.h \
	src/gui_ct.h \
	src/gui_ct_picker.h \
\
	src/ddr_math.h \
	src/misc.h \
	src/cm.h \
	src/sgt.h \
	src/sgt_locus.h \
	src/cms_matrix.h \
\
	src/batch.h \
	src/batch_dialog.h \
	src/export.h \
\
	src/profiler_vignetting.h
#	src/db.h \
#	src/f_distortion.h \


SOURCES	= \
	src/main.cpp \
	src/window.cpp \
\
	src/config.cpp \
	src/system.cpp \
	src/mt.cpp \
	src/memory.cpp \
	src/area.cpp \
	src/area_helper.cpp \
	src/dataset.cpp \
	src/filter.cpp \
	src/filter_cp.cpp \
	src/filter_gp.cpp \
\
	src/browser.cpp \
	src/thumbnail_view.cpp \
	src/thumbnail_loader.cpp \
	src/view.cpp \
	src/view_header.cpp \
	src/view_clock.cpp \
	src/view_zoom.cpp \
	src/views_layout.cpp \
\
	src/dcraw.cpp \
	src/import.cpp \
	src/import_exiv2.cpp \
	src/import_raw.cpp \
	src/import_jpeg.cpp \
	src/import_j2k.cpp \
	src/import_tiff.cpp \
	src/import_png.cpp \
\
	src/f_demosaic.cpp \
	src/f_demosaic_ca.cpp \
	src/f_demosaic_dg.cpp \
	src/f_demosaic_ahd.cpp \
	src/f_vignetting.cpp \
	src/f_chromatic_aberration.cpp \
	src/f_shift.cpp \
	src/f_projection.cpp \
	src/f_rotation.cpp \
	src/f_crop.cpp \
\
	src/f_wb.cpp \
	src/f_crgb_to_cm.cpp \
	src/f_cm_lightness.cpp \
	src/f_cm_rainbow.cpp \
	src/f_cm_sepia.cpp \
	src/f_cm_colors.cpp \
	src/f_cm_to_cs.cpp \
\
	src/f_unsharp.cpp \
	src/f_soften.cpp \
\
	src/metadata.cpp \
	src/photo.cpp \
	src/photo_storage.cpp \
	src/tiles.cpp \
	src/process.cpp \
	src/edit.cpp \
	src/edit_history.cpp \
	src/widgets.cpp \
	src/gui_curve.cpp \
	src/gui_curve_histogram.cpp \
	src/gui_slider.cpp \
	src/gui_ct.cpp \
	src/gui_ct_picker.cpp \
\
	src/ddr_math.cpp \
	src/cm.cpp \
	src/sgt.cpp \
	src/sgt_locus.cpp \
	src/cms_matrix.cpp \
\
	src/batch.cpp \
	src/batch_dialog.cpp \
	src/export.cpp \
\
	src/profiler_vignetting.cpp
#	src/db.cpp \
#	src/f_distortion.cpp \


mac {
	INCLUDEPATH += /usr/local/include 
	LIBS += -L/usr/local/lib 
}

win32 {
	INCLUDEPATH += /local/include
	LIBS += -L/local/lib -lws2_32 # for htonl etc...
}

#LIBS += -lexiv2 -ljpeg -lpng -lz -ltiff -lopenjpeg -llensfun
LIBS += -lexiv2
LIBS += -ljpeg -lpng -lz -ltiff -lopenjp2
LIBS += -lOpenCL

# suppress 'unused-result' for dcraw.cpp
#QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-sign-compare -Wno-unused-result
QMAKE_CXXFLAGS_WARN_ON = -Wall -Wextra -Wno-unused-parameter

QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_RELEASE += -fstack-check
QMAKE_CXXFLAGS_RELEASE += -g
QMAKE_CFLAGS_RELEASE -= -O2
QMAKE_CFLAGS_RELEASE += -O3
QMAKE_CFLAGS_RELEASE += -g

QMAKE_CXXFLAGS_DEBUG += -O3
QMAKE_CXXFLAGS_DEBUG += -ggdb
QMAKE_CXXFLAGS_DEBUG += -fstack-check
QMAKE_CXXFLAGS_DEBUG += -g
QMAKE_CFLAGS_DEBUG += -O3
QMAKE_CFLAGS_DEBUG += -ggdb
QMAKE_CFLAGS_DEBUG += -g

QMAKE_CXXFLAGS_RELEASE += -pedantic
QMAKE_CXXFLAGS_DEBUG += -pedantic

CONFIG += debug_and_release
#CONFIG += debug
#CONFIG += release

CONFIG += thread
CONFIG += c++11

STRIP = 

win32 {
	RC_ICONS = resources/ddroom.ico
#	CONFIG += console
# -O3 leads to unstable code with win32, use -O2 instead
	QMAKE_CFLAGS_RELEASE -= -O3
	QMAKE_CFLAGS_RELEASE += -O2
	QMAKE_CXXFLAGS_RELEASE -= -O3
	QMAKE_CXXFLAGS_RELEASE += -O2
}

QT += widgets svg xml

debug:DESTDIR = debug
debug:target.path = ./
debug:target = ddroom_d

release:DESTDIR = release
release:target.path = ./
release:target = ddroom

OBJECTS_DIR = $$DESTDIR/.obj
MOC_DIR = $$DESTDIR/.moc
RCC_DIR = $$DESTDIR/.qrc
UI_DIR = $$DESTDIR/.ui

sources.files = ddroom.pro $$SOURCES $$HEADERS $$RESOURCES
sources.path = ./src/
INSTALLS += target
RESOURCES = ddroom.qrc

