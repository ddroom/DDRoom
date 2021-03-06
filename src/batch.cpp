/*
 * batch.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

/*
   TODO: change GUI update scheme:
    - create an object of photos in task with more detailed info about photos (as icons etc);
    - use that object as mediator between (export thread) and (batch GUI - extended and simple);
    - add signals from Process::process to update partial status of photo processing;
*/

#include <iostream>

#include "batch.h"
#include "batch_dialog.h"
#include "browser.h"
#include "config.h"
#include "process_h.h"
#include "edit.h"
#include "system.h"
#include "widgets.h"

#include "export.h"

using namespace std;

//------------------------------------------------------------------------------
Batch::Batch(QWidget *parent, class Process *_process, class Edit *_edit, class Browser *_browser) {
	process = _process;
	edit = _edit;
	browser = _browser;
	window = parent;

	connect(edit, SIGNAL(signal_active_photo_changed(void)), this, SLOT(slot_active_photo_changed(void)));
	connect(browser, SIGNAL(signal_selection_changed(int)), this, SLOT(slot_selection_changed(int)));
	selected_photos_count = 0;
	
//	was_run = false;
	to_leave = false;
	to_pause = false;
	c_done = 0;
	c_total = 0;
	widget = nullptr;
	// setup destination dir
	bool flag = Config::instance()->get(CONFIG_SECTION_BATCH, "destination_dir", destination_dir);
	if(flag)
		flag = QDir(QString::fromStdString(destination_dir)).exists();
	if(!flag)
		destination_dir = System::env_home();
}

Batch::~Batch() {
	// save destination dir
	Config::instance()->set(CONFIG_SECTION_BATCH, "destination_dir", destination_dir);
	if(run_thread != nullptr) {
		to_leave = true;
		task_wait.notify_all();
		run_thread->join();
		delete run_thread;
	}
}

void Batch::start(void) {
//	auto ptr = this;
//	run_thread = new std::thread( [ptr](void){ptr->run();} );
	run_thread = new std::thread( [=]{run();} );
}

//------------------------------------------------------------------------------
void Batch::fill_menu(QMenu *menu) {
	action_save_as = new QAction(tr("Save Photo As..."), this);
	action_save_as->setShortcut(tr("Ctrl+S"));
	action_save_as->setEnabled(false);
	connect(action_save_as, SIGNAL(triggered()), this, SLOT(menu_save_as()));

	action_batch = new QAction(tr("Batch process"), this);
	action_batch->setEnabled(false);
	connect(action_batch, SIGNAL(triggered()), this, SLOT(menu_batch()));

	menu->addAction(action_save_as);
	menu->addAction(action_batch);
}

void Batch::slot_export(void) {
	std::list<Photo_ID> selected_list = browser->selected_photos_list();
	if(selected_list.size() == 1) {
		Photo_ID active_photo = selected_list.front();
		process_save_as(active_photo);
	} else {
		if(selected_list.size() > 1)
			process_batch(selected_list);
	}
}

void Batch::menu_save_as(void) {
	// save currently active edit photo
	// get that photo from Edit class
	// this one should run new thread...
	Photo_ID active_photo = edit->active_photo();
	if(active_photo.is_empty()) {
		list<Photo_ID> selected_list = browser->selected_photos_list();
		if(selected_list.size() == 1)
			active_photo = selected_list.front();
	}
	process_save_as(active_photo);
}

void Batch::menu_batch(void) {
	// send to batch processing selected in browser photos
	list<Photo_ID> selected_list = browser->selected_photos_list();
	// this one should run new thread...
	process_batch(selected_list);
}

void Batch::slot_selection_changed(int count) {
	selected_photos_count = count;
	bool active_photo = (!edit->active_photo().is_empty());
	if(!active_photo)
		action_save_as->setEnabled(count == 1);
	action_batch->setEnabled(count > 0);
}

void Batch::slot_active_photo_changed(void) {
	if(!edit->active_photo().is_empty())
		action_save_as->setEnabled(true);
	else {
		action_save_as->setEnabled(selected_photos_count == 1);
	}
}

//------------------------------------------------------------------------------
// status GUI
void Batch::do_pause(void) {
	if(run_thread == nullptr)
		return;
	to_pause = true;
}

