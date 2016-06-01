/*
 * photo.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
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
	metadata = nullptr;
	area_raw = nullptr;
	thumbnail = nullptr;
	cache_process = nullptr;
	EditHistory::photo_constructor(this);
//cerr << "_____________________________________________________________________________________________________________________________ Photo::Photo() - constructor for " << (unsigned long)this << endl;
}

Photo_t::~Photo_t(void) {
cerr << "~Photo()" << endl;
	if(metadata != nullptr)	delete metadata;
	if(area_raw != nullptr) {
cerr << "delete area_raw" << endl;
		delete area_raw;
	}
	for(map<class Filter *, class PS_Base *>::iterator it = map_ps_base.begin(); it != map_ps_base.end(); ++it) {
		if((*it).second != nullptr)
			delete (*it).second;
	}
	for(map<class Filter *, class FS_Base *>::iterator it = map_fs_base.begin(); it != map_fs_base.end(); ++it) {
		if((*it).second != nullptr)
			delete (*it).second;
	}
	if(thumbnail != nullptr) delete thumbnail;
	// delete process cache
	if(cache_process != nullptr) {
cerr << "delete cache_process" << endl;
		delete cache_process;
	}
	EditHistory::photo_destructor(this);
//cerr << "_____________________________________________________________________________________________________________________________ Photo::~Photo() - destructor for " << (unsigned long)this << endl;
}

QString Photo_t::photo_name_with_versions(Photo_ID _photo_id, int v_count) {
	QString _name = QString::fromLocal8Bit(_photo_id.get_file_name().c_str());
	if(v_count <= 1)
		return _name;
	int v_index = _photo_id.get_version_index();
	if(v_index <= 0 || v_index > v_count)
		return _name;
	QFileInfo fi(_name);
	QString rez = QString(" (%1/%2)").arg(v_index).arg(v_count);
	return fi.fileName() + rez;
}

//------------------------------------------------------------------------------
Photo_ID::Photo_ID(void) {
	_version = 0;
	_file_name = "";
}

Photo_ID::Photo_ID(std::string file_name, int version) {
	_version = version;
	_file_name = file_name;
}

std::string Photo_ID::get_file_name(void) {
	return _file_name;
}

int Photo_ID::get_version_index(void) {
	return _version;
}

std::string Photo_ID::get_export_file_name(void) {
	if(_version == 0)
		return _file_name;
	QString fn = QString::fromLocal8Bit(_file_name.c_str());
	QString t = QString("-ver%1").arg(_version);
	return (fn + t).toLocal8Bit().constData();
}

bool Photo_ID::operator == (const Photo_ID &other) const {
	return (_file_name == other._file_name) && (_version == other._version);
}

bool Photo_ID::operator != (const Photo_ID &other) const {
	return (_file_name != other._file_name) || (_version != other._version);
}

bool Photo_ID::operator < (const Photo_ID &other) const {
	if(_file_name != other._file_name)
		return (_file_name < other._file_name);
	return (_version < other._version);
}

bool Photo_ID::is_empty(void) {
	return (_file_name == "");
}

//------------------------------------------------------------------------------
