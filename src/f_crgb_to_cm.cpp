/*
 * f_crgb_to_cm.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTES:
	- used mutators:
		"CM" -> string: "CIECAM02" | "CIELab"
		"CM_compress_saturation" -> bool
		"CM_compress_saturation_factor" -> double
		"CM_compress_strength" -> double
		"CM_desaturation_strength" -> double
 * TODO:
 */

#include <iostream>
#include <math.h>

#include "f_crgb_to_cm.h"
#include "filter_cp.h"
#include "ddr_math.h"
#include "cm.h"
#include "cms_matrix.h"
#include "gui_slider.h"

using namespace std;

//#define DEFAULT_OUTPUT_COLOR_SPACE	"sRGB"
#define DEFAULT_OUTPUT_COLOR_SPACE	"HDTV"
//#define DEFAULT_COLOR_MODEL	CM::cm_type_CIELab
#define DEFAULT_COLOR_MODEL	CM::cm_type_CIECAM02

//#define _USE_ISNAN
#undef _USE_ISNAN

#define DISABLE_CM

//------------------------------------------------------------------------------
class PS_cRGB_to_CM : public PS_Base {
public:
	PS_cRGB_to_CM(void);
	virtual ~PS_cRGB_to_CM();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	string output_color_space;
	CM::cm_type_en cm_type;
	bool compress_saturation;
	bool compress_saturation_manual;
	double compress_saturation_factor;
//	bool desaturate_overexp;
	double compress_strength;
	double desaturation_strength;
};

//------------------------------------------------------------------------------
class FP_cRGB_to_CM : public FilterProcess_CP {
public:
	FP_cRGB_to_CM(void);
	bool is_enabled(const PS_Base *ps_base);
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, void *data);
	void filter_post(fp_cp_args_t *args);
	
protected:
	class task_t;
};

//------------------------------------------------------------------------------
PS_cRGB_to_CM::PS_cRGB_to_CM(void) {
	reset();
}

PS_cRGB_to_CM::~PS_cRGB_to_CM() {
}

PS_Base *F_cRGB_to_CM::newPS(void) {
	return new PS_cRGB_to_CM();
}

