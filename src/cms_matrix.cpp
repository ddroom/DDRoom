/*
 * cms_matrix.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
 TODO:
	- verify and use only CIE 1931 all the time

*/

#include <iostream>
#include <math.h>
#include <map>

#include "cms_matrix.h"
#include "ddr_math.h"

#define DEFAULT_OUTPUT_COLOR_SPACE	"HDTV"

#undef CS_RARE_ENABLE
//#define CS_RARE_ENABLE

using namespace std;
//------------------------------------------------------------------------------
CMS_Matrix::~CMS_Matrix() {
}

class CMS_Matrix::color_space_t {
public:
	static map<string, pair<float, float> > illuminants;

	string key;
	string name;

	float matrix[9];
	string illuminant;
	float gamma_offset;
	float gamma_value;
	float gamma_transition;
	float gamma_slope;
	bool gamma_simple;
	bool hidden;

	color_space_t(void) {
//		illuminant = "D65";
		illuminant = "E";
		gamma_offset = -1.0;
		gamma_value = -1.0;
		gamma_transition = -1.0;
		gamma_slope = -1.0;
		gamma_simple = false;
		for(int i = 0; i < 9; ++i) {
			matrix[i] = 0.0;
		}
		matrix[0] = 1.0;
		matrix[4] = 1.0;
		matrix[8] = 1.0;
		hidden = false;
	}

	void set_matrix(double m0, double m1, double m2, double m3, double m4, double m5, double m6, double m7, double m8) {
		matrix[0] = m0;
		matrix[1] = m1;
		matrix[2] = m2;
		matrix[3] = m3;
		matrix[4] = m4;
		matrix[5] = m5;
		matrix[6] = m6;
		matrix[7] = m7;
		matrix[8] = m8;
	}

	void set_desc(const string &_key) {
		key = _key;
		name = _key;
	}

	void set_key(const string &_key) {
		key = _key;
		if(name == "")
			name = _key;
	}

	void set_name(const string &_name) {
		name = _name;
	}

	void set_gamma(double _offset, double _value, double _transition, double _slope) {
		gamma_simple = false;
		gamma_offset = _offset;
		gamma_value = _value;
		gamma_transition = _transition;
		gamma_slope = _slope;
	}

	void set_gamma(double _value) {
		gamma_offset = -1.0;
		gamma_value = _value;
		gamma_transition = -1.0;
		gamma_slope = -1.0;
		gamma_simple = true;
	}
};

map<string, pair<float, float> > CMS_Matrix::color_space_t::illuminants;

//------------------------------------------------------------------------------
list<CMS_Matrix::color_space_t> CMS_Matrix::color_spaces;

bool CMS_Matrix::get_illuminant_XYZ(float *XYZ, std::string illuminant_name) {
	map<string, pair<float, float> >::iterator it = CMS_Matrix::color_space_t::illuminants.find(illuminant_name);
	if(it == CMS_Matrix::color_space_t::illuminants.end())
		return false;
	XYZ[0] = (*it).second.first;
	XYZ[1] = 1.0;
	XYZ[2] = (*it).second.second;
	return true;
}

