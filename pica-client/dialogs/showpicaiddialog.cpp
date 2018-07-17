/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "showpicaiddialog.h"
#include <QVBoxLayout>
#include <QFontMetrics>
#include <QStyle>

ShowPicaIdDialog::ShowPicaIdDialog(QString name, QByteArray id, QString caption, QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(caption);
	QVBoxLayout *layout = new QVBoxLayout(this);

	lbname_ = new QLabel(tr("Account: <b>%1</b>").arg(name));
	lbname_ ->setTextFormat(Qt::RichText);

	picaid_ = new QLineEdit();

	picaid_ ->setText(id.toBase64().constData());
	picaid_->setReadOnly(true);
	picaid_->setSelection(0, picaid_->text().size());
	picaid_->setMinimumWidth(picaid_->fontMetrics().width(picaid_->text()) + 2 * picaid_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth));

	btok_ = new QPushButton(tr("OK"));
	btok_->setDefault(true);
	btok_->setMaximumWidth(btok_->fontMetrics().width(btok_->text()) * 3 / 2 + 2 * btok_->style()->pixelMetric(QStyle::PM_ButtonMargin));

	layout->addWidget(lbname_);
	layout->addWidget(picaid_);
	layout->addWidget(btok_, 0, Qt::AlignHCenter);

	connect(btok_, SIGNAL(clicked()), this, SLOT(Ok()));
}

void ShowPicaIdDialog::Ok()
{
	done(0);
}
