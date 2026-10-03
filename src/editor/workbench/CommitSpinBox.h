/*
 * 模块名: CommitSpinBox
 * 功能概述: 数值输入先编辑后提交，支持安全取消与一次提交的拖动微调。
 * 对外接口: CommitSpinBox
 * 依赖关系: Qt Widgets
 * 输入输出: 文本或 Alt 拖动到一次 valueChanged；错误保留文本并报告原因。
 * 异常与错误: 拒绝非有限值、范围外输入或 ViewModel 同步拒绝的提交。
 * 维护说明: 拖动只预览字段文字，松开提交；不直接管理场景或撤销栈。
 */
#pragma once

#include <QDoubleSpinBox>

namespace mini3d::editor {
/** @brief 兼容 QDoubleSpinBox 的程序赋值接口，用户输入具有显式提交/取消边界。 */
class CommitSpinBox final : public QDoubleSpinBox {
    Q_OBJECT
  public:
    explicit CommitSpinBox(QWidget* parent = nullptr);
    [[nodiscard]] QString validationMessage() const;
    /** @brief 只在当前字段提交期间接收 ViewModel 的同步失败原因。 */
    void rejectSubmission(const QString& reason);
    /** @brief 文档/操作上下文切换时丢弃草稿和错误，不提交数值或生成历史。 */
    void resetInput();

  protected:
    QValidator::State validate(QString& text, int& position) const override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void stepBy(int steps) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;

  private:
    bool submitInput();
    void cancelInput();
    void showValidation(const QString& reason);
    [[nodiscard]] QString formattedValue(double value) const;
    QString validationMessage_;
    QString submissionError_;
    bool submitting_ = false;
    bool scrubbing_ = false;
    double scrubStartValue_ = 0;
    double scrubStartX_ = 0;
};
} // namespace mini3d::editor