void CMS_Matrix::get_CAT(float *CAT_m, CS_White from, CS_White to) {
	float XYZ_src[3];
	float XYZ_dst[3];
	from.get_XYZ(XYZ_src);
	to.get_XYZ(XYZ_dst);
	// CAT02, is von Kries adaptation with LMS
	float m_CAT02[9] = {
		 0.4002,  0.7076, -0.0808,
		-0.2263,  1.1653,  0.0457,
		 0.0000,  0.0000,  0.9182};
/*
	float m_CAT02[9] = {
		 0.4002,  0.7076, -0.0808,
		-0.2263,  1.1653,  0.0457,
		 0.0000,  0.0000,  0.9182};
*/
	float m_inverse_CAT02[9];
	m3_invert(m_inverse_CAT02, m_CAT02);
	float LMS_src[3];
	float LMS_dst[3];
	m3_v3_mult(LMS_src, m_CAT02, XYZ_src);
	m3_v3_mult(LMS_dst, m_CAT02, XYZ_dst);
	float m_[9] = {
		LMS_dst[0] / LMS_src[0], 0, 0,
		0, LMS_dst[1] / LMS_src[1], 0,
		0, 0, LMS_dst[2] / LMS_src[2]};
	float mt[9];
	m3_m3_mult(mt, m_, m_CAT02);
	m3_m3_mult(CAT_m, m_inverse_CAT02, mt);
/*
	// Bradford CAT
	float r_src =  0.8951 * XYZ_src[0] +  0.2664 * XYZ_src[1] + -0.1614 * XYZ_src[2];
	float g_src = -0.7502 * XYZ_src[0] +  1.7135 * XYZ_src[1] +  0.0367 * XYZ_src[2];
	float b_src =  0.0389 * XYZ_src[0] + -0.0685 * XYZ_src[1] +  1.0296 * XYZ_src[2];
	float r_dst =  0.8951 * XYZ_dst[0] +  0.2664 * XYZ_dst[1] + -0.1614 * XYZ_dst[2];
	float g_dst = -0.7502 * XYZ_dst[0] +  1.7135 * XYZ_dst[1] +  0.0367 * XYZ_dst[2];
	float b_dst =  0.0389 * XYZ_dst[0] + -0.0685 * XYZ_dst[1] +  1.0296 * XYZ_dst[2];
	float m_cone_responce[9] = {
		 0.8951,  0.2664, -0.1614,
		-0.7502,  1.7135,  0.0367,
		 0.0389, -0.0685,  1.0296};
	float m_inverse_cone_responce[9] = {
		 0.986993, -0.147054,  0.159963,
		 0.432305,  0.518360,  0.049291,
		-0.008529,  0.040043,  0.968487};
	float m_[9] = {
		r_dst / r_src, 0, 0,
		0, g_dst / g_src, 0,
		0, 0, b_dst / b_src};
	float mt[9];
	m3_m3_mult(mt, m_, m_cone_responce);
	m3_m3_mult(CAT_m, m_inverse_cone_responce, mt);
*/
}

