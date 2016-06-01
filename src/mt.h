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
//	return atom->fetchAndAddRelaxed(value);
	return atom->fetchAndAddOrdered(value);
}

#define __MT_QSEMAPHORES
//#undef __MT_QSEMAPHORES

//------------------------------------------------------------------------------
class Flow {
	friend class SubFlow;

public:
	enum flow_method_t {
		flow_method_thread,
		flow_method_function
	};
	Flow(void (*method)(void *obj, class SubFlow *subflow, void *data), void *object, void *method_data, flow_method_t flow_method = flow_method_thread);
	virtual ~Flow(void);
	void flow(void);

protected:
	void set_private(void **priv_array);
	class SubFlow **subflows;
	int cores;
#ifdef __MT_QSEMAPHORES
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
#endif
};

//------------------------------------------------------------------------------
class SubFlow {
	friend class Flow;

public:
	SubFlow(Flow *parent);
	virtual ~SubFlow();

	virtual bool is_master(void) {return _master;}
	virtual int cores(void) {return _cores;}

	// Some data that should be shared between all of the threads.
	virtual void set_private(void **priv_array);
	virtual void *get_private(void);

	// Note: avoid recursion of sync_point_pre()/sync_point_post() calls.
	virtual void sync_point(void) = 0;
	virtual bool sync_point_pre(void) = 0;
	virtual void sync_point_post(void) = 0;

	virtual bool wait(void);
	virtual void start(void);

protected:
//	virtual void run(void);
	void (*method)(void *, class SubFlow *, void *);
	void *object;
	void *method_data;

	Flow *_parent;
	bool _master;
	int _cores;
	void *_target_private;
};

//------------------------------------------------------------------------------
class SubFlow_Function : public SubFlow {
public:
	SubFlow_Function(Flow *parent);
	virtual ~SubFlow_Function();
	void sync_point(void);
	bool sync_point_pre(void);
	void sync_point_post(void);
};

//------------------------------------------------------------------------------
class SubFlow_Thread : public QThread,  public SubFlow {
	Q_OBJECT
	friend class Flow;

public:
	virtual ~SubFlow_Thread();
	void sync_point(void);
	bool sync_point_pre(void);
	void sync_point_post(void);

	bool wait(void);
	void start(void);

protected:
	SubFlow_Thread(Flow *parent, bool master, int cores);
	void run(void);

#ifdef __MT_QSEMAPHORES
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
#endif
};

//------------------------------------------------------------------------------
#endif // __H_MT__
