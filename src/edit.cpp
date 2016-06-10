/*
 * edit.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * Edit:
 *	- manage filters UI
 *	- open/close photo edit session, keep association between session and View
 *	- manage photo settings: copy/paste/reset
 *	- create/remove photo duplicate (as copy in browser)

* TODO:
	- load metadata with Import not Import_Raw
	- split "process source" from one element to two - actual process source, by event,
		and source of update as filter, if any - should be necessary for improved cache system,
		and correct undo/redo with F_Demosaic;
	 --use dynamic list of 'edit' filters;

 */

#include <iostream>

#include "browser.h"
#include "config.h"
#include "edit.h"
#include "edit_history.h"
#include "filter.h"
#include "import_raw.h"	// TODO: change usage from Import_Raw to Import for metadata load
#include "photo_storage.h"
#include "process_h.h"
#include "view.h"
#include "views_layout.h"
#include "view_zoom.h"

#include "f_process.h"
#include "f_demosaic.h"
#include "f_chromatic_aberration.h"
#include "f_projection.h"
#include "f_distortion.h"
#include "f_shift.h"
#include "f_rotation.h"
#include "f_crop.h"
#include "f_soften.h"
/*
#include "f_scale.h"
*/
#include "f_wb.h"
#include "f_crgb_to_cm.h"
#include "f_cm_lightness.h"
#include "f_cm_rainbow.h"
#include "f_cm_sepia.h"
#include "f_cm_colors.h"
#include "f_unsharp.h"
#include "f_cm_to_rgb.h"
/*
#include "f_curve.h"
#include "f_invert.h"
*/

#include <QDialog>

using namespace std;

//------------------------------------------------------------------------------
class Process_Runner::task_t {
public:
	void *ptr;
	std::shared_ptr<Photo_t> photo;
	int request_ID;
	class TilesReceiver *tiles_receiver;
	bool is_inactive;
	map<class Filter *, std::shared_ptr<PS_Base> > map_ps_base;
};

Process_Runner::Process_Runner(Process *process) {
	this->process = process;
	auto obj = this;
	std_thread = new std::thread( [obj](void){obj->run();} );
}

Process_Runner::~Process_Runner() {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void Process_Runner::queue(void *ptr, std::shared_ptr<Photo_t> photo, TilesReceiver *tiles_receiver, bool is_inactive) {
//	task_lock.lock();
	int request_ID = Process::newID();
	int request_ID_to_abort = 0;
	if(photo->process_source == ProcessSource::s_view_tiles)
		request_ID_to_abort = tiles_receiver->add_request_ID(request_ID);
	else
		request_ID_to_abort = tiles_receiver->set_request_ID(request_ID);
	Process::ID_request_abort(request_ID_to_abort);

	map<Filter *, std::shared_ptr<PS_Base> > map_ps_base;
	for(auto it = photo->map_ps_base_current.begin(); it != photo->map_ps_base_current.end(); ++it) {
		map_ps_base[(*it).first] = std::shared_ptr<PS_Base>((*it).first->newPS());
		map_ps_base[(*it).first]->load(&photo->map_dataset_current[(*it).first]);
	}
	task_t *t = new task_t;
	t->ptr = ptr;
	t->photo = photo;
	t->request_ID = request_ID;
	t->tiles_receiver = tiles_receiver;
	t->is_inactive = is_inactive;
	t->map_ps_base = map_ps_base;
	// create new list, put there records with Photo_t * other than requested, and put this last request in front
	// and also - remove all records for the same tiles_receiver, as deprecated now
	QList<task_t *> list_new;
	list_new.push_back(t);
	task_lock.lock();
	for(auto it = tasks_list.begin(); it != tasks_list.end(); ++it) {
		if((*it)->photo.get() != photo.get() && (*it)->tiles_receiver != t->tiles_receiver) {
			list_new.push_back((*it));
		} else {
			delete *it;
		}
	}
	tasks_list = list_new;
if(tasks_list.empty())
cerr << "fatal error: tasks_list is empty" << endl;
	// wake up!
	task_lock.unlock();
	process_wait.notify_all();
//cerr << "wake up all; request_ID == " << request_ID << "; to_abort == " << request_ID_to_abort << endl;
}

void Process_Runner::run(void) {
	while(true) {
		std::unique_lock<std::mutex> locker(task_lock, std::defer_lock);
		locker.lock();
		while(true) {
			if(!tasks_list.empty())
				break;
			process_wait.wait(locker);
		}
		//--
		task_t *t = tasks_list.back();
		tasks_list.pop_back();
		locker.unlock();
//cerr << "call process->process_online(...) for request_ID: " << t->request_ID << endl;
		process->process_online(t->ptr, t->photo, t->request_ID, t->is_inactive, t->tiles_receiver, t->map_ps_base);
//cerr << "call process->process_online(...) for request_ID: " << t->request_ID << " - done!" << endl;
		//--
		delete t;
	}
}

//------------------------------------------------------------------------------
class FilterEditDummy : public FilterEdit {
public:
	virtual void edit_mode_exit(void) {};
	virtual void edit_mode_forced_exit(void) {};
};

//------------------------------------------------------------------------------
class Edit::EditSession_t {
public:
	EditSession_t(void);
	class View *view;
	std::shared_ptr<Photo_t> photo;
	bool is_loading;
	bool is_loaded;
};

Edit::EditSession_t::EditSession_t(void) {
	view = nullptr;
	is_loading = false;
	is_loaded = false;
}

//------------------------------------------------------------------------------
Edit::Edit(Process *process, Browser *_browser) {
	browser = _browser;
	browser->set_edit(this);
	view_zoom = new View_Zoom();
	// keep view configuration, as View background color too
	bool flag;
	
	// background color by default
	int view_bg_r = 0x7F;
	int view_bg_g = 0x7F;
	int view_bg_b = 0x7F;
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_R", view_bg_r);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "background_color_R", view_bg_r);
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_G", view_bg_g);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "background_color_G", view_bg_g);
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_B", view_bg_b);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "background_color_B", view_bg_b);

	controls_widget = nullptr;
	this->process = process;
	this->fstore = Filter_Store::_this;
	process_runner = new Process_Runner(process);

	// helper-grid, provided by View
	q_action_view_grid = new QAction(QIcon(":/resources/view_grid.svg"), tr("Show helper grid to use with filters 'shift', 'rotation' etc."), this);
	q_action_view_grid->setCheckable(true);
	connect_view_grid(true);

	// -90/+90 rotation
	q_action_rotate_minus = new QAction(QIcon(":/resources/rotate_minus.svg"), tr("Rotate 90 degree counterclockwise"), this);
	connect(q_action_rotate_minus, SIGNAL(triggered()), this, SLOT(slot_action_rotate_minus()));

	q_action_rotate_plus = new QAction(QIcon(":/resources/rotate_plus.svg"), tr("Rotate 90 degree clockwise"), this);
	connect(q_action_rotate_plus, SIGNAL(triggered()), this, SLOT(slot_action_rotate_plus()));

	// enable/disable filters GUI during image processing
//	connect(this, SIGNAL(signal_controls_enable(bool)), this, SLOT(slot_controls_enable(bool)));

	// processing complete and here is result
	connect(process, SIGNAL(signal_process_complete(void *, class PhotoProcessed_t *)), this, SLOT(slot_process_complete(void *, class PhotoProcessed_t *)));

	// FilterEdit
	for(list<pair<FilterEdit *, Filter *> >::iterator it = fstore->filter_edit_list.begin(); it != fstore->filter_edit_list.end(); ++it)
		connect((*it).second, SIGNAL(signal_filter_edit(FilterEdit *, bool, int)), this, SLOT(slot_filter_edit(FilterEdit *, bool, int)));

	filter_edit_dummy = new FilterEditDummy();
	filter_edit = filter_edit_dummy;

	// filters UI update
//	for(list<Filter *>::iterator it = fstore->filters.begin(); it != fstore->filters.end(); ++it)
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it)
		connect(*it, SIGNAL(signal_update(void *, void *, void *)), this, SLOT(slot_update_filter(void *, void *, void *)));
	// !GEOMETRY!
	connect((QObject *)fstore->f_crop, SIGNAL(signal_view_refresh(void *)), this, SLOT(slot_view_refresh(void *)));

	// sessions
	EditSession_t *session;
	views_widget = new QWidget();
	views_widget->setAttribute(Qt::WA_NoSystemBackground, true);
	views_box = new QHBoxLayout(views_widget);
	views_box->setSpacing(0);
	views_box->setContentsMargins(0, 0, 0, 0);
	for(int i = 0; i < 3; ++i)
		views_splitter[i] = nullptr;
	views_parent = nullptr;
	// create 4 sessions and views
	for(int i = 0; i < 4; ++i) {
		session = new EditSession_t;
		session->view = View::create(this);
		sessions.push_back(session);
		connect(session->view, SIGNAL(signal_view_active(void *)), this, SLOT(slot_view_active(void *)));
		connect(session->view, SIGNAL(signal_view_close(void *)), this, SLOT(slot_view_close(void *)));
		connect(session->view, SIGNAL(signal_view_browser_reopen(void *)), this, SLOT(slot_view_browser_reopen(void *)));
		connect(session->view, SIGNAL(signal_process_update(void *, int)), this, SLOT(slot_view_update(void *, int)));
		connect(session->view, SIGNAL(signal_zoom_ui_update(void)), this, SLOT(slot_view_zoom_ui_update(void)));
	}

//	views_layout = 2;
	views_layout = 1;
	views_orientation[0] = 0;
	views_orientation[1] = 2;
	views_orientation[2] = 0;
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "views_layout", views_layout);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout", views_layout);
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "views_layout_2_orientation", views_orientation[0]);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_2_orientation", views_orientation[0]);
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "views_layout_3_orientation", views_orientation[1]);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_3_orientation", views_orientation[1]);
	flag = Config::instance()->get(CONFIG_SECTION_VIEW, "views_layout_4_orientation", views_orientation[2]);
	if(flag == false)	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_4_orientation", views_orientation[2]);
	ddr::clip(views_layout, 1, 4);
	ddr::clip(views_orientation[0], 0, 1);
	ddr::clip(views_orientation[1], 0, 5);
	ddr::clip(views_orientation[2], 0, 1);
