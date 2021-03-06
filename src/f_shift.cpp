/*
 * f_shift.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
	TODO:
	- flexible and convenient FilterEdit UI;
 */	

#include <iostream>

#include "f_shift.h"
#include "filter_gp.h"
#include "system.h"
#include "gui_slider.h"
#include "ddr_math.h"

using namespace std;

//------------------------------------------------------------------------------
class FP_Shift : public FilterProcess_GP {
public:
	FP_Shift(void);
	virtual ~FP_Shift();
	bool is_enabled(const PS_Base *ps_base);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

//------------------------------------------------------------------------------
class FP_GP_Shift : public FP_GP {
public:
	FP_GP_Shift(const class Metadata *metadata, double angle_v, double angle_h, double angle_r);
	void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);
protected:
	float z_0;
	// matrix of photo plane rotation, and inverted
	float m3_plane_rotation[9];
	float m3_plane_rotation_inverted[9];
	// normal to rotated plane
	float pn_x;
	float pn_y;
	float pn_z;
};

//FP_GP_Shift::shift_point_t(double _focal_length, double _sensor_width, double _sensor_height, double _angle_v, double _angle_h, double _angle_r, float _x_max, float _y_max) {
FP_GP_Shift::FP_GP_Shift(const class Metadata *metadata, double angle_v, double angle_h, double angle_r) {
//cerr << "metadata->width == " << metadata->width << endl;
//cerr << "metadata->height == " << metadata->height << endl;
	float x_max = float(metadata->width) / 2.0;
//	float y_max = float(metadata->height) / 2.0;
	float focal_length = metadata->lens_focal_length;
	if(focal_length < 0.1)
		focal_length = 50.0;
//cerr << "metadata->focal_length == " << metadata->lens_focal_length << endl;
	// TODO: use DB with metadata->camera_make & metadata->camera_model to determine the real sensor size
	double sensor_width = metadata->sensor_mm_width; // sensor size, in millimeters
//	double sensor_width = 22.2; // sensor size, in millimeters
//	double sensor_height = 14.8; // Canon 1.6x size
/*
	angle_v = _angle_v;
	angle_h = _angle_h;
	angle_r = _angle_r;
	x_max = _x_max;
	y_max = _y_max;
*/
//cerr << " new FP_GP_Shift: angle_v == " << angle_v << "; angle_h == " << angle_h << "; angle_r == " << angle_r << endl;
	float angle_v_rads = angle_v * M_PI / 180.0;
	float angle_h_rads = angle_h * M_PI / 180.0;
	float angle_r_rads = angle_r * M_PI / 180.0;
	// calculate coefficients
	z_0 = (x_max * focal_length) / (sensor_width * 0.5);
	// added horizontal angle
	// rotation
	float mv[9];
	float mh[9];
	float mr[9];
	for(int i = 0; i < 9; ++i) {
		mv[i] = 0.0;
		mh[i] = 0.0;
		mr[i] = 0.0;
	}
	float v_cos = cosf(angle_v_rads);
	float v_sin = sinf(angle_v_rads);
	float h_cos = cosf(angle_h_rads);
	float h_sin = sinf(angle_h_rads);
	float r_cos = cosf(angle_r_rads);
	float r_sin = sinf(angle_r_rads);

	// right-hand system rotation about X axis for vertical and Y axis for horizontal angles
	// vertical
	mv[0] = 1.0;
	mv[4] =  v_cos;
	mv[5] =  v_sin;
	mv[7] = -v_sin;
	mv[8] =  v_cos;
	// horizontal
	mh[0] =  h_cos;
	mh[2] = -h_sin;
	mh[4] = 1.0;
	mh[6] =  h_sin;
	mh[8] =  h_cos;
	// about Z axis
	mr[0] =  r_cos;
	mr[1] =  r_sin;
	mr[3] = -r_sin;
	mr[4] =  r_cos;
	mr[8] = 1.0;
	//
	float m3_t[9];
//	m3_m3_mult(m3_t, mh, mv);
	m3_m3_mult(m3_t, mv, mh);
	m3_m3_mult(m3_plane_rotation, m3_t, mr);
	m3_invert(m3_plane_rotation_inverted, m3_plane_rotation);
	// normal to rotated plane
	double p_vector[3];
	p_vector[0] = 0.0;
	p_vector[1] = 0.0;
	p_vector[2] = z_0;
	double p_normal[3];
	m3_v3_mult(p_normal, m3_plane_rotation, p_vector);
	pn_x = p_normal[0];
	pn_y = p_normal[1];
	pn_z = p_normal[2];
}

