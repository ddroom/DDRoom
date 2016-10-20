/*
 * f_crop.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
NOTE:
	- filter shouldn't apply crop at the edit time;
	- crop coordinates are in inclusive form as for px_size == 1.0;
	- on 'scale to size':
		- disable scaling at the UI editing time;
		- apply scaling only on the result of the crop operation;
*/

#include <iostream>
#include <sstream>
#include <math.h>
#include <utility>

#include "f_crop.h"
#include "filter_gp.h"
#include "ddr_math.h"

#define _CROP_MOVE_UNDEFINED	(1 << 0)
#define _CROP_MOVE_PAN		(1 << 1)
#define _CROP_MOVE_SCRATCH	(1 << 2)
#define _CROP_MOVE_LEFT		(1 << 3)
#define _CROP_MOVE_RIGHT	(1 << 4)
#define _CROP_MOVE_TOP		(1 << 5)
#define _CROP_MOVE_BOTTOM	(1 << 6)
#define _CROP_MOVE_LT		(_CROP_MOVE_LEFT | _CROP_MOVE_TOP)
#define _CROP_MOVE_LB		(_CROP_MOVE_LEFT | _CROP_MOVE_BOTTOM)
#define _CROP_MOVE_RT		(_CROP_MOVE_RIGHT | _CROP_MOVE_TOP)
#define _CROP_MOVE_RB		(_CROP_MOVE_RIGHT | _CROP_MOVE_BOTTOM)

#define EDIT_RESIZE_CORNERS	15
#define EDIT_RESIZE_WIDTH	5

/*
#define _ASPECT_MAX 10.0
#define _ASPECT_MIN 0.1
*/
#define _ASPECT_MAX 50.0
#define _ASPECT_MIN 0.02
#define _MIN_SIZE_PX 16
//#define _MIN_SIZE_PX 32

using namespace std;

//------------------------------------------------------------------------------
class AspectRatio {
public:
	AspectRatio(std::string s = "", bool swapped = false);
	void set(std::string s, bool swapped);
	void set(double v, bool swapped);
	std::string get(bool swapped);
	double get_double(bool swapped);
	void get(double &value, bool swapped);
	void get(int &val1, int &val2, bool swapped);
	void swap(void);

protected:
	bool allow_number;
	std::string part_1;
	std::string delimiter;
	std::string part_2;
};

AspectRatio::AspectRatio(std::string s, bool swapped) {
	if(!s.empty())
		set(s, swapped);
}

static size_t __find_real_number(const char *c) {
	bool f_period = false;
	size_t i = 0;
	for(; c[i]; ++i) {
		if(!f_period && (c[i] == ',' || c[i] == '.')) {
			f_period = true;
			continue;
		}
		if(!std::isdigit(c[i]))
			return i;
	}
	return i;
}

void AspectRatio::set(std::string s, bool swapped) {
	if(s == "0x0" || s == "0-0")
		s = "";
	const char *c = s.c_str();
	size_t i = 0;
	// trim
	for(; c[i] && c[i] == ' '; ++i);
	// part_1
	const char *p1_c = &c[i];
	size_t p1_len = __find_real_number(&c[i]);
	i += p1_len;
	// delimiter
	const char *d_c = &c[i];
	size_t i_prev = i;
	for(; c[i] && (!std::isdigit(c[i]) && c[i] != '.' && c[i] != ','); ++i);
	size_t d_len = i - i_prev;
	// part_2
	const char *p2_c = &c[i];
	size_t p2_len = __find_real_number(&c[i]);
	// fill result
	part_1.clear();
	delimiter.clear();
	part_2.clear();
	part_1.append(p1_c, p1_len);
	delimiter.append(d_c, d_len);
	part_2.append(p2_c, p2_len);
	if(swapped) {
		if(part_2.empty() || part_2 == "0")
			part_1 = QString().setNum(1.0 / QString(part_1.c_str()).toDouble()).toStdString();
		else
			std::swap(part_1, part_2);
	}
}

void AspectRatio::set(double v, bool swapped) {
	if(v != 0.0)
		if(swapped)	v = 1.0 / v;
	part_1 = QString().setNum(v).toStdString();
	delimiter = "";
	part_2 = "";
}

std::string AspectRatio::get(bool swapped) {
	if(swapped) {
		if(part_2.empty() && !part_1.empty()) {
			double v;
			get(v, swapped);
			return QString().setNum(v).toStdString();
		}
		return (part_2 + delimiter + part_1);
	}
	return (part_1 + delimiter + part_2);
}

void AspectRatio::get(double &value, bool swapped) {
	const double v1 = QString(part_1.c_str()).toDouble();
	const double v2 = part_2.empty() ? 1.0 : QString(part_2.c_str()).toDouble();
	if(v2 != 0.0 && v1 != 0.0)
		value = swapped ? (v2 / v1) : (v1 / v2);
	else
		value = 1.0;
}

double AspectRatio::get_double(bool swapped) {
	double v;
	get(v, swapped);
	return v;
}

void AspectRatio::get(int &val1, int &val2, bool swapped) {
	val1 = QString(part_1.c_str()).toInt();
	val2 = QString(part_2.c_str()).toInt();
	if(swapped)
		std::swap(val1, val2);
}

void AspectRatio::swap(void) {
	if(!part_2.empty())
		std::swap(part_1, part_2);
	else
		part_1 = QString().setNum(1.0 / QString(part_1.c_str()).toDouble()).toStdString();
}

//------------------------------------------------------------------------------
class PS_Crop : public PS_Base {

public:
	PS_Crop(void);
	virtual ~PS_Crop();
	// crop an actual 1:1 image to the cropped one
	struct t_rect;
	PS_Crop::t_rect apply_to_image(const PS_Crop::t_rect &image);
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool defined;
	bool enabled_crop;

	// inclusive coordinates; aligned to edges, i.e. not at center of pixels; in scale of position.(x_max|y_max) (i.e. 1:1)
	// pay attention, that those coordinates in PS_Base are coordinates of 'unrotated' photo, and differ from those at edit time
	struct t_rect {
		double x1 = 0.0;	// left
		double x2 = 0.0;	// right
		double y1 = 0.0;	// top
		double y2 = 0.0;	// bottom edges
	};
	struct t_rect crop;

	bool fixed_aspect;
	// keep and store it all the time as for unrotated state
	AspectRatio crop_aspect;

	// scale
	bool enabled_scale;
	AspectRatio scale_size;
	bool scale_to_fit;

	int cw_rotation; // used to remap values 'crop_aspect' and 'scale_str' in UI, if any
	bool cw_swapped;
	// for edit purpose only
	double im_x1;	// saved size of unscaled image before crop filter
	double im_x2;
	double im_y1;
	double im_y2;
	double photo_aspect;	// original aspect of photo

};

void F_Crop::set_cw_rotation(int cw_rotation) {
	ps->cw_rotation = cw_rotation;
	ps->cw_swapped = (cw_rotation == 90 || cw_rotation == 270);
//	cerr << "F_Crop::set_cw_rotation(" << cw_rotation << ")" << endl;
	// update UI
	D_GUI_THREAD_CHECK
	le_aspect->setText(ps->crop_aspect.get(ps->cw_swapped).c_str());
	le_scale->setText(ps->scale_size.get(ps->cw_swapped).c_str());
}

//------------------------------------------------------------------------------
class FP_Crop : public FilterProcess_2D {
public:
	FP_Crop(void);
	bool is_enabled(const PS_Base *ps_base);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
//	Area *process(Area *in, Metadata *metadata, class PS_Crop *ps, F_Crop *_this);
};

//------------------------------------------------------------------------------
PS_Crop::PS_Crop(void) {
	reset();
}

PS_Crop::~PS_Crop() {
}

PS_Crop::t_rect PS_Crop::apply_to_image(const PS_Crop::t_rect &image) {
	t_rect res = image;
	if(res.x1 < crop.x1) res.x1 = crop.x1;
	if(res.y1 < crop.y1) res.y1 = crop.y1;
	if(res.x2 > crop.x2) res.x2 = crop.x2;
	if(res.y2 > crop.y2) res.y2 = crop.y2;
	return res;
}

