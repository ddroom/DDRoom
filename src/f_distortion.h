#ifndef __H_F_DISTORTION__
#define __H_F_DISTORTION__
/*
 * f_distortion.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Distortion : public Filter {
	Q_OBJECT

public:
	F_Distortion(int id);
	~F_Distortion();

	// process
	bool get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc);
	Filter::type_t type(void);
	FilterProcess *getFP(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

public slots:
	void slot_edit_link(bool state);
	void slot_checkbox_enable(int state);
	void slot_checkbox_clip(int state);

protected:
	class PS_Distortion *ps;
	class PS_Distortion *_ps;
	static class FP_Distortion *fp;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_enable;
	QCheckBox *checkbox_clip;
	void reconnect(bool to_connect);
	QLabel *label_lens;
	QToolButton *btn_edit_link;

};

//------------------------------------------------------------------------------

#endif //__H_F_DISTORTION__
