#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QSqlError>
#include <QSqlQuery>
#include <QDate>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QDir>
#include <QtPrintSupport/QPrintPreviewDialog>
#include <QtPrintSupport/QPrinter>
#include <QTextDocument>
#include <algorithm>
#include <QTableView>
#include <QHeaderView>
#include <QEvent>
#include <QWheelEvent>
#include <QApplication>
#include "elidetooltipdelegate.h"
#include <QCompleter>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QListView>
#include <QKeyEvent>
#include <QStringListModel>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>

namespace {
QStringList unitOptions() {
    return {"瓶", "袋", "盒", "桶", "箱"};
}

class UnitComboDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        auto *combo = new QComboBox(parent);
        combo->addItems(unitOptions());
        combo->setEditable(false);
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) return;
        const QString value = index.data(Qt::EditRole).toString();
        int idx = combo->findText(value);
        if (idx < 0) idx = 0;
        combo->setCurrentIndex(idx);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) return;
        model->setData(index, combo->currentText());
    }
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ensureCompaniesSchema();
    ensureProductsSchema();
    // 读取上次保存的“表格字号”
    QSettings st("YourCompany", "StockOutApp");
    m_tableFontPt = st.value("ui/tableFontPt", 10).toInt();
    applyTableFont(false);

    // 菜单：视图 → 表格字体放大/缩小/重置
    auto viewMenu = menuBar()->addMenu("视图");

    auto actTblIn  = new QAction("表格字体放大", this);
    auto actTblOut = new QAction("表格字体缩小", this);
    auto actTblRst = new QAction("表格字体重置", this);

    actTblIn->setShortcut(QKeySequence("Ctrl+Shift+="));
    actTblOut->setShortcut(QKeySequence("Ctrl+Shift+-"));
    actTblRst->setShortcut(QKeySequence("Ctrl+Shift+0"));

    connect(actTblIn,  &QAction::triggered, this, &MainWindow::tableZoomIn);
    connect(actTblOut, &QAction::triggered, this, &MainWindow::tableZoomOut);
    connect(actTblRst, &QAction::triggered, this, &MainWindow::tableZoomReset);

    viewMenu->addAction(actTblIn);
    viewMenu->addAction(actTblOut);
    viewMenu->addAction(actTblRst);

    addAction(actTblIn);
    addAction(actTblOut);
    addAction(actTblRst);

    // ===== products =====
    // ✅ 必须先创建/加载 products 模型，否则 m_products 为空会直接崩溃
    m_products = new QSqlTableModel(this);
    m_products->setTable("products");
    m_products->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_products->select();


    ui->tvProducts->setModel(m_products);
    ui->tvProducts->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvProducts->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tvProducts->setSortingEnabled(true);

    // ✅ 隐藏ID列（更稳：用字段名）
    {
        int colId = m_products->fieldIndex("id");
        if (colId < 0) colId = 0;
        ui->tvProducts->hideColumn(colId);
    }

    // ✅ 左侧行号=序号
    ui->tvProducts->verticalHeader()->setVisible(true);
    ui->tvProducts->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui->tvProducts->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->tvProducts->verticalHeader()->setFixedWidth(50);

    // ✅ 表头
    auto setProductHeader = [this](const QString& field, const QString& title) {
        const int col = m_products->fieldIndex(field);
        if (col >= 0) m_products->setHeaderData(col, Qt::Horizontal, title);
    };
    setProductHeader("id", "ID");
    setProductHeader("name", "名称");
    setProductHeader("spec", "规格");
    setProductHeader("unit", "单位");
    setProductHeader("category", "分类");
    setProductHeader("barcode", "条码");
    setProductHeader("stock_qty", "库存");
    setProductHeader("unit_price", "单价");

    const int colSpec = m_products->fieldIndex("spec");
    const int colUnit = m_products->fieldIndex("unit");
    if (colSpec >= 0 && colUnit >= 0 && colUnit != colSpec + 1) {
        ui->tvProducts->horizontalHeader()->moveSection(colUnit, colSpec + 1);
    }

    int colActiveP = m_products->fieldIndex("is_active");
    if (colActiveP >= 0) {
        m_products->setHeaderData(colActiveP, Qt::Horizontal, "启用(1/0)");
    }

    if (colUnit >= 0) {
        ui->tvProducts->setItemDelegateForColumn(colUnit, new UnitComboDelegate(this));
    }

    ui->tvProducts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tvProducts, &QTableView::customContextMenuRequested,
            this, &MainWindow::onProductsContextMenu);

    auto menuProduct = menuBar()->addMenu("产品");
    auto actPToggle = new QAction("停用/启用", this);
    actPToggle->setShortcut(QKeySequence("Ctrl+P"));
    menuProduct->addAction(actPToggle);
    connect(actPToggle, &QAction::triggered, this, &MainWindow::onPToggleActive);

    connect(ui->btnPSearch, &QPushButton::clicked, this, &MainWindow::onPSearch);
    connect(ui->btnPReset,  &QPushButton::clicked, this, &MainWindow::onPReset);
    connect(ui->btnPAdd,    &QPushButton::clicked, this, &MainWindow::onPAdd);
    connect(ui->btnPDel,    &QPushButton::clicked, this, &MainWindow::onPDel);
    connect(ui->btnPSave,   &QPushButton::clicked, this, &MainWindow::onPSave);
    connect(ui->btnPRevert, &QPushButton::clicked, this, &MainWindow::onPRevert);

    connect(ui->tvProducts->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&){
                if (!current.isValid()) return;
                const int row = current.row();

                auto getField = [this, row](const QString& field) -> QString {
                    const int col = m_products->fieldIndex(field);
                    if (col < 0) return {};
                    return m_products->data(m_products->index(row, col)).toString();
                };
                const QString msg = QString("名称：%1   规格：%2   单位：%3   条码：%4")
                                        .arg(getField("name"),
                                             getField("spec"),
                                             getField("unit"),
                                             getField("barcode"));
                statusBar()->showMessage(msg);
            });

    // ===== companies =====
    m_companies = new QSqlTableModel(this);
    m_companies->setTable("companies");
    m_companies->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_companies->select();

    ui->tvCompanies->setModel(m_companies);
    ui->tvCompanies->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvCompanies->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tvCompanies->setSortingEnabled(true);

    // 不显示数据库ID列（✅ 放在 setModel 后）
    {
        int colId = m_companies->fieldIndex("id");
        if (colId < 0) colId = 0;
        ui->tvCompanies->hideColumn(colId);
    }

    // 显示左侧行号（序号）
    ui->tvCompanies->verticalHeader()->setVisible(true);
    ui->tvCompanies->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui->tvCompanies->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->tvCompanies->verticalHeader()->setFixedWidth(50);  // ✅ 比 minimumWidth 更直接

    m_companies->setHeaderData(0, Qt::Horizontal, "ID");
    m_companies->setHeaderData(1, Qt::Horizontal, "公司名");

    int colActive = m_companies->fieldIndex("is_active");
    if (colActive >= 0) {
        m_companies->setHeaderData(colActive, Qt::Horizontal, "启用(1/0)");
    }

    // 右键菜单
    ui->tvCompanies->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tvCompanies, &QTableView::customContextMenuRequested,
            this, &MainWindow::onCompaniesContextMenu);

    // 菜单快捷键（Ctrl+T）
    auto menuCompany = menuBar()->addMenu("公司");
    auto actToggle = new QAction("停用/启用", this);
    actToggle->setShortcut(QKeySequence("Ctrl+T"));
    menuCompany->addAction(actToggle);
    connect(actToggle, &QAction::triggered, this, &MainWindow::onCToggleActive);

    connect(ui->btnCSearch, &QPushButton::clicked, this, &MainWindow::onCSearch);
    connect(ui->btnCReset,  &QPushButton::clicked, this, &MainWindow::onCReset);
    connect(ui->btnCAdd,    &QPushButton::clicked, this, &MainWindow::onCAdd);
    connect(ui->btnCDel,    &QPushButton::clicked, this, &MainWindow::onCDel);
    connect(ui->btnCSave,   &QPushButton::clicked, this, &MainWindow::onCSave);
    connect(ui->btnCRevert, &QPushButton::clicked, this, &MainWindow::onCRevert);

    connect(m_companies, &QAbstractItemModel::modelReset, this, [this](){
        if (!ui || !ui->tvCompanies || !m_companies) return;
        int colId = m_companies->fieldIndex("id");
        if (colId < 0) colId = 0;
        ui->tvCompanies->hideColumn(colId);
    });


    // ===== 入库明细模型 =====
    m_inModel = new InItemModel(this);
    m_inModel->setColumnCount(7);
    m_inModel->setHeaderData(0, Qt::Horizontal, "product_id");
    m_inModel->setHeaderData(1, Qt::Horizontal, "条码");
    m_inModel->setHeaderData(2, Qt::Horizontal, "名称");
    m_inModel->setHeaderData(3, Qt::Horizontal, "规格");
    m_inModel->setHeaderData(4, Qt::Horizontal, "单位");
    m_inModel->setHeaderData(5, Qt::Horizontal, "数量");
    m_inModel->setHeaderData(6, Qt::Horizontal, "当前库存");

    ui->tvInItems->setModel(m_inModel);
    ui->tvInItems->setColumnHidden(0, true);
    ui->tvInItems->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvInItems->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tvInItems->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    connect(m_inModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
        if (!item) return;
        if (item->column() != 5) return;

        bool ok = false;
        int v = item->text().toInt(&ok);
        if (!ok || v <= 0) {
            QSignalBlocker blocker(m_inModel);
            item->setText("1");
        }
        inRecalcTotal();
    });

    connect(ui->leInBarcode, &QLineEdit::returnPressed, this, &MainWindow::onInBarcodeEnter);
    connect(ui->btnInAdd,    &QPushButton::clicked,     this, &MainWindow::onInAdd);
    connect(ui->btnInCommit, &QPushButton::clicked,     this, &MainWindow::onInCommit);
    connect(ui->btnInClear,  &QPushButton::clicked,     this, &MainWindow::onInClear);

    ui->leInQty->setText("1");
    ui->leInBarcode->setFocus();
    ui->lblInTotal->setText("合计数量：0");

    // ===== 出库：公司下拉（包含匹配 + Backspace 兜底）=====
    // ===== 出库：公司下拉（输入任意子串：电子/概论/阿里 都能搜到；并且可正常删除）=====
    // ===== 出库：公司下拉（稳定版：包含匹配，不会强行回填导致删不掉）=====
    reloadCompanyCombo();

    ui->cbOutCompany->setEditable(true);
    ui->cbOutCompany->setInsertPolicy(QComboBox::NoInsert);


    // ✅ 关键：禁用 QComboBox 自带的“自动补全/内联补全”，否则会出现“输入后被自动填充、删不掉”的现象
    ui->cbOutCompany->setCompleter(nullptr);

    // 关键：禁用 QComboBox 自带的补全/回填（用 property，避免不同 Qt 版本 API 差异）
    ui->cbOutCompany->setCompleter(nullptr);
    ui->cbOutCompany->setProperty("autoCompletion", false);

    auto *leCompany = ui->cbOutCompany->lineEdit();
    leCompany->setClearButtonEnabled(true);
    leCompany->setPlaceholderText("输入公司关键字（如：电子）");

    // 让我们可以在 eventFilter 里兜底处理 Backspace/Delete（防止被 QComboBox/Completer 吃键）
    leCompany->installEventFilter(this);

    // 再保险：lineEdit 也清掉旧 completer
    leCompany->setCompleter(nullptr);

    leCompany->setCompleter(nullptr); // 再保险：清掉旧 completer

    // 1) 建议词模型：独立于 combobox model，避免 combo 自己“写回文本”
    if (!m_companySuggestModel) m_companySuggestModel = new QStringListModel(this);
    {
        QStringList names;
        names.reserve(ui->cbOutCompany->count());
        for (int i = 0; i < ui->cbOutCompany->count(); ++i) {
            names << ui->cbOutCompany->itemText(i);
        }
        m_companySuggestModel->setStringList(names);
    }

    // 2) 补全器：包含匹配
    if (m_companyCompleter) {
        // 如果你担心重复创建，也可以 deleteLater 再置空；这里简单处理为直接复用
        m_companyCompleter->setModel(m_companySuggestModel);
    } else {
        m_companyCompleter = new QCompleter(m_companySuggestModel, this);
    }
    m_companyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_companyCompleter->setFilterMode(Qt::MatchContains);              // ✅ 输入“电子”匹配任意位置
    m_companyCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_companyCompleter->setCompletionRole(Qt::DisplayRole);

    if (m_companyCompleter->popup()) {
        m_companyCompleter->popup()->setFocusPolicy(Qt::NoFocus);      // ✅ 不抢焦点，不影响退格/删除
    }

    leCompany->setCompleter(m_companyCompleter);

    // 输入时弹出候选（不改输入框内容）
    connect(leCompany, &QLineEdit::textEdited, this, [this](const QString& t){
        if (!m_companyCompleter) return;

        m_companyCompleter->setCompletionPrefix(t);
        if (t.isEmpty()) {
            if (m_companyCompleter->popup()) m_companyCompleter->popup()->hide();
            return;
        }

        m_companyCompleter->complete();

        // 没有候选就隐藏
        if (m_companyCompleter->completionModel()
            && m_companyCompleter->completionModel()->rowCount() == 0) {
            if (m_companyCompleter->popup()) m_companyCompleter->popup()->hide();
        }
    });

    // 只有用户“选中候选”时，才把 ComboBox 切到对应公司（此时才写回完整公司名）
    connect(m_companyCompleter, QOverload<const QString&>::of(&QCompleter::activated),
            this, [this](const QString& text){
                int idx = ui->cbOutCompany->findText(text, Qt::MatchFixedString);
                if (idx >= 0) ui->cbOutCompany->setCurrentIndex(idx);
            });

    ui->deOutDate->setDate(QDate::currentDate());
    ui->leOutQty->setText("1");
    // ✅ UI：数量输入别占太宽，给“公司”更多显示空间
    ui->leOutQty->setMaximumWidth(90);
    ui->cbOutCompany->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->lblOutTotal->setText("合计金额：0");
    ui->leOutBarcode->setFocus();

    // ===== 出库：明细表模型 =====
    m_outModel = new OutItemModel(this);
    m_outModel->setColumnCount(9);
    m_outModel->setHeaderData(0, Qt::Horizontal, "product_id");
    m_outModel->setHeaderData(1, Qt::Horizontal, "条码");
    m_outModel->setHeaderData(2, Qt::Horizontal, "名称");
    m_outModel->setHeaderData(3, Qt::Horizontal, "规格");
    m_outModel->setHeaderData(4, Qt::Horizontal, "单位");
    m_outModel->setHeaderData(5, Qt::Horizontal, "数量");
    m_outModel->setHeaderData(6, Qt::Horizontal, "单价");
    m_outModel->setHeaderData(7, Qt::Horizontal, "金额");
    m_outModel->setHeaderData(8, Qt::Horizontal, "库存");

    ui->tvOutItems->setModel(m_outModel);
    ui->tvOutItems->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvOutItems->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tvOutItems->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    ui->tvOutItems->setColumnHidden(0, true);

    // 表格滚轮缩放
    ui->tvProducts->viewport()->installEventFilter(this);
    ui->tvCompanies->viewport()->installEventFilter(this);
    ui->tvInItems->viewport()->installEventFilter(this);
    ui->tvOutItems->viewport()->installEventFilter(this);

    // 省略号字段悬停显示完整内容
    auto* tipDel = new ElideTooltipDelegate(this);
    ui->tvProducts->setItemDelegate(tipDel);
    ui->tvCompanies->setItemDelegate(tipDel);
    ui->tvInItems->setItemDelegate(tipDel);
    ui->tvOutItems->setItemDelegate(tipDel);

    // 扫码/按钮
    connect(ui->leOutBarcode, &QLineEdit::returnPressed, this, &MainWindow::onOutBarcodeEnter);
    connect(ui->btnOutAdd,    &QPushButton::clicked,     this, &MainWindow::onOutAdd);
    connect(ui->btnOutCommit, &QPushButton::clicked,     this, &MainWindow::onOutCommit);
    connect(ui->btnOutClear,  &QPushButton::clicked,     this, &MainWindow::onOutClear);

    // 数量修改后：校验 + 自动更新金额/合计
    connect(m_outModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
        if (!item) return;
        if (item->column() != 5) return;

        int row = item->row();
        bool ok = false;
        int qty = item->text().toInt(&ok);
        int stock = m_outModel->item(row, 8)->text().toInt();

        QSignalBlocker blocker(m_outModel);
        if (!ok || qty <= 0) qty = 1;
        if (qty > stock) qty = stock;
        item->setText(QString::number(qty));

        double unit = m_outModel->item(row, 6)->text().toDouble();
        double amount = unit * qty;
        m_outModel->setItem(row, 7, new QStandardItem(QString::number(amount, 'f', 2)));

        outRecalcTotal();
    });

    restoreTableStates();
}

