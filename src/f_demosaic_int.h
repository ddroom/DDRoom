#ifndef __F_DEMOSAIC__INT__H__
#define __F_DEMOSAIC__INT__H__
/*
 * f_demosaic_int.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <atomic>
#include <iostream>

#include "demosaic_pattern.h"
#include "f_demosaic.h"
#include "ddr_math.h"

#define DIRECTIONS_SMOOTH

//------------------------------------------------------------------------------
class TF_CIELab : public TableFunction {
public:
	TF_CIELab(void) {
		_init(0.0, 1.95, 8192);
//		_init(0.0, 1.95, 512);
//		_init(0.0, 1.95, 256);
	}
protected:
	float function(float x) {
		return powf(x, 1.0 / 3.0);
	}
};

class FP_Demosaic : public FilterProcess_2D {
public:
	FP_Demosaic(void);
	~FP_Demosaic();

	bool is_enabled(const PS_Base *);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	
protected:
	void edges_from_CA(int &edge_x, int &edge_y, int width, int height, const class PS_Demosaic *ps);

	class task_t;
	void process_bayer_CA(class SubFlow *);
	void process_bayer_CA_sinc1(class SubFlow *);
	void process_bayer_CA_sinc2(class SubFlow *);
	void process_square(class SubFlow *);
	void process_bilinear(class SubFlow *);
	void process_DG(class SubFlow *);
	void process_AHD(class SubFlow *);
	void process_denoise_wrapper(class SubFlow *);
	float *process_denoise_(class SubFlow *);
	float *process_denoise(class SubFlow *);
	void process_gaussian(class SubFlow *);
	void fuji_45_rotate(class SubFlow *);
	void process_xtrans(class SubFlow *);

	static class TF_CIELab tf_cielab;

	void _init(void);
	static float *kernel_g5x5;
	static float *kernel_rb5x5;
};

//------------------------------------------------------------------------------

class FP_Demosaic::task_t {
public:
	int width;
	int height;
	float *rgba;
	float *bayer;
	int bayer_pattern;

	int32_t x_min;
	int32_t x_max;
	int32_t y_min;
	int32_t y_max;

	class PS_Demosaic *ps;

	float noise_std_dev[4];
//	float black_level[4];

	float c_scale[3];
	float cRGB_to_XYZ[9]; // 3x3 matrix
	float *v_signal;

	// noise analysis
	float *noise_data; // 2 planes: 1. GREEN gaussian 5x5; 2. GREEN std_dev (real signal to gaussian, 5x5);
	float noise_std_dev_min; // minimal GREEN std_dev of delta 'gaussian - ofiginal', considered as noise std_dev;
	void *_tasks; // to synchronize noise_std_dev_min between threads;
	float bayer_import_prescale[4];
//	float black_offset;
	float *gaussian; // 4 planes: red, green, blue, unused - gaussian filtered low-pass signal from bayer
	float max_red;
	float max_green;
	float max_blue;
	float *dn1; // for hot / cold pixels suspension
	float *dn2; // for denoise reduction

	// X-Trans
	class Area *area_in;
	class Area *area_out;
	const class Metadata *metadata;
	int xtrans_passes;

	// DG
	float *D; // 4 planes, with green reconstructed in 4 directions
	float *sm_temp;
	long *dd_hist;
	long dd_hist_size;
	float dd_hist_scale;
	float dd_limit;

	// AHD
	float *fH;
	float *fV;
	float *lH;
	float *lV;

	// Fuji 45 rotation
	class Area *fuji_45_area;
	class Fuji_45 *fuji_45;
//	int fuji_45_width;
	std::atomic_int *fuji_45_flow;
};

//------------------------------------------------------------------------------
struct rgba_t {
	float red;
	float green;
	float blue;
	float alpha;
};

inline float &_value(int w, int h, int x, int y, float *m) {
	return m[(x + 2) + (y + 2) * (w + 4)];
}

// width == real_width_in_memory_minus_4
// height == real_height_in_memory_minus_4
template <class POINT> void mirror_2(int width, int height, POINT *m) {
	const int w4 = width + 4;
	const int h_2 = height + 2;
	// mirror for left and right sides
	for(int y = 2; y < h_2; y++) {
		m[y * w4 + 0] = m[y * w4 + 4];
		m[y * w4 + 1] = m[y * w4 + 3];
		m[y * w4 + w4 - 1] = m[y * w4 + w4 - 5];
		m[y * w4 + w4 - 2] = m[y * w4 + w4 - 4];
	}
	// mirror for top and bottom sides
	for(int x = 0; x < w4; x++) {
		m[x + 0 * w4] = m[x + 4 * w4];
		m[x + 1 * w4] = m[x + 3 * w4];
		m[x + (height + 3) * w4] = m[x + (height - 1) * w4];
		m[x + (height + 2) * w4] = m[x + (height) * w4];
	}
/*
	// mirror for left and right sides
	for(int y = 0; y < height; y++) {
		m[0 + (y + 2) * (width + 4)] = m[4 + (y + 2) * (width + 4)];
		m[1 + (y + 2) * (width + 4)] = m[3 + (y + 2) * (width + 4)];
		m[width + 3 + (y + 2) * (width + 4)] = m[width - 1 + (y + 2) * (width + 4)];
		m[width + 2 + (y + 2) * (width + 4)] = m[width + (y + 2) * (width + 4)];
	}
	// mirror for top and bottom sides
	for(int x = 0; x < (width + 4); x++) {
		m[x + 0 * (width + 4)] = m[x + 4 * (width + 4)];
		m[x + 1 * (width + 4)] = m[x + 3 * (width + 4)];
		m[x + (height + 3) * (width + 4)] = m[x + (height - 1) * (width + 4)];
		m[x + (height + 2) * (width + 4)] = m[x + (height) * (width + 4)];
	}
*/
}

inline void clip(float &v, const float &l1, const float &l2) {
	float min;
	float max;
	if(l1 < l2) {
		min = l1;
		max = l2;
	} else {
		min = l2;
		max = l1;
	}
	if(v < min)	v = min;
	if(v > max)	v = max;
}

inline void clip(float &v) {
	if(v < 0.0) v = 0.0;
	if(v > 1.0) v = 1.0;
}
//------------------------------------------------------------------------------

#endif // __F_DEMOSAIC__INT__H__
