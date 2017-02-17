#ifndef __H_F_CM_COLORS__
#define __H_F_CM_COLORS__
/*
 * f_cm_colors.h
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
class F_CM_Colors : public Filter {
	Q_OBJECT

public:
	F_CM_Colors(int id);
	~F_CM_Colors();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
	void set_CM(std::string cm_name);

public slots:
	void slot_checkbox_saturation(int state);
	void slot_slider_saturation(double value);
	void slot_checkbox_js_curve(int state);
	void slot_checkbox_gamut_use(int state);
	void slot_js_curve_update(const QVector<QVector<QPointF> > &_curve, const QVector<float> &_levels);

protected:
	class PS_CM_Colors *ps;
	class PS_CM_Colors *_ps;
	static class FP_CM_Colors *fp;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_saturation;
	class GuiSlider *slider_saturation;
	QCheckBox *checkbox_js_curve;
	QCheckBox *checkbox_gamut_use;
	void reconnect(bool to_connect);

	GUI_Curve *js_curve;
};

//------------------------------------------------------------------------------
#endif //__H_F_CM_COLORS__
