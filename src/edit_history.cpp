/*
 * edit_history.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * class EditHistory, purposes:
	- provide and mentain enable state for actions undo/redo;
	- provide and mentain widget for edit history visualization and control at side tab
	- implement undo/redo functionality with interface via pair<Filter *, DataSet> classes

	Organization:
		- is singleton that provide GUI controls and accept signals from those
		- history container stored as 'void *' at photo_t of current session, responsible for container creation and destruction
		- accept signal about current session change from Edit class, reference at current session should be stored as pointer that can be == nullptr

*/
/*

	TODO:
	- add config support for option 'compression enabled' and 'compression_delta_time' (in ms)
	- add 'fast' undo and redo
	- add descriptions from filters not from deltas serialization (with translation support)
	- check (f_wb temperature), (f_curve points) etc - show in readable form

*/

//#include "dataset.h"
#include "edit.h"
#include "edit_history.h"
#include "filter.h"
//#include "photo.h"
#include "cm.h"
#include "config.h"

#include <iostream>
#include <list>
#include <set>

using namespace std;

#define COMPRESSION_TIME_DELTA 500

//==============================================================================
eh_filter_record_t::eh_filter_record_t(void) : filter(nullptr) {
}

eh_filter_record_t::eh_filter_record_t(class Filter *_filter, const list<class field_delta_t> &_deltas) : filter(_filter), deltas(_deltas) {
}

QStringList eh_filter_record_t::get_description(void) const {
	// TODO: ask description for deltas from 'record->filter'
	QStringList description;
//cerr << "__ add description: " << endl;
//cerr << "filter->id().c_str()" << endl;
	description.push_back(filter->id().c_str());
	for(list<field_delta_t>::const_iterator it = deltas.begin(); it != deltas.end(); ++it) {
		const field_delta_t &d = *it;
		// TODO: force filter to provide readable localized description - with correct localized field name and readable value name like points at curve etc...
		description.push_back(d.field_name.c_str());
		description.push_back(d.field_before.serialize().c_str());
		description.push_back(d.field_after.serialize().c_str());
/*
		cerr << d.field_name.c_str() << endl;
		cerr << d.field_before.serialize().c_str() << endl;
		cerr << d.field_after.serialize().c_str() << endl;
cerr << "__ add description - done" << endl;
*/
	}
	return description;
}

//==============================================================================
eh_record_t::eh_record_t(void) {
	time = QTime::currentTime();
}

eh_record_t::eh_record_t(const QVector<eh_filter_record_t> &_filter_records) {
	time = QTime::currentTime();
	int count = _filter_records.size();
	if(count == 0)
		throw("incorrect creation of eh_record_t with empty vector of filter records");
	description = QVector<QStringList>(count);
	filter_records = QVector<eh_filter_record_t>(count);
	for(int i = 0; i < count; ++i) {
		description[i] = _filter_records[i].get_description();
		filter_records[i] = _filter_records[i];
	}
}

//==============================================================================
class edit_history_t {
public:
	QList<eh_record_t> h_before;	// above pointer
	QList<eh_record_t> h_after;		// below pointer
	// where pointer is current state
};

//==============================================================================
EditHistory_ItemDelegate::EditHistory_ItemDelegate(void) {
	e_v = 2;	// don't use odd numbers here
	e_h = 2;
	c_ready = false;
}

void EditHistory_ItemDelegate::set_edit_history(EditHistory *_edit_history) {
	edit_history = _edit_history;
}

