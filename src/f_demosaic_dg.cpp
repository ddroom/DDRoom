/*
 * f_demosaic_dg.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */
/*

 On first, that was a slightly modified version of 'DDFAPD':
    "Demosaicing With Directional Filtering and a Posteriori Decision"
    Daniele Menon, Stefano Andriani, Student Member, IEEE, and Giancarlo Calvagno, Member, IEEE
    "IEEE TRANSACTIONS ON IMAGE PROCESSING, VOL. 16, NO. 1, JANUARY 2007", p.132-141
 Then later it was hardly modified and tuned, so now instead of make some sort of
    complicated abbreviation it's called SDrA, which is means 'Spectr's Demosaic revision A'.
    I'm just don't have a time to figure out a some fancy mathematical basis for parts of algorithm
    that would looks nice in theory but not so in reality :)

*/

#include <iostream>

#include "demosaic_pattern.h"
#include "f_demosaic.h"
#include "mt.h"
#include "system.h"
#include "ddr_math.h"
#include "f_demosaic_int.h"

#define HPR_COLD_PIXELS
//#undef HPR_COLD_PIXELS

// use only 2 directions for green instead of 4
//#undef NO_DIAGONAL_GREEN
#define NO_DIAGONAL_GREEN

// calculate directions from 3x3 window of red/blue for red/blue reconstruction
// good at places where correlation red-green and blue-green is low
// bad for diagonal lines on sharpness test charts
//#define USE_DIRECTIONS_FROM_COLOR
// or use directions from green channel instead
#undef USE_DIRECTIONS_FROM_COLOR

using namespace std;

