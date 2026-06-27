#include "led.h"
#include "log.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @file led.c
 * @brief LED 控制实现。
 *
 * @details 默认优先使用 GEC6818 常见的 /dev/led_drv 字符设备。
 * 如果该设备不可用，再回退到 /sys/class/leds/<name>/brightness。
 */

#ifndef LED_DRV_PATH
#define LED_DRV_PATH "/dev/led_drv"
#endif

#ifndef LED_SYSFS_ROOT
#define LED_SYSFS_ROOT "/sys/class/leds"
#endif

#define LED_NAME_MAX 64
#define LED_PATH_MAX 256

static const unsigned char led_drv_nums[] = {7, 8, 9, 10};

typedef struct {
    char name[LED_NAME_MAX];
    int max_brightness;
    int valid;
} led_device_t;

typedef enum {
    LED_BACKEND_NONE = 0,
    LED_BACKEND_DRV,
    LED_BACKEND_SYSFS
} led_backend_t;

static led_device_t led_devices[LED_MAX_COUNT];
static int led_device_count;
static led_backend_t led_backend;
static int led_drv_fd = -1;
static int led_drv_state[LED_MAX_COUNT];

/**
 * @brief 清空 LED 模块运行时状态。
 *
 * @details led_init() 会先调用本函数，确保重复初始化时不会沿用旧的
 * sysfs 设备列表、字符设备状态或缓存亮灭状态。
 */
static void led_clear_state(void)
{
    if(led_drv_fd >= 0) {
        close(led_drv_fd);
        led_drv_fd = -1;
    }

    memset(led_devices, 0, sizeof(led_devices));
    memset(led_drv_state, 0, sizeof(led_drv_state));
    led_device_count = 0;
    led_backend = LED_BACKEND_NONE;
}

/**
 * @brief 判断 LED 名称是否为空白字符串。
 *
 * @param[in] name 待检查字符串。
 *
 * @retval 1 name 为 NULL、空串或只包含空白字符。
 * @retval 0 name 至少包含一个非空白字符。
 */
static int led_is_blank_name(const char *name)
{
    if(name == NULL)
        return 1;

    while(*name != '\0') {
        if(!isspace((unsigned char)*name))
            return 0;
        name++;
    }

    return 1;
}

/**
 * @brief 获取课程 LED 字符设备路径。
 *
 * @retval 非NULL 字符设备路径。
 *
 * @details 优先使用 GEC6818_LED_DRV 环境变量，未设置时使用 LED_DRV_PATH。
 */
static const char *led_driver_path(void)
{
    const char *path = getenv("GEC6818_LED_DRV");

    if(!led_is_blank_name(path))
        return path;

    return LED_DRV_PATH;
}

/**
 * @brief 生成指定 sysfs LED 属性文件路径。
 *
 * @param[out] out 输出路径缓冲区。
 * @param[in] out_size 输出缓冲区大小。
 * @param[in] name LED 设备名。
 * @param[in] file 属性文件名，例如 brightness 或 trigger。
 *
 * @retval 0 路径生成成功。
 * @retval -1 参数无效或路径过长。
 */
static int led_build_path(char *out,
                          size_t out_size,
                          const char *name,
                          const char *file)
{
    int written;

    if(out == NULL || name == NULL || file == NULL)
        return -1;

    written = snprintf(out, out_size, "%s/%s/%s",
                       LED_SYSFS_ROOT, name, file);
    if(written < 0 || (size_t)written >= out_size)
        return -1;

    return 0;
}

/**
 * @brief 从文本文件中读取一个整数。
 *
 * @param[in] path 文件路径。
 * @param[out] value 输出整数。
 *
 * @retval 0 读取成功。
 * @retval -1 打开、解析或参数检查失败。
 */
static int led_read_int_file(const char *path, int *value)
{
    FILE *fp;
    int read_value;

    if(path == NULL || value == NULL)
        return -1;

    fp = fopen(path, "r");
    if(fp == NULL)
        return -1;

    if(fscanf(fp, "%d", &read_value) != 1) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *value = read_value;
    return 0;
}

/**
 * @brief 向文本文件写入字符串。
 *
 * @param[in] path 文件路径。
 * @param[in] text 待写入文本。
 *
 * @retval 0 写入并关闭成功。
 * @retval -1 打开、写入或关闭失败。
 */
static int led_write_text_file(const char *path, const char *text)
{
    FILE *fp;

    if(path == NULL || text == NULL)
        return -1;

    fp = fopen(path, "w");
    if(fp == NULL)
        return -1;

    if(fputs(text, fp) == EOF) {
        fclose(fp);
        return -1;
    }

    if(fclose(fp) != 0)
        return -1;

    return 0;
}

