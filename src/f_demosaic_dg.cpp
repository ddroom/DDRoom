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

// '-' H, '\' L, '|' V, '/' R; i.e. horizontal, top-left, vertical, top-right
#define D2_H	0x00
#define D2_V	0x01
#define D2_MASK	0x01

#define D2D_L	(0x00 << 1)
#define D2D_R	(0x01 << 1)
#define D2D_MASK	(0x01 << 1)

#define D4_H	(0x00 << 2)
#define D4_V	(0x01 << 2)
#define D4_L	(0x02 << 2)
#define D4_R	(0x03 << 2)
#define D4_MASK	(0x03 << 2)

// moire
#define DM_FLAG	(0x01 << 4)
#define DM_MASK	(0x01 << 4)

//------------------------------------------------------------------------------
inline float middle(const float v1, const float v2, const float v3, const float v4) {
	float v[4] = {v1, v2, v3, v4};
	int i[4] = {0, 1, 2, 3};
	if(v[i[0]] > v[i[1]]) std::swap(v[0], v[1]);
	if(v[i[2]] > v[i[3]]) std::swap(v[2], v[3]);
	if(v[i[1]] > v[i[2]]) std::swap(v[1], v[2]);
	if(v[i[0]] > v[i[1]]) std::swap(v[0], v[1]);
	if(v[i[2]] > v[i[3]]) std::swap(v[2], v[3]);
	return (v[i[1]] != v[i[2]]) ? (v[i[1]] + v[i[2]]) * 0.5f : v[i[1]];
}
//------------------------------------------------------------------------------
inline void clip_smooth2(float &v, float l1, float l2) {
	if(l1 > l2) std::swap(l1, l2);
	if(v < l1 || v > l2)
		v = (l1 + l2) * 0.5f;
}

inline void clip_smooth(float &v, float l1, float l2) {
	if(l1 > l2) std::swap(l1, l2);
	if(v < l1)
		v = l1 + (l2 - l1) * 0.333f;
	else
		if(v > l2)
			v = l1 + (l2 - l1) * 0.666f;
}

inline void clip_n(float &v, float l1, float l2) {
	if(l1 > l2) std::swap(l1, l2);
	v = (v < l1) ? l1 : v;
	v = (v > l2) ? l2 : v;
}

inline void clip_n(float &v, float l1, float l2, float l3, float l4) {
	if(l1 > l2) std::swap(l1, l2);
	if(l3 > l4) std::swap(l3, l4);
	if(l2 > l3) std::swap(l2, l3);
	if(l1 < l2) std::swap(l1, l2);
	if(l3 < l4) std::swap(l3, l4);
	v = (v < l1) ? l1 : v;
	v = (v > l4) ? l4 : v;
}

//------------------------------------------------------------------------------
inline float _rc(float a1, float a2, float b, float b1, float b2) {
	if(a1 > a2) {
		std::swap(a1, a2);
		std::swap(b1, b2);
	}
	float bm = (b + b1 + b2) * 0.666666f;
	if(bm > 0.0f) {
		float a = (a1 + a2) * (b / bm);
		if(a < a1)
			return a1 + (a2 - a1) * 0.25f;
		if(a > a2)
			return a1 + (a2 - a1) * 0.75f;
		return a;
	}
	return (a1 + a2) * 0.5f;
}

inline float _reconstruct(float c_low, float b, float b_low) {
//	const float edge_low = 2.0 / 64.0;
//	const float edge_high = 4.0 / 64.0;
	// limits below looks good
//	const float edge_low = 3.0 / 64.0;
//	const float edge_high = 6.0 / 64.0;
	const float edge_low = 0.046875f;
	const float edge_high = 0.09375f;
	const float r_sum = c_low + b - b_low;
	if(b_low != 0.0f) {
		if(b_low < edge_low)
			return r_sum;
		const float r_div = c_low + ((b - b_low) / b_low) * c_low;
		if(b_low > edge_high)
			return r_div;
		const float part = (b_low - edge_low) / (edge_high - edge_low);
		return r_div * part + r_sum * (1.0f - part);
	}
	return r_sum;
}

