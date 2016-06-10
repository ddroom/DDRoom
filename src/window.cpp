/*
 * window.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <lensfun/lensfun.h>
#include <exiv2/exiv2.hpp>
#include <exiv2/version.hpp>

#include "batch.h"
#include "browser.h"
#include "config.h"
#include "dcraw.h"
#include "db.h"
#include "edit.h"
#include "edit_history.h"
#include "process_h.h"
#include "version.h"
#include "views_layout.h"
#include "view_zoom.h"
#include "window.h"
#include "profiler_vignetting.h"

using namespace std;

//------------------------------------------------------------------------------
Main_Splitter::Main_Splitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent) {
}

QSplitterHandle *Main_Splitter::createHandle(void) {
	return new Main_SplitterHandle(orientation(), this);
}

void Main_Splitter::paintEvent(QPaintEvent *event) {
//cerr << "paint event for splitter: " << long(this) << endl;
	QSplitter::paintEvent(event);
}

//------------------------------------------------------------------------------
Main_SplitterHandle::Main_SplitterHandle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent) {
}

void Main_SplitterHandle::mousePressEvent(QMouseEvent *event) {
	if(event->button() == Qt::LeftButton)
		splitter()->setOpaqueResize(true);
}

void Main_SplitterHandle::mouseReleaseEvent(QMouseEvent *event) {
	if(event->button() == Qt::LeftButton)
		splitter()->setOpaqueResize(false);
}

//------------------------------------------------------------------------------
void Window::paintEvent(QPaintEvent *event) {
//	QRect r = event->region().boundingRect();
//	cerr << endl << "Window::paintEvent(); region == " << r.left() << "-" << r.right() << " - " << r.top() << "-" << r.bottom() << endl;
	QMainWindow::paintEvent(event);
}

Window::Window(void) {
	setWindowTitle(APP_NAME);
	is_full_screen = false;

//	setWindowOpacity(0.9);
//	setAttribute(Qt::WA_MacSmallSize, true);
//	setAttribute(Qt::WA_MacMiniSize, true);
//	setAttribute(Qt::WA_MacBrushedMetal, true);
	setUnifiedTitleAndToolBarOnMac(true);

	browser = new Browser();
	process = new Process();
	connect(process, SIGNAL(signal_OOM_notification(void *)), this, SLOT(slot_OOM_notification(void *)));

	edit = new Edit(process, browser);
	QWidget *view_container = edit->get_views_widget(this);
	edit_history = new EditHistory(edit, this);
	edit->set_edit_history(edit_history);

	batch = new Batch(this, process, edit, browser);

	// load or reset photo - from browser to edit
	connect(browser, SIGNAL(signal_load_photo(Photo_ID, QString, QImage)), edit, SLOT(slot_load_photo(Photo_ID, QString, QImage)));

	// update processed thumbnail in browser
	connect(edit, SIGNAL(signal_update_thumbnail(Photo_ID, QImage)), browser, SLOT(slot_update_thumbnail(Photo_ID, QImage)));

	// 'center' browser on already open photo
	connect(edit, SIGNAL(signal_browse_to_photo(Photo_ID)), browser, SLOT(slot_browse_to_photo(Photo_ID)));

	// reset photos selection
	connect(batch, SIGNAL(signal_batch_accepted(void)), browser, SLOT(slot_selection_clear(void)));
	// context menu 'save as'
	connect(browser, SIGNAL(signal_export(void)), batch, SLOT(slot_export(void)));

	create_side_tabs();

	sp_thumbs_left = new Main_Splitter(Qt::Horizontal, this);
	sp_thumbs_left_w = new QWidget();
	sp_thumbs_left->addWidget(sp_thumbs_left_w);
	sp_thumbs_left->addWidget(view_container);
	sp_thumbs_left_w->setVisible(false);
//	sp_thumbs_left->insertWidget(1, view_container);
	sp_thumbs_left->setStretchFactor(0, 0);
	sp_thumbs_left->setStretchFactor(1, 1);
	sp_thumbs_left->setCollapsible(0, false);
	sp_thumbs_left->setCollapsible(1, false);
	sp_thumbs_left->setOpaqueResize(false);

	sp_thumbs_right = new Main_Splitter(Qt::Horizontal, this);
	sp_thumbs_right->addWidget(sp_thumbs_left);
	sp_thumbs_right->setStretchFactor(0, 1);
	sp_thumbs_right->setStretchFactor(1, 0);
	sp_thumbs_right->setCollapsible(0, false);
//	sp_thumbs_right->setCollapsible(1, false);
	sp_thumbs_right->setOpaqueResize(false);

	QSplitter *sp_left = new Main_Splitter(Qt::Horizontal, this);
	sp_left->addWidget(widget_side);
	sp_left->addWidget(sp_thumbs_right);
//	sp_left->addWidget(view_container);
	sp_left->setStretchFactor(0, 0);
	sp_left->setStretchFactor(1, 1);
	sp_left->setCollapsible(0, false);
	sp_left->setCollapsible(1, false);
	sp_left->setOpaqueResize(false);

	sp_thumbs_top = new Main_Splitter(Qt::Vertical, this);
	sp_thumbs_top->addWidget(browser->get_list());	// 0
	sp_thumbs_top->addWidget(sp_left);				// 1
	sp_thumbs_top->setStretchFactor(0, 0);
	sp_thumbs_top->setStretchFactor(1, 1);
	sp_thumbs_top->setCollapsible(0, false);
	sp_thumbs_top->setCollapsible(1, false);
	sp_thumbs_top->setOpaqueResize(false);

	sp_thumbs_bottom = new Main_Splitter(Qt::Vertical, this);
	sp_thumbs_bottom->addWidget(sp_thumbs_top);	// 0
	sp_thumbs_bottom->setStretchFactor(0, 1);
	sp_thumbs_bottom->setStretchFactor(1, 0);
	sp_thumbs_bottom->setCollapsible(0, false);
	sp_thumbs_bottom->setOpaqueResize(false);

	QVBoxLayout *controls_layout = new QVBoxLayout();
	QWidget *controls = edit->get_controls_widget();
	controls_layout->setContentsMargins(0, 0, 0, 0);
	controls_layout->addWidget(controls, 1);
	controls_layout->addWidget(batch->controls());

	QHBoxLayout *main_layout = new QHBoxLayout;
	main_layout->setSpacing(0);
	main_layout->setContentsMargins(0, 0, 0, 0);
	main_layout->addWidget(sp_thumbs_bottom, 1);
	main_layout->addLayout(controls_layout, 0);

	QWidget *main_widget = new QWidget(this);
	main_widget->setContentsMargins(0, 0, 0, 0);
	main_widget->setLayout(main_layout);
	setCentralWidget(main_widget);

	create_menu();
	// place window at the desktop
	// TODO: save and restore geometry after first run
	QRect dr = qApp->desktop()->screenGeometry(this);
	int w = 1000;
	int h = 560;
	dr.setX((dr.width() - w) / 2);
	dr.setY((dr.height() - h) / 3);
	dr.setWidth(w);
	dr.setHeight(h);
	this->setGeometry(dr);

	// reconfigure if necessary
	thumbnails_position = Browser::thumbnails_top;
	string str;
	if(Config::instance()->get(CONFIG_SECTION_LAYOUT, "thumbnails_position", str))
		thumbnails_position = Browser::thumbnails_position_from_string(str);
	change_thumbnails_position(thumbnails_position);

/*
	QSet<QString> styles_blacklist;
	styles_blacklist.insert("motif");
	styles_blacklist.insert("windows");
//	styles_blacklist.insert("cde");
	QStringList l = QStyleFactory::keys();
	for(int i = 0; i < l.size(); ++i) {
		QString entry = l[i].toLower();
		if(!styles_blacklist.contains(entry))
			cerr << "Style: \"" << l[i].toLocal8Bit().constData() << "\"" << endl;
	}
*/
//	System::instance()->qt_style_default = new QStyle();
//	QApplication::setStyle(QStyleFactory::create("plastique"));
//	QApplication::setPalette(QApplication::style()->standardPalette());
}

