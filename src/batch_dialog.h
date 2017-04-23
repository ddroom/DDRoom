#ifndef __H_BATCH_DIALOG__
#define __H_BATCH_DIALOG__
/*
 * batch_dialog.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <string>
#include <QtWidgets>

#include "export.h"

//------------------------------------------------------------------------------
class Batch_Dialog : public QDialog {
	Q_OBJECT

public:
	Batch_Dialog(export_parameters_t *_ep, QWidget *parent = nullptr);
	void set_file_name(std::string file_name);
	void set_folder(std::string folder);

protected slots:
	void slot_image_type_clicked(int id);
	void slot_button_folder_pressed(void);
	void slot_jpeg_iq(double _value);
	void slot_jpeg_subsampling(int id);
	void slot_jpeg_color_space(int id);
	void slot_png_bits(int id);
	void slot_png_alpha(int checked);
	void slot_tiff_bits(int id);
	void slot_tiff_alpha(int checked);
	void slot_line_file_name(void);
	void slot_process_asap(int checked);
	void slot_scaling_enable(int checked);
	void slot_scale_fit_radio(int index);
	void slot_line_scaling_width(void);
	void slot_line_scaling_height(void);
	//
	void slot_button_ok(void);
	void slot_line_folder(void);

protected:
	QLineEdit *line_file_name;
	QLineEdit *line_folder;
	QLineEdit *line_scaling_width;
	QLineEdit *line_scaling_height;
	QLabel *label_scaling_width;
	QLabel *label_scaling_height;
	QButtonGroup *scale_fit_radio;
	QToolButton *b_size_fit;
	QLabel *l_size_fit;
	QToolButton *b_size_fill;
	QLabel *l_size_fill;

	QRadioButton *rb_jpeg_subsampling_22;
	QRadioButton *rb_jpeg_subsampling_11;
	QButtonGroup *rb_jpeg_subsampling;
	QLabel *label_jpeg_subsampling;
	void normalize_jpeg_subsampling(void);

	QTabWidget *tab_type;

	export_parameters_t *ep;

	QStackedWidget *stack_type;
	QWidget *tab_jpeg;
	QWidget *tab_png;
	QWidget *tab_tiff;
};

//------------------------------------------------------------------------------
#endif // __H_BATCH_DIALOG__
