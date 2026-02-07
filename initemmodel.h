#ifndef INITEMMODEL_H
#define INITEMMODEL_H
#pragma once
#include <QStandardItemModel>

class InItemModel : public QStandardItemModel {
    Q_OBJECT
public:
    using QStandardItemModel::QStandardItemModel;

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        auto f = QStandardItemModel::flags(index);
        if (!index.isValid()) return f;

        // 只允许编辑第5列：数量
        if (index.column() == 5) return f | Qt::ItemIsEditable;

        // 其他列禁止编辑
        return f & ~Qt::ItemIsEditable;
    }
};

#endif // INITEMMODEL_H
