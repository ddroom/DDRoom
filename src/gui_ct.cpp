/*
 * gui_ct.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "gui_ct.h"
#include "gui_ct_picker.h"
#include "ddr_math.h"
#include "gui_slider.h"

#define _POINT_RADIUS_SELECTION	20
#define _POINT_RADIUS_REMOVE	4

#include <iostream>
using namespace std;

#define SIZE_W 256
#define SIZE_H 256

//==============================================================================
GUI_CT_config::GUI_CT_config(double _cct_min, double _cct_max, double _cct_middle) {
	cct_middle = _cct_middle;
	cct_min = _cct_min;
	cct_max = _cct_max;
	duv_middle = 0.0;
	duv_min = -500.0;
	duv_max = 500.0;
//	duv_min = -200.0;
//	duv_max = 200.0;
//	duv_min = -50.0;
//	duv_max = 50.0;
}

GUI_CT_config &GUI_CT_config::operator =(double _cct_middle) {
	cct_middle = _cct_middle;
	return *this;
}

//==============================================================================
GUI_CT::GUI_CT(GUI_CT_config _config, QWidget *parent) : QWidget(parent) {
	config = _config;
//	size_w = 256;
//	size_h = 64;
	v_cct = config.cct_middle;
	v_duv = config.duv_middle;

	//--
	QVBoxLayout *vb = new QVBoxLayout(this);
	vb->setSpacing(2);
	vb->setContentsMargins(0, 0, 0, 0);
	vb->setSizeConstraint(QLayout::SetMinimumSize);

	//--
	QGridLayout *t_grid = new QGridLayout();
	t_grid->setSpacing(2);
	t_grid->setContentsMargins(2, 1, 2, 1);
	vb->addLayout(t_grid);

	t_grid->addWidget(new QLabel(tr("Temp")), 0, 0);
	slider_cct = new GuiSlider(config.cct_min, config.cct_max, v_cct, 1, 1, 1000);
	slider_cct->enable_spinbox_stepBy_update(true);
	slider_cct->getSlider()->setSingleStep(25.0);;
	slider_cct->getSlider()->setPageStep(25.0);;
	slider_cct->getSpinBox()->setSingleStep(50.0);;
	t_grid->addWidget(slider_cct, 0, 1);

	t_grid->addWidget(new QLabel(tr("Tint")), 1, 0);
	slider_duv = new GuiSlider(config.duv_min, config.duv_max, v_duv, 1, 1, 50);
	slider_duv->enable_spinbox_stepBy_update(true);
	slider_duv->getSlider()->setSingleStep(5.0);;
	slider_duv->getSlider()->setPageStep(5.0);;
	slider_duv->getSpinBox()->setSingleStep(5.0);;
	t_grid->addWidget(slider_duv, 1, 1);

	slider_temp_connect(true);
}

void GUI_CT::get_temp(double &cct, double &duv) {
	cct = v_cct;
	duv = v_duv;
}

void GUI_CT::set_temp(double cct, double duv) {
	// check limits
	if(cct > config.cct_max)
		cct = config.cct_max;
	if(cct < config.cct_min)
		cct = config.cct_min;
	if(duv > config.duv_max)
		duv = config.duv_max;
	if(duv < config.duv_min)
		duv = config.duv_min;
	//
	v_cct = cct;
	v_duv = duv;
	// update UI
	slider_temp_connect(false);
	slider_cct->setValue(v_cct);
	slider_duv->setValue(v_duv);
	slider_temp_connect(true);
}

void GUI_CT::slider_temp_connect(bool flag) {
	if(flag) {
		connect(slider_cct, SIGNAL(signal_changed(double)), this, SLOT(changed_cct(double)));
		connect(slider_duv, SIGNAL(signal_changed(double)), this, SLOT(changed_duv(double)));
	} else {
		disconnect(slider_cct, SIGNAL(signal_changed(double)), this, SLOT(changed_cct(double)));
		disconnect(slider_duv, SIGNAL(signal_changed(double)), this, SLOT(changed_duv(double)));
	}
}

void GUI_CT::changed_cct(double value) {
	if(v_cct != value) {
		v_cct = value;
//		update_picker();
		emit signal_ct_changed(v_cct, v_duv);
	}
}

void GUI_CT::changed_duv(double value) {
	if(v_duv != value) {
		v_duv = value;
//		update_picker();
		emit signal_ct_changed(v_cct, v_duv);
	}
}

//------------------------------------------------------------------------------


#if 0
//------------------------------------------------------------------------------
GUI_Curve::GUI_Curve(GUI_Curve::channels_type_en _channels_type, QWidget *parent) : QWidget(parent) {
	channels_type = _channels_type;
	size_w = SIZE_W;
	size_h = SIZE_H;
	shift_x = 5;
	shift_y = 5;
	shift_y2 = 14;

	in_edit_mode = false;
	enabled = false;
	point_active_index = -1;
	level_active_index = -1;
	curve_active_index = curve_channel_t::channel_all;

	left_type = 2;
	left_df = 1.0;
	right_type = 2;
	right_df = 1.0;
	cm_type = CM::cm_type_CIECAM02;

	point_radius_create = _POINT_RADIUS_SELECTION * _POINT_RADIUS_SELECTION;
	point_radius_remove = _POINT_RADIUS_REMOVE * _POINT_RADIUS_REMOVE;

	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setFocusPolicy(Qt::NoFocus);
	QSize s = sizeHint();
	setFixedWidth(s.width());
	setFixedHeight(s.height());

	histogram = nullptr;
}

void GUI_Curve::set_histogram(GUI_Histogram *_histogram) {
	if(histogram != nullptr) {
		disconnect(histogram, SIGNAL(signal_update(void)), this, SLOT(slot_update_from_histogram(void)));
	}
	histogram = _histogram;
	connect(histogram, SIGNAL(signal_update(void)), this, SLOT(slot_update_from_histogram(void)));
}

void GUI_Curve::slot_update_from_histogram(void) {
	emit_update();
}

QSize GUI_Curve::sizeHint(void) const {
	int w = size_w;
	w += shift_x * 2;
	int h = size_h;
	h += shift_y + shift_y2;
	h += 2;	// 1 + 1 - spaces
	h += 8;	// 8 for slider
	return QSize(w, h);
}

void GUI_Curve::set_spline_options(int _left_type, float _left_df, int _right_type, float _right_df) {
	left_type = _left_type;
	left_df = _left_df;
	right_type = _right_type;
	right_df = _right_df;
}

void GUI_Curve::paintEvent(QPaintEvent *event) {
	QPainter painter(this);
	draw(&painter);
}

void GUI_Curve::set_CM(std::string cm_name) {
	CM::cm_type_en _cm_type = CM::get_type(cm_name);
	if(cm_type != _cm_type) {
		cm_type = _cm_type;
		cm_type_bw = QVector<int>(0);
	}
}

void GUI_Curve::set_enabled(bool _enabled, const QVector<QVector<QPointF> > &_curves, curve_channel_t::curve_channel channel, QVector<float> _levels, bool allow_edit) {
	enabled = _enabled;
	curves = _curves;
	levels = _levels;
	point_active_index = -1;
	level_active_index = -1;
	curve_active_index = channel;
	if(enabled && allow_edit) {
		setCursor(Qt::CrossCursor);
		setMouseTracking(true);
	} else {
		setCursor(Qt::ArrowCursor);
		setMouseTracking(false);
	}
	in_edit_mode = false;
}

bool GUI_Curve::is_enabled(void) {
	return enabled;
}

void GUI_Curve::set_channel_index(curve_channel_t::curve_channel channel) {
	if(channel != curve_active_index) {
		curve_active_index = channel;
		if(enabled) {
			emit_update();
		}
	}
}

void GUI_Curve::mouseMoveEvent(QMouseEvent *event) {
	if(!enabled)
		return;
	mouse_event(event);
}

void GUI_Curve::mousePressEvent(QMouseEvent *event) {
	if(!enabled)
		return;
	if(curves.size() < 1)
		return;
	if(event->button() == Qt::LeftButton) {
		in_edit_mode = true;
		bool skip = false;
		if(level_active_index != -1) {
			skip = true;
		} else {
			long _y = -(event->pos().y() - (255 + shift_y));
			if(_y < 0 - 2)
				skip = true;
		}
		if(point_active_index == -1 && skip == false) {
			// create a new point
			curves[curve_active_index].push_back(QPointF(event->pos()));
			point_active_index = curves[curve_active_index].size() - 1;
		}
	}
	mouse_event(event);
}

void GUI_Curve::mouseReleaseEvent(QMouseEvent *event) {
	if(!enabled)
		return;
	if(curves.size() < 1)
		return;
	if(event->button() == Qt::LeftButton) {
		in_edit_mode = false;
		bool skip = false;
		if(level_active_index != -1) {
			skip = true;
			// don't reset active level
//			level_active_index = -1;
			do_update();
			emit signal_curve_update(curves, levels);
		}
		if(point_active_index != -1 && skip == false) {
			// check to remove points
			QPointF mouse_position = mouse_event_coords(event);
			mouse_position.setX(mouse_position.x() / (size_w - 1));
			mouse_position.setY(mouse_position.y() / (size_h - 1));
			if(point_should_be_removed(mouse_position)) {
				curves[curve_active_index].remove(point_active_index);
				point_active_index = -1;
				do_update();
			}
			// because GUI will be disabled during process
			// don't reset active point
//			point_active_index = -1;
//			level_active_index = -1;
			do_update();
			emit signal_curve_update(curves, levels);
		}
	}
}

bool GUI_Curve__point_less_than(const QPointF &p1, const QPointF &p2) {
	if(p1.x() == p2.x())
		return p1.y() < p2.y();
	return p1.x() < p2.x();
}

void GUI_Curve::points_normalize(void) {
	QVector<QPointF> &points = curves[curve_active_index];
	QPointF current_point;
	bool to_sort = false;
	for(int i = 0; i < points.size() - 1; ++i) {
		if(points[i].x() > points[i + 1].x()) {
			to_sort = true;
			break;
		}
	}
	if(to_sort) {
		if(point_active_index != -1)
			current_point = points[point_active_index];
		qSort(points.begin(), points.end(), GUI_Curve__point_less_than);
		if(point_active_index != -1) {
			for(int i = 0; i < points.size(); ++i) {
				if(points[i] == current_point) {
					point_active_index = i;
					break;
				}
			}
		}
/*
		for(int i = 0; i < points.size(); ++i) {
			cerr << "points[" << i << "].x() == " << points[i].x() << endl;
		}
		cerr << endl;
*/
	}
}

