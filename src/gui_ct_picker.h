#ifndef __H_GUI_CT_PICKER__
#define __H_GUI_CT_PICKER__
/*
 * gui_ct_picker.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>
#include "cm.h"	// TODO: why it's here?

//==============================================================================
class GUI_CT_Picker {
public:
	GUI_CT_Picker(class GUI_CT_config *config);
	void set_temp(double cct, double duv);
	// size hint - minimal size
	// drawing function

	// callback on mouse change
};

//==============================================================================

#if 0
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
class GUI_Curve : public QWidget {
	Q_OBJECT

public:
	enum channels_type_en {
		channels_rgb,			// edit each channels separatelly and|or all together as one
		channels_lightness,		// one channel only shown as white
	};

	GUI_Curve(GUI_Curve::channels_type_en _channels_type, QWidget *parent = nullptr);
	void set_histogram(GUI_Histogram *_histogram);
	QSize sizeHint(void) const;
	void set_spline_options(int _left_type, float _left_df, int _right_type, float _right_df);
	void set_enabled(bool _enabled, const QVector<QVector<QPointF> > &_curves, curve_channel_t::curve_channel channel, QVector<float> levels, bool allow_edit = true);
	bool is_enabled(void);
	void set_channel_index(curve_channel_t::curve_channel channel);
	void set_CM(std::string cm_name);
	void emit_update(void);

public slots:
	void slot_update_from_histogram(void);

signals:
	void signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &);

protected:
	void mouseMoveEvent(QMouseEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void paintEvent(QPaintEvent *event);
	void draw(QPainter *painter);
	void mouse_event(QMouseEvent *event);

	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);

	channels_type_en channels_type;
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
	bool point_should_be_removed(QPointF position);
	void do_update(void);
	void points_normalize(void);
	QVector<QPointF> on_edit_points(void);

	bool enabled;
	bool in_edit_mode;

	class GUI_Histogram *histogram;
};
#endif

//------------------------------------------------------------------------------

#endif //__H_GUI_CT_PICKER__
