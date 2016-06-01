/*
 * f_demosaic_ahd.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * Adaptive Homogeneity-Directed interpolation is based on
 * the work of Keigo Hirakawa, Thomas Parks, and Paul Lee.
 *
 * ported from dcraw.c as reference
*/

#include <algorithm>
#include "f_demosaic_int.h"

//------------------------------------------------------------------------------
struct rgb_t {
	float red;
	float green;
	float blue;
	float w;
};

struct Lab_t {
	float L;
	float a;
	float b;
};

inline float _square(const float &arg) {
	return arg * arg;
}

inline float _rec(float c_low, float b, float b_low) {
	// limits below looks good on Canon 350D/40D
	const float edge_low = 3.0 / 64.0;
	const float edge_high = 6.0 / 64.0;
	const float r_sum = c_low + b - b_low;
	if(b_low != 0.0) {
		if(b_low < edge_low)
			return r_sum;
		const float r_div = c_low + ((b - b_low) / b_low) * c_low;
		if(b_low > edge_high)
			return r_div;
		const float part = (b_low - edge_low) / (edge_high - edge_low);
		return r_div * part + r_sum * (1.0 - part);
	}
	return r_sum;
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_AHD(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;
	float *rgba = task->rgba;
	int bayer_pattern = task->bayer_pattern;

	float *fH = task->fH;
	float *fV = task->fV;
	rgb_t *_fH = (rgb_t *)fH;
	rgb_t *_fV = (rgb_t *)fV;
	float *lH = task->lH;
	float *lV = task->lV;
	Lab_t *_lH = (Lab_t *)lH;
	Lab_t *_lV = (Lab_t *)lV;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	float *_rgba = rgba;
	int w3 = (width + 4) * 3;
	int w4 = (width + 4) * 4;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	//== interpolate GREEN H and V
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			if(s == p_green_r || s == p_green_b) {
				float c = _value(width, height, x, y, bayer);
				fH[k4 + 1] = c;
				fV[k4 + 1] = c;
			} else {
				int ci = (s == p_red) ? 0 : 2;
				// s == p_red || s == p_blue
				float c = _value(width, height, x, y,  bayer);

				float c1 = _value(width, height, x - 2, y, bayer);
				float g1 = _value(width, height, x - 1, y, bayer);
				float g2 = _value(width, height, x + 1, y, bayer);
				float c2 = _value(width, height, x + 2, y, bayer);
				float gH = ((g1 + g2 + c) * 2.0 - c1 - c2) / 4.0;
//				gH = _rec((g1 + g2) * 0.5, c, (c1 + c2) * 0.2 + c * 0.6);
//				clip(gH, g1, g2);

				float c3 = _value(width, height, x, y - 2, bayer);
				float g3 = _value(width, height, x, y - 1, bayer);
				float g4 = _value(width, height, x, y + 1, bayer);
				float c4 = _value(width, height, x, y + 2, bayer);
				float gV = ((g3 + g4 + c) * 2.0 - c3 - c4) / 4.0;
//				gV = _rec((g3 + g4) * 0.5, c, (c3 + c4) * 0.2 + c * 0.6);
//				clip(gV, g3, g4);

				fH[k4 + 1] = gH;
				fH[k4 + ci] = c;
				fV[k4 + 1] = gV;
				fV[k4 + ci] = c;
			}
		}
	}
	//------------
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _fH);
		mirror_2(width, height, _fV);
	}
	subflow->sync_point_post();

	//== reconstruct RED and BLUE at BLUE and REED
	float *mf[] = {fH, fV};
	float *ml[] = {lH, lV};
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			float c1, c2, c3, c4, g, g1, g2, g3, g4;
//			int k3 = ((width + 4) * (y + 2) + x + 2) * 3;
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				int ci = (s == p_red) ? 2 : 0;
				for(int n = 0; n < 2; n++) {
					float *f = mf[n];
					g = f[k4 + 1];
					g1 = f[k4 - w4 - 4 + 1];
					g2 = f[k4 - w4 + 4 + 1];
					g3 = f[k4 + w4 - 4 + 1];
					g4 = f[k4 + w4 + 4 + 1];
					c1 = f[k4 - w4 - 4 + ci];
					c2 = f[k4 - w4 + 4 + ci];
					c3 = f[k4 + w4 - 4 + ci];
					c4 = f[k4 + w4 + 4 + ci];
/*
					float v;
					if(n == 1) { // horizontal
						if(ddr::abs(c1 - c2) < ddr::abs(c3 - c4))
							v = _rec((c1 + c2) * 0.5, g, (g1 + g2) * 0.5);
						else
							v = _rec((c3 + c4) * 0.5, g, (g3 + g4) * 0.5);
					} else { // vertical
						if(ddr::abs(c1 - c3) < ddr::abs(c2 - c4))
							v = _rec((c1 + c3) * 0.5, g, (g1 + g3) * 0.5);
						else
							v = _rec((c2 + c4) * 0.5, g, (g2 + g4) * 0.5);
					}
*/
					float v = g + (c1 + c2 + c3 + c4 - g1 - g2 - g3 - g4) / 4.0;
//					float v = _rec((c1 + c2 + c3 + c4) * 0.25, g, (g1 + g2 + g3 + g4) * 0.25);
//					clip(v);
					f[k4 + ci] = v;
				}
			}
		}
	}
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _fH);
		mirror_2(width, height, _fV);
