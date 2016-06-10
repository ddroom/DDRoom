/*
 * system.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
//#include <stdint.h>
//#include <unistd.h>
#include <QtGlobal>

#ifdef Q_OS_WIN32
	// CPU count for Windows
	#include <windows.h>
	#include <winbase.h>
#else
	#include <stdint.h>
	// CPU count for MacOSX (BSD systems)
	#include <sys/sysctl.h>
	#include <sys/types.h>
#endif

#include <lensfun/lensfun.h>

#include "system.h"
#include "config.h"

using namespace std;

#undef _PROFILER_OFF
//#define _PROFILER_OFF

//------------------------------------------------------------------------------
// Time profiler. Last entry should be ""
//
Profiler::Profiler(string module) : _module(module) {
	cr_mark = "";
}

void Profiler::mark(string mark) {
	if(cr_mark == "") {
		cr_time = QTime::currentTime();
	} else {
		long d = cr_time.restart();
		vector<pair<string, long> >::iterator it = prof.begin();
		bool flag = false;
		for(; it != prof.end(); ++it) {
			if((*it).first == cr_mark) {
				flag = true;
				break;
			}
		}
		if(flag)
			(*it).second += d;
		else
			prof.push_back(pair<string, long>(cr_mark, d));
	}
	cr_mark = mark;
}

Profiler::~Profiler() {
#ifndef _PROFILER_OFF
	cerr << "__________________________________" << endl;
	cerr << "Profile for " << _module << " : " << endl;
	long total = 0;
	for(vector<pair<string, long> >::iterator it = prof.begin(); it != prof.end(); ++it) {
		QString str;
		str.sprintf("%d:%03d", (int)((*it).second / 1000), (int)((*it).second % 1000));
		total += (*it).second;
		cerr << str.toLocal8Bit().constData() << " sec. for " << (*it).first << endl;
	}
	QString str;
	str.sprintf("TOTAL:    %d:%03d", (int)(total / 1000), (int)(total % 1000));
	cerr << str.toLocal8Bit().constData() << endl;
	cerr << "==================================" << endl;
#endif
}

/*------------------------------------------------------------------------------
 * System capabilities description (singleton)
 */

System *System::_this = nullptr;

#define cpuid(func,ax,bx,cx,dx) \
	__asm__ __volatile__ ("cpuid": \
	"=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));

System::System(void) {
	_cores = QThread::idealThreadCount();
	if(_cores == -1) {
		_cores = 1;
		// Linux
#ifdef _SC_NPROCESSORS_CONF
		_cores = sysconf(_SC_NPROCESSORS_CONF);
#endif

		// BSD systems - MacOSX, probably *BSD
#if defined(CTL_HW) && defined(HW_NCPU)
		int mib[2], cpuc;
		size_t len;
		mib[0] = CTL_HW;
		mib[1] = HW_NCPU;
		len = sizeof(cpuc);
		sysctl(mib, 2, &cpuc, &len, nullptr, 0);
		_cores = cpuc;
#endif

		// WINDOWS
#ifdef Q_OS_WIN
		SYSTEM_INFO SysInfo;
		GetSystemInfo(&SysInfo);
		DWORD count = SysInfo.dwNumberOfProcessors;
		_cores = count;
#endif

#ifdef Q_CC_GNU
		// CPU id
		_sse2 = false;
		int ax, bx, cx, dx;
		cpuid(0x01, ax, bx, cx, dx);
		if(dx & (1 << 26))
			_sse2 = true;
		detected_sse2 = _sse2;
		cerr << "SSE2: " << _sse2 << endl;
#endif
	}
	detected_cores = _cores;
//	cerr << "detected cores: " << _cores << endl;
	apply_config();
	connect(Config::instance(), SIGNAL(changed(void)), this, SLOT(slot_config_changed(void)));
//	cerr << "cores to be used: " << _cores << endl;
//	_ldb = lf_db_new();
	_ldb = lfDatabase::Create();
#ifdef Q_OS_WIN
// deployment
	QString path = QCoreApplication::applicationDirPath();
	path += QDir::separator();
	path += "lensfun/db/";
	QDir db_dir(path);
	QStringList filters;
	filters << "*.xml" << "*.XML";
	QStringList file_list = db_dir.entryList(filters, QDir::Files);
	for(int i = 0; i < file_list.size(); ++i)
		_ldb->Load((QDir::toNativeSeparators(path + file_list[i])).toLocal8Bit().constData());
#else
	_ldb->Load();
#endif
}

System::~System(void) {
	_ldb->Destroy();
}

struct lfDatabase *System::ldb(void) {
	return _ldb;
}

void System::slot_config_changed(void) {
	apply_config();
}

void System::apply_config(void) {
	// check config (RO section)
	_cores = detected_cores;
	bool c_cores_force = false;
	int c_cores = 0;
	Config::instance()->get(CONFIG_SECTION_SYSTEM, "cores_force", c_cores_force);
	Config::instance()->get(CONFIG_SECTION_SYSTEM, "cores", c_cores);
	if(c_cores_force && c_cores > 0)
		_cores = c_cores;
	// SSE2
#ifdef Q_CC_GNU
	_sse2 = detected_sse2;
	bool c_sse2 = false;
	if(Config::instance()->get(CONFIG_SECTION_SYSTEM, "sse2", c_sse2)) {
		if(!c_sse2)
			_sse2 = false;
	}
#endif
	// debug section
}

string System::env_home(void) {
	string prefix = "";
#ifdef Q_OS_MAC
	QDir volumes("/Volumes");
	string next = "";
	QStringList list = volumes.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	QStringListIterator it(list);
	while(it.hasNext()) {
		next = "/Volumes/";
		next += it.next().toLocal8Bit().constData();
		QDir dir(next.c_str());
		dir = dir.canonicalPath();
		if(dir.isRoot())
			break;
		next = "";
	}
	prefix = next;
#endif
	string home = prefix;
	home += QDir::homePath().toLocal8Bit().constData();
	return home;
}

//------------------------------------------------------------------------------