void GUI_Curve::emit_update(void) {
	emit update();
}

void GUI_Curve::do_update(void) {
	points_normalize();
	emit_update();
}

bool GUI_Curve::point_should_be_removed(QPointF position) {
	QVector<QPointF> &points = curves[curve_active_index];
	if(points.size() > 2) {
		float x = position.x();
		float y = position.y();
		for(int i = 0; i < points.size(); ++i) {
			if(i == point_active_index)
				continue;
			float x2 = (size_w - 1) * (size_w - 1);
			float y2 = (size_h - 1) * (size_h - 1);
			float len = (x - points[i].x()) * (x - points[i].x()) * x2 + (y - points[i].y()) * (y - points[i].y()) * y2;
			if(len <= point_radius_remove) {
				return true;
/*
				points.remove(i);
				do_update();
				break;
*/
			}
		}
	}
	return false;
}

void GUI_Curve::enterEvent(QEvent *event) {
//	cerr << "event enter" << endl;
}

void GUI_Curve::leaveEvent(QEvent *event) {
	if(in_edit_mode)
		return;
	if(point_active_index != -1 || level_active_index != -1) {
		point_active_index = -1;
		level_active_index = -1;
		do_update();
	}
//	cerr << "event leave" << endl;
}

QPointF GUI_Curve::mouse_event_coords(QMouseEvent *event) {
	int x = event->pos().x();
	int y = event->pos().y();
	x = x - shift_x;
	y = -(y - (255 + shift_y));
	if(x < 0)	x = 0;
	if(x > 255)	x = 255;
	if(y < 0)	y = 0;
	if(y > 255)	y = 255;
	return QPointF(x, y);
}

