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
decltype(Flow::run_flows) Flow::run_flows = decltype(Flow::run_flows)();
std::mutex Flow::run_flows_lock;

Flow::Flow(priority_t priority, void (*f_ptr)(void *, class SubFlow *, void *), void *f_object, void *f_data, int _threads_count)
	: _priority(priority)
{
	threads_count = _threads_count;
	if(_threads_count <= 0)
		threads_count = System::instance()->cores();
	subflows.resize(threads_count);

	for(int i = 0; i < threads_count; ++i) {
		SubFlow *subflow = new SubFlow(this, i, threads_count);
		subflows[i] = subflow;

		subflow->m_lock = &m_lock;
		subflow->cv_in = &cv_in;
		subflow->cv_out = &cv_out;
		subflow->cv_main = &cv_main;
		subflow->b_counter_in = &b_counter_in;
		subflow->b_counter_out = &b_counter_out;
		subflow->b_flag_in = &b_flag_in;
		subflow->b_flag_out = &b_flag_out;
		subflow->b_flag_main_wakeup = &b_flag_main_wakeup;
		subflow->b_flag_abort = &b_flag_abort;

		subflow->b_flag_pause = &b_flag_pause;
		subflow->cv_pause = &cv_pause;

		subflow->f_ptr = f_ptr;
		subflow->f_object = f_object;
		subflow->f_data = f_data;
	}
}

Flow::~Flow(void) {
	for(auto subflow : subflows) {
		subflow->wait();
		delete subflow;
	}
}

void Flow::flow(void) {
	b_flag_pause.store(0);
	run_flows_add(this);
	// there is no need to run already paused subflows
	std::unique_lock<std::mutex> lock(m_lock);
	while(b_flag_pause)
		cv_pause.wait(lock, [this]{return b_flag_pause.load() == 0;});
	lock.unlock();

	// 
	for(auto subflow : subflows)
		subflow->start();
	for(auto subflow : subflows)
		subflow->wait();

	run_flows_remove(this);
}

void Flow::set_private(void **priv_data) {
	for(int i = 0; i < threads_count; ++i)
		subflows[i]->_target_private = priv_data[i];
}

void Flow::set_private(void *priv_data, int thread_index) {
	subflows[thread_index]->_target_private = priv_data;
}

void *Flow::get_private(int thread_index) {
	return subflows[thread_index]->_target_private;
}

void Flow::run_flows_add(Flow *flow) {
	std::unique_lock<std::mutex> lock(run_flows_lock);
	run_flows.insert(std::pair<Flow::priority_t, Flow *>(flow->_priority, flow));
	rerun_flows();
}

void Flow::run_flows_remove(Flow *flow) {
	std::unique_lock<std::mutex> lock(run_flows_lock);
	auto it = run_flows.find(flow->_priority);
	while(it != run_flows.end() && flow != it->second)
		++it;
	if(it != run_flows.end())
		run_flows.erase(it);
	rerun_flows();
}

void Flow::rerun_flows(void) {
	// run first, pause all other flows
	bool first = true;
	for(auto el : run_flows) {
		if(first) {
			first = false;
			el.second->resume();
		} else {
			el.second->pause();
		}
	}
}

void Flow::pause(void) {
//	std::unique_lock<std::mutex> lock(m_lock);
	b_flag_pause.store(1);
}

void Flow::resume(void) {
/*
//	std::unique_lock<std::mutex> lock(m_lock);
	b_flag_pause.store(0);
//	lock.unlock();
	cv_pause.notify_all()
*/
	if(b_flag_pause.exchange(0) == 1)
		cv_pause.notify_all();
}

//------------------------------------------------------------------------------
SubFlow::SubFlow(Flow *parent, int _id, int threads_count) 
	:_parent(parent), i_id(_id), i_threads_count(threads_count) {
	_main = (i_id == 0);
}

SubFlow::~SubFlow() {
	if(sf_thread != nullptr) {
		sf_thread->join();
		delete sf_thread;
	}
}

