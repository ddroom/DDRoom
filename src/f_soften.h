#ifndef __H_F_SOFTEN__
#define __H_F_SOFTEN__
/*
 * f_soften.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Soften : public Filter {
	Q_OBJECT

public:
	F_Soften(int id);
	~F_Soften();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps_base, FS_Base *new_fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

protected:
	class PS_Soften *ps;
	class PS_Soften *_ps;
	static class FP_Soften *fp;

	// controls
	QWidget *widget;
	class GuiSlider *slider_strength;
	class GuiSlider *slider_radius;
	QCheckBox *checkbox_enable;
	void reconnect(bool to_connect);

public slots:
	void slot_changed_strength(double value);
	void slot_changed_radius(double value);
	void slot_checkbox_enable(int state);

protected:
	void changed_slider(double value_new, double &value);
};

#endif //__H_F_SOFTEN__
