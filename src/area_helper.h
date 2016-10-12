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
	static class Area *convert(class Area *in, Area::format_t out_format, int rotation);
	static class Area *convert_mt(class SubFlow *subflow, class Area *in, Area::format_t out_format, int rotation, class Area *tiled_area = nullptr, int pos_x = 0, int pos_y = 0);

	static class Area *crop(class Area *in, class Area::t_dimensions crop);
	static class Area *rotate(class Area *in, int rotation);
	// Insert area 'tile' into 'insert_into' from position (pos_x,pos_y) of the actual data (i.e. w/o edges);
	// returns 'true' if there was cropping issues. Areas should be 'RGBA float'.
	static bool insert(class Area *insert_into, class Area *tile, int pos_x, int pos_y);

protected:
	class mt_task_t;
	static void f_convert_mt(class SubFlow *subflow);
	static void f_crop_mt(class SubFlow *subflow);
};

//------------------------------------------------------------------------------
#endif //__H_AREA_HELPER__
