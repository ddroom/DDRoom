/*
 * gui_curve_histogram.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <math.h>

#include "gui_curve_histogram.h"
//#include "ddr_math.h"
//#include "system.h"

using namespace std;

//------------------------------------------------------------------------------
void GUI_Curve_Histogram::draw_histogram(QPainter *painter, int size_w, int size_h) {
	int y_max = size_h - 1;
	int ry_2 = 0.50 * y_max;
	int ry_4 = 1.00 * y_max;
	bool show_hist_before = false;
	bool show_hist_after = false;
	QVector<long> *hist_ptr[2] = {nullptr, nullptr};
	int hist_size = size_w;
	data_lock.lock();
	if(data != nullptr) {
		if(lightness_only) {
			if(data->hist_before_scaled.size() != size_w)
				data->hist_before_scaled = GUI_Histogram::rescale_histogram(data->hist_before, size_w);
			if(data->hist_after_scaled.size() != size_w)
				data->hist_after_scaled = GUI_Histogram::rescale_histogram(data->hist_after, size_w);
		} else {
			QVector<long> *v_unscaled[2];
			v_unscaled[0] = &data->hist_before;
			v_unscaled[1] = &data->hist_after;
			QVector<long> *v_scaled[2];
			v_scaled[0] = &data->hist_before_scaled;
			v_scaled[1] = &data->hist_after_scaled;
			for(int k = 0; k < 2; ++k) {
				if(v_scaled[k]->size() != size_w * 3) {
					int size = v_unscaled[k]->size() / 3;
					*v_scaled[k] = QVector<long>(size_w * 3);
					QVector<long> v_in;
					v_in = QVector<long>(size);
					QVector<long> v_out;
					v_out = QVector<long>(hist_size);
					for(int j = 0; j < 3; ++j) {
						for(int i = 0; i < size; ++i)
							v_in[i] = (*v_unscaled[k])[j * size + i];
						v_out = GUI_Histogram::rescale_histogram(v_in, size_w);
						for(int i = 0; i < size_w; ++i)
							(*v_scaled[k])[j * size_w + i] = v_out[i];
					}
				}
			}
		}
		hist_ptr[0] = &data->hist_before_scaled;
		hist_ptr[1] = &data->hist_after_scaled;
		show_hist_before = data->show_hist_before;
		show_hist_after = data->show_hist_after;
	}
	bool hist_show[2] = {show_hist_before, show_hist_after};
	int hist_dy[2] = {0, 0};
	int hist_h[2] = {size_h - 1, size_h - 1};
	if(show_hist_before && show_hist_after) {
		hist_h[0] = ry_4 - ry_2 - 1;
		hist_h[1] = ry_2 - 1;
		hist_dy[0] = ry_2 + 1;
		hist_dy[1] = 0;
	}
	painter->setOpacity(1.0);
	painter->setCompositionMode(QPainter::CompositionMode_Plus);
	for(int j = 0; j < 2; ++j) {
		if(hist_ptr[j] == nullptr)
			continue;
		QVector<long> &hist = *hist_ptr[j];
		int dy = hist_dy[j];
		if(hist.size() != 0 && hist_show[j]) {
			int channels_count = 3;
			if(lightness_only)
				channels_count = 1;
			float s = hist_h[j];
			long max = hist[1];
			for(int i = 0; i < channels_count; ++i) {
				for(int j = 1; j < hist_size - 1; ++j) {
					long value = hist[i * hist_size + j];
					if(value > max)
						max = value;
				}
			}
			float _max = max;
			if(!data->show_hist_linear)
				_max = logf(_max);
			// depend on channels type
			QColor colors[3] = {QColor(0x9F, 0x00, 0x00), QColor(0x00, 0x9F, 0x00), QColor(0x00, 0x00, 0x9F)};
			if(lightness_only)
				colors[0] = QColor(0x7F, 0x7F, 0x7F);
			for(int k = 0; k < channels_count; ++k) {
				painter->setPen(colors[k]);
				for(int i = 0; i < hist_size; ++i) {
					float _v = hist[k * hist_size + i];
					if(!data->show_hist_linear)
						_v = logf(_v);
					_v /= _max;
					_v *= s;
					int l = (int)(_v + 0.05);
					if(l > s)	l = s;
					if(l > 0)
						painter->drawLine(QLineF(i, dy, i, dy + l));
				}
			}
		}
	}
	data_lock.unlock();
}

GUI_Curve_Histogram_data::GUI_Curve_Histogram_data(void) {
	hist_before = QVector<long>(0);
	hist_after = QVector<long>(0);
	hist_before_scaled = QVector<long>(0);
	hist_after_scaled = QVector<long>(0);
	show_hist_before = true;
	show_hist_after = true;
	show_hist_linear = true;
}

GUI_Curve_Histogram::GUI_Curve_Histogram(bool _lightness_only) {
	data = nullptr;
	lightness_only = _lightness_only;
}

void GUI_Curve_Histogram::set_data_object(GUI_Curve_Histogram_data *_data) {
	data_lock.lock();
	data = _data;
	data_lock.unlock();
//cerr << "--->>>                 set_data_object: " << (unsigned long)_data << endl;
}

void GUI_Curve_Histogram::set_histograms(GUI_Curve_Histogram_data *_data, const QVector<long> &before, const QVector<long> &after) {
	bool flag = false;
	data_lock.lock();
	flag = (data == _data);
	if(_data != nullptr) {
		_data->hist_before = before;
		_data->hist_after = after;
		_data->hist_before_scaled = QVector<long>(0);
		_data->hist_after_scaled = QVector<long>(0);
	}
	data_lock.unlock();
	if(flag)
		emit signal_update();
}

void GUI_Curve_Histogram::set_settings(bool _show_hist_before, bool _show_hist_after, bool _show_hist_linear) {
	data_lock.lock();
	if(data != nullptr) {
		data->show_hist_before = _show_hist_before;
		data->show_hist_after = _show_hist_after;
		data->show_hist_linear = _show_hist_linear;
	}
	data_lock.unlock();
}

//------------------------------------------------------------------------------
