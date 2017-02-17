#ifndef __H_SGT__
#define __H_SGT__

/*
 * sgt.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <string>
#include <map>
#include <mutex>
#include "cms_matrix.h"
#include "cm.h"

//------------------------------------------------------------------------------
class Saturation_Gamut {
public:
	Saturation_Gamut(CM::cm_type_en _cm_type, std::string _cs_name);
	~Saturation_Gamut();
	// s = f(J,h) for saturation compression
	float saturation_limit(float J, float h);
	// J = f(s,h) for lightness clipping
	float lightness_limit(float s, float h);
	// return J,s from where J = f(s,h) = const with ascending 's' and constant 'h'
	void lightness_edge_Js(float &J, float &s, float h);
	bool is_empty(void);

protected:
	class CM_to_CS *cm_to_cs;
	class CS_to_CM *cs_to_cm;

	enum CM::cm_type_en cm_type;
	std::string cs_name;

	void generate(void);
	void generate_s_limits(float *, const int);
	void generate_SGT(void);
	float search_s_bright(float s_limit, float _j, float _h, float _s_start, float _s_step);
	float search_s_dark(float s_limit, float _j, float _h, float _s_start, float _s_step);
	float search_j(float _s, float _h, float _j_start, float _j_step);

	//cache for saturation table
	static std::mutex cache_lock;
	class gamut_table_t;
	static std::map<std::string, class gamut_table_t *> map_cache;

	void(*f_cm_to_XYZ)(float *, const float *);
	class gamut_table_t *gamut_table;

	// file .sgt
	void _sgt_save(void);
	bool _sgt_load(CM::cm_type_en _cm_type, std::string _cs_name);
};

//------------------------------------------------------------------------------
#endif // __H_SGT__
