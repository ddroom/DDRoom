/*
 * f_unsharp.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*

Copyright: Mykhailo Malyshko, 2009-2015
License: GPLv3

TECH NOTE:
	- used mutators:
		"CM" -> string: "CIECAM02" | "CIELab"

ALGORITHM NOTE:
	- implemented sharpening with blur (Gaussian) masking; applyed 'local contrast' before 'unsharp mask' if any
	- for sharpness (via 'unsharp mask'), with small radius, used a real (i.e. radial 2D) Gaussian blurring;
	- for 'local contrast' used false Gaussian blurring with 2-pass squared apply 1D gaussian function, i.e. convolution;
		that way is much faster for a really big radius and results are still acceptable;
		'local contrast' is a 'unsharp mask' just with a huge radius and small amount of correction;
	- masking is applyed with adoptation to prevent splashes on already sharp edges (with a big difference between values);
		algorithm of adaptation can be improved, especially to decrease too bright splashes on light side of edge;

TODO:
	- keep open tab of 'Depend on image scale' when apply settings via undo/redo (save index of open page, apply changes, switch open page back)

*/

#include <iostream>
#include <math.h>

//#include "ddr_math.h"
#include "f_unsharp.h"
#include "gui_slider.h"
#include "cm.h"
#include "system.h"
#include "misc.h"

using namespace std;

#define _DEFAULT_SCALED_INDEX 1

//------------------------------------------------------------------------------
class PS_Unsharp : public PS_Base {
public:
	PS_Unsharp(void);
	virtual ~PS_Unsharp();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	bool scaled;
	double amount;
	double radius;
	double threshold;
	double s_amount[2];	// index:	0 - scale == 1.0
	double s_radius[2];	//			1 - scale >= 2.0
	double s_threshold[2];	//

	bool lc_enabled;
	double lc_amount;
	double lc_radius;
	bool lc_brighten;
	bool lc_darken;
};

class FP_params_t {
public:
	double amount;
	double radius;
	double threshold;
	double lc_radius;
};

//------------------------------------------------------------------------------
class FP_Unsharp : public FilterProcess_2D {
public:
	FP_Unsharp(void);
	virtual ~FP_Unsharp();
	bool is_enabled(const PS_Base *ps_base);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
	class task_t;
	void scaled_parameters(const class PS_Unsharp *ps, class FP_params_t *params, double scale_x, double scale_y);
	void process_square(class SubFlow *subflow);
	void process_round(class SubFlow *subflow);
};

//------------------------------------------------------------------------------
PS_Unsharp::PS_Unsharp(void) {
	reset();
}

PS_Unsharp::~PS_Unsharp() {
}

PS_Base *PS_Unsharp::copy(void) {
	PS_Unsharp *ps = new PS_Unsharp;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Unsharp::reset(void) {
	// default settings
	enabled = true;
	scaled = true;
	amount = 1.3f;
	radius = 2.0f;
	threshold = 0.015f;
	s_amount[0] = 2.0f;	// 1.0
	s_radius[0] = 2.5f;
	s_amount[1] = 0.8f;	// > 2.0
	s_radius[1] = 2.0f;	// better to avoid big radius here to avoid halo
	for(int i = 0; i < 2; ++i)
		s_threshold[i] = 0.015f;
	// local contrast
	lc_enabled = false;
	lc_amount = 0.25f;
	lc_radius = 10.0f;
	lc_brighten = true;
	lc_darken = true;
}

bool PS_Unsharp::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("amount", amount);
	dataset->get("radius", radius);
	dataset->get("threshold", threshold);
	// scaled
	dataset->get("scaled", scaled);
	dataset->get("s10_amount", s_amount[0]);
	dataset->get("s20_amount", s_amount[1]);
	dataset->get("s10_radius", s_radius[0]);
	dataset->get("s20_radius", s_radius[1]);
	dataset->get("s10_threshold", s_threshold[0]);
	dataset->get("s20_threshold", s_threshold[1]);
	// local contrast
	dataset->get("local_contrast_enabled", lc_enabled);
	dataset->get("local_contrast_amount", lc_amount);
	dataset->get("local_contrast_radius", lc_radius);
	dataset->get("local_contrast_brighten", lc_brighten);
	dataset->get("local_contrast_darken", lc_darken);
	return true;
}

bool PS_Unsharp::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("amount", amount);
	dataset->set("radius", radius);
	dataset->set("threshold", threshold);
	// scaled
	dataset->set("scaled", scaled);
	dataset->set("s10_amount", s_amount[0]);
	dataset->set("s20_amount", s_amount[1]);
	dataset->set("s10_radius", s_radius[0]);
	dataset->set("s20_radius", s_radius[1]);
	dataset->set("s10_threshold", s_threshold[0]);
	dataset->set("s20_threshold", s_threshold[1]);
//	string n_amount[] = {"s05_amount", "s10_amount", "s20_amount"};
	// local contrast
	dataset->set("local_contrast_enabled", lc_enabled);
	dataset->set("local_contrast_amount", lc_amount);
	dataset->set("local_contrast_radius", lc_radius);
	dataset->set("local_contrast_brighten", lc_brighten);
	dataset->set("local_contrast_darken", lc_darken);
	return true;
}

