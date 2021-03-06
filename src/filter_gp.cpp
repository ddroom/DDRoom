/*
 * filter_gp.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 *	NOTES:
 *	- to prevent downscaled cache abuse in workflow, use 'downscaled' rotation - when result image dimensions will be the same as original, w/o magnification.
 *	- after research there was made decision to avoid usage of the Sinc2 upscaling resampling algorithm as there is no visible improvement in the image quality but minor
 *		increase of the sharpness and possibly minor increase in the color noise too, with significant slowdown. In case of the downscale - there is a significant performance
 *		drop with no visible image improvement but barely visible sharpness increase (and artifacts too).
 */	

#include <iostream>

#include "filter_gp.h"
#include "process_h.h"
#include "ddr_math.h"

using namespace std;

//#define MARK_CORNERS
#undef MARK_CORNERS

//------------------------------------------------------------------------------
bool FP_GP::is_rgb(void) {
	return false;
}

bool FP_GP::to_clip(void) {
	return false;
}

void FP_GP::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	out_x = in_x;
	out_y = in_y;
}

void FP_GP::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	in_x = out_x;
	in_y = out_y;
}

void FP_GP::process_forward_rgb(const float *in, float *out) {
	for(int i = 0; i < 3; ++i)
		process_forward(in[i * 2 + 0], in[i * 2 + 1], out[i * 2 + 0], out[i * 2 + 1]);
}

void FP_GP::process_backward_rgb(float *in, const float *out) {
	for(int i = 0; i < 3; ++i)
		process_backward(in[i * 2 + 0], in[i * 2 + 1], out[i * 2 + 0], out[i * 2 + 1]);
}

//------------------------------------------------------------------------------
FilterProcess_GP::FilterProcess_GP(void) {
	_name = "Unknown FilterProcess_GP";
	_fp_type = FilterProcess::fp_type_gp;
}

FP_Cache_t *FilterProcess_GP::new_FP_Cache(void) {
	return new FP_Cache_t();
}

class FP_GP *FilterProcess_GP::get_new_FP_GP(const class FP_GP_data_t &data) {
	return nullptr;
}

//------------------------------------------------------------------------------
FilterProcess_GP_Wrapper::FilterProcess_GP_Wrapper(const vector<class FP_GP_Wrapper_record_t> &_vector) {
	fp_gp_vector = _vector;
	if(fp_gp_vector.size() == 0) {
		_name = "FP_GP_Wrapper for resampling";
	} else {
		_name = "FP_GP_Wrapper for filters: ";
		for(vector<class FP_GP_Wrapper_record_t>::iterator it = fp_gp_vector.begin(); it != fp_gp_vector.end(); ++it) {
			_name += (*it).fp_gp->name();
			if(it + 1 != fp_gp_vector.end())
				_name += ", ";
		}
	}
//cerr << "created GP_Wrapper: " << _name << endl;
}

FilterProcess_GP_Wrapper::~FilterProcess_GP_Wrapper(void) {
//cerr << "_____________________________________________    FilterProcess_GP_Wrapper::~FilterProcess_GP_Wrapper(void)" << endl;
	for(size_t i = 0; i < gp_vector.size(); ++i)
		delete gp_vector[i];
}

bool FilterProcess_GP_Wrapper::is_enabled(const PS_Base *) {
	return true;
}

void FilterProcess_GP_Wrapper::init_gp(class Metadata *metadata) {
	if(gp_vector.size() == 0) {
		for(size_t i = 0; i < fp_gp_vector.size(); ++i) {
			FP_GP_data_t data;
			data.metadata = metadata;
			data.filter = fp_gp_vector[i].filter;
			data.fs_base = fp_gp_vector[i].fs_base;
			data.ps_base = fp_gp_vector[i].ps_base.get();
			data.cache = fp_gp_vector[i].cache;
			gp_vector.push_back(fp_gp_vector[i].fp_gp->get_new_FP_GP(data));
		}
	}
}

void FilterProcess_GP_Wrapper::size_forward_point(float in_x, float in_y, bool *flag_min_max, float *x_min_max, float *y_min_max) {
	for(int c = 0; c < 3; ++c) {
		float in[6];
		float out[6];
		in[0] = in_x;
		in[1] = in_y;
		in[2] = in[0];
		in[3] = in[1];
		in[4] = in[0];
		in[5] = in[1];
		for(size_t i = 0; i < 6; ++i)
			out[i] = in[i];
		for(size_t i = 0; i < gp_vector.size(); ++i) {
			gp_vector[i]->process_forward_rgb(in, out);
			for(int k = 0; k < 6; ++k)
				in[k] = out[k];
		}
		if(flag_min_max[c]) {
			flag_min_max[c] = false;
			x_min_max[c * 2 + 0] = out[c * 2 + 0];
			x_min_max[c * 2 + 1] = out[c * 2 + 0];
			y_min_max[c * 2 + 0] = out[c * 2 + 1];
			y_min_max[c * 2 + 1] = out[c * 2 + 1];
		} else {
			if(x_min_max[c * 2 + 0] > out[c * 2 + 0]) x_min_max[c * 2 + 0] = out[c * 2 + 0];
			if(x_min_max[c * 2 + 1] < out[c * 2 + 0]) x_min_max[c * 2 + 1] = out[c * 2 + 0];
			if(y_min_max[c * 2 + 0] > out[c * 2 + 1]) y_min_max[c * 2 + 0] = out[c * 2 + 1];
			if(y_min_max[c * 2 + 1] < out[c * 2 + 1]) y_min_max[c * 2 + 1] = out[c * 2 + 1];
		}
	}
}
#if 0
void FilterProcess_GP_Wrapper::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	// NOTE: with RGB - shrink output size to minimal color (because there would be no valid input data)
	// px_size always is 1.0 here
	*d_after = *d_before;
	init_gp(fp_size->metadata);
	// transform coordinates
//cerr << "::size_forward()  -  GP_Wrapper" << endl;
//cerr << "_ d_before->position == " << d_before->position.x << "," << d_before->position.y << endl;
//cerr << "_ d_before->size     == " << d_before->width() << "x" << d_before->height() << endl;
	float px = d_before->position.x - 0.5;
	float py = d_before->position.y - 0.5;
