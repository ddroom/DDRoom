/*
 * import_test.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include "import_test.h"
#include "demosaic_pattern.h"
#include "ddr_math.h"
#include "cms_matrix.h"

#include <QPainter>

using namespace std;
//------------------------------------------------------------------------------
QList<QString> Import_Test::extensions(void) {
	QList<QString> l;
	l.push_back("ddr_test");
	return l;
}

Import_Test::Import_Test(string fname) {
	file_name = fname;
}

Area *Import_Test::image(Metadata *metadata) {
	QString fname = QString::fromLocal8Bit(file_name.c_str());
	if(fname.contains("-xy.ddr_test"))
		return test_xy(metadata);
	if(fname.contains("-demosaic.ddr_test"))
		return test_demosaic(metadata);
	return NULL;
}

Area *Import_Test::test_xy(Metadata *metadata) {
	int w = 1024;
	int h = 1024;
	metadata->width = w;
	metadata->height = h;
	CMS_Matrix::instance()->get_illuminant_XYZ("E", metadata->cRGB_illuminant_XYZ);
	Area *area = new Area(w, h);
	float *ptr = (float *)area->ptr();
	for(int y = 0; y < h; y++) {
		float fy = float(y) / h;
		for(int x = 0; x < w; x++) {
			float fx = float(x) / w;
			float alpha = 1.0;
			float XYZ[3];
			if(fy < 0.5) {
				XYZ[0] = 1.0 - fx;
				XYZ[1] = fy * 2.0;
				XYZ[2] = fx;
			} else {
				XYZ[1] = 1.0 - (fy - 0.5) * 2.0;
				if(fx > 0.5) {
					XYZ[0] = 0.0;
					XYZ[2] = (fx - 0.5) * 2.0;
				} else {
					XYZ[0] = 1.0 - fx * 2.0;
					XYZ[2] = 0.0;
				}
			}
			if(XYZ[0] < 0.0 || XYZ[1] < 0.0 || XYZ[2] < 0.0 || XYZ[0] > 1.0 || XYZ[1] > 1.0 || XYZ[2] > 1.0) {
				XYZ[0] = 0.0;
				XYZ[1] = 0.0;
				XYZ[2] = 0.0;
				alpha = 0.0;
			}
			ptr[((h - y - 1) * w + x) * 4 + 0] = XYZ[0];
			ptr[((h - y - 1) * w + x) * 4 + 1] = XYZ[1];
			ptr[((h - y - 1) * w + x) * 4 + 2] = XYZ[2];
			ptr[((h - y - 1) * w + x) * 4 + 3] = alpha;
		}
	}
	return area;
}

Area *Import_Test::test_demosaic(Metadata *metadata) {
	Area *in = test_draw(metadata);
	int w = metadata->width;
	int h = metadata->height;
	Area *out = new Area(w + 4, h + 4, Area::type_float_p1);
	metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_RGGB;
	metadata->demosaic_unknown = false;
	metadata->demosaic_unsupported = false;
//	int p_red = __bayer_red(metadata->demosaic_pattern);
	int p_green_r = __bayer_green_r(metadata->demosaic_pattern);
	int p_green_b = __bayer_green_b(metadata->demosaic_pattern);
	int p_blue = __bayer_blue(metadata->demosaic_pattern);
	float *ptr_in = (float *)in->ptr();
	float *ptr_out = (float *)out->ptr();
	for(int y = 0; y < h; y++) {
		for(int x = 0; x < w; x++) {
			int s = __bayer_pos_to_c(x, y);
			int a = 0;
			if(s == p_green_r || s == p_green_b)
				a = 1;
			if(s == p_blue)
				a = 2;
			int i_in = (y * w + x) * 4;
			int i_out = (y + 2) * (w + 4) + x + 2;
			ptr_out[i_out] = ptr_in[i_in + a];
		}
	}
	delete in;
	return out;
}

Area *Import_Test::test_draw(Metadata *metadata) {
	int w = 1024;
	int h = 1024;
	metadata->width = w;
	metadata->height = h;
	CMS_Matrix::instance()->get_illuminant_XYZ("D65", metadata->cRGB_illuminant_XYZ);
/*
	float m_srgb_to_xyz[9] = {
		 0.4124564,  0.3575761,  0.1804375,
		 0.2126729,  0.7151522,  0.0721750,
		 0.0193339,  0.1191920,  0.9503041};
*/
//	for(int i = 0; i < 9; i++)
//		metadata->cmatrix[i] = m_srgb_to_xyz[i];
	TableFunction *gamma = CMS_Matrix::instance()->get_inverse_gamma("sRGB");
	Area *area = new Area(w, h);
	// draw to image
	QImage image(QSize(w, h), QImage::Format_ARGB32_Premultiplied);
	QPainter p(&image);
	// linear gradient background
	QLinearGradient gradient(QPointF(0, 0), QPointF(w, 0));
	gradient.setColorAt(0, Qt::white);
	gradient.setColorAt(1, Qt::black);
	p.fillRect(QRect(0, 0, w, h), QBrush(gradient));
	// horizontal lines
	p.setRenderHint(QPainter::Antialiasing, true);
	float lw_a[] = {1.0, 1.5, 2.0, 2.5, 3.0};
	float offset = 0.0;
	QColor colors[] = {
		QColor(0x00, 0x00, 0x00, 0xFF),
		QColor(0xFF, 0x00, 0x00, 0xFF),
		QColor(0x00, 0xFF, 0x00, 0xFF),
		QColor(0x00, 0x00, 0xFF, 0xFF),
		QColor(0xFF, 0xFF, 0xFF, 0xFF),
	};
	int lines[] = {15, 12, 9, 6, 3};
	for(int l = 0; l < 5; l++) {
		for(int j = 0; j < 5; j++) {
			float lw = lw_a[j];
			p.setPen(QPen(colors[l], lw));
			for(int i = 0; i < lines[j]; i++) {
				float dx = w / 32.0;
				float off_x = 0;
				for(int k = 0; k < 32; k++) {
					p.drawLine(QPointF(off_x, offset), QPointF(off_x + dx, offset + (k % 4) * 2));
					off_x += dx;
				}
				offset += lw * 2.0;
			}
		}
	}
	// convert to Area
	float *area_ptr = (float *)area->ptr();
	for(int y = 0; y < h; y++) {
		for(int x = 0; x < w; x++) {
			float *ptr = &area_ptr[(y * w + x) * 4];
			QRgb rgb = image.pixel(x, y);
			ptr[0] = (*gamma)(float(qRed(rgb)) / 0xFF);
			ptr[1] = (*gamma)(float(qGreen(rgb)) / 0xFF);
			ptr[2] = (*gamma)(float(qBlue(rgb)) / 0xFF);
			ptr[3] = 1.0;
		}
	}
	return area;
}

//------------------------------------------------------------------------------
