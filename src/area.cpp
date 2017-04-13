/*
 * area.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
*	TODO:
*	- add support of tiling in scaling
*/

#include <atomic>
#include <iostream>

#include <cmath>
#include <cstring>

#include "area.h"
#include "ddr_math.h"
#include "mt.h"
#include "system.h"

#define MIN_SIZE	128
#define MIN_WIDTH	128
#define MIN_HEIGHT	128

using namespace std;

//------------------------------------------------------------------------------
void Area::t_dimensions::dump(void) const {
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
//	return;
	std::cerr << "--=--=--=--=-->> Area ptr == ";
	mem.ptr_dump();
	std::cerr << " at file: \"" << file << "\":" << line << std::endl;
}

Area::Area(void) {
}

Area::Area(int32_t width, int32_t height, Area::type_t type) {
	_type = type;
	mem = Mem(width * height * type_to_sizeof(_type));
	if(mem.ptr() == nullptr)
		throw Area::bad_alloc();
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
	if(mem.ptr() == nullptr)
		throw Area::bad_alloc();
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
	return (void *)mem.ptr();
}

Area *Area::deep_copy(Area *other) {
	Area *copy = new Area(other->_dimensions.size.w, other->_dimensions.size.h, other->_type);
	size_t size_to_copy = copy->_dimensions.size.w * copy->_dimensions.size.h * type_to_sizeof(copy->_type);
	std::memcpy(copy->ptr(), other->ptr(), size_to_copy);
	copy->_dimensions = other->_dimensions;
	return copy;
}

int16_t Area::type_to_sizeof(Area::type_t t) {
	if(t == Area::type_t::float_p4)
		return sizeof(float) * 4;
	else if(t == Area::type_t::float_p3)
		return sizeof(float) * 3;
	else if(t == Area::type_t::float_p2)
		return sizeof(float) * 2;
	else if(t == Area::type_t::float_p1)
		return sizeof(float);
	else if(t == Area::type_t::float_p6)
		return sizeof(float) * 6;
	else if(t == Area::type_t::int16_p4)
		return sizeof(int16_t) * 4;
	else if(t == Area::type_t::int16_p3)
		return sizeof(int16_t) * 3;
	else if(t == Area::type_t::uint16_p4)
		return sizeof(uint16_t) * 4;
	else if(t == Area::type_t::uint8_p4)
		return sizeof(uint8_t) * 4;
	else if(t == Area::type_t::uint8_p3)
		return sizeof(uint8_t) * 3;
	return 0;
}

std::string Area::type_to_name(Area::type_t t) {
	if(t == Area::type_t::float_p4)
		return "type_float_p4";
	else if(t == Area::type_t::float_p3)
		return "type_float_p3";
	else if(t == Area::type_t::float_p2)
		return "type_float_p2";
	else if(t == Area::type_t::float_p6)
		return "type_float_p6";
	else if(t == Area::type_t::int16_p4)
		return "type_int16_p4";
	else if(t == Area::type_t::int16_p3)
		return "type_int16_p3";
	else if(t == Area::type_t::uint16_p4)
		return "type_uint16_p4";
	else if(t == Area::type_t::uint8_p4)
		return "type_uint8_p4";
	else if(t == Area::type_t::uint8_p3)
		return "type_uint8_p3";
	if(t == Area::type_t::float_p1)
		return "type_float_p1";
	return "unknown";
}

Area::type_t Area::type_for_format(Area::format_t format) {
	if(format == Area::format_t::rgba_32)
		return Area::type_t::float_p4;
	else if(format == Area::format_t::rgba_16)
		return Area::type_t::int16_p4;
	else if(format == Area::format_t::rgb_16)
		return Area::type_t::int16_p3;
	else if(format == Area::format_t::rgba_8 || format == Area::format_t::bgra_8)
		return Area::type_t::uint8_p4;
	else if(format == Area::format_t::rgb_8)
		return Area::type_t::uint8_p3;
	return Area::type_t::float_p4;
}

//==============================================================================
QImage Area::to_qimage(void) {
	int w = dimensions()->width();
	int h = dimensions()->height();
	if(_type == Area::type_t::uint8_p4)
		return QImage((uchar *)ptr(), w, h, w * 4, QImage::Format_ARGB32);
	if(_type == Area::type_t::uint8_p3)
		return QImage((uchar *)ptr(), w, h, w * 3, QImage::Format_RGB888);
	return QImage();
}

