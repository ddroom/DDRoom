/*
 * browser.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
 * Folder tree view with browsing history and 'Home' button
 */

#include <iostream>
#include <list>

#include "browser.h"
#include "config.h"
#include "misc.h"
#include "system.h"
#include "thumbnail_view.h"

#include <QtGlobal>

using namespace std;

// thumbnails size base, i.e. height; use aspect 1.5 (width = height / 1.5)
#define THUMB_SIZE_DEFAULT	80
#define THUMB_SIZE_MIN		64
//#define THUMB_SIZE_MAX		160	// kind of usual thumbnail size in the raw file
//#define THUMB_SIZE_MAX		240	// kind of usual thumbnail size in the raw file
#define THUMB_SIZE_MAX		256	// kind of usual thumbnail size in the raw file

//------------------------------------------------------------------------------
// workaround for scroll to current folder at start
FSTreeView::FSTreeView(QWidget *parent) : QTreeView(parent) {
	counter = 1;
}

void FSTreeView::resizeEvent(QResizeEvent *event) {
	QTreeView::resizeEvent(event);
	if(counter > 4)
		return;
	counter++;
	QModelIndex model_index = currentIndex();
	if(model_index.isValid()) {
		QRect view_rect = viewport()->rect();
		QRect rect = visualRect(model_index);
		if(!view_rect.intersects(rect))
			scrollTo(model_index, QAbstractItemView::PositionAtCenter);
	}
}

