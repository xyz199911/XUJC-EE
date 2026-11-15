total = 0
even_total = 0
for i in range(1, 201):
    total += i
    if i % 2 == 0:
        even_total += i
print("1到200的和：", total)
print("其中偶数的和：", even_total)