MainWindow::~MainWindow()
{
    saveTableStates();
    delete ui;
}

QString MainWindow::escSql(const QString& s) {
    QString t = s.trimmed();
    return t.replace("'", "''");
}

void MainWindow::onPSearch() {
    const QString kw = escSql(ui->lePKeyword->text());
    if (kw.isEmpty()) {
        m_products->setFilter("");
    } else {
        m_products->setFilter(QString("name LIKE '%%1%' OR barcode LIKE '%%1%'").arg(kw));
    }
    m_products->select();
}

void MainWindow::onPReset() {
    ui->lePKeyword->clear();
    m_products->setFilter("");
    m_products->select();
}

void MainWindow::onPAdd() {
    QDialog dialog(this);
    dialog.setWindowTitle("添加商品");
    dialog.setModal(true);

    auto *formLayout = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(&dialog);
    auto *specEdit = new QLineEdit(&dialog);
    auto *unitCombo = new QComboBox(&dialog);
    unitCombo->addItems(unitOptions());
    auto *barcodeEdit = new QLineEdit(&dialog);
    auto *categoryEdit = new QLineEdit(&dialog);
    auto *priceSpin = new QDoubleSpinBox(&dialog);
    priceSpin->setMinimum(0);
    priceSpin->setDecimals(2);
    priceSpin->setMaximum(999999999);
    auto *stockSpin = new QSpinBox(&dialog);
    stockSpin->setMinimum(0);
    stockSpin->setMaximum(999999999);

    formLayout->addRow("名称*", nameEdit);
    formLayout->addRow("规格", specEdit);
    formLayout->addRow("单位", unitCombo);
    formLayout->addRow("条码*", barcodeEdit);
    formLayout->addRow("分类", categoryEdit);
    formLayout->addRow("单价*", priceSpin);
    formLayout->addRow("库存*", stockSpin);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                           Qt::Horizontal, &dialog);
    buttonBox->button(QDialogButtonBox::Ok)->setText("确定");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    formLayout->addRow(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString name = nameEdit->text().trimmed();
        const QString barcode = barcodeEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dialog, "校验失败", "名称不能为空。");
            nameEdit->setFocus();
            return;
        }
        if (barcode.isEmpty()) {
            QMessageBox::warning(&dialog, "校验失败", "条码不能为空。");
            barcodeEdit->setFocus();
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) return;

    const int row = m_products->rowCount();
    m_products->insertRow(row);
    auto setIfExists = [this, row](const QString& field, const QVariant& value) {
        const int col = m_products->fieldIndex(field);
        if (col >= 0) {
            m_products->setData(m_products->index(row, col), value);
        }
    };
    setIfExists("name", nameEdit->text().trimmed());
    setIfExists("spec", specEdit->text().trimmed());
    setIfExists("unit", unitCombo->currentText());
    setIfExists("barcode", barcodeEdit->text().trimmed());
    setIfExists("category", categoryEdit->text().trimmed());
    setIfExists("unit_price", priceSpin->value());
    setIfExists("stock_qty", stockSpin->value());
    ui->tvProducts->selectRow(row);
}

