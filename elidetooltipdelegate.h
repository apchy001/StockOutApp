#ifndef ELIDETOOLTIPDELEGATE_H
#define ELIDETOOLTIPDELEGATE_H
#pragma once
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QHelpEvent>
#include <QAbstractItemView>

class ElideTooltipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) override
    {
        if (!event || !view) return false;

        if (event->type() == QEvent::ToolTip) {
            const QString text = index.data(Qt::DisplayRole).toString();
            if (!text.isEmpty()) {
                QToolTip::showText(event->globalPos(), text, view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
};

#endif // ELIDETOOLTIPDELEGATE_H
