#ifndef __H_GUI_CURVE__
#define __H_GUI_CURVE__
/*
 * gui_curve.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>
#include "cm.h"

//------------------------------------------------------------------------------
class curve_channel_t {
public:
	enum curve_channel {
		channel_rgb,
		channel_red,
		channel_green,
		channel_blue,
		channel_all
	};
};

//------------------------------------------------------------------------------
// class-user of GUI_Curve should implement this interface
class GUI_Histogram : public QObject {
	Q_OBJECT

public:
	virtual void draw_histogram(QPainter *painter, int size_w, int size_h);
	QVector<long> rescale_histogram(const QVector<long> &hist, int new_size);

signals:
	void signal_update(void);
};

//------------------------------------------------------------------------------
class GUI_Curve : public QWidget {
	Q_OBJECT

public:
	enum channels_type_en {
		channels_rgb,			// edit each channels separatelly and|or all together as one
		channels_lightness,		// one channel only shown as white
	};

	GUI_Curve(GUI_Curve::channels_type_en _channels_type, QWidget *parent = nullptr);
	GUI_Curve(GUI_Curve::channels_type_en _channels_type, int width, int height, QWidget *parent = nullptr);
	void set_histogram(GUI_Histogram *_histogram);
	QSize sizeHint(void) const;
	void set_spline_options(int _left_type, float _left_df, int _right_type, float _right_df);
	void set_enabled(bool _enabled, const QVector<QVector<QPointF> > &_curves, curve_channel_t::curve_channel channel, QVector<float> levels);
	bool is_enabled(void);
	void set_channel_index(curve_channel_t::curve_channel channel);
	void set_CM(std::string cm_name);
	void emit_update(void);

public slots:
	void slot_update_from_histogram(void);

signals:
	void signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &);

protected:
	void init(void);
	void mouseMoveEvent(QMouseEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);
	void paintEvent(QPaintEvent *event);
	void draw(QPainter *painter);
	void mouse_event(QMouseEvent *event);
	bool update_active_index(QMouseEvent *event);

	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);

	channels_type_en channels_type;
	QPointF mouse_event_coords_unclipped(QMouseEvent *event);
	QPointF mouse_event_coords(QMouseEvent *event);

	int size_w;
	int size_h;
	int shift_x;
	int shift_y;
	int shift_y2;

	int left_type;
	float left_df;
	int right_type;
	float right_df;
	CM::cm_type_en cm_type;
	QVector<int> cm_type_bw;

	QVector<QVector<QPointF> > curves;
	QVector<float> levels;
	int point_active_index;
	int level_active_index;
	curve_channel_t::curve_channel curve_active_index;
	long point_radius_create;
	long point_radius_remove;
	bool point_to_be_removed(QPointF position);
	void do_update(void);
	void points_normalize(void);
	QVector<QPointF> on_edit_points(void);

	bool active;
	bool in_edit_mode;

	class GUI_Histogram *histogram;
};

//------------------------------------------------------------------------------

#endif //__H_GUI_CURVE__
