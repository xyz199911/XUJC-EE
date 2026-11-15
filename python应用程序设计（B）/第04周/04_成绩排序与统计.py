scores = list(map(int, input("请输入成绩（空格分隔）：").split()))
print()

s = sorted(scores)
n = len(s)

print("升序排序：", " ".join(map(str, s)))
print("最高分：", s[-1])
print("最低分：", s[0])
print("总分：", sum(scores))
print("平均分：", f"{sum(scores) / n:.2f}")
print("中位数：", f"{(s[(n - 1) // 2] + s[n // 2]) / 2:.2f}")