//------------------------------------------------------------------------------
FP_Unsharp *F_Unsharp ::fp = nullptr;

F_Unsharp::F_Unsharp(int id) : Filter() {
	filter_id = id;
	_id = "F_Unsharp";
//	_name = tr("Unsharp");
	_name = tr("Sharpness");
	if(fp == nullptr)
		fp = new FP_Unsharp();
	_ps = (PS_Unsharp *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Unsharp::~F_Unsharp() {
}

PS_Base *F_Unsharp::newPS(void) {
	return new PS_Unsharp();
}

FilterProcess *F_Unsharp::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FS_Unsharp : public FS_Base {
public:
	FS_Unsharp(void);
	int scaled_index;
};

FS_Unsharp::FS_Unsharp(void) {
	scaled_index = _DEFAULT_SCALED_INDEX;
}

FS_Base *F_Unsharp::newFS(void) {
	return new FS_Unsharp;
}

void F_Unsharp::saveFS(FS_Base *fs_base) {
	if(fs_base == nullptr)
		return;
	FS_Unsharp *fs = (FS_Unsharp *)fs_base;
	fs->scaled_index = scaled_index;
}

void F_Unsharp::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Unsharp *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	if(fs_base == nullptr) {
//cerr << "fs_base == nullptr" << endl;
		scaled_index = _DEFAULT_SCALED_INDEX;
	} else {
//cerr << "fs_base != nullptr" << endl;
		FS_Unsharp *fs = (FS_Unsharp *)fs_base;
		scaled_index = fs->scaled_index;
	}
	reconnect(false);

//cerr << "scaled_index == " << scaled_index << endl;
	tab_scaled->setCurrentIndex(scaled_index);
	if(ps->scaled) {
		checkbox_scaled->setCheckState(Qt::Checked);
		widget_unscaled->setVisible(false);
		tab_scaled->setVisible(true);
	} else {
		checkbox_scaled->setCheckState(Qt::Unchecked);
		tab_scaled->setVisible(false);
		widget_unscaled->setVisible(true);
	}
	bool en = ps->enabled;
	slider_amount->setValue(ps->amount);
	slider_radius->setValue(ps->radius);
	int threshold = int((ps->threshold * 100.0) + 0.005);
	slider_threshold->setValue(threshold);
	ps->enabled = en;
	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	for(int i = 0; i < 2; ++i) {
		slider_s_amount[i]->setValue(ps->s_amount[i]);
		slider_s_radius[i]->setValue(ps->s_radius[i]);
		float threshold = int(ps->s_threshold[i] * 1000.0 + 0.005);
		threshold /= 10.0;
		slider_s_threshold[i]->setValue(threshold);
	}
	// local contrast
	checkbox_lc_enable->setCheckState(ps->lc_enabled ? Qt::Checked : Qt::Unchecked);
	slider_lc_amount->setValue(ps->lc_amount);
	slider_lc_radius->setValue(ps->lc_radius);
	checkbox_lc_brighten->setCheckState(ps->lc_brighten ? Qt::Checked : Qt::Unchecked);
	checkbox_lc_darken->setCheckState(ps->lc_darken ? Qt::Checked : Qt::Unchecked);

	reconnect(true);
}

QWidget *F_Unsharp::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	widget = new QWidget(parent);
	QVBoxLayout *wvb = new QVBoxLayout(widget);
	wvb->setSpacing(0);
	wvb->setContentsMargins(0, 0, 0, 0);
	wvb->setSizeConstraint(QLayout::SetMinimumSize);

	//----------------------------
	// Unsharp
	QGroupBox *g_unsharp = new QGroupBox(_name);
	wvb->addWidget(g_unsharp);
//	widget = g_unsharp;
//	QVBoxLayout *vb = new QVBoxLayout(widget);
	QVBoxLayout *vb = new QVBoxLayout(g_unsharp);
	vb->setSpacing(2);
	vb->setContentsMargins(2, 1, 2, 1);
	vb->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout *hb_er = new QHBoxLayout();
	hb_er->setSpacing(0);
	hb_er->setContentsMargins(0, 0, 0, 0);
	hb_er->setSizeConstraint(QLayout::SetMinimumSize);
	checkbox_enable = new QCheckBox(tr("Enable"));
	hb_er->addWidget(checkbox_enable);
	vb->addLayout(hb_er);

	checkbox_scaled = new QCheckBox(tr("Depend on image scale"));
	vb->addWidget(checkbox_scaled);

	//----------------------------
	// widget w/o scaling
	widget_unscaled = new QWidget();
	widget_unscaled->setVisible(false);
	QGridLayout *lw = new QGridLayout(widget_unscaled);
	lw->setSpacing(1);
	lw->setContentsMargins(2, 1, 2, 1);
	lw->setSizeConstraint(QLayout::SetMinimumSize);

	QLabel *l_amount = new QLabel(tr("Amount"));
	lw->addWidget(l_amount, 0, 0);
	slider_amount = new GuiSlider(0.0, 6.0, 1.0, 100, 20, 10);
	lw->addWidget(slider_amount, 0, 1);

	QLabel *l_radius = new QLabel(tr("Radius"));
	lw->addWidget(l_radius, 1, 0);
	slider_radius = new GuiSlider(0.0, 8.0, 1.0, 10, 10, 10);
	lw->addWidget(slider_radius, 1, 1);

	QLabel *l_threshold = new QLabel(tr("Threshold"));
	lw->addWidget(l_threshold, 2, 0);
	QHBoxLayout *hb_l_threshold = new QHBoxLayout();
	hb_l_threshold->setSpacing(0);
	hb_l_threshold->setContentsMargins(0, 0, 0, 0);
	hb_l_threshold->setSizeConstraint(QLayout::SetMinimumSize);
	slider_threshold = new GuiSlider(0.0, 8.0, 0.0, 10, 10, 10);
	hb_l_threshold->addWidget(slider_threshold);
	QLabel *l_threshold_percent = new QLabel(tr("%"));
	hb_l_threshold->addWidget(l_threshold_percent);
	lw->addLayout(hb_l_threshold, 2, 1);

	vb->addWidget(widget_unscaled);

	//----------------------------
	// page for scaling
	tab_scaled = new QTabWidget();
	tab_scaled->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	tab_scaled->setVisible(false);
//	QString tabs_labels[] = {tr("scale: < 0.5x"), tr(" = 1.0x"), tr(" > 2.0x")};
	QString tabs_labels[] = {tr("scale: = 1.0x"), tr(" > 2.0x")};
	for(int i = 0; i < 2; ++i) {
		QWidget *page;
//		if(i == 0)
//			page = new QWidget(parent);
//		else
			page = new QWidget();
		QGridLayout *pl = new QGridLayout(page);
		pl->setSpacing(1);
		pl->setContentsMargins(2, 1, 2, 1);

		QLabel *pl_amount = new QLabel(tr("Amount"));
		pl->addWidget(pl_amount, 1, 0);
		slider_s_amount[i] = new GuiSlider(0.0, 6.0, 1.0, 100, 20, 10);
		pl->addWidget(slider_s_amount[i], 1, 1);

		QLabel *pl_radius = new QLabel(tr("Radius"));
		pl->addWidget(pl_radius, 2, 0);
		slider_s_radius[i] = new GuiSlider(0.0, 8.0, 1.0, 10, 10, 10);
		pl->addWidget(slider_s_radius[i], 2, 1);

		QLabel *pl_threshold = new QLabel(tr("Threshold"));
		pl->addWidget(pl_threshold, 3, 0);
		QHBoxLayout *hb_pl_threshold = new QHBoxLayout();
		hb_pl_threshold->setSpacing(0);
		hb_pl_threshold->setContentsMargins(0, 0, 0, 0);
		hb_pl_threshold->setSizeConstraint(QLayout::SetMinimumSize);
		slider_s_threshold[i] = new GuiSlider(0.0, 8.0, 0.0, 10, 10, 10);
		hb_pl_threshold->addWidget(slider_s_threshold[i]);
		QLabel *l_threshold_percent = new QLabel(tr("%"));
		hb_pl_threshold->addWidget(l_threshold_percent);
		pl->addLayout(hb_pl_threshold, 3, 1);

		tab_scaled->addTab(page, tabs_labels[i]);
	}
	vb->addWidget(tab_scaled);

	//----------------------------
	// local contrast
	wvb->addWidget(gui_local_contrast());

	//----------------------------
	// actually, better to connect _after_ UI loading to prevent false feedback
//	reconnect(true);

	return widget;
}