/*
	views_layout %= 4;
	views_orientation[0] %= 2;
	views_orientation[1] %= 6;
	views_orientation[2] %= 2;
*/

	session_active = 0;
	view_zoom->set_View(sessions[session_active]->view);
	_reassign_views_layout();
}

void Edit::set_edit_history(class EditHistory *_edit_history) {
	edit_history = _edit_history;
}

Edit::~Edit() {
	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout", views_layout);
	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_2_orientation", views_orientation[0]);
	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_3_orientation", views_orientation[1]);
	Config::instance()->set(CONFIG_SECTION_VIEW, "views_layout_4_orientation", views_orientation[2]);
	//--
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i]->photo) {
cerr << "close photo " << sessions[i]->photo->photo_id.get_export_file_name() << endl;
			photo_close(i);
		}
		delete sessions[i];
	}
	delete filter_edit_dummy;
	delete view_zoom;
}

void Edit::connect_view_grid(bool to_connect) {
	if(to_connect)
		connect(q_action_view_grid, SIGNAL(triggered()), this, SLOT(slot_action_view_grid()));
	else
		disconnect(q_action_view_grid, SIGNAL(triggered()), this, SLOT(slot_action_view_grid()));
}

void Edit::set_views_layout(int layout, int orientation) {
	if(layout < 1)	layout = 4;
	if(layout > 4)	layout = 1;
	views_layout = layout;
	if(layout > 1 && layout < 5) {
		int i = layout - 2;
		int j = (i == 1) ? 5 : 1;
		if(orientation < 0)	orientation = j - 1;
		if(orientation > j)	orientation = 0;
		views_orientation[i] = orientation;
	}
	_reassign_views_layout();
}

int Edit::get_views_layout(void) {
	return views_layout;
}

int Edit::get_views_orientation(int layout) {
	if(layout < 2 || layout > 4)	return 0;
	return views_orientation[layout - 2];
}

View_Zoom *Edit::get_view_zoom(void) {
	return view_zoom;
}

