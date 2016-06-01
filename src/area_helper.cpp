/*
 * area_helper.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <iostream>

#include "area_helper.h"
#include "mt.h"
#include "system.h"

using namespace std;

//------------------------------------------------------------------------------
Area *AreaHelper::convert(Area *area_in, Area::format_t out_format, int rotation) {
//cerr << "AreaHelper::convert(): rotation == " << rotation << endl;
//rotation = 0;
	Area *area_out = nullptr;
	Area::t_dimensions d_out;
	d_out.size.w = area_in->dimensions()->width();
	d_out.size.h = area_in->dimensions()->height();
	if(rotation == 90 || rotation == 270) {
		d_out.size.w = area_in->dimensions()->height();
		d_out.size.h = area_in->dimensions()->width();
	}

	if(out_format == Area::format_t::format_rgba_16) {
		area_out = new Area(&d_out, Area::type_t::type_int16_p4);
	} else if(out_format == Area::format_t::format_rgb_16) {
		area_out = new Area(&d_out, Area::type_t::type_int16_p3);
	} else if(out_format == Area::format_t::format_rgba_8 || out_format == Area::format_t::format_bgra_8) {
		area_out = new Area(&d_out, Area::type_t::type_uint8_p4);
	} else if(out_format == Area::format_t::format_rgb_8) {
		area_out = new Area(&d_out, Area::type_t::type_uint8_p3);
	}
	if(!area_out->valid())
		return area_out;
	D_AREA_PTR(area_out)

	// input format is FLOAT RGBA, and output - U8 ARGB
	int in_width = area_in->mem_width();
	float *in = (float *)area_in->ptr();
	int x_off = area_in->dimensions()->edges.x1;
	int x_max = area_in->dimensions()->width();
	int y_off = area_in->dimensions()->edges.y1;
	int y_max = area_in->dimensions()->height();
/*
cerr << "area_in->dimensions()->edges.x1 == " << area_in->dimensions()->edges.x1 << endl;
cerr << "area_in->dimensions()->edges.x2 == " << area_in->dimensions()->edges.x2 << endl;
cerr << "area_in->dimensions()->edges.y1 == " << area_in->dimensions()->edges.y1 << endl;
cerr << "area_in->dimensions()->edges.y2 == " << area_in->dimensions()->edges.y2 << endl;
cerr << "area_in->dimensions()->width()  == " << area_in->dimensions()->width() << endl;
cerr << "area_in->dimensions()->height() == " << area_in->dimensions()->height() << endl;
cerr << "area_out->dimensions()->edges.x1 == " << area_out->dimensions()->edges.x1 << endl;
cerr << "area_out->dimensions()->edges.x2 == " << area_out->dimensions()->edges.x2 << endl;
cerr << "area_out->dimensions()->edges.y1 == " << area_out->dimensions()->edges.y1 << endl;
cerr << "area_out->dimensions()->edges.y2 == " << area_out->dimensions()->edges.y2 << endl;
cerr << "area_out->dimensions()->width()  == " << area_out->dimensions()->width() << endl;
cerr << "area_out->dimensions()->height() == " << area_out->dimensions()->height() << endl;
*/

	uint8_t *out_8 = (uint8_t *)area_out->ptr();
	uint16_t *out_16 = (uint16_t *)area_out->ptr();

	// by default, RGBA_8 format
	int _i_r = 0;
	int _i_g = 1;
	int _i_b = 2;
	int _i_a = 3;
	if(out_format == Area::format_t::format_bgra_8) {
		// weird channel order for QT
		_i_r = 2;
		_i_g = 1;
		_i_b = 0;
		_i_a = 3;
	}
	const int i_r = _i_r;
	const int i_g = _i_g;
	const int i_b = _i_b;
	const int i_a = _i_a;
	bool out_is_16 = false;
	int _out_step = 4;
	if(out_format == Area::format_t::format_rgba_16) {
		out_is_16 = true;
	}
	if(out_format == Area::format_t::format_rgb_16) {
		out_is_16 = true;
		_out_step = 3;
	}
	if(out_format == Area::format_t::format_rgb_8) {
		_out_step = 3;
	}
	const int out_step = _out_step;

	int32_t i_scale = 0x00FFFF;
	float f_scale = 65536.0 - 1.0;

	int32_t v;
	int index_table[4] = {i_r, i_g, i_b, i_a};
	for(int y = 0; y < y_max; y++) {
		for(int x = 0; x < x_max; x++) {
			int l = ((y + y_off) * in_width + (x + x_off)) * 4;
			int k = (y * x_max + x) * out_step;
			if(rotation == 90)
				k = (x * y_max + (y_max - y - 1)) * out_step;
			if(rotation == 180)
				k = ((y_max - y - 1) * x_max + (x_max - x - 1)) * out_step;
			if(rotation == 270)
				k = ((x_max - x - 1) * y_max + y) * out_step;

			for(int c = 0; c < out_step; c++) {
				v = int32_t(in[l + c] * f_scale);
				if(v > i_scale)	v = i_scale;
				else if(v < 0x00)	v = 0x00;
				if(out_is_16)
					out_16[k + index_table[c]] = v;
				else
					out_8[k + index_table[c]] = v >> 8;
			}
		}
	}
	return area_out;
}

