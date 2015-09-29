/*
 * f_rotation.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 *	TODO:
	- notice all mutators, to correct tiles size asked by tiles_receiver (View);

 *	NOTES:
 *	- to prevent downscaled cache abuse in workflow, use 'downscaled' rotation - when result image dimensions will be the same as original, w/o magnification.
 *
 */	

#include <iostream>

#include "f_rotation.h"
#include "filter_gp.h"
//#include "system.h"
#include "gui_slider.h"
#include "ddr_math.h"

using namespace std;

#define FR_MIN_TILE_SIZE 24

//------------------------------------------------------------------------------
class PS_Rotation : public PS_Base {

public:
	PS_Rotation(void);
	virtual ~PS_Rotation();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	double rotation_angle;
//	bool fold;
};

//------------------------------------------------------------------------------
class FP_Rotation : public FilterProcess_GP {
public:
	FP_Rotation(void);
	~FP_Rotation();
	bool is_enabled(const PS_Base *ps_base);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

class FP_GP_Rotation : public FP_GP {
public:
	FP_GP_Rotation(float angle);
	void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);

protected:
	float a_sin;
	float a_cos;
	float b_sin;
	float b_cos;
};

FP_GP_Rotation::FP_GP_Rotation(float angle) {
	a_sin = ::sin(angle * M_PI / 180.0);
	a_cos = ::cos(angle * M_PI / 180.0);
	b_sin = ::sin(-angle * M_PI / 180.0);
	b_cos = ::cos(-angle * M_PI / 180.0);
}

void FP_GP_Rotation::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	out_x = a_cos * in_x - a_sin * in_y;
	out_y = a_sin * in_x + a_cos * in_y;
}

void FP_GP_Rotation::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	in_x = b_cos * out_x - b_sin * out_y;
	in_y = b_sin * out_x + b_cos * out_y;
}

//------------------------------------------------------------------------------
FP_Rotation::FP_Rotation(void) : FilterProcess_GP() {
	_name = "F_Rotation";
}

FP_Rotation::~FP_Rotation() {
}

bool FP_Rotation::is_enabled(const PS_Base *ps_base) {
	PS_Rotation *ps = (PS_Rotation *)ps_base;
	if(!ps->enabled || ps->rotation_angle == 0.0)
		return false;
	return true;
}

FP_GP *FP_Rotation::get_new_FP_GP(const class FP_GP_data_t &data) {
	const PS_Rotation *ps = (const PS_Rotation *)data.ps_base;
	return new FP_GP_Rotation(ps->rotation_angle);
}

//------------------------------------------------------------------------------
PS_Rotation::PS_Rotation(void) {
	reset();
}

PS_Rotation::~PS_Rotation() {
}

PS_Base *PS_Rotation::copy(void) {
	PS_Rotation *ps = new PS_Rotation;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Rotation::reset(void) {
	enabled = false;
	rotation_angle = 0.0;
//	fold = true;
}

bool PS_Rotation::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("rotation_angle", rotation_angle);
//	dataset->get("fold", fold);
	// check values
	if(rotation_angle > 45.0)
		rotation_angle = 45.0;
	if(rotation_angle < -45.0)
		rotation_angle = -45.0;
	return true;
}

bool PS_Rotation::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("rotation_angle", rotation_angle);
//	dataset->set("fold", fold);
	return true;
}

//------------------------------------------------------------------------------
FP_Rotation *F_Rotation::fp = NULL;

F_Rotation::F_Rotation(int id) : Filter() {
	filter_id = id;
	_id = "F_Rotation";
	_name = tr("Rotation");
	if(fp == NULL)
		fp = new FP_Rotation();
	_ps = (PS_Rotation *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = NULL;
	q_action_precise = NULL;
	reset();
	guide_min_length = 100.0;
}

F_Rotation::~F_Rotation() {
}

PS_Base *F_Rotation::newPS(void) {
	return new PS_Rotation();
}

void F_Rotation::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_Rotation *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	edit_mode_enabled = false;
	edit_active = false;
	if(widget != NULL) {
		reconnect(false);
		bool en = ps->enabled;	// store value
		slider_angle->setValue(ps->rotation_angle);
		ps->enabled = en;	// restore it back
		checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
//		checkbox_fold->setCheckState(ps->fold ? Qt::Checked : Qt::Unchecked);
		reconnect(true);
	}
	if(q_action_precise != NULL) {
		q_action_precise->setChecked(false);
	}
}