PS_Base *PS_cRGB_to_CM::copy(void) {
	PS_cRGB_to_CM *ps = new PS_cRGB_to_CM;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_cRGB_to_CM::reset(void) {
	cm_type = DEFAULT_COLOR_MODEL;
	output_color_space = DEFAULT_OUTPUT_COLOR_SPACE;
	compress_saturation = true;
	compress_saturation_manual = false;
	compress_saturation_factor = 1.0;
//	desaturate_overexp = false;
	compress_strength = 1.0;
	desaturation_strength = 1.0;
}

bool PS_cRGB_to_CM::load(DataSet *dataset) {
	reset();
	dataset->get("output_color_space", output_color_space);
	string cm_name = CM::get_type_name(cm_type);
	dataset->get("cm_name", cm_name);
	cm_type = CM::get_type(cm_name);
	dataset->get("compress_saturation", compress_saturation);
	dataset->get("compress_saturation_manual", compress_saturation_manual);
	dataset->get("compress_saturation_factor", compress_saturation_factor);
//	dataset->get("desaturate_overexp", desaturate_overexp);
	dataset->get("compress_strength", compress_strength);
	dataset->get("desaturation_strength", desaturation_strength);
	return true;
}

bool PS_cRGB_to_CM::save(DataSet *dataset) {
	dataset->set("output_color_space", output_color_space);
	string cm_name = CM::get_type_name(cm_type);
	dataset->set("cm_name", cm_name);
	dataset->set("compress_saturation", compress_saturation);
	dataset->set("compress_saturation_manual", compress_saturation_manual);
	dataset->set("compress_saturation_factor", compress_saturation_factor);
//	dataset->set("desaturate_overexp", desaturate_overexp);
	dataset->set("compress_strength", compress_strength);
	dataset->set("desaturation_strength", desaturation_strength);
	return true;
}

//------------------------------------------------------------------------------
FP_cRGB_to_CM *F_cRGB_to_CM::fp = nullptr;

F_cRGB_to_CM::F_cRGB_to_CM(int id) : Filter() {
	filter_id = id;
	_id = "F_cRGB_to_CM";
	_name = tr("Color space and gamut");
//	_name = tr("Color model and output color space");
	if(fp == nullptr)
		fp = new FP_cRGB_to_CM();
	_ps = (PS_cRGB_to_CM *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_cRGB_to_CM::~F_cRGB_to_CM() {
}

FilterProcess *F_cRGB_to_CM::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FS_cRGB_to_CM : public FS_Base {
public:
	double compress_saturation_auto_value;
	FS_cRGB_to_CM() {
		compress_saturation_auto_value = 0.0;
	}
};

FS_Base *F_cRGB_to_CM::newFS(void) {
    return new FS_cRGB_to_CM ;
}

void F_cRGB_to_CM::saveFS(FS_Base *fs_base) {
    if(fs_base == nullptr)
        return;
    FS_cRGB_to_CM *fs = (FS_cRGB_to_CM*)fs_base;
	fs->compress_saturation_auto_value = label_compress_saturation_auto_value;
}

void F_cRGB_to_CM::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != nullptr) {
		ps = (PS_cRGB_to_CM *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);

	if(fs_base != nullptr) {
		FS_cRGB_to_CM *fs = (FS_cRGB_to_CM*)fs_base;
		ui_set_compress_saturation_factor(fs->compress_saturation_auto_value);
	} else {
		ui_set_compress_saturation_factor(0.0);
	}

	// output color space
	int index = combo_output_color_space->findText(QString(DEFAULT_OUTPUT_COLOR_SPACE));
	string name = CMS_Matrix::instance()->get_cs_string_name(ps->output_color_space);
	if(name != "") {
		int current_index = combo_output_color_space->findText(QString(name.c_str()));
		if(current_index != -1)
			index = current_index;
	}
	combo_output_color_space->setCurrentIndex(index);

#ifndef DISABLE_CM
	combo_color_model->setCurrentIndex(ps->cm_type);
#endif

	groupbox_gamut->setChecked(ps->compress_saturation);
	if(ps->compress_saturation_manual)
		radio_compress_saturation_manual->setChecked(true);
	else
		radio_compress_saturation_auto->setChecked(true);
	slider_compress_saturation_factor->setValue(ps->compress_saturation_factor);

//	checkbox_desaturate_overexp->setChecked(ps->desaturate_overexp);
	slider_compress_strength->setValue(ps->compress_strength);
	slider_desaturation_strength->setValue(ps->desaturation_strength);

	reconnect(true);
}

QWidget *F_cRGB_to_CM::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	widget = q;
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);

	int row = 0;
#ifndef DISABLE_CM
	// TODO: add 'none' OCS
	QLabel *label_cm = new QLabel(tr("Working color model: "));
	l->addWidget(label_cm, row, 0);
	combo_color_model = new QComboBox();
	// TODO: add and process correctly 'none'
	list<CM::cm_type_en> cm_types_list = CM::get_types_list();
	for(list<CM::cm_type_en>::iterator it = cm_types_list.begin(); it != cm_types_list.end(); ++it)
		combo_color_model->addItem(QString(CM::get_type_name(*it).c_str()), *it);
	l->addWidget(combo_color_model, row, 1);
	++row;
#endif
	QLabel *label_ocs = new QLabel(tr("Output color space:"));
	l->addWidget(label_ocs, row, 0);
	combo_output_color_space = new QComboBox();
	int index = 0;
	list<string> cs_names_list = CMS_Matrix::instance()->get_cs_names();
	for(list<string>::iterator it = cs_names_list.begin(); it != cs_names_list.end(); ++it) {
		combo_output_color_space->addItem(QString((*it).c_str()), index);
		++index;
	}
	l->addWidget(combo_output_color_space, row, 1);
	++row;

	// saturation
	groupbox_gamut = new QGroupBox(tr("Gamut compression"));
	groupbox_gamut->setCheckable(true);
	l->addWidget(groupbox_gamut, row, 0, 1, -1);
	++row;

	QGridLayout *cs_l = new QGridLayout();
	cs_l->setSpacing(0);
	cs_l->setContentsMargins(2, 1, 2, 1);
	groupbox_gamut->setLayout(cs_l);

	radio_compress_saturation_auto = new QRadioButton(tr("auto"));
	cs_l->addWidget(radio_compress_saturation_auto, 0, 0);
	label_compress_saturation_auto = new QLabel("");
	cs_l->addWidget(label_compress_saturation_auto, 0, 1);

	radio_compress_saturation_manual = new QRadioButton(tr("manual"));
	radio_compress_saturation_manual->setChecked(true);
	cs_l->addWidget(radio_compress_saturation_manual, 1, 0);
//	slider_compress_saturation_factor = new GuiSlider(1.0, 3.0, 1.0, 100, 100, 10);
	slider_compress_saturation_factor = new GuiSlider(1.0, 3.0, 1.0, 100, 100, 50);
	cs_l->addWidget(slider_compress_saturation_factor, 1, 1);

	// compression strength
	QLabel *l_compress_strength = new QLabel(tr("Strength"));
	cs_l->addWidget(l_compress_strength, 2, 0);
	slider_compress_strength = new GuiSlider(0.0, 1.0, 1.0, 100, 100, 10);
	cs_l->addWidget(slider_compress_strength, 2, 1);

	// saturation of overexposed
	QLabel *l_desaturation_strength = new QLabel(tr("Desaturation"));
	cs_l->addWidget(l_desaturation_strength, 3, 0);
	slider_desaturation_strength = new GuiSlider(0.0, 1.0, 1.0, 100, 100, 10);
	cs_l->addWidget(slider_desaturation_strength, 3, 1);

	//--
	reconnect(true);
	return widget;
}

