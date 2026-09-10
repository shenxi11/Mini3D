/*
 * 模块名: ChineseUi
 * 功能概述: 在控件创建前安装内嵌的 Qt 简体中文译文。
 * 对外接口: initializeChineseUi
 * 依赖关系: Qt Core、mini3d_chinese 资源
 * 输入输出: 无参数；为当前应用安装中文翻译器。
 * 异常与错误: 内嵌资源失效时终止启动并报告原因。
 * 维护说明: 译文由应用持有；不改变数字区域格式或用户内容。
 */
#pragma once

namespace mini3d::editor {
// 在创建窗口控件前调用；重复调用不会重复安装翻译器。
void initializeChineseUi();
} // namespace mini3d::editor
