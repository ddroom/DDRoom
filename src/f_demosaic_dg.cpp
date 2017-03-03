/*
 * f_demosaic_dg.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */
/*

 DG - 'Directional Gradients' Bayer demosaic filter.
 Author: Mykhailo Malyshko a.k.a. Spectr.
 Inspired by Bayer demosaic filter 'DDFAPD':
    "Demosaicing With Directional Filtering and a Posteriori Decision"
    Daniele Menon, Stefano Andriani, Student Member, IEEE, and Giancarlo Calvagno, Member, IEEE
    "IEEE TRANSACTIONS ON IMAGE PROCESSING, VOL. 16, NO. 1, JANUARY 2007", p.132-141

*/

#include <iostream>

#include "demosaic_pattern.h"
#include "f_demosaic.h"
#include "mt.h"
#include "system.h"
#include "ddr_math.h"
#include "f_demosaic_int.h"

using namespace std;

// '-' W, '\' NW, '|' N, '/' NE; i.e. West, North and East
#define D2_V	0x00
#define D2_H	0x01
#define D2_N	D2_V
#define D2_W	D2_H
#define D2_MASK	0x01

#define D2D_NW	(0x00 << 1)
#define D2D_NE	(0x01 << 1)
#define D2D_MASK	(0x01 << 1)

#define D4_W	(0x00 << 2)
#define D4_NW	(0x01 << 2)
#define D4_N	(0x02 << 2)
#define D4_NE	(0x03 << 2)
#define D4_MASK	(0x03 << 2)

// moire
#define DM_FLAG	(0x01 << 4)
#define DM_MASK	(0x01 << 4)

//------------------------------------------------------------------------------
inline float middle(const float v1, const float v2, const float v3, const float v4) {
//	return (v1 + v2 + v3 + v4) / 4.0;
	float v[4] = {v1, v2, v3, v4};
	int i_min = 0;
	int i_max = 0;
	for(int i = 1; i < 4; ++i) {
		if(v[i_min] > v[i])
			i_min = i;
		if(v[i_max] < v[i])
			i_max = i;
	}
	float s = 0.0;
	int j = 0;
	for(int i = 0; i < 4; ++i) {
		if(i != i_min && i != i_max) {
			s += v[i];
			++j;
		}
	}
	s /= j;
	return s;
}
//------------------------------------------------------------------------------
inline void clip_smooth2(float &v, const float &l1, const float &l2) {
	float min = l2;
	float max = l1;
	if(l1 < l2) {
		min = l1;
		max = l2;
	}
	if(v < min || v > max)	v = (min + max) * 0.5;
}

inline void clip_smooth(float &v, const float &l1, const float &l2) {
	float min = l2;
	float max = l1;
	if(l1 < l2) {
		min = l1;
		max = l2;
	}
	if(v < min)	v = min + (max - min) * 0.333;
	if(v > max)	v = min + (max - min) * 0.666;
}

inline void clip_n(float &v, const float &l1, const float &l2) {
	float min = l2;
	float max = l1;
	if(l1 < l2) {
		min = l1;
		max = l2;
	}
	if(v < min)	v = min;
	if(v > max)	v = max;
}

inline void clip_n(float &v1, const float &l1, const float &l2, const float &l3, const float &l4) {
	float min = l1;
	float max = l1;
	if(min > l2)    min = l2;
	if(min > l3)    min = l3;
	if(min > l4)    min = l4;
	if(max < l2)    max = l2;
	if(max < l3)    max = l3;
	if(max < l4)    max = l4;
	if(v1 < min)    v1 = min;
	if(v1 > max)    v1 = max;
}

//------------------------------------------------------------------------------
inline float _reconstruct(float c_low, float b, float b_low) {
//	const float edge_low = 2.0 / 64.0;
//	const float edge_high = 4.0 / 64.0;
	// limits below looks good
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
#if 0
//*
	if(b_low != 0.0)
		return c_low + ((b - b_low) / b_low) * c_low;
//*/
	return c_low + b - b_low;
#endif
}
#if 0
inline float _delta(float v1, float v2) {
	return ddr::abs(v1 - v2);
}
#else
inline float _delta(float v1, float v2) {
	return ddr::abs(v2 - v1);
	float min = (v1 < v2) ? v1 : v2;
	float max = (v1 > v2) ? v1 : v2;
	return (max - min) / min;
	// 'classic' - fastest, good enough
	return ddr::abs(v2 - v1);
	// weighted
	if(v1 > v2) {
		float v = v1; v1 = v2; v2 = v;
	}
	if(v2 != 0.0)
		return (v2 - v1) / v2;
	return 0.0;
}
#endif
/*
inline float _delta2(const float &v1, const float &v2, const float &v3) {
	return ddr::abs(v1 - v2) + ddr::abs(v2 - v3);
}
*/

//------------------------------------------------------------------------------
inline float grad(float c1, float c2, float g, float g1, float g2) {
	float c = 0.0;
	if(g1 == 0.0 || g2 == 0.0)
		c = (c1 + c2) * 0.5;
	else
//		c = g * ((c1 + c2) / (g1 + g2));
		c = g * (c1 / g1 + c2 / g2) * 0.5;
	return c;
}

