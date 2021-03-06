/*
 * f_CM_to_CS.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTE:
	- filter is widgetless and ps-less; is controlled via mutators;
	- used mutators:
		"CM" -> string: "CIECAM02" | "CIELab"
		"CM_ocs" -> string: name of the output color space
		"CM_compress_saturation" -> bool
		"CM_compress_saturation_factor" -> double
		"CM_compress_strength" -> double
		"CM_desaturation_strength" -> double
	- auto determined saturation compression factor stored in filter's cache 'FP_Cache_t *'

 */

#include <iostream>
#include <math.h>

#include "f_cm_to_cs.h"
#include "filter_cp.h"
#include "ddr_math.h"
#include "cm.h"
#include "cms_matrix.h"
#include "sgt.h"

using namespace std;

#define DEFAULT_OUTPUT_COLOR_SPACE	"HDTV"

#define SMAX_LENGTH 300

//------------------------------------------------------------------------------
class FP_CM_to_CS : public virtual FilterProcess_CP, public virtual FilterProcess_2D {
protected:
	class task_t;

public:
	FP_CM_to_CS(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	FilterProcess::fp_type_en fp_type(bool process_thumb);
	virtual void *get_ptr(bool process_thumbnail);

	// shared between 2D and CP
	std::vector<class task_t *> task_prepare(int threads_count, class DataSet *mutators, class DataSet *mutators_multipass, bool do_analyze, class FP_CM_to_CS_Cache_t *fp_cache);
	void task_release(std::vector<class task_t *> tasks, int threads_count, class DataSet *mutators_multipass, bool do_analyze, class FP_CM_to_CS_Cache_t *fp_cache);

	// FilterProcess_CP
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, fp_cp_task_t *fp_cp_task);
	void filter_post(fp_cp_args_t *args);

	// FilterProcess_2D
	std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

};

//------------------------------------------------------------------------------
FP_CM_to_CS *F_CM_to_CS::fp = nullptr;

F_CM_to_CS::F_CM_to_CS(int id) : Filter() {
	filter_id = id;
	_id = "F_CM_to_CS";
	_name = "F_CM_to_CS";
	_is_hidden = true;
	if(fp == nullptr)
		fp = new FP_CM_to_CS();
}

F_CM_to_CS::~F_CM_to_CS() {
}

void F_CM_to_CS::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	ps_base = new_ps;
}

//------------------------------------------------------------------------------
Filter::type_t F_CM_to_CS::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_to_CS::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FP_CM_to_CS_Cache_t : public FP_Cache_t {
public:
	FP_CM_to_CS_Cache_t(void);
	bool compression_factor_defined;
	double compression_factor;
};

FP_CM_to_CS_Cache_t::FP_CM_to_CS_Cache_t(void) {
	compression_factor_defined = false;
	compression_factor = 1.0;
}

class FP_CM_to_CS::task_t : public fp_cp_task_t {
public:
	TableFunction *gamma;
	float cmatrix[9];
	bool to_skip;
	std::shared_ptr<CM> cm;
	CM_Convert *cm_convert;
	std::shared_ptr<Saturation_Gamut> sg;
	float compress_saturation_factor;
	float compress_strength;
	float desaturation_strength;

	int pixels_count;
	std::vector<int> smax_count = std::vector<int>(0);
	std::vector<float> smax_value = std::vector<float>(0);

	// 2D
	Area *area_in;
	Area *area_out;
	std::atomic_int *y_flow_p1;
	std::atomic_int *y_flow_p2;
};

FP_CM_to_CS::FP_CM_to_CS(void) : FilterProcess_CP() {
	_name = "F_CM_to_CS_CP";
}

FP_Cache_t *FP_CM_to_CS::new_FP_Cache(void) {
//	return new FP_Cache_t;
	return new FP_CM_to_CS_Cache_t;
}

bool FP_CM_to_CS::is_enabled(const PS_Base *ps_base) {
	return true;
}

FilterProcess::fp_type_en FP_CM_to_CS::fp_type(bool process_thumb) {
	if(process_thumb)
		return FilterProcess::fp_type_2d;
	return FilterProcess::fp_type_cp;
}

void *FP_CM_to_CS::get_ptr(bool process_thumb) {
	if(process_thumb)
		return (void *)((FilterProcess_2D *)this);
	return (void *)((FilterProcess_CP *)this);
}

