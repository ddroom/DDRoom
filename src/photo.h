#ifndef __H_PHOTO__
#define __H_PHOTO__
/*
 * photo.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QSharedPointer>
#include <string>
#include <map>

#include "metadata.h"
#include "memory.h"
#include "misc.h"

#define CHAR_PHOTO_VERSION_SEPARATOR	':'

//------------------------------------------------------------------------------
// interface for caches that are stored in photo
class PhotoCache_t {
public:
	PhotoCache_t(void) {};
	virtual ~PhotoCache_t() {};
};

class Photo_t {
public:
	Photo_t(void);
	~Photo_t(void);

	QString name;
	std::string photo_id;	// full file name, ':', version id; like "IMG_1234.CR2:1" etc
	std::string ps_state;	// at the 'open' moment
	QMutex ids_lock;

	static std::string file_name_from_photo_id(std::string photo_id);
	static int version_index_from_photo_id(std::string photo_id);
	static std::string get_photo_id(std::string file_name, int index);
	static QString photo_name_with_versions(QString photo_name, int version_index, int versions_count);

	// TODO: add lock for concurrent access
	//----
	// TODO: remove that from here and put it in a new class related to processing transaction, not to whole Photo opened for edit;
	// filter-independent PS_Base storage, to avoid asynchronous delay between PS_Base change by filter and 'signal_update' processing
	// that map is related to the processing loop and edit history and undo/redo
//	std::map<class Filter *, ddr_shared_ptr<class PS_Base> > map_ps_base_current;
	std::map<class Filter *, QSharedPointer<class PS_Base> > map_ps_base_current;
	// real PS_Base objects used by filters for interface
	std::map<class Filter *, class PS_Base *> map_ps_base;
	// filters GUI cache
	std::map<class Filter *, class FS_Base *> map_fs_base;

	std::map<class Filter *, class DataSet> map_dataset_initial;
	std::map<class Filter *, class DataSet> map_dataset_current;
	void *edit_history;	// should be used only by Edit class

//	Process::process process_source;	// TODO
	ProcessSource::process process_source;
	long filter_flags; // Filter::flags() return if any, else == 0
	int cw_rotation;
	class Metadata *metadata;
	class Area *area_raw;		// should be keeped in here (?)

	class Area *thumbnail;

	// cache for filters will be stored at 'cache_process' by 'Process' class
	// once created cache object used by filters at all process iterations
	// usage example: f_curve - stored curve function table, synchronized with curve points
	class PhotoCache_t *cache_process;
};

//------------------------------------------------------------------------------
// photo processing result for View/Export class
class PhotoProcessed_t {
public:
	PhotoProcessed_t(void) {is_empty = true;};
	bool is_empty;
	int rotation;
	bool update;
};

//------------------------------------------------------------------------------

#endif // __H_PHOTO__
