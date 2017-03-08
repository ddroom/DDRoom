/*
 * cm.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
 * CM - Color Model;
	used to represent abstract color properties such like lightness etc at edit time;
	used CIECAM02, CIELab with tree components - lightness, saturation and hue;
	lightness and hue are normalized to 0.0 - 1.0 interval; saturation depend on color model
 */
/*
NOTES:
	Jsh|Jsh|Vsh values limits and normalization:
		J|L|V	- 0.0 - 1.0 [0.0 - 100.0]
		s		- 0.0 - ?
		h		- 0.0 - 1.0 [0.0 - 360.0); for CIACAM02 hue is normalized to: 'red' == 0.0|1.0, 'yellow' == 0.33, 'blue' == 0.66
TODO:
	- found a way to fix triangle-alike peaks in CIECAM02 model (source of colors 'jumping' on smooth transitions after saturation increase)
	- verify CIECAM02 Jsh to XYZ code
	- calculate correct CIECAM02 table functions limits: check black levels
	- use only CIE 1931 2degree observer data
*/

/*
 References:
	[1]. The Problem with CAT02 and Its Correction. Changjun Li, Esther Perales, M Ronnier Luo and Francisco Martines-Verdu
	- changed CAT02 matrix to solve 'purple problem', and as result, fixed loop in gamut at blue; loop in gamut of NTSC at red
		is still present but is not so critical, will be subject of further investigation (probably w/o CAT02 modification).
*/

#include <math.h>

#include "cm.h"
#include "cms_matrix.h"
#include "sgt.h"
#include "ddr_math.h"
#include "memory.h"
#include "system.h"

#include <iostream>

#define CIECAM02_FIXED_BLUE
//#undef CIECAM02_FIXED_BLUE

#define USE_CAT false
//#define USE_CAT true

// NOTE: this setting affect SGT
//#define USE_HUE_QUADRANT
#undef USE_HUE_QUADRANT

using namespace std;

//#define FIX_CIECAM02_LIGHTNESS_PERCEPTION
#undef FIX_CIECAM02_LIGHTNESS_PERCEPTION

#ifdef FIX_CIECAM02_LIGHTNESS_PERCEPTION
//	#define PERC_SCALE_R 2.5
//	#define PERC_SCALE_G 1.0
//	#define PERC_SCALE_B 0.4

	#define PERC_SCALE_R 2.0
	#define PERC_SCALE_G 1.0
	#define PERC_SCALE_B 0.4

//	#define PERC_SCALE_R 2.5
//	#define PERC_SCALE_G 0.5
//	#define PERC_SCALE_B 0.4	// fix relative lightness of blue
#else
	#define PERC_SCALE_R 2.0
	#define PERC_SCALE_G 1.0
	#define PERC_SCALE_B 0.05
#endif


#define CIELAB_TABLES_SIZE	32768
//------------------------------------------------------------------------------
float h_to_H_quadrant(float h) {
	h *= 360.0f;
	while(h < 20.14f)	h += 360.0f;
	while(h >= 380.14f)	h -= 360.0f;
	// red, yelow, green, blue, red
	// ??? for CIELab - 24, 90, 162, 246 ???
	const float hi[] = {20.14, 90.0, 164.25, 237.53, 380.14};
	const float ei[] = {0.8, 0.7, 1.0, 1.2, 0.8};
	const float Hi[] = {0.0, 100.0, 200.0, 300.0, 400.0};
	float H = 0.0;
	for(int i = 0; i < 4; ++i) {
		if(h < hi[i + 1]) {
			H = Hi[i] + (100.0f * (h - hi[i]) / ei[i]) / ((h - hi[i]) / ei[i] + (hi[i + 1] - h) / ei[i + 1]);
			break;
//			return Hi[i] + (100 * (h - hi[i]) / ei[i]) / ((h - hi[i]) / ei[i] + (hi[i + 1] - h) / ei[i + 1]);
		}
	}
	while(H >= 400.0f) H -= 400.0f;
	while(H < 0.0f) H += 400.0f;
	H /= 400.0f;
	return H;
}

float H_quadrant_to_h(float H) {
	H *= 400.0f;
	while(H < 0.0f) H += 400.0f;
	while(H >= 400.0f) H -= 400.0f;
	// H == [0.0, 400.0)
	const float hi[] = {20.14, 90.0, 164.25, 237.53, 380.14};
	const float ei[] = {0.8, 0.7, 1.0, 1.2, 0.8};
	const float Hi[] = {0.0, 100.0, 200.0, 300.0, 400.0};
	float h = 0.0f;
	for(int i = 0; i < 4; ++i) {
		if(H >= Hi[i] && H < Hi[i + 1]) {
			float A1 = (H - Hi[i]) / ei[i];
			float A2 = (H - Hi[i]) / ei[i + 1];
			float A3 = 100.0f / ei[i];
			h = ((A1 - A3) * hi[i] - A2 * hi[i + 1]) / (A1 - A2 - A3);
			break;
		}
	}
//	float h = (H / 400.0f) * 360.0f;
	while(h < 0.0f)	h += 360.0f;
	while(h >= 360.0f)	h -= 360.0f;
	h /= 360.0f;
	return h;
}

//------------------------------------------------------------------------------
namespace cm {
// not thread-safe (does not matter for GUI usage) and cms_matrix should have non-movable gamma tables
void sRGB_to_Jsh(float *Jsh, const float *sRGB) {
	static CS_to_CM *cs_to_cm = new CS_to_CM(CM::cm_type_CIECAM02, "sRGB");
	cs_to_cm->convert(Jsh, sRGB);
}

void Jsh_to_sRGB(float *sRGB, const float *Jsh) {
	static CM_to_CS *cm_to_cs = new CM_to_CS(CM::cm_type_CIECAM02, "sRGB");
	cm_to_cs->convert(sRGB, Jsh);
}
}	// namespace cm