void FP_CM_to_CS::filter_pre(fp_cp_args_t *args) {
	std::vector<class task_t *> tasks = task_prepare(args->threads_count, args->mutators, args->mutators_multipass, false, (FP_CM_to_CS_Cache_t *)args->cache);
	for(int i = 0; i < args->threads_count; ++i)
		args->vector_private[i] = std::unique_ptr<fp_cp_task_t>(tasks[i]);
}

std::vector<class FP_CM_to_CS::task_t *> FP_CM_to_CS::task_prepare(int threads_count, class DataSet *mutators, class DataSet *mutators_multipass, bool do_analyze, FP_CM_to_CS_Cache_t *fp_cache) {
	string cm_name;
	mutators->get("CM", cm_name);
	CM::cm_type_en cm_type = CM::get_type(cm_name);
	string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
	mutators->get("CM_ocs", ocs_name);
	bool compress_saturation = false;
	mutators->get("CM_compress_saturation", compress_saturation);
	double compress_saturation_factor = 1.0;
	mutators_multipass->get("CM_compress_saturation_factor", compress_saturation_factor);
	if(fp_cache != nullptr)
		if(fp_cache->compression_factor_defined)
			compress_saturation_factor = fp_cache->compression_factor;
	double compress_strength = 0.0;
	mutators_multipass->get("CM_compress_strength", compress_strength);
	double desaturation_strength = 1.0;
	mutators_multipass->get("CM_desaturation_strength", desaturation_strength);

	// general calculation
	// default - XYZ
	float m_xyz_to_output[9] = {
		1.0, 0.0, 0.0,
		0.0, 1.0, 0.0,
		0.0, 0.0, 1.0};

	// input - Jsh at E
	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	cms_matrix->get_matrix_XYZ_to_CS(ocs_name, m_xyz_to_output);
//	for(int i = 0; i < 9; ++i)
//		matrix[i] = m_xyz_to_output[i];
	TableFunction *gamma = cms_matrix->get_gamma(ocs_name);

	std::shared_ptr<Saturation_Gamut> sg(compress_saturation ? new Saturation_Gamut(cm_type, ocs_name) : nullptr);
	std::shared_ptr<CM> cm(CM::new_CM(cm_type, CS_White("E"), CS_White(cms_matrix->get_illuminant_name(ocs_name))));
	CM_Convert *cm_convert = cm->get_convert_Jsh_to_XYZ();
	std::vector<class task_t *> tasks(threads_count);
	for(int i = 0; i < threads_count; ++i) {
		task_t *task = new task_t;
		tasks[i] = task;

		task->gamma = gamma;
//		task->to_skip = to_skip;
		task->cm = cm;
		task->cm_convert = cm_convert;
		for(int j = 0; j < 9; ++j)
			task->cmatrix[j] = m_xyz_to_output[j];
//			task->cmatrix[j] = matrix[j];
		//--
//		task->saturation_max = 0.0;
		if(do_analyze) {
			task->smax_count.resize(SMAX_LENGTH, 0);
			task->smax_value.resize(SMAX_LENGTH, 0.0f);
		}
		task->pixels_count = 0;

		task->compress_saturation_factor = compress_saturation_factor;
		task->compress_strength = compress_strength;
		task->desaturation_strength = desaturation_strength;
		task->sg = sg;
	}
	return tasks;
}

void FP_CM_to_CS::filter_post(fp_cp_args_t *args) {
	std::vector<task_t *> tasks(args->vector_private.size());
	for(size_t i = 0; i < args->vector_private.size(); ++i)
		tasks[i] = (task_t *)(args->vector_private[i].get());
	task_release(tasks, args->threads_count, args->mutators_multipass, false, (FP_CM_to_CS_Cache_t *)args->cache);
}