inline float _delta(const float &v1, const float &v2) {
	return (v1 < v2) ? v2 - v1 : v1 - v2;
}

//------------------------------------------------------------------------------
inline float grad(float c1, float c2, float g, float g1, float g2) {
	float c = 0.0f;
	if(g1 == 0.0f || g2 == 0.0f)
		c = (c1 + c2) * 0.5f;
	else
		c = g * (c1 / g1 + c2 / g2) * 0.5f;
	return c;
}

inline float grad2(float g1, float g2, float c, float c1, float c2) {
	if(c1 == 0.0f || c2 == 0.0f)
		return (g1 + g2) * 0.5f;
	// g_1 / c == g1 / c1;
	float g_1 = (g1 * c) / c1;
	float g_2 = (g2 * c) / c2;
	return (g_1 + g_2) * 0.5f;
}

inline const float &value_bayer(const int &w, const int &h, const int &x, const int &y, const float *m) {
	return m[(x + 2) + (y + 2) * (w + 4)];
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_DG(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	const int width = task->width;
	const int height = task->height;
	float *bayer = task->bayer;	// input mosaic, float plane 1
	float *rgba = task->rgba;	// output RGBA, float plane 4
	int bayer_pattern = task->bayer_pattern;
//	PS_Demosaic *ps = task->ps;

	const int x_min = task->x_min;
	const int x_max = task->x_max;
	const int y_max = task->height;
	int y;

	float *_rgba = rgba;
	struct rgba_t *_m = (struct rgba_t *)_rgba;
	const int w4 = (width + 4) * 4;
	int32_t *d_ptr = (int32_t *)rgba;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	float *D = (float *)task->D;
	struct rgba_t *_D = (struct rgba_t *)task->D;
	float *sm_temp = (float *)task->sm_temp;

	if(subflow->sync_point_pre())
		mirror_2(width, height, bayer);
	subflow->sync_point_post();

	auto y_flow = task->y_flow;
	//--------------------------------------------------------------------------
	// pass I: reconstruct missed GREEN in all four directions and store all of them
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int s = __bayer_pos_to_c(x, y);
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			if(s == p_green_r || s == p_green_b) {
				float c = value_bayer(width, height, x, y, bayer);
				_rgba[k + 0] = c;
				_rgba[k + 1] = c;
				_rgba[k + 2] = c;
				_rgba[k + 3] = c;
				continue;
			} else {
				const float g1 = value_bayer(width, height, x + 0, y - 1, bayer);
				const float g2 = value_bayer(width, height, x - 1, y + 0, bayer);
				const float g3 = value_bayer(width, height, x + 1, y + 0, bayer);
				const float g4 = value_bayer(width, height, x + 0, y + 1, bayer);

				const float c = value_bayer(width, height, x, y, bayer);

				const float c2 = value_bayer(width, height, x - 2, y + 0, bayer);
				const float c3 = value_bayer(width, height, x + 2, y + 0, bayer);
				float gH = _reconstruct((g2 + g3) * 0.5f, c, (c2 + c3) * 0.2f + c * 0.6f);

				const float c1 = value_bayer(width, height, x + 0, y - 2, bayer);
				const float c4 = value_bayer(width, height, x + 0, y + 2, bayer);
				float gV = _reconstruct((g1 + g4) * 0.5f, c, (c1 + c4) * 0.2f + c * 0.6f);

				float gL;
				if(ddr::abs(g1 - g2) < ddr::abs(g3 - g4)) {
					float d = (g1 + g2) * 0.5f;
					if(ddr::abs(d - g3) < ddr::abs(d - g4))
						gL = ((g1 + g2) * 0.75f + g3 * 0.25f) * 0.57143f;
					else
						gL = ((g1 + g2) * 0.75f + g4 * 0.25f) * 0.57143f;
//					gL = ((g1 + g2) * 0.75 + (g3 + g4) * 0.25) / 2.0;
				} else {
					float d = (g3 + g4) * 0.5f;
					if(ddr::abs(d - g1) < ddr::abs(d - g2))
						gL = ((g3 + g4) * 0.75f + g1 * 0.25f) * 0.57143f;
					else
						gL = ((g3 + g4) * 0.75f + g2 * 0.25f) * 0.57143f;
//					gL = ((g1 + g2) * 0.25 + (g3 + g4) * 0.75) / 2.0;
				}

				float gR;
				if(ddr::abs(g1 - g3) < ddr::abs(g2 - g4)) {
					float d = (g1 + g3) * 0.5f;
					if(ddr::abs(d - g2) < ddr::abs(d - g4))
						gR = ((g1 + g3) * 0.75f + g2 * 0.25f) * 0.57143f;
					else
						gR = ((g1 + g3) * 0.75f + g4 * 0.25f) * 0.57143f;
//					gR = ((g1 + g3) * 0.75 + (g2 + g4) * 0.25) / 2.0;
				} else {
					float d = (g2 + g4) * 0.5f;
					if(ddr::abs(d - g1) < ddr::abs(d - g3))
						gR = ((g2 + g4) * 0.75f + g1 * 0.25f) * 0.57143f;
					else
						gR = ((g2 + g4) * 0.75f + g3 * 0.25f) * 0.57143f;
//					gR = ((g1 + g3) * 0.25 + (g2 + g4) * 0.75) / 2.0;
				}

				_rgba[k + 0] = gH;
				_rgba[k + 1] = gL;
				_rgba[k + 2] = gV;
				_rgba[k + 3] = gR;
			}
		}
	}

	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();

	//--------------------------------------------------------------------------
