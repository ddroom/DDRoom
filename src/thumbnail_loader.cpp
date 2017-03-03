/*
 * thumbnail_loader.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <list>

#include "area.h"
#include "metadata.h"
#include "import_raw.h"
#include "photo_storage.h"
#include "system.h"
#include "thumbnail_loader.h"
#include "thumbnail_view.h"

using namespace std;

//------------------------------------------------------------------------------
ThumbnailLoader::ThumbnailLoader() {
	is_run_flag = false;
	_thumb_size = QSize(128, 128);

	// list_whole - complete list of the thumbnails
	// list_view - thumbnails that are visible now
	list_whole = nullptr;
	list_view = nullptr;

	// configuration
	// preload invisible thumbnails
	conf_load_in_background = true;
}

ThumbnailLoader::~ThumbnailLoader() {
	stop();
	if(list_whole != nullptr) {
		delete list_whole;
		list_whole = nullptr;
	}
	if(list_view != nullptr) {
		delete list_view;
		list_view = nullptr;
	}
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void ThumbnailLoader::set_thumbnail_size(QSize thumbnail_size) {
	_thumb_size = thumbnail_size;
}

void ThumbnailLoader::_start(string _folder, list<thumbnail_record_t> *vlist, PhotoList *_photo_list) {
//cerr << "ThumbnailLoader::_start(); vlist->size() == " << vlist->size() << endl;
	mutex_loader.lock();
	_stop = false;
	tr_folder = _folder;

	mutex_target.lock();
	if(list_view != nullptr)
		delete list_view;
	list_view = vlist;
	mutex_target.unlock();

	photo_list = _photo_list;
	is_run_flag = true;
	mutex_loader.unlock();
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
	auto obj = this;
//std::cerr << "..... 1" << std::endl;
//cerr << " obj == " << (unsigned long)obj << endl;
	std_thread = new std::thread( [obj](void){obj->run();} );
//std::cerr << "..... 2" << std::endl;
}

void ThumbnailLoader::wait(void) {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
		std_thread = nullptr;
	}
}

void ThumbnailLoader::stop(void) {
	mutex_target.lock();
	if(list_view != nullptr)
		delete list_view;
	list_view = nullptr;
	mutex_target.unlock();

	std::unique_lock<std::mutex> locker(mutex_loader, std::defer_lock);
	locker.lock();
	_stop = true;
	while(is_run_flag)
		cv_is_run.wait(locker);
	locker.unlock();
}

void ThumbnailLoader::list_whole_reset(void) {
	mutex_target.lock();
	if(list_whole)
		delete list_whole;
	list_whole = nullptr;
	mutex_target.unlock();
}

void ThumbnailLoader::list_whole_set(list<thumbnail_record_t> *_list_whole) {
	mutex_target.lock();
	if(list_whole)
		delete list_whole;
	list_whole = _list_whole;
	mutex_target.unlock();
}

void ThumbnailLoader::run(void) {
//cerr << "ThumbnailLoader::run()" << endl;
//cerr << "this == " << (unsigned long)this << endl;
	mutex_target.lock();
//cerr << "ThumbnailLoader::run() ... 1" << endl;
	if(list_whole == nullptr)
		list_whole = new list<thumbnail_record_t>;
	if(list_view == nullptr)
		list_view = new list<thumbnail_record_t>;
//cerr << "list_whole.size == " << list_whole->size() << endl;
//cerr << "list_view.size == " << list_view->size() << endl;
	if(list_whole->size() != 0 || list_view->size() != 0) {
		int count = list_whole->size() + list_view->size();
		mutex_target.unlock();
		string folder = tr_folder;
		int threads = System::instance()->cores();
		if(threads > 1) {
			if(threads > count)
				threads = count;
			list<ThumbnailThread *> thl;
//cerr << "... 01" << endl;
			for(int i = 0; i < threads; ++i)
				thl.push_back(new ThumbnailThread(_thumb_size));
//cerr << "... 02" << endl;
			for(list<ThumbnailThread *>::iterator it = thl.begin(); it != thl.end(); ++it)
				(*it)->_start(folder, (void *)this);
//cerr << "... 03" << endl;
//			for(list<ThumbnailThread *>::iterator it = thl.begin(); it != thl.end(); ++it)
//				(*it)->wait();
			for(list<ThumbnailThread *>::iterator it = thl.begin(); it != thl.end(); ++it)
				delete (*it);
//cerr << "... 04" << endl;
		} else {
			thumbnail_record_t target;
			while(target_next(target)) {
				target_done(ThumbnailThread::load(target, folder, _thumb_size), target);
				bool to_break = false;
				mutex_loader.lock();
				to_break = _stop;
				mutex_loader.unlock();
				if(to_break) {
					break;
				}
			}
		}
	} else
		mutex_target.unlock();
	mutex_loader.lock();
	is_run_flag = false;
	_stop = false;
	cv_is_run.notify_all();
	mutex_loader.unlock();

	mutex_target.lock();
//	delete list_whole;
//	list_whole = nullptr;
	delete list_view;
	list_view = nullptr;
	mutex_target.unlock();
}

bool ThumbnailLoader::target_next(thumbnail_record_t &target) {
	bool result = false;
	if(_stop)
		return false;
//usleep(500000);
	mutex_target.lock();
	bool to_skip = false;
	do {
		result = false;
		// thumbs that are shown in view
		if(list_view != nullptr) {
			if(list_view->size() != 0) {
//			if(list_view->begin() != list_view->end()) {
//				target = *(list_view->begin());
				target = list_view->front();
				list_view->pop_front();
				result = true;
			}
		}
		// all thumbs in list
		if(list_whole != nullptr && result == false && conf_load_in_background) {
			if(list_whole->size() != 0) {
//cerr << "list_whole->size() == " << list_whole->size() << endl;
				target = list_whole->front();
				list_whole->pop_front();
				result = true;
			}
		}
		if(!result)
			break;
		to_skip = photo_list->is_item_to_skip(&target);
//cerr << "item to skip == " << to_skip << "; result == " << result << "; file_name == " << ((PhotoList_Item_t *)target.data)->file_name << endl;
	} while(to_skip);
	if(result)
		photo_list->set_item_scheduled(&target);
	mutex_target.unlock();
	return result;
}

void ThumbnailLoader::target_done(PhotoList_Item_t *item, const thumbnail_record_t &target) {
	photo_list->update_item(item, target.index, target.folder_id);
//	emit render_thumb(result.data, result.index, result.folder_id);
}

//------------------------------------------------------------------------------
ThumbnailThread::ThumbnailThread(QSize thumb_size) : _thumb_size(thumb_size) {
//	_thumb_size = thumb_size;
	//
}

ThumbnailThread::~ThumbnailThread() {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void ThumbnailThread::_start(string _folder, void *_thumbnail_loader) {
	thumbnail_loader = _thumbnail_loader;
	tr_folder = _folder;
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
//std::cerr << "..... 3" << std::endl;
	auto obj = this;
	std_thread = new std::thread( [obj](void){obj->run();} );
//std::cerr << "..... 4" << std::endl;
//	this->wait();
//	this->start();
}

void ThumbnailThread::run(void) {
//	setPriority(QThread::HighPriority);
	string folder = tr_folder;
	thumbnail_record_t target;
	ThumbnailLoader *tl = (ThumbnailLoader *)thumbnail_loader;
	while(tl->target_next(target))
		tl->target_done(load(target, folder, _thumb_size), target);
}

PhotoList_Item_t *ThumbnailThread::load(thumbnail_record_t &target, const string &folder, QSize thumb_size) {
	PhotoList_Item_t *item = new PhotoList_Item_t(*(PhotoList_Item_t *)target.data);
	// load thumbnail to QImage, and send it to a main thread by signal...
	Metadata metadata;
//cerr << "thread: " << (unsigned long)QThread::currentThreadId() << "load thumb for: " << item->file_name.c_str() << endl;
	int rotation = 0;
	QImage *thumb_image = Import::thumb(item->photo_id, &metadata, rotation, thumb_size.width(), thumb_size.height());
	// check settings file
	item->flag_edit = PhotoStorage::ps_stored(folder, item->name.toLocal8Bit().constData());
		
	// tooltip with metadata
	item->tooltip = metadata.get_tooltip(item->name);

	QImage qi;
	if(thumb_image != nullptr) {
		int w = thumb_size.width();
		int h = thumb_size.height();
		if(rotation == 90 || rotation == 270) {
			w = thumb_size.height();
			h = thumb_size.width();
		}
		qi = thumb_image->scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		delete thumb_image;
		if(rotation != 0) {
			QTransform qtrans;
			qi = qi.transformed(qtrans.rotate(rotation));
		}
	}
	item->image = qi;
	return item;
//cerr << "load thumb for: " << item->file_name.c_str() << " ...3" << endl;
}

//------------------------------------------------------------------------------