//--------------------------------------------------------------------------
void CM::initialize(void) {
	CIELab::initialize();
	CIECAM02::initialize();
}

CM* CM::new_CM(CM::cm_type_en type, CS_White white_in, CS_White white_out) {
	if(type == CM::cm_type_CIECAM02)
		return new CIECAM02(white_in, white_out);
	if(type == CM::cm_type_CIELab)
		return new CIELab(white_in, white_out);
	return new CM();
}

std::string CM::get_type_name(CM::cm_type_en type) {
	std::string name = "none";
	if(type == CM::cm_type_CIECAM02)	name = "CIECAM02";
	if(type == CM::cm_type_CIELab)		name = "CIELab";
	return name;
}

CM::cm_type_en CM::get_type(std::string name) {
	cm_type_en type = CM::cm_type_none;
	if(name == "CIECAM02")	type = CM::cm_type_CIECAM02;
	if(name == "CIELab")	type = CM::cm_type_CIELab;
	return type;
}

std::list<CM::cm_type_en> CM::get_types_list(void) {
	std::list<CM::cm_type_en> l;
	l.push_back(CM::cm_type_CIECAM02);
	l.push_back(CM::cm_type_CIELab);
	return l;
}

//------------------------------------------------------------------------------
class CIELab_Convert : public CM_Convert {
public:
	virtual void convert(float *Jsh, const float *XYZ){};
	float get_C_from_Jsh(const float *Jsh);
	float get_s_from_JCh(const float *JCh);
};

float CIELab_Convert::get_C_from_Jsh(const float *Jsh) {
	return (Jsh[0] * Jsh[1]);
}

float CIELab_Convert::get_s_from_JCh(const float *JCh) {
	if(JCh[0] > 0.00001f)
		return JCh[1] / JCh[0];
	return 0.0;
}

//------------------------------------------------------------------------------
class TF_CIELab : public TableFunction {
public:
	TF_CIELab(void) {
		_init(0.0f, 1.95f, CIELAB_TABLES_SIZE);
	}
protected:
	float function(float x) {
//		const static float _e = (6.0 / 29.0) * (6.0 / 29.0) * (6.0 / 29.0) / 100.0;
//		const static float _f1 = ((1.0 / 3.0) * (29.0 / 6.0) * (29.0 / 6.0)) / 100.0;
//		const static float _f2 = (4.0 / 29.0) / 100.0;
		x = ddr::clip(x);
//		if(x < 0.0)	return 0.0;
//		if(x > 1.0)	return 1.0;
//		if(x > _e)
			return powf(x, 1.0f / 3.0f);
//		else
//			return _f1 * x + _f2;
	}
};

class CIELab_XYZ_to_Jsh : public CM_Convert {
public:
	void convert(float *Jsh, const float *XYZ);
	float CAT[9];
	class TF_CIELab *tf_lab;
};

void CIELab_XYZ_to_Jsh::convert(float *Jsh, const float *XYZ) {
	float XYZa[3];
	// apply CAT from input to 'E' illuminant
	m3_v3_mult(XYZa, CAT, XYZ);
/*
	// convert from E to D50 with 'wrong von Kries transform'
	float fX = (*tf_lab)(XYZa[0] / 0.96422);
	float fY = (*tf_lab)(XYZa[1] / 1.00000);
	float fZ = (*tf_lab)(XYZa[2] / 0.82521);
*/
	// use 'E' to avoid 'wrong von Kries'

	float fX = (*tf_lab)(XYZa[0]);
	float fY = (*tf_lab)(XYZa[1]);
	float fZ = (*tf_lab)(XYZa[2]);
	//--
	Jsh[0] = (1.16f * fY - 0.16f);
	float a = 5.0f * (fX - fY);
	float b = 2.0f * (fY - fZ);
	Jsh[1] = sqrtf(a * a + b * b);
	Jsh[2] = ((180.0f / M_PI) * atan2f(b, a)) / 360.0f;
	if(Jsh[2] < 0.0f)
		Jsh[2] += 1.0f;
	if(Jsh[0] > 0.00001f)
		Jsh[1] = sqrtf(Jsh[1] / (0.1 * sqrtf(Jsh[0])));
//		Jsh[1] /= Jsh[0];
	else
		Jsh[1] = 0.0f;
#ifdef USE_HUE_QUADRANT
	Jsh[2] = h_to_H_quadrant(Jsh[2]);
#endif
}

class CIELab_Jsh_to_XYZ : public CIELab_Convert {
public:
	void convert(float *XYZ, const float *Jsh);
	float CAT[9];
};

void CIELab_Jsh_to_XYZ::convert(float *XYZ, const float *Jsh) {
//	static const float _s = 6.0 / 29.0;
//	static const float _f1 = 16.0 / 116.0;
//	static const float _f2 = 3.0 * (36.0 / 841.0);

	float h = Jsh[2];
#ifdef USE_HUE_QUADRANT
	h = H_quadrant_to_h(h);
#endif
	h *= 2.0f * M_PI;
	float L = Jsh[0];
//	if(L > 1.0)	L = 1.0;
//	if(L < 0.0)	L = 0.0;
	float C = Jsh[1] * Jsh[1] * 0.1f * sqrtf(L);

	float Y = (L + 0.16f) / 1.16f;
	float X = Y + C * cosf(h) / 5.0f;
	float Z = Y - C * sinf(h) / 2.0f;

	// convert to XYZ
	float XYZa[3];
	XYZa[1] = Y * Y * Y;
	XYZa[0] = X * X * X;
	XYZa[2] = Z * Z * Z;
/*
	if(Y > _s)	XYZa[1] = Y * Y * Y;
	else		XYZa[1] = (Y - _f1) * _f2;
	if(X > _s)	XYZa[0] = X * X * X;
	else		XYZa[0] = (X - _f1) * _f2;
	if(Z > _s)	XYZa[2] = Z * Z * Z;
	else		XYZa[2] = (Z - _f1) * _f2;
*/
/*
	// transform from D50 to E
	XYZa[0] *= 0.96422;
	XYZa[1] *= 1.00000;
	XYZa[2] *= 0.82521;
	// CAT from D50 to output illuminant
*/
	// CAT from 'E' to output illuminant
	m3_v3_mult(XYZ, CAT, XYZa);
}

