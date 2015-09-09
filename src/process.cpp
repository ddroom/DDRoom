/*
 * process.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTE:
	- generated mutators:
		"_p_online" -> bool;
		"_p_thumb" -> bool;
	- comments marked as 'NOTE WB:' related to F_WB caching; those code
		should be replaced with a generalized caching system, with a high priority to be cached;

 * TODO:
	- check that issue: process_online can be called either to process
		tiles that are visible now or tiles that would be visible later.
		So the problem is: should we cache somehow filter wrappers and
		other sensible objects/data in between those two calls (and clear/reset
		those on photo close or PS change), or reconstruct that each call as
		we do it now.
		Probably put that cache in some place like tiles_request_t that should be
		cleared in appropriate destructor.

	- do a real cache - remember the last source of reprocessing and for the next time
		if the source is the same, remember tiles before that filter in hope that in the next time
		this cache can be used.

NOTE on quit:
	-! use quit_lock to ensure access to valid pointer on tiles receiver;
	-! use quit_lock as much as needed to protect relativelly fast code blocks w/o useless frequent checks;
	-+ use quit flag in the same way as 'abort' to abort break processing;
	-+ use quit_lock from master thread, and signal abort to all threads in the same way as actual 'abort' event;

 */

#include <iostream>
#include <QSharedPointer>

#include "area_helper.h"
#include "export.h"
#include "filter.h"
#include "filter_cp.h"
#include "filter_gp.h"
#include "import.h"
//#include "shared_ptr.h"
#include "photo.h"
#include "photo_storage.h"
#include "process_h.h"
#include "system.h"
#include "tiles.h"
#include "widgets.h"

#include "f_demosaic.h"

using namespace std;

//#define _PREVIEW_SIZE	256
#define _PREVIEW_SIZE	384
//#define _PREVIEW_SIZE	512

//------------------------------------------------------------------------------

Filter_Store *Process::fstore = NULL;

// use QSet to store IDs of requests in process
// - after acceptance of processing request ID of process should be added in this set
// - to abort processing, ID should be removed from this set
// - processing trhread should abort processing ASAP as set is checked and there is no ID in progress in this set
// - after successful processing ID should be removed from this set
int Process::ID_counter = 0;
QMutex Process::ID_counter_lock;
QSet<int> Process::IDs_in_process;
bool Process::to_quit = false;
QMutex Process::quit_lock;

void Process::ID_add(int ID) {
	ID_counter_lock.lock();
	IDs_in_process.insert(ID);
	ID_counter_lock.unlock();
}

void Process::ID_remove(int ID) {
	ID_counter_lock.lock();
	IDs_in_process.remove(ID);
	ID_counter_lock.unlock();
}

bool Process::ID_to_abort(int ID) {
	ID_counter_lock.lock();
	bool to_abort = !IDs_in_process.contains(ID);
	ID_counter_lock.unlock();
//	cerr << " process with ID " << ID << "should be aborted: " << to_abort << endl;
	return to_abort;
}

void Process::ID_request_abort(int ID) {
	ID_counter_lock.lock();
	IDs_in_process.remove(ID);
	ID_counter_lock.unlock();
//	cerr << "requested abort for process with ID: " << ID << endl;
}

int Process::newID(void) {
	int ID;
	ID_counter_lock.lock();
	ID_counter++;
	if(ID_counter == 0)
		ID_counter++;
	ID = ID_counter;
	ID_counter_lock.unlock();
	return ID;
}

Process::Process(void) {
	Filter_Store::_this = new Filter_Store();
	fstore = Filter_Store::_this;
}

Process::~Process() {
}

void Process::quit(void) {
	quit_lock.lock();
	to_quit = true;
	quit_lock.unlock();
}

//------------------------------------------------------------------------------
class filter_record_t {
public:
	filter_record_t(void);
	Filter *filter;
	FilterProcess *fp;
	FilterProcess_2D *fp_2d;
//	ddr_shared_ptr<PS_Base> ps_base;
	QSharedPointer<PS_Base> ps_base;
	bool fp_is_wrapper;
};

filter_record_t::filter_record_t(void) {
	filter = NULL;
	fp = NULL;
	fp_2d = NULL;
	fp_is_wrapper = false;
}

class Process::task_run_t {
public:
	task_run_t(void);
	~task_run_t();

	bool update;
	Area *area_transfer;
	QSharedPointer<Photo_t> photo;
//	Photo_t *photo;
	TilesReceiver *tiles_receiver;
	Area::format_t out_format;
	bool is_offline;
	bool is_inactive;	// some filters should know how process is run - like for histogram update etc...

	int request_ID;	// ID of request
	// can be used 'volatile' or pointer - 'volatile' is 'more correct'
	volatile bool to_abort;	// shared flag of abortion

	list<filter_record_t> filter_records[2];	// 0 - thumbnail, 1 - tiles
	DataSet *mutators;
	DataSet *mutators_mpass;
	TilesDescriptor_t *tiles_request;
	int tile_index;

	bool _s_raw_colors;
};

Process::task_run_t::task_run_t(void) {
	area_transfer = NULL;
	tiles_receiver = NULL;
	mutators = NULL;
	mutators_mpass = NULL;
	tiles_request = NULL;
	_s_raw_colors = false;
	request_ID = 0;
//	to_abort = new bool;
//	*to_abort = false;
	to_abort = false;
}

Process::task_run_t::~task_run_t() {
//	delete to_abort;
	// delete all FilterProcess_CP_Wrapper
	for(int fi = 0; fi < 2; fi++) {
		for(list<filter_record_t>::iterator it = filter_records[fi].begin(); it != filter_records[fi].end(); it++)
			if((*it).fp_is_wrapper)
				delete (*it).fp;
	}
}

