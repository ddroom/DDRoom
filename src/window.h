#ifndef __H_WINDOW__
#define __H_WINDOW__
/*
 * window.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "browser.h"

//------------------------------------------------------------------------------
class Main_Splitter : public QSplitter {
	Q_OBJECT

public:
	Main_Splitter(Qt::Orientation orientation, QWidget *parent = 0);

protected:
	QSplitterHandle *createHandle(void);
	void paintEvent(QPaintEvent *event);
};

//------------------------------------------------------------------------------
class Main_SplitterHandle : public QSplitterHandle {
	Q_OBJECT

public:
	Main_SplitterHandle(Qt::Orientation orientation, QSplitter *parent = 0);

protected:
	void mousePressEvent(QMouseEvent * event);
	void mouseReleaseEvent(QMouseEvent * event);

};

//------------------------------------------------------------------------------
class Window : public QMainWindow {
	Q_OBJECT

public:
	Window(void);
	~Window();

protected:
	void keyPressEvent(QKeyEvent *);
	void keyReleaseEvent(QKeyEvent *);

	class Browser *browser;
	class Process *process;
	class Edit *edit;
	class EditHistory *edit_history;
	class Batch *batch;

	class Views_Layout *views_layout;
	void paintEvent(QPaintEvent *event);

protected:
	// Full Screen state
	bool is_full_screen;
	bool is_full_screen_from_maximized;
	void switch_full_screen(void);

	// left side tabs
	QWidget *widget_side;
	void create_side_tabs(void);

	// menu
protected slots:
	void menu_view_toggle_fullscreen(void);
	void menu_view_toggle_view(void);
	void menu_view_toggle_thumbs(void);
	void menu_view_toggle_thumbnails_position(void);
	void menu_view_toggle_tools(void);
	void menu_tools_lens_links_editor(void);
	void menu_tools_vignetting_profiler(void);
	void menu_help_about(void);
	void slot_edit_preferences(void);
	void slot_OOM_notification(void *);

protected:
	QAction *act_help_about;

	QSplitter *sp_thumbs_top;
	QSplitter *sp_thumbs_left;
	QSplitter *sp_thumbs_bottom;
	QSplitter *sp_thumbs_right;
	int thumbnails_position;
	QWidget *sp_thumbs_left_w;
	void change_thumbnails_position(int position);

	QMenu *menu_file;
	QMenu *menu_view;
	QMenu *menu_tools;
	QMenu *menu_help;

	void create_menu(void);
};

//------------------------------------------------------------------------------
#endif // __H_WINDOW__
