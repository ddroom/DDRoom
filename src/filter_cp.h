#ifndef __H_FILTER_CP__
#define __H_FILTER_CP__
/*
 * filter_cp.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QSharedPointer>

#include "filter.h"

//------------------------------------------------------------------------------
// cp == color process 'per pixel'
// per-pixel color filter initialization and processing
class fp_cp_args_t {
public:
	class Metadata *metadata;
	DataSet *mutators;			// <- , \|/ - rename to "mutators_ro" and "mutators_rw"
	DataSet *mutators_mpass; // will keep data between thumb and scaled processing
	class PS_Base *ps_base;
	void **ptr_private;
	int cores;
	class FP_Cache_t *cache;
	class Filter *filter;
	class FS_Base *fs_base;
};
typedef fp_cp_args_t* fp_cp_args_t_ptr;

class FilterProcess_CP : public virtual FilterProcess {
public:
	virtual FilterProcess::fp_type_en fp_type(bool process_thumbnail) {return _fp_type;}
//	FilterProcess::fp_type_en fp_type(void);
	FilterProcess_CP(void);
	virtual ~FilterProcess_CP() {};
	virtual FP_Cache_t *new_FP_Cache(void);
	virtual void *get_ptr(bool process_thumbnail) {return (void *)this;};	// NOTE: can be used dynamic_cast<> instead

	// create cache, save parameters into it; called only for master subflow, before filter processing
	virtual void filter_pre(class fp_cp_args_t *args);
	// do per-pixel filtering; use prepared in cache tables, parameters etc; save histogram data to cache
	//	pixel - float[4] - in and out pixel, rewritable
	//	void *data - per-subflow data and cache
	virtual void filter(float *pixel, void *data) {};
	// reconstruct histogram, clear cache and delete private task_t objects; called only with master subflow
	virtual void filter_post(class fp_cp_args_t *args);
protected:
};

// 'frame' call FP_P3's chain to process each pixel, incapsulate 2D matrix iteration
class FP_CP_Wrapper_record_t {
public:
	class Filter *filter;
	class FilterProcess_CP *fp_cp;
//	ddr_shared_ptr<PS_Base> ps_base;
	QSharedPointer<PS_Base> ps_base;
	class FP_Cache_t *cache;
	class FS_Base *fs_base;
};

class FilterProcess_CP_Wrapper : public FilterProcess_2D {
public:
	FilterProcess_CP_Wrapper(const std::vector<class FP_CP_Wrapper_record_t> &);
	virtual ~FilterProcess_CP_Wrapper(void);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	bool is_enabled(const PS_Base *ps_base);
	void set_destructive(bool);

protected:
	class task_t;
	std::vector<class FP_CP_Wrapper_record_t> fp_cp_vector;
	void process(class SubFlow *subflow);
	bool allow_destructive;
};

//------------------------------------------------------------------------------
#endif //__H_FILTER_CP__
