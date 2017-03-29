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
#include <vector>
#include <map>

/*
 'Flow' presents a multithreaded working unit, and each of threads threads have
 access to the 'SubFlow' object that represent it. The first thread is a 'main'
 thread and only it should allocate resources and split tasks between subflows.
*/
//------------------------------------------------------------------------------
class Flow {
public:
	// Priorities for the ::flow() call - all flows with the lower priority will be paused
	// on the ::flow() start and resumed on the return; and the call will be paused if there are other 
	enum priority_t {
		priority_UI = 0,
		priority_online_interactive,
		priority_online_open,
		priority_offline,
		priority_lowest
	};

	Flow(Flow::priority_t priority, void (*f_ptr)(void *obj, class SubFlow *subflow, void *data), void *f_object, void *f_data, int _threads_count = 0);
	virtual ~Flow(void);
	void flow(void);

protected:
	friend class SubFlow;
	void set_private(void **priv_array);
	void set_private(void *priv_data, int thread_index);
	void *get_private(int thread_index);

	// priority
protected:
	const priority_t _priority;

	static void run_flows_add(Flow *);
	static void run_flows_remove(Flow *);
	static void rerun_flows(void);
	static std::mutex run_flows_lock;
	static std::multimap<priority_t, Flow *> run_flows;

	void pause(void);
	void resume(void);
	// covered with mutex 'm_lock'
	std::atomic_int b_flag_pause;
	std::condition_variable cv_pause;

	// sync point
protected:
	std::vector<class SubFlow *>subflows = std::vector<class SubFlow *>(0);
	int threads_count;

	std::mutex m_lock;
	std::condition_variable cv_in;
	std::condition_variable cv_out;
	std::condition_variable cv_main;
	int b_counter_in = 0;
	int b_counter_out = 0;
	bool b_flag_in = false;
	bool b_flag_out = false;
	bool b_flag_main_wakeup = false;
	bool b_flag_abort = false;
};

//------------------------------------------------------------------------------
class SubFlow {
	friend class Flow;

public:
	SubFlow(Flow *parent, int _id, int threads_count);
	virtual ~SubFlow();

	bool is_main(void) const {return _main;}
	int threads_count(void) const {return i_threads_count;}
	int id(void) const {return i_id;} // for debug purposes

	// Some data that should be shared between all of the threads.
	void set_private(void **priv_array);
	void set_private(void *priv_data, int thread_index);
	void *get_private(void);
	// Will return pointer on the 'priv_data' only for a call from main thread.
	void *get_private(int thread_index);

	// Abort all threads but the 'main', will ignore calls from any subflow but 'main',
	// abort will be done at or in the exit of the sync_point[_pre|_post] calls.
	// Useful for 'OOM' exceptions handling etc..
	void abort(void);
	// 'classic' barrier w/o any priority
	void sync_point(void);
	// splitted barrier
	// will return 'true' for the 'main' thread, which will return ASAP, and 'false' for all other
	bool sync_point_pre(void);
	// 'main' thread will wakeup all other threads, and those will do nothing in here
	void sync_point_post(void);

	void wait(void);
	void start(void);

protected:
	class abort_exception {};
	std::thread *sf_thread = nullptr;
	void (*f_ptr)(void *, class SubFlow *, void *);
	void *f_object;
	void *f_data;
	// Wrapper for thread function to intercept 'abort' exception
	// and finish thread's execution in a correct way.
	void thread_wrapper(void);

	Flow *const _parent;
	bool _main;
	const int i_id;
	const int i_threads_count;
	void *_target_private = nullptr;

	// shared barrier objects from the parent Flow object
	std::mutex *m_lock = nullptr;
	std::condition_variable *cv_in = nullptr;
	std::condition_variable *cv_out = nullptr;
	std::condition_variable *cv_main = nullptr;
	int *b_counter_in = nullptr;
	int *b_counter_out = nullptr;
	bool *b_flag_in = nullptr;
	bool *b_flag_out = nullptr;
	bool *b_flag_main_wakeup = nullptr;
	bool *b_flag_abort = nullptr;

	std::condition_variable *cv_pause = nullptr;
	std::atomic_int *b_flag_pause = nullptr;
};

//------------------------------------------------------------------------------
#endif // __H_MT__
