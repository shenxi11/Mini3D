/*
 * 模块名: LocalAutomationBridge
 * 功能概述: 在应用线程提供默认关闭的同用户命名管道及会话恢复。
 * 对外接口: Options、start、stop、运行状态和诊断身份查询。
 * 依赖关系: EditorApiService、可选 ObservationService、Qt Network。
 * 输入输出: 有界 JSON-RPC 帧到既有业务结果；认证材料只写受限文件。
 * 异常与错误: 认证/信封失败不消费序号，已接收领域失败终结账本。
 * 维护说明: 不创建业务线程、不关闭窗口、不调用阻塞等待或用户动作。
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

namespace mini3d::editor::api {
class EditorApiService;
}
namespace mini3d::editor::observation {
class ObservationService;
}
namespace mini3d::editor::automation {
/** @brief 业务和观察必须比桥存活更久；所有调用均在同一应用线程。 */
class LocalAutomationBridge final : public QObject {
  public:
    struct Options {
        bool enabled = false;
        QString descriptorPath;
        QStringList permissions;
        QStringList readRoots;
        QStringList writeRoots;
    };
    explicit LocalAutomationBridge(api::EditorApiService& service,
                                    observation::ObservationService* observation = nullptr,
                                    QObject* parent = nullptr);
    ~LocalAutomationBridge() override;
    /** @brief enabled=false 保持关闭；父目录须已存在，CREATE_NEW 不覆盖文件。 */
    bool start(const Options& options, QString& error);
    /** @brief 只停止自有连接/捕获及核验过的描述文件，不改变用户文档/窗口。 */
    void stop(QString* error = nullptr);
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString descriptorPath() const;
    [[nodiscard]] QString pipeName() const;
    [[nodiscard]] QString bridgeId() const;
    [[nodiscard]] std::size_t connectionCount() const;
    [[nodiscard]] std::size_t queuedCount() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mini3d::editor::automation