PS_Base *PS_Crop::copy(void) {
	PS_Crop *ps = new PS_Crop;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Crop::reset(void) {
	// TODO: use 'defined' status when edit
	defined = false;
	enabled_crop = false;

	crop.x1 = 0.0;
	crop.x2 = 0.0;
	crop.y1 = 0.0;
	crop.y2 = 0.0;

	im_x1 = 0.0;
	im_x2 = 0.0;
	im_y1 = 0.0;
	im_y2 = 0.0;

	fixed_aspect = false;
	crop_aspect.set("", false);
	photo_aspect = 1.0;

	enabled_scale = false;
	scale_size.set("", false);
	scale_to_fit = true;

	cw_rotation = 0;
	cw_swapped = false;
}

bool PS_Crop::load(DataSet *dataset) {
	std::string s;
	reset();
	dataset->get("defined", defined);
	dataset->get("enabled", enabled_crop);
	dataset->get("crop_x1", crop.x1);
	dataset->get("crop_x2", crop.x2);
	dataset->get("crop_y1", crop.y1);
	dataset->get("crop_y2", crop.y2);
	dataset->get("fixed_aspect", fixed_aspect);
	dataset->get("crop_aspect", s);
	crop_aspect.set(s, false);
	double crop_aspect_v = crop_aspect.get_double(false);
	if(crop_aspect_v < 0.0001 || crop_aspect_v > 1000.0)
		crop_aspect.set("", false);
	dataset->get("enabled_scale", enabled_scale);
	dataset->get("scale_str", s);
	scale_size.set(s, false);
	dataset->get("scale_to_fit", scale_to_fit);
	// verify
	if(defined) {
		if(crop.x1 > crop.x2)
			std::swap(crop.x2, crop.x1);
		if(crop.y1 > crop.y2)
			std::swap(crop.y2, crop.y1);
		const double min_size = _MIN_SIZE_PX;
		if(crop.x2 - crop.x1 < min_size) {
			double cx = (crop.x2 + crop.x1) / 2.0;
			crop.x2 = cx + min_size / 2.0;
			crop.x1 = cx - min_size / 2.0;
		}
		if(crop.y2 - crop.y1 < min_size) {
			double cy = (crop.y2 + crop.y1) / 2.0;
			crop.y2 = cy + min_size / 2.0;
			crop.y1 = cy - min_size / 2.0;
		}
	}
	return true;
}

bool PS_Crop::save(DataSet *dataset) {
	dataset->set("defined", defined);
	dataset->set("enabled", enabled_crop);
	// don't save undefined things
	if(defined) {
		dataset->set("crop_x1", crop.x1);
		dataset->set("crop_x2", crop.x2);
		dataset->set("crop_y1", crop.y1);
		dataset->set("crop_y2", crop.y2);
		dataset->set("fixed_aspect", fixed_aspect);
		dataset->set("crop_aspect", crop_aspect.get(false));
	}
	dataset->set("enabled_scale", enabled_scale);
	dataset->set("scale_str", scale_size.get(false));
	dataset->set("scale_to_fit", scale_to_fit);
	return true;
}

//------------------------------------------------------------------------------
FP_Crop *F_Crop::fp = nullptr;

F_Crop::F_Crop(int id) {
	filter_id = id;
	_id = "F_Crop";
	_name = tr("Crop and scale");
	if(fp == nullptr)
		fp = new FP_Crop();
	_ps = (PS_Crop *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	q_action_edit = nullptr;
	reset();
	mouse_is_pressed = false;
}

F_Crop::~F_Crop() {
}

FilterProcess *F_Crop::getFP(void) {
	return fp;
}

PS_Base *F_Crop::newPS(void) {
	return new PS_Crop();
}

class FS_Crop : public FS_Base {
public:
	FS_Crop(void);

	int crop_move;
	int s_width;
	int s_height;
};

FS_Crop::FS_Crop(void) {
	crop_move = _CROP_MOVE_UNDEFINED;
	s_width = 1;
	s_height = 1;
}

FS_Base *F_Crop::newFS(void) {
	return new FS_Crop;
}

void F_Crop::saveFS(FS_Base *fs_base) {
	if(fs_base != nullptr) {
		FS_Crop *fs = (FS_Crop *)fs_base;
		fs->crop_move = crop_move;
		fs->s_width = s_width;
		fs->s_height = s_height;
	}
}

void F_Crop::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
/*
cerr << "F_Crop::set_PS_and_FS()";
cerr << ", new_ps == 0x" << std::hex << std::setw(8) << std::setfill('0') << (unsigned long)new_ps;
cerr << ", new_fs == 0x" << std::hex << std::setw(8) << std::setfill('0') << (unsigned long)fs_base;
cerr << ", cw_rotation == " << std::dec << args.cw_rotation << endl;
*/
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Crop *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(fs_base == nullptr) {
		crop_move = _CROP_MOVE_UNDEFINED;
		edit_mode_enabled = false;
		edit_mode_enabled = false;
	} else {
		FS_Crop *fs = (FS_Crop *)fs_base;
		crop_move = fs->crop_move;
		s_width = fs->s_width;
		s_height = fs->s_height;
	}
	ps->cw_rotation = args.cw_rotation;
	ps->cw_swapped = (ps->cw_rotation == 90 || ps->cw_rotation == 270);
	// apply settings to UI
	if(widget != nullptr) {
		reconnect(false);
		checkbox_crop->setCheckState(ps->enabled_crop ? Qt::Checked : Qt::Unchecked);
		checkbox_aspect->setCheckState(ps->fixed_aspect ? Qt::Checked : Qt::Unchecked);
		le_aspect->setText(ps->crop_aspect.get(ps->cw_swapped).c_str());
		//
		checkbox_scale->setCheckState(ps->enabled_scale ? Qt::Checked : Qt::Unchecked);
		le_scale->setText(ps->scale_size.get(ps->cw_swapped).c_str());
		reconnect_scale_radio(false);
		int pressed_index = ps->scale_to_fit ? 0 : 1;
		scale_radio->button(pressed_index)->setChecked(true);
		scale_radio->button(1 - pressed_index)->setChecked(false);
		reconnect_scale_radio(true);
		reconnect(true);
	}
	if(q_action_edit != nullptr) {
		q_action_edit->setChecked(false);
	}
}

QWidget *F_Crop::controls(QWidget *parent) {
	D_GUI_THREAD_CHECK
	if(widget != nullptr)
		return widget;
	QGroupBox *crop_q = new QGroupBox(_name, parent);

	QGridLayout *gl = new QGridLayout(crop_q);
	gl->setVerticalSpacing(2);
	gl->setHorizontalSpacing(2);
	gl->setContentsMargins(2, 1, 2, 1);
	gl->setSizeConstraint(QLayout::SetMinimumSize);
	int row = 0;
/*
	QVBoxLayout *crop_l = new QVBoxLayout(crop_q);
	crop_l->setSpacing(0);
	crop_l->setContentsMargins(0, 0, 0, 0);
	crop_l->setSizeConstraint(QLayout::SetMinimumSize);
*/
	// 1-st row
	QHBoxLayout *crop_l1 = new QHBoxLayout();
	crop_l1->setSpacing(8);
	crop_l1->setContentsMargins(0, 0, 0, 0);
//	crop_l1->setContentsMargins(2, 1, 2, 1);
//	crop_l->addLayout(crop_l1);

	checkbox_crop = new QCheckBox(tr("Enable crop"));
	gl->addWidget(checkbox_crop, row, 0);
//	crop_l1->addWidget(checkbox_crop);

	QToolButton *b_original = new QToolButton(parent);
	b_original->setIcon(QIcon(":/resources/crop_original.svg"));
	b_original->setToolTip(tr("Set aspect from photo"));
	b_original->setToolButtonStyle(Qt::ToolButtonIconOnly);
	crop_l1->addWidget(b_original, 1, Qt::AlignRight);

	QToolButton *b_revert = new QToolButton(parent);
	b_revert->setIcon(QIcon(":/resources/crop_revert.svg"));
	b_revert->setToolTip(tr("Revert aspect"));
	b_revert->setToolButtonStyle(Qt::ToolButtonIconOnly);
	crop_l1->addWidget(b_revert, 0, Qt::AlignRight);
	gl->addLayout(crop_l1, row++, 1);

	// row 2
/*
	QHBoxLayout *crop_l2 = new QHBoxLayout();
	crop_l2->setSpacing(8);
	crop_l2->setContentsMargins(2, 1, 2, 1);
	crop_l->addLayout(crop_l2);
*/
	checkbox_aspect = new QCheckBox(tr("Fixed aspect"));
	gl->addWidget(checkbox_aspect, row, 0);
//	crop_l2->addWidget(checkbox_aspect);

	le_aspect = new QLineEdit("");
//	connect(le_aspect, SIGNAL(textChanged(const QString &)), this, SLOT(slot_le_aspect_changed(const QString &)));
	le_aspect->setValidator(new QRegExpValidator(QRegExp("[0-9]{1,1}[0-9|.|,]{,1}[0-9]{,5}[x|X|/|\\-| |*]{,1}[0-9]{,5}"), le_aspect));
	gl->addWidget(le_aspect, row++, 1);
//	crop_l2->addWidget(le_aspect);

	// scale
/*
	QLabel *scale_label_name = new QLabel(tr("Actual 1:1 size:"));
	gl->addWidget(scale_label_name, row, 0);

	scale_label = new QLabel(tr("NNNN x NNNN"));
	gl->addWidget(scale_label, row++, 1);
*/
	//--
	checkbox_scale = new QCheckBox(tr("Scale to size"));
	gl->addWidget(checkbox_scale, row, 0);

	le_scale = new QLineEdit("");
//	connect(le_scale, SIGNAL(textChanged(const QString &)), this, SLOT(slot_le_scale_changed(const QString &)));
	le_scale->setValidator(new QRegExpValidator(QRegExp("|[0-9]{1,5}[x|X|/|\\-| |*]{1,1}[0-9]{1,5}"), le_scale));
//	le_scale->setValidator(new QRegExpValidator(QRegExp("[0-9]{1,5}[x|X|/|\\-| ]{1,1}[0-9]{1,5}"), le_scale));
//	le_scale->setValidator(new QRegExpValidator(QRegExp("[0-9]{1,5}[.|,|x|X|/|\\-| ]{,1}[0-9]{,5}"), le_scale));
	gl->addWidget(le_scale, row++, 1);
	//--
	scale_radio = new QButtonGroup(gl);
	QToolButton *b_size_fit = new QToolButton(parent);
	b_size_fit->setIcon(QIcon(":/resources/scale_fit.svg"));
	b_size_fit->setToolTip(tr("Scale to fit size"));
	b_size_fit->setToolButtonStyle(Qt::ToolButtonIconOnly);
	b_size_fit->setCheckable(true);
	QLabel *l_size_fit = new QLabel(tr("Fit size"));

	QToolButton *b_size_fill = new QToolButton(parent);
	b_size_fill->setIcon(QIcon(":/resources/scale_fill.svg"));
	b_size_fill->setToolTip(tr("Scale to fill size"));
	b_size_fill->setToolButtonStyle(Qt::ToolButtonIconOnly);
	b_size_fill->setCheckable(true);
	QLabel *l_size_fill = new QLabel(tr("Fill size"));

	scale_radio->addButton(b_size_fit, 0);
	scale_radio->addButton(b_size_fill, 1);
#if 0
	QHBoxLayout *l_fit = new QHBoxLayout();
	l_fit->setSpacing(8);
	l_fit->setContentsMargins(2, 1, 2, 1);
	l_fit->addWidget(b_size_fit, 0, Qt::AlignLeft);
	l_fit->addWidget(l_size_fit, 0, Qt::AlignLeft);
	l_fit->addStretch(1);
	gl->addLayout(l_fit, row++, 0, 1, 0);

	QHBoxLayout *l_fill = new QHBoxLayout();
	l_fill->setSpacing(8);
	l_fill->setContentsMargins(2, 1, 2, 1);
	l_fill->addWidget(b_size_fill, 0, Qt::AlignLeft);
	l_fill->addWidget(l_size_fill, 0, Qt::AlignLeft);
	l_fill->addStretch(1);
	gl->addLayout(l_fill, row++, 0, 1, 0);
#else
	QHBoxLayout *l_fit_fill = new QHBoxLayout();
	l_fit_fill->setSpacing(8);
	l_fit_fill->setContentsMargins(2, 1, 2, 1);
	l_fit_fill->addWidget(b_size_fit, 0, Qt::AlignLeft);
	l_fit_fill->addWidget(l_size_fit, 0, Qt::AlignLeft);
	l_fit_fill->addSpacing(8);
	l_fit_fill->addWidget(b_size_fill, 0, Qt::AlignLeft);
	l_fit_fill->addWidget(l_size_fill, 0, Qt::AlignLeft);
	l_fit_fill->addStretch(1);

	gl->addLayout(l_fit_fill, row++, 0, 1, 0);
#endif
	reset();
	connect(b_original, SIGNAL(clicked(bool)), this, SLOT(slot_btn_original(bool)));
	connect(b_revert, SIGNAL(clicked(bool)), this, SLOT(slot_btn_revert(bool)));
	reconnect(true);
	reconnect_scale_radio(true);

	widget = crop_q;
	return widget;
}

void F_Crop::reconnect(bool to_connect) {
	D_GUI_THREAD_CHECK
	if(to_connect) {
		connect(this, SIGNAL(signal_set_text_le_scale(QString)), le_scale, SLOT(setText(QString)));
		connect(this, SIGNAL(signal_set_text_le_aspect(QString)), le_aspect, SLOT(setText(QString)));
		connect(checkbox_crop, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_crop(int)));
		connect(checkbox_aspect, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_aspect(int)));
		connect(le_aspect, SIGNAL(editingFinished(void)), this, SLOT(slot_le_aspect(void)));
		connect(checkbox_scale, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scale(int)));
		connect(le_scale, SIGNAL(editingFinished(void)), this, SLOT(slot_le_scale(void)));
	} else {
		disconnect(this, SIGNAL(signal_set_text_le_scale(QString)), le_scale, SLOT(setText(QString)));
		disconnect(this, SIGNAL(signal_set_text_le_aspect(QString)), le_aspect, SLOT(setText(QString)));
		disconnect(checkbox_crop, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_crop(int)));
		disconnect(checkbox_aspect, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_aspect(int)));
		disconnect(le_aspect, SIGNAL(editingFinished(void)), this, SLOT(slot_le_aspect(void)));
		disconnect(checkbox_scale, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scale(int)));
		disconnect(le_scale, SIGNAL(editingFinished(void)), this, SLOT(slot_le_scale(void)));
	}
}

