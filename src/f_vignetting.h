#ifndef __H_F_VIGNETTING__
#define __H_F_VIGNETTING__
/*
 * f_vignetting.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <string>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Vignetting : public Filter {
	Q_OBJECT

public:
	F_Vignetting(int id);
	~F_Vignetting();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps_base, FS_Base *new_fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

protected:
	class PS_Vignetting *ps;
	class PS_Vignetting *_ps;
	static class FP_Vignetting *fp;

	// controls
	QWidget *widget;
	class GuiSlider *slider_x2;
	class GuiSlider *slider_x3;
	QCheckBox *checkbox_enable;
	QCheckBox *checkbox_x2;
	QCheckBox *checkbox_x3;
	void reconnect(bool to_connect);

public slots:
	void slot_changed_x2(double value);
	void slot_changed_x3(double value);
	void slot_checkbox_enable(int state);
	void slot_checkbox_x2(int state);
	void slot_checkbox_x3(int state);

protected:
	void changed_channel(double value, double &value_ch, bool &enabled_ch, QCheckBox *checkbox_ch);
	void checkbox_channel(int state, bool &enabled_ch, const double &value_ch);
};

//------------------------------------------------------------------------------
#endif //__H_F_VIGNETTING__
