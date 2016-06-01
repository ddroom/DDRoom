/*
 * photo_storage.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "photo.h"
#include "photo_storage.h"

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <map>
#include <iostream>
#include <sstream>

using namespace std;

//#define PS_SETTINGS_EXT	".frps"
#define PS_SETTINGS_EXT	".ddr"

//#define THUMB_JPEG_QUALITY 95
#define THUMB_JPEG_QUALITY 85

using namespace std;
//------------------------------------------------------------------------------
bool PhotoStorage::ps_stored(string fs_folder, string fs_filename) {
	string separator = QDir::toNativeSeparators("/").toLocal8Bit().constData();
	string fn = fs_folder;
	fn += separator;
	fn += fs_filename;
	fn += PS_SETTINGS_EXT;
	QString fn_s = QString::fromLocal8Bit(fn.c_str());
	return QFile::exists(fn_s);
}

//------------------------------------------------------------------------------
bool PS_Loader::is_empty(void) {
	return _is_empty;
}

std::list<int> PS_Loader::versions_list(std::string file_name) {
	file_name += PS_SETTINGS_EXT;
	std::list<int> v_list;

	QFile qifile(file_name.c_str());
	if(qifile.open(QFile::ReadOnly | QFile::Text)) {
		QXmlStreamReader xml(&qifile);
		//
		try {
			bool is_ddr = false;
			do {
				xml.readNext();
				if(xml.hasError()) throw("error");
				if(xml.isStartElement() && xml.name() == "ddr")
					is_ddr = true;
//					cerr << "-> ddr" << endl;
				if(xml.isStartElement() && xml.name() == "version" && is_ddr) {
					int version_index = 0;
					const QXmlStreamAttributes &v_attributes = xml.attributes();
					for(int i = 0; i < v_attributes.size(); i++) {
						string key = v_attributes[i].name().toString().toLocal8Bit().constData();
						QString value = v_attributes[i].value().toString();
						if(key == "index")
							version_index = value.toInt();
					}
//					cerr << "-> version with index \"" << version_index << "\"" << endl;
					if(version_index != 0)
						v_list.push_back(version_index);
				}
			} while(!xml.atEnd());
		} catch(const char *error) {
			cerr << "error of parsing " << file_name << " file" << endl;
		}
		//
		qifile.close();
	}
//	if(v_list.size() == 0)
//		v_list.push_back(1);
	return v_list;
}

PS_Loader::PS_Loader(Photo_ID photo_id) {
	cw_rotation = 0;
	_cw_rotation_empty = true;
	if(!photo_id.is_empty())
		load(photo_id, true);
}

void PS_Loader::load(Photo_ID photo_id, bool use_lock) {
	string file_name = photo_id.get_file_name();
	string lock_name = file_name;
	std::unique_lock<std::mutex> locker(ps_lock, std::defer_lock);
	if(use_lock)
		lock(lock_name, locker);
	//--
	_is_empty = false;
	int v_index = photo_id.get_version_index();
	file_name += PS_SETTINGS_EXT;
	QFile qifile(file_name.c_str());
	if(qifile.open(QFile::ReadOnly | QFile::Text)) {
		QXmlStreamReader xml(&qifile);
		//
		try {
			bool is_ddr = false;
			do {
				xml.readNext();
				if(xml.hasError()) throw("error");
				if(xml.isStartElement() && xml.name() == "ddr")
					is_ddr = true;
//					cerr << "-> ddr" << endl;
				if(xml.isStartElement() && xml.name() == "version" && is_ddr) {
					int version_index = 0;
					const QXmlStreamAttributes &v_attributes = xml.attributes();
					for(int i = 0; i < v_attributes.size(); i++) {
						string key = v_attributes[i].name().toString().toLocal8Bit().constData();
						string value = v_attributes[i].value().toString().toLocal8Bit().constData();
						if(key == "index")
							version_index = v_attributes[i].value().toString().toInt();
					}
//					cerr << "-> version with index \"" << version_index << "\"" << endl;

//cerr << __LINE__ << ": " << qPrintable(xml.name().toString()) << ": " << xml.tokenType()<< endl;
					bool is_thumbnail = false;
					if(version_index != v_index) {
//						cerr << "skip version" << endl;
					} else {
//						cerr << "load version" << endl;
						while(!(xml.isEndElement() && xml.name() == "version")) {
							xml.readNext();
							if(xml.hasError()) throw("error");
							if(xml.isStartElement() && xml.name() == "global_fields") {
								while(!(xml.isEndElement() && xml.name() == "global_fields")) {
									xml.readNext();
									if(xml.hasError()) throw("error");
									if(xml.isStartElement()) {
										string field_name = xml.name().toString().toLocal8Bit().constData();
										if(field_name == "cw_rotation") {
											const QXmlStreamAttributes &attributes = xml.attributes();
											for(int i = 0; i < attributes.size(); i++) {
												string key = attributes[i].name().toString().toLocal8Bit().constData();
												string value = attributes[i].value().toString().toLocal8Bit().constData();
												if(key == "angle" && (value == "0" || value == "90" || value == "180" || value == "270")) {
													_cw_rotation_empty = false;
													cw_rotation = QString(value.c_str()).toInt();
												}
											}
										}
									}
								}
							}
							if(xml.isStartElement() && xml.name() == "filters") {
//								cerr << "--> filters" << endl;
								while(!(xml.isEndElement() && xml.name() == "filters")) {
									xml.readNext();
									if(xml.hasError()) throw("error");
									if(xml.isStartElement()) {
										string filter_name = xml.name().toString().toLocal8Bit().constData();
//										cerr << "---> " << filter_name.c_str() << endl;
										const QXmlStreamAttributes &attributes = xml.attributes();
										for(int i = 0; i < attributes.size(); i++) {
											string key = attributes[i].name().toString().toLocal8Bit().constData();
											string value = attributes[i].value().toString().toLocal8Bit().constData();
//											cerr << "----> \"" << key.c_str() << "\" == \"" << value.c_str() << "\"" << endl;
											dataset_field_t t;
											t.type = dataset_field_t::type_serialized;
											t.vString = value;
											map<string, DataSet>::iterator it = dataset_map.find(filter_name);
											if(it == dataset_map.end()) {
												dataset_map[filter_name] = DataSet();
												it = dataset_map.find(filter_name);
											}
											(*((*it).second.get_dataset_fields()))[key] = t;
										}
									}
								}
							}
							if(xml.name() == "thumbnail") {
								if(xml.isStartElement()) is_thumbnail = true;
								if(xml.isEndElement()) is_thumbnail = false;
							}
							if(is_thumbnail && xml.isCDATA()) {
//								cerr << "--> thumbnail, CDATA" << endl;
//								cerr << "isCDATA == " << xml.isCDATA() << endl;
//								thumbnail = QImage::fromData(QByteArray::fromBase64(xml.text().toString().toLatin1()));
								thumbnail = QImage();
								thumbnail.loadFromData(QByteArray::fromBase64(xml.text().toString().toLatin1()));
//cerr << "thumbnail size == " << thumbnail.width() << "x" << thumbnail.height() << endl;
							}
//cerr << __LINE__ << ": --" << qPrintable(xml.name().toString()) << endl;
						}
					}
				}
			} while(!xml.atEnd());
		} catch(const char *error) {
			_is_empty = true;
			cerr << "error of parsing " << file_name << " file" << endl;
		}
		//
		qifile.close();
	}
	//--
	if(use_lock)
		unlock(lock_name, locker);
}

PS_Loader::~PS_Loader() {
}

//------------------------------------------------------------------------------
std::mutex PS_Loader::ps_lock;
std::condition_variable PS_Loader::ps_lock_wait;
std::set<std::string> PS_Loader::ps_lock_set;

void PS_Loader::lock(std::string file_name, std::unique_lock<std::mutex> &locker) {
	locker.lock();
//cerr << "  lock: " << photo_id.get_export_file_name() << endl;
	while(ps_lock_set.find(file_name) != ps_lock_set.end())
		ps_lock_wait.wait(locker);
	ps_lock_set.insert(file_name);
}

void PS_Loader::unlock(std::string file_name, std::unique_lock<std::mutex> &locker) {
	ps_lock_set.erase(file_name);
//cerr << "unlock: " << photo_id.get_export_file_name << endl;
//for(set<string>::iterator it = ps_lock_set.begin(); it != ps_lock_set.end(); ++it)
//cerr << "... " << *it << endl;
	locker.unlock();
	ps_lock_wait.notify_one();
}

void PS_Loader::version_create(Photo_ID photo_id, PS_Loader *ps_loader) {
	// if photo is open in edit use current settings for a new version instead of saved ones
	version_rearrange(photo_id, false, ps_loader);
}

void PS_Loader::version_remove(Photo_ID photo_id) {
	version_rearrange(photo_id, true);
}

void PS_Loader::version_rearrange(Photo_ID photo_id, bool remove_not_create, PS_Loader *ps_loader) {
	string file_name = photo_id.get_file_name();
	std::unique_lock<std::mutex> locker(ps_lock, std::defer_lock);
	lock(file_name, locker);
	int v_index = photo_id.get_version_index();
	if(v_index < 1) v_index = 1;
	// load all versions
	map<int, PS_Loader *> _ps_map = versions_load(file_name, -1);
	if(_ps_map.size() == 0)
		_ps_map.insert(pair<int, PS_Loader *>(1, new PS_Loader()));
	// copy versions and add 
	map<int, PS_Loader *> ps_map;
	PS_Loader *ps_base = _ps_map[v_index];
	for(int i = 1; i <= _ps_map.size(); i++) {
		if(remove_not_create) {
			if(i < v_index)
				ps_map.insert(std::pair<int, PS_Loader *>(i, _ps_map[i]));
			if(i > v_index)
				ps_map.insert(std::pair<int, PS_Loader *>(i - 1, _ps_map[i]));
		} else {
			if(i <= v_index)
				ps_map.insert(std::pair<int, PS_Loader *>(i, _ps_map[i]));
			if(i >= v_index) {
				if(i == v_index && ps_loader != nullptr)
					ps_map.insert(std::pair<int, PS_Loader *>(i + 1, ps_loader));
				else
					ps_map.insert(std::pair<int, PS_Loader *>(i + 1, _ps_map[i]));
			}
		}
	}
	// save all versions
	versions_save(file_name, ps_map);
	// delete temporary settings
	for(map<int, PS_Loader *>::iterator it = ps_map.begin(); it != ps_map.end(); ++it) {
		if((*it).second != ps_base)
			delete (*it).second;
	}
	delete ps_base;
	unlock(file_name, locker);
}

void PS_Loader::save(Photo_ID photo_id) {
	string file_name = photo_id.get_file_name();
	std::unique_lock<std::mutex> locker(ps_lock, std::defer_lock);
	lock(file_name, locker);
	int v_index = photo_id.get_version_index();
	if(v_index < 1) v_index = 1;
	// load all versions, if any, but this version
	map<int, PS_Loader *> ps_map = versions_load(file_name, v_index);
	// store current version
	ps_map[v_index] = this;
	// save all versions
	versions_save(file_name, ps_map);
	// delete temporary settings
	for(map<int, PS_Loader *>::iterator it = ps_map.begin(); it != ps_map.end(); ++it) {
		if((*it).second != this)
			delete (*it).second;
	}
	unlock(file_name, locker);
}

map<int, PS_Loader *> PS_Loader::versions_load(string file_name, int index_to_skip) {
	list<int> v_list = versions_list(file_name);
	map<int, PS_Loader *> ps_map;
	for(list<int>::iterator it = v_list.begin(); it != v_list.end(); ++it) {
		if(*it == index_to_skip)
			continue;
//		QString v_index(QString("%1").arg(*it));
//		QString id = QString::fromLocal8Bit(file_name.c_str());
//		id = id + ":" + v_index;
		PS_Loader *ps_loader = new PS_Loader();
		ps_loader->load(Photo_ID(file_name, *it), false);
//		ps_loader->load(id.toLocal8Bit().constData(), false);
		ps_map[*it] = ps_loader;
	}
	return ps_map;
}

void PS_Loader::versions_save(string file_name, map<int, PS_Loader *> &ps_map) {
	// create a new settings file
	file_name += PS_SETTINGS_EXT;
	QFile file(file_name.c_str());
	if(!file.open(QFile::WriteOnly | QFile::Text))
		return;
	QXmlStreamWriter xml(&file);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeStartElement("ddr");
	// save all versions
	for(map<int, PS_Loader *>::iterator it = ps_map.begin(); it != ps_map.end(); ++it)
		(*it).second->save(xml, (*it).first);
	// close new settings file 
	xml.writeEndElement();	// ddr
	xml.writeEndDocument();
	file.close();
}

//------------------------------------------------------------------------------
void PS_Loader::save(QXmlStreamWriter &xml, int v_index) {
	xml.writeStartElement("version");
	xml.writeAttribute("index", QString("%1").arg(v_index));
	//--
	if(!_cw_rotation_empty) {
		xml.writeStartElement("global_fields");
		xml.writeEmptyElement("cw_rotation");
		xml.writeAttribute("angle", QString("%1").arg(cw_rotation));
		xml.writeEndElement();	// global_fields
	}
	//--
	xml.writeStartElement("filters");
	for(map<string, DataSet>::iterator it_f = dataset_map.begin(); it_f != dataset_map.end(); ++it_f) {
		xml.writeEmptyElement((*it_f).first.c_str());
//		*ostr << "[" << (*it_f).first << "]" << endl;
		const map<string, dataset_field_t> *d = (*it_f).second.get_dataset_fields();
		for(map<string, dataset_field_t>::const_iterator it = d->begin(); it != d->end(); ++it)
			xml.writeAttribute((*it).first.c_str(), (*it).second.serialize().c_str());
//			*ostr << (*it).first << "=" << (*it).second.serialize() << endl;
//		*ostr << endl;
	}
	xml.writeEndElement();	// filters
	//--
	xml.writeStartElement("thumbnail");
	xml.writeAttribute("type", "jpeg");
	if(!thumbnail.isNull()) {
		QByteArray array;
		QBuffer buffer(&array);
		buffer.open(QIODevice::WriteOnly);
		thumbnail.save(&buffer, "JPEG", THUMB_JPEG_QUALITY);
		xml.writeCDATA(array.toBase64().constData());
	}
	xml.writeEndElement();	// thumbnail
	xml.writeEndElement();	// version
}

void PS_Loader::_serialize(ostream *ostr) {
	// cw_rotation, if defined
	if(!_cw_rotation_empty)
		*ostr << "cw_rotation = " << cw_rotation << endl;
	// dataset
	for(map<string, DataSet>::iterator it_f = dataset_map.begin(); it_f != dataset_map.end(); ++it_f) {
		*ostr << "[" << (*it_f).first << "]" << endl;
		const map<string, dataset_field_t> *d = (*it_f).second.get_dataset_fields();
		for(map<string, dataset_field_t>::const_iterator it = d->begin(); it != d->end(); ++it)
			*ostr << (*it).first << "=" << (*it).second.serialize() << endl;
		*ostr << endl;
	}
}

string PS_Loader::serialize(void) {
	ostringstream oss;
	_serialize(&oss);
	oss.flush();
	return oss.str();
}

DataSet *PS_Loader::get_dataset(const string &name) {
	map<string, DataSet>::iterator it = dataset_map.find(name);
	if(it == dataset_map.end()) {
		dataset_map[name] = DataSet();
		it = dataset_map.find(name);
	}
	return &(*it).second;
}

void PS_Loader::set_dataset(const string &name, const DataSet &_dataset) {
	dataset_map[name] = _dataset;
}

void PS_Loader::set_thumbnail(Area *thumb) {
	if(!thumbnail.isNull())
		thumbnail = QImage();
	if(thumb == nullptr)
		return;
	if(thumb->type() == Area::type_t::type_uint8_p4) {
//cerr << "save thumb: " << thumb->mem_width() << "x" << thumb->mem_height() << endl;
		thumbnail = QImage((uchar *)thumb->ptr(), thumb->mem_width(), thumb->mem_height(), QImage::Format_RGB32).copy();
//		thumbnail = QImage((uchar *)thumb->ptr(), thumb->mem_width(), thumb->mem_height(), QImage::Format_RGB32).scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();
	}
}

QImage PS_Loader::get_thumbnail(void) {
	return thumbnail.copy();
}

void PS_Loader::set_cw_rotation(int rotation) {
	cw_rotation = rotation;
	_cw_rotation_empty = false;
}

int PS_Loader::get_cw_rotation(void) {
	return cw_rotation;
}

bool PS_Loader::cw_rotation_empty(void) {
	return _cw_rotation_empty;
}
//------------------------------------------------------------------------------
