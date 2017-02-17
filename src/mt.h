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
	std::condition_variable w_m_lock;
	std::condition_variable w_s_lock;
	std::condition_variable w_jobs;
	std::mutex m_lock;
	std::mutex m_jobs;
	std::atomic_int c_lock;
	std::atomic_int c_jobs;
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

	virtual void wait(void);
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
class SubFlow_Thread : public SubFlow {
	friend class Flow;

public:
	virtual ~SubFlow_Thread();
	void sync_point(void);
	bool sync_point_pre(void);
	void sync_point_post(void);

	void wait(void);
	void start(void);

protected:
	SubFlow_Thread(Flow *parent, bool master, int cores);
//	void run(void);

	std::thread *std_thread = nullptr;
	std::condition_variable *w_m_lock;
	std::condition_variable *w_s_lock;
	std::condition_variable *w_jobs;
	std::mutex *m_lock;
	std::mutex *m_jobs;
	std::atomic_int *c_lock;
	std::atomic_int *c_jobs;
};

//------------------------------------------------------------------------------
#endif // __H_MT__
