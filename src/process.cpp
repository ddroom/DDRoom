/*
 * process.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTE:
	- generated mutators: '_p_thumb' -> bool;

 * TODO:
	- do a real cache - remember the last source of reprocessing and for the next time
		if the source is the same, remember tiles before that filter in hope that in the next time
		this cache can be used.

 */

#include <algorithm>
#include <iostream>
#include <memory>

#include "area_helper.h"
#include "export.h"
#include "filter.h"
#include "filter_cp.h"
#include "filter_gp.h"
#include "import.h"
#include "photo.h"
#include "photo_storage.h"
#include "process_h.h"
#include "system.h"
#include "tiles.h"
#include "widgets.h"

#include "f_demosaic.h"

using namespace std;

//#define _THUMBNAIL_SIZE	256
//#define _THUMBNAIL_SIZE	384
//#define _THUMBNAIL_SIZE	448
#define _THUMBNAIL_SIZE	512

//------------------------------------------------------------------------------
// Processing of the request should be aborted ASAP if there is no appropriate ID in that set,
//  except the thumbs processing, to improve UI interactivity.
int Process::ID_counter = 0;
std::mutex Process::ID_counter_lock;
std::set<int> Process::IDs_in_process;
std::atomic_int Process::to_quit(0);

void Process::ID_add(int ID) {
	ID_counter_lock.lock();
	IDs_in_process.insert(ID);
	ID_counter_lock.unlock();
}

void Process::ID_remove(int ID) {
	ID_counter_lock.lock();
	auto it = IDs_in_process.find(ID);
	if(it != IDs_in_process.end())
		IDs_in_process.erase(it);
	ID_counter_lock.unlock();
}

bool Process::ID_to_abort(int ID) {
	ID_counter_lock.lock();
//	bool to_abort = !IDs_in_process.contains(ID);
	// there is no such ID so yes, it should be aborted etc..
	bool to_abort = (IDs_in_process.find(ID) == IDs_in_process.end());
	ID_counter_lock.unlock();
//	cerr << " process with ID " << ID << "should be aborted: " << to_abort << endl;
	return to_abort;
}

void Process::ID_request_abort(int ID) {
	ID_counter_lock.lock();
	auto it = IDs_in_process.find(ID);
	if(it != IDs_in_process.end())
		IDs_in_process.erase(it);
//	IDs_in_process.remove(ID);
	ID_counter_lock.unlock();
//	cerr << "requested abort for process with ID: " << ID << endl;
}

int Process::newID(void) {
	int ID;
	ID_counter_lock.lock();
	++ID_counter;
	if(ID_counter == 0)
		++ID_counter;
	ID = ID_counter;
	ID_counter_lock.unlock();
	return ID;
}

Process::Process(void) {
	fstore = Filter_Store::instance();
}

Process::~Process() {
}

void Process::quit(void) {
	to_quit.store(1);
}

//------------------------------------------------------------------------------
class filter_record_t {
public:
	Filter *filter = nullptr;
	FilterProcess *fp = nullptr;
	FilterProcess_2D *fp_2d = nullptr;
	std::shared_ptr<PS_Base> ps_base;
	std::shared_ptr<FilterProcess> wrapper_holder;
	bool use_tiling = true;
	bool cache_result = false;
};

class Process::task_run_t {
public:
	bool update;
	bool is_offline;
	std::shared_ptr<Photo_t> photo;
	Area *area_transfer = nullptr;
	TilesReceiver *tiles_receiver = nullptr;
	Area::format_t out_format;

	int request_ID = 0;	// ID of request
	volatile bool to_abort = false;	// shared flag of abortion
//	Process_t *process_obj = nullptr;
	std::unique_ptr<Process_t> process_obj;

	// results
	bool bad_alloc = false;

	// inner
	// 0 - For thumbnail and 'size_forward' processing, 'whole' and wrapped 'tiled' enabled filters.
	// 1 - For tiles (deferred too) processing, wrapped 'tiled' enabled filters.
	std::vector<filter_record_t> filter_records[2];
	DataSet *mutators = nullptr;
	DataSet *mutators_multipass = nullptr;
	TilesDescriptor_t *tiles_request = nullptr;
	int tile_index = -1;

};

//------------------------------------------------------------------------------
class ProcessCache_t : public PhotoCache_t {
public:
	map<class FilterProcess *, std::shared_ptr<Area>> filters_area_cache;
	map<class FilterProcess *, std::unique_ptr<FP_Cache_t>> filters_cache;

	class FilterProcess *cache_fp_for_second_pass = nullptr;
	std::shared_ptr<Area> cached_area_for_second_pass; // could hold Area from 'filters_area_cache'

	void local_clear(void); // release pass-between area cache ASAP
};

void ProcessCache_t::local_clear(void) {
//	cache_fp_for_second_pass = nullptr;
//	cached_area_for_second_pass.reset();
}

