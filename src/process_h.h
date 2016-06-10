#ifndef __H_PROCESS_H__
#define __H_PROCESS_H__
/*
 * process.h - renamed from "process.h" to "process_h.h" for compatibility with MinGW.
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
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

class Process : public QObject {
	Q_OBJECT

public:
	Process(void);
	virtual ~Process();

	// should be reentrant
	// ptr - pointer for signal 'signal_process_complete'
	// Photo_t - photo to be processed
	// is_inactive - flag, "some filters should know how process is run - like for histogram update etc..."
	// TilesReceiver * - receiver of resulting thumbnail/tiles
	// map<...> - processing settings for filters
	void process_online(void *ptr, std::shared_ptr<class Photo_t>, int request_ID, bool is_inactive, class TilesReceiver *, class std::map<class Filter *, std::shared_ptr<PS_Base> >);
	void process_export(Photo_ID photo_id, std::string fname_export, class export_parameters_t *ep);

	static void quit(void);

	// ID of request to process; value '0' is reserved as undefined request ID.
	static int newID(void);
	static void ID_request_abort(int ID);

signals:
	void signal_process_complete(void *, class PhotoProcessed_t *);
	void signal_OOM_notification(void *); // pointer to OOM_desc_t object

protected:
	static bool to_quit;
	static std::mutex quit_lock;
	//
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

	// NOTE: functions below should be static because those are called from (master) thread
	static void process_demosaic(SubFlow *subflow, void *data);
	static void process_size_forward(Process::task_run_t *task, std::list<class filter_record_t> &pl_filters, class Area::t_dimensions &d_out);
	static void process_size_backward(Process::task_run_t *task, std::list<class filter_record_t> &pl_filters, const Area::t_dimensions &);
	static void process_filters(SubFlow *subflow, Process::task_run_t *task, std::list<class filter_record_t> &pl_filters, bool is_thumb, class Profiler *prof);

	void assign_filters(std::list<class filter_record_t> &filters, class task_run_t *task);
	void allocate_process_caches(std::list<class filter_record_t> &filters, std::shared_ptr<class Photo_t> photo_ptr);

protected:
	static class Filter_Store *fstore;
};

//------------------------------------------------------------------------------

#endif // __H_PROCESS_H__
