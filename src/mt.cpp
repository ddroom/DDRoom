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
// Notes:
// 1. don't try to use recursion between sync_point_pre()/sync_point_post()
//		(will crash)
// 2. Don't try to manipulate with any widget from the thread - that should be done only from the application main thread !

Flow::Flow(void (*method)(void *, class SubFlow *, void *), void *object, void *method_data) {
	cores = System::instance()->cores();
//cerr << "cores == " << cores << endl;
//cerr << "Flow::Flow()" << endl;
	subflows = new SubFlow *[cores];

#ifdef MT_QSEMAPHORES
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
		subflows[i] = new SubFlow(this, (i == 0), cores);
		subflows[i]->method = method;
		subflows[i]->object = object;
		subflows[i]->method_data = method_data;
#ifdef MT_QSEMAPHORES
		subflows[i]->s_master = s_master;
		subflows[i]->s_slaves = s_slaves;
#else
		subflows[i]->w_m_lock = &w_m_lock;
		subflows[i]->w_s_lock = &w_s_lock;
		subflows[i]->w_jobs = &w_jobs;
		subflows[i]->m_lock = &m_lock;
		subflows[i]->m_jobs = &m_jobs;
		subflows[i]->c_lock = &c_lock;
		subflows[i]->c_jobs = &c_jobs;
#endif

	}
//cerr << "Flow::Flow()... done" << endl;
}

Flow::~Flow(void) {
//cerr << "Flow::~Flow()" << endl;
#ifdef MT_QSEMAPHORES
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

void SubFlow::set_private(void **array) {
	if(_master)
		_parent->set_private(array);
}

void SubFlow::sync_point(void) {
	sync_point_pre();
	sync_point_post();
}

#ifdef MT_QSEMAPHORES
bool SubFlow::sync_point_pre(void) {
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

void SubFlow::sync_point_post(void) {
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
bool SubFlow::sync_point_pre(void) {
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

void SubFlow::sync_point_post(void) {
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
