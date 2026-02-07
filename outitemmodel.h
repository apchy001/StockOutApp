#ifndef OUTITEMMODEL_H
#define OUTITEMMODEL_H
#pragma once
#include <QStandardItemModel>

class OutItemModel : public QStandardItemModel {
    Q_OBJECT
public:
    using QStandardItemModel::QStandardItemModel;

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        auto f = QStandardItemModel::flags(index);
        if (!index.isValid()) return f;

        // 只允许编辑第5列：数量
        if (index.column() == 5) return f | Qt::ItemIsEditable;
    }
};

#endif // OUTITEMMODEL_H