void CMS_Matrix::load_color_spaces(void) {
	if(color_spaces.begin() != color_spaces.end())
		return;
	color_space_t cs;

	// CIE 1931, X and Z, Y == 1.0
	color_space_t::illuminants["A"]   = pair<float, float>(1.09850, 0.35585);
	color_space_t::illuminants["B"]   = pair<float, float>(0.99090, 0.85324);
	color_space_t::illuminants["C"]   = pair<float, float>(0.98074, 1.18232);
	color_space_t::illuminants["D50"] = pair<float, float>(0.96422, 0.82521);
	color_space_t::illuminants["D55"] = pair<float, float>(0.95682, 0.92149);
	color_space_t::illuminants["D65"] = pair<float, float>(0.95047, 1.08883);
	color_space_t::illuminants["D75"] = pair<float, float>(0.94972, 1.22638);
	color_space_t::illuminants["E"]   = pair<float, float>(1.00000, 1.00000);

	// L = (L < gamma_transition) ? (gamma_slope * L) : ((1.0 + gamma_offset) * pow(L, gamma_value) - gamma_offset);
	// ProPhoto 0.001953 == 16 ^ (1.8 / (1.0 - 1.8))
	// ProPhoto = (L < 0.001953) ? (16 * V) : (pow(V, 1.0 / 1.8))

	// (!!!) mandatory, preferable hidden
	cs.hidden = true;
	cs.set_key("XYZ");
	cs.set_name("XYZ");
	cs.illuminant = "E";
	cs.set_matrix(	 1.0000000,  0.0000000,  0.0000000,
					 0.0000000,  1.0000000,  0.0000000,
					 0.0000000,  0.0000000,  1.0000000);
	cs.set_gamma(1.0);
	color_spaces.push_back(cs);
	cs.hidden = false;

/*
	cs.set_key("XYZ");
	cs.set_name("XYZ");
	cs.illuminant = "E";
	cs.set_matrix(	 1.0000000,  0.0000000,  0.0000000,
					 0.0000000,  1.0000000,  0.0000000,
					 0.0000000,  0.0000000,  1.0000000);
	cs.set_gamma(1.0);
	color_spaces.push_back(cs);

	cs.set_key("HPE");
	cs.set_name("HPE");
	cs.illuminant = "E";
	cs.set_matrix(	 0.3897100,  0.6889800, -0.0786800,
					-0.2298100,  1.1834000,  0.0464100,
					 0.0000000,  0.0000000,  1.0000000);
	cs.set_gamma(1.00);
	color_spaces.push_back(cs);

	cs.set_key("CAT02");
	cs.set_name("CAT02");
	cs.illuminant = "E";
	cs.set_matrix(	 0.7328000,  0.4296000, -0.1624000,
					-0.7036000,  1.6975000,  0.0061000,
					 0.0000000,  0.0000000,  1.0000000);
	cs.set_gamma(1.00);
	color_spaces.push_back(cs);
*/

	cs.set_key("HDTV");
	cs.set_name("HDTV (HD-CIF)");
	cs.illuminant = "D65";
	cs.set_matrix(	 3.2404542, -1.5371385, -0.4985314,
					-0.9692660,  1.8760108,  0.0415560,
					 0.0556434, -0.2040259,  1.0572252);
	cs.set_gamma(0.0992968, 0.45, 0.018054, 4.5);	// simple == 0.51
	color_spaces.push_back(cs);

	cs.set_desc("sRGB");
	cs.illuminant = "D65";
	cs.set_matrix(	 3.2404542, -1.5371385, -0.4985314,
					-0.9692660,  1.8760108,  0.0415560,
					 0.0556434, -0.2040259,  1.0572252);
	cs.set_gamma(0.055, 0.42, 0.0031308, 12.92);	// simple == 0.45
	color_spaces.push_back(cs);

	cs.set_key("Adobe");
	cs.set_name("Adobe (1998)");
	cs.illuminant = "D65";
	cs.set_matrix(	 2.04159, -0.56501, -0.34473,
					-0.96924,  1.87597,  0.04156,
					 0.01344, -0.11836,  1.01517);
	cs.set_gamma(double(256) / double(563));		// simple == 0.45
	color_spaces.push_back(cs);

	cs.set_key("NTSC");
	cs.set_name("NTSC (1953)");
	cs.illuminant = "C";
	cs.set_matrix(	 1.9100, -0.5325, -0.2882,
					-0.9847,  1.9992, -0.0283,
					 0.0583, -0.1184,  0.8976);
	cs.set_gamma(0.0992968, 0.45, 0.018054, 4.5);	// simple == 0.51
	color_spaces.push_back(cs);

	cs.set_key("PAL_SECAM");
	cs.set_name("PAL / SECAM");
	cs.illuminant = "D65";
	cs.set_matrix(	 3.0629, -1.3932, -0.4758,
					-0.9693,  1.8760,  0.0416,
					 0.0679, -0.2289,  1.0694);
	cs.set_gamma(0.0992968, 0.45, 0.018054, 4.5);	// simple == 0.51
	color_spaces.push_back(cs);

#ifdef CS_RARE_ENABLE
	cs.set_desc("ProPhoto");
	cs.illuminant = "D50";
	cs.set_matrix(	 1.3460, -0.2556, -0.0511,
					-0.5446,  1.5082,  0.0205,
					 0.0000,  0.0000,  1.2123);
	cs.set_gamma(0.0, 1.0 / 1.8, 0.001953, 16);		// simple == 0.55(5)
	color_spaces.push_back(cs);
#endif

#ifdef CS_RARE_ENABLE
	cs.set_key("Wide_Gamut");
	cs.set_name("Wide Gamut");
	cs.illuminant = "D50";
	cs.set_matrix(	 1.4625, -0.1845, -0.2734,
					-0.5228,  1.4479,  0.0681,
					 0.0346, -0.0958,  1.2875);
	cs.set_gamma(0.45);								// simple == 0.45
	color_spaces.push_back(cs);
#endif

#ifdef CS_RARE_ENABLE
	cs.set_desc("CIE");
	cs.illuminant = "E";
	cs.set_matrix(	 2.3707, -0.9001, -0.4706,
					-0.5139,  1.4253,  0.0886,
					 0.0053, -0.0147,  1.0094);
	cs.set_gamma(0.45);								// simple == 0.45
	color_spaces.push_back(cs);
#endif

	cs.set_desc("Apple");
	cs.illuminant = "D65";
	cs.set_matrix(	 2.9516, -1.2894, -0.4738,
					-1.0851,  1.9909,  0.0372,
					 0.0855, -0.2695,  1.0913);
	cs.set_gamma(0.56);								// simple == 0.56
	color_spaces.push_back(cs);

#ifdef CS_RARE_ENABLE
	cs.set_key("Color_Match");
	cs.set_name("Color Match");
	cs.illuminant = "D50";
	cs.set_matrix(	 2.6423, -1.2234, -0.3930,
					-1.1120,  2.0590,  0.0160,
					 0.0822, -0.2807,  1.4560);
	cs.set_gamma(0.56);								// simple == 0.56
	color_spaces.push_back(cs);
#endif

#ifdef CS_RARE_ENABLE
	cs.set_key("SMPTE_C");
	cs.set_name("SMPTE-C");
	cs.illuminant = "D65";
	cs.set_matrix(	 3.5054, -1.7395, -0.5440,
					-1.0691,  1.9778,  0.0352,
					 0.0563, -0.1970,  1.0502);
	cs.set_gamma(0.0992968, 0.45, 0.018054, 4.5);	// simple == 0.51
	color_spaces.push_back(cs);
#endif

#ifdef CS_RARE_ENABLE
	cs.set_key("SMPTE_240M");
	cs.set_name("SMPTE-240M");
	cs.illuminant = "D65";
	cs.set_matrix(	 3.5054, -1.7395, -0.5440,
					-1.0691,  1.9778,  0.0352,
					 0.0563, -0.1970,  1.0502);
	cs.set_gamma(0.1115, 0.45, 0.0228, 4.0);	// simple == 0.52
//	cs.set_gamma(0.112, 0.45, 0.023, 4.0);	// simple == 0.52
	color_spaces.push_back(cs);
#endif

/*
	for(list<CMS_Matrix::color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it) {
		if((*it).key == DEFAULT_OUTPUT_COLOR_SPACE) {
			default_output_color_space = (*it);
			break;
		}
	}
*/
}

