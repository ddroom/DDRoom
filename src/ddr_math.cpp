/*
 * ddr_math.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "ddr_math.h"

#include <iostream>

#include <math.h>
#include <stdint.h>

using namespace std;

#define _USE_ISNAN
//#undef _USE_ISNAN

//------------------------------------------------------------------------------
#define TF_SIZE_MIN	256
#define TF_SIZE_MAX	65536

TableFunction::TableFunction() {
	table = nullptr;
	x_min = 0.0;
	x_max = 0.0;
}

TableFunction::~TableFunction() {
	if(table != nullptr)
		delete[] table;
}

void TableFunction::_init(float _x_min, float _x_max, int _table_size) {
	x_min = _x_min;
	x_max = _x_max;
	table_size = _table_size;
	if(table_size > TF_SIZE_MAX)	table_size = TF_SIZE_MAX;
	if(table_size < TF_SIZE_MIN)	table_size = TF_SIZE_MIN;
	table = new float[table_size];
	scale = x_max - x_min;
	float base = table_size - 1;
	for(int i = 0; i < table_size; ++i)
		table[i] = this->function(x_min + (float(i) / base) * scale);
}

float TableFunction::operator()(float x) {
	// TODO: fix CIECAM02 model to avoid 'nan' argument that
#ifdef isnan
#ifdef _USE_ISNAN
	if(isnan(x)) {
		x = 0.0;
//cerr << "TableFunction(isnan) == 0.0" << endl;
	}
#endif
#endif
	if(x < x_min || x > x_max) {
		return function(x);
	}
	x = (x - x_min) / scale;
	float part = x * (table_size - 1);
	int index = part;
	if(index < 0)
		return table[0];
	if(index >= table_size - 1)
		return table[table_size - 1];
	part -= index;
	// linear interpolation
	float v1 = table[index];
	float v2 = table[index + 1];
	return (v1 + (v2 - v1) * part);
}
/*
float TableFunction::function(float x) {
	return x;
}
*/
//------------------------------------------------------------------------------
GaussianKernel::GaussianKernel(float sigma, int _width, int _height)
	: i_sigma(sigma), i_width(_width), i_height(_height)
{
	i_kernel = new float[i_width * i_height];
	i_offset_x = -i_width / 2;
	i_offset_y = -i_height / 2;
	const float f_offset_x = i_offset_x;
	const float f_offset_y = i_offset_y;
	const float sigma_sq2 = sigma * sigma * 2.0f;
	float w_sum = 0.0f;
	for(int y = 0; y < i_height; ++y) {
		for(int x = 0; x < i_width; ++x) {
			const float fx = f_offset_x + x;
			const float fy = f_offset_y + y;
			const float z = sqrtf(fx * fx + fy * fy);
			const float w = expf(-(z * z) / sigma_sq2);
			i_kernel[y * i_width + x] = w;
			w_sum += w;
		}
	}	
	for(int y = 0; y < i_height; ++y) {
		for(int x = 0; x < i_width; ++x) {
			i_kernel[y * i_width + x] /= w_sum;
		}
	}
}

GaussianKernel::GaussianKernel(const GaussianKernel &other) :
	i_sigma(other.i_sigma), i_width(other.i_width), i_height(other.i_height),
	i_offset_x(other.i_offset_x), i_offset_y(other.i_offset_y)
{
	const int s = i_width * i_height;
	i_kernel = new float[s];
	for(int i = 0; i < s; ++i)
		i_kernel[i] = other.i_kernel[i];
}

GaussianKernel & GaussianKernel::operator=(const GaussianKernel &other) {
	*(const_cast<float *>(&i_sigma)) = other.i_sigma;
	*(const_cast<int *>(&i_width)) = other.i_width;
	*(const_cast<int *>(&i_height)) = other.i_height;
	i_offset_x = other.i_offset_x;
	i_offset_y = other.i_offset_y;

	const int s = i_width * i_height;
	i_kernel = new float[s];
	for(int i = 0; i < s; ++i)
		i_kernel[i] = other.i_kernel[i];
	return *this;
}

GaussianKernel::~GaussianKernel() {
	delete[] i_kernel;
}

//------------------------------------------------------------------------------
// used cubic polynomial spline
Spline_Calc::~Spline_Calc() {
	if(is_spline) {
		delete[] spline_data;
		delete[] px;
		delete[] py;
	}
}

