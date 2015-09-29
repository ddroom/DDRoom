/*
 * f_process.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 *	TODO:
	- notice all mutators, to correct tiles size asked by tiles_receiver (View);
	- add lanczos method;

 *	NOTES:
 *	- to prevent downscaled cache abuse in workflow, use 'downscaled' rotation - when result image dimensions will be the same as original, w/o magnification.
 *
 */	

#include <iostream>

#include "f_projection.h"
#include "filter_gp.h"
#include "system.h"
#include "gui_slider.h"
#include "ddr_math.h"

using namespace std;

#define FR_MIN_TILE_SIZE 24

//------------------------------------------------------------------------------
class PS_Projection : public PS_Base {

public:
	PS_Projection(void);
	virtual ~PS_Projection();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	double strength;

	double angle_px; // angle per pixel at center of image
	double focal_length_px; // focal length equal to sensor size in pixels
};

//------------------------------------------------------------------------------
class TF_Projection : public TableFunction {
public:
	TF_Projection(float angle_px, float focal_length_px, float min, float max);
	float angle_px;
	float focal_length_px;
protected:
	float function(float x);
};

TF_Projection::TF_Projection(float _angle_px, float _focal_length_px, float _min, float _max) {
	angle_px = _angle_px;
	focal_length_px = _focal_length_px;
	_init(_min, _max, TABLE_FUNCTION_TABLE_SIZE);
}

float TF_Projection::function(float x) {
	return tanf(x * angle_px) * focal_length_px;
}

//------------------------------------------------------------------------------
class FP_Projection : public FilterProcess_GP {
public:
	FP_Projection(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

class FP_Projection_Cache : public FP_Cache_t {
public:
	FP_Projection_Cache(void);
	~FP_Projection_Cache(void);
	float angle_px;
	float focal_length_px;
	TF_Projection *tf_projection;
};

FP_Projection_Cache::FP_Projection_Cache(void) {
	angle_px = 0.0;
	focal_length_px = 0.0;
	tf_projection = NULL;
}

FP_Projection_Cache::~FP_Projection_Cache(void) {
	if(tf_projection != NULL)
		delete tf_projection;
}

class FP_GP_Projection : public FP_GP {
public:
	FP_GP_Projection(const class Metadata *metadata, double strength, FP_Projection_Cache *cache);
	void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);

protected:
	float angle_px;
	float focal_length_px;
	float focal_length_px_f;
	TF_Projection *tf_projection;
};

FP_GP_Projection::FP_GP_Projection(const class Metadata *metadata, double strength, FP_Projection_Cache *cache) {
	double sensor[2];
	sensor[0] = metadata->sensor_mm_width;
	sensor[1] = metadata->sensor_mm_height;
	double focal_length = metadata->lens_focal_length;
	if(focal_length < 0.01) // i.e. equal to zero - unknown
		focal_length = 500.0;
	double _angle = atan((sensor[0] * 0.5) / focal_length);
	if(strength < 0.05)
		strength = 0.05;
	_angle *= strength;
	focal_length = (sensor[0] * 0.5) / tan(_angle);
	if(focal_length < 0.01)		focal_length = 0.01;
	if(focal_length > 500.0)	focal_length = 500.0;

	float w = float(metadata->width) / 2.0;
	angle_px = atan(((0.5 * sensor[0]) / focal_length) * (1.0 / w));
//	focal_length_px = w * (focal_length / (0.5 * sensor[0]));
	focal_length_px = w * (focal_length / (0.5 * sensor[0]));
	focal_length_px_f = (0.5 * sensor[0]) / (w * focal_length);
	double mw = double(metadata->width) / 2.0;
	double mh = double(metadata->height) / 2.0;
	double len = sqrt(mw * mw + mh * mh) * 1.2;

	TF_Projection *tf = cache->tf_projection;
	if(tf == NULL || (cache->angle_px != angle_px || cache->focal_length_px != focal_length_px)) {
		if(tf != NULL)
			delete tf;
		tf = new TF_Projection(angle_px, focal_length_px, -len, len);
		cache->tf_projection = tf;
		cache->angle_px = angle_px;
		cache->focal_length_px = focal_length_px;
	}
	tf_projection = cache->tf_projection;
}

void FP_GP_Projection::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	out_x = atanf(in_x * focal_length_px_f) / angle_px;
	out_y = atanf(in_y * focal_length_px_f) / angle_px;
}

void FP_GP_Projection::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	// use interpolation of precalculated values: i.e. class TableFunction
	in_x = (*tf_projection)(out_x);
	in_y = (*tf_projection)(out_y);
	// use plain function call
//	in_x = tanf(out_x * angle_px) * focal_length_px;
//	in_y = tanf(out_y * angle_px) * focal_length_px;
}

FP_Projection::FP_Projection(void) : FilterProcess_GP() {
	_name = "F_Projection";
}

