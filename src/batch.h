#ifndef __H_BATCH__
#define __H_BATCH__
/*
 * batch.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>
#include <list>

#include <QtWidgets>

#include "photo.h"
#include "export.h"

//------------------------------------------------------------------------------
class Batch : public QThread {
	Q_OBJECT

public:
	Batch(class QWidget *window, class Process *process, class Edit *edit, class Browser *browser);
	virtual ~Batch();
	void fill_menu(class QMenu *menu);
	void process_save_as(std::string file_name);
	void process_batch(std::list<std::string> _list);

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
		std::string fname_import;
		std::string fname_export;
		export_parameters_t ep;
//		QSharedPointer<export_parameters_t> ep;
	};
	void process_export(std::list<std::string> _list);

	std::string destination_dir;
	void task_add(std::list<Batch::task_t> &tasks, bool ASAP = false);
	void run_batch(void);
	void run(void);
	void load_default_ep(export_parameters_t *ep);
	void save_default_ep(export_parameters_t *ep);

	QMutex task_list_lock;
	std::list<Batch::task_t> task_list;
	bool was_run;
	QWaitCondition task_wait;
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