// forward projection used to calculate output size of photo and tiles
void FP_GP_Shift::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	const float s = (pn_z * z_0) / (pn_x * in_x + pn_y * in_y + pn_z * z_0);
	float p_3d[3];
	p_3d[0] = s * in_x;
	p_3d[1] = s * in_y;
	p_3d[2] = s * z_0 - z_0;
	float p_2d[3];
	m3_v3_mult(p_2d, m3_plane_rotation_inverted, p_3d);
	out_x = p_2d[0];
	out_y = p_2d[1];
}

// backward projection used to calculate actual output pixels (coordinates for interpolation)
void FP_GP_Shift::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	float p_2d[3];
	p_2d[0] = out_x;
	p_2d[1] = out_y;
	p_2d[2] = 0.0;
	float p_3d[3];
	m3_v3_mult(p_3d, m3_plane_rotation, p_2d);
	p_3d[2] += z_0;
	in_x = (p_3d[0] * z_0) / p_3d[2];
	in_y = (p_3d[1] * z_0) / p_3d[2];
}

//------------------------------------------------------------------------------
class PS_Shift : public PS_Base {
public:
	PS_Shift(void);
	virtual ~PS_Shift();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);
	void map_ps_to_ui(double &angle_ui_v, double &angle_ui_h);
	void map_ui_to_ps(double angle_ui_v, double angle_ui_h);

	bool enabled;
	double angle_v; // vertical and horizontal angled
	double angle_h; // +/- 60 degree
	double angle_r; // rotation
	int cw_rotation;

	void guide_reset(int guide_n);
	bool guide_undefined(int guide_n);
	float guide_first[4];
	float guide_second[4];
};

//------------------------------------------------------------------------------
PS_Shift::PS_Shift(void) {
	reset();
}

PS_Shift::~PS_Shift() {
}

PS_Base *PS_Shift::copy(void) {
	PS_Shift *ps = new PS_Shift;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Shift::reset(void) {
	enabled = false;
	angle_v = 0.0;
	angle_h = 0.0;
	angle_r = 0.0;
	cw_rotation = 0;
	guide_reset(0);
	guide_reset(1);
}

bool PS_Shift::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("angle_v", angle_v);
	dataset->get("angle_h", angle_h);
	dataset->get("angle_r", angle_r);
	//--
	QVector<float> guide(4);
	for(int i = 0; i < 4; ++i)	guide[i] = 0.0;
	dataset->get("guide_first", guide);
	for(int i = 0; i < 4; ++i)	guide_first[i] = guide[i];
	for(int i = 0; i < 4; ++i)	guide[i] = 0.0;
	dataset->get("guide_second", guide);
	for(int i = 0; i < 4; ++i)	guide_second[i] = guide[i];
	//--
	return true;
}

bool PS_Shift::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("angle_v", angle_v);
	dataset->set("angle_h", angle_h);
	dataset->set("angle_r", angle_r);
	//--
	QVector<float> guide(4);
	for(int i = 0; i < 4; ++i)	guide[i] = guide_first[i];
	dataset->set("guide_first", guide);
	for(int i = 0; i < 4; ++i)	guide[i] = guide_second[i];
	dataset->set("guide_second", guide);
	return true;
}

void PS_Shift::guide_reset(int guide_n) {
	float *g = (guide_n == 0) ? guide_first : guide_second;
	for(int i = 0; i < 4; ++i)
		g[i] = 0.0;
}

bool PS_Shift::guide_undefined(int guide_n) {
	float *g = (guide_n == 0) ? guide_first : guide_second;
	bool rez = false;
	for(int i = 0; i < 4; ++i)
		rez |= (g[i] != 0.0);
	return !rez;
}