//void Process::allocate_process_caches(list<filter_record_t> &filters, Photo_t *photo) {
void Process::allocate_process_caches(const std::vector<filter_record_t> &filters, std::shared_ptr<Photo_t> photo) {
	if(photo->cache_process == nullptr)
		photo->cache_process = new ProcessCache_t();

	// create caches for filters here - transfer to filters caches via ready pointers;
	// here - obtain cache pointers from filters via virtual method of Filter::
	ProcessCache_t *pc = (ProcessCache_t *)photo->cache_process;
	for(auto el : filters) {
		if(pc->filters_cache.find(el.fp) == pc->filters_cache.end())
			pc->filters_cache[el.fp] = std::unique_ptr<FP_Cache_t>(el.fp->new_FP_Cache());
	}
}

// The main idea is that: process should use only '2D' filters, all other should be wrapped into 'wrappers',
// those wrappers are '2D' filters-helpers for other filters that should only:
//  - change values of pixel;
//  - change input pixel coordinates for resampling (rotation and other field deformations);
// and those filters don't need to know values (coordinates or actual values) of neighbors.
//void Process::wrap_filters(const std::list<filter_record_t> &filters, task_run_t *task) {
void Process::wrap_filters(const std::vector<filter_record_t> &filters, task_run_t *task) {
	// prepare list of 'whole' filters for thumbnail processing
	for(auto el : filters) {
		const bool is_thumb = true;
		if(el.use_tiling == false && el.fp->fp_type(is_thumb) == FilterProcess::fp_type_2d) {
			filter_record_t r = el;
			bool enabled = r.fp->is_enabled(r.ps_base.get());
			if(!enabled)
				continue;
			r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(is_thumb); // get fp as for thumb
			task->filter_records[0].push_back(r);
//			task->filters_whole.push_back(r);
		}
	}

	// prepare list of 'tiled' filters
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
//	const int pass_max = task->thumbnail_only ? 1 : 2;
//	const int pass_max = 2;
//	for(int pass = 0; pass < pass_max; ++pass) {
	for(int pass = 0; pass < 2; ++pass) {
		vector<class FP_GP_Wrapper_record_t> gp_wrapper_records;
		vector<class FP_CP_Wrapper_record_t> cp_wrapper_records;
		bool gp_wrapper_resampling = false;
		bool gp_wrapper_force = false;
//		bool flag_crgb = false;
		bool flag_crgb = true;
		for(vector<filter_record_t>::const_iterator it = filters.cbegin(); true; ++it) {
			if(it != filters.cend()) {
				if(!(*it).use_tiling) {
//cerr << "skip FP: " << (*it).fp->name() << endl;
					continue;
				}
			}
			FilterProcess::fp_type_en filter_type = FilterProcess::fp_type_unknown;
			if(it != filters.cend())
				filter_type = (*it).fp->fp_type(pass == 0);
			// check if we need to add GP_Wrapper for resampling
			if(!gp_wrapper_resampling) {
//				if(flag_crgb && filter_type != FilterProcess::fp_type_gp && filter_type != FilterProcess::fp_type_2d) {
				if(flag_crgb && filter_type != FilterProcess::fp_type_gp) {
					gp_wrapper_force = true;
					gp_wrapper_resampling = true;
				}
			}

			// create a new GP wrapper
			if(gp_wrapper_force || (gp_wrapper_records.size() > 0 && filter_type != FilterProcess::fp_type_gp)) {
//cerr << "!!! gp_wrapper_force !!! " << endl;
				gp_wrapper_force = false;
				filter_record_t r;
				r.wrapper_holder.reset(new FilterProcess_GP_Wrapper(gp_wrapper_records));
				gp_wrapper_records.clear();
				r.filter = nullptr;
				r.fp = r.wrapper_holder.get();
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}

			// create a new CP wrapper
			if(cp_wrapper_records.size() > 0 && filter_type != FilterProcess::fp_type_cp) {
				filter_record_t r;
				r.wrapper_holder.reset(new FilterProcess_CP_Wrapper(cp_wrapper_records));
				cp_wrapper_records.clear();
				r.filter = nullptr;
				r.fp = r.wrapper_holder.get();
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}

			// analyze next filter if any
			if(it == filters.end())
				break;
			bool enabled = (*it).fp->is_enabled((*it).ps_base.get());
			if(!enabled)
				continue;
			// add record for GP filter for wrapper
			if(filter_type == FilterProcess::fp_type_gp) {
				FilterProcess_GP *fp_gp = (FilterProcess_GP *)(*it).fp->get_ptr(pass == 0);
				FP_GP_Wrapper_record_t wrapper_record;
				wrapper_record.filter = (*it).filter;
				wrapper_record.fp_gp = fp_gp;
				wrapper_record.cache = process_cache->filters_cache[(*it).fp].get();
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
				wrapper_record.cache = process_cache->filters_cache[(*it).fp].get();
				wrapper_record.ps_base = (*it).ps_base;
				wrapper_record.fs_base = task->photo->map_fs_base[(*it).filter];
				cp_wrapper_records.push_back(wrapper_record);
			}
			// add 2D filter into processing chain
			if(filter_type == FilterProcess::fp_type_2d) {
				filter_record_t r = *it;
				r.fp_2d = (FilterProcess_2D *)r.fp->get_ptr(pass == 0);
				task->filter_records[pass].push_back(r);
			}
//			flag_crgb = ((*it).filter->id() == std::string("F_WB"));
		}
	}
}