void F_Crop::reconnect_scale_radio(bool to_connect) {
	D_GUI_THREAD_CHECK
	if(to_connect)
		connect(scale_radio, SIGNAL(buttonClicked(int)), this, SLOT(slot_scale_radio(int)));
	else
		disconnect(scale_radio, SIGNAL(buttonClicked(int)), this, SLOT(slot_scale_radio(int)));
}

QList<QAction *> F_Crop::get_actions_list(void) {
	D_GUI_THREAD_CHECK
	if(q_action_edit == nullptr) {
		q_action_edit = new QAction(QIcon(":/resources/crop_icon.svg"), tr("&Crop"), this);
//		q_action_edit->setShortcut(tr("Ctrl+C"));
		q_action_edit->setStatusTip(tr("Crop photo"));
		q_action_edit->setCheckable(true);
		connect(q_action_edit, SIGNAL(toggled(bool)), this, SLOT(slot_edit_action(bool)));
	}
	QList<QAction *> l;
	l.push_back(q_action_edit);
	return l;
}

Filter::type_t F_Crop::type(void) {
	return Filter::t_geometry;
}

Filter::flags_t F_Crop::flags(void) {
// TODO: remove it
	return Filter::f_geometry_update;
}

bool F_Crop::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
//	desc->name = field_name;
/*
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
	if(field_name == "rotation_angle")
		desc->name = tr("rotation angle");
*/
	return true;
}

//------------------------------------------------------------------------------
FP_Crop::FP_Crop(void) : FilterProcess_2D() {
	_name = "F_Crop";
}

bool FP_Crop::is_enabled(const PS_Base *ps_base) {
	PS_Crop *ps = (PS_Crop *)ps_base;
	if(ps->crop.x1 == 0.0 && ps->crop.x2 == 0.0 && ps->crop.y1 == 0.0 && ps->crop.y2 == 0.0)	// enable call of size_forward() to init edit mode
		return true;
	return (ps->enabled_crop || (ps->enabled_scale && !ps->scale_size.get(false).empty()));
}

void FP_Crop::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
//	const PS_Base *ps_base = fp_size->ps_base;
//	const PS_Crop *ps = (const PS_Crop *)ps_base;
	PS_Crop *ps = (PS_Crop *)fp_size->ps_base;
	*d_after = *d_before;
//cerr << "FP_Crop::size_before()... 1" << endl;
//d_before->dump();
	bool edit_mode = false;
	if(fp_size->filter != nullptr)
		edit_mode = ((F_Crop *)fp_size->filter)->is_edit_mode_enabled();

	if(ps->enabled_crop) {
		double im_x1 = d_after->position.x + d_after->edges.x1;
		double im_x2 = im_x1 + d_after->width();
		double im_y1 = d_after->position.y + d_after->edges.y1;
		double im_y2 = im_y1 + d_after->height();
		if(fp_size->filter != nullptr) {
			// send to Filter (if any) for edit purposes
			PS_Crop *_ps = (PS_Crop *)((F_Crop *)fp_size->filter)->_get_ps();
			_ps->im_x1 = im_x1;
			_ps->im_x2 = im_x2;
			_ps->im_y1 = im_y1;
			_ps->im_y2 = im_y2;
			_ps->photo_aspect = d_before->position._x_max / d_before->position._y_max;
			((F_Crop *)fp_size->filter)->init_le_aspect_from_photo_aspect();
			// fp_size->cw_rotation
			if(_ps->crop.x1 == 0.0 && _ps->crop.x2 == 0.0 && _ps->crop.y1 == 0.0 && _ps->crop.y2 == 0.0) {
				// set default crop with each edge at 10%
				double w = im_x2 - im_x1;
				double h = im_y2 - im_y1;
				_ps->crop.x1 = im_x1 + w * 0.1;
				_ps->crop.x2 = im_x2 - w * 0.1;
				_ps->crop.y1 = im_y1 + h * 0.1;
				_ps->crop.y2 = im_y2 - h * 0.1;
			}
			// set defaults if there was forced edit switch from 'F_Crop' to, say, 'F_Shift'
			if(ps != _ps)
				ps->crop = _ps->crop;
		}
		if(edit_mode == false) {
			const float px_size_x = d_after->position.px_size_x;
			const float px_size_y = d_after->position.px_size_y;
			PS_Crop::t_rect image;
			image.x1 = d_after->position.x - px_size_x * 0.5;
			image.y1 = d_after->position.y - px_size_y * 0.5;
			image.x2 = image.x1 + px_size_x * (d_after->size.w + 1.0);
			image.y2 = image.y1 + px_size_y * (d_after->size.h + 1.0);
			PS_Crop::t_rect crop = ps->apply_to_image(image);

			d_after->position.x = crop.x1 + px_size_x * 0.5;
			d_after->position.y = crop.y1 + px_size_y * 0.5;
			d_after->size.w = (crop.x2 - crop.x1) / px_size_x;
			d_after->size.h = (crop.y2 - crop.y1) / px_size_y;
//d_after->dump();
		}
	}
//cerr << "d_after->position.px_size_x == " << d_after->position.px_size_x << endl;
//cerr << "d_after->position.px_size_y == " << d_after->position.px_size_y << endl;
	bool enabled_scale = (ps->enabled_scale && !ps->scale_size.get(false).empty());
	if(enabled_scale && !edit_mode) {
		int width = 0;
		int height = 0;
		ps->scale_size.get(width, height, false);
		if(ps->scale_to_fit)
			Area::scale_dimensions_to_size_fit(d_after, width, height);
		else
			Area::scale_dimensions_to_size_fill(d_after, width, height);
	}
//cerr << "d_after->size.w == " << d_after->size.w << endl;
//cerr << "d_after->size.h == " << d_after->size.h << endl;
}

void FP_Crop::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	*d_before = *d_after;
}

Area *FP_Crop::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	Area *area_out = nullptr;
	SubFlow *subflow = mt_obj->subflow;
	if(subflow->is_master())
		area_out = new Area(*process_obj->area_in);
	return area_out;
}

//------------------------------------------------------------------------------
void F_Crop::slot_checkbox_scale(int state) {
	D_GUI_THREAD_CHECK
	bool value = (state == Qt::Checked) ? true : false;
	if(value == ps->enabled_scale)
		return;
	ps->enabled_scale = value;
	emit_signal_update();
}

void F_Crop::slot_le_scale(void) {
	D_GUI_THREAD_CHECK
	string value = le_scale->text().toStdString();
	if(value == ps->scale_size.get(ps->cw_swapped))
		return;
	auto value_normalized = ps->scale_size;
	value_normalized.set(value, ps->cw_swapped);
	string scale_normalized = value_normalized.get(ps->cw_swapped);
	bool update_enabled = false;
	bool update_enabled_value = true;
	const bool e_prev = ps->scale_size.get(false).empty();
	const bool e_new = value_normalized.get(false).empty();
	if(e_prev && !e_new)
		update_enabled = true;
	if(!e_prev && e_new) {
		update_enabled = true;
		update_enabled_value = false;
	}
	if(update_enabled) {
		reconnect(false);
		ps->enabled_scale = update_enabled_value;
		checkbox_scale->setCheckState(ps->enabled_scale ? Qt::Checked : Qt::Unchecked);
		reconnect(true);
	}
	if(scale_normalized != value) {
		reconnect(false);
		le_scale->setText(QString::fromStdString(scale_normalized));
		reconnect(true);
	}
	ps->scale_size = value_normalized;
	if(ps->enabled_scale || update_enabled)
		emit_signal_update();
}

void F_Crop::slot_scale_radio(int index) {
	D_GUI_THREAD_CHECK
	bool value = (index == 0);
	if(ps->scale_to_fit == value)
		return;
	ps->scale_to_fit = value;
	if(ps->enabled_scale)
		emit_signal_update();
}

//------------------------------------------------------------------------------
void F_Crop::slot_checkbox_crop(int state) {
	D_GUI_THREAD_CHECK
	bool value = (state == Qt::Checked);
	if(value == ps->enabled_crop)
		return;
	if(!ps->defined) {
		q_action_edit->setChecked(true);
		return;
	}
	ps->enabled_crop = value;
	if(!ps->enabled_crop)
		edit_mode_exit();
	if(!edit_mode_enabled)
		emit_signal_update();
}