void Edit::_reassign_views_layout(void) {
	for(int i = 0; i < 3; ++i) {
		if(views_splitter[i] != nullptr)
			views_splitter[i]->setVisible(false);
	}
	for(int i = 0; i < 4; ++i) {
		if(views_parent != nullptr)
			sessions[i]->view->widget()->setParent(views_parent);
		sessions[i]->view->widget()->hide();
	}
	views_box->removeWidget(views_splitter[0]);
	for(int i = 2; i >= 0; --i) {
		if(views_splitter[i] != nullptr) {
			delete views_splitter[i];
			views_splitter[i] = nullptr;
		}
	}

	Qt::Orientation or_master = Qt::Horizontal;
	Qt::Orientation or_slave = Qt::Vertical;
	int orientation = 0;
	if(views_layout > 1)
		orientation = views_orientation[views_layout - 2];
	if(orientation % 2) {
		or_master = Qt::Vertical;
		or_slave = Qt::Horizontal;
	}
	views_splitter[0] = new View_Splitter(or_master);
	if(views_layout == 4) {
		views_splitter[1] = new View_Splitter(or_slave);
		views_splitter[2] = new View_Splitter(or_slave);
		int indexes_horizontal[4] = {0, 2, 1, 3};
		int indexes_vertical[4] = {0, 1, 2, 3};
		int *indexes = indexes_horizontal;
		if(orientation % 2)
			indexes = indexes_vertical;
		for(int i = 0; i < 4; ++i) {
			int j = i / 2 + 1;
			views_splitter[j]->addWidget(sessions[indexes[i]]->view->widget());
			views_splitter[j]->setStretchFactor(i % 2, 1);
			views_splitter[j]->setCollapsible(i % 2, false);
			sessions[i]->view->widget()->show();
		}
		for(int i = 0; i < 2; ++i) {
			views_splitter[0]->addWidget(views_splitter[i + 1]);
			views_splitter[0]->setStretchFactor(i, 1);
			views_splitter[0]->setCollapsible(i, false);
		}
	} else {
		if(views_layout == 3 && orientation > 1) {
			views_splitter[1] = new View_Splitter(or_slave);
			int indexes[2] = {1, 2};
			if(orientation == 2 || orientation == 3) {
				views_splitter[0]->addWidget(sessions[0]->view->widget());
				views_splitter[0]->addWidget(views_splitter[1]);
			}
			if(orientation == 4 || orientation == 5) {
				views_splitter[0]->addWidget(views_splitter[1]);
				views_splitter[0]->addWidget(sessions[0]->view->widget());
			}
			if(orientation == 3 || orientation == 4) {
				indexes[0] = 2;
				indexes[1] = 1;
			}
			for(int i = 0; i < 2; ++i) {
				views_splitter[1]->addWidget(sessions[indexes[i]]->view->widget());
				views_splitter[1]->setStretchFactor(i % 2, 1);
				views_splitter[1]->setCollapsible(i % 2, false);
				sessions[indexes[i]]->view->widget()->show();
				views_splitter[0]->setStretchFactor(i % 2, 1);
				views_splitter[0]->setCollapsible(i % 2, false);
			}
			sessions[0]->view->widget()->show();
			sessions[1]->view->widget()->show();
			sessions[2]->view->widget()->show();
		} else {
			for(int i = 0; i < views_layout; ++i) {
				views_splitter[0]->addWidget(sessions[i]->view->widget());
				views_splitter[0]->setStretchFactor(i, 1);
				views_splitter[0]->setCollapsible(i, false);
				sessions[i]->view->widget()->show();
			}
		}
	}
	_realign_views_layout();
	views_box->addWidget(views_splitter[0], 1);
/*
	for(int i = 0; i < 3; ++i) {
		if(views_splitter[i] != nullptr)
			views_splitter[i]->setOpaqueResize(false);
	}
*/
	// check invisible views - what to do with open photos in them?
	// reassign active view if previous one is invisible now
	int index = views_layout - 1;
	if(session_active > index) {
		int s = session_active;
		set_session_active(index);
		sessions[s]->view->set_active(false);
		sessions[session_active]->view->set_active(true);
	}
}

void Edit::_realign_views_layout(void) {
	// set equal sizes inside splitter
	for(int i = 2; i >= 0; --i) {
		if(views_splitter[i] == nullptr)
			continue;
		QList<int> s = views_splitter[i]->sizes();
		if(s.size() == 0)
			continue;
		int sum = 0;
		for(int j = 0; j < s.size(); ++j)
			sum += s[j];
		sum /= s.size();
		for(int j = 0; j < s.size(); ++j)
			s[j] = sum;
		views_splitter[i]->setSizes(s);
	}
}

QWidget *Edit::get_views_widget(QWidget *_views_parent) {
	views_parent = _views_parent;
	return views_widget;
}

void Edit::slot_process_complete(void *ptr, PhotoProcessed_t *photo_processed) {
	EditSession_t *session = (EditSession_t *)ptr;
	if(!session->photo) {
		session->is_loading = false;
		return;
	}
	Photo_ID photo_id = session->photo->photo_id;
	bool is_loaded = true;
	if(photo_processed->is_empty == false) {
		slot_controls_enable(true);
//		emit signal_controls_enable(true);
//		cerr << "Edit::slot_process_complete(); photo != nullptr" << endl;
	} else {
		// TODO: signal import failure somehow
		// photo object was destroyed at Process class
		is_loaded = false;
		session->photo.reset();
		slot_controls_enable(false);
//		emit signal_controls_enable(false);
//		cerr << "Edit::slot_process_done(); photo == nullptr" << endl;
	}
	browser->photo_loaded(photo_id, is_loaded);
	session->view->photo_open_finish(photo_processed);
	session->is_loading = false;
	session->is_loaded = is_loaded;
}

void Edit::slot_view_refresh(void *session_id) {
	EditSession_t *session = (EditSession_t *)session_id;
	if(session_id != nullptr)
		session->view->view_refresh();
}

void Edit::slot_action_view_grid(void) {
	if(sessions.size() == 0 || session_active < 0)
        return;
    EditSession_t *session = sessions[session_active];
    View *view = session->view;
	bool checked = q_action_view_grid->isChecked();
	view->helper_grid_enable(checked);
}

void Edit::slot_action_rotate_minus(void) {
	action_rotate(false);
}

void Edit::slot_action_rotate_plus(void) {
	action_rotate(true);
}

//------------------------------------------------------------------------------
// TODO: move that to f_cw_rotation empty filter with PS object to support setting version and undo/redo;
//	but signal 'update' send to View instead of Process;
void Edit::action_rotate(bool clockwise) {
	if(sessions.size() == 0 || session_active < 0)
		return;
	EditSession_t *session = sessions[session_active];
	View *view = session->view;
	view->update_rotation(clockwise);
	// Notify each filter about that event. Particularly filter F_Shift have interest in that
	int cw_rotation = session->photo->cw_rotation;
	for(list<pair<FilterEdit *, Filter *> >::iterator it = fstore->filter_edit_list.begin(); it != fstore->filter_edit_list.end(); ++it)
		(*it).first->set_cw_rotation(cw_rotation);
	// add undo/redo record
}

//------------------------------------------------------------------------------
void Edit::set_session_active(int index) {
	session_active = index;
	emit signal_active_photo_changed();
}

Photo_ID Edit::active_photo(void) {
	Photo_ID photo_id;
	if(sessions.size() != 0 && session_active >= 0) {
		std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
		if(photo)
			photo_id = photo->photo_id;
	}
	return photo_id;
}

//------------------------------------------------------------------------------
bool Edit::version_is_open(Photo_ID photo_id) {
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i] != nullptr)
			if(sessions[i]->photo)
				if(sessions[i]->photo->photo_id == photo_id)
					return true;
	}
	return false;
}

PS_Loader *Edit::version_get_current_ps_loader(Photo_ID photo_id) {
	std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
	if(!photo)
		return nullptr;
	if(photo->photo_id != photo_id)
		return nullptr;
	return get_current_ps_loader(photo);
}

//------------------------------------------------------------------------------
//
void Edit::slot_view_close(void *data) {
	View *view = (View *)data;
	int session_id = -1;
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i]->view == view) {
			session_id = i;
			break;
		}
	}
	if(session_id == -1)
		throw("FATAL: Edit::slot_view_close(): session_id == -1");
	bool flag_reset = (session_id == session_active);
	// clear view
	view->photo_open_start(QImage());
	view->helper_grid_enable(false);
	photo_close(session_id);
	if(flag_reset) {
		filters_control_clear();
		connect_view_grid(false);
		q_action_view_grid->setChecked(false);
		connect_view_grid(true);
	}
}

void Edit::slot_view_browser_reopen(void *data) {
	View *view = (View *)data;
	std::shared_ptr<Photo_t> open_photo;
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i]->view == view) {
			open_photo = sessions[i]->photo;
			break;
		}
	}
	if(!open_photo)
		return;
	emit signal_browse_to_photo(open_photo->photo_id);
}