bool FP_Projection::is_enabled(const PS_Base *ps_base) {
	const PS_Projection *ps = (const PS_Projection *)ps_base;
	if(!ps->enabled)
		return false;
	if(ps->strength == 0.0)
		return false;
	return true;
}

FP_Cache_t *FP_Projection::new_FP_Cache(void) {
	return new FP_Projection_Cache();
}

FP_GP *FP_Projection::get_new_FP_GP(const class FP_GP_data_t &data) {
	const PS_Projection *ps = (const PS_Projection *)data.ps_base;
	return new FP_GP_Projection(data.metadata, ps->strength, (FP_Projection_Cache *)data.cache);
}

//------------------------------------------------------------------------------
PS_Projection::PS_Projection(void) {
	reset();
}

PS_Projection::~PS_Projection() {
}

PS_Base *PS_Projection::copy(void) {
	PS_Projection *ps = new PS_Projection;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Projection::reset(void) {
	enabled = false;
	strength = 0.75;
	angle_px = 0.0;
	focal_length_px = 0.0;
}

bool PS_Projection::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("strength", strength);
	return true;
}

bool PS_Projection::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("strength", strength);
	return true;
}

//------------------------------------------------------------------------------
FP_Projection *F_Projection::fp = NULL;

F_Projection::F_Projection(int id) : Filter() {
	filter_id = id;
	_id = "F_Projection";
	_name = tr("Projection");
	if(fp == NULL)
		fp = new FP_Projection();
	_ps = (PS_Projection *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = NULL;
	q_action_precise = NULL;
	reset();
	guide_min_length = 100.0;
}

F_Projection::~F_Projection() {
}

PS_Base *F_Projection::newPS(void) {
	return new PS_Projection();
}

void F_Projection::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_Projection *)new_ps;
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
		checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
		slider_strength->setValue(ps->strength);
		reconnect(true);
	}
	if(q_action_precise != NULL) {
		q_action_precise->setChecked(false);
	}
}

QWidget *F_Projection::controls(QWidget *parent) {
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
	l->addLayout(hl, 0, 0, 1, -1);

	QLabel *label_strength = new QLabel(tr("Strength"));
	l->addWidget(label_strength, 1, 0);
	slider_strength = new GuiSlider(0.0, 1.2, 1.0, 100, 100, 100);
	l->addWidget(slider_strength, 1, 1);

	reconnect(true);
	widget = q;
	return widget;
}
 
void F_Projection::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
	}
}

QList<QAction *> F_Projection::get_actions_list(void) {
	QList<QAction *> l;
/*
	if(q_action_precise == NULL) {
		q_action_precise = new QAction(QIcon(":/resources/rotate_free.svg"), tr("&Rotate"), this);
//		q_action_precise->setShortcut(tr("Ctrl+R"));
		q_action_precise->setStatusTip(tr("Rotate photo"));
		q_action_precise->setCheckable(true);
		connect(q_action_precise, SIGNAL(toggled(bool)), this, SLOT(slot_action_edit(bool)));
	}
	l.push_back(q_action_precise);
*/
	return l;
}

void F_Projection::edit_mode_exit(void) {
/*
	edit_mode_enabled = false;
	q_action_precise->setChecked(false);
	if(ps->rotation_angle == 0.0) {
		ps->enabled = false;
		checkbox_enable->setCheckState(Qt::Unchecked);
	}
*/
}

void F_Projection::edit_mode_forced_exit(void) {
//	slot_action_edit(false);
}

void F_Projection::slot_action_edit(bool checked) {
/*
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
*/
}

void F_Projection::slot_checkbox_enable(int state) {
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

void F_Projection::slot_changed_strength(double value) {
	bool update = (ps->strength != value);
	if(!ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		ps->strength = value;
		emit_signal_update();
	}
}

void F_Projection::draw(QPainter *painter, FilterEdit_event_t *et) {
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

//bool F_Projection::mousePressEvent(QMouseEvent *event, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Projection::mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {
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

//bool F_Projection::mouseReleaseEvent(QMouseEvent *event, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Projection::mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {
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
//		slider_angle->setValue(edit_angle_normalize(guide.angle() + ps->rotation_angle));
	}
	edit_draw_OSD = false;
	return true;
}

//bool F_Projection::mouseMoveEvent(QMouseEvent *event, bool &accepted, Cursor::cursor &_cursor, const QSize &viewport, const QRect &image) {
bool F_Projection::mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor) {
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
//			edit_OSD_angle = edit_angle_normalize(edit_OSD_offset + ps->rotation_angle);
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

double F_Projection::edit_angle_normalize(double _angle) {
	while(_angle > 45.0)
		_angle -= 90.0;
	while(_angle < -45.0)
		_angle += 90.0;
	return _angle;
}

bool F_Projection::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
//	if(field_name == "rotation_angle")
//		desc->name = tr("rotation angle");
	return true;
}

Filter::type_t F_Projection::type(void) {
	return Filter::t_geometry;
}

FilterProcess *F_Projection::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
