/*
 * dataset.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "dataset.h"

using namespace std;
//------------------------------------------------------------------------------
dataset_field_t::dataset_field_t(void) {
	type = type_empty;
}

dataset_field_t::~dataset_field_t() {
	_clean();
}

dataset_field_t::dataset_field_t(const dataset_field_t &other) {
	if(&other == this)
		return;
	type = other.type;
	vString = other.vString;
	value = other.value;
	// --
	if(type == type_vector_float) {
		const QVector<float> *p = (QVector<float> *)other.value.v_ptr;
		QVector<float> *ptr = new QVector<float>(*p);
		value.v_ptr = (void *)ptr;
	}
	if(type == type_vector_qpointf) {
		const QVector<QPointF> *p = (QVector<QPointF> *)other.value.v_ptr;
		QVector<QPointF> *ptr = new QVector<QPointF>(*p);
		value.v_ptr = (void *)ptr;
	}
}

dataset_field_t &dataset_field_t::operator = (const dataset_field_t &other) {
	if(this != &other) {
		_clean();
		type = other.type;
		vString = other.vString;
		value = other.value;
		// --
		if(type == type_vector_float) {
			const QVector<float> *p = (QVector<float> *)other.value.v_ptr;
			QVector<float> *ptr = new QVector<float>(*p);
			value.v_ptr = (void *)ptr;
		}
		if(type == type_vector_qpointf) {
			const QVector<QPointF> *p = (QVector<QPointF> *)other.value.v_ptr;
			QVector<QPointF> *ptr = new QVector<QPointF>(*p);
			value.v_ptr = (void *)ptr;
		}
	}
	return *this;
}

bool dataset_field_t::operator == (const dataset_field_t &other) {
	if(this == &other)
		return true;
	if(type != other.type)
		return false;
	if(type == type_serialized || type == type_string)
		return (vString == other.vString);
	if(type == type_bool)
		return (value.vBool == other.value.vBool);
	if(type == type_int)
		return (value.vInt == other.value.vInt);
	if(type == type_2int)
		return ((value.v2int.value1 == other.value.v2int.value1) && (value.v2int.value2 == other.value.v2int.value2));
	if(type == type_double)
		return (value.vDouble == other.value.vDouble);
	if(type == type_2double)
		return ((value.v2double.value1 == other.value.v2double.value1) && (value.v2double.value2 == other.value.v2double.value2));
	if(type == type_vector_float)
		return ((*((QVector<float> *)value.v_ptr)) == (*((QVector<float> *)other.value.v_ptr)));
	if(type == type_vector_qpointf)
		return ((*((QVector<QPointF> *)value.v_ptr)) == (*((QVector<QPointF> *)other.value.v_ptr)));
	return true;
}

void dataset_field_t::_clean(void) {
	if(type == type_vector_float) {
		if(value.v_ptr != nullptr)
			delete (QVector<float> *)value.v_ptr;
		value.v_ptr = nullptr;
	}
	if(type == type_vector_qpointf) {
		if(value.v_ptr != nullptr)
			delete (QVector<QPointF> *)value.v_ptr;
		value.v_ptr = nullptr;
	}
}

void dataset_field_t::to_string(void) {
	type = type_string;
}

void dataset_field_t::to_bool(const bool &_value) {
	if(type == type_serialized) {
		type = type_bool;
		value.vBool = _value;
		if(vString == "true" || vString == "True" || vString == "TRUE" || vString == "1")
			value.vBool = true;
		if(vString == "false" || vString == "False" || vString == "FALSE" || vString == "0")
			value.vBool = false;
	}
}

void dataset_field_t::to_int(const int32_t &_value) {
	if(type == type_serialized) {
		type = type_int;
		bool rez_ok = false;
		QString str = vString.c_str();
		value.vInt = str.toInt(&rez_ok);
		if(rez_ok == false)
			value.vInt = _value;
	}
}

void dataset_field_t::to_2int(const int32_t &value1, const int32_t &value2) {
	if(type == type_serialized) {
		type = type_2int;
		QString str = vString.c_str();
		QStringList l = str.split(",");
		bool ok = false;
		if(l.size() == 2) {
			bool rez_ok = false;
			value.v2int.value1 = l[0].toInt(&rez_ok);
			ok = rez_ok;
			value.v2int.value2 = l[1].toInt(&rez_ok);
			ok = ok && rez_ok;
		}
		if(ok == false) {
			value.v2int.value1 = value1;
			value.v2int.value2 = value2;
		}
	}
}

void __normalize(double &value) {
	if(value < 1.0) {
		long long lv = (value + 0.00000005) * 10000000;
		value = (double)lv / double(10000000.0);
	}
}

void dataset_field_t::to_double(const double &_value) {
	if(type == type_serialized) {
		type = type_double;
		QString str = vString.c_str();
		bool rez_ok = false;
		value.vDouble = str.toDouble(&rez_ok);
		if(rez_ok) {
			__normalize(value.vDouble);
/*
			if(value.vDouble < 1.0) {
				long long lv = (value.vDouble + 0.00000005) * 10000000;
				value.vDouble = (double)lv / double(10000000.0);
			}
*/
		} else {
			value.vDouble = _value;
		}
	}
}

