#include "GEC6818.h"

/**
 * @file main.c
 * @brief GEC6818 综合前后端应用主入口。
 *
 * @details main.c 只包含 GEC6818.h，并在这里保留整体主流程：
 * 1. 初始化 LCD、文字、触摸、设备控制和传感器。
 * 2. 进入主循环，持续处理触摸事件和监测页传感器刷新。
 * 3. 出错退出时统一释放资源。
 *
 * @note 业务逻辑不放在 main.c 中，避免入口文件和页面/外设细节耦合。
 * 新增页面、传感器或设备控制逻辑时，应优先放到 lib/src/ui_app.c
 * 或对应外设模块中，再通过 ui_app_init()/ui_app_update() 串起来。
 */

/**
 * @brief 程序入口。
 *
 * @retval 0 正常退出。
 * @retval 1 初始化或运行过程中出现错误。
 *
 * @details 主循环采用“每次调用 ui_app_update() 处理一个事件周期”的形式。
 * 这样 main.c 不需要知道触摸、传感器、相册动画等内部状态，也便于后续
 * 将应用层保持为非阻塞更新模型。
 */
int main(void)
{
    int ret;

    if(ui_app_init() != 0)
        return 1;

    while(1) {
        ret = ui_app_update();
        if(ret != 0)
            break;
    }

    ui_app_cleanup();

    return ret;
}