QWidget *F_Rotation::controls(QWidget *parent) {
	if(widget != NULL)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(2);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout *hl = new QHBoxLayout(widget);
    hl->setSpacing(2);
    hl->setContentsMargins(0, 0, 0, 0);
	checkbox_enable = new QCheckBox(tr("Enable"));
	hl->addWidget(checkbox_enable);
//	checkbox_fold = new QCheckBox(tr("Fold"));
//	hl->addWidget(checkbox_fold, 0, Qt::AlignRight);
	l->addLayout(hl, 0, 0, 1, -1);

	l->addWidget(new QLabel(tr("Angle")), 1, 0);
	slider_angle = new GuiSlider(-45.0, 45.0, 0.0, 100, 10, 50);
	l->addWidget(slider_angle, 1, 1);

	reconnect(true);
	widget = q;
	return widget;
}
 
void F_Rotation::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(slider_angle, SIGNAL(signal_changed(double)), this, SLOT(changed_angle(double)));
//		connect(checkbox_fold, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_fold(int)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(slider_angle, SIGNAL(signal_changed(double)), this, SLOT(changed_angle(double)));
//		disconnect(checkbox_fold, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_fold(int)));
	}
}

QList<QAction *> F_Rotation::get_actions_list(void) {
	QList<QAction *> l;
	if(q_action_precise == NULL) {
		q_action_precise = new QAction(QIcon(":/resources/rotate_free.svg"), tr("&Rotate"), this);
//		q_action_precise->setShortcut(tr("Ctrl+R"));
		q_action_precise->setStatusTip(tr("Rotate photo"));
		q_action_precise->setCheckable(true);
		connect(q_action_precise, SIGNAL(toggled(bool)), this, SLOT(slot_action_edit(bool)));
	}
	l.push_back(q_action_precise);
	return l;
}

void F_Rotation::edit_mode_exit(void) {
	edit_mode_enabled = false;
	q_action_precise->setChecked(false);
	if(ps->rotation_angle == 0.0) {
		ps->enabled = false;
		checkbox_enable->setCheckState(Qt::Unchecked);
	}
}

void F_Rotation::edit_mode_forced_exit(void) {
	slot_action_edit(false);
}

void F_Rotation::slot_action_edit(bool checked) {
	if(checked == edit_mode_enabled)
		return;
	edit_draw_OSD = false;
	edit_mode_enabled = checked;
	if(edit_mode_enabled && !ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
	}
	if(!edit_mode_enabled && ps->rotation_angle == 0.0) {
		ps->enabled = false;
		checkbox_enable->setCheckState(Qt::Unchecked);
	}
	emit signal_filter_edit(this, edit_mode_enabled, edit_mode_enabled ? Cursor::cross : Cursor::arrow);
	emit_signal_update();
}

void F_Rotation::changed_angle(double value) {
	bool update = (ps->rotation_angle != value);
	if(value != 0.0 && ps->enabled == false) {
		ps->enabled = true;
		update = true;
		checkbox_enable->setCheckState(Qt::Checked);
	}
	if(update) {
		ps->rotation_angle = value;
		emit_signal_update();
	}
}

