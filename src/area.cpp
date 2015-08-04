/*
 * area.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
*	TODO:
*	- add support of tiling in scaling
*/

#include <iostream>
#include <math.h>
#include <string.h>

#include "area.h"
#include "ddr_math.h"
#include "mt.h"
#include "system.h"

#define MIN_SIZE	128
#define MIN_WIDTH	128
#define MIN_HEIGHT	128

using namespace std;

//------------------------------------------------------------------------------
void Area::t_dimensions::dump(void) {
	cerr << "Area::t_dimensions::dump()" << endl;
	cerr << "     size()   == " << width() << "x" << height() << endl;
	cerr << "     size     == " << size.w << "x" << size.h << endl;
	cerr << "     edges: x == " << edges.x1 << " - " << edges.x2 << "; y == " << edges.y1 << " - " << edges.y2 << endl;
	cerr << "     position == " << position.x << " - " << position.y << endl;
	cerr << "     _x|y_max == " << position._x_max << " - " << position._y_max << endl;
	cerr << "      px_size == " << position.px_size_x << " - " << position.px_size_y << endl;
	cerr << endl;
}

bool Area::t_dimensions::edges_are_OK(void) {
	bool res = true;
	if(edges.x1 >= size.w || edges.x2 >= size.w || edges.y1 >= size.h || edges.y2 >= size.h)
		res = false;
	if(edges.x1 + edges.x2 >= size.w || edges.y1 + edges.y2 >= size.h)
		res = false;
	return res;
}

void Area::t_dimensions::edges_offset_x1(int offset) {
	int x1 = edges.x1 + offset;
	if(x1 < 0) x1 = 0;
	offset = x1 - edges.x1;
	edges.x1 += offset;
	position.x += position.px_size_x * offset;
/*
	edges.x1 += offset;
	if(edges.x1 < 0)
		edges.x1 = 0;
	else
		position.x += position.px_size * offset;
*/
}

void Area::t_dimensions::edges_offset_x2(int offset) {
	edges.x2 += offset;
	if(edges.x2 < 0)
		edges.x2 = 0;
}

void Area::t_dimensions::edges_offset_y1(int offset) {
	int y1 = edges.y1 + offset;
	if(y1 < 0) y1 = 0;
	offset = y1 - edges.y1;
	edges.y1 += offset;
	position.y += position.px_size_y * offset;
/*
	edges.y1 += offset;
	if(edges.y1 < 0)
		edges.y1 = 0;
	else
		position.y += position.px_size * offset;
*/
}

void Area::t_dimensions::edges_offset_y2(int offset) {
	edges.y2 += offset;
	if(edges.y2 < 0)
		edges.y2 = 0;
}

void Area::t_dimensions::rotate_plus_90(void) {
	Area::t_dimensions d = *this;
	d.size.w = size.h;
	d.size.h = size.w;
	d.edges.x1 = edges.y2;
	d.edges.x2 = edges.y1;
	d.edges.y1 = edges.x1;
	d.edges.y2 = edges.x2;
	d.position._x_max = position._y_max - (height() - 1) * position.px_size_x; // ???
	d.position._y_max = -position._x_max;
	d.position.x = d.position.y;
	*this = d;
}

//------------------------------------------------------------------------------
void Area::dump_ptr(const char *file, int line) {
	return;
	std::cerr << "--=--=--=--=-->> Area ptr == ";
	mem.ptr_dump();
	std::cerr << " at file: \"" << file << "\":" << line << std::endl;
}

Area::Area(void) {
}

Area::Area(int32_t width, int32_t height, Area::type_t type) {
	_type = type;
	mem = Mem(width * height * type_to_sizeof(_type));
	_dimensions.size.w = width;
	_dimensions.size.h = height;
	// initialize positions
	Area::t_position &p = _dimensions.position;
	p.px_size_x = 1.0;
	p.px_size_y = 1.0;
	p._x_max = double(_dimensions.width()) / 2.0;
	p._y_max = double(_dimensions.height()) / 2.0;
	p.x = -p._x_max + 0.5 * p.px_size_x;
	p.y = -p._y_max + 0.5 * p.px_size_y;
}