void EditHistory_ItemDelegate::update_colors(const QStyleOptionViewItem &option) {
	const QColor &c_bg = option.palette.color(QPalette::Active, QPalette::Background);
	const QColor &c_h = option.palette.color(QPalette::Active, QPalette::Highlight);
//		gradient.setColorAt(0, option.palette.color(QPalette::Normal, QPalette::QPalette::Base));
	if(c_h != c_highlight || c_bg != c_background || c_ready == false) {
		c_ready = true;
		c_background = c_bg;
		c_highlight = c_h;
		float rgb[3];
		float jsh_bg[3];
		float jsh_h[3];
		rgb[0] = c_bg.redF();	rgb[1] = c_bg.greenF();	rgb[2] = c_bg.blueF();
		cm::sRGB_to_Jsh(jsh_bg, rgb);
		rgb[0] = c_h.redF();	rgb[1] = c_h.greenF();	rgb[2] = c_h.blueF();
		cm::sRGB_to_Jsh(jsh_h, rgb);
		//--
		float delta = jsh_bg[1] - jsh_h[1];	if(delta < 0)	delta = -delta;
		float min = jsh_bg[1];	if(min > jsh_h[1]) min = jsh_h[1];
		float Jsh[3];
		Jsh[0] = jsh_bg[0];
		Jsh[1] = min + 0.125 * delta;
		Jsh[2] = jsh_h[2];
		cm::Jsh_to_sRGB(rgb, Jsh);
		c_field_name = QColor(rgb[0] * 255.0 + 0.5, rgb[1] * 255.0 + 0.5, rgb[2] * 255.0 + 0.5);
		//--
		Jsh[0] = jsh_bg[0];
		Jsh[1] = min + 0.25 * delta;
		Jsh[2] = jsh_h[2];
		cm::Jsh_to_sRGB(rgb, Jsh);
		c_field_value = QColor(rgb[0] * 255.0 + 0.5, rgb[1] * 255.0 + 0.5, rgb[2] * 255.0 + 0.5);
		//--
	}
}

void EditHistory_ItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	if(edit_history == nullptr) {
cerr << "edit_history == nullptr" << endl;
		return;
	}
	EditHistory_ItemDelegate *_this = const_cast<EditHistory_ItemDelegate *>(this);
	_this->update_colors(option);

	int font_height = option.fontMetrics.height();
//cerr << "paint() font_height == " << font_height << endl;
	bool aa = painter->testRenderHint(QPainter::Antialiasing);
	if(!aa) {
		painter->setRenderHint(QPainter::Antialiasing, true);
		QTransform tr_restore;
		tr_restore.translate(0.5, 0.5);	// shift to fix AA artifacts
		painter->setWorldTransform(tr_restore);
	}
	//--
	QStringList text_list;
	QString text;
	QVariant value = index.data(Qt::DisplayRole);
	if(value.isValid() && !value.isNull()) {
		text_list = value.toStringList();
		text = text_list[0];
	}
	bool is_cursor = (text_list.size() == 1);
	QRect rect_original = option.rect;
	rect_original.setX(rect_original.x() + e_v);
	rect_original.setY(rect_original.y() + e_v);
	rect_original.setWidth(rect_original.width() - e_v * 2 + 1);
	rect_original.setHeight(rect_original.height() - e_h * 2 + 1);
	QFontMetrics font_metrics = QFontMetrics(option.font);
