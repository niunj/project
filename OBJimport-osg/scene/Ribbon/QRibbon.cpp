


#ifdef _WIN32
    // Windows 平台
#include "QRibbon.h"
#include "ui_qribbon.h"
#include "QtGui\qevent.h"

#include <QPropertyAnimation>
#include <QActionGroup>
#include <QDebug>
#include "QtCore\QTimer"
#include "QtWidgets\QMainWindow"
#include "QtCore\qcoreevent.h"
#elif defined(__linux__)
    // Linux 平台
#include "QRibbon.h"
#include "ui_qribbon.h"
#include <QtGui/qevent.h>
#include <QPropertyAnimation>
#include <QActionGroup>
#include <QDebug>
#include <QtCore/QTimer>
#include <QtWidgets/QMainWindow>
#include <QtCore/qcoreevent.h>
#endif

// 定义不同状态下的高度常量
const auto MINIMUM_HEIGHT = 62;         // 仅标题栏高度
const auto FULL_HEIGHT = 146;           // 完整高度（标题栏+菜单栏+内容）


struct QRibbonPrivate
{
    QRibbonPrivate(QRibbon *ribbon)
    {
        _timer = new QTimer();
        QObject::connect(_timer, &QTimer::timeout, ribbon, &QRibbon::onLostFocus);
        _timer->setInterval(100);
    }

    ~QRibbonPrivate()
    {
        delete _timer;
    }

    bool        _pressed = false;
    QPoint      _mouseStartPosition;
    QPoint      _origin;
    QRect _originGeometry;

    QMainWindow *_mainWindow = nullptr;

    QTimer *_timer;

    QPropertyAnimation opacityAnimation;
    QPropertyAnimation animationHideBar;
};

QRibbon::QRibbon()
    : QMenuBar()
    , _(new QRibbonPrivate(this))
{
    ui = new Ui::QRibbon();
    ui->setupUi(this);

    //qss 样式
     // 添加QSS样式设置
    QString customStyle = R"(
        QTreeWidget {
            color: black; /* QTreeWidget文字设为黑色 */
        }
        QRadioButton {
            border: transparent; /* 边框透明 */
            padding: 2px;
        }
        QRadioButton::indicator { /* 控制圆圈（指示器）样式 */
            width: 13px; /* 圆圈宽度 */
            height: 13px; /* 圆圈高度 */
            border: 1px solid black; /* 圆圈边框为黑色 */
            border-radius: 6px; /* 圆形（半径为宽度的一半） */
            background-color: white; /* 圆圈内部背景为白色 */
        }
        QRadioButton::indicator:checked { /* 选中状态 */
            background-color: black; /* 选中时内部填充黑色 */
        }
        QCheckBox {
            border: 1px solid black; /* QCheckBox边框设为黑色 */
            padding: 2px;
        }
        QTableWidget {
            color: black; /* QTableWidget内容文字设为黑色 */
        }
        QWidget#MaterialWidget,#ProjectWidget,#HeatWidget {
            background-color: white; /* QWidget背景设为白色 */
        }
        QTableView::indicator {
            border: 1px solid black; /* 边框黑色 */
            width: 13px;
            height: 13px;
            background-color: white; /* 未选中时背景白色 */
        }
        QTableView::indicator:checked {
            background-color: black; /* 选中时背景保持白色 */
            color: black; /* 对勾颜色设为黑色 */
        }
    )";
    // 合并现有样式表，避免覆盖
    qApp->setStyleSheet(qApp->styleSheet() + customStyle);


    connect(ui->tabWidgetMenuBar, &QTabWidget::tabBarClicked, this, &QRibbon::clickTab);
    connect(ui->tabWidgetMenuBar, &QTabWidget::currentChanged, this, &QRibbon::onTabChanged);

    connect(ui->pushButtonFullScreen, &QPushButton::clicked, this, &QRibbon::toggleMaximized);
    connect(ui->pushButtonMinimizeTab, &QPushButton::clicked, this, &QRibbon::hideTab);
    connect(ui->pushButtonMaxTab, &QPushButton::clicked, this, &QRibbon::expandTab);

    connect(&(_->animationHideBar), &QPropertyAnimation::finished, this, &QRibbon::onHideTabFinished);

    _styleMenu = new QMenu(this);
    _styleMenu->setStyleSheet("QMenu::item{ color:black }");
    auto styleActionGroup = new QActionGroup(_styleMenu);
    styleActionGroup->addAction(_styleMenu->addAction(tr("蓝色"), [&]()
    {
        setColor("rgb(43, 87, 154)");
    }));
    styleActionGroup->addAction(_styleMenu->addAction(tr("绿色"), [&]()
    {
        setColor("rgb(33,115,70)");
    }));
    styleActionGroup->addAction(_styleMenu->addAction(tr("红色"), [&]()
    {
        setColor("rgb(183, 71, 42)");
    }));
    for (auto a : styleActionGroup->actions())
    {
        a->setCheckable(true);
    }
    ui->pushButtonStyle->setMenu(_styleMenu);

    m_bExpandStaus = true;

    hideMenuBar(true);
}

