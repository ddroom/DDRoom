/*
 * main.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <string>

#include <QApplication>
#include <QtGlobal>

#include "cm.h"
#include "sgt.h"
#include "config.h"
#include "ddr_math.h"
#include "system.h"
#include "window.h"
#include "photo.h"

#include <exiv2/xmp.hpp>
#include <exiv2/error.hpp>

using namespace std;

//------------------------------------------------------------------------------
bool parse_arguments(int argc, char *argv[]);

void Exiv2_emptyHandler(int level, const char* s) {
}

void init_libraries(void) {
	// init Exiv2 XMP parser
	Exiv2::XmpParser::initialize();
	Exiv2::LogMsg::setHandler(Exiv2_emptyHandler);
	CM::initialize();
	// do all initializations, with splash screen, here
	// TODO: call math initialize here 
	compression_function(1.0, 1.0);
}

//------------------------------------------------------------------------------
int main(int argc, char *argv[]) {
	try {
		QApplication *application = new QApplication(argc, argv);
		qRegisterMetaType<std::string>("std::string");
		qRegisterMetaType<QVector<long> >("QVector<long>");
		qRegisterMetaType<QVector<double> >("QVector<double>");
		qRegisterMetaType<Photo_ID>("Photo_ID");
		qRegisterMetaType<QList<Photo_ID> >("QList<Photo_ID>");

#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
		{ // patch for plugins
			QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MAC
			dir.cdUp();
#endif
			dir.cd("plugins");
			QCoreApplication::addLibraryPath(dir.absolutePath());
		}
#endif
		// some UI initialization
		application->setWindowIcon(QIcon(":/resources/ddroom.svg"));
		// style
/*
#ifndef Q_OS_MAC
	#ifndef Q_OS_WIN
		QApplication::setStyle("plastique");
	#endif
#endif
*/
		//
		init_libraries();
		if(argc != 1) {
			if(parse_arguments(argc, argv))
				return 0;
		}

		Window *window = new Window();
//		window->setAttribute(Qt::WA_MacBrushedMetal);
//		window->setWindowFlags(Qt::MacWindowToolBarButtonHint);
		window->show();
		int rez = application->exec();
		delete window;
		delete application;
		Config::instance()->finalize();
		return rez;
	} catch(string error) {
		cerr << "throwed message is:" << endl;
		cerr << error << endl;
		return -1;
	}
	return 0;
}

//------------------------------------------------------------------------------
bool parse_arguments(int argc, char *argv[]) {
#if 0
//	cerr << "here is arguments:" << endl;
	bool rez = false;
	for(int i = 1; i < argc; i++) {
		string arg = argv[i];
		if(arg == "--help") {
			cerr << "--generate-sgt - calculate and save saturation gamut tables as cm-cs-ver.sgt files" << endl;
			rez = true;
		}
		if(arg == "--generate-sgt") {
			Saturation_Gamut::generate_sgt();
			rez = true;
		}
//		cerr << "argv[" << i << "] == " << argv[i] << endl;
	}
	return rez;
#else
	return true;
#endif
}

//------------------------------------------------------------------------------
