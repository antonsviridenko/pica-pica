#ifndef DHPARAM_H
#define DHPARAM_H

#include <QObject>
#include <QString>
#include "globals.h"
#include "openssltool.h"

class DHParam : public QObject
{
    Q_OBJECT
public:
    explicit DHParam(QObject *parent = 0);
    static bool VerifyDefault(QString *errormsg);
    static bool VerifyGenerated(QString *errormsg);
    static bool UseDefault();
    static bool StartNewDHParamGeneration(QString *errormsg);

    static QString GetDHParamFilename();

signals:
    
public slots:

private:
    static DHParam *dhparamgenerator;
    static QString current_dhparam_file;

    OpenSSLTool osslt;

private slots:
    void DHParamGenFinished(int retval, QProcess::ExitStatus);
};

#endif // DHPARAM_H