QRibbon::~QRibbon()
{
    delete _;
    delete ui;
}

void QRibbon::initialize(QMainWindow *window)
{
    _->_mainWindow = window;

    if (!_->opacityAnimation.targetObject())
    {
        _->opacityAnimation.setTargetObject(_->_mainWindow);
        _->opacityAnimation.setPropertyName("windowOpacity");
        _->opacityAnimation.setStartValue(1.0);
        _->opacityAnimation.setEndValue(1.0);
    }

    QMenuBar *menuBar = _->_mainWindow->menuBar();

    if (!menuBar)
    {
        return;
    }

    connect(ui->pushButtonMinimum, &QPushButton::clicked, _->_mainWindow, &QWidget::showMinimized);
    connect(ui->pushButtonClose, &QPushButton::clicked, _->_mainWindow, &QWidget::close);

    ui->tabWidgetMenuBar->clear();

    // auto menus = menuBar->actions();
    // for (auto i : menus)
    // {
    //     auto menu = i->menu();

    //     QList<QAction *> actions;

    //     if (!menu)
    //     {
    //         actions.push_back(i);
    //     }
    //     else
    //     {
    //         actions = menu->actions();
    //     }

    //     auto widget = new QWidget;
    //     auto layout = new QHBoxLayout(widget);
    //     widget->setLayout(layout);

    //     for (auto a : actions)
    //     {
    //         QWidget *w;
    //         if (a->isSeparator())
    //         {
    //             auto line = new QWidget();
    //             line->setFixedWidth(1);
    //             line->setStyleSheet("background:rgb(177,177,177)");
    //             w = line;
    //         }
    //         else
    //         {

    //             _->_mainWindow->addAction(a);

    //             QToolButton *btn = new QToolButton;

    //             btn->setText(a->text());
    //             btn->setToolTip(a->text());
    //             btn->setStatusTip(a->text());

    //             a->setToolTip(a->text());
    //             a->setStatusTip(a->text());

    //             btn->setIconSize(QSize(32, 32));
    //             btn->setAutoRaise(true);

    //             if (a->menu())
    //             {
    //                 btn->setPopupMode(QToolButton::MenuButtonPopup);
    //             }
    //             btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    //             btn->setCheckable(a->isCheckable());
    //             btn->setChecked(a->isChecked());

    //             if ( a->icon().isNull() )
    //             {
    //                 static QIcon defaultIcon(":/image/Bar.net.png");
    //                 a->setIcon(defaultIcon);
    //             }
    //             btn->setDefaultAction(a);

    //             w = btn;
    //         }
    //         layout->addWidget(w);
    //     }

    //     layout->setSpacing(6);
    //     layout->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));
    //     ui->tabWidgetMenuBar->addTab(widget, i->text());

    //     menuBar->removeAction(i);

    // }

    window->setWindowFlag(Qt::FramelessWindowHint, true);

    QObject::connect(window, &QMainWindow::windowTitleChanged, this, &QRibbon::setWindowTitle);
    this->setWindowTitle(window->windowTitle());

    window->installEventFilter(this);

    window->menuBar()->setParent(0);
    window->setMenuBar(this);

    _styleMenu->actions()[0]->trigger();

    // _->_originGeometry = window->normalGeometry();

}

