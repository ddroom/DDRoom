#ifndef __H_F_CM_RAINBOW__
#define __H_F_CM_RAINBOW__
/*
 * f_cm_rainbow.h
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
class F_CM_Rainbow : public Filter {
	Q_OBJECT

public:
	F_CM_Rainbow(int id);
	~F_CM_Rainbow();

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
	void slot_color_checkbox(int state);
	void slot_color_slider(double value);
	void slot_reset(bool clicked);

signals:

protected:
	class PS_CM_Rainbow *ps;
	class PS_CM_Rainbow *_ps;
	static class FP_CM_Rainbow *fp;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_enable;
	QCheckBox *color_checkbox[12];
	QToolButton *button_reset;
	class GuiSlider *color_slider[12];
	QObject *color_checkbox_obj[12];
	QObject *color_slider_obj[12];
	void reconnect(bool to_connect);
};

//------------------------------------------------------------------------------
#endif //__H_F_CM_RAINBOW__