//------------------------------------------------------------------------------
class ProcessCache_t : public PhotoCache_t {
public:
	ProcessCache_t(void);
	~ProcessCache_t();

	Area *area_demosaic;
	// specialized cache for F_WB, should be replaced with 'generalized' caching system (?)
	Area *area_wb;
	// cache for 256x256 downscaled thumbnail after demosaic
	Area *area_thumb;
	// cache for downscaled resized area
	Area *area_scaled;
//	QMap<class FilterProcess *, class FP_Cache_t *> filters_cache;
	map<class FilterProcess *, class FP_Cache_t *> filters_cache;
};

ProcessCache_t::ProcessCache_t(void) {
	area_demosaic = NULL;
	area_wb = NULL;
	area_thumb = NULL;
	area_scaled = NULL;
}

ProcessCache_t::~ProcessCache_t(void) {
//cerr << "~ProcessCache_t()" << endl;
	if(area_demosaic != NULL) {
//cerr << "~ProcessCache_t(): delete area_demosaic" << endl;
		delete area_demosaic;
	}
	if(area_wb != NULL)
		delete area_wb;
	if(area_thumb != NULL) {
//cerr << "~ProcessCache_t(): delete area_thumb" << endl;
		delete area_thumb;
	}
	if(area_scaled != NULL) {
//cerr << "~ProcessCache_t(): delete area_scaled" << endl;
		delete area_scaled;
	}
	for(map<class FilterProcess *, class FP_Cache_t *>::iterator it = filters_cache.begin(); it != filters_cache.end(); it++)
		if((*it).second != NULL)
			delete (*it).second;
//cerr << "~ProcessCache_t() - done" << endl;
}

//void Process::allocate_process_caches(list<filter_record_t> &filters, Photo_t *photo) {
void Process::allocate_process_caches(list<filter_record_t> &filters, QSharedPointer<Photo_t> photo) {
	// cache for this process routine
	if(photo->cache_process == NULL) {
		photo->cache_process = new ProcessCache_t();
	}
//cerr << "allocated photo->cache_process == " << (unsigned long)photo->cache_process << endl;
	// create caches for filters here - transfer to filters caches via ready pointers;
	// here - obtain cache pointers from filters via virtual method of Filter::
	ProcessCache_t *pc = (ProcessCache_t *)photo->cache_process;
//	for(list<filter_record_t>::const_iterator it = filters.begin(); it != filters.end(); it++) {
	for(list<filter_record_t>::iterator it = filters.begin(); it != filters.end(); it++) {
//		if(pc->filters_cache[(*it).fp] == NULL)
		if(pc->filters_cache.find((*it).fp) == pc->filters_cache.end())
			pc->filters_cache[(*it).fp] = (*it).fp->new_FP_Cache();
	}
}

// The main idea is that: process should use only '2D' filters, all other should be wrapped into 'wrappers',
// those wrappers are '2D' filters-helpers for other filters that should only:
//  - change values of pixel;
//  - change input pixel coordinates for resampling (rotation and other field deformations);
// and those filters don't need to know values (coordinates or actual values) of neighbors.
void Process::assign_filters(list<filter_record_t> &filters, task_run_t *task) {
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	for(int pass = 0; pass < 2; pass++) {
		vector<class FP_GP_Wrapper_record_t> gp_wrapper_records = vector<class FP_GP_Wrapper_record_t>();
		vector<class FP_CP_Wrapper_record_t> cp_wrapper_records = vector<class FP_CP_Wrapper_record_t>();
		bool gp_wrapper_resampling = false;
		bool gp_wrapper_force = false;
		bool flag_crgb = false;
		for(list<filter_record_t>::const_iterator it = filters.begin(); true; it++) {
			FilterProcess::fp_type_en filter_type = FilterProcess::fp_type_unknown;
			if(it != filters.end())
				filter_type = (*it).fp->fp_type(pass == 0);
			// check if we need to add GP_Wrapper for resampling
			if(!gp_wrapper_resampling) {
//				if(flag_crgb && filter_type != FilterProcess::fp_type_gp && filter_type != FilterProcess::fp_type_2d) {
				if(flag_crgb && filter_type != FilterProcess::fp_type_gp) {
					gp_wrapper_force = true;
					gp_wrapper_resampling = true;
				}
			}
			// create a new GP wrapper if necessary, and add into processing chain
//`			if(gp_wrapper_force || (gp_wrapper_records.size() > 0 && filter_type != FilterProcess::fp_type_gp && filter_type != FilterProcess::fp_type_2d)) {
			if(gp_wrapper_force || (gp_wrapper_records.size() > 0 && filter_type != FilterProcess::fp_type_gp)) {
//				gp_wrapper_resampling = true;
				gp_wrapper_force = false;
				FilterProcess_GP_Wrapper *wrapper = new FilterProcess_GP_Wrapper(gp_wrapper_records);
//cerr << "==============================  add wrapper for filters: " << cp_wrapper_records.size() << " and pass == " << pass << endl;
				gp_wrapper_records = vector<class FP_GP_Wrapper_record_t>();
				filter_record_t r;
				r.fp_is_wrapper = true;
				r.filter = NULL;
				r.fp = wrapper;
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}
			// create a new CP wrapper if necessary, and add into processing chain
			if(cp_wrapper_records.size() > 0 && filter_type != FilterProcess::fp_type_cp) {
				FilterProcess_CP_Wrapper *wrapper = new FilterProcess_CP_Wrapper(cp_wrapper_records);
//cerr << "==============================  add wrapper for filters: " << cp_wrapper_records.size() << " and pass == " << pass << endl;
				cp_wrapper_records = vector<class FP_CP_Wrapper_record_t>();
				filter_record_t r;
				r.fp_is_wrapper = true;
				r.filter = NULL;
				r.fp = wrapper;
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}
			// analyze next filter if any
			if(it == filters.end())
				break;
			bool enabled = (*it).fp->is_enabled((*it).ps_base.data());
			if(!enabled)
				continue;
			// add record for GP filter for wrapper
			if(filter_type == FilterProcess::fp_type_gp) {
				FilterProcess_GP *fp_gp = (FilterProcess_GP *)(*it).fp->get_ptr(pass == 0);
				FP_GP_Wrapper_record_t wrapper_record;
				wrapper_record.filter = (*it).filter;
				wrapper_record.fp_gp = fp_gp;
				wrapper_record.cache = process_cache->filters_cache[(*it).fp];
				wrapper_record.ps_base = (*it).ps_base;
				wrapper_record.fs_base = task->photo->map_fs_base[(*it).filter];
				gp_wrapper_records.push_back(wrapper_record);
			}
			// add record for CP filter for wrapper
			if(filter_type == FilterProcess::fp_type_cp) {
				FilterProcess_CP *fp_cp = (FilterProcess_CP *)(*it).fp->get_ptr(pass == 0);
				FP_CP_Wrapper_record_t wrapper_record;
				wrapper_record.filter = (*it).filter;
				wrapper_record.fp_cp = fp_cp;
				wrapper_record.cache = process_cache->filters_cache[(*it).fp];
				wrapper_record.ps_base = (*it).ps_base;
				wrapper_record.fs_base = task->photo->map_fs_base[(*it).filter];
				cp_wrapper_records.push_back(wrapper_record);
			}
			// add 2D filter into processing chain
			if(filter_type == FilterProcess::fp_type_2d) {
				filter_record_t r = *it;
				r.fp_is_wrapper = false;
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}
//			flag_crgb = ((*it).filter->id() == string("F_Demosaic"));
			flag_crgb = ((*it).filter->id() == string("F_WB"));
//			flag_crgb |= ((*it).filter->id() == string("F_WB"));
		}
	}
#if 0
	for(int pass = 0; pass < 2; pass++) {
		cerr << "pass == " << pass << endl;
		for(std::list<filter_record_t>::iterator it = task->filter_records[pass].begin(); it != task->filter_records[pass].end(); it++)
			cerr << "    \"" << (*it).fp_2d->name() << "\"" << endl;
	}
#endif
}

