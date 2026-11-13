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
#include "chatwindow.h"
#include "globals.h"
#include "contacts.h"
#include "accounts.h"
#include "skynet.h"
#include "msguirouter.h"
#include "filetransfercontroller.h"
#include <QVBoxLayout>
#include <QDateTime>
#include <QAction>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QUuid>
#include <QTextBrowser>
#include <QScrollBar>
#include <QRegularExpression>
#include <QLocale>
#include <QEvent>


ChatWindow::ChatWindow(QByteArray peer_id, QString status) :
	QWidget(0), peer_id_(peer_id), hist(config_dbname, Accounts::GetCurrentAccount().id)
{
	QVBoxLayout *layout;
	Groups::GroupRecord g;

	is_group_ = Groups(config_dbname, Accounts::GetCurrentAccount().id).Get(peer_id_, &g);
	addcontactquestion = false;

	set_peer_name();

	if (!is_group_)
	{
		Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);

		QList<Contacts::ContactRecord>  L = ct.GetContacts(Contacts::temporary);
		addcontactquestion = false;
		for (int i = 0; i < L.size(); i++)
		{
			if (L[i].id == peer_id_)
			{
				addcontactquestion = true;
				break;
			}
		}

		// someone I share a group with is expected to write to me
		QList<Groups::GroupRecord> groups = Groups(config_dbname, Accounts::GetCurrentAccount().id).GetGroups();

		for (int i = 0; i < groups.size(); i++)
			if (groups[i].members.contains(peer_id_))
				addcontactquestion = false;
	}
	{
		Accounts ac(config_dbname);
		my_name_ = ac.GetName(Accounts::GetCurrentAccount().id);

		if (my_name_.isEmpty())
			my_name_ = Accounts::GetCurrentAccount().id.toBase64();
	}

	layout = new QVBoxLayout;

	lbConnStatus = new QLabel(is_group_ ? tr("Group chat, %1 members").arg(g.members.size()) : status);
	{
		QTextBrowser *b = new QTextBrowser(this);
		b->setOpenExternalLinks(true); // only http(s) links are ever made, see message_html()
		chatw = b;
	}
	btNewMsgs = new QPushButton(tr("New messages below"), this);
	btNewMsgs->hide();
	connected_ = MsgUIRouter::status_connected(status);
	offline_note_shown_ = false;
	sendtextw = new TextSend(this);

	connect(sendtextw, SIGNAL(send_pressed()), this, SLOT(send_message()));

	sendtextw->setMinimumHeight(48);
	sendtextw->setMinimumWidth(240);
	sendtextw->setAcceptRichText(false);

	{
		QFont f = lbConnStatus->font();
		f.setBold(true);
		//f.setItalic(true);
		f.setStyleHint(QFont::Monospace);
		lbConnStatus->setFont(f);
	}

	chatw->setReadOnly(true);

	connect(btNewMsgs, &QPushButton::clicked, this, [this] { chatw->moveCursor(QTextCursor::End); });
	connect(chatw->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v)
	{
		if (v >= chatw->verticalScrollBar()->maximum() - 4)
			btNewMsgs->hide();
	});

	layout->addWidget(lbConnStatus, 0);
	layout->addWidget(chatw, 3);
	layout->addWidget(btNewMsgs, 0);
	if (is_group_)
		layout->addWidget(sendtextw, 1, Qt::AlignBottom);
	else
	{
		// files go to one peer over its connection; there is no group fan-out for them
		QHBoxLayout *hl = new QHBoxLayout();
		QPushButton *btSendFile = new QPushButton(tr("Send file..."), this);

		connect(btSendFile, &QPushButton::clicked, this, [this] { ftctrl->send_file(peer_id_); });
		hl->addWidget(sendtextw, 1);
		hl->addWidget(btSendFile, 0, Qt::AlignBottom);
		layout->addLayout(hl, 1);
	}

	if (addcontactquestion)
	{
		QHBoxLayout *hl = new QHBoxLayout();

		lbAddCtQuestion = new QLabel(tr("Do you want to add \"%1\" to your contact list?").arg(peer_name_));
		// peer_name_ comes from the peer's certificate; keep it literal so it
		// cannot be interpreted as rich text.
		lbAddCtQuestion->setTextFormat(Qt::PlainText);
		btAddCtYes = new QPushButton(tr("Yes"));
		btAddCtNo = new QPushButton(tr("No"));
		btAddCtBlacklist = new QPushButton(tr("Add to blacklist"));

		layout->insertWidget(2, lbAddCtQuestion, Qt::AlignLeft);

		hl->addWidget(btAddCtYes);
		hl->addWidget(btAddCtNo);
		hl->addWidget(btAddCtBlacklist);

		layout->insertLayout(3, hl);

		connect(btAddCtYes, SIGNAL(clicked()), this, SLOT(addct_yes()));
		connect(btAddCtNo, SIGNAL(clicked()), this, SLOT(addct_no()));
		connect(btAddCtBlacklist, SIGNAL(clicked()), this, SLOT(addct_blacklist()));
	}

	setLayout(layout);

	setWindowIcon(picapica_ico_sit);

	QWidget::setAttribute(Qt::WA_DeleteOnClose);
	QWidget::setAttribute(Qt::WA_QuitOnClose, false);

	menu = new QMenuBar(this);
	hist24h = new QAction(tr("Last 24 &Hours"), this);
	hist1week = new QAction(tr("Last &Week"), this);
	histAll = new QAction(tr("&All"), this);
	histSearch = new QAction(tr("&Search History..."), this);
	histVerify = new QAction(tr("&Verify Integrity"), this);
	groupMembers = new QAction(tr("&Members..."), this);

	{
		QMenu *m, *h;
		h = menu->addMenu(tr("&History"));
		m = h->addMenu(tr("Show History"));
		m->addAction(hist24h);
		m->addAction(hist1week);
		m->addAction(histAll);
		m->addAction(histSearch);
		h->addAction(histVerify);

		if (is_group_)
			menu->addMenu(tr("&Group"))->addAction(groupMembers);
	}
	layout->insertSpacing(0, menu->size().height());

	connect(hist24h, SIGNAL(triggered()), this, SLOT(show_history24h()));
	connect(hist1week, SIGNAL(triggered()), this, SLOT(show_history1w()));
	connect(histAll, SIGNAL(triggered()), this, SLOT(show_historyAll()));
	connect(histSearch, SIGNAL(triggered()), this, SLOT(show_historySearch()));
	connect(histVerify, SIGNAL(triggered()), this, SLOT(verify_history()));
	connect(groupMembers, SIGNAL(triggered()), this, SLOT(edit_members()));

	print_history(hist.GetLast(peer_id_, is_group_, 50));
}