//	const QColor &c_high = option.palette.color(QPalette::Active, QPalette::Highlight);
//	const QColor &c_bg = option.palette.color(QPalette::Active, QPalette::Background);
	int fh = font_height;
	int fh8 = fh / 8;
	if(fh8 < 1)	fh8 = 1;
	int fh4 = fh / 4;
	int fh2 = fh / 2;
	QRect r = rect_original;
	int r_x = r.x();
	int r_y = r.y();
	int r_w = r.width();
	int r_h = r.height();
	int header_width = font_metrics.width(text);

	if(!is_cursor) {
		r_x += r.width() - header_width - 5 * fh4;
		r_w = header_width + 4 * fh4 - 1;
		r_h = fh + 2 * fh8;
	
		painter->setPen(c_background);
		painter->setBrush(QBrush(c_background));
		painter->drawRoundedRect(r_x, r_y, r_w, r_h, fh4, fh4);
		painter->drawRect(r_x, r_y + r_h / 2, r_w, r_h - r_h / 2 - 1);
	
		painter->setPen(c_background);
		painter->setBrush(QBrush(c_background));
		r_x = r.x();
		r_y = r.y() + r_h;
		r_w = r.width();
		r_h = r.height() - r_h;
		painter->drawRoundedRect(r_x, r_y, r_w, r_h, fh4, fh4);
	}
	if(!aa) {
		painter->setRenderHint(QPainter::Antialiasing, false);
		QTransform tr_restore;
		tr_restore.translate(-0.5, -0.5);
		painter->setWorldTransform(tr_restore);
	}

	if(is_cursor) {
		r_h = option.rect.height();
		r_y += r_h / 2;
		int rh = r_h / 2;

		QLinearGradient gradient(0, 0, 0, 1);
		gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
		gradient.setColorAt(0.0, option.palette.color(QPalette::Normal, QPalette::Base));
		gradient.setColorAt(0.5, c_highlight);
		gradient.setColorAt(1.0, option.palette.color(QPalette::Normal, QPalette::Base));
		gradient.setSpread(QGradient::ReflectSpread);
		painter->fillRect(r_x, r_y - rh + 1, r_w + 1, r_h - 1, QBrush(gradient));

		painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
		r_x += 1;
		painter->drawLine(r_x, r_y, r_x + r_w, r_y);
		for(int i = 0; i < rh; ++i) {
			int l = rh - i;
			painter->drawLine(r_x, r_y + i, r_x + l, r_y + i);
			painter->drawLine(r_x, r_y - i, r_x + l, r_y - i);
			painter->drawLine(r_x + r_w - l, r_y + i, r_x + r_w, r_y + i);
			painter->drawLine(r_x + r_w - l, r_y - i, r_x + r_w, r_y - i);
		}
	}

/*
	if(font_metrics.width(text) >= text_rect.width()) {
		QString text_n;
		int width_p = font_metrics.width("...");
		int width_n = text_rect.width() - width_p;
		for(QChar *q = text.data(); font_metrics.width(text_n) <= width_n; ++q)
		text_n += *q;
		text_n.remove(text_n.length() - 1, 1);
		text = text_n + "...";
	}
*/
	if(!is_cursor) {
		QLinearGradient gradient(0, 0, 0, 1);
		gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
		gradient.setColorAt(0, c_background);
		gradient.setColorAt(0.5, c_highlight);
		gradient.setColorAt(1, c_highlight);
		gradient.setSpread(QGradient::ReflectSpread);

		QFont font = painter->font();
		QFont font_bold = font;
		font_bold.setBold(true);

		painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
		r = rect_original;
		r_x = r.x() + r.width() - header_width - fh4 * 3;
		r_y = r.y() + fh8;
		r_w = header_width + 1;
		r_h = fh + 1;
		painter->drawText(r_x, r_y, r_w, r_h, Qt::AlignLeft, text);
/*
		QLinearGradient gradient_h(0, 0, 1, 0);
		gradient_h.setCoordinateMode(QGradient::ObjectBoundingMode);
		gradient_h.setColorAt(0, c_background);
		gradient_h.setColorAt(0.5, c_highlight);
		gradient_h.setColorAt(1, c_background);
		gradient_h.setSpread(QGradient::ReflectSpread);
		painter->fillRect(r_x - 2 * fh4, r_y + r_h, r_w + 4 * fh4, 1, QBrush(gradient_h));
*/
		painter->setPen(c_highlight);
		painter->drawLine(r_x - 2 * fh4 + 1, r_y + r_h, r_x + r_w + 2 * fh4 - 1, r_y + r_h);

		r_x = r.x() + fh4;
		r_y = r.y() + fh + 3 * fh8;
		r_w = r.width() - 2 * fh4;
		r_h = fh;

//cerr << "text_list.size() == " << text_list.size() << endl;
		for(int i = 0; i < text_list.size() - 1; ++i) {
			text = text_list[1 + i];
//cerr << "text == " << text.toLatin1().data() << endl;
			if(i % 3 == 0) {
				painter->fillRect(r_x, r_y, 1, fh + fh, QBrush(gradient));
				painter->setPen(c_highlight);
				painter->drawLine(r_x + 1, r_y + fh + fh - 0, r_x + r_w, r_y + fh + fh - 0);
				painter->fillRect(r_x + r_w, r_y, 1, fh + fh, QBrush(gradient));
				painter->fillRect(r_x + 1, r_y, r_w - 1, r_h, c_field_name);
				painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
				painter->drawText(r_x + fh4, r_y, r_w - fh2, r_h, Qt::AlignCenter, text);
				r_y += fh;
			}
			if(i % 3 == 1) {
				int rw = r_w / 2 - 1;
				painter->fillRect(r_x + rw - 1, r_y, 1, r_h - 1, QBrush(gradient));
				QColor c_bg = c_field_value;
				QColor c_text = option.palette.color(QPalette::Normal, QPalette::Text);
				if(edit_history->index_just_after_pointer(index.row())) {
					c_bg = c_highlight;
					c_text = option.palette.color(QPalette::Normal, QPalette::HighlightedText);
					painter->setFont(font_bold);
				}
				painter->fillRect(r_x + 1, r_y, rw - 2, r_h - 1, QBrush(c_bg));
				painter->setPen(c_text);
				painter->drawText(r_x + fh4, r_y, rw - fh2, r_h, Qt::AlignLeft, text);
				painter->setFont(font);
			}
			if(i % 3 == 2) {
				int rw = r_w / 2 - 1;
				int rx = r_x + rw;
				rw = r_w - rw;
				QColor c_bg = c_field_value;
				QColor c_text = option.palette.color(QPalette::Normal, QPalette::Text);
				if(edit_history->index_just_before_pointer(index.row())) {
					c_bg = c_highlight;
					c_text = option.palette.color(QPalette::Normal, QPalette::HighlightedText);
					painter->setFont(font_bold);
				}
				painter->fillRect(rx, r_y, rw, r_h - 1, QBrush(c_bg));
				painter->setPen(c_text);
				painter->drawText(rx + fh4, r_y, rw - fh2, r_h, Qt::AlignRight, text);
				painter->setFont(font);
				r_y += fh + fh8;
			}
//			painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
		}
	}
}

