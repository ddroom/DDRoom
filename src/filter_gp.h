#ifndef __H_FILTER_GP__
#define __H_FILTER_GP__
/*
 * filter_gp.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <memory>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class FP_GP_data_t {
public:
	class Metadata *metadata;
	class Filter *filter;
	class FS_Base *fs_base;
	// TODO: check how insecure that could be
	class PS_Base *ps_base;
//	std::shared_ptr<PS_Base> ps_base;
	class FP_Cache_t *cache;
};

class FP_GP {
public:
	virtual ~FP_GP() {};
	virtual bool is_rgb(void);
	// Return 'false' (default behavior) if there is no need to clip size,
	// or 'true' like for filters 'Chromatic Aberration', or 'Distortion' with the user's choise.
	virtual bool to_clip(void);
	// process coordinates for all colors at the same time
	// forward - normal flow of function; used to determine resulting size of photo after processing with scale 1:1.
	// backward - f()-1 - i.e. reconstruct argument from known result of 'forward' function;
	//  used to determine input scale of photo to get desired output scale; and do actual processing with backward ray-tracing
	virtual void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	virtual void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);
	// process coordinates for each color (of three) separatelly; should be used for "Chromatic Aberration" etc...
	// data format: color coordinates as pointers on arrays float rgb_c[6] used as:
	//  rgb_c[0], rgb_c[1]) - (x, y) for RED
	//  rgb_c[2], rgb_c[3]) - (x, y) for GREEN
	//  rgb_c[4], rgb_c[5]) - (x, y) for BLUE
	virtual void process_forward_rgb(const float *in, float *out);
	virtual void process_backward_rgb(float *in, const float *out);
};

class FilterProcess_GP : public virtual FilterProcess {
public:
	virtual FilterProcess::fp_type_en fp_type(bool process_thumbnail) {return _fp_type;}
	FilterProcess_GP(void);
	virtual ~FilterProcess_GP() {};
	virtual FP_Cache_t *new_FP_Cache(void);
	virtual void *get_ptr(bool process_thumbnail) {return (void *)this;};	// NOTE: can be used dynamic_cast<> instead

	virtual class FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
};

class FP_GP_Wrapper_record_t {
public:
	class Filter *filter;
	class FilterProcess_GP *fp_gp;
	std::shared_ptr<PS_Base> ps_base;
	class FP_Cache_t *cache;
	class FS_Base *fs_base;
};

class FilterProcess_GP_Wrapper : public FilterProcess_2D {
public:
	FilterProcess_GP_Wrapper(const std::vector<class FP_GP_Wrapper_record_t> &);
	virtual ~FilterProcess_GP_Wrapper(void);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	bool is_enabled(const PS_Base *ps_base);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
	std::vector<class FP_GP_Wrapper_record_t> fp_gp_vector;
	std::vector<class FP_GP *> gp_vector;
	void init_gp(class Metadata *metadata);

	void size_forward_point(float in_x, float in_y, bool *flag_min_max, float *x_min_max, float *y_min_max);
	void size_backward_point(float out_x, float out_y, bool *flag_min_max, float *x_min_max, float *y_min_max);

	bool is_enabled_v(const PS_Base *ps_base);
	//--
	class task_coordinates_prep_t;
	class task_coordinates_t;
	class task_sampling_t;
	Area *process_sampling(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	void prepare_coordinates(SubFlow *subflow);
	void process_coordinates(SubFlow *subflow);
	void process_sampling(SubFlow *subflow);
	void process_sampling_sinc2(SubFlow *subflow);
	//--
	class task_copy_t;
	Area *process_copy(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	void process_copy(SubFlow *subflow);
};
//------------------------------------------------------------------------------
#endif //__H_FILTER_GP__