Window::~Window() {
	process->quit();

	string str = Browser::thumbnails_position_to_string(Browser::thumbnails_position(thumbnails_position));
	Config::instance()->set(CONFIG_SECTION_LAYOUT, "thumbnails_position", str);

//	delete sp_thumbs_left_w;
	// don't change order !
	delete edit;
	delete edit_history;
	delete browser;
	delete process;
	delete batch;
}

void Window::create_side_tabs(void) {
	widget_side = new QWidget(this);
	QVBoxLayout *vb = new QVBoxLayout(widget_side);

	vb->setSpacing(0);
	vb->setContentsMargins(0, 0, 0, 0);
	QTabWidget *tab_widget = new QTabWidget();
	tab_widget->setTabPosition(QTabWidget::West);
	vb->addWidget(tab_widget);

	tab_widget->addTab(browser->get_tree(), "File System");

	QWidget *edit_history_w = edit_history->get_widget();
	tab_widget->addTab(edit_history_w, "Edit history");
#if 0
	QWidget *view_metadata = new QWidget(this);
	tab_widget->addTab(view_metadata, "Metadata");

	QWidget *batch = new QWidget(this);
	tab_widget->addTab(batch, "Batch");
#endif
}

void Window::create_menu(void) {
	views_layout = new Views_Layout(edit, this);

	// File
	// should be inactive when there is no open photos at Edit
	QAction *act_file_quit = new QAction(tr("&Quit"), this);
	act_file_quit->setShortcut(tr("Ctrl+Q"));
	connect(act_file_quit, SIGNAL(triggered()), this, SLOT(close()));
	//--
	menu_file = menuBar()->addMenu(tr("&File"));
	batch->fill_menu(menu_file);
	menu_file->addSeparator();
	menu_file->addAction(act_file_quit);

	// Edit
	QAction *act_edit_preferences = new QAction(tr("Preferences"), this);
	//--
	QMenu *menu_edit = menuBar()->addMenu(tr("&Edit"));
	edit_history->fill_menu(menu_edit);
	menu_edit->addSeparator();
	// copy/paste settings
	edit->menu_copy_paste(menu_edit);
	menu_edit->addSeparator();
	menu_edit->addAction(act_edit_preferences);
	connect(act_edit_preferences, SIGNAL(triggered()), this, SLOT(slot_edit_preferences()));

	// View
	QAction *act_view_toggle_fullscreen = new QAction(tr("Full Screen"), this);
	act_view_toggle_fullscreen->setShortcut(QKeySequence(tr("F10")));
/*
	QList<QKeySequence> act_view_toggle_fullscreen_k;
	act_view_toggle_fullscreen_k.push_back(QKeySequence(tr("Ctrl+F")));
	act_view_toggle_fullscreen_k.push_back(QKeySequence(tr("F10")));
	act_view_toggle_fullscreen->setShortcuts(act_view_toggle_fullscreen_k);
*/
	connect(act_view_toggle_fullscreen, SIGNAL(triggered()), this, SLOT(menu_view_toggle_fullscreen()));

	QAction *act_view_toggle_thumbs = new QAction(tr("Thumbnails show/hide"), this);
	act_view_toggle_thumbs->setShortcut(tr("F3"));
	connect(act_view_toggle_thumbs, SIGNAL(triggered()), this, SLOT(menu_view_toggle_thumbs()));

	QAction *act_view_toggle_thumbnails_position = new QAction(tr("Thumbnails position"), this);
	act_view_toggle_thumbnails_position->setShortcut(tr("F5"));
	connect(act_view_toggle_thumbnails_position, SIGNAL(triggered()), this, SLOT(menu_view_toggle_thumbnails_position()));

	QAction *act_view_toggle_tools = new QAction(tr("Tools show/hide"), this);
	act_view_toggle_tools->setShortcut(tr("F4"));
	connect(act_view_toggle_tools, SIGNAL(triggered()), this, SLOT(menu_view_toggle_tools()));

	QAction *act_view_toggle_view = new QAction(tr("Toggle view"), this);
//	act_view_toggle_view->setShortcut(tr("Ctrl+F"));
	act_view_toggle_view->setShortcut(tr("F2"));
	connect(act_view_toggle_view, SIGNAL(triggered()), this, SLOT(menu_view_toggle_view()));

	//--
	menu_view = menuBar()->addMenu(tr("&View"));
	menu_view->addAction(act_view_toggle_fullscreen);
	menu_view->addAction(act_view_toggle_thumbs);
	menu_view->addAction(act_view_toggle_thumbnails_position);
	menu_view->addAction(act_view_toggle_tools);
	menu_view->addAction(act_view_toggle_view);
	menu_view->addSeparator();
	views_layout->fill_menu(menu_view);

	// Tools
	QAction *act_tools_lens_links_editor = new QAction(tr("&Lens links 'Exiv2' and 'lensfun' editor"), this);
	connect(act_tools_lens_links_editor, SIGNAL(triggered()), this, SLOT(menu_tools_lens_links_editor()));
	QAction *act_tools_vignetting_profiler = new QAction(tr("&Vignetting profiler"), this);
	connect(act_tools_vignetting_profiler, SIGNAL(triggered()), this, SLOT(menu_tools_vignetting_profiler()));

	menu_tools = menuBar()->addMenu(tr("&Tools"));
	menu_tools->addAction(act_tools_lens_links_editor);
# if 0
	menu_tools->addSeparator();
	menu_tools->addAction(act_tools_vignetting_profiler);
#endif

	// Help
//	QAction *act_help_about_qt = new QAction(tr("About Qt"), this);
//	connect(act_help_about_qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
	act_help_about = new QAction(tr("About..."), this);
	connect(act_help_about, SIGNAL(triggered()), this, SLOT(menu_help_about()));
	//--
	menu_help = menuBar()->addMenu(tr("&Help"));
//	menu_help->addAction(act_help_about_qt);
	menu_help->addAction(act_help_about);

	// toolbar
	// --
#ifdef Q_OS_MAC
	QSize icon_size = iconSize();
	const float scale_factor = 0.6;
	icon_size.setWidth(scale_factor * icon_size.width());
	icon_size.setHeight(scale_factor * icon_size.height());
	setIconSize(icon_size);
#endif

	QToolBar *t = addToolBar("Edit history");
	edit_history->fill_toolbar(t);
	
	// --
	t = addToolBar("Layout");
//	t->addSeparator();
	views_layout->fill_toolbar(t);

	// --
	t = addToolBar("Filters");
//	t->addSeparator();
	QList<QAction *> filters_actions = edit->get_actions();
	for(QList<QAction *>::iterator it = filters_actions.begin(); it != filters_actions.end(); ++it)
		if((*it) == nullptr)
			t->addSeparator();
		else
			t->addAction(*it);
//	t->addSeparator();
	// --
	t = addToolBar("View");
	edit->get_view_zoom()->fill_toolbar(t);
//	View_Zoom *view_zoom = new View_Zoom(edit);
//	view_zoom->fill_toolbar(t);
}