void dataset_field_t::to_2double(const double &value1, const double &value2) {
	if(type == type_serialized) {
		type = type_2int;
		QString str = vString.c_str();
		QStringList l = str.split(",");
		bool ok = false;
		if(l.size() == 2) {
			bool rez_ok = false;
//			value.v2double.value1 = l[0].toInt(&rez_ok);
			value.v2double.value1 = l[0].toDouble(&rez_ok);
			__normalize(value.v2double.value1);
			ok = rez_ok;
//			value.v2double.value1 = l[1].toInt(&rez_ok);
			value.v2double.value2 = l[1].toDouble(&rez_ok);
			__normalize(value.v2double.value2);
			ok = ok && rez_ok;
		}
		if(ok == false) {
			value.v2double.value1 = value1;
			value.v2double.value2 = value2;
		}
	}
}

void dataset_field_t::to_vector_float(const QVector<float> &_value) {
	if(type == type_serialized) {
		type = type_vector_float;
		QVector<float> *ptr = new QVector<float>;
		value.v_ptr = (void *)ptr;
		QStringList l = QString(vString.c_str()).split(",");
		while(l.size() >= 1) {
			bool ok = false;
			float v = (l.first()).toDouble(&ok);
			if(ok && v < 1.0) {
				long long lv = (v + 0.00000005) * 10000000;
				v = float (lv) / float(10000000.0);
			}
			l.pop_front();
			if(ok)
				ptr->push_back(v);
		}
	}
}

void dataset_field_t::to_vector_qpointf(const QVector<QPointF> &_value) {
	if(type == type_serialized) {
		type = type_vector_qpointf;
		QVector<QPointF> *ptr = new QVector<QPointF>;
		value.v_ptr = (void *)ptr;
		QStringList l = QString(vString.c_str()).split(",");
		while(l.size() >= 2) {
			bool ok_x, ok_y;
			float x = (l.first()).toFloat(&ok_x);
			l.pop_front();
			float y = (l.first()).toFloat(&ok_y);
			l.pop_front();
			if(ok_x && ok_y)
				ptr->push_back(QPointF(x, y));
		}
	}
}

QString __double_to_qstring(double v) {
	QString str;
	int precision = 10;
	if(v <= 1.0 && v > -1.0) {
		long long lv = (v + 0.00000005) * 10000000;
		v = (double)lv / double(10000000.0);
	} else {
		precision = 8;
		double v2 = (v > 0) ? v : -v;
		while(v2 > 10.0) {
			precision--;
			v2 /= 10.0;
		}
		if(precision < 0)
			precision = 0;
	}
	return str.setNum(v, 'g', 10);
}