inline float grad2(float g1, float g2, float c, float c1, float c2) {
	if(c1 == 0.0 || c2 == 0.0)
		return (g1 + g2) / 2.0;
	// g_1 / c == g1 / c1;
	float g_1 = (g1 * c) / c1;
	float g_2 = (g2 * c) / c2;
	return (g_1 + g_2) / 2.0;
}

inline float value_bayer(int w, int h, int x, int y, float *m) {
	return m[(x + 2) + (y + 2) * (w + 4)];
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_DG(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;	// input mosaic, float plane 1
	float *rgba = task->rgba;	// output RGBA, float plane 4
	int bayer_pattern = task->bayer_pattern;
//	PS_Demosaic *ps = task->ps;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	float *_rgba = rgba;
	struct rgba_t *_m = (struct rgba_t *)_rgba;
	const int w4 = (width + 4) * 4;
	int32_t *d_ptr = (int32_t *)rgba;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

//	const float max_red = task->max_red;
//	const float max_green = task->max_green;
//	const float max_blue = task->max_blue;

	float *D = (float *)task->D;
	struct rgba_t *_D = (struct rgba_t *)task->D;
	float *sm_temp = (float *)task->sm_temp;

	if(subflow->sync_point_pre())
		mirror_2(width, height, bayer);
	subflow->sync_point_post();

//	float *v_signal = task->v_signal;
	//------------
	// pass I: interpolation of the GREEN at RED and BLUE points
//	float *noise_data = task->noise_data;
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int s = __bayer_pos_to_c(x, y);
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
//			int k2 = ((width + 4) * (y + 2) + x + 2) * 2;
			if(s == p_green_r || s == p_green_b) {
				float c = value_bayer(width, height, x, y, bayer);
				_rgba[k + 0] = c;
				_rgba[k + 2] = c;
				_rgba[k + 1] = c;
				_rgba[k + 3] = c;
			} else {
				float gH, gV, gNE, gNW;
//				float c, c_low, c1, c2, c3, c4, g1, g2, g3, g4;
				float c, c1, c2, c3, c4, g1, g2, g3, g4;
				// s == p_red || s == p_blue
				// 'sim' - i.e. keep proportion of high freq. part; to understand formula just draw pixel values on paper
				// that helps to avoid false colors correlation on monochromatic colors

				//       c1      
				//       g1   
				// c2 g2 c  g3 c3
				//       g4   
				//       c4      
				//---------------
				// '-' horizontal
				c = value_bayer(width, height, x, y, bayer);
				c2 = value_bayer(width, height, x - 2, y + 0, bayer);
				c3 = value_bayer(width, height, x + 2, y + 0, bayer);
				g2 = value_bayer(width, height, x - 1, y + 0, bayer);
				g3 = value_bayer(width, height, x + 1, y + 0, bayer);
#if 0
				// first way - with less noise
				if(ddr::abs(c - c2) < ddr::abs(c - c3))
					gH = (g2 * c) / ((c2 + c) * 0.5);
				else
					gH = (g3 * c) / ((c3 + c) * 0.5);
				clip_smooth(gH, g2, g3);
#else
				// more noise but better for directions detection with current direction detection algorithm
//				c_low = (c2 + c3) * 0.5 + c;
//				gH = (c_low != 0.0) ? ((g2 + g3) * (c / c_low)) : ((g2 + g3) * 0.5);
//				gH = _reconstruct((g2 + g3) * 0.5, c, c_low / 2.0);
				gH = _reconstruct((g2 + g3) * 0.5, c, (c2 + c3) * 0.2 + c * 0.6);
//				gH = (g2 + g3) * 0.5;
#endif
//				clip_smooth(gH, g2, g3); // MARK2, especially diagonals
//				clip(gH, g2, g3);
				//-------------
				// '|' vertical
				c1 = value_bayer(width, height, x + 0, y - 2, bayer);
				c4 = value_bayer(width, height, x + 0, y + 2, bayer);
				g1 = value_bayer(width, height, x + 0, y - 1, bayer);
				g4 = value_bayer(width, height, x + 0, y + 1, bayer);
#if 0
				if(ddr::abs(c - c1) < ddr::abs(c - c4))
					gV = (g1 * c) / ((c1 + c) * 0.5);
				else
					gV = (g4 * c) / ((c4 + c) * 0.5);
				clip_smooth(gV, g1, g4);
#else
//				c_low = (c1 + c4) * 0.5 + c;
//				gV = (c_low != 0.0) ? ((g1 + g4) * (c / c_low)) : ((g1 + g4) * 0.5);
//				gV = _reconstruct((g1 + g4) * 0.5, c, c_low / 2.0);
				gV = _reconstruct((g1 + g4) * 0.5, c, (c1 + c4) * 0.2 + c * 0.6);
//				gV = (g1 + g4) * 0.5;
#endif
//				clip_smooth(gV, g1, g4); // MARK2, especially diagonals
//				clip(gV, g1, g4);
				//========================
				// '\' North-West diagonal
				// '/' North-East diagonal
/*
				// good enough to determine directions
				gNW = middle(g1, g2, g3, g4);
				gNE = gNW;
*/
#if 0
/*
				// gNW - '\'
				float cc1 = value_bayer(width, height, x - 2, y - 2, bayer);
				float cc2 = value_bayer(width, height, x - 1, y - 1, bayer);
				float cc3 = value_bayer(width, height, x    , y    , bayer);
				float cc4 = value_bayer(width, height, x + 1, y + 1, bayer);
				float cc5 = value_bayer(width, height, x + 2, y + 2, bayer);
				float cc = _reconstruct((cc2 + cc4) * 0.5, cc3, ((cc1 + cc5) * 0.5 + cc3) * 0.5);
				if(ddr::abs(g2 - g4) < ddr::abs(g1 - g3)) {
					float gm = (g2 + g4) * 0.5;
					float c_ = value_bayer(width, height, x - 1, y + 1, bayer);
					float cm = (cc + c_) * 0.5;
					// gm / cm = gNW / cc
					gNW = (gm * cc) / cm;
				} else {
					float gm = (g1 + g3) * 0.5;
					float c_ = value_bayer(width, height, x + 1, y - 1, bayer);
					float cm = (cc + c_) * 0.5;
					// gm / cm = gNW / cc
					gNW = (gm * cc) / cm;
				}
				// gNE - '/'
				cc1 = value_bayer(width, height, x + 2, y - 2, bayer);
				cc2 = value_bayer(width, height, x + 1, y - 1, bayer);
				cc3 = value_bayer(width, height, x    , y    , bayer);
				cc4 = value_bayer(width, height, x - 1, y + 1, bayer);
				cc5 = value_bayer(width, height, x - 2, y + 2, bayer);
				cc = _reconstruct((cc2 + cc4) * 0.5, cc3, ((cc1 + cc5) * 0.5 + cc3) * 0.5);
				if(ddr::abs(g1 - g2) < ddr::abs(g3 - g4)) {
					float gm = (g1 + g2) * 0.5;
					float c_ = value_bayer(width, height, x - 1, y - 1, bayer);
					float cm = (cc + c_) * 0.5;
					// gm / cm = gNW / cc
					gNE = (gm * cc) / cm;
				} else {
					float gm = (g3 + g4) * 0.5;
					float c_ = value_bayer(width, height, x + 1, y + 1, bayer);
					float cm = (cc + c_) * 0.5;
					// gm / cm = gNW / cc
					gNE = (gm * cc) / cm;
				}
*/
				// gNE - '/'
///*
				if(ddr::abs(g1 - g2) < ddr::abs(g3 - g4))
					gNW = _reconstruct((g1 + g2) * 0.5, c, ((c1 + c2) * 0.5 + c) * 0.5);
				else
					gNW = _reconstruct((g3 + g4) * 0.5, c, ((c3 + c4) * 0.5 + c) * 0.5);

				if(ddr::abs(g1 - g3) < ddr::abs(g2 - g4))
					gNE = _reconstruct((g1 + g3) * 0.5, c, ((c1 + c3) * 0.5 + c) * 0.5);
				else
					gNE = _reconstruct((g2 + g4) * 0.5, c, ((c2 + c4) * 0.5 + c) * 0.5);
//*/
#else
				if(ddr::abs(g1 - g2) < ddr::abs(g3 - g4)) {
					float d = (g1 + g2) * 0.5;
					if(ddr::abs(d - g3) < ddr::abs(d - g4))
						gNW = ((g1 + g2) * 0.75 + g3 * 0.25) / 1.75;
					else
						gNW = ((g1 + g2) * 0.75 + g4 * 0.25) / 1.75;
//					gNW = ((g1 + g2) * 0.75 + (g3 + g4) * 0.25) / 2.0;
				} else {
					float d = (g3 + g4) * 0.5;
					if(ddr::abs(d - g1) < ddr::abs(d - g2))
						gNW = ((g3 + g4) * 0.75 + g1 * 0.25) / 1.75;
					else
						gNW = ((g3 + g4) * 0.75 + g2 * 0.25) / 1.75;
//					gNW = ((g1 + g2) * 0.25 + (g3 + g4) * 0.75) / 2.0;
				}

				if(ddr::abs(g1 - g3) < ddr::abs(g2 - g4)) {
					float d = (g1 + g3) * 0.5;
					if(ddr::abs(d - g2) < ddr::abs(d - g4))
						gNE = ((g1 + g3) * 0.75 + g2 * 0.25) / 1.75;
					else
						gNE = ((g1 + g3) * 0.75 + g4 * 0.25) / 1.75;
//					gNE = ((g1 + g3) * 0.75 + (g2 + g4) * 0.25) / 2.0;
				} else {
					float d = (g2 + g4) * 0.5;
					if(ddr::abs(d - g1) < ddr::abs(d - g3))
						gNE = ((g2 + g4) * 0.75 + g1 * 0.25) / 1.75;
					else
						gNE = ((g2 + g4) * 0.75 + g3 * 0.25) / 1.75;
//					gNE = ((g1 + g3) * 0.25 + (g2 + g4) * 0.75) / 2.0;
				}
#endif
				// store reconstructed directional-depended GREEN values, at clockwise order from 15 minutes
				// usually set of (gH / gV) are the best, but sometimes, with yellow flowers for example,
				// set (_gH / _gV) are better
				// TODO: use correct directions for color channels, i.e. _gH and _gV as
				// horizontal and vertical directions 
				_rgba[k + 0] = gH;
				_rgba[k + 1] = gNW;
				_rgba[k + 2] = gV;
				_rgba[k + 3] = gNE;
			}
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//--==--
	// use high-freq component of signal to improve direction detection
	// do direction-wise low-pass filter, and then use delta with  to obtain h.f.
	float *sm_in = _rgba;
	float *sm_out = sm_temp;
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			// looks like a good compromise
			float t[4];
			// '-'
			t[0]  = sm_in[((width + 4) * (y + 2 + 0) + x + 2 - 1) * 4 + 0];
			t[0] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 0];
			t[0] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 1) * 4 + 0];
			t[0] *= 2.5f;
			// '\'
			t[1]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 - 1) * 4 + 1];
			t[1] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 1];
			t[1] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 1) * 4 + 1];
			t[1] *= 2.5f;
			// '|'
			t[2]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 0) * 4 + 2];
			t[2] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 2];
			t[2] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 0) * 4 + 2];
			t[2] *= 2.5f;
			// '/'
			t[3]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 1) * 4 + 3];
			t[3] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 3];
			t[3] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 - 1) * 4 + 3];
			t[3] *= 2.5f;
			// 3x3
			for(int m = 0; m < 4; ++m) {
				float v = 0.0f;
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 1) * 4 + m];
				sm_out[k + m] = sm_in[k + m] - (t[m] + v) / (9.0f + 7.5f);
			}
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, (struct rgba_t *)sm_out);
	subflow->sync_point_post();

	//------------
	// pass II: create Dv, Dnw, Dh, Dne tables
	const float w = 1.0;
