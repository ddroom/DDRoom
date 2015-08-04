#ifndef __H_THUMBNAIL_LOADER__
#define __H_THUMBNAIL_LOADER__
/*
 * thumbnail_loader.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>
#include <string>

#include <QtWidgets>

//------------------------------------------------------------------------------
struct thumbnail_record_t {
public:
	std::string folder_id;
	int index;
	void *data;
	thumbnail_record_t(void) {
		folder_id = "";
		index = -1;
		data = NULL;
	}
};

//------------------------------------------------------------------------------
// load thumbnails from files list and return they
class ThumbnailThread : public QThread {
	Q_OBJECT

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
};

//------------------------------------------------------------------------------
// thumbnails loading thread - create and control threads that should load thumbnails;
// process result, and acquire "abort" signal on folder change
class ThumbnailLoader : public QThread {
	Q_OBJECT

public:
	ThumbnailLoader(void);
	~ThumbnailLoader();
	void _start(std::string _folder, std::list<thumbnail_record_t> *_wlist, class PhotoList *_photo_list);
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
	QMutex mutex_loader;	// global for load process, used to break loading on folder change
	QWaitCondition is_run;
	QWaitCondition wait_condition;	// result is ready or abort
	volatile bool is_run_flag;
	volatile bool _stop;
	std::string tr_folder;
	void run(void);

	std::list<thumbnail_record_t> *list_view;	// list of targets that are visible just right now
	std::list<thumbnail_record_t> *list_whole;	// whole targets in the set list
	QMutex mutex_target;
	class PhotoList *photo_list;

	QSize _thumb_size;
};

//------------------------------------------------------------------------------
#endif //__H_THUMBNAIL_LOADER__