void PS_Shift::map_ps_to_ui(double &angle_ui_v, double &angle_ui_h) {
	angle_ui_v = angle_v;
	angle_ui_h = angle_h;
	if(cw_rotation == 90) {
		angle_ui_v = -angle_h;
		angle_ui_h = angle_v;
	}
	if(cw_rotation == 180) {
		angle_ui_v = -angle_v;
		angle_ui_h = -angle_h;
	}
	if(cw_rotation == 270) {
		angle_ui_v = angle_h;
		angle_ui_h = -angle_v;
	}
}

void PS_Shift::map_ui_to_ps(double angle_ui_v, double angle_ui_h) {
	angle_v = angle_ui_v;
	angle_h = angle_ui_h;
	if(cw_rotation == 90) {
		angle_v = angle_ui_h;
		angle_h = -angle_ui_v;
	}
	if(cw_rotation == 180) {
		angle_v = -angle_ui_v;
		angle_h = -angle_ui_h;
	}
	if(cw_rotation == 270) {
		angle_v = -angle_ui_h;
		angle_h = angle_ui_v;
	}
}

//------------------------------------------------------------------------------
FP_Shift *F_Shift::fp = nullptr;

F_Shift::F_Shift(int id) : Filter() {
	filter_id = id;
	_id = "F_Shift";
	_name = tr("Shift");
	if(fp == nullptr)
		fp = new FP_Shift();
	_ps = (PS_Shift *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	q_action_edit_shift = nullptr;
	reset();
	guide_min_length = 100.0;
}

F_Shift::~F_Shift() {
}

PS_Base *F_Shift::newPS(void) {
	return new PS_Shift();
}

void F_Shift::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
/*
if(args.metadata != nullptr)
cerr << "F_Shift::set_PS_and_FS(); metadata->rotation == " << args.metadata->rotation << endl;
cerr << "F_Shift::set_PS_and_FS(); args.cw_rotation == " << args.cw_rotation << endl;
*/
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Shift *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	ps->cw_rotation = args.cw_rotation;
	// FS
	edit_mode_enabled = false;
	edit_active = false;
	if(widget != nullptr) {
		reconnect(false);
		checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
		double angle_ui_v = 0.0;
		double angle_ui_h = 0.0;
		ps->map_ps_to_ui(angle_ui_v, angle_ui_h);
		slider_angle_v->setValue(angle_ui_v);
		slider_angle_h->setValue(angle_ui_h);
//		slider_angle_v->setValue(ps->angle_v);
//		slider_angle_h->setValue(ps->angle_h);
		slider_angle_r->setValue(ps->angle_r);
		reconnect(true);
	}
	if(q_action_edit_shift != nullptr)
		q_action_edit_shift->setChecked(false);
}

QWidget *F_Shift::controls(QWidget *parent) {
	D_GUI_THREAD_CHECK
	if(widget != nullptr)
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

	QLabel *label_angle_v = new QLabel(tr("Vertical"));
	l->addWidget(label_angle_v, 1, 0);
	slider_angle_v = new GuiSlider(-60.0, 60.0, 0.0, 100, 2, 20);
	slider_angle_v->getSpinBox()->setSingleStep(0.5);
	l->addWidget(slider_angle_v, 1, 1);

	QLabel *label_angle_h = new QLabel(tr("Horizontal"));
	l->addWidget(label_angle_h, 2, 0);
	slider_angle_h = new GuiSlider(-60.0, 60.0, 0.0, 100, 2, 20);
	slider_angle_h->getSpinBox()->setSingleStep(0.5);
	l->addWidget(slider_angle_h, 2, 1);

	QLabel *label_angle_r = new QLabel(tr("Rotation"));
	l->addWidget(label_angle_r, 3, 0);
//	slider_angle_r = new GuiSlider(-45.0, 45.0, 0.0, 100, 1, 10);
	slider_angle_r = new GuiSlider(-45.0, 45.0, 0.0, 100, 10, 50);
	slider_angle_r->getSpinBox()->setSingleStep(0.5);
	l->addWidget(slider_angle_r, 3, 1);

	reconnect(true);
	widget = q;
	return widget;
}
 
void F_Shift::reconnect(bool to_connect) {
	D_GUI_THREAD_CHECK
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(slider_angle_v, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_v(double)));
		connect(slider_angle_h, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_h(double)));
		connect(slider_angle_r, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_r(double)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(slider_angle_v, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_v(double)));
		disconnect(slider_angle_h, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_h(double)));
		disconnect(slider_angle_r, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_angle_r(double)));
	}
}