//	const float w = 1.0 / 1.41421356;
#ifdef DIRECTIONS_SMOOTH
	float *v_green = sm_temp;
#else
	float *v_green = _rgba;
#endif
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
#if 0
			for(int i = 0; i < 4; ++i) {
				D[k + i]  = _delta(v_green[k + i], v_green[k +      4 + i]) + _delta(v_green[k      - 4 + i], v_green[k + i]);
				D[k + i] += _delta(v_green[k + i], v_green[k + w4 + 4 + i]) + _delta(v_green[k - w4 - 4 + i], v_green[k + i]);
				D[k + i] += _delta(v_green[k + i], v_green[k - w4 +     i]) + _delta(v_green[k + w4     + i], v_green[k + i]);
				D[k + i] += _delta(v_green[k + i], v_green[k - w4 + 4 + i]) + _delta(v_green[k + w4 - 4 + i], v_green[k + i]);
			}
#else
			// '-' horizontal
			D[k + 0] = _delta(v_green[k + 0], v_green[k + 4 + 0]) + _delta(v_green[k - 4 + 0], v_green[k + 0]);
			// '\' NW
			D[k + 1] = (_delta(v_green[k + 1], v_green[k + w4 + 4 + 1]) + _delta(v_green[k - w4 - 4 + 1], v_green[k + 1])) * w;
			// '|' vertical
			D[k + 2] = _delta(v_green[k + 2], v_green[k - w4 + 2]) + _delta(v_green[k + w4 + 2], v_green[k + 2]);
			// '/' NE
			D[k + 3] = (_delta(v_green[k + 3], v_green[k - w4 + 4 + 3]) + _delta(v_green[k + w4 - 4 + 3], v_green[k + 3])) * w;
