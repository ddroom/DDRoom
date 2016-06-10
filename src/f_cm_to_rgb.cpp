/*
 * f_cm_to_rgb.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
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

 * TODO:
	- apply smooth gamma table
	- rename from CM_to_RGB to CM_to_CS;

 */

#include <iostream>
#include <math.h>

#include "f_cm_to_rgb.h"
#include "filter_cp.h"
#include "ddr_math.h"
#include "cm.h"
#include "cms_matrix.h"
#include "sgt.h"

using namespace std;

#define DEFAULT_OUTPUT_COLOR_SPACE	"HDTV"

//------------------------------------------------------------------------------
class FP_CM_to_RGB : public virtual FilterProcess_CP, public virtual FilterProcess_2D {
protected:
	class task_t;

public:
	FP_CM_to_RGB(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	FilterProcess::fp_type_en fp_type(bool process_thumb);
	virtual void *get_ptr(bool process_thumbnail);

	// shared between 2D and CP
//	void **task_prepare(int cores, class DataSet *mutators, class DataSet *mutators_mpass, bool do_analyze, class FP_CM_to_RGB_Cache_t *fp_cache);
	QVector<class task_t *> task_prepare(int cores, class DataSet *mutators, class DataSet *mutators_mpass, bool do_analyze, class FP_CM_to_RGB_Cache_t *fp_cache);
	void task_release(void **tasks, int cores, class DataSet *mutators_mpass, bool do_analyze, class FP_CM_to_RGB_Cache_t *fp_cache);

	// FilterProcess_CP
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, void *data);
	void filter_post(fp_cp_args_t *args);

	// FilterProcess_2D
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

};

//------------------------------------------------------------------------------
FP_CM_to_RGB *F_CM_to_RGB::fp = nullptr;

F_CM_to_RGB::F_CM_to_RGB(int id) : Filter() {
	filter_id = id;
	_id = "F_CM_to_RGB";
	_name = "F_CM_to_RGB";
	_is_hidden = true;
	if(fp == nullptr)
		fp = new FP_CM_to_RGB();
}

F_CM_to_RGB::~F_CM_to_RGB() {
}

void F_CM_to_RGB::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	ps_base = new_ps;
}

//------------------------------------------------------------------------------
Filter::type_t F_CM_to_RGB::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_to_RGB::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FP_CM_to_RGB_Cache_t : public FP_Cache_t {
public:
	FP_CM_to_RGB_Cache_t(void);
	bool compression_factor_defined;
	double compression_factor;
};

FP_CM_to_RGB_Cache_t::FP_CM_to_RGB_Cache_t(void) {
	compression_factor_defined = false;
	compression_factor = 1.0;
}

class FP_CM_to_RGB::task_t {
public:
	TableFunction *gamma;
	float cmatrix[9];
	bool to_skip;
	CM *cm;
	CM_Convert *cm_convert;
	Saturation_Gamut *sg;
	float compress_saturation_factor;
	float compress_strength;
	float desaturation_strength;

	int pixels_count;
	int *smax_count;
	float *smax_value;
//	float saturation_max;
//	float sm_j;

	// 2D
	Area *area_in;
	Area *area_out;
	std::atomic_int *flow_p1;
	std::atomic_int *flow_p2;
};

FP_CM_to_RGB::FP_CM_to_RGB(void) : FilterProcess_CP() {
	_name = "F_CM_to_RGB_CP";
}

FP_Cache_t *FP_CM_to_RGB::new_FP_Cache(void) {
//	return new FP_Cache_t;
	return new FP_CM_to_RGB_Cache_t;
}

bool FP_CM_to_RGB::is_enabled(const PS_Base *ps_base) {
	return true;
}

FilterProcess::fp_type_en FP_CM_to_RGB::fp_type(bool process_thumb) {
	if(process_thumb)
		return FilterProcess::fp_type_2d;
	return FilterProcess::fp_type_cp;
}