void Batch::do_continue(void) {
	if(run_thread == nullptr)
		return;
	task_list_lock.lock();
	if(task_list.begin() != task_list.end())
		task_wait.notify_all();
	task_list_lock.unlock();
}

void Batch::do_abort(void) {
	task_list_lock.lock();
	task_list.erase(task_list.begin(), task_list.end());
	// to update status if was on the pause
	task_wait.notify_all();
	task_list_lock.unlock();
}

QWidget *Batch::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	widget = new QWidget(parent);
	QHBoxLayout *h = new QHBoxLayout(widget);
	h->setSpacing(4);
	h->setContentsMargins(4, 2, 4, 2);

	QLabel *l = new QLabel(widget);
	l->setText(tr("Processing:"));
	h->addWidget(l);

	progress_bar = new QProgressBar(widget);
#ifndef Q_OS_MAC
	progress_bar->setFormat("%v / %m");
#endif
	h->addWidget(progress_bar);

#ifdef Q_OS_MAC
	progress_label = new QLabel(widget);
	progress_label->setText(tr(" 1 / 1 "));
	h->addWidget(progress_label);
#endif

	button_pause = new QToolButton(widget);
#ifdef Q_OS_MAC
	button_pause->setIconSize(QSize(button_pause->iconSize().width() / 2, button_pause->iconSize().height() / 2));
#endif
	icons_create(widget);
	button_pause_as_pause();
	h->addWidget(button_pause);

	button_abort = new QToolButton(widget);
#ifdef Q_OS_MAC
	button_abort->setIconSize(button_pause->iconSize());
#endif
	button_abort->setIcon(icon_abort);
	button_abort->setToolTip(tr("Abort batch processing"));
	h->addWidget(button_abort);

	widget->hide();

	connect(this, SIGNAL(signal_status(long, long, bool)), this, SLOT(slot_status(long, long, bool)));
	connect(button_pause, SIGNAL(clicked(bool)), this, SLOT(slot_pause(bool)));
	connect(button_abort, SIGNAL(clicked(bool)), this, SLOT(slot_abort(bool)));

	return widget;
}

void Batch::icons_create(QWidget *qwidget) {
	QColor color = qwidget->style()->standardPalette().color(QPalette::ButtonText);
//	QColor color = QColor(255, 63, 63, 255);
	QSize size = button_pause->iconSize();
	int _w = size.width();
	int _h = size.height();
	int x_off = _w / 8;
	int y_off = _h / 8;
	int x_3 = (_w - x_off * 2) / 3;
	int y = _h - y_off * 2;
	QImage paper = QImage(size, QImage::Format_ARGB32);
	paper.fill(QColor(255, 255, 255, 0).rgba());

	// 'pause'
	QPainter *painter = new QPainter(&paper);
	painter->fillRect(x_off, y_off, x_3, y, color);
	painter->fillRect(_w - x_off - x_3, y_off, x_3, y, color);
	delete painter;
	icon_pause = QPixmap::fromImage(paper);
	
	// 'continue' (looks as triangle 'play')
	paper.fill(QColor(255, 255, 255, 0).rgba());
	painter = new QPainter(&paper);
	painter->setRenderHint(QPainter::Antialiasing, true);
	QPointF points[3];
	points[0] = QPointF(0.5 + x_off, 0.5 + y_off);
	points[1] = QPointF(0.5 + _w - x_off, 0.5 + y_off + float(y) / 2.0);
	points[2] = QPointF(0.5 + x_off, 0.5 + y_off + y);
	painter->setBrush(color);
	painter->setPen(color);
	painter->drawPolygon(points, 3, Qt::WindingFill);
	delete painter;
	icon_continue = QPixmap::fromImage(paper);

	// 'abort'
	paper.fill(QColor(255, 255, 255, 0).rgba());
	painter = new QPainter(&paper);
	painter->fillRect(x_off, y_off, _w - x_off * 2, y, color);
	delete painter;
	icon_abort = QPixmap::fromImage(paper);
}

void Batch::button_pause_as_pause(void) {
	button_pause->setIcon(icon_pause);
	button_pause->setToolTip(tr("Pause batch processing"));
}

void Batch::button_pause_as_continue(void) {
	button_pause->setIcon(icon_continue);
	button_pause->setToolTip(tr("Pause batch processing"));
}