void F_Unsharp::reconnect(bool to_connect) {
	if(to_connect) {
		connect(slider_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_amount(double)));
		connect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		connect(slider_threshold, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_threshold(double)));
		for(int i = 0; i < 2; ++i) {
			connect(slider_s_amount[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_amount(double)));
			connect(slider_s_radius[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_radius(double)));
			connect(slider_s_threshold[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_threshold(double)));
		}
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
		connect(tab_scaled, SIGNAL(currentChanged(int)), this, SLOT(slot_tab_scaled(int)));
		// local contrast
		connect(checkbox_lc_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_enable(int)));
		connect(slider_lc_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_amount(double)));
		connect(slider_lc_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_radius(double)));
		connect(checkbox_lc_brighten, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_brighten(int)));
		connect(checkbox_lc_darken, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_darken(int)));
	} else {
		disconnect(slider_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_amount(double)));
		disconnect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		disconnect(slider_threshold, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_threshold(double)));
		for(int i = 0; i < 2; ++i) {
			disconnect(slider_s_amount[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_amount(double)));
			disconnect(slider_s_radius[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_radius(double)));
			disconnect(slider_s_threshold[i], SIGNAL(signal_changed(double)), this, SLOT(slot_changed_s_threshold(double)));
		}
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
		disconnect(tab_scaled, SIGNAL(currentChanged(int)), this, SLOT(slot_tab_scaled(int)));
		// local contrast
		disconnect(checkbox_lc_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_enable(int)));
		disconnect(slider_lc_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_amount(double)));
		disconnect(slider_lc_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_radius(double)));
		disconnect(checkbox_lc_brighten, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_brighten(int)));
		disconnect(checkbox_lc_darken, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_darken(int)));
	}
}