Area::Area(const t_dimensions *_dims, Area::type_t type) {
	_dimensions = *_dims;
	_type = type;
	mem = Mem(_dimensions.size.w * _dimensions.size.h * type_to_sizeof(_type));
}

Area::Area(Area const &other) {
	// note - copy field by field...
	mem = other.mem;
	_type = other._type;
	_dimensions = other._dimensions;
}

Area& Area::operator = (const Area & other) {
	if(this != &other) {
		mem = other.mem;
		_type = other._type;
		_dimensions = other._dimensions;
	}
	return *this;
}

Area::~Area() {
}

void *Area::ptr(void) {
	void *ptr = mem.ptr();
	if(ptr == NULL) {
cerr << "FATAL: Area: access to empty Area object" << endl;
		throw("Area: access to empty Area object");
	}
	return ptr;
}

Area *Area::real_copy(Area *other) {
	Area *r = new Area(other->_dimensions.size.w, other->_dimensions.size.h, other->_type);
	D_AREA_PTR(r);
	memcpy(r->ptr(), other->ptr(), r->_dimensions.size.w * r->_dimensions.size.h * type_to_sizeof(r->_type));
	r->_dimensions = other->_dimensions;
	return r;
}

//==============================================================================
QImage Area::to_qimage(void) {
	int w = dimensions()->width();
	int h = dimensions()->height();
	if(_type == type_uint8_p4)
		return QImage((uchar *)ptr(), w, h, w * 4, QImage::Format_ARGB32);
	if(_type == type_uint8_p3)
		return QImage((uchar *)ptr(), w, h, w * 3, QImage::Format_RGB888);
	return QImage();
}

QPixmap Area::to_qpixmap(void) {
	if(_type != type_uint8_p4)
		return QPixmap();
	int w = dimensions()->width();
	int h = dimensions()->height();
	return QPixmap(QPixmap::fromImage(QImage((uchar *)ptr(), w, h, w * 4, QImage::Format_ARGB32)));
}

//==============================================================================
// input: 'd' - dimensions of area to scale fit limits 'limit_w' and 'limit_h'
// output: 'd' - dimensions as request to Area::scale with updated size.w|.h, position.x|.y and position.px_size to fit asked limits
float Area::scale_dimensions_to_factor(class Area::t_dimensions *d, float scaling_factor) {
	return Area::scale_dimensions_to_size(d, scaling_factor, 0, 0, false);
}

float Area::scale_dimensions_to_size_fit(class Area::t_dimensions *d, int limit_w, int limit_h) {
	return Area::scale_dimensions_to_size(d, 0.0, limit_w, limit_h, true);
}

float Area::scale_dimensions_to_size_fill(class Area::t_dimensions *d, int limit_w, int limit_h) {
	return Area::scale_dimensions_to_size(d, 0.0, limit_w, limit_h, false);
}