Spline_Calc::Spline_Calc(const std::vector<std::pair<float, float>> &_points, float scale, bool linear_2, int type_left, float df_left, int type_right, float df_right) {
	if(_points.size() < 2) {
		throw("wrong points vector in Spline_Calc constructor");
	}
	// normalize points - use middle Y for all points with the same X
	for(size_t i = 0; i < _points.size(); ++i) {
		int c = 1;
		float x = _points[i].first;
		float y = _points[i].second;
		for(size_t j = i + 1; j < _points.size(); ++j) {
			if(_points[i].first != _points[j].first) {
				break;
			} else {
				i = j;
				y += _points[i].second;
				++c;
			}
		}
		if(c != 1)
			y /= c;
		points.push_back(std::pair<float, float>(x / scale, y / scale));
	}
//	points = _points;
	int size = points.size();
	is_spline = false;

	x_left = points[0].first;
	y_left = points[0].second;
	x_right = points[size - 1].first;
	y_right = points[size - 1].second;
	normalize(x_left);
	normalize(y_left);
	normalize(x_right);
	normalize(y_right);

//	if(size <= 2) {
	if(size < 3 && linear_2) {
		a = (y_right - y_left) / (x_right - x_left);
	} else {
		is_spline = true;
		px = new float[size];
		py = new float[size];
		for(int i = 0; i < size; ++i) {
			px[i] = points[i].first;
			py[i] = points[i].second;
		}
		spline_cubic_set(size, px, py, type_left, df_left, type_right, df_right);

//		spline_cubic_set(size, px, py, 2, 1.0, 2, 1.0);

//		spline_cubic_set(size, px, py, 2, 0.0, 2, 0.0);
//		spline_cubic_set(size, px, py, 2, 0.0, 2, 0.0);
//		spline_cubic_set(size, px, py, 1, 1.0, 1, 1.0);
//		spline_cubic_set(size, px, py, 1, 0.0, 1, 0.0);
//		spline_cubic_set(size, px, py, 0, 0.0, 0, 0.0);
	}
}

float Spline_Calc::f(float x) {
	if(x <= x_left)
		return y_left;
	if(x >= x_right)
		return y_right;

	float rez = 0.0;
	if(!is_spline) {
		rez = (x - x_left) * a + y_left;
	} else  {
		rez = spline_cubic_val(points.size(), px, x, py, spline_data);
	}
	normalize(rez);
	return rez;
}

/*
 * from the SPLINE library by John Burkardt, as at Aug 5, 2009 
 * original license is GNU LGPL
 * adapted for DDRoom by Mykhailo Malyshko aka Spectr, 2009
 * license changed to GPLv3
 */
float *Spline_Calc::d3_np_fs(int n, float *a, float *b) {
	double xmult;
	// Check.
	for(int i = 0; i < n; ++i)
		if(a[1 + i * 3] == 0.0)
			return nullptr;
	float *x = new float[n];
	for(int i = 0; i < n; ++i)
		x[i] = b[i];
	for(int i = 1; i < n; ++i) {
		xmult = a[2 + (i - 1) * 3] / a[1 + (i - 1) * 3];
		a[1 + i * 3] = a[1 + i * 3] - xmult * a[0 + i * 3];
		x[i] = x[i] - xmult * x[i - 1];
	}
	x[n - 1] = x[n - 1] / a[1 + (n - 1) * 3];
	for(int i = n - 2; 0 <= i; --i)
		x[i] = (x[i] - a[0 + (i + 1) * 3] * x[i + 1]) / a[1 + i * 3];
	return x;
}

void Spline_Calc::spline_cubic_set(int n, float *t, float *y, int ibcbeg, float ybcbeg, int ibcend, float ybcend) {
	float *ypp = nullptr;
	// Solve the linear system.
	if(n == 2 && ibcbeg == 0 && ibcend == 0) {
		ypp = new float[2];
		ypp[0] = 0.0;
		ypp[1] = 0.0;
	} else {
		float *a = new float[n * 3];
		float *b = new float[n];
		// Set up the first equation.
		switch(ibcbeg) {
		case 0:
			b[0] = 0.0;
			a[1 + 0 * 3] = 1.0;
			a[0 + 1 * 3] = -1.0;
			break;
		case 1:
			b[0] = (y[1] - y[0]) / (t[1] - t[0]) - ybcbeg;
			a[1 + 0 * 3] = (t[1] - t[0]) / 3.0;
			a[0 + 1 * 3] = (t[1] - t[0]) / 6.0;
			break;
		default:
			b[0] = ybcbeg;
			a[1 + 0 * 3] = 1.0;
			a[0 + 1 * 3] = 0.0;
		}
		// Set up the intermediate equations.
		for(int i = 1; i < n - 1; ++i) {
			b[i] = (y[i + 1] - y[i]) / (t[i + 1] - t[i]) - (y[i] - y[i - 1]) / (t[i] - t[i - 1]);
			a[2 + (i - 1) * 3] = (t[i] - t[i - 1]) / 6.0;
			a[1 + i * 3] = (t[i + 1] - t[i - 1]) / 3.0;
			a[0 + (i + 1) * 3] = (t[i + 1] - t[i]) / 6.0;
		}
		// Set up the last equation.
		switch(ibcend) {
		case 0:
			b[n - 1] = 0.0;
			a[2 + (n - 2) * 3] = -1.0;
			a[1 + (n - 1) * 3] = 1.0;
			break;
		case 1:
			b[n - 1] = ybcend - (y[n - 1] - y[n - 2]) / (t[n - 1] - t[n - 2]);
			a[2 + (n - 2) * 3] = (t[n - 1] - t[n - 2]) / 6.0;
			a[1 + (n - 1) * 3] = (t[n - 1] - t[n - 2]) / 3.0;
			break;
		default:
			b[n - 1] = ybcend;
			a[2 + (n - 2) * 3] = 0.0;
			a[1 + (n - 1) * 3] = 1.0;
		}
		ypp = d3_np_fs(n, a, b);
		// if ypp == nullptr - The linear system could not be solved.
		delete[] a;
		delete[] b;
	}
	spline_data = ypp;
}

