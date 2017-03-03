#ifndef __H_F_CM_TO_CS__
#define __H_F_CM_TO_CS__
/*
 * f_cm_to_cs.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_CM_to_CS : public Filter {
	Q_OBJECT

public:
	F_CM_to_CS(int id);
	~F_CM_to_CS();

	Filter::type_t type(void);

	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

protected:
	static class FP_CM_to_CS *fp;
};
//------------------------------------------------------------------------------

#endif //__H_F_CM_TO_CS__