//------------------------------------------------------------------------------
class CMS_Matrix::gamma_function_t : public TableFunction {
public:
	gamma_function_t(float _value, float _offset, float _transition, float _slope, bool _simple, bool _inverse) : value(_value), offset(_offset), transition(_transition), slope(_slope), simple(_simple), inverse(_inverse) {
		_init(0.0, 1.0, 0x010000);
	}
	float value;
	float offset;
	float transition;
	float slope;
	bool simple;
	bool inverse;

protected:
	float function(float x);
};

float CMS_Matrix::gamma_function_t::function(float x) {
	float r = 0.0;
	if(x <= 0.0)
		return r;
	if(inverse == false) {
		if(simple) {
			r = powf(x, value);
		} else {
			r = (x < transition) ? (x * slope) : ((1.0 + offset) * powf(x, value) - offset);
		}
	} else {
		if(simple) {
			r = powf(x, 1.0 / value);
		} else {
			r = (x < transition) ? (x / slope) : (powf(((x + offset) / (1.0 + offset)), 1.0 / value));
		}
	}
	return r;
}

//------------------------------------------------------------------------------
// TODO: clean up those lists?
std::list<CMS_Matrix::gamma_function_t *> CMS_Matrix::gamma_list;
std::mutex CMS_Matrix::gamma_list_lock;

std::list<CMS_Matrix::gamma_function_t *> CMS_Matrix::inverse_gamma_list;
std::mutex CMS_Matrix::inverse_gamma_list_lock;

TableFunction *CMS_Matrix::get_gamma(string cs_name) {
	return _get_gamma(cs_name, &gamma_list, &gamma_list_lock, false);
}

TableFunction *CMS_Matrix::get_inverse_gamma(string cs_name) {
	return _get_gamma(cs_name, &inverse_gamma_list, &inverse_gamma_list_lock, true);
}