// switch filter controls from one view to another
void Edit::slot_view_active(void *data) {
//cerr << "Edit::slot_view_active()" << endl;
	View *view = (View *)data;
	bool update = false;
	std::shared_ptr<Photo_t> photo_prev = sessions[session_active]->photo;
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i]->view == view) {
			set_session_active(i);
			update = true;
			break;
		}
	}
	// here active session is other, but filter keep previous session_id, so process on update will be forced offline
	// leave 'edit' mode
	// TODO: use dynamic list of 'edit' filters
//	fstore->f_wb->edit_mode_forced_exit();
//	fstore->f_projection->edit_mode_forced_exit();
	fstore->f_shift->edit_mode_forced_exit();
	fstore->f_rotation->edit_mode_forced_exit();
	fstore->f_crop->edit_mode_forced_exit();
	if(update) {
//cerr << "Edit::slot_view_active() ...1" << endl;
		std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
		QList<Filter *> filters_list = fstore->get_filters_list();
		if(photo_prev) {
			// store current filter's state
//cerr << "Edit::slot_view_active(), photo_prev->photo_id == \"" << photo_prev->photo_id << "\"" << endl;
			for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it)
				(*it)->saveFS(photo_prev->map_fs_base[*it]);
			filters_control_clear();
		}
		if(photo) {
//cerr << "Edit::slot_view_active(), photo_id == \"" << photo->photo_id << "\"" << endl;
			PS_and_FS_args_t args(photo->metadata, photo->cw_rotation);
			for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
				Filter *f = *it;
				// store current filter's state
//				if(photo_prev)
//					f->saveFS(photo_prev->map_fs_base[f]);
				// switch to the newly active session
				f->set_session_id((void *)sessions[session_active]);
//				f->setPS(photo->map_ps_base[f]);
//				f->load_ui(photo->map_fs_base[f], photo->metadata);
				f->set_PS_and_FS(photo->map_ps_base[f], photo->map_fs_base[f], args);
			}
			slot_controls_enable(sessions[session_active]->is_loaded);
//			emit signal_controls_enable(photo->is_loaded);
//			emit signal_controls_enable(true);
		} else {
			// disable and reset filter's controls
//			filters_control_clear();
		}
		edit_history->set_current_photo(photo);
	}
	menu_copy_paste_update();
	view_zoom->set_View(view);
	connect_view_grid(false);
	q_action_view_grid->setChecked(view->helper_grid_enabled());
	connect_view_grid(true);
}

// disable and reset filters control
void Edit::filters_control_clear(void) {
	slot_controls_enable(false);
//	emit signal_controls_enable(false);
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		Filter *f = *it;
		f->set_session_id(nullptr);
//		f->setPS(nullptr);
//cerr << "filter: " << f->name() << endl;
//		f->load_ui(nullptr, nullptr);
		f->set_PS_and_FS(nullptr, nullptr, PS_and_FS_args_t());
	}
}

//------------------------------------------------------------------------------
void Edit::slot_update_opened_photo_ids(QList<Photo_ID> ids_list) {
	int c = ids_list.size() / 2;
	for(int i = 0; i < c; ++i) {
		Photo_ID id_before = ids_list.at(i * 2 + 0);
		Photo_ID id_after = ids_list.at(i * 2 + 1);
//cerr << "id_before == " << id_before.get_export_file_name() << endl;
//cerr << " id_after == " << id_after.get_export_file_name() << endl;
		for(int j = 0; j < 4; ++j) {
			if(sessions[j]->photo) {
				if(sessions[j]->photo->photo_id == id_before) {
					std::list<int> v_list = PS_Loader::versions_list(id_after.get_file_name());
					sessions[j]->photo->ids_lock.lock();
					sessions[j]->photo->photo_id = id_after;
					sessions[j]->photo->name = Photo_t::photo_name_with_versions(id_after, v_list.size());
					sessions[j]->photo->ids_lock.unlock();
					sessions[j]->view->update_photo_name();
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
// photo open/close
// source of a:
//	- new Photo_t object on open
//	- destruction of exist Photo_t when photo should be closed
void Edit::slot_load_photo(Photo_ID photo_id, QString photo_name, QImage thumbnail_icon) {
	if(sessions[session_active]->is_loading)
		return;
	View *view = sessions[session_active]->view;
cerr << "----Edit::slot_load_photo( \"" << photo_id.get_export_file_name() << "\", ...)" << endl;
	// start loading clock at view
//	v->photo_loading(photo_name, thumbnail_icon);
	photo_close(session_active);
	sessions[session_active]->is_loading = true;
	// block image change and reset settings
	filters_control_clear();
//	if(photo_id == "")
	if(photo_id.is_empty())
		return;
	// load settings
//	Photo_t *photo_ptr = new Photo_t;
	std::shared_ptr<Photo_t> photo(new Photo_t);
	if(sessions[session_active]->photo) {
		cerr << "FATAL: sessions[session_active]->photo != nullptr, but should be." << endl;
		throw("FATAL: sessions[session_active]->photo != nullptr");
	}
	sessions[session_active]->photo = photo;
	// actually locking can be skipped in here
	std::list<int> v_list = PS_Loader::versions_list(photo_id.get_file_name());
	photo->ids_lock.lock();
	photo->name = Photo_t::photo_name_with_versions(photo_id, v_list.size());
	photo->photo_id = photo_id;
	photo->ids_lock.unlock();

	// NOTE: actually, here we need metadata->[rotation|width|height]
	// rotation - to initialize cw_rotation;
	// width/height - for CA scaling UI;
	// but unable to gather it from files w/o exif info, like JPEG/PNG etc...
	Metadata *metadata = new Metadata;
	std::string file_name = photo_id.get_file_name();
//	bool ok = Import::load_metadata(file_name, metadata);
	Import::load_metadata(file_name, metadata);
//	Import::load_metadata(file_name, metadata);
//cerr << "load_metadata(\"" << file_name << "\") == " << ok << endl;
/*
cerr << "load_metadata(\"" << Photo_t::file_name_from_photo_id(photo_id) << "\") == " << ok << endl;
cerr << "           metadata->width == " << metadata->width << endl;
cerr << "     metadata->camera_make == " << metadata->camera_make << endl;
cerr << "metadata->exiv2_lens_model == " << metadata->exiv2_lens_model << endl;
*/	
	// UI update should be done from the _main_ thread - and here we are!
	photo_open(sessions[session_active], metadata);
	// oddly enough, but update of FS from photo_open() not works... run it here
	PS_and_FS_args_t args(metadata, photo->cw_rotation);
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it)
		(*it)->set_PS_and_FS(photo->map_ps_base[*it], photo->map_fs_base[*it], args);
	// update view
//	view->photo_open_start(photo_name, thumbnail_icon, photo);
	view->photo_open_start(thumbnail_icon, photo);
	//
	edit_history->set_current_photo(photo);
	delete metadata;

	process_runner->queue((void *)sessions[session_active], photo, view, false);
}

void Edit::photo_open(EditSession_t *session, Metadata *metadata) {
	if(session == nullptr)
		return;
	std::shared_ptr<Photo_t> photo = session->photo;
	if(!photo)
		return;
//cerr << endl;
cerr << "===============>>>>  open photo: name == \"" << photo->name.toLocal8Bit().constData() << "\"; photo id == \"" << photo->photo_id.get_export_file_name() << "\"" << endl;
//cerr << endl;
	photo->process_source = ProcessSource::s_load;

//	metadata->link_exiv2_with_lensfun();
//cerr << "lensfun lens ID: " << metadata->lensfun_lens_maker << ":" << metadata->lensfun_lens_model << endl;

	// fix that:
	//	1. create new PS_Base, store that at Photo_t
	//	2. load them
	//	3. set up filters with them
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		PS_Base *ps_base = (*it)->newPS();
		photo->map_ps_base[*it] = ps_base;
//		(*it)->setPS(ps_base);
		// don't copy empty PS_Base
//		photo->map_ps_base_current[*it] = ps_base->copy();
		//--
		FS_Base *fs_base = (*it)->newFS();
		photo->map_fs_base[*it] = fs_base;
//		(*it)->set_PS_and_FS(ps_base, fs_base, metadata);
		//--
		(*it)->set_session_id((void *)session);
	}

	PS_Loader *ps_loader = new PS_Loader(photo->photo_id);
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		PS_Base *ps_base = photo->map_ps_base[*it];
		DataSet *dataset = ps_loader->get_dataset((*it)->id());
		ps_base->load(dataset);
		// here it is !
		// TODO: replace with history
		photo->map_ps_base_current[*it] = std::shared_ptr<PS_Base>(ps_base->copy());
		photo->map_dataset_initial[*it] = *dataset;
		ps_base->save(dataset);
		photo->map_dataset_current[*it] = *dataset;
	}

	photo->cw_rotation = metadata->rotation;
	if(!ps_loader->cw_rotation_empty())
		photo->cw_rotation = ps_loader->get_cw_rotation();
	else
		ps_loader->set_cw_rotation(photo->cw_rotation);

	// PS_Base can change real values after load (like do some sort of normalization etc.), so keep that current version
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		DataSet *dataset = ps_loader->get_dataset((*it)->id());
		photo->map_ps_base[*it]->save(dataset);
	}
	photo->ps_state = ps_loader->serialize();

	delete ps_loader;
	menu_copy_paste_update();
}

void Edit::photo_close(int session_id) {
	bool flag_reset = (session_id == session_active);
//	sessions[session_active]->is_loading = false;
//	sessions[session_active]->is_loaded = false;
	sessions[session_id]->is_loading = false;
	sessions[session_id]->is_loaded = false;
	std::shared_ptr<Photo_t> photo = sessions[session_id]->photo;
	if(!photo)
		return;
	if(photo->photo_id.is_empty())
		return;
cerr << endl << "===============>>>> close photo: " << photo->photo_id.get_export_file_name() << endl << endl;
	if(flag_reset) {
		edit_history->set_current_photo(std::shared_ptr<Photo_t>());
		// leave filter edit if any
		leave_filter_edit();
	}
	bool was_changed = flush_current_ps(photo);
	// clean up caches and change 'edited icon' if necessary
	browser->photo_close(photo->photo_id, was_changed);

	// TODO: here save PS_Base from PS_State that should be created for settings copy/paste and history undo/redo !!!
	sessions[session_id]->photo.reset();
	if(flag_reset)
		menu_copy_paste_update();
//	Mem::state_print();
}

bool Edit::flush_current_ps(void) {
	bool was_changed = false;
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i] != nullptr)	{
			if(sessions[i]->photo)
				was_changed = flush_current_ps(sessions[i]->photo);
		}
	}
	return was_changed;
}