void F_Unsharp::slot_checkbox_scaled(int state) {
	bool scaled_enable = (state == Qt::Checked);
	if(scaled_enable) {
		widget_unscaled->setVisible(false);
		tab_scaled->setVisible(true);
	} else {
		tab_scaled->setVisible(false);
		widget_unscaled->setVisible(true);
	}
	if(ps->scaled != scaled_enable) {
		ps->scaled = scaled_enable;
		emit_signal_update();
	}
}

void F_Unsharp::slot_tab_scaled(int index) {
	scaled_index = index;
}

void F_Unsharp::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
//cerr << "emit signal_update(session_id, ) - 1" << endl;
		emit_signal_update();
	}
}

void F_Unsharp::slot_changed_s_amount(double value) {
	changed_slider(value, ps->s_amount[scaled_index], false);
}

void F_Unsharp::slot_changed_s_radius(double value) {
	changed_slider(value, ps->s_radius[scaled_index], false);
}

void F_Unsharp::slot_changed_s_threshold(double value) {
	changed_slider(value, ps->s_threshold[scaled_index], true);
}

void F_Unsharp::slot_changed_amount(double value) {
	changed_slider(value, ps->amount, false);
}

void F_Unsharp::slot_changed_radius(double value) {
	changed_slider(value, ps->radius, false);
}

void F_Unsharp::slot_changed_threshold(double value) {
	changed_slider(value, ps->threshold, true);
}

void F_Unsharp::changed_slider(double value, double &ps_value, bool is_255) {
	double _value = ps_value;
	if(is_255) {
		_value = int(ps_value * 1000.0 + 0.05);
		_value /= 10.0;
	}
	bool update = (_value != value);
	if(value != 0.0 && (!ps->enabled)) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
//cerr << "update == " << update << "; _value == " << _value << "; value == " << value << endl;
	if(update && ps->enabled) {
		if(is_255)
			ps_value = value / 100.0;
		else
			ps_value = value;
		emit_signal_update();
	}
}

Filter::type_t F_Unsharp::type(void) {
	return Filter::t_color;
}

//------------------------------------------------------------------------------
// local contrast
QWidget *F_Unsharp::gui_local_contrast(void) {
	QGroupBox *g = new QGroupBox(tr("Local contrast"));

	QGridLayout *gl = new QGridLayout(g);
	gl->setSpacing(1);
	gl->setContentsMargins(2, 1, 2, 1);
	gl->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_lc_enable = new QCheckBox(tr("Enable"));
//	gl->addWidget(checkbox_lc_enable, 0, 0, 1, 1);
	gl->addWidget(checkbox_lc_enable, 0, 0);
	QHBoxLayout *hb = new QHBoxLayout();
	checkbox_lc_brighten = new QCheckBox(tr("Brighten"));
	hb->addWidget(checkbox_lc_brighten, 0, Qt::AlignRight);
	checkbox_lc_darken = new QCheckBox(tr("Darken"));
	hb->addWidget(checkbox_lc_darken, 0, Qt::AlignRight);
	gl->addLayout(hb, 0, 1);

	QLabel *lc_amount = new QLabel(tr("Amount"));
	gl->addWidget(lc_amount, 1, 0);
	slider_lc_amount = new GuiSlider(0.0, 1.0, 0.25, 100, 100, 10);
	gl->addWidget(slider_lc_amount, 1, 1);

	QLabel *lc_radius = new QLabel(tr("Radius"));
	gl->addWidget(lc_radius, 2, 0);
//	slider_lc_radius = new GuiSlider(0.0, 100.0, 20.0, 10, 10, 100);
	slider_lc_radius = new GuiSlider(0.0, 100.0, 20.0, 10, 1, 10);
	gl->addWidget(slider_lc_radius, 2, 1);

	return g;
}

void F_Unsharp::slot_checkbox_lc_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->lc_enabled != value);
	if(update) {
		ps->lc_enabled = value;
		emit_signal_update();
	}
}

void F_Unsharp::slot_checkbox_lc_brighten(int state) {
	slot_checkbox_lc_do(ps->lc_brighten, state);
}

void F_Unsharp::slot_checkbox_lc_darken(int state) {
	slot_checkbox_lc_do(ps->lc_darken, state);
}

void F_Unsharp::slot_checkbox_lc_do(bool &ps_value, int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps_value != value);
	if(update && !ps->lc_enabled) {
		reconnect(false);
		ps->lc_enabled = true;
		checkbox_lc_enable->setCheckState(Qt::Checked);
		reconnect(true);
	}
	if(update) {
		ps_value = value;
		emit_signal_update();
	}
}

void F_Unsharp::slot_changed_lc_amount(double value) {
	changed_lc_slider(value, ps->lc_amount);
}

void F_Unsharp::slot_changed_lc_radius(double value) {
	changed_lc_slider(value, ps->lc_radius);
}

