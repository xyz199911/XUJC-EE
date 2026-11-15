num = int(input("请输入一个三位数："))
ge = num % 10
shi = num // 10 % 10
bai = num // 100
print(f"百位：{bai}，十位：{shi}，个位：{ge}")