//cerr << "px == " << px << endl;
//cerr << "py == " << py << endl;
	float x1 = px;
	float x2 = px + d_before->width();
	float y1 = py;
	float y2 = py + d_before->height();
	bool flag_min_max[3];
	float x_min_max[6];
	float y_min_max[6];
	for(int i = 0; i < 3; ++i)
		flag_min_max[i] = true;

	// search for min and max values for processed points at each edge
	for(float x = x1; x < x2 + 0.5; x += 1.0) {
		size_forward_point(x, y1, flag_min_max, x_min_max, y_min_max);
		size_forward_point(x, y2, flag_min_max, x_min_max, y_min_max);
	}
	for(float y = y1; y < y2 + 0.5; y += 1.0) {
		size_forward_point(x1, y, flag_min_max, x_min_max, y_min_max);
		size_forward_point(x2, y, flag_min_max, x_min_max, y_min_max);
	}
	// select the smallest size from each color plane
	float x__min = x_min_max[0];
	float x__max = x_min_max[1];
	float y__min = y_min_max[0];
	float y__max = y_min_max[1];
	for(int c = 1; c < 3; ++c) {
		x__min = (x__min > x_min_max[c * 2 + 0]) ? x__min : x_min_max[c * 2 + 0];
		x__max = (x__max < x_min_max[c * 2 + 1]) ? x__max : x_min_max[c * 2 + 1];
		y__min = (y__min > y_min_max[c * 2 + 0]) ? y__min : y_min_max[c * 2 + 0];
		y__max = (y__max < y_min_max[c * 2 + 1]) ? y__max : y_min_max[c * 2 + 1];
	}
	// save result coordinates
	d_after->size.w = ceil(x__max - x__min);// + 1;
	d_after->size.h = ceil(y__max - y__min);// + 1;
	d_after->position.x = x__min + 0.5;
	d_after->position.y = y__min + 0.5;
	d_after->edges.x1 = 0;
	d_after->edges.x2 = 0;
	d_after->edges.y1 = 0;
	d_after->edges.y2 = 0;
//cerr << "__ d_after->position == " << d_after->position.x << "," << d_after->position.y << endl;
//cerr << "__ d_after->size     == " << d_after->size.w << "x" << d_after->size.h << endl;
}
#endif

void FilterProcess_GP_Wrapper::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	// NOTE: with RGB - shrink output size to minimal color (because there would be no valid input data)
	// px_size always is 1.0 here
	*d_after = *d_before;
/*
cerr << "FilterProcess_GP_Wrapper::size_forward(),  in:" << endl;
const Area::t_dimensions *d = d_before;
cerr << "position == " << d->position.x << " - " << d->position.y << endl;
cerr << "edges:   == " << d->position.x - d->position.px_size_x * 0.5 << " - " << d->position.y - d->position.px_size_y * 0.5 << endl;
cerr << endl;
*/
	init_gp(fp_size->metadata);
	// transform coordinates
/*
cerr << "::size_forward()  -  GP_Wrapper" << endl;
cerr << "_ d_before->position == " << d_before->position.x << "," << d_before->position.y << endl;
cerr << "_ d_before->size     == " << d_before->width() << "x" << d_before->height() << endl;
*/
#if 0
	float px = d_before->position.x;
	float py = d_before->position.y;
	float x1 = px;
	float x2 = px + d_before->width() - 1.0;
	float y1 = py;
	float y2 = py + d_before->height() - 1.0;
#else
//	float px = d_before->position.x - 0.5;
//	float py = d_before->position.y - 0.5;
	float px = d_before->position.x;
	float py = d_before->position.y;
	float x1 = px;
	float x2 = px + d_before->width();
	float y1 = py;
	float y2 = py + d_before->height();
#endif
//cerr << "px == " << px << endl;
//cerr << "py == " << py << endl;
	// process sizes for each filter and fold accordingly
	for(size_t i = 0; i < gp_vector.size(); ++i) {
		bool to_clip = gp_vector[i]->to_clip();
		float p_min_in[6];
		float p_min_out[6];
		float p_max_in[6];
		float p_max_out[6];
		float x1_new = x1;
		float x2_new = x2;
		float y1_new = y1;
		float y2_new = y2;
		for(float x = x1; x < x2 + 0.5; x += 1.0) {
			p_min_in[0] = x;	p_min_in[2] = x;	p_min_in[4] = x;
			p_min_in[1] = y1;	p_min_in[3] = y1;	p_min_in[5] = y1;
			gp_vector[i]->process_forward_rgb(p_min_in, p_min_out);
			p_max_in[0] = x;	p_max_in[2] = x;	p_max_in[4] = x;
			p_max_in[1] = y2;	p_max_in[3] = y2;	p_max_in[5] = y2;
			gp_vector[i]->process_forward_rgb(p_max_in, p_max_out);
			if(x == x1) {
				y1_new = p_min_out[3];
				y2_new = p_max_out[3];
			}
			for(int j = 0; j < 3; ++j) {
				if(to_clip) {
					y1_new = (y1_new < p_min_out[j * 2 + 1]) ? p_min_out[j * 2 + 1] : y1_new;
					y2_new = (y2_new > p_max_out[j * 2 + 1]) ? p_max_out[j * 2 + 1] : y2_new;
				} else {
					y1_new = (y1_new > p_min_out[j * 2 + 1]) ? p_min_out[j * 2 + 1] : y1_new;
					y2_new = (y2_new < p_max_out[j * 2 + 1]) ? p_max_out[j * 2 + 1] : y2_new;
				}
			}
		}
		for(float y = y1; y < y2 + 0.5; y += 1.0) {
			p_min_in[0] = x1;	p_min_in[2] = x1;	p_min_in[4] = x1;
			p_min_in[1] = y;	p_min_in[3] = y;	p_min_in[5] = y;
			gp_vector[i]->process_forward_rgb(p_min_in, p_min_out);
			p_max_in[0] = x2;	p_max_in[2] = x2;	p_max_in[4] = x2;
			p_max_in[1] = y;	p_max_in[3] = y;	p_max_in[5] = y;
			gp_vector[i]->process_forward_rgb(p_max_in, p_max_out);
			if(y == y1) {
				x1_new = p_min_out[2];
				x2_new = p_max_out[2];
			}
			for(int j = 0; j < 3; ++j) {
				if(to_clip) {
					x1_new = (x1_new < p_min_out[j * 2]) ? p_min_out[j * 2] : x1_new;
					x2_new = (x2_new > p_max_out[j * 2]) ? p_max_out[j * 2] : x2_new;
				} else {
					x1_new = (x1_new > p_min_out[j * 2]) ? p_min_out[j * 2] : x1_new;
					x2_new = (x2_new < p_max_out[j * 2]) ? p_max_out[j * 2] : x2_new;
				}
			}
		}
		//--
/*
cerr << "x1 == " << x1 << endl;
cerr << "x2 == " << x2 << endl;
cerr << "y1 == " << y1 << endl;
cerr << "y2 == " << y2 << endl;
cerr << "to_clip == " << to_clip << endl;
cerr << "x1_new == " << x1_new << endl;
cerr << "x2_new == " << x2_new << endl;
cerr << "y1_new == " << y1_new << endl;
cerr << "y2_new == " << y2_new << endl;
*/
		x1 = x1_new;
		x2 = x2_new;
		y1 = y1_new;
		y2 = y2_new;
	}
	// save result coordinates