void Window::menu_help_about(void) {
	string about_str = "<H2>";
	about_str += APP_NAME;
	about_str += "</h2>";
	about_str += "version: ";
	about_str += APP_VERSION;
	about_str += "<br>";
	about_str += "Copyright &copy; 2015-2016 Mykhailo Malyshko a.k.a. Spectr";
	about_str += "<br>";
	about_str += "License: GPL version 3";
	about_str += "<br>";
	about_str += "<br>DDRoom is a digital photo processing application.";
	about_str += "<br>Project is placed at: <i><b>https://github.com/ddroom/DDRoom</b></i>.";
	about_str += "<br>Please contact author with feedbacks or donations to email: <i><b>ddroom.spectr@gmail.com</b></i>.";
	// --
	QString about;
	about += about_str.c_str();
//	about += tr("<br>DDRoom is a digital photo processing application.");
//	about += tr("<br>Project is placed at: <i><b>https://github.com/ddroom/DDRoom</b></i>");
//	about += tr("<br>Contact author for feedback or donations on: <i><b>ddroom.spectr@gmail.com</b></i>");
	about += QString("<br><br>Qt toolkit version: %1").arg(qVersion());
	about += QString("<br>Program \"dcraw\" version: %1").arg(DCRaw::get_version().c_str());
	about += QString("<br>Library \"lensfun\" version: %1.%2.%3").arg(LF_VERSION_MAJOR).arg(LF_VERSION_MINOR).arg(LF_VERSION_MICRO);
	about += QString("<br>Library \"exiv2\" version: %1").arg(Exiv2::versionString().c_str());
	QString about_title = tr("About");
	string about_title_str = " ";
	about_title_str += APP_NAME;
	about_title += about_title_str.c_str();
	QMessageBox::about(this, about_title, about);
}