void MainWindow::onPDel() {
    auto sel = ui->tvProducts->selectionModel();
    if (!sel || !sel->hasSelection()) return;

    const int row = sel->currentIndex().row();
    const int productId = m_products->data(m_products->index(row, 0)).toInt();
    const QString name  = m_products->data(m_products->index(row, 1)).toString();

    if (productHasRefs(productId)) {
        QMessageBox::warning(this, "不能删除",
                             QString("商品【%1】已出现在历史单据（入库/出库明细）中，不能删除。\n"
                                     "请用右键 → 停用/启用。").arg(name));
        return;
    }

    if (QMessageBox::question(this, "确认删除", QString("确定删除商品【%1】吗？").arg(name))
        != QMessageBox::Yes) return;

    m_products->removeRow(row);
    if (!m_products->submitAll()) {
        QMessageBox::critical(this, "删除失败", m_products->lastError().text());
        m_products->revertAll();
        return;
    }
    m_products->select();
}


void MainWindow::onPSave() {
    const int colUnit = m_products->fieldIndex("unit");
    if (colUnit >= 0) {
        for (int row = 0; row < m_products->rowCount(); ++row) {
            const QString unit = m_products->data(m_products->index(row, colUnit)).toString().trimmed();
            if (unit.isEmpty()) {
                QMessageBox::warning(this, "保存失败", "单位不能为空，请选择单位。");
                ui->tvProducts->selectRow(row);
                ui->tvProducts->edit(m_products->index(row, colUnit));
                return;
            }
        }
    }

    if (!m_products->submitAll()) {
        const QString dbError = m_products->lastError().text();
        if (dbError.contains("products.barcode", Qt::CaseInsensitive)) {
            QMessageBox::critical(this, "保存失败", "条码输入冲突");
        } else {
            QMessageBox::critical(this, "保存失败", dbError);
        }
        m_products->revertAll();
        return;
    }
    m_products->select();
}