//			D[k + 1] = _delta(_rgba[k + 1], _rgba[k + 4 + 1]) + _delta(_rgba[k - 4 + 1], _rgba[k + 1]);
//			D[k + 3] = _delta(_rgba[k + 3], _rgba[k - w4 + 3]) + _delta(_rgba[k + w4 + 3], _rgba[k + 3]);
#endif
		}
	}

	//------------
	if(subflow->sync_point_pre()) {
		mirror_2(width, height, _m);
		mirror_2(width, height, _D);
	}
	subflow->sync_point_post();

//	float *gaussian = task->gaussian;
	//------------
	// pass III: determine directions of green plane, reconstruct GREEN by direction, copy known RED and BLUE
//	float median = 0.3f;
//	float median_low = median - 0.05f;
//	float median_high = median + 0.05f;
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			float C[4];
			for(int i = 0; i < 4; ++i) {
				float c = D[k - w4 - 4 + i] + D[k - w4 + 0 + i] + D[k - w4 + 4 + i];
				     c += D[k +  0 - 4 + i] + D[k +  0 + 0 + i] + D[k +  0 + 4 + i];
				     c += D[k + w4 - 4 + i] + D[k + w4 + 0 + i] + D[k + w4 + 4 + i];
				C[i] = c;
			}
			// direction from green
			int d = 0;
			float C_min = C[0];
			for(int j = 0; j < 4; ++j) {
				if(C_min > C[j]) {
					d = j;
					C_min = C[j];
				}
			}
#if 0
			// use horizontal and vertical directions for GREEN channel
			if(C[0] < C[2])
				_rgba[k + 1] = _rgba[k + 0];
			if(C[0] > C[2])
				_rgba[k + 1] = _rgba[k + 2];
			if(C[0] == C[2])
				_rgba[k + 1] = (_rgba[k + 2] + _rgba[k + 0]) * 0.5;