void ChatWindow::update_status(QString status)
{
	if (is_group_)
		return;

	lbConnStatus->setText(status);
	connected_ = MsgUIRouter::status_connected(status);

	if (connected_)
		offline_note_shown_ = false;
}

QString ChatWindow::name_of(QByteArray id)
{
	if (id == Accounts::GetCurrentAccount().id)
		return my_name_;

	if (id == peer_id_)
		return peer_name_;

	QString name = Contacts(config_dbname, Accounts::GetCurrentAccount().id).GetContactName(id);

	return name.isEmpty() ? QString(id.toBase64().left(12)) + "..." : name;
}

// delivery marks; each is one character, so one can replace another in place
static const QChar mark_none(0x00A0);
static const QChar mark_pending(0x2026);
static const QChar mark_delivered(0x2713);
static const QChar mark_failed(0x2717);

static const char *color_me = "#2a6fdb";
static const char *color_peer = "#c0392b";

// message text as HTML: everything escaped, http(s) URLs turned into links
static QString message_html(const QString &msg)
{
	static const QRegularExpression url("https?://[^\\s<>\"]+");
	QRegularExpressionMatchIterator it = url.globalMatch(msg);
	QString html;
	int pos = 0;

	while (it.hasNext())
	{
		QRegularExpressionMatch m = it.next();
		QString u = m.captured().toHtmlEscaped();

		html += msg.mid(pos, m.capturedStart() - pos).toHtmlEscaped();
		html += "<a href=\"" + u + "\">" + u + "</a>";
		pos = m.capturedEnd();
	}

	html += msg.mid(pos).toHtmlEscaped();
	html.replace('\n', "<br>");

	return "<span style=\"white-space: pre-wrap;\">" + html + "</span>";
}