//------------------------------------------------------------------------------
class AreaHelper::mt_task_t {
public:
	Area *area_in;
	Area *area_out;

	int32_t i_scale;
	float f_scale;
	int rotation;

	std::atomic_int *y_flow;

	Area::format_t out_format;
};

// rotation already is normalized to [0|90|180|270]
Area *AreaHelper::convert_mt(SubFlow *subflow, Area *area_in, Area::format_t out_format, int rotation) {
	AreaHelper::mt_task_t **tasks = nullptr;
	Area *area_out = nullptr;
	std::atomic_int *y_flow = nullptr;

	// TODO - remove that to task, or as global system setting...
//	bool split_vertical = true;
//rotation = 0;

	if(subflow->sync_point_pre()) {
//cerr << "AreaHelper::convert(): rotation == " << rotation << endl;
//rotation = 0;
//cerr << "___________________-----------------------         rotation == " << rotation << endl;
		int cores = subflow->cores();
		tasks = new AreaHelper::mt_task_t *[cores];

		Area::t_dimensions d_out;
		d_out.size.w = area_in->dimensions()->width();
		d_out.size.h = area_in->dimensions()->height();
		if(rotation == 90 || rotation == 270) {
			d_out.size.w = area_in->dimensions()->height();
			d_out.size.h = area_in->dimensions()->width();
		}
		//--
		if(out_format == Area::format_t::format_rgba_16) {
			area_out = new Area(&d_out, Area::type_t::type_int16_p4);
//cerr << "out_format: rgba_16 - int16_p4" << endl;
		} else if(out_format == Area::format_t::format_rgb_16) {
			area_out = new Area(&d_out, Area::type_t::type_int16_p3);
//cerr << "out_format:  rgb_16 - int16_p3" << endl;
		} else if(out_format == Area::format_t::format_rgba_8 || out_format == Area::format_t::format_bgra_8) {
			area_out = new Area(&d_out, Area::type_t::type_uint8_p4);
//cerr << "out_format: rgba_8 - uint8_p4" << endl;
		} else if(out_format == Area::format_t::format_rgb_8) {
			area_out = new Area(&d_out, Area::type_t::type_uint8_p3);
//cerr << "out_format: rgb_8 - int8_p3" << endl;
		}
		D_AREA_PTR(area_out)
/*
cerr << "convert_mt():" << endl;
cerr << "area_in->dimensions()->edges.x1 == " << area_in->dimensions()->edges.x1 << endl;
cerr << "area_in->dimensions()->edges.x2 == " << area_in->dimensions()->edges.x2 << endl;
cerr << "area_in->dimensions()->edges.y1 == " << area_in->dimensions()->edges.y1 << endl;
cerr << "area_in->dimensions()->edges.y2 == " << area_in->dimensions()->edges.y2 << endl;
cerr << "area_in->dimensions()->width()  == " << area_in->dimensions()->width() << endl;
cerr << "area_in->dimensions()->height() == " << area_in->dimensions()->height() << endl;
cerr << "area_out->dimensions()->edges.x1 == " << area_out->dimensions()->edges.x1 << endl;
cerr << "area_out->dimensions()->edges.x2 == " << area_out->dimensions()->edges.x2 << endl;
cerr << "area_out->dimensions()->edges.y1 == " << area_out->dimensions()->edges.y1 << endl;
cerr << "area_out->dimensions()->edges.y2 == " << area_out->dimensions()->edges.y2 << endl;
cerr << "area_out->dimensions()->width()  == " << area_out->dimensions()->width() << endl;
cerr << "area_out->dimensions()->height() == " << area_out->dimensions()->height() << endl;
*/
		y_flow = new std::atomic_int(0);
		for(int i = 0; i < cores; i++) {
			tasks[i] = new AreaHelper::mt_task_t;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = area_out;
			tasks[i]->i_scale = 0x00FFFF;
			tasks[i]->f_scale = 0x00FFFF;
//			tasks[i]->f_scale = 65536.0 - 1.0;
			tasks[i]->rotation = rotation;
			tasks[i]->out_format = out_format;
			tasks[i]->y_flow = y_flow;
		}
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	AreaHelper::mt_task_t *task = (AreaHelper::mt_task_t *)subflow->get_private();
	if(task->area_out->valid())
		f_convert_mt(subflow);

//	subflow->sync_point();
//	if(subflow->is_master()) {
	if(subflow->sync_point_pre()) {
		for(int i = 0; i < subflow->cores(); i++)
			delete tasks[i];
		delete[] tasks;
		delete y_flow;
		// rotate geometry description if necessary
	}
	subflow->sync_point_post();
//	subflow->sync_point();
	return area_out;
}

void AreaHelper::f_convert_mt(class SubFlow *subflow) {
	AreaHelper::mt_task_t *task = (AreaHelper::mt_task_t *)subflow->get_private();
	Area::format_t out_format = task->out_format;

	// input format is FLOAT RGBA, and output - U8 ARGB
	int in_width = task->area_in->mem_width();
	float *in = (float *)task->area_in->ptr();
	const int rotation = task->rotation;
	int x_off = task->area_in->dimensions()->edges.x1;
	int x_max = task->area_in->dimensions()->width();
	int y_off = task->area_in->dimensions()->edges.y1;
	int y_max = task->area_in->dimensions()->height();

	uint8_t *out_8 = (uint8_t *)task->area_out->ptr();
	uint16_t *out_16 = (uint16_t *)task->area_out->ptr();

	// by default, RGBA_8 format
	int _i_r = 0;
	int _i_g = 1;
	int _i_b = 2;
	int _i_a = 3;
	if(out_format == Area::format_t::format_bgra_8) {
		// weird channel order for QT
		_i_r = 2;
		_i_g = 1;
		_i_b = 0;
		_i_a = 3;
	}
	const int i_r = _i_r;
	const int i_g = _i_g;
	const int i_b = _i_b;
	const int i_a = _i_a;
	bool out_is_16 = false;
//	bool out_is_3 = false;
	int _out_step = 4;
	if(out_format == Area::format_t::format_rgba_16) {
		out_is_16 = true;
	}
	if(out_format == Area::format_t::format_rgb_16) {
		out_is_16 = true;
//		out_is_3 = true;
		_out_step = 3;
	}
	if(out_format == Area::format_t::format_rgb_8) {
//		out_is_3 = true;
		_out_step = 3;
	}
	const int out_step = _out_step;

	int32_t i_scale = task->i_scale;
	float f_scale = task->f_scale;

	// TODO: for export - apply color background for 3 bytes RGB format
	int32_t v;
	int y;
	int index_table[4] = {i_r, i_g, i_b, i_a};
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; x++) {
			int l = ((y + y_off) * in_width + (x + x_off)) * 4;
			int k = (y * x_max + x) * out_step;
			if(rotation == 90)
				k = (x * y_max + (y_max - y - 1)) * out_step;
			if(rotation == 180)
				k = ((y_max - y - 1) * x_max + (x_max - x - 1)) * out_step;
			if(rotation == 270)
				k = ((x_max - x - 1) * y_max + y) * out_step;
/*
			if(in[l + 3] < 1.0) {
				in[l + 0] = 1.0;
				in[l + 1] = 1.0;
				in[l + 2] = 1.0;
				in[l + 3] = 1.0;
			}
*/
//			for(int c = 0; c < 4; c++) {
			for(int c = 0; c < out_step; c++) {
				v = int32_t(in[l + c] * f_scale);
				if(v > i_scale)	v = i_scale;
				else if(v < 0x00)	v = 0x00;
				if(out_is_16)
					out_16[k + index_table[c]] = v;
				else
					out_8[k + index_table[c]] = v >> 8;
			}
		}
	}
}