#else
//		const int s = __bayer_pos_to_c(x, y);
//		if(s == p_red || s == p_blue) {
			// skip clipped areas
			if(C[0] < 0.95f && C[2] < 0.95f && C[0] > 0.05f && C[2] > 0.05f) {
//			if(C[0] < median_high && C[2] < median_high && C[0] > median_low && C[2] > median_low) {
				float dd = ddr::abs(C[0] - C[2]);
				_rgba[k + 1] = dd;
				long dd_index = (dd * task->dd_hist_scale) * task->dd_hist_size;
				if(dd_index > 0 && dd_index < task->dd_hist_size)
					++task->dd_hist[dd_index];
//			}
			}
//		}
#endif
			// store directions - in alpha channel
			// '-' '\' '|' '/' - 0, 1, 2, 3
			d_ptr[k + 3] = 0x00;
			//--
			if(C[0] < C[2])
				d_ptr[k + 3] |= D2_H;
			else
				d_ptr[k + 3] |= D2_V;
			//--
			if(C[1] < C[3])
				d_ptr[k + 3] |= D2D_NW;
			else
				d_ptr[k + 3] |= D2D_NE;
			//--
			if(d == 0)	d_ptr[k + 3] |= D4_W;
			if(d == 1)	d_ptr[k + 3] |= D4_NW;
			if(d == 2)	d_ptr[k + 3] |= D4_N;
			if(d == 3)	d_ptr[k + 3] |= D4_NE;
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

#if 1
	//------------
	// fill missed GREEN with refined from directional noise values
	// TODO: use a more appropriate noise analysis for 'dd_limit' value,
	//       and pprobably not a linear shift function (?)
	// Looks like dd (direction delta) is not the same in dark and bright areas - measure it.
	if(subflow->sync_point_pre()) {
		long *dd_hist = task->dd_hist;
		long dd_hist_size = task->dd_hist_size;
		for(int i = 1; i < subflow->threads_count(); ++i) {
			task_t *_task = ((task_t **)task->_tasks)[i];
			for(int k = 0; k < dd_hist_size; ++k)
				dd_hist[k] += _task->dd_hist[k];
		}
		long dd_max = 0;
		for(int k = 0; k < dd_hist_size; ++k)
			if(dd_hist[k] > dd_max) dd_max = dd_hist[k];
		float dd_limit = task->dd_limit;
		for(int k = dd_hist_size - 1; k >= 0; k--)
			if(dd_hist[k] > dd_max / 10) {
				dd_limit = ((float(k + 1) / dd_hist_size) * task->dd_hist_scale) / 0.9f;
				break;
			}
		dd_limit *= 9.0f;
//		dd_limit *= 3.0f;
//		dd_limit = 0.06f;
		for(int i = 0; i < subflow->threads_count(); ++i) {
			task_t *_task = ((task_t **)task->_tasks)[i];
			_task->dd_limit = dd_limit;
		}
//		cerr << "dd_limit for median == " << median << " == " << dd_limit << endl;
	}
	subflow->sync_point_post();

	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				float dd = _rgba[k + 1];
				const int d4 = d_ptr[k + 3] & D2_MASK;
				float g1;
				if(d4 == D2_H)
					g1 = _rgba[k + 0];
				else
					g1 = _rgba[k + 2];
				if(dd < task->dd_limit) {
					float g2 = (_rgba[k + 0] + _rgba[k + 2]) * 0.5;
					dd *= 1.0f / task->dd_limit;
					g1 = g1 + (g2 - g1) * (1.0f - dd);
				}
				_rgba[k + 1] = g1;
			} else {
				_rgba[k + 1] = _rgba[k + 0];
			}
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
#endif

#if 1
	//------------
	// refine diagonal GREEN at RED and BLUE
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			_rgba[k + 0] = _rgba[k + 1];
			if(s == p_red || s == p_blue) {
				const int d4 = d_ptr[k + 3] & D4_MASK;
				if(d4 == D4_NW) { // '\'
					const int d1 = d_ptr[k - w4 - 4 + 3] & D4_MASK;
					const int d2 = d_ptr[k + w4 + 4 + 3] & D4_MASK;
					if(d1 == D4_NW || d2 == D4_NW)
						_rgba[k + 0] = (_rgba[k - w4 - 4 + 1] + _rgba[k + 1] + _rgba[k + w4 + 4 + 1]) / 3.0f;
//						_rgba[k + 1] = (_rgba[k - w4 - 4 + 1] + _rgba[k + 1] + _rgba[k + w4 + 4 + 1]) / 3.0f;
				}
				if(d4 == D4_NE) { // '/'
					const int d1 = d_ptr[k - w4 + 4 + 3] & D4_MASK;
					const int d2 = d_ptr[k + w4 - 4 + 3] & D4_MASK;
					if(d1 == D4_NE || d2 == D4_NE)
						_rgba[k + 0] = (_rgba[k - w4 + 4 + 1] + _rgba[k + 1] + _rgba[k + w4 - 4 + 1]) / 3.0f;
//						_rgba[k + 1] = (_rgba[k - w4 + 4 + 1] + _rgba[k + 1] + _rgba[k + w4 - 4 + 1]) / 3.0f;
				}
			}
		}
	}
	//---------------------------
