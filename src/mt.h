#ifndef __H_MT__
#define __H_MT__
/*
 * mt.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

//------------------------------------------------------------------------------
class Flow {
	friend class SubFlow;

public:
	Flow(void (*f_ptr)(void *obj, class SubFlow *subflow, void *data), void *f_object, void *f_data, int _threads_count = 0);
	virtual ~Flow(void);
	void flow(void);

protected:
	friend class SubFlow;
	void set_private(void **priv_array);

protected:
	class SubFlow **subflows;
	int threads_count;

	std::mutex m_lock;
	std::condition_variable cv_in;
	std::condition_variable cv_out;
	std::condition_variable cv_master;
	int b_counter_in = 0;
	int b_counter_out = 0;
	bool b_flag_in = false;
	bool b_flag_out = false;
	bool b_flag_master_wakeup = false;
};

//------------------------------------------------------------------------------
class SubFlow {
	friend class Flow;

public:
	SubFlow(Flow *parent, int _id, int threads_count);
	virtual ~SubFlow();

	bool is_master(void) {return _master;}
	int threads_count(void) {return i_threads_count;}
	int id(void) {return i_id;} // for debug purposes

	// Some data that should be shared between all of the threads.
	void set_private(void **priv_array);
	void *get_private(void);

	// 'classic' barrier w/o any priority
	void sync_point(void);
	// splitted barrier
	// will return 'true' for the master thread, which will return ASAP, and 'false' for all other
	bool sync_point_pre(void);
	// master thread will wakeup all other threads, and those will do nothing in here
	void sync_point_post(void);

	void wait(void);
	void start(void);

protected:
	std::thread *std_thread = nullptr;
	void (*f_ptr)(void *, class SubFlow *, void *);
	void *f_object;
	void *f_data;

	Flow *const _parent;
	bool _master;
	const int i_id;
	const int i_threads_count;
	void *_target_private = nullptr;

	// shared barrier objects from the parent Flow object
	std::mutex *m_lock = nullptr;
	std::condition_variable *cv_in = nullptr;
	std::condition_variable *cv_out = nullptr;
	std::condition_variable *cv_master = nullptr;
	int *b_counter_in = nullptr;
	int *b_counter_out = nullptr;
	bool *b_flag_in = nullptr;
	bool *b_flag_out = nullptr;
	bool *b_flag_master_wakeup = nullptr;
};

//------------------------------------------------------------------------------
#endif // __H_MT__
