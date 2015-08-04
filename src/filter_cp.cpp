/*
 * filter_cp.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */


#include <iostream>

#include "filter_cp.h"
//#include "shared_ptr.h"
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
	for(vector<class FP_CP_Wrapper_record_t>::iterator it = fp_cp_vector.begin(); it != fp_cp_vector.end(); it++) {
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
	QAtomicInt *y_flow;
	bool destructive;
	fp_cp_args_t **filter_args;
};

Area *FilterProcess_CP_Wrapper::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	const int size = fp_cp_vector.size();

	task_t **tasks = NULL;
	Area *area_out = NULL;
	QAtomicInt *y_flow = NULL;
	const int cores = subflow->cores();
//	fp_cp_args_t *args[size];
	fp_cp_args_t_ptr *args = new fp_cp_args_t_ptr[size];

	if(subflow->sync_point_pre()) {
		tasks = new task_t *[cores];

		bool destructive = process_obj->allow_destructive && allow_destructive;
//destructive = false;
		if(destructive) {
			area_out = new Area(*area_in);
		} else {
			area_out = new Area(area_in->dimensions());
		}
		D_AREA_PTR(area_out);

		// prepare post-pixel filters
		for(int i = 0; i < size; i++) {
			args[i] = new fp_cp_args_t;
			args[i]->metadata = process_obj->metadata;
			args[i]->mutators = process_obj->mutators;
			args[i]->mutators_mpass = process_obj->mutators_mpass;
			args[i]->ps_base = fp_cp_vector[i].ps_base.data();
			args[i]->ptr_private = new void *[cores];
//			args[i]->ptr_private = new void *[size];
			args[i]->cores = cores;
			args[i]->cache = fp_cp_vector[i].cache;
			args[i]->filter = (filter_obj->is_offline) ? NULL : fp_cp_vector[i].filter;
//			if(filter_obj->fs_base_active && filter_obj->is_offline == false)
			if(filter_obj->is_offline == false)
				args[i]->fs_base = fp_cp_vector[i].fs_base;
			else
				args[i]->fs_base = NULL;
			fp_cp_vector[i].fp_cp->filter_pre(args[i]);
		}

		y_flow = new QAtomicInt(0);
		for(int i = 0; i < cores; i++) {
			tasks[i] = new task_t;
			tasks[i]->flow_index = i;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = area_out;
			tasks[i]->y_flow = y_flow;
			tasks[i]->destructive = destructive;
			tasks[i]->filter_args = args;
		}
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	// run fp_cp_list FP_CP::filter()
	process(subflow);

	subflow->sync_point();
	if(subflow->is_master()) {
		for(int i = 0; i < size; i++) {
			fp_cp_vector[i].fp_cp->filter_post(args[i]);
			delete[] args[i]->ptr_private;
			delete args[i];
		}
		//
		for(int i = 0; i < subflow->cores(); i++)
			delete tasks[i];
		delete[] tasks;
		delete y_flow;
	}
	subflow->sync_point();
	delete[] args;
//	if(subflow->is_master())
//		cerr << "wrapper process: done" << endl;
	return area_out;
}

void FilterProcess_CP_Wrapper::process(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();

	int in_width = task->area_in->mem_width();
	int out_width = task->area_out->mem_width();

	int in_mx1 = task->area_in->dimensions()->edges.x1;
	int in_my1 = task->area_in->dimensions()->edges.y1;
	int out_mx1 = task->area_out->dimensions()->edges.x1;
	int out_my1 = task->area_out->dimensions()->edges.y1;
	int x_max = task->area_out->dimensions()->width();
	int y_max = task->area_out->dimensions()->height();

	float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	int j = 0;
	int size = fp_cp_vector.size();
	Mem m_mt(4 * sizeof(float));
	float *mt = (float *)m_mt.ptr();
	while((j = _mt_qatom_fetch_and_add(task->y_flow, 1)) < y_max) {
		int it_in = ((j + in_my1) * in_width + in_mx1) * 4;
		if(task->destructive) {
			for(int i = 0; i < x_max; i++) {
				if(in[it_in + 3] > 0.0)
					for(int s = 0; s < size; s++)
						fp_cp_vector[s].fp_cp->filter(&in[it_in], task->filter_args[s]->ptr_private[task->flow_index]);
				it_in += 4;
			}
		} else {
			int it_out = ((j + out_my1) * out_width + out_mx1) * 4;
			for(int i = 0; i < x_max; i++) {
				mt[0] = in[it_in + 0];
				mt[1] = in[it_in + 1];
				mt[2] = in[it_in + 2];
				mt[3] = in[it_in + 3];
				if(mt[3] > 0.0)
					for(int s = 0; s < size; s++)
						fp_cp_vector[s].fp_cp->filter(mt, (void *)task->filter_args[s]->ptr_private[task->flow_index]);
				out[it_out + 0] = mt[0];
				out[it_out + 1] = mt[1];
				out[it_out + 2] = mt[2];
				out[it_out + 3] = mt[3];
				it_in += 4;
				it_out += 4;
			}
		}
	}
}

//------------------------------------------------------------------------------