void F_Rotation::slot_checkbox_enable(int state) {
	// TODO: update cursor - change it to "cross" and back
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(value == false && edit_mode_enabled) {
		ps->enabled = false;
		q_action_precise->setChecked(false);
		update = false;
	}
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

/*
void F_Rotation::slot_checkbox_fold(int state) {
	// TODO: update cursor - change it to "cross" and back
	bool value = (state == Qt::Checked);
	if(ps->fold != value) {
		ps->fold = value;
		emit_signal_update();
	}
}
*/
void F_Rotation::draw(QPainter *painter, FilterEdit_event_t *et) {
	if(!edit_mode_enabled || !edit_active)
		return;
	QSize viewport = et->viewport;
//	QRect image = et->image;
//	image_and_viewport_t transform = et->transform;
	// ignore viewport and image - we need just draw on the top of view area, not as a part of the image
	bool aa = painter->testRenderHint(QPainter::Antialiasing);
	if(!aa)
		painter->setRenderHint(QPainter::Antialiasing, true);
//	painter->setPen(QPen(QColor(255, 63, 63, 255)));
	painter->setPen(QPen(QColor(255, 63, 63, 127)));
	// reset world translation
	QTransform tr_restore;
	tr_restore.translate(0.5, 0.5);	// shift to fix AA artifacts
	painter->setWorldTransform(tr_restore);

	QLineF guide(mouse_start, mouse_position);
	if(guide.length() >= guide_min_length) {
		QPen pens[2] = {
			QPen(QColor(255, 255, 255, 63), 3.0),
			QPen(QColor(0, 0, 0, 127), 1.0),
		};
		for(int i = 0; i < 2; i++) {
			painter->setPen(pens[i]);
//			painter->setPen(QPen(QColor(255, 255, 255, 63)));

			// draw helpers
			QLineF h1(mouse_position, mouse_start);
			h1.setLength(viewport.width() + viewport.height());
			h1.setAngle(guide.angle());
			painter->drawLine(h1);
			h1.setAngle(guide.angle() + 90.0);
			painter->drawLine(h1);
			h1.setAngle(guide.angle() - 90.0);
			painter->drawLine(h1);

			QLineF h2 = guide;
			h2.setLength(viewport.width() + viewport.height());
			h2.setAngle(guide.angle() + 180.0);
			painter->drawLine(h2);
			h2.setAngle(guide.angle() + 90.0);
			painter->drawLine(h2);
			h2.setAngle(guide.angle() - 90.0);
			painter->drawLine(h2);
		}
		painter->setPen(QPen(QColor(63, 255, 63, 255)));
//		painter->setPen(QPen(QColor(63, 255, 63, 127)));

		// TODO: update edit angle information ???
	}
	painter->drawLine(guide);
	// draw text...
	if(edit_draw_OSD) {
		int rx = 10;
		int ry = 10;

		// skipping color
		QColor text_color(0xFF, 0xFF, 0xFF);
		// draw info
		QString str;

		QFont fnt = painter->font();
		fnt.setStyleHint(QFont::Courier);
		fnt.setFamily("Fixed");
//		fnt.setBold(true);
		painter->setFont(fnt);

		QString tr_offset = tr("offset");
		QString tr_angle = tr("new angle");
		int tr_len = tr_offset.length();
		if(tr_len < tr_angle.length())
			tr_len = tr_angle.length();
		str = "%1";
		tr_offset = str.arg(tr_offset, tr_len);
		str = "%1";
		tr_angle = str.arg(tr_angle, tr_len);

		QFontMetrics qfm(painter->font());
		str = tr_offset + ": -00.00";
		QRect bg_rect;
		bg_rect = qfm.boundingRect(str);
		int _w = bg_rect.width();
		int _h = bg_rect.height();
		str = tr_angle + ": -00.00";
		bg_rect = qfm.boundingRect(str);
		if(_w < bg_rect.width())	_w = bg_rect.width();
		bg_rect.setX(rx);
		bg_rect.setY(ry);
		bg_rect.setHeight(_h * 2 + 10);
		bg_rect.setWidth(_w + 10);
		painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter->setPen(QColor(0x0F, 0x0F, 0x0F, 0x9F));
		painter->setBrush(QColor(0x0F, 0x0F, 0x0F, 0x7F));
		painter->drawRoundedRect(bg_rect, 5.0, 5.0);
		painter->setPen(text_color);
		string space = "";

		float angle_offset = edit_OSD_offset;
		float angle_new = edit_OSD_angle;
		str.sprintf("%+.0f", angle_offset);
		if(str.length() == 2)
			space = " ";
		str.sprintf(": %s%+.2f", space.c_str(), angle_offset);
		str = tr_offset + str;
		painter->setPen(QColor(0x00, 0x00, 0x00));
		painter->drawText(QPointF(rx + 5, ry + qfm.height()), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(rx + 5 - 1, ry + qfm.height() - 1), str);

		space = "";
		if(angle_new >= 0)
			space += " ";
		str.sprintf("%+.0f", angle_new);
		if(str.length() == 2)
			space += " ";
		str.sprintf(": %s%.2f", space.c_str(), angle_new);
		str = tr_angle + str;
		painter->setPen(QColor(0x00, 0x00, 0x00));
		painter->drawText(QPointF(rx + 5, ry + qfm.height() * 2), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(rx + 5 - 1, ry + qfm.height() * 2 - 1), str);
	}
	if(!aa)
		painter->setRenderHint(QPainter::Antialiasing, false);
}

//bool F_Rotation::mousePressEvent(QMouseEvent *event, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Rotation::mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {
	QMouseEvent *event = (QMouseEvent *)mt->event;
//	const QSize &viewport = mt->viewport;
//	const QRect &image = mt->image;
	if(!edit_mode_enabled)
		return false;
	bool rez = false;
	_cursor = Cursor::cross;
edit_OSD_offset = 0.0;
edit_OSD_angle = 0.0;
	if(event->button() == Qt::LeftButton) {
		edit_active = true;
		mouse_start = mt->cursor_pos;
//		mouse_start = event->pos();
		mouse_position = mouse_start;
		_cursor = Cursor::cross;
		rez = true;
	}
	return rez;
}

