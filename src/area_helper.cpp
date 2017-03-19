/*
 * area_helper.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <iostream>

#include "area_helper.h"
#include "mt.h"
#include "system.h"

using namespace std;

//------------------------------------------------------------------------------
std::unique_ptr<Area> AreaHelper::convert(Area *area_in, Area::format_t out_format, int rotation) {
//cerr << "AreaHelper::convert(): rotation == " << rotation << endl;
	Area::t_dimensions d_out;
	d_out.size.w = area_in->dimensions()->width();
	d_out.size.h = area_in->dimensions()->height();
	if(rotation == 90 || rotation == 270) {
		d_out.size.w = area_in->dimensions()->height();
		d_out.size.h = area_in->dimensions()->width();
	}

	auto area_out = std::unique_ptr<Area>(new Area(&d_out, Area::type_for_format(out_format)));

	// input format is FLOAT RGBA, and output - U8 ARGB
	int in_width = area_in->mem_width();
	float *in = (float *)area_in->ptr();
	int x_off = area_in->dimensions()->edges.x1;
	int x_max = area_in->dimensions()->width();
	int y_off = area_in->dimensions()->edges.y1;
	int y_max = area_in->dimensions()->height();
	uint8_t *out_8 = (uint8_t *)area_out->ptr();
	uint16_t *out_16 = (uint16_t *)area_out->ptr();

	// by default, RGBA_8 format
	int _i_r = 0;
	int _i_g = 1;
	int _i_b = 2;
	int _i_a = 3;
	if(out_format == Area::format_t::bgra_8) {
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
	if(out_format == Area::format_t::rgba_16) {
		out_is_16 = true;
	}
	if(out_format == Area::format_t::rgb_16) {
		out_is_16 = true;
		_out_step = 3;
	}
	if(out_format == Area::format_t::rgb_8) {
		_out_step = 3;
	}
	const int out_step = _out_step;

	int32_t i_scale = 0x00FFFF;
	float f_scale = 65536.0 - 1.0;

	int index_table[4] = {i_r, i_g, i_b, i_a};
	for(int y = 0; y < y_max; ++y) {
		for(int x = 0; x < x_max; ++x) {
			int l = ((y + y_off) * in_width + (x + x_off)) * 4;
			int k = (y * x_max + x) * out_step;
			if(rotation == 90)
				k = (x * y_max + (y_max - y - 1)) * out_step;
			if(rotation == 180)
				k = ((y_max - y - 1) * x_max + (x_max - x - 1)) * out_step;
			if(rotation == 270)
				k = ((x_max - x - 1) * y_max + y) * out_step;

			for(int c = 0; c < out_step; ++c) {
				auto v = int32_t(in[l + c] * f_scale);
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
	Area::format_t out_format;

	int32_t i_scale;
	float f_scale;
	int rotation;
	int pos_x;
	int pos_y;

	std::atomic_int *y_flow;
};

// rotation already is normalized to [0 | 90 | 180 | 270]
// 'pos_x, pos_y' are offsets for insertion into 'tiled_area' if any
std::unique_ptr<Area> AreaHelper::convert_mt(SubFlow *subflow, Area *area_in, Area::format_t out_format, int rotation, Area *tiled_area, int pos_x, int pos_y) {
	std::unique_ptr<Area> area_out;
	Area *area_out_ptr = nullptr;
	std::unique_ptr<std::atomic_int> y_flow;
	std::vector<std::unique_ptr<mt_task_t>> tasks(0);

	if(subflow->sync_point_pre()) {
		Area::t_dimensions d_out;
		d_out.size.w = area_in->dimensions()->width();
		d_out.size.h = area_in->dimensions()->height();
		if(rotation == 90 || rotation == 270) {
			d_out.size.w = area_in->dimensions()->height();
			d_out.size.h = area_in->dimensions()->width();
		}
		//--
		if(tiled_area == nullptr) {
			area_out = std::unique_ptr<Area>(new Area(&d_out, Area::type_for_format(out_format)));
			area_out_ptr = area_out.get();
		} else
			area_out_ptr = tiled_area;
		const int threads_count = subflow->threads_count();
		tasks.resize(threads_count);
		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<mt_task_t>(new mt_task_t);
			mt_task_t *task = tasks[i].get();

			task->area_in = area_in;
			task->area_out = area_out_ptr;
			task->i_scale = 0x00FFFF;
			task->f_scale = 0x00FFFF;
			task->rotation = rotation;
			task->out_format = out_format;
			task->y_flow = y_flow.get();
			task->pos_x = pos_x;
			task->pos_y = pos_y;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	f_convert_mt(subflow);
	subflow->sync_point();

	return area_out;
}

void AreaHelper::f_convert_mt(class SubFlow *subflow) {
	AreaHelper::mt_task_t *task = (AreaHelper::mt_task_t *)subflow->get_private();
	Area::format_t out_format = task->out_format;

	// input format is FLOAT RGBA, and output - U8 ARGB
	int in_width = task->area_in->mem_width();
	const float *in = (const float *)task->area_in->ptr();
	const int rotation = task->rotation;
	const int x_off = task->area_in->dimensions()->edges.x1;
	const int x_max = task->area_in->dimensions()->width();
	const int y_off = task->area_in->dimensions()->edges.y1;
	const int y_max = task->area_in->dimensions()->height();
	const int pos_x = task->pos_x;
	const int pos_y = task->pos_y;

	uint8_t *out_8 = (uint8_t *)task->area_out->ptr();
	uint16_t *out_16 = (uint16_t *)task->area_out->ptr();
	const int out_width = task->area_out->mem_width();
	const int out_height = task->area_out->mem_height();

	// by default, RGBA_8 format
	int i_r = 0;
	int i_g = 1;
	int i_b = 2;
	int i_a = 3;
	if(out_format == Area::format_t::bgra_8) {
		i_r = 2;
		i_b = 0;
	}
	const int index_table[4] = {i_r, i_g, i_b, i_a};

	bool out_is_16 = false;
	int _out_step = 4;
	if(out_format == Area::format_t::rgba_16) {
		out_is_16 = true;
	}
	if(out_format == Area::format_t::rgb_16) {
		out_is_16 = true;
		_out_step = 3;
	}
	if(out_format == Area::format_t::rgb_8) {
		_out_step = 3;
	}
	const int out_step = _out_step;

	int32_t i_scale = task->i_scale;
	float f_scale = task->f_scale;

	// TODO: for export - apply color background for 3 bytes RGB format
	int y;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
			const int l = ((y + y_off) * in_width + (x + x_off)) * 4;
			int k = 0;
			switch(rotation) {
			case 0:
				k = ((y + pos_y) * out_width + (x + pos_x)) * out_step;
				break;
			case 90:
				k = ((x + pos_y) * out_width + (out_width - (y + pos_x) - 1)) * out_step;
				break;
			case 180:
				k = ((out_height - (y + pos_y) - 1) * out_width + (out_width - (x + pos_x) - 1)) * out_step;
				break;
			case 270:
				k = ((out_height - (x + pos_y) - 1) * out_width + (y + pos_x)) * out_step;
				break;
			};
			for(int c = 0; c < out_step; ++c) {
				auto v = int32_t(in[l + c] * f_scale);
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