#if 0
	x1 = floor(x1 - 0.5) + 0.5;
	y1 = floor(y1 - 0.5) + 0.5;
	x2 = floor(x2 + 0.5) - 0.5;
	y2 = floor(y2 + 0.5) - 0.5;
	d_after->size.w = x2 - x1;
	d_after->size.h = y2 - y1;
	d_after->position.x = x1;
	d_after->position.y = y1;
#else
	d_after->size.w = ceil(x2 - x1);// + 1;
	d_after->size.h = ceil(y2 - y1);// + 1;
	d_after->position.x = x1;
	d_after->position.y = y1;
#endif
///*
	d_after->edges.x1 = 0;
	d_after->edges.x2 = 0;
	d_after->edges.y1 = 0;
	d_after->edges.y2 = 0;
//*/
//cerr << "__ d_after->position == " << d_after->position.x << "," << d_after->position.y << endl;
//cerr << "__ d_after->size     == " << d_after->size.w << "x" << d_after->size.h << endl;
/*
cerr << "FilterProcess_GP_Wrapper::size_forward(), out:" << endl;
const Area::t_dimensions *d1 = d_after;
cerr << "position == " << d1->position.x << " - " << d1->position.y << endl;
cerr << "edges:   == " << d1->position.x - d1->position.px_size_x * 0.5 << " - " << d1->position.y - d1->position.px_size_y * 0.5 << endl;
cerr << endl;
*/
}

void FilterProcess_GP_Wrapper::size_backward_point(float out_x, float out_y, bool *flag_min_max, float *x_min_max, float *y_min_max) {
	for(int c = 0; c < 3; ++c) {
		float in[6];
		float out[6];
		out[0] = out_x;
		out[1] = out_y;
		out[2] = out[0];
		out[3] = out[1];
		out[4] = out[0];
		out[5] = out[1];
		for(size_t i = 0; i < 6; ++i)
			in[i] = out[i];
		for(int i = gp_vector.size() - 1; i >= 0; --i) {
			gp_vector[i]->process_backward_rgb(in, out);
			for(size_t k = 0; k < 6; ++k)
				out[k] = in[k];
		}
		if(flag_min_max[c]) {
			flag_min_max[c] = false;
			x_min_max[c * 2 + 0] = out[c * 2 + 0];
			x_min_max[c * 2 + 1] = out[c * 2 + 0];
			y_min_max[c * 2 + 0] = out[c * 2 + 1];
			y_min_max[c * 2 + 1] = out[c * 2 + 1];
		} else {
			if(x_min_max[c * 2 + 0] > out[c * 2 + 0]) x_min_max[c * 2 + 0] = out[c * 2 + 0];
			if(x_min_max[c * 2 + 1] < out[c * 2 + 0]) x_min_max[c * 2 + 1] = out[c * 2 + 0];
			if(y_min_max[c * 2 + 0] > out[c * 2 + 1]) y_min_max[c * 2 + 0] = out[c * 2 + 1];
			if(y_min_max[c * 2 + 1] < out[c * 2 + 1]) y_min_max[c * 2 + 1] = out[c * 2 + 1];
		}
	}
}


