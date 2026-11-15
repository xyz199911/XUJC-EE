def sum_all(*args):
    total = 0
    for x in args:
        total += x
    return total

print("请输入若干整数（空格分隔）：")
nums = [int(x) for x in input().split()]
s = sum_all(*nums)
print("和 =", s)
print("共", len(nums), "个")
