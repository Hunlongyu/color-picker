#pragma once
#pragma execution_character_set("utf-8")

#include <QtWidgets/QMainWindow>
#include "ui_color.h"
#include "libs/framelesswindow.h"
#include <QFile>
#include <QDebug>
#include <QListWidgetItem>
#include <QColor>
#include <QIcon>
#include <QSize>
#include <QSystemTrayIcon>
#include <QMenu>

class color : public CFramelessWindow
{
    Q_OBJECT

public:
    color(QWidget *parent = nullptr);
    ~color();
    QString currentType = "HEX";
    QString selectType = "HEX";
    QString colorValue = "";
    QStringList lists = {};
    QSystemTrayIcon systemTrayIcon;
    QMenu menu;
    
private slots:
    void on_btnMenu_click();
    void on_btnPick_click();
    void on_btnCopy_click();
    void on_btnMini_click();
    void on_colorItem_click(QListWidgetItem* item);
    void on_changeColorType(const QString&);

private:
    Ui::colorClass ui;

    void initUI();
    void initConnect();
    void initTray();
};