//double Spline::spline_cubic_val(int n, double t[], double tval, double y[], double ypp[], double *ypval, double *yppval) {
float Spline_Calc::spline_cubic_val(int n, float *x, float xval, float *y, float *ypp) {
	// Determine the interval [ T(I), T(I+1) ] that contains TVAL.
	// Values below T[0] or above T[N-1] use extrapolation.
	int ival = n - 2;
	for(int i = 0; i < n - 1; ++i) {
		if(xval < x[i+1]) {
			ival = i;
			break;
		}
	}

	// In the interval I, the polynomial is in terms of a normalized
	// coordinate between 0 and 1.
	float dt = xval - x[ival];
	float h = x[ival + 1] - x[ival];
	float yval = y[ival]
		+ dt * ((y[ival + 1] - y[ival]) / h
			- (ypp[ival + 1] / 6.0 + ypp[ival] / 3.0) * h 
		+ dt * (0.5 * ypp[ival] 
		+ dt * ((ypp[ival + 1] - ypp[ival]) / (6.0 * h))));
	// do we need this ???
/*
	*ypval = (y[ival + 1] - y[ival]) / h
		- (ypp[ival + 1] / 6.0 + ypp[ival] / 3.0) * h
		+ dt * (ypp[ival]
		+ dt * (0.5 * ( ypp[ival + 1] - ypp[ival]) / h));
	*yppval = ypp[ival] + dt * (ypp[ival + 1] - ypp[ival]) / h;
*/
	return yval;
}

//------------------------------------------------------------------------------

float compression_function_spline(float x, float x_max);
float compression_function_sqrt(float x, float x_max);
float compression_function_sqrt_F(float x);

float compression_function(float x, float x_max) {
	return compression_function_spline(x, x_max);
//	return compression_function_sqrt(x, x_max);
}

float compression_function_spline(float x, float x_max) {
	if(x_max <= 1.0f)
		return (x < 1.0f) ? x : 1.0f;
	if(x_max > 3.0f)
		x_max = 3.0f;
	if(x >= x_max)
		return 1.0f;
	//
	static Spline_Calc *spline = nullptr;
	if(spline == nullptr) {
		std::vector<std::pair<float, float>> points(2);
		points[0] = std::pair<float, float>(0.0f, 0.0f);
		points[1] = std::pair<float, float>(1.0f, 1.0f / 3.0f);
		spline = new Spline_Calc(points, 1.0f, false, 1, 1.0f, 1, 0.0f);
	}
	float OB = (x_max - 1.0f) * 0.5f;
	float edge = 1.0 - OB;
	if(x <= edge)
		return x;
	float y = spline->f((x - edge) / (x_max - edge));
	return y * 3.0f * OB + edge;
}

float compression_function_sqrt(float x, float x_max) {
	if(x_max <= 1.0f)
		x_max = 1.0f;
	if(x_max >= 3.0f)
		x_max = 3.0f;
	//
	if(x < 0.0f)
		return 0.0f;
	if(x >= x_max)
		return 1.0f;
	//--
	const float px = 0.375f / cosf(M_PI / 4.0f);
	const float py = compression_function_sqrt_F(px);
	const float c_aspect = px / py - 1.0f;
	//--
	float AB = (x_max - 1.0f) / c_aspect;
	float x_linear = 1.0f - AB;
	if(x <= x_linear)
		return x;
	return x_linear + (compression_function_sqrt_F((x - x_linear) * px / (x_max - x_linear)) / py) * AB;
}

float compression_function_sqrt_F(float x) {
	const float cos_angle = cosf(M_PI / 4.0f);
	float l = x;
	float Ax = l * cos_angle;
	float Ay = Ax;
	float k = 2.0f * Ax;
	float Bx = (2.0f * k + 1.0f - sqrtf(4.0f * k + 1.0f)) / 2.0f;
	float By = sqrtf(Bx);
	float dx = Ax - Bx;
	float dy = By - Ay;
	float y = sqrtf(dx * dx + dy * dy);
	return y;
}

//------------------------------------------------------------------------------