QSize EditHistory_ItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	// should be connected to model and check size from model bi index.row()
	int h = get_item_height(index.row(), option.fontMetrics.height());
	return QSize(0, h);
}

int EditHistory_ItemDelegate::get_item_height(int index, int font_height) const {
	if(edit_history == nullptr)
		return 0;
	int c = 0;
	QVector<QStringList> *p = edit_history->get_description_list(index);
/*	actually, should be used 
		QVariant value = index.data(Qt::DisplayRole);
	instead, but that way is faster. Anyway QStringList provide additional link to data model class...
*/
//	if(p != nullptr) {
//		c = (p->size() - 1) / 3;	// TODO: fix GUI drawing
//	}
	if(p != nullptr)
		c = p->size();
	if(c < 0)	c = 0;
//cerr << "c == " << c << endl;
	int h8 = font_height / 8;
	if(h8 < 1)	h8 = 1;
	// cursor
	int h = font_height;
	if(c != 0) {
		// field
		h = h8 * 2 + font_height + c * 2 * font_height + c * h8 + h8;
		h += e_h * 2;
	} else {
		h = h8 * 8;
		if(h < 10)	h = 10;
	}
//cerr << "-- item_height, index == " << index << "; height == " << h << endl;
	return h;
}

//==============================================================================
QVector<QStringList> *EditHistory::get_description_list(int index) const {
	if(edit_history == nullptr)
		return nullptr;
//	index--;
	int index_pointer = edit_history->h_before.size();
//cerr << "index == " << index << "; index_pointer == " << index_pointer << endl;
	if(index < index_pointer) {
//cerr << "__1" << endl;
//cerr << "size == " << edit_history->h_before[index].description.size() << endl;
		return &edit_history->h_before[index].description;
//		return &edit_history->h_before.at(index).description;
	} else {
		if(index > index_pointer) {
//cerr << "__2" << endl;
			int i = index - (index_pointer + 1);
			return &edit_history->h_after[i].description;
//			return &edit_history->h_after.at(i).description;
		}
	}
	return nullptr;
}