//------------------------------------------------------------------------------
// Helper for 'edit'.
// Should be moved somewhere outside.
bool Process::process_edit(void *ptr, std::shared_ptr<Photo_t> photo, int request_ID, TilesReceiver *tiles_receiver, std::map<Filter *, std::shared_ptr<PS_Base>> map_ps_base) {
	Process_task_t process_task;
	process_task.photo = photo;
	process_task.request_ID = request_ID;
	process_task.tiles_receiver = tiles_receiver;

	std::vector<class Filter_process_desc_t> &filters_desc = process_task.filters_desc;
	for(auto el : fstore->get_filters_whole()) {
		Filter_process_desc_t desc{el};
		desc.use_tiling = false;
		desc.cache_result = false;
//		if(el->id() == "F_WB" || el->id() == "F_Demosaic")
		if(el->get_id() == ProcessSource::s_wb || el->get_id() == ProcessSource::s_demosaic)
			desc.cache_result = true;
		filters_desc.push_back(desc);
	}
	for(auto el : fstore->get_filters_tiled()) {
		Filter_process_desc_t desc{el};
		desc.use_tiling = true;
		filters_desc.push_back(desc);
	}

	process_task.map_ps_base = map_ps_base;

	process_task.update = true;
	process_task.is_offline = false;
	process_task.out_format = Area::format_t::bgra_8;


	process_task.priority = Flow::priority_online_interactive;
	if(photo->process_source == ProcessSource::s_load)
		process_task.priority = Flow::priority_online_open;

	Process::process(&process_task);

	PhotoProcessed_t *photo_processed = new PhotoProcessed_t();
	photo_processed->is_empty = process_task.failed;
	photo_processed->rotation = process_task.result_cw_rotation;
	photo_processed->update = process_task.result_update;
	emit signal_process_complete(ptr, photo_processed);
	if(process_task.failed) {
		if(process_task.failed_oom) {
			OOM_desc_t *OOM_desc = new OOM_desc_t;
			OOM_desc->photo_id = photo->photo_id;
			OOM_desc->at_export = false;
			OOM_desc->at_open_stage = process_task.failed_at_import;
			emit signal_OOM_notification((void *)OOM_desc);
		}
	}
	return !process_task.failed;
}

//------------------------------------------------------------------------------
// Helper for 'export'.
// Should be moved somewhere outside.
bool Process::process_export(Photo_ID photo_id, string fname_export, export_parameters_t *ep) {
	if(photo_id.is_empty())
		return false;

	Profiler prof(string("Batch for ") + photo_id.get_export_file_name());
	prof.mark("export");

	std::shared_ptr<Photo_t> photo(new Photo_t());
	photo->process_source = ProcessSource::s_process_export;
	photo->photo_id = photo_id;

	Process_task_t process_task;
	process_task.photo = photo;
	process_task.request_ID = Process::newID();

	std::vector<class Filter_process_desc_t> &filters_desc = process_task.filters_desc;
	for(auto el : fstore->get_filters_whole()) {
		Filter_process_desc_t desc{el};
		desc.use_tiling = false;
		desc.cache_result = false;
		filters_desc.push_back(desc);
	}
	for(auto el : fstore->get_filters_tiled()) {
		Filter_process_desc_t desc{el};
		desc.use_tiling = true;
		filters_desc.push_back(desc);
	}

	process_task.update = false;
	process_task.is_offline = true;
	process_task.priority = Flow::priority_offline;
	const bool ep_alpha = ep->alpha();
	const int ep_bits = ep->bits();
	if(ep_alpha)
		process_task.out_format = ((ep_bits == 16) ? Area::format_t::rgba_16 : Area::format_t::rgba_8);
	else
		process_task.out_format = ((ep_bits == 16) ? Area::format_t::rgb_16 : Area::format_t::rgb_8);

	// load filter settings
	std::unique_ptr<PS_Loader> ps_loader(new PS_Loader(photo_id));
	if(!ps_loader->cw_rotation_empty())
		photo->cw_rotation = ps_loader->get_cw_rotation();
	else {
		// TODO: think how to load just CW rotation in here
		Metadata metadata;
		Import::load_metadata(photo_id.get_file_name(), &metadata);
		photo->cw_rotation = metadata.rotation;
		ps_loader->set_cw_rotation(photo->cw_rotation);
	}
	std::unique_ptr<TilesReceiver> tiles_receiver;
	if(ep->scaling_force)
		tiles_receiver = std::unique_ptr<TilesReceiver>(new TilesReceiver(!ep->scaling_to_fill, ep->scaling_width, ep->scaling_height));
	else
		tiles_receiver = std::unique_ptr<TilesReceiver>(new TilesReceiver());
	process_task.tiles_receiver = tiles_receiver.get();
	process_task.tiles_receiver->use_tiling(true, photo->cw_rotation, process_task.out_format);
	process_task.tiles_receiver->set_request_ID(process_task.request_ID);

	std::map<Filter *, std::shared_ptr<PS_Base>> &map_ps_base = process_task.map_ps_base;
	for(auto el : process_task.filters_desc) {
		Filter *filter = el.filter;
		PS_Base *ps = filter->newPS();
		DataSet *dataset = ps_loader->get_dataset(filter->id());
		ps->load(dataset);
		map_ps_base[filter] = std::move(std::shared_ptr<PS_Base>(ps));
	}
	ps_loader.reset();

	Process::process(&process_task);
	photo->area_raw.reset();

	if(process_task.failed) {
		if(process_task.failed_oom) {
			OOM_desc_t *OOM_desc = new OOM_desc_t;
			OOM_desc->photo_id = photo->photo_id;
			OOM_desc->at_export = false;
			OOM_desc->at_open_stage = process_task.failed_at_import;
			emit signal_OOM_notification((void *)OOM_desc);
		}
	} else {
		Export::export_photo(fname_export, tiles_receiver->area_image, tiles_receiver->area_thumb, ep, photo->cw_rotation, photo->metadata);
	}
	prof.mark("");
	return !process_task.failed;
}