#ifdef DIRECTIONS_SMOOTH
	// use high-freq component of signal to improve direction detection
	// do direction-wise low-pass filter, and then use delta with to obtain h.f.
	float *sm_in = _rgba;
	float *sm_out = sm_temp;
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			// looks like a good compromise
			float t[4];
			t[0]  = (sm_in[k      - 4 + 0] + sm_in[k + 0] + sm_in[k      + 4 + 0]) * 2.5f;
			t[1]  = (sm_in[k - w4 - 4 + 1] + sm_in[k + 1] + sm_in[k + w4 + 4 + 1]) * 2.5f;
			t[2]  = (sm_in[k - w4     + 2] + sm_in[k + 2] + sm_in[k + w4     + 2]) * 2.5f;
			t[3]  = (sm_in[k - w4 + 4 + 3] + sm_in[k + 3] + sm_in[k + w4 - 4 + 3]) * 2.5f;
			// 3x3
			for(int m = 0; m < 4; ++m) {
				float v = 0.0f;
				v += sm_in[k - w4 - 4 + m] + sm_in[k - w4 + m] + sm_in[k - w4 + 4 + m];
				v += sm_in[k      - 4 + m] + sm_in[k      + m] + sm_in[k      + 4 + m];
				v += sm_in[k + w4 - 4 + m] + sm_in[k + w4 + m] + sm_in[k + w4 + 4 + m];
				sm_out[k + m] = sm_in[k + m] - (t[m] + v) * 0.06060606f;
//				sm_out[k + m] = sm_in[k + m] - (t[m] + v) / (9.0f + 7.5f);
			}
		}
	}
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, (struct rgba_t *)sm_out);
	}
	subflow->sync_point_post();
#endif

	//--------------------------------------------------------------------------
	// pass II: create direction tables H, L, V, R at the reed, green, blue and alpha channels
#ifdef DIRECTIONS_SMOOTH
	float *v_green = sm_temp;
#else
	float *v_green = _rgba;
#endif
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			D[k + 0] = _delta(v_green[k + 0], v_green[k      + 4 + 0]) + _delta(v_green[k      - 4 + 0], v_green[k + 0]);
			D[k + 1] = _delta(v_green[k + 1], v_green[k + w4 + 4 + 1]) + _delta(v_green[k - w4 - 4 + 1], v_green[k + 1]);
			D[k + 2] = _delta(v_green[k + 2], v_green[k - w4     + 2]) + _delta(v_green[k + w4     + 2], v_green[k + 2]);
			D[k + 3] = _delta(v_green[k + 3], v_green[k - w4 + 4 + 3]) + _delta(v_green[k + w4 - 4 + 3], v_green[k + 3]);
		}
	}

	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
		mirror_2(width, height, _D);
	}
	subflow->sync_point_post();

	//--------------------------------------------------------------------------
	// pass III: determine directions of green plane, reconstruct GREEN by direction, copy known RED and BLUE