void QRibbon::uninstall()
{
    qDebug() << "TBD...";
}

void QRibbon::setStyleButtonVisible(bool visible)
{
    _styleMenu->setVisible(visible);
}

void QRibbon::install(QMainWindow *window)
{
    if (qobject_cast<QRibbon *>(window->menuBar()))
    {
        return;
    }
    
#ifdef Q_OS_OSX
	window->menuBar()->setNativeMenuBar(false);
#endif

    auto ribbonWidget = new QRibbon();
    ribbonWidget->initialize(window);

}

void QRibbon::setColor(const QString &colorName)
{
    static QString currentColor = "rgb(43,87,154)";

    QString stylesheet;
    if (!_->_mainWindow->styleSheet().isEmpty())
    {
        stylesheet += _->_mainWindow->styleSheet();
        _->_mainWindow->setStyleSheet("");
    }

    if (!ui->widgetContainer->styleSheet().isEmpty())
    {
        stylesheet += ui->widgetContainer->styleSheet();
        ui->widgetContainer->setStyleSheet("");
    }

    if (!qApp->styleSheet().isEmpty())
    {
        stylesheet += qApp->styleSheet();
    }

    stylesheet = stylesheet.replace(currentColor, colorName);

    qApp->setStyleSheet(stylesheet);

    currentColor = colorName;
}

void QRibbon::setWindowTitle(const QString &title)
{
    ui->labelTitle->setText(title);
}

void QRibbon::mousePressEvent(QMouseEvent *evt)
{
    if (evt->button() == Qt::LeftButton && evt->y() < ui->labelTitle->height() + 10)
    {
        _->_pressed = true;

        _->_timer->start();

        _->_originGeometry = _->_mainWindow->normalGeometry();


        _->_mouseStartPosition = evt->globalPos();

        _->_origin = _->_mainWindow->frameGeometry().topLeft();

        if (_->opacityAnimation.state() == QPropertyAnimation::Running)
        {
            _->opacityAnimation.stop();
        }

        if (_->_mainWindow->windowOpacity() != _->opacityAnimation.endValue().toDouble())
        {
            _->opacityAnimation.setDuration(250);
            _->opacityAnimation.setDirection(QAbstractAnimation::Forward);
            _->opacityAnimation.start();
        }
    }
    QMenuBar::mousePressEvent(evt);
}

void QRibbon::mouseMoveEvent(QMouseEvent *evt)
{
    if (_->_pressed)
    {
        auto &&offset = evt->globalPos() - _->_mouseStartPosition;

        bool maxOrFull = _->_mainWindow->windowState() & (Qt::WindowFullScreen | Qt::WindowMaximized);

        if (maxOrFull)
        {
            if (offset.manhattanLength() > 1)
            {
                auto w = _->_mainWindow;
                auto geom = _->_mainWindow->normalGeometry();
                w->mapFromGlobal(evt->globalPos());
                auto localX = geom.width() * (evt->globalX() * 1.0 / w->width());
                geom.moveTopLeft(QPoint(evt->x() - localX, 0));
                toggleMaximized();
                _->_mainWindow->setGeometry(geom);
                _->_origin = _->_mainWindow->frameGeometry().topLeft();
            }
        }
        else
        {
            _->_mainWindow->move(_->_origin + offset);
        }
    }
}