//--------------------------------------------------------------------------
class TF_CIELab *CIELab::tf_CIELab = nullptr;

void CIELab::initialize(void) {
	if(tf_CIELab == nullptr)
		tf_CIELab = new TF_CIELab();
}

CIELab::CIELab(CS_White white_in, CS_White white_out) {
	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	cms_matrix->get_CAT(CAT_in, white_in, CS_White("E"));
	cms_matrix->get_CAT(CAT_out, CS_White("E"), white_out);
	// init functors
	cm_xyz_to_jsh = new CIELab_XYZ_to_Jsh();
	cm_jsh_to_xyz = new CIELab_Jsh_to_XYZ();
	for(int i = 0; i < 9; ++i) {
		cm_xyz_to_jsh->CAT[i] = CAT_in[i];
		cm_jsh_to_xyz->CAT[i] = CAT_out[i];
	}
	cm_xyz_to_jsh->tf_lab = tf_CIELab;
}

class CM_Convert *CIELab::get_convert_XYZ_to_Jsh(void) {
	return cm_xyz_to_jsh;
}

class CM_Convert *CIELab::get_convert_Jsh_to_XYZ(void) {
	return cm_jsh_to_xyz;
}

CIELab::~CIELab() {
	delete cm_xyz_to_jsh;
	delete cm_jsh_to_xyz;
}

//==============================================================================
#define CIECAM02_TABLES_SIZE 	16384
#define CIECAM02_TF_T_L1_MAX	1.0f
#define CIECAM02_TF_T_L2_MAX	800.0f
#define CIECAM02_TF_T_L3_MAX	6000.0f

class TF_pow : public TableFunction {
public:
	TF_pow(float _x_min, float _x_max, float _c) : c(_c) {
		_init(_x_min, _x_max, CIECAM02_TABLES_SIZE);
	}
protected:
	const float c;
	float function(float x) {return powf(x, c);}
};

class TF_nonlinear_post_adaptation : public TableFunction {
public:
	TF_nonlinear_post_adaptation(float _c) : c(_c / 100.0f) {
		_init(0.0f, 2.0f, CIECAM02_TABLES_SIZE);
	}
protected:
	const float c;
	float function(float x) {
		// fix brightness/purple problem -*1
		if(x < 0.0f)	x = 0.0f;
		x = powf(x * c, 0.42f);
		x = ((400.0f * x) / (27.13f + x)) + 0.1f;
		return x;
	}
};

class TF_inverse_nonlinear_post_adaptation : public TableFunction {
public:
	TF_inverse_nonlinear_post_adaptation(float _c) : c(100.0f / _c) {
		_init(0.0f, 2.0f, CIECAM02_TABLES_SIZE);
	}
protected:
	const float c;
	float function(float x) {
//		if(x < 0.1f)	x = 0.1f;
		x = (27.13f * (x - 0.1f)) / (400.0f - (x - 0.1f));
		if(x <= 0.0f)
			return 0.0f;
		x = c * powf(x, 1.0f / 0.42f);
		return x;
	}
};

//------------------------------------------------------------------------------
//CIECAM02 *CIECAM02::_this = nullptr;

class CAT02_t {
public:
	float Aw;
	float scale[3];
	float _s_const;
	float matrix[9];
};

class CIECAM02_priv {
public:
	CIECAM02_priv(void);
	CAT02_t *new_CAT02(CS_White cs_white_in, CS_White cs_white_out, bool inverse);
	class CM_Convert *get_convert_XYZ_to_Jsh(CAT02_t *cat02);
	class CM_Convert *get_convert_Jsh_to_XYZ(CAT02_t *cat02);

//protected:
	// color space related
	float _vc_La;   // adapting luminance
	float _vc_Yb;   // background relative luminance
	float _vc_F;	// maximum degree of adaptation
	float _vc_c;	// impact of surrounding
	float _vc_Nc;   // chromatic induction factor of surround

	// some inner data
	float _n;
	float _C_pow_const;
	float _z;   // base exponential nonlinearity
	float _Fl;  // luminance-level adaptation factor
	float _D;   // note: for emissive media D should be == 1.0
	float _Ncb; // chromatic induction factor of background
	float _Nbb; // brightness induction factor of background
	float _e_mult_const;	// e - eccentricity adjustment
	float _t_pow_const;		// t - temporary quantity
	float _s_const;			// s - saturation

	// subroutines
	double achromatic_response_for_white(const float *XYZ, float D, float Nbb);
	float D_factor(float F, float La);
	float calculate_Fl_from_La(const float &La);

	// cached functions
	class TF_nonlinear_post_adaptation *tf_nonlinear_post_adaptation;
	class TF_inverse_nonlinear_post_adaptation *tf_inverse_nonlinear_post_adaptation;
	class TF_pow *tf_tf;
	class TF_pow *tf_t_l1;
	class TF_pow *tf_t_l2;
	class TF_pow *tf_t_l3;
	class TF_pow *tf_tc_l1;
	class TF_pow *tf_tc_l2;
	class TF_pow *tf_tc_l3;
	class TF_pow *tf_tJ;

	double m_XYZ_to_HPE[9];
	double m_HPE_to_XYZ[9];
	double m_XYZ_to_CAT02[9];
	double m_CAT02_to_XYZ[9];
};

CIECAM02_priv *CIECAM02::priv = nullptr;
void CIECAM02::initialize(void) {
	priv = new CIECAM02_priv();
}