QPixmap Area::to_qpixmap(void) {
	if(_type != Area::type_t::uint8_p4)
		return QPixmap();
	int w = dimensions()->width();
	int h = dimensions()->height();
#if 1
	return QPixmap(QPixmap::fromImage(QImage((uchar *)ptr(), w, h, w * 4, QImage::Format_ARGB32))).copy();
#else
	// store JPEG files on local FS
	static int count = 0;
	count++;
	QPixmap px = QPixmap(QPixmap::fromImage(QImage((uchar *)ptr(), w, h, w * 4, QImage::Format_ARGB32)));
	char buf[128];
	sprintf(buf, "./%04d.jpeg", count);
	px.save(QString::fromStdString(buf), "jpeg", 95);
	return px;
#endif
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
	double px_size_x = (in_w / out_w) * d->position.px_size_x;
	double px_size_y = (in_h / out_h) * d->position.px_size_y;
//cerr << "Area::scale_dimensions_to_size(); pos in  == " << d->position.x << " - " << d->position.y << " - " << d->position.px_size_x << " - " <<  d->position.px_size_y << endl;
	double center_x = d->position.x - d->position.px_size_x * 0.5 + (in_w * d->position.px_size_x) * 0.5;
	double center_y = d->position.y - d->position.px_size_y * 0.5 + (in_h * d->position.px_size_y) * 0.5;
	d->position.x = center_x - double(clip_w) * px_size_x * 0.5 + px_size_x * 0.5;
	d->position.y = center_y - double(clip_h) * px_size_y * 0.5 + px_size_y * 0.5;
//cerr << "center_x == " << center_x << endl;
//cerr << "center_y == " << center_y << endl;
	d->position.px_size_x = px_size_x;
	d->position.px_size_y = px_size_y;
//cerr << "Area::scale_dimensions_to_size(); pos out == " << d->position.x << " - " << d->position.y << " - " << d->position.px_size_x << " - " <<  d->position.px_size_y << endl;
	d->size.w = clip_w;
	d->size.h = clip_h;
#if 0
cerr << "Area::scale_dimensions_to_size(); to_fit == " << to_fit << "; limits == " << limit_w << " x " << limit_h << endl;
cerr << "d->size.w == " << d->size.w << endl;
cerr << "d->size.h == " << d->size.h << endl;
cerr << "px_size_x == " << px_size_x << "; px_size_y == " << px_size_y << endl;
cerr << "position: " << d->position.x << " - " << d->position.y << "; " << d->size.w << " x " << d->size.h << endl;
cerr << endl;
#endif
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
	std::atomic_int *y_flow;

	float scale_x;
	float scale_y;
	float in_x_off;
	float in_y_off;
};

std::unique_ptr<Area> Area::scale(SubFlow *subflow, int out_w, int out_h, float out_scale_x, float out_scale_y) {
// TODO: check original 'px_size' asked by View and 'out_scale'
	// TODO: utilize t_dimensions::position, support of scaling with tiles
//cerr << "Area:scale(...) : " << (unsigned long)this << endl;
	std::unique_ptr<Area> area_out;
	std::vector<std::unique_ptr<scale_task_t>> tasks(0);
	std::unique_ptr<std::atomic_int> y_flow;

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

		if(type() == Area::type_t::uint8_p4)
			area_out = std::unique_ptr<Area>(new Area(out_w, out_h, Area::type_t::uint8_p4));
		else
			area_out = std::unique_ptr<Area>(new Area(out_w, out_h));
		area_out->dimensions()->position.x = d_in->position.x - d_in->position.px_size_x * 0.5 + out_scale_x * 0.5;
		area_out->dimensions()->position.y = d_in->position.y - d_in->position.px_size_y * 0.5 + out_scale_y * 0.5;
		area_out->dimensions()->position.px_size_x = out_scale_x;
		area_out->dimensions()->position.px_size_y = out_scale_y;

		// TODO: apply correct offsets/px_size to area_out
		D_AREA_PTR(area_out);
		const int threads_count = subflow->threads_count();
//		tasks = new scale_task_t *[threads_count];
		tasks.resize(threads_count);
		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<scale_task_t>(new scale_task_t);
			scale_task_t *task = tasks[i].get();

			task->area_in = this;
			task->area_out = area_out.get();
			task->scale_x = out_scale_x;
			task->scale_y = out_scale_y;
			task->y_flow = y_flow.get();
			task->in_x_off = in_x_off;
			task->in_y_off = in_y_off;

			subflow->set_private(task, i);
		}
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

	subflow->sync_point_post();