/**
 * @brief 读取 sysfs LED 最大亮度。
 *
 * @param[in] name LED 设备名。
 *
 * @return 最大亮度值。读取失败或值非法时返回 1。
 */
static int led_read_max_brightness(const char *name)
{
    char path[LED_PATH_MAX];
    int value = 1;

    if(led_build_path(path, sizeof(path), name, "max_brightness") != 0)
        return 1;

    if(led_read_int_file(path, &value) != 0 || value <= 0)
        return 1;

    return value;
}

/**
 * @brief 判断 sysfs LED 是否具有 brightness 属性。
 *
 * @param[in] name LED 设备名。
 *
 * @retval 1 brightness 文件存在。
 * @retval 0 brightness 文件不存在或路径生成失败。
 */
static int led_has_brightness(const char *name)
{
    char path[LED_PATH_MAX];

    if(led_build_path(path, sizeof(path), name, "brightness") != 0)
        return 0;

    return access(path, F_OK) == 0;
}

/**
 * @brief 将一个 sysfs LED 名称加入管理列表。
 *
 * @param[in] name LED 设备名。
 *
 * @retval 0 添加成功、名称为空、重复或不具备 brightness 属性。
 * @retval -3 名称过长。
 *
 * @details 空名称、重复名称和不可控 LED 不视为致命错误，便于环境变量
 * 配置中混入无效项时继续尝试其他 LED。
 */
static int led_add_device(const char *name)
{
    int i;
    size_t len;

    if(led_is_blank_name(name))
        return 0;

    if(led_device_count >= LED_MAX_COUNT)
        return 0;

    len = strlen(name);
    if(len >= LED_NAME_MAX)
        return -3;

    for(i = 0; i < led_device_count; i++) {
        if(strcmp(led_devices[i].name, name) == 0)
            return 0;
    }

    if(!led_has_brightness(name))
        return 0;

    snprintf(led_devices[led_device_count].name,
             sizeof(led_devices[led_device_count].name),
             "%s", name);
    led_devices[led_device_count].max_brightness =
        led_read_max_brightness(name);
    led_devices[led_device_count].valid = 1;
    led_device_count++;

    return 0;
}

/**
 * @brief 原地去掉逗号分隔 LED 名称两端的空白字符。
 *
 * @param[in,out] text 待裁剪字符串。
 */
