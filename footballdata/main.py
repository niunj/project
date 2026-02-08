#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
中国体育彩票竞彩足球历史数据抓取工具
支持胜平负、让球胜平负、比分、总进球数、半全场等不同类型的数据抓取
"""

import os
import time
import random
import requests
from bs4 import BeautifulSoup
import pandas as pd
import tkinter as tk
from tkinter import ttk, messagebox
from datetime import datetime, timedelta
import threading
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

class JczqDataCrawler:
    def __init__(self):
        self.base_url = "https://www.sporttery.cn"
        self.headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
        }
        self.data_dir = "data"
        if not os.path.exists(self.data_dir):
            os.makedirs(self.data_dir)
        self.driver = None
        
    def get_driver(self):
        """获取浏览器驱动实例"""
        if self.driver is None:
            # 配置Chrome选项
            chrome_options = Options()
            chrome_options.add_argument("--headless")  # 无头模式
            chrome_options.add_argument("--disable-gpu")
            chrome_options.add_argument("--no-sandbox")
            chrome_options.add_argument(f"user-agent={self.headers['User-Agent']}")
            chrome_options.add_argument("--disable-extensions")
            chrome_options.add_argument("--disable-dev-shm-usage")
            chrome_options.add_argument("--disable-browser-side-navigation")
            chrome_options.add_argument("--disable-images")  # 禁用图片加载
            
            # 启动浏览器
            self.driver = webdriver.Chrome(options=chrome_options)
            self.driver.set_page_load_timeout(30)
        return self.driver
    
    def close_driver(self):
        """关闭浏览器驱动"""
        if self.driver:
            try:
                self.driver.quit()
            except:
                pass
            self.driver = None
    
    def get_page_content(self, url):
        """获取页面内容"""
        max_retries = 3
        for retry in range(max_retries):
            try:
                # 获取浏览器驱动
                driver = self.get_driver()
                
                # 访问URL
                driver.get(url)
                
                # 等待页面加载完成
                WebDriverWait(driver, 10).until(
                    EC.presence_of_element_located((By.TAG_NAME, "table"))
                )
                
                # 获取页面内容
                content = driver.page_source
                
                return content
            except Exception as e:
                print(f"获取页面失败 (尝试 {retry+1}/{max_retries}): {e}")
                if retry < max_retries - 1:
                    time.sleep(random.uniform(1, 2))
                else:
                    # 如果失败，关闭驱动
                    self.close_driver()
                    return None
        return None
    
    def crawl_data(self, start_date, end_date, data_type):
        """抓取指定类型的竞彩足球数据"""
        all_data = []
        current_date = start_date
        max_batch_days = 15  # 每次抓取不超过15天的数据（每半个月）
        
        try:
            while current_date <= end_date:
                # 计算批次结束日期，不超过15天
                batch_end = current_date + timedelta(days=max_batch_days - 1)
                if batch_end > end_date:
                    batch_end = end_date
                
                # 格式化日期
                start_str = current_date.strftime("%Y-%m-%d")
                end_str = batch_end.strftime("%Y-%m-%d")
                print(f"\n========================================")
                print(f"抓取日期范围: {start_str} 到 {end_str}, 类型: {data_type}")
                
                # 直接使用Selenium模拟网页日期选择器操作，这是最可靠的方式
                batch_data = []
                success = False
                
                try:
                    print("使用Selenium模拟网页日期选择器...")
                    driver = self.get_driver()
                    driver.get(self.base_url + "/jc/zqsgkj/")
                    time.sleep(5)  # 增加等待时间，确保页面完全加载
                    
                    # 打印页面标题和当前URL，确认页面加载正确
                    print(f"页面标题: {driver.title}")
                    print(f"当前URL: {driver.current_url}")
                    
                    # 打印页面中的所有按钮，了解页面结构
                    print("\n页面中的按钮:")
                    buttons = driver.find_elements(By.TAG_NAME, "button")
                    for i, button in enumerate(buttons):
                        text = button.text.strip()
                        print(f"  按钮 {i}: '{text}'")
                    
                    # 打印页面中的所有输入框
                    print("\n页面中的输入框:")
                    inputs = driver.find_elements(By.TAG_NAME, "input")
                    for i, input_elem in enumerate(inputs):
                        input_id = input_elem.get_attribute("id")
                        input_type = input_elem.get_attribute("type")
                        input_value = input_elem.get_attribute("value")
                        print(f"  输入框 {i}: ID='{input_id}', Type='{input_type}', Value='{input_value}'")
                    
                    # 使用JavaScript设置日期，这样可以触发必要的事件
                    try:
                        # 设置开始日期
                        driver.execute_script(f"document.getElementById('start_date').value = '{start_str}';")
                        driver.execute_script(f"document.getElementById('start_date').dispatchEvent(new Event('change'));")
                        driver.execute_script(f"document.getElementById('start_date').dispatchEvent(new Event('blur'));")
                        print(f"\n使用JavaScript设置开始日期: {start_str}")
                        
                        # 设置结束日期
                        driver.execute_script(f"document.getElementById('end_date').value = '{end_str}';")
                        driver.execute_script(f"document.getElementById('end_date').dispatchEvent(new Event('change'));")
                        driver.execute_script(f"document.getElementById('end_date').dispatchEvent(new Event('blur'));")
                        print(f"使用JavaScript设置结束日期: {end_str}")
                        
                        # 验证日期是否设置成功
                        start_value = driver.execute_script("return document.getElementById('start_date').value;")
                        end_value = driver.execute_script("return document.getElementById('end_date').value;")
                        print(f"验证日期设置结果: 开始日期={start_value}, 结束日期={end_value}")
                        
                        # 确保日期设置正确
                        if start_value == start_str and end_value == end_str:
                            print("日期设置成功！")
                        else:
                            print("日期设置失败，尝试使用备用方法...")
                            # 备用方法：直接在URL中传递日期参数
                            date_url = f"{self.base_url}/jc/zqsgkj/?start_date={start_str}&end_date={end_str}"
                            print(f"使用备用URL: {date_url}")
                            driver.get(date_url)
                            # 等待页面加载完成
                            time.sleep(3)
                            WebDriverWait(driver, 10).until(
                                EC.presence_of_element_located((By.TAG_NAME, "table"))
                            )
                    except Exception as e:
                        print(f"设置日期失败: {e}")
                        # 尝试备用方法：直接在URL中传递日期参数
                        date_url = f"{self.base_url}/jc/zqsgkj/?start_date={start_str}&end_date={end_str}"
                        print(f"使用备用URL: {date_url}")
                        driver.get(date_url)
                        # 等待页面加载完成
                        time.sleep(3)
                        WebDriverWait(driver, 10).until(
                            EC.presence_of_element_located((By.TAG_NAME, "table"))
                        )
                    
                    # 查找"开始查询"按钮并点击
                    print("\n查找'开始查询'按钮...")
                    search_button = None
                    
                    # 方式1: 按class查找u-btn（这是正确的查询按钮）
                    try:
                        search_button = driver.find_element(By.CLASS_NAME, "u-btn")
                        print(f"找到查询按钮: class='u-btn', 文本='{search_button.text}'")
                    except:
                        pass
                    
                    # 点击查询按钮
                    if search_button:
                        print("点击查询按钮...")
                        search_button.click()
                        print("点击查询按钮成功！")
                    else:
                        print("未找到查询按钮，尝试使用JavaScript调用click_submit()函数...")
                        try:
                            driver.execute_script("click_submit();")
                            print("JavaScript调用click_submit()成功！")
                        except Exception as e:
                            print(f"JavaScript调用失败: {e}")
                    
                    # 等待页面加载完成，增加等待时间
                    print("等待页面加载完成...")
                    WebDriverWait(driver, 20).until(
                        EC.presence_of_element_located((By.TAG_NAME, "table"))
                    )
                    print("页面加载完成")
                    
                    # 等待查询结果更新，确保数据已加载
                    print("等待查询结果更新...")
                    time.sleep(3)  # 增加等待时间，确保查询结果完全加载
                    
                    # 检查查询结果数量
                    try:
                        match_count_elem = driver.find_element(By.ID, "matchCount")
                        match_count = match_count_elem.text
                        print(f"查询结果: {match_count} 场赛事符合条件")
                    except Exception as e:
                        print(f"无法获取查询结果数量: {e}")
                    
                    # 处理分页，获取所有页面的数据
                    all_page_data = []
                    page_num = 1
                    
                    while True:
                        print(f"\n处理第 {page_num} 页数据...")
                        
                        # 获取当前页面内容
                        content = driver.page_source
                        
                        # 解析当前页面数据
                        page_data = self.parse_page(content, data_type)
                        print(f"第 {page_num} 页获取到 {len(page_data)} 条数据")
                        
                        # 打印当前页面数据的日期范围
                        if page_data:
                            dates = [item.get('日期', '') for item in page_data]
                            unique_dates = list(set(dates))
                            print(f"第 {page_num} 页数据日期: {sorted(unique_dates)}")
                        
                        # 添加到总数据中
                        all_page_data.extend(page_data)
                        
                        # 检查是否有下一页
                        try:
                            # 查找所有页码链接
                            all_links = driver.find_elements(By.TAG_NAME, "a")
                            page_links = []
                            for link in all_links:
                                link_text = link.text.strip()
                                if link_text.isdigit() and int(link_text) > 0:
                                    page_links.append((int(link_text), link))
                            
                            # 按页码排序
                            page_links.sort()
                            
                            print(f"找到 {len(page_links)} 个页码: {[p[0] for p in page_links]}")
                            
                            # 查找下一页链接
                            next_page_link = None
                            for page_num_link, link in page_links:
                                if page_num_link == page_num + 1:
                                    next_page_link = link
                                    break
                            
                            if next_page_link:
                                print(f"点击第 {page_num + 1} 页链接")
                                next_page_link.click()
                                # 等待页面加载完成
                                time.sleep(3)
                                WebDriverWait(driver, 10).until(
                                    EC.presence_of_element_located((By.TAG_NAME, "table"))
                                )
                                page_num += 1
                            else:
                                print("未找到下一页链接，已到达最后一页")
                                break
                        except Exception as e:
                            print(f"处理分页失败: {e}")
                            break
                    
                    # 保存页面内容到文件，方便调试
                    with open("page_debug.html", "w", encoding="utf-8") as f:
                        f.write(content)
                    print("页面内容已保存到 page_debug.html 文件")
                    
                    # 处理所有页面的数据
                    batch_data = all_page_data
                    print(f"\n解析完成，总共获取到 {len(batch_data)} 条数据")
                    
                    # 打印数据的日期范围
                    if batch_data:
                        dates = [item.get('日期', '') for item in batch_data]
                        unique_dates = list(set(dates))
                        print(f"所有数据日期范围: {sorted(unique_dates)}")
                        # 打印前5条数据的详细信息
                        print("前5条数据:")
                        for i, item in enumerate(batch_data[:5]):
                            print(f"  {i+1}. 日期: {item.get('日期', '')}, 联赛: {item.get('联赛', '')}, 对阵: {item.get('对阵', '')}")
                    
                    success = len(batch_data) > 0
                except Exception as e:
                    print(f"模拟网页日期选择器失败: {e}")
                    # 保存页面内容到文件，方便调试
                    try:
                        content = driver.page_source
                        with open("page_error.html", "w", encoding="utf-8") as f:
                            f.write(content)
                        print("错误页面内容已保存到 page_error.html 文件")
                    except:
                        pass
                    # 回退到按天查询
                    print("回退到按天查询...")
                    batch_current = current_date
                    while batch_current <= batch_end:
                        day_str = batch_current.strftime("%Y-%m-%d")
                        print(f"抓取日期: {day_str}")
                        day_url = f"{self.base_url}/jc/zqsgkj/?date={day_str}"
                        day_content = self.get_page_content(day_url)
                        if day_content:
                            day_data = self.parse_page(day_content, data_type)
                            batch_data.extend(day_data)
                            print(f"  获取到 {len(day_data)} 条数据")
                            # 打印获取到的数据日期
                            if day_data:
                                print(f"  数据日期: {day_data[0].get('日期', '')}")
                        time.sleep(random.uniform(0.3, 0.5))
                        batch_current += timedelta(days=1)
                    success = len(batch_data) > 0
                
                # 立即将批次数据写入CSV文件
                if batch_data:
                    print(f"\n当前批次共获取到 {len(batch_data)} 条数据，立即写入文件...")
                    save_success = self.save_batch_data(batch_data, data_type)
                    if save_success:
                        print(f"批次数据保存成功，共写入 {len(batch_data)} 条记录")
                        all_data.extend(batch_data)
                    else:
                        print("批次数据保存失败")
                else:
                    print(f"\n当前批次未获取到数据")
                
                # 打印批次结果
                print(f"\n批次结果: {'成功' if success else '失败'}")
                print(f"获取数据记录数: {len(batch_data)}")
                print(f"写入文件记录数: {len(batch_data) if len(batch_data) > 0 else 0}")
                print("========================================")
                
                # 避免请求过快
                time.sleep(random.uniform(1.5, 2.5))
                current_date += timedelta(days=max_batch_days)
        finally:
            # 抓取完成后关闭浏览器驱动
            self.close_driver()
        
        return all_data
    
    def parse_page(self, content, data_type):
        """解析页面数据"""
        data = []
        soup = BeautifulSoup(content, "html.parser")
        
        # 查找所有表格
        tables = soup.find_all("table")
        
        # 找到包含比赛数据的表格（通常是第二个表格）
        target_table = None
        for table in tables:
            rows = table.find_all("tr")
            if len(rows) > 10:  # 比赛数据表格通常有很多行
                target_table = table
                break
        
        if not target_table:
            return data
        
        # 解析表格数据
        rows = target_table.find_all("tr")
        for row in rows[1:]:  # 跳过表头
            cells = row.find_all("td")
            if len(cells) >= 10:
                # 提取数据
                date = cells[0].text.strip() if len(cells) > 0 else ""
                match_id = cells[1].text.strip() if len(cells) > 1 else ""
                league = cells[2].text.strip() if len(cells) > 2 else ""
                match_info = cells[3].text.strip() if len(cells) > 3 else ""
                half_time_score = cells[4].text.strip() if len(cells) > 4 else ""
                full_time_score = cells[5].text.strip() if len(cells) > 5 else ""
                win_odds = cells[6].text.strip() if len(cells) > 6 else ""
                draw_odds = cells[7].text.strip() if len(cells) > 7 else ""
                lose_odds = cells[8].text.strip() if len(cells) > 8 else ""
                status = cells[9].text.strip() if len(cells) > 9 else ""
                
                # 根据不同类型构造数据
                if data_type in ["胜平负", "让球胜平负"]:
                    match_data = {
                        "日期": date,
                        "编号": match_id,
                        "联赛": league,
                        "对阵": match_info,
                        "半场比分": half_time_score,
                        "全场比分": full_time_score,
                        "胜赔率": win_odds,
                        "平赔率": draw_odds,
                        "负赔率": lose_odds,
                        "开奖结果": status
                    }
                elif data_type == "比分":
                    match_data = {
                        "日期": date,
                        "编号": match_id,
                        "联赛": league,
                        "对阵": match_info,
                        "半场比分": half_time_score,
                        "全场比分": full_time_score,
                        "赔率": win_odds,  # 比分玩法只有一个赔率
                        "开奖结果": full_time_score
                    }
                elif data_type == "总进球数":
                    # 计算总进球数
                    if full_time_score:
                        try:
                            home_goals, away_goals = map(int, full_time_score.split(":"))
                            total_goals = home_goals + away_goals
                        except:
                            total_goals = ""
                    else:
                        total_goals = ""
                    
                    match_data = {
                        "日期": date,
                        "编号": match_id,
                        "联赛": league,
                        "对阵": match_info,
                        "半场比分": half_time_score,
                        "全场比分": full_time_score,
                        "赔率": win_odds,  # 总进球数玩法只有一个赔率
                        "开奖结果": str(total_goals)
                    }
                elif data_type == "半全场":
                    match_data = {
                        "日期": date,
                        "编号": match_id,
                        "联赛": league,
                        "对阵": match_info,
                        "半场比分": half_time_score,
                        "全场比分": full_time_score,
                        "赔率": win_odds,  # 半全场玩法只有一个赔率
                        "开奖结果": f"{half_time_score}-{full_time_score}"
                    }
                else:
                    continue
                
                data.append(match_data)
        
        return data
    
    def save_batch_data(self, data, data_type):
        """保存批次数据到对应文件（追加模式）"""
        if not data:
            print(f"没有数据需要保存: {data_type}")
            return False
        
        # 生成文件名（使用绝对路径）
        filename = os.path.join(os.path.abspath(self.data_dir), f"{data_type}.csv")
        
        # 转换数据为DataFrame
        df = pd.DataFrame(data)
        
        # 检查文件是否存在
        file_exists = os.path.exists(filename)
        
        # 保存数据到CSV文件（追加模式），添加重试机制
        max_retries = 3
        for retry in range(max_retries):
            try:
                df.to_csv(filename, index=False, encoding="utf-8-sig", mode="a", header=not file_exists)
                print(f"批次数据保存成功: {filename}, 共 {len(data)} 条记录")
                return True
            except PermissionError as e:
                print(f"保存批次数据失败（权限错误）: {e}")
                print(f"尝试 {retry+1}/{max_retries}: 等待文件解锁...")
                time.sleep(2)  # 等待2秒后重试
            except Exception as e:
                print(f"保存批次数据失败: {e}")
                if retry < max_retries - 1:
                    print(f"尝试 {retry+1}/{max_retries}: 重试中...")
                    time.sleep(1)
                else:
                    return False
        return False
    
    def save_data(self, data, data_type):
        """保存数据到对应文件"""
        if not data:
            print(f"没有数据需要保存: {data_type}")
            return False
        
        # 生成文件名
        filename = os.path.join(self.data_dir, f"{data_type}.csv")
        
        # 转换数据为DataFrame
        df = pd.DataFrame(data)
        
        # 保存数据到CSV文件
        try:
            df.to_csv(filename, index=False, encoding="utf-8-sig")
            print(f"数据保存成功: {filename}, 共 {len(data)} 条记录")
            return True
        except Exception as e:
            print(f"保存数据失败: {e}")
            return False

class JczqDataApp:
    def __init__(self, root):
        self.root = root
        self.root.title("中国体育彩票竞彩足球历史数据抓取工具")
        self.root.geometry("800x600")
        
        # 创建抓取器实例
        self.crawler = JczqDataCrawler()
        
        # 创建主框架
        self.main_frame = ttk.Frame(root, padding="20")
        self.main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 创建标题
        self.title_label = ttk.Label(self.main_frame, text="竞彩足球历史数据抓取", font=("微软雅黑", 16, "bold"))
        self.title_label.pack(pady=20)
        
        # 创建日期选择框架
        self.date_frame = ttk.LabelFrame(self.main_frame, text="日期范围", padding="10")
        self.date_frame.pack(fill=tk.X, pady=10)
        
        # 开始日期
        ttk.Label(self.date_frame, text="开始日期:").grid(row=0, column=0, padx=10, pady=5, sticky=tk.W)
        self.start_date_var = tk.StringVar(value=(datetime.now() - timedelta(days=30)).strftime("%Y-%m-%d"))
        ttk.Entry(self.date_frame, textvariable=self.start_date_var, width=20).grid(row=0, column=1, padx=10, pady=5)
        
        # 结束日期
        ttk.Label(self.date_frame, text="结束日期:").grid(row=0, column=2, padx=10, pady=5, sticky=tk.W)
        self.end_date_var = tk.StringVar(value=datetime.now().strftime("%Y-%m-%d"))
        ttk.Entry(self.date_frame, textvariable=self.end_date_var, width=20).grid(row=0, column=3, padx=10, pady=5)
        
        # 创建数据类型选择框架
        self.type_frame = ttk.LabelFrame(self.main_frame, text="数据类型", padding="10")
        self.type_frame.pack(fill=tk.X, pady=10)
        
        # 数据类型选项
        self.data_types = ["胜平负", "让球胜平负", "比分", "总进球数", "半全场"]
        self.selected_types = []
        self.check_vars = []
        
        for i, data_type in enumerate(self.data_types):
            var = tk.BooleanVar(value=True)
            self.check_vars.append(var)
            ttk.Checkbutton(self.type_frame, text=data_type, variable=var).grid(row=0, column=i, padx=20, pady=5)
        
        # 创建抓取按钮
        self.button_frame = ttk.Frame(self.main_frame)
        self.button_frame.pack(pady=20)
        
        self.start_button = ttk.Button(self.button_frame, text="开始抓取", command=self.start_crawl)
        self.start_button.pack(padx=10, pady=10)
        
        # 创建进度显示
        self.progress_frame = ttk.LabelFrame(self.main_frame, text="抓取进度", padding="10")
        self.progress_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 进度条
        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(self.progress_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(fill=tk.X, pady=10)
        
        # 进度文本
        self.progress_text = tk.Text(self.progress_frame, height=15, width=80)
        self.progress_text.pack(fill=tk.BOTH, expand=True)
        self.progress_text.config(state=tk.DISABLED)
    
    def start_crawl(self):
        """开始抓取数据"""
        # 获取选中的数据类型
        self.selected_types = [self.data_types[i] for i, var in enumerate(self.check_vars) if var.get()]
        if not self.selected_types:
            messagebox.showerror("错误", "请至少选择一种数据类型")
            return
        
        # 获取日期范围
        try:
            start_date = datetime.strptime(self.start_date_var.get(), "%Y-%m-%d")
            end_date = datetime.strptime(self.end_date_var.get(), "%Y-%m-%d")
            if start_date > end_date:
                messagebox.showerror("错误", "开始日期不能晚于结束日期")
                return
        except ValueError:
            messagebox.showerror("错误", "日期格式错误，请使用YYYY-MM-DD格式")
            return
        
        # 禁用开始按钮
        self.start_button.config(state=tk.DISABLED)
        
        # 清空进度文本
        self.progress_text.config(state=tk.NORMAL)
        self.progress_text.delete(1.0, tk.END)
        self.progress_text.config(state=tk.DISABLED)
        
        # 启动抓取线程
        def crawl_thread():
            total_types = len(self.selected_types)
            for i, data_type in enumerate(self.selected_types):
                # 更新进度
                progress = (i / total_types) * 100
                self.progress_var.set(progress)
                
                # 显示进度
                self.log(f"开始抓取 {data_type} 数据...")
                
                # 抓取数据
                data = self.crawler.crawl_data(start_date, end_date, data_type)
                
                # 保存数据
                if data:
                    self.crawler.save_data(data, data_type)
                    self.log(f"{data_type} 数据抓取完成，共 {len(data)} 条记录")
                else:
                    self.log(f"{data_type} 数据抓取失败，未获取到数据")
            
            # 完成抓取
            self.progress_var.set(100)
            self.log("所有数据抓取完成！")
            self.start_button.config(state=tk.NORMAL)
            messagebox.showinfo("完成", "所有数据抓取完成！")
        
        # 启动线程
        thread = threading.Thread(target=crawl_thread)
        thread.daemon = True
        thread.start()
    
    def log(self, message):
        """记录日志"""
        self.progress_text.config(state=tk.NORMAL)
        self.progress_text.insert(tk.END, f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} - {message}\n")
        self.progress_text.see(tk.END)
        self.progress_text.config(state=tk.DISABLED)

def main():
    """主函数"""
    # 创建抓取器实例
    crawler = JczqDataCrawler()
    
    # 设置日期范围：2015年到今天
    start_date = datetime(2015, 1, 1)
    end_date = datetime.now()
    
    print(f"开始抓取从 {start_date.strftime('%Y-%m-%d')} 到 {end_date.strftime('%Y-%m-%d')} 的胜平负数据...")
    print("每半个月抓取一次数据")
    
    # 抓取胜平负数据
    data = crawler.crawl_data(start_date, end_date, "胜平负")
    
    # 保存数据
    if data:
        crawler.save_data(data, "胜平负")
        print(f"抓取完成！共获取 {len(data)} 条胜平负数据")
    else:
        print("抓取失败，未获取到数据")

if __name__ == "__main__":
    main()