void ChatWindow::put_message(QString msg, QByteArray id, bool is_me, QByteArray msg_id)
{
	int pos;

	pos = draw_message(msg, name_of(id), QDateTime::currentDateTime(),
	                   is_me ? color_me : color_peer, is_me ? mark_pending : mark_none);

	shown_.insert(msg_id);

	if (is_me)
		undelivered_msgs[msg_id] = pos;
}

// Appends at the end of the document whatever the user has selected or clicked,
// and returns the position of the delivery mark.
int ChatWindow::draw_message(QString msg, QString nickname, QDateTime when, QString color, QChar mark)
{
	QScrollBar *sb = chatw->verticalScrollBar();
	bool at_bottom = sb->value() >= sb->maximum() - 4;
	QTextCursor c(chatw->document());
	int ret;

	c.movePosition(QTextCursor::End);

	if (when.date() != last_day_)
	{
		last_day_ = when.date();
		c.insertHtml(QString("<i><font color=gray>&mdash; %1 &mdash;</font></i>")
		             .arg(QLocale().toString(last_day_, QLocale::LongFormat).toHtmlEscaped()));
		c.insertText("\n", QTextCharFormat());
	}

	ret = c.position();

	c.insertHtml(
	    QString("<b><font color=%1>%2(%3) %4 : </font></b>").
	    arg(color).
	    arg(QString(mark)).
	    arg(when.toString("hh:mm:ss")).
	    arg(nickname.toHtmlEscaped())
	);
	c.insertHtml(message_html(msg));
	c.insertText("\n", QTextCharFormat());

	// follow the conversation only while the user is at its end
	if (at_bottom)
		chatw->setTextCursor(c);
	else
		btNewMsgs->show();

	return ret;
}

void ChatWindow::print_history(QList<History::HistoryRecord> H)
{
	chatw->clear();
	undelivered_msgs.clear(); // positions refer to the cleared text
	shown_.clear();
	last_day_ = QDate();
	btNewMsgs->hide();

	while(!H.isEmpty())
	{
		History::HistoryRecord r = H.takeFirst();
		QDateTime dt;
		int pos;

		dt.setTime_t(r.timestamp);
		pos = draw_message(r.message, name_of(hist.Author(r)), dt, r.is_me ? color_me : color_peer,
		                   !r.is_me ? mark_none : r.is_delivered ? mark_delivered : mark_pending);

		shown_.insert(r.msg_id);

		if (r.is_me && !r.is_delivered)
			undelivered_msgs[r.msg_id] = pos;
	}
}

void ChatWindow::msg_from_peer(QByteArray author, QString msg, QByteArray msg_id)
{
	if (isMinimized())
		showNormal();

	// a window opened by this very message has already drawn it from the history
	if (!shown_.contains(msg_id))
		put_message(msg, author, false, msg_id);

	if (isActiveWindow())
		mark_read();
}

void ChatWindow::set_mark(QByteArray msg_id, QChar mark)
{
	// messages that are not in the view are already marked in history
	if (!undelivered_msgs.contains(msg_id))
		return;

	QTextCursor c(chatw->document());
	c.setPosition(undelivered_msgs.take(msg_id));
	c.deleteChar();
	c.insertText(QString(mark));
}

void ChatWindow::msg_delivered(QByteArray msg_id)
{
	set_mark(msg_id, mark_delivered);
}

void ChatWindow::msg_failed(QByteArray msg_id)
{
	set_mark(msg_id, mark_failed);
}

void ChatWindow::mark_read()
{
	hist.MarkRead(peer_id_, is_group_);
	emit conversation_read();
}

void ChatWindow::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::ActivationChange && isActiveWindow())
		mark_read();

	QWidget::changeEvent(e);
}

