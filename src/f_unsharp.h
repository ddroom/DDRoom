#ifndef __H_F_UNSHARP__
#define __H_F_UNSHARP__
/*
 * f_unsharp.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Unsharp : public Filter {
	Q_OBJECT

public:
	F_Unsharp(int id);
	~F_Unsharp();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

protected slots:
	void slot_checkbox_enable(int state);
	void slot_changed_amount(double value);
	void slot_changed_radius(double value);
	void slot_changed_threshold(double value);

	void slot_checkbox_scaled(int state);
	void slot_tab_scaled(int index);
	void slot_changed_s_amount(double value);
	void slot_changed_s_radius(double value);
	void slot_changed_s_threshold(double value);

protected:
	class PS_Unsharp *ps;
	class PS_Unsharp *_ps;
	static class FP_Unsharp *fp;

	QWidget *widget;
	QCheckBox *checkbox_enable;
	class GuiSlider *slider_amount;
	class GuiSlider *slider_radius;
	class GuiSlider *slider_threshold;
	QWidget *widget_unscaled;

	QTabWidget *tab_scaled;
	QCheckBox *checkbox_scaled;
	class GuiSlider *slider_s_amount[2];
	class GuiSlider *slider_s_radius[2];
	class GuiSlider *slider_s_threshold[2];
	int scaled_index;

	void reconnect(bool to_connect);
	void changed_slider(double value, double &ps_value, bool is_255);

	//== local contrast
protected slots:
	void slot_checkbox_lc_enable(int state);
	void slot_changed_lc_amount(double value);
	void slot_changed_lc_radius(double value);
	void slot_checkbox_lc_brighten(int state);
	void slot_checkbox_lc_darken(int state);

protected:
	QWidget *gui_local_contrast(void);
	void changed_lc_slider(double value, double &ps_value);
	QCheckBox *checkbox_lc_enable;
	QCheckBox *checkbox_lc_brighten;
	QCheckBox *checkbox_lc_darken;
	class GuiSlider *slider_lc_amount;
	class GuiSlider *slider_lc_radius;
	void slot_checkbox_lc_do(bool &ps_value, int state);

};

//------------------------------------------------------------------------------
#endif //__H_F_UNSHARP__
