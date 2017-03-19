#ifndef __H_THUMBNAIL_LOADER__
#define __H_THUMBNAIL_LOADER__
/*
 * thumbnail_loader.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <string>

#include <QtWidgets>

//------------------------------------------------------------------------------
struct thumbnail_record_t {
public:
	std::string folder_id = std::string("");
	int index = -1;
	void *data = nullptr;
};

//------------------------------------------------------------------------------
// load and return thumbnails from files list
class ThumbnailThread {
public:
	ThumbnailThread(QSize thumb_size);
	~ThumbnailThread();
	void _start(std::string _folder, void *_thumbnail_loader);
	void stop(void);

	static class PhotoList_Item_t *load(thumbnail_record_t &target, const std::string &folder, QSize thumb_size);

protected:
	std::string tr_folder;
	void run(void);
	QSize _thumb_size;
	void *thumbnail_loader;
	std::thread *std_thread = nullptr;
};

//------------------------------------------------------------------------------
// thumbnails loading thread - create and control threads that should load thumbnails;
// process result, and acquire "abort" signal on folder change
class ThumbnailLoader : public QObject {
	Q_OBJECT

public:
	ThumbnailLoader(void);
	~ThumbnailLoader();
	void _start(std::string _folder, std::list<thumbnail_record_t> *_wlist, class PhotoList *_photo_list);
	void wait(void);
	void stop(void);

	void list_whole_set(std::list<thumbnail_record_t> *_list_whole);
	void list_whole_reset(void);

	bool target_next(thumbnail_record_t &target);
	void target_done(class PhotoList_Item_t *, const thumbnail_record_t &result);

	void set_thumbnail_size(QSize thumbnail_size);

	// configuration
	bool conf_load_in_background;

signals:
	void render_thumb(void *data, int index, QString id);

protected:
	std::thread *std_thread = nullptr;
	std::mutex mutex_loader;	// global for load process, used to break loading on folder change
	std::condition_variable cv_is_run;
	volatile bool is_run_flag;
	volatile bool _stop;
	std::string tr_folder;
	void run(void);

	std::list<thumbnail_record_t> *list_view;	// list of targets that are visible just right now
	std::list<thumbnail_record_t> *list_whole;	// whole targets in the set list
	std::mutex mutex_target;
	class PhotoList *photo_list;

	QSize _thumb_size;
};

//------------------------------------------------------------------------------
#endif //__H_THUMBNAIL_LOADER__
