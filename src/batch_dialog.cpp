/*
 * batch_dialog.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include "batch_dialog.h"
#include "gui_slider.h"

#include <fstream>
#include <iostream>

#include <zlib.h>
#include <jpeglib.h>

using namespace std;
//------------------------------------------------------------------------------
Batch_Dialog::Batch_Dialog(export_parameters_t *_ep, QWidget *parent) : QDialog(parent) {
	ep = _ep;

	if(ep->process_single)
		setWindowTitle(tr("Save active photo as"));
	else
		setWindowTitle(tr("Process and save selected photos"));
	setModal(true);
	setMinimumWidth(700);
	setMaximumWidth(QApplication::desktop()->width());
	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

	QString type_name_jpeg = QString(tr("JPEG"));
	QString type_name_png = QString(tr("PNG"));
	QString type_name_tiff = QString(tr("TIFF"));

	QVBoxLayout *layout_main = new QVBoxLayout(this);
	QGroupBox *gb_file_opts = new QGroupBox(tr("File options"));
	QGridLayout *layout_save_opts = new QGridLayout(gb_file_opts);

	// create widgets
	int row = 0;
	if(ep->process_single) {
		QLabel *label_name = new QLabel(tr("Save with name"));
		layout_save_opts->addWidget(label_name, row++, 0, Qt::AlignRight);
	}
	QLabel *label_folder = new QLabel(tr("Destination folder"));
	layout_save_opts->addWidget(label_folder, row++, 0, Qt::AlignRight);

	// file options
	// file name
	row = 0;
	if(ep->process_single)
		line_file_name = new QLineEdit();
	else
		line_file_name = nullptr;
	// destination folder
	QHBoxLayout *hb_folder = new QHBoxLayout();
	line_folder = new QLineEdit();
	QPushButton *button_folder = new QPushButton();
	button_folder->setText(tr("Browse..."));
	hb_folder->addWidget(line_folder);
	hb_folder->addWidget(button_folder, 0, Qt::AlignRight);
	// asap
	QCheckBox *check_process_asap = new QCheckBox(tr("Process photo as soon as possible"));
	// --==--
	if(ep->process_single) {
		layout_save_opts->addWidget(line_file_name, row++, 1);
	}
	layout_save_opts->addLayout(hb_folder, row++, 1, Qt::AlignLeft);
	layout_save_opts->addWidget(check_process_asap, row++, 0, 1, 2);
	
	layout_main->addWidget(gb_file_opts);
	
	//-------------------------------
	// pages for image type paramaters (in alphabetical order)
	// image type radio
	QHBoxLayout *hb_type = new QHBoxLayout();
	QLabel *label_type = new QLabel(tr("Image type: "));
	QRadioButton *radio_type_jpeg = new QRadioButton(type_name_jpeg);
	QRadioButton *radio_type_png = new QRadioButton(type_name_png);
	QRadioButton *radio_type_tiff = new QRadioButton(type_name_tiff);
	QButtonGroup *radio_type = new QButtonGroup(hb_type);
	radio_type->addButton(radio_type_jpeg, export_parameters_t::image_type_jpeg);
	radio_type->addButton(radio_type_png, export_parameters_t::image_type_png);
	radio_type->addButton(radio_type_tiff, export_parameters_t::image_type_tiff);
	hb_type->addWidget(label_type);
	hb_type->addWidget(radio_type_jpeg);
	hb_type->addWidget(radio_type_png);
	hb_type->addWidget(radio_type_tiff);
	hb_type->addStretch();

	QGroupBox *gb_type = new QGroupBox(tr("Image type parameters"));
	QVBoxLayout *vb_type = new QVBoxLayout(gb_type);
	stack_type = new QStackedWidget();
	vb_type->addLayout(hb_type);
	vb_type->addWidget(stack_type);
	
	// JPEG
//	tab_jpeg = new QWidget(this);
	tab_jpeg = new QGroupBox(tr("JPEG parameters"));

	QVBoxLayout *l_jpeg = new QVBoxLayout(tab_jpeg);
	l_jpeg->setSizeConstraint(QLayout::SetMinimumSize);	

	QHBoxLayout *hb_jpeg_iq = new QHBoxLayout();
	QLabel *label_jpeg_iq = new QLabel();
	label_jpeg_iq->setText(tr("Image quality:"));
	hb_jpeg_iq->addWidget(label_jpeg_iq);
	GuiSlider *slider_jpeg_iq = new GuiSlider(0.0, 100.0, ep->t_jpeg_iq, 1, 1, 5);
	hb_jpeg_iq->addWidget(slider_jpeg_iq);
	QLabel *label_jpeg_iq_percent = new QLabel();
	label_jpeg_iq_percent->setText(tr("%"));
	hb_jpeg_iq->addWidget(label_jpeg_iq_percent);
	l_jpeg->addLayout(hb_jpeg_iq);

	QGridLayout *gl_jpeg_color = new QGridLayout();
//	gl_jpeg_color->setSpacing(1);
//	gl_jpeg_color->setContentsMargins(2, 1, 2, 1);
	gl_jpeg_color->setSizeConstraint(QLayout::SetMinimumSize);
	l_jpeg->addLayout(gl_jpeg_color);
	int gl_jpeg_color_row = 0;

	// color space: YCbCr or RGB
	QHBoxLayout *lb_color_space = new QHBoxLayout();
	QLabel *label_color_space = new QLabel(tr("Color space:"));
	QRadioButton *radio_color_space_ycbcr = new QRadioButton(tr("YCbCr"));
	QRadioButton *radio_color_space_rgb = new QRadioButton(tr("RGB"));
	QButtonGroup *radio_jpeg_color_space = new QButtonGroup(lb_color_space);
	radio_jpeg_color_space->addButton(radio_color_space_ycbcr, 0);
	radio_jpeg_color_space->addButton(radio_color_space_rgb, 1);
	lb_color_space->addWidget(radio_color_space_ycbcr);
	lb_color_space->addWidget(radio_color_space_rgb);
	lb_color_space->addStretch();
	gl_jpeg_color->addWidget(label_color_space, gl_jpeg_color_row, 0, Qt::AlignRight | Qt::AlignTop);
	gl_jpeg_color->addLayout(lb_color_space, gl_jpeg_color_row++, 1);
	if(ep->t_jpeg_color_space_rgb == 0)		radio_color_space_ycbcr->setChecked(true);
	if(ep->t_jpeg_color_space_rgb == 1)		radio_color_space_rgb->setChecked(true);

	// color subsampling: 2x2 or 1x1
	QHBoxLayout *lb_jpeg_subsampling = new QHBoxLayout();
	label_jpeg_subsampling = new QLabel(tr("Color subsampling:"));
	rb_jpeg_subsampling_11 = new QRadioButton(tr("1x1"));
	rb_jpeg_subsampling_22 = new QRadioButton(tr("2x2"));
	rb_jpeg_subsampling = new QButtonGroup(lb_jpeg_subsampling);
	rb_jpeg_subsampling->addButton(rb_jpeg_subsampling_11, 1);
	rb_jpeg_subsampling->addButton(rb_jpeg_subsampling_22, 0);
	lb_jpeg_subsampling->addWidget(rb_jpeg_subsampling_11);
	lb_jpeg_subsampling->addWidget(rb_jpeg_subsampling_22);
	lb_jpeg_subsampling->addStretch();
	gl_jpeg_color->addWidget(label_jpeg_subsampling, gl_jpeg_color_row, 0, Qt::AlignRight | Qt::AlignTop);
	gl_jpeg_color->addLayout(lb_jpeg_subsampling, gl_jpeg_color_row++, 1);
	if(ep->t_jpeg_color_subsampling_1x1 == 0)		rb_jpeg_subsampling_22->setChecked(true);
	if(ep->t_jpeg_color_subsampling_1x1 == 1)		rb_jpeg_subsampling_11->setChecked(true);

	l_jpeg->addStretch();
	stack_type->addWidget(tab_jpeg);

	// PNG
	tab_png = new QGroupBox(tr("PNG parameters"));
//	tab_png = new QWidget(this);

	QVBoxLayout *l_png = new QVBoxLayout(tab_png);
	l_png->setSizeConstraint(QLayout::SetMinimumSize);	

#if 0
	// compression
	QHBoxLayout *hb_png_compression = new QHBoxLayout();
	QLabel *label_png_compression = new QLabel();
	label_png_compression->setText(tr("Compression level: "));
	hb_png_compression->addWidget(label_png_compression);
	GuiSlider *slider_png_compression = new GuiSlider(0.0, Z_BEST_COMPRESSION, ep->t_png_compression, 1, 1, 1);
	hb_png_compression->addWidget(slider_png_compression);
	l_png->addLayout(hb_png_compression);
#endif

	// alpha
	QCheckBox *check_png_alpha = new QCheckBox(tr("Save alpha channel"));
	l_png->addWidget(check_png_alpha);

	// bits per pixel
	QHBoxLayout *hb_png_bits = new QHBoxLayout();
	QLabel *label_png_bits = new QLabel(tr("Bits per channel:"));
	QRadioButton *radio_png_bits_8 = new QRadioButton(tr("8 bits"));
	QRadioButton *radio_png_bits_16 = new QRadioButton(tr("16 bits"));
	QButtonGroup *radio_png_bits = new QButtonGroup(hb_png_bits);
	radio_png_bits->addButton(radio_png_bits_8, 8);
	radio_png_bits->addButton(radio_png_bits_16, 16);
	hb_png_bits->addWidget(label_png_bits);
	hb_png_bits->addWidget(radio_png_bits_8);
	hb_png_bits->addWidget(radio_png_bits_16);
	hb_png_bits->addStretch();
	l_png->addLayout(hb_png_bits);

	l_png->addStretch();
	stack_type->addWidget(tab_png);

	// TIFF
	tab_tiff = new QGroupBox(tr("TIFF parameters"));
//	tab_tiff = new QWidget(this);

	QVBoxLayout *l_tiff = new QVBoxLayout(tab_tiff);
	l_tiff->setSizeConstraint(QLayout::SetMinimumSize);	

	// alpha
	QCheckBox *check_tiff_alpha = new QCheckBox(tr("Save alpha channel"));
	l_tiff->addWidget(check_tiff_alpha);

	// bits per pixel
	QHBoxLayout *hb_tiff_bits = new QHBoxLayout();
	QLabel *label_tiff_bits = new QLabel(tr("Bits per channel:"));
	QRadioButton *radio_tiff_bits_8 = new QRadioButton(tr("8 bits"));
	QRadioButton *radio_tiff_bits_16 = new QRadioButton(tr("16 bits"));
	QButtonGroup *radio_tiff_bits = new QButtonGroup(hb_tiff_bits);
	radio_tiff_bits->addButton(radio_tiff_bits_8, 8);
	radio_tiff_bits->addButton(radio_tiff_bits_16, 16);
	hb_tiff_bits->addWidget(label_tiff_bits);
	hb_tiff_bits->addWidget(radio_tiff_bits_8);
	hb_tiff_bits->addWidget(radio_tiff_bits_16);
	hb_tiff_bits->addStretch();
	l_tiff->addLayout(hb_tiff_bits);

	l_tiff->addStretch();
	stack_type->addWidget(tab_tiff);
	// --==--
	QHBoxLayout *hb_main = new QHBoxLayout();
	hb_main->addWidget(gb_type);

	//-------------------------------
	// scaling
	QGroupBox *gb_scaling = new QGroupBox(tr("Photo scaling"));
	QVBoxLayout *vb_scaling = new QVBoxLayout(gb_scaling);
//	QGridLayout *layout_scaling = new QGridLayout(gb_scaling);
	QGridLayout *layout_scaling = new QGridLayout();
	QCheckBox *check_scaling_enable = new QCheckBox(tr("Enable"));
	layout_scaling->addWidget(check_scaling_enable, 0, 0);

//	check_scaling_to_fill = new QCheckBox(tr("Cut to fill size"));
//	layout_scaling->addWidget(check_scaling_to_fill, 1, 0);
	// width
	label_scaling_width = new QLabel(tr("Width:"));
	layout_scaling->addWidget(label_scaling_width, 0, 1, Qt::AlignRight);
	line_scaling_width = new QLineEdit();
	layout_scaling->addWidget(line_scaling_width, 0, 2, Qt::AlignLeft);
	line_scaling_width->setValidator(new QRegExpValidator(QRegExp("[0-9]{,5}"), line_scaling_width));
	// height
	label_scaling_height = new QLabel(tr("Height:"));
	layout_scaling->addWidget(label_scaling_height, 1, 1, Qt::AlignRight);
	line_scaling_height = new QLineEdit();
	layout_scaling->addWidget(line_scaling_height, 1, 2, Qt::AlignLeft);
	line_scaling_height->setValidator(new QRegExpValidator(QRegExp("[0-9]{,5}"), line_scaling_height));
	// fit-fill radio
	scale_fit_radio = new QButtonGroup(vb_scaling);
	b_size_fit = new QToolButton();
	b_size_fit->setIcon(QIcon(":/resources/scale_fit.svg"));
	b_size_fit->setToolTip(tr("Scale to fit size"));
	b_size_fit->setToolButtonStyle(Qt::ToolButtonIconOnly);
	b_size_fit->setCheckable(true);
	l_size_fit = new QLabel(tr("Fit size"));

	b_size_fill = new QToolButton();
	b_size_fill->setIcon(QIcon(":/resources/scale_fill.svg"));
	b_size_fill->setToolTip(tr("Scale to fill size"));
	b_size_fill->setToolButtonStyle(Qt::ToolButtonIconOnly);
	b_size_fill->setCheckable(true);
	l_size_fill = new QLabel(tr("Fill size"));

	scale_fit_radio->addButton(b_size_fit, 0);
	scale_fit_radio->addButton(b_size_fill, 1);

	QHBoxLayout *l_fit_fill = new QHBoxLayout();
	l_fit_fill->setSpacing(8);
	l_fit_fill->setContentsMargins(2, 1, 2, 1);
	l_fit_fill->addWidget(b_size_fit, 0, Qt::AlignLeft);
	l_fit_fill->addWidget(l_size_fit, 0, Qt::AlignLeft);
	l_fit_fill->addSpacing(8);
	l_fit_fill->addWidget(b_size_fill, 0, Qt::AlignLeft);
	l_fit_fill->addWidget(l_size_fill, 0, Qt::AlignLeft);
	l_fit_fill->addStretch(1);
	layout_scaling->addLayout(l_fit_fill, 2, 0, 1, 0);
//	vb_scaling->addLayout(l_fit_fill);
	//
	vb_scaling->addLayout(layout_scaling);
	vb_scaling->addStretch();

	hb_main->addWidget(gb_scaling);
	layout_main->addLayout(hb_main);
	layout_main->addStretch();

	//-------------------------------
	// set default
	if(ep->image_type == export_parameters_t::image_type_jpeg) {
		radio_type_jpeg->setChecked(true);
		stack_type->setCurrentWidget(tab_jpeg);
	}
	if(ep->image_type == export_parameters_t::image_type_png) {
		radio_type_png->setChecked(true);
		stack_type->setCurrentWidget(tab_png);
	}
	if(ep->image_type == export_parameters_t::image_type_tiff) {
		radio_type_tiff->setChecked(true);
		stack_type->setCurrentWidget(tab_tiff);
	}
//	tab_type->setCurrentIndex(ep->image_type);

	if(ep->t_png_bits == 8)		radio_png_bits_8->setChecked(true);
	if(ep->t_png_bits == 16)	radio_png_bits_16->setChecked(true);

	if(ep->t_tiff_bits == 8)	radio_tiff_bits_8->setChecked(true);
	if(ep->t_tiff_bits == 16)	radio_tiff_bits_16->setChecked(true);
	check_png_alpha->setCheckState(ep->t_png_alpha ? Qt::Checked : Qt::Unchecked);
	check_tiff_alpha->setCheckState(ep->t_tiff_alpha ? Qt::Checked : Qt::Unchecked);

	set_folder(ep->folder);
	QString name = QString::fromStdString(ep->get_file_name());
	if(ep->process_single)
		line_file_name->setText(name);

	check_process_asap->setCheckState(ep->process_asap ? Qt::Checked : Qt::Unchecked);

	check_scaling_enable->setCheckState(ep->scaling_force ? Qt::Checked : Qt::Unchecked);
//	check_scaling_to_fill->setCheckState(ep->scaling_to_fill ? Qt::Checked : Qt::Unchecked);
	int pressed_index = ep->scaling_to_fill ? 1 : 0;
	scale_fit_radio->button(pressed_index)->setChecked(true);
	scale_fit_radio->button(1 - pressed_index)->setChecked(false);
	line_scaling_width->setText(QString::number(ep->scaling_width));
	line_scaling_height->setText(QString::number(ep->scaling_height));

	//-------------------------------
	// buttons
	QHBoxLayout *hb_buttons = new QHBoxLayout();
	QPushButton *button_ok = new QPushButton(tr("Ok"));
	QPushButton *button_cancel = new QPushButton(tr("Cancel"));
	hb_buttons->addStretch();
	hb_buttons->addWidget(button_ok);
	hb_buttons->addWidget(button_cancel);
	layout_main->addLayout(hb_buttons);
	button_ok->setDefault(true);

	// connect
	connect(radio_type, SIGNAL(buttonPressed(int)), this, SLOT(slot_image_type_clicked(int)));
	connect(button_folder, SIGNAL(pressed(void)), this, SLOT(slot_button_folder_pressed(void)));
	connect(radio_png_bits, SIGNAL(buttonClicked(int)), this, SLOT(slot_png_bits(int)));
	connect(radio_tiff_bits, SIGNAL(buttonClicked(int)), this, SLOT(slot_tiff_bits(int)));
	connect(check_png_alpha, SIGNAL(stateChanged(int)), this, SLOT(slot_png_alpha(int)));
	connect(check_tiff_alpha, SIGNAL(stateChanged(int)), this, SLOT(slot_tiff_alpha(int)));
	connect(slider_jpeg_iq, SIGNAL(signal_changed(double)), this, SLOT(slot_jpeg_iq(double)));
	connect(radio_jpeg_color_space, SIGNAL(buttonClicked(int)), this, SLOT(slot_jpeg_color_space(int)));
	connect(rb_jpeg_subsampling, SIGNAL(buttonClicked(int)), this, SLOT(slot_jpeg_subsampling(int)));
#if 0
	connect(slider_png_compression, SIGNAL(signal_changed(double)), this, SLOT(slot_png_compression(double)));
#endif
	if(line_file_name != nullptr)
		connect(line_file_name, SIGNAL(editingFinished(void)), this, SLOT(slot_line_file_name(void)));
	connect(check_process_asap, SIGNAL(stateChanged(int)), this, SLOT(slot_process_asap(int)));
	connect(line_scaling_width, SIGNAL(editingFinished(void)), this, SLOT(slot_line_scaling_width(void)));
	connect(line_scaling_height, SIGNAL(editingFinished(void)), this, SLOT(slot_line_scaling_height(void)));
	connect(check_scaling_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_scaling_enable(int)));
	connect(scale_fit_radio, SIGNAL(buttonClicked(int)), this, SLOT(slot_scale_fit_radio(int)));

	connect(line_folder, SIGNAL(editingFinished(void)), this, SLOT(slot_line_folder(void)));
	connect(button_ok, SIGNAL(pressed(void)), this, SLOT(slot_button_ok(void)));
	connect(button_cancel, SIGNAL(pressed(void)), this, SLOT(reject(void)));

	setLayout(layout_main);
	setFixedHeight(sizeHint().height());

	normalize_jpeg_subsampling();

	emit slot_scaling_enable(ep->scaling_force ? Qt::Checked : Qt::Unchecked);
}

void Batch_Dialog::set_folder(string folder) {
	ep->folder = folder;
	line_folder->setText(QString::fromStdString(ep->folder));
}

void Batch_Dialog::slot_image_type_clicked(int id) {
	ep->image_type = (export_parameters_t::image_type_t)id;
	if(ep->image_type == export_parameters_t::image_type_jpeg)
		stack_type->setCurrentWidget(tab_jpeg);
	if(ep->image_type == export_parameters_t::image_type_png)
		stack_type->setCurrentWidget(tab_png);
	if(ep->image_type == export_parameters_t::image_type_tiff)
		stack_type->setCurrentWidget(tab_tiff);
	if(ep->process_single) {
		QString name = QString::fromStdString(ep->get_file_name());
		line_file_name->setText(name);
	}
}

void Batch_Dialog::slot_button_folder_pressed(void) {
	QString q_folder = QFileDialog::getExistingDirectory(this, tr("Select destination folder"), QString::fromStdString(ep->folder), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	line_folder->setText(q_folder);
	ep->folder = q_folder.toStdString();
}

void Batch_Dialog::slot_png_bits(int id) {
	ep->t_png_bits = id;
}

void Batch_Dialog::slot_tiff_bits(int id) {
	ep->t_tiff_bits = id;
}

void Batch_Dialog::slot_png_alpha(int checked) {
	ep->t_png_alpha = (checked == Qt::Checked);
}

void Batch_Dialog::slot_tiff_alpha(int checked) {
	ep->t_tiff_alpha = (checked == Qt::Checked);
}

void Batch_Dialog::slot_jpeg_iq(double _value) {
	ep->t_jpeg_iq = _value + 0.05;
}

void Batch_Dialog::normalize_jpeg_subsampling(void) {
	disconnect(rb_jpeg_subsampling, SIGNAL(buttonClicked(int)), this, SLOT(slot_jpeg_subsampling(int)));
	if(ep->t_jpeg_color_space_rgb == 1) {
		rb_jpeg_subsampling_11->setChecked(true);
		rb_jpeg_subsampling_22->setEnabled(false);
		rb_jpeg_subsampling_11->setEnabled(false);
		label_jpeg_subsampling->setEnabled(false);
	} else {
		if(ep->t_jpeg_color_subsampling_1x1 == 0)		rb_jpeg_subsampling_22->setChecked(true);
		if(ep->t_jpeg_color_subsampling_1x1 == 1)		rb_jpeg_subsampling_11->setChecked(true);
		rb_jpeg_subsampling_22->setEnabled(true);
		rb_jpeg_subsampling_11->setEnabled(true);
		label_jpeg_subsampling->setEnabled(true);
	}
	connect(rb_jpeg_subsampling, SIGNAL(buttonClicked(int)), this, SLOT(slot_jpeg_subsampling(int)));
}

void Batch_Dialog::slot_jpeg_subsampling(int id) {
	ep->t_jpeg_color_subsampling_1x1 = id;
}

void Batch_Dialog::slot_jpeg_color_space(int id) {
	ep->t_jpeg_color_space_rgb = id;
	normalize_jpeg_subsampling();
}

void Batch_Dialog::slot_png_compression(double _value) {
	ep->t_png_compression = _value + 0.05;
}

void Batch_Dialog::slot_line_file_name(void) {
	QString text = line_file_name->text();
	ep->cut_and_set_file_name(text.toStdString());
	QString name = QString::fromStdString(ep->get_file_name());
	line_file_name->setText(name);
}

void Batch_Dialog::slot_process_asap(int checked) {
	ep->process_asap = (checked == Qt::Checked);
}

void Batch_Dialog::slot_scaling_enable(int checked) {
//cerr << "slot_scaling_enable: " << ep->scaling_force << endl;
	ep->scaling_force = (checked == Qt::Checked);
//cerr << "slot_scaling_enable: " << ep->scaling_force << endl;
	line_scaling_width->setEnabled(ep->scaling_force);
	line_scaling_height->setEnabled(ep->scaling_force);
//	check_scaling_to_fill->setEnabled(ep->scaling_force);
	label_scaling_width->setEnabled(ep->scaling_force);
	label_scaling_height->setEnabled(ep->scaling_force);
	l_size_fit->setEnabled(ep->scaling_force);
	l_size_fill->setEnabled(ep->scaling_force);
	b_size_fit->setEnabled(ep->scaling_force);
	b_size_fill->setEnabled(ep->scaling_force);
}

void Batch_Dialog::slot_scale_fit_radio(int index) {
	ep->scaling_to_fill = (index == 1);
}

void Batch_Dialog::slot_line_scaling_width(void) {
	int width = line_scaling_width->text().toInt();
	int w = width;
	if(w < 256)	w = 256;
	if(w > 65536)	w = 65536;
	if(w != width) {
		QString str;
		line_scaling_width->setText(str.setNum(w));
	}
	ep->scaling_width = w;
}

void Batch_Dialog::slot_line_scaling_height(void) {
	int height = line_scaling_height->text().toInt();
	int h = height;
	if(h < 256)	h = 256;
	if(h > 65536)	h = 65536;
	if(h != height) {
		QString str;
		line_scaling_height->setText(str.setNum(h));
	}
	ep->scaling_height = h;
}

void Batch_Dialog::slot_line_folder(void) {
	QString new_folder = line_folder->text();
/*
	if(!QDir(new_folder).exists()) {
		QString error = tr(" Folder <b>\"%1\"</b> does not exist.");
		error = error.arg(new_folder);
		QMessageBox::warning(this, tr("Folder does not exist"), error);
	}
*/
	ep->folder = new_folder.toStdString();
}

void Batch_Dialog::slot_button_ok(void) {
	QString qs_folder = QString::fromStdString(ep->folder);
	QFileInfo fi(qs_folder);
	if(fi.isDir() == false) {
		QString error = tr("Folder <b>\"%1\"</b> does not exist, please change destination folder.");
		error = error.arg(qs_folder);
		QMessageBox::critical(this, tr("Incorrect destination folder"), error);
		return;
	}
	if(fi.isExecutable() == false || fi.isWritable() == false) {
		QString error = tr("Can't write to folder <b>\"%1\"</b>, please change destination folder.");
		error = error.arg(qs_folder);
		QMessageBox::critical(this, tr("Incorrect destination folder"), error);
		return;
	}
	emit accept();
}
//------------------------------------------------------------------------------
