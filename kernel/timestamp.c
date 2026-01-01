/*************************************************************************//**
 *****************************************************************************
 * @file   kernel/timestamp.c
 * @brief  时间戳功能实现
 * @date   2026
 *****************************************************************************
 *****************************************************************************/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

/* CMOS端口定义 */
#define CMOS_ADDR_PORT  0x70
#define CMOS_DATA_PORT  0x71

/* CMOS寄存器地址 */
#define CMOS_SEC        0x00
#define CMOS_MIN        0x02
#define CMOS_HOUR       0x04
#define CMOS_DAY        0x07
#define CMOS_MONTH      0x08
#define CMOS_YEAR       0x09
#define CMOS_STATUS_B   0x0B

/* 记录系统启动时的时间戳基准 */
PRIVATE u32 boot_timestamp = 0;
PRIVATE int timestamp_initialized = 0;

/*****************************************************************************
 *                                read_cmos
 *****************************************************************************/
/**
 * 从CMOS读取一个字节
 */
PRIVATE u8 read_cmos(u8 reg)
{
    out_byte(CMOS_ADDR_PORT, reg);
    return in_byte(CMOS_DATA_PORT);
}

/*****************************************************************************
 *                                bcd_to_bin
 *****************************************************************************/
/**
 * 将BCD码转换为二进制
 */
PRIVATE u8 bcd_to_bin(u8 bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*****************************************************************************
 *                                is_leap_year
 *****************************************************************************/
/**
 * 判断是否为闰年
 */
PRIVATE int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*****************************************************************************
 *                                days_in_month
 *****************************************************************************/
/**
 * 获取指定月份的天数
 */
PRIVATE int days_in_month(int year, int month)
{
    static int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year))
        return 29;
    return days[month - 1];
}

/*****************************************************************************
 *                                calc_timestamp
 *****************************************************************************/
/**
 * 从年月日时分秒计算Unix时间戳（简化版，从2000年开始计算）
 */
PRIVATE u32 calc_timestamp(int year, int month, int day, int hour, int min, int sec)
{
    u32 timestamp = 0;
    int y, m;
    
    /* 计算从2000年1月1日到指定日期的秒数 */
    /* 2000年1月1日 00:00:00 UTC 对应的Unix时间戳是 946684800 */
    
    /* 累加完整年份的秒数 */
    for (y = 2000; y < year; y++) {
        timestamp += is_leap_year(y) ? 366 * 86400 : 365 * 86400;
    }
    
    /* 累加当年完整月份的秒数 */
    for (m = 1; m < month; m++) {
        timestamp += days_in_month(year, m) * 86400;
    }
    
    /* 累加当月的天数（day-1，因为当天还没过完） */
    timestamp += (day - 1) * 86400;
    
    /* 累加小时、分钟、秒 */
    timestamp += hour * 3600;
    timestamp += min * 60;
    timestamp += sec;
    
    /* 加上2000年之前的偏移 */
    timestamp += 946684800;
    
    return timestamp;
}

/*****************************************************************************
 *                                read_rtc_timestamp
 *****************************************************************************/
/**
 * 从RTC读取当前时间并转换为时间戳
 */
PRIVATE u32 read_rtc_timestamp()
{
    u8 sec, min, hour, day, month, year;
    u8 status_b;
    
    /* 读取CMOS状态寄存器B判断是BCD还是二进制格式 */
    status_b = read_cmos(CMOS_STATUS_B);
    
    /* 读取时间 */
    sec   = read_cmos(CMOS_SEC);
    min   = read_cmos(CMOS_MIN);
    hour  = read_cmos(CMOS_HOUR);
    day   = read_cmos(CMOS_DAY);
    month = read_cmos(CMOS_MONTH);
    year  = read_cmos(CMOS_YEAR);
    
    /* 如果是BCD格式则转换 */
    if (!(status_b & 0x04)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }
    
    /* year是两位数，假设是20xx年 */
    int full_year = 2000 + year;
    
    return calc_timestamp(full_year, month, day, hour, min, sec);
}

/*****************************************************************************
 *                                init_timestamp
 *****************************************************************************/
/**
 * <Ring 0~1> 初始化时间戳模块
 */
PUBLIC void init_timestamp()
{
    if (!timestamp_initialized) {
        boot_timestamp = read_rtc_timestamp();
        timestamp_initialized = 1;
    }
}

/*****************************************************************************
 *                                get_timestamp
 *****************************************************************************/
/**
 * <Ring 0~3> 获取当前32位时间戳
 * 通过RTC基准时间 + ticks计算得到近似当前时间
 */
PUBLIC u32 get_timestamp()
{
    if (!timestamp_initialized) {
        init_timestamp();
    }
    
    /* 根据ticks计算经过的秒数（假设HZ=100，即每秒100个ticks） */
    u32 elapsed_secs = ticks / HZ;
    
    return boot_timestamp + elapsed_secs;
}

/*****************************************************************************
 *                                generate_checksum_key
 *****************************************************************************/
/**
 * <Ring 0~3> 生成校验用的key
 * key = 当前时间戳（32位） ^ ticks
 */
PUBLIC u32 generate_checksum_key()
{
    u32 ts = get_timestamp();
    return ts ^ (u32)ticks;
}