void FilterProcess_GP_Wrapper::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	*d_before = *d_after;
//cerr << "::size_backward()  -  GP_Wrapper" << endl;
//cerr << "__ d_after->position == " << d_after->position.x << "," << d_after->position.y << endl;
//cerr << "__ d_after->size     == " << d_after->width() << "x" << d_after->height() << endl;
//cerr << "__ d_after->size     == " << d_after->size.w << "x" << d_after->size.h << endl;
	const double px_size_x = d_after->position.px_size_x;
	const double px_size_y = d_after->position.px_size_y;
	init_gp(fp_size->metadata);
	// increase asked input tile to 1px each side to make a seamless supersampling
	float px = d_after->position.x - 0.5 * px_size_x;
	float py = d_after->position.y - 0.5 * px_size_y;
	float x1 = px - px_size_x;
	float x2 = px + (1.0 + d_after->width()) * px_size_x;
	float y1 = py - px_size_y;
	float y2 = py + (1.0 + d_after->height()) * px_size_y;
	bool flag_min_max[3];
	float x_min_max[6];
	float y_min_max[6];
	for(int i = 0; i < 3; ++i)
		flag_min_max[i] = true;
	// search for min and max values for processed points at each edge
	for(float x = x1; x < x2 + 0.5; x += 1.0) {
		size_backward_point(x, y1, flag_min_max, x_min_max, y_min_max);
		size_backward_point(x, y2, flag_min_max, x_min_max, y_min_max);
	}
	for(float y = y1; y < y2 + 0.5; y += 1.0) {
		size_backward_point(x1, y, flag_min_max, x_min_max, y_min_max);
		size_backward_point(x2, y, flag_min_max, x_min_max, y_min_max);
	}
	// select the biggest size from each color plane
	float x__min = x_min_max[0];
	float x__max = x_min_max[1];
	float y__min = y_min_max[0];
	float y__max = y_min_max[1];
	for(int c = 1; c < 3; ++c) {
		x__min = (x__min < x_min_max[c * 2 + 0]) ? x__min : x_min_max[c * 2 + 0];
		x__max = (x__max > x_min_max[c * 2 + 1]) ? x__max : x_min_max[c * 2 + 1];
		y__min = (y__min < y_min_max[c * 2 + 0]) ? y__min : y_min_max[c * 2 + 0];
		y__max = (y__max > y_min_max[c * 2 + 1]) ? y__max : y_min_max[c * 2 + 1];
	}
	// return coordinates
	d_before->position.x = x__min + px_size_x * 0.5;
	d_before->position.y = y__min + px_size_y * 0.5;
	d_before->size.w = ceil((x__max - x__min) / px_size_x);// + 1;
	d_before->size.h = ceil((y__max - y__min) / px_size_y);// + 1;
	d_before->edges.x1 = 0;
	d_before->edges.x2 = 0;
	d_before->edges.y1 = 0;
	d_before->edges.y2 = 0;
//cerr << "size_backward, area with position: (" << d_before->position.x << ", " << d_before->position.y << "); and size: " << d_before->size.w << "x" << d_before->size.h << endl;
//cerr << "_ d_before->position == " << d_before->position.x << "," << d_before->position.y << endl;
//cerr << "_ d_before->size     == " << d_before->width() << "x" << d_before->height() << endl;
}

//==============================================================================
std::unique_ptr<Area> FilterProcess_GP_Wrapper::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	const bool just_copy = (fp_gp_vector.size() == 0 && process_obj->position.px_size_x == 1.0 && process_obj->position.px_size_y == 1.0);
	if(just_copy)
		return process_copy(mt_obj, process_obj, filter_obj);
	return process_sampling(mt_obj, process_obj, filter_obj);
}

//==============================================================================
class FilterProcess_GP_Wrapper::task_copy_t {
public:
	Area *area_in;  // (read only) input pixels
	Area *area_out; // (write only) resulting pixels
	std::atomic_int *y_flow;

	int in_x_offset;
	int in_y_offset;
	float wb_a[4];
	float wb_b[4];
};

std::unique_ptr<Area> FilterProcess_GP_Wrapper::process_copy(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;

	std::unique_ptr<Area> area_out;
	std::vector<std::unique_ptr<task_copy_t>> tasks(0);
	std::unique_ptr<std::atomic_int> y_flow;

	const int threads_count = subflow->threads_count();
	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;
//area_in->dump_ptr(__FILE__, __LINE__);

		Area::t_dimensions d_out = *area_in->dimensions();
		Tile_t::t_position &tp = process_obj->position;
		d_out.position.x = tp.x;
		d_out.position.y = tp.y;
		d_out.position._x_max = area_in->dimensions()->position._x_max;
		d_out.position._y_max = area_in->dimensions()->position._y_max;
		d_out.position.px_size_x = tp.px_size_x;
		d_out.position.px_size_y = tp.px_size_y;
		d_out.size.w = tp.width;
		d_out.size.h = tp.height;
		d_out.edges = Area::t_edges();
/*
		d_out.edges.x1 = 0;
		d_out.edges.x2 = 0;
		d_out.edges.y1 = 0;
		d_out.edges.y2 = 0;
*/
		area_out = std::unique_ptr<Area>(new Area(&d_out));
//area_out.get()->dump_ptr(__FILE__, __LINE__);

		int offset_x = tp.x - area_in->dimensions()->position.x;
		int offset_y = tp.y - area_in->dimensions()->position.y;

		double wb_a[3];
		double wb_b[3];
		for(int j = 0; j < 3; ++j) {
			wb_a[j] = 1.0;
			wb_b[j] = 0.0;
		}
		process_obj->mutators->get("wb_R_a", wb_a[0]);
		process_obj->mutators->get("wb_G_a", wb_a[1]);
		process_obj->mutators->get("wb_B_a", wb_a[2]);
		process_obj->mutators->get("wb_R_b", wb_b[0]);
		process_obj->mutators->get("wb_G_b", wb_b[1]);
		process_obj->mutators->get("wb_B_b", wb_b[2]);

		tasks.resize(threads_count);
		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
//cerr << "offset == " << offset_x << " - " << offset_y << endl;
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_copy_t>(new task_copy_t);
			task_copy_t *task = tasks[i].get();

			task->area_in = area_in;
			task->area_out = area_out.get();
			task->y_flow = y_flow.get();

			task->in_x_offset = offset_x;
			task->in_y_offset = offset_y;
			for(int j = 0; j < 3; ++j) {
				task->wb_a[j] = wb_a[j];
				task->wb_b[j] = wb_b[j];
			}
			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process_copy(subflow);

	subflow->sync_point();
	return area_out;
}

