#ifndef __H_F_PROCESS__
#define __H_F_PROCESS__
/*
 * f_process.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string.h>

#include <QtWidgets>

#include "filter.h"

//------------------------------------------------------------------------------
class F_Process : public Filter_Control {
	Q_OBJECT

public:
	F_Process(int id);
	~F_Process();

	void get_mutators(class DataSet *mutators, class DataSet *ps_dataset = nullptr);
	Filter::type_t type(void);

	QWidget *controls(QWidget *parent = 0);
	class PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
public slots:
	void slot_checkbox_raw_colors(int state);
	void slot_checkbox_skip_demosaic(int state);

protected:
	class PS_Process *ps;
	class PS_Process *_ps;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_raw_colors;
	QCheckBox *checkbox_skip_demosaic;
	void reconnect(bool to_connect);

	void slot_checkbox_process(int state, bool &value);
};

//------------------------------------------------------------------------------
#endif // __H_F_PROCESS__
