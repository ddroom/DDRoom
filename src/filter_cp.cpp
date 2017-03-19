/*
 * filter_cp.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "filter_cp.h"
#include "process_h.h"

using namespace std;

//------------------------------------------------------------------------------
FilterProcess_CP::FilterProcess_CP(void) {
	_name = "Unknown FilterProcess_CP";
	_fp_type = FilterProcess::fp_type_cp;
}

FP_Cache_t *FilterProcess_CP::new_FP_Cache(void) {
	return new FP_Cache_t();
}

void FilterProcess_CP::filter_pre(fp_cp_args_t *args) {
}

void FilterProcess_CP::filter_post(fp_cp_args_t *args) {
}

//------------------------------------------------------------------------------
FilterProcess_CP_Wrapper::FilterProcess_CP_Wrapper(const vector<class FP_CP_Wrapper_record_t> &_vector) {
	fp_cp_vector = _vector;
	_name = "FP_CP_Wrapper for filters: ";
	for(vector<class FP_CP_Wrapper_record_t>::iterator it = fp_cp_vector.begin(); it != fp_cp_vector.end(); ++it) {
		_name += (*it).fp_cp->name();
		if(it + 1 != fp_cp_vector.end())
			_name += ", ";
	}
	allow_destructive = true;
//cerr << "created CP_Wrapper: " << _name << endl;
}

FilterProcess_CP_Wrapper::~FilterProcess_CP_Wrapper(void) {
//cerr << "_____________________________________________    FilterProcess_CP_Wrapper::~FilterProcess_CP_Wrapper(void)" << endl;
}

void FilterProcess_CP_Wrapper::set_destructive(bool v) {
	allow_destructive = v;
}

bool FilterProcess_CP_Wrapper::is_enabled(const PS_Base *) {
	return true;
}

class FilterProcess_CP_Wrapper::task_t {
public:
	int flow_index;
	Area *area_in;
	Area *area_out;
	std::atomic_int *y_flow;
	bool destructive;
	std::vector<std::unique_ptr<fp_cp_args_t>> *filter_args;
};

std::unique_ptr<Area> FilterProcess_CP_Wrapper::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;

	const int filters_count = fp_cp_vector.size();
	const int threads_count = subflow->threads_count();

	std::unique_ptr<Area> area_out;
	std::vector<std::unique_ptr<fp_cp_args_t>> args(0);
	std::unique_ptr<std::atomic_int> y_flow;
	std::vector<std::unique_ptr<task_t>> tasks(0);

	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;

		bool destructive = process_obj->allow_destructive && allow_destructive;
		if(destructive)
			area_out = std::unique_ptr<Area>(new Area(*area_in));
		else
			area_out = std::unique_ptr<Area>(new Area(area_in->dimensions()));

		args.resize(filters_count);
		// prepare post-pixel filters
		for(int i = 0; i < filters_count; ++i) {
			args[i] = std::unique_ptr<fp_cp_args_t>(new fp_cp_args_t);
			fp_cp_args_t *arg = args[i].get();

			arg->metadata = process_obj->metadata;
			arg->mutators = process_obj->mutators;
			arg->mutators_multipass = process_obj->mutators_multipass;
			arg->ps_base = fp_cp_vector[i].ps_base.get();
			arg->vector_private = std::vector<std::unique_ptr<fp_cp_task_t>>(threads_count);
			arg->threads_count = threads_count;
			arg->cache = fp_cp_vector[i].cache;
			arg->filter = (filter_obj->is_offline) ? nullptr : fp_cp_vector[i].filter;
//			if(filter_obj->fs_base_active && filter_obj->is_offline == false)
			if(filter_obj->is_offline == false)
				arg->fs_base = fp_cp_vector[i].fs_base;
			else
				arg->fs_base = nullptr;
			fp_cp_vector[i].fp_cp->filter_pre(arg);
		}

		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_t>(new task_t);
			task_t *task = tasks[i].get();

			task->flow_index = i;
			task->area_in = area_in;
			task->area_out = area_out.get();
			task->y_flow = y_flow.get();
			task->destructive = destructive;
			task->filter_args = &args;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process(subflow);

	if(subflow->sync_point_pre()) {
		for(int i = 0; i < filters_count; ++i) {
			fp_cp_vector[i].fp_cp->filter_post(args[i].get());
		}
	}
	subflow->sync_point_post();

	return area_out;
}

void FilterProcess_CP_Wrapper::process(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();

	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

	const int in_mx1 = task->area_in->dimensions()->edges.x1;
	const int in_my1 = task->area_in->dimensions()->edges.y1;
	const int out_mx1 = task->area_out->dimensions()->edges.x1;
	const int out_my1 = task->area_out->dimensions()->edges.y1;
	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();

	float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const int filters_count = fp_cp_vector.size();
	float mt[4];
	const int flow_index = task->flow_index;
	const bool destructive = task->destructive;
	int j;
	auto y_flow = task->y_flow;
	while((j = y_flow->fetch_add(1)) < y_max) {
		int i_in = ((j + in_my1) * in_width + in_mx1) * 4;
		if(destructive) {
			for(int i = 0; i < x_max; ++i) {
				if(in[i_in + 3] > 0.0)
					for(int fi = 0; fi < filters_count; ++fi)
						fp_cp_vector[fi].fp_cp->filter(&in[i_in], (*(task->filter_args))[fi]->vector_private[flow_index].get());
				i_in += 4;
			}
		} else {
			int i_out = ((j + out_my1) * out_width + out_mx1) * 4;
			for(int i = 0; i < x_max; ++i) {
				if(in[i_in + 3] > 0.0) {
					mt[0] = in[i_in + 0];
					mt[1] = in[i_in + 1];
					mt[2] = in[i_in + 2];
					mt[3] = in[i_in + 3];
					for(int fi = 0; fi < filters_count; ++fi)
						fp_cp_vector[fi].fp_cp->filter(mt, ((*(task->filter_args))[fi]->vector_private[flow_index].get()));
					out[i_out + 0] = mt[0];
					out[i_out + 1] = mt[1];
					out[i_out + 2] = mt[2];
					out[i_out + 3] = mt[3];
				}
				i_in += 4;
				i_out += 4;
			}
		}
	}
}

//------------------------------------------------------------------------------