void FilterProcess_GP_Wrapper::process_copy(SubFlow *subflow) {
	task_copy_t *task = (task_copy_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->area_out;

	const int out_width = area_out->dimensions()->width();
	const int in_width = area_in->mem_width();

	const int out_x_max = area_out->dimensions()->width();
	const int out_y_max = area_out->dimensions()->height();
	const int in_x_min = area_in->dimensions()->edges.x1;
	const int in_x_max = area_in->dimensions()->width() + in_x_min;
	const int in_y_min = area_in->dimensions()->edges.y1;
	const int in_y_max = area_in->dimensions()->height() + in_y_min;

//cerr << "_w = " << _w << endl;
	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();

	const int in_x_offset = task->in_x_offset + in_x_min;
	const int in_y_offset = task->in_y_offset + in_y_min;
//cerr << "in_edges  == " << in_x_min << " - " << in_y_min << endl;
//cerr << "in_offset == " << in_x_offset << " - " << in_y_offset << endl;

	float color_pixel[16];
	color_pixel[ 0] = 0.5;
	color_pixel[ 1] = 0.5;
	color_pixel[ 2] = 0.5;
	// WARNING: keep that alpha at zero !!!
	color_pixel[ 3] = 0.0;
	color_pixel[ 4] = 1.0;
	color_pixel[ 5] = 0.0;
	color_pixel[ 6] = 0.0;
	color_pixel[ 7] = 0.75;
	color_pixel[ 8] = 0.0;
	color_pixel[ 9] = 1.0;
	color_pixel[10] = 0.0;
	color_pixel[11] = 0.75;

	float *empty_pixel = &color_pixel[0];
#ifdef MARK_CORNERS
	float *mark_lt_pixel = &color_pixel[4];
	float *mark_rb_pixel = &color_pixel[8];
#endif
	int it_y;
	auto y_flow = task->y_flow;
	while((it_y = y_flow->fetch_add(1)) < out_y_max) {
		for(int it_x = 0; it_x < out_x_max; ++it_x) {
			float *const out = &_out[(it_y * out_width + it_x) << 2];
			const int in_x = in_x_offset + it_x;
			const int in_y = in_y_offset + it_y;
			if(in_x < in_x_min || in_x >= in_x_max || in_y < in_y_min || in_y >= in_y_max) {
				out[0] = empty_pixel[0];
				out[1] = empty_pixel[1];
				out[2] = empty_pixel[2];
				out[3] = empty_pixel[3];
			} else  {
				float *const in = &_in[(in_y * in_width + in_x) << 2];
//				float *in = &_in[((in_y_offset + it_y) * in_width + in_x_offset + it_x) * 4 + 0];
				out[0] = in[0] * task->wb_a[0] + task->wb_b[0];
				out[1] = in[1] * task->wb_a[1] + task->wb_b[1];
				out[2] = in[2] * task->wb_a[2] + task->wb_b[2];
//				out[3] = 1.0f;
				out[3] = in[3];
			}
#ifdef MARK_CORNERS
			// mark corners
			const int mark_near = 2;
			const int mark_far = 31;//15;
			bool flag_mark_lt = false;
			bool flag_mark_rb = false;
			const int mx = out_x_max - 1;
			const int my = out_y_max - 1;
			if((it_y == 0 && it_x > mark_near && it_x < mark_far) || (it_y > mark_near && it_y < mark_far && it_x == 0))
				flag_mark_lt = true;
			if((it_y == 0 && it_x < mx - mark_near && it_x > mx - mark_far) || (it_y > my - mark_far && it_y < my - mark_near && it_x == 0))
				flag_mark_lt = true;
			if((it_y == my && it_x > mark_near && it_x < mark_far) || (it_y > mark_near && it_y < mark_far && it_x == mx))
				flag_mark_rb = true;
			if((it_y == my && it_x < mx - mark_near && it_x > mx - mark_far) || (it_y > my - mark_far && it_y < my - mark_near && it_x == mx))
				flag_mark_rb = true;
			if(flag_mark_lt)
				for(int i = 0; i < 4; ++i)
					out[i] = mark_lt_pixel[i];
			if(flag_mark_rb)
				for(int i = 0; i < 4; ++i)
					out[i] = mark_rb_pixel[i];
#endif
		}
	}
}

//==============================================================================
class FilterProcess_GP_Wrapper::task_coordinates_prep_t {
public:
	Area *area_out;	// (write only) area with initial coordinates, farther to demosaic
	std::atomic_int *y_flow;

	float start_x;
	float start_y;
	float delta_x;
	float delta_y;
};

class FilterProcess_GP_Wrapper::task_coordinates_t {
public:
	Area *area_in;  // (read only) area with coordinates to be changed, farther to demosaic
	Area *area_out;	// (write only) area with changed coordinates, nearer to demosaic
	bool coordinates_rgb;
	std::atomic_int *y_flow;
};

class FilterProcess_GP_Wrapper::task_sampling_t {
public:
	Area *area_in;  // (read only) input pixels
	Area *area_coordinates; // 'area_out' from 'task_coordinates_t'
	bool coordinates_rgb;
	Area *area_out; // (write only) resulting pixels
	std::atomic_int *y_flow;

	float px_size_x;
	float px_size_y;
	float offset_x;
	float offset_y;
};

std::unique_ptr<Area> FilterProcess_GP_Wrapper::process_sampling(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	if(gp_vector.size() == 0) {
		if(subflow->sync_point_pre()) {
			init_gp(process_obj->metadata);
		}
		subflow->sync_point_post();
	}

	std::unique_ptr<Area> area_coordinates_prep;
	std::vector<std::unique_ptr<task_coordinates_prep_t>> tasks_coordinates_prep(0);
	std::unique_ptr<std::atomic_int> y_flow_coordinates_prep;

	std::unique_ptr<Area> area_out_coordinates;
	std::vector<std::unique_ptr<task_coordinates_t>> tasks_coordinates(0);
	std::unique_ptr<std::atomic_int> y_flow_coordinates;
	bool coordinates_rgb = false;

	std::unique_ptr<Area> area_out_sampling;
	std::vector<std::unique_ptr<task_sampling_t>> tasks_sampling(0);
	std::unique_ptr<std::atomic_int> y_flow_sampling;

	float px_size_in_x = 1.0;
	float px_size_in_y = 1.0;
	float px_size_out_x = 1.0;
	float px_size_out_y = 1.0;
	if(subflow->is_main()) {
		px_size_in_x = area_in->dimensions()->position.px_size_x;
		px_size_in_y = area_in->dimensions()->position.px_size_y;
		px_size_out_x = process_obj->position.px_size_x;
		px_size_out_y = process_obj->position.px_size_y;
	}
	bool resampling_only = (gp_vector.size() == 0);

	const int threads_count = subflow->threads_count();
	// prepare coordinates
	if(subflow->sync_point_pre()) {
		Area::t_dimensions d_out = *area_in->dimensions();
		Tile_t::t_position &tp = process_obj->position;
		// add 1px. strip for each edge on purpose - to simplify recalculations at rescaling stage
		d_out.position.x = tp.x - px_size_out_x;
		d_out.position.y = tp.y - px_size_out_y;
		d_out.size.w = tp.width + int(px_size_out_x + 1.0) * 2;
		d_out.size.h = tp.height + int(px_size_out_y + 1.0) * 2;
		d_out.position._x_max = area_in->dimensions()->position._x_max;
		d_out.position._y_max = area_in->dimensions()->position._y_max;
		d_out.position.px_size_x = px_size_out_x;
		d_out.position.px_size_y = px_size_out_y;
		d_out.edges.reset();

		area_coordinates_prep = std::unique_ptr<Area>(new Area(&d_out, Area::type_t::float_p2));

		float start_x = d_out.position.x;
		float start_y = d_out.position.y;
		float delta_x = px_size_out_x;
		float delta_y = px_size_out_y;

		y_flow_coordinates_prep = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		tasks_coordinates_prep.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks_coordinates_prep[i] = std::unique_ptr<task_coordinates_prep_t>(new task_coordinates_prep_t);
			task_coordinates_prep_t * task = tasks_coordinates_prep[i].get();

			task->area_out = area_coordinates_prep.get();
			task->y_flow = y_flow_coordinates_prep.get();
			task->start_x = start_x;
			task->start_y = start_y;
			task->delta_x = delta_x;
			task->delta_y = delta_y;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	prepare_coordinates(subflow);

	// process coordinates
	if(!resampling_only) {
		if(subflow->sync_point_pre()) {
			Area::t_dimensions d_out = *area_in->dimensions();
			Tile_t::t_position &tp = process_obj->position;
			d_out.position.x = tp.x - px_size_out_x;
			d_out.position.y = tp.y - px_size_out_y;
			d_out.size.w = tp.width + int(px_size_out_x + 1.0) * 2;
			d_out.size.h = tp.height + int(px_size_out_y + 1.0) * 2;
			d_out.position._x_max = area_in->dimensions()->position._x_max;
			d_out.position._y_max = area_in->dimensions()->position._y_max;
			d_out.position.px_size_x = px_size_out_x;
			d_out.position.px_size_y = px_size_out_y;
			d_out.edges.reset();

			for(size_t i = 0; i < gp_vector.size(); ++i)
				coordinates_rgb |= gp_vector[i]->is_rgb();
			Area::type_t area_type = coordinates_rgb ? Area::type_t::float_p6 : Area::type_t::float_p2;
			area_out_coordinates = std::unique_ptr<Area>(new Area(&d_out, area_type));

			y_flow_coordinates = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
			tasks_coordinates.resize(threads_count);
			for(int i = 0; i < threads_count; ++i) {
				tasks_coordinates[i] = std::unique_ptr<task_coordinates_t>(new task_coordinates_t);
				task_coordinates_t *task = tasks_coordinates[i].get();

				task->area_in = area_coordinates_prep.get();
				task->area_out = area_out_coordinates.get();
				task->coordinates_rgb = coordinates_rgb;
				task->y_flow = y_flow_coordinates.get();

				subflow->set_private(task, i);
			}
		}
		subflow->sync_point_post();
		process_coordinates(subflow);
	}

	// do supersampling
	if(subflow->sync_point_pre()) {
		Area::t_dimensions d_out = *area_in->dimensions();
		Tile_t::t_position &tp = process_obj->position;
		d_out.position.x = tp.x;
		d_out.position.y = tp.y;
		d_out.position._x_max = area_in->dimensions()->position._x_max;
		d_out.position._y_max = area_in->dimensions()->position._y_max;
		d_out.position.px_size_x = px_size_out_x;
		d_out.position.px_size_y = px_size_out_y;
		d_out.size.w = tp.width;
		d_out.size.h = tp.height;
		d_out.edges.reset();

		area_out_sampling = std::unique_ptr<Area>(new Area(&d_out));
		float offset_x = area_in->dimensions()->position.x - 0.5 * px_size_in_x;
		float offset_y = area_in->dimensions()->position.y - 0.5 * px_size_in_y;
		float px_size_x = px_size_in_x;
		float px_size_y = px_size_in_y;

		y_flow_sampling = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		tasks_sampling.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks_sampling[i] = std::unique_ptr<task_sampling_t>(new task_sampling_t);
			task_sampling_t *task = tasks_sampling[i].get();

			task->area_in = area_in;
			if(!resampling_only) {
				task->area_coordinates = area_out_coordinates.get();
				task->coordinates_rgb = coordinates_rgb;
			} else {
				task->area_coordinates = area_coordinates_prep.get();
				task->coordinates_rgb = false;
			}
			task->area_out = area_out_sampling.get();
			task->y_flow = y_flow_sampling.get();
			task->px_size_x = px_size_x;
			task->px_size_y = px_size_y;
			task->offset_x = offset_x;
			task->offset_y = offset_y;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process_sampling(subflow);
	subflow->sync_point();

	return area_out_sampling;
}

//------------------------------------------------------------------------------
void FilterProcess_GP_Wrapper::prepare_coordinates(SubFlow *subflow) {
	task_coordinates_prep_t *task = (task_coordinates_prep_t *)subflow->get_private();
	// size of area_in and area_out should be the same in here
	Area *area_out = task->area_out;

	const int out_width = area_out->dimensions()->width();
	const int out_x_max = area_out->dimensions()->width();
	const int out_y_max = area_out->dimensions()->height();
//cerr << "out_width == " << area_out->dimensions()->width() << endl;
//cerr << "out_height == " << area_out->dimensions()->height() << endl;

	float *_out = (float *)area_out->ptr();

	const float start_x = task->start_x;
	const float start_y = task->start_y;
	const float delta_x = task->delta_x;
	const float delta_y = task->delta_y;
	int it_y;
	int it_y_prev = 0;
	float value_y = start_y;
	auto y_flow = task->y_flow;
	while((it_y = y_flow->fetch_add(1)) < out_y_max) {
		value_y += delta_y * (it_y - it_y_prev);
		it_y_prev = it_y;
		float value_x = start_x;
		for(int it_x = 0; it_x < out_x_max; ++it_x) {
			float *rez = &_out[(it_y * out_width + it_x) * 2];
			rez[0] = value_x;
			rez[1] = value_y;
			value_x += delta_x;
//			rez[0] = start_x + delta_x * it_x;
//			rez[1] = start_y + delta_y * it_y;
/*
if(it_y == 3 && it_x < 5)
cerr << "x == " << rez[0] << endl;
*/
		}
	}
}

void FilterProcess_GP_Wrapper::process_coordinates(SubFlow *subflow) {
	task_coordinates_t *task = (task_coordinates_t *)subflow->get_private();
	// size of area_in and area_out should be the same in here
	Area *area_in = task->area_in;
	Area *area_out = task->area_out;

	const int out_width = area_out->dimensions()->width();
//	int out_height = area_out->dimensions()->height();
	const int out_x_max = area_out->dimensions()->width();
	const int out_y_max = area_out->dimensions()->height();

//	int _w = area_in->mem_width();
//cerr << "_w = " << _w << endl;
	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();

	int it_y;
	const int j_max = gp_vector.size() - 1;
	if(task->coordinates_rgb) {
		auto y_flow = task->y_flow;
		while((it_y = y_flow->fetch_add(1)) < out_y_max) {
			for(int it_x = 0; it_x < out_x_max; ++it_x) {
				float in[6];
				float out[6];
				out[0] = _in[(it_y * out_width + it_x) * 2 + 0];
				out[1] = _in[(it_y * out_width + it_x) * 2 + 1];
				bool c_rgb = false;
				for(int j = j_max; j >= 0; j--) {
					bool c_rgb_prev = c_rgb;
					c_rgb |= gp_vector[j]->is_rgb();
					if(c_rgb) {
						if(!c_rgb_prev) {
							out[2] = out[0];
							out[3] = out[1];
							out[4] = out[0];
							out[5] = out[1];
						}
						gp_vector[j]->process_backward_rgb(in, out);
						for(int i = 0; i < 6; ++i)
							out[i] = in[i];
					} else {
						gp_vector[j]->process_backward(in[0], in[1], out[0], out[1]);
						out[0] = in[0];
						out[1] = in[1];
					}
				}
				for(int i = 0; i < 6; ++i)
					_out[(it_y * out_width + it_x) * 6 + i] = in[i];
			}
		}
	} else {
		auto y_flow = task->y_flow;
		while((it_y = y_flow->fetch_add(1)) < out_y_max) {
			for(int it_x = 0; it_x < out_x_max; ++it_x) {
				float in[2];
				float out[2];
				out[0] = _in[(it_y * out_width + it_x) * 2 + 0];
				out[1] = _in[(it_y * out_width + it_x) * 2 + 1];
				for(int j = j_max; j >= 0; j--) {
					gp_vector[j]->process_backward(in[0], in[1], out[0], out[1]);
					out[0] = in[0];
					out[1] = in[1];
				}
				_out[(it_y * out_width + it_x) * 2 + 0] = in[0];
				_out[(it_y * out_width + it_x) * 2 + 1] = in[1];
			}
		}
	}
}

// - size_backward: ask input area size as for output area with edge == 1px;
// - coordinates processing - prepare coordinates for output area with edge == 1px
//      for additional pixels at edges for correct samping. i.e.:
//        - pixels for windowing;
//        - coordinates for correct window size;

// Supersampling; used:
//  - for upsampling, bilinear interpolation
//  - for downsampling, windowed with 'boxcar' function
// TODO:
//  - use check for edges at area with pixels now, use mirroring on edge to 1 px. later, to skip check (?)
//  - for testing purposes: add color rendering of pixels where window is clipped
//  - try other windows for downsampling - done, no need to change something
// NOTE: never ever use ceil()/floor() instead of int/float conversions
void FilterProcess_GP_Wrapper::process_sampling(SubFlow *subflow) {
	task_sampling_t *task = (task_sampling_t *)subflow->get_private();
//	PS_Shift *ps = task->ps;
	Area *area_in = task->area_in;
	Area *area_out = task->area_out;
	Area *area_coordinates = task->area_coordinates;

	const int out_width = area_out->dimensions()->width();
	const int coords_width = area_coordinates->dimensions()->width();

	const int out_x_max = area_out->dimensions()->width();
	const int out_y_max = area_out->dimensions()->height();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int in_x1 = area_in->dimensions()->edges.x1;
	const int in_y1 = area_in->dimensions()->edges.y1;
	const int in_x2 = area_in->dimensions()->width() + in_x1;// - 4;
	const int in_y2 = area_in->dimensions()->height() + in_y1;// - 4;

//cerr << "in_x1 == " << in_x1 << endl;
//cerr << "in_y1 == " << in_y1 << endl;

	const int _w = area_in->mem_width();
	float *_in = (float *)area_in->ptr();
	float *_out = (float *)area_out->ptr();
	float *_coordinates = (float *)area_coordinates->ptr();

	float empty_pixel[4] = {0.5, 0.5, 0.5, 0.0};
//	float empty_pixel[4] = {1.0, 1.0, 1.0, 0.5};

#ifdef MARK_CORNERS
	float mark_lt_pixel[4] = {1.0, 0.0, 0.0, 0.75};
	float mark_rb_pixel[4] = {0.0, 1.0, 0.0, 0.75};
#endif

	int it_y;
	const float px_size_x = task->px_size_x;
	const float px_size_y = task->px_size_y;
	const float offset_x = task->offset_x;
	const float offset_y = task->offset_y;
	const int rgb_count = task->coordinates_rgb ? 4 : 1;
	const int rgb_size = task->coordinates_rgb ? 6 : 2;
	auto y_flow = task->y_flow;
	while((it_y = y_flow->fetch_add(1)) < out_y_max) {
		for(int it_x = 0; it_x < out_x_max; ++it_x) {
			float *rez = &_out[(it_y * out_width + it_x) * 4];
			float px_sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			for(int k = 0; k < rgb_count; ++k) {
				int rgb_offset = 2 * k;
				// use coordinates for green channel for alpha channel for now
				if(k == 3)
					rgb_offset = 2;
				// window boundaries
				const int cit_x = it_x + 1;
				const int cit_y = it_y + 1;
				float px1 = _coordinates[(cit_y * coords_width + cit_x - 1) * rgb_size + rgb_offset + 0];
				float _px = _coordinates[(cit_y * coords_width + cit_x    ) * rgb_size + rgb_offset + 0];
				float px2 = _coordinates[(cit_y * coords_width + cit_x + 1) * rgb_size + rgb_offset + 0];
				float py1 = _coordinates[((cit_y - 1) * coords_width + cit_x) * rgb_size + rgb_offset + 1];
				float _py = _coordinates[((cit_y    ) * coords_width + cit_x) * rgb_size + rgb_offset + 1];
				float py2 = _coordinates[((cit_y + 1) * coords_width + cit_x) * rgb_size + rgb_offset + 1];
				px1 = (px1 + _px) * 0.5f;
				px2 = (_px + px2) * 0.5f;
				py1 = (py1 + _py) * 0.5f;
				py2 = (_py + py2) * 0.5f;
				float x1 = (px1 - offset_x) / px_size_x;
				float x2 = (px2 - offset_x) / px_size_x;
				float y1 = (py1 - offset_y) / px_size_y;
				float y2 = (py2 - offset_y) / px_size_y;
				float lx = x2 - x1;
				float ly = y2 - y1;
				float xst = x1;
				float yst = y1;
				bool flag_to_skip = false;
				// X
				float wx = 1.0f - (xst - floor(xst));
				if(lx < 1.0f)
					lx = 1.0f;
				int ix1 = floor(xst);
				int ix2 = floor(xst + lx);
				ix1 += in_x_offset;
				ix2 += in_x_offset;
				if(ix2 < in_x1 || ix1 >= in_x2)
					flag_to_skip = true;
				// Y
				float wy = 1.0f - (yst - floor(yst));
				if(ly < 1.0f)
					ly = 1.0f;
				int iy1 = int(yst);
				int iy2 = int(yst + ly);
				iy1 += in_y_offset;
				iy2 += in_y_offset;
				if(iy2 < in_y1 || iy1 >= in_y2)
					flag_to_skip = true;
				// --==--
				// empty pixels
				if(flag_to_skip) {
					for(int i = 0; i < 4; ++i)
						rez[i] = empty_pixel[i];
					continue;
				}
				// supersampling
				float w_sum = 0.0f;
				float w_sum_alpha = 0.0f;
				float w_y = wy;
				if(w_y < 0.0f)
					w_y = -w_y;
				float l_y = ly;
				for(int y = iy1; y <= iy2; ++y) {
					float w_x = wx;
					if(w_x < 0.0f)
						w_x = -w_x;
					float l_x = lx;
					for(int x = ix1; x <= ix2; ++x) {
						float w = w_x * w_y;
						l_x -= w_x;
						w_x = (l_x > 1.0f) ? 1.0f : l_x;
						if(x >= in_x1 && x < in_x2 && y >= in_y1 && y < in_y2) {
							w_sum += w;
							if(task->coordinates_rgb) {
								if(k == 3) {
									px_sum[3] += 1.0f * w;
								} else {
									px_sum[k] += _in[(y * _w + x) * 4 + k] * w;
								}
							} else {
								for(int i = 0; i < 3; ++i)
									px_sum[i] += _in[(y * _w + x) * 4 + i] * w;
								px_sum[3] += 1.0f * w;
							}
						}
						w_sum_alpha += w;
					}
					l_y -= w_y;
					w_y = (l_y > 1.0f) ? 1.0f : l_y;
				}
				if(task->coordinates_rgb) {
					if(k == 3) {
						rez[k] = px_sum[k] / w_sum_alpha;
					} else {
						rez[k] = px_sum[k] / w_sum;
					}
				} else {
					for(int i = 0; i < 3; ++i)
						rez[i] = px_sum[i] / w_sum;
					rez[3] = px_sum[3] / w_sum_alpha;
				}
			}
#ifdef MARK_CORNERS
			// mark corners
			const int mark_near = 2;
			const int mark_far = 31;//15;
			bool flag_mark_lt = false;
			bool flag_mark_rb = false;
			const int mx = out_x_max - 1;
			const int my = out_y_max - 1;
			if((it_y == 0 && it_x > mark_near && it_x < mark_far) || (it_y > mark_near && it_y < mark_far && it_x == 0))
				flag_mark_lt = true;
			if((it_y == 0 && it_x < mx - mark_near && it_x > mx - mark_far) || (it_y > my - mark_far && it_y < my - mark_near && it_x == 0))
				flag_mark_lt = true;
			if((it_y == my && it_x > mark_near && it_x < mark_far) || (it_y > mark_near && it_y < mark_far && it_x == mx))
				flag_mark_rb = true;
			if((it_y == my && it_x < mx - mark_near && it_x > mx - mark_far) || (it_y > my - mark_far && it_y < my - mark_near && it_x == mx))
				flag_mark_rb = true;
			if(flag_mark_lt)
				for(int i = 0; i < 4; ++i)
					rez[i] = mark_lt_pixel[i];
			if(flag_mark_rb)
				for(int i = 0; i < 4; ++i)
					rez[i] = mark_rb_pixel[i];
#endif
			if(rez[3] > 0.99f)
				rez[3] = 1.0f;
			if(rez[3] < 0.01f) { // avoid 'isnan' for unprocessed transparent pixels
				rez[0] = 0.0f;
				rez[1] = 0.0f;
				rez[2] = 0.0f;
				rez[3] = 0.0f;
			}
//			rez[3] = 0.5;
		}
	}
}

//==============================================================================
