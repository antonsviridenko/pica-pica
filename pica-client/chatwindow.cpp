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
	}
	{
		Accounts ac(config_dbname);
		my_name_ = ac.GetName(Accounts::GetCurrentAccount().id);

		if (my_name_.isEmpty())
			my_name_ = Accounts::GetCurrentAccount().id.toBase64();
	}

	layout = new QVBoxLayout;

	lbConnStatus = new QLabel(is_group_ ? tr("Group chat, %1 members").arg(g.members.size()) : status);
	chatw = new QTextEdit(this);
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

	layout->addWidget(lbConnStatus, 0);
	layout->addWidget(chatw, 3);
	layout->addWidget(sendtextw, 1, Qt::AlignBottom);

	if (addcontactquestion)
	{
		QHBoxLayout *hl = new QHBoxLayout();

		lbAddCtQuestion = new QLabel(tr("Do you want to add \"%1\" to your contact list?").arg(peer_name_));
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

}

void ChatWindow::update_status(QString status)
{
	if (is_group_)
		return;

	lbConnStatus->setText(status);
}

QString ChatWindow::name_of(QByteArray id)
{
	if (id == Accounts::GetCurrentAccount().id)
		return my_name_;

	if (id == peer_id_)
		return peer_name_;

	QString name = Contacts(config_dbname, Accounts::GetCurrentAccount().id).GetContactName(id);

	return name.isEmpty() ? QString(id.toBase64()) : name;
}

void ChatWindow::put_message(QString msg, QByteArray id, bool is_me, QByteArray msg_id)
{
	int pos;

	pos = draw_message(msg, name_of(id), QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"),
	                   is_me ? "#2a6fdb" : "#c0392b", false);

	if (is_me)
		undelivered_msgs[msg_id] = pos;
}

int ChatWindow::draw_message(QString msg, QString nickname, QString datetime, QString color, bool is_delivered)
{
	QTextCharFormat fmt;
	QString delivered_mark;
	int ret;

	if (is_delivered)
		delivered_mark = "&#x2713;";
	else
		delivered_mark = "&nbsp;";

	QTextCursor c(chatw->document());
	c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);

	ret = c.position();

	chatw->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
	fmt = chatw->currentCharFormat();
	chatw->insertHtml(
	    QString("<b><font color=%1>%2(%3) %4 : </font></b>").
	    arg(color).
	    arg(delivered_mark).
	    arg(datetime).
	    arg(nickname)
	);
	chatw->setCurrentCharFormat(fmt);
	chatw->insertPlainText(msg + '\n');

	chatw->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);

	return ret;
}

void ChatWindow::print_history(QList<History::HistoryRecord> H)
{
	chatw->clear();
	undelivered_msgs.clear(); // positions refer to the cleared text

	while(!H.isEmpty())
	{
		History::HistoryRecord r = H.takeFirst();
		QDateTime dt;

		dt.setTime_t(r.timestamp);
		draw_message(r.message, name_of(hist.Author(r)), dt.toString("yyyy-MM-dd hh:mm:ss"),
		             r.is_me ? "#2a6fdb" : "#c0392b", r.is_delivered);
	}
}

void ChatWindow::msg_from_peer(QByteArray author, QString msg)
{
	if (isMinimized())
		showNormal();

	put_message(msg, author, false);
}

void ChatWindow::msg_delivered(QByteArray msg_id)
{
	// messages sent before this window was opened are not shown, and are already marked in history
	if (!undelivered_msgs.contains(msg_id))
		return;

	QTextCursor c(chatw->document());
	c.setPosition(undelivered_msgs.take(msg_id));
	c.deleteChar();
	c.insertText(QString(QChar(0x2713)));
}

void ChatWindow::msg_informational(QString text)
{
	QString color;
	QTextCharFormat fmt;

	color = "gray";

	fmt = chatw->currentCharFormat();

	chatw->insertHtml(
	    QString("<i><font color=%1>(%2)</font></i>").
	    arg(color).
	    arg(text)
	);
	chatw->insertPlainText("\n");

	chatw->setCurrentCharFormat(fmt);
	chatw->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
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

	if (edit_group_dialog(this, &g.name, &g.members, g.owner == me) && g.owner == me)
		skynet->SaveGroup(g.id, g.name, g.members);
}

bool edit_group_dialog(QWidget *parent, QString *name, QList<QByteArray> *members, bool editable)
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
		QListWidgetItem *it = new QListWidgetItem(QString("(%1...) %2").arg(ids[i].toBase64().left(8).constData()).arg(cname), lw);

		it->setFlags(editable ? Qt::ItemIsUserCheckable | Qt::ItemIsEnabled : Qt::ItemIsEnabled);
		it->setCheckState(members->contains(ids[i]) ? Qt::Checked : Qt::Unchecked);
	}

	le->setReadOnly(!editable);
	form->addRow(QObject::tr("Group name:"), le);
	form->addRow(QObject::tr("Members:"), lw);
	form->addRow(bb);
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

