/*
 * view_zoom.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * TODO:
	- disable controls when active View has no open photo
	- update state on photo load/close
 *
 */

#include "config.h"
#include "view_zoom.h"
#include "view.h"

#include <iostream>
using namespace std;

//------------------------------------------------------------------------------
View_Zoom::View_Zoom(void) : QObject() {
	view = nullptr;
	ui_exist = false;
//	default_zoom_scale = 100.0;
	default_zoom_scale = 50.0;
	zoom_scale = default_zoom_scale;
}

void View_Zoom::fill_toolbar(QToolBar *t) {
	button_100 = new QToolButton();
	button_100->setText(tr("1:1"));
	button_100->setCheckable(true);
	t->addWidget(button_100);

	slider = new QSlider(Qt::Horizontal);
	slider->setMaximumWidth(100);
	slider->setMinimum(1);
	slider->setMaximum(100);
	// so mouse wheel will work smooth - usefull on small values
	slider->setPageStep(1);
	slider->setValue(zoom_scale);
	t->addWidget(slider);

	button_custom = new QToolButton();
//	button_custom->setText("100%");
	QString s = QString("%1%").arg(zoom_scale);
	button_custom->setText(s);
	button_custom->setCheckable(true);
	t->addWidget(button_custom);

	button_fit = new QToolButton();
	button_fit->setText(tr("Fit"));
	button_fit->setCheckable(true);
	t->addWidget(button_fit);

	t->addSeparator();
	reconnect(true);
	ui_exist = true;
	ui_disable(true);
}

void View_Zoom::reconnect(bool enable) {
	if(enable) {
		connect(button_100, SIGNAL(toggled(bool)), this, SLOT(slot_button_100_toggled(bool)));
		connect(button_custom, SIGNAL(toggled(bool)), this, SLOT(slot_button_custom_toggled(bool)));
		connect(button_fit, SIGNAL(toggled(bool)), this, SLOT(slot_button_fit_toggled(bool)));
		connect(slider, SIGNAL(valueChanged(int)), this, SLOT(slot_slider_value_changed(int)));
	} else {
		disconnect(button_100, SIGNAL(toggled(bool)), this, SLOT(slot_button_100_toggled(bool)));
		disconnect(button_custom, SIGNAL(toggled(bool)), this, SLOT(slot_button_custom_toggled(bool)));
		disconnect(button_fit, SIGNAL(toggled(bool)), this, SLOT(slot_button_fit_toggled(bool)));
		disconnect(slider, SIGNAL(valueChanged(int)), this, SLOT(slot_slider_value_changed(int)));
	}
}

void View_Zoom::slot_button_fit_toggled(bool checked) {
	button_fit_toggled(checked, true);
}

void View_Zoom::slot_button_custom_toggled(bool checked) {
	button_custom_toggled(checked, true);
}

void View_Zoom::slot_button_100_toggled(bool checked) {
	button_100_toggled(checked, true);
}

void View_Zoom::slot_slider_value_changed(int value) {
	slider_value_changed(value, true);
}

void View_Zoom::button_fit_toggled(bool checked, bool update_view) {
	if(view == nullptr) checked = false;
	reconnect(false);
	button_100->setChecked(false);
	button_custom->setChecked(false);
	button_fit->setChecked(checked);
	reconnect(true);
	if(update_view)
		set_state(View::zoom_t::zoom_fit);
}

void View_Zoom::button_custom_toggled(bool checked, bool update_view) {
	if(view == nullptr) checked = false;
	reconnect(false);
	button_100->setChecked(false);
	button_custom->setChecked(checked);
	button_fit->setChecked(false);
	reconnect(true);
	if(update_view)
		set_state(View::zoom_t::zoom_custom);
}

void View_Zoom::button_100_toggled(bool checked, bool update_view) {
	if(view == nullptr) checked = false;
	reconnect(false);
	button_100->setChecked(checked);
	button_custom->setChecked(false);
	button_fit->setChecked(false);
	reconnect(true);
	if(update_view)
		set_state(View::zoom_t::zoom_100);
}

void View_Zoom::button_custom_set_value(int value) {
	QString s = QString("%1%").arg(value);
	button_custom->setText(s);
}

void View_Zoom::slider_value_changed(int value, bool update_view) {
	button_custom->setText("100%");
	button_custom->setMinimumWidth(button_custom->width());

	button_custom_set_value(value);
	zoom_scale = value;
//	button_custom_toggled(true);
	bool checked = true;
	if(view == nullptr) checked = false;
	reconnect(false);
	button_100->setChecked(false);
	button_custom->setChecked(checked);
	button_fit->setChecked(false);
	reconnect(true);
	if(update_view)
		set_state(View::zoom_t::zoom_custom);
}

void View_Zoom::set_state(View::zoom_t state) {
	if(view != nullptr)
		view->set_zoom(state, zoom_scale);
}

// set state according to active View
void View_Zoom::set_View(View *_view) {
	view = _view;
	if(ui_exist)
		update();
}

void View_Zoom::ui_disable(bool disable) {
	bool enabled = !disable;
	button_100->setEnabled(enabled);
	button_custom->setEnabled(enabled);
	button_fit->setEnabled(enabled);
	slider->setEnabled(enabled);
}

// signal from View, on photo load/close or zoom changed from mouse double click, etc...
void View_Zoom::update(void) {
	if(view == nullptr)
		return;
	// read status
	View::zoom_t zoom_type;
	float zoom_scale;
	bool zoom_ui_disabled;
	view->get_zoom(zoom_type, zoom_scale, zoom_ui_disabled);
	// disable UI
	ui_disable(zoom_ui_disabled);
	if(zoom_ui_disabled) {
		reconnect(false);
		button_100->setChecked(false);
		button_custom->setChecked(false);
		button_fit->setChecked(false);
		slider->setValue(default_zoom_scale);
		reconnect(true);
	} else {
		// update enabled UI
		if(zoom_type == View::zoom_t::zoom_100)
			button_100_toggled(true, false);
		if(zoom_type == View::zoom_t::zoom_custom) {
			button_custom_toggled(true, false);
			reconnect(false);
			slider->setValue(zoom_scale);
			reconnect(true);
			button_custom_set_value(zoom_scale);
		}
		if(zoom_type == View::zoom_t::zoom_fit)
			button_fit_toggled(true, false);
	}
}

//------------------------------------------------------------------------------