void FP_CM_to_CS::task_release(std::vector<class task_t *> tasks, int threads_count, class DataSet *mutators_multipass, bool do_analyze, FP_CM_to_CS_Cache_t *fp_cache) {
	FP_CM_to_CS::task_t *task = tasks[0];

	if(do_analyze) {
		// determine saturation factor
		std::vector<int> smax_count(SMAX_LENGTH, 0);
		std::vector<float> smax_value(SMAX_LENGTH, 0.0f);
		long pixels_count = 0;
		for(int j = 0; j < threads_count; ++j) {
			task = tasks[j];
			pixels_count += task->pixels_count;
			for(int i = 0; i < SMAX_LENGTH; ++i) {
				smax_count[i] += task->smax_count[i];
				smax_value[i] = (smax_value[i] > task->smax_value[i]) ? smax_value[i] : task->smax_value[i];
			}
		}
		pixels_count /= (SMAX_LENGTH - 1);
		int c = 0;
		float smax_factor = 0.0;
		for(int i = 1; i < SMAX_LENGTH; ++i) {
			if(c >= pixels_count && smax_factor > 1.0)
				break;
			c += smax_count[i];
			if(smax_factor < smax_value[i])
				smax_factor = smax_value[i];
		}
// NOTE 1
		smax_factor = (smax_factor < 1.0) ? 1.0 : smax_factor;
		if(smax_factor > 0.0) {
			double _v;
			if(mutators_multipass->get("CM_compress_saturation_factor", _v) == false) {
				_v = smax_factor;
				mutators_multipass->set("CM_compress_saturation_factor", _v);
				if(fp_cache != nullptr) {
					fp_cache->compression_factor_defined = true;
					fp_cache->compression_factor = smax_factor;
				}
//cerr << "_____________________________________" << endl;
//cerr << "set CM_compress_saturation_factor == " << _v << endl;
			} else {
				fp_cache->compression_factor_defined = false;
			}
		}
	}
}

void FP_CM_to_CS::filter(float *pixel, fp_cp_task_t *fp_cp_task) {
	task_t *task = (task_t *)fp_cp_task;
//	if(task->to_skip)
//		return;
	// CIECAM02 to XYZ
	// normalize input
	if(pixel[0] > 1.0)	pixel[0] = 1.0;
//	if(pixel[0] < 0.0001)	pixel[0] = 0.0;
//	if(pixel[1] < 0.01)	pixel[0] = 0.0;
	if(pixel[2] >= 1.0)	pixel[2] -= 1.0;
	if(pixel[2] < 0.0)	pixel[2] += 1.0;
	// TODO: add two-pass thumbnail processing - first one to determine maximum saturation level, second - to apply it;
	// TODO: use real 'x_max' value;

	float const compress_strength = task->compress_strength;

	if(task->sg != nullptr) {
		// compress saturation
		// should we check brightness of that pixel or not
		float J_edge, s_edge;
		task->sg->lightness_edge_Js(J_edge, s_edge, pixel[2]);
//		float v_J = pixel[0];
//		float v_s = pixel[1];
		//
		float s1 = task->sg->saturation_limit(pixel[0], pixel[2]);
		float s2 = s1;
		if(pixel[0] > J_edge)
			s2 = s_edge;
//		float s_limit = task->sg->saturation_limit(pixel[0], pixel[2]);
		float s_limit = s1 + (s2 - s1) * (1.0 - task->desaturation_strength);
		if(s_limit > 0.001) {
			float s = compression_function(pixel[1] / s_limit, task->compress_saturation_factor) * s_limit;
			pixel[1] = s + (pixel[1] - s) * (1.0 - compress_strength);
		} else {
			pixel[1] = 0.0;
		}
		float J_max = task->sg->lightness_limit(pixel[1], pixel[2]);
		if(pixel[0] > J_max)
			pixel[0] = J_max + (pixel[0] - J_max) * (1.0 - compress_strength);
	}
	float XYZ[3];
	task->cm_convert->convert(XYZ, pixel);
	//-------------------
	// convert XYZ to RGB
	m3_v3_mult(pixel, task->cmatrix, XYZ);
	// apply gamma
	for(int i = 0; i < 3; ++i)
		pixel[i] = (*task->gamma)(pixel[i]);
}

//------------------------------------------------------------------------------
std::unique_ptr<Area> FP_CM_to_CS::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;

	DataSet *mutators = process_obj->mutators;
	DataSet *mutators_multipass = process_obj->mutators_multipass;