//------------------------------------------------------------------------------
class CIECAM02_Convert : public CM_Convert {
public:
	virtual void convert(float *XYZ, const float *Jsh) {};
	float get_C_from_Jsh(const float *Jsh);
	float get_s_from_JCh(const float *JCh);
	float cat02_s_const;
};

float CIECAM02_Convert::get_C_from_Jsh(const float *Jsh) {
	return (Jsh[1] * Jsh[1]) * cat02_s_const * sqrtf(Jsh[0]);
}

float CIECAM02_Convert::get_s_from_JCh(const float *JCh) {
	float s = 0.0f;
	if(JCh[0] > 0.0001f)
		s = sqrtf(JCh[1] / (sqrtf(JCh[0]) * cat02_s_const));
	return s;
}

//------------------------------------------------------------------------------
class CIECAM02_XYZ_to_Jsh : public CIECAM02_Convert {
public:
	void convert(float *XYZ, const float *Jsh);
	float cat02_matrix[9];
	float _e_mult_const;
	float _Nbb;
	float cat02_Aw;
	float _C_pow_const;
	class TF_nonlinear_post_adaptation *tf_nonlinear_post_adaptation;
	class TF_pow *tf_tJ;
	class TF_pow *tf_tc_l1;
	class TF_pow *tf_tc_l2;
	class TF_pow *tf_tc_l3;
	float tf_tc(float tx);
};

void CIECAM02_XYZ_to_Jsh::convert(float *Jsh, const float *XYZ) {
	float HPE[3];
	// XYZ to CAT02 and to HPE
	m3_v3_mult(HPE, cat02_matrix, XYZ);
	float Ra = (*tf_nonlinear_post_adaptation)(HPE[0]);
	float Ga = (*tf_nonlinear_post_adaptation)(HPE[1]);
	float Ba = (*tf_nonlinear_post_adaptation)(HPE[2]);
	float a = Ra - Ga * (12.0f / 11.0f) + Ba / 11.0f;
	float b = (1.0f / 9.0f) * (Ra + Ga - 2.0f * Ba);
	Jsh[2] = atan2f(b, a);
	float e = _e_mult_const * (cosf(Jsh[2] + 2.0f) + 3.8f);
	float t_div = Ra + Ga + Ba * (21.0f / 20.0f);
	if(t_div < 0.000001f)	t_div = 0.000001f;
	float t = (e * sqrtf((a * a) + (b * b))) / t_div;
//	float A = (2.0f * Ra + Ga + 0.05f * Ba - 0.305f) * _Nbb;
	float A = (PERC_SCALE_R * Ra + PERC_SCALE_G * Ga + PERC_SCALE_B * Ba - 0.305f) * _Nbb;
	if(A < 0.0f) A = 0.0f;
	Jsh[0] = (*tf_tJ)(A / cat02_Aw);
	Jsh[1] = sqrtf((tf_tc(t) * _C_pow_const) / cat02_s_const);
	if(Jsh[2] < 0.0f)	Jsh[2] += M_PI * 2.0f;
	Jsh[2] /= (M_PI * 2.0f);
#ifdef USE_HUE_QUADRANT
	Jsh[2] = h_to_H_quadrant(Jsh[2]);
#endif
}

float CIECAM02_XYZ_to_Jsh::tf_tc(float tx) {
	if(tx >= 0.0f && tx < CIECAM02_TF_T_L3_MAX) {
		if(tx < CIECAM02_TF_T_L1_MAX) {
			return (*tf_tc_l1)(tx);
		} else {
			if(tx < CIECAM02_TF_T_L2_MAX) {
				return (*tf_tc_l2)(tx);
			} else {
				return (*tf_tc_l3)(tx);
			}
		}
	} else
		return powf(tx, 0.9);
}

//------------------------------------------------------------------------------
class CIECAM02_Jsh_to_XYZ : public CIECAM02_Convert {
public:
	void convert(float *XYZ, const float *Jsh);
	float _e_mult_const;
//	float cat02_s_const;
	float _t_pow_const;
	float cat02_Aw;
	float _Nbb;
	float cat02_matrix[9];
	class TF_inverse_nonlinear_post_adaptation *tf_inverse_nonlinear_post_adaptation;
	class TF_pow *tf_tf;
	class TF_pow *tf_t_l1;
	class TF_pow *tf_t_l2;
	class TF_pow *tf_t_l3;
	float tf_t(float tx);
};