void ChatWindow::msg_informational(QString text)
{
	QScrollBar *sb = chatw->verticalScrollBar();
	bool at_bottom = sb->value() >= sb->maximum() - 4;
	QTextCursor c(chatw->document());

	c.movePosition(QTextCursor::End);
	c.insertHtml(QString("<i><font color=gray>(%1)</font></i>").arg(text.toHtmlEscaped()));
	c.insertText("\n", QTextCharFormat());

	if (at_bottom)
		chatw->setTextCursor(c);
}

bool ChatWindow::isEmptyMessage(const QString &msg) const
{
	int size = msg.size();
	for (int i = 0; i < size; ++i)
	{
		if (!msg.at(i).isSpace())
		{
			return false;
		}
	}
	return true;
}

void ChatWindow::send_message()
{
	QString message = sendtextw->toPlainText();
	if (!isEmptyMessage(message))
	{
		QByteArray msg_id = QUuid::createUuid().toRfc4122();

		put_message(message, Accounts::GetCurrentAccount().id, true, msg_id);
		chatw->moveCursor(QTextCursor::End); // my own message is always brought into view

		if (!is_group_ && !connected_ && !offline_note_shown_)
		{
			msg_informational(tr("Not connected - the message will be sent when the contact is reachable"));
			offline_note_shown_ = true;
		}

		emit msg_input(message, msg_id, this);

		sendtextw->clear();
	}
}

void ChatWindow::closeEvent(QCloseEvent *e)
{
	emit chatwindow_close(this);

	e->accept();
}

TextSend::TextSend(QWidget *parent) :
	QTextEdit(parent)
{

}

void TextSend::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Return && e->modifiers() == Qt::NoModifier )
	{
		emit send_pressed();
		return;
	}

	QTextEdit::keyPressEvent(e);
}

void ChatWindow::show_history24h()
{
	print_history(hist.GetMessages(peer_id_, is_group_, QDateTime::currentDateTime().addDays(-1).toTime_t(),
	                               QDateTime::currentDateTime().toTime_t()));
}

void ChatWindow::show_history1w()
{
	print_history(hist.GetMessages(peer_id_, is_group_, QDateTime::currentDateTime().addDays(-7).toTime_t(),
	                               QDateTime::currentDateTime().toTime_t()));
}

void ChatWindow::show_historyAll()
{
	print_history(hist.GetMessages(peer_id_, is_group_, 0, QDateTime::currentDateTime().toTime_t()));
}

void ChatWindow::show_historySearch()
{
	bool ok;
	QString keyword = QInputDialog::getText(this, tr("Search in message history"),
	                                     tr("Find:"), QLineEdit::Normal,
	                                     QString(), &ok);

	if (ok)
		print_history(hist.GetMessages(peer_id_, is_group_, keyword));
}

void ChatWindow::verify_history()
{
	History::ChainReport r = hist.VerifyChain(peer_id_, is_group_);
	QMessageBox mbx;

	mbx.setIcon(r.bad_signatures || r.broken_links ? QMessageBox::Warning : QMessageBox::Information);
	mbx.setText(tr("Messages: %1\nSigned and valid: %2\nUnsigned (from older clients): %3\n"
	               "Invalid signatures: %4\nLinks to missing messages: %5\n\n"
	               "Links to missing messages are normal for messages you were offline for, "
	               "until they are synchronized.")
	            .arg(r.total).arg(r.valid).arg(r.unsigned_msgs).arg(r.bad_signatures).arg(r.broken_links));
	mbx.exec();
}

void ChatWindow::edit_members()
{
	Groups::GroupRecord g;
	QByteArray me = Accounts::GetCurrentAccount().id;

	if (!Groups(config_dbname, me).Get(peer_id_, &g))
		return;

	bool leave = false;

	if (edit_group_dialog(this, &g.name, &g.members, g.owner == me, g.owner, &leave) && g.owner == me)
		skynet->SaveGroup(g.id, g.name, g.members);

	if (leave)
	{
		skynet->LeaveGroup(g.id);
		close();
	}
}