bool Edit::flush_current_ps(std::shared_ptr<Photo_t> photo) {
	bool was_changed = false;
	PS_Loader *ps_loader = get_current_ps_loader(photo);
	string ps_state_new = ps_loader->serialize();
	if(photo->ps_state != ps_state_new) {
		ps_loader->save(photo->photo_id);
		was_changed = true;
	}
	photo->ps_state = "";
	delete ps_loader;
	return was_changed;
}

PS_Loader *Edit::get_current_ps_loader(std::shared_ptr<Photo_t> photo) {
	PS_Loader *ps_loader = new PS_Loader();
	ps_loader->set_cw_rotation(photo->cw_rotation);
//cerr << "save for " << photo->photo_id.c_str() << endl;
//int c = 0;
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		DataSet *dataset = ps_loader->get_dataset((*it)->id());
		map<Filter *, PS_Base *>::iterator it_ps = photo->map_ps_base.find(*it);
		if(it_ps != photo->map_ps_base.end()) {
//c++;
			PS_Base *ps_base = (*it_ps).second;
			ps_base->save(dataset);
		}
	}
//cerr << "fields to save: " << c << endl;
	ps_loader->set_thumbnail(photo->thumbnail);
	return ps_loader;
}

//------------------------------------------------------------------------------
// filters UI
QList<QAction *> Edit::get_actions(void) {
	// keep actions to do disable/enable
	filters_actions_list = QList<QAction *>();
	// TODO: add here 'panning' action from View class (somewhere should be 'zoom' also)
	filters_actions_list.push_back(q_action_view_grid);
	filters_actions_list.push_back(nullptr);
	// rotation -90, +90
	filters_actions_list.push_back(q_action_rotate_minus);
	filters_actions_list.push_back(q_action_rotate_plus);
	// separator
	filters_actions_list.push_back(nullptr);
	for(list<pair<FilterEdit *, Filter *> >::iterator it = fstore->filter_edit_list.begin(); it != fstore->filter_edit_list.end(); ++it)
		if((*it).second->type() == Filter::t_geometry)
			filters_actions_list += (*it).first->get_actions_list();
	filters_actions_list.push_back(nullptr);
	for(list<pair<FilterEdit *, Filter *> >::iterator it = fstore->filter_edit_list.begin(); it != fstore->filter_edit_list.end(); ++it)
		if((*it).second->type() == Filter::t_color)
			filters_actions_list += (*it).first->get_actions_list();
			
	slot_controls_enable(false);
	return filters_actions_list;
}