void EditHistory::set_view_model(void) {
	int size = 0;
	size = get_rows_count();
	// emit update
	int n_size = get_rows_count();
	if(size < n_size)	size = n_size;
//	QModelIndex index_begin = createIndex(0, 0);
//	QModelIndex index_end = createIndex(size, 0);
//	emit dataChanged(index_begin, index_end);
//	emit dataChanged(index_begin, index_end);
//	emit scrollTo();
//	dataChanged(index_begin, index_end);
	emit layoutChanged();
	if(edit_history != nullptr) {
		int index = edit_history->h_before.size();
		view->scrollTo(createIndex(index, 0));
	}
}

int EditHistory::get_rows_count(void) const {
	if(edit_history == nullptr)
		return 0;
	int c = 1;
	c += edit_history->h_before.size();
	c += edit_history->h_after.size();
	if(c == 1)
		return 0;
	return c;
}

bool EditHistory::index_just_before_pointer(int index) {
	int index_pointer = edit_history->h_before.size();
	if(index_pointer != 0)
		return index == index_pointer - 1;
	return false;
}

bool EditHistory::index_just_after_pointer(int index) {
	int index_pointer = edit_history->h_before.size();
//	if(index_pointer != 0)
		return index == index_pointer + 1;
//	return false;
}

int EditHistory::rowCount(const QModelIndex &parent) const {
	return get_rows_count();
}

QVariant EditHistory::data(const QModelIndex &qm_index, int role) const {
	if(edit_history == nullptr || !qm_index.isValid())
		return QVariant();
	if(role == Qt::TextAlignmentRole) {
		return int(Qt::AlignLeft | Qt::AlignVCenter);
	} else if(role == Qt::DisplayRole) {
#if 0
		QList<QVariant> l;
		QVector<QStringList> *p = get_description_list(qm_index.row());
		if(p == nullptr) {
cerr << "nullptr; qm_index.row() == " << qm_index.row() << endl;
			QStringList sl;
			sl.push_back("");
			l.push_back(QVariant(sl));
		} else {
cerr << "...1  qm_index.row() == " << qm_index.row() << endl;
cerr << "p->size() == " << p->size() << endl;
			
			for(int i = 0; i < p->size(); ++i)
				l.push_back(QVariant((*p)[i]));
cerr << "...2" << endl;
		}
		return QVariant(l);
#else
		QStringList l;
//		QList<QVariant> l;
		QVector<QStringList> *p = get_description_list(qm_index.row());
		if(p == nullptr) {
//cerr << "nullptr; qm_index.row() == " << qm_index.row() << endl;
//			QStringList sl;
			l.push_back("");
//			l.push_back(QVariant(sl));
		} else {
//cerr << "...1  qm_index.row() == " << qm_index.row() << endl;
//cerr << "p->size() == " << p->size() << endl;
			for(int i = 0; i < p->size(); ++i)
				l += (*p)[i];
//cerr << "...2" << endl;
		}
		return QVariant(l);
#endif
	}
	return QVariant();
}

//==============================================================================
EditHistory::EditHistory(void) {
}

EditHistory::EditHistory(class Edit *_edit, QWidget *_parent) {
	edit = _edit;
	parent = _parent;
	edit_history = nullptr;

	_create_widgets();
}

EditHistory::~EditHistory() {
	item_delegate->set_edit_history(nullptr);
	view->setItemDelegate(nullptr);
	delete item_delegate;
	edit_history = nullptr;
}

void EditHistory::fill_toolbar(QToolBar *t) {
	t->addAction(action_undo);
	t->addAction(action_redo);
}

void EditHistory::fill_menu(QMenu *menu) {
	menu->addAction(action_undo);
	menu->addAction(action_redo);
}

