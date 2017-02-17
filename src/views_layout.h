#ifndef __H_VIEWS_LAYOUT__
#define __H_VIEWS_LAYOUT__
/*
 * views_layout.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

//------------------------------------------------------------------------------
// goal: double-click on splitter handle to set sizes equally to both child widgets
class View_SplitterHandle : public QSplitterHandle {
public:
	Q_OBJECT
public:
	View_SplitterHandle(Qt::Orientation orientation, class View_Splitter *parent = 0);
protected:
	void mouseDoubleClickEvent(QMouseEvent * event);
	QSplitter *splitter_parent;
};

class View_Splitter : public QSplitter {
	Q_OBJECT
public:
	View_Splitter(Qt::Orientation orientation, QWidget *parent = 0);
protected:
	QSplitterHandle *createHandle(void);
	void paintEvent(QPaintEvent *event);
};

//------------------------------------------------------------------------------
class Views_Layout : public QObject {
	Q_OBJECT

public:
	Views_Layout(class Edit *edit, QWidget *parent);
	~Views_Layout();

	void fill_toolbar(QToolBar *t);
	void fill_menu(QMenu *menu);

private slots:
	void menu_view_toggle_views_layout(void);
	void menu_view_toggle_views_orientation(void);
	void slot_views_layout_1(bool);
	void slot_views_layout_2(bool);
	void slot_views_layout_3(bool);
	void slot_views_layout_4(bool);
	void slot_views_layout_20(void);
	void slot_views_layout_21(void);
	void slot_views_layout_30(void);
	void slot_views_layout_31(void);
	void slot_views_layout_32(void);
	void slot_views_layout_33(void);
	void slot_views_layout_34(void);
	void slot_views_layout_35(void);
	void slot_views_layout_40(void);
	void slot_views_layout_41(void);

protected:
	Views_Layout(void);
	class Edit *edit;
	QWidget *parent;
	void tb_views_reconnect(bool to_connect);
	void f_views_layout(int layout);
	void f_views_layout(int layout, int orientation, bool checked);
	int views_layout_n_to_index(int n);
	QToolButton *tb_views_layout_1;
	QToolButton *tb_views_layout_2;
	QToolButton *tb_views_layout_3;
	QToolButton *tb_views_layout_4;
	QAction *action_views_layout[11];
	QIcon icon_views_layout[11];
};

//------------------------------------------------------------------------------
#endif // __H_VIEWS_LAYOUT__