void F_Crop::init_le_aspect_from_photo_aspect(void) {
	if(le_aspect->text() == "" && ps->photo_aspect != 0.0) {
//cerr << "init_le_aspect_from_photo_aspect()" << endl;
		ps->crop_aspect.set(ps->photo_aspect, false);
		emit signal_set_text_le_aspect(QString(ps->crop_aspect.get(ps->cw_swapped).c_str()));
	}
}

void F_Crop::slot_checkbox_aspect(int state) {
	D_GUI_THREAD_CHECK
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	if(value == ps->fixed_aspect)
		return;
	ps->fixed_aspect = value;
	if(ps->fixed_aspect) {
		init_le_aspect_from_photo_aspect();
		if(!ps->enabled_crop)
			checkbox_crop->setChecked(true);
		if(aspect_normalize()) {
//			emit signal_view_refresh(session_id);
			if(ps->enabled_crop) {
				if(edit_mode_enabled)
					emit signal_view_refresh(session_id);
				else
					emit_signal_update();
			}
		}
	}
}

void F_Crop::slot_le_aspect(void) {
	D_GUI_THREAD_CHECK
	string aspect_str = le_aspect->text().toStdString();
	if(aspect_str == "")
		return;
	if(aspect_str == ps->crop_aspect.get(ps->cw_swapped))
		return;
	auto crop_aspect_prev = ps->crop_aspect;
	ps->crop_aspect.set(aspect_str, ps->cw_swapped);
	double aspect = ps->crop_aspect.get_double(ps->cw_swapped);
	if(aspect <= _ASPECT_MAX && aspect >= _ASPECT_MIN) {
		aspect_normalize();
		if(!ps->fixed_aspect) {
			ps->fixed_aspect = true;
			reconnect(false);
			checkbox_aspect->setChecked(true);
			reconnect(true);
		}
//		emit signal_view_refresh(session_id);
		if(ps->enabled_crop) {
			if(edit_mode_enabled)
				emit signal_view_refresh(session_id);
			else
				emit_signal_update();
		}
	} else {
		ps->crop_aspect = crop_aspect_prev;
	}
}

void F_Crop::slot_btn_original(bool checked) {
	D_GUI_THREAD_CHECK
	if(ps->crop_aspect.get_double(false) != ps->photo_aspect) {
		ps->crop_aspect.set(ps->photo_aspect, false);
		le_aspect->setText(ps->crop_aspect.get(ps->cw_swapped).c_str());
		aspect_normalize();
		if(ps->enabled_crop) {
			if(edit_mode_enabled)
				emit signal_view_refresh(session_id);
			else
				emit_signal_update();
		}
	}
}

void F_Crop::slot_btn_revert(bool checked) {
	D_GUI_THREAD_CHECK
	ps->crop_aspect.swap();
	le_aspect->setText(ps->crop_aspect.get(ps->cw_swapped).c_str());
	aspect_normalize();
	if(ps->enabled_crop) {
		if(edit_mode_enabled)
			emit signal_view_refresh(session_id);
		else
			emit_signal_update();
	}
}

// check photo and crop aspect !!!
bool F_Crop::aspect_normalize(void) {
	if(ps->crop_aspect.get(ps->cw_swapped).empty())
		ps->crop_aspect.set(ps->photo_aspect, ps->cw_swapped);
	double w2 = (ps->crop.x2 - ps->crop.x1) / 2.0;
	double h2 = (ps->crop.y2 - ps->crop.y1) / 2.0;
	double cx = ps->crop.x1 + w2;
	double cy = ps->crop.y1 + h2;
	double scale = sqrt(w2 * w2 + h2 * h2);
	double aspect = ps->crop_aspect.get_double(false);
	double angle = atan(1.0 / aspect);
	w2 = scale * cos(angle);
	h2 = scale * sin(angle);
	ps->crop.x1 = cx - w2;
	ps->crop.x2 = cx + w2;
	ps->crop.y1 = cy - h2;
	ps->crop.y2 = cy + h2;
	return true;
}

void F_Crop::edit_mode_exit(void) {
	D_GUI_THREAD_CHECK
//cerr << "F_Crop::edit_mode_exit()" << endl;
	edit_mode_enabled = false;
	q_action_edit->setChecked(false);
}

void F_Crop::edit_mode_forced_exit(void) {
	D_GUI_THREAD_CHECK
	edit_mode_enabled = false;
//	checkbox_crop->setCheckState(Qt::Unchecked);
	slot_edit_action(false);
}

void F_Crop::slot_edit_action(bool checked) {
	D_GUI_THREAD_CHECK
	if(checked == edit_mode_enabled)
		return;
	edit_mode_enabled = checked;
	// apply crop when leave edit crop mode and cropping is enabled
//	if(checked && !ps->enabled_crop && !ps->defined) {
	if(checked && !ps->enabled_crop) {
		ps->enabled_crop = true;
		checkbox_crop->setCheckState(Qt::Checked);
	}
	ps->defined = true;
//	ps->edit_mode = checked;
	emit signal_filter_edit(this, edit_mode_enabled, Cursor::arrow);
	emit_signal_update();
}

void *F_Crop::_get_ps(void) {
	return (void *)ps;
}

class rotated_crop_t {
public:
	rotated_crop_t(PS_Crop *ps, int _rotation);
	void apply_to_ps(PS_Crop *ps);
	double crop_x1;
	double crop_x2;
	double crop_y1;
	double crop_y2;
	double im_x1;
	double im_x2;
	double im_y1;
	double im_y2;
	int rotation;
};

rotated_crop_t::rotated_crop_t(PS_Crop *ps, int _rotation) {
	rotation = _rotation;
//cerr << "ROTATION == " << rotation << endl;
	switch (rotation) {
	case 0:
		crop_x1 = ps->crop.x1;
		crop_x2 = ps->crop.x2;
		crop_y1 = ps->crop.y1;
		crop_y2 = ps->crop.y2;
		im_x1 = ps->im_x1;
		im_x2 = ps->im_x2;
		im_y1 = ps->im_y1;
		im_y2 = ps->im_y2;
		break;
	case 90:
		crop_x1 = -ps->crop.y2;
		crop_x2 = -ps->crop.y1;
		crop_y1 = ps->crop.x1;
		crop_y2 = ps->crop.x2;
		im_x1 = -ps->im_y2;
		im_x2 = -ps->im_y1;
		im_y1 = ps->im_x1;
		im_y2 = ps->im_x2;
		break;
	case 180:
		crop_x1 = -ps->crop.x2;
		crop_x2 = -ps->crop.x1;
		crop_y1 = -ps->crop.y2;
		crop_y2 = -ps->crop.y1;
		im_x1 = -ps->im_x2;
		im_x2 = -ps->im_x1;
		im_y1 = -ps->im_y2;
		im_y2 = -ps->im_y1;
		break;
	case 270:
		crop_x1 = ps->crop.y1;
		crop_x2 = ps->crop.y2;
		crop_y1 = -ps->crop.x2;
		crop_y2 = -ps->crop.x1;
		im_x1 = ps->im_y1;
		im_x2 = ps->im_y2;
		im_y1 = -ps->im_x2;
		im_y2 = -ps->im_x1;
		break;
	}
}

void rotated_crop_t::apply_to_ps(PS_Crop *ps) {
	switch (rotation) {
	case 0:
		ps->crop.x1 = crop_x1;
		ps->crop.x2 = crop_x2;
		ps->crop.y1 = crop_y1;
		ps->crop.y2 = crop_y2;
		break;
	case 90:
		ps->crop.x1 = crop_y1;
		ps->crop.x2 = crop_y2;
		ps->crop.y1 = -crop_x2;
		ps->crop.y2 = -crop_x1;
		break;
	case 180:
		ps->crop.x1 = -crop_x2;
		ps->crop.x2 = -crop_x1;
		ps->crop.y1 = -crop_y2;
		ps->crop.y2 = -crop_y1;
		break;
	case 270:
		ps->crop.x1 = -crop_y2;
		ps->crop.x2 = -crop_y1;
		ps->crop.y1 = crop_x1;
		ps->crop.y2 = crop_x2;
		break;
	}
}

QRect F_Crop::view_crop_rect(const QRect &image, image_and_viewport_t transform) {
	int im_x1, im_x2, im_y1, im_y2;
	transform.photo_to_image(im_x1, im_y1, ps->crop.x1, ps->crop.y1);
	transform.photo_to_image(im_x2, im_y2, ps->crop.x2, ps->crop.y2);
	int x1, x2, y1, y2;
	transform.image_to_viewport(x1, y1, im_x1, im_y1, true);
	transform.image_to_viewport(x2, y2, im_x2, im_y2, true);
	if(x1 > x2) std::swap(x1, x2);
	if(y1 > y2) std::swap(y1, y2);
	QRect rez(x1, y1, x2 - x1, y2 - y1);
	return rez;
#if 0
	rotated_crop_t rc(ps, transform.get_cw_rotation());
	int im_x1, im_x2, im_y1, im_y2;
	transform.photo_to_image(im_x1, im_y1, rc.crop_x1, rc.crop_y1);
	transform.photo_to_image(im_x2, im_y2, rc.crop_x2, rc.crop_y2);
	int x1, x2, y1, y2;
	transform.image_to_viewport(x1, y1, im_x1, im_y1, false);
	transform.image_to_viewport(x2, y2, im_x2, im_y2, false);
	if(x1 > x2) std::swap(x1, x2);
	if(y1 > y2) std::swap(y1, y2);
	QRect rez(x1, y1, x2 - x1, y2 - y1);
	return rez;
#endif
}

