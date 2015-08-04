#ifndef __H_WIDGETS__
#define __H_WIDGETS__
/*
 * widgets.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

//------------------------------------------------------------------------------
class Cursor {
public:
	enum cursor {
		unknown,
		arrow,
		size_ver,
		size_hor,
		size_bdiag,
		size_fdiag,
		hand_open,
		hand_closed,
		cross
	};
	static QCursor to_qt(enum cursor &c);
};

//Q_DECLARE_METATYPE(Cursor)

//------------------------------------------------------------------------------
class ControlsArea : public QScrollArea {
	Q_OBJECT

public:
	ControlsArea(QWidget *parent = 0);

protected:
	void resizeEvent(QResizeEvent *event);
};

//------------------------------------------------------------------------------

#endif // __H_WIDGETS__