//------------------------------------------------------------------------------
inline float middle(const float v1, const float v2, const float v3, const float v4) {
//	return (v1 + v2 + v3 + v4) / 4.0;
	float v[4] = {v1, v2, v3, v4};
	int i_min = 0;
	int i_max = 0;
	for(int i = 1; i < 4; i++) {
		if(v[i_min] > v[i])
			i_min = i;
		if(v[i_max] < v[i])
			i_max = i;
	}
	float s = 0.0;
	int j = 0;
	for(int i = 0; i < 4; i++) {
		if(i != i_min && i != i_max) {
			s += v[i];
			j++;
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
	return _abs(v1 - v2);
}
#else
inline float _delta(float v1, float v2) {
	return _abs(v2 - v1);
	float min = (v1 < v2) ? v1 : v2;
	float max = (v1 > v2) ? v1 : v2;
	return (max - min) / min;
	// 'classic' - fastest, good enough
	return _abs(v2 - v1);
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
	return _abs(v1 - v2) + _abs(v2 - v3);
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
	int w4 = (width + 4) * 4;
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
//	float *dn3 = task->dn3;
	float *sm_temp = (float *)task->sm_temp;

	if(subflow->sync_point_pre())
		mirror_2(width, height, bayer);
	subflow->sync_point_post();

	float *v_signal = task->v_signal;
	//------------
	// pass I: interpolation of the GREEN at RED and BLUE points
	float *noise_data = task->noise_data;
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int s = __bayer_pos_to_c(x, y);
			int k = ((width + 4) * (y + 2) + x + 2) * 4;
//			int k2 = ((width + 4) * (y + 2) + x + 2) * 2;
			if(s == p_green_r || s == p_green_b) {
				float c = value_bayer(width, height, x, y, bayer);
				_rgba[k + 0] = c;
				_rgba[k + 2] = c;
				_rgba[k + 1] = c;
				_rgba[k + 3] = c;
/*
				dn3[k + 0] = c;
				dn3[k + 2] = c;
				dn3[k + 1] = c;
				dn3[k + 3] = c;
*/
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
				if(d_abs(c - c2) < d_abs(c - c3))
					gH = (g2 * c) / ((c2 + c) * 0.5);
				else
					gH = (g3 * c) / ((c3 + c) * 0.5);
				clip_smooth(gH, g2, g3);
				dn3[k + 0] = gH;
#else
				// more noise but better for directions detection with current direction detection algorithm
//				c_low = (c2 + c3) * 0.5 + c;
//				gH = (c_low != 0.0) ? ((g2 + g3) * (c / c_low)) : ((g2 + g3) * 0.5);
//				gH = _reconstruct((g2 + g3) * 0.5, c, c_low / 2.0);
				gH = _reconstruct((g2 + g3) * 0.5, c, (c2 + c3) * 0.2 + c * 0.6);
//				gH = (g2 + g3) * 0.5;
#endif
				clip_smooth(gH, g2, g3);
//				clip(gH, g2, g3);
				//-------------
				// '|' vertical
				c1 = value_bayer(width, height, x + 0, y - 2, bayer);
				c4 = value_bayer(width, height, x + 0, y + 2, bayer);
				g1 = value_bayer(width, height, x + 0, y - 1, bayer);
				g4 = value_bayer(width, height, x + 0, y + 1, bayer);
#if 0
				if(d_abs(c - c1) < d_abs(c - c4))
					gV = (g1 * c) / ((c1 + c) * 0.5);
				else
					gV = (g4 * c) / ((c4 + c) * 0.5);
				clip_smooth(gV, g1, g4);
				dn3[k + 2] = gV;
#else
//				c_low = (c1 + c4) * 0.5 + c;
//				gV = (c_low != 0.0) ? ((g1 + g4) * (c / c_low)) : ((g1 + g4) * 0.5);
//				gV = _reconstruct((g1 + g4) * 0.5, c, c_low / 2.0);
				gV = _reconstruct((g1 + g4) * 0.5, c, (c1 + c4) * 0.2 + c * 0.6);
//				gV = (g1 + g4) * 0.5;
#endif
				clip_smooth(gV, g1, g4);
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
				if(_abs(g2 - g4) < _abs(g1 - g3)) {
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
				if(_abs(g1 - g2) < _abs(g3 - g4)) {
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
				if(_abs(g1 - g2) < _abs(g3 - g4))
					gNW = _reconstruct((g1 + g2) * 0.5, c, ((c1 + c2) * 0.5 + c) * 0.5);
				else
					gNW = _reconstruct((g3 + g4) * 0.5, c, ((c3 + c4) * 0.5 + c) * 0.5);

				if(_abs(g1 - g3) < _abs(g2 - g4))
					gNE = _reconstruct((g1 + g3) * 0.5, c, ((c1 + c3) * 0.5 + c) * 0.5);
				else
					gNE = _reconstruct((g2 + g4) * 0.5, c, ((c2 + c4) * 0.5 + c) * 0.5);
//*/
#else
				if(_abs(g1 - g2) < _abs(g3 - g4)) {
					float d = (g1 + g2) * 0.5;
					if(_abs(d - g3) < _abs(d - g4))
						gNW = ((g1 + g2) * 0.75 + g3 * 0.25) / 1.75;
					else
						gNW = ((g1 + g2) * 0.75 + g4 * 0.25) / 1.75;
//					gNW = ((g1 + g2) * 0.75 + (g3 + g4) * 0.25) / 2.0;
				} else {
					float d = (g3 + g4) * 0.5;
					if(_abs(d - g1) < _abs(d - g2))
						gNW = ((g3 + g4) * 0.75 + g1 * 0.25) / 1.75;
					else
						gNW = ((g3 + g4) * 0.75 + g2 * 0.25) / 1.75;
//					gNW = ((g1 + g2) * 0.25 + (g3 + g4) * 0.75) / 2.0;
				}

				if(_abs(g1 - g3) < _abs(g2 - g4)) {
					float d = (g1 + g3) * 0.5;
					if(_abs(d - g2) < _abs(d - g4))
						gNE = ((g1 + g3) * 0.75 + g2 * 0.25) / 1.75;
					else
						gNE = ((g1 + g3) * 0.75 + g4 * 0.25) / 1.75;
//					gNE = ((g1 + g3) * 0.75 + (g2 + g4) * 0.25) / 2.0;
				} else {
					float d = (g2 + g4) * 0.5;
					if(_abs(d - g1) < _abs(d - g3))
						gNE = ((g2 + g4) * 0.75 + g1 * 0.25) / 1.75;
					else
						gNE = ((g2 + g4) * 0.75 + g3 * 0.25) / 1.75;
//					gNE = ((g1 + g3) * 0.25 + (g2 + g4) * 0.75) / 2.0;
				}
#endif

//				noise_data[k2 + 0] = middle(g1, g2, g3, g4);

				// store reconstructed directional-depended GREEN values, at clockwise order from 15 minutes
				// usually set of (gH / gV) are the best, but sometimes, with yellow flowers for example,
				// set (_gH / _gV) are better
				// TODO: use correct directions for color channels, i.e. _gH and _gV as
				// horizontal and vertical directions 
				_rgba[k + 0] = gH;
				_rgba[k + 1] = gNW;
				_rgba[k + 2] = gV;
				_rgba[k + 3] = gNE;
//				dn3[k + 1] = gNW;
//				dn3[k + 3] = gNE;
			}
#ifdef DIRECTIONS_SMOOTH
			const float *gaussian = task->gaussian;
			const float gr = gaussian[k + 0];
			const float gg = gaussian[k + 1];
			const float gb = gaussian[k + 2];
			for(int m = 0; m < 4; m++) {
				float rgb[3];
				rgb[1] = _rgba[k + m];
				float scale = 1.0;
				if(gg != 0.0)
					scale = rgb[1] / gg;
				rgb[0] = gr * scale;
				rgb[2] = gb * scale;

				float XYZ[3];
				m3_v3_mult(XYZ, task->cRGB_to_XYZ, rgb);
				v_signal[k + m] = XYZ[1];
//				v_signal[k + m] = tf_cielab(XYZ[1]);
//				v_signal[k + m] = (1.16f * tf_cielab(XYZ[1]) - 0.16f); // use 'L' from 'CIELab'
//				v_signal[k + m] = _max(_max(XYZ[0], XYZ[1]), XYZ[2]);

//				v_signal[k + m] = _max(_max(rgb[0], rgb[1]), rgb[2]);
//				v_signal[k + m] = rgb[1];
//				v_signal[k + m] = tf_cielab(_max(_max(rgb[0], rgb[1]), rgb[2]));
//				_rgba[k + m] = v_signal[k + m];
			}
#endif
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//--==--
	// low-pass filter signal, and extract high-freq component
	float *sm_in = v_signal;
	float *sm_out = sm_temp;
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = ((width + 4) * (y + 2) + x + 2) * 4;
			// looks like a good compromise - code should be optimized
			sm_out[k + 0]  = sm_in[((width + 4) * (y + 2 + 0) + x + 2 - 1) * 4 + 0];
			sm_out[k + 0] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 0];
			sm_out[k + 0] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 1) * 4 + 0];
			sm_out[k + 0] *= 2.0;

			sm_out[k + 1]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 - 1) * 4 + 1];
			sm_out[k + 1] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 1];
			sm_out[k + 1] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 1) * 4 + 1];
			sm_out[k + 1] *= 2.0;

			sm_out[k + 2]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 0) * 4 + 2];
			sm_out[k + 2] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 2];
			sm_out[k + 2] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 0) * 4 + 2];
			sm_out[k + 2] *= 2.0;

			sm_out[k + 3]  = sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 1) * 4 + 3];
			sm_out[k + 3] += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + 3];
			sm_out[k + 3] += sm_in[((width + 4) * (y + 2 + 1) + x + 2 - 1) * 4 + 3];
			sm_out[k + 3] *= 2.0;

