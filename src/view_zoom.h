#ifndef __H_VIEW_ZOOM__
#define __H_VIEW_ZOOM__
/*
 * view_zoom.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <QtWidgets>
#include "view.h"

//------------------------------------------------------------------------------
class View_Zoom : public QObject {
	Q_OBJECT

public:
	View_Zoom(void);

	void fill_toolbar(QToolBar *t);
	void set_View(class View *_view);
	void update(void);

protected slots:
	void slot_button_100_toggled(bool);
	void slot_button_custom_toggled(bool);
	void slot_button_fit_toggled(bool);
	void slot_slider_value_changed(int value);

protected:
	void set_state(View::zoom_t state);
	void ui_disable(bool disable);
	void reconnect(bool);
	QToolButton *button_100;
	QToolButton *button_custom;
	QToolButton *button_fit;
	QSlider *slider;

	class View *view;
	bool ui_exist;
	float zoom_scale;
	float default_zoom_scale;

	void button_100_toggled(bool, bool update_view);
	void button_custom_toggled(bool, bool update_view);
	void button_fit_toggled(bool, bool update_view);
	void slider_value_changed(int value, bool update_view);
	void button_custom_set_value(int value);
};

//------------------------------------------------------------------------------

#endif // __H_VIEW_ZOOM__