void QRibbon::mouseReleaseEvent(QMouseEvent *evt)
{
    if (evt->button() == Qt::LeftButton)
    {
        _->_pressed = false;

        auto moved = (evt->globalPos() - _->_mouseStartPosition).manhattanLength() > 3;

        bool maxOrFull = _->_mainWindow->windowState() & (Qt::WindowFullScreen | Qt::WindowMaximized);

        if (moved && !maxOrFull && evt->globalY() < 3)
        {
            _->_mainWindow->setGeometry(_->_originGeometry);
            toggleMaximized();
        }
        else if (_->_mainWindow->y() < 0)
        {
            _->_mainWindow->move(_->_mainWindow->x(), 0);
        }
    }

    if (_->opacityAnimation.state() == QPropertyAnimation::Running)
    {
        _->opacityAnimation.stop();
    }
    if (_->_mainWindow->windowOpacity() != _->opacityAnimation.startValue().toDouble())
    {
        _->opacityAnimation.setDuration(150);
        _->opacityAnimation.setDirection(QAbstractAnimation::Backward);
        _->opacityAnimation.start();
    }

    QMenuBar::mouseReleaseEvent(evt);
}

void QRibbon::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->y() < ui->labelTitle->height() + 10)
    {
        toggleMaximized();
    }

}


void QRibbon::toggleMaximized()
{
    _->_mainWindow->setWindowState(_->_mainWindow->windowState() & (Qt::WindowFullScreen | Qt::WindowMaximized) ? Qt::WindowActive : Qt::WindowActive | Qt::WindowMaximized);
}

void QRibbon::hideMenuBar(bool hide)
{
    // 隐藏所有菜单相关部件
    ui->tabWidgetMenuBar->setVisible(!hide);
    ui->widgetMenuBar->setVisible(!hide);
    ui->widgetMenuBar_2->setVisible(!hide);
    
    // 隐藏底部内容区域
    ui->widgetBottomBar->setVisible(!hide);
    
    // 调整QRibbon的高度
    if (hide)
    {
        // 设置为最小高度（仅标题栏）
        this->setMinimumHeight(MINIMUM_HEIGHT);
        this->setMaximumHeight(MINIMUM_HEIGHT);
        this->resize(this->width(), MINIMUM_HEIGHT);
    }
    else
    {
        // 恢复完整高度
        this->setMinimumHeight(FULL_HEIGHT);
        this->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}


void QRibbon::onHideTabFinished()
{
    ui->widgetBottomBar->setMinimumHeight(_->animationHideBar.direction() == QAbstractAnimation::Forward ? 0 : 1);
}

void QRibbon::expandTab()
{
    _->animationHideBar.setDirection(QAbstractAnimation::Backward);
    _->animationHideBar.start();

    m_bExpandStaus = true;
}

void QRibbon::hideTab()
{
    if (!_->animationHideBar.targetObject())
    {
        _->animationHideBar.setTargetObject(this);
        _->animationHideBar.setPropertyName("minimumHeight");
        _->animationHideBar.setStartValue(height());
        _->animationHideBar.setEndValue(MINIMUM_HEIGHT);
        _->animationHideBar.setEasingCurve(QEasingCurve::Linear);  
    }

    if (_->animationHideBar.state() == QAbstractAnimation::Running)
    {
        _->animationHideBar.stop();
    }

    _->animationHideBar.setDirection(QAbstractAnimation::Forward);
    _->animationHideBar.start();

    m_bExpandStaus = false;
}

void QRibbon::clickTab()
{
    if(m_bExpandStaus == false)
    {
        expandTab();
        m_bExpandStaus = true;
    }
    else
    {
        hideTab();
        m_bExpandStaus = false;
    }
}


void QRibbon::onTabChanged()
{
    if(m_bExpandStaus == false)
    {
        expandTab();
        m_bExpandStaus = true;
    }
}

void QRibbon::onLostFocus()
{
    if (!_->_mainWindow)
    {
        return;
    }

    if (_->_pressed == true)
    {
        if (_->_mainWindow->isActiveWindow())
        {
            return;
        }
        _->_pressed = false;
        _->_mainWindow->setWindowOpacity(1.0);
    }
    _->_timer->stop();
}

