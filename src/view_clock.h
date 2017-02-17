#ifndef __H_VIEW_CLOCK__
#define __H_VIEW_CLOCK__
/*
 * view_clock.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

//------------------------------------------------------------------------------
class ViewClock : public QObject {
	Q_OBJECT

public:
	ViewClock(void);
	~ViewClock();
	bool is_active(void);
	void draw(QPainter *painter, float center_x, float center_y);
	void start(QImage photo_image, QColor bg_color);
	void stop(void);
	void start(void);

signals:
	void signal_update(void);

protected:
	void draw_wait(QPainter *painter, float center_x, float center_y);
	bool clock_time_start;
	int clock_angle;
	int clock_size;
	QImage clock_icon_image;
	QPixmap clock_icon_pixmap;
	QTimer *clock_timer;
	QColor clock_bg_color;
	bool clock_wait;

protected slots:
	void slot_timer(void);
};

//------------------------------------------------------------------------------
#endif // __H_VIEW_CLOCK__
