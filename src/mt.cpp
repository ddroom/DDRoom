/*
 * mt.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "mt.h"
#include "system.h"

#define SYNC_POINTS_COUNT	3

#include <iostream>

using namespace std;

//------------------------------------------------------------------------------
Flow::Flow(void (*method)(void *, class SubFlow *, void *), void *object, void *method_data, int force_cores_to) {
	cores = System::instance()->cores();
	if(force_cores_to != 0)
		cores = force_cores_to;
//cerr << "cores == " << cores << endl;
//cerr << "Flow::Flow()" << endl;
	subflows = new SubFlow *[cores];

#ifdef __MT_QSEMAPHORES
	s_master = new QSemaphore *[SYNC_POINTS_COUNT];
	s_slaves = new QSemaphore *[SYNC_POINTS_COUNT];
	if(cores > 1) {
		for(int i = 0; i < SYNC_POINTS_COUNT; i++) {
			s_master[i] = new QSemaphore(0);
			s_slaves[i] = new QSemaphore(0);
		}
	}
#else
	c_lock = QAtomicInt(0);
	c_jobs = QAtomicInt(0);
#endif

	for(int i = 0; i < cores; i++) {
		SubFlow_Thread *subflow = new SubFlow_Thread(this, (i == 0), cores);
		subflow->method = method;
		subflow->object = object;
		subflow->method_data = method_data;
#ifdef __MT_QSEMAPHORES
		subflow->s_master = s_master;
		subflow->s_slaves = s_slaves;
#else
		subflow->w_m_lock = &w_m_lock;
		subflow->w_s_lock = &w_s_lock;
		subflow->w_jobs = &w_jobs;
		subflow->m_lock = &m_lock;
		subflow->m_jobs = &m_jobs;
		subflow->c_lock = &c_lock;
		subflow->c_jobs = &c_jobs;
#endif
		subflows[i] = subflow;
	}
//cerr << "Flow::Flow()... done" << endl;
}

Flow::~Flow(void) {
//cerr << "Flow::~Flow()" << endl;
#ifdef __MT_QSEMAPHORES
	if(cores > 1) {
		for(int i = 0; i < SYNC_POINTS_COUNT; i++) {
			delete s_master[i];
			delete s_slaves[i];
		}
	}
	delete[] s_master;
	delete[] s_slaves;
#endif
	for(int i = 0; i < cores; i++)
		subflows[i]->wait();
	for(int i = 0; i < cores; i++)
		delete subflows[i];
	delete[] subflows;	
//cerr << "Flow::~Flow()... done" << endl;
}

void Flow::flow(void) {
//cerr << "Flow::flow()" << endl;
	for(int i = 0; i < cores; i++)
		subflows[i]->start();
	for(int i = 0; i < cores; i++)
		subflows[i]->wait();
//cerr << "Flow::flow()" << endl;
}

void Flow::set_private(void **data) {
	for(int i = 0; i < cores; i++)
		subflows[i]->_target_private = data[i];
}

//------------------------------------------------------------------------------
SubFlow::SubFlow(Flow *parent) {
	_parent = parent;
	_master = true;
	_cores = 1;
	_target_private = NULL;
}

SubFlow::~SubFlow() {
}

void *SubFlow::get_private(void) {
	return _target_private;
}

void SubFlow::set_private(void **array) {
	if(_master)
		_parent->set_private(array);
}

bool SubFlow::wait(void) {
	return true;
}

void SubFlow::start(void) {
	if(method)
		method(object, this, method_data);
}

//------------------------------------------------------------------------------
void SubFlow_Function::sync_point(void) {
}

bool SubFlow_Function::sync_point_pre(void) {
	return true;
}

void SubFlow_Function::sync_point_post(void) {
}

SubFlow_Function::~SubFlow_Function() {
}

//------------------------------------------------------------------------------
SubFlow_Thread::SubFlow_Thread(Flow *parent, bool master, int cores) : QThread(), SubFlow(parent) {
//	_parent = parent;
	_master = master;
	_cores = cores;
	_target_private = NULL;
#ifdef __MT_QSEMAPHORES
	_sync_point = 0;
#endif
}

SubFlow_Thread::~SubFlow_Thread() {
}

bool SubFlow_Thread::wait(void) {
	return QThread::wait();
}

void SubFlow_Thread::start(void) {
	QThread::start();
}

void SubFlow_Thread::run(void) {
	if(method)
		method(object, this, method_data);
}

void SubFlow_Thread::sync_point(void) {
	sync_point_pre();
	sync_point_post();
}

#ifdef __MT_QSEMAPHORES
bool SubFlow_Thread::sync_point_pre(void) {
	if(_cores > 1) {
		if(_master) {
			s_master[_sync_point]->acquire(_cores - 1);
			return true;
		} else {
			return false;
		}
	}
	return true;
}

void SubFlow_Thread::sync_point_post(void) {
	int sp = _sync_point;
	if(_cores > 1) {
		if(_master) {
			_sync_point++;
			if(_sync_point > SYNC_POINTS_COUNT - 1)
				_sync_point = 0;
			s_slaves[sp]->release(_cores - 1);
		} else {
			s_master[sp]->release();
			_sync_point++;
			if(_sync_point > SYNC_POINTS_COUNT - 1)
				_sync_point = 0;
			s_slaves[sp]->acquire();
		}
	}
}
#else
// - master flow would be waiting for call of ::sync_point_pre() from a slave flows, and then - return back.
// - slave flows would be in a waiting state inside of this call for a call of ::sync_point_post() from a master flow.
// - the goal of this calls pair: thread-safe and synchronous manipulation of flow's private data from the master flow.
bool SubFlow_Thread::sync_point_pre(void) {
	if(_cores > 1) {
		if(_master) {
			// wait till all slaves are done
			m_lock->lock();
//			while(*c_lock != _cores - 1)
//			if(*c_lock != _cores - 1)
			if(c_lock->load() != _cores - 1)
				w_m_lock->wait(m_lock);
			m_lock->unlock();
			return true;
		} else {
///*
			// wait till slaves complete their jobs
			m_jobs->lock();
//			if(*c_jobs != 0) {
//				--(*c_jobs);
//				if(*c_jobs == 0) {
			if(c_jobs->load() != 0) {
				int jobs = c_jobs->fetchAndAddOrdered(-1) - 1;
				if(jobs == 0) {
					w_jobs->wakeAll();
				} else {
//					while(*c_jobs != 0) {
//					if(*c_jobs != 0) {
					if(jobs != 0) {
						w_jobs->wait(m_jobs);
					}
				}
			}
			m_jobs->unlock();
			// hey master, wake up !!!
			m_lock->lock();
//			++(*c_lock);
//			if(*c_lock == _cores - 1)
			if((c_lock->fetchAndAddOrdered(1) + 1) == _cores - 1)
				w_m_lock->wakeAll();
			m_lock->unlock();
//*/
/*
			m_lock->lock();
			++(*c_lock);
			if(*c_lock == _cores - 1)
				w_m_lock->wakeAll();
			m_lock->unlock();
*/
			// this one is not master...
			return false;
		}
	}
	return true;
}

void SubFlow_Thread::sync_point_post(void) {
	if(_cores > 1) {
		if(_master) {
			// slaves back to work
///*
			m_jobs->lock();
//			*c_jobs = _cores - 1;
			c_jobs->store(_cores - 1);
			m_jobs->unlock();
//*/
			m_lock->lock();
//			*c_lock = 0;
			c_lock->store(0);
			w_s_lock->wakeAll();
			m_lock->unlock();
		} else {
			// wait for master's signal
			m_lock->lock();
//			while(*c_lock != 0)
//			if(*c_lock != 0)
			if(c_lock->load() != 0)
				w_s_lock->wait(m_lock);
			m_lock->unlock();
		}
	}
}
#endif
//------------------------------------------------------------------------------
