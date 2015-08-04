#ifndef __H_PHOTO_STORAGE__
#define __H_PHOTO_STORAGE__
/*
 * photo_storage.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <map>
#include <ostream>
#include <set>
#include <string>

#include <QImage>

#include "dataset.h"

//------------------------------------------------------------------------------
class PhotoStorage {
public:
	static bool ps_stored(std::string fs_folder, std::string fs_filename);
};

//------------------------------------------------------------------------------
// Photo Settings (PS) load/save
class PS_Loader {
public:
	PS_Loader(std::string photo_id = "");
	~PS_Loader();
	void save(std::string photo_id);

	class DataSet *get_dataset(const std::string &);
	void set_dataset(const std::string &, const class DataSet &);
	bool is_empty(void);

	std::string serialize(void);

	void set_thumbnail(Area *thumb);
	QImage get_thumbnail(void);

	void set_cw_rotation(int cw_rotation);
	int get_cw_rotation(void);
	bool cw_rotation_empty(void);

	static std::list<int> versions_list(std::string photo_id);	// file name is acceptable too

	static void version_create(std::string photo_id, class PS_Loader *ps_loader = NULL);
	static void version_remove(std::string photo_id);

protected:
	std::map<std::string, class DataSet> dataset_map;
	bool _is_empty;
	void _serialize(std::ostream *ostr);
	QImage thumbnail;
	int cw_rotation;
	bool _cw_rotation_empty;

	void load(std::string photo_id, bool use_lock);
	void save(QXmlStreamWriter &xml, int v_index);

	static void lock(std::string photo_id);
	static void unlock(std::string photo_id);
	static QMutex ps_lock;
	static QWaitCondition ps_lock_wait;
	static std::set<std::string> ps_lock_set;
	static void version_rearrange(std::string photo_id, bool remove_not_create, class PS_Loader *ps_loader = NULL);

	static std::map<int, PS_Loader *> versions_load(std::string file_name, int index_to_skip);
	static void versions_save(std::string file_name, std::map<int, PS_Loader *> &ps_map);

};
//------------------------------------------------------------------------------

#endif // __H_PHOTO_STORAGE__