//	float median = 0.3f;
//	float median_low = median - 0.05f;
//	float median_high = median + 0.05f;
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = ((width + 4) * (y + 2) + x + 2) * 4;
			float C[4];
			for(int i = 0; i < 4; ++i) {
				float c;
				c  = D[k - w4 - 4 + i] + D[k - w4 + i] + D[k - w4 + 4 + i];
				c += D[k      - 4 + i] + D[k      + i] + D[k      + 4 + i];
				c += D[k + w4 - 4 + i] + D[k + w4 + i] + D[k + w4 + 4 + i];
				C[i] = c;
			}
			int d = 0;
			float C_min = C[0];
			if(C_min > C[1]) {
				C_min = C[1];
				d = 1;
			}
			if(C_min > C[2]) {
				C_min = C[2];
				d = 2;
			}
			if(C_min > C[3]) {
				C_min = C[3];
				d = 3;
			}
#if 0
			int d = 0;
			float C_min = 0.0f;
//			float C_max = 0.0f;
			for(int i = 0; i < 4; ++i) {
				float c;
				c  = D[k - w4 - 4 + i] + D[k - w4 + i] + D[k - w4 + 4 + i];
				c += D[k      - 4 + i] + D[k      + i] + D[k      + 4 + i];
				c += D[k + w4 - 4 + i] + D[k + w4 + i] + D[k + w4 + 4 + i];
				C[i] = c;
				if(i == 0) {
					C_min = c;
//					C_max = c;
				} else {
					d = (C_min > c) ? i : d;
					C_min = (C_min > c) ? c : C_min;
//					C_max = (C_max < c) ? c : C_max;
				}
			}
#endif
#if 0
			const float dd = C_max - C_min;
			_rgba[k + 1] = dd;
			// skip clipped areas
			if(C_min > 0.02f && C_max < 0.98f) {
				const long dd_index = (dd * task->dd_hist_scale) * task->dd_hist.size();
				if(dd_index >= 0 && dd_index < task->dd_hist.size())
					++task->dd_hist[dd_index];
			}
#endif
			// store directions in alpha channel
			int32_t d_mask = 0x00;
			d_mask = 0x00;
			d_mask |= (C[0] < C[2]) ? D2_H : D2_V;
			d_mask |= (C[1] < C[3]) ? D2D_L : D2D_R;
			d_mask |= (d == 0) * D4_H;
			d_mask |= (d == 1) * D4_L;
			d_mask |= (d == 2) * D4_V;
			d_mask |= (d == 3) * D4_R;

			d_ptr[k + 3] = d_mask;
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue)
				_rgba[k + 1] = ((d_mask & D2_MASK)== D2_H) ? _rgba[k + 0] : _rgba[k + 2];
		}
	}

	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();

#if 0
	//--------------------------------------------------------------------------
	// fill missed GREEN with refined from directional noise values
	// TODO: use a more appropriate noise analysis for 'dd_limit' value,
	//       and pprobably not a linear shift function (?)
	// Looks like dd (direction delta) is not the same in dark and bright areas - measure it.
	if(subflow->sync_point_pre()) {
		// TODO: try it with the single std::vector<std::atomic_int>
		std::vector<long> &dd_hist = task->dd_hist;
		long dd_hist_size = dd_hist.size();
		for(int i = 1; i < subflow->threads_count(); ++i) {
			task_t *_task = (task_t *)subflow->get_private(i);
			for(int k = 0; k < dd_hist_size; ++k)
				dd_hist[k] += _task->dd_hist[k];
		}
		long dd_max = 0;
		for(int k = 0; k < dd_hist_size; ++k)
			if(dd_hist[k] > dd_max) dd_max = dd_hist[k];
		float dd_limit = task->dd_limit;
		for(int k = dd_hist_size - 1; k >= 0; k--)
			if(dd_hist[k] > dd_max / 10) {
				dd_limit = ((float(k + 1) / dd_hist_size) * task->dd_hist_scale) * 1.11111111f;
				break;
			}
		dd_limit *= 25.0f;
//		dd_limit *= 15.0f;
//		dd_limit *= 9.0f;
//		dd_limit *= 5.0f;
//		dd_limit *= 3.0f;
//		dd_limit = 0.06f;
		for(int i = 0; i < subflow->threads_count(); ++i) {
			task_t *_task = (task_t *)subflow->get_private(i);
			_task->dd_limit = dd_limit;
		}
//		cerr << "dd_limit for median == " << median << " == " << dd_limit << endl;
	}
	subflow->sync_point_post();
