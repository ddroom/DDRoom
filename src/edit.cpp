/*
 * edit.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
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
#include <iomanip>

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

#include "f_demosaic.h"
#include "f_chromatic_aberration.h"
#include "f_vignetting.h"
//#include "f_distortion.h"
#include "f_projection.h"
#include "f_shift.h"
#include "f_rotation.h"
#include "f_crop.h"
#include "f_soften.h"
#include "f_wb.h"
#include "f_crgb_to_cm.h"
#include "f_cm_lightness.h"
#include "f_cm_rainbow.h"
#include "f_cm_sepia.h"
#include "f_cm_colors.h"
#include "f_unsharp.h"
#include "f_cm_to_cs.h"

#include <QDialog>

using namespace std;

//------------------------------------------------------------------------------
class Process_Runner {

public:
	Process_Runner(class Process *);
	virtual ~Process_Runner();
	void queue(void *ptr, std::shared_ptr<Photo_t>, class TilesReceiver *tiles_receiver, bool is_inactive);

protected:
	class Process *const process;

	class task_t;
	void run(void);
	std::mutex tasks_lock;
	std::list<std::unique_ptr<task_t>> tasks;
	std::condition_variable cv_tasks;
	std::thread *tasks_thread = nullptr;
	bool tasks_abort = false;
	std::atomic_int tasks_semaphore;

	static void run_task(Process *process, task_t *task);
};

class Process_Runner::task_t {
public:
	~task_t();
	void *ptr;
	std::shared_ptr<Photo_t> photo;
	int request_ID;
	class TilesReceiver *tiles_receiver;
	bool is_inactive;
	std::map<class Filter *, std::shared_ptr<PS_Base>> map_ps_base;

	bool in_progress = false;
	bool is_complete = false;
	// results
	bool success = false;
	std::mutex *lock = nullptr;
	std::condition_variable *cv = nullptr;
	std::thread *_thread = nullptr;
	std::atomic_int *semaphore = nullptr;
};

Process_Runner::task_t::~task_t() {
	if(_thread) {
		_thread->join();
		delete _thread;
		_thread = nullptr;
	}
}

//------------------------------------------------------------------------------
Process_Runner::Process_Runner(Process *_process) : process(_process) {
	tasks_abort = false;
	tasks_semaphore.store(0);
	tasks_thread = new std::thread( [=](void){ run(); } );
}

Process_Runner::~Process_Runner() {
	if(tasks_thread != nullptr) {
		std::unique_lock<std::mutex> lock(tasks_lock);
		tasks_abort = true;
		lock.unlock();
		cv_tasks.notify_all();
		tasks_thread->join();
		delete tasks_thread;
	}
	for(auto it = tasks.begin(); it != tasks.end(); ++it)
		if((*it)->_thread != nullptr) {
			(*it)->_thread->join();
			delete (*it)->_thread;
			(*it)->_thread = nullptr;
		}
}

void Process_Runner::queue(void *ptr, std::shared_ptr<Photo_t> photo, TilesReceiver *tiles_receiver, bool is_inactive) {
	int request_ID = Process::newID();
	int request_ID_to_abort = 0;
	if(photo->process_source == ProcessSource::s_view_tiles)
		request_ID_to_abort = tiles_receiver->add_request_ID(request_ID);
	else
		request_ID_to_abort = tiles_receiver->set_request_ID(request_ID);
	Process::ID_request_abort(request_ID_to_abort);

	auto new_task = std::unique_ptr<task_t>(new task_t);
	new_task->ptr = ptr;
	new_task->photo = photo;
	new_task->request_ID = request_ID;
	new_task->tiles_receiver = tiles_receiver;
	new_task->is_inactive = is_inactive;
	for(auto it = photo->map_dataset.begin(); it != photo->map_dataset.end(); ++it) {
		Filter *filter = (*it).first;
		new_task->map_ps_base[filter] = std::shared_ptr<PS_Base>(filter->newPS());
		new_task->map_ps_base[filter]->load(&photo->map_dataset[filter]);
	}
	new_task->in_progress = false;
	new_task->lock = &tasks_lock;
	new_task->cv = &cv_tasks;
	new_task->semaphore = &tasks_semaphore;

	// disable other records for the same tiles_receiver, as deprecated now
	tasks_lock.lock();
#if 0
	bool in_progress = false;
	for(auto it = tasks.begin(); it != tasks.end(); ++it) {
		if(*it != nullptr) {
			if((*it)->in_progress == false) {
				if((*it)->photo.get() == photo.get() || (*it)->tiles_receiver == tiles_receiver)
					(*it).reset(nullptr);
			} else {
				if((*it)->photo.get() == photo.get())
					in_progress = true;
			}
		}
	}
	if(in_progress)
		tasks.push_back(std::move(new_task));
	else
		tasks.push_front(std::move(new_task));
#else
	std::list<std::unique_ptr<task_t>> tasks_new;
	for(auto it = tasks.begin(); it != tasks.end(); ++it) {
		bool keep = false;
		if((*it)->in_progress == true)
			keep = true;
		else
			if((*it)->photo.get() != photo.get() && (*it)->tiles_receiver != tiles_receiver)
				keep = true;
		if(keep) {
			tasks_new.push_back(std::unique_ptr<task_t>());
			tasks_new.back().swap(*it);
		}
	}
	tasks_new.push_back(std::move(new_task));
	tasks.swap(tasks_new);
	tasks_semaphore.fetch_add(1);
#endif
	tasks_lock.unlock();
	cv_tasks.notify_all();
//cerr << "queue " << request_ID << ", ... 4" << endl;
//cerr << "wake up all; request_ID == " << request_ID << "; to_abort == " << request_ID_to_abort << endl;
}

void Process_Runner::run(void) {
	while(true) {
		std::unique_lock<std::mutex> locker(tasks_lock);
		if((tasks.empty() == true || tasks_semaphore.load() == 0) && tasks_abort == false)
			cv_tasks.wait(locker, [this]{ return ((tasks.empty() == false && tasks_semaphore.load() != 0) || tasks_abort == true); });
		if(tasks_abort)
			return;

		tasks_semaphore.store(0);
		std::set<Photo_t *> in_progress;
		auto it = tasks.begin();
//cerr << endl << "...1" << endl;
		while(it != tasks.end()) {
			auto task = (*it).get();
			if(task->is_complete) {
//cerr << "__ remove completed task: " << (unsigned long)task << " -> " << (unsigned long)task->photo.get() << endl;
				// TODO: check 'task->success', process OOM case
				tasks.erase(it);
				it = tasks.begin();
				continue;
			}
			if(task->in_progress == true) {
//cerr << "__ task just in progress: " << (unsigned long)task << " -> " << (unsigned long)task->photo.get() << endl;
				in_progress.insert(task->photo.get());
			} else {
				if(in_progress.find(task->photo.get()) == in_progress.end()) {
//cerr << "__ -- run  task: " << (unsigned long)task << " -> " << (unsigned long)task->photo.get() << endl;
					task->in_progress = true;
					task->_thread = new std::thread( [=]{Process_Runner::run_task(process, task);} );
				} else {
//cerr << "__ .. skip task: " << (unsigned long)task << " -> " << (unsigned long)task->photo.get() << endl;
				}
			}
			++it;
//cerr << "...2" << endl;
		}
//cerr << endl;
	}
}

void Process_Runner::run_task(Process *process, task_t *task) {
//cerr << "task == " << (unsigned long)task << " -> " << (unsigned long)task->photo.get() << endl;
	bool success = false;
	try {
		success = process->process_edit(task->ptr, task->photo, task->request_ID, task->tiles_receiver, task->map_ps_base);
	} catch(...) {
		// should never happen ?
		terminate();
	}
	task->lock->lock();
	task->success = success;
	task->is_complete = true;
	task->semaphore->fetch_add(1);
	std::condition_variable *cv = task->cv;
	task->lock->unlock();
	cv->notify_all();
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
	this->fstore = Filter_Store::instance();
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
	const std::vector<Filter *> &filters = fstore->get_filters();
	for(Filter *f : filters)
		connect(f, SIGNAL(signal_update(void *, void *, void *)), this, SLOT(slot_update_filter(void *, void *, void *)));
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

	delete process_runner;

	for(size_t i = 0; i < sessions.size(); ++i) {
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

void Edit::slot_process_complete(void *ptr, class PhotoProcessed_t *photo_processed_ptr) {
	EditSession_t *session = (EditSession_t *)ptr;
	std::unique_ptr<PhotoProcessed_t> photo_processed(photo_processed_ptr);
	if(!session->photo) {
		session->is_loading = false;
		return;
	}
	Photo_ID photo_id = session->photo->photo_id;
	bool is_loaded = true;
	if(photo_processed->is_empty == false) {
		if(session->view->is_active())
			slot_controls_enable(true);
//		emit signal_controls_enable(true);
//		cerr << "Edit::slot_process_complete(); photo != nullptr" << endl;
	} else {
		// TODO: signal import failure somehow
		// photo object was destroyed at Process class
		is_loaded = false;
		session->photo.reset();
		if(session->view->is_active())
			slot_controls_enable(false);
//		emit signal_controls_enable(false);
//		cerr << "Edit::slot_process_done(); photo == nullptr" << endl;
	}
	browser->photo_loaded(photo_id, is_loaded);
	session->view->photo_open_finish(photo_processed.get());
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
	for(auto el : fstore->filter_edit_list)
		el.first->set_cw_rotation(cw_rotation);
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
		std::vector<Filter *> filters = fstore->get_filters();
		if(photo_prev) {
			// store current filter's state
//cerr << "Edit::slot_view_active(), photo_prev->photo_id == \"" << photo_prev->photo_id << "\"" << endl;
			for(Filter *f : filters)
				f->saveFS(photo_prev->map_fs_base[f]);
//			filters_control_clear();
		}
		if(photo) {
//cerr << "Edit::slot_view_active(), photo_id == \"" << photo->photo_id << "\"" << endl;
			PS_and_FS_args_t args(photo->metadata, photo->cw_rotation);
			for(Filter *f : filters) {
				// store current filter's state
//				if(photo_prev)
//					f->saveFS(photo_prev->map_fs_base[f]);
				// switch to the newly active session
				f->set_session_id((void *)sessions[session_active]);
				f->set_PS_and_FS(photo->map_ps_base[f], photo->map_fs_base[f], args);
			}
			slot_controls_enable(sessions[session_active]->is_loaded);
//			emit signal_controls_enable(photo->is_loaded);
//			emit signal_controls_enable(true);
		} else {
			// disable and reset filter's controls
			if(photo_prev)
				filters_control_clear();
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
	std::vector<Filter *> filters = fstore->get_filters();
	for(Filter *f : filters) {
		f->set_session_id(nullptr);
//		f->setPS(nullptr);
//cerr << "filter: " << f->name() << endl;
//		f->load_ui(nullptr, nullptr);
		f->set_PS_and_FS(nullptr, nullptr, PS_and_FS_args_t());
	}
}

//------------------------------------------------------------------------------
void Edit::slot_update_opened_photo_ids(QList<Photo_ID> ids_list, int versions_count) {
	size_t c = ids_list.size() / 2;
	for(size_t i = 0; i < c; ++i) {
		Photo_ID id_before = ids_list.at(i * 2 + 0);
		Photo_ID id_after = ids_list.at(i * 2 + 1);
#if 0
cerr << "id_before == " << id_before.get_export_file_name() << endl;
cerr << " id_after == " << id_after.get_export_file_name() << endl;
cerr << "id_before == " << id_before.get_version_index() << endl;
cerr << " id_after == " << id_after.get_version_index() << endl;
cerr << "versions_count == " << versions_count << endl;
#endif
		for(int j = 0; j < 4; ++j) {
			if(sessions[j]->photo) {
				if(sessions[j]->photo->photo_id == id_before) {
					sessions[j]->photo->ids_lock.lock();
					sessions[j]->photo->photo_id = id_after;
//cerr << "id_after.index == " << id_after.get_version_index() << endl;
					sessions[j]->photo->name = Photo_t::photo_name_with_versions(id_after, versions_count);
//cerr << "set name to: \"" << sessions[j]->photo->name.toStdString().c_str() << "\"" << endl;
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
	std::vector<Filter *> filters = fstore->get_filters();
	for(Filter *f : filters)
		f->set_PS_and_FS(photo->map_ps_base[f], photo->map_fs_base[f], args);
	// update view
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
cerr << "===============>>>>  open photo: name == \"" << photo->name.toStdString() << "\"; photo id == \"" << photo->photo_id.get_export_file_name() << "\"" << endl;
	photo->process_source = ProcessSource::s_load;

//	metadata->link_exiv2_with_lensfun();
//cerr << "lensfun lens ID: " << metadata->lensfun_lens_maker << ":" << metadata->lensfun_lens_model << endl;

	PS_Loader *ps_loader = new PS_Loader(photo->photo_id);
	std::vector<Filter *> filters = fstore->get_filters();
	for(Filter *filter : filters) {
		PS_Base *ps_base = filter->newPS();
		photo->map_ps_base[filter] = ps_base;

		DataSet *dataset = ps_loader->get_dataset(filter->id());
		ps_base->load(dataset);
		// could be some normalizations at the 'load' point
		ps_base->save(dataset);
		photo->map_dataset[filter] = *dataset;
//		filter->setPS(ps_base);

		FS_Base *fs_base = filter->newFS();
		photo->map_fs_base[filter] = fs_base;
//		filter->set_PS_and_FS(ps_base, fs_base, metadata);

		filter->set_session_id((void *)session);
	}

	photo->cw_rotation = metadata->rotation;
	if(!ps_loader->cw_rotation_empty())
		photo->cw_rotation = ps_loader->get_cw_rotation();
	else
		ps_loader->set_cw_rotation(photo->cw_rotation);

/*
	// PS_Base can change real values after load (like do some sort of normalization etc.), so keep that current version
	for(Filter *f : filters) {
		DataSet *dataset = ps_loader->get_dataset(f->id());
		photo->map_ps_base[f]->save(dataset);
	}
*/
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
	std::vector<Filter *> filters = fstore->get_filters();
	for(Filter *f : filters) {
		DataSet *dataset = ps_loader->get_dataset(f->id());
		map<Filter *, PS_Base *>::iterator it_ps = photo->map_ps_base.find(f);
		if(it_ps != photo->map_ps_base.end())
			(*it_ps).second->save(dataset);
	}
