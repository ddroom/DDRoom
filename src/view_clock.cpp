/*
 * view_clock.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "view_clock.h"

#include <iostream>
using namespace std;
//------------------------------------------------------------------------------
ViewClock::ViewClock(void) {
	clock_wait = false;
	clock_size = 128;
	clock_timer = new QTimer();
	clock_timer->setInterval(75);
	clock_timer->setSingleShot(false);
	connect(clock_timer, SIGNAL(timeout()), this, SLOT(slot_timer(void)));
}

ViewClock::~ViewClock(void) {
	delete clock_timer;
}

bool ViewClock::is_active(void) {
	return clock_timer->isActive();
}

void ViewClock::slot_timer(void) {
	clock_angle += 30;
	if(clock_angle >= 360)
		clock_angle = 0;
	emit signal_update();
}

void ViewClock::start(void) {
	clock_wait = true;
	// ignore image related issues...
	clock_angle = 0;
	clock_timer->start();
	clock_time_start = true;
}

void ViewClock::start(QImage photo_image, QColor bg_color) {
	if(clock_timer->isActive())
		return; // should never happen
	clock_wait = false;
	clock_bg_color = bg_color;
	if(!photo_image.isNull()) {
		clock_icon_image = photo_image;
		clock_icon_pixmap = QPixmap();
	}
	clock_angle = 0;
	clock_timer->start();
	clock_time_start = true;
}

void ViewClock::stop(void) {
	if(clock_timer->isActive())
		clock_timer->stop();
}

void ViewClock::draw(QPainter *painter, float center_x, float center_y) {
	if(clock_wait) {
		draw_wait(painter, center_x, center_y);
		return;
	}
	if(clock_icon_pixmap.isNull() && !clock_icon_image.isNull()) {
		clock_icon_pixmap = QPixmap::fromImage(clock_icon_image).scaled(clock_size, clock_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		clock_icon_image = QImage();
	}
	// draw timer
	painter->save();
	int len = clock_size;
	int l1 = len / 3 + 2;
	int ld = len / 2;
	float x = center_x;
	float y = center_y;
	int r = clock_size / 8;
	r -= 1;
	float r2 = r / 2;
	// draw cache pixmap
	int c_r = 255;
	int c_g = 255;
	int c_b = 255;
	if(clock_time_start) {
		clock_time_start = false;
		QImage _image(QSize(len + 1, len + 1), QImage::Format_ARGB32_Premultiplied);
		QPainter imagePainter(&_image);
		QPainter *p = &imagePainter;
		p->setBackground(QBrush(clock_bg_color));
		p->eraseRect(QRect(0, 0, len + 1, len + 1));
		p->drawPixmap(QPoint(ld - clock_icon_pixmap.width() / 2, ld - clock_icon_pixmap.height() / 2), clock_icon_pixmap);
		p->setRenderHint(QPainter::Antialiasing, true);
		p->setBrush(QBrush(QColor(c_r, c_g, c_b, 32)));
		p->setPen(QPen(QColor(c_r, c_g, c_b, 128), 1.0));
		p->drawRoundRect(QRectF(0.5, 0.5, len, len), 20, 20);
		p->setBrush(QBrush(QColor(c_r, c_g, c_b, 31)));
		p->setPen(QPen(QColor(c_r, c_g, c_b, 63), 1.0));
		for(int i = 0; i < 360; i += 30) {
			p->save();
			QTransform tr;
			tr.translate(ld, ld);
			tr.rotate(i);
			p->setWorldTransform(tr);
			p->drawEllipse(0 - r2, - l1 - r2, r, r);
			p->restore();
		}
		clock_icon_pixmap = QPixmap::fromImage(_image);
	}
	painter->drawPixmap(QPoint(x - (clock_icon_pixmap.width()) / 2, y - (clock_icon_pixmap.height()) / 2), clock_icon_pixmap);
	painter->setRenderHint(QPainter::Antialiasing, true);
	int p = clock_angle;
	int c1 = 127;
	int c2 = 255;
	for(int i = 0; i <= 3; ++i) {
		painter->save();
		painter->setBrush(QBrush(QColor(c_r, c_g, c_b, c1)));
		painter->setPen(QPen(QColor(c_r, c_g, c_b, c2), 1.0));
		QTransform tr;
		tr.translate(x, y);
		tr.rotate(p);
		painter->setWorldTransform(tr);
		painter->drawEllipse(0 - r2, -l1 - r2, r, r);
		painter->restore();
		p -= 30;
		c1 -= 32;
		c2 -= 64;
	}
	painter->restore();
}

void ViewClock::draw_wait(QPainter *painter, float center_x, float center_y) {
	painter->save();
	int len = clock_size;
	int l1 = len / 3 + 2;
	float x = center_x;
	float y = center_y;
	int r = clock_size / 8;
	r -= 1;
	float r2 = r / 2;
	int c_r = 255;
	int c_g = 255;
	int c_b = 255;
	painter->setRenderHint(QPainter::Antialiasing, true);
	int p = clock_angle;
	int c1 = 127;
	int c2 = 255;
	for(int i = 0; i <= 3; ++i) {
		painter->save();
		painter->setBrush(QBrush(QColor(c_r, c_g, c_b, c1)));
		painter->setPen(QPen(QColor(c_r, c_g, c_b, c2), 1.0));
		QTransform tr;
		tr.translate(x, y);
		tr.rotate(p);
		painter->setWorldTransform(tr);
		painter->drawEllipse(0 - r2, -l1 - r2, r, r);
		painter->restore();
		p -= 30;
		c1 -= 32;
		c2 -= 64;
	}
	painter->restore();
}

//------------------------------------------------------------------------------
