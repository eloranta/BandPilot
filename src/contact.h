#ifndef CONTACT_H
#define CONTACT_H

#include <QString>

struct Contact
{
    QString date;
    QString time;
    QString call;
    QString band;
    QString frequency;
    QString mode;
    QString submode;
    QString country;
    QString grid;
    QString rstTx;
    QString rstRx;
    QString qsl;
    QString comment;
};

#endif // CONTACT_H
