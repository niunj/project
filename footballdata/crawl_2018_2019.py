#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
抓取2018-2019年的竞彩足球数据
"""

from datetime import datetime
from main import JczqDataCrawler

def crawl_2018_2019():
    """抓取2018-2019年的数据"""
    print("=" * 60)
    print("开始抓取2018-2019年的数据")
    print("=" * 60)
    
    # 设置日期范围
    start_date = datetime(2018, 1, 1)
    end_date = datetime(2019, 12, 31)
    
    print(f"\n抓取区间: {start_date.strftime('%Y-%m-%d')} 到 {end_date.strftime('%Y-%m-%d')}")
    
    # 创建爬虫实例
    crawler = JczqDataCrawler()
    
    # 抓取数据
    data = crawler.crawl_data(start_date, end_date, "胜平负")
    
    print(f"\n抓取完成！共获取 {len(data)} 条数据")
    print(f"数据已保存到: data/胜平负.csv")

if __name__ == "__main__":
    crawl_2018_2019()