void Window::menu_view_toggle_fullscreen(void) {
	switch_full_screen();
}

void Window::menu_view_toggle_view(void) {
	if(browser->get_list()->isVisible() || widget_side->isVisible()) {
		browser->get_list()->setVisible(false);
		widget_side->setVisible(false);
	} else {
		browser->get_list()->setVisible(true);
		widget_side->setVisible(true);
	}
	return;
}

void Window::menu_tools_lens_links_editor(void) {
	DB_lens_links::instance()->UI_browse_lens_links(this);
//	Profiler_Vignetting *profiler_vignetting = new Profiler_Vignetting();
//	profiler_vignetting->process(folder);
//	delete profiler_vignetting;
}

void Window::menu_tools_vignetting_profiler(void) {
	cerr << endl;
	cerr << "Menu -> Tools -> vignetting profiler" << endl;
	cerr << endl;
	std::string folder = browser->get_current_folder();
	cerr << "slot_vignetting_profiler: folder == \"" << folder << "\"" << endl;
	Profiler_Vignetting *profiler_vignetting = new Profiler_Vignetting();
	profiler_vignetting->process(folder);
	delete profiler_vignetting;
}

void Window::menu_view_toggle_thumbs(void) {
	bool is_visible = !browser->get_list()->isVisible();
	browser->get_list()->setVisible(is_visible);
	return;
}

