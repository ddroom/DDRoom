#ifndef __H_AREA_HELPER__
#define __H_AREA_HELPER__
/*
 * area_helper.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include "area.h"

//------------------------------------------------------------------------------
class AreaHelper {
public:
	static std::unique_ptr<Area> convert(class Area *in, Area::format_t out_format, int rotation);
	static std::unique_ptr<Area> convert_mt(class SubFlow *subflow, class Area *in, Area::format_t out_format, int rotation, class Area *tiled_area = nullptr, int pos_x = 0, int pos_y = 0);

protected:
	class mt_task_t;
	static void f_convert_mt(class SubFlow *subflow);
	static void f_crop_mt(class SubFlow *subflow);
};

//------------------------------------------------------------------------------
#endif //__H_AREA_HELPER__