void Batch::slot_status(long done, long total, bool to_disable) {
//	if(total - done <= 1) {
	if(to_disable) {
		button_pause->setEnabled(false);
		button_abort->setEnabled(false);
	} else {
		button_pause->setEnabled(true);
		button_abort->setEnabled(true);
	}
	if(!widget->isVisible())
		button_pause_as_pause();
	if(done >= total)
		widget->hide();
	else {
		widget->show();
		progress_bar->setMaximum(total);
		progress_bar->setValue(done);
#ifdef Q_OS_MAC
		QString progress;
		progress = QString().setNum(done) + " / " + QString().setNum(total);
		progress_label->setText(progress);
#endif
	}
}

void Batch::slot_pause(bool checked) {
	if(to_pause) {
		button_pause_as_pause();
		task_wait.notify_all();
	} else {
		do_pause();
	}
}

void Batch::slot_abort(bool checked) {
	do_abort();
}

//------------------------------------------------------------------------------
void Batch::run_batch(void) {
	// TODO: replace with capture PS_Base for each photo in the batch at this time, dynamically.
	edit->flush_current_ps();
	if(run_thread == nullptr) {
//		auto ptr = this;
//		run_thread = new std::thread( [ptr](void){ptr->run();} );
		run_thread = new std::thread( [=]{ run(); } );
	} else {
		// wake up thread
		task_wait.notify_all();
	}
}

void Batch::task_add(list<Batch::task_t> &tasks, bool ASAP) {
	task_list_lock.lock();
	c_total += tasks.size();
	// weird - there is no lists append or insert, so do it by hand
	if(ASAP) {
		for(list<Batch::task_t>::iterator it = tasks.end(); it != tasks.begin();) {
			--it;
			task_list.push_front(*it);
		}
	} else {
		for(list<Batch::task_t>::iterator it = tasks.begin(); it != tasks.end(); ++it)
			task_list.push_back(*it);
	}
	long _c_total = c_total;
	long _c_done = c_done;
	bool _to_pause = to_pause;
	task_list_lock.unlock();
	emit signal_status(_c_done, _c_total, (_c_total - _c_done) <= 1 && !_to_pause);
}

//------------------------------------------------------------------------------
// TODO: change GUI update schema.
void Batch::run(void) {
	// run in background - so user can edit images in the same time
	while(!to_leave) {
		long _c_total = c_total;
		// show "0 / total" at start
		emit signal_status(c_done, _c_total, false);
		while(true) {
			task_list_lock.lock();
			if(task_list.begin() == task_list.end()) {
				task_list_lock.unlock();
				break;
			}
			Batch::task_t task = *task_list.begin();
			task_list.pop_front();
			_c_total = c_total;
			task_list_lock.unlock();
//cerr << "batch process: import: " << task.photo_id.get_export_file_name() << endl;
//cerr << "               export: " << task.fname_export << endl;
			// synchronous call here
			process->process_export(task.photo_id, task.fname_export, &task.ep);
			if(to_leave) {
				break;
			}
			task_list_lock.lock();
			c_done++;
			long _c_done = c_done;
			_c_total = c_total;
			task_list_lock.unlock();
			// emit signal to update status
			emit signal_status(_c_done, _c_total, (_c_total - _c_done) <= 1 && !to_pause);
			if(to_pause) {
				button_pause_as_continue();
				break;
			}
		}
cerr << endl << "batch process: DONE" << endl << endl;
		if(to_leave)
			break;
		std::unique_lock<std::mutex> locker(task_list_lock, std::defer_lock);
		locker.lock();
		if(!to_pause) {
			c_done = 0;
			c_total = 0;
			emit signal_status(c_done, c_total, false);
		}
		if((task_list.begin() == task_list.end() && !to_leave) || to_pause)
			task_wait.wait(locker);
		to_pause = false;
		locker.unlock();
	}
//cerr << "leave!!!" << endl;
}