string dataset_field_t::serialize(void) const {
	if(type == type_empty)	return "";
	if(type == type_serialized || type == type_string)	return vString;
	if(type == type_bool) {
		if(value.vBool)	return "true";
		else			return "false";
	}
	if(type == type_int) {
		QString str;
		return str.setNum(value.vInt).toLocal8Bit().constData();
	}
	if(type == type_2int) {
		QString str = QString::number(value.v2int.value1) + ", " + QString::number(value.v2int.value2);
		return str.toLocal8Bit().constData();
	}
	if(type == type_double) {
		return __double_to_qstring(value.vDouble).toLocal8Bit().constData();
/*
		QString str;
		double v = value.vDouble;
		int precision = 10;
		if(v <= 1.0 && v > -1.0) {
			long long lv = (v + 0.00000005) * 10000000;
			v = (double)lv / double(10000000.0);
		} else {
			precision = 8;
			double v2 = (v > 0) ? v : -v;
			while(v2 > 10.0) {
				precision--;
				v2 /= 10.0;
			}
			if(precision < 0)
				precision = 0;
		}
		return str.setNum(v, 'g', 10).toLocal8Bit().constData();
*/
/*
		cerr << "convert of " << v << " to " << str.setNum(v, 'g', precision).toLocal8Bit().constData() << " with precision " << precision << endl;
		QString str2;
		return str2.setNum(v, 'g', precision).toLocal8Bit().constData();
*/
	}
	if(type == type_2double) {
		QString str = __double_to_qstring(value.v2double.value1) + ", " + __double_to_qstring(value.v2double.value2);
		return str.toLocal8Bit().constData();
	}
	if(type == type_vector_float) {
		QVector<float> &ptr = *((QVector<float> *)value.v_ptr);
		QString rez, str;
		for(int i = 0; i < ptr.size(); ++i) {
			str = "";
			float v = ptr[i];
			if(v <= 1.00001) {
				long long lv = (v + 0.00000005) * 10000000;
				v = lv;
				v /= float(10000000.0);
			}
			str.setNum(v, 'g', 10).toLocal8Bit().constData();
			str += ", ";
			rez += str;
		}
		rez.remove(rez.size() - 2, 2);	// remove last ", "
		return rez.toLocal8Bit().constData();
	}
	if(type == type_vector_qpointf) {
		QVector<QPointF> &ptr = *((QVector<QPointF> *)value.v_ptr);
		QString rez, str;
		for(int i = 0; i < ptr.size(); ++i) {
			float x = ptr[i].x();
			float y = ptr[i].y();
			str = "%1, ";
			rez += str.arg(x + 0.000005, 0, 'f', 4);
			rez += str.arg(y + 0.000005, 0, 'f', 4);
		}
		rez.remove(rez.size() - 2, 2);
		return rez.toLocal8Bit().constData();
	}
	return "";
// toLower()
}

//------------------------------------------------------------------------------
DataSet::DataSet(void) {
}

void DataSet::_dump(void) {
	cerr << "---- DataSet: dump:" << endl;
	for(map<string, dataset_field_t>::const_iterator it = dataset_fields.begin(); it != dataset_fields.end(); ++it) {
		cerr << "\"" << (*it).first << "\" == \"";
		cerr << (*it).second.serialize();
		cerr << "\"" << endl;
	}
	cerr << "---- DataSet: dump - done" << endl;
}

std::string DataSet::serialize(void) {
	string rez;
	for(map<string, dataset_field_t>::const_iterator it = dataset_fields.begin(); it != dataset_fields.end(); ++it) {
		rez += (*it).first;
		rez += "=";
		rez += (*it).second.serialize();
		rez += "\r\n";
	}
	return rez;
}

//------------------------------------------------------------------------------
list<class field_delta_t> DataSet::get_fields_delta(DataSet *before, DataSet *after) {
	list<class field_delta_t> l;
	map<string, dataset_field_t> &m_before = *before->get_dataset_fields();
	map<string, dataset_field_t> &m_after = *after->get_dataset_fields();
	for(map<string, dataset_field_t>::iterator it = m_before.begin(); it != m_before.end(); ++it) {
		map<string, dataset_field_t>::iterator it_after = m_after.find((*it).first);
		if(it_after != m_after.end()) {
			dataset_field_t &d_before = (*it).second;
			dataset_field_t &d_after = (*it_after).second;
			if(!(d_before == d_after)) {
				field_delta_t d;
				d.field_name = (*it).first;
				d.field_before = d_before;
				d.field_after = d_after;
				l.push_back(d);
			}
		}
	}
	return l;
}

void DataSet::apply_fields_delta(const std::list<field_delta_t> *fields_delta, bool to_state_before) {
	for(list<field_delta_t>::const_iterator it = fields_delta->begin(); it != fields_delta->end(); ++it) {
//		cerr << "to_state_before == " << to_state_before << endl;
//		cerr << "field: \"" << (*it).field_name << "\" == " << "\"" << dataset_fields[(*it).field_name].serialize() << endl;
//		cerr << "delta before == \"" << (*it).field_before.serialize() << "\"" << endl;
//		cerr << "       after == \"" << (*it).field_after.serialize() << "\"" << endl;
		if(to_state_before)
			dataset_fields[(*it).field_name] = (*it).field_before;
		else
			dataset_fields[(*it).field_name] = (*it).field_after;
//		cerr << "       \"" << (*it).field_name << "\" == " << "\"" << dataset_fields[(*it).field_name].serialize() << endl;
//cerr << endl;
	}
}

