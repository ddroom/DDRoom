#ifndef __H_F_CMS_MATRIX__
#define __H_F_CMS_MATRIX__
/*
 * cms_matrix.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


//#include <stdint.h>
#include <string>
#include <list>

#include <QMutex>

//------------------------------------------------------------------------------
class CS_White {
public:
	CS_White(void);
	CS_White(std::string illuminant_name);
	CS_White(float x, float y);
	CS_White(const float *XYZ);

	std::string get_name(void);
	void get_XYZ(float *XYZ);
	void get_XYZ(double *XYZ);

protected:
	std::string name;
	float XYZ[3];
};

class CMS_Matrix {
public:
	virtual ~CMS_Matrix();
	static CMS_Matrix *instance(void) {
		if(_this == NULL)
			_this = new CMS_Matrix();
		return _this;
	}

	// return float [9] matrix for conversion XYZ color to CS (color space)
	bool get_matrix_XYZ_to_CS(std::string cs_name, float *matrix);
	bool get_matrix_CS_to_XYZ(std::string cs_name, float *matrix);
	// return illuminant name
	std::string get_illuminant_name(std::string cs_name);
	void get_illuminant_XYZ(std::string illuminant_name, float *XYZ);
	// return CAT matrix for illuminant conversion
	void get_CAT(float *CAT, CS_White white_from, CS_White white_to);

	class TableFunction *get_gamma(std::string cs_name);
	class TableFunction *get_inverse_gamma(std::string cs_name);

	// access to the CS "readable" names
	std::list<std::string> get_cs_names(void);
	std::string get_cs_string_name(std::string cs_name);
	std::string get_cs_name_from_string_name(std::string cs_string_name);
	static bool get_illuminant_XYZ(float *XYZ, std::string illuminant_name);

protected:
	static CMS_Matrix *_this;
	CMS_Matrix(void);
	// FP_ interaction
	class color_space_t;
	static std::list<color_space_t> color_spaces;

	class gamma_function_t;
	// matrix + gamma output color spaces
	static void load_color_spaces(void);
	static std::list<class gamma_function_t *> gamma_list;
	static QMutex gamma_list_lock;
	static std::list<class gamma_function_t *> inverse_gamma_list;
	static QMutex inverse_gamma_list_lock;

	class TableFunction *_get_gamma(std::string cs_name, std::list<gamma_function_t *> *ptr_list, QMutex *ptr_list_lock, bool inverse);
};

#endif //__H_F_CMS_MATRIX__
