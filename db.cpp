#include "db.h"
#include <QStandardPaths>
#include <QDir>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDebug>

static QSqlDatabase g_db;

Db::Db()
{

}

static bool execSql(const QString& sql) {
    QSqlQuery q(g_db);
    if (!q.exec(sql)) {
        qDebug() << "[SQL error]" << q.lastError().text();
        qDebug() << "SQL:" << sql;
        return false;
    }
    return true;
}

bool Db::init() {
    if (g_db.isValid() && g_db.isOpen()) return true;

    // 确认 QSQLITE 驱动存在
    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
        qDebug() << "No QSQLITE driver! Available:" << QSqlDatabase::drivers();
        return false;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/stockout.sqlite";
    qDebug() << "DB path =" << path;

    g_db = QSqlDatabase::addDatabase("QSQLITE");
    g_db.setDatabaseName(path);

    if (!g_db.open()) {
        qDebug() << "DB open failed:" << g_db.lastError().text();
        return false;
    }

    bool ok = true;
    auto runSql = [&ok](const QString& sql) {
        ok = execSql(sql) && ok;
    };

    runSql("PRAGMA foreign_keys = ON;");

    // products：加 spec + barcode（条码唯一）
    runSql(R"(CREATE TABLE IF NOT EXISTS products (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        spec TEXT,
        category TEXT,
        barcode TEXT UNIQUE,
        stock_qty INTEGER NOT NULL DEFAULT 0 CHECK(stock_qty >= 0),
        unit_price REAL NOT NULL DEFAULT 0 CHECK(unit_price >= 0)
    );)");

    runSql(R"(CREATE TABLE IF NOT EXISTS companies (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL UNIQUE
    );)");

    // 出库单
    runSql(R"(CREATE TABLE IF NOT EXISTS shipments (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        company_id INTEGER NOT NULL,
        ship_date TEXT NOT NULL,
        total_amount REAL NOT NULL DEFAULT 0,
        note TEXT,
        FOREIGN KEY(company_id) REFERENCES companies(id)
    );)");

    runSql(R"(CREATE TABLE IF NOT EXISTS shipment_items (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        shipment_id INTEGER NOT NULL,
        product_id INTEGER NOT NULL,
        qty INTEGER NOT NULL CHECK(qty > 0),
        unit_price REAL NOT NULL CHECK(unit_price >= 0),
        amount REAL NOT NULL CHECK(amount >= 0),
        FOREIGN KEY(shipment_id) REFERENCES shipments(id) ON DELETE CASCADE,
        FOREIGN KEY(product_id) REFERENCES products(id)
    );)");

    // 入库单
    runSql(R"(CREATE TABLE IF NOT EXISTS receipts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        recv_date TEXT NOT NULL,
        total_qty INTEGER NOT NULL DEFAULT 0,
        note TEXT
    );)");

    runSql(R"(CREATE TABLE IF NOT EXISTS receipt_items (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        receipt_id INTEGER NOT NULL,
        product_id INTEGER NOT NULL,
        qty INTEGER NOT NULL CHECK(qty > 0),
        FOREIGN KEY(receipt_id) REFERENCES receipts(id) ON DELETE CASCADE,
        FOREIGN KEY(product_id) REFERENCES products(id)
    );)");

    runSql("CREATE INDEX IF NOT EXISTS idx_products_name ON products(name);");
    runSql("CREATE INDEX IF NOT EXISTS idx_products_barcode ON products(barcode);");

    if (!ok) {
        qDebug() << "DB schema initialization failed.";
        return false;
    }

    return true;
}

QSqlDatabase Db::db() { return g_db; }
