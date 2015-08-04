#ifndef __H_THUMBNAIL_VIEW__
#define __H_THUMBNAIL_VIEW__
/*
 * thumbnail_view.h
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

#include "window.h"
#include "browser.h"

#include "debug.h"

//------------------------------------------------------------------------------
class PhotoList_Item_t {
public:
	PhotoList_Item_t(void);
	~PhotoList_Item_t();

	std::string photo_id;	// file name and version, like "/home/user/raw/IMG_0000.CR2:0"
	std::string file_name;
	QString name;	// name at the list
	QString tooltip;
	// prescaled images
	QImage image;
	bool is_scheduled;
	bool is_loaded;
	bool flag_edit;	// settings file exist, draw pen on icon

	// versions
//	QString version_name;
	int version_index;	// 1, [2...]
	int version_count;
};

//------------------------------------------------------------------------------
class PhotoList_Delegate : public QAbstractItemDelegate {
	Q_OBJECT

public:
	PhotoList_Delegate(QSize thumb_size, QObject *parent = 0);
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;
	QSize sizeHint(int font_height) const;
	QSize icon_size(void) {return thumbnail_size;}

	void set_model(class PhotoList *_photo_list);
	void set_thumbnail_size(QSize thumbnail_size);

protected:
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
	QSize thumbnail_size;
	class PhotoList *photo_list;

	static QImage image_edit;
	static QImage image_edit_cache;
	static int image_edit_cache_size;

	int l_image_offset_x;
	int l_image_offset_y;
};

//------------------------------------------------------------------------------
class PhotoList_LoadThread : public QThread {
public:
	PhotoList_LoadThread(class PhotoList *_photo_list);

protected:
	class PhotoList *photo_list;
	void run(void);
};

//------------------------------------------------------------------------------
class PhotoList_View : public QListView {
	Q_OBJECT

public:
	PhotoList_View(PhotoList_Delegate *_photo_list_delegate, PhotoList *_photo_list, QWidget *parent = 0);
	~PhotoList_View(void);
	void set_position(Browser::thumbnails_position position);
	void set_delegate(PhotoList_Delegate *_photo_list_delegate);
	QSize sizeHint(void) const;
	void do_update(void);

	// selection
public:
	QModelIndexList get_selected_indexes(void) const;
signals:
	void signal_selection_changed(int);
protected slots:
	void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
	//

protected:
	void resizeEvent(QResizeEvent *event);
	void scrollContentsBy(int dx, int dy);
	void contextMenuEvent(QContextMenuEvent *event);
	Browser::thumbnails_position position;

private:
	PhotoList *photo_list;
	PhotoList_Delegate *photo_list_delegate;
	int space;
};

//------------------------------------------------------------------------------
class PhotoList : public QAbstractListModel {
	Q_OBJECT

public:
	PhotoList(QSize thumbnail_size, QWidget *_parent = 0);
	~PhotoList();
	void set_edit(class Edit *edit);
	QWidget *get_widget(void);

	int rowCount(const QModelIndex&) const;
	QVariant data(const QModelIndex&, int) const;

	void set_position(Browser::thumbnails_position position);

	// use pointer is safe - draw and list clean can called be only from the same (main) thread
	PhotoList_Item_t *item_from_index(const QModelIndex &index);
	void set_folder(QString id);
	void set_folder_f(void);
	void set_item_scheduled(struct thumbnail_record_t *record);
	bool is_item_to_skip(const struct thumbnail_record_t *record);

	void update_icons(void);
	void set_thumbnail_size(QSize thumbnail_size);

	// for test porposes
	int _test_count(void);
	void _test_activate_index(int);

	void update_item(PhotoList_Item_t *item, int index, std::string folder_id);
	void update_thumbnail(std::string photo_id, QImage thumbnail);
	void photo_close(std::string photo_id, bool was_changed);

	QMutex items_lock;			// to prevent delete item until icon update, and change items till redraw; access from PhotoList_Delegate

private:
	PhotoList_Delegate *thumbnail_delegate;
	PhotoList_View *view;
	QWidget *parent;
	QTimer *thumbs_update_timer;
	class Edit *edit;

	std::string current_folder_id;

	// used to store thumbnails of photos opened in View, even if it's not exist
	// as item in thumb_view for now;
	std::map<std::string, class thumbnail_desc_t> thumbnails_cache;
//	QVector<class PhotoList_Item_t> items;
	QList<class PhotoList_Item_t> items;

	class ThumbnailLoader *thumbnail_loader;

	QMutex setup_folder_lock;
	QString setup_folder_id;
	bool setup_folder_flag;
	PhotoList_LoadThread *load_thread;

	QImage image_thumb_wait;
	QImage image_thumb_empty;
	void update_template_images(void);
	void thumbs_update(void);

	// save position as icon in center of thumbnails list
	std::string scroll_list_to;
	std::string scroll_list_to_save;
	void update_scroll_list_to_save(void);

protected slots:
	void slot_icons_creation_done(void);
	void slot_item_refresh(int);
	void slot_scroll_to(int);

signals:
	void signal_icons_creation_done(void);
	void signal_item_refresh(int);
	void signal_scroll_to(int);
	
public slots:
	void thumbs_update_timeout(void);
	void slot_item_clicked(const QModelIndex &index);

signals:
	void item_clicked(std::string, QString, QImage);
	void signal_export(void);
	void signal_update_opened_photo_ids(QStringList);

	// context menu: versions, 'save as' ...
public:
	void fill_context_menu(QMenu &menu, int item_index);

protected:
	QAction *action_version_add;
	QAction *action_version_remove;
	QAction *action_save_photo_as;
	int context_menu_index;

protected slots:
	void slot_version_add(void);
	void slot_version_remove(void);
	void slot_save_photo_as(void);

	// selection
public:
	std::list<std::string> get_selection_list(void);
	void clear_selection(void);
signals:
	void signal_selection_changed(int);
protected slots:
	void slot_selection_changed(int);

};

//------------------------------------------------------------------------------

#endif // __H_THUMBNAIL_VIEW__