void *FP_CM_to_RGB::get_ptr(bool process_thumb) {
	if(process_thumb)
		return (void *)((FilterProcess_2D *)this);
	return (void *)((FilterProcess_CP *)this);
}

void FP_CM_to_RGB::filter_pre(fp_cp_args_t *args) {
//	void **tasks = task_prepare(args->cores, args->mutators, args->mutators_mpass, false, (FP_CM_to_RGB_Cache_t *)args->cache);
//	void *tasks[args->cores];
//	task_prepare(tasks, args->cores, args->mutators, args->mutators_mpass, false, (FP_CM_to_RGB_Cache_t *)args->cache);
	QVector<class task_t *> tasks = task_prepare(args->cores, args->mutators, args->mutators_mpass, false, (FP_CM_to_RGB_Cache_t *)args->cache);
	for(int i = 0; i < args->cores; ++i)
		args->ptr_private[i] = tasks[i];
//		args->ptr_private[i] = tasks[i];
//	delete[] tasks;
// somewhere here - crash...
}

//void **FP_CM_to_RGB::task_prepare(int cores, class DataSet *mutators, class DataSet *mutators_mpass, bool do_analyze, FP_CM_to_RGB_Cache_t *fp_cache) {
//void FP_CM_to_RGB::task_prepare(void **tasks, int cores, class DataSet *mutators, class DataSet *mutators_mpass, bool do_analyze, FP_CM_to_RGB_Cache_t *fp_cache) {
QVector<class FP_CM_to_RGB::task_t *> FP_CM_to_RGB::task_prepare(int cores, class DataSet *mutators, class DataSet *mutators_mpass, bool do_analyze, FP_CM_to_RGB_Cache_t *fp_cache) {
	string cm_name;
	mutators->get("CM", cm_name);
	CM::cm_type_en cm_type = CM::get_type(cm_name);
//	bool _enabled = (cm_type == CM::cm_type_CIECAM02 || cm_type == CM::cm_type_CIELab);
	string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
	mutators->get("CM_ocs", ocs_name);
	bool compress_saturation = false;
	mutators->get("CM_compress_saturation", compress_saturation);
	double compress_saturation_factor = 1.0;
	mutators_mpass->get("CM_compress_saturation_factor", compress_saturation_factor);
//	if(mutators_mpass->get("CM_compress_saturation_factor", compress_saturation_factor) == false)
//		compress_saturation_factor = 1.0;
	if(fp_cache != nullptr)
		if(fp_cache->compression_factor_defined)
			compress_saturation_factor = fp_cache->compression_factor;
//cerr << "compress_saturation_factor == " << compress_saturation_factor << endl;
	double compress_strength = 0.0;
	mutators_mpass->get("CM_compress_strength", compress_strength);
//	if(mutators_mpass->get("CM_compress_strength", compress_strength) == false)
//		compress_strength = 0.0;
	double desaturation_strength = 1.0;
	mutators_mpass->get("CM_desaturation_strength", desaturation_strength);
//	if(mutators_mpass->get("CM_desaturation_strength", desaturation_strength) == false)
//		desaturation_strength = 0.0;

	// shared parameters
//	float matrix[9];

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

	Saturation_Gamut *sg = nullptr;
	if(compress_saturation)
		sg = new Saturation_Gamut(cm_type, ocs_name);
	CM *cm = CM::new_CM(cm_type, CS_White("E"), CS_White(cms_matrix->get_illuminant_name(ocs_name)));
	CM_Convert *cm_convert = cm->get_convert_Jsh_to_XYZ();
//	void **tasks = new void *[cores];
	QVector<class task_t *> tasks(cores);
	for(int i = 0; i < cores; ++i) {
		task_t *task = new task_t;
		task->gamma = gamma;
//		task->to_skip = to_skip;
		task->cm = cm;
		task->cm_convert = cm_convert;
		for(int j = 0; j < 9; ++j)
			task->cmatrix[j] = m_xyz_to_output[j];
//			task->cmatrix[j] = matrix[j];
		//--
//		task->saturation_max = 0.0;
		task->smax_count = nullptr;
		task->smax_value = nullptr;
		if(do_analyze) {
			task->smax_count = new int[101];
			task->smax_value = new float[101];
			for(int j = 0; j < 101; ++j) {
				task->smax_count[j] = 0;
				task->smax_value[j] = 0.0;
			}
		}
		task->pixels_count = 0;

		task->compress_saturation_factor = compress_saturation_factor;
		task->compress_strength = compress_strength;
		task->desaturation_strength = desaturation_strength;
		task->sg = sg;
		//--
		tasks[i] = task;
//		tasks[i] = (void *)task;
//		args->ptr_private[i] = (void *)task;
	}
	return tasks;
//	return tasks;
}