//------------------------------------------------------------------------------
// call from Edit
//void Process::process_online(void *ptr, QSharedPointer<Photo_t> photo, int request_ID, bool is_inactive, TilesReceiver *tiles_receiver, map<Filter *, ddr_shared_ptr<PS_Base> > map_ps_base) {
void Process::process_online(void *ptr, QSharedPointer<Photo_t> photo, int request_ID, bool is_inactive, TilesReceiver *tiles_receiver, map<Filter *, QSharedPointer<PS_Base> > map_ps_base) {
	ID_add(request_ID);
	// TODO: register (thread-safe, with lock) tasks in some list, and then use flag to abort processing at request (i.e. UI parameters change during slow tiles processing)
	// use request ID to identify process to abort
//cerr << "   run() on " << photo->file_name << endl;
//cerr << "   process_online()" << endl;
//cerr << "   request_ID == " << request_ID << endl;
	Process::task_run_t task;
	task.update = true;
	task.out_format = Area::format_bgra_8;
	task.is_offline = false;
	task.is_inactive = is_inactive;
	task.tiles_receiver = tiles_receiver;
	task.request_ID = request_ID;

	PhotoProcessed_t *photo_processed = new PhotoProcessed_t();
	photo_processed->is_empty = true;

	// import photo if necessary
	if(photo->process_source == ProcessSource::s_load) {
		photo->metadata = new Metadata;
		// TODO: move to the one place
		photo->area_raw = Import::image(photo->photo_id.get_file_name(), photo->metadata);
//cerr << "photo->area_raw == " << (unsigned long)photo->area_raw << endl;
		if(photo->area_raw == NULL) {
cerr << "decline processing task, failed to import \"" << photo->photo_id.get_export_file_name() << "\"" << endl;
			emit signal_process_complete(ptr, photo_processed);
			return;
		}
		task.update = false;
	}
	task.photo = photo;

	// check control filter for skipping some other filters
	DataSet controls;
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); it++) {
		if((*it)->type() == Filter::t_control) {
			Filter_Control *fc = (Filter_Control *)(*it);
			fc->get_mutators(&controls);
		}
	}
	controls.get("_s_raw_colors", task._s_raw_colors);

	// create filters list
	list<filter_record_t> filter_records;
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); it++) {
		if((*it)->type() == Filter::t_control)
			continue;
		if(task._s_raw_colors)
			if((*it)->type() == Filter::t_color || (*it)->type() == Filter::t_wb)
				continue;
		filter_record_t filter_record;
		filter_record.filter = *it;
		// TODO: separate edit update and real process; move edit to size_forward routine, or somewhere else...
		//	- check on 'f_crop'
		filter_record.ps_base = map_ps_base[*it];
		// here filter can determine its passes tpe - single or two;
		filter_record.fp = (*it)->getFP();
		filter_records.push_back(filter_record);
	}
	allocate_process_caches(filter_records, photo);
	assign_filters(filter_records, &task);
//cerr << "task == " << (unsigned long)(void *)(task) << endl;
//cerr << "photo == " << (unsigned long)(void *)(photo) << endl;
//cerr << "photo->area_raw == " << (unsigned long)(void *)(task->photo->area_raw) << endl;

	// apply filters
	Flow flow(Process::subflow_run_mt, NULL, (void *)&task);
	flow.flow();

	// a real result should be handled by View via TilesReceiver; signal 'signal_process_complete' should be send from the other place
	photo_processed->is_empty = false;
	photo_processed->rotation = photo->cw_rotation;
	photo_processed->update = task.update;
	emit signal_process_complete(ptr, photo_processed);
	ID_remove(task.request_ID);