QWidget *Edit::get_controls_widget(QWidget *parent) {
	// TODO: use arrays and lists
	if(controls_widget != nullptr)
		return controls_widget;

	controls_widget = new QWidget(parent);
	QVBoxLayout *vb = new QVBoxLayout(controls_widget);
	vb->setSpacing(0);
	vb->setContentsMargins(0, 0, 0, 0);

	QTabWidget *tab_widget = new QTabWidget();
//	tab_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
//	tab_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
//	tab_widget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
	tab_widget->setTabPosition(QTabWidget::East);
	vb->addWidget(tab_widget);

//	int pages_min_width = 256 + 24;
	int pages_min_width = 256 + 8;

//	const int pages_count = 8;
	const int pages_count = 7;
	QWidget *pages[pages_count];
	ControlsArea *controls_areas[pages_count];
//	string page_names[pages_count] = {"WB", "demosaic && CA", "geometry", "sharpness", "colors", "lightness", "out RGB", "rainbow"};
	string page_names[pages_count] = {"WB", "demosaic && CA", "geometry", "sharpness", "colors", "lightness", "rainbow"};
	std::list<QWidget *> page_widgets[pages_count];
	//--
	page_widgets[0].push_back(fstore->f_wb->controls());
#if DEBUG_RAW_COLORS
	page_widgets[1].push_back(fstore->f_process->controls());
#else
	fstore->f_process->controls();
#endif
	page_widgets[1].push_back(fstore->f_demosaic->controls());
	page_widgets[1].push_back(fstore->f_chromatic_aberration->controls());
	page_widgets[2].push_back(fstore->f_crop->controls());
	page_widgets[2].push_back(fstore->f_shift->controls());
	page_widgets[2].push_back(fstore->f_rotation->controls());
	page_widgets[2].push_back(fstore->f_distortion->controls());
	page_widgets[2].push_back(fstore->f_projection->controls());
/*
	page_widgets[2].push_back(fstore->f_shift->controls());
	page_widgets[2].push_back(fstore->f_rotation->controls());
	page_widgets[2].push_back(fstore->f_projection->controls());
	page_widgets[2].push_back(fstore->f_distortion->controls());
	page_widgets[2].push_back(fstore->f_crop->controls());
*/
	page_widgets[3].push_back(fstore->f_unsharp->controls());
	page_widgets[3].push_back(fstore->f_soften->controls());
	page_widgets[4].push_back(fstore->f_crgb_to_cm->controls());
	page_widgets[4].push_back(fstore->f_cm_colors->controls());
	page_widgets[5].push_back(fstore->f_cm_lightness->controls());
	page_widgets[6].push_back(fstore->f_cm_rainbow->controls());
	page_widgets[6].push_back(fstore->f_cm_sepia->controls());
//	page_widgets[6].push_back(fstore->f_curve->controls());
//	page_widgets[7].push_back(fstore->f_cm_rainbow->controls());

	for(int i = 0; i < pages_count; ++i) {
		pages[i] = new QWidget();
		pages[i]->setMinimumWidth(pages_min_width);
		QVBoxLayout *l = new QVBoxLayout(pages[i]);
		l->setSpacing(2);
		l->setContentsMargins(2, 2, 2, 2);
		l->setSizeConstraint(QLayout::SetMinimumSize);
		for(std::list<QWidget *>::iterator it = page_widgets[i].begin(); it != page_widgets[i].end(); ++it)
			l->addWidget(*it);
		l->addStretch();
		controls_areas[i] = new ControlsArea();
		controls_areas[i]->setWidget(pages[i]);
		tab_widget->addTab(controls_areas[i], page_names[i].c_str());
	}
	int max = pages[0]->width();
	for(int i = 0; i < pages_count; ++i)
		if(max < pages[i]->width())
			max = pages[i]->width();
	for(int i = 0; i < pages_count; ++i)
		pages[i]->setMinimumWidth(max);
	max += controls_areas[0]->verticalScrollBar()->sizeHint().width();
	for(int i = 0; i < pages_count; ++i) {
		controls_areas[i]->setMinimumWidth(max);
		controls_areas[i]->setWidgetResizable(true);
	}

	slot_controls_enable(false);
	return controls_widget;
}

void Edit::slot_controls_enable(bool state) {
//	if(controls_widget == nullptr)
//		return;
//	if(state == true)
	if(controls_widget != nullptr)
		controls_widget->setEnabled(state);
	for(QList<QAction *>::iterator it = filters_actions_list.begin(); it != filters_actions_list.end(); ++it)
		if((*it) != nullptr)
			(*it)->setEnabled(state);
}

void Edit::slot_update_filter(void *session_id, void *_filter, void *_ps_base) {
	slot_update(session_id, ProcessSource::s_none, _filter, _ps_base);
}

void Edit::slot_update(void *session_id, int process_id, void *_filter, void *_ps_base) {
	PS_Base *ps_base = (PS_Base *)_ps_base;
	Filter *filter = (Filter *)_filter;
//	int id = ProcessSource::s_view;
//cerr << "                                                                                       ++++++++ slot_update by " << process_id << endl;
	bool skip = false;
	EditSession_t *session = (EditSession_t *)session_id;
//cerr << "session_id == " << (long)session_id << endl;
	std::shared_ptr<Photo_t> photo;
	if(session_id == nullptr) {
		skip = true;
	} else {
		photo = session->photo;
		if(!photo) {
			skip = true;
		} else {
			if(photo->area_raw == nullptr)
				skip = true;
		}
	}
	if(skip) {
//		cerr << "skipped, area_raw == nullptr" << endl;
		if(ps_base != nullptr)
			delete ps_base;
		return;
	}

	// save state deltas at edit history
	if(filter != nullptr && ps_base != nullptr) {
/*
		cerr << endl;
		cerr << "update from filter " << filter->name() << endl;
*/
		DataSet dataset_new;
		ps_base->save(&dataset_new);
		DataSet *dataset_old = &photo->map_dataset_current[filter];
		if(dataset_old == nullptr)
			throw string("Fatal: missed map_dataset_current record");
		// get difference, and dump it
		list<field_delta_t> deltas = DataSet::get_fields_delta(dataset_old, &dataset_new);
		edit_history->add_eh_filter_record(eh_filter_record_t(filter, deltas));
/*
		for(list<field_delta_t>::iterator it = deltas.begin(); it != deltas.end(); ++it) {
			cerr << "delta for field \"" << (*it).field_name << "\": before == " << (*it).field_before.serialize() << "; after == " << (*it).field_after.serialize() << endl;
		}
*/
		// save delta in history
		// and update current dataset
		*dataset_old = dataset_new;
/*
		cerr << "__________________________________" << endl;
//		dataset._dump();
		cerr << endl;
*/
	}

	photo->filter_flags = 0;
	if(filter != nullptr) {
		process_id = filter->get_id();
		photo->filter_flags = filter->flags();
	}
	photo->process_source = (ProcessSource::process)process_id;
	bool is_inactive = false;
	if(session != sessions[session_active]) {
		is_inactive = true;
	}
	if(photo->process_source != ProcessSource::s_view_tiles)
		session->view->reset_deferred_tiles();
	process_runner->queue((void *)session, photo, session->view, is_inactive);
}

