/*
 * views_layout.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "edit.h"
#include "views_layout.h"

using namespace std;


//------------------------------------------------------------------------------
View_Splitter::View_Splitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent) {
}

QSplitterHandle *View_Splitter::createHandle(void) {
	return new View_SplitterHandle(orientation(), this);
}

void View_Splitter::paintEvent(QPaintEvent *event) {
//cerr << "paint event for splitter: " << long(this) << endl;
	QSplitter::paintEvent(event);
}

//------------------------------------------------------------------------------
View_SplitterHandle::View_SplitterHandle(Qt::Orientation orientation, View_Splitter *parent) : QSplitterHandle(orientation, parent) {
	splitter_parent = parent;
}

void View_SplitterHandle::mouseDoubleClickEvent(QMouseEvent *event) {
//	QSplitter *splitter = (QSplitter *)parent;
	QList<int> sizes;
	sizes.push_back(1);
	sizes.push_back(1);
	splitter_parent->setSizes(sizes);
//	QSplitterHandle::mouseDoubleClickEvent(event);
//	emit signal_mouse_double_click();
}

//------------------------------------------------------------------------------
Views_Layout::Views_Layout(Edit *_edit, QWidget *_parent) {
	edit = _edit;
	parent = _parent;
}

Views_Layout::Views_Layout(void) {
}

Views_Layout::~Views_Layout() {
}

//------------------------------------------------------------------------------
QToolButton *_views_layout_tool_button(QIcon icon, QString text, QWidget *parent) {
	QToolButton *tb = new QToolButton(parent);
	tb->setIcon(icon);
	tb->setText(text);
	tb->setToolTip(text);
	tb->setCheckable(true);
	return tb;
}

void Views_Layout::slot_views_layout_20(void) {	f_views_layout(20);}
void Views_Layout::slot_views_layout_21(void) {	f_views_layout(21);}
void Views_Layout::slot_views_layout_30(void) {	f_views_layout(30);}
void Views_Layout::slot_views_layout_31(void) {	f_views_layout(31);}
void Views_Layout::slot_views_layout_32(void) {	f_views_layout(32);}
void Views_Layout::slot_views_layout_33(void) {	f_views_layout(33);}
void Views_Layout::slot_views_layout_34(void) {	f_views_layout(34);}
void Views_Layout::slot_views_layout_35(void) {	f_views_layout(35);}
void Views_Layout::slot_views_layout_40(void) {	f_views_layout(40);}
void Views_Layout::slot_views_layout_41(void) {	f_views_layout(41);}

void Views_Layout::slot_views_layout_1(bool checked) {f_views_layout(1, -1, checked);}
void Views_Layout::slot_views_layout_2(bool checked) {f_views_layout(2, -1, checked);}
void Views_Layout::slot_views_layout_3(bool checked) {f_views_layout(3, -1, checked);}
void Views_Layout::slot_views_layout_4(bool checked) {f_views_layout(4, -1, checked);}

void Views_Layout::f_views_layout(int n) {
	f_views_layout(n / 10, n % 10, true);
}

void Views_Layout::f_views_layout(int layout, int orientation, bool checked) {
	tb_views_reconnect(false);
	if(checked) {
		if(layout != 1)	tb_views_layout_1->setChecked(false);
		if(layout != 2)	tb_views_layout_2->setChecked(false);
		if(layout != 3)	tb_views_layout_3->setChecked(false);
		if(layout != 4)	tb_views_layout_4->setChecked(false);
	} else {
		if(layout == 1) {
			tb_views_layout_2->setChecked(true);
			layout = 2;
		} else {
			tb_views_layout_1->setChecked(true);
			layout = 1;
		}
		checked = true;
	}
	if(layout == 1)	tb_views_layout_1->setChecked(true);
	if(layout == 2)	tb_views_layout_2->setChecked(true);
	if(layout == 3)	tb_views_layout_3->setChecked(true);
	if(layout == 4)	tb_views_layout_4->setChecked(true);
	tb_views_reconnect(true);
	if(checked) {
		if(orientation < 0)
			orientation = edit->get_views_orientation(layout);
		edit->set_views_layout(layout, orientation);
		orientation = edit->get_views_orientation(layout);
		// update toolbar
		if(layout == 2)	tb_views_layout_2->setIcon(icon_views_layout[views_layout_n_to_index(20 + orientation)]);
		if(layout == 3)	tb_views_layout_3->setIcon(icon_views_layout[views_layout_n_to_index(30 + orientation)]);
		if(layout == 4)	tb_views_layout_4->setIcon(icon_views_layout[views_layout_n_to_index(40 + orientation)]);
	}
}

int Views_Layout::views_layout_n_to_index(int n) {
	int index = 0;
	int i = n / 10;
	int o = n % 10;
	if(i == 2)	index = 1 + o;	// 1, 2
	if(i == 3)	index = 3 + o;	// 3, 8
	if(i == 4)	index = 9 + o;	// 9, 10
	return index;
}

void Views_Layout::fill_toolbar(QToolBar *t) {
	QString tn_text[11] = {
		tr(""),
		tr("vertical"), tr("horizontal"),
		tr("vertical"), tr("horizontal"), tr("first left"), tr("first top"), tr("first right"), tr("first bottom"),
		tr("vertical"), tr("horizontal")
	};
	int tn[11] = {10, 20, 21, 30, 31, 32, 33, 34, 35, 40, 41};
	for(int i = 0; i < 11; ++i) {
		QString icon_str = ":/resources/view_layout_";//".svg";
		icon_str += QString::number(tn[i]);
		icon_str += ".svg";
		icon_views_layout[i] = QIcon(icon_str);
		action_views_layout[i] = new QAction(icon_views_layout[i], tn_text[i], parent);
	}
	connect(action_views_layout[views_layout_n_to_index(20)], SIGNAL(triggered()), this, SLOT(slot_views_layout_20()));
	connect(action_views_layout[views_layout_n_to_index(21)], SIGNAL(triggered()), this, SLOT(slot_views_layout_21()));
	connect(action_views_layout[views_layout_n_to_index(30)], SIGNAL(triggered()), this, SLOT(slot_views_layout_30()));
	connect(action_views_layout[views_layout_n_to_index(31)], SIGNAL(triggered()), this, SLOT(slot_views_layout_31()));
	connect(action_views_layout[views_layout_n_to_index(32)], SIGNAL(triggered()), this, SLOT(slot_views_layout_32()));
	connect(action_views_layout[views_layout_n_to_index(33)], SIGNAL(triggered()), this, SLOT(slot_views_layout_33()));
	connect(action_views_layout[views_layout_n_to_index(34)], SIGNAL(triggered()), this, SLOT(slot_views_layout_34()));
	connect(action_views_layout[views_layout_n_to_index(35)], SIGNAL(triggered()), this, SLOT(slot_views_layout_35()));
	connect(action_views_layout[views_layout_n_to_index(40)], SIGNAL(triggered()), this, SLOT(slot_views_layout_40()));
	connect(action_views_layout[views_layout_n_to_index(41)], SIGNAL(triggered()), this, SLOT(slot_views_layout_41()));

	// toolbar
	tb_views_layout_1 = _views_layout_tool_button(icon_views_layout[views_layout_n_to_index(10)], tr("1 view"), parent);
	t->addWidget(tb_views_layout_1);

	QMenu *v2m = new QMenu();
	v2m->addAction(action_views_layout[views_layout_n_to_index(20)]);
	v2m->addAction(action_views_layout[views_layout_n_to_index(21)]);
	tb_views_layout_2 = _views_layout_tool_button(icon_views_layout[views_layout_n_to_index(20)], tr("2 views"), parent);
	tb_views_layout_2->setPopupMode(QToolButton::DelayedPopup);
#ifdef Q_OS_MAC
	tb_views_layout_2->setPopupMode(QToolButton::MenuButtonPopup);
#endif
	tb_views_layout_2->setMenu(v2m);
	t->addWidget(tb_views_layout_2);

	QMenu *v3m = new QMenu();
	for(int i = 0; i < 6; ++i)
		v3m->addAction(action_views_layout[views_layout_n_to_index(30 + i)]);
	tb_views_layout_3 = _views_layout_tool_button(icon_views_layout[views_layout_n_to_index(30)], tr("3 views"), parent);
	tb_views_layout_3->setPopupMode(QToolButton::DelayedPopup);
#ifdef Q_OS_MAC
	tb_views_layout_3->setPopupMode(QToolButton::MenuButtonPopup);
#endif
	tb_views_layout_3->setMenu(v3m);
	t->addWidget(tb_views_layout_3);

	QMenu *v4m = new QMenu();
	v4m->addAction(action_views_layout[views_layout_n_to_index(40)]);
	v4m->addAction(action_views_layout[views_layout_n_to_index(41)]);
	tb_views_layout_4 = _views_layout_tool_button(icon_views_layout[views_layout_n_to_index(40)], tr("4 views"), parent);
	tb_views_layout_4->setPopupMode(QToolButton::DelayedPopup);
#ifdef Q_OS_MAC
	tb_views_layout_4->setPopupMode(QToolButton::MenuButtonPopup);
#endif
	tb_views_layout_4->setMenu(v4m);
	t->addWidget(tb_views_layout_4);

	// current settings - edit actually did settings load
	f_views_layout(edit->get_views_layout(), edit->get_views_orientation(edit->get_views_layout()), true);
	tb_views_layout_2->setIcon(icon_views_layout[views_layout_n_to_index(20 + edit->get_views_orientation(2))]);
	tb_views_layout_3->setIcon(icon_views_layout[views_layout_n_to_index(30 + edit->get_views_orientation(3))]);
	tb_views_layout_4->setIcon(icon_views_layout[views_layout_n_to_index(40 + edit->get_views_orientation(4))]);

	tb_views_reconnect(true);
}

void Views_Layout::tb_views_reconnect(bool to_connect) {
	if(to_connect) {
		connect(tb_views_layout_1, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_1(bool)));
		connect(tb_views_layout_2, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_2(bool)));
		connect(tb_views_layout_3, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_3(bool)));
		connect(tb_views_layout_4, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_4(bool)));
	} else {
		disconnect(tb_views_layout_1, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_1(bool)));
		disconnect(tb_views_layout_2, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_2(bool)));
		disconnect(tb_views_layout_3, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_3(bool)));
		disconnect(tb_views_layout_4, SIGNAL(toggled(bool)), this, SLOT(slot_views_layout_4(bool)));
	}
}

void Views_Layout::fill_menu(QMenu *menu) {
	QAction *act_view_toggle_views_layout = new QAction(tr("Toggle views layout"), parent);
	act_view_toggle_views_layout->setShortcut(tr("F6"));
	connect(act_view_toggle_views_layout , SIGNAL(triggered()), this, SLOT(menu_view_toggle_views_layout()));

	QAction *act_view_toggle_views_orientation = new QAction(tr("Toggle views orientation"), parent);
	act_view_toggle_views_orientation->setShortcut(tr("F7"));
	connect(act_view_toggle_views_orientation , SIGNAL(triggered()), this, SLOT(menu_view_toggle_views_orientation()));
	menu->addAction(act_view_toggle_views_layout);
	menu->addAction(act_view_toggle_views_orientation);
}

void Views_Layout::menu_view_toggle_views_layout(void) {
	int layout = edit->get_views_layout() + 1;
	if(layout > 4)	layout = 1;
	f_views_layout(layout, edit->get_views_orientation(layout), true);

}

void Views_Layout::menu_view_toggle_views_orientation(void) {
	int layout = edit->get_views_layout();
	f_views_layout(layout, edit->get_views_orientation(layout) + 1, true);
}

//------------------------------------------------------------------------------
