#ifndef __H_AREA_HELPER__
#define __H_AREA_HELPER__
/*
 * area_helper.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include "area.h"

//------------------------------------------------------------------------------
class AreaHelper {
public:
	static Area *convert(Area *in, Area::format_t out_format, int rotation);
	static Area *convert_mt(class SubFlow *subflow, Area *in, Area::format_t out_format, int rotation);

	static Area *crop(Area *in, Area::t_dimensions crop);
	static Area *rotate(Area *in, int rotation);

protected:
	class mt_task_t;
	static void f_convert_mt(class SubFlow *subflow);
	static void f_crop_mt(class SubFlow *subflow);
};

//------------------------------------------------------------------------------
#endif //__H_VIEW_HELPER__
