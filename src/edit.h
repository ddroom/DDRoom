#ifndef __H_EDIT__
#define __H_EDIT__
/*
 * edit.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QtWidgets>

//#include "area.h"
#include "dataset.h"
#include "widgets.h"
#include "filter.h"
#include "photo.h"

//------------------------------------------------------------------------------
class Process_Runner {

public:
	Process_Runner(class Process *);
	virtual ~Process_Runner();
	void queue(void *ptr, std::shared_ptr<Photo_t>, class TilesReceiver *tiles_receiver, bool is_inactive);

protected:
	class task_t;
	void run(void);
	QList<class task_t *> tasks_list;
//	std::list<class task_t *> tasks_list;
	class Process *process;
	std::thread *std_thread = nullptr;
	std::mutex task_lock;
	std::condition_variable process_wait;
	std::mutex process_lock;
};

//------------------------------------------------------------------------------
// manage View's
// transfer request from View to Process
// reconnect filters GUI to active View and state of open Photo

class Edit : public QObject, public Coordinates_Tracer {
	Q_OBJECT

public:
	Edit(class Process *process, class Browser *_browser);
	~Edit();
	void set_edit_history(class EditHistory *);
	bool flush_current_ps(void);	// call from Batch to force save ps of all open photos

	// versions support, used from ThumbnailView for interaction with context menu
	bool version_is_open(Photo_ID photo_id);
	class PS_Loader *version_get_current_ps_loader(Photo_ID photo_id);

public slots:
	void slot_process_complete(void *, class PhotoProcessed_t *);
	void slot_action_view_grid(void);
	void slot_action_rotate_minus(void);
	void slot_action_rotate_plus(void);
	void slot_update_opened_photo_ids(QList<Photo_ID>);

signals:
	void signal_process_load_photo(std::string);

	// active photo
public:
	// return full file name of active photo or "" otherwise
	Photo_ID active_photo(void);
signals:
	void signal_active_photo_changed(void);
	void signal_browse_to_photo(Photo_ID);
protected:
	void set_session_active(int); // change 'session_active' index and emit signal
	void connect_view_grid(bool to_connect);
	//--

protected:
	class Browser *browser;
	class Process *process;
	class EditHistory *edit_history;

	class EditSession_t;
	std::vector<class EditSession_t *> sessions;
	int session_active;

	class Process_Runner *process_runner;
	class Filter_Store *fstore;

	QAction *q_action_view_grid;
	QAction *q_action_rotate_minus;
	QAction *q_action_rotate_plus;

	class PS_Loader *get_current_ps_loader(std::shared_ptr<Photo_t> photo);
	bool flush_current_ps(std::shared_ptr<Photo_t> photo);

	// photo open/close, thumbnail
public:
	void update_thumbnail(void *ptr, QImage thumbnail);

public slots:
	void slot_load_photo(Photo_ID, QString, QImage);

signals:
	void signal_update_thumbnail(Photo_ID, QImage);

protected:
	void photo_open(class EditSession_t *session, class Metadata *metadata);
	void photo_close(int session_id);
	void filters_control_clear(void);

	void action_rotate(bool clockwise);
	class EditSession_t *session_of_view(class View *view);

	// edit history
public:
	void history_apply(std::list<class eh_record_t>, bool is_undo);

	//-----------------
	// filters UI (controls)
public:
	QWidget *get_controls_widget(QWidget *parent = nullptr);
	QList<QAction *> get_actions(void);
public slots:
	void slot_update_filter(void *, void *, void *);
	void slot_update(void *, int, void *, void *);
	void slot_view_refresh(void *);
	void slot_view_update(void *, int);	// View ask to reprocess photo
	void slot_view_zoom_ui_update(void);
protected slots:
	void slot_controls_enable(bool);
signals:
	void signal_controls_enable(bool);
protected:
	QWidget *controls_widget;
	std::vector<QWidget *> filters_pages;
	QList<QAction *> filters_actions_list;

	//-----------------
	// View
public:
	QWidget *get_views_widget(QWidget *views_parent);
	void set_views_layout(int layout, int orientation);
	int get_views_layout(void);
	int get_views_orientation(int layout);
	class View_Zoom *get_view_zoom(void);
protected:
	int views_layout;	// 1,2,3,4 views
	int views_orientation[3];	// 0 -> for layout 2, 1 -> 3, 2 -> 4
	QWidget *views_parent;
	QWidget *views_widget;
	QHBoxLayout *views_box;
	class View_Splitter *views_splitter[3];
	void _reassign_views_layout(void);
	void _realign_views_layout(void);
	class View_Zoom *view_zoom;
protected slots:
	void slot_view_close(void *);
	void slot_view_active(void *);
	void slot_view_browser_reopen(void *);

	//-----------------
	// FilterEdit proxy
public:
	void keyEvent(QKeyEvent *event);
public slots:
	void slot_filter_edit(class FilterEdit *ptr, bool active, int cursor);
public:
	void draw(QPainter *painter, const QSize &viewport, const QRect &image, image_and_viewport_t transform);
	// mouse
	bool keyEvent(class FilterEdit_event_t *mt, Cursor::cursor &cursor);
	bool mousePressEvent(class FilterEdit_event_t *mt, Cursor::cursor &cursor);
	bool mouseReleaseEvent(class FilterEdit_event_t *mt, Cursor::cursor &cursor);
	bool mouseMoveEvent(class FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &cursor);
	bool enterEvent(QEvent *event);
	bool leaveEvent(QEvent *event);
protected:
	class FilterEdit *filter_edit_dummy;
	class FilterEdit *filter_edit;
	// == true, if edit mode was active
	bool leave_filter_edit(void);

	// settings copy/paste
public:
	void menu_copy_paste(class QMenu *menu);

protected:
	QAction *action_edit_copy;
	QAction *action_edit_paste;
	QAction *action_edit_fine_copy;
	QAction *action_edit_fine_paste;
	void do_copy_paste_fine(bool to_copy);
	void do_copy_paste(bool to_copy);
	void do_copy(void);
	void do_paste(void);

	class Copy_Paste_Dialog *copy_paste_dialog;
	std::set<std::string> copy_paste_filters_to_skip;
	std::map<class Filter *, class DataSet> copy_paste_map_dataset;
	void menu_copy_paste_update(void);

protected slots:
	void slot_copy_paste_copy(void);
	void slot_copy_paste_paste(void);
	void slot_copy_paste_fine_copy(void);
	void slot_copy_paste_fine_paste(void);

// public CoordinatesTracer
public:
	class Area *viewport_to_filter(class Area *viewport_coords, std::string filter_id);
	class Area *filter_to_viewport(class Area *filter_coords, std::string filter_id);
};

//------------------------------------------------------------------------------
class Copy_Paste_Dialog : public QDialog {
	Q_OBJECT

public:
	Copy_Paste_Dialog(bool to_copy, std::set<std::string> *copy_paste_set, QWidget *parent = nullptr);
	~Copy_Paste_Dialog(void);

protected:
	QVector<QPair<QCheckBox *, std::string> > flags;
	std::set<std::string> *copy_paste_set;

protected slots:
	void slot_button_ok(void);
};

//------------------------------------------------------------------------------

#endif // __H_EDIT__