//------------------------------------------------------------------------------
Browser::Browser(void) {
	bool flag;

	//----------------------------------
	// config
	// use home folder if config's folder doesn't exist
	flag = Config::instance()->get(CONFIG_SECTION_BROWSER, "folder", _folder);
	if(flag)
		flag = QDir(QString::fromStdString(_folder)).exists();
	if(!flag)
		_folder = System::env_home();

	// normalize thumbnail size or use default one
	Config::instance()->set_limits(CONFIG_SECTION_BROWSER, "thumb_size", THUMB_SIZE_MIN, THUMB_SIZE_MAX);
	flag = Config::instance()->get(CONFIG_SECTION_BROWSER, "thumb_size", _thumb_size);
	if(!flag)
		_thumb_size = THUMB_SIZE_DEFAULT;
	ddr::clip(_thumb_size, THUMB_SIZE_MIN, THUMB_SIZE_MAX);
	if(!flag)
		Config::instance()->set(CONFIG_SECTION_BROWSER, "thumb_size", _thumb_size);

//	cerr << "_folder == " << _folder << endl;
//	cerr << "_thumb_size == " << _thumb_size << endl;
	//----------------------------------
	// Folder tree
	fs_model = new QFileSystemModel();
	fs_model->setFilter(QDir::Dirs | QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot);
	fs_model->setReadOnly(true);
//	tree = new QTreeView();
	tree = new FSTreeView();
	tree->setModel(fs_model);
#ifdef Q_OS_LINUX
	tree->setRootIsDecorated(false);
#endif
#ifdef Q_OS_MAC
	fs_model->setResolveSymlinks(false); // to keep "/Volumes" link for "main" drive
#endif
#ifdef Q_OS_MAC
	tree->setRootIndex(fs_model->index("/Volumes"));
	fs_model->setRootPath("/Volumes");
#else
	#ifdef Q_OS_WIN
	    fs_model->setRootPath("");
	#else
		fs_model->setRootPath("/");
	#endif
#endif
	tree->setUniformRowHeights(true);
	for(int i = fs_model->columnCount(); i > 0; --i)
		tree->setColumnHidden(i, true);
	tree->setHeaderHidden(true);

	// active folder is collapsed
	connect(tree, SIGNAL(collapsed(const QModelIndex &)), this, SLOT(folder_collapsed(const QModelIndex &)));
	connect(tree, SIGNAL(expanded(const QModelIndex &)), this, SLOT(folder_expanded(const QModelIndex &)));
	connect(tree->selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(folder_current(const QModelIndex &, const QModelIndex &)));

	//----------------------------------
	// thumbnails list
	photo_list = new PhotoList(QSize(_thumb_size * 1.5, _thumb_size));
	connect(photo_list, SIGNAL(item_clicked(Photo_ID, QString, QImage)), this, SLOT(slot_item_clicked(Photo_ID, QString, QImage)));
	connect(photo_list, SIGNAL(signal_selection_changed(int)), this, SLOT(slot_selection_changed(int)));
	connect(photo_list, SIGNAL(signal_export(void)), this, SLOT(slot_export(void)));
	active_item = nullptr;

//	image_is_loading = false;

	// toolbar
	tree_widget = new QWidget();
	QVBoxLayout *l1 = new QVBoxLayout(tree_widget);
	l1->setSpacing(0);
#ifdef Q_OS_MAC
	l1->setContentsMargins(2, 2, 2, 2);
#else
	l1->setContentsMargins(0, 0, 0, 0);
#endif
/*
	QToolBar *tb = new QToolBar("browser", nullptr);
	tb->setMovable(false);
	tb->setFloatable(false);
	// TODO: add some styles management
	tb->setIconSize(QSize(20, 20));
#ifdef Q_OS_MAC
	tb->setContentsMargins(2, 2, 2, 2);
#endif
*/
	action_backward = new QAction(QIcon(":/resources/br_backward.svg"), QString("Back"), nullptr);
	action_backward->setDisabled(true);
	action_forward = new QAction(QIcon(":/resources/br_forward.svg"), QString("Forward"), nullptr);
	action_forward->setDisabled(true);
	action_home = new QAction(QIcon(":/resources/br_home.svg"), QString("Home"), nullptr);
/*
	tb->addAction(action_backward);
	tb->addAction(action_forward);
	tb->addAction(action_home);
*/
	QHBoxLayout *hb = new QHBoxLayout();
	hb->setSpacing(2);
#ifdef Q_OS_MAC
	hb->setContentsMargins(2, 2, 2, 2);
#else
	hb->setContentsMargins(0, 0, 0, 0);
#endif
	hb->setAlignment(Qt::AlignLeft);
	QToolButton *tb_backward = new QToolButton();
	tb_backward->setDefaultAction(action_backward);
	// TODO: add something like style->init_toolbutton(QToolButton *);
	tb_backward->setIconSize(QSize(20, 20));
	tb_backward->setAutoRaise(true);
	hb->addWidget(tb_backward);
	QToolButton *tb_forward = new QToolButton();
	tb_forward->setDefaultAction(action_forward);
	tb_forward->setAutoRaise(true);
	tb_forward->setIconSize(QSize(20, 20));
	hb->addWidget(tb_forward);

	QToolButton *tb_home = new QToolButton();
	tb_home->setDefaultAction(action_home);
	tb_home->setAutoRaise(true);
	tb_home->setIconSize(QSize(20, 20));
	hb->addWidget(tb_home);
//	l->addWidget(w);

	connect(action_backward, SIGNAL(triggered(void)), this, SLOT(slot_br_backward(void)));
	connect(action_forward, SIGNAL(triggered(void)), this, SLOT(slot_br_forward(void)));
	connect(action_home, SIGNAL(triggered(void)), this, SLOT(slot_br_home(void)));
	connect(Config::instance(), SIGNAL(changed(void)), this, SLOT(slot_config_changed(void)));

	l1->addLayout(hb);	// toolbar
	l1->addWidget(tree);

#ifdef DEBUG_PHOTO_LOAD
	// debug timer to reload photos in cycle
	debug_pl_timer = new QTimer();
	debug_pl_timer->setInterval(1000);
//	debug_pl_timer->setInterval(50);
//	debug_pl_timer->setInterval(500);
	debug_pl_timer->setSingleShot(true);
	connect(debug_pl_timer, SIGNAL(timeout()), this, SLOT(debug_pl_timeout(void)));

	debug_pl_active = false;
	debug_pl_index = 0;
	debug_pl_dir = true;
#endif

	// apply setting 
	string __folder = _folder;
	_folder = "";	// reset to update current folder
	set_current_folder(__folder);

//cerr << "...done" << endl;
}

Browser::~Browser() {
	delete photo_list;
#ifdef DEBUG_PHOTO_LOAD
	delete debug_pl_timer;
#endif
	Config::instance()->set(CONFIG_SECTION_BROWSER, "folder", _folder);
//	Config::instance()->set(CONFIG_SECTION_BROWSER, "thumb_size", _thumb_size);
}