void GUI_Curve::mouse_event(QMouseEvent *event) {
	if(curves.size() < 1)
		return;
	QVector<QPointF> &points = curves[curve_active_index];
	if(event->buttons() & Qt::LeftButton) {
		bool done = false;
		bool update = false;
		// check levels
		if(level_active_index != -1) {
			update = true;
			done = true;
			float x = mouse_event_coords(event).x();
			x /= 255;
			if(x < 0.0)	x = 0.0;
			if(x > 1.0)	x = 1.0;
			levels[level_active_index] = x;
		}
		// check points
		if(point_active_index != -1 && done == false) {
			update = true;
			// move exist point
			QPointF point = mouse_event_coords(event);
			points[point_active_index] = QPointF(point.x() / 255, point.y() / 255);

			// switch - should be 'normalization'
			if(points[0].x() > points[1].x()) {
				point = points[0];
				points[0] = points[1];
				points[1] = point;
				if(point_active_index == 1) {
					point_active_index = 0;
				} else {
					point_active_index = 1;
				}
			}
		}
		if(update)
			do_update();
	} else {
		QPointF point = mouse_event_coords(event);
		long x = point.x();
		long y = point.y();
		bool done = false;
		bool update = false;
		// try to catch levels here
		if(y == 0) {
			// check bottom levels
			long _x = event->pos().x() - shift_x;
			long _y = -(event->pos().y() - (255 + shift_y));
			if(_y < 0 - 2) {
				// levels
				done = true;
				if(levels.size() > 1) {
					int level_active_index_prev = level_active_index;
					level_active_index = -1;
					// skip black level for lightness
					int levels_start = 0;
//					if(channels_type == channels_lightness)
//						levels_start = 1;
					for(int i = levels_start; i < 2; ++i) {
						int pos_x = (levels[i] * 255.0 + 0.5);
						if(_x > pos_x - 5 && _x < pos_x + 5) {
							level_active_index = i;
							break;
						}
					}
					if(level_active_index != level_active_index_prev)
						update = true;
				}
			}
		} else {
			// reset active level
			if(level_active_index != -1) {
				level_active_index = -1;
				update = true;
			}
		}
		// otherwise, check points
		if(!done) {
			QVector<long> lengths;
			for(int i = 0; i < points.size(); ++i) {
				lengths.push_back((x - points[i].x() * 255) * (x - points[i].x() * 255) + (y - points[i].y() * 255) * (y - points[i].y() * 255));
			}
			long near_length = lengths[0];
			int near_index = 0;
			for(int i = 0; i < lengths.size(); ++i) {
				if(lengths[i] < near_length) {
					near_length = lengths[i];
					near_index = i;
				}
			}
			int pai = point_active_index;
			if(near_length < point_radius_create)
				point_active_index = near_index;
			else
				point_active_index = -1;
			if(point_active_index != pai) {
				update = true;
			}
		} else {
			// reset selection
			if(point_active_index != -1) {
				point_active_index = -1;
				update = true;
			}
		}
		if(update)
			do_update();
	}
}

