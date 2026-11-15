print("continue跳过3： ", end="")
for i in range(1, 7):
    if i == 3:
        continue
    print(i, end=" ")
print()

print("break遇到5停止： ", end="")
for i in range(1, 7):
    if i == 5:
        break
    print(i, end=" ")
print()