void EditHistory::_create_widgets(void) {
	action_undo = new QAction(tr("&Undo"), parent);
	action_undo->setShortcut(QKeySequence(tr("Ctrl+Z")));
	action_undo->setIcon(QIcon(":/resources/edit_undo.svg"));
	action_undo->setDisabled(true);

	action_redo = new QAction(tr("&Redo"), parent);
	action_redo->setShortcut(QKeySequence(tr("Ctrl+Y")));
	action_redo->setIcon(QIcon(":/resources/edit_redo.svg"));
	action_redo->setDisabled(true);

	connect(action_undo, SIGNAL(triggered()), this, SLOT(slot_action_undo()));
	connect(action_redo, SIGNAL(triggered()), this, SLOT(slot_action_redo()));

	// view widget
	widget = new QWidget(parent);
	QVBoxLayout *l = new QVBoxLayout(widget);
	l->setSpacing(2);
	l->setContentsMargins(2, 1, 2, 1);

	// toolbar
/*
	QToolBar *tb = new QToolBar("Edit history", nullptr);
	l->addWidget(tb);
//	tb->setMovable(false);
//	tb->setFloatable(false);
	tb->setIconSize(QSize(20, 20));
#ifdef Q_OS_MAC
	tb->setContentsMargins(2, 2, 2, 2);
#endif
	tb->addAction(action_undo);
	tb->addAction(action_redo);
*/
	QHBoxLayout *hb = new QHBoxLayout();
	hb->setSpacing(2);
	hb->setContentsMargins(2, 2, 2, 2);
	hb->setAlignment(Qt::AlignLeft);
	QToolButton *tb_undo = new QToolButton();
	tb_undo->setDefaultAction(action_undo);
	// TODO: add something like style->init_toolbutton(QToolButton *);
	tb_undo->setIconSize(QSize(20, 20));
	tb_undo->setAutoRaise(true);
	hb->addWidget(tb_undo);
	QToolButton *tb_redo = new QToolButton();
	tb_redo->setDefaultAction(action_redo);
	tb_redo->setAutoRaise(true);
	tb_redo->setIconSize(QSize(20, 20));
	hb->addWidget(tb_redo);
	l->addLayout(hb);

	// list view
	view = new QListView();
	view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	view->setUniformItemSizes(false);
	view->setSpacing(0);
	view->setModel(this);
	item_delegate = new EditHistory_ItemDelegate();
	item_delegate->set_edit_history(this);
	view->setItemDelegate(item_delegate);
	l->addWidget(view);
}

QWidget *EditHistory::get_widget(void) {
	return widget;
}

//------------------------------------------------------------------------------
void EditHistory::photo_constructor(Photo_t *photo) {
	photo->edit_history = (void *)(new edit_history_t());
}

void EditHistory::photo_destructor(Photo_t *photo) {
	if(photo->edit_history != nullptr)
		delete (edit_history_t *)photo->edit_history;
}

// switch GUI to edit history state saved at Photo_t object
// TODO: acquire signal from edit class on active view change to switch actions disable and upgrade history view
void EditHistory::set_current_photo(std::shared_ptr<Photo_t> photo) {
	edit_history = nullptr;
//	if(!photo.isNull())
	if(photo)
		edit_history = (edit_history_t *)photo->edit_history;
	update_gui_state();
}

void EditHistory::update_gui_state(void) {
	// update actions
	if(action_undo != nullptr) {
		bool disabled = true;
		if(edit_history != nullptr)
			disabled = (edit_history->h_before.size() == 0);
		action_undo->setDisabled(disabled);
	}
	if(action_redo != nullptr) {
		bool disabled = true;
		if(edit_history != nullptr)
			disabled = (edit_history->h_after.size() == 0);
		action_redo->setDisabled(disabled);
	}
	// update view
	set_view_model();
}

void EditHistory::add_eh_filter_record(const eh_filter_record_t &filter_record) {
	QVector<eh_filter_record_t> records = QVector<eh_filter_record_t>(1);
	records[0] = filter_record;
	add_eh_filter_records(records);
}