void Edit::history_apply(list<eh_record_t> l, bool is_undo) {
	// TODO: add real list support for 'fast' undo/redo or by clicking on history view
	// TODO: add support of cw_rotation
	std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
	eh_record_t &record = l.front();
	int filters_count = record.filter_records.size();
	PS_and_FS_args_t args(photo->metadata, photo->cw_rotation);
	ProcessSource::process s_id = ProcessSource::s_none;
	for(int i = 0; i < filters_count; ++i) {
//cerr << "filters_count == " << filters_count << endl;
		eh_filter_record_t &f_record = record.filter_records[i];
		Filter *filter = f_record.filter;
//		for(list<eh_filter_record_t>::iterator it = f_record.begin(); it != l.end(); ++it) {
//			if((*it).filter->get_id() < filter->get_id())
//				filter = (*it).filter;
//		}
//		eh_filter_record_t record = l.front();
//		filter = record.filter;
		DataSet *dataset = &photo->map_dataset_current[filter];
		dataset->apply_fields_delta(&f_record.deltas, is_undo);
		// update filters UI here
		photo->map_ps_base[filter]->load(dataset);
//		filter->setPS(photo->map_ps_base[filter]);
//		filter->load_ui(photo->map_fs_base[filter], photo->metadata);
		filter->set_PS_and_FS(photo->map_ps_base[filter], photo->map_fs_base[filter], args);
		if((ProcessSource::process)filter->get_id() < s_id)
			s_id = (ProcessSource::process)filter->get_id();
	}
	// emit signal to update
//	slot_update(sessions[session_active], (ProcessSource::process)filter->get_id(), filter, photo->map_ps_base[filter]);
//	slot_update(sessions[session_active], (ProcessSource::process)filter->get_id(), nullptr, nullptr);
	slot_update(sessions[session_active], s_id, nullptr, nullptr);
//	slot_update(sessions[session_active], ProcessSource::s_undo_redo, nullptr, nullptr);
}

Edit::EditSession_t *Edit::session_of_view(View *view) {
	EditSession_t *session = nullptr;
	for(int i = 0; i < sessions.size(); ++i) {
		if(sessions[i]->view == view) {
			session = sessions[i];
			break;
		}
	}
	return session;
}

// resize or rescale signal from View
void Edit::slot_view_update(void *ptr, int process_id) {
//cerr << "Edit::slot_view_update" << endl;
	EditSession_t *session = session_of_view((View *)ptr);
	slot_update(session, process_id, nullptr, nullptr);
//	slot_update(session, ProcessSource::s_view, nullptr);
}

void Edit::slot_view_zoom_ui_update(void) {
	view_zoom->update();
//	EditSession_t *session = session_of_view((View *)ptr);
//	slot_update(session, process_id, nullptr, nullptr);
//	slot_update(session, ProcessSource::s_view, nullptr);
}

void Edit::update_thumbnail(void *ptr, QImage thumbnail) {
	EditSession_t *session = session_of_view((View *)ptr);
	if(session != nullptr) {
		if(session->photo) {
//cerr << endl << endl;
//cerr << "update thumbnail for photo_id == \"" << session->photo->photo_id << "\"" << endl;
			emit signal_update_thumbnail(session->photo->photo_id, thumbnail);
		}
	}
}

//------------------------------------------------------------------------------
// FilterEdit proxy

void Edit::keyEvent(QKeyEvent *event) {
	View *v = sessions[session_active]->view;
	if(v != nullptr)
		v->keyEvent(event);
}

void Edit::slot_filter_edit(FilterEdit *_filter_edit, bool active, int cursor) {
	// can be here race condition ???
	if(_filter_edit != filter_edit && active)
		filter_edit->edit_mode_exit();
	if(active)
		filter_edit = _filter_edit;
	else
		filter_edit = filter_edit_dummy;
	sessions[session_active]->view->set_cursor((Cursor::cursor)cursor);
}

bool Edit::leave_filter_edit(void) {
	if(filter_edit == filter_edit_dummy)
		return false;
	filter_edit->edit_mode_exit();
	filter_edit = filter_edit_dummy;
	return true;
}

void Edit::draw(QPainter *painter, const QSize &viewport, const QRect &image, image_and_viewport_t transform) {
	FilterEdit_event_t et(nullptr);
	et.viewport = viewport;
	et.image = image;
	et.transform = transform;
	et.tracer = this;
	filter_edit->draw(painter, &et);
}

bool Edit::keyEvent(FilterEdit_event_t *et, Cursor::cursor &cursor) {
	et->tracer = this;
	return filter_edit->keyEvent(et, cursor);
}

bool Edit::mouseMoveEvent(FilterEdit_event_t *et, bool &accepted, Cursor::cursor &cursor) {
	et->tracer = this;
	return filter_edit->mouseMoveEvent(et, accepted, cursor);
}

bool Edit::mousePressEvent(FilterEdit_event_t *et, Cursor::cursor &cursor) {
	et->tracer = this;
	return filter_edit->mousePressEvent(et, cursor);
}

bool Edit::mouseReleaseEvent(FilterEdit_event_t *et, Cursor::cursor &cursor) {
	et->tracer = this;
	return filter_edit->mouseReleaseEvent(et, cursor);
}

bool Edit::enterEvent(QEvent *event) {
	return filter_edit->enterEvent(event);
}

bool Edit::leaveEvent(QEvent *event) {
	return filter_edit->leaveEvent(event);
}

//------------------------------------------------------------------------------
// Copy/Paste
void Edit::menu_copy_paste(QMenu *menu) {
	// Copy/Paste settings

	action_edit_copy = new QAction(tr("&Copy settings"), this);
	action_edit_copy->setShortcut(QKeySequence(tr("Ctrl+C")));
	action_edit_paste = new QAction(tr("&Paste settings"), this);
	action_edit_paste->setShortcut(QKeySequence(tr("Ctrl+V")));
	action_edit_fine_copy = new QAction(tr("Fine copy settings"), this);
	action_edit_fine_copy->setShortcut(QKeySequence(tr("Ctrl+Shift+C")));
	action_edit_fine_paste = new QAction(tr("Fine paste settings"), this);
	action_edit_fine_paste->setShortcut(QKeySequence(tr("Ctrl+Shift+V")));

	connect(action_edit_fine_copy, SIGNAL(triggered()), this, SLOT(slot_copy_paste_fine_copy()));
	connect(action_edit_fine_paste, SIGNAL(triggered()), this, SLOT(slot_copy_paste_fine_paste()));
	connect(action_edit_copy, SIGNAL(triggered()), this, SLOT(slot_copy_paste_copy()));
	connect(action_edit_paste, SIGNAL(triggered()), this, SLOT(slot_copy_paste_paste()));

	action_edit_copy->setDisabled(true);
	action_edit_paste->setDisabled(true);
	action_edit_fine_copy->setDisabled(true);
	action_edit_fine_paste->setDisabled(true);

	menu->addAction(action_edit_copy);
	menu->addAction(action_edit_paste);
	menu->addAction(action_edit_fine_copy);
	menu->addAction(action_edit_fine_paste);
}

// should be called after photo open/close, view switch, and settings copy (to enable paste first time)
void Edit::menu_copy_paste_update(void) {
	bool empty = true;
	if(sessions.size() != 0 && session_active >= 0) {
		if(sessions[session_active] != nullptr)
			if(sessions[session_active]->photo)
				empty = false;
	}
	if(empty) {
		action_edit_copy->setDisabled(true);
		action_edit_fine_copy->setDisabled(true);
	} else {
		action_edit_copy->setDisabled(false);
		action_edit_fine_copy->setDisabled(false);
	}
	if(copy_paste_map_dataset.size() == 0 || empty) {
		action_edit_paste->setDisabled(true);
		action_edit_fine_paste->setDisabled(true);
	} else if(empty == false) {
		action_edit_paste->setDisabled(false);
		action_edit_fine_paste->setDisabled(false);
	}
}

void Edit::slot_copy_paste_copy(void) {
	do_copy_paste(true);
}