void F_Crop::draw(QPainter *painter, FilterEdit_event_t *et) {
	D_GUI_THREAD_CHECK
	if(!edit_mode_enabled)
		return;
	QSize viewport = et->viewport;
	QRect image = et->image;
	image_and_viewport_t transform = et->transform;
	int w = image.width();
	int h = image.height();
	long vw = viewport.width();
	long vh = viewport.height();
	if(vw < w)
		vw = w;
	if(vh < h)
		vh = h;
	// reset world translation, because that would be fixed with 'image_and_viewport_t' object
	QTransform tr_restore;
	tr_restore.translate(0.0, 0.0);	// shift to fix AA artifacts
	painter->setWorldTransform(tr_restore);
	QRect crop_rect = view_crop_rect(image, transform);
	long crop_x = crop_rect.x();
	long crop_y = crop_rect.y();
//cerr << "draw, crop == " << crop_x << " - " << crop_y << endl;
	long crop_w = crop_rect.width();
	long crop_h = crop_rect.height();

	int cw_rotation = et->transform.get_cw_rotation();

	QBrush crop_brush(QColor(0, 0, 0, 127 + 32));
	// top
	painter->fillRect(0, 0, vw, crop_y, crop_brush);
	// left
	painter->fillRect(0, crop_y, crop_x, crop_h, crop_brush);
	// bottom
	painter->fillRect(0, crop_y + crop_h, vw, vh - crop_h - crop_y, crop_brush);
	// right
	painter->fillRect(crop_x + crop_w, crop_y, vw - crop_x - crop_w, crop_h, crop_brush);
	// crop rectangle
//	painter->setPen(QPen(QColor(255, 255, 255, 127 + 64), 1.0));
//	painter->drawRect(crop_x, crop_y, crop_w - 1, crop_h - 1);
	// resize edges
	int mh = EDIT_RESIZE_CORNERS;
	int mv = EDIT_RESIZE_CORNERS;
	if(mh > crop_w)
		mh = crop_w;
	if(mv > crop_h)
		mv = crop_h;
	// corners
	QPen pen_out = QPen(QColor(255, 255, 255, 255), 1.0);
	// top left
	painter->setPen(pen_out);
	painter->drawLine(crop_x - 1, crop_y - 1, crop_x - 1 + mh, crop_y - 1);
	painter->drawLine(crop_x - 1, crop_y - 1, crop_x - 1, crop_y - 1 + mv);
	// top right
	painter->drawLine(crop_x + crop_w, crop_y - 1, crop_x + crop_w - mh, crop_y - 1);
	painter->drawLine(crop_x + crop_w, crop_y - 1, crop_x + crop_w, crop_y - 1 + mv);
	// bottom left
	painter->drawLine(crop_x - 1, crop_y + crop_h, crop_x - 1 + mh, crop_y + crop_h);
	painter->drawLine(crop_x - 1, crop_y + crop_h, crop_x - 1, crop_y + crop_h - mv);
	// bottom right
	painter->drawLine(crop_x + crop_w, crop_y + crop_h, crop_x + crop_w - mh, crop_y + crop_h);
	painter->drawLine(crop_x + crop_w, crop_y + crop_h, crop_x + crop_w, crop_y + crop_h - mv);
	// thirds lines rule
	painter->setPen(QPen(QColor(255, 255, 255, 63), 1.0));
	bool aa = painter->testRenderHint(QPainter::Antialiasing);
	if(!aa)
		painter->setRenderHint(QPainter::Antialiasing, true);
	QPen pens[2] = {
		QPen(QColor(0, 0, 0, 255), 1.0),
		QPen(QColor(255, 255, 255, 127), 1.5)
	};
	for(int i = 0; i < 2; ++i) {
		painter->setPen(pens[i]);
		// horizontal
		painter->drawLine(QLineF(0.5 + crop_x,	0.5 + crop_y + crop_h / 3,		0.5 + crop_x + crop_w,	0.5 + crop_y + crop_h / 3));
		painter->drawLine(QLineF(0.5 + crop_x,	0.5 + crop_y + 2 * crop_h / 3,	0.5 + crop_x + crop_w,	0.5 + crop_y + 2 * crop_h / 3));
		// vertical
		painter->drawLine(QLineF(0.5 + crop_x + crop_w / 3,		0.5 + crop_y,	0.5 + crop_x + crop_w / 3,		0.5 + crop_y + crop_h));
		painter->drawLine(QLineF(0.5 + crop_x + 2 * crop_w / 3,	0.5 + crop_y,	0.5 + crop_x + 2 * crop_w / 3,	0.5 + crop_y + crop_h));
	}
	painter->setPen(pen_out);
	if(mv > crop_h / 3)
		mv = crop_h / 3;
	if(mv > 1) {
		painter->drawLine(QLineF(0.5 + crop_x - 1, 0.5 + crop_y + crop_h / 3 - mv, 0.5 + crop_x - 1, 0.5 + crop_y + crop_h / 3 + mv));
		painter->drawLine(QLineF(0.5 + crop_x + crop_w, 0.5 + crop_y + crop_h / 3 - mv, 0.5 + crop_x + crop_w, 0.5 + crop_y + crop_h / 3 + mv));
		painter->drawLine(QLineF(0.5 + crop_x - 1,	0.5 + crop_y + 2 * crop_h / 3 - mv,	0.5 + crop_x - 1,	0.5 + crop_y + 2 * crop_h / 3 + mv));
		painter->drawLine(QLineF(0.5 + crop_x + crop_w,	0.5 + crop_y + 2 * crop_h / 3 - mv,	0.5 + crop_x + crop_w,	0.5 + crop_y + 2 * crop_h / 3 + mv));
	}
	if(mh > crop_w / 3)
		mh = crop_w / 3;
	if(mh > 1) {
		painter->drawLine(QLineF(0.5 + crop_x + crop_w / 3 - mh, 0.5 + crop_y - 1, 0.5 + crop_x + crop_w / 3 + mh, 0.5 + crop_y - 1));
		painter->drawLine(QLineF(0.5 + crop_x + crop_w / 3 - mh, 0.5 + crop_y + crop_h, 0.5 + crop_x + crop_w / 3 + mh, 0.5 + crop_y + crop_h));
		painter->drawLine(QLineF(0.5 + crop_x + 2 * crop_w / 3 - mh, 0.5 + crop_y - 1, 0.5 + crop_x + 2 * crop_w / 3 + mh, 0.5 + crop_y - 1));
		painter->drawLine(QLineF(0.5 + crop_x + 2 * crop_w / 3 - mh, 0.5 + crop_y + crop_h, 0.5 + crop_x + 2 * crop_w / 3 + mh, 0.5 + crop_y + crop_h));
	}

	// draw text...
	if(true) {
		// reset world translation
		QTransform tr_restore;
		tr_restore.translate(0.5, 0.5);	// shift to fix AA artifacts
		painter->setWorldTransform(tr_restore);

		int rx = 10;
		int ry = 10;
		QColor text_color(0xFF, 0xFF, 0xFF);
		QString str;

		QFont fnt = painter->font();
		fnt.setStyleHint(QFont::Courier);
		fnt.setFamily("Fixed");
//		fnt.setBold(true);
		painter->setFont(fnt);

		QString tr_size = tr("size");
		QString tr_offset = tr("offset");
		QString tr_aspect = tr("aspect");
		QString *tr_str[] = {&tr_size, &tr_offset, &tr_aspect};
		int tr_l = tr_str[0]->length();
		for(int i = 0; i < 3; ++i)
			if(tr_str[i]->length() > tr_l)
				tr_l = tr_str[i]->length();
		for(int i = 0; i < 3; ++i)
			while(tr_str[i]->length() < tr_l)
				*tr_str[i] = QString(" ") + *tr_str[i];

		QFontMetrics qfm(painter->font());
		str = tr_size + ": 65535x65535";
		QRect bg_rect = qfm.boundingRect(str);
		int _w = bg_rect.width();
		int _h = bg_rect.height();
		bg_rect.setX(rx);
		bg_rect.setY(ry);
		bg_rect.setHeight(_h * 3 + 10);
		bg_rect.setWidth(_w + 10);
		painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter->setPen(QColor(0x0F, 0x0F, 0x0F, 0x9F));
		painter->setBrush(QColor(0x0F, 0x0F, 0x0F, 0x7F));
		painter->drawRoundedRect(bg_rect, 5.0, 5.0);
		painter->setPen(text_color);

		int n_width = ps->crop.x2 - ps->crop.x1;
		int n_height = ps->crop.y2 - ps->crop.y1;
		if(cw_rotation == 90 || cw_rotation == 270)
			std::swap(n_width, n_height);

		str.sprintf(": %dx%d", n_width, n_height);
		str = tr_size + str;
		painter->setPen(QColor(0x00, 0x00, 0x00));
		painter->drawText(QPointF(rx + 5, ry + qfm.height()), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(rx + 5 - 1, ry + qfm.height() - 1), str);

		double off_x = (ps->crop.x1 + ps->crop.x2) / 2.0;
		double off_y = (ps->crop.y1 + ps->crop.y2) / 2.0;
		if(cw_rotation == 0) {
			off_y = -off_y;
		}
		if(cw_rotation == 90) {
			std::swap(off_x, off_y);
			off_x = -off_x;
			off_y = -off_y;
		}
		if(cw_rotation == 180) {
			off_x = -off_x;
		}
		if(cw_rotation == 270) {
			std::swap(off_x, off_y);
		}
		str.sprintf(": %dx%d", int(off_x), int(off_y));
		str = tr_offset + str;
		painter->setPen(QColor(0x00, 0x00, 0x00));
		painter->drawText(QPointF(rx + 5, ry + qfm.height() * 2), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(rx + 5 - 1, ry + qfm.height() * 2 - 1), str);
		double aspect = (double)n_width / n_height;
		string space = "";
		str.sprintf("%+.0f", aspect);
		if(str.length() == 2)
			space += " ";
		str.sprintf(": %s%.4f", space.c_str(), aspect);
		str = tr_aspect + str;
		painter->setPen(QColor(0x00, 0x00, 0x00));
		painter->drawText(QPointF(rx + 5, ry + qfm.height() * 3), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(rx + 5 - 1, ry + qfm.height() * 3 - 1), str);
	}

	if(!aa)
		painter->setRenderHint(QPainter::Antialiasing, false);
}