//	PS_CM_to_CS *ps = (PS_CM_to_CS *)(filter_obj->ps_base);
	FP_CM_to_CS_Cache_t *fp_cache = (FP_CM_to_CS_Cache_t *)process_obj->fp_cache;

	std::unique_ptr<Area> area_out;
	task_t *task = nullptr;
	std::vector<std::unique_ptr<task_t>> tasks(0);
	std::unique_ptr<std::atomic_int> y_flow_p1;
	std::unique_ptr<std::atomic_int> y_flow_p2;
	const int threads_count = subflow->threads_count();

	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;

		std::vector<class task_t *> v_tasks = task_prepare(threads_count, mutators, mutators_multipass, true, fp_cache);
		// TODO: check here destructive processing
		area_out = std::unique_ptr<Area>(new Area(*area_in));
		y_flow_p1 = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		y_flow_p2 = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			task = v_tasks[i];
			tasks[i] = std::unique_ptr<task_t>(task);

			task->area_in = area_in;
			task->area_out = area_out.get();
			task->y_flow_p1 = y_flow_p1.get();
			task->y_flow_p2 = y_flow_p2.get();
			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	// process
	task = (task_t *)subflow->get_private();

	//--
	float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();

	const int in_mx = task->area_in->dimensions()->edges.x1;
	const int in_my = task->area_in->dimensions()->edges.y1;
	const int out_mx = task->area_out->dimensions()->edges.x1;
	const int out_my = task->area_out->dimensions()->edges.y1;
	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

	int y;
	// pass 1: collect information, get histogram;
	while((y = task->y_flow_p1->fetch_add(1)) < y_max) {
		int in_index = ((y + in_my) * in_width + in_mx) * 4;
		int out_index = ((y + out_my) * out_width + out_mx) * 4;
		for(int x = 0; x < x_max; ++x) {
			float *pixel = &in[in_index];
			//--
			if(pixel[0] > 1.0)	pixel[0] = 1.0;
			if(pixel[2] >= 1.0)	pixel[2] -= 1.0;
			if(pixel[2] < 0.0)	pixel[2] += 1.0;
			if(task->sg != nullptr && pixel[3] > 0.01) {
				// compress saturation
				float J_edge, s_edge;
				task->sg->lightness_edge_Js(J_edge, s_edge, pixel[2]);
				float s_max = s_edge;
				if(pixel[0] <= J_edge)
					s_max = task->sg->saturation_limit(pixel[0], pixel[2]);
				// TODO: check histograms
				int index = 0;
				float _J = (pixel[0] < J_edge) ? pixel[0] : J_edge;
//				index = ((J_edge - _J) / J_edge) * 100 + 1;
//				if(index > 0 && index <= 100) {
				index = ((J_edge - _J) / J_edge) * (SMAX_LENGTH - 1) + 1;
				if(index > 0 && index <= (SMAX_LENGTH - 1)) {
//				ddr::clip(index, 0, 100);
/*
				if(pixel[0] < J_edge) {
					index = ((J_edge - pixel[0]) / J_edge) * 100 + 1;
					if(index > 100)	index = 100;
				}
*/
					++task->smax_count[index];
					if(task->smax_value[index] < pixel[1] / s_max)
						task->smax_value[index] = pixel[1] / s_max;
					++task->pixels_count;
				}
			}
			in_index += 4;
			out_index += 4;
		}
	}

	// master thread: analyze information, determine compression factor
	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;

		std::vector<task_t *> v_tasks(threads_count);
		for(int i = 0; i < threads_count; ++i)
			v_tasks[i] = (task_t *)tasks[i].get();
		task_release(v_tasks, threads_count, mutators_multipass, true, fp_cache);
		v_tasks = task_prepare(threads_count, mutators, mutators_multipass, false, fp_cache);
		// TODO: check here destructive processing
		for(int i = 0; i < threads_count; ++i) {
			task = v_tasks[i];
			tasks[i] = std::unique_ptr<task_t>(task);

			task->area_in = area_in;
			task->area_out = area_out.get();
			task->y_flow_p1 = y_flow_p1.get();
			task->y_flow_p2 = y_flow_p2.get();
			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	// pass 2: apply compression with CP process function
	task = (task_t *)subflow->get_private();
	while((y = task->y_flow_p2->fetch_add(1)) < y_max) {
		int in_index = ((y + in_my) * in_width + in_mx) * 4;
		int out_index = ((y + out_my) * out_width + out_mx) * 4;
		for(int x = 0; x < x_max; ++x) {
			float *pixel = &in[in_index];
			filter(pixel, task);
			out[out_index + 0] = in[in_index + 0];
			out[out_index + 1] = in[in_index + 1];
			out[out_index + 2] = in[in_index + 2];
			out[out_index + 3] = in[in_index + 3];
//			out[out_index + 1] = 0.0;
			in_index += 4;
			out_index += 4;
		}
	}

	// clean up
	if(subflow->sync_point_pre()) {
		std::vector<task_t *> v_tasks(threads_count);
		for(int i = 0; i < threads_count; ++i)
			v_tasks[i] = (task_t *)tasks[i].get();
		task_release(v_tasks, threads_count, mutators_multipass, false, fp_cache);
	}
	subflow->sync_point_post();

	return area_out;
}
//------------------------------------------------------------------------------