//cerr << "fields to save: " << c << endl;
	ps_loader->set_thumbnail(photo->thumbnail.get());
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
//	for(list<pair<FilterEdit *, Filter *> >::iterator it = fstore->filter_edit_list.begin(); it != fstore->filter_edit_list.end(); ++it)
	for(auto el : fstore->filter_edit_list)
		if(el.second->type() == Filter::t_geometry)
			filters_actions_list += el.first->get_actions_list();
	filters_actions_list.push_back(nullptr);
	for(auto el : fstore->filter_edit_list)
		if(el.second->type() == Filter::t_color)
			filters_actions_list += el.first->get_actions_list();
			
	slot_controls_enable(false);
	return filters_actions_list;
}

QWidget *Edit::get_controls_widget(QWidget *parent) {
	// TODO: use arrays and lists
	if(controls_widget != nullptr)
		return controls_widget;

	QTabWidget *tab_widget = new QTabWidget(parent);
	controls_widget = tab_widget;
//	tab_widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
//	tab_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
//	tab_widget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
	tab_widget->setTabPosition(QTabWidget::East);

//	int pages_min_width = 256 + 24;
	int pages_min_width = 256 + 8;

//	const int pages_count = 8;
	const int pages_count = 7;
	ControlsArea *controls_areas[pages_count];
//	string page_names[pages_count] = {"WB", "demosaic && CA", "geometry", "sharpness", "colors", "lightness", "rainbow"};
	string page_names[pages_count] = {"WB", "demosaic && lens", "geometry", "sharpness", "colors", "lightness", "rainbow"};
	std::list<QWidget *> page_widgets[pages_count];
	//--
	page_widgets[0].push_back(fstore->f_wb->controls());
	page_widgets[1].push_back(fstore->f_demosaic->controls());
	page_widgets[1].push_back(fstore->f_chromatic_aberration->controls());
	page_widgets[1].push_back(fstore->f_vignetting->controls());
//	page_widgets[1].push_back(fstore->f_distortion->controls());
	page_widgets[2].push_back(fstore->f_crop->controls());
	page_widgets[2].push_back(fstore->f_shift->controls());
	page_widgets[2].push_back(fstore->f_rotation->controls());
	page_widgets[2].push_back(fstore->f_projection->controls());
	page_widgets[3].push_back(fstore->f_unsharp->controls());
	page_widgets[3].push_back(fstore->f_soften->controls());
	page_widgets[4].push_back(fstore->f_crgb_to_cm->controls());
	page_widgets[4].push_back(fstore->f_cm_colors->controls());
	page_widgets[5].push_back(fstore->f_cm_lightness->controls());
	page_widgets[6].push_back(fstore->f_cm_rainbow->controls());
	page_widgets[6].push_back(fstore->f_cm_sepia->controls());

	for(int i = 0; i < pages_count; ++i) {
		QWidget *page = new QWidget();
		page->setMinimumWidth(pages_min_width);
		QVBoxLayout *l = new QVBoxLayout(page);
		l->setSpacing(2);
		l->setContentsMargins(2, 2, 2, 2);
		l->setSizeConstraint(QLayout::SetMinimumSize);
		for(std::list<QWidget *>::iterator it = page_widgets[i].begin(); it != page_widgets[i].end(); ++it)
			l->addWidget(*it);
		l->addStretch();
		controls_areas[i] = new ControlsArea();
		controls_areas[i]->setWidget(page);
		tab_widget->addTab(controls_areas[i], page_names[i].c_str());
		filters_pages.push_back(page);
	}
	int max = filters_pages[0]->width();
	for(int i = 0; i < pages_count; ++i)
		if(max < filters_pages[i]->width())
			max = filters_pages[i]->width();
	for(int i = 0; i < pages_count; ++i)
		filters_pages[i]->setMinimumWidth(max);
	max += controls_areas[0]->verticalScrollBar()->sizeHint().width();
	for(int i = 0; i < pages_count; ++i) {
		controls_areas[i]->setMinimumWidth(max);
		controls_areas[i]->setWidgetResizable(true);
	}

	slot_controls_enable(false);
	return controls_widget;
}

