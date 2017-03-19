#ifndef __H_PHOTO__
#define __H_PHOTO__
/*
 * photo.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "metadata.h"
#include "memory.h"
#include "misc.h"

#define CHAR_PHOTO_VERSION_SEPARATOR	':'

//------------------------------------------------------------------------------
class Photo_ID {
public:
	Photo_ID();
	Photo_ID(std::string file_name, int version);
	std::string get_file_name(void);
	int get_version_index(void);
	std::string get_export_file_name(void);
	bool operator == (const Photo_ID &other) const;
	bool operator != (const Photo_ID &other) const;
	bool operator < (const Photo_ID &other) const;
	bool is_empty(void);

protected:
	std::string _file_name;
	int _version;
};

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
	Photo_ID photo_id;	// full file name, ':', version id; like "IMG_1234.CR2:1" etc
	std::string ps_state;	// at the 'open' moment
	std::mutex ids_lock;

	static QString photo_name_with_versions(Photo_ID photo_id, int versions_count);

	// TODO: add lock for concurrent access
	//----
	// TODO: remove that from here and put it in a new class related to processing transaction, not to whole Photo opened for edit;
	// filter-independent PS_Base storage, to avoid asynchronous delay between PS_Base change by filter and 'signal_update' processing
	// that map is related to the processing loop and edit history and undo/redo
	std::map<class Filter *, std::shared_ptr<class PS_Base> > map_ps_base_current;
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
	std::unique_ptr<class Area> area_raw;
	std::unique_ptr<class Area> thumbnail;

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