//------------------------------------------------------------------------------
bool Process::process(Process_task_t *process_task) {
	ID_add(process_task->request_ID);

	Process::task_run_t task;
	task.request_ID = process_task->request_ID;
	task.photo = process_task->photo;
	task.update = process_task->update;
	task.out_format = process_task->out_format;
	task.is_offline = process_task->is_offline;
	task.tiles_receiver = process_task->tiles_receiver;

	// import photo if necessary
	bool bad_alloc = false;
	bool import = false;
	import |= (task.photo->process_source == ProcessSource::s_load);
	import |= (task.photo->process_source == ProcessSource::s_process_export);
	if(import || task.photo->area_raw == nullptr) {
		try {
			if(task.photo->metadata == nullptr)
				task.photo->metadata = new Metadata;
			task.photo->area_raw = std::unique_ptr<Area>(Import::image(task.photo->photo_id.get_file_name(), task.photo->metadata));

			Area *a = task.photo->area_raw.get();
			Area::t_dimensions *d = a->dimensions();
		} catch(Area::bad_alloc) {
			bad_alloc = true;
		} catch(std::bad_alloc) {
			bad_alloc = true;
		}
		if(task.photo->area_raw == nullptr || bad_alloc) {
			process_task->failed = true;
			process_task->failed_oom = bad_alloc;
			process_task->failed_at_import = true;
cerr << "decline processing task, failed to import \"" << task.photo->photo_id.get_export_file_name() << "\"" << endl;
			ID_remove(process_task->request_ID);
			return false;
		}
		task.update = false;
	}

	// prepare filters list
	std::vector<filter_record_t> filter_records;
	for(auto el : process_task->filters_desc) {
		filter_record_t filter_record;
		filter_record.filter = el.filter;
		filter_record.fp = el.filter->getFP();
		filter_record.ps_base = process_task->map_ps_base[el.filter];
		filter_record.use_tiling = el.use_tiling;
		filter_record.cache_result = el.cache_result;
		filter_records.push_back(filter_record);
	}
	allocate_process_caches(filter_records, task.photo);
	wrap_filters(filter_records, &task);

	// apply filters
	bad_alloc = false;
	try {
		Flow flow(process_task->priority, Process::subflow_run_mt, nullptr, (void *)&task);
		flow.flow();
		bad_alloc = task.bad_alloc;
	} catch(Area::bad_alloc) {
		bad_alloc = true;
	} catch(std::bad_alloc) {
		bad_alloc = true;
	}

	ID_remove(task.request_ID);

	if(bad_alloc) {
		process_task->failed = true;
		process_task->failed_oom = true;
		return false;
	}
	process_task->result_cw_rotation = process_task->photo->cw_rotation;
	process_task->result_update = task.update;
	return true;
}

//------------------------------------------------------------------------------
void Process::subflow_run_mt(void *obj, SubFlow *subflow, void *data) {
	bool bad_alloc = false;
	try {
		run_mt(subflow, data);
	} catch(Area::bad_alloc) {
		bad_alloc = true;
	} catch(std::bad_alloc) {
		bad_alloc = true;
	}
	if(bad_alloc && subflow->is_main()) {
		subflow->abort();
		task_run_t *task = (task_run_t *)data;
		task->bad_alloc = true;
	}
	// clean up
	if(subflow->is_main()) {
		task_run_t *task = (task_run_t *)data;
		((ProcessCache_t *)task->photo->cache_process)->local_clear();
	}
}