//		mirror_2(width, height, _lH);
//		mirror_2(width, height, _lV);
	}
	subflow->sync_point_post();

	//== reconstruct RED and BLUE at GREEN
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			float c1, c2, g, g1, g2;
//			float c1, c2, c3, c4, g, g1, g2, g3, g4;
//			int k3 = ((width + 4) * (y + 2) + x + 2) * 3;
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			if(s == p_green_r || s == p_green_b) {
				int ci = (s == p_green_r) ? 0 : 2;
				for(int n = 0; n < 2; n++) {
					float *f = mf[n];
					g = f[k4 + 1];
					g1 = f[k4 - 4 + 1];
					c1 = f[k4 - 4 + ci];
					c2 = f[k4 + 4 + ci];
					g2 = f[k4 + 4 + 1];
					float v = g + (c1 + c2 - g1 - g2) / 2.0;
//					clip(v);
					f[k4 + ci] = v;

					g1 = f[k4 - w4 + 1];
					c1 = f[k4 - w4 + 2 - ci];
					c2 = f[k4 + w4 + 2 - ci];
					g2 = f[k4 + w4 + 1];
					v = g + (c1 + c2 - g1 - g2) / 2.0;
//					clip(v);
					f[k4 + 2 - ci] = v;
				}
			}
		}
	}
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _fH);
		mirror_2(width, height, _fV);
//		mirror_2(width, height, _lH);
//		mirror_2(width, height, _lV);
	}
	subflow->sync_point_post();

	//== calculate Lab
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
//			float c1, c2, c3, c4, g, g1, g2, g3, g4;
			int k3 = ((width + 4) * (y + 2) + x + 2) * 3;
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
//			int s = __bayer_pos_to_c(x, y);
			// calculate Lab
			for(int n = 0; n < 2; n++) {
				float *f = mf[n];
				float *l = ml[n];
				float rgb[3];
				rgb[0] = f[k4 + 0];
				rgb[1] = f[k4 + 1];
				rgb[2] = f[k4 + 2];
				float *lab = &l[k3];
				float XYZ[3];
				// to XYZ
				// NOTE: check cRGB_to_XYZ matrix converted to D50
				m3_v3_mult(XYZ, task->cRGB_to_XYZ, rgb);
				// wrong Von Kries transform D50, curve
				float fX = tf_cielab(XYZ[0] / 0.96422);
				float fY = tf_cielab(XYZ[1] / 1.00000);
				float fZ = tf_cielab(XYZ[2] / 0.82521);
				lab[0] = (116.0 * fY - 16.0);
				lab[1] = 500.0 * (fX - fY);
				lab[2] = 200.0 * (fY - fZ);
			}
		}
	}
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _fH);
		mirror_2(width, height, _fV);
		mirror_2(width, height, _lH);
		mirror_2(width, height, _lV);
	}
	subflow->sync_point_post();

	//== Homogeneity maps
	float dl[2][4];
	float dc[2][4];
	int offsets[4] = {3, -3, w3, -w3};
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k3 = ((width + 4) * (y + 2) + x + 2) * 3;
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
			for(int n = 0; n < 2; n++) {
				float *l = ml[n];
				for(int i = 0; i < 4; i++) {
					dl[n][i] = ddr::abs(l[k3] - l[k3 + offsets[i]]);
					dc[n][i] = _square(l[k3 + 1] - l[k3 + offsets[i] + 1]) + _square(l[k3 + 2] - l[k3 + offsets[i] + 2]);
				}
			}
			float el = ddr::min(ddr::max(dl[0][0], dl[0][1]), ddr::max(dl[1][2], dl[1][3]));
			float ec = ddr::min(ddr::max(dc[0][0], dc[0][1]), ddr::max(dc[1][2], dc[1][3]));
			for(int n = 0; n < 2; n++) {
				float *f = mf[n];
				f[k4 + 3] = 0.0;
				for(int i = 0; i < 4; i++) {
					if(dl[n][i] <= el && dc[n][i] <= ec)
						f[k4 + 3] += 1.0;
				}
			}
		}
	}
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _lH);
		mirror_2(width, height, _lV);
	}
	subflow->sync_point_post();

	//== combine fH and fV
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k4 = ((width + 4) * (y + 2) + x + 2) * 4;
			float w[2];
			for(int n = 0; n < 2; n++) {
				w[n] = 0.0;
				float *f = mf[n];
				for(int j = -1; j <= 1; j++) {
					for(int i = -1; i <= 1; i++) {
						w[n] += f[k4 + w4 * j + i * 4 + 3];
					}
				}
			}
			// fill output table;
			if(w[0] != w[1]) {
				float *f = mf[((w[0] > w[1]) ? 0 : 1)];
				_rgba[k4 + 0] = f[k4 + 0];
				_rgba[k4 + 1] = f[k4 + 1];
				_rgba[k4 + 2] = f[k4 + 2];

			} else {
				_rgba[k4 + 0] = (fH[k4 + 0] + fV[k4 + 0]) / 2.0;
				_rgba[k4 + 1] = (fH[k4 + 1] + fV[k4 + 1]) / 2.0;
				_rgba[k4 + 2] = (fH[k4 + 2] + fV[k4 + 2]) / 2.0;
			}

#if 0
			_rgba[k4 + 0] = _rgba[k4 + 1];
			_rgba[k4 + 2] = _rgba[k4 + 1];
#endif
/*
			_rgba[k4 + 0] = (_rgba[k4 + 0] - black_offset) * black_scale;
			_rgba[k4 + 1] = (_rgba[k4 + 1] - black_offset) * black_scale;
			_rgba[k4 + 2] = (_rgba[k4 + 2] - black_offset) * black_scale;
*/
			_rgba[k4 + 0] /= task->c_scale[0];
			_rgba[k4 + 1] /= task->c_scale[1];
			_rgba[k4 + 2] /= task->c_scale[2];
			_rgba[k4 + 3] = 1.0;
		}
	}
}

//------------------------------------------------------------------------------
