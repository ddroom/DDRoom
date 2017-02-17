#ifndef __H_DEMOSAIC_PATTERN__
#define __H_DEMOSAIC_PATTERN__
/*
 * bayer.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <math.h>

#define DEMOSAIC_PATTERN_NONE		0
#define DEMOSAIC_PATTERN_BAYER_RGGB	1
#define DEMOSAIC_PATTERN_BAYER_GBRG	2
#define DEMOSAIC_PATTERN_BAYER_GRBG	3
#define DEMOSAIC_PATTERN_BAYER_BGGR	4
#define DEMOSAIC_PATTERN_FOVEON		6
#define DEMOSAIC_PATTERN_XTRANS		5

inline bool demosaic_pattern_is_bayer(int pattern) {
	bool bayer = false;
	bayer |= pattern == DEMOSAIC_PATTERN_BAYER_RGGB;
	bayer |= pattern == DEMOSAIC_PATTERN_BAYER_GBRG;
	bayer |= pattern == DEMOSAIC_PATTERN_BAYER_GRBG;
	bayer |= pattern == DEMOSAIC_PATTERN_BAYER_BGGR;
	return bayer;
}

inline int __bayer_pos_to_c(const int &x, const int &y) {
	return (x + 0) % 2 + ((y + 0) % 2) * 2;
}

inline int __bayer_pattern_shift_x(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return DEMOSAIC_PATTERN_BAYER_GRBG;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return DEMOSAIC_PATTERN_BAYER_BGGR;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return DEMOSAIC_PATTERN_BAYER_RGGB;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return DEMOSAIC_PATTERN_BAYER_GBRG;
	return pattern;
}

inline int __bayer_pattern_shift_y(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return DEMOSAIC_PATTERN_BAYER_GBRG;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return DEMOSAIC_PATTERN_BAYER_RGGB;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return DEMOSAIC_PATTERN_BAYER_BGGR;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return DEMOSAIC_PATTERN_BAYER_GRBG;
	return pattern;
}

inline int __bayer_pattern_rotate(int pattern, int rotation) {
//	if(pattern == DEMOSAIC_PATTERN_BAYER_NONE)
//		return pattern;
	if(rotation % 90 != 0)
		return pattern;
	while(rotation < 0)
		rotation += 360;
	while(rotation > 270)
		rotation -= 360;
	while(rotation != 0) {
		rotation -= 90;
		int p = pattern;
		if(p == DEMOSAIC_PATTERN_BAYER_RGGB)
			pattern = DEMOSAIC_PATTERN_BAYER_GRBG;
		if(p == DEMOSAIC_PATTERN_BAYER_GBRG)
			pattern = DEMOSAIC_PATTERN_BAYER_RGGB;
		if(p == DEMOSAIC_PATTERN_BAYER_GRBG)
			pattern = DEMOSAIC_PATTERN_BAYER_BGGR;
		if(p == DEMOSAIC_PATTERN_BAYER_BGGR)
			pattern = DEMOSAIC_PATTERN_BAYER_GBRG;
	}
	return pattern;
}

inline int __bayer_red(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return 0;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return 2;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return 1;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return 3;
	return -1;
}

inline int __bayer_green_r(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return 1;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return 3;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return 0;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return 2;
	return -1;
}

inline int __bayer_green_b(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return 2;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return 0;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return 3;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return 1;
	return -1;
}

inline int __bayer_blue(int pattern) {
	if(pattern == DEMOSAIC_PATTERN_BAYER_RGGB)
		return 3;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GBRG)
		return 1;
	if(pattern == DEMOSAIC_PATTERN_BAYER_GRBG)
		return 2;
	if(pattern == DEMOSAIC_PATTERN_BAYER_BGGR)
		return 0;
	return -1;
}

//------------------------------------------------------------------------------
// Fuji 45 support - code adapted from dcraw's fuji_rotate()
// raw size is considered as with edge at 2px on each side, for demosaic convenience
class Fuji_45 {
public:
	Fuji_45(int _fuji_width, int _width, int _height, bool from_raw_size = false) {
		fuji_width = _fuji_width - 1;
		step = sqrtf(0.5);
		step_2 = 1.0f / (2.0f * step);
		if(from_raw_size) {
//			int raw_width = _width;
			int raw_height = _height;
			f_width = fuji_width / step;
			f_height = (raw_height - fuji_width) / step;
		} else {
			f_width = _width;
			f_height = _height;
		}
	}
	int width(void) {return f_width;}
	int height(void) {return f_height;}
	// edge - to decrease effective area increase 'edge'
	bool raw_is_outside(int x, int y, int edge = 0) {
		float i = (x - y + fuji_width) * step_2;
		float j = (y - fuji_width + x) * step_2;
		bool skip = false;
		skip |= (i < (0.0f + edge - 1.0f) || i > f_width - edge - 4.0f);
		skip |= (j < (0.0f + edge) || j > f_height - edge - 4.0f);
		return skip;
	}
	// in/out - float 4 planes per px.
	// in - with offsets at each edge of 2px with junk
	// out - w/o any edge offsets
	void rotate_45(float *out, int x, int y, int in_width, int in_height, const float *in) {
		// (3, 4) - tweaked offset
		float r = fuji_width + (y + 3 - x) * step;
		float c = (y + 4 + x) * step;
		unsigned ur = r;
		unsigned uc = c;
		int ox = (y * f_width + x) * 4;
		if(r > in_height - 2 || c > in_width - 2) {
			out[ox + 0] = 0.0f;
			out[ox + 1] = 0.0f;
			out[ox + 2] = 0.0f;
			out[ox + 3] = 0.0f;
			return;
		}
		float fr = r - ur;
		float fc = c - uc;
		int ix = (ur * in_width + uc) * 4;
//		int ox = (y * out_width + x) * 4;
		for(int i = 0; i < 3; i++) {
			out[ox + i]  = (in[ix + i] * (1.0 - fc) + in[ix + 4 + i] * fc) * (1.0 - fr);
			out[ox + i] += (in[ix + in_width * 4 + i] * (1.0 - fc) + in[ix + in_width * 4 + 4 + i] * fc) * fr;
		}
		out[ox + 3] = 1.0f;
	}

protected:
	int fuji_width;
	int f_width;
	int f_height;
	float step;
	float step_2;
};

//------------------------------------------------------------------------------

#endif // __H_DEMOSAIC_PATTERN__