//------------------------------------------------------------------------------
// Determine cached area to be used, update filters list to process,
// determine filter that result of should be cached between first and second passes.
class Area *Process::select_cached_area_and_filters_to_process(std::vector<filter_record_t> &filters_to_process, class task_run_t *task, const int pass, const bool is_main) {
//cerr << "select_cached_area_and_filters_to_process" << endl;

	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	std::vector<filter_record_t> &task_filter_records = task->filter_records[pass];
	// Determine what possible cached area use as input.
	// TODO: add support of the caches for 'tiled' area too

	if(pass == 1) {
		// not too much to do...
/*
cerr << "Pass 1..." << endl;
cerr << "Pass 1: cache_fp_for_second_pass == " << process_cache->cache_fp_for_second_pass << endl;
cerr << "Pass 1: cache_fp_for_second_pass == \"" << process_cache->cache_fp_for_second_pass->name() << "\"" << endl;
cerr << "cached_area == " << process_cache->cached_area_for_second_pass.get() << endl;
*/
		Area *cached_area = process_cache->cached_area_for_second_pass.get();
//cerr << "cached_area->type == " << Area::type_to_name(cached_area->type()) << endl;
		filters_to_process = task_filter_records;
		return cached_area;
	}

	// pass == 0
	const ProcessSource::process process_source = task->photo->process_source;

	// remove deprecated caches if any
	bool remove_cache = false;
	int index_tiling = task_filter_records.size() - 1;
	for(int i = 0; i < task_filter_records.size(); ++i) {
		if(task_filter_records[i].use_tiling) {
			index_tiling = i;
			break;
		}
		if(task_filter_records[i].filter->get_id() == process_source)
			remove_cache = true;
		if(remove_cache) {
			auto cache_result = process_cache->filters_area_cache.find(task_filter_records[i].fp);
			if(cache_result != process_cache->filters_area_cache.end())
					process_cache->filters_area_cache.erase(cache_result);
		}
	}

	// prepare the list of the filters to be processed after the cache
	filters_to_process.reserve(task_filter_records.size());
	Area *cached_area = nullptr;
	int index = 0;
	FilterProcess *fp = nullptr;
	FilterProcess *cached_area_fp = nullptr;

	// index from where filters to process begin
	// search for the last usable cache
	for(int i = 0; i < index_tiling; ++i) {
		fp = task_filter_records[i].fp;
		if(task_filter_records[i].cache_result == true) {
			auto cache_result = process_cache->filters_area_cache.find(fp);
			if(cache_result != process_cache->filters_area_cache.end()) {
				cached_area_fp = fp;
				cached_area = (*cache_result).second.get();
				index = i + 1;
			}
		}
	}
	if(is_main) {
		process_cache->cache_fp_for_second_pass = fp;
//cerr << "Pass 0: cache_fp_for_second_pass == \"" << fp->name() << "\"" << endl;
		if(cached_area == nullptr)
			process_cache->cached_area_for_second_pass.reset();
		if(cached_area != nullptr && fp == cached_area_fp) {
//			process_cache->cache_fp_for_second_pass = nullptr;
//			if(process_cache->cached_area_for_second_pass == nullptr)
				process_cache->cached_area_for_second_pass.reset(new Area(*cached_area));
		}
	}

	// prepare list of filters to process
	if(index != 0)
		for(int i = index; i < task_filter_records.size(); ++i)
			filters_to_process.push_back(task_filter_records[i]);
	else
		filters_to_process = task_filter_records;
	return cached_area;
}

