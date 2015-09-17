#ifndef __H_BROWSER__
#define __H_BROWSER__
/*
 * browser.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>
#include <stdlib.h>
#include <string>

#include <QtWidgets>

#include "debug.h"
#include "photo.h"
#include "window.h"

//------------------------------------------------------------------------------
class FSTreeView : public QTreeView {
	Q_OBJECT

public:
	FSTreeView(QWidget *parent = NULL);

protected:
	void resizeEvent(QResizeEvent *event);
	int counter;
};

//------------------------------------------------------------------------------
class Browser : public QObject {
	Q_OBJECT

public:
	Browser(void);
	~Browser();
	QWidget *get_tree(void);
	QWidget *get_list(void);

	void set_edit(class Edit *);

	void photo_loaded(Photo_ID, bool);
	void photo_close(Photo_ID, bool was_changed);
	enum thumbnails_position {thumbnails_top, thumbnails_left, thumbnails_bottom, thumbnails_right};
	static std::string thumbnails_position_to_string(Browser::thumbnails_position position);
	static Browser::thumbnails_position thumbnails_position_from_string(std::string str);
	void set_thumbnails_position(Browser::thumbnails_position);

	std::string get_current_folder(void);

public slots:
	void folder_current(const QModelIndex &current, const QModelIndex &prev);
	void folder_collapsed(const QModelIndex &index);
	void folder_expanded(const QModelIndex &index);
	void slot_update_thumbnail(Photo_ID, QImage);
	void slot_browse_to_photo(Photo_ID photo_id);

	void slot_item_clicked(Photo_ID, QString, QImage);
	void slot_selection_clear(void);

	void slot_br_backward(void);
	void slot_br_forward(void);
	void slot_br_home(void);
	void slot_export(void);

signals:
	void signal_load_photo(Photo_ID, QString, QImage);
	void signal_export(void);

	// selection
public:
	std::list<Photo_ID> selected_photos_list(void);
signals:
	void signal_selection_changed(int);	// number of selected (including active) thumbnails
protected slots:
	void slot_selection_changed(int);
	//

protected slots:
	void slot_config_changed(void);

protected:
	// configurable
	std::string _folder;
	int _thumb_size;
	class PhotoList *photo_list;
	// folders tree
	QFileSystemModel *fs_model;
//	QTreeView *tree;
	FSTreeView *tree;
	QWidget *tree_widget;
	// thumbnails list
//	class ThumbnailListWidget *list;
	QListWidgetItem *active_item;
	// actions
	QAction *action_forward;
	QAction *action_backward;
	QAction *action_home;

	// block open new photo while current at loading stage
//	volatile bool image_is_loading;	// TODO: remove volatile

	void set_current_folder(std::string folder, bool center = true, std::string scroll_to = std::string(""));
	std::string photo_list_scroll_to;

	std::list<std::string> history_left;
	std::list<std::string> history_right;
	void history_seek(bool is_forward);
	void history_push(std::string record);

#ifdef DEBUG_PHOTO_LOAD
	QTimer *debug_pl_timer;
	int debug_pl_index;
	int debug_pl_dir;
	bool debug_pl_active;

public:
	void debug_photo_load(bool);

public slots:
	void debug_pl_timeout(void);

#endif
};

//------------------------------------------------------------------------------

#endif // __H_BROWSER__