// crop edit with pressed CTRL
void F_Crop::edit_mouse_scratch(FilterEdit_event_t *mt, bool press, bool release) {
//cerr << "edit scratch" << endl;
//	QPoint p = mt->transform.viewport_coords_to_image(mt->cursor_pos, false);
//	double px = p.x();
//	double py = p.y();
	int im_x, im_y;
	mt->transform.viewport_to_image(im_x, im_y, mt->cursor_pos.x(), mt->cursor_pos.y(), false);
	double px = im_x;
	double py = im_y;
	int rotation = mt->transform.get_cw_rotation();
	rotated_crop_t rc(ps, mt->transform.get_cw_rotation());
//	if(rotation == 90 || rotation == 270)
//		std::swap(image_w, image_h);
/*
	const QRect &image = mt->image;
	int image_w = image.width();
	int image_h = image.height();
	double scale_x = (rc.im_x2 - rc.im_x1) / image_w;
	double scale_y = (rc.im_y2 - rc.im_y1) / image_h;
	px = px * scale_x + rc.im_x1;
	py = py * scale_y + rc.im_y1;
*/
	float photo_x;
	float photo_y;
	float px_size_x;
	float px_size_y;
	mt->transform.get_photo_params(photo_x, photo_y, px_size_x, px_size_y);
	px = px * px_size_x + rc.im_x1;
	py = py * px_size_y + rc.im_y1;

	if(press) {
		edit_mouse_scratch_pos_x = px;
		edit_mouse_scratch_pos_y = py;
	} else {
		if(release) {
			crop_move = _CROP_MOVE_UNDEFINED;
		} else {	// move
		}
	}
	rc.crop_x1 = edit_mouse_scratch_pos_x;
	rc.crop_y1 = edit_mouse_scratch_pos_y;
	rc.crop_x2 = px;
	rc.crop_y2 = py;
	// normalize fixed aspect
	if(ps->fixed_aspect) {
		// TODO: fix edit glitches

		// 1: transpose to NE corner
		//    +---+ p2 - realigned
		//    |  /|
		//    | / |
		//    |/  |
		// p1 +---+    - fixed
		double p2w = ddr::abs(rc.crop_x1 - rc.crop_x2);
		double p2h = ddr::abs(rc.crop_y1 - rc.crop_y2);
		double aspect = ps->crop_aspect.get_double((rotation == 90 || rotation == 270));
//		if(rotation == 90 || rotation == 270)
//			aspect = 1.0 / aspect;
		// transpose edges limits
		double max_w = rc.crop_x1 - rc.im_x1;
		if(rc.crop_x2 > rc.crop_x1)
			max_w = rc.im_x2 - rc.crop_x1;
		double max_h = rc.crop_y1 - rc.im_y1;
		if(rc.crop_y2 > rc.crop_y1)
			max_h = rc.im_y2 - rc.crop_y1;
		// align to aspect
		double new_w = p2h * aspect;
		double new_h = p2w / aspect;
		if(new_w > p2w) {
			new_h = p2h;
		} else {
			new_w = p2w;
		}
		// crop by image edges, keep aspect
		if(new_w > max_w) {
			new_w = max_w;
			new_h = new_w / aspect;
		}
		if(new_h > max_h) {
			new_h = max_h;
			new_w = new_h * aspect;
		}
		// apply
		if(rc.crop_x2 > rc.crop_x1)
			rc.crop_x2 = rc.crop_x1 + new_w;
		else
			rc.crop_x2 = rc.crop_x1 - new_w;
		if(rc.crop_y2 > rc.crop_y1)
			rc.crop_y2 = rc.crop_y1 + new_h;
		else
			rc.crop_y2 = rc.crop_y1 - new_h;
	}
	// normalize crop
	if(rc.crop_x1 > rc.crop_x2) std::swap(rc.crop_x1, rc.crop_x2);
	if(rc.crop_y1 > rc.crop_y2) std::swap(rc.crop_y1, rc.crop_y2);
	ddr::clip_min(rc.crop_x1, rc.im_x1);
	ddr::clip_max(rc.crop_x2, rc.im_x2);
	ddr::clip_min(rc.crop_y1, rc.im_y1);
	ddr::clip_max(rc.crop_y2, rc.im_y2);
	rc.apply_to_ps(ps);
}

bool F_Crop::keyEvent(FilterEdit_event_t *mt, Cursor::cursor &cursor) {
	QKeyEvent *event = (QKeyEvent *)mt->event;
	if(!edit_mode_enabled)
		return false;
	if(mt->image_pixels.width() == -1 || mt->image_pixels.height() == -1)
		return false;
	if(event->key() == Qt::Key_Control) {
		edit_update_cursor(cursor, mt);
	}
	return false;
}

bool F_Crop::mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &cursor) {
	QMouseEvent *event = (QMouseEvent *)mt->event;
	mouse_is_pressed = true;
	if(!edit_mode_enabled)
		return false;
	if(mt->image_pixels.width() == -1 || mt->image_pixels.height() == -1)
		return false;
	bool rez = false;

	if(event->button() == Qt::LeftButton) {
//		mouse_last_pos = mt->transform.viewport_coords_to_image(mt->cursor_pos, false);
		int im_x, im_y;
		mt->transform.viewport_to_image(im_x, im_y, mt->cursor_pos.x(), mt->cursor_pos.y(), false);
		mouse_last_pos = QPoint(im_x, im_y);
		if(event->modifiers() & Qt::ControlModifier) {
			crop_move = _CROP_MOVE_SCRATCH;
			cursor = Cursor::cross;
		}
		if(crop_move == _CROP_MOVE_SCRATCH) {
			edit_mouse_scratch(mt, true, false);
			rez = true;
		}
		if(crop_move == _CROP_MOVE_PAN) {
			cursor = Cursor::hand_closed;
			rez = true;
		}
	}
	return rez;
}

bool F_Crop::mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &cursor) {
	D_GUI_THREAD_CHECK
	QMouseEvent *event = (QMouseEvent *)mt->event;
	mouse_is_pressed = false;
	if(!edit_mode_enabled)
		return false;
	if(mt->image_pixels.width() == -1 || mt->image_pixels.height() == -1)
		return false;
	bool rez = false;

	if(event->button() == Qt::LeftButton) {
		if(crop_move == _CROP_MOVE_SCRATCH) {
			edit_mouse_scratch(mt, false, true);
			rez = true;
		}
		if(crop_move == _CROP_MOVE_PAN) {
			cursor = Cursor::hand_open;
			rez = true;
		}
		// update aspect_le if aspect is not fixed
		if(!ps->fixed_aspect) {
			double aspect = (ps->crop.x2 - ps->crop.x1) / (ps->crop.y2 - ps->crop.y1);
//			cerr << "mouseReleaseEvent, set aspect == " << aspect << " with swapped == " << ps->cw_swapped << endl;
			ps->crop_aspect.set(aspect, false);
			le_aspect->setText(ps->crop_aspect.get(ps->cw_swapped).c_str());
		}
		if(crop_move == _CROP_MOVE_UNDEFINED) {
			// refresh cursor and edit mode
			long crop_move_prev = crop_move;
			edit_update_cursor(cursor, mt);
			if(crop_move_prev != crop_move)
				rez = true;
		}
	}
	return rez;
}