void SubFlow::set_private(void **priv_array) {
	if(_main)
		_parent->set_private(priv_array);
}

void SubFlow::set_private(void *priv_data, int thread_index) {
	if(_main || thread_index == i_id)
		_parent->set_private(priv_data, thread_index);
}

void *SubFlow::get_private(void) {
	return _target_private;
}

void *SubFlow::get_private(int thread_index) {
	return _main ? _parent->get_private(thread_index) : nullptr;
}

void SubFlow::wait(void) {
	if(i_threads_count == 1)
		return;

	if(sf_thread != nullptr) {
		sf_thread->join();
		delete sf_thread;
		sf_thread = nullptr;
	}
}

void SubFlow::thread_wrapper(void) {
	try {
		f_ptr(f_object, this, f_data);
	} catch(SubFlow::abort_exception abort) {
		// normal 'abort' case
		return;
	} catch(...) {
		// any other exception should be handled in the thread function
//		std::cerr << "fatal: thread with id == " << i_id " throws an unhandled exception !" << std::endl;
		std::terminate();
	}
}

void SubFlow::start(void) {
	if(i_threads_count == 1) {
		if(f_ptr)
			f_ptr(f_object, this, f_data);
		return;
	}

	if(sf_thread != nullptr) {
		sf_thread->join();
		delete sf_thread;
	}
	if(f_ptr != nullptr) {
		sf_thread = new std::thread( [=]{ thread_wrapper(); } );
	}
}

void SubFlow::abort() {
	if(i_id != 0 || i_threads_count == 1)
		return;
	std::unique_lock<std::mutex> lock(*m_lock);
	*b_flag_abort = true;
	lock.unlock();
	cv_in->notify_all();
	cv_out->notify_all();
}

void SubFlow::sync_point(void) {
	if(i_threads_count == 1)
		return;

	// pause if necessary
	std::unique_lock<std::mutex> lock(*m_lock);
	if(b_flag_pause->load() != 0)
		cv_pause->wait(lock, [this]{return b_flag_pause->load() == 0;});

	// wait till all threads leave barrier if any
	if(*b_counter_out != 0) {
		*b_flag_out = true;
		cv_out->wait(lock, [this]{return (*b_flag_out == false || *b_flag_abort);});
		if(i_id != 0 && *b_flag_abort)
			throw SubFlow::abort_exception();
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
	if(*b_flag_in == true) {
		cv_in->wait(lock, [this]{return (*b_flag_in == false || *b_flag_abort);});
		if(i_id != 0 && *b_flag_abort)
			throw SubFlow::abort_exception();
	}

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
	std::unique_lock<std::mutex> lock(*m_lock);

	// pause if necessary
	if(b_flag_pause->load() != 0)
		cv_pause->wait(lock, [this]{return b_flag_pause->load() == 0;});

	// wait till all threads leave barrier if any
	if(*b_counter_out != 0) {
		*b_flag_out = true;
		cv_out->wait(lock, [this]{return (*b_flag_out == false || *b_flag_abort);});
		if(i_id != 0 && *b_flag_abort)
			throw SubFlow::abort_exception();
	}

	// count in
	*b_flag_main_wakeup = false;
	*b_flag_in = true;
	++(*b_counter_in);
	if(*b_counter_in == i_threads_count) {
		// the last thread
		*b_counter_out = i_threads_count;
		if(i_id == 0) { // 'main' - wakeup all later
			lock.unlock();
			return true;
		}
		// not 'main' - wake up 'main'
		*b_flag_main_wakeup = true;
		lock.unlock();
		cv_main->notify_one();
		lock.lock();
	}
	if(i_id == 0) { // 'main' isn't last - sleep till wakeup by last thread
		cv_main->wait(lock, [this]{return *b_flag_main_wakeup == true;});
		lock.unlock();
		return true;
	}
	if(*b_flag_in == true) {
		cv_in->wait(lock, [this]{return (*b_flag_in == false || *b_flag_abort);});
		if(i_id != 0 && *b_flag_abort)
			throw SubFlow::abort_exception();
	}

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