void MainWindow::onPRevert() {
    m_products->revertAll();
    m_products->select();
}

void MainWindow::onCSearch() {
    const QString kw = escSql(ui->leCKeyword->text());
    if (kw.isEmpty()) m_companies->setFilter("");
    else m_companies->setFilter(QString("name LIKE '%%1%'").arg(kw));
    m_companies->select();
}

void MainWindow::onCReset() {
    ui->leCKeyword->clear();
    m_companies->setFilter("");
    m_companies->select();
}

void MainWindow::onCAdd() {
    const int row = m_companies->rowCount();
    m_companies->insertRow(row);
    ui->tvCompanies->selectRow(row);
}

void MainWindow::onCDel() {
    auto sel = ui->tvCompanies->selectionModel();
    if (!sel || !sel->hasSelection()) return;

    const int row = sel->currentIndex().row();
    const int companyId = m_companies->data(m_companies->index(row, 0)).toInt();
    const QString companyName = m_companies->data(m_companies->index(row, 1)).toString();

    if (companyHasShipments(companyId)) {
        QMessageBox::warning(this, "不能删除",
                             QString("【%1】已有历史出库单，不能删除。\n请使用：右键 → 停用/启用。")
                                 .arg(companyName));
        return;
    }

    if (QMessageBox::question(this, "确认删除",
                              QString("确定删除公司【%1】吗？").arg(companyName))
        != QMessageBox::Yes) return;

    m_companies->removeRow(row);
    if (!m_companies->submitAll()) {
        QMessageBox::critical(this, "删除失败", m_companies->lastError().text());
        m_companies->revertAll();
        return;
    }

    m_companies->select();
    reloadCompanyCombo();
}

void MainWindow::onCSave() {
    if (!m_companies->submitAll()) {
        QMessageBox::critical(this, "保存失败", m_companies->lastError().text());
        m_companies->revertAll();
        return;
    }
    m_companies->select();
    reloadCompanyCombo();
}

void MainWindow::onCRevert() {
    m_companies->revertAll();
    m_companies->select();
}

void MainWindow::onInBarcodeEnter() {
    const QString code = ui->leInBarcode->text().trimmed();
    int qty = ui->leInQty->text().trimmed().toInt();
    if (qty <= 0) qty = 1;

    ui->leInBarcode->clear();
    ui->leInBarcode->setFocus();

    if (code.isEmpty()) return;
    inAddProductByBarcode(code, qty);
}

void MainWindow::onInAdd() {
    const QString code = ui->leInBarcode->text().trimmed();
    int qty = ui->leInQty->text().trimmed().toInt();
    if (qty <= 0) qty = 1;

    if (code.isEmpty()) return;

    ui->leInBarcode->clear();
    ui->leInBarcode->setFocus();

    inAddProductByBarcode(code, qty);
}

void MainWindow::inAddProductByBarcode(const QString& barcode, int qty) {
    // 查产品
    QSqlQuery q;
    q.prepare("SELECT id, name, spec, unit, stock_qty, is_active FROM products WHERE barcode = ?");
    q.addBindValue(barcode);

    if (!q.exec() || !q.next()) {
        QMessageBox::warning(this, "未找到商品",
                             "条码未匹配到商品：\n" + barcode + "\n\n请先在【产品库】里录入该条码商品。");
        return;
    }

    const int active = q.value(5).toInt();
    if (!active) {
        QMessageBox::warning(this, "商品已停用",
                             "该条码商品已停用/下架，禁止入库。\n如需继续使用，请先在【产品库】里启用它。");
        return;
    }

    const int productId = q.value(0).toInt();
    const QString name  = q.value(1).toString();
    const QString spec  = q.value(2).toString();
    const QString unit  = q.value(3).toString();
    const int stock     = q.value(4).toInt();

    // 如果同一个 product 已经在明细里，则数量累加
    for (int r = 0; r < m_inModel->rowCount(); ++r) {
        if (m_inModel->item(r, 0)->text().toInt() == productId) {
            int oldQty = m_inModel->item(r, 5)->text().toInt();
            m_inModel->setItem(r, 5, new QStandardItem(QString::number(oldQty + qty)));
            inRecalcTotal();
            return;
        }
    }

    // 否则新增一行
    QList<QStandardItem*> row;
    row << new QStandardItem(QString::number(productId));
    row << new QStandardItem(barcode);
    row << new QStandardItem(name);
    row << new QStandardItem(spec);
    row << new QStandardItem(unit);
    row << new QStandardItem(QString::number(qty));
    row << new QStandardItem(QString::number(stock));
    m_inModel->appendRow(row);

    inRecalcTotal();
}

void MainWindow::inRecalcTotal() {
    int total = 0;
    for (int r = 0; r < m_inModel->rowCount(); ++r) {
        total += m_inModel->item(r, 5)->text().toInt();
    }
    m_inTotalQty = total;
    ui->lblInTotal->setText(QString("合计数量：%1").arg(total));
}

void MainWindow::onInClear() {
    m_inModel->removeRows(0, m_inModel->rowCount());
    m_inTotalQty = 0;
    ui->lblInTotal->setText("合计数量：0");
    ui->leInBarcode->setFocus();
}

void MainWindow::onInCommit() {
    if (m_inModel->rowCount() == 0) {
        QMessageBox::information(this, "提示", "入库明细为空。");
        return;
    }

    auto db = QSqlDatabase::database();
    if (!db.transaction()) {
        QMessageBox::critical(this, "错误", "开启事务失败。");
        return;
    }

    auto rollbackFail = [&](const QString& msg){
        db.rollback();
        QMessageBox::critical(this, "入库失败", msg);
    };

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");

    // 1) receipts 插入主表
    QSqlQuery insR(db);
    insR.prepare("INSERT INTO receipts(recv_date, total_qty, note) VALUES(?, ?, '')");
    insR.addBindValue(today);
    insR.addBindValue(m_inTotalQty);
    if (!insR.exec()) return rollbackFail(insR.lastError().text());

    const int receiptId = insR.lastInsertId().toInt();

    // 2) 明细表 + 增库存
    QSqlQuery insItem(db);
    insItem.prepare("INSERT INTO receipt_items(receipt_id, product_id, qty) VALUES(?,?,?)");

    QSqlQuery upd(db);
    upd.prepare("UPDATE products SET stock_qty = stock_qty + ? WHERE id = ?");

    for (int r = 0; r < m_inModel->rowCount(); ++r) {
        const int productId = m_inModel->item(r, 0)->text().toInt();
        const int qty = m_inModel->item(r, 5)->text().toInt();

        insItem.addBindValue(receiptId);
        insItem.addBindValue(productId);
        insItem.addBindValue(qty);
        if (!insItem.exec()) return rollbackFail(insItem.lastError().text());
        insItem.finish();

        upd.addBindValue(qty);
        upd.addBindValue(productId);
        if (!upd.exec()) return rollbackFail(upd.lastError().text());
        upd.finish();
    }

    if (!db.commit()) {
        rollbackFail("提交事务失败");
        return;
    }

    // 刷新产品库表（让库存立即变化）
    m_products->select();

    QMessageBox::information(this, "成功", QString("入库完成 ✅\n入库单号：%1").arg(receiptId));
    onInClear();
}