/*
	if(subflow->sync_point_pre()) {
		for(int i = 0; i < subflow->threads_count(); ++i)
			delete tasks[i];
		delete[] tasks;
		delete y_flow;
	}
	subflow->sync_point_post();
*/
//if(subflow->is_main())
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
	bool flag_8b = (area_in->type() == Area::type_t::uint8_p4);

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
		while((y = task->y_flow->fetch_add(1)) < out_h) {
			for(int x = 0; x < out_w; ++x) {
				int index_in = ((y + in_y_offset) * in_width + x + in_x_offset) * 4;
				int index_out = ((y + out_y_offset) * out_width + x + out_x_offset) * 4;
				_out[index_out + 0] = _in[index_in + 0];
				_out[index_out + 1] = _in[index_in + 1];
				_out[index_out + 2] = _in[index_in + 2];
				_out[index_out + 3] = _in[index_in + 3];
			}
		}
	} else {
		while((y = task->y_flow->fetch_add(1)) < out_h) {
			for(int x = 0; x < out_w; ++x) {
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
	bool flag_8b = (area_in->type() == Area::type_t::uint8_p4);
	const float scale_x = task->scale_x;
	const float scale_y = task->scale_y;
	const float w_div = scale_x * scale_y;
	int j = 0;
	while((j = task->y_flow->fetch_add(1)) < j_max) {
		int out_y = j;
		const float f_in_y = f_offset_y + scale_y * j;
		for(int i = 0; i < i_max; ++i) {
			int out_x = i;
			const float f_in_x = f_offset_x + scale_x * i;
			// accumulator
			float px[4];
			for(int k = 0; k < 4; ++k)
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

	bool flag_8b = (area_in->type() == Area::type_t::uint8_p4);
	const float scale_x = task->scale_x;
	const float scale_y = task->scale_y;
	int j = 0;
	while((j = task->y_flow->fetch_add(1)) < j_max) {
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
		for(int i = 0; i < i_max; ++i) {
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
			for(int k = 0; k < 4; ++k)
				px[k] = 0.0;
			// interpolation
			float w[4];
			w[0] = wx_0 * wy_0;
			w[1] = wx_1 * wy_0;
			w[2] = wx_0 * wy_1;
			w[3] = wx_1 * wy_1;
			if(flag_8b == false) {
				for(int c = 0; c < 4; ++c) {
					int offset_x = c % 2;
					int offset_y = c / 2;
					for(int k = 0; k < 4; ++k)
						px[k] += _in[(in_x + offset_x + in_x_min + (in_y + offset_y + in_y_min) * in_width) * 4 + k] * w[c];
				}
				for(int k = 0; k < 4; ++k)
					_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + k] = px[k];
			} else {
				for(int c = 0; c < 4; ++c) {
					int offset_x = c % 2;
					int offset_y = c / 2;
					for(int k = 0; k < 4; ++k)
						px[k] += w[c] * u_in[(in_x + offset_x + in_x_min + (in_y + offset_y + in_y_min) * in_width) * 4 + k] * 0xFF;
				}
				for(int k = 0; k < 4; ++k)
					u_out[((out_y + out_y_min) * out_width + out_x + out_x_min) * 4 + k] = px[k];
			}
		}
	}
}

//------------------------------------------------------------------------------
// one thread smooth downscale with kept aspect ration, for thumbnails
std::unique_ptr<Area> Area::scale(int scale_width, int scale_height, bool to_fit) {
	std::unique_ptr<Area> area_out;
	Area::t_dimensions d_out = this->_dimensions;
	float scale = 1.0;
	if(to_fit)
		scale = scale_dimensions_to_size_fit(&d_out, scale_width, scale_height);
	else
		scale = scale_dimensions_to_size_fill(&d_out, scale_width, scale_height);
	if(scale <= 1.0f) // upscale
		return std::unique_ptr<Area>(new Area(*this));

	area_out = std::unique_ptr<Area>(new Area(&d_out, this->_type));

	// perform downscale
	float *_in = (float *)this->ptr();
	float *_out = (float *)area_out->ptr();
	uint8_t *u_in = (uint8_t *)this->ptr();
	uint8_t *u_out = (uint8_t *)area_out->ptr();

	int in_x_min = this->dimensions()->edges.x1;
	int in_y_min = this->dimensions()->edges.y1;
	int in_w = this->dimensions()->width();
	int in_h = this->dimensions()->height();
	int in_width = this->mem_width();

	int out_x_min = area_out->dimensions()->edges.x1;
	int out_y_min = area_out->dimensions()->edges.y1;
	int out_width = area_out->mem_width();

	float out_scale_x = scale;
	float out_scale_y = scale;

	Area::t_dimensions *d_in = this->dimensions();
	float x_off = d_in->position.x - d_in->position.px_size_x * 0.5 + out_scale_x * 0.5;
	float y_off = d_in->position.y - d_in->position.px_size_y * 0.5 + out_scale_y * 0.5;
	int in_x_off = x_off;
	int in_y_off = y_off;
	float f_offset_x = x_off - in_x_off;
	float f_offset_y = y_off - in_y_off;

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
	bool flag_8b = (this->type() == Area::type_t::uint8_p4);
	const float scale_x = out_scale_x;
	const float scale_y = out_scale_y;
	const float w_div = scale_x * scale_y;
//	int j = 0;
	for(int j = 0; j < j_max; ++j) {
		int out_y = j;
		const float f_in_y = f_offset_y + scale_y * j;
		for(int i = 0; i < i_max; ++i) {
			int out_x = i;
			const float f_in_x = f_offset_x + scale_x * i;
			// accumulator
			float px[4];
			for(int k = 0; k < 4; ++k)
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
	return area_out;
}

//------------------------------------------------------------------------------
