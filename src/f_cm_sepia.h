#ifndef __H_F_CM_SEPIA__
#define __H_F_CM_SEPIA__
/*
 * f_cm_sepia.h
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
#include "gui_curve.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_CM_Sepia : public Filter {
	Q_OBJECT

public:
	F_CM_Sepia(int id);
	~F_CM_Sepia();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
	void set_CM(std::string cm_name);

public slots:
	void slot_checkbox_enable(int state);
	void slot_slider_hue(double value);
	void slot_slider_strength(double value);
	void slot_slider_saturation(double value);

signals:

protected:
	class PS_CM_Sepia *ps;
	class PS_CM_Sepia *_ps;
	static class FP_CM_Sepia *fp;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_enable;
	class GuiSlider *slider_hue;
	class GuiSlider *slider_strength;
	class GuiSlider *slider_saturation;
	void slot_slider(double value, double &ps_value);
	void reconnect(bool to_connect);
};

//------------------------------------------------------------------------------
#endif //__H_F_CM_SEPIA__
