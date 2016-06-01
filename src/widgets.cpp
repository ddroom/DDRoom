/*
 * widgets.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <math.h>

#include "widgets.h"

#include <iostream>
using namespace std;

//------------------------------------------------------------------------------
QCursor Cursor::to_qt(enum cursor &c) {
	switch(c) {
	case size_ver:
		return Qt::SizeVerCursor;
	case size_hor:
		return Qt::SizeHorCursor;
	case size_bdiag:
		return Qt::SizeBDiagCursor;
	case size_fdiag:
		return Qt::SizeFDiagCursor;
	case hand_open:
		return Qt::OpenHandCursor;
	case hand_closed:
		return Qt::ClosedHandCursor;
	case cross:
		return Qt::CrossCursor;
	default:
		return Qt::ArrowCursor;
	}
	return Qt::ArrowCursor;
}

//------------------------------------------------------------------------------
ControlsArea::ControlsArea(QWidget *parent) : QScrollArea(parent) {
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
//	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
//	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setContentsMargins(0, 0, 0, 0);
	setViewportMargins(0, 0, 0, 0);
}

void ControlsArea::resizeEvent(QResizeEvent *event) {
	QScrollArea::resizeEvent(event);
/*
//		updateGeometry();
cerr << "viewport - width == " << viewport()->width() << endl;
//	updateWidgetPosition();
//
//	int w = 0;
	int w = widget()->width();
//	int w = viewport()->width();
cerr << "parent width == " << parentWidget()->width() << "; width == " << width() << "; w == " << w << endl;
cerr << "vertical_scroll_bar visible: " << verticalScrollBar()->isVisible() << endl;
	if(verticalScrollBar()->isVisible())
		w += verticalScrollBar()->width();
//	if(viewport()->width() < widget()->width())
//		w += widget()->width() - viewport()->width();
//	if(verticalScrollBar()->width() != 0)
//		w += verticalScrollBar()->width();
//	w += widget()->width();
	if(width() != w) {
cerr << "parent width == " << parentWidget()->width() << "; width == " << width() << "; w == " << w << endl;
//	if(parentWidget()->width() != w) {
//		parentWidget()->setFixedWidth(w);
//		parentWidget()->setMinimumWidth(w);
		setMinimumWidth(w);
		updateGeometry();
	}
*/
}

//------------------------------------------------------------------------------