bool edit_group_dialog(QWidget *parent, QString *name, QList<QByteArray> *members, bool editable, QByteArray owner, bool *leave)
{
	QByteArray me = Accounts::GetCurrentAccount().id;
	Contacts ct(config_dbname, me);
	QList<Contacts::ContactRecord> contacts = ct.GetContacts();
	QList<QByteArray> ids;
	QDialog d(parent);
	QFormLayout *form = new QFormLayout(&d);
	QLineEdit *le = new QLineEdit(*name);
	QListWidget *lw = new QListWidget;
	QDialogButtonBox *bb = new QDialogButtonBox(editable ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Close);

	for (int i = 0; i < contacts.size(); i++)
		ids << contacts[i].id;

	for (int i = 0; i < members->size(); i++)
		if (!ids.contains(members->at(i)) && members->at(i) != me)
			ids << members->at(i);

	for (int i = 0; i < ids.size(); i++)
	{
		QString cname = ct.GetContactName(ids[i]);
		QListWidgetItem *it = new QListWidgetItem(QString("(%1...) %2%3").arg(ids[i].toBase64().left(8).constData()).arg(cname)
		                                          .arg(ids[i] == owner ? QObject::tr(" - owner") : QString()), lw);

		it->setFlags(editable ? Qt::ItemIsUserCheckable | Qt::ItemIsEnabled : Qt::ItemIsEnabled);
		it->setCheckState(members->contains(ids[i]) ? Qt::Checked : Qt::Unchecked);
	}

	le->setReadOnly(!editable);
	form->addRow(QObject::tr("Group name:"), le);
	form->addRow(QObject::tr("Members:"), lw);
	form->addRow(bb);

	// an existing group can be left; its owner can only dissolve it
	if (leave)
	{
		QPushButton *pb = bb->addButton(owner == me ? QObject::tr("Delete group") : QObject::tr("Leave group"), QDialogButtonBox::DestructiveRole);

		QObject::connect(pb, &QPushButton::clicked, &d, [&d, leave]
		{
			if (QMessageBox::question(&d, QObject::tr("Group"), QObject::tr("Are you sure?")) == QMessageBox::Yes)
			{
				*leave = true;
				d.reject();
			}
		});
	}

	QObject::connect(bb, SIGNAL(accepted()), &d, SLOT(accept()));
	QObject::connect(bb, SIGNAL(rejected()), &d, SLOT(reject()));
	d.setWindowTitle(QObject::tr("Group"));

	if (d.exec() != QDialog::Accepted || !editable)
		return false;

	*name = le->text().trimmed();
	members->clear();

	for (int i = 0; i < ids.size(); i++)
		if (lw->item(i)->checkState() == Qt::Checked)
			*members << ids[i];

	return true;
}

void ChatWindow::addct_yes()
{
	Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);

	ct.Add(peer_id_, Contacts::regular);
	addct_removequestion();

	emit contacts_update();
}

void ChatWindow::addct_no()
{
	addct_removequestion();
}

void ChatWindow::addct_blacklist()
{
	Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);

	ct.Add(peer_id_, Contacts::blacklisted);
	addct_removequestion();
}

void ChatWindow::addct_removequestion()
{
	if (addcontactquestion)
	{
		delete btAddCtYes;
		delete btAddCtNo;
		delete btAddCtBlacklist;
		delete lbAddCtQuestion;
		addcontactquestion = false;
	}
}

void ChatWindow::set_peer_name()
{
	QString title;
	Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);
	Groups::GroupRecord g;

	if (Groups(config_dbname, Accounts::GetCurrentAccount().id).Get(peer_id_, &g))
	{
		peer_name_ = g.name;
		setWindowTitle(QObject::tr("Group chat: ") + g.name);
		return;
	}

	peer_name_ = ct.GetContactName(peer_id_);

	title = peer_name_;

	if (peer_name_.isEmpty())
		peer_name_ = peer_id_.toBase64();


	title = QObject::tr("Chat with ") + title + " - " + peer_id_.toBase64();
	setWindowTitle(title);
}