//	if(subflow->sync_point_pre())
//		mirror_2(width, height, _m);
//	subflow->sync_point_post();
	//---------------------------
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			_rgba[k + 1] = _rgba[k + 0];
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
	//---------------------------
#endif

#if 1
	//------------
	// detect moire
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int d4 = d_ptr[k + 3] & D4_MASK;
			const float g = _rgba[k + 1];
			float g1 = g;
			float g2 = g;
			if(d4 == D4_W) { // '-'
				g1 = _rgba[k - w4 + 1];
				g2 = _rgba[k + w4 + 1];
			}
			if(d4 == D4_NW) { // '\'
				g1 = _rgba[k - w4 + 4 + 1];
				g2 = _rgba[k + w4 - 4 + 1];
			}
			if(d4 == D4_N) { // '|'
				g1 = _rgba[k - 4 + 1];
				g2 = _rgba[k + 4 + 1];
			}
			if(d4 == D4_NE) { // '/'
				g1 = _rgba[k - w4 - 4 + 1];
				g2 = _rgba[k + w4 + 4 + 1];
			}
			bool flag = false;
			flag |= (g1 > g) && (g2 > g);
			flag |= (g1 < g) && (g2 < g);
			if(flag)
				d_ptr[k + 3] |= DM_FLAG;
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
#endif

	//------------
	// copy known RED and BLUE pixels into output array
/*
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			int k = w4 * (y + 2) + (x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			float r = 0.0;
			float b = 0.0;
			if(s == p_red || s == p_blue) {
				float v = value_bayer(width, height, x, y, bayer);
				if(s == p_red)
					r = v;
				else // s == p_blue
					b = v;
			}
			_rgba[k + 0] = r;
			_rgba[k + 2] = b;
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
*/
	//------------
	// pass IV: interpolation of the RED and BLUE at the BLUE and RED with known direction and all GREEN points;
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				float v = value_bayer(width, height, x, y, bayer);
				const int ci_2 = (s == p_red) ? 0 : 2;
				_rgba[k + ci_2] = v;
				// interpolation for other color
				float c = 0.0;
				const int ci = (s == p_red) ? 2 : 0;
				// c1 c2
				// c3 c4
				float c1 = value_bayer(width, height, x - 1, y - 1, bayer);
				float c2 = value_bayer(width, height, x + 1, y - 1, bayer);
				float c3 = value_bayer(width, height, x - 1, y + 1, bayer);
				float c4 = value_bayer(width, height, x + 1, y + 1, bayer);
				float g = _rgba[k + 1];
				// MARK2
				const int dm = d_ptr[k + 3] & DM_MASK;
				if(dm != DM_FLAG) {
//				if(false) {
					const int d = d_ptr[k + 3] & D2D_MASK;
					if(d == D2D_NW) { // '\'
						float g1 = _rgba[k - w4 - 4 + 1];
						float g4 = _rgba[k + w4 + 4 + 1];
						c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
						clip_smooth(c, c1, c4);
					} else { // '/'
						float g2 = _rgba[k - w4 + 4 + 1];
						float g3 = _rgba[k + w4 - 4 + 1];
						c = _reconstruct((c2 + c3) * 0.5, g, (g2 + g3) * 0.5);
						clip_smooth(c, c2, c3);
					}
					_rgba[k + ci] = c;
				} else {
					// used 4-directions RED_at_BLUE interpolation, with greatly (almost at all) reduced color moire
					// and keeps significant improvement of reconstruction on diagonal edges;
					// TODO: don't use '-' and '|' directions where is no correlations of RED/GREEN
					// TODO: check how to improve diagonal direction detection and processing
					int d4 = d_ptr[k + 3] & D4_MASK;
					int d2 = ((d_ptr[k + 3] & D2_MASK) == D2_N) ? D4_N : D4_W;
					int d = d2;
					if(d4 == D4_NW || d4 == D4_NE) {
						d = d4;
						if(d4 == D4_NW) { // '\'
							float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] & D4_MASK;
							float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] & D4_MASK;
							if(d4_1 != D4_NW && d4_2 != D4_NW)
								d = d2;
						} else { // '/'
							float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] & D4_MASK;
							float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] & D4_MASK;
							if(d4_1 != D4_NE && d4_2 != D4_NE)
								d = d2;
						}
					}
					//--
					if(d == D4_W) { // '-'
						float g = _rgba[k + 1];
						float g1 = _rgba[k - w4 + 1];
						float g2 = _rgba[k + w4 + 1];
						float g11 = _rgba[k - 4 - w4 + 1];
						float g12 = _rgba[k + 4 - w4 + 1];
						float g21 = _rgba[k - 4 + w4 + 1];
						float g22 = _rgba[k + 4 + w4 + 1];
						// use low freq from the color channel, and use high freq from the green channel
						float gt = (g1 + g11 + g12) / 3.0;
						float gb = (g2 + g21 + g22) / 3.0;
//						if(ddr::abs(g - gt) < ddr::abs(g - gb))
							c = _reconstruct((c1 + c2) * 0.5, g, gt);
//						else
							c += _reconstruct((c3 + c4) * 0.5, g, gb);
						c /= 2.0;
//						c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
					}
					if(d == D4_N) { // '|'
						float g = _rgba[k + 1];
						float g1 = _rgba[k - 4 + 1];
						float g2 = _rgba[k + 4 + 1];
						float g11 = _rgba[k - 4 - w4 + 1];
						float g12 = _rgba[k - 4 + w4 + 1];
						float g21 = _rgba[k + 4 - w4 + 1];
						float g22 = _rgba[k + 4 + w4 + 1];
						float gl = (g1 + g11 + g12) / 3.0;
						float gr = (g2 + g21 + g22) / 3.0;
//						if(ddr::abs(g - gl) < ddr::abs(g - gr))
							c = _reconstruct((c1 + c3) * 0.5, g, gl);
//						else
							c += _reconstruct((c2 + c4) * 0.5, g, gr);
						c /= 2.0;
//						c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
					}
					if(d == D4_NW) { // '\'
						float g1 = _rgba[k - w4 - 4 + 1];
						float g4 = _rgba[k + w4 + 4 + 1];
						c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
						clip_smooth(c, c1, c4);
					}
					if(d == D4_NE) { // '/'
						float g2 = _rgba[k - w4 + 4 + 1];
						float g3 = _rgba[k + w4 - 4 + 1];
						c = _reconstruct((c2 + c3) * 0.5, g, (g2 + g3) * 0.5);
						clip_smooth(c, c2, c3);
					}
					_rgba[k + ci] = c;
				}
			}
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//------------
	// pass V: interpolation of the RED and BLUE at the GREEN;
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			//--
			if(s == p_red || s == p_blue) {
				continue;
			} else {
				const int ci = (s == p_green_r) ? 0 : 2;
				float g = _rgba[k + 1];
#if 1
				// check direction
				int d4 = d_ptr[k + 3] & D4_MASK;
				int d2 = ((d_ptr[k + 3] & D2_MASK) == D2_N) ? D4_N : D4_W;
				int d = d2;
				if(d4 == D4_NW || d4 == D4_NE) {
					d = d4;
					if(d4 == D4_NW) { // '\'
						float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] & D4_MASK;
						float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] & D4_MASK;
						if(d4_1 != D4_NW && d4_2 != D4_NW)
							d = d2;
					} else { // '/'
						float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] & D4_MASK;
						float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] & D4_MASK;
						if(d4_1 != D4_NE && d4_2 != D4_NE)
							d = d2;
					}
				}
				// apply
				if(d == D4_W || d == D4_N) {
					int doff = (d == D4_W) ? 4 : w4;	// horizontal - vertical
					int ai[2] = {ci, 2 - ci};
					for(int i = 0; i < 2; ++i) {
						float c1 = _rgba[k - doff + ai[i]];
						float c2 = _rgba[k + doff + ai[i]];
						float g1 = _rgba[k - doff + 1];
						float g2 = _rgba[k + doff + 1];
						float c = _reconstruct((c1 + c2) * 0.5, g, (g1 + g2) * 0.5);
						_rgba[k + ai[i]] = c;
					}
				} else {
					float g1 = _rgba[k - w4 + 1];
					float g2 = _rgba[k -  4 + 1];
					float g3 = _rgba[k +  4 + 1];
					float g4 = _rgba[k + w4 + 1];
					if(d == D4_NW) { // '\'
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; ++i) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(ddr::abs(c1 - c2) < ddr::abs(g3 - g4))
								c = _reconstruct((c1 + c2) * 0.5, g, (g1 + g2) * 0.5);
							else
								c = _reconstruct((c3 + c4) * 0.5, g, (g3 + g4) * 0.5);
							_rgba[k + ai[i]] = c;
						}