///*
			for(int m = 0; m < 4; m++) {
				float v = 0.0;
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 0) + x + 2 + 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 - 1) + x + 2 + 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 0) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 - 1) * 4 + m];
				v += sm_in[((width + 4) * (y + 2 + 1) + x + 2 + 1) * 4 + m];
//				v /= 9.0;
				sm_out[k + m] += v;
				sm_out[k + m] /= 9.0 + 6.0;
			}
//*/
		}
	}

	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	sm_in = v_signal;
	sm_out = sm_temp;
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = ((width + 4) * (y + 2) + x + 2) * 4;
			for(int m = 0; m < 4; m++) {
				v_signal[k + m] = sm_out[k + m] - v_signal[k + m];
			}
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();


	//------------
	// pass II: create Dv, Dnw, Dh, Dne tables
	const float w = 1.0;
//	const float w = 1.0 / 1.41421356;
#ifdef DIRECTIONS_SMOOTH
	float *v_green = v_signal;
#else
	float *v_green = _rgba;
#endif
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = ((width + 4) * (y + 2) + x + 2) * 4;
#if 0
			for(int i = 0; i < 4; i++) {
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
//	float noise_std_dev_min = task->noise_std_dev_min * 2.0;
	const float n_s0 = task->noise_std_dev_min;
	float n_s1 = 1.0;
//	float *noise_data = task->noise_data;
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int s = __bayer_pos_to_c(x, y);
			int k = ((width + 4) * (y + 2) + x + 2) * 4;
			int k2 = ((width + 4) * (y + 2) + x + 2) * 2;
			float C[4];
			for(int i = 0; i < 4; i++) {
				float c = D[k - w4 - 4 + i] + D[k - w4 + 0 + i] + D[k - w4 + 4 + i];
				     c += D[k +  0 - 4 + i] + D[k +  0 + 0 + i] + D[k +  0 + 4 + i];
				     c += D[k + w4 - 4 + i] + D[k + w4 + 0 + i] + D[k + w4 + 4 + i];
				C[i] = c;
			}
			// direction from green
			int d = 0;
			float C_min = C[0];
			for(int j = 0; j < 4; j++) {
				if(C_min > C[j]) {
					d = j;
					C_min = C[j];
				}
			}
#if 0
			// use 4 directions for GREEN channel
			// actually, usage of 4 directions for GREEN worse than 2 directions
			_rgba[k + 1] = _rgba[k + d];
/*
			if(d == 1 || d == 3)
//				_rgba[k + 1] = gaussian[k + 1];
				_rgba[k + 1] = 0.0;
*/
/*
			// TODO: put that detection into noise analyzing code
			bool flag = false;
			for(int j = 0; j < 5; j++) {
				for(int i = 0; i < 5; i++) {
					flag |= value_bayer(width, height, x + i - 2, y + j - 2, bayer) >= 0.99;
				}
			}
			if(flag)
				_rgba[k + 1];
*/
#else
			// use horizontal and vertical directions for GREEN channel
			if(C[0] < C[2])
				_rgba[k + 1] = _rgba[k + 0];
			if(C[0] > C[2])
				_rgba[k + 1] = _rgba[k + 2];
			if(C[0] == C[2])
				_rgba[k + 1] = (_rgba[k + 2] + _rgba[k + 0]) * 0.5;
#endif
			// use Gaussian blur instead of directional interpolation in places with high noise;
			if(s == p_red || s == p_blue)
				n_s1 = n_s0 * 3.0;
			else
				n_s1 = n_s0;
			float n_s3 = n_s1 * 3.0;
			if(noise_data[k2 + 1] < n_s3) {
				float smooth = noise_data[k2 + 0];
				float value = _rgba[k + 1];
				if(noise_data[k2 + 1] < n_s1) {
					value = smooth;
				} else {
					if(value > smooth)
						value = smooth + ((value - smooth) - n_s1) * 1.5;
					else
						value = smooth - ((smooth - value) - n_s1) * 1.5;
				}
				_rgba[k + 1] = value;
			}
			// store directions - in alpha channel
			// '-' '\' '|' '/' - 0, 1, 2, 3
			d_ptr[k + 3] = d * 16;
			// 11110000 - directions from GREEN
			// 00001111 - directions for color
			// 0000000x, x == 0 - horizontal, x == 1 - vertical
			// 0000000x, x == 0 - '\', x == 2 - '/'
#if 1
			// use 4 directions for colors
			if(C[0] > C[2]) // & 0x01 ==> '|'
				d_ptr[k + 3] += 1;
			if(C[1] > C[3]) // & 0x02 ==> '/'
				d_ptr[k + 3] += 2;
#else
			// save only vertical-horizontal directions
			int d_ = 0;
			if(C[0] > C[2])
				d_ = 2 * 16 + 1;
			d_ptr[k + 3] = d_;
#endif
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//------------
	// copy known RED and BLUE pixels into output array
/*
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
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
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = w4 * (y + 2) + (x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				float v = value_bayer(width, height, x, y, bayer);
				int ci_2 = (s == p_red) ? 0 : 2;
				_rgba[k + ci_2] = v;
/*
				float scale = 1.0;
				float scale = task->c_scale[0];
				if(s == p_blue)
					scale = task->c_scale[2];
*/
				// interpolation for other color
				float c = 0.0;
				int ci = (s == p_red) ? 2 : 0;
				// c1 c2
				// c3 c4
				float c1 = value_bayer(width, height, x - 1, y - 1, bayer);
				float c2 = value_bayer(width, height, x + 1, y - 1, bayer);
				float c3 = value_bayer(width, height, x - 1, y + 1, bayer);
				float c4 = value_bayer(width, height, x + 1, y + 1, bayer);
				float g = _rgba[k + 1];
				// used 4-directions RED_at_BLUE interpolation, with greatly (almost at all) reduced color moire
				// and keeps significant improvement of reconstruction on diagonal edges;
				// TODO: don't use '-' and '|' directions where is no correlations of RED/GREEN
				// TODO: check how to improve diagonal direction detection and processing
				int d4 = d_ptr[k + 3] / 16;
				int d2 = (((d_ptr[k + 3] & 0x0F) % 2) * 2);
				int d = d2;
				if(d4 == 1 || d4 == 3) {
					d = d4;
					if(d4 == 1) { // '\'
						float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] / 16;
						float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] / 16;
						if(d4_1 != 1 && d4_2 != 1)
							d = d2;
					} else { // '/'
						float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] / 16;
						float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] / 16;
						if(d4_1 != 3 && d4_2 != 3)
							d = d2;
					}
				}
				//--
				if(d == 0) { // '-'
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
//					if(_abs(g - gt) < _abs(g - gb))
						c = _reconstruct((c1 + c2) * 0.5, g, gt);
//					else
						c += _reconstruct((c3 + c4) * 0.5, g, gb);
					c /= 2.0;
//					c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
				}
				if(d == 2) { // '|'
					float g = _rgba[k + 1];
					float g1 = _rgba[k - 4 + 1];
					float g2 = _rgba[k + 4 + 1];
					float g11 = _rgba[k - 4 - w4 + 1];
					float g12 = _rgba[k - 4 + w4 + 1];
					float g21 = _rgba[k + 4 - w4 + 1];
					float g22 = _rgba[k + 4 + w4 + 1];
					float gl = (g1 + g11 + g12) / 3.0;
					float gr = (g2 + g21 + g22) / 3.0;
//					if(_abs(g - gl) < _abs(g - gr))
						c = _reconstruct((c1 + c3) * 0.5, g, gl);
//					else
						c += _reconstruct((c2 + c4) * 0.5, g, gr);
					c /= 2.0;
//					c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
				}
				if(d == 1) { // '\'
					float g1 = _rgba[k - w4 - 4 + 1];
					float g4 = _rgba[k + w4 + 4 + 1];
					c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
					clip_smooth(c, c1, c4);
				}
				if(d == 3) { // '/'
					float g2 = _rgba[k - w4 + 4 + 1];
					float g3 = _rgba[k + w4 - 4 + 1];
					c = _reconstruct((c2 + c3) * 0.5, g, (g2 + g3) * 0.5);
					clip_smooth(c, c2, c3);
				}
				_rgba[k + ci] = c;
			}
		}
	}
	//------------
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();

	//------------
	// pass V: interpolation of the RED and BLUE at the GREEN;
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = w4 * (y + 2) + (x + 2) * 4;
			int s = __bayer_pos_to_c(x, y);
			// save alpha