void Window::menu_view_toggle_thumbnails_position(void) {
	if(thumbnails_position == Browser::thumbnails_top) {
		thumbnails_position = Browser::thumbnails_left;
	} else if(thumbnails_position == Browser::thumbnails_left) {
		thumbnails_position = Browser::thumbnails_bottom;
	} else if(thumbnails_position == Browser::thumbnails_bottom) {
		thumbnails_position = Browser::thumbnails_right;
	} else if(thumbnails_position == Browser::thumbnails_right) {
		thumbnails_position = Browser::thumbnails_top;
	}
	change_thumbnails_position(thumbnails_position);
}

void Window::change_thumbnails_position(int position) {
	QList<int> sizes;
	sizes.append(1);
	sizes.append(1);
	if(position == Browser::thumbnails_left) {
		sp_thumbs_left->insertWidget(0, browser->get_list());
		browser->set_thumbnails_position(Browser::thumbnails_left);
		sp_thumbs_left->setCollapsible(0, false);
		sp_thumbs_left->setCollapsible(1, false);
		sp_thumbs_left->setSizes(sizes);
	}
	if(position == Browser::thumbnails_bottom) {
		sp_thumbs_bottom->insertWidget(1, browser->get_list());
		browser->set_thumbnails_position(Browser::thumbnails_bottom);
		sp_thumbs_bottom->setCollapsible(0, false);
		sp_thumbs_bottom->setCollapsible(1, false);
		sp_thumbs_bottom->setStretchFactor(0, 1);
		sp_thumbs_bottom->setStretchFactor(1, 0);
		sp_thumbs_bottom->setSizes(sizes);
	}
	if(position == Browser::thumbnails_right) {
		sp_thumbs_right->insertWidget(1, browser->get_list());
		browser->set_thumbnails_position(Browser::thumbnails_right);
		sp_thumbs_right->setCollapsible(0, false);
		sp_thumbs_right->setCollapsible(1, false);
		sp_thumbs_right->setSizes(sizes);
	}
	if(position == Browser::thumbnails_top) {
		sp_thumbs_top->insertWidget(0, browser->get_list());
		browser->set_thumbnails_position(Browser::thumbnails_top);
		sp_thumbs_top->setCollapsible(0, false);
		sp_thumbs_top->setCollapsible(1, false);
		sp_thumbs_top->setStretchFactor(0, 0);
		sp_thumbs_top->setStretchFactor(1, 1);
		sp_thumbs_top->setSizes(sizes);
	}
}

void Window::menu_view_toggle_tools(void) {
	widget_side->setVisible(!widget_side->isVisible());
	return;
}

void Window::keyReleaseEvent(QKeyEvent *event) {
	int key = event->key();
	// pass to view for filter edit
	if(key == Qt::Key_Control) {
		edit->keyEvent(event);
		return;
	}
}

void Window::keyPressEvent(QKeyEvent *event) {
	int key = event->key();
	// pass to view for filter edit
	if(key == Qt::Key_Control) {
		edit->keyEvent(event);
		return;
	}
#ifdef DEBUG_PHOTO_LOAD
	if(key == Qt::Key_F12) {
		static bool debug_photo_load = false;
		debug_photo_load = !debug_photo_load;
		browser->debug_photo_load(debug_photo_load);
	}
#endif
}

void Window::switch_full_screen(void) {
	if(is_full_screen) {
		if(is_full_screen_from_maximized) {
			setVisible(false);
			showMaximized();
			setVisible(true);
		} else {
			showNormal();
		}
	} else {
		is_full_screen_from_maximized = windowState() & Qt::WindowMaximized;
		setVisible(false);
		showFullScreen();
		setVisible(true);
	}
	is_full_screen = !is_full_screen;
}

//------------------------------------------------------------------------------
void Window::slot_edit_preferences(void) {
	Config::instance()->show_preferences_dialog(this);
}

void Window::slot_OOM_notification(void *data) {
cerr << "slot OOM" << endl;
	OOM_desc_t *desc = (OOM_desc_t *)data;
	QString title = tr("Out of memory.");
	QString photo_name = QString::fromLocal8Bit(desc->photo_id.get_export_file_name().c_str());
	QString action = tr("export");
	if(desc->at_export == false) {
		photo_name = QString::fromLocal8Bit(desc->photo_id.get_file_name().c_str());
		photo_name += QString(" (%1)").arg(desc->photo_id.get_version_index());
		if(desc->at_open_stage)
			action = tr("open");
		else
			action = tr("process");
	}
	QString text = tr("Out of memory.<br>Can't %1 photo \"%2\".").arg(action).arg(photo_name);
	delete desc;
	QMessageBox::critical(this, title, text);
}
//------------------------------------------------------------------------------
