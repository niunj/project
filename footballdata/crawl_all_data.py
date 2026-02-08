#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
抓取从2020年到现在的所有竞彩足球数据
"""

from datetime import datetime, timedelta
from main import JczqDataCrawler

def crawl_all_data():
    """抓取从2020年到现在的所有数据"""
    crawler = JczqDataCrawler()
    
    # 设置日期范围：从2020年1月1日到今天
    start_date = datetime(2020, 1, 1)
    end_date = datetime.now()
    data_type = "胜平负"
    
    print("=" * 60)
    print("开始抓取竞彩足球历史数据")
    print("=" * 60)
    print(f"开始日期: {start_date.strftime('%Y-%m-%d')}")
    print(f"结束日期: {end_date.strftime('%Y-%m-%d')}")
    print(f"数据类型: {data_type}")
    print("=" * 60)
    
    # 开始抓取数据
    data = crawler.crawl_data(start_date, end_date, data_type)
    
    print("=" * 60)
    print("抓取完成！")
    print("=" * 60)
    print(f"总共获取到 {len(data)} 条数据")
    
    if data:
        dates = [item.get('日期', '') for item in data]
        unique_dates = list(set(dates))
        print(f"数据日期范围: {min(unique_dates)} 到 {max(unique_dates)}")
        print(f"总共涵盖 {len(unique_dates)} 个不同的日期")
    
    print(f"数据已保存到: data/{data_type}.csv")

if __name__ == "__main__":
    crawl_all_data()