void F_Unsharp::changed_lc_slider(double value, double &ps_value) {
	double _value = ps_value;
	bool update = (_value != value);
	if(value != 0.0 && (!ps->lc_enabled)) {
		reconnect(false);
		ps->lc_enabled = true;
		checkbox_lc_enable->setCheckState(Qt::Checked);
		reconnect(true);
		update = true;
	}
	if(update && ps->lc_enabled) {
		ps_value = value;
		emit_signal_update();
	}
}

//------------------------------------------------------------------------------
FP_Unsharp::FP_Unsharp(void) : FilterProcess_2D() {
	_name = "F_Unsharp";
}

FP_Unsharp::~FP_Unsharp() {
}

bool FP_Unsharp::is_enabled(const PS_Base *ps_base) {
	const PS_Unsharp *ps = (const PS_Unsharp *)ps_base;
	bool enabled = false;
	if(ps->enabled && ps->amount != 0.0 && ps->radius != 0.0)
		enabled = true;
	if(ps->lc_enabled && ps->lc_amount != 0.0 && ps->lc_radius != 0.0)
		enabled = true;
	return enabled;
}

void FP_Unsharp::scaled_parameters(const class PS_Unsharp *ps, class FP_params_t *params, double scale_x, double scale_y) {
	double scale = (scale_x + scale_y) / 2.0;
/*
cerr << "_____________________________________________________________________" << endl;
cerr << "=====================================================================" << endl;
cerr << "scale == " << scale << endl;
*/
	params->lc_radius = 0.0;
	if(ps->lc_enabled) {
		params->lc_radius = ps->lc_radius;
		// limit excessive increasing on too small crops etc.
		float rescale = (scale > 0.5) ? scale : 0.5;
		params->lc_radius /= rescale;
	}

	params->radius = 0.0;
	params->amount = 0.0;
	if(ps->enabled) {
		if(!ps->scaled) {
			params->amount = ps->amount;
			params->radius = ps->radius;
			params->threshold = ps->threshold;
		} else {
			if(scale >= 2.0) {
				params->amount = ps->s_amount[1];
				params->radius = ps->s_radius[1];
				params->threshold = ps->s_threshold[1];
			} else if(scale <= 1.0) {
				params->amount = ps->s_amount[0];
				params->radius = ps->s_radius[0];
				params->threshold = ps->s_threshold[0];
			} else {
				scale -= 1.0;
				params->amount = ps->s_amount[0] + (ps->s_amount[1] - ps->s_amount[0]) * scale;
				params->radius = ps->s_radius[0] + (ps->s_radius[1] - ps->s_radius[0]) * scale;
				params->threshold = ps->s_threshold[0] + (ps->s_threshold[1] - ps->s_threshold[0]) * scale;
			}
		}
	}
/*
cerr << "ps->amount[0] == " << ps->s_amount[0] << "; ps->amount[1] == " << ps->s_amount[1] << endl;
cerr << "ps->radius[0] == " << ps->s_radius[0] << "; ps->radius[1] == " << ps->s_radius[1] << endl;
cerr << "ps->threshold[0] == " << ps->s_threshold[0] << "; ps->threshold[1] == " << ps->s_threshold[1] << endl;
cerr << "params->amount == " << params->amount << endl;
cerr << "params->radius == " << params->radius << endl;
cerr << "params->threshold == " << params->threshold << endl;
cerr << endl;
*/
}

//------------------------------------------------------------------------------
void FP_Unsharp::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	// Well, here we have 1:1 size and all edges are outer, so no cropping here
//	const PS_Base *ps_base = fp_size->ps_base;
	*d_after = *d_before;
//	if(is_enabled(ps_base) == false)
//		return;
}

void FP_Unsharp::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	// here we have tiles - so crop inner edges of them as necessary
	const PS_Base *ps_base = fp_size->ps_base;
	*d_before = *d_after;
	if(is_enabled(ps_base) == false)
		return;
	const PS_Unsharp *ps = (const PS_Unsharp *)ps_base;
	FP_params_t params;
//cerr << "d_before->position.size == " << d_before->size.w << " x " << d_before->size.h << endl;
//cerr << " d_after->position.size == " << d_after->size.w << " x " << d_after->size.h << endl;
	scaled_parameters(ps, &params, d_after->position.px_size_x, d_after->position.px_size_y);
	// again, do handle overlapping issue here
	// TODO: check together 'unsharp' and 'local contrast'
	*d_before = *d_after;
	int edge = 0;
	if(params.lc_radius > 0.0 && ps->lc_enabled) {
		edge += int(params.lc_radius * 2.0 + 1.0) / 2 + 1;
//		edge += int(params.lc_radius + 2.0);
	}
	if(params.radius > 0.0 && ps->enabled) {
		edge += int(params.radius * 2.0 + 1.0) / 2 + 1;
//		edge += int(params.radius + 1.0);
	}
//cerr << endl;
//cerr << "sizze_backward(); params.lc_radius == " << params.lc_radius << "; params.radius == " << params.radius << endl;
//cerr << endl;
	const float px_size_x = d_before->position.px_size_x;
	const float px_size_y = d_before->position.px_size_y;
	d_before->position.x -= px_size_x * edge;
	d_before->position.y -= px_size_y * edge;
	d_before->size.w += edge * 2;
	d_before->size.h += edge * 2;
}