QList<QAction *> F_Shift::get_actions_list(void) {
	D_GUI_THREAD_CHECK
	QList<QAction *> l;
	if(q_action_edit_shift == nullptr) {
		q_action_edit_shift = new QAction(QIcon(":/resources/shift_v.svg"), tr("Shift"), this);
//		q_action_edit_shift->setShortcut(tr("Ctrl+R"));
		q_action_edit_shift->setStatusTip(tr("Compensate shift of the camera at the shooting time"));
		q_action_edit_shift->setCheckable(true);
		connect(q_action_edit_shift, SIGNAL(toggled(bool)), this, SLOT(slot_action_edit_shift(bool)));
	}
	l.push_back(q_action_edit_shift);
//	if(q_action_edit_... == nullptr) {
//		...
//	}
//	l.push_back(q_action_edit_...);
	return l;
}

void F_Shift::edit_mode_exit(void) {
	D_GUI_THREAD_CHECK
	edit_mode_enabled = false;
	q_action_edit_shift->setChecked(false);
//	q_action_edit_...->setChecked(false);
	if(ps->angle_v == 0.0 && ps->angle_h == 0.0 && ps->angle_r == 0.0) {
		ps->enabled = false;
		checkbox_enable->setCheckState(Qt::Unchecked);
	}
}

void F_Shift::edit_mode_forced_exit(void) {
	D_GUI_THREAD_CHECK
	slot_action_edit_shift(false);
//	slot_action_edit_...(false);
}

// update horizontal and vertical angles so result of operation would be unchanged
void F_Shift::set_cw_rotation(int cw_rotation) {
	D_GUI_THREAD_CHECK
//	cerr << "F_Shift::set_cw_rotation( " << cw_rotation << " );" << endl;
	ps->cw_rotation = cw_rotation;
	double angle_ui_v;
	double angle_ui_h;
	ps->map_ps_to_ui(angle_ui_v, angle_ui_h);
	reconnect(false);
	slider_angle_v->setValue(angle_ui_v);
	slider_angle_h->setValue(angle_ui_h);
	reconnect(true);
}

void F_Shift::slot_action_edit_shift(bool checked) {
	D_GUI_THREAD_CHECK
	if(checked == edit_mode_enabled)
		return;
//	edit_mode_shift = checked;
//	edit_mode_horizontal = false;
	fn_action_edit(checked);
}

void F_Shift::fn_action_edit(bool checked) {
	D_GUI_THREAD_CHECK
	edit_draw_OSD = false;
	edit_mode_enabled = checked;
	if(edit_mode_enabled && !ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		edit_mode_scratch = true;
//		ps->guide_reset(0);
//		ps->guide_reset(1);
	}
	if(!edit_mode_enabled && ps->angle_v == 0.0 && ps->angle_h == 0.0 && ps->angle_r == 0.0) {
		ps->enabled = false;
		checkbox_enable->setCheckState(Qt::Unchecked);
	}
	emit signal_filter_edit(this, edit_mode_enabled, edit_mode_enabled ? Cursor::cross : Cursor::arrow);
	emit_signal_update();
}