void CIECAM02_Jsh_to_XYZ::convert(float *XYZ, const float *_Jsh) {
	float J = _Jsh[0];
	float s = _Jsh[1];
	float h = _Jsh[2];
#ifdef USE_HUE_QUADRANT
	h = H_quadrant_to_h(h);
#endif
	h *= 2.0f * M_PI;
	if(J <= 0.0001f) // 0.01 step in [0 - 100] scale
		J = 0.0001f;
	float e = _e_mult_const * (cosf(h + 2.0f) + 3.8f);	// (18)
	float A = (*tf_tf)(J) * cat02_Aw;	// (21)
	float t = tf_t((s * s * cat02_s_const) / _t_pow_const);	// (23), (25)

	// formulas - (R, G, B) = F(A, a, b)
	float v_in[3] = {0, 0, (A / _Nbb) + 0.305f};
	float m_forward[9];
	m_forward[6] = PERC_SCALE_R;	// 2.0
	m_forward[7] = PERC_SCALE_G;	// 1.0
	m_forward[8] = PERC_SCALE_B;	// 0.05
	float sign_a = 1.0f;
	if(h > M_PI * 0.5f && h < M_PI * 1.5f) sign_a = -1.0f;
	float sign_b = 1.0f;
	if(h > M_PI) {
		h -= M_PI;
		sign_b = -1.0f;
	}
	if(h < M_PI * 0.25f || h > M_PI * 0.75f) {
		float tn = tanf(h);
		float Ct = sign_a * fabs((e / t) * sqrtf(1.0f + tn * tn));
		float Ctn = sign_b * fabs((9.0f * tn) / Ct);
		float Ct_r = 1.0f / Ct;
		m_forward[0] = 1.0f - Ct_r;
		m_forward[1] = -(12.0f / 11.0f) - Ct_r;
		m_forward[2] = 1.0f / 11.0f - Ct_r;
		m_forward[3] = 1.0f - Ctn;
		m_forward[4] = m_forward[3];
		m_forward[5] = -2.0f - (21.0f / 20.0f) * Ctn;
	} else {
		h = M_PI * 0.5f - h;
		float tn = tanf(h);
		float Ct = sign_b * fabs((e / t) * sqrtf(1.0f + tn * tn));
		float Ctn = sign_a * fabs(tn / Ct);
		float Ct_r = 9.0f / Ct;
		m_forward[0] = 1.0f - Ctn;
		m_forward[1] = -(12.0f / 11.0f) - Ctn;
		m_forward[2] = 1.0f / 11.0f - Ctn;
		m_forward[3] = 1.0f - Ct_r;
		m_forward[4] = m_forward[3];
		m_forward[5] = -2.0f - (21.0f / 20.0f) * Ct_r;
	}
	// solution
	float m_backward[9];
	m3_invert(m_backward, m_forward);
	float v[3];
	m3_v3_mult(v, m_backward, v_in);

	// reverse post adaptation compression to HPE
	v[0] = (*tf_inverse_nonlinear_post_adaptation)(v[0]);
	v[1] = (*tf_inverse_nonlinear_post_adaptation)(v[1]);
	v[2] = (*tf_inverse_nonlinear_post_adaptation)(v[2]);
	// and apply reverse HPE to CAT02 to XYZ
	m3_v3_mult(XYZ, cat02_matrix, v);
}

float CIECAM02_Jsh_to_XYZ::tf_t(float tx) {
	if(tx >= 0.0f && tx <= CIECAM02_TF_T_L3_MAX) {
		if(tx < CIECAM02_TF_T_L1_MAX) {
			return (*tf_t_l1)(tx);
		} else {
			return (*tf_t_l2)(tx);
		}
	} else {
		return powf(tx, 10.0f / 9.0f);
	}
}
//------------------------------------------------------------------------------
class CM_Convert *CIECAM02_priv::get_convert_XYZ_to_Jsh(CAT02_t *cat02) {
	CIECAM02_XYZ_to_Jsh *c = new CIECAM02_XYZ_to_Jsh();
	c->_e_mult_const = _e_mult_const;
	c->_Nbb = _Nbb;
	c->_C_pow_const = _C_pow_const;
	c->tf_nonlinear_post_adaptation = tf_nonlinear_post_adaptation;
	c->tf_tJ = tf_tJ;
	c->tf_tc_l1 = tf_tc_l1;
	c->tf_tc_l2 = tf_tc_l2;
	c->tf_tc_l3 = tf_tc_l3;
	for(int i = 0; i < 9; ++i)
		c->cat02_matrix[i] = cat02->matrix[i];
	c->cat02_Aw = cat02->Aw;
	c->cat02_s_const = cat02->_s_const;
	return c;
}

class CM_Convert *CIECAM02_priv::get_convert_Jsh_to_XYZ(CAT02_t *cat02) {
	CIECAM02_Jsh_to_XYZ *c = new CIECAM02_Jsh_to_XYZ();
	c->_e_mult_const = _e_mult_const;
	c->_t_pow_const = _t_pow_const;
	c->_Nbb = _Nbb;
	for(int i = 0; i < 9; ++i)
		c->cat02_matrix[i] = cat02->matrix[i];
	c->cat02_Aw = cat02->Aw;
	c->cat02_s_const = cat02->_s_const;
	c->tf_inverse_nonlinear_post_adaptation = tf_inverse_nonlinear_post_adaptation;
	c->tf_tf = tf_tf;
	c->tf_t_l1 = tf_t_l1;
	c->tf_t_l2 = tf_t_l2;
	c->tf_t_l3 = tf_t_l3;
	return c;
}

//------------------------------------------------------------------------------
CIECAM02::CIECAM02(CS_White white_in, CS_White white_out) {
	// CAT02 related
	cat02_in = priv->new_CAT02(white_in, CS_White("E"), false);
	cat02_out = priv->new_CAT02(CS_White("E"), white_out, true);
	cm_convert_XYZ_to_Jsh = priv->get_convert_XYZ_to_Jsh(cat02_in);
	cm_convert_Jsh_to_XYZ = priv->get_convert_Jsh_to_XYZ(cat02_out);
}

CIECAM02::~CIECAM02() {
	delete cat02_in;
	delete cat02_out;
	delete cm_convert_XYZ_to_Jsh;
	delete cm_convert_Jsh_to_XYZ;
}

class CM_Convert *CIECAM02::get_convert_XYZ_to_Jsh(void) {
	return cm_convert_XYZ_to_Jsh;
}

class CM_Convert *CIECAM02::get_convert_Jsh_to_XYZ(void) {
	return cm_convert_Jsh_to_XYZ;
}