void Edit::slot_controls_enable(bool state) {
	for(size_t i = 0; i < filters_pages.size(); ++i)
		filters_pages[i]->setEnabled(state);
	for(QAction *action : filters_actions_list)
		if(action != nullptr)
			action->setEnabled(state);
}

void Edit::slot_update_filter(void *session_id, void *_filter, void *_ps_base) {
	Filter *filter = (Filter *)_filter;
	int process_id = filter ? (ProcessSource::process)filter->get_id() : ProcessSource::s_none;
	slot_update(session_id, process_id, _filter, _ps_base);
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

	// save photo settings change as dataset deltas at edit history
	if(filter != nullptr && ps_base != nullptr) {
		DataSet dataset_new;
		ps_base->save(&dataset_new);
		auto it = photo->map_dataset.find(filter);
		if(it == photo->map_dataset.end())
			throw string("Fatal: missed map_dataset record");
		DataSet *dataset_old = &((*it).second);
/*
		DataSet *dataset_old = &photo->map_dataset[filter];
		if(dataset_old == nullptr)
			throw string("Fatal: missed map_dataset record");
*/
		// save difference
		list<field_delta_t> deltas = DataSet::get_fields_delta(dataset_old, &dataset_new);
		edit_history->add_eh_filter_record(eh_filter_record_t(filter, deltas));
		*dataset_old = dataset_new;
//		dataset._dump();
	}

	photo->process_source = (ProcessSource::process)process_id;
	bool is_inactive = false;
	if(session != sessions[session_active]) {
		is_inactive = true;
	}
	if(photo->process_source != ProcessSource::s_view_tiles)
		session->view->reset_deferred_tiles();
//cerr << "           process_runner->queue                                                ++++++++ slot_update by " << process_id << endl;
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
		DataSet *dataset = &photo->map_dataset[filter];
		dataset->apply_fields_delta(&f_record.deltas, is_undo);
		// update filters UI here
		photo->map_ps_base[filter]->load(dataset);
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
	for(size_t i = 0; i < sessions.size(); ++i) {
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
	std::vector<Filter *> filters = fstore->get_filters();
	for(auto filter : filters) {
		// skip copy
		if(copy_paste_filters_to_skip.find(filter->id()) != copy_paste_filters_to_skip.end())
			continue;
		// do copy
		// TODO: add copy from opened photo _or_ from closed file via browser
		PS_Base *ps_base = photo->map_ps_base[filter];
		DataSet dataset;
		ps_base->save(&dataset);
		copy_paste_map_dataset.insert(pair<Filter *, DataSet>(filter, dataset));
	}
	// enable paste menu
	menu_copy_paste_update();
	// TODO: add history (undo/redo) support
}

void Edit::do_paste(void) {
	std::shared_ptr<Photo_t> photo = sessions[session_active]->photo;
	Filter *process_filter = nullptr;
	QVector<eh_filter_record_t> filter_records;
	std::vector<Filter *> filters = fstore->get_filters();
	for(Filter *filter : filters) {
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
			photo->map_dataset[filter] = (*it_d).second;
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

	std::vector<class Filter *> filters = Filter_Store::instance()->get_filters();
	int i = 0;
	for(Filter *f : filters) {
		if(f->is_hidden())
			continue;
		string id = f->id();
		QCheckBox *check_box = new QCheckBox(f->name());
		flags.push_back(QPair<QCheckBox *, string>(check_box, id));
		bool do_copy = (copy_paste_set->find(id) == copy_paste_set->end());
		check_box->setCheckState(do_copy ? Qt::Checked : Qt::Unchecked);
		gl->addWidget(check_box, i, 0);
		++i;
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