void MainWindow::reloadCompanyCombo() {
    int prevId = ui->cbOutCompany->currentData().toInt();

    ui->cbOutCompany->blockSignals(true);
    ui->cbOutCompany->clear();

    QSqlQuery q("SELECT id, name FROM companies WHERE is_active = 1 ORDER BY name");
    while (q.next()) {
        ui->cbOutCompany->addItem(q.value(1).toString(), q.value(0).toInt());
    }


    // ✅ 同步更新自动补全的候选列表（否则新增公司后补全还是旧数据）
    if (m_companySuggestModel) {
        QStringList names;
        names.reserve(ui->cbOutCompany->count());
        for (int i = 0; i < ui->cbOutCompany->count(); ++i)
            names << ui->cbOutCompany->itemText(i);
        m_companySuggestModel->setStringList(names);
    }

    int idx = ui->cbOutCompany->findData(prevId);
    if (idx >= 0) ui->cbOutCompany->setCurrentIndex(idx);
    else ui->cbOutCompany->setCurrentIndex(-1);

    ui->cbOutCompany->blockSignals(false);

    // ✅ 如果你用的是 QStringListModel 那套补全（之前我们改过），这里也要刷新建议词
    if (m_companySuggestModel) {
        QStringList names;
        for (int i = 0; i < ui->cbOutCompany->count(); ++i) names << ui->cbOutCompany->itemText(i);
        m_companySuggestModel->setStringList(names);
    }
}

void MainWindow::onOutBarcodeEnter() {
    const QString code = ui->leOutBarcode->text().trimmed();
    int qty = ui->leOutQty->text().trimmed().toInt();
    if (qty <= 0) qty = 1;

    ui->leOutBarcode->clear();
    ui->leOutBarcode->setFocus();

    if (code.isEmpty()) return;
    outAddByBarcode(code, qty);
}

void MainWindow::onOutAdd() {
    const QString code = ui->leOutBarcode->text().trimmed();
    int qty = ui->leOutQty->text().trimmed().toInt();
    if (qty <= 0) qty = 1;

    if (code.isEmpty()) return;

    ui->leOutBarcode->clear();
    ui->leOutBarcode->setFocus();

    outAddByBarcode(code, qty);
}

void MainWindow::outAddByBarcode(const QString& barcode, int qty) {
    QSqlQuery q;
    q.prepare("SELECT id, name, spec, unit, unit_price, stock_qty, is_active FROM products WHERE barcode = ?");
    q.addBindValue(barcode);

    if (!q.exec() || !q.next()) {
        QMessageBox::warning(this, "未找到商品",
                             "条码未匹配到商品：\n" + barcode + "\n\n请先在【产品库】里录入该条码商品。");
        return;
    }

    const int active = q.value(6).toInt();
    if (!active) {
        QMessageBox::warning(this, "商品已停用",
                             "该条码商品已停用/下架，禁止出库。\n如需继续使用，请先在【产品库】里启用它。");
        return;
    }

    const int productId = q.value(0).toInt();
    const QString name  = q.value(1).toString();
    const QString spec  = q.value(2).toString();
    const QString unitName = q.value(3).toString();
    const double unit   = q.value(4).toDouble();
    const int stock     = q.value(5).toInt();

    if (stock <= 0) {
        QMessageBox::warning(this, "库存不足", "该商品库存为 0，无法出库。");
        return;
    }

    // 已存在则累加（并不超过库存）
    for (int r = 0; r < m_outModel->rowCount(); ++r) {
        if (m_outModel->item(r, 0)->text().toInt() == productId) {
            int oldQty = m_outModel->item(r, 5)->text().toInt();
            int newQty = oldQty + qty;
            if (newQty > stock) newQty = stock;

            QSignalBlocker blocker(m_outModel);
            m_outModel->setItem(r, 5, new QStandardItem(QString::number(newQty)));
            m_outModel->setItem(r, 7, new QStandardItem(QString::number(unit * newQty, 'f', 2)));
            outRecalcTotal();
            ui->tvOutItems->selectRow(r);
            ui->tvOutItems->edit(m_outModel->index(r, 5));
            return;
        }
    }

    // 新增一行
    QList<QStandardItem*> row;
    row << new QStandardItem(QString::number(productId));
    row << new QStandardItem(barcode);
    row << new QStandardItem(name);
    row << new QStandardItem(spec);
    row << new QStandardItem(unitName);

    int realQty = qty;
    if (realQty > stock) realQty = stock;
    row << new QStandardItem(QString::number(realQty));
    row << new QStandardItem(QString::number(unit, 'f', 2));
    row << new QStandardItem(QString::number(unit * realQty, 'f', 2));
    row << new QStandardItem(QString::number(stock));

    m_outModel->appendRow(row);
    outRecalcTotal();

    int r = m_outModel->rowCount() - 1;
    ui->tvOutItems->selectRow(r);
    ui->tvOutItems->edit(m_outModel->index(r, 5));
}

void MainWindow::outRecalcTotal() {
    double total = 0.0;
    for (int r = 0; r < m_outModel->rowCount(); ++r) {
        total += m_outModel->item(r, 7)->text().toDouble();
    }
    ui->lblOutTotal->setText(QString("合计金额：%1").arg(QString::number(total, 'f', 2)));
}

void MainWindow::onOutClear() {
    m_outModel->removeRows(0, m_outModel->rowCount());
    m_lastShipmentId = -1;
    ui->lblOutTotal->setText("合计金额：0");
    ui->leOutBarcode->setFocus();
}