CAT02_t *CIECAM02_priv::new_CAT02(CS_White cs_white_in, CS_White cs_white_out, bool inverse) {
/*
cerr << "inverse == " << inverse << endl;
cerr << "\tcs white in:  " << cs_white_in.get_name() << endl;
cerr << "\tcs white out: " << cs_white_out.get_name() << endl;
*/
	CAT02_t *cat02 = new CAT02_t();
	float _D = D_factor(_vc_F, _vc_La);
	float _XYZw[3];
	cs_white_out.get_XYZ(_XYZw);
	if(inverse)
		cs_white_in.get_XYZ(_XYZw);
	cat02->Aw = achromatic_response_for_white(_XYZw, _D, _Nbb);
	double _CAT02w[3];
	m3_v3_mult(_CAT02w, m_XYZ_to_CAT02, _XYZw);
	for(int i = 0; i < 3; ++i)
		cat02->scale[i] = (((_XYZw[1] * _D) / _CAT02w[i]) + (1.0f - _D));
	cat02->_s_const = (4.0f / _vc_c) * (cat02->Aw + 4.0f); 
	// CAT02 related matrices
	double m1[9];
	double m2[9];
	double vk1[9];
	double vk2[9];
	for(int i = 0; i < 9; ++i) {
		vk1[i] = 0.0f;
		vk2[i] = 0.0f;
	}
	double V_in[3];
	double V_out[3];
	double V[3];
	cs_white_in.get_XYZ(V);
	m3_v3_mult(V_in, m_XYZ_to_CAT02, V);
	cs_white_out.get_XYZ(V);
	m3_v3_mult(V_out, m_XYZ_to_CAT02, V);
	bool use_CAT = USE_CAT;
	if(inverse == false) {
		if(use_CAT == false) {
			// CAT02 XYZ to HPE
			for(int i = 0; i < 3; ++i) {
				vk1[i * 3 + i] = 1.0f / V_in[i];
				vk2[i * 3 + i] = V_out[i];
			}
			m3_m3_mult(m2, vk1, m_XYZ_to_CAT02);
			m3_m3_mult(m1, vk2, m2);
			for(int i = 0; i < 3; ++i)
				for(int j = 0; j < 3; ++j)
					m1[i * 3 + j] *= cat02->scale[i];
			m3_m3_mult(m2, m_CAT02_to_XYZ, m1);
			m3_m3_mult(cat02->matrix, m_XYZ_to_HPE, m2);
		} else {
			// CAT
			CMS_Matrix *cms_matrix = CMS_Matrix::instance();
			float m_CAT[9];
			cms_matrix->get_CAT(m_CAT, cs_white_in, cs_white_out);
			m3_m3_mult(m1, m_XYZ_to_CAT02, m_CAT);
			for(int i = 0; i < 3; ++i)
				for(int j = 0; j < 3; ++j)
					m1[i * 3 + j] *= cat02->scale[i];
			m3_m3_mult(m2, m_CAT02_to_XYZ, m1);
			m3_m3_mult(cat02->matrix, m_XYZ_to_HPE, m2);
		}
	} else {
		if(use_CAT == false) {
			// CAT02 HPE to XYZ
			m3_m3_mult(m1, m_XYZ_to_CAT02, m_HPE_to_XYZ);
			for(int i = 0; i < 3; ++i)
				for(int j = 0; j < 3; ++j)
					m1[i * 3 + j] /= (double)cat02->scale[i];
			for(int i = 0; i < 3; ++i) {
				vk1[i * 3 + i] = V_in[i];
				vk2[i * 3 + i] = 1.0f / V_out[i];
			}
			m3_invert(m2, vk1);
			m3_m3_mult(vk1, m2, m1);
			m3_invert(m2, vk2);
			m3_m3_mult(m1, m2, vk1);
			m3_m3_mult(cat02->matrix, m_CAT02_to_XYZ, m1);
		} else {
			// CAT
			m3_m3_mult(m1, m_XYZ_to_CAT02, m_HPE_to_XYZ);
			for(int i = 0; i < 3; ++i)
				for(int j = 0; j < 3; ++j)
					m1[i * 3 + j] /= (double)cat02->scale[i];
			m3_m3_mult(m2, m_CAT02_to_XYZ, m1);
			CMS_Matrix *cms_matrix = CMS_Matrix::instance();
			float m_CAT[9];
			cms_matrix->get_CAT(m_CAT, cs_white_in, cs_white_out);
			m3_m3_mult(cat02->matrix, m_CAT, m2);
		}
	}
	return cat02;
}

CIECAM02_priv::CIECAM02_priv(void) {
	_vc_La = 16.0f;
	_vc_Yb = 0.2f;	// XYZ are normalized to 0.0 - 1.0, so Yw will be 1.0 instead of 100.0, so Yb == 20.0
//	_vc_La = 4.0f;
//	_vc_Yb = 0.00001f;

	// surround  F    c      Nc
	// condition
	// average   1.0  0.69   1.0
	// dim       0.9  0.59   0.95
	// dark      0.8  0.525  0.8
	// use 'dim' for convenient 'ligtness' curve
	_vc_F = 0.9f;
	_vc_c = 0.59f;
	_vc_Nc = 0.95f;
	//--
//	_n = _vc_Yb / _XYZw[1];
	_n = _vc_Yb / 1.0f;	// use constant Yw == 100.0, i.e. Yw == 1.0;
	_Ncb = 0.725f * powf(1.0f / _n, 0.2f);
	_Nbb = _Ncb;
	_C_pow_const = powf(1.64f - powf(0.29f, _n), 0.73f);
	_z = 1.48f + sqrtf(_n);
	_Fl = calculate_Fl_from_La(_vc_La);

	// initialize tables
	tf_nonlinear_post_adaptation = new TF_nonlinear_post_adaptation(_Fl);
	tf_tJ = new TF_pow(0.0f, 1.4f, _vc_c * _z);
	tf_tc_l1 = new TF_pow(0.0f, CIECAM02_TF_T_L1_MAX, 0.9f);
	tf_tc_l2 = new TF_pow(CIECAM02_TF_T_L1_MAX, CIECAM02_TF_T_L2_MAX, 0.9f);
	tf_tc_l3 = new TF_pow(CIECAM02_TF_T_L2_MAX, CIECAM02_TF_T_L3_MAX, 0.9f);
	//--
	tf_inverse_nonlinear_post_adaptation = new TF_inverse_nonlinear_post_adaptation(_Fl);
	tf_tf = new TF_pow(0.0f, 1.0f, 1.0f / (_vc_c * _z));
	tf_t_l1 = new TF_pow(0.0f, CIECAM02_TF_T_L1_MAX, 10.0f / 9.0f);
	tf_t_l2 = new TF_pow(CIECAM02_TF_T_L1_MAX, CIECAM02_TF_T_L2_MAX, 10.0f / 9.0f);
	tf_t_l3 = new TF_pow(CIECAM02_TF_T_L2_MAX, CIECAM02_TF_T_L3_MAX, 10.0f / 9.0f);
	// constants
	_e_mult_const = (50000.0f / 13.0f) * _vc_Nc * _Ncb;
	_t_pow_const = powf(1.64f - powf(0.29f, _n), 0.73f);

	// matrices
	// LMS cone responses
	double _m_XYZ_to_HPE[] = {
		 0.38971, 0.68898, -0.07868,
//		 0.38971, 0.68898, -0.06200,
		-0.22981, 1.18340,  0.04641,
		 0.00000, 0.00000,  1.0};
	double _m_HPE_to_XYZ[9];
	m3_invert(_m_HPE_to_XYZ, _m_XYZ_to_HPE);
	// sharpened LMS
	// check [1]
	double _m_XYZ_to_CAT02[] = {
		 1.007245, 0.011136, -0.018381,
		-0.318061, 1.314589,  0.003471,
		 0.000000, 0.000000,  1.000000};
/*
	double _m_XYZ_to_CAT02[] = {
		0.7328, 0.4296, -0.1624,
		-0.7036, 1.6975,  0.0061,
#ifdef CIECAM02_FIXED_BLUE
		 0.0000, 0.0000,  1.0000};
#else
		 0.0030, 0.0136,  0.9834};
#endif
*/
	double _m_CAT02_to_XYZ[9];
	m3_invert(_m_CAT02_to_XYZ, _m_XYZ_to_CAT02);
	for(int i = 0; i < 9; ++i) {
		m_XYZ_to_HPE[i] = _m_XYZ_to_HPE[i];
		m_HPE_to_XYZ[i] = _m_HPE_to_XYZ[i];
		m_XYZ_to_CAT02[i] = _m_XYZ_to_CAT02[i];
		m_CAT02_to_XYZ[i] = _m_CAT02_to_XYZ[i];
	}
}

