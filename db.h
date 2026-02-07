#ifndef DB_H
#define DB_H
#pragma once
#include <QtSql/QSqlDatabase>

class Db
{
public:
    Db();
public:
    static bool init();
    static QSqlDatabase db();
};

#endif // DB_H