// Keep top left edge as top left edge; use ceil() for integer size after scaling;
// and use edges repeat to fill excess space at edges that is less than 1 pixel.
// Scaling factor: > 1.0 - downscale; < 1.0 - upscale; i.e. actually is desired 'px_size'
float Area::scale_dimensions_to_size(class Area::t_dimensions *d, float scaling_factor, int limit_w, int limit_h, bool to_fit) {
	double in_w = d->width();
	double in_h = d->height();
//float x_in = d->position.x;
//float y_in = d->position.y;
	int out_w = in_w;
	int out_h = in_h;
	int clip_w = out_w;
	int clip_h = out_h;
	// use scaling factor
	if(limit_w == 0 && limit_h == 0 && scaling_factor > 0.0) {
		out_w = ceil(in_w * scaling_factor);
		out_h = ceil(in_h * scaling_factor);
	// 'fit' or 'fill' scaling
//cerr << "Area::scale_dimensions_to_size(): scaling_factor" << endl;
	} else {
		double scale_x = in_w / limit_w;
		double scale_y = in_h / limit_h;
		out_w = limit_w;
		out_h = limit_h;
		if(to_fit) {
//cerr << "Area::scale_dimensions_to_size(): to_fit" << endl;
			if(scale_x < scale_y) {
				double w = in_w / scale_y;
				out_w = ceil(w);
			} else {
				double h = in_h / scale_x;
				out_h = ceil(h);
			}
			clip_w = out_w;
			clip_h = out_h;
		} else {
//cerr << "Area::scale_dimensions_to_size(): to_fill" << endl;
			if(scale_x < scale_y) {
				out_w = limit_w;
				out_h = in_h / scale_x;
			} else {
				out_w = in_w / scale_y;
				out_h = limit_h;
			}
			clip_w = limit_w;
			clip_h = limit_h;
		}
	}
/*
float x1_old = d->position.x - d->position.px_size_x * 0.5;
float x2_old = x1_old + d->size.w * d->position.px_size_x;
float y1_old = d->position.y - d->position.px_size_y * 0.5;
float y2_old = y1_old + d->size.h * d->position.px_size_y;
*/
	double px_size_x = in_w / out_w;
	double px_size_y = in_h / out_h;
//cerr << "Area::scale_dimensions_to_size(); pos in  == " << d->position.x << " - " << d->position.y << " - " << d->position.px_size_x << " - " <<  d->position.px_size_y << endl;
	double center_x = d->position.x - d->position.px_size_x * 0.5 + (in_w / d->position.px_size_x) * 0.5;
	d->position.x = center_x - double(clip_w) * px_size_x * 0.5 + px_size_x * 0.5;
	double center_y = d->position.y - d->position.px_size_y * 0.5 + (in_h / d->position.px_size_y) * 0.5;
	d->position.y = center_y - double(clip_h) * px_size_y * 0.5 + px_size_y * 0.5;
	d->position.px_size_x = px_size_x;
	d->position.px_size_y = px_size_y;
//cerr << "Area::scale_dimensions_to_size(); pos out == " << d->position.x << " - " << d->position.y << " - " << d->position.px_size_x << " - " <<  d->position.px_size_y << endl;
	d->size.w = clip_w;
	d->size.h = clip_h;
/*
cerr << "Area::scale_dimensions_to_size(); to_fit == " << to_fit << "; limits == " << limit_w << " x " << limit_h << endl;
cerr << "d->size.w == " << d->size.w << endl;
cerr << "d->size.h == " << d->size.h << endl;
float x1_new = d->position.x - d->position.px_size_x * 0.5;
float x2_new = x1_new + d->size.w * d->position.px_size_x;
float y1_new = d->position.y - d->position.px_size_y * 0.5;
float y2_new = y1_new + d->size.h * d->position.px_size_y;
cerr << "x in == " << x_in << "; y in == " << y_in << endl;
cerr << "x1  in == " << x1_old << "; x1 out == " << x1_new << endl;
cerr << "x2  in == " << x2_old << "; x2 out == " << x2_new << endl;
cerr << "y1  in == " << y1_old << "; y1 out == " << y1_new << endl;
cerr << "y2  in == " << y2_old << "; y2 out == " << y2_new << endl;
cerr << "px_size_x == " << px_size_x << "; px_size_y == " << px_size_y << endl;
cerr << "position: " << d->position.x << " - " << d->position.y << "; " << d->size.w << " x " << d->size.h << endl;
cerr << endl;
*/
	d->edges.x1 = 0;
	d->edges.x2 = 0;
	d->edges.y1 = 0;
	d->edges.y2 = 0;
	return (px_size_x + px_size_y) / 2.0;
}

//------------------------------------------------------------------------------
class Area::scale_task_t {
public:
	Area *area_in;
	Area *area_out;
	float scale_x;
	float scale_y;
	QAtomicInt *y_flow;
	float in_x_off;
	float in_y_off;
};