//bool F_Rotation::mouseReleaseEvent(QMouseEvent *event, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Rotation::mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {
//	QMouseEvent *event = (QMouseEvent *)mt->event;
//	const QSize &viewport = mt->viewport;
//	const QRect &image = mt->image;
	// TODO: process only release of the left button
	if(!edit_mode_enabled)
		return false;
	// check what was released left button!
	_cursor = Cursor::cross;
//	_cursor = Cursor::arrow;
	// should throw signal to apply angle
	edit_active = false;
	QLineF guide(mouse_start, mouse_position);
	if(guide.length() >= guide_min_length) {
		slider_angle->setValue(edit_angle_normalize(guide.angle() + ps->rotation_angle));
	}
	edit_draw_OSD = false;
	return true;
}

//bool F_Rotation::mouseMoveEvent(QMouseEvent *event, bool &accepted, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Rotation::mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor) {
	QMouseEvent *event = (QMouseEvent *)mt->event;
//	const QSize &viewport = mt->viewport;
//	const QRect &image = mt->image;
	accepted = true;
	bool rez = false;
	_cursor = Cursor::cross;
	if(event->buttons() & Qt::LeftButton) {
		mouse_position = mt->cursor_pos;
//		mouse_position = event->pos();
		// update current angle/offset for OSD helper
///*
		QLineF guide(mouse_start, mouse_position);
		if(guide.length() >= guide_min_length) {
//			double angle = guide.angle();
			edit_OSD_offset = edit_angle_normalize(guide.angle());
			edit_OSD_angle = edit_angle_normalize(edit_OSD_offset + ps->rotation_angle);
			edit_draw_OSD = true;
		} else {
			edit_draw_OSD = false;
		}
//*/
//		mouse_position.setX(mouse_position.x() - image.x());
//		mouse_position.setY(mouse_position.y() - image.y());
		rez = true;
	}
	return rez;
}

double F_Rotation::edit_angle_normalize(double _angle) {
	while(_angle > 45.0)
		_angle -= 90.0;
	while(_angle < -45.0)
		_angle += 90.0;
	return _angle;
}

bool F_Rotation::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
	if(field_name == "rotation_angle")
		desc->name = tr("rotation angle");
	return true;
}

Filter::type_t F_Rotation::type(void) {
	return Filter::t_geometry;
}

FilterProcess *F_Rotation::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
