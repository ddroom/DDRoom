#ifndef __H_GUI_CURVE_HISTOGRAM__
#define __H_GUI_CURVE_HISTOGRAM__
/*
 * gui_curve_histogram.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

//#include "area.h"
//#include "filter.h"
#include "gui_curve.h"
//#include "mt.h"

//------------------------------------------------------------------------------
class GUI_Curve_Histogram_data {
public:
	GUI_Curve_Histogram_data(void);

	QVector<long> hist_before;
	QVector<long> hist_after;
	QVector<long> hist_before_scaled;
	QVector<long> hist_after_scaled;

	bool show_hist_before;
	bool show_hist_after;
	bool show_hist_linear;
};

class GUI_Curve_Histogram : public GUI_Histogram {
	Q_OBJECT

public:
	GUI_Curve_Histogram(bool _lightness_only);
	void set_data_object(class GUI_Curve_Histogram_data *);
	void draw_histogram(QPainter *painter, int size_w, int size_h);
	void set_histograms(GUI_Curve_Histogram_data *_data, const QVector<long> &before, const QVector<long> &after);
	void set_settings(bool _show_hist_before, bool _show_hist_after, bool _show_hist_linear);

protected:
	QMutex data_lock;
	class GUI_Curve_Histogram_data *data;
	bool lightness_only;
};

//------------------------------------------------------------------------------
#endif //__H_GUI_CURVE_HISTOGRAM__