void F_cRGB_to_CM::ui_set_compress_saturation_factor(double factor) {
	label_compress_saturation_auto_value = factor;
	if(factor == 0.0)
		label_compress_saturation_auto->setText("");
	else
		label_compress_saturation_auto->setText(QString("factor: %1").arg(factor, 0, 'g', 3));
}

void F_cRGB_to_CM::reconnect(bool to_connect) {
	if(to_connect) {
		connect(combo_output_color_space, SIGNAL(currentIndexChanged(int)), this, SLOT(slot_combo_output_color_space(int)));
#ifndef DISABLE_CM
		connect(combo_color_model, SIGNAL(currentIndexChanged(int)), this, SLOT(slot_combo_color_model(int)));
#endif
		connect(groupbox_gamut, SIGNAL(toggled(bool)), this, SLOT(slot_groupbox_gamut(bool)));
		connect(radio_compress_saturation_manual, SIGNAL(toggled(bool)), this, SLOT(slot_compress_saturation_manual(bool)));
		connect(slider_compress_saturation_factor, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_compress_saturation_factor(double)));
//		connect(checkbox_desaturate_overexp, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_desaturate_overexp(int)));
		connect(slider_compress_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_compress_strength(double)));
		connect(slider_desaturation_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_desaturation_strength(double)));
	} else {
		disconnect(combo_output_color_space, SIGNAL(currentIndexChanged(int)), this, SLOT(slot_combo_output_color_space(int)));
#ifndef DISABLE_CM
		disconnect(combo_color_model, SIGNAL(currentIndexChanged(int)), this, SLOT(slot_combo_color_model(int)));
#endif
		disconnect(groupbox_gamut, SIGNAL(toggled(bool)), this, SLOT(slot_groupbox_gamut(bool)));
		disconnect(radio_compress_saturation_manual, SIGNAL(toggled(bool)), this, SLOT(slot_compress_saturation_manual(bool)));
		disconnect(slider_compress_saturation_factor, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_compress_saturation_factor(double)));
//		disconnect(checkbox_desaturate_overexp, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_desaturate_overexp(int)));
		disconnect(slider_compress_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_compress_strength(double)));
		disconnect(slider_desaturation_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_desaturation_strength(double)));
	}
}

