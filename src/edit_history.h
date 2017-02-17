#ifndef __H_EDIT_HISTORY__
#define __H_EDIT_HISTORY__
/*
 * edit_history.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <list>
#include <memory>

#include <QtWidgets>

#include "photo.h"
#include "dataset.h"

//------------------------------------------------------------------------------
class eh_filter_record_t {
public:
	eh_filter_record_t(void);
	eh_filter_record_t(class Filter *_filter, const std::list<class field_delta_t> &_deltas);

	class Filter *filter;
	std::list<class field_delta_t> deltas;
	QStringList get_description(void) const;
};

class eh_record_t {
public:
	eh_record_t(void);
//	eh_record_t(int cw_rotation_before, int cw_rotation_after);	// TODO: get readable description from _static_ Edit::...function();
	eh_record_t(const QVector<eh_filter_record_t> &filter_record);
//	eh_record_t(...);	// TODO: some action for filters

	enum eh_record_type_t {eh_record_type_filter, eh_record_type_cw_rotation};
	eh_record_type_t type;
	QVector<eh_filter_record_t> filter_records;
//	class field_delta_t rotation_cw;	// can be changed
	QVector<QStringList> description;	// filter_name, [field_name, value_before, value_after]
	QTime time;	// time of record creation
};

//------------------------------------------------------------------------------
class EditHistory_ItemDelegate : public QAbstractItemDelegate {
	Q_OBJECT

public:
	EditHistory_ItemDelegate(void);
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;

	void set_edit_history(class EditHistory *_edit_history);

protected:
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
	class EditHistory *edit_history;
	int get_item_height(int index, int font_height) const;
	int e_v;	// additional left and right offsets
	int e_h;	// the same, top and bottom

	void update_colors(const QStyleOptionViewItem &option);
	bool c_ready;
	QColor c_background;
	QColor c_highlight;
	QColor c_field_name;
	QColor c_field_value;
};

//------------------------------------------------------------------------------
class EditHistory : public QAbstractListModel {
	Q_OBJECT

public:
	EditHistory(class Edit *edit, QWidget *parent);
	~EditHistory();

	void fill_toolbar(QToolBar *t);
	void fill_menu(QMenu *menu);
	QWidget *get_widget(void);

	// call from Edit class three times: 'open photo', 'close photo', 'change active view'
	void add_eh_filter_record(const eh_filter_record_t &filter_record);
	void add_eh_filter_records(const QVector<eh_filter_record_t> &filter_records);
	// switch GUI to records object stored at Photo_t
	void set_current_photo(std::shared_ptr<Photo_t> photo);

	static void photo_constructor(Photo_t *photo);
	static void photo_destructor(Photo_t *photo);

	QVector<QStringList> *get_description_list(int index) const;

	bool index_just_before_pointer(int index);
	bool index_just_after_pointer(int index);
	
protected:
	EditHistory(void);

	void _create_widgets(void);
	class Edit *edit;
	class edit_history_t *edit_history;
	QWidget *parent;
	QAction *action_undo;
	QAction *action_redo;
	void update_gui_state(void);
	void move(bool is_undo);

	QWidget *widget;
	QListView *view;
	EditHistory_ItemDelegate *item_delegate;

protected slots:
	void slot_action_undo(void);
	void slot_action_redo(void);

signals:
	void signal_view_model_set(void *ptr);

	// view model
public:
	int rowCount(const QModelIndex &parent) const;
	QVariant data(const QModelIndex &index, int role) const;

protected:
	void set_view_model();
	int get_rows_count(void) const;
};

//------------------------------------------------------------------------------

#endif //__H_EDIT_HISTORY__