void Browser::set_edit(class Edit *edit) {
	photo_list->set_edit(edit);
}

QWidget *Browser::get_tree(void) {
	return tree_widget;
}

QWidget *Browser::get_list(void) {
	return photo_list->get_widget();
}

void Browser::slot_config_changed(void) {
	int new_thumb_size = _thumb_size;
	bool flag = Config::instance()->get(CONFIG_SECTION_BROWSER, "thumb_size", new_thumb_size);
	if(flag && new_thumb_size != _thumb_size) {
//cerr << "slot_config_changed" << endl;
		_thumb_size = new_thumb_size;
		photo_list->set_thumbnail_size(QSize(new_thumb_size * 1.5, new_thumb_size));
	}	
}

//------------------------------------------------------------------------------
// selection
std::list<Photo_ID> Browser::selected_photos_list(void) {
	std::list<Photo_ID> _list = photo_list->get_selection_list();
	return _list;
}

void Browser::slot_selection_changed(int count) {
	emit signal_selection_changed(count);
}

void Browser::slot_export(void) {
	emit signal_export();
}

//------------------------------------------------------------------------------
// Browsing history - count only non-empty folders
void Browser::slot_br_backward(void) {
	history_seek(false);
}

void Browser::slot_br_forward(void) {
	history_seek(true);
}

void Browser::slot_br_home(void) {
	set_current_folder(System::env_home());
}

void Browser::set_current_folder(string folder, bool center, std::string scroll_to) {
	QModelIndex index = fs_model->index(QString::fromStdString(folder));
	if(index.isValid() == false)
		photo_list->set_folder(QString(""));
	photo_list_scroll_to = scroll_to;
	tree->setCurrentIndex(index);
	tree->setExpanded(index, true);
	tree->scrollTo(index, (center) ? QAbstractItemView::PositionAtCenter : QAbstractItemView::EnsureVisible);
	tree->resizeColumnToContents(0);
}

void Browser::history_seek(bool is_forward) {
	if(is_forward) {
		string folder = history_right.front();
		history_right.pop_front();
		if(history_right.empty())
			action_forward->setDisabled(true);
		history_left.push_back(_folder);
		action_backward->setEnabled(true);
		_folder = "";
		set_current_folder(folder, false);
	} else {
		string folder = history_left.back();
		history_left.pop_back();
		if(history_left.empty())
			action_backward->setDisabled(true);
		history_right.push_front(_folder);
		action_forward->setEnabled(true);
		_folder = "";
		set_current_folder(folder, false);
	}
}

void Browser::history_push(string record) {
	if(record.empty())
		return;
	history_right.erase(history_right.begin(), history_right.end());
	action_forward->setDisabled(true);
	history_left.push_back(record);
	action_backward->setEnabled(true);
}

//------------------------------------------------------------------------------
void Browser::folder_collapsed(const QModelIndex &index) {
	string _f = _folder;
	string separator = QDir::toNativeSeparators("/").toStdString();
	_f += separator;
	string n = fs_model->filePath(index).toStdString();
	if(n != separator)
		n += separator;
	const char *f1 = _f.c_str();
	const char *f2 = n.c_str();
	unsigned i = 0;
	for(; f1[i] && f2[i]; ++i)
		if(f1[i] != f2[i])
			break;
	if(i == n.length()) {
		tree->setCurrentIndex(index);
		folder_current(index, index);
	}
	tree->resizeColumnToContents(0);
}

void Browser::folder_expanded(const QModelIndex &index) {
	tree->resizeColumnToContents(0);
}

void Browser::folder_current(const QModelIndex &current, const QModelIndex &prev) {
	QString qs_folder = fs_model->filePath(current);
	string n = qs_folder.toStdString();
	if(n == _folder)
		return;
	// update thumbnails to new folder content
	photo_list->set_folder(qs_folder, photo_list_scroll_to);
	photo_list_scroll_to = "";
	history_push(_folder);
	_folder = n;
//cerr << "Browser::folder_current: " << _folder << endl;
}

std::string Browser::get_current_folder(void) {
	return _folder;
}