//------------------------------------------------------------------------------
// use 'edges' for crop if any of them is not zero (for filters w/o geometry change)
// use 'position.x|.y' otherwise (for filters like f_rotation, with geometry change)
Area *AreaHelper::crop(Area *area_in, Area::t_dimensions crop) {
//cerr << "AreaHelper::crop()" << endl;
//cerr << "asked crop: " << crop.width() << "x" << crop.height() << endl;
	// can be asked from f_rotation etc
	if(crop.width() > area_in->dimensions()->width() || crop.height() > area_in->dimensions()->height()) {
//cerr << "AreaHelper::crop(): asked too big crop: area_input size is " << area_in->dimensions()->width() << "x" << area_in->dimensions()->height() << "; crop size is " << crop.width() << "x" << crop.height() << endl;
//		return new Area(*area_in);
cerr << "WARNING (\?\?): AreaHelper::crop(): asked too big crop: input size is " << area_in->dimensions()->width() << "x" << area_in->dimensions()->height() << "; crop size is " << crop.width() << "x" << crop.height() << endl;
	}
	if(!crop.edges_are_OK()) {
cerr << "AreaHelper::crop(): asked invalid crop: crop size == " << crop.size.w << "x" << crop.size.h << "; edges for x == " << crop.edges.x1 << " - " << crop.edges.x2;
cerr << "; edges for y == " << crop.edges.y1 << " - " << crop.edges.y2 << endl;
//		return new Area(*area_in);
		Area *a = new Area(*area_in);
		D_AREA_PTR(a)
		return a;
	}
//cerr << "crop(): asked size is " << crop.width() << "x" << crop.height() << endl;

	Area *area_out = new Area(crop.width(), crop.height(), area_in->type());
	if(!area_out->valid())
		return area_out;
	D_AREA_PTR(area_out);
	Area::t_dimensions *d_in = area_in->dimensions();
	Area::t_dimensions *d = area_out->dimensions();
	d->position = crop.position;
//cerr << "AreaHelper::crop(): d->position == " << d->position.x << " - " << d->position.y << ", px_size == " << d->position.px_size << endl;
//cerr << "AreaHelper::crop(): d->position._max == " << d->position._x_max << " - " << d->position._y_max << endl;
/*
cerr << "d->edges x == " << d->edges.x1 << " - " << d->edges.x2 << endl;
cerr << "d->edges y == " << d->edges.y1 << " - " << d->edges.y2 << endl;
cerr << "z->edges x == " << d_in->edges.x1 << " - " << d_in->edges.x2 << endl;
cerr << "z->edges y == " << d_in->edges.y1 << " - " << d_in->edges.y2 << endl;
cerr << "z->size    == " << d_in->size.w << " - " << d_in->size.h << endl;
*/
/*
cerr << "AreaHelper::crop()" << endl;
cerr << "area in size is " << in->dimensions()->width() << "x" << in->dimensions()->height() << endl;
cerr << "desired size is " << crop.width() << "x" << crop.height() << endl;
*/

	int in_width = area_in->dimensions()->width();
	int in_height = area_in->dimensions()->height();
	int x_min = crop.edges.x1;
	int y_min = crop.edges.y1;
	int x_max = crop.size.w - crop.edges.x2;
	int y_max = crop.size.h - crop.edges.y2;
	uint8_t *ptr_in = (uint8_t *)area_in->ptr();
	uint8_t *ptr_out = (uint8_t *)area_out->ptr();
	uint8_t *pi, *po;
	uint8_t black = 0x7F;
	int s = area_in->type_to_sizeof();
/*
cerr << "s == " << s << endl;
cerr << "edges x == " << crop.edges.x1 << " - " << crop.edges.x2 << endl;
cerr << "crop: X from " << crop.edges.x1 << " to " << x_max << " inside of " << crop.size.w << endl;
cerr << "crop: Y from " << crop.edges.y1 << " to " << y_max << " inside of " << crop.size.h << endl;
cerr << "desired size is " << crop.width() << "x" << crop.height() << endl;
cerr << "area in size is " << in->dimensions()->width() << "x" << in->dimensions()->height() << endl;
*/
	int offset_x = d_in->edges.x1;
	int offset_y = d_in->edges.y1;

	if(crop.edges.x1 == 0 && crop.edges.x2 == 0 && crop.edges.y1 == 0 && crop.edges.y2 == 0) {
		// use position instead of edges
		x_min = (crop.position.x - d_in->position.x) / crop.position.px_size_x;
		y_min = (crop.position.y - d_in->position.y) / crop.position.px_size_y;
		x_max = crop.size.w + x_min;
		y_max = crop.size.h + y_min;
//		x_max = in_width - x_min - crop.size.w;
//		y_max = in_height - y_min - crop.size.h;
	}
/*
cerr << "x_min == " << x_min << endl;
cerr << "y_min == " << y_min << endl;
cerr << "x_max == " << x_max << endl;
cerr << "y_max == " << y_max << endl;
cerr << "crop.width() == " << crop.width() << endl;
cerr << "crop.height() == " << crop.height() << endl;
cerr << "offset_x == " << offset_x << endl;
cerr << "offset_y == " << offset_y << endl;
*/
	d->position.x = d_in->position.x + x_min * d_in->position.px_size_x;
	d->position.y = d_in->position.y + y_min * d_in->position.px_size_y;

	int in_w = d_in->size.w;
/*
cerr << "in_w == " << in_w << endl;
cerr << "in_width == " << in_width << endl;
cerr << "in_h == " << d_in->size.h << endl;
cerr << "in_height == " << in_height << endl;
*/

	long offset = 0;
	float black_f[4] = {0.0, 0.0, 0.0, 0.0};
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			if(x < 0 || x >= in_width || y < 0 || y >= in_height)
				pi = (uint8_t *)black_f;
			else
				pi = &ptr_in[((y + offset_y) * in_w + x + offset_x) * s];
			po = &ptr_out[offset];
			for(int i = 0; i < s; i++)
				po[i] = pi[i];
//				ptr_out[offset + i] = ptr_in[(y * crop.size.w + x) * s + i];
			offset += s;
///*
			// mark corners
			bool draw = false;
			if((y == y_min || y == y_max - 1) && (x < x_min + 20 || x > x_max - 21))
				draw = true;
			if((y < y_min + 20 || y > y_max - 21) && (x == x_min || x == x_max - 1))
				draw = true;
			if(draw) {
				for(int i = 0; i < s; i++)
					po[i] = black;
			}
//*/
		}
	}