//------------------------------------------------------------------------------
std::map<std::string, dataset_field_t> *DataSet::get_dataset_fields(void) {
	return &dataset_fields;
}

bool DataSet::get(string name, bool &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_bool(value);
	if((*it).second.type != dataset_field_t::type_bool)	return false;
	value = (*it).second.value.vBool;
	return true;
}

bool DataSet::get(string name, int32_t &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_int(value);
	if((*it).second.type != dataset_field_t::type_int)	return false;
	value = (*it).second.value.vInt;
	return true;
}

bool DataSet::get(string name, int32_t &value1, int32_t &value2) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_2int(value1, value2);
	if((*it).second.type != dataset_field_t::type_2int)	return false;
	value1 = (*it).second.value.v2int.value1;
	value2 = (*it).second.value.v2int.value2;
	return true;
}

bool DataSet::get(string name, double &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_double(value);
	if((*it).second.type != dataset_field_t::type_double)	return false;
	value = (*it).second.value.vDouble;
	return true;
}

bool DataSet::get(string name, double &value1, double &value2) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_2double(value1, value2);
	if((*it).second.type != dataset_field_t::type_2double)	return false;
	value1 = (*it).second.value.v2double.value1;
	value2 = (*it).second.value.v2double.value2;
	return true;
}

bool DataSet::get(string name, string &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_string();
	if((*it).second.type != dataset_field_t::type_string)	return false;
	value = (*it).second.vString;
	return true;
}

bool DataSet::get(string name, QVector<float> &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_vector_float(value);
	if((*it).second.type != dataset_field_t::type_vector_float)	return false;
	QVector<float> *ptr = (QVector<float> *)(*it).second.value.v_ptr;
	value = *ptr;
	return true;
}

bool DataSet::get(string name, QVector<QPointF> &value) {
	map<string, dataset_field_t>::iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	if((*it).second.type == dataset_field_t::type_serialized)	(*it).second.to_vector_qpointf(value);
	if((*it).second.type != dataset_field_t::type_vector_qpointf)	return false;
	QVector<QPointF> *ptr = (QVector<QPointF> *)(*it).second.value.v_ptr;
	value = *ptr;
	return true;
}

bool DataSet::get_type(string name, dataset_field_t::type_en &type) const {
	map<string, dataset_field_t>::const_iterator it = dataset_fields.find(name);
	if(it == dataset_fields.end())	return false;
	type = (*it).second.type;
	return true;
}

bool DataSet::is_present(string name) const {
	if(dataset_fields.find(name) != dataset_fields.end())
		return true;
	return false;
}

void DataSet::set(string name, const bool &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_bool;
	t.value.vBool = value;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const int32_t &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_int;
	t.value.vInt = value;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const int32_t &value1, const int32_t &value2) {
	dataset_field_t t;
	t.type = dataset_field_t::type_2int;
	t.value.v2int.value1 = value1;
	t.value.v2int.value2 = value2;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const double &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_double;
	t.value.vDouble = value;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const double &value1, const double &value2) {
	dataset_field_t t;
	t.type = dataset_field_t::type_2double;
	t.value.v2double.value1 = value1;
	t.value.v2double.value2 = value2;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const char *value) {
	string str = value;
	set(name, str);
}

void DataSet::set(string name, const string &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_string;
	t.vString = value;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const QVector<float> &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_vector_float;
	QVector<float> *ptr = new QVector<float>(value);
	t.value.v_ptr = (void *)ptr;
	dataset_fields[name] = t;
}

void DataSet::set(string name, const QVector<QPointF> &value) {
	dataset_field_t t;
	t.type = dataset_field_t::type_vector_qpointf;
	QVector<QPointF> *ptr = new QVector<QPointF>(value);
	t.value.v_ptr = (void *)ptr;
	dataset_fields[name] = t;
}

//------------------------------------------------------------------------------
