#ifndef __H_DDR_MATH__
#define __H_DDR_MATH__
/*
 * ddr_match.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <map>

//#include <mutex>
#include <QVector>
#include <QPointF>

#include <iostream>

#include "misc.h"

//------------------------------------------------------------------------------
#define TABLE_FUNCTION_DEFAULT_SIZE 16384

class TableFunction {
public:
	virtual ~TableFunction();
	float operator()(float x);

protected:
	TableFunction(void);
	float x_min;
	float x_max;
	float scale;
	float *table;
	int table_size;

	void _init(float _x_min, float _x_max, int table_size);
	virtual float function(float x) = 0;
};

//------------------------------------------------------------------------------
// Spline - curves
class Spline_Calc {
public:
	Spline_Calc(const QVector<QPointF> &_points, float scale = 1.0, bool linear_2 = true, int type_left = 2, float df_left = 1.0, int type_right = 2, float df_right = 1.0);
	virtual ~Spline_Calc();

	float f(float x);

protected:
	QVector<QPointF> points;
	bool is_spline;
	// spline
	float *spline_data;
	float *px;
	float *py;
	// line
	float a;
	// edges
	float x_left, y_left;
	float x_right, y_right;

	float *d3_np_fs(int n, float *a, float *b);
	float spline_cubic_val(int n, float *x, float xval, float *y, float *ypp);
	void spline_cubic_set(int n, float *t, float *y, int ibcbeg, float ybcbeg, int ibcend, float ybcend);

	inline void normalize(float &v) {
		if(v < 0.0)	v = 0.0;
		if(v > 1.0)	v = 1.0;
	}
};

//------------------------------------------------------------------------------
// copy 3x3 matrix
template<class T1, class T2> void m3_copy(T1 *rez, const T2 *a) {
	for(int i = 0; i < 9; i++)
		rez[i] = a[i];
}

template<class T1, class T2> void v3_copy(T1 *rez, const T2 *a) {
	for(int i = 0; i < 3; i++)
		rez[i] = a[i];
}

// initialize matrix with value
template<class T1, class T2> void m3_init(T1 *rez, T2 value) {
	for(int i = 0; i < 9; i++)
		rez[i] = value;
}

// multiply two 3x3 matrices
template<class T> void m3_m3_mult(T *rez, const T *a, const T *b) {
	for(int i = 0; i < 3; i++) {
		for(int j = 0; j < 3; j++) {
			T v = 0;
			v += a[0 + j * 3] * b[i + 0 * 3];
			v += a[1 + j * 3] * b[i + 1 * 3];
			v += a[2 + j * 3] * b[i + 2 * 3];
			rez[i + j * 3] = v;
		}
	}
}

template<class T1, class T2, class T3> void m3_m3_mult(T1 *rez, const T2 *a, const T3 *b) {
	for(int i = 0; i < 3; i++) {
		for(int j = 0; j < 3; j++) {
			T1 v = 0;
			v += a[0 + j * 3] * b[i + 0 * 3];
			v += a[1 + j * 3] * b[i + 1 * 3];
			v += a[2 + j * 3] * b[i + 2 * 3];
			rez[i + j * 3] = v;
		}
	}
}

// invert 3x3 matrix
template<class T> bool m3_invert(T *rez, const T *arg) {
	const T *m = arg;
	T _m[9];
	if(rez == arg) {
		for(int i = 0; i < 9; i++)
			_m[i] = arg[i];
		m = _m;
	}
	const T &a = m[0];
	const T &b = m[1];
	const T &c = m[2];
	const T &d = m[3];
	const T &e = m[4];
	const T &f = m[5];
	const T &g = m[6];
	const T &h = m[7];
	const T &i = m[8];
	rez[0] =  (e * i - f * h);
	rez[1] = -(b * i - c * h);
	rez[2] =  (b * f - c * e);
	rez[3] = -(d * i - f * g);
	rez[4] =  (a * i - c * g);
	rez[5] = -(a * f - c * d);
	rez[6] =  (d * h - e * g);
	rez[7] = -(a * h - b * g);
	rez[8] =  (a * e - b * d);
	T det = a * (e * i - f * h) - b * (i * d - f * g) + c * (d * h - e * g);
	if(det == 0.0)
		return false;
	for(int i = 0; i < 9; i++)
		rez[i] /= det;
	return true;
/*
	rez[0 * 3 + 0] = m[1 * 3 + 1] * m[2 * 3 + 2] - m[1 * 3 + 2] * m[2 * 3 + 1];
	rez[0 * 3 + 1] = m[0 * 3 + 2] * m[2 * 3 + 1] - m[0 * 3 + 1] * m[2 * 3 + 2];
	rez[0 * 3 + 2] = m[0 * 3 + 1] * m[1 * 3 + 2] - m[0 * 3 + 2] * m[1 * 3 + 1];
	rez[1 * 3 + 0] = m[1 * 3 + 2] * m[2 * 3 + 0] - m[1 * 3 + 0] * m[2 * 3 + 2];
	rez[1 * 3 + 1] = m[0 * 3 + 0] * m[2 * 3 + 2] - m[0 * 3 + 2] * m[2 * 3 + 0];
	rez[1 * 3 + 2] = m[0 * 3 + 2] * m[1 * 3 + 0] - m[0 * 3 + 0] * m[1 * 3 + 2];
	rez[2 * 3 + 0] = m[1 * 3 + 0] * m[2 * 3 + 1] - m[1 * 3 + 1] * m[2 * 3 + 0];
	rez[2 * 3 + 1] = m[0 * 3 + 1] * m[2 * 3 + 0] - m[0 * 3 + 0] * m[2 * 3 + 1];
	rez[2 * 3 + 2] = m[0 * 3 + 0] * m[1 * 3 + 1] - m[0 * 3 + 1] * m[1 * 3 + 0];

	T det = m[0 * 3 + 0] * rez[0 * 3 + 0] + m[0 * 3 + 1] * rez[1 * 3 + 0] + m[0 * 3 + 2] * rez[2 * 3 + 0];
	if(det == 0.0)
		return false;
	for(int i = 0; i < 9; i++)
		rez[i] /= det;
	return true;
*/
}

