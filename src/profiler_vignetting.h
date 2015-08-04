#ifndef __H_PROFILER_VIGNETTING__
#define __H_PROFILER_VIGNETTING__
/*
 * profiler_vignetting.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


//#include <QtWidgets>
#include <QMap>

#include <string>

//------------------------------------------------------------------------------

class Profiler_Vignetting {
public:
	Profiler_Vignetting(void);
	void process(std::string folder);

protected:
	void process_photos(QMap<float, std::string> &map_fn_to_fl);
};

//------------------------------------------------------------------------------
#endif //__H_PROFILER_VIGNETTING__
