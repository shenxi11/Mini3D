/*
 * 模块名: ChineseUi
 * 功能概述: 保证静态库内的中文资源被链接并安装到应用。
 * 对外接口: initializeChineseUi
 * 依赖关系: QCoreApplication、QTranslator、Qt 资源系统
 * 输入输出: 内嵌 qm 到 Qt 标准控件译文。
 * 异常与错误: 缺失内嵌译文时明确终止，不静默退回英文。
 * 维护说明: 仅初始化界面文本，不更改存储协议或数值区域设置。
 */
#include "ChineseUi.h"

#include <QCoreApplication>
#include <QTranslator>

// Q_INIT_RESOURCE 必须在全局命名空间调用，以链接静态资源符号。
static void initializeChineseResources() {
    Q_INIT_RESOURCE(mini3d_chinese);
}

namespace mini3d::editor {
void initializeChineseUi() {
    auto* application = QCoreApplication::instance();
    if (application->findChild<QTranslator*>(QStringLiteral("Mini3DChineseTranslator"))) {
        return;
    }
    initializeChineseResources();
    auto* translator = new QTranslator(application);
    translator->setObjectName(QStringLiteral("Mini3DChineseTranslator"));
    if (!translator->load(QStringLiteral(":/mini3d/translations/qtbase_zh_CN.qm")) ||
        !QCoreApplication::installTranslator(translator)) {
        qFatal("%s", "无法加载内嵌的 Qt 简体中文资源。");
    }
}
} // namespace mini3d::editor
