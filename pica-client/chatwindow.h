/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include <QTextEdit>
#include <QList>
#include <QMenuBar>
#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QLabel>
#include <QSet>
#include <QDate>
#include "history.h"

class TextSend : public QTextEdit
{
	Q_OBJECT
public:
	explicit TextSend(QWidget *parent);
signals:
	void send_pressed();
private:
	void keyPressEvent(QKeyEvent *e);

};

class ChatWindow : public QWidget
{
	Q_OBJECT
public:
	// peer_id is a contact's ID or a group ID
	explicit ChatWindow(QByteArray peer_id, QString status);

	QByteArray getPeerId()
	{
		return peer_id_;
	};

	QString getPeerName()
	{
		return peer_name_;
	};

	QString name_of(QByteArray id); // display name of me, the peer or a group member

	bool isGroup()
	{
		return is_group_;
	};

signals:
	void msg_input(QString msg, QByteArray msg_id, ChatWindow *sender_window);
	void chatwindow_close(ChatWindow *sender_window);
	void contacts_update();
	void conversation_read();

public slots:
	void msg_from_peer(QByteArray author, QString msg, QByteArray msg_id);
	void msg_delivered(QByteArray msg_id);
	void msg_failed(QByteArray msg_id);
	void msg_informational(QString text);//юзер в оффлайне, набирает сообщение, и т.д. серым шрифтом, курсив
	void set_peer_name();
	void update_status(QString status);


private:
	QTextEdit *chatw;
	TextSend *sendtextw;
	QByteArray peer_id_;
	QMap<QByteArray, int> undelivered_msgs; // msg_id -> position of the delivery mark
	bool is_group_;
	History hist;
	QMenuBar *menu;
	QAction *hist24h;
	QAction *hist1week;
	QAction *histAll;
	QAction *histSearch;
	QAction *histVerify;
	QAction *groupMembers;
	QString peer_name_;
	QString my_name_;
	bool addcontactquestion;
	QPushButton *btAddCtYes;
	QPushButton *btAddCtNo;
	QPushButton *btAddCtBlacklist;
	QLabel *lbAddCtQuestion;
	QLabel *lbConnStatus;
	QPushButton *btNewMsgs; // shown when a message arrives while the view is scrolled up
	QSet<QByteArray> shown_; // IDs of the messages in the view, so none is drawn twice
	QDate last_day_; // day of the last message drawn, for the day separators
	bool connected_;
	bool offline_note_shown_;

	void set_mark(QByteArray msg_id, QChar mark);
	void mark_read();
	void changeEvent(QEvent *e);

	bool isEmptyMessage(const QString &msg) const;
	void put_message(QString msg, QByteArray id, bool is_me, QByteArray msg_id = QByteArray());
	int draw_message(QString msg, QString nickname, QDateTime when, QString color, QChar mark);
	void print_history(QList<History::HistoryRecord> H);

	void closeEvent(QCloseEvent *e);

private slots:
	void send_message();

	void show_history24h();
	void show_history1w();
	void show_historyAll();
	void show_historySearch();
	void verify_history();
	void edit_members();

	void addct_yes();
	void addct_no();
	void addct_blacklist();
	void addct_removequestion();
};

// Lets the user pick a group name and members among the contacts. Returns false if cancelled.
// owner marks that member in the list; leave, when given, adds a leave/delete button and reports its use
bool edit_group_dialog(QWidget *parent, QString *name, QList<QByteArray> *members, bool editable,
                       QByteArray owner = QByteArray(), bool *leave = nullptr);

#endif // CHATWINDOW_H
