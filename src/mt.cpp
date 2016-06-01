/*
 * mt.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "mt.h"
#include "system.h"

#define SYNC_POINTS_COUNT	3

#include <iostream>

using namespace std;

//------------------------------------------------------------------------------
Flow::Flow(void (*method)(void *, class SubFlow *, void *), void *object, void *method_data, Flow::flow_method_t flow_method) {
	cores = System::instance()->cores();
	if(flow_method == flow_method_function)
		cores = 1;
//cerr << "cores == " << cores << endl;
//cerr << "Flow::Flow()" << endl;
	subflows = new SubFlow *[cores];

	c_lock.store(0);
	c_jobs.store(0);

	for(int i = 0; i < cores; i++) {
		SubFlow *subflow(nullptr);
		if(flow_method == flow_method_thread) {
			SubFlow_Thread *st = new SubFlow_Thread(this, (i == 0), cores);
			st->w_m_lock = &w_m_lock;
			st->w_s_lock = &w_s_lock;
			st->w_jobs = &w_jobs;
			st->m_lock = &m_lock;
			st->m_jobs = &m_jobs;
			st->c_lock = &c_lock;
			st->c_jobs = &c_jobs;
			subflow = st;
		}
		if(flow_method == flow_method_function)
			subflow = new SubFlow_Function(this);
		subflow->method = method;
		subflow->object = object;
		subflow->method_data = method_data;
		subflows[i] = subflow;
	}
//cerr << "Flow::Flow()... done" << endl;
}

Flow::~Flow(void) {
//cerr << "Flow::~Flow()" << endl;
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
	_target_private = nullptr;
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

void SubFlow::wait(void) {
}

void SubFlow::start(void) {
	if(method)
		method(object, this, method_data);
}

//------------------------------------------------------------------------------
SubFlow_Function::SubFlow_Function(Flow *parent) : SubFlow(parent) {
}

SubFlow_Function::~SubFlow_Function() {
}

void SubFlow_Function::sync_point(void) {
}

bool SubFlow_Function::sync_point_pre(void) {
	return true;
}

void SubFlow_Function::sync_point_post(void) {
}

//------------------------------------------------------------------------------
SubFlow_Thread::SubFlow_Thread(Flow *parent, bool master, int cores) : SubFlow(parent) {
	_master = master;
	_cores = cores;
	_target_private = nullptr;
}

SubFlow_Thread::~SubFlow_Thread() {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void SubFlow_Thread::wait(void) {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
		std_thread = nullptr;
	}
}

void SubFlow_Thread::start(void) {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
	if(method != nullptr)
		std_thread = new std::thread(method, object, this, method_data);
//	QThread::start();
}

/*
void SubFlow_Thread::run(void) {
	if(method)
		method(object, this, method_data);
}
*/

void SubFlow_Thread::sync_point(void) {
	sync_point_pre();
	sync_point_post();
}

// - master flow will wait for call of ::sync_point_pre() from a slave flows, and then - return back.
// - slave flows will waits state inside of this call for a call of ::sync_point_post() from a master flow.
// - the goal of this calls pair: thread-safe and synchronous manipulation of flow's private data from the master flow.
bool SubFlow_Thread::sync_point_pre(void) {
	if(_cores > 1) {
		if(_master) {
			// wait till all slaves are done
			std::unique_lock<std::mutex> m_locker(*m_lock);
			if(c_lock->load() != _cores - 1)
				w_m_lock->wait(m_locker);
			return true;
		} else {
			// wait till slaves complete their jobs
			std::unique_lock<std::mutex> m_jobs_locker(*m_jobs);
			if(c_jobs->load() != 0) {
				int jobs = c_jobs->fetch_add(-1) - 1;
				if(jobs == 0) {
					w_jobs->notify_all();
				} else {
					if(jobs != 0) {
						w_jobs->wait(m_jobs_locker);
					}
				}
			}
			m_jobs_locker.unlock();
			// hey master, wake up !!!
			m_lock->lock();
			if((c_lock->fetch_add(1) + 1) == _cores - 1)
				w_m_lock->notify_all();
			m_lock->unlock();
			// this one is not master...
			return false;
		}
	}
	return true;
}

void SubFlow_Thread::sync_point_post(void) {
	if(_cores > 1) {
		if(_master) {
			// put slaves back to work
			m_jobs->lock();
			c_jobs->store(_cores - 1);
			m_jobs->unlock();
			m_lock->lock();
			c_lock->store(0);
			w_s_lock->notify_all();
			m_lock->unlock();
		} else {
			// wait for master's signal
			std::unique_lock<std::mutex> m_lock_locker(*m_lock);
			if(c_lock->load() != 0)
				w_s_lock->wait(m_lock_locker);
		}
	}
}
//------------------------------------------------------------------------------