double CIECAM02_priv::achromatic_response_for_white(const float *XYZ, float D, float Nbb) {
	double _XYZ[3];
	for(int i = 0; i < 3; ++i)
		_XYZ[i] = (double)XYZ[i];
	double CAT02[3];
	m3_v3_mult(CAT02, m_XYZ_to_CAT02, _XYZ);
	CAT02[0] = CAT02[0] * (((XYZ[1] * D) / CAT02[0]) + (1.0 - D));
	CAT02[1] = CAT02[1] * (((XYZ[1] * D) / CAT02[1]) + (1.0 - D));
	CAT02[2] = CAT02[2] * (((XYZ[1] * D) / CAT02[2]) + (1.0 - D));
	double HPE[3];
	m3_v3_mult(_XYZ, m_CAT02_to_XYZ, CAT02);
	m3_v3_mult(HPE, m_XYZ_to_HPE, _XYZ);
	HPE[0] = (*tf_nonlinear_post_adaptation)(HPE[0]);
	HPE[1] = (*tf_nonlinear_post_adaptation)(HPE[1]);
	HPE[2] = (*tf_nonlinear_post_adaptation)(HPE[2]);
//	return (2.0 * HPE[0] + HPE[1] + 0.05 * HPE[2] - 0.305) * Nbb;
	return (PERC_SCALE_R * HPE[0] + PERC_SCALE_G * HPE[1] + PERC_SCALE_B * HPE[2] - 0.305f) * Nbb;
}

float CIECAM02_priv::D_factor(float F, float La) {
	return F * (1.0f - ((1.0f / 3.6f) * exp((-La - 42.0f) / 92.0f)));
}

float CIECAM02_priv::calculate_Fl_from_La(const float &La) {
	float La5 = La * 5.0f;
	float k = 1.0f / (La5 + 1.0f);
	k = k * k;
	k = k * k;
	return (0.2f * k * La5) + (0.1f * (1.0f - k) * (1.0f - k) * powf(La5, 1.0f / 3.0f));
}

//------------------------------------------------------------------------------
CS_to_CM::CS_to_CM(CM::cm_type_en _cm_type, std::string _cs_name) {
	cm_type = _cm_type;
	cs_name = _cs_name;

	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	cm = CM::new_CM(cm_type, CS_White(cms_matrix->get_illuminant_name(cs_name)), CS_White("E"));
	cm_convert = cm->get_convert_XYZ_to_Jsh();
	float m[9];
	cms_matrix->get_matrix_XYZ_to_CS(cs_name, m);
	m3_invert(matrix_CS_to_XYZ, m);
	float use_CAT = USE_CAT;
	if(use_CAT) {
//		const float *m_illuminant_convert = cms_matrix->get_CAT(m_CAT, CS_White("E"), cms_matrix->get_illuminant_name(cs_name));
		float m_CAT[9];
		cms_matrix->get_CAT(m_CAT, CS_White("E"), CS_White(cms_matrix->get_illuminant_name(cs_name)));
		m3_m3_mult(matrix_CS_to_XYZ, matrix_CS_to_XYZ, m_CAT);
	}

	inverse_gamma = cms_matrix->get_inverse_gamma(cs_name);
}

CS_to_CM::~CS_to_CM(void) {
	delete cm;
}

float CS_to_CM::get_C_from_Jsh(const float *Jsh) {
	return cm_convert->get_C_from_Jsh(Jsh);
}

float CS_to_CM::get_s_from_JCh(const float *JCh) {
	return cm_convert->get_s_from_JCh(JCh);
}

