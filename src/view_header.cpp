/*
 * view_header.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "view_header.h"

using namespace std;

//------------------------------------------------------------------------------
ViewHeaderButton::ViewHeaderButton(int size, QWidget *parent) : QToolButton(parent) {
	setMaximumSize(size, size);
}

//------------------------------------------------------------------------------
list<ViewHeader *> ViewHeader::vh_list;
std::mutex ViewHeader::vh_list_lock;

ViewHeader::ViewHeader(QWidget *parent) : QWidget(parent) {
	active = false;

	// height = font_height * 3 / 2
	QStyleOption option;
	option.initFrom(this);
	text_height = option.fontMetrics.height();
	text_offset = text_height / 6 + 1;
	setMinimumSize(0, text_height + text_offset * 2);

	vh_list_lock.lock();
	// change that if 'active' will be saved/restored between program runs
	if(vh_list.size() == 0)
		this->active = true;
	vh_list.push_back(this);
	vh_list_lock.unlock();

	QHBoxLayout *h = new QHBoxLayout(this);
	h->setAlignment(Qt::AlignRight);
	h->setSpacing(4);
	h->setContentsMargins(0, text_offset, text_offset, text_offset);

//	QToolButton *button_close = new QToolButton(this);
//	QToolButton *button_close = new QToolButton(text_height, this);
	button_close = new ViewHeaderButton(text_height, this);
//	button_close->setSize(QSize(text_height, text_height));
#ifdef Q_OS_MAC
//	button_close->setIconSize(QSize(button_close->iconSize().width() / 2, button_close->iconSize().height() / 2));
#endif
	h->addWidget(button_close);

	connect(button_close, SIGNAL(pressed()), this, SLOT(slot_button_close(void)));

	create_icons(this, button_close->iconSize().width());
	button_close->setIcon(icon_close);
	set_enabled(false);
}

ViewHeader::~ViewHeader() {
	vh_list_lock.lock();
	vh_list.remove(this);
	vh_list_lock.unlock();
}

void ViewHeader::set_enabled(bool enable) {
//cerr << "____set_enabled == " << enable << endl;
	if(enable) {
		button_close->setEnabled(true);
	} else {
		button_close->setEnabled(false);
	}
}

void ViewHeader::slot_button_close(void) {
	emit signal_button_close();
	button_close->setEnabled(false);
}

void ViewHeader::create_icons(QWidget *qwidget, int size_l) {
//	QColor color = qwidget->style()->standardPalette().color(QPalette::ButtonText);
	size_l -= 2;
	QSize size = QSize(size_l, size_l);
	int _w = size_l - 1;
	int _h = size_l - 1;
	QImage paper = QImage(size, QImage::Format_ARGB32);
	paper.fill(QColor(255, 255, 255, 0).rgba());

	// 'close'
	QPainter *painter = new QPainter(&paper);
	painter->drawLine(1, 0, _w - 0, _h - 1);
	painter->drawLine(1, 1, _w - 1, _h - 1);
	painter->drawLine(0, 1, _w - 1, _h - 0);

	painter->drawLine(1, _h - 0, _w - 0, 1);
	painter->drawLine(1, _h - 1, _w - 1, 1);
	painter->drawLine(0, _h - 1, _w - 1, 0);
	delete painter;
	icon_close = QPixmap::fromImage(paper);
}

void ViewHeader::set_text(QString text) {
	this->text = text;
	emit update();
}

void ViewHeader::mousePressEvent(QMouseEvent *event) {
//cerr << "mouse press event" << endl;
	if(event->button() == Qt::LeftButton && !active) {
		vh_set_active(this);
//		active = !active;
//		emit update();
	}
}

void ViewHeader::mouseDoubleClickEvent(QMouseEvent *event) {
//cerr << "dbl_clk" << endl;
	emit signal_double_click();
}

bool ViewHeader::is_active(void) {
	return active;
}

void ViewHeader::vh_set_active(ViewHeader *_this) {
	for(list<ViewHeader *>::iterator it = vh_list.begin(); it != vh_list.end(); ++it)
		if((*it)->active == true && _this == *it)
			return;
	// deactivate all, then - set up active to improve visual impression
	for(list<ViewHeader *>::iterator it = vh_list.begin(); it != vh_list.end(); ++it)
		if(_this != *it)
			(*it)->set_active(false);
	_this->set_active(true);
}

void ViewHeader::set_active(bool active) {
	if(this->active != active) {
		this->active = active;
		emit update();
		emit signal_active(active);
//		if(active)
//			emit signal_switch_active();	// - send signal to the relaited View object
	}
}

void ViewHeader::paintEvent(QPaintEvent *event) {
	QStylePainter painter(this);
	
//cerr << "ViewHeader::paintEvent()" << endl;
	// fill background
	if(active) {
		QColor c_bg = palette().color(QPalette::Active, QPalette::Highlight);
		painter.fillRect(QRect(0, 0, width(), height()), c_bg);
	}

	// draw frame
	QStyleOptionFrame option;
	option.initFrom(this);
	painter.drawPrimitive(QStyle::PE_Frame, option);

	// draw text
	if(active) {
		painter.setPen(palette().color(QPalette::Normal, QPalette::HighlightedText));
		QFont bold = painter.font();
		bold.setBold(true);
		painter.setFont(bold);
	} else
		painter.setPen(palette().color(QPalette::Normal, QPalette::Text));
	QRect text_rect(text_offset * 2, text_offset, width(), text_height);
	painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, text);
}

//------------------------------------------------------------------------------