bool F_Crop::mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &cursor) {
//cerr << endl;
	QMouseEvent *event = (QMouseEvent *)mt->event;
//	const QSize &viewport = mt->viewport;
	const QRect &image = mt->image;
	if(!edit_mode_enabled)
		return false;
	if(mt->image_pixels.width() == -1 || mt->image_pixels.height() == -1)
		return false;
	accepted = true;

	if(crop_move == _CROP_MOVE_SCRATCH && event->buttons() & Qt::LeftButton) {
		edit_mouse_scratch(mt, false, false);
		return true;
	}

	long image_w = image.width();
	long image_h = image.height();
/*
	long image_drawn_w = image_w;
	long image_drawn_h = image_h;
	int rotation = mt->transform.get_cw_rotation();
	if(rotation == 90 || rotation == 270)
		std::swap(image_drawn_w, image_drawn_h);
	double scale_x = (ps->im_x2 - ps->im_x1) / image_drawn_w;
	double scale_y = (ps->im_y2 - ps->im_y1) / image_drawn_h;
*/
	int rotation = mt->transform.get_cw_rotation();
	float photo_x;
	float photo_y;
	float px_size_x;
	float px_size_y;
	mt->transform.get_photo_params(photo_x, photo_y, px_size_x, px_size_y);
	// calculate offsets for corner edges
//	int x = mt->cursor_pos.x() - image.x();
//	int y = mt->cursor_pos.y() - image.y();
//cerr << "cursor: " << mt->cursor_pos.x() << " - " << mt->cursor_pos.y() << endl;
	int x, y;
	mt->transform.viewport_to_image(x, y, mt->cursor_pos.x(), mt->cursor_pos.y(), false);
/*
	QPoint p = mt->transform.viewport_coords_to_image(mt->cursor_pos, false);
	int x = p.x();
	int y = p.y();
*/
//cerr << "x == " << x << "; y == " << y << endl;
//	int x = mt->image_cursor.x();
//	int y = mt->image_cursor.y();
//cerr << "image_size == " << mt->image_size.width() << "x" << mt->image_size.height() << endl;
//cerr << endl;
//cerr << "  x == " << mt->cursor_pos.x() << ", y == " << mt->cursor_pos.y() << endl;
//cerr << "..x == " << x << ", y == " << y << endl;
//	p = mt->transform.image_coords_to_viewport(x, y);
//	cerr << "__x == " << p.x() << ", y == " << p.y() << endl;
//cerr << "x == " << mt->image_cursor.x() << ", y == " << mt->image_cursor.y() << endl;
	mouse_last_pos_trans.setX(x);
	mouse_last_pos_trans.setY(y);
	if(!(event->buttons() & Qt::LeftButton)) {
		edit_update_cursor(cursor, mt);
	}
	if(event->buttons() & Qt::LeftButton) {
//cerr << "x == " << x << "; y == " << y << endl;
		if(x < 0)	x = 0;
		if(y < 0)	y = 0;
//		if(x >= image_drawn_w)	x = image_drawn_w;
//		if(y >= image_drawn_h)	y = image_drawn_h;
		if(x >= image_w)	x = image_w;
		if(y >= image_h)	y = image_h;
//cerr << "x == " << x << "; y == " << y << endl;

		// pan or resize crop area
		double crop_x1_prev = ps->crop.x1;
		double crop_x2_prev = ps->crop.x2;
		double crop_y1_prev = ps->crop.y1;
		double crop_y2_prev = ps->crop.y2;
		rotated_crop_t rc(ps, rotation);
		if(crop_move == _CROP_MOVE_PAN) {
			if(x <= 0)	x = -1;
//			if(x >= image_drawn_w)	x = image_drawn_w + 1;
			if(x >= image_w)	x = image_w + 1;
			if(y <= 0)	y = -1;
//			if(y >= image_drawn_h)	y = image_drawn_h + 1;
			if(y >= image_h)	y = image_h + 1;
//			double dx = scale_x * (x - mouse_last_pos.x());
//			double dy = scale_y * (y - mouse_last_pos.y());
			double dx = px_size_x * (x - mouse_last_pos.x());
			double dy = px_size_y * (y - mouse_last_pos.y());
			double crop_xl = rc.crop_x2 - rc.crop_x1;
			double crop_yl = rc.crop_y2 - rc.crop_y1;
			// horizontal pan
			if(dx > 0) {
				rc.crop_x2 += dx;
				if(rc.crop_x2 > rc.im_x2)	rc.crop_x2 = rc.im_x2;
				rc.crop_x1 = rc.crop_x2 - crop_xl;
			}
			if(dx < 0) {
				rc.crop_x1 += dx;
				if(rc.crop_x1 < rc.im_x1)	rc.crop_x1 = rc.im_x1;
				rc.crop_x2 = rc.crop_x1 + crop_xl;
			}
			// vertical pan
			if(dy > 0) {
				rc.crop_y2 += dy;
				if(rc.crop_y2 > rc.im_y2)	rc.crop_y2 = rc.im_y2;
				rc.crop_y1 = rc.crop_y2 - crop_yl;
			}
			if(dy < 0) {
				rc.crop_y1 += dy;
				if(rc.crop_y1 < rc.im_y1)	rc.crop_y1 = rc.im_y1;
				rc.crop_y2 = rc.crop_y1 + crop_yl;
			}
		} else {
			double new_x = double(x) / double(image_w);
			double new_y = double(y) / double(image_h);
//cerr << "image_size == " << image_w << "x" << image_h << "; new_pos == " << new_x << " - " << new_y << endl;
//			new_x = scale_x * x + rc.im_x1;
//			new_y = scale_x * y + rc.im_y1;
			new_x = px_size_x * x + rc.im_x1;
			new_y = px_size_y * y + rc.im_y1;
			int move_type = crop_move;
			if(!ps->fixed_aspect) {
				if(move_type & _CROP_MOVE_LEFT)
					rc.crop_x1 = new_x;
				if(move_type & _CROP_MOVE_RIGHT)
					rc.crop_x2 = new_x;
				if(move_type & _CROP_MOVE_TOP)
					rc.crop_y1 = new_y;
				if(move_type & _CROP_MOVE_BOTTOM)
					rc.crop_y2 = new_y;
			} else {
				// with fixed aspect...
				// TODO: simplify as in edit_mouse_scratch(...) ???
				double crop_cx = (rc.crop_x1 + rc.crop_x2) / 2.0;
				double crop_cy = (rc.crop_y1 + rc.crop_y2) / 2.0;
				double crop_w = rc.crop_x2 - rc.crop_x1;
				double crop_h = rc.crop_y2 - rc.crop_y1;
				double aspect = ps->crop_aspect.get_double((rotation == 90 || rotation == 270));
				//--==-- edges
				if(move_type == _CROP_MOVE_LEFT || move_type == _CROP_MOVE_RIGHT) {
					if(move_type == _CROP_MOVE_LEFT)
						rc.crop_x1 = new_x;
					if(move_type == _CROP_MOVE_RIGHT)
						rc.crop_x2 = new_x;
					double crop_l = rc.crop_x2 - rc.crop_x1;
					crop_l /= 2.0;
					crop_l /= aspect;
					if(crop_l <= crop_h) {
						rc.crop_y1 = crop_cy - crop_l;
						rc.crop_y2 = crop_cy + crop_l;
					} else {
						crop_l = crop_h;
						rc.crop_y1 = crop_cy - crop_l;
						rc.crop_y2 = crop_cy + crop_l;
						crop_l *= aspect;
						crop_l *= 2.0;
						if(move_type == _CROP_MOVE_LEFT)
							rc.crop_x1 = rc.crop_x2 - crop_l;
						if(move_type == _CROP_MOVE_RIGHT)
							rc.crop_x2 = rc.crop_x1 + crop_l;
					}
				}
				if(move_type == _CROP_MOVE_TOP || move_type == _CROP_MOVE_BOTTOM) {
					if(move_type & _CROP_MOVE_TOP)
						rc.crop_y1 = new_y;
					if(move_type & _CROP_MOVE_BOTTOM)
						rc.crop_y2 = new_y;
					double crop_l = rc.crop_y2 - rc.crop_y1;
					crop_l /= 2.0;
					crop_l *= aspect;
					if(crop_l <= crop_w) {
						rc.crop_x1 = crop_cx - crop_l;
						rc.crop_x2 = crop_cx + crop_l;
					} else {
						crop_l = crop_w;
						rc.crop_x1 = crop_cx - crop_l;
						rc.crop_x2 = crop_cx + crop_l;
						crop_l /= aspect;
						crop_l *= 2.0;
						if(move_type == _CROP_MOVE_TOP)
							rc.crop_y1 = rc.crop_y2 - crop_l;
						if(move_type == _CROP_MOVE_BOTTOM)
							rc.crop_y2 = rc.crop_y1 + crop_l;
					}
				}
				//--==-- corners
				if(move_type == _CROP_MOVE_LT || move_type == _CROP_MOVE_LB || move_type == _CROP_MOVE_RT || move_type == _CROP_MOVE_RB) {
					// check which edge is 'main', i.e. a source of resizing, and the align other
					QRect crop_r = view_crop_rect(image, mt->transform);
					double cx = crop_r.x() - image.x();
					cx += double(crop_r.width()) / 2.0;
					double cy = crop_r.y() - image.y();
					cy += double(crop_r.height()) / 2.0;
					double mx = mt->cursor_pos.x();
					double my = mt->cursor_pos.y();
					mx -= cx;
					my -= cy;
					double m_x = mx;
					double m_y = my;
					if(mx < 0)	mx = -mx;
					if(my < 0)	my = -my;

					bool edge_is_horizontal = false;
					double ca = crop_r.height();
					if(ca < 1.0)	ca = 1.0;
					ca = double(crop_r.width()) / ca;
					if(mx > my) {
						if(mx / ca < my) {
							edge_is_horizontal = true;
						}
					} else {
						if(my * ca > mx) {
							edge_is_horizontal = true;
						}
					}
					if(move_type == _CROP_MOVE_LT) {
						// right to crop - still horizontal
						if(m_x > 0 && m_y < 0)
							edge_is_horizontal = true;
						// down to crop - is vertical
						if(m_x < 0 && m_y > 0)
							edge_is_horizontal = false;
						if(!edge_is_horizontal) {
							// left
							double crop_new = rc.crop_y2 - (rc.crop_x2 - new_x) / aspect;
							if(crop_new >= rc.im_y1) {
								rc.crop_y1 = crop_new;
								rc.crop_x1 = new_x;
							} else {
								rc.crop_y1 = rc.im_y1;
								rc.crop_x1 = rc.crop_x2 - crop_h * aspect;
							}
						} else {
							// top
							double crop_new = rc.crop_x2 - (rc.crop_y2 - new_y) * aspect;
							if(crop_new >= rc.im_x1) {
								rc.crop_x1 = crop_new;
								rc.crop_y1 = new_y;
							} else {
								rc.crop_x1 = rc.im_x1;
								rc.crop_y1 = rc.crop_y2 - crop_w / aspect;
							}
						}
					}
					if(move_type == _CROP_MOVE_LB) {
						// etc...
						if(m_x > 0 && m_y > 0)
							edge_is_horizontal = true;
						if(m_x < 0 && m_y < 0)
							edge_is_horizontal = false;
						if(!edge_is_horizontal) {
							// left
							double crop_new = rc.crop_y1 + (rc.crop_x2 - new_x) / aspect;
							if(crop_new <= rc.im_y2) {
								rc.crop_y2 = crop_new;
								rc.crop_x1 = new_x;
							} else {
								rc.crop_y2 = rc.im_y2;
								rc.crop_x1 = rc.crop_x2 - crop_h * aspect;
							}
						} else {
							// bottom
							double crop_new = rc.crop_x2 - (new_y - rc.crop_y1) * aspect;
							if(crop_new >= rc.im_x1) {
								rc.crop_x1 = crop_new;
								rc.crop_y2 = new_y;
							} else {
								rc.crop_x1 = rc.im_x1;
								rc.crop_y2 = rc.crop_y1 + crop_w / aspect;
							}
						}
					}
					if(move_type == _CROP_MOVE_RT) {
						if(m_x < 0 && m_y < 0)
							edge_is_horizontal = true;
						if(m_x > 0 && m_y > 0)
							edge_is_horizontal = false;
						if(!edge_is_horizontal) {
							// right
							double crop_new = rc.crop_y2 - (new_x - rc.crop_x1) / aspect;
							if(crop_new >= rc.im_y1) {
								rc.crop_y1 = crop_new;
								rc.crop_x2 = new_x;
							} else {
								rc.crop_y1 = rc.im_y1;
								rc.crop_x2 = rc.crop_x1 + crop_h * aspect;
							}
						} else {
							// top
							double crop_new = rc.crop_x1 + (rc.crop_y2 - new_y) * aspect;
							if(crop_new <= rc.im_x2) {
								rc.crop_x2 = crop_new;
								rc.crop_y1 = new_y;
							} else {
								rc.crop_x2 = rc.im_x2;
								rc.crop_y1 = rc.crop_y2 - crop_w / aspect;
							}
						}
					}
					if(move_type == _CROP_MOVE_RB) {
						if(m_x < 0 && m_y > 0)
							edge_is_horizontal = true;
						if(m_x > 0 && m_y < 0)
							edge_is_horizontal = false;
						if(!edge_is_horizontal) {
							// right
							double crop_new = rc.crop_y1 + (new_x - rc.crop_x1) / aspect;
							if(crop_new <= rc.im_y2) {
								rc.crop_y2 = crop_new;
								rc.crop_x2 = new_x;
							} else {
								rc.crop_y2 = rc.im_y2;
								rc.crop_x2 = rc.crop_x1 + crop_h * aspect;
							}
						} else {
							// bottom
							double crop_new = rc.crop_x1 + (new_y - rc.crop_y1) * aspect;
							if(crop_new <= rc.im_x2) {
								rc.crop_x2 = crop_new;
								rc.crop_y2 = new_y;
							} else {
								rc.crop_x2 = rc.im_x2;
								rc.crop_y2 = rc.crop_y1 + crop_w / aspect;
							}
						}
					}
				}
			}
		}
		mouse_last_pos.setX(x);
		mouse_last_pos.setY(y);
		if(rc.crop_x1 < rc.im_x1)	rc.crop_x1 = rc.im_x1;
		if(rc.crop_x2 > rc.im_x2)	rc.crop_x2 = rc.im_x2;
		if(rc.crop_y1 < rc.im_y1)	rc.crop_y1 = rc.im_y1;
		if(rc.crop_y2 > rc.im_y2)	rc.crop_y2 = rc.im_y2;

		rc.apply_to_ps(ps);

//cerr << "crop: x = " << ps->crop.x1 << " - " << ps->crop.x2 << "; y = " << ps->crop.y1 << " - " << ps->crop.y2 << endl;
		if(ps->crop.x1 != crop_x1_prev || ps->crop.x2 != crop_x2_prev || ps->crop.y1 != crop_y1_prev || ps->crop.y2 != crop_y2_prev) {
			edit_mirror_cursor(cursor, rotation);
//cerr << "crop_x: " << ps->crop.x1 << " - " << ps->crop.x2 << "; crop_y: " << ps->crop.y1 << " - " << ps->crop.y2 << endl;
			return true;
		}
	}
	return false;
}

