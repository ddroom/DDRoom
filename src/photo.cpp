/*
 * photo.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "photo.h"
#include "area.h"
#include "filter.h"
#include "edit_history.h"
#include <QString>

#include <iostream>

using namespace std;

//------------------------------------------------------------------------------
Photo_t::Photo_t(void) {
	process_source = ProcessSource::s_none;
	metadata = NULL;
	area_raw = NULL;
	thumbnail = NULL;
	cache_process = NULL;
	EditHistory::photo_constructor(this);
//cerr << "_____________________________________________________________________________________________________________________________ Photo::Photo() - constructor for " << (unsigned long)this << endl;
}

Photo_t::~Photo_t(void) {
cerr << "~Photo()" << endl;
	if(metadata != NULL)	delete metadata;
	if(area_raw != NULL) {
cerr << "delete area_raw" << endl;
		delete area_raw;
	}
	for(map<class Filter *, class PS_Base *>::iterator it = map_ps_base.begin(); it != map_ps_base.end(); it++) {
		if((*it).second != NULL)
			delete (*it).second;
	}
	for(map<class Filter *, class FS_Base *>::iterator it = map_fs_base.begin(); it != map_fs_base.end(); it++) {
		if((*it).second != NULL)
			delete (*it).second;
	}
	if(thumbnail != NULL) delete thumbnail;
	// delete process cache
	if(cache_process != NULL) {
cerr << "delete cache_process" << endl;
		delete cache_process;
	}
	EditHistory::photo_destructor(this);
//cerr << "_____________________________________________________________________________________________________________________________ Photo::~Photo() - destructor for " << (unsigned long)this << endl;
}

string Photo_t::file_name_from_photo_id(string photo_id) {
	string file_name = photo_id;
	const char *ptr = photo_id.c_str();
	int i = photo_id.length();
	// search for separator ':', but be aware of possible combination like 'C:\...:...' on windows platforms
	for(; i > 0 && ptr[i] != CHAR_PHOTO_VERSION_SEPARATOR; i--);
	if(i > 0 && i != photo_id.length())
		if(ptr[i + 1] != '\\' && ptr[i + 1] != '/')
			file_name.erase(i, photo_id.length());
	return file_name;
}

int Photo_t::version_index_from_photo_id(string photo_id) {
	int index = 0;
	const char *ptr = photo_id.c_str();
	int i = photo_id.length();
	for(; i > 0 && ptr[i] != CHAR_PHOTO_VERSION_SEPARATOR; i--);
	if(i > 0) {
		QString str(&ptr[i + 1]);
		index = str.toInt();
	}
	if(index < 1)	index = 1;
	return index;
}

std::string Photo_t::get_photo_id(std::string file_name, int index) {
	QString v_index(QString("%1").arg(index));
	QString photo_id = QString::fromLocal8Bit(file_name.c_str());
	photo_id = photo_id + CHAR_PHOTO_VERSION_SEPARATOR + v_index;
	return photo_id.toLocal8Bit().constData();
}

QString Photo_t::photo_name_with_versions(QString _name, int v_index, int v_count) {
	if(v_count <= 1)
		return _name;
	if(v_index <= 0 || v_index > v_count)
		return _name;
	QFileInfo fi(_name);
	QString rez = QString(" (%1/%2)").arg(v_index).arg(v_count);
	return fi.fileName() + rez;
}

//------------------------------------------------------------------------------