void EditHistory::add_eh_filter_records(const QVector<eh_filter_record_t> &filter_records) {
	eh_record_t record(filter_records);
	//--
	bool add_record = true;
	bool edit_history_compression = true;
	int compression_time_delta = COMPRESSION_TIME_DELTA;
	Config::instance()->get(CONFIG_SECTION_BEHAVIOR, "edit_history_compression", edit_history_compression);
	Config::instance()->get(CONFIG_SECTION_BEHAVIOR, "edit_history_compression_timeout", compression_time_delta);
	if(!edit_history->h_before.empty() && edit_history_compression) {
		eh_record_t &last_record = edit_history->h_before.last();
		// check time delta
//cerr << "time delta: " << ddr::abs(last_record.time.msecsTo(record.time)) << endl;
		if(ddr::abs(last_record.time.msecsTo(record.time)) <= compression_time_delta) {
			// check filters
			std::set<std::string> set_1;
			for(int i = 0; i < last_record.filter_records.size(); ++i)
				set_1.insert(last_record.filter_records[i].filter->id().c_str());
			std::set<std::string> set_2;
			for(int i = 0; i < record.filter_records.size(); ++i)
				set_2.insert(record.filter_records[i].filter->id().c_str());
			if(set_1 == set_2) {
				add_record = false;
				// relplace 'field_after' for record at the top of 'history_before', to do an actual compression
				for(int i = 0; i < last_record.filter_records.size(); ++i) {
					eh_filter_record_t &fr_last = last_record.filter_records[i];
					for(int j = 0; j < record.filter_records.size(); ++j) {
						if(record.filter_records[j].filter->id() == fr_last.filter->id()) {
							//--
							eh_filter_record_t &fr = record.filter_records[j];
							for(std::list<class field_delta_t>::iterator it_last = fr_last.deltas.begin(); it_last != fr_last.deltas.end(); ++it_last) {
								for(std::list<class field_delta_t>::iterator it = fr.deltas.begin(); it != fr.deltas.end(); ++it) {
									if((*it_last).field_name == (*it).field_name) {
										(*it_last).field_after = (*it).field_after;
										break;
									}
								}
							}
						}
					}
					last_record.description[i] = last_record.filter_records[i].get_description();
				}
			}
		}
	}
	if(add_record) {
		edit_history->h_after.clear();
		edit_history->h_before.push_back(record);
#if 0
cerr << "add record" << endl;
		for(int i = 0; i < filter_records.size(); ++i) {
			const std::list<class field_delta_t> &deltas = filter_records[i].deltas;
			for(std::list<class field_delta_t>::const_iterator it = deltas.begin(); it != deltas.end(); ++it) {
cerr << "field: " << (*it).field_name << endl;
cerr << "       " << (*it).field_before.serialize() << endl;
cerr << "       " << (*it).field_after.serialize() << endl;
			}
		}
#endif
	}
	//--
	// TODO: how signal to ViewModel what was changed ?
	update_gui_state();
	// update history list widget
}

void EditHistory::slot_action_undo(void) {
	// use active session here - we already know that from previous signals from edit
//cerr << "EditHistory::slot_action_undo()" << endl;
	move(true);
}

void EditHistory::slot_action_redo(void) {
//cerr << "EditHistory::slot_action_redo()" << endl;
	move(false);
}

void EditHistory::move(bool is_undo) {
	// TODO: add 'fast' undo/redo
	eh_record_t record;
	if(is_undo) {
		if(edit_history->h_before.empty())
			return;
//cerr << "undo, h_before.size() == " << edit_history->h_before.size() << "; h_after.size() == " << edit_history->h_after.size() << endl;
		record = edit_history->h_before.back();
		edit_history->h_before.pop_back();
		edit_history->h_after.push_front(record);
	} else {
		if(edit_history->h_after.empty())
			return;
//cerr << "redo, h_before.size() == " << edit_history->h_before.size() << "; h_after.size() == " << edit_history->h_after.size() << endl;
		record = edit_history->h_after.front();
		edit_history->h_after.pop_front();
		edit_history->h_before.push_back(record);
	}
	// transfer record to edit
	list<eh_record_t> l;
	l.push_back(record);
	edit->history_apply(l, is_undo);
	//--
	update_gui_state();
	// update history list widget
}

//==============================================================================
