#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSqlTableModel>
#include <QStandardItemModel>
#include "initemmodel.h"
#include "outitemmodel.h"
#include <QStandardItem>
#include <QSettings>
#include <QCompleter>
#include <QSortFilterProxyModel>
#include <QStringListModel>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void onPSearch();
    void onPReset();
    void onPAdd();
    void onPDel();
    void onPSave();
    void onPRevert();

    void onCSearch();
    void onCReset();
    void onCAdd();
    void onCDel();
    void onCSave();
    void onCRevert();

    void onInBarcodeEnter();
    void onInAdd();
    void onInCommit();
    void onInClear();

    void onOutBarcodeEnter();
    void onOutAdd();
    void onOutCommit();
    void onOutExport();
    void onOutClear();

    void tableZoomIn();
    void tableZoomOut();
    void tableZoomReset();
    void onCToggleActive();
    void onCompaniesContextMenu(const QPoint& pos);
    void onPToggleActive();
    void onProductsContextMenu(const QPoint& pos);
private:
    void inAddProductByBarcode(const QString& barcode, int qty);
    void inRecalcTotal();
private:
    void reloadCompanyCombo();
    void outAddByBarcode(const QString& barcode, int qty);
    void outRecalcTotal();
    bool exportShipmentCsv(int shipmentId, const QString& path, QString* err = nullptr);
    static QString csvEscape(const QString& s);
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
private:
    void saveTableStates();
    void restoreTableStates();
private:
    Ui::MainWindow *ui;
private:
    QSqlTableModel* m_products = nullptr;
    QSqlTableModel* m_companies = nullptr;
    InItemModel* m_inModel = nullptr;
    int m_inTotalQty = 0;
    OutItemModel* m_outModel = nullptr;
    int m_lastShipmentId = -1;
    static QString safeFileName(QString s);
    static QString escSql(const QString& s);
    QCompleter* m_companyCompleter = nullptr; // ✅ 出库公司补全器（给 eventFilter 用）
    QStringListModel* m_companySuggestModel = nullptr;

private:
    QString buildShipmentHtml(int shipmentId, QString* err = nullptr);
    bool printShipment(int shipmentId, QString* err = nullptr);
    int m_tableFontPt = 10;          // 表格字体大小（只影响表格）
    void applyTableFont(bool save);  // 应用到所有 QTableView
    void ensureCompaniesSchema();
    bool companyHasShipments(int companyId) const;
    void ensureProductsSchema();
    bool productHasRefs(int productId) const;

};
#endif // MAINWINDOW_H
