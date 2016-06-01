#ifndef __H_PHOTO_STORAGE__
#define __H_PHOTO_STORAGE__
/*
 * photo_storage.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <map>
#include <mutex>
#include <condition_variable>
#include <ostream>
#include <set>
#include <string>

#include <QImage>

#include "dataset.h"
#include "photo.h"

//------------------------------------------------------------------------------
class PhotoStorage {
public:
	static bool ps_stored(std::string fs_folder, std::string fs_filename);
};

//------------------------------------------------------------------------------
// Photo Settings (PS) load/save
class PS_Loader {
public:
	PS_Loader(Photo_ID photo_id = Photo_ID());
	~PS_Loader();
	void save(Photo_ID photo_id);

	class DataSet *get_dataset(const std::string &);
	void set_dataset(const std::string &, const class DataSet &);
	bool is_empty(void);

	std::string serialize(void);

	void set_thumbnail(Area *thumb);
	QImage get_thumbnail(void);

	void set_cw_rotation(int cw_rotation);
	int get_cw_rotation(void);
	bool cw_rotation_empty(void);

	static std::list<int> versions_list(std::string file_name);

	static void version_create(Photo_ID photo_id, class PS_Loader *ps_loader = nullptr);
	static void version_remove(Photo_ID photo_id);

protected:
	std::map<std::string, class DataSet> dataset_map;
	bool _is_empty;
	void _serialize(std::ostream *ostr);
	QImage thumbnail;
	int cw_rotation;
	bool _cw_rotation_empty;

	void load(Photo_ID photo_id, bool use_lock);
	void save(QXmlStreamWriter &xml, int v_index);

	static void lock(std::string file_name, std::unique_lock<std::mutex> &mutex_locker);
	static void unlock(std::string file_name, std::unique_lock<std::mutex> &mutex_locker);
	static std::mutex ps_lock;
	static std::condition_variable ps_lock_wait;
	static std::set<std::string> ps_lock_set;
	static void version_rearrange(Photo_ID photo_id, bool remove_not_create, class PS_Loader *ps_loader = nullptr);

	static std::map<int, PS_Loader *> versions_load(std::string file_name, int index_to_skip);
	static void versions_save(std::string file_name, std::map<int, PS_Loader *> &ps_map);

};
//------------------------------------------------------------------------------

#endif // __H_PHOTO_STORAGE__
