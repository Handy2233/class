#ifndef UI_APP_H
#define UI_APP_H

/**
 * @file ui_app.h
 * @brief 前端页面和后端外设整合后的应用入口。
 *
 * @details 应用层对 main.c 只暴露三个生命周期函数：
 * 1. ui_app_init() 初始化 LCD、字体、触摸、LED、蜂鸣器和传感器。
 * 2. ui_app_update() 在主循环中反复调用，处理触摸和传感器轮询。
 * 3. ui_app_cleanup() 保存配置并释放所有外设资源。
 *
 * 页面细节、报警记录、相册浏览和外设控制都封装在 ui_app.c 内，避免
 * main.c 直接依赖具体硬件模块。
 */

/**
 * @brief 初始化 LCD 触摸综合应用。
 *
 * @retval 0 初始化成功。
 * @retval 1 初始化失败。
 *
 * @details 调用成功后必须通过 ui_app_update() 持续驱动应用逻辑；程序
 * 退出前应调用 ui_app_cleanup()。
 */
int ui_app_init(void);

/**
 * @brief 执行一次应用主循环更新。
 *
 * @retval 0 本次更新成功，程序应继续运行。
 * @retval 1 触摸读取或页面处理失败，程序应退出。
 *
 * @details 本函数按非阻塞事件循环设计：优先处理触摸输入，再推进传感器
 * 状态机。调用方不需要在 main.c 中额外 sleep。
 */
int ui_app_update(void);

/**
 * @brief 释放 LCD 触摸综合应用资源。
 *
 * @details 可在 ui_app_init() 成功后调用一次。函数会保存配置、关闭报警
 * 蜂鸣器、释放传感器/设备/LCD/字体/触摸资源。
 */
void ui_app_cleanup(void);

#endif
