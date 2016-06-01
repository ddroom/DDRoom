#ifndef __H_F_DEMOSAIC__
#define __H_F_DEMOSAIC__
/*
 * f_demosaic.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string.h>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Demosaic : public Filter {
	Q_OBJECT

public:
	F_Demosaic(int id);
	~F_Demosaic();

	Filter::type_t type(void);

	QWidget *controls(QWidget *parent = 0);
	class PS_Base *newPS(void);
	void set_PS_and_FS(class PS_Base *new_ps, class FS_Base *new_fs, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
public slots:
	void slot_checkbox_hot_pixels(int state);
	void slot_checkbox_luma(int state);
	void slot_checkbox_chroma(int state);
	void slot_changed_luma(double value);
	void slot_changed_chroma(double value);

	void slot_checkbox_CA(int state);
	void slot_checkbox_RC(int state);
	void slot_checkbox_BY(int state);
	void slot_changed_RC(double value);
	void slot_changed_BY(double value);

	void slot_xtrans_passes(bool);

protected:
	class PS_Demosaic *ps;
	class PS_Demosaic *_ps;
	static class FP_Demosaic *fp;

	// controls
	QWidget *widget;
	// Bayer UI
	QWidget *widget_bayer;
	QCheckBox *checkbox_hot_pixels;
	QCheckBox *checkbox_luma;
	class GuiSlider *slider_luma;
	QCheckBox *checkbox_chroma;
	class GuiSlider *slider_chroma;
	QCheckBox *checkbox_CA;
	QCheckBox *checkbox_RC;
	QCheckBox *checkbox_BY;
	class GuiSlider *slider_RC;
	class GuiSlider *slider_BY;
	// X-Trans UI
	QWidget *widget_xtrans;
	QRadioButton *radio_xtrans_passes_1;
	QRadioButton *radio_xtrans_passes_3;

	void reconnect(bool to_connect);
	void slot_checkbox_process(int state, bool &value);
};

//------------------------------------------------------------------------------
#endif // __H_F_DEMOSAIC__