void F_Crop::edit_update_cursor(Cursor::cursor &cursor, FilterEdit_event_t *mt) {//QEvent *event, const QRect &image) {
	QEvent *event = mt->event;
	QRect image = mt->image;
	bool type_key = false;
	if(event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
		type_key = true;
	QMouseEvent *event_mouse = (QMouseEvent *)event;
	QKeyEvent *event_key = (QKeyEvent *)event;
	int x = mouse_last_pos_trans.x();
	int y = mouse_last_pos_trans.y();
	if(!type_key) {
		// mouse event
		x = mt->cursor_pos.x();
		y = mt->cursor_pos.y();
//		y = event_mouse->y() - image.y();
//		x = event_mouse->x() - image.x();
//		y = event_mouse->y() - image.y();
//		QPoint p = mt->transform.viewport_coords_to_image(mt->cursor_pos, false);
//		x = p.x();
//		y = p.y();
	}
	// check keyboard
	if(type_key) {
		if(mouse_is_pressed)
			return;
		if(event_key->type() == QEvent::KeyPress && mouse_is_pressed == false) {
			crop_move = _CROP_MOVE_SCRATCH;
			cursor = Cursor::cross;
			if(x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
//			if(x < image.x() || y < image.y() || x >= (image.width() + image.x()) || y >= (image.height() + image.y)) {
				// outside of the photo
				crop_move = _CROP_MOVE_UNDEFINED;
				cursor = Cursor::arrow;
			}
			return;
		}
	}
	// mouse, or key release
	int off_e = EDIT_RESIZE_CORNERS;
	int off_w = EDIT_RESIZE_WIDTH;
	// convert crop edges to viewport coordinates
	QRect rect = view_crop_rect(mt->image, mt->transform);
	int x_left = rect.x();
	int y_top = rect.y();
	int x_right = x_left + rect.width();
	int y_bottom = y_top + rect.height();
//cerr << "x = " << x_left << " - " << x_right << "; y = " << y_top << " - " << y_bottom << endl;
//cerr << "x == " << x << "; y == " << y << endl;
/*
	int x_left = int(ps->crop.x1 * image.width());
	int x_right = int(ps->crop.x2 * image.width());
	int y_top = int(ps->crop.y1 * image.height());
	int y_bottom = int(ps->crop.y2 * image.height());
*/
	// off_e - offset around corners
	// off_w - offset around edges
	int crop_move_prev = crop_move;
	crop_move = _CROP_MOVE_UNDEFINED;
	int off_h = off_e;
	int off_v = off_e;
	if(x > x_left + off_w && x < x_right - off_w && y > y_top + off_w && y < y_bottom - off_w) {
		crop_move = _CROP_MOVE_PAN;
	} else if(x >= x_left - off_w && x <= x_left + off_w) {
		if(y > y_top - off_w && y < y_top + off_v)
			crop_move = _CROP_MOVE_LT;
		if(y >= y_top + off_v && y < y_bottom - off_v)
			crop_move = _CROP_MOVE_LEFT;
		if(y >= y_bottom - off_v && y < y_bottom + off_w)
			crop_move = _CROP_MOVE_LB;
	} else if(x >= x_right - off_w && x <= x_right + off_w) {
		if(y > y_top - off_w && y < y_top + off_v)
			crop_move = _CROP_MOVE_RT;
		if(y >= y_top + off_v && y < y_bottom - off_v)
			crop_move = _CROP_MOVE_RIGHT;
		if(y >= y_bottom - off_v && y < y_bottom + off_w)
			crop_move = _CROP_MOVE_RB;
	} else if(y > y_top - off_w && y < y_top + off_v) {
		if(x >= x_left + off_h && x <= x_right - off_h)
			crop_move = _CROP_MOVE_TOP;
		if(x > x_left - off_w && x < x_left + off_h)
			crop_move = _CROP_MOVE_LT;
		if(x > x_right - off_h && x < x_right + off_w)
			crop_move = _CROP_MOVE_RT;
	} else if(y > y_bottom - off_w && y < y_bottom + off_w) {
		if(x >= x_left + off_h && x <= x_right - off_h)
			crop_move = _CROP_MOVE_BOTTOM;
		if(x > x_left - off_w && x < x_left + off_h)
			crop_move = _CROP_MOVE_LB;
		if(x > x_right - off_h && x < x_right + off_w)
			crop_move = _CROP_MOVE_RB;
	}
	// --
	bool scratch_flag = false;
	if(type_key == false)
		if(event_mouse->modifiers() & Qt::ControlModifier)
			scratch_flag = true;
	if(crop_move == _CROP_MOVE_UNDEFINED)
		scratch_flag = true;
	if(scratch_flag) {
		crop_move = _CROP_MOVE_SCRATCH;
		cursor = Cursor::cross;
	}
	// --
	if(crop_move_prev != crop_move) {
		if(crop_move == _CROP_MOVE_LEFT || crop_move == _CROP_MOVE_RIGHT)
			cursor = Cursor::size_hor;
		if(crop_move == _CROP_MOVE_TOP || crop_move == _CROP_MOVE_BOTTOM)
			cursor = Cursor::size_ver;
		if(crop_move == _CROP_MOVE_LT || crop_move == _CROP_MOVE_RB)
			cursor = Cursor::size_fdiag;
		if(crop_move == _CROP_MOVE_LB || crop_move == _CROP_MOVE_RT)
			cursor = Cursor::size_bdiag;
		if(crop_move == _CROP_MOVE_PAN)
			cursor = Cursor::hand_open;
		// outside of the image
		if(crop_move == _CROP_MOVE_UNDEFINED)
			cursor = Cursor::arrow;
	}
	if(x < image.x() || y < image.y() || x >= (image.width() + image.x()) || y >= (image.height() + image.y())) {
//	if(x < 0 || y < 0 || x >= image_w || y >= image_h) {
		// outside of the photo
		crop_move = _CROP_MOVE_UNDEFINED;
		cursor = Cursor::arrow;
	}
//cerr << "___ crop_move == " << crop_move << endl;
	mouse_last_pos.setX(x);
	mouse_last_pos.setY(y);
}

// mirror cursor along axis
void F_Crop::edit_mirror_cursor(Cursor::cursor &cursor, int rotation) {
	// mirror cursor
	bool switch_x = false;
	bool switch_y = false;
	rotated_crop_t rc(ps, rotation);
	if(rc.crop_x2 < rc.crop_x1) {
		double c = rc.crop_x2;
		rc.crop_x2 = rc.crop_x1;
		rc.crop_x1 = c;
		if(crop_move == _CROP_MOVE_LEFT) {
			crop_move = _CROP_MOVE_RIGHT;
		} else {
			if(crop_move == _CROP_MOVE_RIGHT)
				crop_move = _CROP_MOVE_LEFT;
		}
		switch_x = true;
	}
	if(rc.crop_y2 < rc.crop_y1) {
		double c = rc.crop_y2;
		rc.crop_y2 = rc.crop_y1;
		rc.crop_y1 = c;
		if(crop_move == _CROP_MOVE_TOP) {
			crop_move = _CROP_MOVE_BOTTOM;
		} else {
			if(crop_move == _CROP_MOVE_BOTTOM)
			crop_move = _CROP_MOVE_TOP;
		}
		switch_y = true;
	}
	if(switch_x || switch_y) {
		if(switch_x && switch_y) {
			if(crop_move == _CROP_MOVE_LT) {
				crop_move = _CROP_MOVE_RB;
			} else {
				if(crop_move == _CROP_MOVE_RB)
					crop_move = _CROP_MOVE_LT;
			}
			if(crop_move == _CROP_MOVE_RT) {
				crop_move = _CROP_MOVE_LB;
			} else {
				if(crop_move == _CROP_MOVE_LB)
					crop_move = _CROP_MOVE_RT;
			}
		} else {
			if(switch_x) {
				if(crop_move == _CROP_MOVE_LT) {
					crop_move = _CROP_MOVE_RT;
				} else {
					if(crop_move == _CROP_MOVE_RT)
						crop_move = _CROP_MOVE_LT;
				}
				if(crop_move == _CROP_MOVE_LB) {
					crop_move = _CROP_MOVE_RB;
				} else {
					if(crop_move == _CROP_MOVE_RB)
						crop_move = _CROP_MOVE_LB;
				}
			} else {
				if(crop_move == _CROP_MOVE_LT) {
					crop_move = _CROP_MOVE_LB;
				} else {
					if(crop_move == _CROP_MOVE_LB)
						crop_move = _CROP_MOVE_LT;
				}
				if(crop_move == _CROP_MOVE_RT) {
					crop_move = _CROP_MOVE_RB;
				} else {
					if(crop_move == _CROP_MOVE_RB)
						crop_move = _CROP_MOVE_RT;
				}
			}
		}
		if(crop_move == _CROP_MOVE_LT || crop_move == _CROP_MOVE_RB)
			cursor = Cursor::size_fdiag;
		if(crop_move == _CROP_MOVE_LB || crop_move == _CROP_MOVE_RT)
			cursor = Cursor::size_bdiag;
	}
}
//------------------------------------------------------------------------------