void MainWindow::onOutCommit() {
    if (ui->cbOutCompany->currentIndex() < 0) {
        QMessageBox::warning(this, "提示", "请先选择公司。");
        return;
    }
    if (m_outModel->rowCount() == 0) {
        QMessageBox::information(this, "提示", "出库明细为空。");
        return;
    }

    int companyId = ui->cbOutCompany->currentData().toInt();
    QString date = ui->deOutDate->date().toString("yyyy-MM-dd");

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction()) {
        QMessageBox::critical(this, "错误", "开启事务失败。");
        return;
    }

    auto rollbackFail = [&](const QString& msg){
        db.rollback();
        QMessageBox::critical(this, "出库失败", msg);
    };

    // 1) 创建 shipments
    QSqlQuery insShip(db);
    insShip.prepare("INSERT INTO shipments(company_id, ship_date, total_amount, note) VALUES(?,?,0,'')");
    insShip.addBindValue(companyId);
    insShip.addBindValue(date);
    if (!insShip.exec()) return rollbackFail(insShip.lastError().text());

    int shipmentId = insShip.lastInsertId().toInt();

    // 2) 明细 + 扣库存
    QSqlQuery insItem(db);
    insItem.prepare("INSERT INTO shipment_items(shipment_id, product_id, qty, unit_price, amount) "
                    "VALUES(?,?,?,?,?)");

    QSqlQuery upd(db);
    upd.prepare("UPDATE products SET stock_qty = stock_qty - ? WHERE id = ?");

    double totalAmount = 0.0;

    for (int r = 0; r < m_outModel->rowCount(); ++r) {
        int productId = m_outModel->item(r, 0)->text().toInt();
        int qty = m_outModel->item(r, 5)->text().toInt();
        double unit = m_outModel->item(r, 6)->text().toDouble();
        double amount = unit * qty;

        // 再查一次库存（保险）
        QSqlQuery chk(db);
        chk.prepare("SELECT stock_qty FROM products WHERE id = ?");
        chk.addBindValue(productId);
        if (!chk.exec() || !chk.next()) return rollbackFail("查询库存失败");
        int stockNow = chk.value(0).toInt();
        if (stockNow < qty) return rollbackFail(QString("库存不足：product_id=%1，库存=%2，需求=%3")
                                    .arg(productId).arg(stockNow).arg(qty));

        insItem.addBindValue(shipmentId);
        insItem.addBindValue(productId);
        insItem.addBindValue(qty);
        insItem.addBindValue(unit);
        insItem.addBindValue(amount);
        if (!insItem.exec()) return rollbackFail(insItem.lastError().text());
        insItem.finish();

        upd.addBindValue(qty);
        upd.addBindValue(productId);
        if (!upd.exec()) return rollbackFail(upd.lastError().text());
        upd.finish();

        totalAmount += amount;
    }

    // 3) 更新总价
    QSqlQuery updTotal(db);
    updTotal.prepare("UPDATE shipments SET total_amount = ? WHERE id = ?");
    updTotal.addBindValue(totalAmount);
    updTotal.addBindValue(shipmentId);
    if (!updTotal.exec()) return rollbackFail(updTotal.lastError().text());

    if (!db.commit()) {
        rollbackFail("提交事务失败");
        return;
    }

    m_lastShipmentId = shipmentId;

    // ===== 自动导出CSV（不弹对话框，固定目录）=====
    {
        QString company = safeFileName(ui->cbOutCompany->currentText());
        QString date = ui->deOutDate->date().toString("yyyyMMdd");

        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + "/StockOutExports";
        QDir().mkpath(dir);

        QString path = QString("%1/出货单_%2_%3_%4.csv")
                           .arg(dir, company, date)
                           .arg(shipmentId);

        QString err;
        if (!exportShipmentCsv(shipmentId, path, &err)) {
            QMessageBox::warning(this, "出库成功但导出失败",
                                 "出库已完成 ✅\n但CSV导出失败：\n" + err + "\n\n你可以稍后点【导出CSV】重试。");
        } else {
            QMessageBox::information(this, "出库完成 ✅",
                                     QString("出库单号：%1\n已自动导出：\n%2")
                                         .arg(shipmentId).arg(path));
        }
    }

    // 刷新产品库库存显示
    m_products->select();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle("出库完成 ✅");
    box.setText(QString("出库单号：%1\n合计：%2\n\n要不要现在打印？")
                    .arg(shipmentId)
                    .arg(ui->lblOutTotal->text().replace("合计金额：", "").trimmed()));

    auto btnPrint = box.addButton("打印", QMessageBox::ActionRole);
    box.addButton("确定", QMessageBox::AcceptRole);

    box.exec();

    if (box.clickedButton() == btnPrint) {
        QString perr;
        if (!printShipment(shipmentId, &perr)) {
            QMessageBox::warning(this, "打印失败", perr.isEmpty() ? "用户取消或打印机不可用" : perr);
        }
    }

}

QString MainWindow::csvEscape(const QString& s) {
    QString t = s;
    t.replace("\"", "\"\"");
    return "\"" + t + "\"";
}

void MainWindow::onOutExport() {
    if (m_lastShipmentId <= 0) {
        QMessageBox::information(this, "提示", "请先完成一次出库，再导出。");
        return;
    }

    QString company = ui->cbOutCompany->currentText();
    QString date = ui->deOutDate->date().toString("yyyyMMdd");
    QString defaultName = QString("出货单_%1_%2_%3.csv").arg(company, date).arg(m_lastShipmentId);

    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(this, "保存出货单", dir + "/" + defaultName, "CSV (*.csv)");
    if (path.isEmpty()) return;

    QString err;
    if (!exportShipmentCsv(m_lastShipmentId, path, &err)) {
        QMessageBox::critical(this, "导出失败", err);
        return;
    }
    QMessageBox::information(this, "导出成功", "已导出到：\n" + path);
}

bool MainWindow::exportShipmentCsv(int shipmentId, const QString& path, QString* err) {
    QSqlQuery q;
    q.prepare(R"(
        SELECT c.name, s.ship_date, s.total_amount
        FROM shipments s
        JOIN companies c ON c.id = s.company_id
        WHERE s.id = ?
    )");
    q.addBindValue(shipmentId);
    if (!q.exec() || !q.next()) {
        if (err) *err = "查询出库单头信息失败";
        return false;
    }
    QString company = q.value(0).toString();
    QString shipDate = q.value(1).toString();
    double total = q.value(2).toDouble();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = "无法写入文件";
        return false;
    }

    // Excel 友好 BOM
    file.write("\xEF\xBB\xBF", 3);

    QTextStream ts(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    ts.setEncoding(QStringConverter::Utf8);
#endif

    ts << "公司," << csvEscape(company) << "\n";
    ts << "日期," << csvEscape(shipDate) << "\n";
    ts << "单号," << shipmentId << "\n\n";

    ts << "名称,规格,单位,条码,数量,单价,金额\n";

    QSqlQuery qi;
    qi.prepare(R"(
        SELECT p.name, p.spec, p.unit, p.barcode, i.qty, i.unit_price, i.amount
        FROM shipment_items i
        JOIN products p ON p.id = i.product_id
        WHERE i.shipment_id = ?
        ORDER BY p.name
    )");
    qi.addBindValue(shipmentId);

    if (!qi.exec()) {
        if (err) *err = "查询出库明细失败";
        return false;
    }

    while (qi.next()) {
        ts << csvEscape(qi.value(0).toString()) << ","
           << csvEscape(qi.value(1).toString()) << ","
           << csvEscape(qi.value(2).toString()) << ","
           << csvEscape(qi.value(3).toString()) << ","
           << qi.value(4).toInt() << ","
           << QString::number(qi.value(5).toDouble(), 'f', 2) << ","
           << QString::number(qi.value(6).toDouble(), 'f', 2) << "\n";
    }

    ts << "\n合计,,,,,," << QString::number(total, 'f', 2) << "\n";
    return true;
}

QString MainWindow::safeFileName(QString s) {
    s = s.trimmed();
    // Windows 不允许的文件名字符：\ / : * ? " < > |
    const QString bad = "\\/:*?\"<>|";
    for (const QChar& ch : bad) s.replace(ch, '_');
    s.replace('\n', '_');
    s.replace('\r', '_');
    return s;
}

