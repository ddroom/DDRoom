#ifndef __H_SHARED_PTR__
#define __H_SHARED_PTR__
/*
 * shared_ptr.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


// NOTE: should be used flag '-frepo' for compilation with gcc

//------------------------------------------------------------------------------
template <class T> class ddr_shared_ptr<T>::ddr_shared_ptr_t {
public:
	long counter;
	QMutex mutex;
	void _free(void);
};

template <class T> ddr_shared_ptr<T>::ddr_shared_ptr(void) {
	_ptr = NULL;
	_ptr_c = NULL;
}

template <class T> void ddr_shared_ptr<T>::_free(void) {
	if(_ptr_c != NULL) {
		_ptr_c->mutex.lock();
		_ptr_c->counter--;
		if(_ptr_c->counter == 0) {
			if(_ptr != NULL)
				delete _ptr;
			_ptr_c->mutex.unlock();
			delete _ptr_c;
		} else {
			_ptr_c->mutex.unlock();
		}
		_ptr_c = NULL;
	}
	_ptr = NULL;
}

template <class T> ddr_shared_ptr<T>::ddr_shared_ptr(T *ptr) {
//	*this = ptr;
//	this->_free();
//	if(ptr != NULL) {
		_ptr = ptr;
		_ptr_c = new ddr_shared_ptr_t();
		_ptr_c->mutex.lock();
		_ptr_c->counter = 1;
		_ptr_c->mutex.unlock();
//	}
}

template <class T> ddr_shared_ptr<T>::ddr_shared_ptr(const ddr_shared_ptr<T> &other) {
	_ptr = other._ptr;
	_ptr_c = other._ptr_c;
	if(_ptr_c != NULL) {
		_ptr_c->mutex.lock();
		_ptr_c->counter++;
		_ptr_c->mutex.unlock();
	}
}

template <class T> ddr_shared_ptr<T> & ddr_shared_ptr<T>::operator = (const ddr_shared_ptr<T> &other) {
	if(this != &other) {
		this->_free();
		_ptr = other._ptr;
		_ptr_c = other._ptr_c;
		if(_ptr_c != NULL) {
			_ptr_c->mutex.lock();
			_ptr_c->counter++;
			_ptr_c->mutex.unlock();
		}
	}
	return *this;
}

//template <class T> ddr_shared_ptr<T> & ddr_shared_ptr<T>::operator = (const T *ptr) {
template <class T> ddr_shared_ptr<T> & ddr_shared_ptr<T>::operator = (T *ptr) {
//	if(ptr != NULL) {
		this->_free();
		_ptr = ptr;
		_ptr_c = new ddr_shared_ptr_t();
		_ptr_c->mutex.lock();
		_ptr_c->counter = 1;
		_ptr_c->mutex.unlock();
//	}
	return *this;
}

template <class T> T & ddr_shared_ptr<T>::operator *(void) const {
	return *_ptr;
}

template <class T> T * ddr_shared_ptr<T>::operator ->(void) const {
	return _ptr;
}

template <class T> T *ddr_shared_ptr<T>::ptr(void) const {
	return _ptr;
}

template <class T> bool ddr_shared_ptr<T>::isNull(void) const {
	return (_ptr == NULL);
}
//------------------------------------------------------------------------------

#endif //__H_SHARED_PTR__