class FP_Unsharp::task_t {
public:
	Area *area_in;
	Area *area_out;
	PS_Unsharp *ps;
	std::atomic_int *y_flow_pass_1;
	std::atomic_int *y_flow_pass_2;
	Area *area_temp;

	const float *kernel;
	int kernel_length;
	int kernel_offset;
	float amount;
	float threshold;
//	float radius;
	bool lc_brighten;
	bool lc_darken;

	int in_x_offset;
	int in_y_offset;
};

// requirements for caller:
// - should be skipped call for 'is_enabled() == false' filters
// - if OpenCL is enabled, should be called only for 'master' thread - i.e. subflow/::task_t will be ignored
Area *FP_Unsharp::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	PS_Unsharp *ps = (PS_Unsharp *)filter_obj->ps_base;
	Area *area_out = nullptr;
	Area *area_to_delete = nullptr;

	for(int type = 0; type < 2 && !process_obj->OOM; ++type) {
		if(type == 0 && ps->lc_enabled == false) // type == 0 - local contrast, square blur
			continue;
		if(type == 1 && ps->enabled == false)    // type == 1 - sharpness, round blur
			continue;
		task_t **tasks = nullptr;
		std::atomic_int *y_flow_pass_1 = nullptr;
		std::atomic_int *y_flow_pass_2 = nullptr;
		float *kernel = nullptr;
		Area *area_temp = nullptr;

		FP_params_t params;
		if(subflow->sync_point_pre()) {
//cerr << "FP_UNSHARP::PROCESS" << endl;
			if(area_out != nullptr) {
				area_to_delete = area_out;
				area_in = area_out;
			}
			// non-destructive processing
			int cores = subflow->cores();
			tasks = new task_t *[cores];

			Area::t_dimensions d_out = *area_in->dimensions();
			Tile_t::t_position &tp = process_obj->position;
			// keep _x_max, _y_max, px_size the same
			d_out.position.x = tp.x;
			d_out.position.y = tp.y;
			d_out.size.w = tp.width;
			d_out.size.h = tp.height;
			d_out.edges.x1 = 0;
			d_out.edges.x2 = 0;
			d_out.edges.y1 = 0;
			d_out.edges.y2 = 0;
			const float px_size_x = area_in->dimensions()->position.px_size_x;
			const float px_size_y = area_in->dimensions()->position.px_size_y;

			scaled_parameters(ps, &params, px_size_x, px_size_y);
//			scaled_parameters(ps, &params, area_in->dimensions()->position.px_size);
			if(type == 0) {
				// increase output of 'local contrast' if 'sharpness' is enabled
				if(params.radius > 0.0 && ps->enabled) {
					int edge = int(params.radius * 2.0 + 1.0) / 2 + 1;
					d_out.position.x -= px_size_x * edge;
					d_out.position.y -= px_size_y * edge;
					d_out.size.w += edge * 2;
					d_out.size.h += edge * 2;
				}
				// local contrast parameters
				params.amount = ps->lc_amount;
				params.radius = params.lc_radius;
				params.threshold = 0.0;
			}
//cerr << endl;
//cerr << "px_size_x == " << px_size_x << endl;
//cerr << "px_size_y == " << px_size_y << endl;
//cerr << "   params.amount == " << params.amount << endl;
//cerr << "   params.radius == " << params.radius << endl;
//cerr << "params.lc_radius == " << params.lc_radius << endl;
			// fill gaussian kernel
			const float sigma = (params.radius * 2.0) / 6.0;
			const float sigma_sq = sigma * sigma;
			const int kernel_length = 2 * floor(params.radius) + 1;
			const int kernel_offset = -floor(params.radius);
			const float kernel_offset_f = -floor(params.radius);
			int kernel_length_y = (type == 1) ? kernel_length : 1;
//cerr << "kernel_length == " << kernel_length << ", kernel_length_y == " << kernel_length_y << endl;
			kernel = new float[kernel_length * kernel_length_y];
			for(int y = 0; y < kernel_length_y; ++y) {
				for(int x = 0; x < kernel_length; ++x) {
					float fx = kernel_offset_f + x;
					float fy = kernel_offset_f + y;
					float z = sqrtf(fx * fx + fy * fy);
					float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
					int index = y * kernel_length + x;
					kernel[index] = w;
				}
			}
			//--
			area_out = new Area(&d_out);
			process_obj->OOM |= !area_out->valid();
			if(type == 0) {
				area_temp = new Area(area_in->dimensions(), Area::type_t::type_float_p1);
				process_obj->OOM |= !area_temp->valid();
			}
			//--
			int in_x_offset = (d_out.position.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
			int in_y_offset = (d_out.position.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
			y_flow_pass_1 = new std::atomic_int(0);
			y_flow_pass_2 = new std::atomic_int(0);
			for(int i = 0; i < cores; ++i) {
				tasks[i] = new task_t;
				tasks[i]->area_in = area_in;
				tasks[i]->area_out = area_out;
				tasks[i]->ps = ps;
				tasks[i]->y_flow_pass_1 = y_flow_pass_1;
				tasks[i]->y_flow_pass_2 = y_flow_pass_2;
				tasks[i]->area_temp = area_temp;

				tasks[i]->kernel = kernel;
				tasks[i]->kernel_length = kernel_length;
				tasks[i]->kernel_offset = kernel_offset;
				tasks[i]->amount = params.amount;
				tasks[i]->threshold = params.threshold;
				tasks[i]->lc_brighten = ps->lc_brighten;
				tasks[i]->lc_darken = ps->lc_darken;

//				tasks[i]->radius = params.radius;

				tasks[i]->in_x_offset = in_x_offset;
				tasks[i]->in_y_offset = in_y_offset;
			}
			subflow->set_private((void **)tasks);
		}
		subflow->sync_point_post();

		if(!process_obj->OOM) {
			if(type == 0)
				process_square(subflow);
			else
				process_round(subflow);
		}

		subflow->sync_point();
		if(subflow->is_master()) {
			if(area_to_delete != nullptr)
				delete area_to_delete;
			if(type == 0)
				delete area_temp;
			delete y_flow_pass_1;
			delete y_flow_pass_2;
			for(int i = 0; i < subflow->cores(); ++i)
				delete tasks[i];
			delete[] tasks;
			delete[] kernel;
		}
	}
	return area_out;
}

//------------------------------------------------------------------------------
void FP_Unsharp::process_square(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();

	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

	const int in_x_offset = task->in_x_offset;
	const int in_y_offset = task->in_y_offset;
	const int out_x_offset = task->area_out->dimensions()->edges.x1;
	const int out_y_offset = task->area_out->dimensions()->edges.y1;
	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();
	const int in_w = task->area_in->dimensions()->width();
	const int in_h = task->area_in->dimensions()->height();
	const int t_x_max = task->area_temp->dimensions()->width();
	const int t_y_max = task->area_temp->dimensions()->height();
	const int t_x_offset = task->area_temp->dimensions()->edges.x1;
	const int t_y_offset = task->area_temp->dimensions()->edges.y1;

	const float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();
	float *temp = (float *)task->area_temp->ptr();

	const float *kernel = task->kernel;
	const int kernel_length = task->kernel_length;
	const int kernel_offset = task->kernel_offset;

	// horizontal pass - from input to temporal area
	int j = 0;
	while((j = task->y_flow_pass_1->fetch_add(1)) < t_y_max) {
		for(int i = 0; i < t_x_max; ++i) {
//			const int i_in = ((j + t_y_offset) * in_width + (i + t_x_offset)) * 4;
			const int i_temp = ((j + t_y_offset) * in_width + (i + t_x_offset));
//			const int i_out = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4;
//			const int i_temp = j * temp_width + i;
//			if(in[i_in + 3] <= 0.0)
//				continue;
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int x = 0; x < kernel_length; ++x) {
//				const int in_x = i + x + kernel_offset + in_x_offset;
				const int in_x = i + x + kernel_offset + t_x_offset;
				if(in_x >= 0 && in_x < in_w) {
					float alpha = in[((j + t_y_offset) * in_width + in_x) * 4 + 3];
//					if(alpha == 1.0f) {
					if(alpha > 0.05f) {
						float v_in = in[((j + t_y_offset) * in_width + in_x) * 4 + 0];
						if(v_in < 0.0f)
							v_in = 0.0f;
						float kv = kernel[x];
						v_blur += v_in * kv;
						v_blur_w += kv;
					}
				}
			}
			if(v_blur_w == 0.0f) {
				temp[i_temp] = 0.0f;
				continue;
			}
			temp[i_temp] = v_blur / v_blur_w;
		}
	}

	// synchronize temporary array
	subflow->sync_point();

	float amount = task->amount;
	float threshold = task->threshold;
	// vertical pass - from temporary to output area
	j = 0;
	while((j = task->y_flow_pass_2->fetch_add(1)) < y_max) {
		for(int i = 0; i < x_max; ++i) {
			const int i_in = ((j + in_y_offset) * in_width + (i + in_x_offset)) * 4; // k
			const int i_out = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4; // l
//			const int i_temp = j * temp_width + i;
			out[i_out + 0] = in[i_in + 0];
			out[i_out + 1] = in[i_in + 1];
			out[i_out + 2] = in[i_in + 2];
			out[i_out + 3] = in[i_in + 3];
			if(in[i_in + 3] <= 0.0f)
				continue;
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int y = 0; y < kernel_length; ++y) {
//				const int temp_y = j + y + kernel_offset;
				const int in_y = j + y + kernel_offset + in_y_offset;
				if(in_y >= 0 && in_y < in_h) {
					float alpha = in[(in_y * in_width + i + in_x_offset) * 4 + 3];
//					if(alpha == 1.0) {
					if(alpha > 0.05f) {
						float v_temp = temp[in_y * in_width + i + in_x_offset];
//						float v_temp = temp[temp_y * temp_width + i];
						float kv = kernel[y];
						v_blur += v_temp * kv;
						v_blur_w += kv;
					}
				}
			}
			if(v_blur_w == 0.0f) {
				out[i_out + 0] = 0.5f;
				out[i_out + 3] = 0.0f;
				continue;
			}
			v_blur /= v_blur_w;

//			out[i_out + 0] = v_blur;
//			continue;

			float v_in = in[i_in + 0];
			float v_out = v_in - v_blur;
//			v_out = amount * ((ddr::abs(v_out) < threshold) ? 0.0 : v_out);
			if(ddr::abs(v_out) < threshold && threshold > 0.0f) {
				float s = v_out / threshold;
				v_out = (v_out < 0.0f) ? (-s * s) : (s * s);
				v_out *= threshold;
			}
			v_out *= amount;
			// do adaptation to prevent signal splashing at the hard edges (where difference is already very big...)
			if(v_out < 0.0f && v_in > 0.0f)
				v_out *= (v_in + v_out) / v_in;
			if(v_out > 0.0f && v_in + v_out != 0.0f)
				v_out *= v_in / (v_in + v_out);
			v_out = v_in + v_out;
			if(v_out < 0.0f)	v_out = 0.0f;
			if(v_out > 1.0f)	v_out = 1.0f;
			// darken only alike
			if(!task->lc_brighten && v_out > v_in)
				v_out = v_in;
			// brighten only alike
			if(!task->lc_darken && v_out < v_in)
				v_out = v_in;
			out[i_out + 0] = v_out;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Unsharp::process_round(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();

	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

	const int in_x_offset = task->in_x_offset;
	const int in_y_offset = task->in_y_offset;
	const int out_x_offset = task->area_out->dimensions()->edges.x1;
	const int out_y_offset = task->area_out->dimensions()->edges.y1;
	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();
	const int in_w = task->area_in->dimensions()->width();
	const int in_h = task->area_in->dimensions()->height();

	const float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const float amount = task->amount;
	const float threshold = task->threshold;
/*
	const float sigma = (task->radius * 2.0) / 6.0;
	const float sigma_sq = sigma * sigma;
	const int kernel_length = 2 * floor(task->radius) + 1;
	const float kernel_offset = -floor(task->radius);
	const int kl_off = kernel_offset;
*/
	const float *kernel = task->kernel;
	const int kernel_length = task->kernel_length;
	const int kernel_offset = task->kernel_offset;

	// float z = sqrtf(x * x + y * y);
	// float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));

	int j = 0;
	while((j = task->y_flow_pass_1->fetch_add(1)) < y_max) {
		for(int i = 0; i < x_max; ++i) {
			int l = ((j + in_y_offset) * in_width + (i + in_x_offset)) * 4;
			int k = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4;
			//--
			out[k + 1] = in[l + 1];
			out[k + 2] = in[l + 2];
			out[k + 3] = in[l + 3];
			if(in[l + 3] <= 0.0f) {
				out[k + 0] = 0.5f;
				continue;
			}
			// calculate blurred value
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int y = 0; y < kernel_length; ++y) {
				for(int x = 0; x < kernel_length; ++x) {
					const int in_x = i + x + kernel_offset + in_x_offset;
					const int in_y = j + y + kernel_offset + in_y_offset;
					if(in_x >= 0 && in_x < in_w && in_y >= 0 && in_y < in_h) {
						float alpha = in[(in_y * in_width + in_x) * 4 + 3];
//						if(alpha == 1.0) {
						if(alpha > 0.05f) {
//							float v_in = in[((in_y + in_y_offset) * in_width + in_x + in_x_offset) * 4 + 0];
							float v_in = in[(in_y * in_width + in_x) * 4 + 0];
/*
							float fx = kernel_offset + x;
							float fy = kernel_offset + y;
							float z = sqrtf(fx * fx + fy * fy);
							float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
*/
							int kernel_index = y * kernel_length + x;
							float w = kernel[kernel_index];
							v_blur += v_in * w;
							v_blur_w += w;
						}
					}
				}
			}
			if(v_blur_w == 0.0f) {
				out[k + 0] = 0.5f;
				out[k + 3] = 0.0f;
				continue;
			}
			v_blur /= v_blur_w;

			float v_in = in[l + 0];
			float v_out = v_in - v_blur;
			if(threshold > 0.0f && ddr::abs(v_out) < threshold) {
				float s = v_out / threshold;
				v_out = (v_out < 0.0f) ? (-s * s) : (s * s);
				v_out *= threshold;
			}
			v_out *= amount;
			// do adaptation to prevent signal splashing at the "hard" edges (where difference is already very big...)
			if(v_out < 0.0f && v_in > 0.0f)
				v_out *= (v_in + v_out) / v_in;
			if(v_out > 0.0f && v_in + v_out != 0.0f)
				v_out *= v_in / (v_in + v_out);
			//--
			v_out = v_in + v_out;
//			if(v_out > v_in)
//				v_out = v_in;
			ddr::clip(v_out);
			out[k + 0] = v_out;
		}
	}
}

//------------------------------------------------------------------------------