QString MainWindow::buildShipmentHtml(int shipmentId, QString* err) {
    QSqlQuery q;
    q.prepare(R"(
        SELECT c.name, s.ship_date, s.total_amount
        FROM shipments s
        JOIN companies c ON c.id = s.company_id
        WHERE s.id = ?
    )");
    q.addBindValue(shipmentId);
    if (!q.exec() || !q.next()) {
        if (err) *err = "查询出库单头信息失败";
        return {};
    }

    const QString company  = q.value(0).toString();
    const QString shipDate = q.value(1).toString();
    const double totalAmt  = q.value(2).toDouble();
    const QString supplier = ui->leOutSupplier ? ui->leOutSupplier->text() : QString();
    const QString deliverer = ui->leOutDeliverer ? ui->leOutDeliverer->text() : QString();

    auto esc = [](const QString& s){ return s.toHtmlEscaped(); };

    QString html;
    html += "<html><head><meta charset='utf-8'></head><body>";
    html += "<div style='margin-bottom:8px;'>";
    html += "<table style='width:100%;border-collapse:collapse;'>";
    html += "<tr>";
    html += "<div style='width:100%;text-align:center;margin-top:10px;"
            "font-size:120px;font-weight:700;'>";
    html += "健力源" + esc(company) + "计划表</td>";
    html += "</tr>";
    html += "</table>";
    html += "<div style='width:100%;text-align:center;margin-top:6px;'>";
    html += "供货商：" + esc(supplier)
            + "&nbsp;&nbsp;&nbsp;送货人：" + esc(deliverer)
            + "&nbsp;&nbsp;&nbsp;日期：" + esc(shipDate);
    html += "</div>";
    html += "</div>";

    html += "<table border='1' cellspacing='0' cellpadding='6' width='100%' style='border-collapse:collapse;text-align:center;'>";
    html += "<tr>"
            "<th>序号</th><th>品名</th><th>规格</th><th>单位</th>"
            "<th>申请数量</th><th>验收数量</th><th>单价</th><th>合计金额</th><th>备注</th>"
            "</tr>";

    QSqlQuery qi;
    qi.prepare(R"(
        SELECT p.name, p.spec, p.unit, p.barcode, i.qty, i.unit_price, i.amount
        FROM shipment_items i
        JOIN products p ON p.id = i.product_id
        WHERE i.shipment_id = ?
        ORDER BY p.name
    )");
    qi.addBindValue(shipmentId);
    if (!qi.exec()) {
        if (err) *err = "查询出库明细失败";
        return {};
    }

    int rowNo = 1;
    while (qi.next()) {
        const QString name   = qi.value(0).toString();
        const QString spec   = qi.value(1).toString();
        const QString unitName = qi.value(2).toString();
        const int qty        = qi.value(4).toInt();
        const double unit    = qi.value(5).toDouble();
        const double amt     = qi.value(6).toDouble();

        html += "<tr>";
        html += "<td>" + QString::number(rowNo) + "</td>";
        html += "<td>" + esc(name) + "</td>";
        html += "<td>" + esc(spec) + "</td>";
        html += "<td>" + esc(unitName) + "</td>";
        html += "<td></td>";
        html += "<td>" + QString::number(qty) + "</td>";
        html += "<td>" + QString::number(unit, 'f', 2) + "</td>";
        html += "<td>" + QString::number(amt, 'f', 2) + "</td>";
        html += "<td></td>";
        html += "</tr>";
        ++rowNo;
    }

    html += QString("<tr><td colspan='7'><b>合计</b></td>"
                    "<td><b>%1</b></td><td></td></tr>")
                .arg(QString::number(totalAmt, 'f', 2));

    // ... 上面是合计行的代码 ...
    html += "</table>";

    // --- 签字栏开始 ---
    // 1. 外层容器：宽度设为 90% 或 100% 都可以，margin-top 稍微加大一点(30px)拉开和表格的距离
    html += "<div style='width:98%; margin:30px auto 0 auto;'>";

    // 2. 签字栏表格：宽度铺满，去掉边框
    html += "  <table width='100%' style='border:none; border-collapse:collapse;'>";
    html += "    <tr>";

    // 【左侧】申请人：align='left' 强制靠左
    // 这里的 padding-left:10px 是为了不让文字紧贴纸张边缘，留一点点呼吸感，比 120px 安全得多
    html += "      <td width='33%' align='left' style='border:none; padding-left:10px;'>申请人：__________</td>";

    // 【中间】验收人：align='center' 强制居中
    html += "      <td width='34%' align='center' style='border:none;'>验收人：__________</td>";

    // 【右侧】审核人：align='right' 强制靠右
    // 同样加一点 padding-right 防止紧贴右边缘
    html += "      <td width='33%' align='right' style='border:none; padding-right:10px;'>审核人：__________</td>";

    html += "    </tr>";
    html += "  </table>";
    html += "</div>";
    // --- 签字栏结束 ---

    html += "</body></html>";





    return html;
}

bool MainWindow::printShipment(int shipmentId, QString* err) {
    QString html = buildShipmentHtml(shipmentId, err);
    if (html.isEmpty()) return false;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QString("Shipment_%1").arg(shipmentId));

    QTextDocument doc;
    doc.setHtml(html);

    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle("打印预览");
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter* p) {
        doc.print(p);
    });

    if (preview.exec() != QDialog::Accepted) return false;
    return true;
}

void MainWindow::applyTableFont(bool save) {
    m_tableFontPt = std::clamp(m_tableFontPt, 8, 24);

    auto applyOne = [&](QTableView* tv) {
        if (!tv) return;

        QFont f = tv->font();
        f.setPointSize(m_tableFontPt);
        tv->setFont(f);

        // 表头也一起变（更协调）
        if (tv->horizontalHeader()) tv->horizontalHeader()->setFont(f);
        if (tv->verticalHeader()) {
            tv->verticalHeader()->setFont(f);
            // 行高跟着字号走（别挤成一条线）
            tv->verticalHeader()->setDefaultSectionSize(std::max(22, m_tableFontPt * 2 + 8));
        }

        // 这俩可能在数据很多时略慢；你表很大就先注释掉
        tv->resizeRowsToContents();
        // tv->resizeColumnsToContents();
    };

    applyOne(ui->tvProducts);
    applyOne(ui->tvCompanies);
    applyOne(ui->tvInItems);
    applyOne(ui->tvOutItems);

    if (save) {
        QSettings st("YourCompany", "StockOutApp");
        st.setValue("ui/tableFontPt", m_tableFontPt);
    }
}

void MainWindow::tableZoomIn() {
    ++m_tableFontPt;
    applyTableFont(true);
}

void MainWindow::tableZoomOut() {
    --m_tableFontPt;
    applyTableFont(true);
}

void MainWindow::tableZoomReset() {
    m_tableFontPt = 10;  // 你也可以改成 QApplication::font().pointSize()
    applyTableFont(true);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // ===== 出库公司输入框：兜底处理 Backspace / Delete，避免被 QComboBox/Completer “抢写回”导致删不掉 =====
    if (ui && ui->cbOutCompany && obj == ui->cbOutCompany->lineEdit()
        && event->type() == QEvent::KeyPress) {

        auto* ke = static_cast<QKeyEvent*>(event);
        auto* le = ui->cbOutCompany->lineEdit();
        if (!le) return QMainWindow::eventFilter(obj, event);

        if (ke->key() == Qt::Key_Backspace) {
            le->backspace();
            // 同步刷新补全（不抢焦点）
            if (m_companyCompleter) {
                const QString t = le->text();
                m_companyCompleter->setCompletionPrefix(t);
                if (t.isEmpty()) {
                    if (m_companyCompleter->popup()) m_companyCompleter->popup()->hide();
                } else {
                    m_companyCompleter->complete();
                }
            }
            return true;
        }
        if (ke->key() == Qt::Key_Delete) {
            le->del();
            if (m_companyCompleter) {
                const QString t = le->text();
                m_companyCompleter->setCompletionPrefix(t);
                if (t.isEmpty()) {
                    if (m_companyCompleter->popup()) m_companyCompleter->popup()->hide();
                } else {
                    m_companyCompleter->complete();
                }
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            if (m_companyCompleter && m_companyCompleter->popup())
                m_companyCompleter->popup()->hide();
            return false;
        }
    }

    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);

        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            auto* vpP = ui->tvProducts ? ui->tvProducts->viewport() : nullptr;
            auto* vpC = ui->tvCompanies ? ui->tvCompanies->viewport() : nullptr;
            auto* vpI = ui->tvInItems ? ui->tvInItems->viewport() : nullptr;
            auto* vpO = ui->tvOutItems ? ui->tvOutItems->viewport() : nullptr;

            if (obj == vpP || obj == vpC || obj == vpI || obj == vpO) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                const int delta = we->angleDelta().y();
#else
                const int delta = we->delta();
#endif
                if (delta > 0) ++m_tableFontPt;
                else if (delta < 0) --m_tableFontPt;

                applyTableFont(true);
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(obj, event);
}


