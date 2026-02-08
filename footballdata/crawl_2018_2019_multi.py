#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
多线程抓取2018-2019年的竞彩足球数据
"""

import threading
from datetime import datetime
from main import JczqDataCrawler

class YearCrawlerThread(threading.Thread):
    """年份数据抓取线程"""
    def __init__(self, year, thread_name):
        threading.Thread.__init__(self)
        self.year = year
        self.thread_name = thread_name
        self.crawler = JczqDataCrawler()
        self.data_type = "胜平负"
        self.total_count = 0
    
    def run(self):
        """线程运行方法"""
        start_date = datetime(self.year, 1, 1)
        end_date = datetime(self.year, 12, 31)
        
        print(f"\n{self.thread_name} 开始抓取 {self.year} 年的数据")
        
        try:
            # 抓取数据
            data = self.crawler.crawl_data(start_date, end_date, self.data_type)
            self.total_count = len(data)
            print(f"\n{self.thread_name} 完成！")
            print(f"{self.thread_name} 共获取 {len(data)} 条数据")
        except Exception as e:
            print(f"\n{self.thread_name} 出错: {e}")

def crawl_2018_2019_multi():
    """多线程抓取2018-2019年的数据"""
    print("=" * 60)
    print("开始多线程抓取2018-2019年数据")
    print("=" * 60)
    
    # 创建线程
    threads = []
    
    # 2018年线程
    thread_2018 = YearCrawlerThread(2018, "2018年线程")
    threads.append(thread_2018)
    
    # 2019年线程
    thread_2019 = YearCrawlerThread(2019, "2019年线程")
    threads.append(thread_2019)
    
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
    print("2018-2019年数据抓取完成！")
    print("=" * 60)
    print(f"总数据量: {total_data} 条")
    print(f"数据已追加到: data/胜平负.csv")

if __name__ == "__main__":
    crawl_2018_2019_multi()