//cerr << "crop - done" << endl;
	return area_out;
}

//------------------------------------------------------------------------------
// use it only with type_float_p4
Area *AreaHelper::rotate(Area *area_in, int rotation) {
	if(rotation == 0 || rotation % 90 != 0 || area_in->type() != Area::type_t::type_float_p4) {
		Area *a = new Area(*area_in);
		D_AREA_PTR(a);
		return a;
//		return new Area(*area_in);
	}

	Area::t_dimensions dims = *area_in->dimensions();
	int r = rotation;
	while(r != 0) {
		r -= 90;
		int t;
		t = dims.size.w;
		dims.size.w = dims.size.h;
		dims.size.h = t;
		t = dims.edges.x1;
		dims.edges.x1 = dims.edges.y2;
		dims.edges.y2 = dims.edges.x2;
		dims.edges.x2 = dims.edges.y1;
		dims.edges.y1 = t;
	}
	Area *area_out = new Area(&dims, area_in->type());
	if(!area_out->valid())
		return area_out;
	D_AREA_PTR(area_out);
	const Area::t_dimensions *in_dim = area_in->dimensions();
	int width = dims.size.w;
	int height = dims.size.h;

	int offset = width;
	int offset_x = 1;
	int offset_y = 0;
	if(rotation == 90) {
		offset = width - 1;
		offset_x = width;
		offset_y = -(width * height + 1);
	}
	if(rotation == 180) {
		offset = width * height - 1;
		offset_x = -1;
		offset_y = 0;
	}
	if(rotation == 270) {
		offset = (height - 1) * width;
		offset_x = -width;
		offset_y = height * width + 1;
	}
	float *p_in = (float *)area_in->ptr();
	float *p_out = (float *)area_out->ptr();
	int x_max = in_dim->size.w;
	int y_max = in_dim->size.h;
	for(int y = 0; y < y_max; y++) {
		for(int x = 0; x < x_max; x++) {
			for(int i = 0; i < 4; i++)
				p_out[offset * 4 + i] = p_in[i];
			offset += offset_x;
			p_in += 4;
		}
		offset += offset_y;
	}
	return area_out;
}

//------------------------------------------------------------------------------