//------------------------------------------------------------------------------
void Batch::load_default_ep(export_parameters_t *ep) {
	Config::instance()->get(CONFIG_SECTION_BATCH, "process_asap", ep->process_asap);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_jpeg_image_quality", ep->options_jpeg.image_quality);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_jpeg_color_subsampling", ep->options_jpeg.color_subsampling_1x1);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_jpeg_color_space", ep->options_jpeg.color_space_rgb);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_png_alpha", ep->options_png.alpha);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_png_bits", ep->options_png.bits);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_tiff_alpha", ep->options_tiff.alpha);
	Config::instance()->get(CONFIG_SECTION_BATCH, "type_tiff_bits", ep->options_tiff.bits);
	string type_name = ep->image_type_to_name(ep->image_type);
	Config::instance()->get(CONFIG_SECTION_BATCH, "image_type", type_name);
	ep->image_type = ep->image_name_to_type(type_name);
	Config::instance()->get(CONFIG_SECTION_BATCH, "scaling_force", ep->scaling_force);
	Config::instance()->get(CONFIG_SECTION_BATCH, "scaling_to_fill", ep->scaling_to_fill);
	Config::instance()->get(CONFIG_SECTION_BATCH, "scaling_width", ep->scaling_width);
	Config::instance()->get(CONFIG_SECTION_BATCH, "scaling_height", ep->scaling_height);
}

void Batch::save_default_ep(export_parameters_t *ep) {
	Config::instance()->set(CONFIG_SECTION_BATCH, "process_asap", ep->process_asap);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_jpeg_image_quality", ep->options_jpeg.image_quality);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_jpeg_color_subsampling", ep->options_jpeg.color_subsampling_1x1);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_jpeg_color_space", ep->options_jpeg.color_space_rgb);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_png_alpha", ep->options_png.alpha);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_png_bits", ep->options_png.bits);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_tiff_alpha", ep->options_tiff.alpha);
	Config::instance()->set(CONFIG_SECTION_BATCH, "type_tiff_bits", ep->options_tiff.bits);
	Config::instance()->set(CONFIG_SECTION_BATCH, "scaling_force", ep->scaling_force);
	Config::instance()->set(CONFIG_SECTION_BATCH, "scaling_to_fill", ep->scaling_to_fill);
	Config::instance()->set(CONFIG_SECTION_BATCH, "scaling_width", ep->scaling_width);
	Config::instance()->set(CONFIG_SECTION_BATCH, "scaling_height", ep->scaling_height);
	string type_name = ep->image_type_to_name(ep->image_type);
	Config::instance()->set(CONFIG_SECTION_BATCH, "image_type", type_name);
}

void Batch::process_save_as(Photo_ID photo_id) {
	if(photo_id.is_empty())
		return;
	cerr << "Batch::process_save_as( \"" << photo_id.get_export_file_name() << "\" )" << endl;
	list<Photo_ID> p_list;
	p_list.push_back(photo_id);
	process_export(p_list);
}

void Batch::process_batch(list<Photo_ID> _list) {
	if(_list.begin() == _list.end())
		return;
	process_export(_list);
}

void Batch::process_export(list<Photo_ID> _list) {
	string file_name = "";
	export_parameters_t *ep = new export_parameters_t;
	ep->folder = destination_dir;
	ep->process_single = (_list.size() == 1);
	Photo_ID photo_id;
	if(ep->process_single) {
		photo_id = *_list.begin();
		file_name = (*_list.begin()).get_export_file_name();
		ep->set_file_name(file_name);
	}
	load_default_ep(ep);

	Batch_Dialog *dialog = new Batch_Dialog(ep, window);
	bool accepted = dialog->exec();
	delete dialog;

	if(accepted) {
		save_default_ep(ep);
		destination_dir = ep->folder;

		list<Batch::task_t> tasks;
		if(ep->process_single) {
			string out_file_name = ep->folder;
			out_file_name += QDir::toNativeSeparators("/").toStdString();
			out_file_name += ep->get_file_name();
			task_t task;
			task.photo_id = photo_id;
			task.fname_export = out_file_name;
			task.ep = *ep;
//			task.ep = std::shared_ptr<export_parameters_t>(ep);
			tasks.push_back(task);
		} else {
			string separator = QDir::toNativeSeparators("/").toStdString();
			for(list<Photo_ID>::iterator it = _list.begin(); it != _list.end(); ++it) {
				// create target filename
				task_t task;
				task.photo_id = *it;
				ep->set_file_name((*it).get_export_file_name());
				string rez_fn = ep->folder + separator + ep->get_file_name();
				task.fname_export = rez_fn;
				task.ep = *ep;
//				task.ep = std::shared_ptr<export_parameters_t>(ep);
				tasks.push_back(task);
			}
		}
		task_add(tasks, ep->process_asap);
		run_batch();
	}
}

//------------------------------------------------------------------------------
