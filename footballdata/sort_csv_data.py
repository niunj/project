#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
整理并排序胜平负.csv文件中的数据
按日期排序，同一天的数据按编号排序
"""

import pandas as pd
import os

def sort_csv_data():
    """整理并排序CSV数据"""
    print("=" * 60)
    print("开始整理并排序CSV数据")
    print("=" * 60)
    
    # 输入文件路径
    input_file = os.path.join("data", "胜平负.csv")
    output_file = os.path.join("data", "胜负平.csv")
    
    # 检查文件是否存在
    if not os.path.exists(input_file):
        print(f"错误：输入文件 {input_file} 不存在")
        return
    
    print(f"读取文件: {input_file}")
    
    # 读取CSV文件
    try:
        df = pd.read_csv(input_file, encoding="utf-8-sig")
        print(f"成功读取 {len(df)} 条数据")
    except Exception as e:
        print(f"读取文件失败: {e}")
        return
    
    # 检查必要的列是否存在
    required_columns = ["日期", "编号"]
    for col in required_columns:
        if col not in df.columns:
            print(f"错误：文件缺少 '{col}' 列")
            return
    
    # 转换日期列
    try:
        df["日期"] = pd.to_datetime(df["日期"], format="%Y-%m-%d")
        print("成功转换日期列")
    except Exception as e:
        print(f"转换日期列失败: {e}")
        # 尝试使用mixed格式
        try:
            df["日期"] = pd.to_datetime(df["日期"], format="mixed")
            print("成功使用mixed格式转换日期列")
        except Exception as e2:
            print(f"混合格式转换也失败: {e2}")
            return
    
    # 提取编号中的数字部分用于排序
    def extract_number(code):
        """从编号中提取数字"""
        try:
            # 提取数字部分
            import re
            match = re.search(r'\d+', str(code))
            if match:
                return int(match.group())
            return 0
        except:
            return 0
    
    df["编号数字"] = df["编号"].apply(extract_number)
    print("成功提取编号数字")
    
    # 排序：先按日期，再按编号数字
    df_sorted = df.sort_values(by=["日期", "编号数字"])
    print("成功排序数据")
    
    # 删除临时列
    df_sorted = df_sorted.drop(columns=["编号数字"])
    
    # 保存排序后的数据
    try:
        df_sorted.to_csv(output_file, index=False, encoding="utf-8-sig")
        print(f"成功保存排序后的数据到: {output_file}")
        print(f"排序后的数据量: {len(df_sorted)} 条")
    except Exception as e:
        print(f"保存文件失败: {e}")
        return
    
    # 显示排序结果的前10条数据
    print("\n排序结果前10条:")
    print(df_sorted.head(10)[["日期", "编号", "联赛", "对阵"]])
    
    print("\n" + "=" * 60)
    print("数据整理完成！")
    print("=" * 60)

if __name__ == "__main__":
    sort_csv_data()