//Area *Area::scale(SubFlow *subflow, int out_w, int out_h, float out_x, float out_y, float out_scale) {
Area *Area::scale(SubFlow *subflow, int out_w, int out_h, float out_scale_x, float out_scale_y) {
// TODO: check original 'px_size' asked by View and 'out_scale'
	// TODO: utilize t_dimensions::position, support of scaling with tiles
//cerr << "Area:scale(...) : " << (unsigned long)this << endl;
	scale_task_t **tasks = NULL;
	Area *area_out = NULL;
	QAtomicInt *y_flow = NULL;
	if(subflow->sync_point_pre()) {
		Area::t_dimensions *d_in = this->dimensions();
///*
cerr << "Area::scale(): out_scale == " << out_scale_x << " - " << out_scale_y << endl;
		cerr << "d_in position: " << d_in->position.x << "_" << d_in->position.y << "; max == " << d_in->position._x_max << "_" << d_in->position._y_max << endl;
//cerr << "out position == " << out_x << "-" << out_y << endl;
cerr << "   out scale == " << out_scale_x << " - " << out_scale_y << endl;
cerr << "current size == " << d_in->width() << "x" << d_in->height() << endl;
//*/
		// TODO: check " - d_in->position.px_size / 2.0" part
//		float in_x_off = out_x - (d_in->position.x - d_in->position.px_size / 2.0) - out_scale / 2.0;
//		float in_y_off = out_y - (d_in->position.y - d_in->position.px_size / 2.0) - out_scale / 2.0;
//		float in_x_off = 0 - (d_in->position.x - d_in->position.px_size / 2.0) - out_scale / 2.0;
//		float in_y_off = 0 - (d_in->position.y - d_in->position.px_size / 2.0) - out_scale / 2.0;
		float in_x_off = d_in->position.x - d_in->position.px_size_x * 0.5 + out_scale_x * 0.5;
		float in_y_off = d_in->position.y - d_in->position.px_size_y * 0.5 + out_scale_y * 0.5;
cerr << "in_x_off == " << in_x_off << endl;
cerr << "in_y_off == " << in_y_off << endl;

//cerr << "in_x_off == " << in_x_off << endl;
//cerr << "in_y_off == " << in_y_off << endl;
cerr << "out size == " << out_w << "x" << out_h << endl;

		if(type() == Area::type_uint8_p4)
			area_out = new Area(out_w, out_h, Area::type_uint8_p4);
		else
			area_out = new Area(out_w, out_h);
		area_out->dimensions()->position.x = d_in->position.x - d_in->position.px_size_x * 0.5 + out_scale_x * 0.5;
		area_out->dimensions()->position.y = d_in->position.y - d_in->position.px_size_y * 0.5 + out_scale_y * 0.5;
		area_out->dimensions()->position.px_size_x = out_scale_x;
		area_out->dimensions()->position.px_size_y = out_scale_y;

		// TODO: apply correct offsets/px_size to area_out
		D_AREA_PTR(area_out);
		int cores = subflow->cores();
		tasks = new scale_task_t *[cores];
		y_flow = new QAtomicInt(0);
		for(int i = 0; i < cores; i++) {
			tasks[i] = new scale_task_t;
			tasks[i]->area_in = this;
			tasks[i]->area_out = area_out;
			tasks[i]->scale_x = out_scale_x;
			tasks[i]->scale_y = out_scale_y;
			tasks[i]->y_flow = y_flow;
			tasks[i]->in_x_off = in_x_off;
			tasks[i]->in_y_off = in_y_off;
		}
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	float scale_factor_x = ((scale_task_t *)subflow->get_private())->scale_x;
	float scale_factor_y = ((scale_task_t *)subflow->get_private())->scale_y;
	float scale_factor = (scale_factor_x + scale_factor_y) / 2.0;
	if(scale_factor == 1.0) {
//cerr << "scale_process_copy()" << endl;
		scale_process_copy(subflow);
	} else {
		if(scale_factor > 1.0) {
//cerr << "scale > 1.0, downscale: " << scale_factor << endl;
			scale_process_downscale(subflow);
		} else {
//cerr << "scale < 1.0, upscale: " << scale_factor << endl;
			scale_process_upscale(subflow);
		}
	}

	if(subflow->sync_point_pre()) {
		for(int i = 0; i < subflow->cores(); i++)
			delete tasks[i];
		delete[] tasks;
		delete y_flow;
/*
		area_out->dimensions()->position.x = position_x;
		area_out->dimensions()->position.y = position_y;
//		area_out->dimensions()->position.x = out_x;
//		area_out->dimensions()->position.y = out_y;
		area_out->dimensions()->position.px_size = out_scale;
cerr << "---->>>> Area::scale(): -->> out x|y: " << area_out->dimensions()->position.x << " - " << area_out->dimensions()->position.y << endl;
cerr << "---->>>> Area::scale(): -->> out x|y: " << area_out->dimensions()->position.x << " - " << area_out->dimensions()->position.y << endl;
*/
	}
	subflow->sync_point_post();

//if(subflow->is_master())
//cerr << "area_scale - done" << endl;
	return area_out;
}

//------------------------------------------------------------------------------
// NOTE: that is a real copy of Area - useful for destructive processing;
// TODO: add implementation with virtual copy, for non-destructive processing;
void Area::scale_process_copy(SubFlow *subflow) {
	scale_task_t *task = (scale_task_t *)subflow->get_private();

	Area *area_in = task->area_in;
	Area *area_out = task->area_out;

	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();
	uint8_t *u_in = (uint8_t *)area_in->ptr();
	uint8_t *u_out = (uint8_t *)area_out->ptr();
	bool flag_8b = (area_in->type() == Area::type_uint8_p4);

	int out_x_offset = area_out->dimensions()->edges.x1;
	int out_y_offset = area_out->dimensions()->edges.y1;
	int out_width = area_out->mem_width();
	int out_w = area_out->dimensions()->width();
	int out_h = area_out->dimensions()->height();

//	int in_x_offset = task->in_x_off;
//	int in_y_offset = task->in_y_off;
	int in_x_offset = area_in->dimensions()->edges.x1;
	int in_y_offset = area_in->dimensions()->edges.y1;
	int in_width = area_in->mem_width();
/*
cerr << "Area::scale_process_copy()" << endl;
cerr << " in_x_offset = " << in_x_offset << endl;
cerr << " in_y_offset = " << in_y_offset << endl;
cerr << "out_x_offset = " << out_x_offset << endl;
cerr << "out_y_offset = " << out_y_offset << endl;
cerr << " area_in->dimensions()->position.x == " << area_in->dimensions()->position.x << endl;
cerr << " area_in->dimensions()->position.y == " << area_in->dimensions()->position.y << endl;
cerr << " area_in->dimensions()->position.px_size == " << area_in->dimensions()->position.px_size << endl;
cerr << "area_out->dimensions()->position.x == " << area_out->dimensions()->position.x << endl;
cerr << "area_out->dimensions()->position.y == " << area_out->dimensions()->position.y << endl;
cerr << "area_out->dimensions()->position.px_size == " << area_out->dimensions()->position.px_size << endl;
*/

	int y = 0;
	if(flag_8b == false) {
		while((y = _mt_qatom_fetch_and_add(task->y_flow, 1)) < out_h) {
			for(int x = 0; x < out_w; x++) {
				int index_in = ((y + in_y_offset) * in_width + x + in_x_offset) * 4;
				int index_out = ((y + out_y_offset) * out_width + x + out_x_offset) * 4;
				_out[index_out + 0] = _in[index_in + 0];
				_out[index_out + 1] = _in[index_in + 1];
				_out[index_out + 2] = _in[index_in + 2];
				_out[index_out + 3] = _in[index_in + 3];
			}
		}
	} else {
		while((y = _mt_qatom_fetch_and_add(task->y_flow, 1)) < out_h) {
			for(int x = 0; x < out_w; x++) {
				int index_in = ((y + in_y_offset) * in_width + x + in_x_offset) * 4;
				int index_out = ((y + out_y_offset) * out_width + x + out_x_offset) * 4;
				u_out[index_out + 0] = u_in[index_in + 0];
				u_out[index_out + 1] = u_in[index_in + 1];
				u_out[index_out + 2] = u_in[index_in + 2];
				u_out[index_out + 3] = u_in[index_in + 3];
			}
		}
	}
}

//------------------------------------------------------------------------------
void Area::scale_process_downscale(SubFlow *subflow) {
	scale_task_t *task = (scale_task_t *)subflow->get_private();

	Area *area_in = task->area_in;
	Area *area_out = task->area_out;

	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();
	uint8_t *u_in = (uint8_t *)area_in->ptr();
	uint8_t *u_out = (uint8_t *)area_out->ptr();

	int in_x_min = area_in->dimensions()->edges.x1;
	int in_y_min = area_in->dimensions()->edges.y1;
	int in_w = area_in->dimensions()->width();
	int in_h = area_in->dimensions()->height();
	int in_width = area_in->mem_width();

	int out_x_min = area_out->dimensions()->edges.x1;
	int out_y_min = area_out->dimensions()->edges.y1;
	int out_width = area_out->mem_width();

	int in_x_off = task->in_x_off;
	int in_y_off = task->in_y_off;
	float f_offset_x = task->in_x_off - in_x_off;
	float f_offset_y = task->in_y_off - in_y_off;

	int out_w = area_out->dimensions()->width();
	int out_h = area_out->dimensions()->height();
	int j_max = out_h;
	int i_max = out_w;

	/* used 'windowed' method:
	 * | | | | | | - input,  pixels [0 - 4]
	 * |    |    | - output, pixels [0 - 1]
	 *  out[0] = (in[0] * 1.0 + in[1] * 1.0 + in[2] * 0.5) / 2.5
	 *  out[1] = (in[2] * 0.5 + in[3] * 1.0 + in[4] * 1.0) / 2.5
	 *	- for scale == 2.5; and so on
	 */
	bool flag_8b = (area_in->type() == Area::type_uint8_p4);
	const float scale_x = task->scale_x;
	const float scale_y = task->scale_y;
	const float w_div = scale_x * scale_y;
	int j = 0;
	while((j = _mt_qatom_fetch_and_add(task->y_flow, 1)) < j_max) {
		int out_y = j;
		const float f_in_y = f_offset_y + scale_y * j;
		for(int i = 0; i < i_max; i++) {
			int out_x = i;
			const float f_in_x = f_offset_x + scale_x * i;
			// accumulator
			float px[4];
			for(int k = 0; k < 4; k++)
				px[k] = 0.0;
			// process window
			int in_y = floor(f_in_y);
			float dy = scale_y;
			float wy = 1.0 - (f_in_y - in_y);
			while(dy > 0.0) {
				int in_x = floor(f_in_x);
				float dx = scale_x;
				float wx = 1.0 - (f_in_x - in_x);
				while(dx > 0.0) {
					// sum pixels
					float weight = wx * wy;
/*
					int x = in_x;
					if(in_x < 0)		x = 0;
					if(in_x >= in_w)	x = in_w - 1;
					int y = in_y;
					if(in_y < 0)		y = 0;
					if(in_y >= in_h)	y = in_h - 1;
*/
					bool flag_out = false;
					int x = in_x;
					flag_out |= in_x < 0;
					flag_out |= in_x >= in_w;
					int y = in_y;
					flag_out |= in_y < 0;
					flag_out |= in_y >= in_h;
					if(flag_out == false) {
						if(flag_8b == false) {
							px[0] += _in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 0] * weight;
							px[1] += _in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 1] * weight;
							px[2] += _in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 2] * weight;
							px[3] += _in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 3] * weight;
//							px[3] += weight;
						} else {
							px[0] += weight * u_in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 0] * 0xFF;
							px[1] += weight * u_in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 1] * 0xFF;
							px[2] += weight * u_in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 2] * 0xFF;
							px[3] += weight * u_in[(x + in_x_min + (y + in_y_min) * in_width) * 4 + 3] * 0xFF;
//							px[3] += weight;
						}
					} // else pixel is missing and replaced by black transparent pixel [0.0, 0.0, 0.0, 0.0] and can be ignored because of multiplication results
					// X turnaround
					dx -= wx;
					if(dx >= 1.0)	wx = 1.0;
					else			wx = dx;
					in_x++;
				}
				// Y turnaround
				dy -= wy;
				if(dy >= 1.0)	wy = 1.0;
				else			wy = dy;
				in_y++;
			}
			if(flag_8b == false) {
				_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 0] = px[0] / w_div;
				_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 1] = px[1] / w_div;
				_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 2] = px[2] / w_div;
				_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 3] = px[3] / w_div;
			} else {
				u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 0] = px[0] / w_div;
				u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 1] = px[1] / w_div;
				u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 2] = px[2] / w_div;
				u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + 3] = px[3] / w_div;
			}
		}
	}
}