TableFunction *CMS_Matrix::_get_gamma(string cs_name, std::list<gamma_function_t *> *ptr_list, std::mutex *ptr_list_lock, bool inverse_gamma) {
	// find associated color space, by default use "default"
	color_space_t color_space;
	for(list<CMS_Matrix::color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it) {
		if((*it).key == cs_name) {
			color_space = (*it);
			break;
		}
	}
	// try to found gamma
	CMS_Matrix::gamma_function_t *gamma_function = nullptr;
	for(list<gamma_function_t *>::iterator it = ptr_list->begin(); it != ptr_list->end(); ++it) {
		if(color_space.gamma_value == ((*it)->value)) {
			if(!color_space.gamma_simple) {
				if((*it)->offset != color_space.gamma_offset || (*it)->transition != color_space.gamma_transition || (*it)->slope != color_space.gamma_slope)
					continue;
			}
			gamma_function = *it;
			break;
		}
	}
	if(gamma_function == nullptr) {
		ptr_list_lock->lock();
		bool is_exist = false;
		for(list<gamma_function_t *>::iterator it = ptr_list->begin(); it != ptr_list->end(); ++it) {
			if(color_space.gamma_value == ((*it)->value)) {
				if(!color_space.gamma_simple) {
					if((*it)->offset != color_space.gamma_offset || (*it)->transition != color_space.gamma_transition || (*it)->slope != color_space.gamma_slope)
						continue;
				}
				gamma_function = *it;
				is_exist = true;
				break;
			}
		}
		if(is_exist == false) {
			gamma_function = new gamma_function_t(color_space.gamma_value, color_space.gamma_offset, color_space.gamma_transition, color_space.gamma_slope, color_space.gamma_simple, inverse_gamma);
			ptr_list->push_back(gamma_function);
		}
		ptr_list_lock->unlock();
	}
	return gamma_function;
}

//------------------------------------------------------------------------------
CMS_Matrix *CMS_Matrix::_this = nullptr;

CMS_Matrix::CMS_Matrix(void) {
	if(_this == nullptr) {
		load_color_spaces();
	}
}

bool CMS_Matrix::get_matrix_XYZ_to_CS(string cs_name, float *matrix) {
	for(list<color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it) {
		if((*it).key == cs_name) {
			for(int k = 0; k < 9; ++k)
				matrix[k] = (*it).matrix[k];
			return true;
		}
	}
	return false;
}

bool CMS_Matrix::get_matrix_CS_to_XYZ(string cs_name, float *matrix) {
	float m[9];
	if(get_matrix_XYZ_to_CS(cs_name, m)) {
		m3_invert(matrix, m);
		return true;
	}
	return false;
}

string CMS_Matrix::get_illuminant_name(string cs_name) {
	for(list<color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it) {
		if((*it).key == cs_name) {
			return (*it).illuminant;
		}
	}
	return "";
}

void CMS_Matrix::get_illuminant_XYZ(string name, float *XYZ) {
	if(color_space_t::illuminants.find(name) == color_space_t::illuminants.end()) {
		XYZ[0] = 0.0;
		XYZ[1] = 0.0;
		XYZ[2] = 0.0;
	} else {
		XYZ[0] = color_space_t::illuminants[name].first;
		XYZ[1] = 1.0;
		XYZ[2] = color_space_t::illuminants[name].second;
	}
}

string CMS_Matrix::get_cs_string_name(string cs_name) {
	for(list<color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it)
		if((*it).key == cs_name)
			return (*it).name;
	return "";
}

list<string> CMS_Matrix::get_cs_names(void) {
	list<string> rez;
	for(list<color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it) {
		if(!(*it).hidden)
			rez.push_back((*it).name);
	}
	return rez;
}

string CMS_Matrix::get_cs_name_from_string_name(string cs_string_name) {
	for(list<color_space_t>::iterator it = color_spaces.begin(); it != color_spaces.end(); ++it)
		if((*it).name == cs_string_name)
			return (*it).key;
	return "";
}
//------------------------------------------------------------------------------

CS_White::CS_White(void) {
	name = "E";
	XYZ[0] = 1.0;
	XYZ[1] = 1.0;
	XYZ[2] = 1.0;
}

CS_White::CS_White(std::string illuminant_name) {
	name = illuminant_name;
	CMS_Matrix::get_illuminant_XYZ(XYZ, name);
}

CS_White::CS_White(float x, float y) {
	name = "";
}

CS_White::CS_White(const float *_XYZ) {
	name = "";
	XYZ[0] = _XYZ[0];
	XYZ[1] = _XYZ[1];
	XYZ[2] = _XYZ[2];
}

std::string CS_White::get_name(void) {
	return name;
}

void CS_White::get_XYZ(float *_XYZ) {
	_XYZ[0] = XYZ[0];
	_XYZ[1] = XYZ[1];
	_XYZ[2] = XYZ[2];
}

void CS_White::get_XYZ(double *_XYZ) {
	_XYZ[0] = XYZ[0];
	_XYZ[1] = XYZ[1];
	_XYZ[2] = XYZ[2];
}

//------------------------------------------------------------------------------