//	delete task;
}

//------------------------------------------------------------------------------
void Process::process_export(Photo_ID photo_id, string fname_export, export_parameters_t *ep) {
	if(photo_id.is_empty())
		return;
//cerr << "process_export() on " << photo_id << endl;
	Process::task_run_t task;
	task.is_offline = true;
	task.update = false;
	task.out_format = Area::format_rgb_8;
	task.request_ID = Process::newID();
	ID_add(task.request_ID);
	bool ep_alpha = ep->alpha();
	int ep_bits = ep->bits();
	if(ep_alpha)
		task.out_format = ((ep_bits == 16) ? Area::format_rgba_16 : Area::format_rgba_8);
	else
		task.out_format = ((ep_bits == 16) ? Area::format_rgb_16 : Area::format_rgb_8);

	if(ep->scaling_force) {
		task.tiles_receiver = new TilesReceiver(!ep->scaling_to_fill, ep->scaling_width, ep->scaling_height);
	} else {
		task.tiles_receiver = new TilesReceiver();
	}
	task.tiles_receiver->set_request_ID(task.request_ID);

Profiler prof(string("Batch for ") + photo_id.get_export_file_name());
prof.mark("load RAW");

	Photo_t *photo_ptr = new Photo_t();
	QSharedPointer<Photo_t> photo(photo_ptr);
	task.photo = photo;
	photo->process_source = ProcessSource::s_process_export;
	photo->metadata = new Metadata;
	photo->photo_id = photo_id;
	photo->area_raw = Import::image(photo->photo_id.get_file_name(), photo->metadata);
	if(photo->area_raw == NULL) {
		delete task.tiles_receiver;
		return;
	}

	// check control filter for skipping some other filters
	PS_Loader *ps_loader = new PS_Loader(photo_id);	// load settings

	photo->cw_rotation = photo->metadata->rotation;
	if(!ps_loader->cw_rotation_empty())
		photo->cw_rotation = ps_loader->get_cw_rotation();
	else
		ps_loader->set_cw_rotation(photo->cw_rotation);

//cerr << "offline ps_loader->serialize() == " << ps_loader->serialize() << endl;
	DataSet controls;
	QList<Filter *> filters_list = fstore->get_filters_list(false);
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); it++) {
		if((*it)->type() == Filter::t_control) {
			DataSet *ps_dataset = ps_loader->get_dataset((*it)->id());
			Filter_Control *fc = (Filter_Control *)(*it);
			fc->get_mutators(&controls, ps_dataset);
		}
	}
	controls.get("_s_raw_colors", task._s_raw_colors);

	// create filters list
	list<filter_record_t> filter_records;
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); it++) {
		if((*it)->type() == Filter::t_control)
			continue;
		if(task._s_raw_colors)
			if((*it)->type() == Filter::t_color || (*it)->type() == Filter::t_wb)
				continue;
		filter_record_t filter_record;
		filter_record.filter = *it;
		filter_record.fp = (*it)->getFP();
		filter_record.ps_base = QSharedPointer<PS_Base>((*it)->newPS());
		DataSet *dataset = ps_loader->get_dataset((*it)->id());
		filter_record.ps_base->load(dataset);
		filter_records.push_back(filter_record);
	}
	allocate_process_caches(filter_records, photo);
	assign_filters(filter_records, &task);
	delete ps_loader;

prof.mark("filters");
//	Flow flow(Process::subflow_run_mt, (void *)this, (void *)&task);
	Flow flow(Process::subflow_run_mt, NULL, (void *)&task);
	flow.flow();

	delete photo->area_raw;
	photo->area_raw = NULL;

prof.mark("export");
	// TODO: check Area geometry
	Export::export_photo(fname_export, task.tiles_receiver->area_image, task.tiles_receiver->area_thumb, ep, photo->cw_rotation, photo->metadata);

prof.mark("");
	ID_remove(task.request_ID);
	delete task.tiles_receiver;
}

//------------------------------------------------------------------------------
void Process::subflow_run_mt(void *obj, SubFlow *subflow, void *data) {
	run_mt(subflow, data);
//	((Process *)obj)->run_mt(subflow, data);
}

//------------------------------------------------------------------------------
// return PS_Base for Filter
PS_Base *ps_fr(Filter *filter, list<filter_record_t> &filter_records) {
	for(list<filter_record_t>::iterator it = filter_records.begin(); it != filter_records.end(); it++) {
		if((*it).filter == filter)
			return (*it).ps_base.data();
	}
	return NULL;
}