/*
 * points that will be on mouse button release by user
 * should be used to draw curve and fill table
 * (probably on mouseRelease handler too...)
 */
QVector<QPointF> GUI_Curve::on_edit_points(void) {
	QVector<QPointF> &points = curves[curve_active_index];
	if(points.size() == 2)
		return points;
	QVector<QPointF> rez;
	for(int i = 0; i < points.size(); ++i) {
		bool to_remove = false;
		if(i == point_active_index) {
			if(point_should_be_removed(points[i]))
				to_remove = true;
		}
		if(!to_remove)
			rez.push_back(points[i]);
	}
	return rez;
}

void GUI_Curve::draw(QPainter *_painter) {
	int x_max = size_w - 1;
	int y_max = size_h - 1;
	int rx_1 = 0.25 * x_max;
	int rx_2 = 0.50 * x_max;
	int rx_3 = 0.75 * x_max;
//	int rx_4 = 1.00 * x_max;
	int ry_1 = 0.25 * y_max;
	int ry_2 = 0.50 * y_max;
	int ry_3 = 0.75 * y_max;
//	int ry_4 = 1.00 * y_max;
//	int rw = rx_4 - 1;
	QImage image_bg(sizeHint(), QImage::Format_ARGB32_Premultiplied);
	QPainter imagePainter(&image_bg);
	imagePainter.initFrom(this);
	imagePainter.eraseRect(rect());
	QPainter *painter = &imagePainter;
//	painter->scale(1.0, -1.0);
	int gx_1 = rx_1;
	int gx_2 = rx_2;
	int gx_3 = rx_3;
//	int gx_4 = rx_4;

	bool debug = false;

	// put 0,0 to left bottom point inside of box
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->save();
	QTransform tr;
	tr.translate(shift_x, shift_y);
	tr.rotate(-180.0, Qt::XAxis);
//	tr.translate(0.5, -255.5);
	float translate_y = (float)size_h - 0.5;
	tr.translate(0.5, -translate_y);
	painter->setWorldTransform(tr);

	// debug only
	if(debug)
		painter->fillRect(QRect(-20, -20, size_w + 40, size_h + 40), QColor(0x3F, 0xFF, 0x3F));

	QColor grid = QColor(0x7F, 0x7F, 0x7F);
	painter->setPen(Qt::black);
	// main box
	painter->fillRect(QRect(-1, -1, x_max + 2, y_max + 2), QColor(0x2F, 0x2F, 0x2F));
	painter->drawRect(-1, -1, x_max + 2, y_max + 2);
	// grids
	painter->setPen(grid);
	painter->drawLine(0, ry_1, x_max, ry_1);
	painter->drawLine(0, ry_2, x_max, ry_2);
	painter->drawLine(0, ry_3, x_max, ry_3);
	painter->drawLine(gx_1, 0, gx_1, y_max);
	painter->drawLine(gx_2, 0, gx_2, y_max);
	painter->drawLine(gx_3, 0, gx_3, y_max);

	painter->setPen(QColor(0xFF, 0xFF, 0xFF, 0x1F));
	painter->drawLine(0, 0, x_max, y_max);
	// draw grids in other way for WB

	QColor curve_colors[] = {
		QColor(0xFF, 0xFF, 0xFF),
		QColor(0xFF, 0x00, 0x00),
		QColor(0x00, 0xFF, 0x00),
		QColor(0x00, 0x00, 0xFF),
	};

	// histograms
	if(enabled && histogram != nullptr)
		histogram->draw_histogram(painter, size_w, size_h);

	painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
	// curve
	if(curve_active_index != curve_channel_t::channel_all) {
		QVector<curve_channel_t::curve_channel> indexes;
		// depend on channels type
		if(channels_type == channels_rgb) {
			indexes.push_back(curve_channel_t::channel_blue);
			indexes.push_back(curve_channel_t::channel_green);
			indexes.push_back(curve_channel_t::channel_red);
		}
		indexes.push_back(curve_channel_t::channel_rgb);
		for(int i = 0; i < indexes.size(); ++i) {
			if(indexes[i] == curve_active_index) {
				indexes.remove(i);
				break;
			}
		}
		indexes.push_back(curve_active_index);
		for(int i = 0; i < indexes.size(); ++i) {
			QVector<QPointF> cpoints;
			QPen pen(curve_colors[indexes[i]]);
			if(indexes[i] == curve_active_index) {
				cpoints = on_edit_points();
				pen.setWidthF(1.50);
			} else {
				cpoints = curves[indexes[i]];
				pen.setWidthF(1.00);
			}
			painter->setPen(pen);
			// spline
			Spline_Calc spline(cpoints, 1.0, true, left_type, left_df, right_type, right_df);
			float _px = cpoints[0].x();
			float _py = cpoints[0].y();
			for(int i = 0; i < x_max; ++i) {
				float _x = float(i) / x_max;
				float _y = spline.f(_x) * x_max;
				_x *= x_max;
				if(!(int(_px) == 0 && int(_x) == 0)) // to avoid parasitic vertical line
					painter->drawLine(QPointF(_px, _py), QPointF(_x, _y));
				_px = _x;
				_py = _y;
			}
		}
	}

	// black-to-white lightness (as argument of curve function) bar
	painter->setPen(Qt::black);
	painter->drawRect(-1, -1, x_max + 2, -14);
	if(cm_type_bw.size() == 0) {
		CM_to_CS converter(cm_type, "sRGB");
		float Jsh[3] = {0.0, 0.0, 0.0};
		float RGB[3] = {0.0, 0.0, 0.0};
		cm_type_bw = QVector<int>(size_w);
		for(int i = 0; i < size_w; ++i) {
			Jsh[0] = float(i) / size_w;
			converter.convert(RGB, Jsh);
			cm_type_bw[i] = RGB[0] * 0xFF;
		}
	}
	for(int i = 0; i < size_w; ++i) {
		// TODO: check output color space - how correct is that usage?
		int c = cm_type_bw[i];
		painter->setPen(QColor(c, c, c, 0xFF));
		QPoint p1(i, -2);
		QPoint p2(i, -14);
		painter->drawLine(p1, p2);
	}
	
	// levels
	int len = levels.size();
	if(len > 2)	len = 2;
	if(len == 2) {
		QPointF arrow[3];
		QColor color_pen[2]		= {QColor(Qt::white), QColor(Qt::black)};
		QColor color_brush[2]	= {QColor(Qt::black), QColor(Qt::white)};
//		QColor color_line[2]	= {QColor(Qt::black), QColor(Qt::white)};
		QColor color_line[2]	= {QColor(0xBF, 0xBF, 0xBF), QColor(0xFF, 0xFF, 0xFF)};
		// skip black level for lightness
		for(int i = 0; i < 2; ++i) {
			QPen pen(color_pen[i]);
			pen.setWidthF(1.00);
			if(level_active_index == i)
				pen.setWidthF(2.00);
			painter->setPen(pen);
			painter->setBrush(color_brush[i]);
			int l_x = int(levels[i] * 255 + 0.5);
			arrow[0] = QPointF(l_x    , -2);
			arrow[1] = QPointF(l_x - 4, -13);
			arrow[2] = QPointF(l_x + 4, -13);
			painter->drawPolygon(arrow, 3);
			if(level_active_index == i) {
				QPen pen_l(color_line[i]);
				if(i == 1)
					pen_l.setStyle(Qt::DotLine);
				else
					pen_l.setStyle(Qt::DashDotLine);
//					pen_l.setStyle(Qt::DashDotDotLine);
				painter->setPen(pen_l);
				painter->drawLine(QPointF(l_x, 0), QPointF(l_x, 255));
			}
		}
	}

	// points
	QPen pen = QPen(QColor(0x00, 0x00, 0x00));
	pen.setWidthF(1.00);
	painter->setPen(pen);
//	painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
	if(curve_active_index != curve_channel_t::channel_all) {
		painter->setBrush(curve_colors[curve_active_index]);
		pen = QPen(curve_colors[curve_active_index]);
		const QVector<QPointF> &points = curves[curve_active_index];
		for(int i = 0; i < points.size(); ++i) {
			if(i != point_active_index)
				painter->drawEllipse(QPointF(points[i].x() * 255.0, points[i].y() * 255.0), 3.0, 3.0);
		}

		// selected point
		if(point_active_index != -1) {
			QColor color(curve_colors[curve_active_index]);
			color.setAlpha(0xBF);
//			QPen pen_l(QColor(0xBF, 0xBF, 0xBF, 0x7F));
			QPen pen_l(color);
			pen_l.setStyle(Qt::DotLine);
			painter->setPen(pen_l);
			QPointF p(points[point_active_index]);
			painter->drawLine(QPointF(p.x() * 255.0, 0.0), QPointF(p.x() * 255.0, 255.0));
			painter->drawLine(QPointF(0.0, p.y() * 255.0), QPointF(255.0, p.y() * 255.0));
			// circle around
			color.setAlpha(0x5F);
			painter->setPen(color);
			color.setAlpha(0x3F);
			painter->setBrush(color);
			painter->drawEllipse(QPointF(p.x() * 255.0, p.y() * 255.0), _POINT_RADIUS_SELECTION, _POINT_RADIUS_SELECTION);
			// point
			painter->setPen(QColor(0xFF, 0xFF, 0xFF));
			painter->setBrush(QColor(0x00, 0x00, 0x00));
			painter->drawEllipse(QPointF(p.x() * 255.0, p.y() * 255.0), 3.0, 3.0);
		}
	}
	painter->restore();

	// point coordinates
	if(enabled && (point_active_index != -1 || level_active_index != -1)) {
		painter->setWorldTransform(QTransform().translate(shift_x, shift_y));
		QColor text_color(0xFF, 0xFF, 0xFF);
		if(point_active_index && level_active_index == -1) {
			bool to_remove = point_should_be_removed(QPointF(curves[curve_active_index][point_active_index]));
			if(to_remove)
				text_color = QColor(0xFF, 0x3F, 0x3F);
		}
		// draw 'OSD' point/level info
		QString str;

		QFont fnt = painter->font();
		fnt.setStyleHint(QFont::Courier);
		fnt.setFamily("Fixed");
		painter->setFont(fnt);

		QFontMetrics qfm(painter->font());
//		str = "out: 000.00";
//		str = "out: 000";
		str = "out: 0.000";
		QRect bg_rect = qfm.boundingRect(str);
		int _w = bg_rect.width();
		int _h = bg_rect.height();
		bg_rect.setX(10);
		bg_rect.setY(10);
		bg_rect.setHeight(_h * 2 + 10);
		bg_rect.setWidth(_w + 10);
		painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter->setPen(QColor(0x0F, 0x0F, 0x0F, 0x9F));
		painter->setBrush(QColor(0x0F, 0x0F, 0x0F, 0x7F));
		painter->drawRoundedRect(bg_rect, 5.0, 5.0);

		float value_x = 0;
		float value_y = 0;
		if(point_active_index != -1) {
			value_x = curves[curve_active_index][point_active_index].x();
			value_y = curves[curve_active_index][point_active_index].y();
		}
		if(level_active_index != -1) {
			if(level_active_index == 0)
				value_y = 0;
			else
				value_y = 1;
			value_x = levels[level_active_index];
		}

		str = " in: %1";
		str = str.arg(value_x, 4, 'f', 3);
//		str = str.arg(value_x, 4, 'f', 2);
//		str = str.arg((int)(value_x + 0.005), 3);
		painter->setPen(QColor(0, 0, 0));
		painter->drawText(QPointF(15, 10 + qfm.height()), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(15 - 1, 10 + qfm.height() - 1), str);

		str = "out: %1";
		str = str.arg(value_y, 4, 'f', 3);
//		str = str.arg(value_y, 4, 'f', 2);
//		str = str.arg((int)(value_y + 0.005), 3);
		painter->setPen(QColor(0, 0, 0));
		painter->drawText(QPointF(15, 10 + qfm.height() * 2), str);
		painter->setPen(text_color);
		painter->drawText(QPointF(15 - 1, 10 + qfm.height() * 2 - 1), str);
	}
	_painter->drawImage(0, 0, image_bg);
}

//------------------------------------------------------------------------------
#endif

//------------------------------------------------------------------------------