#endif

#if 0
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				const int d2 = d_ptr[k + 3] & D2_MASK;
				float g1 = (d2 == D2_H) ? _rgba[k + 0] : _rgba[k + 2];
/*
				const int d4 = d_ptr[k + 3] & D4_MASK;
				float dd = _rgba[k + 1];
				if(dd < task->dd_limit) {
//				if(dd < task->dd_limit && (d4 == D4_H || d4 == D4_V)) {
					float g2;
					if(d4 == D4_H)
						g2  = _rgba[k - w4] + _rgba[k + w4];
					else
						g2 = _rgba[k - 4] + _rgba[k + 4];
					g2 *= 0.5f;
					float factor = (task->dd_limit - dd) / task->dd_limit;
					g1 = g1 + (g2 - g1) * factor;
				}
*/
				_rgba[k + 1] = g1;
			} else {
				_rgba[k + 1] = _rgba[k + 0];
			}
		}
	}

	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();
#endif

	//--------------------------------------------------------------------------
	// refine diagonal GREEN at RED and BLUE, write result in the RED channel
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			float g = _rgba[k + 0] = _rgba[k + 1];
			if(s == p_red || s == p_blue) {
				const int d4 = d_ptr[k + 3] & D4_MASK;
				if(d4 == D4_L) {
					const int d1 = d_ptr[k - w4 - 4 + 3] & D4_MASK;
					const int d2 = d_ptr[k + w4 + 4 + 3] & D4_MASK;
					if(d1 == D4_L || d2 == D4_L)
						_rgba[k + 0] = (_rgba[k - w4 - 4 + 1] + _rgba[k + w4 + 4 + 1]) * 0.25f + _rgba[k + 1] * 0.5f;
					continue;
				}
				if(d4 == D4_R) {
					const int d1 = d_ptr[k - w4 + 4 + 3] & D4_MASK;
					const int d2 = d_ptr[k + w4 - 4 + 3] & D4_MASK;
					if(d1 == D4_R || d2 == D4_R)
						_rgba[k + 0] = (_rgba[k - w4 + 4 + 1] + _rgba[k + w4 - 4 + 1]) * 0.25f + _rgba[k + 1] * 0.5f;
					continue;
				}
				if(d4 == D4_V) {
					float g1 = _rgba[k - w4 + 1];
					float g2 = _rgba[k + w4 + 1];
					if(g1 > g2) std::swap(g1, g2);
					if(g < g1) _rgba[k + 0] = g1 + (g2 - g1) * 0.25f;
					if(g > g2) _rgba[k + 0] = g1 + (g2 - g1) * 0.75f;
					continue;
				}
				if(d4 == D4_H) {
					float g1 = _rgba[k - 4 + 1];
					float g2 = _rgba[k + 4 + 1];
					if(g1 > g2) std::swap(g1, g2);
					if(g < g1) _rgba[k + 0] = g1 + (g2 - g1) * 0.25f;
					if(g > g2) _rgba[k + 0] = g1 + (g2 - g1) * 0.75f;
					continue;
				}
			}
		}
	}
	if(subflow->sync_point_pre())
		y_flow->store(0);
	subflow->sync_point_post();

	// move refined GREEN from the RED channel
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			_rgba[k + 1] = _rgba[k + 0];
		}
	}
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();

	//--------------------------------------------------------------------------