//------------------------------------------------------------------------------
void Process::run_mt(SubFlow *subflow, void *data) {
	Process::task_run_t *task = (Process::task_run_t *)data;
	const bool is_main = subflow->is_main();
	const ProcessSource::process process_source = task->photo->process_source;
	std::unique_ptr<DataSet> mutators_multipass;
	//--------------------------------------------------------------------------
	Profiler *prof = nullptr;
	if(subflow->sync_point_pre()) {
		prof = new Profiler("Process");
//		Mem::state_reset();
		mutators_multipass.reset(new DataSet());
		task->mutators_multipass = mutators_multipass.get();
	}
	subflow->sync_point_post();
	if(to_quit.load() != 0)
		return;

	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	const bool process_deferred_tiles = (process_source == ProcessSource::s_view_tiles);

	// determine size of processed full-size image; used in View for correct Thumb rescaling
	Area::t_dimensions d_full_forward; // used from the 'main' thread

	// true for deferred process of tiles that was out of view before signal 's_view_refresh'
	if(process_deferred_tiles == false) {
		if(subflow->sync_point_pre()) {
			// call "size_forward()" through all enabled filters; result is geometry of photo as after processing with scale 1:1
			// then register it at tiles receiver to be able correctly determine desired scaling factor later.
			DataSet mutators;
			task->mutators = &mutators;
			task->mutators->set("_p_thumb", false);

			// image size will be as after processing with 1:1 scale
			Area::t_dimensions *d_in_ptr = task->photo->area_raw.get()->dimensions();
			process_size_forward(d_full_forward, task, task->filter_records[0], d_in_ptr);
			task->tiles_receiver->register_forward_dimensions(&d_full_forward);
			task->mutators = nullptr;
		}
		subflow->sync_point_post();
	}

	//  Request set of tiles (from TilesReceiver) and process each in a line, in a two passes.
	//  first pass - process thumbnail _THUMBNAIL_SIZE x _THUMBNAIL_SIZE on CPU only
	//  second pass - process requested tiles
	int pass = (process_deferred_tiles) ? 1 : 0;
	for(; pass < 2; ++pass) {
		const bool is_thumb = (pass == 0);

		auto &task_filter_records = task->filter_records[pass];
		if(task_filter_records.size() == 0) {
			if(is_thumb)
				task->tiles_request->receiver->process_done(is_thumb);
			continue;
		}

		std::vector<filter_record_t> filter_records;
		Area *last_cached_area = select_cached_area_and_filters_to_process(filter_records, task, pass, is_main);

		// Prepare tiles.
		std::unique_ptr<DataSet> mutators;
		if(subflow->sync_point_pre()) {
			task->area_transfer = task->photo->area_raw.get();
			if(last_cached_area != nullptr)
				task->area_transfer = last_cached_area;

			mutators.reset(new DataSet());
			task->mutators = mutators.get();
			task->mutators->set("_p_thumb", is_thumb);
			if(process_deferred_tiles == false) { // ** generate a new complete tiles request for a whole photo
				Area::t_dimensions target_dimensions;
//cerr << "size == " << d_full_forward.width() << "x" << d_full_forward.height() << endl;
				target_dimensions = d_full_forward;
				if(is_thumb) {
					const int thumbnail_w = _THUMBNAIL_SIZE;
					const int thumbnail_h = _THUMBNAIL_SIZE;
					Area::scale_dimensions_to_size_fit(&target_dimensions, thumbnail_w, thumbnail_h);
				}
				// get_tiles, asked rescaled size is inside tiles_request, and tiles exactly inside of that size
				task->tiles_request = task->tiles_receiver->get_tiles(&target_dimensions, task->photo->cw_rotation, is_thumb);
				target_dimensions.position.px_size_x = task->tiles_request->scale_factor_x;
				target_dimensions.position.px_size_y = task->tiles_request->scale_factor_y;
//cerr << "target_dimensions.px_size == " << target_dimensions.position.px_size_x << endl;
				// prepare input sizes for tiles processing
				process_size_backward(task, filter_records, target_dimensions);
			} else { // ** use already created tiles request, w/o creation a new set of tiles
//cerr << "process: get_tiles()...2" << endl;
				task->tiles_request = task->tiles_receiver->get_tiles();
			}
			// 
		}
		subflow->sync_point_post();
//		if(is_main)
//			cerr << "process_filters - start" << endl;

		try {
			process_filters(subflow, task, filter_records, is_thumb, prof);
		} catch(...) {
			if(is_main) // can be OOM - release task receiver
				task->tiles_request->receiver->process_done(is_thumb);
			throw;
		}

//		if(is_main)
//			cerr << "process_filters - done, now clean up" << endl;
		// clean
		subflow->sync_point();

		if(is_main) {
//			delete task->mutators;
			task->mutators = nullptr;
			// TODO: handle that to tiles_receiver (?)
			task->tiles_request = nullptr;
		}
		if(task->to_abort) {
			if(is_main)
				ID_remove(task->request_ID);
			break;
		}
		// don't clean up mess on quit
		if(to_quit.load() != 0)
			return;
	}
	//---------------------------------------------
	// clean
	if(subflow->sync_point_pre()) {
		prof->mark("");
		delete prof;
		Mem::state_print();
//		Mem::state_reset();
		// remove inter-pass caches if any
		process_cache->local_clear();
	}
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
void Process::process_size_forward(Area::t_dimensions &d_out, Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, Area::t_dimensions *d_in_ptr) {
	Area::t_dimensions d_in = *d_in_ptr;
	d_in.size.w = d_in.width();
	d_in.size.h = d_in.height();
	d_in.edges.x1 = 0;
	d_in.edges.x2 = 0;
	d_in.edges.y1 = 0;
	d_in.edges.y2 = 0;
	d_out = d_in;	// forward geometry result
/*
cerr << endl << "process_size_forward():  in geometry: " << endl;
cerr << "     d_in.edges == " << d_in.edges.x1 << " - " << d_in.edges.x2 << endl;
cerr << "  d_in.position == " << d_in.position.x << " x " << d_in.position.y << endl;
cerr << "      d_in.size == " << d_in.width() << " x " << d_in.height() << endl;
cerr << "       d_in.w&h == " << d_in.size.w << " x " << d_in.size.h << endl;
*/
//	list<filter_record_t>::iterator it;
//	for(it = pl_filters.begin(); it != pl_filters.end(); ++it) {
	for(auto el : pl_filters) {
//		FP_size_t fp_size((*it).ps_base.get());
		FP_size_t fp_size(el.ps_base.get());
		fp_size.metadata = task->photo->metadata;
		fp_size.mutators = task->mutators;
		fp_size.mutators_multipass = task->mutators_multipass;
		fp_size.cw_rotation = task->photo->cw_rotation;
		// allow edit mode for online processing
		if(!task->is_offline)
			fp_size.filter = el.filter;
//			fp_size.filter = (*it).filter;
		d_out = d_in;
		el.fp_2d->size_forward(&fp_size, &d_in, &d_out);
//		(*it).fp_2d->size_forward(&fp_size, &d_in, &d_out);
		d_in = d_out;
/*
cerr << "after size_forward with FP_2D \"" << el.fp_2d->name() << "\" area == " << d_out.width() << "x" << d_out.height() << endl;
cerr << "    \"" << el.fp_2d->name() << "\"" << endl;
cerr << "..................... d_out-> size == " << d_out.width() << "x" << d_out.height() << endl;
cerr << "..................... d_out->  w&h == " << d_out.size.w << "x" << d_out.size.h << endl;
*/
	}
/*
cerr << endl << "process_size_forward():  out geometry: " << endl;
cerr << "     d_out.edges == " << d_out.edges.x1 << " - " << d_out.edges.x2 << endl;
cerr << "  d_out.position == " << d_out.position.x << " x " << d_out.position.y << endl;
cerr << "      d_out.size == " << d_out.width() << " x " << d_out.height() << endl;
cerr << "       d_out.w&h == " << d_out.size.w << " x " << d_out.size.h << endl;
cerr << endl;
*/
}

//------------------------------------------------------------------------------
void Process::process_size_backward(Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, const Area::t_dimensions &target_dimensions) {
	Area::t_dimensions d_in;
	Area::t_dimensions d_out;
	TilesDescriptor_t *tiles_request = task->tiles_request;
/*
cerr << "Process::process_size_backward(),  in geometry:" << endl;
cerr << "      size: " << tiles_request->post_width << "-" << tiles_request->post_height << endl;
cerr << "   px_size: " << tiles_request->scale_factor_x << "-" << tiles_request->scale_factor_y << endl;
cerr << "  position: " << target_dimensions.position.x << "-" << target_dimensions.position.y << endl;
*/
	for(int i = 0; i < tiles_request->tiles.size() + 1; ++i) {
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
		auto it = pl_filters.end();
		if(it != pl_filters.begin()) {
			do {
				--it;
				FP_size_t fp_size((*it).ps_base.get());
				fp_size.metadata = task->photo->metadata;
				fp_size.mutators = task->mutators;
				fp_size.mutators_multipass = task->mutators_multipass;
				fp_size.cw_rotation = task->photo->cw_rotation;
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
cerr << "Process::process_size_backward(), out geometry:" << endl;
cerr << "      size: " << d_out.size.w << " - " << d_out.size.h << endl;
cerr << "   px_size: " << d_out.position.px_size_x << " - " << d_out.position.px_size_y << endl;
cerr << "  position: " << d_out.position.x << ", " << d_out.position.y << endl;
*/
		} else {
			tiles_request->tiles[i - 1].dimensions_pre = d_out;
		}
	}
//cerr << endl << "====================================================" << "process_size_backward() - return" << endl;
}

//------------------------------------------------------------------------------
void Process::process_filters(SubFlow *subflow, Process::task_run_t *task, std::vector<class filter_record_t> &pl_filters, bool is_thumb, Profiler *prof) {
	TilesDescriptor_t *tiles_request = task->tiles_request;
	const bool is_main = subflow->is_main();
	ProcessCache_t *process_cache = (ProcessCache_t *)task->photo->cache_process;
	Area *area_original = task->area_transfer;
	const bool allow_destructive = true;
//	const bool allow_destructive = false;
	std::set<int> tiles_processed;	// used with disabled tiling
	bool was_abortion = false;
	while(true) {
		// cycle of tiles
		// for improved interactivity of UI, it's better to always update thumb
		//   when we at last are in processing request;
		// otherwise there could be a long cycle of discarded requests
		//   without any visible interaction with user.
		if(is_main) {
			if(Process::ID_to_abort(task->request_ID) && !is_thumb)
				task->to_abort = true;
			if(to_quit.load() != 0)
				task->to_abort = true;
		}
		subflow->sync_point();
		if(task->to_abort) {
			was_abortion = true;
			break;
		}
//if(is_thumb) cerr << "____ process_filters(): is_thumb" << endl;

		//-- get index of the next tile to process
		if(subflow->sync_point_pre()) {
			task->tile_index = -1;
			tiles_request->index_list_lock.lock();
			if(!tiles_request->index_list.empty()) {
				task->tile_index = tiles_request->index_list.front();
				tiles_request->index_list.pop_front();
			}
			tiles_request->index_list_lock.unlock();
			if(tiles_processed.find(task->tile_index) != tiles_processed.end())
				task->tile_index = -1;
			else
				tiles_processed.insert(task->tile_index);
		}
		subflow->sync_point_post();
		int index = task->tile_index;
		if(index == -1)
			break;

		//-- process tile
		Tile_t *tile = &tiles_request->tiles[index];
		if(subflow->sync_point_pre()) {
			tile->request_ID = task->request_ID;
			task->area_transfer = new Area(*area_original);
		}
		subflow->sync_point_post();
		bool flag_long_wait = false;
		for(auto it = pl_filters.begin(); it != pl_filters.end(); ++it) {
			// process current tile with each FilterProcess_2D
			if(is_main) {
				prof->mark((*it).fp->name());
				if(flag_long_wait == false) {
					if((*it).use_tiling == false) {
						flag_long_wait = true;
						task->tiles_receiver->long_wait(true);
					}
				} else {
					if((*it).use_tiling == true) {
						flag_long_wait = false;
						task->tiles_receiver->long_wait(false);
					}
				}
			}

			if(subflow->sync_point_pre()) {
				Process_t *process_obj = new Process_t;
				task->process_obj.reset(process_obj);
				process_obj->area_in = task->area_transfer;
				process_obj->position = tile->fp_position[(*it).fp_2d->name()];
				process_obj->metadata = task->photo->metadata;
				process_obj->allow_destructive = allow_destructive;
				process_obj->mutators = task->mutators;
				process_obj->mutators_multipass = task->mutators_multipass;
				process_obj->fp_cache = nullptr;
				//--
				auto it_cache = process_cache->filters_cache.find((*it).fp);
				if(it_cache != process_cache->filters_cache.end())
					task->process_obj->fp_cache = (*it_cache).second.get();
			}
			subflow->sync_point_post();
			MT_t mt_obj;
			mt_obj.subflow = subflow;
			Filter_t filter_obj;
			filter_obj.ps_base = (*it).ps_base.get();
			filter_obj.fs_base = nullptr;
			filter_obj.filter = task->is_offline ? nullptr : (*it).filter;
			filter_obj.is_offline = task->is_offline;
			filter_obj.fs_base = task->photo->map_fs_base[(*it).filter];
//			if(is_main)
//				cerr << "process filter: \"" << (*it).fp_2d->name() << "\"" << endl;
			std::unique_ptr<Area> u_ptr = (*it).fp_2d->process(&mt_obj, task->process_obj.get(), &filter_obj);
			Area *result_area = u_ptr.release();
			if(subflow->sync_point_pre()) {
				const bool result_is_empty = (result_area == nullptr);
				if(result_area == nullptr)
					result_area = new Area(*task->area_transfer);
				// cache 'whole' filters
				if(is_thumb && (*it).use_tiling == false) {
					if((*it).cache_result) {
						if(!result_is_empty)
							process_cache->filters_area_cache[(*it).fp] = std::shared_ptr<Area>{new Area(*result_area)};
						if(process_cache->cache_fp_for_second_pass == nullptr)
							process_cache->cached_area_for_second_pass = process_cache->filters_area_cache[(*it).fp];
					}
					if(process_cache->cache_fp_for_second_pass == (*it).fp) {
						process_cache->cached_area_for_second_pass.reset(new Area(*result_area));
					}
				}
				delete task->area_transfer;
				task->area_transfer = result_area;
				task->process_obj.reset(nullptr);
			}
			subflow->sync_point_post();
		}
		// convert to asked format
		if(is_main)
			prof->mark("convert tiles");
		Area *area_out = nullptr;
		Area *tiled_area = nullptr;
		int insert_pos_x = 0;
		int insert_pos_y = 0;
		if(!is_thumb && is_main)
			tiled_area = task->tiles_request->receiver->get_area_to_insert_tile_into(insert_pos_x, insert_pos_y, tile);
		if(tiled_area != nullptr) {
			AreaHelper::convert_mt(subflow, task->area_transfer, task->out_format, task->photo->cw_rotation, tiled_area, insert_pos_x, insert_pos_y);
			area_out = tiled_area;
		} else {
			area_out = AreaHelper::convert_mt(subflow, task->area_transfer, task->out_format, task->photo->cw_rotation).release();
			// here 'area_out' will be the 'area_to_insert' if the last one wasn't 'nullptr'
		}
		if(subflow->sync_point_pre()) {
			// delete 'type_float_p4' area
			delete task->area_transfer;
			// update thumbnail for PS_Loader
			if(is_thumb)
				task->photo->thumbnail.reset(new Area(*area_out));
			// send result
			if(tile->area != nullptr)
				delete tile->area;
			tile->area = area_out;
//			tile->request_ID = task->request_ID;
			// there is no reason to clean up on quit
			if(to_quit.load() == 0)
				tiles_request->receiver->receive_tile(tile, is_thumb);
		}
		subflow->sync_point_post();
	}
	if(is_main && !was_abortion)
		tiles_request->receiver->process_done(is_thumb);
}

//------------------------------------------------------------------------------
