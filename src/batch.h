#ifndef __H_BATCH__
#define __H_BATCH__
/*
 * batch.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <condition_variable>
#include <list>
#include <string>
#include <thread>

#include <QtWidgets>

#include "photo.h"
#include "export.h"

//------------------------------------------------------------------------------
class Batch : public QObject {
	Q_OBJECT

public:
	Batch(class QWidget *window, class Process *process, class Edit *edit, class Browser *browser);
	virtual ~Batch();
	void fill_menu(class QMenu *menu);
	void process_save_as(Photo_ID photo_id);
	void process_batch(std::list<Photo_ID> _list);

	void start(void);

	void do_pause(void);
	void do_continue(void);
	void do_abort(void);

	QWidget *controls(QWidget *parent = 0);

public slots:
	void slot_status(long done, long total, bool to_disable = false);
	void slot_pause(bool checked);
	void slot_abort(bool checked);
	void slot_export(void);

signals:
	void signal_status(long done, long total, bool to_disable = false);
	void signal_batch_accepted(void);

	//menu
protected:
	QAction *action_save_as;
	QAction *action_batch;

protected slots:
	void menu_save_as(void);
	void menu_batch(void);
	//--
	void slot_selection_changed(int);
	void slot_active_photo_changed(void);

protected:
	int selected_photos_count;

protected:
	class QWidget *window;
	class Process *process;
	class Edit *edit;
	class Browser *browser;
	class task_t {
	public:
		Photo_ID photo_id;
		std::string fname_export;
		export_parameters_t ep;
//		std::shared_ptr<export_parameters_t> ep;
	};
	void process_export(std::list<Photo_ID> _list);

	std::string destination_dir;
	void task_add(std::list<Batch::task_t> &tasks, bool ASAP = false);
	void run_batch(void);
	void run(void);
	std::thread *std_thread = nullptr;
	void load_default_ep(export_parameters_t *ep);
	void save_default_ep(export_parameters_t *ep);

	std::mutex task_list_lock;
	std::list<Batch::task_t> task_list;
	bool was_run;
	std::condition_variable task_wait;
	volatile bool to_leave;
	volatile bool to_pause;
	volatile long c_done;
	volatile long c_total;

	// controls & status
	QWidget *widget;
	QProgressBar *progress_bar;
	QToolButton *button_pause;
	QToolButton *button_abort;
#ifdef Q_OS_MAC
	QLabel *progress_label;
#endif
	void button_pause_as_pause(void);
	void button_pause_as_continue(void);
	void icons_create(QWidget *);
	QIcon icon_pause;
	QIcon icon_continue;
	QIcon icon_abort;

};

//------------------------------------------------------------------------------
#endif // __H_BATCH__
