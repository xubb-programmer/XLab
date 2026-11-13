#include "ButtonMenuWidget.h"
#include "ui_ButtonMenuWidget.h"

ButtonMenuWidget::ButtonMenuWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ButtonMenuWidget)
{
    ui->setupUi(this);
    this->setAttribute(Qt::WA_AcceptTouchEvents, true);
}

ButtonMenuWidget::~ButtonMenuWidget()
{
    delete ui;
}

void ButtonMenuWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin=pPlugin;
    m_pPlugin->setStyle("day");
    qDebug()<<"init"<<m_pPlugin->getConfigPath()<<m_pPlugin->getId();
    creatBtnStatus(m_pPlugin->getConfigPath() + QString("/btnstate_%1.xml").arg(m_pPlugin->getId()));
}

void ButtonMenuWidget::initData()
{

}

void ButtonMenuWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (auto pPlugin, vPlugins) {
        connect(pPlugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(slotsignalPluginMsg(pluginmsg))); //接收
        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),pPlugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)));//发送
    }
}

void ButtonMenuWidget::resetQss(const QString &strStyleName)
{

}

bool ButtonMenuWidget::creatBtnStatus(QString filepath)
{
    QFile file(filepath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
        return false;
    }
    QDomDocument doc;//创建DOM树
    QString error;
    int row = 0, column = 0;
    if (!doc.setContent(&file, false, &error, &row, &column))//填充dom树并且错误判断
    {
        return false;
    }
    QDomElement eRoot = doc.documentElement();//获取dom的根标签
    QDomNodeList btnStateList = eRoot.childNodes();//获取dom根标签的子标签
    for (int nIndex = 0;nIndex < btnStateList.count();++nIndex)//遍历子标签
    {
        QDomElement element = btnStateList.at(nIndex).toElement();
        QString strTagName = element.tagName();
        if(strTagName == "status")
        {
            //创建按钮
            MenuWidget *pMenu=new MenuWidget(ui->background);
            qRegisterMetaType<BtnBaseInfo>("BtnBaseInfo");
            connect(pMenu,SIGNAL(signalSendMsg(BtnBaseInfo)),this,SLOT(slotBtnSendMsg(BtnBaseInfo)));
             ui->horizontalLayout->addWidget(pMenu);
            //读取按钮配置
            pMenu->m_stBaseInfo.id=element.attribute("id").toInt();


            QDomNodeList nextList = element.childNodes();
            //读取按钮大小与等级信息
            for(int j = 0; j < nextList.count();++j)
            {

                QDomElement nextElement = nextList.at(j).toElement();
                strTagName = nextElement.tagName();
                if(strTagName == "state")
                {
                    //初始化 读取id为1的配置状态

                    if(nextElement.attribute("stateid").toInt()==1)
                    {
                        pMenu->changeBtnName(nextElement.attribute("name"));
                        pMenu->m_stBaseInfo.name=element.attribute("name");
                        pMenu->m_stBaseInfo.stateid=nextElement.attribute("stateid").toInt();
                        pMenu->changeBtnSize(nextElement.attribute("width").toInt(),nextElement.attribute("height").toInt());
                        pMenu->loadBtnQss(m_pPlugin->getStyle(),nextElement.attribute("level").toInt(),nextElement.attribute("qss"),nextElement.attribute("padding").toInt(),nextElement.attribute("check").toInt());
                      //  pMenu->move(nextElement.attribute("x").toInt(),nextElement.attribute("y").toInt());


                    }
                    //配置信息
                    BtnStatusInfo info;
                    info.width=nextElement.attribute("width").toInt();
                    info.height=nextElement.attribute("height").toInt();
                    info.level=nextElement.attribute("level").toInt();
                    info.stateid=nextElement.attribute("stateid").toInt();
                    info.name=nextElement.attribute("name");
                    info.qss=nextElement.attribute("qss");
                    info.x=nextElement.attribute("x").toInt();
                    info.y=nextElement.attribute("y").toInt();
                    info.pad=nextElement.attribute("padding").toInt();
                    info.check=nextElement.attribute("check").toInt();
                    //记录多种状态信息
                    pMenu->m_statusInfo.insert(info.stateid,info);

                }

            }
            //记录按钮与id关系
            m_allBtnStatus.insert(pMenu->m_stBaseInfo.id,pMenu);
            pMenu->show();
        }
    }
    return true;

}

void ButtonMenuWidget::slotBtnSendMsg(BtnBaseInfo data)
{
    pluginmsg msg;
    memset(&msg,0,sizeof(pluginmsg));
    msg.id=data.id;
    msg.state=data.stateid;
    msg.curtype=1;//规定自身点击发送信号，类型为0
    msg.type=m_pPlugin->getId();//type为当前按钮等级
    emit signalPluginMsg(msg);
}

void ButtonMenuWidget::slotsignalPluginMsg(pluginmsg msg)
{
    //获取除自身以外的消息，改变按钮样式
    if(msg.type==m_pPlugin->getId())
    {
        return;
    }

    //查找信号对应按钮
    itor=m_allBtnStatus.find(msg.id);
    if(itor!=m_allBtnStatus.end())
    {
        //查找对应stateid样式
        itor.value()->m_it=itor.value()->m_statusInfo.find(msg.state);
        if(itor.value()->m_it!=itor.value()->m_statusInfo.end())
        {

            itor.value()->changeBtnName(itor.value()->m_it.value().name);
            itor.value()->loadBtnQss(m_pPlugin->getStyle(),itor.value()->m_it.value().level,itor.value()->m_it.value().qss,itor.value()->m_it.value().pad,itor.value()->m_it.value().check);
            itor.value()->changeBtnSize(itor.value()->m_it.value().width,itor.value()->m_it.value().height);
            itor.value()->m_stBaseInfo.stateid=itor.value()->m_it.value().stateid;
        }
        //未找到该样式
        else
        {
            return;
        }
    }
    //未找到该信号
    else
    {
        return;
    }
}