//------------------------------------------------------------------------------
void Area::scale_process_upscale(SubFlow *subflow) {
	scale_task_t *task = (scale_task_t *)subflow->get_private();

	Area *area_in = task->area_in;
	Area *area_out = task->area_out;

	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();
	uint8_t *u_in = (uint8_t *)area_in->ptr();
	uint8_t *u_out = (uint8_t *)area_out->ptr();

	int in_x_min = area_in->dimensions()->edges.x1;
	int in_y_min = area_in->dimensions()->edges.y1;
	int in_width = area_in->mem_width();
//	int in_height = area_in->mem_height();
	int in_w = area_in->dimensions()->width();
	int in_h = area_in->dimensions()->height();

	int out_x_min = area_out->dimensions()->edges.x1;
	int out_y_min = area_out->dimensions()->edges.y1;
	int out_width = area_out->mem_width();

	int in_x_off = task->in_x_off;
	int in_y_off = task->in_y_off;
	float f_offset_x = task->in_x_off - in_x_off;
	float f_offset_y = task->in_y_off - in_y_off;

cerr << "upscale: x == " << task->scale_x << endl;
cerr << "upscale: y == " << task->scale_y << endl;
cerr << "	f_offset_x == " << f_offset_x << endl;
cerr << "	f_offset_y == " << f_offset_y << endl;
	int out_w = area_out->dimensions()->width();
	int out_h = area_out->dimensions()->height();
	int j_max = out_h;
	int i_max = out_w;

	bool flag_8b = (area_in->type() == Area::type_uint8_p4);
	const float scale_x = task->scale_x;
	const float scale_y = task->scale_y;
	int j = 0;
	while((j = _mt_qatom_fetch_and_add(task->y_flow, 1)) < j_max) {
		int out_y = j;
		const float f_in_y = f_offset_y + scale_y * j;
		float floor_in_y = floor(f_in_y);
		int in_y = floor_in_y;
		float wy_1 = f_in_y - floor_in_y;
		float wy_0 = 1.0 - wy_1;
		// Y limits
		if(in_y < 0) {
			in_y = 0;
			wy_0 = 1.0;
			wy_1 = 0.0;
		}
		if(in_y >= in_h - 1) {
			in_y = in_h - 2;
			wy_0 = 0.0;
			wy_1 = 1.0;
		}
		for(int i = 0; i < i_max; i++) {
			int out_x = i;
			const float f_in_x = f_offset_x + scale_x * i;
			//--
			float floor_in_x = floor(f_in_x);
			int in_x = floor_in_x;
			float wx_1 = f_in_x - floor_in_x;
			float wx_0 = 1.0 - wx_1;
			// X limits
			if(in_x < 0) {
				in_x = 0;
				wx_0 = 1.0;
				wx_1 = 0.0;
			}
			if(in_x >= in_w - 1) {
				in_x = in_w - 2;
				wx_0 = 0.0;
				wx_1 = 1.0;
			}
			// accumulator
			float px[4];
			for(int k = 0; k < 4; k++)
				px[k] = 0.0;
			// interpolation
			float w[4];
			w[0] = wx_0 * wy_0;
			w[1] = wx_1 * wy_0;
			w[2] = wx_0 * wy_1;
			w[3] = wx_1 * wy_1;
			if(flag_8b == false) {
				for(int c = 0; c < 4; c++) {
					int offset_x = c % 2;
					int offset_y = c / 2;
					for(int k = 0; k < 4; k++)
						px[k] += _in[(in_x + offset_x + in_x_min + (in_y + offset_y + in_y_min) * in_width) * 4 + k] * w[c];
				}
				for(int k = 0; k < 4; k++)
					_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + k] = px[k];
			} else {
				for(int c = 0; c < 4; c++) {
					int offset_x = c % 2;
					int offset_y = c / 2;
					for(int k = 0; k < 4; k++)
						px[k] += w[c] * u_in[(in_x + offset_x + in_x_min + (in_y + offset_y + in_y_min) * in_width) * 4 + k] * 0xFF;
				}
				for(int k = 0; k < 4; k++)
					u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + k] = px[k];
			}
		}
	}
}

//------------------------------------------------------------------------------
//==============================================================================
