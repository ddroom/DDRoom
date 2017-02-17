#ifndef __H_F_CRGB_TO_CM__
#define __H_F_CRGB_TO_CM__
/*
 * f_crgb_to_cm.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_cRGB_to_CM : public Filter {
	Q_OBJECT

public:
	F_cRGB_to_CM(int id);
	~F_cRGB_to_CM();

	Filter::type_t type(void);

	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

	void ui_set_compress_saturation_factor(double factor);

public slots:
	void slot_combo_output_color_space(int index);
	void slot_combo_color_model(int index);
	void slot_groupbox_gamut(bool enabled);
	void slot_compress_saturation_manual(bool state);
	void slot_slider_compress_saturation_factor(double value);
//	void slot_checkbox_desaturate_overexp(int state);
	void slot_slider_compress_strength(double value);
	void slot_slider_desaturation_strength(double value);

protected:
signals:
	void signal_ui_set_compress_saturation_factor(double factor);

protected slots:
	void slot_ui_set_compress_saturation_factor(double factor);

protected:
	class PS_cRGB_to_CM *ps;
	class PS_cRGB_to_CM *_ps;
	static class FP_cRGB_to_CM *fp;

	// controls
	QWidget *widget;
	QComboBox *combo_output_color_space;
	QComboBox *combo_color_model;
	// gamut compression
	QGroupBox *groupbox_gamut;
	QRadioButton *radio_compress_saturation_manual;
	QRadioButton *radio_compress_saturation_auto;
	QLabel *label_compress_saturation_auto;
	double label_compress_saturation_auto_value;
	class GuiSlider *slider_compress_saturation_factor;
//	QCheckBox *checkbox_desaturate_overexp;
	class GuiSlider *slider_compress_strength;
	class GuiSlider *slider_desaturation_strength;
	
	void reconnect(bool to_connect);

};
//------------------------------------------------------------------------------

#endif //__H_F_CRGB_TO_CM__