void Browser::slot_item_clicked(Photo_ID photo_id, QString name, QImage icon) {
//	if(image_is_loading)
//		return;
	// TODO: change behavior for item selection - by double-click, not by one click...
//	image_is_loading = true;
	emit signal_load_photo(photo_id, name, icon);
}

void Browser::slot_selection_clear(void) {
	photo_list->clear_selection();
}

void Browser::photo_loaded(Photo_ID photo_id, bool is_loaded) {
	if(is_loaded == false)
		photo_list->photo_close(photo_id, false);	// clear thumbnail cache
//	image_is_loading = false;
	// TODO: send signal to thumbnail_view
#ifdef DEBUG_PHOTO_LOAD
	if(debug_pl_active)
		debug_pl_timer->start();
#endif
}

void Browser::slot_update_thumbnail(Photo_ID photo_id, QImage thumbnail) {
//cerr << "slot_update_thumbnail for " << photo_id << endl;
	photo_list->update_thumbnail(photo_id, thumbnail);
}

void Browser::slot_browse_to_photo(Photo_ID photo_id) {
	std::string photo_id_str = photo_id.get_file_name();
//cerr << "browse to photo: " << photo_id_str << endl;
	QString separator = QDir::toNativeSeparators("/");
	QString photo = QDir::toNativeSeparators(QString::fromStdString(photo_id_str));
	QStringList f_list = photo.split(separator);
	QString photo_file = f_list.takeLast();
	string photo_file_str = photo_file.toStdString();
	QString photo_folder = f_list.join(separator);
	string photo_folder_str = photo_folder.toStdString();
//cerr << "	photo_file_str == " << photo_file_str << endl;
//	photo_list->set_folder(photo_folder, photo_file_str);
//	set_current_folder(photo_folder_str, false, photo_file_str);
	set_current_folder(photo_folder_str, false, photo_id_str);
}

void Browser::photo_close(Photo_ID photo_id, bool was_changed) {
//cerr << "photo_close for " << photo_id << endl;
	photo_list->photo_close(photo_id, was_changed);
}

std::string Browser::thumbnails_position_to_string(Browser::thumbnails_position position) {
	string str = "top";
	if(position == Browser::thumbnails_left)
		str = "left";
	if(position == Browser::thumbnails_bottom)
		str = "bottom";
	if(position == Browser::thumbnails_right)
		str = "right";
	return str;
}

Browser::thumbnails_position Browser::thumbnails_position_from_string(std::string str) {
	Browser::thumbnails_position position = Browser::thumbnails_top;
	if(str == "left")
		position = Browser::thumbnails_left;
	if(str == "bottom")
		position = Browser::thumbnails_bottom;
	if(str == "right")
		position = Browser::thumbnails_right;
	return position;
}

void Browser::set_thumbnails_position(Browser::thumbnails_position position) {
	photo_list->set_position(position);
}

#ifdef DEBUG_PHOTO_LOAD
void Browser::debug_photo_load(bool state_active) {
	debug_pl_active = state_active;
	if(state_active) {
		debug_pl_index = 0;
		debug_pl_timer->start();
	} else
		debug_pl_timer->stop();
}

void Browser::debug_pl_timeout(void) {
cerr << "void Browser::debug_pl_timeout(void)" << endl;
#ifdef DEBUG_PHOTO_LOAD_RND
	for(;true;) {
		double rnd = random();
		rnd /= RAND_MAX;
		rnd *= double(-0.6) + photo_list->count();
		debug_pl_index = rnd;
		if(debug_pl_index < 0)
			debug_pl_index = 0;
		if(debug_pl_index > photo_list->count() - 1)
			debug_pl_index = photo_list->count() - 1;
		if(debug_pl_index != photo_list->currentRow())
			break;
	}
#else
	if(debug_pl_dir)
		debug_pl_index++;
	else
		debug_pl_index--;
	if(debug_pl_index >= photo_list->_test_count()) {
		debug_pl_index = photo_list->_test_count() - 2;
		debug_pl_dir = false;
	}
	if(debug_pl_index < 0) {
		debug_pl_index = 1;
		debug_pl_dir = true;
	}
#endif
	photo_list->_test_activate_index(debug_pl_index);
}
#endif

//------------------------------------------------------------------------------
