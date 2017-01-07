/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_SIMPLECW_TEXTAREA
#define H_SIMPLECW_TEXTAREA





#include "config.h"





#include <QWidget>
#include <QTextEdit>
#include <QEvent>
#include <QMenu>




class Application;

class TextArea : public QTextEdit {
	//Q_OBJECT
 public:
	TextArea(Application *application, QWidget *parent = 0);
	~TextArea() {};

	void append(char c);
	void backspace();

 protected:
	// Functions overridden to catch events from the parent class.
	void keyPressEvent(QKeyEvent *event);
	void keyReleaseEvent(QKeyEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	// Are these necessary after adding fontPointSize() in constructor?
	virtual QMenu *createPopupMenu(const QPoint &);
	virtual QMenu *createPopupMenu();

 private:
	// Application to forward key and mouse events to.
	Application *app;

	// Prevent unwanted operations.
	TextArea(const TextArea &);
	TextArea &operator=(const TextArea &);
};




#endif /* #ifndef H_SIMPLECW_TEXTAREA */