void F_cRGB_to_CM::slot_combo_output_color_space(int index) {
	string output_color_space = ps->output_color_space;
	string new_cs = combo_output_color_space->currentText().toStdString();
	ps->output_color_space = CMS_Matrix::instance()->get_cs_name_from_string_name(new_cs);
	bool update = (ps->output_color_space != output_color_space);
	if(update) {
		emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_combo_color_model(int index) {
	CM::cm_type_en cm_type = CM::get_type(combo_color_model->currentText().toStdString());
	if(ps->cm_type != cm_type) {
		ps->cm_type = cm_type;
		emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_groupbox_gamut(bool enabled) {
	bool update = (ps->compress_saturation != enabled);
	if(update) {
		ps->compress_saturation = enabled;
		emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_compress_saturation_manual(bool state) {
	bool update = (ps->compress_saturation_manual != state);
	if(update) {
		ps->compress_saturation_manual = state;
		emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_slider_compress_saturation_factor(double value) {
	bool update = (ps->compress_saturation_factor != value);
	if(update) {
		ps->compress_saturation_factor = value;
		if(ps->compress_saturation_manual == false)
			radio_compress_saturation_manual->setChecked(true);
		else
			emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_slider_compress_strength(double value) {
	bool update = (ps->compress_strength != value);
	if(update) {
		ps->compress_strength = value;
		emit_signal_update();
	}
}

void F_cRGB_to_CM::slot_slider_desaturation_strength(double value) {
	bool update = (ps->desaturation_strength != value);
	if(update) {
		ps->desaturation_strength = value;
		emit_signal_update();
	}
}

//------------------------------------------------------------------------------
Filter::type_t F_cRGB_to_CM::type(void) {
	return Filter::t_color;
}

//------------------------------------------------------------------------------
class FP_cRGB_to_CM::task_t {
public:
	CM *cm;
	CM_Convert *cm_convert;
	float cmatrix[9];
};

FP_cRGB_to_CM::FP_cRGB_to_CM(void) : FilterProcess_CP() {
	_name = "F_cRGB_to_CM_CP";
}

bool FP_cRGB_to_CM::is_enabled(const PS_Base *ps_base) {
	return true;
}

void FP_cRGB_to_CM::filter_pre(fp_cp_args_t *args) {
	PS_cRGB_to_CM *ps = (PS_cRGB_to_CM *)args->ps_base;

	args->mutators->set("CM", CM::get_type_name(ps->cm_type));
	args->mutators->set("CM_ocs", ps->output_color_space);
	F_cRGB_to_CM *filter = (F_cRGB_to_CM *)args->filter;

	double factor = 0.0;
	args->mutators->set("CM_compress_saturation", ps->compress_saturation);
	if(ps->compress_saturation_manual) {
		args->mutators->set("CM_compress_saturation_factor", ps->compress_saturation_factor);
		args->mutators_mpass->set("CM_compress_saturation_factor", ps->compress_saturation_factor);
		if(args->filter != nullptr)
			filter->ui_set_compress_saturation_factor(factor);
	} else {
		if(args->filter != nullptr) {
			if(args->mutators_mpass->get("CM_compress_saturation_factor", factor))
				filter->ui_set_compress_saturation_factor(factor);
		}
	}
	args->mutators_mpass->set("CM_compress_strength", ps->compress_strength);
	args->mutators_mpass->set("CM_desaturation_strength", ps->desaturation_strength);
/*
	if(ps->desaturate_overexp && ps->compress_strength != 0.0) {
		args->mutators_mpass->set("CM_compress_strength", ps->compress_strength);
	} else {
		args->mutators_mpass->set("CM_compress_strength", 0.0);
	}
*/

	// shared parameters
	float matrix[9];
	for(int i = 0; i < 9; ++i)
		matrix[i] = args->metadata->cRGB_to_XYZ[i];
	CM *cm = CM::new_CM(ps->cm_type, CS_White(args->metadata->cRGB_illuminant_XYZ), CS_White("E"));;
	CM_Convert *cm_convert = cm->get_convert_XYZ_to_Jsh();
	for(int i = 0; i < args->cores; ++i) {
		task_t *task = new task_t;
		for(int j = 0; j < 9; ++j)
			task->cmatrix[j] = matrix[j];
		task->cm = cm;
		task->cm_convert = cm_convert;
		args->ptr_private[i] = (void *)task;
	}
}

void FP_cRGB_to_CM::filter_post(fp_cp_args_t *args) {
	FP_cRGB_to_CM::task_t *t = (FP_cRGB_to_CM::task_t *)args->ptr_private[0];
	for(int i = 0; i < args->cores; ++i) {
		t = (FP_cRGB_to_CM::task_t *)args->ptr_private[i];
		if(i == 0)
			delete t->cm;
		delete t;
	}
}

void FP_cRGB_to_CM::filter(float *pixel, void *data) {
	task_t *task = (task_t *)data;

	float XYZ[3];
	ddr::clip(pixel[0]);
	ddr::clip(pixel[1]);
	ddr::clip(pixel[2]);
/*
	if(pixel[0] > 1.0)	pixel[0] = 1.0;
	if(pixel[0] < 0.0)	pixel[0] = 0.0;
	if(pixel[1] > 1.0)	pixel[1] = 1.0;
	if(pixel[1] < 0.0)	pixel[1] = 0.0;
	if(pixel[2] > 1.0)	pixel[2] = 1.0;
	if(pixel[2] < 0.0)	pixel[2] = 0.0;
*/
	// convert cRGB to XYZ
/*
	const float *m = task->cmatrix;
	XYZ[0] = pixel[0] * m[0 * 3 + 0] + pixel[1] * m[0 * 3 + 1] + pixel[2] * m[0 * 3 + 2];
	XYZ[1] = pixel[0] * m[1 * 3 + 0] + pixel[1] * m[1 * 3 + 1] + pixel[2] * m[1 * 3 + 2];
	XYZ[2] = pixel[0] * m[2 * 3 + 0] + pixel[1] * m[2 * 3 + 1] + pixel[2] * m[2 * 3 + 2];
*/
	m3_v3_mult(XYZ, task->cmatrix, pixel);
	task->cm_convert->convert(pixel, XYZ);

	// TODO: remove that
	// output to 0.0 - 1.0
#ifdef _USE_ISNAN
#ifdef isnan
	if(isnan(pixel[0]))	pixel[0] = 0.0;
	if(isnan(pixel[1]))	pixel[1] = 0.0;
	if(isnan(pixel[2]))	pixel[2] = 0.0;
#endif
#endif
}

//------------------------------------------------------------------------------