#if 1
	// detect moire
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int d4 = d_ptr[k + 3] & D4_MASK;
			const float g = _rgba[k + 1];
			float g1 = g;
			float g2 = g;
			if(d4 == D4_H) {
				g1 = _rgba[k - w4 + 1];
				g2 = _rgba[k + w4 + 1];
			}
			if(d4 == D4_L) {
				g1 = _rgba[k - w4 + 4 + 1];
				g2 = _rgba[k + w4 - 4 + 1];
			}
			if(d4 == D4_V) {
				g1 = _rgba[k - 4 + 1];
				g2 = _rgba[k + 4 + 1];
			}
			if(d4 == D4_R) {
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
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();
#endif

	// copy known RED and BLUE pixels into output array
/*
	while((y = y_flow->fetch_add(1)) < y_max) {
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
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();
*/

	// pass IV: interpolation of the RED and BLUE at the BLUE and RED with known direction and all GREEN points;
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			if(s == p_red || s == p_blue) {
				float v = value_bayer(width, height, x, y, bayer);
				const int ci_2 = (s == p_red) ? 0 : 2;
				_rgba[k + ci_2] = v;
				// interpolation for other color
				float c = 0.0f;
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
					if(d == D2D_L) {
						float g1 = _rgba[k - w4 - 4 + 1];
						float g4 = _rgba[k + w4 + 4 + 1];
						c = _reconstruct((c1 + c4) * 0.5f, g, (g1 + g4) * 0.5f);
						clip_smooth(c, c1, c4);
					} else {
						float g2 = _rgba[k - w4 + 4 + 1];
						float g3 = _rgba[k + w4 - 4 + 1];
						c = _reconstruct((c2 + c3) * 0.5f, g, (g2 + g3) * 0.5f);
						clip_smooth(c, c2, c3);
					}
					_rgba[k + ci] = c;
				} else {
					// used 4-directions RED_at_BLUE interpolation, with greatly (almost at all) reduced color moire
					// and keeps significant improvement of reconstruction on diagonal edges;
					// TODO: don't use '-' and '|' directions where is no correlations of RED/GREEN
					// TODO: check how to improve diagonal direction detection and processing
					int d4 = d_ptr[k + 3] & D4_MASK;
					int d2 = ((d_ptr[k + 3] & D2_MASK) == D2_V) ? D4_V : D4_H;
					int d = d2;
					if(d4 == D4_L || d4 == D4_R) {
						d = d4;
						if(d4 == D4_L) {
							float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] & D4_MASK;
							float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] & D4_MASK;
							if(d4_1 != D4_L && d4_2 != D4_L)
								d = d2;
						} else {
							float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] & D4_MASK;
							float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] & D4_MASK;
							if(d4_1 != D4_R && d4_2 != D4_R)
								d = d2;
						}
					}
					//--
					if(d == D4_H) {
						float g = _rgba[k + 1];
						float g1 = _rgba[k - w4 + 1];
						float g2 = _rgba[k + w4 + 1];
						float g11 = _rgba[k - 4 - w4 + 1];
						float g12 = _rgba[k + 4 - w4 + 1];
						float g21 = _rgba[k - 4 + w4 + 1];
						float g22 = _rgba[k + 4 + w4 + 1];
						// use low freq from the color channel, and use high freq from the green channel
						float gt = (g1 + g11 + g12) * 0.33333333f;
						float gb = (g2 + g21 + g22) * 0.33333333f;
//						if(ddr::abs(g - gt) < ddr::abs(g - gb))
							c = _reconstruct((c1 + c2) * 0.5f, g, gt);
//						else
							c += _reconstruct((c3 + c4) * 0.5f, g, gb);
						c *= 0.5f;
//						c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
						_rgba[k + ci] = c;
						continue;
					}
					if(d == D4_V) {
						float g = _rgba[k + 1];
						float g1 = _rgba[k - 4 + 1];
						float g2 = _rgba[k + 4 + 1];
						float g11 = _rgba[k - 4 - w4 + 1];
						float g12 = _rgba[k - 4 + w4 + 1];
						float g21 = _rgba[k + 4 - w4 + 1];
						float g22 = _rgba[k + 4 + w4 + 1];
						float gl = (g1 + g11 + g12) * 0.33333333f;
						float gr = (g2 + g21 + g22) * 0.33333333f;
//						if(ddr::abs(g - gl) < ddr::abs(g - gr))
							c = _reconstruct((c1 + c3) * 0.5f, g, gl);
//						else
							c += _reconstruct((c2 + c4) * 0.5f, g, gr);
						c *= 0.5f;
//						c = _reconstruct((c2 + c3 + c1 + c4) * 0.25, g, (g1 + g2) * 0.5);
						_rgba[k + ci] = c;
						continue;
					}
					if(d == D4_L) {
						float g1 = _rgba[k - w4 - 4 + 1];
						float g4 = _rgba[k + w4 + 4 + 1];
						c = _reconstruct((c1 + c4) * 0.5f, g, (g1 + g4) * 0.5f);
						clip_smooth(c, c1, c4);
						_rgba[k + ci] = c;
						continue;
					}
					if(d == D4_R) {
						float g2 = _rgba[k - w4 + 4 + 1];
						float g3 = _rgba[k + w4 - 4 + 1];
						c = _reconstruct((c2 + c3) * 0.5f, g, (g2 + g3) * 0.5f);
						clip_smooth(c, c2, c3);
						_rgba[k + ci] = c;
						continue;
					}
				}
			}
		}
	}
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();

	// pass V: interpolation of the RED and BLUE at the GREEN;
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int s = __bayer_pos_to_c(x, y);
			//--
			if(s == p_red || s == p_blue) {
				continue;
			} else {
				const int ci = (s == p_green_r) ? 0 : 2;
				float g = _rgba[k + 1];
#if 0
				// check direction
				int d4 = d_ptr[k + 3] & D4_MASK;
				int d2 = ((d_ptr[k + 3] & D2_MASK) == D2_V) ? D4_V : D4_H;
				int d = d2;
				if(d4 == D4_L || d4 == D4_R) {
					d = d4;
					if(d4 == D4_L) {
						float d4_1 = d_ptr[k - w4 - w4 - 4 - 4 + 3] & D4_MASK;
						float d4_2 = d_ptr[k + w4 + w4 + 4 + 4 + 3] & D4_MASK;
						if(d4_1 != D4_L && d4_2 != D4_L)
							d = d2;
					} else { // '/'
						float d4_1 = d_ptr[k - w4 - w4 + 4 + 4 + 3] & D4_MASK;
						float d4_2 = d_ptr[k + w4 + w4 - 4 - 4 + 3] & D4_MASK;
						if(d4_1 != D4_R && d4_2 != D4_R)
							d = d2;
					}
				}
				// apply
				if(d == D4_H || d == D4_V) {
					int doff = (d == D4_H) ? 4 : w4;
					int ai[2] = {ci, 2 - ci};
					for(int i = 0; i < 2; ++i) {
						float c1 = _rgba[k - doff + ai[i]];
						float c2 = _rgba[k + doff + ai[i]];
						float g1 = _rgba[k - doff + 1];
						float g2 = _rgba[k + doff + 1];
						float c = _reconstruct((c1 + c2) * 0.5f, g, (g1 + g2) * 0.5f);
						_rgba[k + ai[i]] = c;
					}
				} else {
					float g1 = _rgba[k - w4 + 1];
					float g2 = _rgba[k -  4 + 1];
					float g3 = _rgba[k +  4 + 1];
					float g4 = _rgba[k + w4 + 1];
					if(d == D4_L) {
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; ++i) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(ddr::abs(c1 - c2) < ddr::abs(g3 - g4))
								c = _reconstruct((c1 + c2) * 0.5f, g, (g1 + g2) * 0.5f);
							else
								c = _reconstruct((c3 + c4) * 0.5f, g, (g3 + g4) * 0.5f);
							_rgba[k + ai[i]] = c;
						}
//						c = _reconstruct((c1 + c4) * 0.5, g, (g1 + g4) * 0.5);
//						clip_smooth(c, c1, c4);
					} else {
						int ai[2] = {ci, 2 - ci};
						for(int i = 0; i < 2; ++i) {
							float c1 = _rgba[k - w4 + ai[i]];
							float c2 = _rgba[k -  4 + ai[i]];
							float c3 = _rgba[k +  4 + ai[i]];
							float c4 = _rgba[k + w4 + ai[i]];
							float c;
							if(ddr::abs(c1 - c3) < ddr::abs(g2 - g4))
								c = _reconstruct((c1 + c3) * 0.5f, g, (g1 + g3) * 0.5f);
							else
								c = _reconstruct((c2 + c4) * 0.5f, g, (g2 + g4) * 0.5f);
							_rgba[k + ai[i]] = c;
						}
					}
				}
