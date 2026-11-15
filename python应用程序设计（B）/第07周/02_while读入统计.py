count = 0
total = 0
while True:
    print("请输入整数（输入-1结束）：")
    x = int(input())
    if x == -1:
        break
    total += x
    count += 1
print("个数：", count)
print("总和：", total)
