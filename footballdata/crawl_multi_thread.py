#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
多线程抓取从2020年到现在的所有竞彩足球数据
"""

import threading
import os
from datetime import datetime, timedelta
from main import JczqDataCrawler

class DataCrawlerThread(threading.Thread):
    """数据抓取线程"""
    def __init__(self, start_date, end_date, thread_name):
        threading.Thread.__init__(self)
        self.start_date = start_date
        self.end_date = end_date
        self.thread_name = thread_name
        self.crawler = JczqDataCrawler()
        self.data_type = "胜平负"
        self.total_count = 0
    
    def run(self):
        """线程运行方法"""
        print(f"\n{self.thread_name} 开始抓取: {self.start_date.strftime('%Y-%m-%d')} 到 {self.end_date.strftime('%Y-%m-%d')}")
        
        try:
            # 抓取数据
            data = self.crawler.crawl_data(self.start_date, self.end_date, self.data_type)
            self.total_count = len(data)
            print(f"\n{self.thread_name} 完成！")
            print(f"{self.thread_name} 共获取 {len(data)} 条数据")
        except Exception as e:
            print(f"\n{self.thread_name} 出错: {e}")

def crawl_multi_thread():
    """多线程抓取数据"""
    print("=" * 60)
    print("开始多线程抓取竞彩足球历史数据")
    print("=" * 60)
    
    # 清理旧文件
    data_file = os.path.join("data", "胜平负.csv")
    if os.path.exists(data_file):
        os.remove(data_file)
        print("已清理旧数据文件")
    
    # 计算日期范围
    start_date = datetime(2020, 1, 1)
    end_date = datetime.now()
    
    print(f"总日期范围: {start_date.strftime('%Y-%m-%d')} 到 {end_date.strftime('%Y-%m-%d')}")
    
    # 分成年份区间
    date_ranges = []
    current_start = start_date
    
    # 按年份划分
    while current_start < end_date:
        # 计算年份结束日期
        year_end = datetime(current_start.year, 12, 31)
        if year_end > end_date:
            year_end = end_date
        
        date_ranges.append((current_start, year_end))
        current_start = datetime(current_start.year + 1, 1, 1)
    
    print(f"\n划分为 {len(date_ranges)} 个年份区间:")
    for i, (s, e) in enumerate(date_ranges):
        print(f"区间 {i+1}: {s.strftime('%Y-%m-%d')} 到 {e.strftime('%Y-%m-%d')}")
    
    # 创建线程
    threads = []
    for i, (s, e) in enumerate(date_ranges):
        thread_name = f"线程 {i+1}"
        thread = DataCrawlerThread(s, e, thread_name)
        threads.append(thread)
    
    # 启动线程
    print("\n启动所有线程...")
    for thread in threads:
        thread.start()
    
    # 等待所有线程完成
    print("\n等待所有线程完成...")
    total_data = 0
    for thread in threads:
        thread.join()
        total_data += thread.total_count
    
    print("\n" + "=" * 60)
    print("多线程抓取完成！")
    print("=" * 60)
    print(f"总数据量: {total_data} 条")
    print(f"数据已保存到: data/胜平负.csv")

if __name__ == "__main__":
    crawl_multi_thread()