static void led_trim_token(char *text)
{
    char *start;
    char *end;

    if(text == NULL)
        return;

    start = text;
    while(isspace((unsigned char)*start))
        start++;

    if(start != text)
        memmove(text, start, strlen(start) + 1);

    end = text + strlen(text);
    while(end > text && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
}

/**
 * @brief 从 GEC6818_LED_NAMES 形式的逗号列表加载 LED 名称。
 *
 * @param[in] names 逗号分隔的 LED 名称列表。
 *
 * @retval >=0 当前已注册 LED 数量。
 * @retval -3 列表字符串过长或 LED 名称过长。
 */
static int led_load_names_from_list(const char *names)
{
    char buffer[LED_MAX_COUNT * LED_NAME_MAX];
    char *cursor;
    char *token;
    int ret;

    if(led_is_blank_name(names))
        return 0;

    if(strlen(names) >= sizeof(buffer))
        return -3;

    snprintf(buffer, sizeof(buffer), "%s", names);
    cursor = buffer;

    while((token = strtok(cursor, ",")) != NULL) {
        cursor = NULL;
        led_trim_token(token);
        ret = led_add_device(token);
        if(ret != 0)
            return ret;
    }

    return led_device_count;
}

/**
 * @brief 从 GEC6818_LED0..GEC6818_LED3 环境变量加载 LED 名称。
 *
 * @retval >=0 当前已注册 LED 数量。
 * @retval -3 某个 LED 名称过长。
 */
static int led_load_names_from_env_slots(void)
{
    int i;

    for(i = 0; i < LED_MAX_COUNT; i++) {
        char env_name[32];
        const char *name;
        int ret;

        snprintf(env_name, sizeof(env_name), "GEC6818_LED%d", i);
        name = getenv(env_name);
        if(name == NULL)
            continue;

        ret = led_add_device(name);
        if(ret != 0)
            return ret;
    }

    return led_device_count;
}

/**
 * @brief qsort() 使用的 LED 名称字典序比较函数。
 *
 * @param[in] left 左侧 char * 指针地址。
 * @param[in] right 右侧 char * 指针地址。
 *
 * @return strcmp() 的比较结果。
 */
static int led_entry_compare(const void *left, const void *right)
{
    const char *a = *(const char * const *)left;
    const char *b = *(const char * const *)right;

    return strcmp(a, b);
}

/**
 * @brief 扫描 /sys/class/leds 并注册可控 LED。
 *
 * @retval >=0 扫描后已注册 LED 数量。
 * @retval -1 内存分配失败。
 * @retval -2 打开 LED sysfs 根目录失败。
 *
 * @details 先收集并排序目录名，再逐个注册。排序可以保证不同文件系统
 * 返回目录项顺序不一致时，UI 中 LED 顺序仍然稳定。
 */
static int led_scan_sysfs(void)
{
    DIR *dir;
    struct dirent *entry;
    char *names[LED_MAX_COUNT * 4];
    int name_count = 0;
    int i;

    dir = opendir(LED_SYSFS_ROOT);
    if(dir == NULL) {
        LOG_PERROR("opendir " LED_SYSFS_ROOT);
        return -2;
    }

    while((entry = readdir(dir)) != NULL &&
          name_count < (int)(sizeof(names) / sizeof(names[0]))) {
        if(entry->d_name[0] == '.')
            continue;

        names[name_count] = malloc(strlen(entry->d_name) + 1);
        if(names[name_count] == NULL) {
            for(i = 0; i < name_count; i++)
                free(names[i]);
            closedir(dir);
            return -1;
        }
        strcpy(names[name_count], entry->d_name);
        name_count++;
    }

    closedir(dir);

    qsort(names, (size_t)name_count, sizeof(names[0]), led_entry_compare);

    for(i = 0; i < name_count; i++) {
        int ret = led_add_device(names[i]);
        if(ret != 0) {
            int j;

            for(j = i; j < name_count; j++)
                free(names[j]);
            return ret;
        }
        free(names[i]);
    }

    return led_device_count;
}

/**
 * @brief 关闭 sysfs LED 的内核 trigger。
 *
 * @param[in] led LED 设备描述。
 *
 * @retval 0 trigger 不存在或已成功写为 none。
 * @retval -1 参数无效、路径生成失败或写 trigger 失败。
 *
 * @details 某些 LED 默认被 heartbeat、timer 等 trigger 接管。写
 * brightness 前先关闭 trigger，避免用户态刚写入又被内核覆盖。
 */
static int led_disable_trigger(const led_device_t *led)
{
    char path[LED_PATH_MAX];

    if(led == NULL || !led->valid)
        return -1;

    if(led_build_path(path, sizeof(path), led->name, "trigger") != 0)
        return -1;

    if(access(path, F_OK) != 0)
        return 0;

    if(led_write_text_file(path, "none") != 0) {
        LOG_PERROR("write led trigger");
        return -1;
    }

    return 0;
}

/**
 * @brief 尝试初始化课程 LED 字符设备后端。
 *
 * @retval >0 字符设备后端可用，返回支持的 LED 数量。
 * @retval -1 字符设备不可用。
 */
static int led_init_drv(void)
{
    const char *path = led_driver_path();
    int fd;
    int i;

    fd = open(path, O_RDWR);
    if(fd < 0)
        return -1;
    close(fd);

    led_backend = LED_BACKEND_DRV;
    led_device_count = (int)(sizeof(led_drv_nums) / sizeof(led_drv_nums[0]));

    for(i = 0; i < led_device_count; i++) {
        snprintf(led_devices[i].name, sizeof(led_devices[i].name),
                 "D%d", led_drv_nums[i]);
        led_devices[i].max_brightness = 1;
        led_devices[i].valid = 1;
        led_drv_state[i] = 0;
    }

    return led_device_count;
}

/**
 * @brief 通过课程字符设备协议设置 LED。
 *
 * @param[in] index LED 下标。
 * @param[in] on 非 0 点亮，0 熄灭。
 *
 * @retval 0 写入成功。
 * @retval -1 打开或写入字符设备失败。
 *
 * @details 驱动协议为写入两个字节：第 1 字节表示状态，第 2 字节表示
 * D7/D8/D9/D10 的灯号。字符设备没有读取接口，因此成功写入后更新
 * led_drv_state[] 作为 led_get() 的缓存来源。
 */
static int led_drv_set(int index, int on)
{
    const char *path = led_driver_path();
    unsigned char buf[2];
    ssize_t written;
    int fd;

    fd = open(path, O_RDWR);
    if(fd < 0) {
        LOG_PERROR("open " LED_DRV_PATH);
        return -1;
    }

    buf[0] = on ? 1U : 0U;
    buf[1] = led_drv_nums[index];

    written = write(fd, buf, sizeof(buf));
    close(fd);

    if(written >= 0) {
        led_drv_state[index] = on ? 1 : 0;
        return 0;
    }

    LOG_PERROR("control " LED_DRV_PATH " by write");
    return -1;
}

/**
 * @brief 初始化 LED 控制后端。
 *
 * @retval >0 可控制的 LED 数量。
 * @retval -1 未发现可用 LED。
 * @retval -2 扫描 sysfs LED 目录失败。
 * @retval -3 LED 名称配置过长。
 *
 * @details 优先尝试课程字符设备 /dev/led_drv；不可用时再按环境变量
 * 或 sysfs 扫描结果注册 LED。
 */
int led_init(void)
{
    const char *names;
    int ret;

    led_clear_state();

    ret = led_init_drv();
    if(ret > 0)
        return ret;

    names = getenv("GEC6818_LED_NAMES");
    ret = led_load_names_from_list(names);
    if(ret < 0)
        return ret;

    ret = led_load_names_from_env_slots();
    if(ret < 0)
        return ret;

    if(led_device_count == 0) {
        ret = led_scan_sysfs();
        if(ret < 0)
            return ret;
    }

    if(led_device_count <= 0) {
        LOG_WARN("no led device found in " LED_DRV_PATH " or " LED_SYSFS_ROOT);
        return -1;
    }

    led_backend = LED_BACKEND_SYSFS;
    return led_device_count;
}

/**
 * @brief 释放 LED 模块状态。
 */
void led_uninit(void)
{
    led_clear_state();
}

/**
 * @brief 获取当前可控制 LED 数量。
 *
 * @return LED 数量。
 */
int led_count(void)
{
    return led_device_count;
}

/**
 * @brief 获取 LED 显示名称。
 *
 * @param[in] index LED 下标。
 *
 * @retval 非NULL LED 名称。
 * @retval NULL 下标非法。
 */
const char *led_name(int index)
{
    if(index < 0 || index >= led_device_count)
        return NULL;

    return led_devices[index].name;
}

/**
 * @brief 设置指定 LED 亮灭。
 *
 * @param[in] index LED 下标。
 * @param[in] on 非 0 点亮，0 熄灭。
 *
 * @retval 0 设置成功。
 * @retval -1 下标非法。
 * @retval -2 sysfs trigger 处理失败。
 * @retval -3 sysfs brightness 写入失败。
 * @retval -4 字符设备控制失败。
 */
int led_set(int index, int on)
{
    char path[LED_PATH_MAX];
    char value[16];
    const led_device_t *led;

    if(index < 0 || index >= led_device_count)
        return -1;

    if(led_backend == LED_BACKEND_DRV)
        return led_drv_set(index, on) == 0 ? 0 : -4;

    led = &led_devices[index];
    if(led_disable_trigger(led) != 0)
        return -2;

    if(led_build_path(path, sizeof(path), led->name, "brightness") != 0)
        return -3;

    snprintf(value, sizeof(value), "%d", on ? led->max_brightness : 0);
    if(led_write_text_file(path, value) != 0) {
        LOG_PERROR("write led brightness");
        return -3;
    }

    return 0;
}

/**
 * @brief 读取指定 LED 当前状态。
 *
 * @param[in] index LED 下标。
 * @param[out] on 输出状态，非 0 表示点亮。
 *
 * @retval 0 读取成功。
 * @retval -1 参数或下标非法。
 * @retval -2 sysfs brightness 读取失败。
 */
int led_get(int index, int *on)
{
    char path[LED_PATH_MAX];
    int value;

    if(index < 0 || index >= led_device_count || on == NULL)
        return -1;

    if(led_backend == LED_BACKEND_DRV) {
        *on = led_drv_state[index];
        return 0;
    }

    if(led_build_path(path, sizeof(path),
                      led_devices[index].name, "brightness") != 0)
        return -2;

    if(led_read_int_file(path, &value) != 0) {
        LOG_PERROR("read led brightness");
        return -2;
    }

    *on = value > 0;
    return 0;
}

/**
 * @brief 翻转指定 LED 状态。
 *
 * @param[in] index LED 下标。
 *
 * @retval 0 翻转成功。
 * @retval <0 led_get() 或 led_set() 返回的错误。
 */
int led_toggle(int index)
{
    int on;
    int ret;

    ret = led_get(index, &on);
    if(ret != 0)
        return ret;

    return led_set(index, !on);
}