#else
				int doff = ((d_ptr[k + 3] & D2_MASK) == D2_V) ? w4 : 4;
				int ai[2] = {ci, 2 - ci};
				for(int i = 0; i < 2; ++i) {
					float c1 = _rgba[k - doff + ai[i]];
					float c2 = _rgba[k + doff + ai[i]];
///*
					float g1 = _rgba[k - doff + 1];
					float g2 = _rgba[k + doff + 1];
					float c = _reconstruct((c1 + c2) * 0.5f, g, (g1 + g2) * 0.5f);
//*/
					if(((c1 - c2) * (g1 - g2) < 0.0f) || (c < c1 && c < c2) || (c > c1 && c > c2))
						c = (c1 + c2) * 0.5f;
//					clip_n(c, c1, c2);
					_rgba[k + ai[i]] = c;
				}
#endif
			}
		}
	}
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();

	// refine horizontal and vertical RED at BLUE and BLUE at RED
#if 1
	while((y = y_flow->fetch_add(1)) < y_max) {
		for(int x = x_min; x < x_max; ++x) {
			const int k = w4 * (y + 2) + (x + 2) * 4;
			const int dm = d_ptr[k + 3] & DM_MASK;
			if(dm != DM_FLAG) { // was used diagonal reconstruction only
				const int s = __bayer_pos_to_c(x, y);
				if(s == p_red || s == p_blue) {
					const int d4 = d_ptr[k + 3] & D4_MASK;
					const int ci = (s == p_blue) ? 0 : 2;
					if(d4 == D4_H) {
						const int d1 = d_ptr[k - 4 + 3] & D4_MASK;
						const int d2 = d_ptr[k + 4 + 3] & D4_MASK;
						if(d1 == D4_H || d2 == D4_H)
							_rgba[k + ci] = (_rgba[k - 4 + ci] + _rgba[k + ci] + _rgba[k + 4 + ci]) * 0.333333f;
					}
					if(d4 == D4_V) {
						const int d1 = d_ptr[k - w4 + 3] & D4_MASK;
						const int d2 = d_ptr[k + w4 + 3] & D4_MASK;
						if(d1 == D4_V || d2 == D4_V)
							_rgba[k + ci] = (_rgba[k - w4 + ci] + _rgba[k + ci] + _rgba[k + w4 + ci]) * 0.333333f;
					}
				}
			}
		}
	}
	//---------------------------
	if(subflow->sync_point_pre()) {
		y_flow->store(0);
		mirror_2(width, height, _m);
	}
	subflow->sync_point_post();
#endif

	//------------
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	while((y = y_flow->fetch_add(1)) < y_max) {
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
			const int ic = 1;
			_rgba[k + 0] = _rgba[k + ic];
			_rgba[k + 2] = _rgba[k + ic];
#endif
			_rgba[k + 0] /= task->c_scale[0];
			_rgba[k + 1] /= task->c_scale[1];
			_rgba[k + 2] /= task->c_scale[2];
			_rgba[k + 3] = 1.0;
		}
	}
	if(subflow->sync_point_pre())
		mirror_2(width, height, _m);
	subflow->sync_point_post();
}

//------------------------------------------------------------------------------