//						c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
//						clip_smooth(c, c1, c4);
					} else { // '/'
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; ++i) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(ddr::abs(c1 - c3) < ddr::abs(g2 - g4))
								c = _reconstruct((c1 + c3) * 0.5, g, (g1 + g3) * 0.5);
							else
								c = _reconstruct((c2 + c4) * 0.5, g, (g2 + g4) * 0.5);
							_rgba[k + ai[i]] = c;
						}
					}
				}
#else
				int doff = (direction % 2 == 0) ? 4 : w4;	// horizontal - vertical
				int ai[2] = {ci, 2 - ci};
				for(int i = 0; i < 2; ++i) {
					float c1 = _rgba[k - doff + ai[i]];
					float c2 = _rgba[k + doff + ai[i]];
					float g1 = _rgba[k - doff + 1];
					float g2 = _rgba[k + doff + 1];
					float c = _reconstruct((c1 + c2) * 0.5, g, (g1 + g2) * 0.5);
//					clip_n(c, c1, c2);
					_rgba[k + ai[i]] = c;
				}
#endif
			}
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//------------
	// refine horizontal and vertical RED at BLUE and BLUE at RED
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int dm = d_ptr[k + 3] & DM_MASK;
			if(dm != DM_FLAG) { // was used diagonal reconstruction only
				const int s = __bayer_pos_to_c(x, y);
				if(s == p_red || s == p_blue) {
					const int d4 = d_ptr[k + 3] & D4_MASK;
					const int ci = (s == p_blue) ? 0 : 2;
					if(d4 == D4_W) { // '-'
						const int d1 = d_ptr[k - 4 + 3] & D4_MASK;
						const int d2 = d_ptr[k + 4 + 3] & D4_MASK;
						if(d1 == D4_W || d2 == D4_W)
							_rgba[k + ci] = (_rgba[k - 4 + ci] + _rgba[k + ci] + _rgba[k + 4 + ci]) / 3.0f;
					}
					if(d4 == D4_N) { // '|'
						const int d1 = d_ptr[k - w4 + 3] & D4_MASK;
						const int d2 = d_ptr[k + w4 + 3] & D4_MASK;
						if(d1 == D4_N || d2 == D4_N)
							_rgba[k + ci] = (_rgba[k - w4 + ci] + _rgba[k + ci] + _rgba[k + w4 + ci]) / 3.0f;
					}
				}
			}
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

