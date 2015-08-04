#ifndef __H_MT__
#define __H_MT__
/*
 * mt.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QSemaphore>
#include <QWaitCondition>
#include <QMutex>
#include <QAtomicInt>
#include <QThread>

inline int _mt_qatom_fetch_and_add(QAtomicInt *atom, int value) {
	return atom->fetchAndAddOrdered(value);
}

//#define MT_QSEMAPHORES
#undef MT_QSEMAPHORES

//------------------------------------------------------------------------------
class Flow {
	friend class SubFlow;

public:
	Flow(void (*method)(void *obj, class SubFlow *subflow, void *data), void *object, void *method_data);
	~Flow(void);
	void flow(void);

protected:
	void set_private(void **priv_array);
	class SubFlow **subflows;
	int cores;
#ifdef MT_QSEMAPHORES
	QSemaphore **s_master;
	QSemaphore **s_slaves;
#else
	QWaitCondition w_m_lock;
	QWaitCondition w_s_lock;
	QWaitCondition w_jobs;
	QMutex m_lock;
	QMutex m_jobs;
	QAtomicInt c_lock;
	QAtomicInt c_jobs;
//	volatile int c_lock;
//	volatile int c_jobs;
#endif
};

//------------------------------------------------------------------------------
class SubFlow : public QThread {
	Q_OBJECT

	friend class Flow;
public:
	bool is_master(void) {return _master;}
	int cores(void) {return _cores;}

	void set_private(void **priv_array);

	void sync_point(void);
	bool sync_point_pre(void);
	void sync_point_post(void);

	// some data that should be transferred from the master to all threads
	void *get_private(void) {
		return _target_private;
	}

protected:
	SubFlow(Flow *parent, bool master, int cores) : QThread() {
		_parent = parent;
		_master = master;
		_cores = cores;
		_target_private = NULL;
#ifdef MT_QSEMAPHORES
		_sync_point = 0;
#endif
	}

	void run(void) {
		method(object, this, method_data);
	}
	void (*method)(void *, class SubFlow *, void *);
	void *object;
	void *method_data;

	Flow *_parent;
	bool _master;
	int _cores;
	
	void *_target_private;

#ifdef MT_QSEMAPHORES
	QSemaphore **s_master;
	QSemaphore **s_slaves;
	int _sync_point;
#else
	QWaitCondition *w_m_lock;
	QWaitCondition *w_s_lock;
	QWaitCondition *w_jobs;
	QMutex *m_lock;
	QMutex *m_jobs;
	QAtomicInt *c_lock;
	QAtomicInt *c_jobs;
//	volatile int *c_lock;
//	volatile int *c_jobs;
#endif
};

//------------------------------------------------------------------------------

#endif // __H_MT__
