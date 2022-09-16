#include "color.h"

color::color(QWidget *parent)
    : CFramelessWindow(parent)
{
    ui.setupUi(this);
	initUI();
	initConnect();
	initTray();
}

color::~color()
{}

void color::initUI()
{
    setTitleBar(ui.header);

	QFile file(":/assets/qss/color.css");
	file.open(QFile::ReadOnly);
	if (file.isOpen()) {
		QString styleSheet = this->styleSheet();
		styleSheet = QLatin1String(file.readAll());
		this->setStyleSheet(styleSheet);
	}
	file.close();

	ui.lists->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	ui.lists->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void color::initConnect()
{
	connect(ui.btnMenu, &QPushButton::clicked, this, &color::on_btnMenu_click);
	connect(ui.btnMini, &QPushButton::clicked, this, &color::on_btnMini_click);
	connect(ui.btnGet, &QPushButton::clicked, this, &color::on_btnPick_click);
	connect(ui.btnCopy, &QPushButton::clicked, this, &color::on_btnCopy_click);
	connect(ui.cbType, SIGNAL(currentTextChanged(const QString &)), this, SLOT(on_changeColorType(const QString &)));
	connect(ui.lists, &QListWidget::itemClicked, this, &color::on_colorItem_click);
}

void color::initTray()
{
	systemTrayIcon.setIcon(QIcon(":/assets/img/icon.png"));
	systemTrayIcon.setToolTip("Color Picker 取色器");
	QAction* showAction = new QAction("显示", &menu);
	QAction* exitAction = new QAction("退出", &menu);
	menu.addAction(showAction);
	menu.addAction(exitAction);
	connect(showAction, &QAction::triggered, [this]() {
		this->show();
	});
	connect(exitAction, &QAction::triggered, [this]() {
		this->close();
	});
	connect(&systemTrayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
		switch (reason) {
		case QSystemTrayIcon::DoubleClick:
			this->show();
			break;
		}
	});
	systemTrayIcon.setContextMenu(&menu);
	systemTrayIcon.show();
}

void color::on_btnMenu_click()
{
	qDebug() << "on_btnMenu_click";
}

void color::on_btnMini_click()
{
	this->hide();
}

void color::on_btnPick_click()
{
	qDebug() << "on_btnPick_click";
	//lists.append("#123456");
	QListWidgetItem* w = new QListWidgetItem();
	w->setBackgroundColor("#123456");
	w->setSizeHint(QSize(30, 30));
	ui.lists->addItem(w);
	/*for (int i = 0; i < lists.size(); i++) {
	}*/
}

void color::on_btnCopy_click()
{
	qDebug() << "on_btnCopy_click";
}

void color::on_colorItem_click(QListWidgetItem* item)
{
	qDebug() << "on_colorItem_click" << item->text();
}

void color::on_changeColorType(const QString &text)
{
	qDebug() << "changeColorType: " << text;
}
