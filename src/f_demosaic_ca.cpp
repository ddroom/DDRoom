/*
 * f_demosaic_ca.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "demosaic_pattern.h"
#include "f_demosaic.h"
#include "mt.h"
//#include "system.h"
//#include "ddr_math.h"
#include "f_demosaic_int.h"

using namespace std;

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[3];
			float wym[3];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1;
				wm[0] = 1.0;
				if(px_size < 1.0) {	// upscaling - interpolation
					*in2 = *in1 + 2;
					wm[0] = 1.0 - fw;
					wm[1] = fw;
				}
				if(px_size > 1.0) {
					wm[0] = 1.0 - fw;
					fw = px_size - wm[0];
					if(fw <= 1.0) {
						*in2 = *in1 + 2;
						wm[1] = fw;
					} else {
						wm[1] = 1.0;
						wm[2] = fw - 1.0;
						*in2 = *in1 + 4;
					}
				}
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset + offset_y) * in_w + i + in_x_offset + offset_x] * w;
						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
			out[out_index] = v / w_sum;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA_sinc1(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
//			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
//				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
//				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[2];
			float wym[2];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1 + 2;
				wm[0] = (*task->tf_sinc1)(-fw);
				wm[1] = (*task->tf_sinc1)(1.0 - fw);
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset + offset_y) * in_w + i + in_x_offset + offset_x] * w;
						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
			out[out_index] = v / w_sum;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA_sinc2(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
//			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
//				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
//				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[4];
			float wym[4];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1 + 2;
				*in1 -= 2;
				*in2 += 2;
				wm[0] = (*task->tf_sinc2)(-fw - 1.0);
				wm[1] = (*task->tf_sinc2)(-fw);
				wm[2] = (*task->tf_sinc2)(1.0 - fw);
				wm[3] = (*task->tf_sinc2)(2.0 - fw);
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			float limits[4];
			int limits_index = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						float value = in[(j + in_y_offset) * in_w + i + in_x_offset];
						if(i > ix1 && i < ix2 && j > iy1 && j < iy2) {
							limits[limits_index] = value;
							++limits_index;
						}
						v += value * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
//			out[out_index] = v / w_sum;
			v = v / w_sum;
//			if(v < 0.0) v = 0.0;
			float limits_min = limits[0];
			float limits_max = limits[0];
			for(int i = 0; i < limits_index; ++i) {
				if(limits_min > limits[i]) limits_min = limits[i];
				if(limits_max < limits[i]) limits_max = limits[i];
			}
			if(v < limits_min) v = limits_min;
			if(v > limits_max) v = limits_max;
			out[out_index] = v;
		}
	}
}

//------------------------------------------------------------------------------