#include <QSettings>
#include <QHeaderView>

void MainWindow::saveTableStates() {
    QSettings st("YourCompany", "StockOutApp");

    auto saveOne = [&](QTableView* tv, const QString& key){
        if (!tv || !tv->horizontalHeader()) return;
        st.setValue(key, tv->horizontalHeader()->saveState());
    };

    saveOne(ui->tvProducts,  "ui/tvProducts/header");
    saveOne(ui->tvCompanies, "ui/tvCompanies/header");
    saveOne(ui->tvInItems,   "ui/tvInItems/header");
    saveOne(ui->tvOutItems,  "ui/tvOutItems/header");
}

void MainWindow::restoreTableStates() {
    QSettings st("YourCompany", "StockOutApp");

    auto restoreOne = [&](QTableView* tv, const QString& key){
        if (!tv || !tv->horizontalHeader()) return;
        const QByteArray ba = st.value(key).toByteArray();
        if (!ba.isEmpty()) tv->horizontalHeader()->restoreState(ba);
    };

    restoreOne(ui->tvProducts,  "ui/tvProducts/header");
    restoreOne(ui->tvCompanies, "ui/tvCompanies/header");
    restoreOne(ui->tvInItems,   "ui/tvInItems/header");
    restoreOne(ui->tvOutItems,  "ui/tvOutItems/header");
}

static bool hasColumn(const QString& table, const QString& col)
{
    QSqlQuery q;
    q.exec(QString("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString() == col) return true; // 1列是字段名
    }
    return false;
}

void MainWindow::ensureCompaniesSchema()
{
    // 给 companies 增加 is_active（1启用/0停用）
    if (!hasColumn("companies", "is_active")) {
        QSqlQuery q;
        if (!q.exec("ALTER TABLE companies ADD COLUMN is_active INTEGER NOT NULL DEFAULT 1")) {
            QMessageBox::warning(this, "数据库升级失败", q.lastError().text());
        }
    }
}

void MainWindow::ensureProductsSchema()
{
    if (!hasColumn("products", "is_active")) {
        QSqlQuery q;
        if (!q.exec("ALTER TABLE products ADD COLUMN is_active INTEGER NOT NULL DEFAULT 1")) {
            QMessageBox::warning(this, "数据库升级失败", q.lastError().text());
        }
    }

    if (!hasColumn("products", "unit")) {
        QSqlQuery q;
        if (!q.exec("ALTER TABLE products ADD COLUMN unit TEXT NOT NULL DEFAULT '瓶'")) {
            QMessageBox::warning(this, "数据库升级失败", q.lastError().text());
        }
    }

    if (hasColumn("products", "unit")) {
        QSqlQuery q;
        if (!q.exec("UPDATE products SET unit = '瓶' WHERE unit IS NULL OR unit = ''")) {
            QMessageBox::warning(this, "数据库升级失败", q.lastError().text());
        }
    }
}

bool MainWindow::productHasRefs(int productId) const
{
    auto hasRef = [&](const QString& table) -> bool {
        QSqlQuery q;
        q.prepare(QString("SELECT COUNT(*) FROM %1 WHERE product_id = ?").arg(table));
        q.addBindValue(productId);
        if (!q.exec() || !q.next()) return false;
        return q.value(0).toInt() > 0;
    };
    return hasRef("receipt_items") || hasRef("shipment_items");
}


bool MainWindow::companyHasShipments(int companyId) const
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM shipments WHERE company_id = ?");
    q.addBindValue(companyId);
    if (!q.exec() || !q.next()) return false;
    return q.value(0).toInt() > 0;
}

void MainWindow::onCompaniesContextMenu(const QPoint& pos)
{
    QModelIndex idx = ui->tvCompanies->indexAt(pos);
    if (!idx.isValid()) return;

    QMenu menu(this);
    menu.addAction("停用/启用", this, &MainWindow::onCToggleActive);
    menu.exec(ui->tvCompanies->viewport()->mapToGlobal(pos));
}

void MainWindow::onCToggleActive()
{
    auto sel = ui->tvCompanies->selectionModel();
    if (!sel || !sel->hasSelection()) return;

    int row = sel->currentIndex().row();
    int companyId = m_companies->data(m_companies->index(row, 0)).toInt();
    QString name  = m_companies->data(m_companies->index(row, 1)).toString();

    int colActive = m_companies->fieldIndex("is_active");
    if (colActive < 0) colActive = 2; // 保险

    int active = m_companies->data(m_companies->index(row, colActive)).toInt();
    int newVal = active ? 0 : 1;

    // 避免和 OnManualSubmit 未保存编辑打架：先回滚未保存改动
    m_companies->revertAll();

    QSqlQuery q;
    q.prepare("UPDATE companies SET is_active = ? WHERE id = ?");
    q.addBindValue(newVal);
    q.addBindValue(companyId);
    if (!q.exec()) {
        QMessageBox::critical(this, "操作失败", q.lastError().text());
        return;
    }

    m_companies->select();
    reloadCompanyCombo();

    QMessageBox::information(this, "完成",
                             QString("已%1：%2").arg(newVal ? "启用" : "停用").arg(name));
}

void MainWindow::onProductsContextMenu(const QPoint& pos)
{
    QModelIndex idx = ui->tvProducts->indexAt(pos);
    if (!idx.isValid()) return;

    QMenu menu(this);
    menu.addAction("停用/启用", this, &MainWindow::onPToggleActive);
    menu.exec(ui->tvProducts->viewport()->mapToGlobal(pos));
}

void MainWindow::onPToggleActive()
{
    auto sel = ui->tvProducts->selectionModel();
    if (!sel || !sel->hasSelection()) return;

    int row = sel->currentIndex().row();
    int productId = m_products->data(m_products->index(row, 0)).toInt();
    QString name  = m_products->data(m_products->index(row, 1)).toString();

    int colActive = m_products->fieldIndex("is_active");
    if (colActive < 0) {
        QMessageBox::warning(this, "提示", "products 表没有 is_active 字段。");
        return;
    }

    int active = m_products->data(m_products->index(row, colActive)).toInt();
    int newVal = active ? 0 : 1;

    // OnManualSubmit 下，先回滚未保存编辑，避免冲突
    m_products->revertAll();

    QSqlQuery q;
    q.prepare("UPDATE products SET is_active = ? WHERE id = ?");
    q.addBindValue(newVal);
    q.addBindValue(productId);
    if (!q.exec()) {
        QMessageBox::critical(this, "操作失败", q.lastError().text());
        return;
    }

    m_products->select();
    QMessageBox::information(this, "完成", QString("已%1：%2").arg(newVal ? "启用" : "停用").arg(name));
}