void F_Shift::slot_checkbox_enable(int state) {
	D_GUI_THREAD_CHECK
	// TODO: update cursor - change it to "cross" and back
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(value == false && edit_mode_enabled) {
		ps->enabled = false;
		q_action_edit_shift->setChecked(false);
//		q_action_edit_...->setChecked(false);
		update = false;
	}
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_Shift::slot_changed_angle_v(double value) {
	D_GUI_THREAD_CHECK
	double angle_ui_v = value;
	double angle_ui_h = slider_angle_h->value();
	double angle_v, angle_h;
	ps->map_ps_to_ui(angle_v, angle_h);
	//--
	bool update = (angle_v != angle_ui_v || angle_h != angle_ui_h);
	if(!ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		ps->map_ui_to_ps(angle_ui_v, angle_ui_h);
		emit_signal_update();
	}
}

void F_Shift::slot_changed_angle_h(double value) {
	D_GUI_THREAD_CHECK
	double angle_ui_v = slider_angle_v->value();
	double angle_ui_h = value;
	double angle_v, angle_h;
	ps->map_ps_to_ui(angle_v, angle_h);
	//--
	bool update = (angle_v != angle_ui_v || angle_h != angle_ui_h);
	if(!ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		ps->map_ui_to_ps(angle_ui_v, angle_ui_h);
		emit_signal_update();
	}
}

void F_Shift::slot_changed_angle_r(double value) {
	D_GUI_THREAD_CHECK
	bool update = (ps->angle_r != value);
	if(!ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		ps->angle_r = value;
		emit_signal_update();
	}
}

void F_Shift::draw(QPainter *painter, FilterEdit_event_t *et) {
	D_GUI_THREAD_CHECK
//	if(!edit_mode_enabled || !edit_active)
	if(!edit_mode_enabled)
		return;
	QSize viewport = et->viewport;
//	QRect image = et->image;
//	image_and_viewport_t transform = et->transform;
	// ignore viewport and image - we need just draw on the top of view area, not as a part of the image
	bool aa = painter->testRenderHint(QPainter::Antialiasing);
	if(!aa)
		painter->setRenderHint(QPainter::Antialiasing, true);
//	painter->setPen(QPen(QColor(255, 63, 63, 255)));
//	painter->setPen(QPen(QColor(255, 63, 63, 127)));
	// reset world translation
	QTransform tr_restore;
	tr_restore.translate(0.5, 0.5);	// shift to fix AA artifacts
	painter->setWorldTransform(tr_restore);

	float *guides[2];
	guides[0] = ps->guide_first;
	guides[1] = ps->guide_second;
	for(int i = 0; i < 2; ++i) {
		if(ps->guide_undefined(i))
			continue;
		float vp[4];
		for(int j = 0; j < 2; ++j) {
			int off = j * 2;
			float im[2];
			et->transform.photo_to_image_f(im[0], im[1], guides[i][0 + off], guides[i][1 + off]);
			et->transform.image_to_viewport_f(vp[0 + off], vp[1 + off], im[0], im[1]);
		}
/*
		if(vp[0] != vp[2]) {
			float a = (vp[3] - vp[1]) / (vp[2] - vp[0]);
			float b = vp[1] - guide_a * vp[0];

			painter->setPen(QPen(QColor(63, 127, 63, 127)));
			painter->drawLine(QLineF(vp[0], vp[1], vp[2], vp[3]));
		}
*/
		painter->setPen(QPen(QColor(63, 191, 63, 191)));
		painter->drawLine(QLineF(vp[0], vp[1], vp[2], vp[3]));
	}
	// edit_active section
	if(!edit_active) {
		if(!aa)
			painter->setRenderHint(QPainter::Antialiasing, false);
		return;
	}
	// draw edit guide if any
	painter->setPen(QPen(QColor(255, 63, 63, 127)));
	QLineF guide(mouse_start, mouse_position);
	if(guide.length() >= guide_min_length) {
		QPen pens[2] = {
			QPen(QColor(255, 255, 255, 63), 3.0),
			QPen(QColor(0, 0, 0, 127), 1.0),
		};
		for(int i = 0; i < 2; ++i) {
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
//		painter->setPen(QPen(QColor(63, 255, 63, 255)));
		painter->setPen(QPen(QColor(63, 191, 63, 191)));
		// TODO: update edit angle information ???
	}
	painter->drawLine(guide);
	// draw text...
//	if(edit_draw_OSD) {
	if(false) {
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

bool F_Shift::mousePressEvent(FilterEdit_event_t *et, Cursor::cursor &_cursor) {
	QMouseEvent *event = (QMouseEvent *)et->event;
//	const QSize &viewport = et->viewport;
//	const QRect &image = et->image;
	if(!edit_mode_enabled)
		return false;
	bool rez = false;
	_cursor = Cursor::cross;
edit_OSD_offset = 0.0;
edit_OSD_angle = 0.0;
	if(event->button() == Qt::LeftButton) {
		edit_active = true;
		mouse_start = et->cursor_pos;
//		mouse_start = event->pos();
		mouse_position = mouse_start;
		_cursor = Cursor::cross;
		rez = true;
		if(event->modifiers() & Qt::ControlModifier) {
			edit_mode_scratch = true;
			// reset all exist guides to prevent draw of them
			ps->guide_reset(0);
			ps->guide_reset(1);
		} else {
			if(!ps->guide_undefined(0) && !ps->guide_undefined(1)) {
				float distance[2];
				float *guides[2];
				guides[0] = ps->guide_first;
				guides[1] = ps->guide_second;
				const float x0 = et->cursor_pos.x();
				const float y0 = et->cursor_pos.y();
				QPointF mp(et->cursor_pos);
				for(int i = 0; i < 2; ++i) {
					float x1, y1;
					float x2, y2;
					float im[2];
					et->transform.photo_to_image_f(im[0], im[1], guides[i][0], guides[i][1]);
					et->transform.image_to_viewport_f(x1, y1, im[0], im[1]);
					et->transform.photo_to_image_f(im[0], im[1], guides[i][2], guides[i][3]);
					et->transform.image_to_viewport_f(x2, y2, im[0], im[1]);
					distance[i] = ddr::abs((y2 - y1) * x0 - (x2 - x1) * y0 + x2 * y1 - y2 * x1) / sqrtf((y2 - y1) * (y2 - y1) + (x2 - x1) * (x2 - x1));
				}
				int closest_guide = distance[0] < distance[1] ? 0 : 1;
				ps->guide_reset(closest_guide);
			}
		}
	}
	return rez;
}

bool F_Shift::mouseReleaseEvent(FilterEdit_event_t *et, Cursor::cursor &_cursor) {
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
//		if(event->modifiers() & Qt::ControlModifier) {
		// process a new line
		edit_UI_process_guide(guide, et);
//		slider_angle->setValue(edit_angle_normalize(guide.angle() + ps->rotation_angle));
	}
	edit_draw_OSD = false;
	return true;
}

float angle(class Metadata *metadata, double angle_v, double angle_h, double angle_r, float *guide_0, float *guide_1) {
	float g0[4];
	float g1[4];
	FP_GP_Shift shift(metadata, angle_v, angle_h, angle_r);
//cerr << "angle_v == " << angle_v << endl;
	shift.process_forward(guide_0[0], guide_0[1], g0[0], g0[1]);
	shift.process_forward(guide_0[2], guide_0[3], g0[2], g0[3]);
	if(guide_1 != nullptr) {
		shift.process_forward(guide_1[0], guide_1[1], g1[0], g1[1]);
		shift.process_forward(guide_1[2], guide_1[3], g1[2], g1[3]);
	} else {
		g1[0] = g0[0];
		g1[1] = g0[1];
		g1[2] = g0[0];
		g1[3] = g0[1] + ((g0[3] - g0[1]) < 0.0f ? -50.0f : 50.0f);
	}
	// calculate angle
	float u0 = g0[2] - g0[0];
	float u1 = g0[3] - g0[1];
	float v0 = g1[2] - g1[0];
	float v1 = g1[3] - g1[1];
	float lu = sqrtf(u0 * u0 + u1 * u1);
	float lv = sqrtf(v0 * v0 + v1 * v1);
	u0 /= lu; u1 /= lu;
	v0 /= lv; v1 /= lv;
	float dot_product = u0 * v0 + u1 * v1;
//	float cos_a = dot_product / (lu * lv);
	float cos_a = dot_product;
//cerr << "cos_a == " << cos_a << endl;
	float angle = acosf(cos_a);
	angle = (angle / M_PI) * 180.0f;
	return angle;
}

void F_Shift::edit_UI_process_guide(QLineF guide, FilterEdit_event_t *et) {
	bool is_first_guide = ps->guide_undefined(0);
//cerr << "is_first_guide == " << is_first_guide << endl;
	float *ps_guide = is_first_guide ? ps->guide_first : ps->guide_second;
	float vp[4];
	vp[0] = guide.x1();
	vp[1] = guide.y1();
	vp[2] = guide.x2();
	vp[3] = guide.y2();
	for(int j = 0; j < 2; ++j) {
		int off = j * 2;
		float im[2];
		et->transform.viewport_to_image_f(im[0], im[1], vp[0 + off], vp[1 + off]);
		et->transform.image_to_photo_f(ps_guide[0 + off], ps_guide[1 + off], im[0], im[1]);
//cerr << "image[" << j << "] at " << im[0] << " - " << im[1] << endl;
	}
	if(!ps->guide_undefined(0) && !ps->guide_undefined(1) && et->metadata != nullptr) {
		cerr << "!!! process guides !!!" << endl;
		double angle_v = 0.0f;
		double angle_h = 0.0f;
		double angle_r = 0.0f;
		float guide_first[4];
		float guide_second[4];
		for(int i = 0; i < 4; ++i) {
			guide_first[i] = ps->guide_first[i];
			guide_second[i] = ps->guide_second[i];
		}
		float original_angle = angle(et->metadata, angle_v, angle_h, angle_r, guide_first, guide_second);
		if(ddr::abs(original_angle) > 90.0f) {
			guide_second[0] = ps->guide_second[2];
			guide_second[1] = ps->guide_second[3];
			guide_second[2] = ps->guide_second[0];
			guide_second[3] = ps->guide_second[1];
		}
		float a_delta = 60.0f * 1.2f;
//cerr << "a_prev == " << a_prev << endl;
		// - use a real selection with decreased grain
		// - add rotation correction
		while(true) {
			float a_minus = angle(et->metadata, angle_v - a_delta, angle_h, angle_r, guide_first, guide_second);
			float a_plus = angle(et->metadata, angle_v + a_delta, angle_h, angle_r, guide_first, guide_second);
			a_delta *= 0.66f;
			if(a_minus < a_plus) {
				angle_v -= a_delta;
			} else {
				angle_v += a_delta;
			}
			if(a_delta < 0.001f)
				break;
		}
cerr << "angle_v == " << angle_v << "; a_delta == " << a_delta << endl;
/*
		float guide_v[4];
		guide_v[0] = guide_first[0];
		guide_v[1] = guide_first[1];
		guide_v[2] = guide_first[0];
		guide_v[3] = guide_first[1] + (((guide_first[3] - guide_first[1]) > 0.0f) ? 100.0f : -100.0f);
*/
		angle_r = -angle(et->metadata, angle_v, angle_h, angle_r, guide_first, nullptr);
cerr << "angle_r == " << angle_r << endl;
cerr << endl;
	}
/*
	cerr << "ps_guide[0] == " << ps_guide[0] << endl;
	cerr << "ps_guide[1] == " << ps_guide[1] << endl;
	cerr << "ps_guide[2] == " << ps_guide[2] << endl;
	cerr << "ps_guide[3] == " << ps_guide[3] << endl;
cerr << endl;
*/
}

bool F_Shift::mouseMoveEvent(FilterEdit_event_t *et, bool &accepted, Cursor::cursor &_cursor) {
	QMouseEvent *event = (QMouseEvent *)et->event;
//	const QSize &viewport = et->viewport;
//	const QRect &image = et->image;
	accepted = true;
	bool rez = false;
	_cursor = Cursor::cross;
	if(event->buttons() & Qt::LeftButton) {
		mouse_position = et->cursor_pos;
//		int im_x, im_y;
//		et->transform.viewport_to_image(im_x, im_y, mouse_position.x(), mouse_position.y());
//cerr << im_x << " - " << im_y << endl;
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

double F_Shift::edit_angle_normalize(double _angle) {
	while(_angle > 45.0)
		_angle -= 90.0;
	while(_angle < -45.0)
		_angle += 90.0;
	return _angle;
}

bool F_Shift::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
//	if(field_name == "rotation_angle")
//		desc->name = tr("rotation angle");
	return true;
}

Filter::type_t F_Shift::type(void) {
	return Filter::t_geometry;
}

FilterProcess *F_Shift::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
FP_Shift::FP_Shift(void) : FilterProcess_GP() {
	_name = "F_Shift";
}

FP_Shift::~FP_Shift() {
}

bool FP_Shift::is_enabled(const PS_Base *ps_base) {
	const PS_Shift *ps = (const PS_Shift *)ps_base;
	if(!ps->enabled)
		return false;
//	if(ps->angle_v == 0.0 && ps->angle_h == 0.0 && ps->angle_r == 0.0)
//		return false;
	return true;
}

FP_GP *FP_Shift::get_new_FP_GP(const class FP_GP_data_t &data) {
	const PS_Shift *ps = (const PS_Shift *)data.ps_base;
	return new FP_GP_Shift(data.metadata, ps->angle_v, ps->angle_h, ps->angle_r);
//	return new 
}
//------------------------------------------------------------------------------
