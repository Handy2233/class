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

static const char *led_driver_path(void)
{
    const char *path = getenv("GEC6818_LED_DRV");

    if(!led_is_blank_name(path))
        return path;

    return LED_DRV_PATH;
}

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

static int led_has_brightness(const char *name)
{
    char path[LED_PATH_MAX];

    if(led_build_path(path, sizeof(path), name, "brightness") != 0)
        return 0;

    return access(path, F_OK) == 0;
}

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

static int led_entry_compare(const void *left, const void *right)
{
    const char *a = *(const char * const *)left;
    const char *b = *(const char * const *)right;

    return strcmp(a, b);
}

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

void led_uninit(void)
{
    led_clear_state();
}

int led_count(void)
{
    return led_device_count;
}

const char *led_name(int index)
{
    if(index < 0 || index >= led_device_count)
        return NULL;

    return led_devices[index].name;
}

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

int led_toggle(int index)
{
    int on;
    int ret;

    ret = led_get(index, &on);
    if(ret != 0)
        return ret;

    return led_set(index, !on);
}