void FP_CM_to_RGB::filter_post(fp_cp_args_t *args) {
	task_release(args->ptr_private, args->cores, args->mutators_mpass, false, (FP_CM_to_RGB_Cache_t *)args->cache);
}

//void FP_CM_to_RGB::filter_post(fp_cp_args_t *args) {
void FP_CM_to_RGB::task_release(void **tasks, int cores, class DataSet *mutators_mpass, bool do_analyze, FP_CM_to_RGB_Cache_t *fp_cache) {
	FP_CM_to_RGB::task_t *t = (FP_CM_to_RGB::task_t *)tasks[0];
	if(t->sg != nullptr)
		delete t->sg;

	if(do_analyze) {
		// determine saturation factor
		int   smax_count[101];
		float smax_value[101];
		for(int i = 0; i < 101; ++i) {
			smax_count[i] = 0;
			smax_value[i] = 0.0;
		}
		int pixels_count = 0;
		for(int j = 0; j < cores; ++j) {
			t = (FP_CM_to_RGB::task_t *)tasks[j];
			pixels_count += t->pixels_count;
			for(int i = 0; i < 101; ++i) {
				smax_count[i] += t->smax_count[i];
				smax_value[i] = (smax_value[i] > t->smax_value[i]) ? smax_value[i] : t->smax_value[i];
			}
		}
		pixels_count /= 100;
		int c = 0;
		float smax_factor = 0.0;
//int index = 0;
		for(int i = 1; i < 101; ++i) {
//index = i;
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
			if(mutators_mpass->get("CM_compress_saturation_factor", _v) == false) {
				_v = smax_factor;
				mutators_mpass->set("CM_compress_saturation_factor", _v);
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
	for(int j = 0; j < cores; ++j) {
		t = (FP_CM_to_RGB::task_t *)tasks[j];
		if(t->smax_count)
			delete[] t->smax_count;
		if(t->smax_value)
			delete[] t->smax_value;
		delete t;
	}
}

void FP_CM_to_RGB::filter(float *pixel, void *data) {
	task_t *task = (task_t *)data;
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
			pixel[1] = s + (pixel[1] - s) * (1.0 - task->compress_strength);
		} else {
			pixel[1] = 0.0;
		}
		float J_max = task->sg->lightness_limit(pixel[1], pixel[2]);
		if(pixel[0] > J_max)
			pixel[0] = J_max + (pixel[0] - J_max) * (1.0 - task->compress_strength);
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
Area *FP_CM_to_RGB::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	DataSet *mutators = process_obj->mutators;
	DataSet *mutators_mpass = process_obj->mutators_mpass;
//	PS_CM_to_RGB *ps = (PS_CM_to_RGB *)(filter_obj->ps_base);
	FP_CM_to_RGB_Cache_t *fp_cache = (FP_CM_to_RGB_Cache_t *)process_obj->fp_cache;

	Area *_area_out = nullptr;
	task_t **tasks = nullptr;
	std::atomic_int *_flow_p1 = nullptr;
	std::atomic_int *_flow_p2 = nullptr;
	int cores = subflow->cores();

	if(subflow->sync_point_pre()) {
//cerr << "Area *FP_CM_to_RGB::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj)" << endl;
//		void *t_tasks[cores];
//		task_prepare((void **)&t_tasks, cores, mutators, mutators_mpass, true, fp_cache);
		QVector<class task_t *> t_tasks = task_prepare(cores, mutators, mutators_mpass, true, fp_cache);
//		void **t_tasks = task_prepare(cores, mutators, mutators_mpass, true, fp_cache);
//		for(int i = 0; i < args->cores; ++i)
//			args->ptr_private[i] = t_tasks[i];
//	void **FP_CM_to_RGB::task_prepare(int cores, class DataSet *mutators, class DataSet *mutators_mpass) {
		// TODO: check here destructive processing
		_area_out = new Area(*area_in);
D_AREA_PTR(_area_out)
		_flow_p1 = new std::atomic_int(0);
		_flow_p2 = new std::atomic_int(0);
		tasks = new task_t *[cores];
		for(int i = 0; i < cores; ++i) {
//			tasks[i] = (FP_CM_to_RGB::task_t *)t_tasks[i];
			tasks[i] = t_tasks[i];
//			tasks[i] = new task_t;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = _area_out;
			tasks[i]->flow_p1 = _flow_p1;
			tasks[i]->flow_p2 = _flow_p2;
			//--
		}
//		delete[] t_tasks;
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	// process
	task_t *task = (task_t *)subflow->get_private();

//	float *matrix = task->cmatrix;
//	TableFunction *gamma = task->gamma;
//	const float *m = matrix;

	//--
	float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	int x_max = task->area_out->dimensions()->width();
	int y_max = task->area_out->dimensions()->height();

	int in_mx = task->area_in->dimensions()->edges.x1;
	int in_my = task->area_in->dimensions()->edges.y1;
	int out_mx = task->area_out->dimensions()->edges.x1;
	int out_my = task->area_out->dimensions()->edges.y1;
	int in_width = task->area_in->mem_width();
	int out_width = task->area_out->mem_width();
/*
	float saturation = 1.0;
	if(ps->enabled_saturation && ps->saturation != 1.0)
		saturation = ps->saturation;
*/		

	int y;
	// pass 1: collect information, get histogram;
	while((y = task->flow_p1->fetch_add(1)) < y_max) {
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
				index = ((J_edge - _J) / J_edge) * 100 + 1;
				if(index > 0 && index <= 100) {
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
		task_release((void **)tasks, cores, mutators_mpass, true, fp_cache);
		//--
//		void *t_tasks[cores];
//		task_prepare((void **)t_tasks, cores, mutators, mutators_mpass, false, fp_cache);
		QVector<class task_t *> t_tasks = task_prepare(cores, mutators, mutators_mpass, false, fp_cache);
//		void **t_tasks = task_prepare(cores, mutators, mutators_mpass, false, fp_cache);
		// TODO: check here destructive processing
		tasks = new task_t *[cores];
		for(int i = 0; i < cores; ++i) {
//			tasks[i] = (FP_CM_to_RGB::task_t *)t_tasks[i];
			tasks[i] = t_tasks[i];
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = _area_out;
			tasks[i]->flow_p1 = _flow_p1;
			tasks[i]->flow_p2 = _flow_p2;
		}
//		delete[] t_tasks;
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	// pass 2: apply compression with CP process function
	task = (task_t *)subflow->get_private();
	while((y = task->flow_p2->fetch_add(1)) < y_max) {
		int in_index = ((y + in_my) * in_width + in_mx) * 4;
		int out_index = ((y + out_my) * out_width + out_mx) * 4;
		for(int x = 0; x < x_max; ++x) {
			float *pixel = &in[in_index];
			filter(pixel, (void *)task);
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
		task_release((void **)tasks, cores, mutators_mpass, false, fp_cache);
		delete _flow_p1;
		delete _flow_p2;
//		for(int i = 0; i < subflow->cores(); ++i)
//			delete tasks[i];
		delete[] tasks;
	}
	subflow->sync_point_post();

	return _area_out;
}
//------------------------------------------------------------------------------