#if 0
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int d4 = d_ptr[k + 3] & D4_MASK;
			if(d4 == D4_W) { // '-' - check for moire
				bool moire[3];
				for(int i = -1; i < 2; ++i) {
					moire[i + 1] = false;
					const int d = d_ptr[k + i * 4 + 3] & D4_MASK;
					if(d == D4_W) {
						const float g1 = _rgba[k - w4 + i * 4 + 1];
						const float g2 = _rgba[k      + i * 4 + 1];
						const float g3 = _rgba[k + w4 + i * 4 + 1];
						moire[i + 1] |= (g1 > g2) && (g3 > g2);
						moire[i + 1] |= (g1 < g2) && (g3 < g2);
//						moire[i + 1] = (g1 - g2) * (g2 - g3) < 0.0f;
//						if((g1 - g2) * (g2 - g3) < 0.0f)
//							moire[i + 1] = true;
					}
				}
//				if((moire[0] && moire[1]) || (moire[1] && moire[2])) {
				if(moire[1]) {
					_rgba[k + 0] = 0.0f;
					_rgba[k + 1] = 0.0f;
					_rgba[k + 2] = 0.0f;
				}
			}
		}
	}
	//---------------------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
#endif

	//------------
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
#if 0
			if((d_ptr[k + 3] & DM_MASK) & DM_FLAG) {
				_rgba[k + 0] = 0.0f;
				_rgba[k + 1] = 0.0f;
				_rgba[k + 2] = 0.0f;
			}
#endif
/*
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				_rgba[k + 0] = 0.0f;
				_rgba[k + 1] = 0.0f;
				_rgba[k + 2] = 0.0f;
			} else {
				_rgba[k + 0] = _rgba[k + 1];
				_rgba[k + 2] = _rgba[k + 1];
			}
*/
			//--
//			_rgba[k + 0] = gaussian[k + 0];
//			_rgba[k + 1] = gaussian[k + 1];
//			_rgba[k + 2] = gaussian[k + 2];
			//--
#if 0
			if(false) {
//			if(_rgba[k + 0] >= max_red || _rgba[k + 1] >= max_green || _rgba[k + 2] >= max_blue) {
//			if(_rgba[k + 0] >= 1.0 || _rgba[k + 1] >= 1.0 || _rgba[k + 2] >= 1.0) {
//			if(d_ptr[k + 3] / 16 == 3) {
				_rgba[k + 0] = 0.0;
				_rgba[k + 1] = 0.0;
				_rgba[k + 2] = 0.0;
			} else {
				_rgba[k + 0] = (_rgba[k + 0] - black_offset) * black_scale;
				_rgba[k + 1] = (_rgba[k + 1] - black_offset) * black_scale;
				_rgba[k + 2] = (_rgba[k + 2] - black_offset) * black_scale;
			}
#endif
#if 0
			float *gaussian = task->gaussian;
			// experimental, but with good results, color reconstruction: modulation of color gaussian signal with reconstructed green signal
//			float gr = (gaussian[k + 0] - black_offset) * black_scale;
//			float gg = (gaussian[k + 1] - black_offset) * black_scale;
//			float gb = (gaussian[k + 2] - black_offset) * black_scale;
			float gr = gaussian[k + 0];
			float gg = gaussian[k + 1];
			float gb = gaussian[k + 2];
			// c / g = gc / gg;
			// c = gc * (g / gg);
			float scale = 1.0;
			if(gg != 0.0)
				scale = _rgba[k + 1] / gg;
			_rgba[k + 0] = gr * scale;
			_rgba[k + 2] = gb * scale;
#endif
/*
			ddr::clip(_rgba[k + 0]);
			ddr::clip(_rgba[k + 1]);
			ddr::clip(_rgba[k + 2]);
*/
#if 0
			_rgba[k + 0] = _rgba[k + 1];
			_rgba[k + 2] = _rgba[k + 1];
#endif
#if 0
			_rgba[k + 1] = _rgba[k + 0];
			_rgba[k + 2] = _rgba[k + 0];
#endif
#if 0
			_rgba[k + 1] = _rgba[k + 2];
			_rgba[k + 0] = _rgba[k + 2];
#endif
			_rgba[k + 3] = 1.0;
			_rgba[k + 0] /= task->c_scale[0];
			_rgba[k + 1] /= task->c_scale[1];
			_rgba[k + 2] /= task->c_scale[2];
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
