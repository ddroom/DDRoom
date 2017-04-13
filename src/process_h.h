#ifndef __H_PROCESS_H__
#define __H_PROCESS_H__
/*
 * process.h - renamed from "process.h" to "process_h.h" for compatibility with MinGW.
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <QtCore>

#include "area.h"
#include "filter.h"
#include "memory.h"
#include "mt.h"
#include "photo.h"

//------------------------------------------------------------------------------
class OOM_desc_t {
public:
	class Photo_ID photo_id;
	bool at_export;
	bool at_open_stage;
};

class Filter_process_desc_t {
public:
	Filter_process_desc_t(Filter *_filter) : filter(_filter) {}
	class Filter *filter = nullptr;
	bool use_tiling = true;
	// right now not used if 'use_tiling == true'
	bool cache_result = false;
};

class Process_task_t {
public:
	// arguments
	std::shared_ptr<Photo_t> photo;
	int request_ID = 0;

	bool update;
	Area::format_t out_format;
	bool is_offline;
	Flow::priority_t priority = Flow::priority_lowest;
	TilesReceiver *tiles_receiver = nullptr;

	// Set of (all) filters settings for processing
	std::map<class Filter *, std::shared_ptr<PS_Base>> map_ps_base;
	std::vector<class Filter_process_desc_t> filters_desc;

	// results
	bool failed = false;
	bool failed_oom = false;
	bool failed_at_import = false;
	int result_cw_rotation = 0;
	int result_update;
};

class Process : public QObject {
	Q_OBJECT

public:
	Process(void);
	virtual ~Process();

	static bool process(Process_task_t *process_task);
	// should be reentrant
	// ptr - pointer for signal 'signal_process_complete'
	// Photo_t - photo to be processed
	// TilesReceiver * - receiver of resulting thumbnail/tiles
	// map<...> - processing settings for filters
	// Return 'false' if failed - like out-of-memory etc...
	bool process_edit(void *ptr, std::shared_ptr<class Photo_t>, int request_ID, class TilesReceiver *, class std::map<class Filter *, std::shared_ptr<PS_Base> >);
	bool process_export(Photo_ID photo_id, std::string fname_export, class export_parameters_t *ep);

	static void quit(void);

	// ID of request to process; value '0' is reserved as undefined request ID.
	static int newID(void);
	static void ID_request_abort(int ID);

signals:
	void signal_process_complete(void *, class PhotoProcessed_t *);
	void signal_OOM_notification(void *); // pointer to OOM_desc_t object

protected:
	static std::atomic_int to_quit;

	static int ID_counter;
	static std::mutex ID_counter_lock;
	static std::set<int> IDs_in_process;
	static void ID_add(int ID);
	static void ID_remove(int ID);
	static bool ID_to_abort(int ID);

	// thread properties
	class task_run_t;
	static void subflow_run_mt(void *obj, SubFlow *subflow, void *data);
	static void run_mt(SubFlow *subflow, void *data);

	static void process_size_forward(Area::t_dimensions &d_out, Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, Area::t_dimensions *d_in_ptr);
	static void process_size_backward(Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, const Area::t_dimensions &);
	static void process_filters(SubFlow *subflow, Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, bool is_thumb, class Profiler *prof);

	static void wrap_filters(const std::vector<class filter_record_t> &filters, class task_run_t *task);
	static void allocate_process_caches(const std::vector<class filter_record_t> &filters, std::shared_ptr<class Photo_t> photo_ptr);
	static class Area *select_cached_area_and_filters_to_process(std::vector<filter_record_t> &filter_records, class task_run_t *task, const int pass, const bool is_main);

protected:
	class Filter_Store *fstore;
};

//------------------------------------------------------------------------------

#endif // __H_PROCESS_H__
