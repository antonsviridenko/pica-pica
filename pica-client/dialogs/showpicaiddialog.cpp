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