void CS_to_CM::convert(float *Jsh, const float *RGB) {
	float rgb[3];
	for(int i = 0; i < 3; ++i) {
		rgb[i] = (*inverse_gamma)(ddr::clip(RGB[i]));
/*
		rgb[i] = RGB[i];
		if(rgb[i] < 0.0f) rgb[i] = 0.0f;
		if(rgb[i] > 1.0f) rgb[i] = 1.0f;
		rgb[i] = (*inverse_gamma)(rgb[i]);
*/
	}
	float XYZ[3];
	m3_v3_mult(XYZ, matrix_CS_to_XYZ, rgb);
	cm_convert->convert(Jsh, XYZ);
}

//------------------------------------------------------------------------------
CM_to_CS::CM_to_CS(CM::cm_type_en _cm_type, std::string _cs_name) {
	cm_type = _cm_type;
	cs_name = _cs_name;
	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	cm = CM::new_CM(cm_type, CS_White("E"), CS_White(cms_matrix->get_illuminant_name(cs_name)));
	cms_matrix->get_matrix_XYZ_to_CS(cs_name, matrix_XYZ_to_CS);
	bool use_CAT = USE_CAT;
	if(use_CAT) {
		float m[9];
		float m_CAT[9];
		cms_matrix->get_CAT(m_CAT, CS_White("E"), CS_White(cms_matrix->get_illuminant_name(cs_name)));
		m3_m3_mult(m, matrix_XYZ_to_CS, m_CAT);
		for(int i = 0; i < 9; ++i)
			matrix_XYZ_to_CS[i] = m[i];
	}
	cm_convert = cm->get_convert_Jsh_to_XYZ();
	gamma = cms_matrix->get_gamma(cs_name);
}

CM_to_CS::~CM_to_CS(void) {
	delete cm;
}

float CM_to_CS::get_C_from_Jsh(const float *Jsh) {
	return cm_convert->get_C_from_Jsh(Jsh);
}

float CM_to_CS::get_s_from_JCh(const float *JCh) {
	return cm_convert->get_s_from_JCh(JCh);
}

void CM_to_CS::convert(float *RGB, const float *Jsh, bool clip) {
	float XYZ[3];
	cm_convert->convert(XYZ, Jsh);
	m3_v3_mult(RGB, matrix_XYZ_to_CS, XYZ);
	if(clip) {
		for(int i = 0; i < 3; ++i) {
			RGB[i] = (*gamma)(ddr::clip(RGB[i]));
/*
			if(RGB[i] < 0.0) RGB[i] = 0.0;
			if(RGB[i] > 1.0) RGB[i] = 1.0;
			RGB[i] = (*gamma)(RGB[i]);
*/
		}
	} else {
		for(int i = 0; i < 3; ++i) {
			if(RGB[i] < 0.0) RGB[i] = 0.0;
			if(RGB[i] > 0.0 && RGB[i] < 1.0)
				RGB[i] = (*gamma)(RGB[i]);
		}
	}
}

//------------------------------------------------------------------------------
/*
	x = X / (X + Y + Z);
	y = Y / (X + Y + Z);
	z = Z / (X + Y + Z);
	x + y + z = 1.0;
	usually used xyY with Y normalized to 1.0
*/

#define TEMP_KELVIN_MIN 1000
#define TEMP_KELVIN_MAX 14999
void temp_to_xy(float *xy, float temp) {
	// Calculate CCR x, y from Kelvin via approximated Plankian locus in the CIE 1960 UCS 
	// This approximation is accurate to within delta(u) < 8e-5 and delta(v) < 9e-5 for 1000K < T < 15000K
	// reference: http://en.wikipedia.org/wiki/Planckian_locus as is at August, 2009
	if(temp < TEMP_KELVIN_MIN)
		temp = TEMP_KELVIN_MIN;
	if(temp > TEMP_KELVIN_MAX)
		temp = TEMP_KELVIN_MAX;
	double t = temp;
	double t2 = t * t;
	double u = (double(0.860117757) + t * 1.54118254e-4 + t2 * 1.28641212e-7) / (double(1.0) + t * 8.42420235e-4 + t2 * 7.08145163e-7);
	double v = (double(0.317398726) + t * 4.22806245e-5 + t2 * 4.20481691e-8) / (double(1.0) - t * 2.89741816e-5 + t2 * 1.61456053e-7);
	xy[0] = (u * 3) / (u * 2 - v * 8 + 4);
	xy[1] = (v * 2) / (u * 2 - v * 8 + 4);
}

void xy_to_XYZ(float *XYZ, const float *xy) {
	XYZ[0] = xy[0] / xy[1];
	XYZ[1] = 1.0;
	XYZ[2] = (1.0 - xy[0] - xy[1]) / xy[1];
}

void XYZ_to_xy(float *xy, const float *XYZ) {
	float X = XYZ[0] / XYZ[1];
	float Z = XYZ[2] / XYZ[1];
	xy[0] = X / (X + 1.0 + Z);
	xy[1] = 1.0 / (X + 1.0 + Z);
}

// compute RGB to XYZ convertion matrix from given RGB primaries xy and reference white XYZ
void primaries_to_matrix(float *M, const float *red_xy, const float *green_xy, const float *blue_xy, const float *white_XYZ) {
	float XYZr[3];
	float XYZg[3];
	float XYZb[3];
	xy_to_XYZ(XYZr, red_xy);
	xy_to_XYZ(XYZg, green_xy);
	xy_to_XYZ(XYZb, blue_xy);
	float m[9];
	for(int i = 0 ; i < 3; ++i) {
		m[i * 3 + 0] = XYZr[i];
		m[i * 3 + 1] = XYZg[i];
		m[i * 3 + 2] = XYZb[i];
	}
	m3_invert(m, m);
	float S[3];
	m3_v3_mult(S, m, white_XYZ);
	for(int i = 0; i < 3; ++i) {
		M[i * 3 + 0] = S[0] * XYZr[i];
		M[i * 3 + 1] = S[1] * XYZg[i];
		M[i * 3 + 2] = S[2] * XYZb[i];
	}
}

//------------------------------------------------------------------------------