//------------------------------------------------------------------------------
void Process::process_demosaic(SubFlow *subflow, void *data) {
	Process::task_run_t *task = (Process::task_run_t *)data;
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;

//	if(subflow->is_master()) {
	if(subflow->sync_point_pre()) {
		// offline, for thumbnail processing
		if(process_cache->area_demosaic != NULL)
			delete process_cache->area_demosaic;
		process_cache->area_demosaic = NULL;
		task->area_transfer = task->photo->area_raw;
		task->mutators = new DataSet();
		task->mutators->set("_s_raw_colors", task->_s_raw_colors);
	}
	subflow->sync_point_post();

	// ----
//	Filter *demosaic = NULL;
	FilterProcess_2D *fp_demosaic = NULL;
//	PS_Base *ps_base;
	// TODO: use different types of demosaic for thumbnail and thumbs
	for(list<filter_record_t>::iterator it = task->filter_records[0].begin(); it != task->filter_records[0].end(); it++) {
		if((*it).filter->id() == string("F_Demosaic")) {
//			demosaic = (*it).filter;
			fp_demosaic = (*it).fp_2d;
//			ps_base = (*it).ps_base.ptr();
			break;
		}
	}

	MT_t mt_obj;
	mt_obj.subflow = subflow;

	Process_t process_obj;
	process_obj.area_in = task->area_transfer;
	process_obj.metadata = task->photo->metadata;
	process_obj.mutators = task->mutators;
	process_obj.allow_destructive = false;

	Filter_t filter_obj;
	filter_obj.ps_base = ps_fr((Filter *)fstore->f_demosaic, task->filter_records[0]);
	filter_obj.filter = fstore->f_demosaic;

	Area *a_demosaic = fp_demosaic->process(&mt_obj, &process_obj, &filter_obj);

	if(subflow->sync_point_pre()) {
		process_cache->area_demosaic = a_demosaic;
		delete task->mutators;
//		cerr << "after demosaic size == " << a_demosaic->dimensions()->width() << " x " << a_demosaic->dimensions()->height() << endl;
	}
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
void Process::run_mt(SubFlow *subflow, void *data) {
	Process::task_run_t *task = (Process::task_run_t *)data;
	const bool is_master = subflow->is_master();
	const bool online = !task->is_offline;
	const ProcessSource::process process_source = task->photo->process_source;
	//--------------------------------------------------------------------------
	Profiler *prof = NULL;
	if(subflow->sync_point_pre()) {
		quit_lock.lock();
		prof = new Profiler("Process");
		Mem::state_reset();
		task->mutators_mpass = new DataSet();
	}
	subflow->sync_point_post();
	if(to_quit)
		return;
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	//--------------------------------------------------------------------------
	// do demosaic
	bool to_process_demosaic = false;
	if(process_source == ProcessSource::s_process_export || process_source == ProcessSource::s_load || process_source == ProcessSource::s_demosaic)
		to_process_demosaic = true;
	if(to_process_demosaic) {
		if(is_master) {
			prof->mark("Demosaic");
			task->tiles_receiver->long_wait(true);
		}
		process_demosaic(subflow, data);
		if(is_master) {
			task->tiles_receiver->long_wait(false);
		}			
	}
	//--------------------------------------------------------------------------
	// NOTE WB: reset WB cache if necessary
	bool process_wb = (process_source == ProcessSource::s_wb);
	process_wb |= to_process_demosaic;
	process_wb |= task->is_offline;
	if(is_master && process_wb && process_cache->area_wb != NULL) {
		delete process_cache->area_wb;
		process_cache->area_wb = NULL;
//cerr << "!!! delete reset area_wb" << endl;
	}
	// create list of enabled filters
	list<class filter_record_t> pl_filters[3];	// 0 - thumbnail, 1 - tiles, 2 - tiles & demosaic
	for(int fi = 0; fi < 3; fi++) {
		int fr_i = (fi == 0) ? 0 : 1;
//		cerr << "filters list, iteration == " << fi << endl;
		for(list<filter_record_t>::iterator it = task->filter_records[fr_i].begin(); it != task->filter_records[fr_i].end(); it++) {
			Filter *filter = (*it).filter;
			// skip demosaic filter
			if(filter != NULL) {
				if(filter->type() == Filter::t_demosaic && fi != 2)
					continue;
				// NOTE WB: skip f_wb
//				if(!process_wb && filter->id() == string("F_WB"))
				if(filter->id() == string("F_WB")) {
					if(!process_wb)	continue;
					if(!task->is_offline && fi != 0)	continue;
				}
			}
			if((*it).fp->is_enabled((*it).ps_base.data())) {
				// check type - 'CP' or '2D'; create wrapper for 'CP'
				if((*it).fp->fp_type(fi == 0) == FilterProcess::fp_type_2d) {
					pl_filters[fi].push_back(*it);
//cerr << "  " << (*it).fp->name() << endl;
				} else {
				}
			} else {
			}
		}
	}
	//--------------------------------------------------------------------------
	// TODO: use this processing workflow:
	//	1. process thumbnail with result size 256x256; in online mode - push result to View;
	//	2.	- for online processing: ask view about desired size (according to the view scale)
	//		- for offline processing: process with 1:1 desired scale
	// adapt step 2 for tiles usage
	// TODO: check process_source == s_view_tiles - dont'd update geometry, scales etc - just ask tiles via get_tiles and process them
	Area::t_dimensions d_full_forward; // determine size of processed full-size image; used in View for correct Thumb rescaling
	bool process_view_tiles = false; // true for deferred process of tiles that was out of view before signal 's_view_refresh'
	if(process_source == ProcessSource::s_view_tiles)
		process_view_tiles = true;
	// ** call "size_forward()" through all enabled filters; result is geometry of photo as after processing with scale 1:1
	// ** then register it at tiles receiver to be able correctly determine desired scaling factor later.
	if(process_view_tiles == false) {
		if(subflow->sync_point_pre()) {
			task->mutators = new DataSet();
			task->mutators->set("_p_online", online);
			task->mutators->set("_p_thumb", false);
			task->area_transfer = process_cache->area_demosaic;
			// NOTE WB: use wb cache if any
			if(process_cache->area_wb != NULL)
				task->area_transfer = process_cache->area_wb;
			Area::t_dimensions d;
			// image size will be as after processing with 1:1 scale
			process_size_forward(task, pl_filters[2], d_full_forward);
/*
cerr << "after process_size_forward  px_size is " << d_full_forward.position.px_size_x << ", " << d_full_forward.position.px_size_y << endl;
cerr << "after process_size_forward position is " << d_full_forward.position.x << ", " << d_full_forward.position.y << endl;
cerr << "after process_size_forward     size is " << d_full_forward.width() << " x " << d_full_forward.height() << endl;
*/
			bool scale_to_size = false;
			int scale_to_width = 0;
			int scale_to_height = 0;
			bool scale_to_fit = true;
			task->mutators->get("scale_to_size", scale_to_size);
			if(scale_to_size) {
				task->mutators->get("scale_to_width", scale_to_width);
				task->mutators->get("scale_to_height", scale_to_height);
				task->mutators->get("scale_to_fit", scale_to_fit);
//cerr << "scale_to_size: " << scale_to_width << " x " << scale_to_height << "; and scale_to_fit == " << scale_to_fit << endl;
				if(scale_to_fit)
					Area::scale_dimensions_to_size_fit(&d_full_forward, scale_to_width, scale_to_height);
				else
					Area::scale_dimensions_to_size_fill(&d_full_forward, scale_to_width, scale_to_height);
			}
/*
cerr << "after process_size_forward  px_size is " << d_full_forward.position.px_size_x << ", " << d_full_forward.position.px_size_y << endl;
cerr << "after process_size_forward position is " << d_full_forward.position.x << ", " << d_full_forward.position.y << endl;
cerr << "after process_size_forward     size is " << d_full_forward.width() << " x " << d_full_forward.height() << endl;
*/
			task->tiles_receiver->register_forward_dimensions(&d_full_forward);
			delete task->mutators;
			task->mutators = NULL;
		}
		subflow->sync_point_post();
	}
	// ** request set of tiles (from TilesReceiver) and process each in a line, in a two passes
	// ** first pass - process thumbnail _PREVIEW_SIZE x _PREVIEW_SIZE on CPU only
	// ** second pass - process requested tiles
	int iteration = (process_view_tiles) ? 1 : 0;
	for(; iteration < 2; iteration++) {
//		if(is_master)
//			cerr << "iteration == " << iteration << endl;
		// NOTE: some ideas about possible tiles cache: useless for geometry filters, useful - for colors
		if(subflow->sync_point_pre()) {
			task->mutators = new DataSet();
			task->mutators->set("_p_online", online);
			task->mutators->set("_p_thumb", (bool)(iteration == 0));
			// was used for prepare_scaled_area, but now leads to memory leaks
			task->area_transfer = process_cache->area_demosaic;
			// NOTE WB: use wb cache if any
			if(process_cache->area_wb != NULL)
				task->area_transfer = process_cache->area_wb;
			D_AREA_PTR(task->area_transfer)
			if(process_view_tiles == false) { // ** generate a new complete tiles request for a whole photo
				Area::t_dimensions target_dimensions;
//cerr << "size == " << d_full_forward.width() << "x" << d_full_forward.height() << endl;
				target_dimensions = d_full_forward;
				if(iteration == 0) {
					int preview_size_w = _PREVIEW_SIZE;
					int preview_size_h = _PREVIEW_SIZE;
					Area::scale_dimensions_to_size_fit(&target_dimensions, preview_size_w, preview_size_h);
/*
cerr << "preview size: " << preview_size_w << " x " << preview_size_h << endl;
cerr << "  px_size is: " << target_dimensions.position.px_size_x << ", " << target_dimensions.position.px_size_y << endl;
cerr << " position is: " << target_dimensions.position.x << ", " << target_dimensions.position.y << endl;
cerr << "     size is: " << target_dimensions.width() << " x " << target_dimensions.height() << endl;
*/					
				}
				// get_tiles, asked rescaled size is inside tiles_request, and tiles exactly inside of that size
				task->tiles_request = task->tiles_receiver->get_tiles(&target_dimensions, task->photo->cw_rotation, (iteration == 0));
				target_dimensions.position.px_size_x = task->tiles_request->scale_factor_x;
				target_dimensions.position.px_size_y = task->tiles_request->scale_factor_y;
//cerr << "target_dimensions.px_size == " << target_dimensions.position.px_size << endl;
					// necessary for F_Rotation; is determination of {px_size with 'iteration == 0'} is deprecated now ??
				// ** prepare input sizes for tiles processing
				process_size_backward(task, pl_filters[iteration], target_dimensions);
			} else { // ** use already created tiles request, w/o creation a new set of tiles
//cerr << "process: get_tiles()...2" << endl;
				task->tiles_request = task->tiles_receiver->get_tiles();
			}
			// 
		}
		subflow->sync_point_post();
//		if(is_master)
//			cerr << "process_filters - start" << endl;
		if(subflow->sync_point_pre()) {
			quit_lock.unlock();
			task->mutators->set("_s_raw_colors", task->_s_raw_colors);
		}
		subflow->sync_point_post();
		process_filters(subflow, task, pl_filters[iteration], (iteration == 0), prof);
//		if(is_master)
//			cerr << "process_filters - done, now clean up" << endl;
		if(is_master)
			quit_lock.lock();
		// clean
		subflow->sync_point();
		if(is_master) {
			delete task->mutators;
			task->mutators = NULL;
			// TODO: handle that to tiles_receiver (?)
			task->tiles_request = NULL;
		}
//		if(*task->to_abort) {
		if(task->to_abort) {
			if(is_master)
				ID_remove(task->request_ID);
			break;
		}
		// don't clean up mess on quit
		if(to_quit)
			return;
/*
		bool to_abort = Process::ID_to_abort(task->request_ID);
		if(to_abort) {
			subflow->sync_point();
			if(is_master) {
				ID_remove(task->request_ID);
			}
			break;
		}
*/
	}
	//---------------------------------------------
	// clean
	if(subflow->sync_point_pre()) {
		quit_lock.unlock();
		prof->mark("");
		delete prof;
		Mem::state_print();
		Mem::state_reset(false);
		delete task->mutators_mpass;
	}
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
//void Process::process_size_forward(Process::task_run_t *task, const list<class filter_record_t> &pl_filters, Area::t_dimensions &d_out) {
void Process::process_size_forward(Process::task_run_t *task, list<class filter_record_t> &pl_filters, Area::t_dimensions &d_out) {
	Area::t_dimensions d_in = *task->area_transfer->dimensions();
	// TODO: fix that nonsense situation with edges; and fix incorrect, as result of false offset, position, in appropriate way
	d_in.size.w = d_in.width();
	d_in.size.h = d_in.height();
	d_in.edges.x1 = 0;
	d_in.edges.x2 = 0;
	d_in.edges.y1 = 0;
	d_in.edges.y2 = 0;
	d_out = d_in;	// forward geometry result

//cerr << endl << "process_size_forward(): " << endl;
//cerr << "process_size_forward, d_in.edges == " << d_in.edges.x1 << " - " << d_in.edges.x2 << endl;
//cerr << "process_size_forward,  d_in-> size == " << d_in.width() << "x" << d_in.height() << endl;
//	list<filter_record_t>::const_iterator it;
	list<filter_record_t>::iterator it;
	for(it = pl_filters.begin(); it != pl_filters.end(); it++) {
		FP_size_t fp_size((*it).ps_base.data());
		fp_size.metadata = task->photo->metadata;
		fp_size.mutators = task->mutators;
		d_out = d_in;
		// allow edit mode for online processing
		if(!task->is_offline) {
			fp_size.filter = (*it).filter;
		}
		(*it).fp_2d->size_forward(&fp_size, &d_in, &d_out);
//cerr << "after size_forward with FP_2D \"" << (*it).fp_2d->name() << "\" area == " << d_out.width() << "x" << d_out.height() << endl;
		d_in = d_out;
//cerr << "    \"" << (*it).fp_2d->name() << "\"" << endl;
//cerr << "..................... d_out-> size == " << d_out.width() << "x" << d_out.height() << endl;
	}
//cerr << "process_size_forward, d_out-> size == " << d_out.width() << "x" << d_out.height() << endl;
//cerr << "process_size_forward, d_out->edges == " << d_out.edges.x1 << " - " << d_out.edges.x2 << endl;
}

//------------------------------------------------------------------------------
//void Process::process_size_backward(Process::task_run_t *task, const list<class filter_record_t> &pl_filters, const Area::t_dimensions &target_dimensions) {
void Process::process_size_backward(Process::task_run_t *task, list<class filter_record_t> &pl_filters, const Area::t_dimensions &target_dimensions) {
//	int input_width = task->area_transfer->dimensions()->width();
//	int input_height = task->area_transfer->dimensions()->height();
	Area::t_dimensions d_in;
	Area::t_dimensions d_out;
	TilesDescriptor_t *tiles_request = task->tiles_request;
//Area::t_position &p = task->area_transfer->dimensions()->position;
//cerr << "Process::process_size_backward(): " << p.x << " - " << p.y << endl;
//cerr << "---------------->> Process::process_size_backward(): px_size == " << target_dimensions.position.px_size << endl;
	for(int i = 0; i < tiles_request->tiles.size() + 1; i++) {
		// actually, tiles coordinates and edges flags (outer|inner) was prepared in tiles_request, nothing to do here
//cerr << endl << "....................................................................." << "process_size_backward(); iteration == " << i << endl;
		if(i == 0) {
			// 1:1 size
			d_in = Area::t_dimensions(tiles_request->post_width, tiles_request->post_height);
			d_in.position = target_dimensions.position;
		} else {
			// tiles
			d_in = tiles_request->tiles[i - 1].dimensions_post;
		}
		// backward
//		list<filter_record_t>::const_iterator it = pl_filters.end();
		list<filter_record_t>::iterator it = pl_filters.end();
		if(it != pl_filters.begin()) {
			do {
				it--;
				FP_size_t fp_size((*it).ps_base.data());
				fp_size.metadata = task->photo->metadata;
				fp_size.mutators = task->mutators;
				fp_size.is_tile = (i != 0);
				d_out = d_in;
				if(i > 0) {
					// cache position and size of dimensions_after so at process time we can send exactly dimensions that are expecting by next filters and tiles receiver
					Tile_t::t_position tp;
					tp.x = d_in.position.x;
					tp.y = d_in.position.y;
					tp.width = d_in.width();
					tp.height = d_in.height();
					tp.px_size_x = d_in.position.px_size_x;
					tp.px_size_y = d_in.position.px_size_y;
//					tiles_request->tiles[i - 1].fp_position[(void *)((*it).fp_2d)] = tp;
					tiles_request->tiles[i - 1].fp_position[(*it).fp_2d->name()] = tp;
				}
				// allow edit mode for online processing
				if(!task->is_offline) {
					fp_size.filter = (*it).filter;
				}
				(*it).fp_2d->size_backward(&fp_size, &d_out, &d_in);
				d_in = d_out;
			} while(it != pl_filters.begin());
		} else {
			d_out = d_in;
		}
		// store result
		if(i == 0) {
			// i.e. 1:1 size
/*
cerr << "...___+++===>>>___: tiles_request->" << endl;
cerr << "...___+++===>>>___:   position: " << d_out.position.x << "_" << d_out.position.y << "; position max: " << d_out.position._x_max << "_" << d_out.position._y_max << endl;
cerr << "...___+++===>>>___:   px_size:  " << d_out.position.px_size << endl;
cerr << "...___+++===>>>___:   scale_factor was == " << tiles_request->scale_factor << endl;
*/
		} else {
			tiles_request->tiles[i - 1].dimensions_pre = d_out;
		}
	}
//cerr << endl << "====================================================" << "process_size_backward() - return" << endl;
}

//------------------------------------------------------------------------------
void Process::process_filters(SubFlow *subflow, Process::task_run_t *task, std::list<class filter_record_t> &pl_filters, bool is_thumb, Profiler *prof) {
	TilesDescriptor_t *tiles_request = task->tiles_request;
	const bool is_master = subflow->is_master();
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	Area *area_original = task->area_transfer;
	const bool allow_destructive = true;
//	const bool allow_destructive = false;
	QList<int> tiles_processed;	// used with disabled tiling
	bool was_abortion = false;
	while(true) {
		// cycle of tiles
		// for improved interactivity of UI, it's better to always update thumb
		//   when we at last are in processing request;
		// otherwise there could be a long cycle of discarded request
		//   without any visible to user results of UI settings change.
		if(is_master) {
			if(Process::ID_to_abort(task->request_ID) && !is_thumb)
				task->to_abort = true;
			quit_lock.lock();
			if(to_quit)
				task->to_abort = true;
			quit_lock.unlock();
		}
		subflow->sync_point();
		if(task->to_abort) {
			was_abortion = true;
			break;
		}
//if(is_thumb)
//cerr << "____ process_filters(): is_thumb" << endl;
		//-- get index of the next tile to process
		if(subflow->sync_point_pre()) {
			task->tile_index = -1;
			tiles_request->index_list_lock.lock();
			if(!tiles_request->index_list.isEmpty())
				task->tile_index = tiles_request->index_list.takeFirst();
			tiles_request->index_list_lock.unlock();
			if(tiles_processed.contains(task->tile_index))
				task->tile_index = -1;
			else
				tiles_processed.push_back(task->tile_index);
		}
		subflow->sync_point_post();
		int index = task->tile_index;
		if(index == -1)
			break;
		//-- process tile
		Tile_t *tile = &tiles_request->tiles[index];
		Area *area_in = NULL;
		if(subflow->sync_point_pre()) {
//			area_in = AreaHelper::crop(area_original, tile->dimensions_pre);
//cerr << "area mem size == " << area_original->mem_width() << " x " << area_original->mem_height() << endl;
//cerr << "area     size == " << area_original->dimensions()->width() << " x " << area_original->dimensions()->height() << endl;
			area_in = new Area(*area_original);
			task->area_transfer = area_in;
		}
		subflow->sync_point_post();
		for(list<filter_record_t>::iterator it = pl_filters.begin(); it != pl_filters.end(); it++) {
			// process current tile with each FilterProcess_2D
			Filter *filter = (*it).filter;
			PS_Base *ps = (*it).ps_base.data();
			if(is_master)
				prof->mark((*it).fp->name());
			MT_t mt_obj;
			mt_obj.subflow = subflow;
			Process_t process_obj;
			process_obj.area_in = task->area_transfer;
			if(subflow->is_master())
				process_obj.position = tile->fp_position[(*it).fp_2d->name()];
			process_obj.metadata = task->photo->metadata;
			process_obj.allow_destructive = allow_destructive;
			process_obj.mutators = task->mutators;
			process_obj.mutators_mpass = task->mutators_mpass;
			// looks like map operator [] is not thread-safe... Do below only from master thread
			process_obj.fp_cache = NULL;
			Filter_t filter_obj;
			filter_obj.ps_base = ps;
			filter_obj.fs_base = NULL;
			if(subflow->sync_point_pre()) {
				map<class FilterProcess *, class FP_Cache_t *>::iterator it_cache = process_cache->filters_cache.find((*it).fp);
				if(it_cache != process_cache->filters_cache.end())
					process_obj.fp_cache = (*it_cache).second;
				filter_obj.fs_base = task->photo->map_fs_base[filter];
			}
			subflow->sync_point_post();
			filter_obj.filter = task->is_offline ? NULL : filter;
			filter_obj.is_offline = task->is_offline;
//			if(is_master)
//				cerr << "process filter: \"" << (*it).fp_2d->name() << "\"" << endl;
			Area *area_rez = (*it).fp_2d->process(&mt_obj, &process_obj, &filter_obj);
			if(subflow->sync_point_pre()) {
				// NOTE WB: cache area if result of WB filter
				if(process_cache->area_wb == NULL && !task->is_offline) {
					if((*it).fp_2d->name() == "F_WB_2D") {
//cerr << "__ filter WB..." << endl;
//cerr << "__             process_cache->area_wb == " << (unsigned long)process_cache->area_wb << endl;
						process_cache->area_wb = new Area(*area_rez);
					}
				}
				task->area_transfer = area_rez;
				delete area_in;
				area_in = task->area_transfer;
			}
			subflow->sync_point_post();
		}
		// convert to asked format
		if(is_master) {
			prof->mark("convert tiles");
		}
		Area *area_out = AreaHelper::convert_mt(subflow, area_in, task->out_format, task->photo->cw_rotation);
		if(subflow->sync_point_pre()) {
			// delete P4_FLOAT area
			delete area_in;
			// update thumbnail for PS_Loader
			if(is_thumb) {
				if(task->photo->thumbnail != NULL)
					delete task->photo->thumbnail;
				task->photo->thumbnail = new Area(*area_out);
				D_AREA_PTR(task->photo->thumbnail);
			}
			// send result
			tile->area = area_out;
			tile->request_ID = task->request_ID;
			quit_lock.lock();
			// there is no reason to clean up on quit
			if(!to_quit)
				tiles_request->receiver->receive_tile(tile, is_thumb);
			quit_lock.unlock();
		}
		subflow->sync_point_post();
	}
	if(is_master && !was_abortion)
		tiles_request->receiver->process_done(is_thumb);
}

//------------------------------------------------------------------------------
