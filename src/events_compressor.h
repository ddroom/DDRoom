#ifndef __H_EVENTS_COMPRESSOR__
#define __H_EVENTS_COMPRESSOR__
/*
 * events_compressor.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QThread>
#include <QMutex>
#include <QWaitCondition>

//------------------------------------------------------------------------------
// class-agent to complete asynchronous tasks by one performer and loose all accepted tasks while performer is busy, except the last one.
// useful to perform actions initiated by mouse
template<class T> class events_compressor : public QThread {
public:
	events_compressor(void (*ptr)(void *data, T task), void *data) {
		f_ptr = ptr;
		f_data = data;
		task_is_active = false;
		do_not_stop = true;
		start();
//		exec();
	}
	~events_compressor() {
		do_not_stop = false;
		task_lock.lock();
		task_wait.wakeAll();
		task_lock.unlock();
		wait();
	}
	void assign_task(T task) {
		task_lock.lock();
		task_current = task;
		task_is_active = true;
		task_wait.wakeAll();
		task_lock.unlock();
	}
	void run(void) {
		task_lock.lock();
		while(do_not_stop) {
			if(task_is_active) {
				task_is_active = false;
				T task = task_current;
				task_lock.unlock();
				// that will take a while
				f_ptr(f_data, task);
				task_lock.lock();
			}
			while(task_is_active == false && do_not_stop)
				task_wait.wait(&task_lock);
		}
		task_lock.unlock();
	}
protected:
	events_compressor(void);
	void (*f_ptr)(void *data, T task);
	void *f_data;
	QMutex task_lock;
	QWaitCondition task_wait;
	T task_current;
	bool task_is_active;
	bool do_not_stop;
};

//------------------------------------------------------------------------------
#endif // __H_EVENTS_COMPRESSOR__