//			int direction = d_ptr[k + 3] & 0x000F;
//			_rgba[k + 3] = 1.0;
			//--
			if(s == p_red || s == p_blue) {
				continue;
			} else {
				int ci = (s == p_green_r) ? 0 : 2;
				float g = _rgba[k + 1];
#if 1
				// check direction
				int d4 = d_ptr[k + 3] / 16;
//				int d = d4;
///*
				int d2 = (((d_ptr[k + 3] & 0x0F) % 2) * 2);
				int d = d2;
				if(d4 == 1 || d4 == 3) {
					d = d4;
					if(d4 == 1) { // '\'
						float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] / 16;
						float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] / 16;
						if(d4_1 != 1 && d4_2 != 1)
							d = d2;
					} else { // '/'
						float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] / 16;
						float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] / 16;
						if(d4_1 != 3 && d4_2 != 3)
							d = d2;
					}
				}
//*/
				// apply
				if(d == 0 || d == 2) {
					int doff = (d == 0) ? 4 : w4;	// horizontal - vertical
					int ai[2] = {ci, 2 - ci};
					for(int i = 0; i < 2; i++) {
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
					if(d == 1) { // '\'
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; i++) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(_abs(c1 - c2) < _abs(g3 - g4))
								c = _reconstruct((c1 + c2) * 0.5, g, (g1 + g2) * 0.5);
							else
								c = _reconstruct((c3 + c4) * 0.5, g, (g3 + g4) * 0.5);
							_rgba[k + ai[i]] = c;
						}
//						c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
//						clip_smooth(c, c1, c4);
					} else { // '/'
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; i++) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(_abs(c1 - c3) < _abs(g2 - g4))
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
				for(int i = 0; i < 2; i++) {
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
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	for(int y = y_min; y < y_max; y++) {
		for(int x = x_min; x < x_max; x++) {
			int k = w4 * (y + 2) + (x + 2) * 4;
//			_rgba[k + 3] = 1.0;
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
//			_rgba[k + 1] = gaussian[k + 1];
			_rgba[k + 2] = gb * scale;
#endif
			_rgba[k + 0] /= task->c_scale[0];
			_rgba[k + 1] /= task->c_scale[1];
			_rgba[k + 2] /= task->c_scale[2];
/*
			_clip(_rgba[k + 0], 0.0f, 1.0f);
			_clip(_rgba[k + 1], 0.0f, 1.0f);
			_clip(_rgba[k + 2], 0.0f, 1.0f);
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
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
