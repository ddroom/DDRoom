#ifndef __H_CM__
#define __H_CM__
/*
 * cm.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>
#include <list>
#include "cms_matrix.h"
//#include "sgt.h"

//------------------------------------------------------------------------------
namespace cm {
// TODO: remove that
void sRGB_to_Jsh(float *Jsh, const float *sRGB);
void Jsh_to_sRGB(float *sRGB, const float *Jsh);
}

//------------------------------------------------------------------------------
class CM_Convert {
public:
	virtual ~CM_Convert(void) {};
	virtual void convert(float *to, const float *from) {};
	virtual float get_C_from_Jsh(const float *Jsh) {return Jsh[1];}
	virtual float get_s_from_JCh(const float *JCh) {return JCh[1];}
};

class CM {
public:
	enum cm_type_en {
		cm_type_CIECAM02,
		cm_type_CIELab,
		cm_type_none,
	};
	static std::string get_type_name(cm_type_en type);
	static cm_type_en get_type(std::string type_name);
	static std::list<cm_type_en> get_types_list(void);

	static void initialize(void);

	static CM *new_CM(CM::cm_type_en, CS_White white_in, CS_White white_out);
	virtual ~CM(void){};

	virtual class CM_Convert *get_convert_XYZ_to_Jsh(void){return nullptr;}
	virtual class CM_Convert *get_convert_Jsh_to_XYZ(void){return nullptr;}

protected:
	CM(void){};
};

//------------------------------------------------------------------------------
class CIELab : public CM {
public:
	CIELab(CS_White white_in, CS_White white_out);
	virtual ~CIELab(void);
	class CM_Convert *get_convert_XYZ_to_Jsh(void);
	class CM_Convert *get_convert_Jsh_to_XYZ(void);

	static void initialize(void);

protected:
	static class TF_CIELab *tf_CIELab;
	float CAT_in[9];
	float CAT_out[9];
	class CIELab_XYZ_to_Jsh *cm_xyz_to_jsh;
	class CIELab_Jsh_to_XYZ *cm_jsh_to_xyz;
};

//------------------------------------------------------------------------------
class CIECAM02 : public CM {
public:
	CIECAM02(CS_White white_in, CS_White white_out);
	virtual ~CIECAM02(void);
	class CM_Convert *get_convert_XYZ_to_Jsh(void);
	class CM_Convert *get_convert_Jsh_to_XYZ(void);

	static void initialize(void);

protected:
	static class CIECAM02_priv *priv;

	// CAT02 / white point
	class CAT02_t *cat02_in;
	class CAT02_t *cat02_out;
	class CM_Convert *cm_convert_XYZ_to_Jsh;
	class CM_Convert *cm_convert_Jsh_to_XYZ;
};

//------------------------------------------------------------------------------
class CS_to_CM {
public:
	CS_to_CM(CM::cm_type_en _cm_type, std::string _cs_name);
	virtual ~CS_to_CM();
	void convert(float *Jsh, const float *RGB);
	float get_C_from_Jsh(const float *Jsh);
	float get_s_from_JCh(const float *JCh);

protected:
	CM::cm_type_en cm_type;
	std::string cs_name;
	class TableFunction *inverse_gamma;
	float matrix_CS_to_XYZ[9];
	CM *cm;
	CM_Convert *cm_convert;
};

class CM_to_CS {
public:
	CM_to_CS(CM::cm_type_en _cm_type, std::string _cs_name);
	virtual ~CM_to_CS();
	void convert(float *RGB, const float *Jsh, bool clip = true);
	float get_C_from_Jsh(const float *Jsh);
	float get_s_from_JCh(const float *JCh);

protected:
	CM::cm_type_en cm_type;
	std::string cs_name;
	class TableFunction *gamma;
	float matrix_XYZ_to_CS[9];
	CM *cm;
	CM_Convert *cm_convert;
};

//------------------------------------------------------------------------------
#endif // __H_CM__
