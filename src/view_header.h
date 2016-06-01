#ifndef __H_VIEW_HEADER__
#define __H_VIEW_HEADER__
/*
 * view_header.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>
#include <mutex>

#include <QtWidgets>

//------------------------------------------------------------------------------
class ViewHeaderButton : public QToolButton {
	Q_OBJECT

public:
	ViewHeaderButton(int size, QWidget *parent = nullptr);
};

//------------------------------------------------------------------------------
class ViewHeader : public QWidget {
	Q_OBJECT

public:
	ViewHeader(QWidget *parent = nullptr);
	~ViewHeader();
	void set_text(QString text);
	void set_enabled(bool enable);
	static void vh_set_active(ViewHeader *_this);
	bool is_active(void);

signals:
	void signal_button_close(void);
	void signal_active(bool);
	void signal_double_click(void);

protected slots:
	void slot_button_close(void);

protected:
	int text_height;
	int text_offset;
	QString text;

	bool active;

	void mousePressEvent(QMouseEvent *event);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void paintEvent(QPaintEvent *event);

	static std::list<ViewHeader *> vh_list;
	static std::mutex vh_list_lock;

	void set_active(bool _active);

	void create_icons(QWidget *qwidget, int size_l);
	QIcon icon_close;

	class ViewHeaderButton *button_close;
};

//------------------------------------------------------------------------------

#endif // __H_VIEW_HEADER__