void Edit::slot_copy_paste_paste(void) {
	do_copy_paste(false);
}

void Edit::slot_copy_paste_fine_copy(void) {
	do_copy_paste_fine(true);
}

void Edit::slot_copy_paste_fine_paste(void) {
	do_copy_paste_fine(false);
}

void Edit::do_copy_paste_fine(bool to_copy) {
	copy_paste_dialog = new Copy_Paste_Dialog(to_copy, &copy_paste_filters_to_skip);
	bool accepted = copy_paste_dialog->exec();
	delete copy_paste_dialog;
	if(!accepted)
		return;
	bool skip = true;
	if(sessions.size() != 0 && session_active >= 0) {
		if(sessions[session_active] != nullptr)
			if(sessions[session_active]->photo)
				skip = false;
	}
	if(skip)
		return;
	do_copy_paste(to_copy);
//	cerr << "process copy/paste (is copy == " << to_copy << ") action" << endl;
}

void Edit::do_copy_paste(bool to_copy) {
	if(to_copy)
		do_copy();
	else
		do_paste();
}

void Edit::do_copy(void) {
	std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
	copy_paste_map_dataset.erase(copy_paste_map_dataset.begin(), copy_paste_map_dataset.end());
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		// skip copy
		if(copy_paste_filters_to_skip.find((*it)->id()) != copy_paste_filters_to_skip.end())
			continue;
		// do copy
		// TODO: add copy from opened photo _or_ from closed file via browser
		PS_Base *ps_base = photo->map_ps_base[*it];
		DataSet dataset;
		ps_base->save(&dataset);
		copy_paste_map_dataset.insert(pair<Filter *, DataSet>((*it), dataset));
	}
	// enable paste menu
	menu_copy_paste_update();
	// TODO: add history (undo/redo) support
}

void Edit::do_paste(void) {
	std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
	Filter *process_filter = nullptr;
	QVector<eh_filter_record_t> filter_records;
	QList<Filter *> filters_list = fstore->get_filters_list();
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		Filter *filter = *it;
		// skip copy
		if(copy_paste_filters_to_skip.find(filter->id()) != copy_paste_filters_to_skip.end())
			continue;
		// do paste
		// TODO: add paste to opened photo _or_ to closed file via browser
		map<Filter *, DataSet>::iterator it_d = copy_paste_map_dataset.find(filter);
		PS_and_FS_args_t args(photo->metadata, photo->cw_rotation);
		if(it_d != copy_paste_map_dataset.end()) {
			// compare current and settings to paste
			DataSet dataset_current;
			photo->map_ps_base[filter]->save(&dataset_current);
			if(dataset_current.serialize() == (*it_d).second.serialize())
				continue;
			// get deltas and save to edit history
			list<field_delta_t> deltas = DataSet::get_fields_delta(&dataset_current, &(*it_d).second);
			filter_records.push_back(eh_filter_record_t(filter, deltas));
			// update settings
			photo->map_dataset_current[filter] = (*it_d).second;
			photo->map_ps_base[filter]->load(&((*it_d).second));
//			filter->setPS(photo->map_ps_base[filter]);
//			filter->load_ui(photo->map_fs_base[filter], photo->metadata);
			filter->set_PS_and_FS(photo->map_ps_base[filter], photo->map_fs_base[filter], args);
			// detect first in process line filter
			if(process_filter != nullptr) {
				if(filter->get_id() < process_filter->get_id())
					process_filter = filter;
			} else
				process_filter = filter;
		}
	}
	// copy set of records to edit history
	if(filter_records.size() != 0)
		edit_history->add_eh_filter_records(filter_records);
	slot_update(sessions[session_active], ProcessSource::s_copy_paste, process_filter, nullptr);
	// TODO: add history (undo/redo) support
}

//------------------------------------------------------------------------------
class Area *Edit::viewport_to_filter(class Area *viewport_coords, std::string filter_id) {
	return new Area(*viewport_coords);
}

class Area *Edit::filter_to_viewport(class Area *filter_coords, std::string filter_id) {
	return new Area(*filter_coords);
}
//------------------------------------------------------------------------------
// NOTE: store list of filters to skip on copy/paste, but return IDs of allowed filters
//
Copy_Paste_Dialog::Copy_Paste_Dialog(bool to_copy, std::set<std::string> *_copy_paste_set, QWidget *parent) : QDialog(parent) {
	copy_paste_set = _copy_paste_set;
//	QString _title = tr("filters to copy");
	QString _title = tr("Copy");
	QString _groupbox = tr("Select filters to copy");
	if(!to_copy) {
//		_title = tr("filters to paste");
		_title = tr("Paste");
		_groupbox = tr("Select filters to paste");
	}
	setWindowTitle(_title);
//	setModal(true);
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

	QVBoxLayout *layout_main = new QVBoxLayout(this);
	QGroupBox *gb_filters = new QGroupBox(_groupbox);
	QGridLayout *gl = new QGridLayout(gb_filters);

//	std::list<class Filter *> &filters = Filter_Store::_this->filters;
	QList<class Filter *> filters_list = Filter_Store::_this->get_filters_list();
	int i = 0;
	for(QList<Filter *>::iterator it = filters_list.begin(); it != filters_list.end(); ++it) {
		Filter *f = *it;
		if(f->is_hidden())
			continue;
		string id = f->id();
		QCheckBox *check_box = new QCheckBox(f->name());
		flags.push_back(QPair<QCheckBox *, string>(check_box, id));
		bool do_copy = (copy_paste_set->find(id) == copy_paste_set->end());
		check_box->setCheckState(do_copy ? Qt::Checked : Qt::Unchecked);
		gl->addWidget(check_box, i, 0);
		i++;
	}
	layout_main->addWidget(gb_filters);
	//-------------------------------
	// buttons
	QHBoxLayout *hb_buttons = new QHBoxLayout();
	QPushButton *button_ok = new QPushButton(tr("Ok"));
	QPushButton *button_cancel = new QPushButton(tr("Cancel"));
	hb_buttons->addStretch();
	hb_buttons->addWidget(button_ok);
	hb_buttons->addWidget(button_cancel);
	layout_main->addLayout(hb_buttons);
	button_ok->setDefault(true);

	connect(button_ok, SIGNAL(pressed(void)), this, SLOT(slot_button_ok(void)));
	connect(button_cancel, SIGNAL(pressed(void)), this, SLOT(reject(void)));

	setFixedSize(sizeHint());
}

Copy_Paste_Dialog::~Copy_Paste_Dialog(void) {
	for(int i = 0; i < flags.size(); ++i)
		if(flags[i].first != nullptr)
			delete flags[i].first;
}

void Copy_Paste_Dialog::slot_button_ok(void) {
	for(int i = 0; i < flags.size(); ++i) {
		std::string id = flags[i].second;
		bool skip = (flags[i].first->checkState() == Qt::Unchecked);
		if(skip)
			copy_paste_set->insert(id);
		else
			copy_paste_set->erase(id);
	}
//	cerr << "slot_button_ok" << endl;
	emit accept();
}
//------------------------------------------------------------------------------
