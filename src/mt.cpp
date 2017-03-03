/*
 * mt.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "mt.h"
#include "system.h"

//------------------------------------------------------------------------------
Flow::Flow(void (*f_ptr)(void *, class SubFlow *, void *), void *f_object, void *f_data, int _threads_count) {
	threads_count = _threads_count;
	if(_threads_count <= 0)
		threads_count = System::instance()->cores();
	subflows = new SubFlow *[threads_count];

	for(int i = 0; i < threads_count; ++i) {
		SubFlow *subflow = new SubFlow(this, i, threads_count);
		subflows[i] = subflow;

		subflow->m_lock = &m_lock;
		subflow->cv_in = &cv_in;
		subflow->cv_out = &cv_out;
		subflow->cv_master = &cv_master;
		subflow->b_counter_in = &b_counter_in;
		subflow->b_counter_out = &b_counter_out;
		subflow->b_flag_in = &b_flag_in;
		subflow->b_flag_out = &b_flag_out;
		subflow->b_flag_master_wakeup = &b_flag_master_wakeup;

		subflow->f_ptr = f_ptr;
		subflow->f_object = f_object;
		subflow->f_data = f_data;
	}
}

Flow::~Flow(void) {
	for(int i = 0; i < threads_count; ++i)
		subflows[i]->wait();
	for(int i = 0; i < threads_count; ++i)
		delete subflows[i];
	delete[] subflows;	
}

void Flow::flow(void) {
	for(int i = 0; i < threads_count; ++i)
		subflows[i]->start();
	for(int i = 0; i < threads_count; ++i)
		subflows[i]->wait();
}

void Flow::set_private(void **data) {
	for(int i = 0; i < threads_count; ++i)
		subflows[i]->_target_private = data[i];
}

//------------------------------------------------------------------------------
SubFlow::SubFlow(Flow *parent, int _id, int threads_count) 
	:_parent(parent), i_id(_id), i_threads_count(threads_count) {
	_master = (i_id == 0);
}

SubFlow::~SubFlow() {
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void SubFlow::set_private(void **array) {
	if(_master)
		_parent->set_private(array);
}

void *SubFlow::get_private(void) {
	return _target_private;
}

void SubFlow::wait(void) {
	if(i_threads_count == 1)
		return;

	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
		std_thread = nullptr;
	}
}

void SubFlow::start(void) {
	if(i_threads_count == 1) {
		if(f_ptr)
			f_ptr(f_object, this, f_data);
		return;
	}

	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
	if(f_ptr != nullptr)
		std_thread = new std::thread(f_ptr, f_object, this, f_data);
}

void SubFlow::sync_point(void) {
	if(i_threads_count == 1)
		return;

	// wait till all threads leave barrier if any
	std::unique_lock<std::mutex> lock(*m_lock);
	if(*b_counter_out != 0) {
		*b_flag_out = true;
		cv_out->wait(lock, [this]{return *(this->b_flag_out) == false;});
	}

	// count in
	*b_flag_in = true;
	++(*b_counter_in);
	if(*b_counter_in == i_threads_count) {
		// the last thread - release all other
		*b_flag_in = false;
		--(*b_counter_in);
		*b_counter_out = *b_counter_in;
		lock.unlock();
		cv_in->notify_all();
		return;
	}
	if(*b_flag_in == true)
		cv_in->wait(lock, [this]{return *(this->b_flag_in) == false;});

	--(*b_counter_in);
	--(*b_counter_out);
	bool flag_last = (*b_counter_in == 0);
	bool flag_wakeup = false;
	if(flag_last) {
		flag_wakeup = *b_flag_out;
		*b_flag_out = false;
	}
	lock.unlock();
	if(flag_wakeup)
		cv_out->notify_all();
}

bool SubFlow::sync_point_pre(void) {
	if(i_threads_count == 1)
		return true;

	// wait till all threads leave barrier if any
	std::unique_lock<std::mutex> lock(*m_lock);
	if(*b_counter_out != 0) {
		*b_flag_out = true;
		cv_out->wait(lock, [this]{return *(this->b_flag_out) == false;});
	}

	// count in
	*b_flag_master_wakeup = false;
	*b_flag_in = true;
	++(*b_counter_in);
	if(*b_counter_in == i_threads_count) {
		// the last thread
		*b_counter_out = i_threads_count;
		if(i_id == 0) { // master - wakeup all later
			lock.unlock();
			return true;
		}
		// not master - wake up master
		*b_flag_master_wakeup = true;
		lock.unlock();
		cv_master->notify_one();
		lock.lock();
	}
	if(i_id == 0) { // master isn't last - sleep till wakeup by last thread
		cv_master->wait(lock, [this]{return *b_flag_master_wakeup == true;});
		lock.unlock();
		return true;
	}
	if(*b_flag_in == true)
		cv_in->wait(lock, [this]{return *(this->b_flag_in) == false;});

	--(*b_counter_in);
	--(*b_counter_out);
	bool flag_last = (*b_counter_in == 0);
	bool flag_wakeup = false;
	if(flag_last) {
		flag_wakeup = *b_flag_out;
		*b_flag_out = false;
	}
	lock.unlock();
	if(flag_wakeup)
		cv_out->notify_all();
	return false;
}

void SubFlow::sync_point_post(void) {
	if(i_threads_count == 1 || i_id != 0)
		return;

	// wake up all threads
	std::unique_lock<std::mutex> lock(*m_lock);
	--(*b_counter_in);
	--(*b_counter_out);
	*b_flag_in = false;
	bool flag_last = (*b_counter_in == 0);
	bool flag_wakeup = false;
	if(flag_last) {
		flag_wakeup = *b_flag_out;
		*b_flag_out = false;
	}
	lock.unlock();
	cv_in->notify_all();
	if(flag_wakeup)
		cv_out->notify_all();
}

//------------------------------------------------------------------------------