// multiply 3x3 matrix and 3x1 vector
template<class T> void m3_v3_mult(T *rez, const T *m, const T *v) {
	for(int i = 0; i < 3; i++)
		rez[i] = v[0] * m[i * 3 + 0] + v[1] * m[i * 3 + 1] + v[2] * m[i * 3 + 2];
}

template<class T1, class T2, class T3> void m3_v3_mult(T1 *rez, const T2 *m, const T3 *v) {
	for(int i = 0; i < 3; i++)
		rez[i] = v[0] * m[i * 3 + 0] + v[1] * m[i * 3 + 1] + v[2] * m[i * 3 + 2];
}

template<class T> void m3_dump(T *m) {
	std::cerr << "._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._._" << std::endl;
	std::cerr << "dump of 3x3 matrix: " << std::endl;
	std::cerr << "\t" << m[0 * 3 + 0] << ", " << m[0 * 3 + 1] << ", " << m[0 * 3 + 2] << std::endl;
	std::cerr << "\t" << m[1 * 3 + 0] << ", " << m[1 * 3 + 1] << ", " << m[1 * 3 + 2] << std::endl;
	std::cerr << "\t" << m[2 * 3 + 0] << ", " << m[2 * 3 + 1] << ", " << m[2 * 3 + 2] << std::endl;
	std::cerr << "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-" << std::endl;
}

//------------------------------------------------------------------------------
float compression_function(float x, float x_max);

//------------------------------------------------------------------------------
// Gaussian elimination method to solve system of 3 linear equations.
// input: matrix 'm' 3x3 and vector 'v' 1x3:
// m[0] * x + m[1] * y + m[2] * z = v[0];
// m[3] * x + m[4] * y + m[5] * z = v[1];
// m[6] * x + m[7] * y + m[8] * z = v[2];
// output: vector [x, y, z]
template<class T> void m3_gaussian_elimination(T *v_rez, const T *m_in, const T *v_in) {
	T m[9];
	for(int i = 0; i < 9; i++)
		m[i] = m_in[i];
	T v[3];
	for(int i = 0; i < 3; i++)
		v[i] = v_in[i];
	int mi[3] = {0, 1, 2};
	T scale;
	// sort (around zero) by first column
	if(ddr::abs(m[mi[0] * 3 + 0]) < ddr::abs(m[mi[1] * 3 + 0])) ddr::swap(mi[0], mi[1]);
	if(ddr::abs(m[mi[0] * 3 + 0]) < ddr::abs(m[mi[2] * 3 + 0])) ddr::swap(mi[0], mi[2]);
	if(ddr::abs(m[mi[1] * 3 + 0]) < ddr::abs(m[mi[2] * 3 + 0])) ddr::swap(mi[1], mi[2]);
	// eliminate 'x'
	for(int i = 1; i < 3; i++) {
		if(m[mi[0] * 3 + 0] != 0.0)
			scale = m[mi[i] * 3 + 0] / m[mi[0] * 3 + 0];
		else
			scale = 1.0;
		m[mi[i] * 3 + 0] -= m[mi[0] * 3 + 0] * scale;
		m[mi[i] * 3 + 1] -= m[mi[0] * 3 + 1] * scale;
		m[mi[i] * 3 + 2] -= m[mi[0] * 3 + 2] * scale;
		v[mi[i]] -= v[mi[0]] * scale;
	}
	// sort (around zero) by second column
	if(ddr::abs(m[mi[1] * 3 + 1]) < ddr::abs(m[mi[2] * 3 + 1])) ddr::swap(mi[1], mi[2]);
	// eliminate 'y'
	if(m[mi[1] * 3 + 1] != 0.0)
		scale = m[mi[2] * 3 + 1] / m[mi[1] * 3 + 1];
	else
		scale = 1.0;
	m[mi[2] * 3 + 0] -= m[mi[1] * 3 + 0] * scale;
	m[mi[2] * 3 + 1] -= m[mi[1] * 3 + 1] * scale;
	m[mi[2] * 3 + 2] -= m[mi[1] * 3 + 2] * scale;
	v[mi[2]] -= v[mi[1]] * scale;
	// solve
	T z = 0.0;
	if(m[mi[2] * 3 + 2] != 0.0)
		z = v[mi[2]] / m[mi[2] * 3 + 2];
	T left = v[mi[1]] - z * m[mi[1] * 3 + 2];
	T y = 0.0;
	if(m[mi[1] * 3 + 1] != 0.0)
		y = left / m[mi[1] * 3 + 1];
	left = v[mi[0]] - y * m[mi[0] * 3 + 1] - z * m[mi[0] * 3 + 2];
	T x = 0.0;
	if(m[mi[0] * 3 + 0] != 0.0)
		x = left / m[mi[0] * 3 + 0];
	// return results
	v_rez[0] = x;
	v_rez[1] = y;
	v_rez[2] = z;
}

//------------------------------------------------------------------------------

#endif //__H_DDR_MATH__
